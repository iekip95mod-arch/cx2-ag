import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { claimIssue } from './prepare-codex-worker.mjs';
import { publishDraft } from './publish-worker-pr.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const bot = { login: 'worker-amber[bot]', slug: 'worker-amber', id: 7 };
const target = { repository, number: 42, run: 123, expectedBranch: 'codex/issue-42', bot };
function fixture(options = {}) {
  const calls = [];
  const issue = { state: 'open', assignees: [], labels: [], title: 'Printer refusal', ...options.issue };
  const api = async (method, endpoint, body) => {
    calls.push({ method, endpoint, body });
    if (method === 'GET' && endpoint === `repos/${repository}/issues/42`) return issue;
    if (method === 'GET' && endpoint === `repos/${repository}/issues/84`) return { state: 'open', pull_request: {} };
    if (endpoint === `users/${bot.login}`) return { type: 'Bot', login: bot.login, id: bot.id, ...options.identity };
    if (endpoint === `repos/${repository}/pulls/84`) return options.pr;
    if (method === 'GET' && endpoint.endsWith('/git/ref/heads/main')) return { object: { sha: 'a'.repeat(40) } };
    if (method === 'GET' && endpoint.includes('/git/ref/heads/')) return options.existing ?? null;
    if (method === 'GET' && endpoint.includes('/assignees/')) return options.assignable ? {} : null;
    if (method === 'GET' && endpoint.includes('/labels/')) return null;
    if (method === 'POST' && endpoint.endsWith('/assignees')) return { assignees: options.assignment ?? [{ login: bot.login }] };
    if (method === 'POST') return {};
    throw Error(`Unexpected call ${method} ${endpoint}`);
  };
  return { calls, api };
}
test('claim creates the branch and identifies the leased bot without requiring assignment', async () => {
  const { calls, api } = fixture();
  const claim = await claimIssue(target, api);
  assert.equal(claim.branch, 'codex/issue-42');
  assert.equal(claim.login, bot.login);
  assert.equal(claim.email, '7+worker-amber[bot]@users.noreply.github.com');
  assert.equal(calls.some(call => call.endpoint === 'user'), false);
  assert.equal(calls.some(call => call.method === 'POST' && call.endpoint.endsWith('/assignees')), false);
  assert.deepEqual(calls.find(call => call.endpoint.endsWith('/issues/42/labels')).body, { labels: ['worker:worker-amber'] });
  assert.match(calls.find(call => call.endpoint.endsWith('/comments')).body.body, /worker-amber\[bot\].*executor lease/);
});
test('invalid targets and absent App metadata make no mutations', async () => {
  for (const changed of [{ repository: 'other/repo' }, { number: '../42' }, { number: 0 }, { run: '' }, { bot: undefined }, { bot: { ...bot, slug: 'different' } }]) {
    const { calls, api } = fixture();
    await assert.rejects(claimIssue({ ...target, ...changed }, api));
    assert.equal(calls.length, 0);
  }
});
test('closed, externally assigned and other provider issues cannot be claimed', async () => {
  for (const issue of [{ state: 'closed' }, { assignees: [{ login: 'someone-else' }] }, { labels: [{ name: 'claude' }] }]) {
    const { calls, api } = fixture({ issue });
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.method === 'POST'), false);
  }
});
test('a leased owner resumes its branch without replacing its ref', async () => {
  const { calls, api } = fixture({ existing: { object: { sha: 'b'.repeat(40) } } });
  await claimIssue(target, api);
  assert.equal(calls.some(call => call.method === 'POST' && call.endpoint.endsWith('/git/refs')), false);
  await assert.rejects(claimIssue({ ...target, expectedBranch: undefined }, fixture().api), /durable branch lease/);
});
test('pull request resumption checks repository, author and canonical issue branch', async () => {
  const pr = { head: { repo: { full_name: repository }, ref: 'codex/issue-42' }, user: { login: bot.login } };
  const valid = fixture({ pr, existing: {} });
  assert.equal((await claimIssue({ ...target, number: 84 }, valid.api)).number, 42);
  for (const changed of [{ ...pr, user: { login: 'someone-else' } }, { ...pr, head: { ...pr.head, ref: 'codex/arbitrary' } }, { ...pr, head: { ...pr.head, repo: { full_name: 'other/repo' } } }]) {
    const { calls, api } = fixture({ pr: changed });
    await assert.rejects(claimIssue({ ...target, number: 84 }, api));
    assert.equal(calls.some(call => call.method === 'POST'), false);
  }
});
test('legacy ownership migration requires the explicit trusted principal', async () => {
  const pr = { head: { repo: { full_name: repository }, ref: 'codex/issue-42' }, user: { login: 'iekip95mod-arch' } };
  const options = { issue: { assignees: [{ login: 'iekip95mod-arch' }] }, pr, existing: {} };
  await assert.rejects(claimIssue({ ...target, number: 84 }, fixture(options).api));
  await claimIssue({ ...target, number: 84, legacyOwner: 'iekip95mod-arch' }, fixture(options).api);
  await assert.rejects(claimIssue({ ...target, legacyOwner: 'someone-else' }, fixture().api));
});
test('assignable bots are assigned and the returned assignment must match', async () => {
  const supported = fixture({ assignable: true });
  await claimIssue(target, supported.api);
  assert.equal(supported.calls.some(call => call.method === 'POST' && call.endpoint.endsWith('/assignees')), true);
  await assert.rejects(claimIssue(target, fixture({ assignable: true, assignment: [] }).api), /confirm the bot assignment/);
});
test('incorrect GitHub identity cannot mutate or report a claim', async () => {
  for (const identity of [{ type: 'User' }, { id: 8 }, { login: 'other[bot]' }]) {
    const { calls, api } = fixture({ identity });
    await assert.rejects(claimIssue(target, api));
    assert.equal(calls.some(call => call.method === 'POST'), false);
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
    BOT_LOGIN: bot.login, BOT_USER_ID: String(bot.id), BOT_APP_SLUG: bot.slug,
    GH_TOKEN: 'worker-fixture-token', GITHUB_REPOSITORY: repository, GITHUB_RUN_ID: '123', EXPECTED_BRANCH: 'codex/issue-42',
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
  const execute = (args = []) => spawnSync(process.execPath, [fileURLToPath(new URL('./prepare-codex-worker.mjs', import.meta.url)), ...args], { cwd: checkout, env, encoding: 'utf8' });
  const calls = () => existsSync(config.log) ? readFileSync(config.log, 'utf8').trim().split('\n').map(line => JSON.parse(line)) : [];
  return { checkout, remote, temporary, env, base, issue, git, execute, calls };
}

function verifyCheckout(fixture, branch, sha) {
  fixture.env.EXPECTED_BRANCH = branch;
  const execution = fixture.execute();
  assert.equal(execution.status, 0, execution.stderr);
  const directory = join(fixture.checkout, '.Internal/workspaces/codex-issue-42/tree');
  assert.equal(readFileSync(fixture.env.GITHUB_OUTPUT, 'utf8'), `directory=${directory}\nbranch=${branch}\nissue=42\n`);
  assert.deepEqual(JSON.parse(readFileSync(join(fixture.temporary, 'codex-issue.json'), 'utf8')), fixture.issue);
  assert.equal(fixture.git('-C', directory, 'rev-parse', 'HEAD'), sha);
  assert.equal(fixture.git('-C', directory, 'branch', '--show-current'), branch);
  assert.equal(fixture.git('-C', directory, 'rev-parse', '--abbrev-ref', '@{upstream}'), `origin/${branch}`);
  assert.equal(fixture.git('-C', directory, 'config', 'user.name'), bot.login);
  assert.equal(fixture.git('-C', directory, 'config', 'user.email'), '7+worker-amber[bot]@users.noreply.github.com');
  assert.equal(fixture.git('-C', directory, 'status', '--porcelain'), '');
  assert.equal(fixture.calls().filter(call => call.args.join(' ') === 'auth setup-git').length, 1);
  assert.equal((execution.stdout + execution.stderr).includes(fixture.env.GH_TOKEN), false);
}

test('entry point parses manual and issue events and prepares a real tracked checkout', () => {
  for (const event of [{ inputs: { issue_number: '42' } }, { issue: { number: 42 } }]) {
    const fixture = entryFixture(event);
    verifyCheckout(fixture, 'codex/issue-42', fixture.base);
    assert.equal(fixture.git('--git-dir', fixture.remote, 'rev-parse', 'refs/heads/codex/issue-42'), fixture.base);
    assert.equal(fixture.calls().some(call => call.args[2] === 'POST' && call.args[3].endsWith('/assignees')), false);
  }
});

test('entry point resumes the PR branch commit instead of resetting it to main', () => {
  const branch = 'codex/issue-42';
  const fixture = entryFixture({ pull_request: { number: 84 } }, {
    number: 84,
    prIssue: { state: 'open', pull_request: {} },
    pr: { head: { repo: { full_name: repository }, ref: branch }, user: { login: bot.login } }
  });
  fixture.git('-C', fixture.checkout, 'commit', '--allow-empty', '-m', 'Advance branch fixture');
  const sha = fixture.git('-C', fixture.checkout, 'rev-parse', 'HEAD');
  fixture.git('-C', fixture.checkout, 'push', 'origin', `HEAD:refs/heads/${branch}`);
  verifyCheckout(fixture, branch, sha);
  assert.equal(fixture.calls().some(call => call.args[2] === 'POST' && call.args[3].endsWith('/git/refs')), false);
});

test('entry point rejects missing credentials and malformed events before CLI calls', () => {
  for (const failure of ['token', 'json', 'number', 'queue']) {
    const fixture = entryFixture({ inputs: { issue_number: '42' } });
    if (failure === 'token') delete fixture.env.GH_TOKEN;
    if (failure === 'json') writeFileSync(fixture.env.GITHUB_EVENT_PATH, '{');
    if (failure === 'number') writeFileSync(fixture.env.GITHUB_EVENT_PATH, JSON.stringify({ inputs: { issue_number: '../42' } }));
    if (failure === 'queue') delete fixture.env.EXPECTED_BRANCH;
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

test('issue and PR aliases resolve to the same branch queue without claiming work', () => {
  const branch = 'codex/issue-42';
  const pr = { head: { repo: { full_name: repository }, ref: branch }, user: { login: bot.login } };
  for (const [event, options] of [
    [{ issue: { number: 42 } }, {}],
    [{ issue: { number: 84 } }, { number: 84, prIssue: { state: 'open', pull_request: {} }, pr }],
    [{ pull_request: { number: 84 } }, { number: 84, prIssue: { state: 'open', pull_request: {} }, pr }],
    [{ inputs: { issue_number: '84' } }, { number: 84, prIssue: { state: 'open', pull_request: {} }, pr }]
  ]) {
    const fixture = entryFixture(event, options);
    const execution = fixture.execute(['--resolve']);
    assert.equal(execution.status, 0, execution.stderr);
    assert.equal(readFileSync(fixture.env.GITHUB_OUTPUT, 'utf8'), `branch=${branch}\n`);
    assert.equal(fixture.calls().every(call => call.args[0] === 'api' && call.args[2] === 'GET'), true);
    assert.equal(existsSync(join(fixture.checkout, '.Internal/workspaces')), false);
  }
});

test('a branch change after queue resolution cannot claim or prepare a different branch', () => {
  const fixture = entryFixture({ issue: { number: 42 } });
  fixture.env.EXPECTED_BRANCH = 'codex/another-issue';
  const execution = fixture.execute();
  assert.notEqual(execution.status, 0);
  assert.match(execution.stderr, /branch changed after queue resolution/);
  assert.equal(fixture.calls().some(call => call.args[2] === 'POST'), false);
  assert.equal(existsSync(fixture.env.GITHUB_OUTPUT), false);
});

test('general responses resolve without an issue branch or GitHub mutations', () => {
  const fixture = entryFixture({ inputs: { task: 'Explain the build' } });
  const execution = fixture.execute(['--resolve']);
  assert.equal(execution.status, 0, execution.stderr);
  assert.equal(readFileSync(fixture.env.GITHUB_OUTPUT, 'utf8'), 'branch=\n');
  assert.equal(fixture.calls().length, 0);
});

test('the first meaningful commit publishes a linked draft and later commits reuse it', () => {
  const fixture = entryFixture({ issue: { number: 42 } });
  verifyCheckout(fixture, 'codex/issue-42', fixture.base);
  const directory = join(fixture.checkout, '.Internal/workspaces/codex-issue-42/tree');
  for (const key of ['GIT_AUTHOR_NAME', 'GIT_AUTHOR_EMAIL', 'GIT_COMMITTER_NAME', 'GIT_COMMITTER_EMAIL']) delete fixture.env[key];
  fixture.git('-C', directory, 'commit', '--allow-empty', '-m', 'Empty fixture checkpoint');
  assert.equal(fixture.calls().some(call => call.args[2] === 'POST' && call.args[3].endsWith('/pulls')), false);
  for (const content of ['first meaningful fixture\n', 'second meaningful fixture\n']) {
    writeFileSync(join(directory, 'regression.txt'), content);
    fixture.git('-C', directory, 'add', 'regression.txt');
    fixture.git('-C', directory, 'commit', '-m', 'Record regression fixture');
    assert.equal(fixture.git('--git-dir', fixture.remote, 'rev-parse', 'refs/heads/codex/issue-42'), fixture.git('-C', directory, 'rev-parse', 'HEAD'));
  }
  const published = fixture.calls().filter(call => call.args[2] === 'POST' && call.args[3].endsWith('/pulls'));
  assert.equal(published.length, 1);
  assert.equal(published[0].body.draft, true);
  assert.equal(published[0].body.head, 'codex/issue-42');
  assert.match(published[0].body.body, /^Closes #42\n/);
  assert.equal(fixture.git('-C', directory, 'log', '-1', '--format=%an <%ae>'), 'worker-amber[bot] <7+worker-amber[bot]@users.noreply.github.com>');
  assert.throws(() => fixture.git('-C', fixture.checkout, 'config', '--worktree', '--get', 'core.hooksPath'));
});

test('publication refuses wrong checkout, wrong branch, closed issues and foreign PRs before pushing', async () => {
  const context = { repository, issue: 42, branch: 'codex/issue-42', login: bot.login, title: 'Printer refusal', directory: '/owned/tree' };
  for (const failure of ['checkout', 'branch', 'closed', 'foreign', 'duplicate', 'closed-pr']) {
    const pushes = [];
    const git = (...args) => {
      if (args[0] === 'rev-parse') return failure === 'checkout' ? '/another/tree' : context.directory;
      if (args[0] === 'branch') return failure === 'branch' ? 'main' : context.branch;
      if (args[0] === 'diff') return 'regression.cc';
      pushes.push(args);
      return '';
    };
    const api = async (method, endpoint) => {
      assert.equal(method, 'GET');
      if (endpoint.includes('/issues/')) return { state: failure === 'closed' ? 'closed' : 'open' };
      const pr = { state: failure === 'closed-pr' ? 'closed' : 'open', user: { login: failure === 'foreign' ? 'someone-else' : bot.login } };
      return failure === 'duplicate' ? [pr, pr] : [pr];
    };
    await assert.rejects(publishDraft(context, git, api));
    assert.equal(pushes.length, 0);
  }
});

test('Claude uses its own canonical checkout and the same publication hook', () => {
  const fixture = entryFixture({ client_payload: { issue_number: 42 } });
  fixture.env.BOT_PROVIDER = 'claude';
  fixture.env.EXPECTED_BRANCH = 'claude/issue-42';
  const execution = fixture.execute();
  assert.equal(execution.status, 0, execution.stderr);
  const directory = join(fixture.checkout, '.Internal/workspaces/claude-issue-42/tree');
  assert.equal(fixture.git('-C', directory, 'branch', '--show-current'), 'claude/issue-42');
  assert.equal(fixture.git('-C', directory, 'config', 'user.name'), bot.login);
  assert.equal(existsSync(join(fixture.git('-C', directory, 'config', '--worktree', 'core.hooksPath'), 'post-commit')), true);
  const executable = join(fixture.temporary, 'claude-worker');
  const stub = join(dirname(fixture.env.WORKER_FIXTURE), 'bin/claude');
  writeFileSync(stub, '#!/usr/bin/env node\nconsole.log(JSON.stringify({cwd:process.cwd(),args:process.argv.slice(2)}));\n');
  chmodSync(stub, 0o755);
  const invocation = spawnSync(executable, ['--version'], { cwd: fixture.checkout, env: fixture.env, encoding: 'utf8' });
  assert.equal(invocation.status, 0, invocation.stderr);
  assert.deepEqual(JSON.parse(invocation.stdout), { cwd: directory, args: ['--version'] });
});

test('a publication API failure preserves the commit and fails recovery without exposing credentials', () => {
  const fixture = entryFixture({ issue: { number: 42 } }, { failEndpoint: `repos/${repository}/pulls`, failStatus: 403 });
  verifyCheckout(fixture, 'codex/issue-42', fixture.base);
  const directory = join(fixture.checkout, '.Internal/workspaces/codex-issue-42/tree');
  writeFileSync(join(directory, 'regression.txt'), 'publication failure fixture\n');
  fixture.git('-C', directory, 'add', 'regression.txt');
  const commit = spawnSync('git', ['commit', '-m', 'Record publication fixture'], { cwd: directory, env: fixture.env, encoding: 'utf8' });
  assert.equal(commit.status, 0, commit.stderr);
  assert.match(commit.stderr, /The commit exists, but draft PR publication failed/);
  const sha = fixture.git('-C', directory, 'rev-parse', 'HEAD');
  const recovery = spawnSync(process.execPath, [fileURLToPath(new URL('./publish-worker-pr.mjs', import.meta.url)), join(fixture.temporary, 'worker-publication.json')], { cwd: directory, env: fixture.env, encoding: 'utf8' });
  assert.equal(recovery.status, 1);
  assert.match(recovery.stderr, /Retry publication without creating another commit/);
  assert.equal((commit.stdout + commit.stderr + recovery.stdout + recovery.stderr).includes(fixture.env.GH_TOKEN), false);
  assert.equal(fixture.git('-C', directory, 'rev-parse', 'HEAD'), sha);
});
