import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { appendFileSync, readFileSync } from 'node:fs';
import { publishProgress } from './agent-progress.mjs';
import { assignedReviewProvider, findIdentity, loadRoster, selectReviewProvider } from './bot-identities.mjs';

export async function executeGhApi(execute, args, body) {
  const pending = execute('gh', args, { timeout: 30000, maxBuffer: 8 * 1024 * 1024 });
  if (body !== undefined) pending.child.stdin.end(JSON.stringify(body));
  const { stdout } = await pending;
  return stdout.trim() ? JSON.parse(stdout) : null;
}

const transientStatus = new Set([408, 429, 500, 502, 503, 504]);
// A gateway can lose a request it never applied, so only methods a repeat cannot duplicate retry.
const repeatableMethod = new Set(['GET', 'HEAD', 'PUT', 'DELETE']);

export async function requestGitHub(request, token, method, endpoint, body, missing = false, options = {}) {
  const attempts = repeatableMethod.has(method) ? options.attempts ?? 3 : 1;
  const sleep = options.sleep ?? (ms => new Promise(resolve => setTimeout(resolve, ms)));
  for (let attempt = 1; ; attempt++) {
    let response;
    try {
      response = await request(`https://api.github.com/${endpoint}`, {
        method,
        headers: { Accept: 'application/vnd.github+json', Authorization: `Bearer ${token}`, 'User-Agent': 'cx2-agent-progress', 'X-GitHub-Api-Version': '2022-11-28' },
        ...(body === undefined ? {} : { body: JSON.stringify(body) }),
        signal: AbortSignal.timeout(30000)
      });
    } catch (error) {
      if (attempt >= attempts) throw error;
      await sleep(500 * attempt);
      continue;
    }
    if (missing && response.status === 404) return null;
    if (!response.ok) {
      if (attempt >= attempts || !transientStatus.has(response.status)) throw Object.assign(Error(`GitHub API ${method} ${endpoint} failed with ${response.status}`), { status: response.status });
      await sleep(500 * attempt);
      continue;
    }
    const text = await response.text();
    return text ? JSON.parse(text) : null;
  }
}

export function reviewLabels(current) {
  return current.labels.map(label => label.name).filter(name => ['codex-review', 'claude-review', 'gemini-review'].includes(name));
}

