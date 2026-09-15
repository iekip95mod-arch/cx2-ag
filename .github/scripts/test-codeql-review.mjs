import test from 'node:test';
import assert from 'node:assert/strict';
import { approvalState, executeGhApi, publishQueuedReview, requestGitHub, reviewState as checkReview, waitForReview as wait, trustedAuthor } from './wait-for-review.mjs';
import * as reviewRuntime from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const sha = 'a'.repeat(40);
const roster = [
  { provider: 'codex', role: 'reviewer', slug: 'cx2-codex-review-amber', login: 'cx2-codex-review-amber[bot]', userId: 200, appId: 301, clientId: 'Iv1.review-fixture' },
  { provider: 'claude', role: 'reviewer', slug: 'cx2-claude-review-amber', login: 'cx2-claude-review-amber[bot]', userId: 201, appId: 302, clientId: 'Iv1.review-fixture' },
  { provider: 'codex', role: 'executor', login: 'cx2-codex-amber[bot]', userId: 100, appId: 300, clientId: 'Iv1.fixture' }
];
const options = { roster, selectProvider: async () => {}, providerLookup: async () => undefined, assignment: async ({ provider }) => roster.find(identity => identity.provider === provider && identity.role === 'reviewer') };
const reviewState = (read, repository, pr, sha, overrides = options) => checkReview(read, repository, pr, sha, overrides);
const waitForReview = (read, repository, pr, sha, sleep, attempts) => wait(read, repository, pr, sha, sleep, attempts, options);

test('rerouting validates the executor provider rather than the requested reviewer provider', async () => {
  const executor = roster.find(bot => bot.role === 'executor');
  const event = { pull_request: { number: 85, labels: [{ name: 'claude-review' }], head: { ref: 'codex/issue-42', repo: { full_name: repository } } }, sender: { type: 'Bot', login: executor.login, id: executor.userId } };
  const configured = { ...options, assignment: async ({ provider, role }) => {
    assert.equal(provider, 'codex');
    assert.equal(role, 'executor');
    return executor;
  } };
  assert.equal(await reviewRuntime.trustedRequester(async () => {}, repository, event, executor.login, executor.userId, configured), executor.login);
});

test('finished reviewers clear their request and identity labels without erasing a later request', async () => {
  for (const identity of roster.filter(bot => bot.role === 'reviewer')) {
    for (const renewed of [false, true]) {
      const labels = [`${identity.provider}-review`, `reviewer:${identity.slug}`, 'worker:assigned'];
      const deleted = [];
      const current = { state: 'open', head: { sha, ref: `${identity.provider}/issue-42`, repo: { full_name: repository } }, labels: labels.map(name => ({ name })) };
      const api = async (method, endpoint) => {
        if (method === 'DELETE') { deleted.push(decodeURIComponent(endpoint.split('/').at(-1))); return []; }
        if (endpoint.includes('/comments?')) return [];
        if (endpoint.includes('/attempts/')) return { run_started_at: '2026-09-13T12:00:00Z' };
        if (endpoint.includes('/events?')) return labels.map(name => ({ event: 'labeled', label: { name }, created_at: renewed && name.endsWith('-review') ? '2026-09-13T12:01:00Z' : '2026-09-13T11:59:00Z' }));
        return current;
      };
      await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'final', [], { ...options, model: 'test-model', effort: 'high', phase: 'succeeded' });
      assert.deepEqual(deleted, renewed ? [] : labels.slice(0, 2));
      deleted.length = 0;
      current.head.sha = 'b'.repeat(40);
      await reviewRuntime.clearReviewLabels(api, repository, 85, sha, identity, '100', '1');
      assert.deepEqual(deleted, []);
    }
  }
});

