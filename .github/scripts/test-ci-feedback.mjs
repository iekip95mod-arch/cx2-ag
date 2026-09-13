import assert from 'node:assert/strict';
import test from 'node:test';
import { loadRoster } from './bot-identities.mjs';
import { dispatchCiFeedback } from './review-feedback.mjs';

function fixture(provider = 'codex') {
  const repository = 'iekip95mod-arch/cx2-ag';
  const executor = loadRoster().find(bot => bot.provider === provider && bot.role === 'executor');
  const branch = `${provider}/issue-42`, head = 'a'.repeat(40);
  const ci = { id: 77, run_attempt: 1, status: 'completed', conclusion: 'failure', event: 'pull_request', head_repository: { full_name: repository }, head_sha: head, path: '.github/workflows/check.yml', pull_requests: [{ number: 90 }] };
  const pr = { state: 'open', draft: false, head: { sha: head, ref: branch, repo: { full_name: repository } }, user: { login: executor.login, id: executor.userId, type: 'Bot' } };
  const responses = {
    [`repos/${repository}/actions/runs/77`]: ci,
    [`repos/${repository}/actions/runs/77/attempts/1/jobs?per_page=100&page=1`]: { total_count: 1, jobs: [{ name: 'fast', conclusion: 'failure' }] },
    [`repos/${repository}/pulls/90`]: pr,
    [`repos/${repository}/issues/42`]: { state: 'open' },
    [`repos/${repository}/contents/assignments.json?ref=bot-assignments`]: { sha: 'lease', content: Buffer.from(JSON.stringify({ version: 1, assignments: [{ key: `${provider}/executor/issue-42`, provider, role: 'executor', issue: 42, pr: 90, branch, slug: executor.slug, released: false }] })).toString('base64') },
    [`repos/${repository}/actions/workflows/agent-review-feedback.yml/runs?event=workflow_run&per_page=100`]: { workflow_runs: [] },
    [`repos/${repository}/actions/workflows/agent-review-feedback.yml/runs?event=pull_request_review&per_page=100`]: { workflow_runs: [] },
  };
  const calls = [];
  const api = async (method, endpoint, body) => { calls.push({ method, endpoint, body }); if (method === 'POST') return {}; assert.ok(Object.hasOwn(responses, endpoint), endpoint); return structuredClone(responses[endpoint]); };
  return { repository, ci, pr, responses, calls, api, options: { repository, run: 100, event: { action: 'completed', repository: { full_name: repository }, workflow_run: structuredClone(ci) } } };
}

test('failed CI resumes both providers on the same issue even while the PR is draft', async () => {
  for (const provider of ['codex', 'claude']) for (const draft of [false, true]) {
    const f = fixture(provider); f.pr.draft = draft;
    assert.equal(await dispatchCiFeedback(f.options, f.api), true);
    const sent = f.calls.filter(call => call.method === 'POST');
    assert.equal(sent.length, 1);
    assert.equal(sent[0].body.inputs.issue_number, '42');
    assert.equal(sent[0].body.ref, 'main');
    assert.match(sent[0].endpoint, provider === 'codex' ? /agent-codex.yml/ : /agent.yml/);
    assert.match(sent[0].body.inputs.task, /all failed jobs/);
    assert.match(sent[0].body.inputs.task, /untrusted task content/);
  }
});

