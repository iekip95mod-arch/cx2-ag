import assert from 'node:assert/strict';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { diffAnchors, reviewRequest, publishReview } from './publish-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const patch = '@@ -10,3 +10,4 @@ function solve()\n context\n-old\n+new\n+extra\n tail\n@@ -30 +31 @@\n-last\n+final\n\\ No newline at end of file';
const files = [{ filename: 'nps/src/solve.cc', patch }];
const head = 'a'.repeat(40);
const inline = { path: files[0].filename, line: 11, side: 'RIGHT', body: '[P2] Preserve the refusal condition' };

function fixture(provider = 'codex', verdict = 'CHANGES_REQUESTED') {
  const appSlug = `cx2-${provider}-reviewer`;
  const options = { repository, pr: 91, head, appSlug, login: `${appSlug}[bot]`, review: { verdict, body: 'Configured model and effort are recorded here.', comments: [{ ...inline }] } };
  const endpoint = `repos/${repository}/pulls/91`;
  const pull = { state: 'open', draft: false, changed_files: 1, head: { sha: head, repo: { full_name: repository } }, base: { repo: { full_name: repository } }, user: { login: 'executor[bot]' } };
  const published = { id: 456, state: verdict, commit_id: head, user: { login: options.login, type: 'Bot' } };
  const responses = { [endpoint]: pull, [`${endpoint}/files?per_page=100&page=1`]: files, [`${endpoint}/reviews`]: published };
  const calls = [];
  const api = async (method, path, body) => { calls.push({ method, path, body }); assert.ok(Object.hasOwn(responses, path), path); return responses[path]; };
  return { options, endpoint, pull, published, responses, calls, api };
}

test('diff anchors distinguish deleted, added and context lines across hunks', () => {
  assert.deepEqual([...diffAnchors(patch)], ['RIGHT:10', 'LEFT:11', 'RIGHT:11', 'RIGHT:12', 'RIGHT:13', 'LEFT:30', 'RIGHT:31']);
  assert.deepEqual([...diffAnchors('@@ -0,0 +1,2 @@\n+one\n+two\n')], ['RIGHT:1', 'RIGHT:2']);
  assert.deepEqual([...diffAnchors('@@ -1,2 +0,0 @@\n-one\n-two')], ['LEFT:1', 'LEFT:2']);
  for (const invalid of [undefined, '', '@@ -1,2 +1 @@\n-one\n+two', '@@ -1 +1 @@\n-one\n+two\n+extra', '@@ -0 +1 @@\n-one\n+two', '@@ -2 +2 @@\n x\n@@ -1 +1 @@\n y']) assert.throws(() => diffAnchors(invalid));
});

test('inline requests retain exact left, right and context anchors', () => {
  const review = { verdict: 'CHANGES_REQUESTED', body: 'Three findings', comments: [{ ...inline, side: 'LEFT' }, inline, { ...inline, line: 10 }] };
  assert.deepEqual(reviewRequest(review, files, head), { commit_id: head, event: 'REQUEST_CHANGES', body: review.body, comments: review.comments });
  for (const changes of [{ path: 'not-changed.cc' }, { path: '../solve.cc' }, { line: 99 }, { line: 0 }, { line: 1.5 }, { line: 10, side: 'LEFT' }, { side: 'BOTH' }, { body: '' }]) assert.throws(() => reviewRequest({ ...review, comments: [{ ...inline, ...changes }] }, files, head));
  assert.throws(() => reviewRequest(review, [{ filename: inline.path }], head), /available text patch/);
  assert.throws(() => reviewRequest({ ...review, verdict: 'COMMENTED' }, files, head));
});

test('both providers submit one native formal review with attached comments for either verdict', async () => {
  for (const provider of ['codex', 'claude']) for (const verdict of ['APPROVED', 'CHANGES_REQUESTED']) {
    const f = fixture(provider, verdict);
    assert.equal((await publishReview(f.options, f.api)).id, 456);
    assert.deepEqual(f.calls.filter(call => call.method === 'POST'), [{ method: 'POST', path: `${f.endpoint}/reviews`, body: { commit_id: head, event: verdict === 'APPROVED' ? 'APPROVE' : 'REQUEST_CHANGES', body: f.options.review.body, comments: [inline] } }]);
    assert.equal(f.calls.filter(call => call.path === f.endpoint).length, 2);
  }
});

test('approvals and nonline verification gaps may have zero inline comments', async () => {
  for (const verdict of ['APPROVED', 'CHANGES_REQUESTED']) {
    const f = fixture('codex', verdict);
    f.options.review.comments = [];
    f.options.review.body = verdict === 'APPROVED' ? 'No findings.' : 'Unable to complete the required verification.';
    await publishReview(f.options, f.api);
    assert.deepEqual(f.calls.at(-1).body.comments, []);
  }
});