test('a completed custom-branch review retains its leased provider after label cleanup', async () => {
  for (const identity of roster.filter(bot => bot.role === 'reviewer')) for (const branch of ['feature/manual-issue', 'gemini/issue-42']) {
    const current = { state: 'open', draft: false, head: { sha, ref: branch, repo: { full_name: repository } }, user: { login: 'owner', id: 1 }, labels: [{ name: `${identity.provider}-review` }, { name: `reviewer:${identity.slug}` }] };
    const lease = { key: `${identity.provider}/reviewer/issue-42`, role: 'reviewer', provider: identity.provider, issue: 42, pr: 85, branch: current.head.ref, slug: identity.slug, released: false };
    const previous = roster.find(bot => bot.role === 'reviewer' && bot.provider !== identity.provider);
    let assignments = [{ ...lease, provider: previous.provider, key: `${previous.provider}/reviewer/issue-42`, slug: previous.slug }, lease];
    const api = async (method, endpoint, body) => {
      if (method === 'DELETE') { current.labels = current.labels.filter(label => label.name !== decodeURIComponent(endpoint.split('/').at(-1))); return []; }
      if (method === 'PUT') { assignments = JSON.parse(Buffer.from(body.content, 'base64').toString()).assignments; return {}; }
      if (endpoint.includes('/contents/assignments.json')) return { sha: 'lease-revision', content: Buffer.from(JSON.stringify({ version: 1, assignments })).toString('base64') };
      if (endpoint.includes('/reviews?')) return [{ id: 2, state: 'APPROVED', commit_id: sha, user: { type: 'Bot', login: identity.login, id: identity.userId } }];
      if (endpoint.includes('/comments?') || endpoint.includes('/events?')) return [];
      if (endpoint.includes('/attempts/')) return { run_started_at: '2026-09-13T12:00:00Z' };
      return current;
    };
    const configured = { ...options, selectProvider: undefined, providerLookup: undefined, model: 'test-model', effort: 'high', phase: 'succeeded' };
    await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'final', [], configured);
    assert.deepEqual(current.labels, []);
    assert.equal(await approvalState(endpoint => api('GET', endpoint), repository, 85, sha, { ...configured, provider: identity.provider }), 'approved');
    await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'final', [], configured);
  }
});

test('failed label cleanup records a failed lifecycle instead of completed work', async () => {
  const identity = roster[0];
  const current = { state: 'open', draft: false, head: { sha, ref: 'codex/issue-42', repo: { full_name: repository } }, labels: [{ name: 'codex-review' }] };
  const comments = [];
  const api = async (method, endpoint, body) => {
    if (endpoint.includes('/events?')) throw Error('Cleanup unavailable');
    if (endpoint.includes('/attempts/')) return { run_started_at: '2026-09-13T12:00:00Z' };
    if (method === 'GET' && endpoint.includes('/comments?')) return comments;
    if (method === 'GET') return current;
    const comment = { id: 10, body: body.body, user: { type: 'Bot', id: identity.userId, login: identity.login } };
    comments[0] = comment;
    return comment;
  };
  const configured = { ...options, model: 'test-model', effort: 'high', phase: 'succeeded' };
  await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'progress', [], configured);
  await assert.rejects(reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'final', [], configured), /Cleanup unavailable/);
  assert.match(comments[0].body, /Status: \*\*Failed\*\*/);
  assert.doesNotMatch(comments[0].body, /Status: \*\*Completed\*\*/);
});

test('GitHub API reads do not touch stdin after the child has exited', async () => {
  const pending = Object.assign(Promise.resolve({ stdout: '{"ok":true}' }), {
    child: { stdin: { end() { throw Object.assign(Error('write EPIPE'), { code: 'EPIPE' }); } } }
  });
  const result = await executeGhApi(() => pending, ['api', 'repos/example/project']);
  assert.deepEqual(result, { ok: true });
});

test('GitHub API writes serialize request bodies and close child stdin', async () => {
  let finish;
  let input;
  const pending = Object.assign(new Promise(resolve => { finish = resolve; }), {
    child: { stdin: { end(value) { input = value; finish({ stdout: '{"ok":true}' }); } } }
  });
  const result = await executeGhApi(() => pending, ['api', 'graphql', '--method', 'POST', '--input', '-'], { query: 'query { viewer { login } }' });
  assert.deepEqual(result, { ok: true });
  assert.equal(input, '{"query":"query { viewer { login } }"}');
});

test('review publication uses bounded direct API requests', async () => {
  let target;
  let options;
  const result = await requestGitHub(async (url, init) => {
    target = url;
    options = init;
    return { ok: true, status: 200, text: async () => '{"ok":true}' };
  }, 'secret', 'GET', 'repos/example/project');
  assert.deepEqual(result, { ok: true });
  assert.equal(target, 'https://api.github.com/repos/example/project');
  assert.equal(options.method, 'GET');
  assert.equal(options.headers.Authorization, 'Bearer secret');
  assert.ok(options.signal);
});

