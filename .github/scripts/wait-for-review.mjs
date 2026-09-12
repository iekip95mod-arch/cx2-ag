import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { readFileSync } from 'node:fs';

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

async function assignedReviewer(read, repository, pr, provider, options) {
  const runtime = options.assignment ? null : await import('./bot-identities.mjs');
  const roster = options.roster ?? runtime.loadRoster();
  const assignment = options.assignment ?? runtime.readAssignment;
  const lease = await assignment({ repository, provider, role: 'reviewer', pr }, async (method, endpoint, body) => {
    if (method === 'POST' && endpoint === 'graphql' && body?.query?.startsWith('query(')) return read(endpoint, false, body);
    if (method !== 'GET') throw Error('Approval lookup must be read-only');
    return read(endpoint);
  }, roster);
  const identity = lease;
  if (!identity || !roster.some(entry => entry.role === 'reviewer' && entry.provider === provider && entry.login === identity.login && entry.userId === identity.userId && Number.isSafeInteger(entry.userId) && entry.userId > 0)) throw Error('Reviewer lease does not identify a configured reviewer');
  return identity;
}

export async function approvalState(read, repository, pr, sha, options = {}) {
  const current = await read(`repos/${repository}/pulls/${pr}`);
  if (current.head.sha !== sha || current.state !== 'open' || current.draft) throw Error('PR is superseded, closed or draft');
  const provider = reviewProvider(current);
  if (options.provider && options.provider !== provider) throw Error('The selected reviewer changed');
  const identity = await assignedReviewer(read, repository, pr, provider, options);
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
  if (process.argv.includes('--approval-gate')) {
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
