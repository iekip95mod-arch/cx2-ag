import test from 'node:test';
import assert from 'node:assert/strict';
import { reviewState, waitForReview } from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const sha = 'a'.repeat(40);
const run = (id, extra = {}) => ({ id, head_sha: sha, pull_requests: [{ number: 85 }], status: 'completed', conclusion: 'success', ...extra });
function fixture(runs, { current = { head: { sha }, state: 'open', draft: false }, jobs = [{ name: 'review-approved', status: 'completed', conclusion: 'success' }] } = {}) {
  return async endpoint => {
    if (endpoint.endsWith('/pulls/85')) return current;
    if (endpoint.includes('/workflows/')) return [{ workflow_runs: runs }];
    assert.match(endpoint, /\/actions\/runs\/\d+\/jobs\?filter=latest&per_page=100$/);
    return [{ jobs }];
  };
}
test('approval is scoped to the current PR and exact head', async () => {
  assert.equal(await reviewState(fixture([run(1)]), repository, 85, sha), 'approved');
  assert.equal(await reviewState(fixture([run(1, { head_sha: 'b'.repeat(40) }), run(2, { pull_requests: [{ number: 86 }] })]), repository, 85, sha), 'pending');
});
test('failed, cancelled and missing approvals cannot start CodeQL', async () => {
  for (const conclusion of ['failure', 'cancelled', 'timed_out', 'skipped', null]) {
    await assert.rejects(reviewState(fixture([run(1, { conclusion })]), repository, 85, sha));
    await assert.rejects(reviewState(fixture([run(1)], { jobs: [{ name: 'review-approved', status: 'completed', conclusion }] }), repository, 85, sha));
  }
  await assert.rejects(reviewState(fixture([run(1)], { jobs: [] }), repository, 85, sha));
  await assert.rejects(reviewState(fixture([run(1)], { jobs: [{ name: 'review-approved', status: 'in_progress', conclusion: 'success' }] }), repository, 85, sha));
});
test('new review attempts supersede failures and pending attempts prevent early scanning', async () => {
  assert.equal(await reviewState(fixture([run(1, { conclusion: 'failure' }), run(2)]), repository, 85, sha), 'approved');
  await assert.rejects(reviewState(fixture([run(1), run(2, { conclusion: 'failure' })]), repository, 85, sha));
  assert.equal(await reviewState(fixture([run(1, { status: 'in_progress', conclusion: null }), run(2)]), repository, 85, sha), 'pending');
});
test('changed, closed and draft PRs cannot start CodeQL', async () => {
  for (const current of [{ head: { sha: 'b'.repeat(40) }, state: 'open' }, { head: { sha }, state: 'closed' }, { head: { sha }, state: 'open', draft: true }]) {
    await assert.rejects(reviewState(fixture([run(1)], { current }), repository, 85, sha));
  }
});
test('pending review waits, completed approval releases and missing review times out', async () => {
  let waited = 0;
  const read = async (...args) => fixture(waited ? [run(1)] : [])(...args);
  await waitForReview(read, repository, 85, sha, async ms => { assert.equal(ms, 30000); waited++; }, 2);
  assert.equal(waited, 1);
  await assert.rejects(waitForReview(fixture([]), repository, 85, sha, async () => {}, 2), /Timed out/);
});
test('API errors fail closed', async () => {
  await assert.rejects(reviewState(async () => { throw Error('API unavailable'); }, repository, 85, sha), /API unavailable/);
});
