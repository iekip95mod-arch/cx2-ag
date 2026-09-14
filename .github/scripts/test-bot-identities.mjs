import assert from 'node:assert/strict';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync } from 'node:fs';
import { delimiter, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { allocateIdentity, assignedReviewProvider, findIdentity, loadRoster, readAssignment, resolveTarget, selectReviewProvider } from './bot-identities.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const roster = loadRoster().map((identity, index) => ({ ...identity, appId: index + 1, userId: index + 100, clientId: `Iv1.test${index}` }));
const options = (issue, provider = 'codex', role = 'executor') => ({ repository, provider, role, issue });

test('reviewer provider lookup uses only this PR active lease and rejects ambiguity', async () => {
  const f = fixture();
  const identity = roster.find(bot => bot.provider === 'codex' && bot.role === 'reviewer');
  const lease = { key: 'codex/reviewer/issue-42', provider: 'codex', role: 'reviewer', issue: 42, pr: 85, branch: 'feature/manual', slug: identity.slug, released: false };
  const target = { repository, pr: 85, branch: lease.branch };
  f.assignments = [lease];
  assert.equal(await assignedReviewProvider(target, f.api, roster), 'codex');
  for (const change of [{ released: true }, { pr: 86 }, { branch: 'feature/other' }]) {
    f.assignments = [{ ...lease, ...change }];
    assert.equal(await assignedReviewProvider(target, f.api, roster), undefined);
  }
  const other = roster.find(bot => bot.provider === 'claude' && bot.role === 'reviewer');
  f.assignments = [lease, { ...lease, provider: 'claude', key: 'claude/reviewer/issue-42', slug: other.slug }];
  await assert.rejects(assignedReviewProvider(target, f.api, roster), /Multiple active reviewers/);
  for (const selected of [identity, other, identity]) {
    await selectReviewProvider({ ...target, login: selected.login }, f.api, roster);
    assert.equal(await assignedReviewProvider(target, f.api, roster), selected.provider);
    assert.equal(f.assignments.filter(assignment => assignment.selectedReview).length, 1);
    assert.equal(f.assignments.filter(assignment => !assignment.released).length, 2);
  }
  f.assignments = [{ ...lease, pr: null, branch: 'codex/issue-42' }];
  assert.equal(await assignedReviewProvider({ ...target, branch: 'codex/issue-42' }, f.api, roster), 'codex');
});

function fixture() {
  const tickets = new Map();
  const pulls = new Map();
  const branches = new Set();
  const calls = [];
  let assignments = [];
  let revision = 0;
  let conflicts = 0;
  let collisionCount = 0;
  let userOverride;
  let links = [];
  let linkedOpenPrs = 0;
  function ticket(number) {
    if (!tickets.has(number)) tickets.set(number, { number, state: 'open', assignees: [], labels: [] });
    return tickets.get(number);
  }
  function pull(number, issue, provider = 'codex') {
    const pr = { number, state: 'open', user: { login: roster.find(identity => identity.provider === provider && identity.role === 'executor').login }, head: { ref: `${provider}/issue-${issue}`, repo: { full_name: repository } } };
    pulls.set(number, pr);
    tickets.set(number, { number, pull_request: {}, state: 'open' });
    return pr;
  }
  const api = async (method, endpoint, body) => {
    calls.push({ method, endpoint, body });
    const path = endpoint.replace(`repos/${repository}/`, '');
    if (method === 'GET' && path.startsWith('issues/')) return structuredClone(ticket(Number(path.slice(7))));
    if (method === 'GET' && /^pulls\/\d+$/.test(path)) return structuredClone(pulls.get(Number(path.slice(6))));
    if (method === 'GET' && path.startsWith('pulls?')) {
      const query = new URLSearchParams(path.slice(6));
      const branch = query.get('head').split(':')[1];
      const start = (Number(query.get('page')) - 1) * 100;
      return structuredClone([...pulls.values()].filter(pr => pr.head.ref === branch).slice(start, start + 100));
    }
    if (method === 'GET' && endpoint.startsWith('users/')) {
      const identity = roster.find(identity => identity.login === decodeURIComponent(endpoint.slice(6)));
      return userOverride ?? { type: 'Bot', id: identity.userId, login: identity.login };
    }
    if (method === 'GET' && path === 'contents/assignments.json?ref=bot-assignments') return revision ? { sha: String(revision), content: Buffer.from(JSON.stringify({ version: 1, assignments })).toString('base64') } : null;
    if (method === 'GET' && path.startsWith('git/ref/heads/')) return path === 'git/ref/heads/main' || branches.has(path.slice(14)) ? { object: { sha: 'base' } } : null;
    if (method === 'POST' && path === 'git/refs') {
      const branch = body.ref.slice(11);
      if (branches.has(branch)) throw Object.assign(Error('exists'), { status: 422 });
      branches.add(branch);
      return {};
    }
    if (method === 'POST' && endpoint === 'graphql') return body.query.includes('closedByPullRequestsReferences')
      ? { data: { repository: { issue: { closedByPullRequestsReferences: { totalCount: linkedOpenPrs, nodes: Array.from({ length: linkedOpenPrs }, () => ({ state: 'OPEN' })), pageInfo: { hasNextPage: false } } } } } }
      : { data: { repository: { pullRequest: { closingIssuesReferences: { nodes: links, pageInfo: { hasNextPage: false } } } } } };
    if (method === 'PUT' && path === 'contents/assignments.json') {
      if (conflicts-- > 0 || body.sha !== (revision ? String(revision) : undefined)) {
        collisionCount++;
        throw Object.assign(Error('conflict'), { status: 409 });
      }
      assignments = JSON.parse(Buffer.from(body.content, 'base64').toString('utf8')).assignments;
      revision++;
      return {};
    }
    throw Error(`Unexpected ${method} ${endpoint}`);
  };
  return { api, calls, ticket, pull, branches, get assignments() { return assignments; }, set assignments(value) { assignments = value; revision++; }, get revision() { return revision; }, get collisionCount() { return collisionCount; }, set conflicts(value) { conflicts = value; }, set user(value) { userOverride = value; }, set links(value) { links = value; }, set linkedOpenPrs(value) { linkedOpenPrs = value; } };
}

test('the catalogue contains twelve identities per provider and role', () => {
  assert.equal(roster.length, 48);
  for (const provider of ['codex', 'claude']) for (const role of ['executor', 'reviewer']) assert.equal(roster.filter(identity => identity.provider === provider && identity.role === role).length, 12);
  assert.equal(new Set(roster.map(identity => identity.login)).size, 48);
  assert.throws(() => findIdentity(roster, 'unknown[bot]'), /Unknown/);
  assert.throws(() => findIdentity(roster, roster[0].login, 'claude', 'executor'), /Unknown/);
  assert.throws(() => findIdentity([{ ...roster[0], appId: null }], roster[0].login), /unconfigured/);
  assert.throws(() => findIdentity([{ ...roster[0], userId: null }], roster[0].login), /unconfigured/);
});

test('concurrent issues reserve distinct slots with optimistic retry', async () => {
  const github = fixture();
  const workers = await Promise.all([allocateIdentity(options(20), github.api, roster), allocateIdentity(options(42), github.api, roster)]);
  assert.notEqual(workers[0].login, workers[1].login);
  assert.equal(github.assignments.filter(assignment => !assignment.released).length, 2);
  assert.ok(github.collisionCount >= 1);
  assert.equal(github.revision, 2);
});

test('concurrent retries on the same issue retain one identity', async () => {
  const github = fixture();
  const workers = await Promise.all([allocateIdentity(options(20), github.api, roster), allocateIdentity(options(20), github.api, roster)]);
  assert.equal(workers[0].login, workers[1].login);
  assert.equal(github.assignments.length, 1);
  assert.equal(github.revision, 1);
});

test('issue and PR aliases reuse the original identity without another write', async () => {
  const github = fixture();
  const worker = await allocateIdentity(options(20), github.api, roster);
  github.pull(87, 20);
  github.branches.add('codex/issue-20');
  for (const target of [{ repository, provider: 'codex', role: 'executor', pr: 87 }, options(87)]) {
    assert.equal((await allocateIdentity(target, github.api, roster)).login, worker.login);
    assert.equal((await readAssignment(target, github.api, roster)).issue, 20);
  }
  assert.equal(github.revision, 1);
});

test('twelve executor slots per provider retain ownership and reject overflow', async () => {
  for (const provider of ['codex', 'claude']) {
    const github = fixture();
    const workers = [];
    for (let issue = 1; issue <= 12; issue++) workers.push(await allocateIdentity({ ...options(issue), provider }, github.api, roster));
    assert.equal(new Set(workers.map(worker => worker.login)).size, 12);
    for (let issue = 1; issue <= 12; issue++) assert.equal((await allocateIdentity({ ...options(issue), provider }, github.api, roster)).login, workers[issue - 1].login);
    await assert.rejects(allocateIdentity({ ...options(13), provider }, github.api, roster), new RegExp(`All 12 ${provider} executor bots are occupied`));
    assert.equal(github.assignments.length, 12);
    assert.equal(github.revision, 12);
  }
});

test('twelve reviewer slots retain assignments and reject overflow per provider', async () => {
  for (const provider of ['codex', 'claude']) {
    const github = fixture();
    const reviewers = [];
    for (let issue = 1; issue <= 12; issue++) reviewers.push(await allocateIdentity(options(issue, provider, 'reviewer'), github.api, roster));
    assert.equal(new Set(reviewers.map(reviewer => reviewer.login)).size, 12);
    for (let issue = 1; issue <= 12; issue++) assert.equal((await allocateIdentity(options(issue, provider, 'reviewer'), github.api, roster)).login, reviewers[issue - 1].login);
    await assert.rejects(allocateIdentity(options(13, provider, 'reviewer'), github.api, roster), new RegExp(`All 12 ${provider} reviewer bots are occupied`));
    assert.equal(github.assignments.length, 12);
    assert.equal(github.revision, 12);
  }
});

test('a closed issue keeps its identity until every branch PR closes, including later pages', async () => {
  const github = fixture();
  const first = await allocateIdentity(options(20), github.api, roster);
  github.ticket(20).state = 'closed';
  for (let number = 1000; number <= 1100; number++) github.pull(number, 20).state = number === 1100 ? 'open' : 'closed';
  const second = await allocateIdentity(options(42), github.api, roster);
  assert.notEqual(second.login, first.login);
  assert.equal((await readAssignment(options(20), github.api, roster)).login, first.login);
  github.pull(1100, 20).state = 'closed';
  const third = await allocateIdentity(options(51), github.api, roster);
  assert.equal(third.login, first.login);
  assert.equal(github.assignments.find(assignment => assignment.issue === 20).released, true);
  await assert.rejects(readAssignment(options(20), github.api, roster), /No active/);
});

test('closed PRs alone do not release an open issue', async () => {
  const github = fixture();
  const first = await allocateIdentity(options(20), github.api, roster);
  github.pull(87, 20).state = 'closed';
  const second = await allocateIdentity(options(42), github.api, roster);
  assert.notEqual(first.login, second.login);
  assert.equal(github.assignments[0].released, false);
});

test('a merged linked PR releases the identity after its issue closes', async () => {
  const github = fixture();
  const first = await allocateIdentity(options(20), github.api, roster);
  github.ticket(20).state = 'closed';
  github.pull(87, 20).state = 'closed';
  const api = (method, endpoint, body) => method === 'POST' && endpoint === 'graphql' && body.query.includes('closedByPullRequestsReferences')
    ? { data: { repository: { issue: { closedByPullRequestsReferences: { totalCount: 1, nodes: [{ state: 'MERGED' }], pageInfo: { hasNextPage: false } } } } } }
    : github.api(method, endpoint, body);
  const second = await allocateIdentity(options(42), api, roster);
  assert.equal(second.login, first.login);
});

test('an open linked PR on another branch keeps the closed issue lease', async () => {
  const github = fixture();
  const first = await allocateIdentity(options(20), github.api, roster);
  github.ticket(20).state = 'closed';
  github.linkedOpenPrs = 1;
  const second = await allocateIdentity(options(42), github.api, roster);
  assert.notEqual(first.login, second.login);
  github.linkedOpenPrs = 0;
  const third = await allocateIdentity(options(51), github.api, roster);
  assert.equal(first.login, third.login);
});

test('an open linked PR on a second GraphQL page keeps the closed issue lease', async () => {
  const github = fixture();
  const first = await allocateIdentity(options(20), github.api, roster);
  github.ticket(20).state = 'closed';
  const cursors = [];
  const api = (method, endpoint, body) => {
    if (method !== 'POST' || endpoint !== 'graphql' || !body.query.includes('closedByPullRequestsReferences')) return github.api(method, endpoint, body);
    cursors.push(body.variables.cursor);
    const secondPage = body.variables.cursor === 'linked-page-2';
    return { data: { repository: { issue: { closedByPullRequestsReferences: {
      totalCount: 101,
      nodes: secondPage ? [{ state: 'OPEN' }] : Array.from({ length: 100 }, () => ({ state: 'MERGED' })),
      pageInfo: { hasNextPage: !secondPage, endCursor: secondPage ? null : 'linked-page-2' },
    } } } } };
  };
  const second = await allocateIdentity(options(42), api, roster);
  assert.notEqual(second.login, first.login);
  assert.deepEqual(cursors, [null, 'linked-page-2']);
  assert.equal((await readAssignment(options(20), github.api, roster)).login, first.login);
});

test('executor and reviewer pools are separate while provider ownership is exclusive', async () => {
  const github = fixture();
  const executor = await allocateIdentity(options(20), github.api, roster);
  const reviewer = await allocateIdentity(options(20, 'codex', 'reviewer'), github.api, roster);
  assert.notEqual(executor.login, reviewer.login);
  await assert.rejects(allocateIdentity(options(20, 'claude'), github.api, roster), /Another provider/);
  const claude = await allocateIdentity(options(42, 'claude'), github.api, roster);
  assert.match(claude.login, /claude-atlas/);
});

test('unknown or mismatched bot metadata never reserves a slot', async () => {
  const github = fixture();
  github.user = { type: 'User', id: roster[0].userId, login: roster[0].login };
  await assert.rejects(allocateIdentity(options(20), github.api, roster), /GitHub does not match/);
  github.user = { type: 'Bot', id: 99999, login: roster[0].login };
  await assert.rejects(allocateIdentity(options(20), github.api, roster), /GitHub does not match/);
  assert.equal(github.revision, 0);
  assert.equal(github.calls.filter(call => call.method === 'PUT' || call.method === 'POST').length, 0);
});

test('foreign ownership and unclaimed existing branches fail before reservation', async () => {
  for (const change of [github => github.ticket(20).assignees.push({ login: 'other' }), github => github.ticket(20).labels.push({ name: 'claude' }), github => github.branches.add('codex/issue-20'), github => { github.pull(87, 20).user.login = 'other'; }]) {
    const github = fixture();
    change(github);
    await assert.rejects(allocateIdentity(options(20), github.api, roster), /owns|unclaimed/);
    assert.equal(github.revision, 0);
  }
});

test('legacy ownership requires the explicit repository owner migration option', async () => {
  const github = fixture();
  github.ticket(20).assignees.push({ login: 'iekip95mod-arch' });
  github.branches.add('codex/issue-20');
  github.pull(87, 20).user.login = 'iekip95mod-arch';
  await assert.rejects(allocateIdentity(options(20), github.api, roster), /Another identity/);
  const worker = await allocateIdentity({ ...options(20), legacyOwner: 'iekip95mod-arch' }, github.api, roster);
  assert.equal(worker.issue, 20);
  assert.equal(github.revision, 1);
});

test('read-only resolution rejects forks, invalid input and cross-provider PRs', async () => {
  const github = fixture();
  github.pull(87, 20);
  await assert.rejects(resolveTarget({ ...options(20), repository: 'other/repo' }, github.api), /Invalid/);
  await assert.rejects(resolveTarget({ ...options(20), pr: 87 }, github.api), /Specify one/);
  await assert.rejects(resolveTarget(options('20\nextra'), github.api), /Specify one/);
  await assert.rejects(resolveTarget({ repository, provider: 'claude', role: 'executor', pr: 87 }, github.api), /another provider/);
  github.pull(87, 20).head.repo.full_name = 'other/repo';
  await assert.rejects(resolveTarget({ repository, provider: 'codex', role: 'executor', pr: 87 }, github.api), /fork/);
  assert.equal(github.calls.some(call => call.method !== 'GET'), false);
});

function legacyFixture() {
  const github = fixture();
  const pull = github.pull(91, 90);
  pull.head.ref = 'codex/named-bot-identities';
  pull.user.login = 'iekip95mod-arch';
  github.branches.add(pull.head.ref);
  github.links = [{ number: 90, repository: { nameWithOwner: repository } }];
  return { github, pull, target: { repository, provider: 'codex', role: 'executor', pr: 91, legacyOwner: 'iekip95mod-arch' } };
}

test('explicit legacy PR migration keeps its executor identity for issue and PR retries', async () => {
  const { github, target } = legacyFixture();
  const worker = await allocateIdentity(target, github.api, roster);
  assert.equal(worker.issue, 90);
  assert.equal(worker.branch, 'codex/named-bot-identities');
  assert.equal(worker.role, 'executor');
  for (const alias of [options(90), options(91), { ...target, legacyOwner: undefined }]) {
    const resolved = await resolveTarget(alias, github.api);
    const read = await readAssignment(alias, github.api, roster);
    const resumed = await allocateIdentity(alias, github.api, roster);
    assert.equal(resolved.branch, worker.branch);
    assert.equal(read.login, worker.login);
    assert.equal(resumed.login, worker.login);
  }
  assert.equal(github.revision, 1);
});

test('legacy executor migration rejects missing authority, foreign authors, branches and issue links', async () => {
  for (const change of [
    ({ target }) => { delete target.legacyOwner; },
    ({ target }) => { target.legacyOwner = 'someone-else'; },
    ({ pull }) => { pull.user.login = 'someone-else'; },
    ({ pull }) => { pull.head.ref = 'claude/setup'; },
    ({ pull }) => { pull.head.ref = 'unrelated/setup'; },
    ({ pull }) => { pull.head.repo.full_name = 'other/repo'; },
    ({ github }) => { github.links = []; },
    ({ github }) => { github.links = [{ number: 90, repository: { nameWithOwner: repository } }, { number: 92, repository: { nameWithOwner: repository } }]; },
    ({ github }) => { github.links = [{ number: 90, repository: { nameWithOwner: 'other/repo' } }]; },
  ]) {
    const setup = legacyFixture();
    change(setup);
    await assert.rejects(allocateIdentity(setup.target, setup.github.api, roster));
    assert.equal(setup.github.revision, 0);
    assert.equal(setup.github.calls.some(call => call.method === 'PUT'), false);
  }
});

test('legacy executor lookup revalidates the recorded PR branch, author and linked issue', async () => {
  for (const change of [
    ({ github }) => { github.links = [{ number: 92, repository: { nameWithOwner: repository } }]; },
    ({ pull }) => { pull.head.ref = 'codex/changed-branch'; },
    ({ pull }) => { pull.user.login = 'someone-else'; },
    ({ github }) => { github.assignments = github.assignments.map(record => ({ ...record, pr: null })); },
    ({ github }) => { github.assignments = github.assignments.map(record => ({ ...record, legacyOwner: 'someone-else' })); },
  ]) {
    const setup = legacyFixture();
    await allocateIdentity(setup.target, setup.github.api, roster);
    change(setup);
    await assert.rejects(readAssignment(options(90), setup.github.api, roster));
    await assert.rejects(allocateIdentity(options(90), setup.github.api, roster));
  }
});

test('noncanonical reviewer PRs use authoritative linked issues or a standalone PR lease', async () => {
  const github = fixture();
  github.pull(91, 90).head.ref = 'codex/named-bot-identities';
  const target = { repository, provider: 'codex', role: 'reviewer', pr: 91 };
  const standalone = await resolveTarget(target, github.api);
  assert.equal(standalone.key, 'codex/reviewer/pr-91');
  github.links = [{ number: 90, repository: { nameWithOwner: repository } }];
  const reviewer = await allocateIdentity(target, github.api, roster);
  assert.equal(reviewer.issue, 90);
  assert.equal((await readAssignment(target, github.api, roster)).login, reviewer.login);
  github.ticket(90).state = 'closed';
  assert.equal((await allocateIdentity(target, github.api, roster)).login, reviewer.login);
  github.links = [{ number: 90, repository: { nameWithOwner: 'other/repo' } }];
  await assert.rejects(resolveTarget(target, github.api), /another repository/);
});

test('closed standalone PR identity is reusable and contention is bounded', async () => {
  const github = fixture();
  github.pull(91, 90).head.ref = 'codex/setup';
  const first = await allocateIdentity({ repository, provider: 'codex', role: 'reviewer', pr: 91 }, github.api, roster);
  github.pull(91, 90).head.ref = 'codex/setup';
  github.pull(91, 90).state = 'closed';
  const second = await allocateIdentity(options(20, 'codex', 'reviewer'), github.api, roster);
  assert.equal(first.login, second.login);
  const contended = fixture();
  contended.conflicts = 10;
  await assert.rejects(allocateIdentity(options(20), contended.api, roster), /eight attempts/);
  assert.equal(contended.collisionCount, 8);
  assert.equal(contended.revision, 0);
});

test('assignment corruption cannot authorize an unknown identity or duplicate live slot', async () => {
  const github = fixture();
  await allocateIdentity(options(20), github.api, roster);
  github.assignments = [{ ...github.assignments[0], slug: 'attacker' }];
  await assert.rejects(readAssignment(options(20), github.api, roster), /Unknown/);
  const record = { ...github.assignments[0], slug: roster[0].slug };
  github.assignments = [record, { ...record, key: 'codex/executor/issue-42', issue: 42, branch: 'codex/issue-42' }];
  await assert.rejects(readAssignment(options(20), github.api, roster), /Conflicting/);
});

test('the CLI resolves an issue into workflow outputs without exposing credentials or mutating GitHub', () => {
  const scratch = new URL('../../.Internal/workspaces/bot-identity-tests/', import.meta.url);
  mkdirSync(scratch, { recursive: true });
  const directory = mkdtempSync(join(fileURLToPath(scratch), 'resolve-'));
  const gh = join(directory, 'gh');
  copyFileSync(new URL('./fixtures/bot-gh.mjs', import.meta.url), gh);
  chmodSync(gh, 0o700);
  const output = join(directory, 'outputs');
  const log = join(directory, 'requests');
  const env = { ...process.env, PATH: `${directory}${delimiter}${process.env.PATH}`, GH_TOKEN: 'fixture-secret-never-print', GITHUB_REPOSITORY: repository, GITHUB_OUTPUT: output, BOT_TEST_LOG: log };
  const cli = fileURLToPath(new URL('./bot-identities.mjs', import.meta.url));
  const run = spawnSync(process.execPath, [cli, 'resolve', '--provider', 'codex', '--role', 'executor', '--issue', '42'], { env, encoding: 'utf8' });
  assert.equal(run.status, 0, run.stderr);
  assert.equal(readFileSync(output, 'utf8'), 'branch=codex/issue-42\nissue=42\n');
  assert.deepEqual(readFileSync(log, 'utf8').trim().split('\n').map(line => JSON.parse(line)), [['api', '--method', 'GET', `repos/${repository}/issues/42`], ['api', '--method', 'GET', `repos/${repository}/contents/assignments.json?ref=bot-assignments`]]);
  assert.doesNotMatch(run.stdout + run.stderr, /fixture-secret/);
  const invalidOutput = join(directory, 'invalid-outputs');
  const invalid = spawnSync(process.execPath, [cli, 'allocate', '--provider', 'codex', '--role', 'executor', '--issue', '42'], { env: { ...env, GH_TOKEN: '', GITHUB_OUTPUT: invalidOutput }, encoding: 'utf8' });
  assert.equal(invalid.status, 1);
  assert.match(invalid.stderr, /trusted routing token/);
  assert.equal(existsSync(invalidOutput), false);
});
