import assert from 'node:assert/strict';
import test from 'node:test';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

const workflow = fileURLToPath(new URL('../workflows/agent-gemini.yml', import.meta.url));
const job = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0])["jobs"]["respond"])', workflow], { encoding: 'utf8' }));
const configured = "steps.key.outputs.have == 'true'";
const step = name => job.steps.find(entry => entry.name === name);

test('Gemini uses durable issue allocation and isolated publication credentials', () => {
  const workflowSource = readFileSync(workflow, 'utf8');
  const workflowJobs = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0])["jobs"])', workflow], { encoding: 'utf8' }));
  assert.match(workflowJobs.resolve.steps.at(-1).run, /prepare-codex-worker.mjs --resolve/);
  assert.match(workflowJobs.allocate.steps.at(-1).run, /allocate --provider gemini --role executor/);
  assert.deepEqual(job.needs, ['resolve', 'allocate']);
  assert.equal(job.concurrency.group, '${{ needs.resolve.outputs.branch || github.run_id }}'.replace('${{', 'agent-gemini-${{'));
  assert.equal(job.concurrency['cancel-in-progress'], false);
  assert.equal(job.permissions.contents, 'read');
  assert.equal(job.env.WORKER_MODEL, "${{ vars.GEMINI_MODEL || 'gemini-3.8-flash-high' }}");
  assert.equal(step('Run the agent').env.GH_TOKEN, '${{ steps.bot-token.outputs.token }}');
  assert.equal(step('Run the agent').env.GEMINI_MODE, 'executor');
  assert.match(step('Run the agent').run, /run-gemini.mjs/);
  assert.match(step('Ensure the first commit has a linked draft PR').if, /always\(\)/);
  assert.doesNotMatch(workflowSource, /dangerously-skip-permissions|secrets\.GEMINI_API_KEY/);
});

test('Gemini restores subscription credentials with role-specific permissions', async () => {
  const { prepareGemini } = await import('./prepare-gemini.mjs');
  const base = fileURLToPath(new URL('../../.Internal/workspaces/gemini-tests/', import.meta.url));
  mkdirSync(base, { recursive: true });
  for (const mode of ['executor', 'reviewer', 'reply']) {
    const home = mkdtempSync(join(base, 'credentials-'));
    const workspace = join(home, 'workspace'), temporary = join(home, 'temporary');
    const options = { mode, home, workspace, temporary };
    assert.throws(() => prepareGemini({ ...options, credentials: '' }), /ANTIGRAVITY_OAUTH_CREDS/);
    assert.equal(existsSync(join(home, '.gemini')), false);
    prepareGemini({ ...options, credentials: '{"fixture":true}' });
    assert.equal(readFileSync(join(home, '.gemini/antigravity-cli/antigravity-oauth-token'), 'utf8'), '{"fixture":true}');
    const settings = JSON.parse(readFileSync(join(home, '.gemini/antigravity-cli/settings.json'), 'utf8'));
    assert.equal(settings.enableTerminalSandbox, mode !== 'reply');
    assert.ok(!settings.permissions.allow.some(rule => rule.startsWith('unsandboxed(')));
    assert.ok(settings.permissions.deny.includes(`read_file(${home}/.gemini)`));
    assert.equal(settings.permissions.allow.includes('command(*)'), mode !== 'reply');
    assert.equal(settings.permissions.allow.includes('read_url(api.github.com)'), mode === 'executor');
    assert.equal(settings.permissions.deny.includes('write_file(*)'), mode === 'reply');
  }
});

test('Gemini deducts setup from both CLI and process deadlines before inference', async () => {
  const { runGemini } = await import('./run-gemini.mjs');
  const startedAt = 1789232400;
  for (const setup of [1, 120, 300, 1499]) {
    let called = false;
    const reply = runGemini({ startedAt, timeoutMinutes: 30, now: startedAt + setup, prompt: 'Inspect the source', binary: 'antigravity' }, (binary, args, options) => {
      called = true;
      assert.equal(binary, 'antigravity');
      assert.deepEqual(args.slice(0, 8), ['--model', 'gemini-3.8-flash-high', '--effort', 'high', '--print-timeout', `${1500 - setup}s`, '--output-format', 'json']);
      assert.equal(options.timeout, (1500 - setup) * 1000);
      assert.equal(options.killSignal, 'SIGKILL');
      assert.match(args.at(-1), /final 300 seconds reserved for publication/);
      assert.match(args.at(-1), /Inspect the source/);
      assert.ok(!args.includes('--dangerously-skip-permissions'));
      return { status: 0, stdout: JSON.stringify({ status: 'SUCCESS', response: 'A checked answer' }) };
    });
    assert.equal(called, true);
    assert.match(reply, /^A checked answer\n/);
    assert.match(reply, /Model: gemini-3.8-flash-high/);
  }
  assert.throws(() => runGemini({ startedAt, timeoutMinutes: 30, now: startedAt + 1500 }, () => assert.fail('Expired jobs must not start inference')), /budget is exhausted/);
});

