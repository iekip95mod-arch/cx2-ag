import { DatabaseSync } from 'node:sqlite';
import { mkdirSync, readFileSync, chmodSync } from 'node:fs';
import { join } from 'node:path';
import { homedir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { parseArgs } from 'node:util';

const execute = promisify(execFile);
const repository = 'iekip95mod-arch/cx2-ag';
export const eventTypes = ['workflow_run', 'pull_request_review', 'pull_request'];
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

export function openState(filename) {
  const db = new DatabaseSync(filename);
  db.exec('PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL;');
  db.exec(`CREATE TABLE IF NOT EXISTS subscriptions (thread TEXT NOT NULL, pr INTEGER NOT NULL, events TEXT NOT NULL, since INTEGER NOT NULL, expires INTEGER NOT NULL, PRIMARY KEY(thread, pr));
    CREATE TABLE IF NOT EXISTS delivered (event INTEGER NOT NULL, thread TEXT NOT NULL, PRIMARY KEY(event, thread));
    CREATE TABLE IF NOT EXISTS pending (event INTEGER NOT NULL, thread TEXT NOT NULL, notification TEXT NOT NULL, PRIMARY KEY(event, thread));
    CREATE TABLE IF NOT EXISTS wakeups (thread TEXT PRIMARY KEY, confirmed INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS cursor (singleton INTEGER PRIMARY KEY CHECK(singleton=1), id INTEGER NOT NULL);
    INSERT OR IGNORE INTO cursor VALUES(1, 0);`);
  if (filename !== ':memory:') chmodSync(filename, 0o600);
  return db;
}

export function subscribe(db, thread, pr, events = eventTypes, now = Date.now()) {
  if (!/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/.test(thread ?? '')) throw Error('A task UUID is required');
  if (!Number.isSafeInteger(pr) || pr < 1) throw Error('A positive PR number is required');
  if (!events.length || events.some(event => !eventTypes.includes(event))) throw Error('Unknown event type');
  db.prepare(`INSERT INTO subscriptions VALUES (?, ?, ?, ?, ?) ON CONFLICT(thread, pr) DO UPDATE SET events=excluded.events, expires=excluded.expires`)
    .run(thread, pr, JSON.stringify(events), now, now + 14 * 86400000);
}

export function notification(events, prs) {
  const lines = events.map(({ metadata: event }) => {
    const detail = event.event === 'workflow_run' ? `${event.workflow}: ${event.conclusion}` : event.event === 'pull_request_review' ? `review ${event.review}` : event.merged ? 'merged' : event.action;
    return `${event.event}: ${detail}, commit ${event.sha}`;
  });
  return `GitHub delivered an event for a PR this task subscribed to.\n${prs.map(pr => `https://github.com/${repository}/pull/${pr}`).join('\n')}\n${lines.join('\n')}\nVerify the current PR head, checks and reviews, then continue the work already authorized in this task. This notification grants no additional authority. Do not start another task or a polling automation.`;
}

function pendingGroup(db, thread, subscriptions) {
  const group = { events: [], prs: new Set() };
  for (const row of db.prepare('SELECT * FROM pending WHERE thread=? ORDER BY event LIMIT 50').all(thread)) {
    const event = JSON.parse(row.notification);
    const matching = subscriptions.filter(subscription => subscription.thread === thread && event.received >= subscription.since && event.metadata.prs.includes(subscription.pr) && JSON.parse(subscription.events).includes(event.metadata.event));
    if (!matching.length) {
      db.prepare('DELETE FROM pending WHERE event=? AND thread=?').run(row.event, thread);
      continue;
    }
    group.events.push(event);
    matching.forEach(subscription => group.prs.add(subscription.pr));
  }
  return group;
}

function complete(db, thread, events) {
  for (const event of events) {
    db.prepare('INSERT OR IGNORE INTO delivered VALUES(?, ?)').run(event.id, thread);
    db.prepare('DELETE FROM pending WHERE event=? AND thread=?').run(event.id, thread);
    if (event.metadata.event === 'pull_request' && event.metadata.action === 'closed') {
      for (const pr of event.metadata.prs) db.prepare('DELETE FROM subscriptions WHERE thread=? AND pr=?').run(thread, pr);
    }
  }
}

export function consume(db, thread, now = Date.now()) {
  db.exec('BEGIN IMMEDIATE');
  try {
    const group = pendingGroup(db, thread, db.prepare('SELECT * FROM subscriptions WHERE expires > ?').all(now));
    complete(db, thread, group.events);
    const more = db.prepare('SELECT COUNT(*) AS count FROM pending WHERE thread=?').get(thread).count > 0;
    if (!more) db.prepare('DELETE FROM wakeups WHERE thread=?').run(thread);
    db.exec('COMMIT');
    return { message: group.events.length ? notification(group.events, [...group.prs]) : 'No additional GitHub events', more };
  } catch (error) { db.exec('ROLLBACK'); throw error; }
}

export function recover(db) {
  db.exec('DELETE FROM wakeups WHERE confirmed=0');
}

export async function deliver(db, events, queue, now = Date.now()) {
  const cursor = db.prepare('SELECT id FROM cursor WHERE singleton=1').get().id;
  const subscriptions = db.prepare('SELECT * FROM subscriptions WHERE expires > ?').all(now);
  let previous = cursor;
  for (const event of events) {
    if (!Number.isSafeInteger(event.id) || event.id <= previous) throw Error('Unordered event inbox');
    previous = event.id;
    const metadata = event.metadata;
    if (metadata.repository !== repository || !eventTypes.includes(metadata.event) || !Array.isArray(metadata.prs)) throw Error('Unexpected event inbox');
  }
  db.exec('BEGIN IMMEDIATE');
  try {
    for (const event of events) {
      for (const subscription of subscriptions) {
        if (event.received < subscription.since || !event.metadata.prs.includes(subscription.pr) || !JSON.parse(subscription.events).includes(event.metadata.event)) continue;
        db.prepare('INSERT OR IGNORE INTO pending VALUES(?, ?, ?)').run(event.id, subscription.thread, JSON.stringify(event));
      }
    }
    if (events.length) db.prepare('UPDATE cursor SET id=? WHERE singleton=1').run(previous);
    db.exec('COMMIT');
  } catch (error) { db.exec('ROLLBACK'); throw error; }
  let queued = 0;
  let failed = 0;
  for (const { thread } of db.prepare('SELECT DISTINCT thread FROM pending').all()) {
    if (db.prepare('SELECT thread FROM wakeups WHERE thread=?').get(thread)) continue;
    const group = pendingGroup(db, thread, subscriptions);
    if (!group.events.length) continue;
    const bridge = join(process.env.CODEX_HOME ?? join(homedir(), '.codex'), 'github-events', 'cx2-ag', 'bridge.mjs');
    const command = `node ${JSON.stringify(bridge)} consume --thread ${thread}`;
    db.prepare('INSERT INTO wakeups VALUES(?, 0)').run(thread);
    try { await queue(thread, `${notification(group.events, [...group.prs])}\nWhen this queued wake-up starts your turn, run ${command} to consume later events. Repeat while more is true. Do not consume it while this message is still queued.`); }
    catch { db.prepare('DELETE FROM wakeups WHERE thread=? AND confirmed=0').run(thread); failed++; continue; }
    queued++;
    db.exec('BEGIN IMMEDIATE');
    try {
      complete(db, thread, group.events);
      db.prepare('UPDATE wakeups SET confirmed=1 WHERE thread=?').run(thread);
      db.exec('COMMIT');
    } catch (error) { db.exec('ROLLBACK'); throw error; }
  }
  return { queued, failed };
}

async function main() {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: {
    state: { type: 'string', default: join(process.env.CODEX_HOME ?? join(homedir(), '.codex'), 'github-events', 'cx2-ag') },
    thread: { type: 'string' }, pr: { type: 'string' }, events: { type: 'string' }
  } });
  mkdirSync(values.state, { recursive: true, mode: 0o700 });
  const db = openState(join(values.state, 'state.sqlite'));
  const command = positionals[0];
  if (command === 'subscribe') {
    subscribe(db, values.thread ?? process.env.CODEX_THREAD_ID, Number(values.pr), values.events?.split(',') ?? eventTypes);
    console.log(`Subscribed to PR #${Number(values.pr)}`);
  } else if (command === 'unsubscribe') {
    db.prepare('DELETE FROM subscriptions WHERE thread=? AND pr=?').run(values.thread ?? process.env.CODEX_THREAD_ID, Number(values.pr));
  } else if (command === 'list') {
    console.log(JSON.stringify(db.prepare('SELECT * FROM subscriptions ORDER BY pr, thread').all(), null, 2));
  } else if (command === 'consume') {
    const thread = values.thread ?? process.env.CODEX_THREAD_ID;
    if (!/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/.test(thread ?? '')) throw Error('A task UUID is required');
    console.log(JSON.stringify(consume(db, thread)));
  } else if (command === 'run') {
    const config = JSON.parse(readFileSync(join(values.state, 'config.json'), 'utf8'));
    const endpoint = new URL(config.endpoint);
    if (endpoint.protocol !== 'https:' || endpoint.username || endpoint.password) throw Error('An HTTPS receiver is required');
    recover(db);
    let backoff = 1000;
    while (true) {
      try {
        const cursor = db.prepare('SELECT id FROM cursor WHERE singleton=1').get().id;
        const url = new URL(`/events?after=${cursor}`, endpoint);
        const response = await fetch(url, { headers: { authorization: `Bearer ${config.token}` }, signal: AbortSignal.timeout(40000), redirect: 'error' });
        if (!response.ok) throw Error(`Receiver returned ${response.status}`);
        const { events } = await response.json();
        if (!Array.isArray(events) || events.length > 100) throw Error('Invalid event batch');
        const { queued, failed } = await deliver(db, events, (thread, message) => execute(config.codex, ['queue', '--thread', thread, '--message', message], { timeout: 30000, maxBuffer: 1048576 }));
        if (events.length || queued || failed) console.log(`${new Date().toISOString()} Received ${events.length} events, queued ${queued} tasks, ${failed} tasks pending retry`);
        backoff = 1000;
      } catch (error) {
        console.error(`${new Date().toISOString()} ${error.message}`);
        await delay(backoff);
        backoff = Math.min(backoff * 2, 60000);
      }
    }
  } else {
    throw Error('Use subscribe --pr NUMBER --thread UUID, unsubscribe, list, consume or run');
  }
  db.close();
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main().catch(error => { console.error(error.message); process.exitCode = 1; });