test('housekeeping calls retry a transient gateway failure instead of discarding a published review', async () => {
  const statuses = [504, 502, 204];
  const delays = [];
  let calls = 0;
  const result = await requestGitHub(async () => {
    const status = statuses[calls++];
    return { ok: status < 400, status, text: async () => '' };
  }, 'secret', 'DELETE', 'repos/example/project/issues/318/labels/claude-review', undefined, false, { sleep: async ms => { delays.push(ms); } });
  assert.equal(result, null);
  assert.equal(calls, 3);
  assert.equal(delays.length, 2);
  assert.ok(delays.every(delay => delay > 0));
});

test('a network failure retries while a rejected request and a create do not repeat', async () => {
  let dropped = 0;
  const recovered = await requestGitHub(async () => {
    if (dropped++ === 0) throw Error('socket hang up');
    return { ok: true, status: 200, text: async () => '{"ok":true}' };
  }, 'secret', 'PUT', 'repos/example/project/pulls/318/reviews/7', { body: 'note' }, false, { sleep: async () => {} });
  assert.deepEqual(recovered, { ok: true });
  assert.equal(dropped, 2);
  let refused = 0;
  await assert.rejects(requestGitHub(async () => {
    refused++;
    return { ok: false, status: 422, text: async () => '' };
  }, 'secret', 'DELETE', 'repos/example/project/issues/318/labels/claude-review', undefined, false, { sleep: async () => {} }), /failed with 422/);
  assert.equal(refused, 1);
  let posted = 0;
  await assert.rejects(requestGitHub(async () => {
    posted++;
    return { ok: false, status: 502, text: async () => '' };
  }, 'secret', 'POST', 'repos/example/project/issues/318/comments', { body: 'progress' }, false, { sleep: async () => {} }), /failed with 502/);
  assert.equal(posted, 1);
  let exhausted = 0;
  await assert.rejects(requestGitHub(async () => {
    exhausted++;
    return { ok: false, status: 504, text: async () => '' };
  }, 'secret', 'GET', 'repos/example/project', undefined, false, { sleep: async () => {} }), /failed with 504/);
  assert.equal(exhausted, 3);
});

test('queued review progress reuses trusted assignment outputs without rereading the lease', async () => {
  const identity = roster[0];
  const current = { head: { sha, ref: 'codex/issue-42', repo: { full_name: repository } }, labels: [], state: 'open', draft: false };
  const endpoints = [];
  const api = async (method, endpoint, body) => {
    endpoints.push(endpoint);
    if (endpoint.endsWith('/pulls/85')) return current;
    if (method === 'GET') return [];
    return { id: 10, body: body.body, user: { login: identity.login, id: identity.userId, type: 'Bot' } };
  };
  await publishQueuedReview(api, repository, 85, sha, identity.slug, identity.login, identity.userId, '100', '1', { roster, model: 'gpt-6-astra', effort: 'high' });
  assert.deepEqual(endpoints, [`repos/${repository}/pulls/85`, `repos/${repository}/issues/85/comments?per_page=100&page=1`, `repos/${repository}/issues/85/comments`]);
  await assert.rejects(publishQueuedReview(api, repository, 85, sha, identity.slug, identity.login, 999, '100', '1', { roster, model: 'gpt-6-astra', effort: 'high' }));
});

