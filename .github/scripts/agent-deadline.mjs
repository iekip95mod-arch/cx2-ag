import { appendFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export function agentDeadline({ startedAt, timeoutMinutes, now = Math.floor(Date.now() / 1000), clockTools = true }) {
  if (![startedAt, timeoutMinutes, now].every(Number.isSafeInteger) || startedAt < 1 || now < startedAt || timeoutMinutes <= 10 || timeoutMinutes > 360) throw Error('Invalid agent job clock or timeout');
  const deadline = startedAt + timeoutMinutes * 60;
  const cutoff = deadline - 300;
  const wrapup = cutoff - 300;
  if (now >= cutoff) throw Error('The agent execution budget is exhausted. Preserve existing work and report the incomplete run.');
  const remaining = cutoff - now;
  const utc = seconds => new Date(seconds * 1000).toISOString();
  const clock = clockTools ? 'Check the current UTC clock with date before long tool calls. Bound each command timeout below the time remaining to the cutoff and recheck after it returns.' : 'Use the supplied clock snapshot and answer promptly without tools or waiting.';
  const text = `Job time budget measured at ${utc(now)}: ${remaining} seconds remain until the agent cutoff ${utc(cutoff)}. Begin wrapping up at ${utc(wrapup)}${now >= wrapup ? ' (already reached, wrap up now)' : ''}. The GitHub job deadline is ${utc(deadline)}, with its final 300 seconds reserved for publication and cleanup. ${clock} At wrap-up, stop starting new work, preserve partial changes on the existing draft PR when applicable, and report completed checks and verification gaps. Finish your response before the cutoff. Never invent successful checks or approval to meet the deadline.`;
  return { deadline, cutoff, wrapup, remaining, text };
}

function main() {
  const clockTools = process.env.AGENT_CLOCK_TOOLS ?? 'true';
  if (!['true', 'false'].includes(clockTools)) throw Error('Invalid clock tool mode');
  const budget = agentDeadline({ startedAt: Number(process.env.AGENT_JOB_STARTED_AT), timeoutMinutes: Number(process.env.AGENT_JOB_TIMEOUT_MINUTES), clockTools: clockTools === 'true' });
  if (!process.env.GITHUB_ENV) throw Error('The workflow environment file is required');
  appendFileSync(process.env.GITHUB_ENV, `AGENT_TIME_BUDGET=${budget.text}\n`);
  console.log(budget.text);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) { console.error(`::error::${error.message}`); process.exitCode = 1; }
}
