import { execFileSync } from 'node:child_process';
import { appendFileSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { findIdentity, loadRoster, readAssignment } from './bot-identities.mjs';

const repositoryName = 'iekip95mod-arch/cx2-ag';

function authoredBy(user, identity) {
  return user?.type === 'Bot' && user.login === identity.login && user.id === identity.userId;
}

function announcesLease(comment, identity, branch) {
  return authoredBy(comment.user, identity) && typeof comment.body === 'string' && comment.body.split('\n').includes(`Executor lease: ${branch}`);
}

async function participates(repository, number, identity, branch, api) {
  for (let page = 1; ; page++) {
    const comments = await api('GET', `repos/${repository}/issues/${number}/comments?per_page=100&page=${page}`);
    if (!Array.isArray(comments)) throw Error('Could not verify executor participation');
    if (comments.some(comment => announcesLease(comment, identity, branch))) return true;
    if (comments.length < 100) return false;
  }
}

function validateAssignees(ticket, identity, legacyOwner) {
  if (!Array.isArray(ticket.assignees) || ticket.assignees.some(user => user.login !== identity.login && user.login !== legacyOwner)) throw Error('Another identity owns the assignment target');
}

export async function assignExecutor({ event, eventName, repository, legacyOwner }, api, roster = loadRoster()) {
  const outcome = { assigned: [], unassigned: [], skipped: [] };
  if (repository !== repositoryName || event.repository?.full_name !== repository || !['issue_comment', 'pull_request_target'].includes(eventName)) return outcome;
  if ((eventName === 'issue_comment' && event.action !== 'created') || (eventName === 'pull_request_target' && !['opened', 'reopened'].includes(event.action))) return outcome;
  let identity;
  try { identity = findIdentity(roster, event.sender?.login, undefined, 'executor'); } catch { return outcome; }
  if (!authoredBy(event.sender, identity)) return outcome;
  if (legacyOwner !== undefined && legacyOwner !== repository.split('/')[0]) throw Error('Invalid legacy assignment owner');
  const number = event.issue?.number ?? event.pull_request?.number;
  if (!Number.isSafeInteger(number) || number <= 0) return outcome;
  const ticket = await api('GET', `repos/${repository}/issues/${number}`);
  if (ticket.number !== number || ticket.state !== 'open') return outcome;
  let pull;
  if (ticket.pull_request) {
    pull = await api('GET', `repos/${repository}/pulls/${number}`);
    if (pull.state !== 'open' || pull.head?.repo?.full_name !== repository) return outcome;
    if (pull.user.login !== identity.login && pull.user.login !== legacyOwner) return outcome;
    if (eventName === 'pull_request_target' && (event.pull_request.head?.ref !== pull.head.ref || event.pull_request.head?.sha !== pull.head.sha)) return outcome;
  } else if (eventName === 'pull_request_target') return outcome;
  const lease = await readAssignment({ repository, provider: identity.provider, role: 'executor', ...(pull ? { pr: number } : { issue: number }) }, api, roster);
  if (lease.login !== identity.login || lease.userId !== identity.userId || (pull && lease.branch !== pull.head.ref)) return outcome;
  if (eventName === 'issue_comment') {
    if (!Number.isSafeInteger(event.comment?.id) || event.comment.id <= 0) return outcome;
    const comment = await api('GET', `repos/${repository}/issues/comments/${event.comment.id}`, undefined, true);
    if (!comment || comment.issue_url !== `https://api.github.com/repos/${repository}/issues/${number}` || comment.body !== event.comment.body || comment.updated_at !== event.comment.updated_at || !announcesLease(comment, identity, lease.branch)) return outcome;
  } else if (!await participates(repository, number, identity, lease.branch, api)) return outcome;
  const numbers = pull ? [...new Set([lease.issue, number])] : [number];
  const targets = [];
  for (const target of numbers) {
    if (!Number.isSafeInteger(target) || target <= 0) throw Error('The executor lease must identify an issue');
    const current = target === number ? ticket : await api('GET', `repos/${repository}/issues/${target}`);
    if (current.state !== 'open') { outcome.skipped.push(target); continue; }
    if (target === lease.issue && current.pull_request) throw Error('The executor lease issue is a PR');
    validateAssignees(current, identity, legacyOwner);
    if (target !== number && !await participates(repository, target, identity, lease.branch, api)) { outcome.skipped.push(target); continue; }
    targets.push(current);
  }
  for (const current of targets) {
    let assigned = current;
    try {
      if (!current.assignees.some(user => user.login === identity.login)) assigned = await api('POST', `repos/${repository}/issues/${current.number}/assignees`, { assignees: [identity.login] });
      if (!assigned.assignees?.some(user => user.login === identity.login)) throw Error(`GitHub did not assign the executor to #${current.number}`);
      validateAssignees(assigned, identity, legacyOwner);
      if (assigned.assignees.some(user => user.login === legacyOwner)) assigned = await api('DELETE', `repos/${repository}/issues/${current.number}/assignees`, { assignees: [legacyOwner] });
      if (assigned.assignees?.length !== 1 || assigned.assignees[0].login !== identity.login) throw Error(`GitHub did not leave the executor as the sole assignee of #${current.number}`);
      outcome.assigned.push(current.number);
    } catch (error) {
      if (![403, 422].includes(error.status)) throw error;
      await api('POST', `repos/${repository}/issues/${current.number}/labels`, { labels: [`worker:${identity.slug}`] });
      outcome.unassigned.push({ number: current.number, reason: `GitHub refused native assignment (HTTP ${error.status}). The worker label records the lease but is not a native assignment.` });
    }
  }
  return outcome;
}

async function main() {
  if (!process.env.GH_TOKEN) throw Error('CODEX_GITHUB_TOKEN is required for administrative native assignment');
  const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
  const api = async (method, endpoint, body, missing = false) => {
    const args = ['api', '--method', method, endpoint];
    if (body) args.push('--input', '-');
    try {
      return JSON.parse(execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }));
    } catch (error) {
      const status = Number(/\(HTTP (\d+)\)/.exec(String(error.stderr))?.[1]);
      if (missing && status === 404) return null;
      throw Object.assign(Error(`Administrative assignment ${method} ${endpoint} failed`), { status });
    }
  };
  const outcome = await assignExecutor({ event, eventName: process.env.GITHUB_EVENT_NAME, repository: process.env.GITHUB_REPOSITORY, legacyOwner: process.env.LEGACY_OWNER }, api);
  for (const failure of outcome.unassigned) console.log(`::warning::#${failure.number}: ${failure.reason}`);
  if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, `Native executor assignment verified: ${outcome.assigned.map(number => `#${number}`).join(', ') || 'none'}.\n${outcome.unassigned.map(failure => `#${failure.number}: ${failure.reason}\n`).join('')}`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