test('Gemini rejects failed processes, incomplete envelopes and empty replies', async () => {
  const { runGemini } = await import('./run-gemini.mjs');
  const options = { startedAt: 1789232400, timeoutMinutes: 30, now: 1789232401 };
  const executions = [
    { status: 1, stdout: '{"status":"SUCCESS","response":"partial"}' },
    { status: null, error: Error('timeout') },
    { status: 0, stdout: 'invalid JSON' },
    ...['ERROR', 'CANCELED', 'INTERRUPTED', 'INVALID', 'WAITING', 'RUNNING'].map(status => ({ status: 0, stdout: JSON.stringify({ status, response: 'partial' }) })),
    ...['', '   ', null, 7].map(response => ({ status: 0, stdout: JSON.stringify({ status: 'SUCCESS', response }) })),
  ];
  for (const execution of executions) assert.throws(() => runGemini(options, () => execution));
});

test('Gemini installation retains the pinned binary and verifies it before extraction', () => {
  const install = readFileSync(new URL('./install-gemini.sh', import.meta.url), 'utf8');
  assert.match(install, /1\.2\.2-6061403484848128\/linux-x64\/cli_linux_x64\.tar\.gz/);
  assert.match(install, /74342cf2a78b344392e573b638a648a6ad1f8e877f494b96e20f9c2b79158d5c423c40b2dcf788703362bb0a9150f09c707fde599d7557ce01c12208802a63cb/);
  assert.ok(install.indexOf('sha512sum --check') < install.indexOf('tar -xzf'));
  assert.match(step('Install Gemini CLI').run, /install-gemini.sh/);
  assert.match(step('Restore Gemini subscription login').run, /prepare-gemini.mjs/);
});

test('Gemini reviews use the shared native review schema without a Markdown suffix', async () => {
  const { runGemini } = await import('./run-gemini.mjs');
  const review = { verdict: 'APPROVED', body: 'Verified the change', comments: [] };
  const options = { startedAt: 1789232400, timeoutMinutes: 45, now: 1789232401, mode: 'reviewer', schema: 'schema.json', directory: '/workspace' };
  const reply = runGemini(options, (binary, args, execution) => {
    assert.ok(args.includes('--sandbox'));
    assert.ok(args.includes('--json-schema'));
    assert.equal(args.at(-1), 'schema.json');
    assert.equal(execution.cwd, '/workspace');
    assert.equal(args[args.indexOf('--add-dir') + 1], '/workspace');
    return { status: 0, stdout: JSON.stringify({ status: 'SUCCESS', response: JSON.stringify(review) }) };
  });
  assert.deepEqual(JSON.parse(reply), review);
  assert.throws(() => runGemini(options, () => ({ status: 0, stdout: JSON.stringify({ status: 'SUCCESS', response: 'not a review' }) })));
});

test('Gemini review execution receives history without the publication credential', () => {
  const review = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0])["jobs"]["gemini-review"])', fileURLToPath(new URL('../workflows/agent-review.yml', import.meta.url))], { encoding: 'utf8' }));
  const infer = review.steps.find(entry => entry.name === 'Review with subscription login');
  assert.equal(review.needs, 'select-reviewer');
  assert.match(review.if, /reviewer == 'gemini'/);
  assert.match(review.if, /requested == 'true'/);
  assert.ok(!Object.keys({ ...review.env, ...infer.env }).some(key => /TOKEN|PRIVATE_KEY/.test(key)));
  assert.match(infer.run, /entire cumulative PR/);
  assert.match(infer.run, /review-history.json/);
  assert.match(infer.env.GEMINI_SCHEMA, /review-schema.json/);
  const publish = review.steps.find(entry => entry.id === 'publish');
  assert.equal(publish.env.GH_TOKEN, '${{ steps.bot.outputs.token }}');
  assert.match(publish.run, /publish-review.mjs/);
  assert.match(review.steps.at(-1).run, /wait-for-review.mjs --review-final/);
});
