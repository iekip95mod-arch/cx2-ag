import { spawnSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { agentDeadline } from './agent-deadline.mjs';

export function runClaudeReview({ startedAt, timeoutMinutes, now, prompt, schema, model, effort, allowedTools }, execute = spawnSync) {
  const budget = agentDeadline({ startedAt, timeoutMinutes, now });
  const args = ['--print', '--model', model, '--effort', effort, '--max-turns', '256', '--output-format', 'json', '--json-schema', schema, '--permission-mode', 'acceptEdits', '--tools', 'Read,Write,Edit,Grep,Glob,Bash', '--allowedTools', allowedTools];
  const run = execute('claude', args, { input: `${budget.text}\n\n${prompt}`, encoding: 'utf8', timeout: budget.remaining * 1000, killSignal: 'SIGKILL', maxBuffer: 8 * 1024 * 1024, env: { ...process.env, GITHUB_TOKEN: '', ANTHROPIC_API_KEY: '' } });
  if (run.stderr) process.stderr.write(run.stderr);
  if (run.error || run.status !== 0) throw Error('Claude review execution failed. No approval will be published');
  const envelope = JSON.parse(run.stdout);
  if (envelope.type !== 'result' || envelope.subtype !== 'success' || envelope.is_error !== false || !envelope.structured_output || typeof envelope.structured_output !== 'object' || Array.isArray(envelope.structured_output)) throw Error('Claude did not return a complete structured review');
  return JSON.stringify(envelope.structured_output);
}

function main() {
  const review = runClaudeReview({ startedAt: Number(process.env.AGENT_JOB_STARTED_AT), timeoutMinutes: Number(process.env.AGENT_JOB_TIMEOUT_MINUTES), prompt: process.env.REVIEW_PROMPT, schema: process.env.REVIEW_SCHEMA, model: process.env.REVIEW_MODEL, effort: process.env.REVIEW_EFFORT, allowedTools: process.env.REVIEW_ALLOWED_TOOLS });
  writeFileSync(process.env.REVIEW_OUTPUT, review);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) { console.error(error.message); process.exitCode = 1; }
}
