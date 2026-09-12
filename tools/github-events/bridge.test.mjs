import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync } from 'node:fs';
import { join } from 'node:path';
import { openState, subscribe, deliver } from './bridge.mjs';
import * as bridge from './bridge.mjs';

const first = '11111111-1111-1111-1111-111111111111';
const second = '22222222-2222-2222-2222-222222222222';
const now = 1700000000000;
const event = (id, pr = 84, extra = {}) => ({ id, received: now + 1, metadata: { repository: 'iekip95mod-arch/cx2-ag', event: 'workflow_run', prs: [pr], sha: 'a'.repeat(40), workflow: 'check', conclusion: 'success', ...extra } });

test('routes only subscribed PRs and event types to each requesting task', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['workflow_run'], now);
  subscribe(db, second, 85, ['pull_request_review'], now);
  const queued = [];
  await deliver(db, [event(1), event(2, 85), event(3, 85, { event: 'pull_request_review', review: 'approved' })], async (...args) => queued.push(args), now + 2);
  assert.equal(queued.length, 2);
  assert.equal(queued[0][0], first);
  assert.equal(queued[1][0], second);
  assert.match(queued[0][1], /pull\/84/);
  assert.doesNotMatch(queued[0][1], /pull\/85/);
  assert.deepEqual(queued[0][1].split('\n').filter(line => /^(workflow_run|pull_request_review):/.test(line)), [`workflow_run: check: success, commit ${'a'.repeat(40)}`]);
  assert.deepEqual(queued[1][1].split('\n').filter(line => /^(workflow_run|pull_request_review):/.test(line)), [`pull_request_review: review approved, commit ${'a'.repeat(40)}`]);
  assert.deepEqual(db.prepare('SELECT event, thread FROM delivered ORDER BY event, thread').all().map(row => ({ ...row })), [{ event: 1, thread: first }, { event: 3, thread: second }]);
  db.close();
});
test('unrelated PRs and excluded event types queue nothing before a permitted review', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['pull_request_review'], now);
  const queued = [];
  await deliver(db, [event(1, 85, { event: 'pull_request_review', review: 'approved' }), event(2)], async (...args) => queued.push(args), now + 2);
  assert.deepEqual(queued, []);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM pending').get().count, 0);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM delivered').get().count, 0);
  await deliver(db, [event(3, 84, { event: 'pull_request_review', review: 'approved' })], async (...args) => queued.push(args), now + 2);
  assert.equal(queued.length, 1);
  assert.equal(queued[0][0], first);
  assert.match(queued[0][1], /pull_request_review: review approved/);
  assert.deepEqual(db.prepare('SELECT event FROM delivered').all().map(row => row.event), [3]);
  db.close();
});
test('offline backlog and cursor survive process restart', async () => {
  const directory = mkdtempSync(join(process.env.TEST_WORKSPACE, 'bridge-'));
  const filename = join(directory, 'state.sqlite');
  let db = openState(filename);
  subscribe(db, first, 84, ['workflow_run'], now);
  db.close();
  db = openState(filename);
  const queued = [];
  await deliver(db, [event(1)], async (...args) => queued.push(args), now + 100000);
  db.close();
  db = openState(filename);
  assert.equal(db.prepare('SELECT id FROM cursor').get().id, 1);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM delivered').get().count, 1);
  assert.equal(queued.length, 1);
  db.close();
});
test('failed task delivery retries without repeating successful task delivery', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['workflow_run'], now);
  subscribe(db, second, 84, ['workflow_run'], now);
  const queued = [];
  await deliver(db, [event(1)], async thread => { if (thread === second) throw Error('queue failed'); queued.push(thread); }, now + 2);
  assert.equal(db.prepare('SELECT id FROM cursor').get().id, 1);
  await deliver(db, [], async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [first, second]);
  db.close();
});
test('unavailable first task cannot block healthy tasks or later inbox pages', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['workflow_run'], now);
  subscribe(db, second, 85, ['workflow_run'], now);
  const queued = [];
  const queue = async thread => { if (thread === first) throw Error('task unavailable'); queued.push(thread); };
  await deliver(db, [event(1), event(2, 85)], queue, now + 2);
  bridge.consume(db, second, now + 2);
  await deliver(db, [event(3, 85)], queue, now + 2);
  assert.deepEqual(queued, [second, second]);
  assert.equal(db.prepare('SELECT id FROM cursor').get().id, 3);
  await deliver(db, [], async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [second, second, first]);
  db.close();
});
test('pending delivery survives restart after the inbox cursor advances', async () => {
  const directory = mkdtempSync(join(process.env.TEST_WORKSPACE, 'pending-'));
  const filename = join(directory, 'state.sqlite');
  let db = openState(filename);
  subscribe(db, first, 84, ['workflow_run'], now);
  const outcome = await deliver(db, [event(1)], async () => { throw Error('queue unavailable'); }, now + 2);
  assert.equal(outcome.failed, 1);
  db.close();
  db = openState(filename);
  assert.equal(db.prepare('SELECT id FROM cursor').get().id, 1);
  const queued = [];
  await deliver(db, [], async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [first]);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM pending').get().count, 0);
  db.close();
});
test('old, expired and closed subscriptions do not cause later wake-ups', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['workflow_run', 'pull_request'], now);
  const queued = [];
  await deliver(db, [{ ...event(1), received: now - 1 }], async thread => queued.push(thread), now + 2);
  assert.equal(queued.length, 0);
  await deliver(db, [event(2, 84, { event: 'pull_request', action: 'closed', merged: true })], async thread => queued.push(thread), now + 2);
  await deliver(db, [event(3)], async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [first]);
  subscribe(db, first, 84, ['workflow_run'], now);
  await deliver(db, [event(4)], async thread => queued.push(thread), now + 15 * 86400000);
  assert.equal(queued.length, 1);
  db.close();
});
test('invalid subscriptions and unordered inbox batches fail', async () => {
  const db = openState(':memory:');
  assert.throws(() => subscribe(db, '--unexpected-option', 84));
  assert.throws(() => subscribe(db, first, -1));
  assert.throws(() => subscribe(db, first, 84, ['unknown']));
  await assert.rejects(deliver(db, [event(2), event(1)], async () => {}, now));
  db.close();
});

