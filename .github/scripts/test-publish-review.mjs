import assert from 'node:assert/strict';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { agentDeadline } from './agent-deadline.mjs';
import { diffAnchors, reviewRequest, publishReview, fallbackBlockedReview } from './publish-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const patch = '@@ -10,3 +10,4 @@ function solve()\n context\n-old\n+new\n+extra\n tail\n@@ -30 +31 @@\n-last\n+final\n\\ No newline at end of file';
const files = [{ filename: 'nps/src/solve.cc', patch }];
const head = 'a'.repeat(40);
const inline = { path: files[0].filename, line: 11, side: 'RIGHT', body: '[P2] Preserve the refusal condition' };

test('environment blockers publish a nonapproving review without code findings', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) {
    const f = fixture(provider, 'BLOCKED');
    f.options.review.comments = [];
    f.published.state = 'COMMENTED';
    await publishReview(f.options, f.api);
    assert.equal(f.calls.at(-1).body.event, 'COMMENT');
    assert.match(f.calls.at(-1).body.body, /<!-- review-blocked -->/);
    f.options.review.comments = [inline];
    assert.throws(() => reviewRequest(f.options.review, files, head), /blocked review/i);
  }
});

function fixture(provider = 'codex', verdict = 'CHANGES_REQUESTED') {
  const appSlug = `cx2-${provider}-reviewer`;
  const options = { repository, pr: 91, head, appSlug, login: `${appSlug}[bot]`, review: { verdict, body: 'Configured model and effort are recorded here.', comments: [{ ...inline }] } };
  const endpoint = `repos/${repository}/pulls/91`;
  const pull = { state: 'open', draft: false, changed_files: 1, head: { sha: head, repo: { full_name: repository } }, base: { repo: { full_name: repository } }, user: { login: 'executor[bot]' } };
  const published = { id: 456, state: verdict, commit_id: head, user: { login: options.login, type: 'Bot' } };
  const responses = { [endpoint]: pull, [`${endpoint}/files?per_page=100&page=1`]: files, [`${endpoint}/reviews`]: published };
  const calls = [];
  const api = async (method, path, body) => { calls.push({ method, path, body }); assert.ok(Object.hasOwn(responses, path), path); return responses[path]; };
  return { options, endpoint, pull, published, responses, calls, api };
}

test('diff anchors distinguish deleted, added and context lines across hunks', () => {
  assert.deepEqual([...diffAnchors(patch)], ['RIGHT:10', 'LEFT:11', 'RIGHT:11', 'RIGHT:12', 'RIGHT:13', 'LEFT:30', 'RIGHT:31']);
  assert.deepEqual([...diffAnchors('@@ -0,0 +1,2 @@\n+one\n+two\n')], ['RIGHT:1', 'RIGHT:2']);
  assert.deepEqual([...diffAnchors('@@ -1,2 +0,0 @@\n-one\n-two')], ['LEFT:1', 'LEFT:2']);
  for (const invalid of [undefined, '', '@@ -1,2 +1 @@\n-one\n+two', '@@ -1 +1 @@\n-one\n+two\n+extra', '@@ -0 +1 @@\n-one\n+two', '@@ -2 +2 @@\n x\n@@ -1 +1 @@\n y']) assert.throws(() => diffAnchors(invalid));
});

test('inline requests retain exact left, right and context anchors', () => {
  const review = { verdict: 'CHANGES_REQUESTED', body: 'Three findings', comments: [{ ...inline, side: 'LEFT' }, inline, { ...inline, line: 10 }] };
  assert.deepEqual(reviewRequest(review, files, head), { commit_id: head, event: 'REQUEST_CHANGES', body: review.body, comments: review.comments });
  for (const changes of [{ path: 'not-changed.cc' }, { path: '../solve.cc' }, { line: 99 }, { line: 0 }, { line: 1.5 }, { line: 10, side: 'LEFT' }, { side: 'BOTH' }, { body: '' }]) assert.throws(() => reviewRequest({ ...review, comments: [{ ...inline, ...changes }] }, files, head));
  assert.throws(() => reviewRequest(review, [{ filename: inline.path }], head), /available text patch/);
  assert.throws(() => reviewRequest({ ...review, verdict: 'COMMENTED' }, files, head));
});

