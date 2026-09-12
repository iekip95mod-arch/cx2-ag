import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { appendFileSync, readFileSync } from 'node:fs';

export function reviewProvider(current) {
  const labels = current.labels.map(label => label.name).filter(name => ['codex-review', 'claude-review'].includes(name));
  if (labels.length > 1) throw Error('Keep only the selected provider review label');
  return labels[0]?.replace('-review', '') ?? (current.head.ref.startsWith('codex/') ? 'codex' : 'claude');
}

export function trustedAuthor(current, repository, roster) {
  if (current.head.repo?.full_name !== repository) throw Error('Review requires a same-repository branch');
  const author = current.user;
  if (author?.type === 'User' && ['OWNER', 'MEMBER', 'COLLABORATOR'].includes(current.author_association)) return;
  if (author?.type === 'Bot' && roster.some(identity => identity.role === 'executor' && identity.login === author.login && Number.isSafeInteger(identity.appId) && identity.appId > 0 && /^[A-Za-z0-9_.-]+$/.test(identity.clientId ?? '') && Number.isSafeInteger(identity.userId) && identity.userId > 0 && identity.userId === author.id)) return;
  throw Error('PR author is not a trusted executor or collaborator');
}

async function assignedIdentity(read, repository, pr, provider, role, options) {
  const runtime = options.assignment ? null : await import('./bot-identities.mjs');
  const roster = options.roster ?? runtime.loadRoster();
  const assignment = options.assignment ?? runtime.readAssignment;
  const lease = await assignment({ repository, provider, role, pr }, async (method, endpoint, body) => {
    if (method === 'POST' && endpoint === 'graphql' && body?.query?.startsWith('query(')) return read(endpoint, false, body);
    if (method !== 'GET') throw Error('Approval lookup must be read-only');
    return read(endpoint);
  }, roster);
  const identity = lease;
  if (!identity || !roster.some(entry => entry.role === role && entry.provider === provider && entry.login === identity.login && entry.userId === identity.userId && Number.isSafeInteger(entry.userId) && entry.userId > 0)) throw Error('The lease does not identify a configured bot');
  return identity;
}

export async function trustedRequester(read, repository, event, actor, actorId, options = {}) {
  const current = event.pull_request;
  const sender = event.sender;
  if (current.head.repo?.full_name !== repository || sender?.login !== actor || sender?.id !== Number(actorId) || !Number.isSafeInteger(sender?.id) || sender.id < 1) throw Error('The review requester does not match the event actor');
  if (sender.type === 'User') return '';
  if (sender.type !== 'Bot') throw Error('Unknown review requester type');
  const identity = await assignedIdentity(read, repository, current.number, reviewProvider(current), 'executor', options);
  if (identity.login !== sender.login || identity.userId !== sender.id) throw Error('Only the assigned executor can request a bot review');
  return identity.login;
}

export async function trustedAssignment(read, repository, event, actor, actorId, options = {}) {
  if (event.action !== 'labeled' || !event.label?.name?.startsWith('reviewer:')) throw Error('A reviewer assignment label is required');
  const current = await read(`repos/${repository}/pulls/${event.pull_request.number}`);
  if (current.head.repo?.full_name !== repository || current.head.sha !== event.pull_request.head.sha || current.state !== 'open' || current.draft) throw Error('The assigned PR revision is no longer reviewable');
  const identity = await assignedIdentity(read, repository, event.pull_request.number, reviewProvider(current), 'reviewer', options);
  const sender = event.sender;
  if (sender?.type !== 'Bot' || sender.login !== identity.login || sender.id !== identity.userId || actor !== identity.login || Number(actorId) !== identity.userId) throw Error('Only the assigned reviewer App can deliver a review assignment');
  const label = `reviewer:${identity.slug}`;
  if (event.label.name !== label || !current.labels.some(entry => entry.name === label)) throw Error('The assignment label does not match the active reviewer');
  if (current.user?.login === identity.login || current.user?.id === identity.userId) throw Error('The PR author cannot review its own work');
  return identity;
}

