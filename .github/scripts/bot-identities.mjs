import { execFileSync } from 'node:child_process';
import { appendFileSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const repositoryName = 'iekip95mod-arch/cx2-ag';
const assignmentBranch = 'bot-assignments';
const assignmentPath = 'assignments.json';

export function loadRoster(path = new URL('./bot-identities.json', import.meta.url)) {
  const roster = JSON.parse(readFileSync(path, 'utf8'));
  if (!Array.isArray(roster) || roster.length !== 36) throw Error('The bot roster must contain 36 identities');
  for (const field of ['slug', 'login', 'secretName']) {
    if (new Set(roster.map(identity => identity[field])).size !== roster.length) throw Error(`Duplicate bot ${field}`);
  }
  for (const provider of ['codex', 'claude']) for (const role of ['executor', 'reviewer']) {
    if (roster.filter(identity => identity.provider === provider && identity.role === role).length !== (role === 'executor' ? 12 : 6)) throw Error('Each provider must contain twelve executors and six reviewers');
  }
  for (const identity of roster) {
    const slug = `cx2-ag-${identity.provider}-${identity.role === 'reviewer' ? 'review-' : ''}${identity.name}`;
    if (!/^[a-z]+$/.test(identity.name) || identity.slug !== slug || identity.login !== `${slug}[bot]` || identity.secretName !== `${slug.replaceAll('-', '_').toUpperCase()}_PRIVATE_KEY`) throw Error('Invalid bot roster identity');
  }
  return roster;
}

export function findIdentity(roster, login, provider, role) {
  const identity = roster.find(candidate => candidate.login === login && (!provider || candidate.provider === provider) && (!role || candidate.role === role));
  if (!identity || !Number.isSafeInteger(identity.appId) || identity.appId <= 0 || !Number.isSafeInteger(identity.userId) || identity.userId <= 0 || typeof identity.clientId !== 'string' || !/^[A-Za-z0-9_.-]+$/.test(identity.clientId)) throw Error('Unknown or unconfigured bot identity');
  return identity;
}

export async function resolveTarget({ repository, provider, role, issue, pr, legacyOwner }, api) {
  if (repository !== repositoryName || !['codex', 'claude'].includes(provider) || !['executor', 'reviewer'].includes(role)) throw Error('Invalid bot repository or pool');
  if ((issue === undefined) === (pr === undefined) || !/^[1-9][0-9]*$/.test(String(issue ?? pr)) || !Number.isSafeInteger(Number(issue ?? pr))) throw Error('Specify one issue or PR number');
  if (issue !== undefined) {
    const ticket = await api('GET', `repos/${repository}/issues/${issue}`);
    if (ticket.pull_request) return resolveTarget({ repository, provider, role, pr: issue, legacyOwner }, api);
    const state = await readState(repository, api);
    const existing = state.assignments.find(assignment => assignment.key === `${provider}/${role}/issue-${issue}` && !assignment.released);
    if (existing && existing.branch !== `${provider}/issue-${issue}`) {
      const target = await resolveTarget({ repository, provider, role, pr: existing.pr }, api);
      if (target.key !== existing.key || target.branch !== existing.branch) throw Error('The assigned PR no longer matches this issue and branch');
      return target;
    }
    return { key: `${provider}/${role}/issue-${issue}`, provider, role, issue: Number(issue), pr: null, branch: `${provider}/issue-${issue}` };
  }
  const pull = await api('GET', `repos/${repository}/pulls/${pr}`);
  if (pull.head.repo?.full_name !== repository) throw Error('A bot cannot own a fork PR');
  const canonical = /^(codex|claude)\/issue-([1-9][0-9]*)$/.exec(pull.head.ref);
  if (canonical && canonical[1] !== provider) throw Error('The PR belongs to another provider');
  if (canonical) {
    const ticket = await api('GET', `repos/${repository}/issues/${canonical[2]}`);
    if (ticket.pull_request) throw Error('The canonical branch must refer to an issue, not a PR');
    return { key: `${provider}/${role}/issue-${canonical[2]}`, provider, role, issue: Number(canonical[2]), pr: Number(pr), branch: pull.head.ref };
  }
  if (role === 'executor' && !pull.head.ref.startsWith(`${provider}/`)) throw Error('The legacy executor branch belongs to another provider');
  const [owner, name] = repository.split('/');
  const linked = await api('POST', 'graphql', {
    query: 'query($owner:String!,$name:String!,$number:Int!){repository(owner:$owner,name:$name){pullRequest(number:$number){closingIssuesReferences(first:100){nodes{number repository{nameWithOwner}} pageInfo{hasNextPage}}}}}',
    variables: { owner, name, number: Number(pr) },
  });
  const references = linked.data?.repository?.pullRequest?.closingIssuesReferences;
  if (!references || references.pageInfo.hasNextPage || references.nodes.length > 1 || (role === 'executor' && references.nodes.length !== 1)) throw Error('A legacy executor PR must link exactly one issue, and a reviewer PR at most one');
  if (references.nodes.length) {
    const ticket = references.nodes[0];
    if (ticket.repository.nameWithOwner !== repository) throw Error('The linked issue belongs to another repository');
    if (role === 'executor') {
      const state = await readState(repository, api);
      const existing = state.assignments.find(assignment => assignment.key === `${provider}/${role}/issue-${ticket.number}` && !assignment.released);
      if (existing && (existing.pr !== Number(pr) || existing.branch !== pull.head.ref)) throw Error('The issue already has a different assigned PR or branch');
      const owner = existing?.legacyOwner ?? legacyOwner;
      if (owner !== repository.split('/')[0] || pull.user.login !== owner) throw Error('Legacy executor migration requires the explicit repository owner and matching PR author');
      return { key: `${provider}/${role}/issue-${ticket.number}`, provider, role, issue: ticket.number, pr: Number(pr), branch: pull.head.ref, legacyOwner: owner };
    }
    return { key: `${provider}/${role}/issue-${ticket.number}`, provider, role, issue: ticket.number, pr: Number(pr), branch: pull.head.ref };
  }
  return { key: `${provider}/${role}/pr-${pr}`, provider, role, issue: null, pr: Number(pr), branch: pull.head.ref };
}

async function readState(repository, api) {
  const file = await api('GET', `repos/${repository}/contents/${assignmentPath}?ref=${assignmentBranch}`, undefined, true);
  if (!file) return { sha: undefined, assignments: [] };
  const document = JSON.parse(Buffer.from(file.content, 'base64').toString('utf8'));
  if (document.version !== 1 || !Array.isArray(document.assignments)) throw Error('Invalid bot assignments document');
  const active = document.assignments.filter(assignment => !assignment.released);
  if (new Set(active.map(assignment => assignment.key)).size !== active.length || new Set(active.map(assignment => assignment.slug)).size !== active.length) throw Error('Conflicting active bot assignments');
  for (const assignment of document.assignments) {
    if (!['codex', 'claude'].includes(assignment.provider) || !['executor', 'reviewer'].includes(assignment.role) || typeof assignment.branch !== 'string' || typeof assignment.slug !== 'string' || typeof assignment.released !== 'boolean') throw Error('Invalid bot assignment');
    const number = assignment.issue ?? assignment.pr;
    if (!Number.isSafeInteger(number) || number <= 0 || assignment.key !== `${assignment.provider}/${assignment.role}/${assignment.issue ? 'issue' : 'pr'}-${number}`) throw Error('Invalid bot assignment target');
    if (assignment.role === 'executor' && assignment.branch !== `${assignment.provider}/issue-${assignment.issue}` && (!Number.isSafeInteger(assignment.pr) || assignment.pr <= 0 || !assignment.issue || !assignment.branch.startsWith(`${assignment.provider}/`) || assignment.legacyOwner !== repository.split('/')[0])) throw Error('Invalid legacy bot assignment branch');
  }
  return { sha: file.sha, assignments: document.assignments };
}

async function branchPulls(repository, branch, api) {
  const pulls = [];
  for (let page = 1; ; page++) {
    const batch = await api('GET', `repos/${repository}/pulls?state=all&head=${encodeURIComponent(`${repository.split('/')[0]}:${branch}`)}&per_page=100&page=${page}`);
    if (!Array.isArray(batch)) throw Error('Invalid branch PR response');
    pulls.push(...batch.filter(pull => pull.head.repo?.full_name === repository && pull.head.ref === branch));
    if (batch.length < 100) return pulls;
  }
}

async function closed(repository, assignment, api) {
  if (assignment.issue) {
    const ticket = await api('GET', `repos/${repository}/issues/${assignment.issue}`);
    if (ticket.state !== 'closed') return false;
    const [owner, name] = repository.split('/');
    let cursor = null;
    do {
      const linked = await api('POST', 'graphql', {
        query: 'query($owner:String!,$name:String!,$number:Int!,$cursor:String){repository(owner:$owner,name:$name){issue(number:$number){closedByPullRequestsReferences(first:100,after:$cursor,includeClosedPrs:true){totalCount nodes{state} pageInfo{hasNextPage endCursor}}}}}',
        variables: { owner, name, number: assignment.issue, cursor },
      });
      const connection = linked.data?.repository?.issue?.closedByPullRequestsReferences;
      if (!Number.isSafeInteger(connection?.totalCount) || connection.totalCount < 0) throw Error('Could not verify all linked PRs are closed');
      if (connection.totalCount === 0) break;
      if (!Array.isArray(connection.nodes) || !connection.nodes.length || !connection.nodes.every(pr => ['OPEN', 'CLOSED', 'MERGED'].includes(pr.state)) || typeof connection.pageInfo?.hasNextPage !== 'boolean') throw Error('Could not verify linked PR states');
      if (connection.nodes.some(pr => pr.state === 'OPEN')) return false;
      if (!connection.pageInfo.hasNextPage) break;
      if (!connection.pageInfo.endCursor || connection.pageInfo.endCursor === cursor) throw Error('Invalid linked PR cursor');
      cursor = connection.pageInfo.endCursor;
    } while (cursor);
  }
  if (assignment.pr) {
    const pull = await api('GET', `repos/${repository}/pulls/${assignment.pr}`);
    if (pull.state !== 'closed') return false;
  }
  return (await branchPulls(repository, assignment.branch, api)).every(pull => pull.state === 'closed');
}

async function verifyIdentity(identity, api) {
  const user = await api('GET', `users/${encodeURIComponent(identity.login)}`);
  if (user.type !== 'Bot' || user.id !== identity.userId || user.login !== identity.login) throw Error('GitHub does not match the configured bot identity');
}

async function verifyOwnership(repository, target, identity, legacyOwner, api, leased = false) {
  if (target.role !== 'executor') return;
  const ticket = await api('GET', `repos/${repository}/issues/${target.issue}`);
  const allowed = new Set([identity.login]);
  if ((target.legacyOwner ?? legacyOwner) === repository.split('/')[0]) allowed.add(repository.split('/')[0]);
  if (ticket.assignees.some(assignee => !allowed.has(assignee.login))) throw Error('Another identity owns the issue');
  if (ticket.labels.some(label => label.name === (target.provider === 'codex' ? 'claude' : 'codex'))) throw Error('Another provider owns the issue');
  const branch = await api('GET', `repos/${repository}/git/ref/heads/${target.branch}`, undefined, true);
  const pulls = await branchPulls(repository, target.branch, api);
  if (pulls.some(pull => !allowed.has(pull.user.login))) throw Error('Another identity owns a branch PR');
  if (branch && !leased && !target.legacyOwner && !ticket.assignees.some(assignee => allowed.has(assignee.login))) throw Error('An unclaimed branch already exists');
}

function reviewerMatches(state, pr, branch) {
  const canonical = /^(codex|claude)\/issue-([1-9][0-9]*)$/.exec(branch);
  return state.assignments.filter(assignment => !assignment.released && assignment.role === 'reviewer' && assignment.branch === branch &&
    (assignment.pr === pr || (!assignment.pr && canonical && assignment.issue === Number(canonical[2]))));
}

export async function selectReviewProvider({ repository, pr, branch, login }, api, roster = loadRoster()) {
  if (repository !== repositoryName || !Number.isSafeInteger(pr) || pr < 1 || typeof branch !== 'string' || !branch) throw Error('Invalid reviewer provider target');
  const identity = findIdentity(roster, login, undefined, 'reviewer');
  for (let attempt = 0; attempt < 8; attempt++) {
    const state = await readState(repository, api);
    const matches = reviewerMatches(state, pr, branch);
    if (!matches.some(assignment => assignment.slug === identity.slug)) throw Error('The reviewer has no active lease for this PR');
    if (matches.length === 1) return;
    for (const assignment of matches) assignment.selectedReview = assignment.slug === identity.slug;
    try {
      await api('PUT', `repos/${repository}/contents/${assignmentPath}`, { message: 'Record the selected reviewer', branch: assignmentBranch, sha: state.sha, content: Buffer.from(JSON.stringify({ version: 1, assignments: state.assignments }, null, 2) + '\n').toString('base64') });
      return;
    } catch (error) { if (error.status !== 409 || attempt === 7) throw error; }
  }
}

export async function assignedReviewProvider({ repository, pr, branch }, api, roster = loadRoster()) {
  if (repository !== repositoryName || !Number.isSafeInteger(pr) || pr < 1 || typeof branch !== 'string' || !branch) throw Error('Invalid reviewer provider target');
  const state = await readState(repository, api);
  let matches = reviewerMatches(state, pr, branch);
  if (matches.length > 1) {
    matches = matches.filter(assignment => assignment.selectedReview === true);
    if (matches.length !== 1) throw Error('Multiple active reviewers claim this PR');
  }
  if (!matches.length) return undefined;
  return findIdentity(roster, `${matches[0].slug}[bot]`, matches[0].provider, 'reviewer').provider;
}

export async function readAssignment(options, api, roster = loadRoster()) {
  const target = await resolveTarget(options, api);
  const state = await readState(options.repository, api);
  const assignment = state.assignments.find(candidate => candidate.key === target.key && !candidate.released);
  if (!assignment || assignment.branch !== target.branch) throw Error('No active bot assignment for this target');
  const identity = findIdentity(roster, `${assignment.slug}[bot]`, target.provider, target.role);
  return { ...identity, ...target };
}

export async function reviewerCapacity(repository, api, roster = loadRoster()) {
  const state = await readState(repository, api);
  const free = Object.fromEntries(['codex', 'claude'].map(provider => [provider, roster.filter(identity => identity.provider === provider && identity.role === 'reviewer').length]));
  for (const assignment of state.assignments.filter(assignment => !assignment.released && assignment.role === 'reviewer')) {
    findIdentity(roster, `${assignment.slug}[bot]`, assignment.provider, 'reviewer');
    if (!await closed(repository, assignment, api)) free[assignment.provider]--;
  }
  return free;
}

export async function allocateIdentity(options, api, roster = loadRoster()) {
  const target = await resolveTarget(options, api);
  if (await closed(options.repository, target, api)) throw Error('The issue and its PRs are already closed');
  for (let attempt = 0; attempt < 8; attempt++) {
    const state = await readState(options.repository, api);
    const existing = state.assignments.find(assignment => assignment.key === target.key && !assignment.released);
    if (existing) {
      if (existing.branch !== target.branch) throw Error('This issue already has a different assigned branch');
      const identity = findIdentity(roster, `${existing.slug}[bot]`, target.provider, target.role);
      await verifyIdentity(identity, api);
      await verifyOwnership(options.repository, target, identity, options.legacyOwner, api, true);
      return { ...identity, ...target };
    }
    for (const assignment of state.assignments.filter(assignment => !assignment.released)) {
      findIdentity(roster, `${assignment.slug}[bot]`, assignment.provider, assignment.role);
      if (await closed(options.repository, assignment, api)) assignment.released = true;
    }
    if (target.role === 'executor' && state.assignments.some(assignment => !assignment.released && assignment.role === 'executor' && assignment.issue === target.issue)) throw Error('Another provider already has this issue');
    const candidate = roster.find(identity => identity.provider === target.provider && identity.role === target.role && !state.assignments.some(assignment => !assignment.released && assignment.slug === identity.slug));
    if (!candidate) throw Object.assign(Error(`All ${roster.filter(identity => identity.provider === target.provider && identity.role === target.role).length} ${target.provider} ${target.role} bots are occupied`), { code: 'BOT_POOL_OCCUPIED' });
    const identity = findIdentity(roster, candidate.login, target.provider, target.role);
    await verifyIdentity(identity, api);
    await verifyOwnership(options.repository, target, identity, options.legacyOwner, api);
    state.assignments.push({ ...target, slug: identity.slug, released: false });
    const branch = await api('GET', `repos/${options.repository}/git/ref/heads/${assignmentBranch}`, undefined, true);
    if (!branch) {
      const main = await api('GET', `repos/${options.repository}/git/ref/heads/main`);
      try {
        await api('POST', `repos/${options.repository}/git/refs`, { ref: `refs/heads/${assignmentBranch}`, sha: main.object.sha });
      } catch (error) {
        if (error.status !== 422 || !await api('GET', `repos/${options.repository}/git/ref/heads/${assignmentBranch}`, undefined, true)) throw error;
      }
    }
    try {
      await api('PUT', `repos/${options.repository}/contents/${assignmentPath}`, {
        branch: assignmentBranch,
        message: 'Reserve repository worker identity',
        content: Buffer.from(JSON.stringify({ version: 1, assignments: state.assignments }, null, 2) + '\n').toString('base64'),
        ...(state.sha ? { sha: state.sha } : {}),
      });
      return { ...identity, ...target };
    } catch (error) {
      if (![409, 422].includes(error.status)) throw error;
    }
  }
  throw Error('Bot assignment contention exceeded eight attempts');
}

async function main() {
  if (!process.env.GH_TOKEN) throw Error('The trusted routing token is required');
  const [command, ...args] = process.argv.slice(2);
  if (!['allocate', 'resolve', 'lookup'].includes(command) || args.length % 2) throw Error('Use allocate, resolve or lookup with --provider, --role and --issue or --pr');
  const options = { repository: process.env.GITHUB_REPOSITORY, legacyOwner: process.env.LEGACY_OWNER };
  for (let index = 0; index < args.length; index += 2) {
    if (!['--provider', '--role', '--issue', '--pr'].includes(args[index]) || Object.hasOwn(options, args[index].slice(2))) throw Error('Invalid bot identity option');
    options[args[index].slice(2)] = args[index + 1];
  }
  const api = async (method, endpoint, body, missing = false) => {
    const args = ['api', '--method', method, endpoint];
    if (body) args.push('--input', '-');
    try {
      return JSON.parse(execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }));
    } catch (error) {
      const status = Number(/\(HTTP (\d+)\)/.exec(String(error.stderr))?.[1]);
      if (missing && status === 404) return null;
      throw Object.assign(Error(`GitHub ${method} ${endpoint} failed`), { status });
    }
  };
  let identity;
  try { identity = await (command === 'allocate' ? allocateIdentity(options, api) : command === 'lookup' ? readAssignment(options, api) : resolveTarget(options, api)); }
  catch (error) {
    if (command !== 'allocate' || options.role !== 'reviewer' || process.env.REVIEW_CAPACITY_WAIT !== 'true' || error.code !== 'BOT_POOL_OCCUPIED') throw error;
    appendFileSync(process.env.GITHUB_OUTPUT, 'waiting=true\n');
    return;
  }
  const outputs = { branch: identity.branch, issue: identity.issue ?? '' };
  if (command !== 'resolve') Object.assign(outputs, { app_id: identity.appId, client_id: identity.clientId, secret_name: identity.secretName, login: identity.login, user_id: identity.userId });
  appendFileSync(process.env.GITHUB_OUTPUT, Object.entries(outputs).map(([key, value]) => `${key}=${value}\n`).join(''));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
