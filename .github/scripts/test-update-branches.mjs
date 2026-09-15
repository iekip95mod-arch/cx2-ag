import assert from 'node:assert/strict';
import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { discoverBranches, updateBranch } from './update-branches.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const root = `repos/${repository}`;

function fixture(provider = 'codex', labels = [`${provider}-review`], recorded) {
  const identity = { provider, branch: `${provider}/issue-42`, login: 'executor[bot]', userId: 7, appId: 8, secretName: 'EXECUTOR_KEY' };
  const pr = { number: 90, state: 'open', draft: false, mergeable: true, mergeable_state: 'behind', user: { login: identity.login, id: 7, type: 'Bot' }, base: { ref: 'main', repo: { full_name: repository } }, head: { ref: identity.branch, sha: 'a'.repeat(40), repo: { full_name: repository } }, labels: labels.map(name => ({ name })) };
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
  const reviewer = async ({ repository: name, pr: number, branch }) => {
    assert.equal(name, repository);
    assert.equal(number, pr.number);
    assert.equal(branch, pr.head.ref);
    return recorded;
  };
  return { pr, identity, writes, api, review, reviewer, assignment: async () => identity, set behind(value) { behind = value; } };
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

test('an update re-requests the review label the PR carries rather than the branch prefix provider', async () => {
  for (const [branch, reviewer] of [['gemini', 'claude'], ['claude', 'codex'], ['codex', 'gemini']]) {
    const f = fixture(branch, [`${reviewer}-review`, 'defect']);
    assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment), 'updated');
    assert.deepEqual(f.writes.map(write => `${write.method} ${write.endpoint}`), [
      `PUT ${root}/pulls/90/update-branch`,
      `DELETE ${root}/issues/90/labels/${reviewer}-review`,
      `POST ${root}/issues/90/labels`,
    ]);
    assert.deepEqual(f.writes.at(-1).body.labels, [`${reviewer}-review`]);
    assert.equal(f.writes.some(write => write.endpoint.includes(`${branch}-review`)), false);
  }
});

test('an update requests the recorded reviewer lease provider when the PR carries no review label', async () => {
  for (const [branch, reviewer] of [['gemini', 'claude'], ['claude', 'codex'], ['codex', 'gemini'], ['claude', 'claude']]) {
    for (const labels of [[], ['defect']]) {
      const f = fixture(branch, labels, reviewer);
      assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment, f.api, undefined, f.reviewer), 'updated');
      assert.deepEqual(f.writes.map(write => `${write.method} ${write.endpoint}`), [
        `PUT ${root}/pulls/90/update-branch`,
        `POST ${root}/issues/90/labels`,
      ]);
      assert.deepEqual(f.writes.at(-1).body.labels, [`${reviewer}-review`]);
    }
  }
});

test('an update adds no review label when the PR carries none and no reviewer lease records one', async () => {
  for (const branch of ['codex', 'claude', 'gemini']) for (const labels of [[], ['defect']]) {
    const f = fixture(branch, labels, undefined);
    assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment, f.api, undefined, f.reviewer), 'updated');
    assert.deepEqual(f.writes.map(write => `${write.method} ${write.endpoint}`), [`PUT ${root}/pulls/90/update-branch`]);
  }
});

test('a carried review label outranks a reviewer lease recording another provider', async () => {
  const f = fixture('gemini', ['claude-review'], 'gemini');
  assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment, f.api, undefined, f.reviewer), 'updated');
  assert.deepEqual(f.writes.at(-1).body.labels, ['claude-review']);
  assert.equal(f.writes.some(write => write.endpoint.includes('gemini-review')), false);
});

test('an update leaves an ambiguous review label pair alone', async () => {
  const f = fixture('claude', ['claude-review', 'gemini-review']);
  assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment), 'updated');
  assert.deepEqual(f.writes.map(write => write.method), ['PUT']);
});

test('a behind branch is held while any other gate is still deciding it', async () => {
  for (const state of ['blocked', 'unstable', 'draft', 'clean', 'dirty']) {
    const f = fixture();
    f.pr.mergeable_state = state;
    assert.deepEqual(await discoverBranches(f.api, f.assignment), { include: [] });
    assert.deepEqual(await discoverBranches(f.api, f.assignment, 90), { include: [] });
    assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment), 'gated');
    assert.equal(f.writes.length, 0);
  }
});

test('a branch whose only remaining blocker is being out of date is still updated', async () => {
  const f = fixture();
  assert.equal(f.pr.mergeable_state, 'behind');
  assert.equal((await discoverBranches(f.api, f.assignment)).include.length, 1);
  assert.equal(await updateBranch({ pr: 90, login: f.identity.login }, f.api, async () => {}, f.assignment), 'updated');
  assert.deepEqual(f.writes.map(write => write.method), ['PUT', 'DELETE', 'POST']);
});

test('an uncomputed mergeability is waited for rather than treated as ready', async () => {
  const pending = fixture();
  pending.pr.mergeable_state = 'unknown';
  let waits = 0;
  assert.equal(await updateBranch({ pr: 90, login: pending.identity.login }, pending.api, async () => { waits++; }, pending.assignment), 'unknown');
  assert.equal(pending.writes.length, 0);
  assert.ok(waits >= 1);
  assert.deepEqual(await discoverBranches(pending.api, pending.assignment, undefined, async () => {}), { include: [] });
});

test('a mergeability that settles on behind while waiting proceeds with the update', async () => {
  const settling = fixture();
  settling.pr.mergeable_state = 'unknown';
  assert.equal((await discoverBranches(settling.api, settling.assignment, 90, async () => { settling.pr.mergeable_state = 'behind'; })).include.length, 1);
  const update = fixture();
  update.pr.mergeable_state = 'unknown';
  assert.equal(await updateBranch({ pr: 90, login: update.identity.login }, update.api, async () => { update.pr.mergeable_state = 'behind'; }, update.assignment), 'updated');
  assert.deepEqual(update.writes.map(write => write.method), ['PUT', 'DELETE', 'POST']);
});

test('a completed check re-examines only its own pull request, so a branch that just became ready waits for no further merge', () => {
  const path = fileURLToPath(new URL('../workflows/update-branches.yml', import.meta.url));
  const workflow = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', path], { encoding: 'utf8' }));
  const events = workflow.on ?? workflow.true;
  assert.deepEqual(events.workflow_run, { workflows: ['check'], types: ['completed'] });
  const discover = workflow.jobs.discover;
  assert.ok(discover.if.includes("github.event_name != 'workflow_run' || github.event.workflow_run.pull_requests[0] != null"));
  assert.ok(discover.if.includes("github.event.pull_request.head.repo.full_name == github.repository"));
  assert.equal(discover.steps.at(-1).env.PR_NUMBER, '${{ github.event.pull_request.number || github.event.workflow_run.pull_requests[0].number }}');
});