export async function dispatchReview(api, repository, pr, sha, appSlug, options = {}) {
  const read = (endpoint, paginate, body) => api(body ? 'POST' : 'GET', endpoint, body);
  const current = await read(`repos/${repository}/pulls/${pr}`);
  if (current.head.repo?.full_name !== repository || current.head.sha !== sha || current.state !== 'open' || current.draft) throw Error('The review request is superseded, closed or draft');
  const identity = await assignedIdentity(read, repository, pr, reviewProvider(current), 'reviewer', options);
  if (identity.slug !== appSlug) throw Error('The routing token does not belong to the assigned reviewer');
  const label = `reviewer:${identity.slug}`;
  const endpoint = `repos/${repository}/labels/${encodeURIComponent(label)}`;
  if (!await api('GET', endpoint, undefined, true)) await api('POST', `repos/${repository}/labels`, { name: label, description: 'Assigned reviewer identity', color: '8250df' });
  if (current.labels.some(entry => entry.name === label)) await api('DELETE', `repos/${repository}/issues/${pr}/labels/${encodeURIComponent(label)}`);
  const latest = await read(`repos/${repository}/pulls/${pr}`);
  if (latest.head.sha !== sha || latest.state !== 'open' || latest.draft || reviewProvider(latest) !== identity.provider) throw Error('The PR changed before reviewer assignment');
  await api('POST', `repos/${repository}/issues/${pr}/labels`, { labels: [label] });
  return identity;
}

export async function approvalState(read, repository, pr, sha, options = {}) {
  const current = await read(`repos/${repository}/pulls/${pr}`);
  if (current.head.sha !== sha || current.state !== 'open' || current.draft) throw Error('PR is superseded, closed or draft');
  const provider = reviewProvider(current);
  if (options.provider && options.provider !== provider) throw Error('The selected reviewer changed');
  const identity = await assignedIdentity(read, repository, pr, provider, 'reviewer', options);
  if (current.user?.id === identity.userId || current.user?.login === identity.login) throw Error('The PR author cannot review its own work');
  const reviews = await read(`repos/${repository}/pulls/${pr}/reviews?per_page=100`, true);
  const before = options.before ?? [];
  const latestReview = reviews.flat().filter(review => review.commit_id === sha && review.user.login === identity.login && review.user.id === identity.userId && review.user.type === 'Bot' && !before.includes(review.id)).sort((a, b) => b.id - a.id)[0];
  if (latestReview?.state !== 'APPROVED') throw Error('The assigned reviewer no longer approves this PR revision');
  return 'approved';
}

export async function reviewState(read, repository, pr, sha, options = {}) {
  const prefix = `repos/${repository}`;
  const current = await read(`${prefix}/pulls/${pr}`);
  if (current.head.sha !== sha || current.state !== 'open' || current.draft) throw Error('PR is superseded, closed or draft');
  const pages = await read(`${prefix}/actions/workflows/agent-review.yml/runs?head_sha=${sha}&event=pull_request&per_page=100`, true);
  const runs = pages.flatMap(page => page.workflow_runs).filter(run => run.head_sha === sha && run.pull_requests.some(pull => pull.number === pr) && run.display_title === `Review PR #${pr} (requested)`).sort((a, b) => b.id - a.id);
  const requests = await read(`${prefix}/actions/workflows/agent-review-request.yml/runs?head_sha=${sha}&event=pull_request&per_page=100`, true);
  const assignments = requests.flatMap(page => page.workflow_runs).filter(run => run.head_sha === sha && run.pull_requests.some(pull => pull.number === pr) && run.display_title === `Assign reviewer for PR #${pr} (requested)`);
  if (assignments.some(run => run.status !== 'completed')) return 'pending';
  for (const run of assignments) {
    if (!Number.isSafeInteger(run.run_attempt) || run.run_attempt < 1 || !Number.isFinite(Date.parse(run.run_started_at))) throw Error('The assignment attempt cannot be identified');
  }
  const request = assignments.sort((a, b) => Date.parse(b.run_started_at) - Date.parse(a.run_started_at) || b.id - a.id)[0];
  if (request) {
    if (request.conclusion !== 'success') throw Error('The reviewer assignment failed');
    if (!runs.length) return 'pending';
    const reviewCreated = Date.parse(runs[0].created_at);
    if (!Number.isFinite(reviewCreated)) throw Error('The review cannot be correlated with the assignment attempt');
    if (reviewCreated <= Date.parse(request.run_started_at)) return 'pending';
  }
  if (!runs.length || runs.some(run => run.status !== 'completed')) return 'pending';
  const latest = runs[0];
  if (latest.conclusion !== 'success') throw Error('The current review workflow did not approve this PR');
  const jobs = await read(`${prefix}/actions/runs/${latest.id}/jobs?filter=latest&per_page=100`, true);
  const approval = jobs.flatMap(page => page.jobs).find(job => job.name === 'review-approved');
  if (approval?.status !== 'completed' || approval.conclusion !== 'success') throw Error('The review approval gate did not pass');
  return approvalState(read, repository, pr, sha, options);
}