test('review progress preserves each attempt and remains separate from formal reviews', async () => {
  const identity = roster[0];
  const current = { head: { sha, ref: 'codex/issue-42', repo: { full_name: repository } }, labels: [], state: 'open', draft: false };
  const reviews = [];
  const comments = [];
  const writes = [];
  const api = async (method, endpoint, body) => {
    if (method === 'GET' && endpoint.endsWith('/pulls/85')) return current;
    if (method === 'GET' && endpoint.includes('/reviews?')) return reviews;
    if (method === 'GET' && endpoint.includes('/comments?')) return comments;
    writes.push({ method, endpoint, body });
    const comment = { id: 10 + comments.length, body: body.body, user: { login: identity.login, id: identity.userId, type: 'Bot' } };
    if (method === 'POST') comments.push(comment);
    else comments[0] = comment;
    return comment;
  };
  const configured = { ...options, model: 'gpt-6-astra', effort: 'high' };
  await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'progress', [], configured);
  await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'progress', [], configured);
  assert.equal(writes.length, 1);
  assert.equal(writes[0].endpoint, `repos/${repository}/issues/85/comments`);
  assert.match(writes[0].body.body, /Configured model: gpt-6-astra/);
  assert.match(writes[0].body.body, /Configured effort: high/);
  assert.match(writes[0].body.body, /formal verdict will be recorded separately/);
  assert.equal(reviews.length, 0);
  await assert.rejects(approvalState(async endpoint => endpoint.includes('/reviews?') ? [reviews] : current, repository, 85, sha, options));
  await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '2', 'progress', [], configured);
  assert.equal(writes.length, 2);
  assert.equal(writes[1].method, 'POST');
  assert.equal(comments.length, 2);
  assert.match(comments[0].body, /attempts\/1$/);
  assert.match(comments[1].body, /attempts\/2$/);
  await assert.rejects(reviewRuntime.publishReviewNote(api, repository, 85, sha, 'wrong-app', '100', '2', 'progress', [], configured));
  await assert.rejects(reviewRuntime.publishReviewNote(api, repository, 85, 'b'.repeat(40), identity.slug, '100', '2', 'progress', [], configured));
  assert.equal(writes.length, 2);
});

test('both providers finish assignment progress without removing the review request', async () => {
  for (const identity of roster.filter(bot => bot.role === 'reviewer')) {
    const current = { state: 'open', draft: false, head: { sha, ref: `${identity.provider}/issue-42`, repo: { full_name: repository } }, labels: [{ name: `${identity.provider}-review` }] };
    const comments = [];
    const writes = [];
    const api = async (method, endpoint, body) => {
      if (method === 'GET' && endpoint.endsWith('/pulls/85')) return current;
      if (method === 'GET' && endpoint.includes('/comments?')) return comments;
      if (!['POST', 'PATCH'].includes(method) || !endpoint.includes('/comments')) throw Error(endpoint);
      writes.push({ method, endpoint });
      const comment = { id: 10, body: body.body, user: { login: identity.login, id: identity.userId, type: 'Bot' } };
      if (method === 'POST') comments.push(comment);
      else comments[0] = comment;
      return comment;
    };
    const configured = { ...options, provider: identity.provider, model: 'gpt-5.6-sol', effort: 'high' };
    await publishQueuedReview(api, repository, 85, sha, identity.slug, identity.login, identity.userId, '100', '1', configured);
    await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'handoff', [], configured);
    await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'handoff', [], configured);
    assert.deepEqual(writes.map(write => write.method), ['POST', 'PATCH']);
    assert.match(comments[0].body, /Status: \*\*Handed off\*\*/);
    assert.match(comments[0].body, /pull\/85\/checks/);
    assert.equal(current.labels[0].name, `${identity.provider}-review`);
  }
});

test('both providers append deterministic configured metadata to a fresh formal verdict', async () => {
  for (const identity of roster.filter(entry => entry.role === 'reviewer')) {
    const current = { head: { sha, ref: `${identity.provider}/issue-42`, repo: { full_name: repository } }, labels: [], state: 'open', draft: false };
    const verdict = { id: 2, commit_id: sha, state: 'APPROVED', body: 'Validated the changed behavior.', user: { login: identity.login, id: identity.userId, type: 'Bot' } };
    const writes = [];
    const api = async (method, endpoint, body) => {
      if (method === 'GET' && endpoint.endsWith('/pulls/85')) return current;
      if (method === 'GET' && endpoint.includes('/reviews?')) return [verdict];
      writes.push({ method, endpoint, body });
      verdict.body = body.body;
      return verdict;
    };
    const configured = { ...options, model: identity.provider === 'claude' ? 'opus' : 'gpt-6-astra', effort: 'high' };
    await assert.rejects(reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'disclosure', [2], configured));
    await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'disclosure', [1], configured);
    await reviewRuntime.publishReviewNote(api, repository, 85, sha, identity.slug, '100', '1', 'disclosure', [1], configured);
    assert.equal(writes.length, 1);
    assert.equal(writes[0].method, 'PUT');
    assert.equal(writes[0].endpoint, `repos/${repository}/pulls/85/reviews/2`);
    assert.match(verdict.body, /^Validated the changed behavior\./);
    assert.match(verdict.body, /Configured reasoning effort: high/);
    if (identity.provider === 'claude') assert.match(verdict.body, /opus \(alias, resolved model unverified\)/);
    else assert.match(verdict.body, /Configured model: gpt-6-astra/);
    assert.equal(verdict.state, 'APPROVED');
  }
});

