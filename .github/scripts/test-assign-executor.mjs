import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { assignExecutor } from './assign-executor.mjs';
import { loadRoster } from './bot-identities.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const roster = loadRoster().map((identity, index) => ({ ...identity, appId: index + 1, userId: index + 100, clientId: `Iv1.test${index}` }));

function fixture(pr = false) {
  const identity = roster[0];
  const user = { login: identity.login, id: identity.userId, type: 'Bot' };
  const issue = { number: 42, state: 'open', assignees: [] };
  const pull = { number: 87, state: 'open', assignees: [], pull_request: {}, user, head: { ref: 'codex/issue-42', sha: 'a'.repeat(40), repo: { full_name: repository } } };
  const number = pr ? 87 : 42;
  const comment = { id: 1000, body: 'Starting work.\nExecutor lease: codex/issue-42', user, updated_at: '2026-09-12T22:00:00Z', issue_url: `https://api.github.com/repos/${repository}/issues/${number}` };
  const event = { action: 'created', repository: { full_name: repository }, sender: user, issue: structuredClone(pr ? pull : issue), comment: structuredClone(comment) };
  const calls = [];
  let lease = { key: 'codex/executor/issue-42', provider: 'codex', role: 'executor', issue: 42, pr: null, branch: 'codex/issue-42', slug: identity.slug, released: false };
  let currentComment = comment;
  let participation = true;
  let assignmentError;
  let emptyAssignment = false;
  const api = async (method, endpoint, body) => {
    calls.push({ method, endpoint, body });
    const path = endpoint.replace(`repos/${repository}/`, '');
    if (method === 'GET' && path === 'issues/42') return structuredClone(issue);
    if (method === 'GET' && path === 'issues/87') return structuredClone(pull);
    if (method === 'GET' && path === 'pulls/87') return structuredClone(pull);
    if (method === 'GET' && path === 'issues/comments/1000') return structuredClone(currentComment);
    if (method === 'GET' && /issues\/(42|87)\/comments\?/.test(path)) return participation ? [structuredClone(comment)] : [];
    if (method === 'GET' && path === 'contents/assignments.json?ref=bot-assignments') return { sha: 'lease-sha', content: Buffer.from(JSON.stringify({ version: 1, assignments: lease ? [lease] : [] })).toString('base64') };
    if (method === 'POST' && /issues\/(42|87)\/assignees$/.test(path)) {
      const target = path.startsWith('issues/42/') ? issue : pull;
      if (assignmentError && target.number === 42) throw Object.assign(Error('Forbidden'), { status: assignmentError });
      if (!emptyAssignment) target.assignees.push(user);
      return structuredClone(target);
    }
    if (method === 'DELETE' && /issues\/(42|87)\/assignees$/.test(path)) {
      const target = path.startsWith('issues/42/') ? issue : pull;
      target.assignees = target.assignees.filter(assignee => !body.assignees.includes(assignee.login));
      return structuredClone(target);
    }
    if (method === 'POST' && /issues\/(42|87)\/labels$/.test(path)) return body.labels.map(name => ({ name }));
    throw Error(`Unexpected ${method} ${endpoint}`);
  };
  return { identity, user, issue, pull, event, comment, calls, api, options: { event, eventName: 'issue_comment', repository }, get mutations() { return calls.filter(call => call.method !== 'GET'); }, set lease(value) { lease = value; }, set comment(value) { currentComment = value; }, set participation(value) { participation = value; }, set assignmentError(value) { assignmentError = value; }, set emptyAssignment(value) { emptyAssignment = value; } };
}

test('a live leased executor claim receives native issue assignment', async () => {
  const github = fixture();
  const outcome = await assignExecutor(github.options, github.api, roster);
  assert.deepEqual(outcome, { assigned: [42], unassigned: [], skipped: [] });
  assert.deepEqual(github.issue.assignees, [github.user]);
  assert.deepEqual(github.mutations, [{ method: 'POST', endpoint: `repos/${repository}/issues/42/assignees`, body: { assignees: [github.identity.login] } }]);
});

test('a PR start comment assigns the participating executor on both issue and PR', async () => {
  const github = fixture(true);
  const outcome = await assignExecutor(github.options, github.api, roster);
  assert.deepEqual(outcome.assigned, [42, 87]);
  assert.deepEqual(github.issue.assignees, [github.user]);
  assert.deepEqual(github.pull.assignees, [github.user]);
  assert.equal(github.mutations.length, 2);
});

test('an already assigned executor produces no additional mutations', async () => {
  const github = fixture(true);
  github.issue.assignees = [github.user];
  github.pull.assignees = [github.user];
  assert.deepEqual((await assignExecutor(github.options, github.api, roster)).assigned, [42, 87]);
  assert.equal(github.mutations.length, 0);
});

test('reviewers, forged numeric identities and unknown bots cannot assign themselves', async () => {
  for (const sender of [{ login: roster[12].login, id: roster[12].userId, type: 'Bot' }, { login: roster[0].login, id: 999999, type: 'Bot' }, { login: 'unknown[bot]', id: 123, type: 'Bot' }, { login: roster[0].login, id: roster[0].userId, type: 'User' }]) {
    const github = fixture();
    github.event.sender = sender;
    assert.deepEqual((await assignExecutor(github.options, github.api, roster)).assigned, []);
    assert.equal(github.calls.length, 0);
  }
});