export async function waitForReview(read, repository, pr, sha, sleep, attempts = 90, options = {}) {
  for (let attempt = 0; attempt < attempts; attempt++) {
    if (await reviewState(read, repository, pr, sha, options) === 'approved') return;
    if (attempt + 1 < attempts) await sleep(30000);
  }
  throw Error('Timed out waiting for reviewer approval. Request review, then rerun the failed CI jobs');
}

async function main() {
  if (process.argv.includes('--trust-author')) {
    const { loadRoster } = await import('./bot-identities.mjs');
    trustedAuthor(JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8')).pull_request, process.env.GITHUB_REPOSITORY, loadRoster());
    return;
  }
  if (process.env.GITHUB_EVENT_NAME !== 'pull_request') return;
  const repository = process.env.GITHUB_REPOSITORY;
  const pr = Number(process.env.PR_NUMBER);
  const sha = process.env.PR_HEAD_SHA;
  if (!/^[\w.-]+\/[\w.-]+$/.test(repository ?? '') || !Number.isSafeInteger(pr) || pr < 1 || !/^[a-f0-9]{40}$/.test(sha ?? '')) throw Error('Invalid PR review target');
  const execute = promisify(execFile);
  const read = async (endpoint, paginate = false, body) => {
    const pending = execute('gh', ['api', ...(paginate ? ['--paginate', '--slurp'] : []), endpoint, ...(body ? ['--method', 'POST', '--input', '-'] : [])], { timeout: 30000, maxBuffer: 8 * 1024 * 1024 });
    if (body) pending.child.stdin.end(JSON.stringify(body));
    const { stdout } = await pending;
    return JSON.parse(stdout);
  };
  if (process.argv.includes('--trust-assignment')) {
    const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
    const identity = await trustedAssignment(read, repository, event, process.env.GITHUB_ACTOR, process.env.GITHUB_ACTOR_ID);
    const outputs = { reviewer: identity.provider, requested: 'true', app_id: identity.appId, secret_name: identity.secretName, login: identity.login, user_id: identity.userId, allowed_bots: identity.login };
    appendFileSync(process.env.GITHUB_OUTPUT, Object.entries(outputs).map(([key, value]) => `${key}=${value}\n`).join(''));
    return;
  } else if (process.argv.includes('--dispatch-review')) {
    const api = async (method, endpoint, body, missing = false) => {
      const pending = execute('gh', ['api', '--method', method, endpoint, ...(body ? ['--input', '-'] : [])], { timeout: 30000, maxBuffer: 8 * 1024 * 1024 });
      if (body) pending.child.stdin.end(JSON.stringify(body));
      try {
        const { stdout } = await pending;
        return stdout.trim() ? JSON.parse(stdout) : null;
      } catch (error) {
        if (missing && /\(HTTP 404\)/.test(error.stderr ?? '')) return null;
        throw error;
      }
    };
    const identity = await dispatchReview(api, repository, pr, sha, process.env.APP_SLUG);
    appendFileSync(process.env.GITHUB_STEP_SUMMARY, `Assigned ${identity.login} through reviewer:${identity.slug}. GitHub does not retain these Apps in requested reviewers, so the assignment label starts the review workflow.\n`);
    return;
  } else if (process.argv.includes('--trust-requester')) {
    const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
    const allowed = await trustedRequester(read, repository, event, process.env.GITHUB_ACTOR, process.env.GITHUB_ACTOR_ID);
    appendFileSync(process.env.GITHUB_OUTPUT, `allowed_bots=${allowed}\n`);
    return;
  } else if (process.argv.includes('--approval-gate')) {
    if (process.env.SELECT_RESULT !== 'success') throw Error('Reviewer selection failed');
    const requested = process.env.REVIEW_REQUESTED;
    const provider = process.env.REVIEWER;
    const providerResult = provider === 'codex' ? process.env.CODEX_RESULT : provider === 'claude' ? process.env.CLAUDE_RESULT : null;
    if (!['true', 'false'].includes(requested) || (requested === 'true' && providerResult !== 'success')) throw Error('The requested reviewer did not succeed');
    await approvalState(read, repository, pr, sha, { provider, before: requested === 'true' ? JSON.parse(process.env.BEFORE) : [] });
  } else {
    await waitForReview(read, repository, pr, sha, ms => new Promise(resolve => setTimeout(resolve, ms)));
  }
  console.log('Reviewer approval passed for this PR revision');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main().catch(error => { console.error(error.message); process.exitCode = 1; });
