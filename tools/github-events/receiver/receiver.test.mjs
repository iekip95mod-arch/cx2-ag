import test from 'node:test';
import assert from 'node:assert/strict';
import { createHmac, randomUUID } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync, readdirSync } from 'node:fs';
import worker, { verify, normalize } from './receiver.mjs';

const secret = 'test-webhook-secret';
const repository = { full_name: 'iekip95mod-arch/cx2-ag' };
const sha = 'a'.repeat(40);
const review = { repository, action: 'submitted', pull_request: { number: 84, head: { sha } }, review: { state: 'approved', body: 'Untrusted review instructions' } };
function database() {
  const db = new DatabaseSync(':memory:');
  for (const file of readdirSync(new URL('./drizzle/', import.meta.url)).filter(file => file.endsWith('.sql'))) db.exec(readFileSync(new URL(`./drizzle/${file}`, import.meta.url), 'utf8'));
  return { prepare(sql) { return { bind(...args) { return { async run() { return db.prepare(sql).run(...args); }, async all() { return { results: db.prepare(sql).all(...args) }; } }; } }; } };
}
function request(body = review, delivery = randomUUID(), changes = {}) {
  const bytes = JSON.stringify(body);
  return new Request('https://receiver.test/github', { method: 'POST', body: bytes, headers: { 'x-github-event': 'pull_request_review', 'x-github-delivery': delivery, 'x-hub-signature-256': `sha256=${createHmac('sha256', secret).update(bytes).digest('hex')}`, ...changes } });
}
test('GitHub signature vector and tampering', async () => {
  const signature = 'sha256=757107ea0eb2509fc211221cce984b8a37570b6d7586c22c46f4379c8b043e17';
  assert.equal(await verify("It's a Secret to Everybody", new TextEncoder().encode('Hello, World!'), signature), true);
  assert.equal(await verify(secret, new TextEncoder().encode('Hello, World!'), signature), false);
  assert.equal(await verify(secret, new Uint8Array(), 'sha256=00'), false);
});
test('signed deliveries persist once, metadata only, across requests', async () => {
  const env = { DB: database(), WEBHOOK_SECRET: secret, BRIDGE_TOKEN: 'test-reader' };
  const delivery = randomUUID();
  assert.equal((await worker.fetch(request(review, delivery), env)).status, 202);
  assert.equal((await worker.fetch(request(review, delivery), env)).status, 202);
  const response = await worker.fetch(new Request('https://receiver.test/events?wait=0', { headers: { authorization: 'Bearer test-reader' } }), env);
  const { events } = await response.json();
  assert.equal(events.length, 1);
  assert.deepEqual(events[0].metadata.prs, [84]);
  assert.equal(JSON.stringify(events).includes('Untrusted'), false);
  const later = await worker.fetch(new Request(`https://receiver.test/events?wait=0&after=${events[0].id}`, { headers: { authorization: 'Bearer test-reader' } }), env);
  assert.deepEqual((await later.json()).events, []);
});
test('unauthorized, malformed and unrelated deliveries cannot enter the inbox', async () => {
  const env = { DB: database(), WEBHOOK_SECRET: secret, BRIDGE_TOKEN: 'test-reader' };
  assert.equal((await worker.fetch(request(review, randomUUID(), { 'x-hub-signature-256': 'wrong' }), env)).status, 401);
  assert.equal((await worker.fetch(request(review, 'bad-id'), env)).status, 400);
  assert.equal((await worker.fetch(new Request('https://receiver.test/events'), env)).status, 401);
  assert.equal((await worker.fetch(request({ ...review, repository: { full_name: 'unrelated/repository' } }), env)).status, 202);
  const unread = await worker.fetch(new Request('https://receiver.test/events?wait=0', { headers: { authorization: 'Bearer test-reader' } }), env);
  assert.deepEqual((await unread.json()).events, []);
  assert.equal((await worker.fetch(new Request('https://receiver.test/github', { method: 'POST', body: 'x'.repeat(1048577) }), env)).status, 413);
});
test('only relevant CI and review events are retained', () => {
  const run = { repository, action: 'completed', workflow_run: { id: 123, head_sha: sha, name: 'check', path: '.github/workflows/check.yml', conclusion: 'failure', pull_requests: [{ number: 84 }, { number: 85 }] } };
  assert.deepEqual(normalize('workflow_run', run).prs, [84, 85]);
  assert.equal(normalize('workflow_run', { ...run, action: 'in_progress' }), null);
  assert.equal(normalize('workflow_run', { ...run, workflow_run: { ...run.workflow_run, path: '.github/workflows/unrelated.yml' } }), null);
  assert.equal(normalize('pull_request_review', { ...review, review: { state: 'commented' } }), null);
  assert.equal(normalize('pull_request', { ...review, action: 'closed', pull_request: { ...review.pull_request, merged: true } }).merged, true);
});
test('storage errors reject delivery for redelivery', async () => {
  const env = { WEBHOOK_SECRET: secret, DB: { prepare() { throw Error('unavailable'); } } };
  assert.equal((await worker.fetch(request(), env)).status, 503);
});