test('closed targets, fork PRs and stale PR opening events are ignored', async () => {
  for (const change of [github => { github.pull.state = 'closed'; }, github => { github.pull.head.repo.full_name = 'other/repo'; }, github => { github.options.eventName = 'pull_request_target'; github.event.action = 'opened'; github.event.pull_request = structuredClone(github.pull); github.event.pull_request.head.sha = 'b'.repeat(40); }]) {
    const github = fixture(true);
    change(github);
    assert.deepEqual((await assignExecutor(github.options, github.api, roster)).assigned, []);
    assert.equal(github.mutations.length, 0);
  }
});

test('superseded or missing executor leases cannot cause native assignment', async () => {
  const missing = fixture();
  missing.lease = null;
  await assert.rejects(assignExecutor(missing.options, missing.api, roster), /No active/);
  assert.equal(missing.mutations.length, 0);
  const superseded = fixture();
  superseded.lease = { key: 'codex/executor/issue-42', provider: 'codex', role: 'executor', issue: 42, pr: null, branch: 'codex/issue-42', slug: roster[1].slug, released: false };
  assert.deepEqual((await assignExecutor(superseded.options, superseded.api, roster)).assigned, []);
  assert.equal(superseded.mutations.length, 0);
});

test('deleted, edited, transplanted and falsely attributed comments do not assign', async () => {
  for (const change of [comment => null, comment => ({ ...comment, body: 'Changed' }), comment => ({ ...comment, updated_at: '2026-09-13T00:00:00Z' }), comment => ({ ...comment, issue_url: `https://api.github.com/repos/${repository}/issues/99` }), comment => ({ ...comment, user: { ...comment.user, id: 999 } })]) {
    const github = fixture();
    github.comment = change(github.event.comment);
    assert.deepEqual((await assignExecutor(github.options, github.api, roster)).assigned, []);
    assert.equal(github.mutations.length, 0);
  }
});

test('PR opening waits for a regular executor lease comment', async () => {
  const github = fixture(true);
  github.options.eventName = 'pull_request_target';
  github.event.action = 'opened';
  github.event.pull_request = structuredClone(github.pull);
  github.participation = false;
  assert.deepEqual((await assignExecutor(github.options, github.api, roster)).assigned, []);
  assert.equal(github.mutations.length, 0);
  github.participation = true;
  assert.deepEqual((await assignExecutor(github.options, github.api, roster)).assigned, [42, 87]);
});

test('foreign assignees remain untouched and explicit legacy migration removes only the owner', async () => {
  const foreign = fixture(true);
  foreign.issue.assignees = [{ login: 'someone-else' }];
  await assert.rejects(assignExecutor(foreign.options, foreign.api, roster), /Another identity/);
  assert.equal(foreign.mutations.length, 0);
  const legacy = fixture();
  legacy.issue.assignees = [{ login: 'iekip95mod-arch' }];
  await assert.rejects(assignExecutor(legacy.options, legacy.api, roster), /Another identity/);
  legacy.options.legacyOwner = 'iekip95mod-arch';
  assert.deepEqual((await assignExecutor(legacy.options, legacy.api, roster)).assigned, [42]);
  assert.deepEqual(legacy.issue.assignees, [legacy.user]);
  assert.deepEqual(legacy.mutations.map(call => call.method), ['POST', 'DELETE']);
});

test('an issue assignment refusal is explicit while PR assignment can still succeed', async () => {
  const github = fixture(true);
  github.assignmentError = 403;
  const outcome = await assignExecutor(github.options, github.api, roster);
  assert.deepEqual(outcome.assigned, [87]);
  assert.equal(outcome.unassigned[0].number, 42);
  assert.match(outcome.unassigned[0].reason, /HTTP 403.*not a native assignment/);
  assert.deepEqual(github.issue.assignees, []);
  assert.deepEqual(github.pull.assignees, [github.user]);
  assert.ok(github.mutations.some(call => call.endpoint.endsWith('/issues/42/labels') && call.body.labels[0] === `worker:${github.identity.slug}`));
});

test('empty successful assignment responses and server failures remain failures', async () => {
  const empty = fixture();
  empty.emptyAssignment = true;
  await assert.rejects(assignExecutor(empty.options, empty.api, roster), /did not assign/);
  const failure = fixture();
  failure.assignmentError = 500;
  await assert.rejects(assignExecutor(failure.options, failure.api, roster), /Forbidden/);
});

test('the workflow restricts the owner credential to a trusted metadata command', () => {
  const workflow = readFileSync(new URL('../workflows/agent-assignment.yml', import.meta.url), 'utf8');
  assert.match(workflow, /pull_request_target:/);
  assert.match(workflow, /ref: main\n\s+persist-credentials: false/);
  assert.match(workflow, /GH_TOKEN: \$\{\{ secrets.CODEX_GITHUB_TOKEN \}\}/);
  assert.match(workflow, /run: node \.github\/scripts\/assign-executor.mjs/);
  assert.doesNotMatch(workflow, /codex exec|claude-code-action|head\.sha|head\.ref/);
  assert.match(workflow, /Native assignment routing starts after its setup PR merges into main/);
});