test('both providers submit one native formal review with attached comments for either verdict', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) for (const verdict of ['APPROVED', 'CHANGES_REQUESTED']) {
    const f = fixture(provider, verdict);
    assert.equal((await publishReview(f.options, f.api)).id, 456);
    assert.deepEqual(f.calls.filter(call => call.method === 'POST'), [{ method: 'POST', path: `${f.endpoint}/reviews`, body: { commit_id: head, event: verdict === 'APPROVED' ? 'APPROVE' : 'REQUEST_CHANGES', body: f.options.review.body, comments: [inline] } }]);
    assert.equal(f.calls.filter(call => call.path === f.endpoint).length, 2);
  }
});

test('approvals and nonline verification gaps may have zero inline comments', async () => {
  for (const verdict of ['APPROVED', 'CHANGES_REQUESTED']) {
    const f = fixture('codex', verdict);
    f.options.review.comments = [];
    f.options.review.body = verdict === 'APPROVED' ? 'No findings.' : 'Unable to complete the required verification.';
    await publishReview(f.options, f.api);
    assert.deepEqual(f.calls.at(-1).body.comments, []);
  }
});

test('stale heads, forks, drafts, closed PRs and invalid identities never publish', async () => {
  for (const change of [
    f => { f.pull.head.sha = 'b'.repeat(40); },
    f => { f.pull.head.repo.full_name = 'foreign/repo'; },
    f => { f.pull.base.repo.full_name = 'foreign/repo'; },
    f => { f.pull.draft = true; },
    f => { f.pull.state = 'closed'; },
    f => { f.pull.user.login = f.options.login; },
    f => { f.options.appSlug = 'wrong-bot'; },
    f => { f.options.review.comments[0].line = 1000; },
  ]) {
    const f = fixture(); change(f);
    await assert.rejects(publishReview(f.options, f.api));
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
  const f = fixture(); let reads = 0;
  await assert.rejects(publishReview(f.options, async (...args) => { const response = await f.api(...args); if (args[1] === f.endpoint && ++reads === 2) return { ...response, head: { ...response.head, sha: 'b'.repeat(40) } }; return response; }), /changed before/);
  assert.equal(f.calls.some(call => call.method === 'POST'), false);
});

test('file pagination includes later anchors and fails closed on incomplete or oversized lists', async () => {
  const f = fixture();
  f.pull.changed_files = 101;
  f.responses[`${f.endpoint}/files?per_page=100&page=1`] = Array.from({ length: 100 }, (_, i) => ({ filename: `file-${i}.cc` }));
  f.responses[`${f.endpoint}/files?per_page=100&page=2`] = files;
  await publishReview(f.options, f.api);
  assert.equal(f.calls.filter(call => call.path.includes('/files?')).length, 2);
  for (const count of [2, 3001]) {
    const missing = fixture(); missing.pull.changed_files = count;
    await assert.rejects(publishReview(missing.options, missing.api));
    assert.equal(missing.calls.some(call => call.method === 'POST'), false);
  }
});

test('unexpected publication response reports uncertainty without retrying', async () => {
  for (const changes of [{ state: 'PENDING' }, { commit_id: 'b'.repeat(40) }, { user: { login: 'foreign[bot]', type: 'Bot' } }, { user: { login: 'cx2-codex-reviewer[bot]', type: 'User' } }]) {
    const f = fixture(); Object.assign(f.published, changes);
    await assert.rejects(publishReview(f.options, f.api), /Review may exist/);
    assert.equal(f.calls.filter(call => call.method === 'POST').length, 1);
  }
});

test('actual CLI uses JSON stdin, assigned token and exact native review arguments', () => {
  const scripts = dirname(fileURLToPath(import.meta.url));
  const root = join(scripts, '../../.Internal/workspaces/publish-review-tests'); mkdirSync(root, { recursive: true });
  for (const provider of ['codex', 'claude', 'gemini']) for (const fail of [false, true]) {
    const directory = mkdtempSync(join(root, 'cli-')); const bin = join(directory, 'bin'); mkdirSync(bin);
    copyFileSync(join(scripts, 'fixtures/review-feedback-gh.mjs'), join(bin, 'gh')); chmodSync(join(bin, 'gh'), 0o755);
    const f = fixture(provider); const log = join(directory, 'calls.jsonl'); const config = join(directory, 'fixture.json'); const review = join(directory, 'review.json');
    f.options.review.comments[0].body = 'Literal $(false), `false`, quotes " and\nnewlines';
    writeFileSync(config, JSON.stringify({ responses: f.responses, log, ...(fail ? { fail: `${f.endpoint}/reviews` } : {}) }));
    writeFileSync(review, JSON.stringify(f.options.review));
    const run = spawnSync(process.execPath, [join(scripts, 'publish-review.mjs')], { encoding: 'utf8', env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, GH_TOKEN: 'fixture-private-token', FEEDBACK_FIXTURE: config, REVIEW_FILE: review, REPO: repository, PR: '91', HEAD_SHA: head, APP_SLUG: f.options.appSlug, EXPECTED_LOGIN: f.options.login } });
    assert.equal(run.status, fail ? 1 : 0, run.stderr);
    const calls = readFileSync(log, 'utf8').trim().split('\n').map(JSON.parse);
    const posts = calls.filter(call => call.args[2] === 'POST');
    assert.equal(posts.length, 1);
    assert.deepEqual(posts[0].args, ['api', '--method', 'POST', `${f.endpoint}/reviews`, '--input', '-']);
    assert.deepEqual(posts[0].body.comments, f.options.review.comments);
    assert.equal((run.stdout + run.stderr + JSON.stringify(calls)).includes('fixture-private-token'), false);
    if (fail) assert.match(run.stderr, /Inspect the PR before retrying/);
  }
});

