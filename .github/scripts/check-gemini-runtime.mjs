import assert from 'node:assert/strict';
import { mkdirSync, readFileSync, appendFileSync } from 'node:fs';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { prepareGemini } from './prepare-gemini.mjs';
import { runGemini } from './run-gemini.mjs';
import { reviewRequest } from './publish-review.mjs';

for (const mode of ['executor', 'reviewer']) {
  const root = join(process.env.RUNNER_TEMP, `gemini-runtime-${mode}`);
  const home = join(root, 'home'), directory = join(root, 'workspace'), temporary = join(root, 'temporary');
  for (const folder of [home, directory, temporary]) mkdirSync(folder, { recursive: true });
  prepareGemini({ credentials: process.env.OAUTH_CREDS, mode, home, workspace: directory, temporary });
  const nonce = randomUUID();
  const prompt = `Runtime smoke test only. In ${directory}, create smoke.txt containing exactly ${nonce} followed by a newline. Run cat smoke.txt using the command tool in the sandbox and return its actual output in body. Return APPROVED and an empty comments array only if both operations succeeded. Otherwise return BLOCKED and explain the failure. Do not read credentials, change other files or access GitHub. Finish promptly.`;
  const response = runGemini({ startedAt: Number(process.env.AGENT_JOB_STARTED_AT), timeoutMinutes: 15, mode, directory, prompt, schema: readFileSync(new URL('./review-schema.json', import.meta.url), 'utf8'), binary: join(process.env.RUNNER_TEMP, 'antigravity-bin/antigravity') }, (binary, args, options) => spawnSync(binary, [...args, '--log-file', join(root, 'cli.log')], { ...options, env: { ...process.env, HOME: home, OAUTH_CREDS: '', GH_TOKEN: '', GITHUB_TOKEN: '' } }));
  const review = JSON.parse(response);
  assert.equal(readFileSync(join(directory, 'smoke.txt'), 'utf8'), `${nonce}\n`);
  if (review.verdict !== 'APPROVED') {
    const diagnostic = join(root, 'cli.log');
    console.error(readFileSync(diagnostic, 'utf8').split('\n').filter(line => /sandbox|unshare|mount|namespace|fatal|panic|denied/i.test(line)).slice(-20).map(line => line.slice(0, 1000)).join('\n'));
  }
  assert.equal(review.verdict, 'APPROVED', review.body);
  assert.ok(review.body.includes(nonce), 'The response must contain actual command output');
  assert.equal(reviewRequest(review, [], 'a'.repeat(40)).event, 'APPROVE');
  const evidence = `${mode}: subscription file editing, sandboxed command execution and native review schema passed.\n`;
  console.log(evidence);
  appendFileSync(process.env.GITHUB_STEP_SUMMARY, evidence);
}
