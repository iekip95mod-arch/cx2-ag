import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { cancelObsoleteReviews, retryWaitingReviews } from './review-queue.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const root = `repos/${repository}`;
function fixture() {
  const pr = { number: 42, state: 'open', draft: false, head: { ref: 'codex/issue-17', sha: 'b'.repeat(40), repo: { full_name: repository } } };
  const runs = [];
  const writes = [];
  const jobs = [{ steps: [{ name: 'Wait for reviewer capacity', conclusion: 'success' }] }];
  const api = async (method, endpoint) => {
    if (method === 'POST') { writes.push(endpoint); return {}; }
    if (endpoint === `${root}/pulls/42`) return structuredClone(pr);
    if (endpoint.includes('/pulls?')) return [structuredClone(pr)];
    if (endpoint.includes('/jobs?')) return { jobs };
    if (endpoint.includes('/workflows/')) {
      const workflow = endpoint.includes('agent-review-request.yml') ? 'agent-review-request.yml' : 'agent-review.yml';
      return { workflow_runs: structuredClone(runs.filter(run => run.path.endsWith('/' + workflow))) };
    }
    if (/\/runs\/\d+$/.test(endpoint)) return structuredClone(runs.find(run => run.id === Number(endpoint.split('/').at(-1))));
    throw Error(endpoint);
  };
  const add = (id, workflow = 'agent-review.yml', overrides = {}) => {
    const run = { id, path: `.github/workflows/${workflow}`, event: 'pull_request', head_repository: { full_name: repository }, head_branch: pr.head.ref, head_sha: 'a'.repeat(40), status: 'in_progress', run_attempt: 1, run_started_at: '2026-09-14T00:00:00Z', pull_requests: [{ number: 42 }], display_title: 'Assign reviewer for PR #42 (requested)', ...overrides };
    runs.push(run);
    return run;
  };
  return { pr, runs, writes, jobs, api, add };
}

test('cancel obsolete assignment and review runs without cancelling current work', async () => {
  const f = fixture();
  f.add(1);
  f.add(2, 'agent-review-request.yml');
  f.add(3, 'agent-review.yml', { head_sha: f.pr.head.sha });
  f.add(4, 'agent-review.yml', { status: 'completed' });
  assert.deepEqual(await cancelObsoleteReviews(42, f.api), [1, 2]);
  assert.deepEqual(f.writes, [`${root}/actions/runs/1/cancel`, `${root}/actions/runs/2/cancel`]);
});

test('foreign, unrelated and changed current runs remain untouched', async () => {
  for (const overrides of [{ event: 'workflow_dispatch' }, { head_repository: { full_name: 'other/repo' } }, { pull_requests: [{ number: 43 }] }, { head_branch: 'codex/issue-99' }]) {
    const f = fixture(); f.add(1, 'agent-review.yml', overrides);
    assert.deepEqual(await cancelObsoleteReviews(42, f.api), []);
  }
  const f = fixture(); const run = f.add(1);
  const api = (method, endpoint) => {
    if (endpoint === `${root}/actions/runs/1`) run.head_sha = f.pr.head.sha;
    return f.api(method, endpoint);
  };
  assert.deepEqual(await cancelObsoleteReviews(42, api), []);
});

test('closing or drafting a PR cancels even its current review', async () => {
  for (const change of [{ state: 'closed' }, { draft: true }]) {
    const f = fixture(); Object.assign(f.pr, change); f.add(1, 'agent-review.yml', { head_sha: f.pr.head.sha });
    assert.deepEqual(await cancelObsoleteReviews(42, f.api), [1]);
  }
});

test('capacity release retries a waiting assignment only on its current revision', async () => {
  for (const provider of ['codex', 'claude']) {
    const f = fixture(); f.pr.head.ref = `${provider}/issue-17`;
    f.add(10, 'agent-review-request.yml', { head_sha: f.pr.head.sha, status: 'completed', conclusion: 'success' });
    assert.deepEqual(await retryWaitingReviews(f.api, async () => ({ [provider]: 1 })), [42]);
    assert.deepEqual(f.writes, [`${root}/actions/runs/10/rerun`]);
  }
});

test('no capacity, stale revision, pending work and unrelated failures never retry', async () => {
  for (const kind of ['full', 'stale', 'pending', 'other', 'draft']) {
    const f = fixture();
    f.add(10, 'agent-review-request.yml', { head_sha: kind === 'stale' ? 'a'.repeat(40) : f.pr.head.sha, status: kind === 'pending' ? 'queued' : 'completed', conclusion: 'failure' });
    if (kind === 'draft') f.pr.draft = true;
    if (kind === 'other') f.jobs[0].steps = [{ name: 'Require a trusted PR author', conclusion: 'failure' }];
    assert.deepEqual(await retryWaitingReviews(f.api, async () => ({ codex: kind === 'full' ? 0 : 1, claude: 0 })), []);
  }
});

test('an allocation failure without a capacity marker is not retried', async () => {
  const f = fixture(); f.add(10, 'agent-review-request.yml', { head_sha: f.pr.head.sha, status: 'completed', conclusion: 'failure' });
  f.jobs[0].steps = [{ name: 'Reserve the reviewer identity', conclusion: 'failure' }];
  assert.deepEqual(await retryWaitingReviews(f.api, async () => ({ codex: 1, claude: 0 })), []);
});

test('head movement and newer run attempts prevent retry', async () => {
  for (const kind of ['head', 'attempt']) {
    const f = fixture(); const run = f.add(10, 'agent-review-request.yml', { head_sha: f.pr.head.sha, status: 'completed', conclusion: 'success' });
    const api = (method, endpoint) => {
      if (endpoint === `${root}/pulls/42` && kind === 'head') f.pr.head.sha = 'c'.repeat(40);
      if (endpoint === `${root}/actions/runs/10` && kind === 'attempt') run.run_attempt++;
      return f.api(method, endpoint);
    };
    assert.deepEqual(await retryWaitingReviews(api, async () => ({ codex: 1, claude: 0 })), []);
  }
});

test('recovery uses trusted main on Ubuntu and waiting skips token publication', () => {
  const workflow = readFileSync(new URL('../workflows/review-queue.yml', import.meta.url), 'utf8');
  assert.match(workflow, /ref: main/);
  assert.match(workflow, /runs-on: ubuntu-latest/);
  assert.match(workflow, /actions: write/);
  assert.match(workflow, /workflows: \[agent-review-request\]/);
  assert.match(workflow, /types: \[completed\]/);
  assert.doesNotMatch(workflow, /pull_request:\s|secrets\.(?!GITHUB_TOKEN)/);
  const request = readFileSync(new URL('../workflows/agent-review-request.yml', import.meta.url), 'utf8');
  assert.match(request, /REVIEW_CAPACITY_WAIT: 'true'/);
  assert.equal((request.match(/if: steps.identity.outputs.waiting != 'true'/g) ?? []).length, 4);
});