test('fallbackBlockedReview produces an empty-comment blocked review explaining budget exhaustion', () => {
  const fallback = fallbackBlockedReview();
  assert.equal(fallback.verdict, 'BLOCKED');
  assert.match(fallback.body, /budget.*exhausted|verification/i);
  assert.deepEqual(fallback.comments, []);
  const custom = fallbackBlockedReview('Custom deadline exceeded');
  assert.equal(custom.verdict, 'BLOCKED');
  assert.equal(custom.body, 'Custom deadline exceeded');
  assert.deepEqual(custom.comments, []);
});

test('review-budget exhaustion preserves completed trustworthy reviews and yields explicit blocked review on incomplete output', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) {
    // Completed trustworthy review (e.g. APPROVED or CHANGES_REQUESTED) must NOT be replaced with BLOCKED
    for (const verdict of ['APPROVED', 'CHANGES_REQUESTED']) {
      const f = fixture(provider, verdict);
      f.published.state = verdict;
      await publishReview({ ...f.options, fallbackBlocked: true }, f.api);
      assert.equal(f.calls.at(-1).body.event, verdict === 'APPROVED' ? 'APPROVE' : 'REQUEST_CHANGES');
      assert.deepEqual(f.calls.at(-1).body.comments, [inline]);
    }

    // Incomplete/missing review must yield explicit BLOCKED review when fallbackBlocked is true
    for (const incomplete of [null, undefined, {}, { verdict: 'INVALID' }, { verdict: 'APPROVED', body: '', comments: [] }, { verdict: 'APPROVED', body: 'Valid', comments: [{ path: 'wrong.cc', line: 1, side: 'RIGHT', body: 'b' }] }]) {
      const f = fixture(provider, 'BLOCKED');
      f.options.review = incomplete;
      f.published.state = 'COMMENTED';
      await publishReview({ ...f.options, fallbackBlocked: true }, f.api);
      assert.equal(f.calls.at(-1).body.event, 'COMMENT');
      assert.match(f.calls.at(-1).body.body, /<!-- review-blocked -->/);
      assert.match(f.calls.at(-1).body.body, /budget.*exhausted|verification/i);
      assert.deepEqual(f.calls.at(-1).body.comments, []);
    }

    // Without fallbackBlocked, incomplete review must fail closed
    const f = fixture(provider);
    f.options.review = null;
    await assert.rejects(publishReview({ ...f.options, fallbackBlocked: false }, f.api));
    assert.equal(f.calls.some(call => call.method === 'POST'), false);
  }
});

