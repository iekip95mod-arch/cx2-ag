import test from 'node:test';
import assert from 'node:assert/strict';
import { claimIssue } from './prepare-codex-worker.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const target = { repository, number: 42, run: 123 };
function fixture(options = {}) {
  const calls = [];
  const issue = { state: 'open', assignees: [], labels: [], ...options.issue };
  const api = async (method, endpoint, body) => {
    calls.push({ method, endpoint, body });
    if (method === 'GET' && endpoint === `repos/${repository}/issues/42`) return issue;
    if (endpoint === 'user') return { type: 'User', login: 'worker-owner', id: 7, ...options.identity };
    if (endpoint === `repos/${repository}/pulls/42`) return options.pr;
    if (method === 'GET' && endpoint.endsWith('/git/ref/heads/main')) return { object: { sha: 'a'.repeat(40) } };
    if (method === 'GET' && endpoint.includes('/git/ref/heads/')) return options.existing ?? null;
    if (method === 'POST' && endpoint.endsWith('/assignees')) return { assignees: options.assignment ?? [{ login: 'worker-owner' }] };
    if (method === 'POST') return {};
    throw Error(`Unexpected call ${method} ${endpoint}`);
  };
  return { calls, api };
}
test('claim creates the issue branch, assigns the publishing account and records the run', async () => {
  const { calls, api } = fixture();
  const claim = await claimIssue(target, api);
  assert.equal(claim.branch, 'codex/issue-42');
  assert.deepEqual(calls.filter(call => call.method === 'POST').map(call => [call.endpoint, call.body]), [
    [`repos/${repository}/git/refs`, { ref: 'refs/heads/codex/issue-42', sha: 'a'.repeat(40) }],
    [`repos/${repository}/issues/42/assignees`, { assignees: ['worker-owner'] }],
    [`repos/${repository}/issues/42/comments`, { body: 'Codex worker claimed this issue.\n\nBranch: codex/issue-42\nRun: https://github.com/iekip95mod-arch/cx2-ag/actions/runs/123\n\nThis worker owns this issue only. Its changes require an independent Codex review and all protected checks before merging.' }]
  ]);
});
test('invalid targets make no API requests', async () => {
  for (const changed of [{ repository: 'other/repo' }, { number: '../42' }, { number: 0 }, { run: '' }]) {
    const { calls, api } = fixture();
    await assert.rejects(claimIssue({ ...target, ...changed }, api));
    assert.equal(calls.length, 0);
  }
});
test('closed, externally assigned and Claude issues cannot be claimed', async () => {
  for (const issue of [{ state: 'closed' }, { assignees: [{ login: 'someone-else' }] }, { labels: [{ name: 'claude' }] }]) {
    const { calls, api } = fixture({ issue });
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.method === 'POST'), false);
  }
});
test('a known owner resumes a branch without replacing its ref', async () => {
  const { calls, api } = fixture({ issue: { assignees: [{ login: 'worker-owner' }] }, existing: { object: { sha: 'b'.repeat(40) } } });
  await claimIssue(target, api);
  assert.equal(calls.some(call => call.method === 'POST' && call.endpoint.endsWith('/git/refs')), false);
  const unclaimed = fixture({ existing: {} });
  await assert.rejects(claimIssue(target, unclaimed.api), /unclaimed branch/);
  assert.equal(unclaimed.calls.some(call => call.method === 'POST'), false);
});
test('pull request resumption checks repository, author and branch', async () => {
  const pr = { head: { repo: { full_name: repository }, ref: 'codex/issue-42' }, user: { login: 'worker-owner' } };
  const valid = fixture({ issue: { pull_request: {} }, pr, existing: {} });
  assert.equal((await claimIssue(target, valid.api)).branch, pr.head.ref);
  for (const changed of [{ ...pr, user: { login: 'someone-else' } }, { ...pr, head: { ...pr.head, ref: 'main' } }, { ...pr, head: { ...pr.head, repo: { full_name: 'other/repo' } } }]) {
    const { calls, api } = fixture({ issue: { pull_request: {} }, pr: changed });
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.method === 'POST'), false);
  }
});
test('an ignored assignment or non-user identity cannot report a successful claim', async () => {
  for (const options of [{ assignment: [] }, { identity: { type: 'Bot' } }]) {
    const { calls, api } = fixture(options);
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.endpoint.endsWith('/comments')), false);
  }
});
test('an API failure stops the claim before mutation', async () => {
  await assert.rejects(claimIssue(target, async () => { throw Error('unavailable'); }), /unavailable/);
});