test('review execution requires a verified reviewer assignment event', async () => {
  for (const identity of roster.filter(entry => entry.role === 'reviewer')) {
    const current = { number: 85, state: 'open', draft: false, user: { login: 'owner', id: 10, type: 'User' }, head: { sha, ref: `${identity.provider}/issue-42`, repo: { full_name: repository } }, labels: [{ name: `reviewer:${identity.slug}` }] };
    const event = { action: 'labeled', label: current.labels[0], pull_request: current, sender: { login: identity.login, id: identity.userId, type: 'Bot' } };
    assert.equal((await reviewRuntime.trustedAssignment(async () => current, repository, event, identity.login, identity.userId, options)).login, identity.login);
    for (const bad of [
      { ...event, action: 'ready_for_review' },
      { ...event, label: { name: `${identity.provider}-review` } },
      { ...event, label: { name: 'reviewer:arbitrary' } },
      { ...event, sender: { ...event.sender, id: 999 } },
      { ...event, sender: { ...event.sender, type: 'User' } },
      { ...event, sender: { ...event.sender, login: roster[2].login } }
    ]) await assert.rejects(reviewRuntime.trustedAssignment(async () => current, repository, bad, identity.login, identity.userId, options));
    await assert.rejects(reviewRuntime.trustedAssignment(async () => ({ ...current, labels: [] }), repository, event, identity.login, identity.userId, options));
    await assert.rejects(reviewRuntime.trustedAssignment(async () => ({ ...current, head: { ...current.head, sha: 'b'.repeat(40) } }), repository, event, identity.login, identity.userId, options));
    await assert.rejects(reviewRuntime.trustedAssignment(async () => current, repository, event, 'other[bot]', identity.userId, options));
    await assert.rejects(reviewRuntime.trustedAssignment(async () => current, repository, event, identity.login, identity.userId, { ...options, assignment: async () => { throw Error('No active lease'); } }));
  }
});

test('assignment delivery removes and re-adds the same leased label without allocating or launching a model', async () => {
  const identity = roster[0];
  const label = `reviewer:${identity.slug}`;
  let current = { number: 85, head: { sha, ref: 'codex/issue-42', repo: { full_name: repository } }, labels: [{ name: label }], state: 'open', draft: false };
  const writes = [];
  const api = async (method, endpoint, body) => {
    if (method === 'GET' && endpoint.endsWith('/pulls/85')) return current;
    if (method === 'GET' && endpoint.includes('/labels/')) return { name: label };
    writes.push({ method, endpoint, body });
    if (method === 'DELETE') current = { ...current, labels: [] };
    if (method === 'POST') current = { ...current, labels: body.labels.map(name => ({ name })) };
  };
  assert.equal((await reviewRuntime.dispatchReview(api, repository, 85, sha, identity.slug, options)).login, identity.login);
  assert.deepEqual(writes, [
    { method: 'DELETE', endpoint: `repos/${repository}/issues/85/labels/${encodeURIComponent(label)}`, body: undefined },
    { method: 'POST', endpoint: `repos/${repository}/issues/85/labels`, body: { labels: [label] } }
  ]);
  const event = { action: 'labeled', label: { name: label }, pull_request: current, sender: { login: identity.login, id: identity.userId, type: 'Bot' } };
  assert.equal((await reviewRuntime.trustedAssignment(async () => current, repository, event, identity.login, identity.userId, options)).login, identity.login);
  writes.length = 0;
  await assert.rejects(reviewRuntime.dispatchReview(api, repository, 85, sha, 'unassigned-app', options));
  await assert.rejects(reviewRuntime.dispatchReview(api, repository, 85, 'b'.repeat(40), identity.slug, options));
  assert.deepEqual(writes, []);
});
const run = (id, extra = {}) => ({ id, run_attempt: 1, created_at: new Date(Date.UTC(2026, 8, 12) + id * 1000).toISOString(), run_started_at: new Date(Date.UTC(2026, 8, 12) + id * 1000).toISOString(), display_title: 'Review PR #85 (requested)', head_sha: sha, pull_requests: [{ number: 85 }], status: 'completed', conclusion: 'success', ...extra });
const approval = (id = 1, extra = {}) => ({ id, commit_id: sha, state: 'APPROVED', user: { login: roster[0].login, id: roster[0].userId, type: 'Bot' }, ...extra });
function fixture(runs, { current = { head: { sha, ref: 'codex/review-gate' }, labels: [], state: 'open', draft: false }, jobs = [{ name: 'review-approved', status: 'completed', conclusion: 'success' }], reviews = [approval()], requests = [] } = {}) {
  return async endpoint => {
    if (endpoint.endsWith('/pulls/85')) return current;
    if (endpoint.endsWith('/pulls/85/reviews?per_page=100')) return [reviews];
    if (endpoint.includes('/workflows/agent-review-request.yml/')) return [{ workflow_runs: requests }];
    if (endpoint.includes('/workflows/')) return [{ workflow_runs: runs }];
    assert.match(endpoint, /\/actions\/runs\/\d+\/jobs\?filter=latest&per_page=100$/);
    return [{ jobs }];
  };
}
test('approval is scoped to the current PR and exact head', async () => {
  assert.equal(await reviewState(fixture([run(1)]), repository, 85, sha), 'approved');
  assert.equal(await reviewState(fixture([run(1, { head_sha: 'b'.repeat(40) }), run(2, { pull_requests: [{ number: 86 }] })]), repository, 85, sha), 'pending');
});

