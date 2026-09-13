import assert from 'node:assert/strict';
import test from 'node:test';
import { publishProgress } from './agent-progress.mjs';

const base = {
  repository: 'iekip95mod-arch/cx2-ag', number: 97, role: 'executor',
  login: 'cx2-ag-codex-amber[bot]', userId: 100, model: 'gpt-5.6-sol',
  effort: 'high', run: 123, attempt: 1, detail: 'Working on the assigned branch.'
};

function fixture(comments = []) {
  const writes = [];
  const starts = new Map();
  const api = async (method, endpoint, body) => {
    const attempt = /\/actions\/runs\/(\d+)\/attempts\/(\d+)$/.exec(endpoint);
    if (attempt) return { run_started_at: new Date(starts.get(`${attempt[1]}:${attempt[2]}`) ?? Number(attempt[1]) * 1000).toISOString() };
    if (method === 'GET') return comments;
    writes.push({ method, endpoint, body });
    if (method === 'POST') comments.push({ id: 10, body: body.body, user: { login: base.login, id: base.userId, type: 'Bot' } });
    if (method === 'PATCH') comments[0].body = body.body;
    return comments[0];
  };
  return { comments, writes, api, starts };
}

test('a later retry of an older run replaces completed newer-run progress', async () => {
  const f = fixture();
  await publishProgress({ ...base, run: 201, phase: 'succeeded' }, f.api);
  f.starts.set('200:2', 202000);
  await publishProgress({ ...base, run: 200, attempt: 2, phase: 'running' }, f.api);
  assert.match(f.comments[0].body, /Status: \*\*Running\*\*/);
  assert.match(f.comments[0].body, /actions\/runs\/200\/attempts\/2/);
  await publishProgress({ ...base, run: 201, phase: 'succeeded' }, f.api);
  assert.match(f.comments[0].body, /actions\/runs\/200\/attempts\/2/);
});

test('one executor comment records the complete lifecycle and final failure', async () => {
  const f = fixture();
  for (const phase of ['queued', 'preparing', 'running', 'publishing', 'failed']) await publishProgress({ ...base, phase }, f.api);
  assert.equal(f.writes[0].method, 'POST');
  assert.deepEqual(f.writes.slice(1).map(write => write.method), ['PATCH', 'PATCH', 'PATCH', 'PATCH']);
  assert.equal(f.comments.length, 1);
  assert.match(f.comments[0].body, /Status: \*\*Failed\*\*/);
  assert.match(f.comments[0].body, /Configured model: gpt-5\.6-sol/);
  assert.match(f.comments[0].body, /actions\/runs\/123\/attempts\/1/);
});

test('reviewer progress is isolated from executor and foreign marker comments', async () => {
  const marker = '<!-- cx2-agent-progress:executor:cx2-ag-codex-amber[bot] -->';
  const foreign = { id: 1, body: marker, user: { login: 'foreign[bot]', id: 999, type: 'Bot' } };
  const executor = { id: 2, body: marker, user: { login: base.login, id: base.userId, type: 'Bot' } };
  const f = fixture([foreign, executor]);
  await publishProgress({ ...base, role: 'reviewer', login: 'cx2-ag-codex-review-aegis[bot]', userId: 200, model: 'opus', phase: 'queued' }, f.api);
  assert.equal(f.writes[0].method, 'POST');
  assert.match(f.writes[0].body.body, /cx2-agent-progress:reviewer:cx2-ag-codex-review-aegis/);
  assert.match(f.writes[0].body.body, /alias, resolved model unverified/);
});

test('a status update only edits a comment owned by the exact bot ID', async () => {
  const body = '<!-- cx2-agent-progress:executor:cx2-ag-codex-amber[bot] -->';
  const f = fixture([{ id: 4, body, user: { login: base.login, id: 999, type: 'Bot' } }]);
  await publishProgress({ ...base, phase: 'succeeded' }, f.api);
  assert.equal(f.writes[0].method, 'POST');
  await assert.rejects(publishProgress({ ...base, phase: 'running', detail: '@codex wake up' }, f.api), /controlled text/);
});

test('an owned progress comment with invalid workflow metadata fails closed', async () => {
  const marker = '<!-- cx2-agent-progress:executor:cx2-ag-codex-amber[bot] -->';
  const f = fixture([{ id: 5, body: marker, user: { login: base.login, id: base.userId, type: 'Bot' } }]);
  await assert.rejects(publishProgress({ ...base, phase: 'running' }, f.api), /Invalid existing progress workflow/);
  assert.equal(f.writes.length, 0);
});

test('terminal recovery never creates a status that did not start', async () => {
  const f = fixture();
  assert.equal(await publishProgress({ ...base, phase: 'cancelled', updateOnly: true }, f.api), false);
  assert.equal(f.writes.length, 0);
  await publishProgress({ ...base, phase: 'running' }, f.api);
  await publishProgress({ ...base, phase: 'cancelled', updateOnly: true }, f.api);
  assert.match(f.comments[0].body, /Status: \*\*Cancelled\*\*/);
  assert.match(f.comments[0].body, /\[x\] Agent execution started/);
  assert.match(f.comments[0].body, /\[ \] Publishing result/);
});

test('a new workflow attempt resets lifecycle checkmarks', async () => {
  const f = fixture();
  await publishProgress({ ...base, phase: 'succeeded' }, f.api);
  await publishProgress({ ...base, run: 124, attempt: 2, phase: 'queued' }, f.api);
  assert.match(f.comments[0].body, /Status: \*\*Queued\*\*/);
  assert.match(f.comments[0].body, /\[ \] Agent execution started/);
  assert.match(f.comments[0].body, /\[ \] Publishing result/);
  assert.match(f.comments[0].body, /actions\/runs\/124\/attempts\/2/);
});

test('an older workflow run or attempt cannot replace newer progress', async () => {
  const f = fixture();
  await publishProgress({ ...base, run: 200, attempt: 1, phase: 'running' }, f.api);
  await publishProgress({ ...base, run: 201, attempt: 1, phase: 'queued' }, f.api);
  const writesAfterNewRun = f.writes.length;
  await publishProgress({ ...base, run: 200, attempt: 1, phase: 'cancelled' }, f.api);
  assert.equal(f.writes.length, writesAfterNewRun);
  assert.match(f.comments[0].body, /Status: \*\*Queued\*\*/);
  assert.match(f.comments[0].body, /actions\/runs\/201\/attempts\/1/);

  await publishProgress({ ...base, run: 201, attempt: 2, phase: 'running' }, f.api);
  const writesAfterNewAttempt = f.writes.length;
  await publishProgress({ ...base, run: 201, attempt: 1, phase: 'cancelled' }, f.api);
  assert.equal(f.writes.length, writesAfterNewAttempt);
  assert.match(f.comments[0].body, /Status: \*\*Running\*\*/);
  assert.match(f.comments[0].body, /actions\/runs\/201\/attempts\/2/);
});