test('successful, superseded, foreign and worker-generated failures never dispatch', async () => {
  for (const change of [
    f => { f.ci.conclusion = 'success'; },
    f => { f.ci.run_attempt = 2; },
    f => { f.ci.event = 'workflow_dispatch'; },
    f => { f.ci.head_repository.full_name = 'other/repo'; },
    f => { f.pr.head.sha = 'b'.repeat(40); },
    f => { f.pr.state = 'closed'; },
    f => { f.pr.user.id++; },
  ]) {
    const f = fixture(); change(f);
    assert.equal(await dispatchCiFeedback(f.options, f.api), false);
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
});

test('duplicate CI delivery is suppressed and a retry starting during validation prevents dispatch', async () => {
  const f = fixture();
  const history = `repos/${f.repository}/actions/workflows/agent-review-feedback.yml/runs?event=workflow_run&per_page=100`;
  f.responses[history].workflow_runs = [{ id: 99, display_title: 'CI feedback 77 attempt 1', conclusion: 'success' }];
  f.responses[`repos/${f.repository}/actions/runs/99/jobs?per_page=100`] = { total_count: 1, jobs: [{ steps: [{ name: 'Confirm executor dispatch', conclusion: 'success' }] }] };
  assert.equal(await dispatchCiFeedback(f.options, f.api), false);
  f.responses[history].workflow_runs = [];
  let reads = 0;
  assert.equal(await dispatchCiFeedback(f.options, async (...args) => {
    const response = await f.api(...args);
    if (args[1].endsWith('/runs/77') && ++reads === 2) response.run_attempt++;
    return response;
  }), false);
  assert.equal(f.calls.some(call => call.method === 'POST'), false);
});

test('rejected-review gate failures do not dispatch a second executor but real CI failures do', async () => {
  for (const provider of ['codex', 'claude']) {
    const f = fixture(provider);
    const reviewer = loadRoster().find(bot => bot.provider === provider && bot.role === 'reviewer');
    const path = `repos/${f.repository}/contents/assignments.json?ref=bot-assignments`;
    const assignment = JSON.parse(Buffer.from(f.responses[path].content, 'base64').toString());
    assignment.assignments.push({ key: `${provider}/reviewer/issue-42`, provider, role: 'reviewer', issue: 42, pr: 90, branch: f.pr.head.ref, slug: reviewer.slug, released: false });
    f.responses[path].content = Buffer.from(JSON.stringify(assignment)).toString('base64');
    f.responses[`repos/${f.repository}/pulls/90/reviews?per_page=100&page=1`] = [{ id: 5, state: 'CHANGES_REQUESTED', commit_id: f.pr.head.sha, user: { login: reviewer.login, id: reviewer.userId, type: 'Bot' }, submitted_at: '2026-09-13T01:00:00Z' }];
    const jobs = f.responses[`repos/${f.repository}/actions/runs/77/attempts/1/jobs?per_page=100&page=1`].jobs;
    jobs[0].name = 'review-approved';
    assert.equal(await dispatchCiFeedback(f.options, f.api), true);
    const history = `repos/${f.repository}/actions/workflows/agent-review-feedback.yml/runs?event=pull_request_review&per_page=100`;
    f.responses[history] = { workflow_runs: [{ id: 99, display_title: 'Review feedback 5', conclusion: 'failure' }] };
    f.responses[`repos/${f.repository}/actions/runs/99/jobs?per_page=100`] = { total_count: 1, jobs: [{ steps: [{ name: 'Confirm executor dispatch', conclusion: 'success' }] }] };
    assert.equal(await dispatchCiFeedback(f.options, f.api), false);
    f.responses[`repos/${f.repository}/actions/runs/99/jobs?per_page=100`].jobs[0].steps[0].conclusion = 'skipped';
    f.pr.draft = true;
    assert.equal(await dispatchCiFeedback(f.options, f.api), true);
    const verdict = f.responses[`repos/${f.repository}/pulls/90/reviews?per_page=100&page=1`][0];
    verdict.state = 'COMMENTED';
    verdict.body = 'Setup is unavailable. <!-- review-blocked -->';
    assert.equal(await dispatchCiFeedback(f.options, f.api), false);
    jobs.push({ name: 'full', conclusion: 'failure' });
    f.responses[`repos/${f.repository}/actions/runs/77/attempts/1/jobs?per_page=100&page=1`].total_count = 2;
    assert.equal(await dispatchCiFeedback(f.options, f.api), true);
  }
});
