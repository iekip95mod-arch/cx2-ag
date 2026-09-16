import assert from 'node:assert/strict';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync } from 'node:fs';
import { delimiter, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { allocateIdentity, assignedReviewProvider, findIdentity, loadRoster, readAssignment, releaseDeadLeases, releaseIdentity, resolveTarget, reviewerCapacity, selectReviewProvider } from './bot-identities.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const roster = loadRoster().map((identity, index) => ({ ...identity, appId: index + 1, userId: index + 100, clientId: `Iv1.test${index}` }));
const options = (issue, provider = 'codex', role = 'executor') => ({ repository, provider, role, issue });

test('Claude can review a Gemini issue without changing its executor lease', async () => {
  const github = fixture();
  github.pull(250, 239, 'gemini');
  const worker = await allocateIdentity(options(239, 'gemini'), github.api, roster);
  const reviewer = await allocateIdentity({ repository, provider: 'claude', role: 'reviewer', pr: 250 }, github.api, roster);
  assert.equal(reviewer.branch, 'gemini/issue-239');
  assert.equal(reviewer.issue, 239);
  assert.equal((await readAssignment(options(239, 'gemini'), github.api, roster)).login, worker.login);
  assert.equal((await readAssignment({ repository, provider: 'claude', role: 'reviewer', pr: 250 }, github.api, roster)).login, reviewer.login);
  await assert.rejects(resolveTarget({ repository, provider: 'claude', role: 'executor', pr: 250 }, github.api), /another provider/);
});

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

// The reviewer key is the branch, so a second pull request there is meant to keep the same bot. The
// record kept naming the first one, and reviewerMatches makes that mismatch fatal, so the reviewer
// the allocator had just handed out was refused by the job it was handed to.
test('a reviewer lease reused for the next PR on its branch names that PR', async () => {
  const github = fixture();
  const first = github.pull(301, 55, 'claude');
  const before = await allocateIdentity({ repository, provider: 'claude', role: 'reviewer', pr: 301 }, github.api, roster);
  first.state = 'closed';
  github.pull(302, 55, 'claude');
  const after = await allocateIdentity({ repository, provider: 'claude', role: 'reviewer', pr: 302 }, github.api, roster);
  assert.equal(after.login, before.login, 'the branch keeps its reviewer across the two pull requests');
  assert.deepEqual(github.assignments.filter(assignment => !assignment.released).map(assignment => assignment.pr), [302]);
  await selectReviewProvider({ repository, pr: 302, branch: 'claude/issue-55', login: after.login }, github.api, roster);
  assert.equal(await assignedReviewProvider({ repository, pr: 302, branch: 'claude/issue-55' }, github.api, roster), 'claude');
});

test('a reviewer lease is not taken from a pull request that is still open', async () => {
  const github = fixture();
  github.pull(311, 56, 'claude');
  await allocateIdentity({ repository, provider: 'claude', role: 'reviewer', pr: 311 }, github.api, roster);
  github.pull(312, 56, 'claude');
  await assert.rejects(allocateIdentity({ repository, provider: 'claude', role: 'reviewer', pr: 312 }, github.api, roster), /still serves an open pull request/);
  assert.deepEqual(github.assignments.filter(assignment => !assignment.released).map(assignment => assignment.pr), [311]);
});

function fixture() {
  const tickets = new Map();
  const pulls = new Map();
  const branches = new Set();
  const calls = [];
  let assignments = [];
  let revision = 0;
  let conflicts = 0;
  let hardFailures = 0;
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
      if (hardFailures-- > 0) throw Object.assign(Error('assignments.json write failed'), { status: 500 });
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
  return { api, calls, ticket, pull, branches, get assignments() { return assignments; }, set assignments(value) { assignments = value; revision++; }, get revision() { return revision; }, get collisionCount() { return collisionCount; }, set conflicts(value) { conflicts = value; }, set hardFailures(value) { hardFailures = value; }, set user(value) { userOverride = value; }, set links(value) { links = value; }, set linkedOpenPrs(value) { linkedOpenPrs = value; } };
}

// A lease is held for the whole life of an open issue, so an issue that stalls without ever opening a
// PR keeps one of the twelve until somebody intervenes. These two rows are that intervention and its
// one refusal: a branch with an open PR still has a writer on it.
test('releasing a stalled lease returns its identity to the pool', async () => {
  const github = fixture();
  for (let issue = 1; issue <= 12; issue++) await allocateIdentity(options(issue, 'claude'), github.api, roster);
  await assert.rejects(allocateIdentity(options(20, 'claude'), github.api, roster), /All 12 claude executor bots are occupied/);
  github.branches.add('claude/issue-7');
  const freed = await releaseIdentity(options(7, 'claude'), github.api, roster);
  assert.equal(freed.stranded, true);
  assert.equal(github.assignments.filter(assignment => !assignment.released).length, 11);
  assert.equal((await allocateIdentity(options(20, 'claude'), github.api, roster)).login, freed.login);
  await assert.rejects(releaseIdentity(options(7, 'claude'), github.api, roster), /No active bot assignment/);
});

test('releasing refuses while an open PR still holds the branch', async () => {
  const github = fixture();
  await allocateIdentity(options(42, 'claude'), github.api, roster);
  github.pull(96, 42, 'claude');
  await assert.rejects(releaseIdentity(options(42, 'claude'), github.api, roster), /Pull request 96 is still open/);
  assert.equal(github.assignments.filter(assignment => !assignment.released).length, 1);
  github.pull(96, 42, 'claude').state = 'closed';
  const freed = await releaseIdentity(options(42, 'claude'), github.api, roster);
  assert.equal(freed.issue, 42);
  assert.equal(freed.stranded, false);
  assert.equal(github.assignments.filter(assignment => !assignment.released).length, 0);
});

test('the catalogue contains twelve identities per provider and role', () => {
  assert.equal(roster.length, 72);
  for (const provider of ['codex', 'claude', 'gemini']) for (const role of ['executor', 'reviewer']) assert.equal(roster.filter(identity => identity.provider === provider && identity.role === role).length, 12);
  assert.equal(new Set(roster.map(identity => identity.login)).size, 72);
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
  for (const provider of ['codex', 'claude', 'gemini']) {
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

test('reviewer capacity retains open issue leases and reports pool exhaustion distinctly', async () => {
  const github = fixture();
  const count = roster.filter(identity => identity.provider === 'codex' && identity.role === 'reviewer').length;
  for (let issue = 1; issue <= count; issue++) await allocateIdentity(options(issue, 'codex', 'reviewer'), github.api, roster);
  assert.equal((await reviewerCapacity(repository, github.api, roster)).codex, 0);
  await assert.rejects(allocateIdentity(options(count + 1, 'codex', 'reviewer'), github.api, roster), { code: 'BOT_POOL_OCCUPIED' });
  github.ticket(1).state = 'closed';
  assert.equal((await reviewerCapacity(repository, github.api, roster)).codex, 1);
  const active = github.pull(50, 1, 'codex');
  active.state = 'open';
  assert.equal((await reviewerCapacity(repository, github.api, roster)).codex, 0);
});

test('a reviewer pool filled entirely with dead leases still assigns', async () => {
  const github = fixture();
  const target = pr => ({ repository, provider: 'claude', role: 'reviewer', pr });
  for (let issue = 1; issue <= 13; issue++) github.pull(200 + issue, issue, 'claude');
  for (let issue = 1; issue <= 12; issue++) await allocateIdentity(target(200 + issue), github.api, roster);
  for (let issue = 1; issue <= 12; issue++) github.pull(200 + issue, issue, 'claude').state = 'closed';
  const reviewer = await allocateIdentity(target(213), github.api, roster);
  assert.equal(reviewer.pr, 213);
  assert.equal(github.assignments.filter(assignment => !assignment.released).length, 1);
});

test('a refused allocation records the dead leases it verified', async () => {
  const github = fixture();
  for (let issue = 1; issue <= 12; issue++) await allocateIdentity(options(issue, 'claude', 'reviewer'), github.api, roster);
  const dead = await allocateIdentity(options(20, 'codex', 'reviewer'), github.api, roster);
  github.ticket(20).state = 'closed';
  const revision = github.revision;
  await assert.rejects(allocateIdentity(options(13, 'claude', 'reviewer'), github.api, roster), { code: 'BOT_POOL_OCCUPIED' });
  assert.equal(github.assignments.find(assignment => assignment.slug === dead.slug).released, true);
  assert.equal(github.assignments.filter(assignment => !assignment.released).length, 12);
  assert.equal(github.revision, revision + 1);
});

// The pool being full is one refusal out of several, and every one of them walks the whole document
// and asks GitHub about each live lease first. These three are the other exits.
test('an executor refused to a rival provider still records the sweep', async () => {
  const github = fixture();
  await allocateIdentity(options(5, 'codex'), github.api, roster);
  const dead = await allocateIdentity(options(20, 'claude', 'reviewer'), github.api, roster);
  github.ticket(20).state = 'closed';
  const revision = github.revision;
  await assert.rejects(allocateIdentity(options(5, 'claude'), github.api, roster), /Another provider already has this issue/);
  assert.equal(github.assignments.find(assignment => assignment.slug === dead.slug).released, true);
  assert.equal(github.revision, revision + 1);
});

test('an ownership refusal still records the sweep', async () => {
  const github = fixture();
  const dead = await allocateIdentity(options(21, 'claude', 'reviewer'), github.api, roster);
  github.ticket(21).state = 'closed';
  github.branches.add('claude/issue-8');
  const revision = github.revision;
  await assert.rejects(allocateIdentity(options(8, 'claude'), github.api, roster), /An unclaimed branch already exists/);
  assert.equal(github.assignments.find(assignment => assignment.slug === dead.slug).released, true);
  assert.equal(github.revision, revision + 1);
});

// The sweep is kept, and the reservation that write was carrying is not, because the caller is about
// to be told it failed and a lease nobody holds is worse than the walk being paid for twice.
test('a reservation write that fails outright records the sweep and nothing else', async () => {
  const github = fixture();
  const dead = await allocateIdentity(options(22, 'claude', 'reviewer'), github.api, roster);
  github.ticket(22).state = 'closed';
  const revision = github.revision;
  github.hardFailures = 1;
  await assert.rejects(allocateIdentity(options(9, 'claude'), github.api, roster), { status: 500 });
  assert.equal(github.assignments.find(assignment => assignment.slug === dead.slug).released, true);
  assert.equal(github.assignments.filter(assignment => !assignment.released).length, 0);
  assert.equal(github.revision, revision + 1);
});

test('a refusal still reports pool exhaustion when its sweep write fails', async () => {
  const github = fixture();
  for (let issue = 1; issue <= 12; issue++) await allocateIdentity(options(issue, 'claude', 'reviewer'), github.api, roster);
  await allocateIdentity(options(20, 'codex', 'reviewer'), github.api, roster);
  github.ticket(20).state = 'closed';
  const api = async (method, endpoint, body, missing) => {
    if (method === 'PUT') throw Object.assign(Error('server'), { status: 500 });
    return github.api(method, endpoint, body, missing);
  };
  await assert.rejects(allocateIdentity(options(13, 'claude', 'reviewer'), api, roster), { code: 'BOT_POOL_OCCUPIED' });
});

test('the reaper releases dead leases without an allocation and writes only when one dies', async () => {
  const github = fixture();
  const live = await allocateIdentity(options(20, 'claude', 'reviewer'), github.api, roster);
  const dying = await allocateIdentity(options(42, 'claude', 'reviewer'), github.api, roster);
  const revision = github.revision;
  assert.deepEqual(await releaseDeadLeases(repository, github.api, roster), []);
  assert.equal(github.revision, revision);
  github.ticket(42).state = 'closed';
  assert.deepEqual(await releaseDeadLeases(repository, github.api, roster), [dying.slug]);
  assert.equal(github.revision, revision + 1);
  assert.equal(github.assignments.find(assignment => assignment.slug === dying.slug).released, true);
  assert.equal(github.assignments.find(assignment => assignment.slug === live.slug).released, false);
  assert.deepEqual(await releaseDeadLeases(repository, github.api, roster), []);
  assert.equal(github.revision, revision + 1);
});

test('the reaper reports a reused slug only when its current lease dies', async () => {
  const github = fixture();
  const first = await allocateIdentity(options(20, 'claude', 'reviewer'), github.api, roster);
  github.ticket(20).state = 'closed';
  assert.deepEqual(await releaseDeadLeases(repository, github.api, roster), [first.slug]);
  const reused = await allocateIdentity(options(30, 'claude', 'reviewer'), github.api, roster);
  assert.equal(reused.slug, first.slug);
  const dying = await allocateIdentity(options(40, 'claude', 'reviewer'), github.api, roster);
  github.ticket(40).state = 'closed';
  assert.deepEqual(await releaseDeadLeases(repository, github.api, roster), [dying.slug]);
  assert.equal(github.assignments.find(assignment => !assignment.released && assignment.slug === reused.slug).key, 'claude/reviewer/issue-30');
});

test('the reaper bounds contention at eight attempts and keeps the lease held', async () => {
  const github = fixture();
  const lease = await allocateIdentity(options(20, 'claude', 'reviewer'), github.api, roster);
  github.ticket(20).state = 'closed';
  github.conflicts = 10;
  await assert.rejects(releaseDeadLeases(repository, github.api, roster), /eight attempts/);
  assert.equal(github.collisionCount, 8);
  assert.equal(github.assignments.find(assignment => assignment.slug === lease.slug).released, false);
});

test('a merged reviewer PR frees its identity while its issue stays open', async () => {
  const github = fixture();
  const target = pr => ({ repository, provider: 'claude', role: 'reviewer', pr });
  for (let issue = 1; issue <= 13; issue++) github.pull(200 + issue, issue, 'claude');
  const reviewers = [];
  for (let issue = 1; issue <= 12; issue++) reviewers.push(await allocateIdentity(target(200 + issue), github.api, roster));
  assert.equal((await reviewerCapacity(repository, github.api, roster)).claude, 0);
  await assert.rejects(allocateIdentity(target(213), github.api, roster), { code: 'BOT_POOL_OCCUPIED' });
  github.pull(201, 1, 'claude').state = 'closed';
  assert.equal(github.ticket(1).state, 'open');
  assert.equal((await reviewerCapacity(repository, github.api, roster)).claude, 1);
  assert.equal((await allocateIdentity(target(213), github.api, roster)).login, reviewers[0].login);
  assert.equal(github.assignments.find(assignment => assignment.pr === 201).released, true);
  await assert.rejects(allocateIdentity(target(201), github.api, roster), /already closed/);
});

// This lease used to be held for the whole life of an open issue. It is now given up once the branch's
// pull requests have all closed, because a landed branch has no writer left for the lease to keep out,
// and an issue that stays open for follow-up work was pinning an identity nothing was using.
//
// Identity continuity across that issue survives without anything extra. An issue that comes back while
// its lease is still active takes the existing-lease path above and keeps its own bot, so the only case
// that changes identity is one where another issue had already claimed the freed slot.
test('an executor lease allocated from its PR is given up once that PR closes', async () => {
  const github = fixture();
  github.pull(87, 20, 'claude');
  const worker = await allocateIdentity({ repository, provider: 'claude', role: 'executor', pr: 87 }, github.api, roster);
  assert.equal(github.assignments[0].pr, 87);
  github.pull(87, 20, 'claude').state = 'closed';
  assert.equal(github.ticket(20).state, 'open');
  const other = await allocateIdentity(options(42, 'claude'), github.api, roster);
  assert.equal(github.assignments.find(assignment => assignment.issue === 20).released, true);
  // The freed name is the one the next issue gets, which is what freeing it was for.
  assert.equal(other.login, worker.login);
  await assert.rejects(readAssignment(options(20, 'claude'), github.api, roster), /No active/);
});

test('twelve reviewer slots retain assignments and reject overflow per provider', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) {
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

test('closed PRs release an open issue, but an unopened one keeps its lease', async () => {
  const github = fixture();
  const first = await allocateIdentity(options(20), github.api, roster);
  const second = await allocateIdentity(options(42), github.api, roster);
  assert.notEqual(first.login, second.login);
  // Issue 42 has no pull request at all, which is the state a lane sits in between claiming its branch
  // and publishing its draft. That one keeps its lease, or the sweep would take a slot out from under a
  // worker that is still running.
  assert.equal(github.assignments.find(assignment => assignment.issue === 42).released, false);
  github.pull(87, 20).state = 'closed';
  await allocateIdentity(options(51), github.api, roster);
  assert.equal(github.assignments.find(assignment => assignment.issue === 20).released, true);
  assert.equal(github.assignments.find(assignment => assignment.issue === 42).released, false);
});

// The shape that filled the pool in practice: a pull request merges, its issue stays open because the
// acceptance criteria are not all met, and the branch survives the merge. The lease was held for every
// one of those and nothing was using it. All twelve slots went this way.
test('a landed branch frees its slot even with the issue open and the branch still there', async () => {
  const github = fixture();
  const workers = [];
  for (let issue = 1; issue <= 12; issue++) workers.push(await allocateIdentity(options(issue, 'claude'), github.api, roster));
  await assert.rejects(allocateIdentity(options(99, 'claude'), github.api, roster), { code: 'BOT_POOL_OCCUPIED' });
  // Issue 5's work landed. Its branch is still on the remote and its issue is still open.
  github.pull(500, 5, 'claude').state = 'closed';
  github.branches.add('claude/issue-5');
  assert.equal(github.ticket(5).state, 'open');
  const taken = await allocateIdentity(options(99, 'claude'), github.api, roster);
  assert.equal(taken.login, workers[4].login);
  assert.equal(github.assignments.find(assignment => assignment.issue === 5).released, true);
  // And issue 5 is not locked out of coming back. Its old branch and its merged PR belong to a bot that
  // no longer holds it, and neither may refuse the next lane, or freeing the slot would strand the issue.
  // Issue 6 lands too, so there is a slot for issue 5 to return into rather than the twelfth one it
  // just gave up, which issue 99 is now using.
  github.pull(600, 6, 'claude').state = 'closed';
  const successor = await allocateIdentity(options(5, 'claude'), github.api, roster);
  assert.notEqual(successor.login, taken.login);
  assert.equal(successor.login, workers[5].login);
  assert.equal(github.assignments.filter(assignment => assignment.issue === 5 && !assignment.released).length, 1);
});

// Reclaiming a spent lease must not hand the issue's own leftovers the power to refuse it. `settled`
// is a snapshot and it reverts: a closed pull request reopens, or the lane opens its next draft on the
// same branch, and by then the lease is already gone. If the previous holder counted as a rival, the
// issue would be locked out of every identity including the one that made those leftovers.
test('an issue is not locked out by the branch and PR its own previous lease left behind', async () => {
  const github = fixture();
  github.pull(87, 20, 'claude');
  const first = await allocateIdentity({ repository, provider: 'claude', role: 'executor', pr: 87 }, github.api, roster);
  github.branches.add('claude/issue-20');
  github.pull(87, 20, 'claude').state = 'closed';
  // Another issue allocating sweeps the spent lease and takes the freed name for itself.
  const other = await allocateIdentity(options(99, 'claude'), github.api, roster);
  assert.equal(other.login, first.login);
  assert.equal(github.assignments.find(assignment => assignment.issue === 20).released, true);
  // The lane was never dead. It opens its next draft on the same branch, so the branch stops being
  // settled while the lease it used to hold is already gone.
  github.pull(88, 20, 'claude');
  const resumed = await allocateIdentity(options(20, 'claude'), github.api, roster);
  assert.notEqual(resumed.login, first.login);
  assert.equal(github.assignments.filter(assignment => assignment.issue === 20 && !assignment.released).length, 1);
});

// Only an executor lease is reclaimed on a landed branch. A reviewer lease is keyed to its pull request
// rather than to the branch, and `closed()` already decides it, so reclaiming one on branch state would
// free a reviewer that is still mid-review.
test('a reviewer lease survives a landed branch that would reclaim an executor', async () => {
  const github = fixture();
  const reviewer = await allocateIdentity(options(20, 'claude', 'reviewer'), github.api, roster);
  const worker = await allocateIdentity(options(20, 'claude'), github.api, roster);
  // One closed pull request on the shared branch. That settles the branch, and the issue stays open, so
  // the executor lease is spent and the reviewer lease is not: `closed()` decides a reviewer by its own
  // pull request rather than by what else has landed on the branch it happens to be watching.
  github.pull(87, 20, 'claude').state = 'closed';
  assert.equal(github.ticket(20).state, 'open');
  await allocateIdentity(options(99, 'claude'), github.api, roster);
  assert.equal(github.assignments.find(assignment => assignment.slug === worker.slug).released, true);
  assert.equal(github.assignments.find(assignment => assignment.slug === reviewer.slug).released, false);
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

test('the CLI waits only for reviewer capacity and never publishes credentials while waiting', () => {
  const scratch = new URL('../../.Internal/workspaces/bot-identity-tests/', import.meta.url);
  mkdirSync(scratch, { recursive: true });
  for (const provider of ['codex', 'claude', 'gemini']) {
    const directory = mkdtempSync(join(fileURLToPath(scratch), 'capacity-'));
    const gh = join(directory, 'gh');
    copyFileSync(new URL('./fixtures/bot-gh.mjs', import.meta.url), gh);
    chmodSync(gh, 0o700);
    const output = join(directory, 'outputs');
    const env = { ...process.env, PATH: `${directory}${delimiter}${process.env.PATH}`, GH_TOKEN: 'fixture-secret-never-print', GITHUB_REPOSITORY: repository, GITHUB_OUTPUT: output, BOT_TEST_LOG: join(directory, 'requests'), BOT_TEST_ROSTER: fileURLToPath(new URL('./bot-identities.json', import.meta.url)), BOT_TEST_PROVIDER: provider, REVIEW_CAPACITY_WAIT: 'true' };
    const cli = fileURLToPath(new URL('./bot-identities.mjs', import.meta.url));
    const args = [cli, 'allocate', '--provider', provider, '--role', 'reviewer', '--issue', '42'];
    const run = spawnSync(process.execPath, args, { env, encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
    assert.equal(readFileSync(output, 'utf8'), 'waiting=true\n');
    assert.doesNotMatch(run.stdout + run.stderr, /fixture-secret/);
    const refused = spawnSync(process.execPath, args, { env: { ...env, REVIEW_CAPACITY_WAIT: 'false' }, encoding: 'utf8' });
    assert.equal(refused.status, 1);
    const unauthenticated = spawnSync(process.execPath, args, { env: { ...env, GH_TOKEN: '' }, encoding: 'utf8' });
    assert.equal(unauthenticated.status, 1);
  }
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
