import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { claimIssue } from './prepare-codex-worker.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const target = { repository, number: 42, run: 123 };
function fixture(options = {}) {
  const calls = [];
  const issue = { state: 'open', assignees: [], labels: [], ...options.issue };
  const api = async (method, endpoint, body) => {
    calls.push({ method, endpoint, body });
    if (method === 'GET' && endpoint === `repos/${repository}/issues/42`) return issue;
    if (endpoint === 'user') return { type: 'User', login: 'worker-owner', id: 7, ...options.identity };
    if (endpoint === `repos/${repository}/pulls/42`) return options.pr;
    if (method === 'GET' && endpoint.endsWith('/git/ref/heads/main')) return { object: { sha: 'a'.repeat(40) } };
    if (method === 'GET' && endpoint.includes('/git/ref/heads/')) return options.existing ?? null;
    if (method === 'POST' && endpoint.endsWith('/assignees')) return { assignees: options.assignment ?? [{ login: 'worker-owner' }] };
    if (method === 'POST') return {};
    throw Error(`Unexpected call ${method} ${endpoint}`);
  };
  return { calls, api };
}
test('claim creates the issue branch, assigns the publishing account and records the run', async () => {
  const { calls, api } = fixture();
  const claim = await claimIssue(target, api);
  assert.equal(claim.branch, 'codex/issue-42');
  assert.deepEqual(calls.filter(call => call.method === 'POST').map(call => [call.endpoint, call.body]), [
    [`repos/${repository}/git/refs`, { ref: 'refs/heads/codex/issue-42', sha: 'a'.repeat(40) }],
    [`repos/${repository}/issues/42/assignees`, { assignees: ['worker-owner'] }],
    [`repos/${repository}/issues/42/comments`, { body: 'Codex worker claimed this issue.\n\nBranch: codex/issue-42\nRun: https://github.com/iekip95mod-arch/cx2-ag/actions/runs/123\n\nThis worker owns this issue only. Its changes require an independent Codex review and all protected checks before merging.' }]
  ]);
});
test('invalid targets make no API requests', async () => {
  for (const changed of [{ repository: 'other/repo' }, { number: '../42' }, { number: 0 }, { run: '' }]) {
    const { calls, api } = fixture();
    await assert.rejects(claimIssue({ ...target, ...changed }, api));
    assert.equal(calls.length, 0);
  }
});
test('closed, externally assigned and Claude issues cannot be claimed', async () => {
  for (const issue of [{ state: 'closed' }, { assignees: [{ login: 'someone-else' }] }, { labels: [{ name: 'claude' }] }]) {
    const { calls, api } = fixture({ issue });
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.method === 'POST'), false);
  }
});
test('a known owner resumes a branch without replacing its ref', async () => {
  const { calls, api } = fixture({ issue: { assignees: [{ login: 'worker-owner' }] }, existing: { object: { sha: 'b'.repeat(40) } } });
  await claimIssue(target, api);
  assert.equal(calls.some(call => call.method === 'POST' && call.endpoint.endsWith('/git/refs')), false);
  const unclaimed = fixture({ existing: {} });
  await assert.rejects(claimIssue(target, unclaimed.api), /unclaimed branch/);
  assert.equal(unclaimed.calls.some(call => call.method === 'POST'), false);
});
test('pull request resumption checks repository, author and branch', async () => {
  const pr = { head: { repo: { full_name: repository }, ref: 'codex/issue-42' }, user: { login: 'worker-owner' } };
  const valid = fixture({ issue: { pull_request: {} }, pr, existing: {} });
  assert.equal((await claimIssue(target, valid.api)).branch, pr.head.ref);
  for (const changed of [{ ...pr, user: { login: 'someone-else' } }, { ...pr, head: { ...pr.head, ref: 'main' } }, { ...pr, head: { ...pr.head, repo: { full_name: 'other/repo' } } }]) {
    const { calls, api } = fixture({ issue: { pull_request: {} }, pr: changed });
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.method === 'POST'), false);
  }
});
test('an ignored assignment or non-user identity cannot report a successful claim', async () => {
  for (const options of [{ assignment: [] }, { identity: { type: 'Bot' } }]) {
    const { calls, api } = fixture(options);
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.endpoint.endsWith('/comments')), false);
  }
});
test('an API failure stops the claim before mutation', async () => {
  await assert.rejects(claimIssue(target, async () => { throw Error('unavailable'); }), /unavailable/);
});

