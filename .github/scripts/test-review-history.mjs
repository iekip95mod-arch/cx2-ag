import assert from 'node:assert/strict';
import test from 'node:test';
import { chmodSync, copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { reviewHistory } from './review-history.mjs';

function fixture() {
  const options = { repository: 'iekip95mod-arch/cx2-ag', pr: 91, head: 'a'.repeat(40), base: 'b'.repeat(40) };
  const endpoint = `repos/${options.repository}/pulls/91`;
  const responses = {
    [`repos/${options.repository}/issues/91/comments?per_page=100&page=1`]: [{ id: 4, body: 'Executor verification', user: { login: 'executor[bot]' } }],
    [`repos/${options.repository}/actions/workflows/check.yml/runs?head_sha=${options.head}&event=pull_request&per_page=100&page=1`]: { workflow_runs: [{ id: 12, run_attempt: 2, head_sha: options.head, event: 'pull_request', head_repository: { full_name: options.repository }, pull_requests: [{ number: 91 }], status: 'completed', conclusion: 'success' }] },
    [`repos/${options.repository}/actions/runs/12/attempts/2/jobs?per_page=100&page=1`]: { total_count: 1, jobs: [{ name: 'full', conclusion: 'success', status: 'completed' }] },
    [endpoint]: { state: 'open', draft: false, changed_files: 2, head: { sha: options.head, repo: { full_name: options.repository } }, base: { sha: options.base } },
    [`${endpoint}/files?per_page=100&page=1`]: [{ filename: 'earlier.cc', status: 'modified' }, { filename: 'latest.cc', status: 'added' }],
    [`${endpoint}/reviews?per_page=100&page=1`]: [{ id: 1, commit_id: 'c'.repeat(40), state: 'CHANGES_REQUESTED', body: 'Earlier finding', user: { login: 'reviewer[bot]' } }],
    [`${endpoint}/comments?per_page=100&page=1`]: [{ id: 2, pull_request_review_id: 1, path: 'earlier.cc', body: 'Old finding' }, { id: 3, in_reply_to_id: 2, body: 'Fix claimed' }],
  };
  return { options, endpoint, responses, api: async path => structuredClone(responses[path]) };
}

test('review history includes earlier files, findings and executor replies', async () => {
  const f = fixture();
  const history = await reviewHistory(f.options, f.api);
  assert.deepEqual(history.files.map(file => file.path), ['earlier.cc', 'latest.cc']);
  assert.equal(history.reviews[0].head, 'c'.repeat(40));
  assert.equal(history.comments[1].replyTo, 2);
  assert.match(history.scope, /untrusted/);
  assert.equal(history.discussion[0].body, 'Executor verification');
  assert.equal(history.ci[0].attempt, 2);
  assert.equal(history.ci[0].jobs[0].conclusion, 'success');
});

test('CI evidence excludes other heads, repositories and PRs and refuses missing jobs', async () => {
  for (const change of [
    run => { run.head_sha = 'c'.repeat(40); },
    run => { run.head_repository.full_name = 'another/repository'; },
    run => { run.pull_requests = [{ number: 90 }]; },
    run => { run.event = 'workflow_dispatch'; },
  ]) {
    const f = fixture();
    change(f.responses[`repos/${f.options.repository}/actions/workflows/check.yml/runs?head_sha=${f.options.head}&event=pull_request&per_page=100&page=1`].workflow_runs[0]);
    assert.deepEqual((await reviewHistory(f.options, f.api)).ci, []);
  }
  const f = fixture();
  f.responses[`repos/${f.options.repository}/actions/runs/12/attempts/2/jobs?per_page=100&page=1`].total_count = 2;
  await assert.rejects(reviewHistory(f.options, f.api), /Incomplete CI/);
});

test('history paginates and refuses incomplete or superseded review snapshots', async () => {
  const f = fixture();
  f.responses[`${f.endpoint}/comments?per_page=100&page=1`] = Array.from({ length: 100 }, (_, id) => ({ id }));
  f.responses[`${f.endpoint}/comments?per_page=100&page=2`] = [{ id: 101 }];
  assert.equal((await reviewHistory(f.options, f.api)).comments.length, 101);
  let reads = 0;
  await assert.rejects(reviewHistory(f.options, async path => {
    const response = await f.api(path);
    if (path === f.endpoint && ++reads === 2) response.head.sha = 'd'.repeat(40);
    return response;
  }), /changed/);
  f.responses[f.endpoint].changed_files = 3;
  await assert.rejects(reviewHistory(f.options, f.api), /Incomplete/);
});

test('actual CLI accepts a valid cumulative file page larger than one MiB', () => {
  const f = fixture();
  f.responses[f.endpoint].changed_files = 100;
  f.responses[`${f.endpoint}/files?per_page=100&page=1`] = Array.from({ length: 100 }, (_, id) => ({ filename: `file-${id}.cc`, status: 'modified', patch: 'x'.repeat(13000) }));
  f.responses[`${f.endpoint}/files?per_page=100&page=2`] = [];
  const workspace = fileURLToPath(new URL('../../.Internal/workspaces/', import.meta.url));
  mkdirSync(workspace, { recursive: true });
  const root = mkdtempSync(join(workspace, 'review-history-'));
  const bin = join(root, 'bin'); mkdirSync(bin);
  copyFileSync(new URL('./fixtures/review-history-gh.mjs', import.meta.url), join(bin, 'gh')); chmodSync(join(bin, 'gh'), 0o755);
  const responses = join(root, 'responses.json'); writeFileSync(responses, JSON.stringify(f.responses));
  const env = { PATH: `${bin}:${dirname(process.execPath)}:${process.env.PATH}`, GH_TOKEN: 'history-fixture', HISTORY_FIXTURE: responses, RUNNER_TEMP: root, GITHUB_REPOSITORY: f.options.repository, PR_NUMBER: '91', HEAD_SHA: f.options.head, BASE_SHA: f.options.base };
  const execution = spawnSync(process.execPath, [fileURLToPath(new URL('./review-history.mjs', import.meta.url))], { env, encoding: 'utf8' });
  assert.equal(execution.status, 0, execution.stderr);
  assert.equal(JSON.parse(readFileSync(join(root, 'review-history.json'), 'utf8')).files.length, 100);
});
