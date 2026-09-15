import { spawnSync } from 'node:child_process';
import { appendFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { agentDeadline, cycleStart } from './agent-deadline.mjs';

// The window the CLI compacts at, shared by every Claude agent here so one number moves them all.
// The CLI accepts auto or 100k to 1M, and rejects anything else when the flag is parsed.
export const autocompactTokens = 400000;

function recordExecution(run) {
  const outcome = `exit status ${run.status ?? 'none'}, signal ${run.signal ?? 'none'}${run.error ? `, error ${run.error.message}` : ''}`;
  if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, `The Claude review CLI ended with ${outcome}. Its stderr is in this job log, streamed while it ran.\n`);
  return outcome;
}

export function runClaudeReview({ startedAt, timeoutMinutes, cycleStartedAt = null, now, prompt, schema, model, effort, allowedTools }, execute = spawnSync) {
  // This computes the reviewer's budget itself rather than reading AGENT_TIME_BUDGET, so the pull
  // request's shared cycle has to be handed in here too or the longest job in the loop is the one
  // job the cap does not reach.
  const budget = agentDeadline({ startedAt, timeoutMinutes, cycleStartedAt, now });
  const args = ['--print', '--model', model, '--effort', effort, '--max-turns', '256', '--autocompact', String(autocompactTokens), '--output-format', 'json', '--json-schema', schema, '--permission-mode', 'acceptEdits', '--tools', 'Read,Write,Edit,Grep,Glob,Bash', '--allowedTools', allowedTools];
  const run = execute('claude', args, { input: `${budget.text}\n\n${prompt}`, encoding: 'utf8', timeout: budget.remaining * 1000, killSignal: 'SIGKILL', maxBuffer: 8 * 1024 * 1024, stdio: ['pipe', 'pipe', 'inherit'], env: { ...process.env, GITHUB_TOKEN: '', ANTHROPIC_API_KEY: '' } });
  if (run.error || run.status !== 0) throw Error(`Claude review execution failed with ${recordExecution(run)}. No approval will be published`);
  const envelope = JSON.parse(run.stdout);
  if (envelope.type !== 'result' || envelope.subtype !== 'success' || envelope.is_error !== false || !envelope.structured_output || typeof envelope.structured_output !== 'object' || Array.isArray(envelope.structured_output)) throw Error('Claude did not return a complete structured review');
  return JSON.stringify(envelope.structured_output);
}

function main() {
  const review = runClaudeReview({ startedAt: Number(process.env.AGENT_JOB_STARTED_AT), timeoutMinutes: Number(process.env.AGENT_JOB_TIMEOUT_MINUTES), cycleStartedAt: cycleStart(process.env.AGENT_CYCLE_STARTED_AT), prompt: process.env.REVIEW_PROMPT, schema: process.env.REVIEW_SCHEMA, model: process.env.REVIEW_MODEL, effort: process.env.REVIEW_EFFORT, allowedTools: process.env.REVIEW_ALLOWED_TOOLS });
  writeFileSync(process.env.REVIEW_OUTPUT, review);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