function entryFixture(event, options = {}) {
  const workspace = process.env.TEST_WORKSPACE ?? fileURLToPath(new URL('../../.Internal/workspaces/', import.meta.url));
  mkdirSync(workspace, { recursive: true });
  const root = mkdtempSync(join(workspace, 'worker-entry-'));
  const checkout = join(root, 'checkout with spaces');
  const remote = join(root, 'remote.git');
  const temporary = join(root, 'runner temporary');
  const home = join(root, 'home');
  const bin = join(root, 'bin');
  for (const directory of [temporary, home, bin]) mkdirSync(directory);
  copyFileSync(new URL('./fixtures/codex-worker-gh.mjs', import.meta.url), join(bin, 'gh'));
  chmodSync(join(bin, 'gh'), 0o755);
  const env = {
    PATH: `${bin}:${dirname(process.execPath)}:${process.env.PATH}`,
    HOME: home, GIT_CONFIG_NOSYSTEM: '1', GIT_CONFIG_GLOBAL: '/dev/null',
    GIT_AUTHOR_NAME: 'Worker fixture', GIT_AUTHOR_EMAIL: 'fixture@users.noreply.github.com',
    GIT_COMMITTER_NAME: 'Worker fixture', GIT_COMMITTER_EMAIL: 'fixture@users.noreply.github.com',
    GH_TOKEN: 'worker-fixture-token', GITHUB_REPOSITORY: repository, GITHUB_RUN_ID: '123',
    GITHUB_WORKSPACE: checkout, RUNNER_TEMP: temporary,
    GITHUB_EVENT_PATH: join(root, 'event.json'), GITHUB_OUTPUT: join(root, 'outputs'),
    WORKER_FIXTURE: join(root, 'fixture.json')
  };
  const git = (...args) => execFileSync('git', args, { cwd: root, env, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
  git('init', '--bare', '--initial-branch=main', remote);
  git('clone', remote, checkout);
  git('-C', checkout, 'commit', '--allow-empty', '-m', 'Initialize fixture');
  git('-C', checkout, 'push', 'origin', 'main');
  const base = git('-C', checkout, 'rev-parse', 'HEAD');
  const issue = { state: 'open', assignees: [], labels: [], title: 'Printer refusal', ...options.issue };
  const config = { ...options, remote, issue, log: join(root, 'calls.jsonl') };
  writeFileSync(env.WORKER_FIXTURE, JSON.stringify(config));
  writeFileSync(env.GITHUB_EVENT_PATH, JSON.stringify(event));
  const execute = () => spawnSync(process.execPath, [fileURLToPath(new URL('./prepare-codex-worker.mjs', import.meta.url))], { cwd: checkout, env, encoding: 'utf8' });
  const calls = () => existsSync(config.log) ? readFileSync(config.log, 'utf8').trim().split('\n').map(line => JSON.parse(line)) : [];
  return { checkout, remote, temporary, env, base, issue, git, execute, calls };
}

function verifyCheckout(fixture, branch, sha) {
  const execution = fixture.execute();
  assert.equal(execution.status, 0, execution.stderr);
  const directory = join(fixture.checkout, '.Internal/workspaces/codex-issue-42/tree');
  assert.equal(readFileSync(fixture.env.GITHUB_OUTPUT, 'utf8'), `directory=${directory}\nbranch=${branch}\nissue=42\n`);
  assert.deepEqual(JSON.parse(readFileSync(join(fixture.temporary, 'codex-issue.json'), 'utf8')), fixture.issue);
  assert.equal(fixture.git('-C', directory, 'rev-parse', 'HEAD'), sha);
  assert.equal(fixture.git('-C', directory, 'branch', '--show-current'), branch);
  assert.equal(fixture.git('-C', directory, 'rev-parse', '--abbrev-ref', '@{upstream}'), `origin/${branch}`);
  assert.equal(fixture.git('-C', directory, 'config', 'user.name'), 'worker-owner');
  assert.equal(fixture.git('-C', directory, 'config', 'user.email'), '7+worker-owner@users.noreply.github.com');
  assert.equal(fixture.git('-C', directory, 'status', '--porcelain'), '');
  assert.equal(fixture.calls().filter(call => call.args.join(' ') === 'auth setup-git').length, 1);
  assert.equal((execution.stdout + execution.stderr).includes(fixture.env.GH_TOKEN), false);
}

test('entry point parses manual and issue events and prepares a real tracked checkout', () => {
  for (const event of [{ inputs: { issue_number: '42' } }, { issue: { number: 42 } }]) {
    const fixture = entryFixture(event);
    verifyCheckout(fixture, 'codex/issue-42', fixture.base);
    assert.equal(fixture.git('--git-dir', fixture.remote, 'rev-parse', 'refs/heads/codex/issue-42'), fixture.base);
    assert.deepEqual(fixture.calls().find(call => call.args[3].endsWith('/assignees')).body, { assignees: ['worker-owner'] });
  }
});

test('entry point resumes the PR branch commit instead of resetting it to main', () => {
  const branch = 'codex/printer-refusal';
  const fixture = entryFixture({ pull_request: { number: 42 } }, {
    issue: { pull_request: {} },
    pr: { head: { repo: { full_name: repository }, ref: branch }, user: { login: 'worker-owner' } }
  });
  fixture.git('-C', fixture.checkout, 'commit', '--allow-empty', '-m', 'Advance branch fixture');
  const sha = fixture.git('-C', fixture.checkout, 'rev-parse', 'HEAD');
  fixture.git('-C', fixture.checkout, 'push', 'origin', `HEAD:refs/heads/${branch}`);
  verifyCheckout(fixture, branch, sha);
  assert.equal(fixture.calls().some(call => call.args[2] === 'POST' && call.args[3].endsWith('/git/refs')), false);
});

test('entry point rejects missing credentials and malformed events before CLI calls', () => {
  for (const failure of ['token', 'json', 'number']) {
    const fixture = entryFixture({ inputs: { issue_number: '42' } });
    if (failure === 'token') delete fixture.env.GH_TOKEN;
    if (failure === 'json') writeFileSync(fixture.env.GITHUB_EVENT_PATH, '{');
    if (failure === 'number') writeFileSync(fixture.env.GITHUB_EVENT_PATH, JSON.stringify({ inputs: { issue_number: '../42' } }));
    const execution = fixture.execute();
    assert.notEqual(execution.status, 0);
    assert.match(execution.stderr, /::error::/);
    assert.equal(fixture.calls().length, 0);
    assert.equal(existsSync(fixture.env.GITHUB_OUTPUT), false);
  }
});

test('entry point does not interpret denied or unavailable ref queries as missing refs', () => {
  for (const failStatus of [403, 500]) {
    const fixture = entryFixture({ issue: { number: 42 } }, { failEndpoint: `repos/${repository}/git/ref/heads/codex/issue-42`, failStatus });
    const execution = fixture.execute();
    assert.notEqual(execution.status, 0);
    assert.match(execution.stderr, /Check the publishing token permissions/);
    assert.equal(fixture.calls().some(call => call.args[2] === 'POST'), false);
    assert.equal(existsSync(fixture.env.GITHUB_OUTPUT), false);
  }
});

test('entry point emits no ready outputs when authentication, fetch or worktree creation fails', () => {
  for (const failure of ['authentication', 'fetch', 'worktree']) {
    const fixture = entryFixture({ issue: { number: 42 } }, { authFailure: failure === 'authentication' });
    if (failure === 'fetch') fixture.git('-C', fixture.checkout, 'remote', 'set-url', 'origin', join(fixture.temporary, 'absent.git'));
    if (failure === 'worktree') fixture.git('-C', fixture.checkout, 'branch', 'codex/issue-42');
    const execution = fixture.execute();
    assert.notEqual(execution.status, 0);
    assert.match(execution.stderr, /::error::/);
    assert.equal(existsSync(fixture.env.GITHUB_OUTPUT), false);
    assert.equal(existsSync(join(fixture.temporary, 'codex-issue.json')), false);
  }
});
