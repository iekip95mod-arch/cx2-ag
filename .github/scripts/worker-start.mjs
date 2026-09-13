import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { readAssignment } from './bot-identities.mjs';

export async function announceWorker({ context, task = '', model, effort, run }, api) {
  const { repository, issue, branch, login, legacyOwner = '' } = context;
  if (repository !== 'iekip95mod-arch/cx2-ag' || !Number.isSafeInteger(issue) || issue < 1 || !['codex', 'claude'].some(provider => branch === `${provider}/issue-${issue}`) || !/^[a-z0-9-]+\[bot\]$/.test(login)) throw Error('Invalid executor announcement target');
  if (!/^[a-zA-Z0-9_.:\[\]-]+$/.test(model) || !['none', 'minimal', 'low', 'medium', 'high', 'xhigh', 'max'].includes(effort) || !/^[1-9][0-9]*$/.test(String(run))) throw Error('Configured model, effort and run are required');
  const ticket = await api('GET', `repos/${repository}/issues/${issue}`);
  if (ticket.state !== 'open' || ticket.pull_request) return false;
  let number = issue;
  let acknowledgement = 'Starting the assigned executor run.';
  const reviewTarget = /^Continue the existing issue lease and branch for PR #([1-9][0-9]*), review ([1-9][0-9]*), head ([a-f0-9]{40})\./.exec(task);
  if (reviewTarget) {
    const [, prNumber, reviewId, head] = reviewTarget;
    const pr = await api('GET', `repos/${repository}/pulls/${prNumber}`);
    const review = await api('GET', `repos/${repository}/pulls/${prNumber}/reviews/${reviewId}`);
    if (pr.state !== 'open' || pr.draft || pr.head?.repo?.full_name !== repository || pr.head.ref !== branch || pr.head.sha !== head || review.commit_id !== head || !['APPROVED', 'CHANGES_REQUESTED'].includes(review.state)) return false;
    if (pr.user.login !== login && !(legacyOwner === 'iekip95mod-arch' && pr.user.login === legacyOwner)) throw Error('The review PR belongs to another executor');
    let reviewer;
    try {
      reviewer = await readAssignment({ repository, provider: branch.split('/')[0], role: 'reviewer', issue }, api);
    } catch (error) {
      if (error.message === 'No active bot assignment for this target') return false;
      throw error;
    }
    if (reviewer.branch !== branch || reviewer.issue !== issue || review.id !== Number(reviewId) || review.user?.type !== 'Bot' || review.user.login !== reviewer.login || review.user.id !== reviewer.userId) return false;
    let latest;
    for (let page = 1; page <= 10; page++) {
      const reviews = await api('GET', `repos/${repository}/pulls/${prNumber}/reviews?per_page=100&page=${page}`);
      if (!Array.isArray(reviews)) throw Error('Invalid PR review history');
      for (const candidate of reviews) {
        if (candidate.commit_id !== head || candidate.user?.login !== reviewer.login || candidate.user.id !== reviewer.userId || candidate.user.type !== 'Bot' || !['APPROVED', 'CHANGES_REQUESTED'].includes(candidate.state)) continue;
        const submitted = Date.parse(candidate.submitted_at);
        if (!Number.isFinite(submitted) || !Number.isSafeInteger(candidate.id)) throw Error('Invalid formal review submission');
        if (!latest || submitted > latest.submitted || (submitted === latest.submitted && candidate.id > latest.id)) latest = { id: candidate.id, submitted, state: candidate.state };
      }
      if (reviews.length < 100) break;
      if (page === 10) throw Error('PR review history exceeds the acknowledgement lookup limit');
    }
    if (latest?.id !== review.id || latest.state !== review.state) return false;
    number = Number(prNumber);
    const link = `[review #${reviewId}](https://github.com/${repository}/pull/${prNumber}#pullrequestreview-${reviewId})`;
    acknowledgement = review.state === 'CHANGES_REQUESTED' ? `Beginning to address ${link} on head ${head}.` : `Beginning the merge checks following ${link} on head ${head}.`;
  }
  const description = /^(opus|sonnet|haiku|opusplan|default)(\[1m\])?$/.test(model) ? `${model} (configured alias, resolved model unverified)` : model;
  const body = `${acknowledgement}\n\nConfigured model: ${description}\nConfigured effort: ${effort}\nExecutor: ${login}\nRun: https://github.com/${repository}/actions/runs/${run}`;
  await api('POST', `repos/${repository}/issues/${number}/comments`, { body });
  return true;
}

async function main() {
  if (!process.env.GH_TOKEN) throw Error('The leased executor token is required');
  const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
  const api = async (method, endpoint, body, missing = false) => {
    const args = ['api', '--method', method, endpoint];
    if (body) args.push('--input', '-');
    try {
      return JSON.parse(execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }));
    } catch (error) {
      if (missing && String(error.stderr).includes('(HTTP 404)')) return null;
      throw Error(`Executor announcement ${method} ${endpoint} failed`);
    }
  };
  await announceWorker({ context: JSON.parse(readFileSync(process.argv[2], 'utf8')), task: event.inputs?.task ?? event.client_payload?.task ?? '', model: process.env.WORKER_MODEL, effort: process.env.WORKER_EFFORT, run: process.env.GITHUB_RUN_ID }, api);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
