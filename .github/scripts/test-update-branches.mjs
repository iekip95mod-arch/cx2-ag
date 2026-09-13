import assert from 'node:assert/strict';
import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { discoverBranches, updateBranch } from './update-branches.mjs';

function fixture(provider = 'codex') {
  const repository = 'iekip95mod-arch/cx2-ag';
  const identity = { provider, branch: `${provider}/issue-42`, login: 'executor[bot]', userId: 7, appId: 8, secretName: 'EXECUTOR_KEY' };
  const pr = { number: 90, state: 'open', draft: false, mergeable: true, user: { login: identity.login, id: 7, type: 'Bot' }, base: { ref: 'main', repo: { full_name: repository } }, head: { ref: identity.branch, sha: 'a'.repeat(40), repo: { full_name: repository } }, labels: [{ name: `${provider}-review` }] };
  const writes = [];
  let behind = 1;
  const api = async (method, endpoint, body) => {
    if (method !== 'GET') {
      writes.push({ method, endpoint, body });
      if (method === 'PUT') { assert.equal(body.expected_head_sha, pr.head.sha); pr.head.sha = 'b'.repeat(40); behind = 0; }
      return {};
    }
    if (endpoint.includes('/pulls?')) return [structuredClone(pr)];
    if (endpoint.endsWith('/pulls/90')) return structuredClone(pr);
    if (endpoint.includes('/git/ref/')) return { object: { sha: 'c'.repeat(40) } };
    if (endpoint.includes('/compare/')) return { behind_by: behind };
    throw Error(endpoint);
  };
  return { pr, identity, writes, api, assignment: async () => identity, set behind(value) { behind = value; } };
}

test('both providers update their existing branch and request review only after the new head exists', async () => {
  for (const provider of ['codex', 'claude']) for (const draft of [false, true]) {
    const f = fixture(provider); f.pr.draft = draft;
    const matrix = await discoverBranches(f.api, f.assignment);
    assert.equal(matrix.include[0].branch, f.identity.branch);
    assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment), 'updated');
    assert.deepEqual(f.writes.map(write => write.method), draft ? ['PUT'] : ['PUT', 'DELETE', 'POST']);
    if (!draft) assert.deepEqual(f.writes.at(-1).body.labels, [`${provider}-review`]);
  }
});

test('current, conflicted, closed, foreign and wrongly assigned branches cannot be updated', async () => {
  for (const change of [f => { f.behind = 0; }, f => { f.pr.mergeable = false; }, f => { f.pr.state = 'closed'; }, f => { f.pr.head.repo.full_name = 'other/repo'; }, f => { f.pr.user.id++; }, f => { f.identity.branch = 'codex/issue-43'; }]) {
    const f = fixture(); change(f);
    await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment);
    assert.equal(f.writes.length, 0);
  }
});

test('a concurrent push or merge conflict rejection does not retry or request review', async () => {
  const f = fixture();
  const api = (method, ...args) => { if (method === 'PUT') throw Object.assign(Error('Head changed'), { status: 422 }); return f.api(method, ...args); };
  assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, api, async () => {}, f.assignment), 'changed-or-conflicted');
  assert.equal(f.writes.length, 0);
});

test('a PR update discovers only that PR instead of scheduling every branch again', async () => {
  const f = fixture();
  const endpoints = [];
  const matrix = await discoverBranches(async (...args) => { endpoints.push(args[1]); return f.api(...args); }, f.assignment, 90);
  assert.equal(matrix.include.length, 1);
  assert.deepEqual(endpoints, ['repos/iekip95mod-arch/cx2-ag/pulls/90']);
});

test('branch updates use trusted main scripts and share the executor branch lock', () => {
  const path = fileURLToPath(new URL('../workflows/update-branches.yml', import.meta.url));
  const workflow = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', path], { encoding: 'utf8' }));
  const events = workflow.on ?? workflow.true;
  assert.deepEqual(events.push.branches, ['main']);
  assert.ok(events.pull_request_target.types.includes('synchronize'));
  for (const job of Object.values(workflow.jobs)) for (const step of job.steps) {
    if (step.uses?.startsWith('actions/checkout')) assert.deepEqual(step.with, { ref: 'main', 'persist-credentials': false });
  }
  assert.equal(workflow.jobs.update.concurrency.group, 'agent-${{ matrix.provider }}-${{ matrix.branch }}');
  assert.equal(workflow.jobs.update.concurrency['cancel-in-progress'], false);
  const update = workflow.jobs.update.steps.at(-1);
  assert.equal(update.env.GH_TOKEN, '${{ steps.bot.outputs.token }}');
});
