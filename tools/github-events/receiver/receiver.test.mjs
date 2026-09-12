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
  const run = { repository, action: 'completed', workflow_run: { id: 123, head_sha: sha, name: 'check', conclusion: 'failure', pull_requests: [{ number: 84 }, { number: 85 }] } };
  assert.deepEqual(normalize('workflow_run', run).prs, [84, 85]);
  assert.equal(normalize('workflow_run', { ...run, action: 'in_progress' }), null);
  assert.equal(normalize('workflow_run', { ...run, workflow_run: { ...run.workflow_run, name: 'unrelated' } }), null);
  assert.equal(normalize('pull_request_review', { ...review, review: { state: 'commented' } }), null);
  assert.equal(normalize('pull_request', { ...review, action: 'closed', pull_request: { ...review.pull_request, merged: true } }).merged, true);
});
test('storage errors reject delivery for redelivery', async () => {
  const env = { WEBHOOK_SECRET: secret, DB: { prepare() { throw Error('unavailable'); } } };
  assert.equal((await worker.fetch(request(), env)).status, 503);
});