export function reviewProvider(current) {
  const labels = reviewLabels(current);
  if (labels.length > 1) throw Error('Keep only the selected provider review label');
  return labels[0]?.replace('-review', '') ?? (/^(codex|claude|gemini)\//.exec(current.head.ref)?.[1] ?? 'claude');
}

async function selectedProvider(read, repository, pr, current, options = {}) {
  const fallback = reviewProvider(current);
  if (reviewLabels(current).length) return fallback;
  const lookup = options.providerLookup ?? assignedReviewProvider;
  const provider = await lookup({ repository, pr, branch: current.head.ref }, (method, endpoint, body) => read(endpoint, false, body), options.roster);
  return provider ?? fallback;
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
  const executorProvider = /^(codex|claude|gemini)\//.exec(current.head.ref)?.[1] ?? await selectedProvider(read, repository, current.number, current, options);
  const identity = await assignedIdentity(read, repository, current.number, executorProvider, 'executor', options);
  if (identity.login !== sender.login || identity.userId !== sender.id) throw Error('Only the assigned executor can request a bot review');
  return identity.login;
}

export async function trustedAssignment(read, repository, event, actor, actorId, options = {}) {
  if (event.action !== 'labeled' || !event.label?.name?.startsWith('reviewer:')) throw Error('A reviewer assignment label is required');
  const current = await read(`repos/${repository}/pulls/${event.pull_request.number}`);
  if (current.head.repo?.full_name !== repository || current.head.sha !== event.pull_request.head.sha || current.state !== 'open' || current.draft) throw Error('The assigned PR revision is no longer reviewable');
  const identity = await assignedIdentity(read, repository, event.pull_request.number, await selectedProvider(read, repository, event.pull_request.number, current, options), 'reviewer', options);
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
  const identity = await assignedIdentity(read, repository, pr, await selectedProvider(read, repository, pr, current, options), 'reviewer', options);
  if (identity.slug !== appSlug) throw Error('The routing token does not belong to the assigned reviewer');
  await (options.selectProvider ?? selectReviewProvider)({ repository, pr, branch: current.head.ref, login: identity.login }, api, options.roster);
  const label = `reviewer:${identity.slug}`;
  const endpoint = `repos/${repository}/labels/${encodeURIComponent(label)}`;
  if (!await api('GET', endpoint, undefined, true)) await api('POST', `repos/${repository}/labels`, { name: label, description: 'Assigned reviewer identity', color: '8250df' });
  if (current.labels.some(entry => entry.name === label)) await api('DELETE', `repos/${repository}/issues/${pr}/labels/${encodeURIComponent(label)}`);
  const latest = await read(`repos/${repository}/pulls/${pr}`);
  if (latest.head.sha !== sha || latest.state !== 'open' || latest.draft || await selectedProvider(read, repository, pr, latest, options) !== identity.provider) throw Error('The PR changed before reviewer assignment');
  await api('POST', `repos/${repository}/issues/${pr}/labels`, { labels: [label] });
  return identity;
}

export async function clearReviewLabels(api, repository, pr, sha, identity, runId, attempt, options = {}) {
  const endpoint = `repos/${repository}/issues/${pr}`;
  const current = await api('GET', `repos/${repository}/pulls/${pr}`);
  if (current.head.sha !== sha) return;
  const labels = [`${identity.provider}-review`, `reviewer:${identity.slug}`];
  if (!current.labels.some(label => labels.includes(label.name))) return;
  const run = await api('GET', `repos/${repository}/actions/runs/${runId}/attempts/${attempt}`);
  const started = Date.parse(run.run_started_at);
  if (!Number.isFinite(started)) throw Error('Review cleanup requires a known workflow start');
  const events = [];
  for (let page = 1; ; page++) {
    const batch = await api('GET', `${endpoint}/events?per_page=100&page=${page}`);
    events.push(...batch.filter(event => event.event === 'labeled' && labels.includes(event.label?.name)));
    if (batch.length < 100) break;
    if (page === 10) throw Error('Review label history exceeds the lookup limit');
  }
  if (events.some(event => !Number.isFinite(Date.parse(event.created_at)) || Date.parse(event.created_at) >= started)) return;
  const latest = await api('GET', `repos/${repository}/pulls/${pr}`);
  if (latest.head.sha !== sha) return;
  await (options.selectProvider ?? selectReviewProvider)({ repository, pr, branch: latest.head.ref, login: identity.login }, api, options.roster);
  for (const label of labels) {
    if (latest.labels.some(entry => entry.name === label)) await api('DELETE', `${endpoint}/labels/${encodeURIComponent(label)}`, undefined, true);
  }
}

export async function publishReviewNote(api, repository, pr, sha, appSlug, runId, attempt, mode, before, options = {}) {
  if (!/^[1-9][0-9]*$/.test(runId) || !/^[1-9][0-9]*$/.test(attempt) || !['queued', 'progress', 'publishing', 'disclosure', 'final', 'handoff'].includes(mode)) throw Error('Invalid review attempt');
  const { model, effort } = options;
  if (!/^[A-Za-z0-9._:[\]/-]+$/.test(model ?? '') || !/^[a-z]+$/.test(effort ?? '')) throw Error('Explicit review model and effort are required');
  const read = (endpoint, paginate, body) => api(body ? 'POST' : 'GET', endpoint, body);
  const current = await read(`repos/${repository}/pulls/${pr}`);
  if (current.head.repo?.full_name !== repository || (mode !== 'final' && (current.head.sha !== sha || current.state !== 'open' || current.draft))) throw Error('The review revision is no longer current');
  const identity = await assignedIdentity(read, repository, pr, await selectedProvider(read, repository, pr, current, options), 'reviewer', options);
  if (identity.slug !== appSlug) throw Error('The publishing App is not the assigned reviewer');
  if (mode !== 'disclosure') {
    const phase = mode === 'handoff' ? 'handed-off' : mode === 'queued' ? 'queued' : mode === 'progress' ? 'running' : mode === 'publishing' ? 'publishing' : options.phase;
    let cleanupError;
    if (mode === 'final') {
      try { await clearReviewLabels(api, repository, pr, sha, identity, runId, attempt, options); }
      catch (error) { cleanupError = error; }
    }
    const progress = await publishProgress({ repository, number: pr, role: 'reviewer', login: identity.login, userId: identity.userId, model, effort, run: runId, attempt, phase: cleanupError ? 'failed' : phase,
      detail: cleanupError ? `Review label cleanup failed for commit ${sha}. The formal verdict is recorded separately.` : mode === 'handoff' ? `Assignment delivered for commit ${sha}. Follow the separate review run in [PR checks](https://github.com/${repository}/pull/${pr}/checks). This assignment is complete, not the review.` : mode === 'final' ? `Review workflow finished for commit ${sha}. The formal verdict is recorded separately.` : `Reviewing commit ${sha}. The formal verdict will be recorded separately.`, updateOnly: mode === 'final' || mode === 'handoff' }, api);
    if (cleanupError) throw cleanupError;
    return progress;
  }
  const marker = `<!-- review-${mode}:${runId}:${attempt} -->`;
  const alias = identity.provider === 'claude' && ['opus', 'sonnet', 'haiku', 'fable', 'opusplan'].includes(model) ? ' (alias, resolved model unverified)' : '';
  const disclosure = `Configured model: ${model}${alias}\nConfigured reasoning effort: ${effort}\nWorkflow attempt: https://github.com/${repository}/actions/runs/${runId}/attempts/${attempt}`;
  const reviews = [];
  for (let page = 1; ; page++) {
    const batch = await api('GET', `repos/${repository}/pulls/${pr}/reviews?per_page=100&page=${page}`);
    reviews.push(...batch.filter(review => review.commit_id === sha && review.user?.type === 'Bot' && review.user.login === identity.login && review.user.id === identity.userId));
    if (batch.length < 100) break;
  }
  const existing = reviews.find(review => review.body?.includes(marker));
  if (existing) return existing;
  const verdict = reviews.filter(review => !before.includes(review.id) && (['APPROVED', 'CHANGES_REQUESTED'].includes(review.state) || (review.state === 'COMMENTED' && review.body?.includes('<!-- review-blocked -->')))).sort((a, b) => b.id - a.id)[0];
  if (!verdict) throw Error('No fresh formal verdict exists to disclose');
  return api('PUT', `repos/${repository}/pulls/${pr}/reviews/${verdict.id}`, { body: `${verdict.body}\n\n${disclosure}\n\n${marker}` });
}

export async function publishQueuedReview(api, repository, pr, sha, appSlug, login, userId, runId, attempt, options = {}) {
  const identity = findIdentity(options.roster ?? loadRoster(), login, undefined, 'reviewer');
  if (identity.slug !== appSlug || identity.userId !== userId) throw Error('Queued progress identity does not match the assigned reviewer');
  const current = await api('GET', `repos/${repository}/pulls/${pr}`);
  if (current.head.repo?.full_name !== repository || current.head.sha !== sha || current.state !== 'open' || current.draft || reviewProvider(current) !== identity.provider) throw Error('The review revision is no longer current');
  return publishProgress({ repository, number: pr, role: 'reviewer', login, userId, model: options.model, effort: options.effort, run: runId, attempt, phase: 'queued',
    detail: `Reviewing commit ${sha}. The formal verdict will be recorded separately.` }, api);
}

export async function approvalState(read, repository, pr, sha, options = {}) {
  const current = await read(`repos/${repository}/pulls/${pr}`);
  if (current.head.sha !== sha || current.state !== 'open' || current.draft) throw Error('PR is superseded, closed or draft');
  const provider = await selectedProvider(read, repository, pr, current, options);
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

export async function waitForReview(read, repository, pr, sha, sleep, attempts = 270, options = {}) {
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
    return executeGhApi(execute, ['api', ...(paginate ? ['--paginate', '--slurp'] : []), endpoint, ...(body ? ['--method', 'POST', '--input', '-'] : [])], body);
  };
  if (process.argv.includes('--trust-assignment')) {
    const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
    const identity = await trustedAssignment(read, repository, event, process.env.GITHUB_ACTOR, process.env.GITHUB_ACTOR_ID);
    const outputs = { reviewer: identity.provider, requested: 'true', app_id: identity.appId, secret_name: identity.secretName, login: identity.login, user_id: identity.userId, allowed_bots: identity.login };
    appendFileSync(process.env.GITHUB_OUTPUT, Object.entries(outputs).map(([key, value]) => `${key}=${value}\n`).join(''));
    return;
  } else if (['--dispatch-review', '--review-queued', '--review-progress', '--review-publishing', '--review-disclosure', '--review-final', '--review-handoff'].some(mode => process.argv.includes(mode))) {
    const api = (method, endpoint, body, missing = false) => requestGitHub(fetch, process.env.GH_TOKEN, method, endpoint, body, missing);
    if (!process.argv.includes('--dispatch-review')) {
      const mode = process.argv.includes('--review-handoff') ? 'handoff' : process.argv.includes('--review-queued') ? 'queued' : process.argv.includes('--review-progress') ? 'progress' : process.argv.includes('--review-publishing') ? 'publishing' : process.argv.includes('--review-final') ? 'final' : 'disclosure';
      if (mode === 'queued') {
        await publishQueuedReview(api, repository, pr, sha, process.env.APP_SLUG, process.env.REVIEW_LOGIN, Number(process.env.REVIEW_USER_ID), process.env.GITHUB_RUN_ID, process.env.GITHUB_RUN_ATTEMPT, { model: process.env.REVIEW_MODEL, effort: process.env.REVIEW_EFFORT });
        return;
      }
      await publishReviewNote(api, repository, pr, sha, process.env.APP_SLUG, process.env.GITHUB_RUN_ID, process.env.GITHUB_RUN_ATTEMPT, mode, JSON.parse(process.env.BEFORE ?? '[]'), { model: process.env.REVIEW_MODEL, effort: process.env.REVIEW_EFFORT, phase: process.env.PROGRESS_PHASE });
      return;
    }
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
    const providerResult = provider === 'codex' ? process.env.CODEX_RESULT : provider === 'claude' ? process.env.CLAUDE_RESULT : provider === 'gemini' ? process.env.GEMINI_RESULT : null;
    if (!['true', 'false'].includes(requested) || (requested === 'true' && providerResult !== 'success')) throw Error('The requested reviewer did not succeed');
    await approvalState(read, repository, pr, sha, { provider, before: requested === 'true' ? JSON.parse(process.env.BEFORE) : [] });
  } else {
    await waitForReview(read, repository, pr, sha, ms => new Promise(resolve => setTimeout(resolve, ms)));
  }
  console.log('Reviewer approval passed for this PR revision');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main().catch(error => { console.error(error.message); process.exitCode = 1; });
