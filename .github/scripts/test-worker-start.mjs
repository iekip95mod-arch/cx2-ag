import assert from 'node:assert/strict';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { announceWorker } from './worker-start.mjs';
import { loadRoster } from './bot-identities.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
function fixture(provider = 'codex', state = 'CHANGES_REQUESTED') {
  const roster = loadRoster();
  const executor = roster.find(bot => bot.provider === provider && bot.role === 'executor');
  const reviewer = roster.find(bot => bot.provider === provider && bot.role === 'reviewer');
  const context = { repository, issue: 42, branch: `${provider}/issue-42`, login: executor.login, userId: executor.userId };
  const head = 'a'.repeat(40);
  const options = { context, task: `Continue the existing issue lease and branch for PR #90, review 1234, head ${head}.`, model: provider === 'codex' ? 'gpt-5.6-sol' : 'opus', effort: 'high', run: 123 };
  const responses = {
    [`repos/${repository}/issues/42`]: { state: 'open' },
    [`repos/${repository}/pulls/90`]: { state: 'open', draft: false, user: { login: context.login }, head: { sha: head, ref: context.branch, repo: { full_name: repository } } },
    [`repos/${repository}/pulls/90/reviews/1234`]: { id: 1234, state, commit_id: head, user: { login: reviewer.login, id: reviewer.userId, type: 'Bot' }, submitted_at: '2026-09-12T12:00:00Z' },
    [`repos/${repository}/issues/90/comments`]: {},
    [`repos/${repository}/contents/assignments.json?ref=bot-assignments`]: { sha: 'fixture', content: Buffer.from(JSON.stringify({ version: 1, assignments: [{ key: `${provider}/reviewer/issue-42`, provider, role: 'reviewer', issue: 42, pr: 90, branch: context.branch, slug: reviewer.slug, released: false }] })).toString('base64') },
  };
  responses[`repos/${repository}/pulls/90/reviews?per_page=100&page=1`] = [responses[`repos/${repository}/pulls/90/reviews/1234`]];
  const calls = [];
  const api = async (method, endpoint, body) => {
    calls.push({ method, endpoint, body });
    if (method === 'GET' && /\/issues\/[0-9]+\/comments\?/.test(endpoint)) return [];
    return responses[endpoint] ?? {};
  };
  return { options, responses, calls, api };
}

test('CI continuations acknowledge on their PR and skip already recovered runs', async () => {
  const f = fixture();
  const head = 'a'.repeat(40);
  f.options.task = `Continue the existing issue lease and branch for PR #90, CI run 77, attempt 1, head ${head}.`;
  const ci = { id: 77, run_attempt: 1, head_sha: head, head_repository: { full_name: repository }, event: 'pull_request', status: 'completed', conclusion: 'failure' };
  f.responses[`repos/${repository}/actions/runs/77`] = ci;
  assert.equal(await announceWorker(f.options, f.api), true);
  assert.match(f.calls.find(call => call.method === 'POST').body.body, /Beginning to investigate the failed jobs/);
  ci.conclusion = 'success';
  assert.equal(await announceWorker(f.options, f.api), false);
});

test('both executors acknowledge verified CodeQL findings on the PR', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) {
    const f = fixture(provider, 'COMMENTED');
    const review = f.responses[`repos/${repository}/pulls/90/reviews/1234`];
    review.user = { login: 'github-advanced-security[bot]', id: 62310815, type: 'Bot' };
    f.responses[`repos/${repository}/pulls/90/reviews/1234/comments?per_page=100&page=1`] = [{ id: 77, pull_request_review_id: 1234, commit_id: review.commit_id, user: review.user, body: `https://github.com/${repository}/security/code-scanning/285` }];
    assert.equal(await announceWorker(f.options, f.api), true);
    const sent = f.calls.filter(call => call.method === 'POST');
    assert.equal(sent[0].endpoint, `repos/${repository}/issues/90/comments`);
    assert.match(sent[0].body.body, /Beginning to investigate CodeQL/);
    review.user.id++;
    assert.equal(await announceWorker(f.options, f.api), false);
  }
});