test('CLI publishes trustworthy reviews when present and falls back to explicit blocked reviews on missing or malformed output only when enabled', () => {
  const scripts = dirname(fileURLToPath(import.meta.url));
  const root = join(scripts, '../../.Internal/workspaces/publish-review-tests'); mkdirSync(root, { recursive: true });
  for (const provider of ['codex', 'claude', 'gemini']) {
    const directory = mkdtempSync(join(root, 'cli-budget-')); const bin = join(directory, 'bin'); mkdirSync(bin);
    copyFileSync(join(scripts, 'fixtures/review-feedback-gh.mjs'), join(bin, 'gh')); chmodSync(join(bin, 'gh'), 0o755);

    // 1. Missing review file with FALLBACK_BLOCKED=true publishes BLOCKED review
    const fMissing = fixture(provider, 'BLOCKED'); fMissing.published.state = 'COMMENTED';
    const logMissing = join(directory, 'missing-calls.jsonl'); const configMissing = join(directory, 'missing-fixture.json');
    writeFileSync(configMissing, JSON.stringify({ responses: fMissing.responses, log: logMissing }));
    const runMissing = spawnSync(process.execPath, [join(scripts, 'publish-review.mjs')], { encoding: 'utf8', env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, GH_TOKEN: 'fixture-private-token', FEEDBACK_FIXTURE: configMissing, REVIEW_FILE: join(directory, 'nonexistent.json'), REPO: repository, PR: '91', HEAD_SHA: head, APP_SLUG: fMissing.options.appSlug, EXPECTED_LOGIN: fMissing.options.login, FALLBACK_BLOCKED: 'true' } });
    assert.equal(runMissing.status, 0, runMissing.stderr);
    const callsMissing = readFileSync(logMissing, 'utf8').trim().split('\n').map(JSON.parse);
    const postsMissing = callsMissing.filter(call => call.args[2] === 'POST');
    assert.equal(postsMissing.length, 1);
    assert.equal(postsMissing[0].body.event, 'COMMENT');
    assert.match(postsMissing[0].body.body, /<!-- review-blocked -->/);
    assert.deepEqual(postsMissing[0].body.comments, []);

    // 2. Malformed review file with FALLBACK_BLOCKED=true publishes BLOCKED review
    const fBad = fixture(provider, 'BLOCKED'); fBad.published.state = 'COMMENTED';
    const logBad = join(directory, 'bad-calls.jsonl'); const configBad = join(directory, 'bad-fixture.json'); const badReview = join(directory, 'bad.json');
    writeFileSync(badReview, '{ invalid json');
    writeFileSync(configBad, JSON.stringify({ responses: fBad.responses, log: logBad }));
    const runBad = spawnSync(process.execPath, [join(scripts, 'publish-review.mjs')], { encoding: 'utf8', env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, GH_TOKEN: 'fixture-private-token', FEEDBACK_FIXTURE: configBad, REVIEW_FILE: badReview, REPO: repository, PR: '91', HEAD_SHA: head, APP_SLUG: fBad.options.appSlug, EXPECTED_LOGIN: fBad.options.login, FALLBACK_BLOCKED: 'true' } });
    assert.equal(runBad.status, 0, runBad.stderr);
    const callsBad = readFileSync(logBad, 'utf8').trim().split('\n').map(JSON.parse);
    const postsBad = callsBad.filter(call => call.args[2] === 'POST');
    assert.equal(postsBad.length, 1);
    assert.equal(postsBad[0].body.event, 'COMMENT');
    assert.match(postsBad[0].body.body, /<!-- review-blocked -->/);

    // 3. Valid completed review with FALLBACK_BLOCKED=true publishes the actual review (not overwritten with BLOCKED)
    const fValid = fixture(provider, 'APPROVED'); fValid.published.state = 'APPROVED';
    const logValid = join(directory, 'valid-calls.jsonl'); const configValid = join(directory, 'valid-fixture.json'); const validReview = join(directory, 'valid.json');
    writeFileSync(validReview, JSON.stringify(fValid.options.review));
    writeFileSync(configValid, JSON.stringify({ responses: fValid.responses, log: logValid }));
    const runValid = spawnSync(process.execPath, [join(scripts, 'publish-review.mjs')], { encoding: 'utf8', env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, GH_TOKEN: 'fixture-private-token', FEEDBACK_FIXTURE: configValid, REVIEW_FILE: validReview, REPO: repository, PR: '91', HEAD_SHA: head, APP_SLUG: fValid.options.appSlug, EXPECTED_LOGIN: fValid.options.login, FALLBACK_BLOCKED: 'true' } });
    assert.equal(runValid.status, 0, runValid.stderr);
    const callsValid = readFileSync(logValid, 'utf8').trim().split('\n').map(JSON.parse);
    const postsValid = callsValid.filter(call => call.args[2] === 'POST');
    assert.equal(postsValid.length, 1);
    assert.equal(postsValid[0].body.event, 'APPROVE');
    assert.deepEqual(postsValid[0].body.comments, fValid.options.review.comments);

    // 4. Missing review file WITHOUT FALLBACK_BLOCKED fails closed
    const runNoFallback = spawnSync(process.execPath, [join(scripts, 'publish-review.mjs')], { encoding: 'utf8', env: { ...process.env, PATH: `${bin}:${process.env.PATH}`, GH_TOKEN: 'fixture-private-token', REVIEW_FILE: join(directory, 'nonexistent2.json'), REPO: repository, PR: '91', HEAD_SHA: head, APP_SLUG: fMissing.options.appSlug, EXPECTED_LOGIN: fMissing.options.login } });
    assert.equal(runNoFallback.status, 1);
    assert.match(runNoFallback.stderr, /The structured review file could not be read/);
  }
});