test('review answers retain only routing metadata and ignore ordinary discussion', async () => {
  const env = { DB: database(), WEBHOOK_SECRET: secret, BRIDGE_TOKEN: 'test-reader' };
  const bot = { login: 'cx2-ag-codex-review-aegis[bot]', id: 328534539, type: 'Bot' };
  const answer = { repository, action: 'created', pull_request: review.pull_request, sender: bot, comment: { id: 99, in_reply_to_id: 70, user: bot, body: '<!-- cx2-review-answer:88 -->\nUntrusted answer text' } };
  assert.deepEqual(normalize('pull_request_review_comment', answer), { repository: repository.full_name, event: 'pull_request_review_comment', action: 'created', prs: [84], sha, comment: 99, question: 88, reviewer: bot.login });
  for (const change of [
    item => { item.action = 'edited'; },
    item => { item.comment.body = '/ask-reviewer What does this mean?'; },
    item => { item.comment.body = '<!-- cx2-review-answer:0 -->\nAnswer'; },
    item => { item.comment.in_reply_to_id = undefined; },
    item => { item.comment.user = { ...bot, type: 'User' }; },
    item => { item.comment.user = { ...bot, login: 'cx2-ag-codex-amber[bot]' }; },
    item => { item.sender = { ...bot, id: 1 }; },
  ]) {
    const invalid = structuredClone(answer); change(invalid);
    assert.equal(normalize('pull_request_review_comment', invalid), null);
  }
  const response = await worker.fetch(request(answer, randomUUID(), { 'x-github-event': 'pull_request_review_comment' }), env);
  assert.equal(response.status, 202);
  const inbox = await worker.fetch(new Request('https://receiver.test/events?wait=0', { headers: { authorization: 'Bearer test-reader' } }), env);
  const stored = await inbox.json();
  assert.equal(stored.events.length, 1);
  assert.equal(JSON.stringify(stored).includes('Untrusted answer text'), false);
});

test('workflow paths route renamed review runs and reject unrelated workflows', () => {
  for (const name of ['Review PR #85 (requested)', 'Review PR #85 (metadata)']) {
    const run = { repository, action: 'completed', workflow_run: { id: 34719249175, head_sha: sha, name, path: '.github/workflows/agent-review.yml', conclusion: 'success', pull_requests: [{ number: 85 }] } };
    assert.equal(normalize('workflow_run', run)?.workflow, 'agent-review');
    assert.equal(normalize('workflow_run', { ...run, workflow_run: { ...run.workflow_run, name: 'agent-review', path: '.github/workflows/unrelated.yml' } }), null);
    assert.equal(normalize('workflow_run', { ...run, workflow_run: { ...run.workflow_run, name: 'agent-review', path: undefined } }), null);
  }
});

test('discussion failures notify without duplicating successful answer notifications', () => {
  const delivery = { repository, action: 'completed', workflow_run: { id: 123, head_sha: sha, path: '.github/workflows/agent-review-discussion.yml', conclusion: 'failure', pull_requests: [{ number: 84 }] } };
  assert.equal(normalize('workflow_run', delivery)?.workflow, 'agent-review-discussion');
  for (const conclusion of ['success', 'neutral', 'skipped']) assert.equal(normalize('workflow_run', { ...delivery, workflow_run: { ...delivery.workflow_run, conclusion } }), null);
});

test('every supported notification route persists its exact metadata', async () => {
  const env = { DB: database(), WEBHOOK_SECRET: secret, BRIDGE_TOKEN: 'test-reader' };
  const cases = [];
  for (const name of ['check', 'agent-review-request', 'agent-review', 'agent-review-feedback']) {
    for (const conclusion of ['success', 'failure', 'cancelled', 'timed_out', 'action_required', 'neutral', 'skipped', 'stale', 'startup_failure']) {
      cases.push({ event: 'workflow_run', body: { repository, action: 'completed', workflow_run: { id: 123, head_sha: sha, name: name === 'agent-review' ? 'Review PR #84 (requested)' : name, path: `.github/workflows/${name}.yml`, conclusion, pull_requests: [{ number: 84 }] } }, expected: { repository: repository.full_name, event: 'workflow_run', action: 'completed', prs: [84], sha, workflow: name, conclusion, run: 123 } });
    }
  }
  for (const state of ['approved', 'changes_requested', 'dismissed']) {
    const action = state === 'dismissed' ? 'dismissed' : 'submitted';
    cases.push({ event: 'pull_request_review', body: { ...review, action, review: { state } }, expected: { repository: repository.full_name, event: 'pull_request_review', action, prs: [84], sha, review: state } });
  }
  for (const [action, merged] of [['synchronize', false], ['closed', false], ['closed', true]]) {
    cases.push({ event: 'pull_request', body: { ...review, action, pull_request: { ...review.pull_request, merged } }, expected: { repository: repository.full_name, event: 'pull_request', action, prs: [84], sha, merged } });
  }
  for (const route of cases) {
    const response = await worker.fetch(request(route.body, randomUUID(), { 'x-github-event': route.event }), env);
    assert.equal(response.status, 202);
    assert.deepEqual(await response.json(), { accepted: true }, JSON.stringify(route.expected));
  }
  const inbox = await worker.fetch(new Request('https://receiver.test/events?wait=0', { headers: { authorization: 'Bearer test-reader' } }), env);
  assert.deepEqual((await inbox.json()).events.map(event => event.metadata), cases.map(route => route.expected));
});
