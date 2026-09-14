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

test('Gemini checks out trusted code without persisting credentials', () => {
  const checkout = job.steps.find(entry => entry.uses?.startsWith('actions/checkout@'));
  assert.equal(checkout.with.ref, "${{ github.event_name == 'workflow_dispatch' && github.sha || github.event.repository.default_branch }}");
  assert.equal(checkout.with['persist-credentials'], false);
  assert.equal(job.steps[0].run, 'echo "AGENT_JOB_STARTED_AT=$(date +%s)" >> "$GITHUB_ENV"');
  assert.equal(job.env.AGENT_JOB_TIMEOUT_MINUTES, job['timeout-minutes']);
  assert.doesNotMatch(readFileSync(workflow, 'utf8'), /dangerously-skip-permissions|secrets\.GEMINI_API_KEY/);
  for (const name of ['Install Antigravity CLI', 'Run Gemini']) assert.equal(step(name).if, configured);
});

test('Gemini handles configured and missing subscription credentials without API fallback', () => {
  const key = step('Decide whether credentials are configured');
  assert.equal(key.env.OAUTH_CREDS, '${{ secrets.ANTIGRAVITY_OAUTH_CREDS }}');
  const workspace = fileURLToPath(new URL('../../.Internal/workspaces/gemini-tests/', import.meta.url));
  mkdirSync(workspace, { recursive: true });
  for (const credentials of ['', 'fixture-token']) {
    const home = mkdtempSync(join(workspace, 'credentials-'));
    const output = join(home, 'output'), summary = join(home, 'summary');
    const run = spawnSync('bash', ['-eu', '-o', 'pipefail', '-c', key.run], { encoding: 'utf8', env: { ...process.env, HOME: home, GITHUB_WORKSPACE: join(home, 'workspace'), OAUTH_CREDS: credentials, GITHUB_OUTPUT: output, GITHUB_STEP_SUMMARY: summary } });
    assert.equal(run.status, 0, run.stderr);
    assert.equal(readFileSync(output, 'utf8'), `have=${Boolean(credentials)}\n`);
    const token = join(home, '.gemini/antigravity-cli/antigravity-oauth-token');
    assert.equal(existsSync(token), Boolean(credentials));
    if (credentials) {
      assert.equal(readFileSync(token, 'utf8'), credentials);
      const settings = JSON.parse(readFileSync(join(home, '.gemini/antigravity-cli/settings.json'), 'utf8'));
      assert.deepEqual(settings.permissions.allow, [`read_file(${join(home, 'workspace')})`]);
      for (const deny of ['command(*)', 'unsandboxed(*)', 'write_file(*)', 'read_url(*)', 'execute_url(*)', 'mcp(*)', `read_file(${home}/.gemini)`]) assert.ok(settings.permissions.deny.includes(deny));
    } else assert.match(readFileSync(summary, 'utf8'), /ANTIGRAVITY_OAUTH_CREDS/);
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

test('Gemini installation and publication retain the pinned binary and target', () => {
  const install = step('Install Antigravity CLI').run;
  assert.match(install, /1\.2\.2-6061403484848128\/linux-x64\/cli_linux_x64\.tar\.gz/);
  assert.match(install, /74342cf2a78b344392e573b638a648a6ad1f8e877f494b96e20f9c2b79158d5c423c40b2dcf788703362bb0a9150f09c707fde599d7557ce01c12208802a63cb/);
  assert.ok(install.indexOf('sha512sum --check') < install.indexOf('tar -xzf'));
  assert.match(install, /tar -xzf .* antigravity/);
  assert.match(step('Run Gemini').run, /node .github\/scripts\/run-gemini.mjs/);
  const publish = step('Post the reply');
  assert.match(publish.if, /steps\.key\.outputs\.have == 'true'/);
  assert.equal(publish.env.NUMBER, '${{ github.event.issue.number || github.event.pull_request.number }}');
  assert.match(publish.run, /--body-file gemini-reply.md/);
});
