import test from 'node:test';
import assert from 'node:assert/strict';
import { reviewState, waitForReview } from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const sha = 'a'.repeat(40);
const run = (id, extra = {}) => ({ id, display_title: 'Review PR #85 (requested)', head_sha: sha, pull_requests: [{ number: 85 }], status: 'completed', conclusion: 'success', ...extra });
const approval = (id = 1, extra = {}) => ({ id, commit_id: sha, state: 'APPROVED', user: { login: 'github-actions[bot]', type: 'Bot' }, ...extra });
function fixture(runs, { current = { head: { sha, ref: 'codex/review-gate' }, labels: [], state: 'open', draft: false }, jobs = [{ name: 'review-approved', status: 'completed', conclusion: 'success' }], reviews = [approval()] } = {}) {
  return async endpoint => {
    if (endpoint.endsWith('/pulls/85')) return current;
    if (endpoint.endsWith('/pulls/85/reviews?per_page=100')) return [reviews];
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
test('metadata-only runs cannot supersede the requested review attempt', async () => {
  const metadata = extra => run(2, { display_title: 'Review PR #85 (metadata)', ...extra });
  assert.equal(await reviewState(fixture([run(1), metadata({ conclusion: 'failure' })]), repository, 85, sha), 'approved');
  assert.equal(await reviewState(fixture([run(1), metadata({ status: 'in_progress', conclusion: null })]), repository, 85, sha), 'approved');
  await assert.rejects(reviewState(fixture([run(1, { conclusion: 'failure' }), metadata({})]), repository, 85, sha));
  assert.equal(await reviewState(fixture([metadata({})]), repository, 85, sha), 'pending');
});
test('dismissed and superseded approvals cannot release CodeQL', async () => {
  for (const state of ['DISMISSED', 'CHANGES_REQUESTED', 'COMMENTED']) {
    await assert.rejects(reviewState(fixture([run(1)], { reviews: [approval(1, { state })] }), repository, 85, sha));
    await assert.rejects(reviewState(fixture([run(1)], { reviews: [approval(2, { state }), approval(1)] }), repository, 85, sha));
  }
  assert.equal(await reviewState(fixture([run(1)], { reviews: [approval(2), approval(1, { state: 'CHANGES_REQUESTED' })] }), repository, 85, sha), 'approved');
});
test('only the selected provider bot can approve the current revision', async () => {
  for (const reviews of [[], [approval(1, { commit_id: 'b'.repeat(40) })], [approval(1, { user: { login: 'claude[bot]', type: 'Bot' } })], [approval(1, { user: { login: 'github-actions[bot]', type: 'User' } })]]) {
    await assert.rejects(reviewState(fixture([run(1)], { reviews }), repository, 85, sha));
  }
  for (const [ref, labels, bot] of [['claude/fix', [], 'claude[bot]'], ['codex/fix', [{ name: 'claude-review' }], 'claude[bot]'], ['claude/fix', [{ name: 'codex-review' }], 'github-actions[bot]']]) {
    const current = { head: { sha, ref }, state: 'open', labels };
    assert.equal(await reviewState(fixture([run(1)], { current, reviews: [approval(1, { user: { login: bot, type: 'Bot' } })] }), repository, 85, sha), 'approved');
  }
  await assert.rejects(reviewState(fixture([run(1)], { current: { head: { sha, ref: 'codex/fix' }, state: 'open', labels: [{ name: 'claude-review' }, { name: 'codex-review' }] } }), repository, 85, sha));
});