test('Claude native output survives a missing file and failed execution cannot approve', () => {
  const scripts = dirname(fileURLToPath(import.meta.url));
  const root = join(scripts, '../../.Internal/workspaces/publish-review-tests');
  mkdirSync(root, { recursive: true });
  for (const [native, outcome, expected] of [
    [JSON.stringify({ verdict: 'APPROVED', body: 'Verified the current diff.', comments: [] }), 'success', 'APPROVED'],
    [JSON.stringify({ verdict: 'APPROVED', body: 'Partial checkpoint.', comments: [] }), 'failure', 'COMMENTED'],
    ['', 'success', 'COMMENTED'],
    ['not json', 'success', 'COMMENTED']
  ]) {
    const directory = mkdtempSync(join(root, 'native-'));
    const bin = join(directory, 'bin'); mkdirSync(bin);
    copyFileSync(join(scripts, 'fixtures/review-feedback-gh.mjs'), join(bin, 'gh')); chmodSync(join(bin, 'gh'), 0o755);
    const f = fixture('claude', expected); f.published.state = expected;
    const log = join(directory, 'calls.jsonl'); const config = join(directory, 'fixture.json');
    const checkpoint = join(directory, 'checkpoint.json');
    writeFileSync(checkpoint, JSON.stringify({ verdict: 'APPROVED', body: 'Stale checkpoint.', comments: [] }));
    writeFileSync(config, JSON.stringify({ responses: f.responses, log }));
    const invocation = spawnSync(process.execPath, [join(scripts, 'publish-review.mjs')], { encoding: 'utf8', env: {
      ...process.env, PATH: `${bin}:${process.env.PATH}`, GH_TOKEN: 'fixture-private-token', FEEDBACK_FIXTURE: config,
      REVIEW_FILE: expected === 'APPROVED' ? join(directory, 'missing.json') : checkpoint,
      REVIEW_JSON: native, REVIEW_OUTCOME: outcome, FALLBACK_BLOCKED: 'true',
      REPO: repository, PR: '91', HEAD_SHA: head, APP_SLUG: f.options.appSlug, EXPECTED_LOGIN: f.options.login
    } });
    assert.equal(invocation.status, 0, invocation.stderr);
    const published = readFileSync(log, 'utf8').trim().split('\n').map(JSON.parse).find(call => call.args[2] === 'POST');
    assert.equal(published.body.event, expected === 'APPROVED' ? 'APPROVE' : 'COMMENT');
    if (expected !== 'APPROVED') assert.match(published.body.body, /review-blocked/);
  }
});