test('both executors acknowledge the review with the configured model, effort and review link', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) for (const state of ['APPROVED', 'CHANGES_REQUESTED']) {
    const f = fixture(provider, state);
    assert.equal(await announceWorker(f.options, f.api), true);
    const sent = f.calls.filter(call => call.method === 'POST');
    assert.equal(sent.length, 1);
    assert.equal(sent[0].endpoint, `repos/${repository}/issues/90/comments`);
    assert.match(sent[0].body.body, state === 'APPROVED' ? /Beginning the merge checks/ : /Beginning to address/);
    assert.match(sent[0].body.body, /pull\/90#pullrequestreview-1234/);
    assert.ok(sent[0].body.body.includes(`Configured model: ${f.options.model}`));
    assert.match(sent[0].body.body, /Configured effort: high/);
    if (provider === 'claude') assert.match(sent[0].body.body, /configured alias, resolved model unverified/);
  }
});

test('a normal assigned run announces on its issue without pretending to handle a review', async () => {
  const f = fixture();
  f.options.task = 'Implement the assigned issue';
  assert.equal(await announceWorker(f.options, f.api), true);
  const sent = f.calls.find(call => call.method === 'POST');
  assert.equal(sent.endpoint, `repos/${repository}/issues/42/comments`);
  assert.match(sent.body.body, /Starting the assigned executor run/);
  assert.equal(sent.body.body.includes('review #'), false);
});

test('draft repairs acknowledge but draft approvals cannot start merge work', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) for (const state of ['APPROVED', 'CHANGES_REQUESTED']) {
    const f = fixture(provider, state);
    f.responses[`repos/${repository}/pulls/90`].draft = true;
    assert.equal(await announceWorker(f.options, f.api), state === 'CHANGES_REQUESTED');
  }
});

test('a newer blocked review prevents stale startup acknowledgement', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) {
    const f = fixture(provider);
    const delivered = f.responses[`repos/${repository}/pulls/90/reviews/1234`];
    f.responses[`repos/${repository}/pulls/90/reviews?per_page=100&page=1`].push({ ...delivered, id: 1235, state: 'COMMENTED', body: '<!-- review-blocked -->', submitted_at: '2026-09-12T12:01:00Z' });
    assert.equal(await announceWorker(f.options, f.api), false);
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
});

test('closed and stale review work never receives a start acknowledgement', async () => {
  for (const change of [
    f => { f.responses[`repos/${repository}/issues/42`].state = 'closed'; },
    f => { f.responses[`repos/${repository}/pulls/90`].head.sha = 'b'.repeat(40); },
    f => { f.responses[`repos/${repository}/pulls/90/reviews/1234`].state = 'DISMISSED'; },
  ]) {
    const f = fixture(); change(f);
    assert.equal(await announceWorker(f.options, f.api), false);
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
});

test('queued verdicts superseded before worker startup receive no acknowledgement', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) for (const state of ['CHANGES_REQUESTED', 'APPROVED']) {
    const f = fixture(provider, state);
    const delivered = f.responses[`repos/${repository}/pulls/90/reviews/1234`];
    f.responses[`repos/${repository}/pulls/90/reviews?per_page=100&page=1`].push({ ...delivered, id: 1235, state: state === 'APPROVED' ? 'CHANGES_REQUESTED' : 'APPROVED', submitted_at: '2026-09-12T12:01:00Z' });
    assert.equal(await announceWorker(f.options, f.api), false);
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
    assert.equal(f.calls.some(call => call.endpoint.includes('/reviews?')), true);
  }
});