test('a new assignment must deliver a review before an older approval can release CodeQL', async () => {
  const request = run(2, { display_title: 'Assign reviewer for PR #85 (requested)' });
  assert.equal(await reviewState(fixture([run(1)], { requests: [request] }), repository, 85, sha), 'pending');
  await assert.rejects(reviewState(fixture([run(1)], { requests: [{ ...request, conclusion: 'failure' }] }), repository, 85, sha), /assignment failed/);
  assert.equal(await reviewState(fixture([run(3)], { requests: [request] }), repository, 85, sha), 'approved');
  assert.equal(await reviewState(fixture([run(1)], { requests: [{ ...request, display_title: 'Assign reviewer for PR #85 (metadata)' }] }), repository, 85, sha), 'approved');
});
test('a skipped duplicate assignment trigger does not override a successful one', async () => {
  const success = run(10, { display_title: 'Assign reviewer for PR #85 (requested)', run_started_at: '2026-09-15T20:44:45Z' });
  const skipped = run(11, { display_title: 'Assign reviewer for PR #85 (requested)', conclusion: 'skipped', run_started_at: '2026-09-15T20:45:36Z', run_attempt: undefined });
  const review = run(20, { created_at: '2026-09-15T20:46:00Z' });
  assert.equal(await reviewState(fixture([review], { requests: [success, skipped] }), repository, 85, sha), 'approved');
});
test('rerunning an older assignment blocks approval from its previous attempt', async () => {
  const request = run(100, { display_title: 'Assign reviewer for PR #85 (requested)', run_attempt: 2, run_started_at: '2026-09-12T18:00:00Z', status: 'in_progress', conclusion: null });
  const previous = run(101, { created_at: '2026-09-12T17:00:00Z' });
  assert.equal(await reviewState(fixture([previous], { requests: [request] }), repository, 85, sha), 'pending');
  assert.equal(await reviewState(fixture([previous], { requests: [{ ...request, status: 'completed', conclusion: 'success' }] }), repository, 85, sha), 'pending');
  await assert.rejects(reviewState(fixture([previous], { requests: [{ ...request, status: 'completed', conclusion: 'failure' }] }), repository, 85, sha), /assignment failed/);
  const completed = { ...request, status: 'completed', conclusion: 'success' };
  const fresh = run(102, { created_at: '2026-09-12T18:01:00Z' });
  assert.equal(await reviewState(fixture([fresh], { requests: [completed] }), repository, 85, sha), 'approved');
  assert.equal(await reviewState(fixture([{ ...previous, run_attempt: 2, run_started_at: '2026-09-12T18:01:00Z' }], { requests: [completed] }), repository, 85, sha), 'pending');
  const higherId = run(105, { display_title: request.display_title, run_started_at: '2026-09-12T17:30:00Z' });
  assert.equal(await reviewState(fixture([previous], { requests: [higherId, completed] }), repository, 85, sha), 'pending');
  await assert.rejects(reviewState(fixture([fresh], { requests: [{ ...completed, run_started_at: undefined }] }), repository, 85, sha), /attempt cannot be identified/);
  await assert.rejects(reviewState(fixture([{ ...fresh, created_at: undefined }], { requests: [completed] }), repository, 85, sha), /cannot be correlated/);
});
test('failed, cancelled and missing approvals cannot start CodeQL', async () => {
  for (const conclusion of ['failure', 'cancelled', 'timed_out', 'skipped', null]) {
    await assert.rejects(reviewState(fixture([run(1, { conclusion })]), repository, 85, sha));
    await assert.rejects(reviewState(fixture([run(1)], { jobs: [{ name: 'review-approved', status: 'completed', conclusion }] }), repository, 85, sha));
  }
  await assert.rejects(reviewState(fixture([run(1)], { jobs: [] }), repository, 85, sha));
  await assert.rejects(reviewState(fixture([run(1)], { jobs: [{ name: 'review-approved', status: 'in_progress', conclusion: 'success' }] }), repository, 85, sha));
});
test('default approval wait covers cold reviewer toolchain setup', async () => {
  let milliseconds = 0;
  await assert.rejects(waitForReview(fixture([]), repository, 85, sha, async ms => { milliseconds += ms; }), /Timed out/);
  assert.equal(milliseconds, 269 * 30000);
});