test('stale heads, forks, drafts, closed PRs and invalid identities never publish', async () => {
  for (const change of [
    f => { f.pull.head.sha = 'b'.repeat(40); },
    f => { f.pull.head.repo.full_name = 'foreign/repo'; },
    f => { f.pull.base.repo.full_name = 'foreign/repo'; },
    f => { f.pull.draft = true; },
    f => { f.pull.state = 'closed'; },
    f => { f.pull.user.login = f.options.login; },
    f => { f.options.appSlug = 'wrong-bot'; },
    f => { f.options.review.comments[0].line = 1000; },
  ]) {
    const f = fixture(); change(f);
    await assert.rejects(publishReview(f.options, f.api));
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
  const f = fixture(); let reads = 0;
  await assert.rejects(publishReview(f.options, async (...args) => { const response = await f.api(...args); if (args[1] === f.endpoint && ++reads === 2) return { ...response, head: { ...response.head, sha: 'b'.repeat(40) } }; return response; }), /changed before/);
  assert.equal(f.calls.some(call => call.method === 'POST'), false);
});

test('file pagination includes later anchors and fails closed on incomplete or oversized lists', async () => {
  const f = fixture();
  f.pull.changed_files = 101;
  f.responses[`${f.endpoint}/files?per_page=100&page=1`] = Array.from({ length: 100 }, (_, i) => ({ filename: `file-${i}.cc` }));
  f.responses[`${f.endpoint}/files?per_page=100&page=2`] = files;
  await publishReview(f.options, f.api);
  assert.equal(f.calls.filter(call => call.path.includes('/files?')).length, 2);
  for (const count of [2, 3001]) {
    const missing = fixture(); missing.pull.changed_files = count;
    await assert.rejects(publishReview(missing.options, missing.api));
    assert.equal(missing.calls.some(call => call.method === 'POST'), false);
  }
});

test('unexpected publication response reports uncertainty without retrying', async () => {
  for (const changes of [{ state: 'PENDING' }, { commit_id: 'b'.repeat(40) }, { user: { login: 'foreign[bot]', type: 'Bot' } }, { user: { login: 'cx2-codex-reviewer[bot]', type: 'User' } }]) {
    const f = fixture(); Object.assign(f.published, changes);
    await assert.rejects(publishReview(f.options, f.api), /Review may exist/);
    assert.equal(f.calls.filter(call => call.method === 'POST').length, 1);
  }
});

test('actual CLI uses JSON stdin, assigned token and exact native review arguments', () => {
  const scripts = dirname(fileURLToPath(import.meta.url));
  const root = join(scripts, '../../.Internal/workspaces/publish-review-tests'); mkdirSync(root, { recursive: true });
  for (const provider of ['codex', 'claude']) for (const fail of [false, true]) {
    const directory = mkdtempSync(join(root, 'cli-')); const bin = join(directory, 'bin'); mkdirSync(bin);
    copyFileSync(join(scripts, 'fixtures/review-feedback-gh.mjs'), join(bin, 'gh')); chmodSync(join(bin, 'gh'), 0o755);
    const f = fixture(provider); const log = join(directory, 'calls.jsonl'); const config = join(directory, 'fixture.json'); const review = join(directory, 'review.json');
    f.options.review.comments[0].body = 'Literal $(false), `false`, quotes " and\nnewlines';
    writeFileSync(config, JSON.stringify({ responses: f.responses, log, ...(fail ? { fail: `${f.endpoint}/reviews` } : {}) }));
    writeFileSync(review, JSON.stringify(f.options.review));
    const run = spawnSync(process.execPath, [join(scripts, 'publish-review.mjs')], { encoding: 'utf8', env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, GH_TOKEN: 'fixture-private-token', FEEDBACK_FIXTURE: config, REVIEW_FILE: review, REPO: repository, PR: '91', HEAD_SHA: head, APP_SLUG: f.options.appSlug, EXPECTED_LOGIN: f.options.login } });
    assert.equal(run.status, fail ? 1 : 0, run.stderr);
    const calls = readFileSync(log, 'utf8').trim().split('\n').map(JSON.parse);
    const posts = calls.filter(call => call.args[2] === 'POST');
    assert.equal(posts.length, 1);
    assert.deepEqual(posts[0].args, ['api', '--method', 'POST', `${f.endpoint}/reviews`, '--input', '-']);
    assert.deepEqual(posts[0].body.comments, f.options.review.comments);
    assert.equal((run.stdout + run.stderr + JSON.stringify(calls)).includes('fixture-private-token'), false);
    if (fail) assert.match(run.stderr, /Inspect the PR before retrying/);
  }
});
