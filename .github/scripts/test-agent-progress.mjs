import assert from 'node:assert/strict';
import test from 'node:test';
import { publishProgress } from './agent-progress.mjs';

const base = {
  repository: 'iekip95mod-arch/cx2-ag', number: 97, role: 'executor',
  login: 'cx2-ag-codex-amber[bot]', userId: 100, model: 'gpt-5.6-sol',
  effort: 'high', run: 123, attempt: 1, detail: 'Working on the assigned branch.'
};

function fixture(comments = [], identity = base) {
  const writes = [];
  const api = async (method, endpoint, body) => {
    const attempt = /\/actions\/runs\/(\d+)\/attempts\/(\d+)$/.exec(endpoint);
    if (attempt) return { run_started_at: new Date(Number(attempt[1]) * 1000).toISOString() };
    if (method === 'GET') return comments;
    writes.push({ method, endpoint, body });
    if (method === 'POST') {
      const comment = { id: 10 + comments.length, body: body.body, user: { login: identity.login, id: identity.userId, type: 'Bot' } };
      comments.push(comment);
      return comment;
    }
    const comment = comments.find(comment => endpoint.endsWith(`/comments/${comment.id}`));
    assert.ok(comment);
    comment.body = body.body;
    return comment;
  };
  return { comments, writes, api };
}

test('a later retry of an older run preserves completed newer-run progress', async () => {
  const f = fixture();
  await publishProgress({ ...base, run: 201, phase: 'succeeded' }, f.api);
  const completed = f.comments[0].body;
  await publishProgress({ ...base, run: 200, attempt: 2, phase: 'running' }, f.api);
  assert.equal(f.comments.length, 2);
  assert.equal(f.comments[0].body, completed);
  assert.match(f.comments[1].body, /Status: \*\*Running\*\*/);
  assert.match(f.comments[1].body, /actions\/runs\/200\/attempts\/2/);
  await publishProgress({ ...base, run: 201, phase: 'succeeded' }, f.api);
  assert.equal(f.writes.length, 2);
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
  assert.equal(await publishProgress({ ...base, run: 124, phase: 'cancelled', updateOnly: true }, f.api), false);
  assert.equal(f.writes.length, 1);
  await publishProgress({ ...base, phase: 'cancelled', updateOnly: true }, f.api);
  assert.match(f.comments[0].body, /Status: \*\*Cancelled\*\*/);
  assert.match(f.comments[0].body, /\[x\] Agent execution started/);
  assert.match(f.comments[0].body, /\[ \] Publishing result/);
});

test('a new workflow attempt resets lifecycle checkmarks', async () => {
  const f = fixture();
  await publishProgress({ ...base, phase: 'succeeded' }, f.api);
  await publishProgress({ ...base, run: 124, attempt: 2, phase: 'queued' }, f.api);
  assert.equal(f.comments.length, 2);
  assert.match(f.comments[0].body, /Status: \*\*Completed\*\*/);
  assert.match(f.comments[1].body, /Status: \*\*Queued\*\*/);
  assert.match(f.comments[1].body, /\[ \] Agent execution started/);
  assert.match(f.comments[1].body, /\[ \] Publishing result/);
  assert.match(f.comments[1].body, /actions\/runs\/124\/attempts\/2/);
});

test('overlapping runs and retries update only their own progress comment', async () => {
  const f = fixture();
  await publishProgress({ ...base, run: 200, attempt: 1, phase: 'running' }, f.api);
  await publishProgress({ ...base, run: 201, attempt: 1, phase: 'queued' }, f.api);
  const writesAfterNewRun = f.writes.length;
  await publishProgress({ ...base, run: 200, attempt: 1, phase: 'cancelled' }, f.api);
  assert.equal(f.writes.length, writesAfterNewRun + 1);
  assert.match(f.comments[0].body, /Status: \*\*Cancelled\*\*/);
  assert.match(f.comments[1].body, /Status: \*\*Queued\*\*/);
  assert.match(f.comments[1].body, /actions\/runs\/201\/attempts\/1/);

  await publishProgress({ ...base, run: 201, attempt: 2, phase: 'running' }, f.api);
  const writesAfterNewAttempt = f.writes.length;
  await publishProgress({ ...base, run: 201, attempt: 1, phase: 'cancelled' }, f.api);
  assert.equal(f.writes.length, writesAfterNewAttempt + 1);
  assert.equal(f.comments.length, 3);
  assert.match(f.comments[1].body, /Status: \*\*Cancelled\*\*/);
  assert.match(f.comments[2].body, /Status: \*\*Running\*\*/);
  assert.match(f.comments[2].body, /actions\/runs\/201\/attempts\/2/);
});

test('both reviewer providers preserve earlier run comments', async () => {
  for (const provider of ['codex', 'claude']) {
    const reviewer = { ...base, role: 'reviewer', login: `cx2-ag-${provider}-review-aegis[bot]`, userId: 200 };
    const f = fixture([], reviewer);
    await publishProgress({ ...reviewer, phase: 'succeeded' }, f.api);
    const previous = f.comments[0].body;
    await publishProgress({ ...reviewer, run: 124, phase: 'running' }, f.api);
    await publishProgress({ ...reviewer, run: 124, phase: 'failed' }, f.api);
    assert.equal(f.comments.length, 2);
    assert.equal(f.comments[0].body, previous);
    assert.match(f.comments[1].body, /Status: \*\*Failed\*\*/);
  }
});

test('legacy comments match exact attempt numbers and omitted attempt defaults to one', async () => {
  const f = fixture();
  await publishProgress({ ...base, attempt: 10, phase: 'succeeded' }, f.api);
  await publishProgress({ ...base, attempt: undefined, phase: 'running' }, f.api);
  await publishProgress({ ...base, phase: 'failed' }, f.api);
  assert.equal(f.comments.length, 2);
  assert.match(f.comments[0].body, /Status: \*\*Completed\*\*/);
  assert.match(f.comments[1].body, /attempts\/1$/);
  assert.match(f.comments[1].body, /Status: \*\*Failed\*\*/);
});

test('duplicate comments for the same attempt fail without changing history', async () => {
  const f = fixture();
  await publishProgress({ ...base, phase: 'running' }, f.api);
  f.comments.push({ ...f.comments[0], id: 20 });
  await assert.rejects(publishProgress({ ...base, phase: 'succeeded' }, f.api), /Multiple progress comments/);
  assert.equal(f.writes.length, 1);
});

test('reviewer handoff completes assignment without claiming model execution', async () => {
  const reviewer = { ...base, role: 'reviewer' };
  const f = fixture();
  await publishProgress({ ...reviewer, phase: 'queued' }, f.api);
  await publishProgress({ ...reviewer, phase: 'handed-off', updateOnly: true }, f.api);
  assert.equal(f.comments.length, 1);
  assert.match(f.comments[0].body, /Status: \*\*Handed off\*\*/);
  assert.match(f.comments[0].body, /\[x\] Assignment delivered/);
  assert.doesNotMatch(f.comments[0].body, /\[x\] Agent execution started|\[x\] Publishing result|\[x\] Finished successfully/);
});