test('new review attempts supersede failures and pending attempts prevent early scanning', async () => {
  assert.equal(await reviewState(fixture([run(1, { conclusion: 'failure' }), run(2)]), repository, 85, sha), 'approved');
  await assert.rejects(reviewState(fixture([run(1), run(2, { conclusion: 'failure' })]), repository, 85, sha));
  assert.equal(await reviewState(fixture([run(1, { status: 'in_progress', conclusion: null }), run(2)]), repository, 85, sha), 'pending');
});
test('changed, closed and draft PRs cannot start CodeQL', async () => {
  for (const current of [{ head: { sha: 'b'.repeat(40) }, state: 'open' }, { head: { sha }, state: 'closed' }, { head: { sha }, state: 'open', draft: true }]) {
    await assert.rejects(reviewState(fixture([run(1)], { current }), repository, 85, sha));
  }
});
test('pending review waits, completed approval releases and missing review times out', async () => {
  let waited = 0;
  const read = async (...args) => fixture(waited ? [run(1)] : [])(...args);
  await waitForReview(read, repository, 85, sha, async ms => { assert.equal(ms, 30000); waited++; }, 2);
  assert.equal(waited, 1);
  await assert.rejects(waitForReview(fixture([]), repository, 85, sha, async () => {}, 2), /Timed out/);
});
test('API errors fail closed', async () => {
  await assert.rejects(reviewState(async () => { throw Error('API unavailable'); }, repository, 85, sha), /API unavailable/);
});
test('metadata-only runs cannot supersede the requested review attempt', async () => {
  const metadata = extra => run(2, { display_title: 'Review PR #85 (metadata)', ...extra });
  assert.equal(await reviewState(fixture([run(1), metadata({ conclusion: 'failure' })]), repository, 85, sha), 'approved');
  assert.equal(await reviewState(fixture([run(1), metadata({ status: 'in_progress', conclusion: null })]), repository, 85, sha), 'approved');
  await assert.rejects(reviewState(fixture([run(1, { conclusion: 'failure' }), metadata({})]), repository, 85, sha));
  assert.equal(await reviewState(fixture([metadata({})]), repository, 85, sha), 'pending');
});
test('dismissed and superseded approvals cannot release CodeQL', async () => {
  for (const state of ['DISMISSED', 'CHANGES_REQUESTED', 'COMMENTED']) {
    await assert.rejects(reviewState(fixture([run(1)], { reviews: [approval(1, { state })] }), repository, 85, sha));
    await assert.rejects(reviewState(fixture([run(1)], { reviews: [approval(2, { state }), approval(1)] }), repository, 85, sha));
  }
  assert.equal(await reviewState(fixture([run(1)], { reviews: [approval(2), approval(1, { state: 'CHANGES_REQUESTED' })] }), repository, 85, sha), 'approved');
});
test('only the selected provider bot can approve the current revision', async () => {
  for (const reviews of [[], [approval(1, { commit_id: 'b'.repeat(40) })], [approval(1, { user: { login: 'claude[bot]', type: 'Bot' } })], [approval(1, { user: { login: 'github-actions[bot]', type: 'User' } })]]) {
    await assert.rejects(reviewState(fixture([run(1)], { reviews }), repository, 85, sha));
  }
  for (const [ref, labels, bot] of [['claude/fix', [], roster[1]], ['codex/fix', [{ name: 'claude-review' }], roster[1]], ['claude/fix', [{ name: 'codex-review' }], roster[0]]]) {
    const current = { head: { sha, ref }, state: 'open', labels };
    assert.equal(await reviewState(fixture([run(1)], { current, reviews: [approval(1, { user: { login: bot.login, id: bot.userId, type: 'Bot' } })] }), repository, 85, sha), 'approved');
  }
  await assert.rejects(reviewState(fixture([run(1)], { current: { head: { sha, ref: 'codex/fix' }, state: 'open', labels: [{ name: 'claude-review' }, { name: 'codex-review' }] } }), repository, 85, sha));
});

