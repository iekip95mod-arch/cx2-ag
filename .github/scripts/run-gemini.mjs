import { spawnSync } from 'node:child_process';
import { writeFileSync, appendFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { agentDeadline } from './agent-deadline.mjs';

export function runGemini({ startedAt, timeoutMinutes, prompt, binary, now }, execute = spawnSync) {
  const budget = agentDeadline({ startedAt, timeoutMinutes, now, clockTools: false });
  const args = ['--model', 'gemini-3.8-flash-high', '--effort', 'high', '--print-timeout', `${budget.remaining}s`, '--output-format', 'json', '--print', `${budget.text}\n\n${prompt}`];
  const run = execute(binary, args, { encoding: 'utf8', timeout: budget.remaining * 1000, killSignal: 'SIGKILL', maxBuffer: 4 * 1024 * 1024 });
  if (run.error || run.status !== 0) throw Error('Gemini did not finish successfully within its execution budget');
  const envelope = JSON.parse(run.stdout);
  if (envelope.status !== 'SUCCESS' || typeof envelope.response !== 'string' || !envelope.response.trim()) throw Error(`Gemini response status ${envelope.status}: ${envelope.error || 'empty response'}`);
  return `${envelope.response.trim()}\n\nModel: gemini-3.8-flash-high. Reasoning effort: high. Authentication: subscription OAuth.\n`;
}

function main() {
  const reply = runGemini({ startedAt: Number(process.env.AGENT_JOB_STARTED_AT), timeoutMinutes: Number(process.env.AGENT_JOB_TIMEOUT_MINUTES), prompt: process.env.GEMINI_PROMPT, binary: join(process.env.RUNNER_TEMP, 'antigravity-bin/antigravity') });
  writeFileSync('gemini-reply.md', reply);
  appendFileSync(process.env.GITHUB_STEP_SUMMARY, reply);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