test('bursts keep one outstanding wake-up per task until it is consumed', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['workflow_run'], now);
  subscribe(db, second, 85, ['workflow_run'], now);
  const queued = [];
  const queue = async thread => queued.push(thread);
  await deliver(db, [event(1)], queue, now + 2);
  await deliver(db, [event(2), event(3)], queue, now + 2);
  await deliver(db, [event(4, 85)], queue, now + 2);
  assert.deepEqual(queued, [first, second]);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM pending WHERE thread=?').get(first).count, 2);
  const consumed = bridge.consume(db, first, now + 2);
  assert.match(consumed.message, /pull\/84/);
  assert.equal(consumed.more, false);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM pending WHERE thread=?').get(first).count, 0);
  await deliver(db, [event(5)], queue, now + 2);
  assert.deepEqual(queued, [first, second, first]);
  db.close();
});

test('outstanding wake-ups survive restart and a large backlog drains before rearming', async () => {
  const directory = mkdtempSync(join(process.env.TEST_WORKSPACE, 'wakeups-'));
  const filename = join(directory, 'state.sqlite');
  let db = openState(filename);
  subscribe(db, first, 84, ['workflow_run'], now);
  const queued = [];
  await deliver(db, [event(1)], async thread => queued.push(thread), now + 2);
  db.close();
  db = openState(filename);
  await deliver(db, Array.from({ length: 100 }, (_, index) => event(index + 2)), async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [first]);
  assert.equal(bridge.consume(db, first, now + 2).more, true);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM wakeups').get().count, 1);
  assert.equal(bridge.consume(db, first, now + 2).more, false);
  assert.equal(db.prepare('SELECT COUNT(*) AS count FROM delivered').get().count, 101);
  await deliver(db, [event(102)], async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [first, first]);
  db.close();
});

test('consuming a wake-up before queue confirmation does not leave the task held', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['workflow_run'], now);
  const queued = [];
  await deliver(db, [event(1)], async thread => { queued.push(thread); bridge.consume(db, thread, now + 2); }, now + 2);
  await deliver(db, [event(2)], async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [first, first]);
  db.close();
});

test('startup retries an interrupted queue attempt without clearing confirmed wake-ups', async () => {
  const db = openState(':memory:');
  subscribe(db, first, 84, ['workflow_run'], now);
  subscribe(db, second, 85, ['workflow_run'], now);
  await deliver(db, [event(1)], async () => {}, now + 2);
  db.prepare('INSERT INTO pending VALUES(?, ?, ?)').run(2, second, JSON.stringify(event(2, 85)));
  db.prepare('INSERT INTO wakeups VALUES(?, 0)').run(second);
  bridge.recover(db);
  const queued = [];
  await deliver(db, [], async thread => queued.push(thread), now + 2);
  assert.deepEqual(queued, [second]);
  assert.equal(db.prepare('SELECT confirmed FROM wakeups WHERE thread=?').get(first).confirmed, 1);
  db.close();
});