test('an unleased legacy bot cannot approve a new named reviewer run', async () => {
  await assert.rejects(reviewState(fixture([run(900)], { reviews: [approval(1, { user: { login: 'github-actions[bot]', id: 41898282, type: 'Bot' } })] }), repository, 85, sha));
});

test('the exact leased reviewer ID is required and writers cannot self-approve', async () => {
  for (const user of [
    { login: roster[0].login, id: 999, type: 'Bot' },
    { login: roster[0].login, id: roster[0].userId, type: 'User' },
    { login: roster[2].login, id: roster[2].userId, type: 'Bot' }
  ]) await assert.rejects(reviewState(fixture([run(1)], { reviews: [approval(1, { user })] }), repository, 85, sha));
  await assert.rejects(reviewState(fixture([run(1)]), repository, 85, sha, { ...options, assignment: async () => roster[2] }));
  await assert.rejects(reviewState(fixture([run(1)]), repository, 85, sha, { ...options, assignment: async () => { throw Error('No lease'); } }), /No lease/);
  const current = { head: { sha, ref: 'codex/fix' }, labels: [], state: 'open', user: { login: roster[0].login, id: roster[0].userId } };
  await assert.rejects(reviewState(fixture([run(1)], { current }), repository, 85, sha), /own work/);
});

test('approval publication must be fresh for this attempt and match its selected provider', async () => {
  const read = fixture([run(1)]);
  await assert.rejects(approvalState(read, repository, 85, sha, { ...options, before: [1] }));
  await assert.rejects(approvalState(read, repository, 85, sha, { ...options, provider: 'claude' }));
  assert.equal(await approvalState(read, repository, 85, sha, { ...options, before: [], provider: 'codex' }), 'approved');
});

test('only configured executor IDs or associated human authors can request trusted review', () => {
  const current = { head: { repo: { full_name: repository } }, author_association: 'NONE', user: { login: roster[2].login, id: roster[2].userId, type: 'Bot' } };
  trustedAuthor(current, repository, roster);
  for (const association of ['OWNER', 'MEMBER', 'COLLABORATOR']) trustedAuthor({ ...current, author_association: association, user: { type: 'User' } }, repository, roster);
  for (const user of [{ ...current.user, id: 999 }, { ...current.user, login: 'dependabot[bot]' }, { login: roster[0].login, id: roster[0].userId, type: 'Bot' }, { type: 'User' }]) assert.throws(() => trustedAuthor({ ...current, user }, repository, roster));
  assert.throws(() => trustedAuthor({ ...current, head: { repo: { full_name: 'other/repo' } } }, repository, roster));
  assert.throws(() => trustedAuthor(current, repository, [{ ...roster[2], appId: null }]));
});