test('workflow review budget handling enforces fail-closed fallback and preserves turn limits', () => {
  const scripts = dirname(fileURLToPath(import.meta.url));
  const reviewWorkflow = JSON.parse(spawnSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', join(scripts, '../workflows/agent-review.yml')], { encoding: 'utf8' }).stdout);
  const agentWorkflow = JSON.parse(spawnSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0]))', join(scripts, '../workflows/agent.yml')], { encoding: 'utf8' }).stdout);

  // Turn limits are preserved (not merely removed)
  const claudeReview = reviewWorkflow.jobs.review.steps.find(step => step.name === 'Review the pull request');
  assert.match(claudeReview.with.claude_args, /--max-turns\s+\d+/);
  const claudeAgent = agentWorkflow.jobs.respond.steps.find(step => step.name === 'Run the agent');
  assert.match(claudeAgent.with.claude_args, /--max-turns\s+\d+/);

  // Review step continues on error to preserve completed trustworthy output or fall back to BLOCKED
  assert.equal(claudeReview['continue-on-error'], true);
  const codexReview = reviewWorkflow.jobs['codex-review'].steps.find(step => step.name === 'Review with subscription login');
  assert.equal(codexReview['continue-on-error'], true);
  const geminiJob = reviewWorkflow.jobs['gemini-review'];
  const geminiReview = geminiJob.steps.find(step => step.name === 'Review with subscription login');
  const deadline = geminiJob.steps.find(step => step.name === 'Set execution deadline');
  assert.equal(deadline.env.AGENT_JOB_TIMEOUT_MINUTES, geminiJob['timeout-minutes']);
  assert.equal(geminiReview.env.AGENT_JOB_TIMEOUT_MINUTES, geminiJob['timeout-minutes']);
  const afterSetup = agentDeadline({ startedAt: 1000, now: 1000 + 32 * 60, timeoutMinutes: geminiJob['timeout-minutes'] });
  assert.ok(afterSetup.remaining >= 20 * 60, 'A cold toolchain setup must leave a usable review budget');
  assert.ok(geminiJob['timeout-minutes'] < reviewWorkflow.jobs.review['timeout-minutes']);
  assert.ok(geminiJob['timeout-minutes'] < reviewWorkflow.jobs['codex-review']['timeout-minutes']);
  assert.equal(geminiReview['continue-on-error'], true);

  // Each reviewer job configures fallback blocked publishing
  for (const jobName of ['review', 'codex-review', 'gemini-review']) {
    const publishStep = reviewWorkflow.jobs[jobName].steps.find(step => step.name === 'Publish the review for the reviewed commit');
    assert.equal(publishStep.env.FALLBACK_BLOCKED, 'true');
  }
});

