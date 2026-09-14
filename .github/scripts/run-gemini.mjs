import { spawnSync } from 'node:child_process';
import { writeFileSync, appendFileSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { agentDeadline } from './agent-deadline.mjs';

export function runGemini({ startedAt, timeoutMinutes, prompt, binary, now, mode = 'reply', model = 'gemini-3.8-flash-high', effort = 'high', directory, schema }, execute = spawnSync) {
  if (!['executor', 'reviewer', 'reply'].includes(mode) || !/^[A-Za-z0-9._-]+$/.test(model) || !['low', 'medium', 'high'].includes(effort)) throw Error('Invalid Gemini execution configuration');
  const budget = agentDeadline({ startedAt, timeoutMinutes, now, clockTools: mode !== 'reply' });
  const args = ['--model', model, '--effort', effort, '--print-timeout', `${budget.remaining}s`, '--output-format', 'json', '--print', `${budget.text}\n\n${prompt}`];
  if (directory) args.push('--add-dir', directory);
  if (mode !== 'reply') args.push('--sandbox', '--mode', 'accept-edits');
  if (schema) args.push('--json-schema', schema);
  const run = execute(binary, args, { cwd: directory, encoding: 'utf8', timeout: budget.remaining * 1000, killSignal: 'SIGKILL', maxBuffer: 4 * 1024 * 1024 });
  if (run.stderr) process.stderr.write(run.stderr);
  if (run.error || run.status !== 0) throw Error('Gemini did not finish successfully within its execution budget');
  const envelope = JSON.parse(run.stdout);
  if (envelope.status !== 'SUCCESS' || typeof envelope.response !== 'string' || !envelope.response.trim()) throw Error(`Gemini response status ${envelope.status}: ${envelope.error || 'empty response'}`);
  const response = envelope.response.trim();
  if (schema) return JSON.stringify(JSON.parse(response));
  return `${response}\n\nModel: ${model}. Reasoning effort: ${effort}. Authentication: subscription OAuth.\n`;
}

function main() {
  const reply = runGemini({ startedAt: Number(process.env.AGENT_JOB_STARTED_AT), timeoutMinutes: Number(process.env.AGENT_JOB_TIMEOUT_MINUTES), prompt: process.env.GEMINI_PROMPT_FILE ? readFileSync(process.env.GEMINI_PROMPT_FILE, 'utf8') : process.env.GEMINI_PROMPT, binary: join(process.env.RUNNER_TEMP, 'antigravity-bin/antigravity'), mode: process.env.GEMINI_MODE, model: process.env.WORKER_MODEL ?? process.env.REVIEW_MODEL, effort: process.env.WORKER_EFFORT ?? process.env.REVIEW_EFFORT, directory: process.env.WORKER_DIRECTORY ?? process.env.GITHUB_WORKSPACE, schema: process.env.GEMINI_SCHEMA });
  writeFileSync(process.env.GEMINI_OUTPUT ?? 'gemini-reply.md', reply);
  appendFileSync(process.env.GITHUB_STEP_SUMMARY, reply);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
