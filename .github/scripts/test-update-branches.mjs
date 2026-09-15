import assert from 'node:assert/strict';
import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { discoverBranches, updateBranch } from './update-branches.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const root = `repos/${repository}`;

function fixture(provider = 'codex') {
  const identity = { provider, branch: `${provider}/issue-42`, login: 'executor[bot]', userId: 7, appId: 8, secretName: 'EXECUTOR_KEY' };
  const pr = { number: 90, state: 'open', draft: false, mergeable: true, user: { login: identity.login, id: 7, type: 'Bot' }, base: { ref: 'main', repo: { full_name: repository } }, head: { ref: identity.branch, sha: 'a'.repeat(40), repo: { full_name: repository } }, labels: [{ name: `${provider}-review` }] };
  const writes = [];
  const runs = [];
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
    if (endpoint.includes('/actions/workflows/')) {
      const workflow = endpoint.includes('agent-review-request.yml') ? 'agent-review-request.yml' : 'agent-review.yml';
      return { workflow_runs: structuredClone(runs.filter(run => run.path.endsWith('/' + workflow))) };
    }
    if (/\/actions\/runs\/\d+$/.test(endpoint)) return structuredClone(runs.find(run => run.id === Number(endpoint.split('/').at(-1))));
    throw Error(endpoint);
  };
  const review = (id, workflow = 'agent-review.yml', overrides = {}) => {
    runs.push({ id, path: `.github/workflows/${workflow}`, event: 'pull_request', head_repository: { full_name: repository }, head_branch: pr.head.ref, head_sha: 'a'.repeat(40), status: 'in_progress', run_attempt: 1, pull_requests: [{ number: 90 }], ...overrides });
  };
  return { pr, identity, writes, api, review, assignment: async () => identity, set behind(value) { behind = value; } };
}

test('both providers update their existing branch and request review only after the new head exists', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) for (const draft of [false, true]) {
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
  assert.equal(endpoints.filter(endpoint => endpoint.includes('/pulls/')).length, 1);
  assert.equal(endpoints.some(endpoint => endpoint.includes('/pulls?')), false);
});

test('current branches are excluded before entering the writer queue', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) for (const number of [undefined, 90]) {
    const f = fixture(provider);
    f.behind = 0;
    assert.deepEqual(await discoverBranches(f.api, f.assignment, number), { include: [] });
    assert.equal(f.writes.length, 0);
  }
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
  assert.equal(workflow.jobs.update.concurrency.queue, 'max');
  for (const name of ['agent-codex.yml', 'agent.yml']) {
    const executorPath = fileURLToPath(new URL(`../workflows/${name}`, import.meta.url));
    const executor = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', executorPath], { encoding: 'utf8' }));
    assert.equal(executor.jobs.respond.concurrency.queue, 'max', name);
    assert.equal(executor.jobs.respond.concurrency['cancel-in-progress'], false);
  }
  const update = workflow.jobs.update.steps.at(-1);
  assert.equal(update.env.GH_TOKEN, '${{ steps.bot.outputs.token }}');
});

test('the update that supersedes a revision cancels the review still reading it before asking for another', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) {
    const f = fixture(provider);
    f.review(1);
    f.review(2, 'agent-review-request.yml');
    assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment), 'updated');
    assert.deepEqual(f.writes.map(write => `${write.method} ${write.endpoint}`), [
      `${root}/pulls/90/update-branch`,
      `${root}/actions/runs/1/cancel`,
      `${root}/actions/runs/2/cancel`,
      `${root}/issues/90/labels/${provider}-review`,
      `${root}/issues/90/labels`,
    ].map((endpoint, index) => `${['PUT', 'POST', 'POST', 'DELETE', 'POST'][index]} ${endpoint}`));
  }
});

test('a draft update cancels its superseded review and a review reading the new head survives', async () => {
  const draft = fixture(); draft.pr.draft = true; draft.review(1);
  assert.equal(await updateBranch({ pr: 90, login: draft.identity.login }, draft.api, async () => {}, draft.assignment), 'updated');
  assert.deepEqual(draft.writes.map(write => write.endpoint), [`${root}/pulls/90/update-branch`, `${root}/actions/runs/1/cancel`]);
  const current = fixture(); current.review(1, 'agent-review.yml', { head_sha: 'b'.repeat(40) }); current.review(2, 'agent-review.yml', { status: 'completed' });
  assert.equal(await updateBranch({ pr: 90, login: current.identity.login }, current.api, async () => {}, current.assignment), 'updated');
  assert.equal(current.writes.some(write => write.endpoint.includes('/cancel')), false);
});

test('a branch that was not moved leaves every running review alone', async () => {
  for (const change of [f => { f.behind = 0; }, f => { f.pr.mergeable = false; }, f => { f.pr.state = 'closed'; }]) {
    const f = fixture(); f.review(1); change(f);
    await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment);
    assert.equal(f.writes.length, 0);
  }
});

test('cancellation uses the workflow credential rather than the executor app token', async () => {
  const f = fixture();
  f.review(1);
  const actions = [];
  const scoped = async (method, endpoint, body) => { actions.push(`${method} ${endpoint}`); return f.api(method, endpoint, body); };
  assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment, scoped), 'updated');
  assert.ok(actions.includes(`POST ${root}/actions/runs/1/cancel`));
  assert.equal(actions.some(call => call.includes('/update-branch') || call.endsWith('/issues/90/labels')), false);
});

test('the branch updater is granted the workflow token it cancels superseded reviews with', () => {
  const path = fileURLToPath(new URL('../workflows/update-branches.yml', import.meta.url));
  const workflow = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', path], { encoding: 'utf8' }));
  assert.equal(workflow.jobs.update.permissions.actions, 'write');
  assert.equal(workflow.jobs.update.steps.at(-1).env.ACTIONS_TOKEN, '${{ secrets.GITHUB_TOKEN }}');
  assert.equal(workflow.jobs.discover.permissions, undefined);
});