test('an unleased or foreign reviewer cannot receive an executor acknowledgement', async () => {
  for (const changed of ['login', 'id', 'type', 'lease']) {
    const f = fixture();
    const review = f.responses[`repos/${repository}/pulls/90/reviews/1234`];
    if (changed === 'login') review.user.login = 'foreign[bot]';
    if (changed === 'id') review.user.id++;
    if (changed === 'type') review.user.type = 'User';
    if (changed === 'lease') f.responses[`repos/${repository}/contents/assignments.json?ref=bot-assignments`].content = Buffer.from(JSON.stringify({ version: 1, assignments: [] })).toString('base64');
    assert.equal(await announceWorker(f.options, f.api), false);
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
});

test('startup verdict validation reads later pages and fails closed at its history cap', async () => {
  for (const oversized of [false, true]) {
    const f = fixture();
    const review = f.responses[`repos/${repository}/pulls/90/reviews/1234`];
    for (let page = 1; page <= (oversized ? 10 : 1); page++) f.responses[`repos/${repository}/pulls/90/reviews?per_page=100&page=${page}`] = Array.from({ length: 100 }, () => ({ ...review, state: 'COMMENTED' }));
    f.responses[`repos/${repository}/pulls/90/reviews?per_page=100&page=2`] = oversized ? f.responses[`repos/${repository}/pulls/90/reviews?per_page=100&page=1`] : [review, { ...review, id: 1200, state: 'APPROVED', submitted_at: '2026-09-12T12:01:00Z' }];
    if (oversized) await assert.rejects(announceWorker(f.options, f.api), /history.*limit/);
    else assert.equal(await announceWorker(f.options, f.api), false);
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
});

test('the actual entry point posts with configured metadata without printing its credential', () => {
  const f = fixture('claude');
  const workspace = fileURLToPath(new URL('../../.Internal/workspaces/', import.meta.url));
  mkdirSync(workspace, { recursive: true });
  const root = mkdtempSync(join(workspace, 'worker-start-'));
  const bin = join(root, 'bin'); mkdirSync(bin);
  copyFileSync(new URL('./fixtures/review-feedback-gh.mjs', import.meta.url), join(bin, 'gh'));
  chmodSync(join(bin, 'gh'), 0o755);
  const event = join(root, 'event.json');
  const context = join(root, 'context.json');
  const config = join(root, 'fixture.json');
  const log = join(root, 'calls.jsonl');
  writeFileSync(event, JSON.stringify({ inputs: { task: f.options.task } }));
  writeFileSync(context, JSON.stringify(f.options.context));
  f.responses[`repos/${repository}/issues/90/comments?per_page=100&page=1`] = [];
  writeFileSync(config, JSON.stringify({ responses: f.responses, log }));
  const env = { PATH: `${bin}:${dirname(process.execPath)}:${process.env.PATH}`, GH_TOKEN: 'executor-start-fixture-token', GITHUB_EVENT_PATH: event, GITHUB_RUN_ID: '123', GITHUB_RUN_ATTEMPT: '1', WORKER_MODEL: 'opus', WORKER_EFFORT: 'high', FEEDBACK_FIXTURE: config, GITHUB_OUTPUT: join(root, 'outputs') };
  const execution = spawnSync(process.execPath, [fileURLToPath(new URL('./worker-start.mjs', import.meta.url)), context], { env, encoding: 'utf8' });
  assert.equal(execution.status, 0, execution.stderr);
  const calls = readFileSync(log, 'utf8').trim().split('\n').map(line => JSON.parse(line));
  assert.equal(calls.filter(call => call.args[2] === 'POST').length, 1);
  assert.match(calls.at(-1).body.body, /Configured effort: high/);
  assert.equal((execution.stdout + execution.stderr).includes(env.GH_TOKEN), false);
  assert.equal(readFileSync(env.GITHUB_OUTPUT, 'utf8'), 'proceed=true\n');
  f.responses[`repos/${repository}/issues/42`].state = 'closed';
  writeFileSync(config, JSON.stringify({ responses: f.responses, log }));
  const stopped = spawnSync(process.execPath, [fileURLToPath(new URL('./worker-start.mjs', import.meta.url)), context], { env, encoding: 'utf8' });
  assert.equal(stopped.status, 0, stopped.stderr);
  assert.equal(readFileSync(env.GITHUB_OUTPUT, 'utf8'), 'proceed=true\nproceed=false\n');
});
