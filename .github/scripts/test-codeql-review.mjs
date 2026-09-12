import test from 'node:test';
import assert from 'node:assert/strict';
import { approvalState, reviewState as checkReview, waitForReview as wait, trustedAuthor } from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const sha = 'a'.repeat(40);
const roster = [
  { provider: 'codex', role: 'reviewer', login: 'cx2-codex-review-amber[bot]', userId: 200 },
  { provider: 'claude', role: 'reviewer', login: 'cx2-claude-review-amber[bot]', userId: 201 },
  { provider: 'codex', role: 'executor', login: 'cx2-codex-amber[bot]', userId: 100, appId: 300, clientId: 'Iv1.fixture' }
];
const options = { roster, assignment: async ({ provider }) => roster.find(identity => identity.provider === provider && identity.role === 'reviewer') };
const reviewState = (read, repository, pr, sha, overrides = options) => checkReview(read, repository, pr, sha, overrides);
const waitForReview = (read, repository, pr, sha, sleep, attempts) => wait(read, repository, pr, sha, sleep, attempts, options);
const run = (id, extra = {}) => ({ id, display_title: 'Review PR #85 (requested)', head_sha: sha, pull_requests: [{ number: 85 }], status: 'completed', conclusion: 'success', ...extra });
const approval = (id = 1, extra = {}) => ({ id, commit_id: sha, state: 'APPROVED', user: { login: roster[0].login, id: roster[0].userId, type: 'Bot' }, ...extra });
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
  for (const [ref, labels, bot] of [['claude/fix', [], roster[1]], ['codex/fix', [{ name: 'claude-review' }], roster[1]], ['claude/fix', [{ name: 'codex-review' }], roster[0]]]) {
    const current = { head: { sha, ref }, state: 'open', labels };
    assert.equal(await reviewState(fixture([run(1)], { current, reviews: [approval(1, { user: { login: bot.login, id: bot.userId, type: 'Bot' } })] }), repository, 85, sha), 'approved');
  }
  await assert.rejects(reviewState(fixture([run(1)], { current: { head: { sha, ref: 'codex/fix' }, state: 'open', labels: [{ name: 'claude-review' }, { name: 'codex-review' }] } }), repository, 85, sha));
});

test('an unleased legacy bot cannot approve a new named reviewer run', async () => {
  await assert.rejects(reviewState(fixture([run(900)], { reviews: [approval(1, { user: { login: 'github-actions[bot]', id: 41898282, type: 'Bot' } })] }), repository, 85, sha));
});

test('the exact leased reviewer ID is required and writers cannot self-approve', async () => {
  for (const user of [
    { login: roster[0].login, id: 999, type: 'Bot' },
    { login: roster[0].login, id: roster[0].userId, type: 'User' },
    { login: roster[2].login, id: roster[2].userId, type: 'Bot' }
  ]) await assert.rejects(reviewState(fixture([run(1)], { reviews: [approval(1, { user })] }), repository, 85, sha));
  await assert.rejects(reviewState(fixture([run(1)]), repository, 85, sha, { roster, assignment: async () => roster[2] }));
  await assert.rejects(reviewState(fixture([run(1)]), repository, 85, sha, { roster, assignment: async () => { throw Error('No lease'); } }), /No lease/);
  const current = { head: { sha, ref: 'codex/fix' }, labels: [], state: 'open', user: { login: roster[0].login, id: roster[0].userId } };
  await assert.rejects(reviewState(fixture([run(1)], { current }), repository, 85, sha), /own work/);
});

test('approval publication must be fresh for this attempt and match its selected provider', async () => {
  const read = fixture([run(1)]);
  await assert.rejects(approvalState(read, repository, 85, sha, { ...options, before: [1] }));
  await assert.rejects(approvalState(read, repository, 85, sha, { ...options, provider: 'claude' }));
  assert.equal(await approvalState(read, repository, 85, sha, { ...options, before: [], provider: 'codex' }), 'approved');
});

test('only configured executor IDs or associated human authors can request trusted review', () => {
  const current = { head: { repo: { full_name: repository } }, author_association: 'NONE', user: { login: roster[2].login, id: roster[2].userId, type: 'Bot' } };
  trustedAuthor(current, repository, roster);
  for (const association of ['OWNER', 'MEMBER', 'COLLABORATOR']) trustedAuthor({ ...current, author_association: association, user: { type: 'User' } }, repository, roster);
  for (const user of [{ ...current.user, id: 999 }, { ...current.user, login: 'dependabot[bot]' }, { login: roster[0].login, id: roster[0].userId, type: 'Bot' }, { type: 'User' }]) assert.throws(() => trustedAuthor({ ...current, user }, repository, roster));
  assert.throws(() => trustedAuthor({ ...current, head: { repo: { full_name: 'other/repo' } } }, repository, roster));
  assert.throws(() => trustedAuthor(current, repository, [{ ...roster[2], appId: null }]));
});
