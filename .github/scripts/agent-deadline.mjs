import { appendFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export const cycleBudgetMinutes = 240;

export function agentDeadline({ startedAt, timeoutMinutes, cycleStartedAt = null, cycleMinutes = cycleBudgetMinutes, now = Math.floor(Date.now() / 1000), clockTools = true }) {
  if (![startedAt, timeoutMinutes, now].every(Number.isSafeInteger) || startedAt < 1 || now < startedAt || timeoutMinutes <= 10 || timeoutMinutes > 360) throw Error('Invalid agent job clock or timeout');
  // The cycle is the pull request, and every worker and reviewer job on it shares one budget. Four
  // hours is the ceiling rather than a default, so a larger request is a mistake rather than a policy.
  if (cycleStartedAt !== null && (!Number.isSafeInteger(cycleStartedAt) || !Number.isSafeInteger(cycleMinutes) || cycleStartedAt < 1 || cycleStartedAt > now || cycleMinutes <= 10 || cycleMinutes > cycleBudgetMinutes)) throw Error('Invalid agent cycle clock or budget');
  const jobDeadline = startedAt + timeoutMinutes * 60;
  const cycleDeadline = cycleStartedAt === null ? null : cycleStartedAt + cycleMinutes * 60;
  // A spent cycle must not refuse the job. Refusing stops the reviewer rendering a verdict, so the
  // pull request can never be approved and therefore can never merge, and the budget that was meant
  // to bound the work ends up preventing it from ever finishing. Past the budget the job keeps its
  // own limit and is told to land or report rather than to start anything.
  const overBudget = cycleDeadline !== null && now >= cycleDeadline - 300;
  const boundByCycle = cycleDeadline !== null && !overBudget && cycleDeadline < jobDeadline;
  const deadline = boundByCycle ? cycleDeadline : jobDeadline;
  const cutoff = deadline - 300;
  const wrapup = cutoff - 300;
  if (now >= cutoff) throw Error('The agent execution budget is exhausted. Preserve existing work and report the incomplete run.');
  const remaining = cutoff - now;
  const utc = seconds => new Date(seconds * 1000).toISOString();
  const clock = clockTools ? 'Check the current UTC clock with date before long tool calls. Bound each command timeout below the time remaining to the cutoff and recheck after it returns.' : 'Use the supplied clock snapshot and answer promptly without tools or waiting.';
  const delivery = 'Delivery target set September 14, 2026: finish the existing PR backlog and the remaining 119 issues within 32 hours. Count an issue as finished only when its acceptance criteria are met and its PR is merged, not when a draft, comment or partial fix is published.';
  const focus = clockTools ? 'Prioritize completing the assigned issue and removing blockers to merge. Start from the existing branch, review history and current evidence. Identify the shortest complete implementation and validation path before exploring. Fix the underlying defect without unrelated refactors or speculative scope. Run focused checks first and repeat expensive checks only when changes or unresolved risks justify them. Reviewers must inspect the cumulative PR and verify prior fixes, then return a supported verdict promptly. Do not wait for full or emulator CI that is gated on your approval. After 10 minutes without new evidence or progress on the same blocker, report the exact blocker and required next action instead of repeating unchanged attempts. Preserve required checks and honest coverage gaps. Publish verified work and hand off immediately when ready rather than using the entire allowance.' : 'Answer the specific review question directly from the supplied evidence and state any missing evidence promptly.';
  const spent = `That budget ran out at ${cycleDeadline === null ? '' : utc(cycleDeadline)}, so this pull request is over its allowance. Do not start new work, do not widen scope and do not begin an investigation you cannot finish. Land what is already here, or state precisely what blocks the merge and hand it back. A reviewer past the budget still returns its verdict rather than withholding one, because withholding is what would strand the branch.`;
  const cycle = cycleDeadline === null ? '' : ` Every worker and reviewer job on this pull request shares one ${cycleMinutes} minute budget, which started at ${utc(cycleStartedAt)} and expires at ${utc(cycleDeadline)}. ${overBudget ? spent : boundByCycle ? 'That is earlier than this job limit, so the cutoff above is the pull request budget rather than your own job. Spend what is left on reaching a merge or on reporting precisely what blocks one.' : 'This job limit falls first, so leave the branch in a state the next job can finish inside what remains of that budget.'}`;
  const text = `${delivery} ${focus} Job time budget measured at ${utc(now)}: ${remaining} seconds remain until the agent cutoff ${utc(cutoff)}. Begin wrapping up at ${utc(wrapup)}${now >= wrapup ? ' (already reached, wrap up now)' : ''}. The GitHub job deadline is ${utc(jobDeadline)}, with its final 300 seconds reserved for publication and cleanup.${cycle} ${clock} At wrap-up, stop starting new work, preserve partial changes on the existing draft PR when applicable, and report completed checks and verification gaps. Finish your response before the cutoff. Never invent successful checks or approval to meet the deadline.`;
  return { deadline, jobDeadline, cycleDeadline, boundByCycle, overBudget, cutoff, wrapup, remaining, text };
}

// The workflow has the pull request's created_at as an ISO timestamp, so it is converted here rather
// than in shell. An absent value leaves the job on its own limit, which is what a dispatch without a
// pull request wants.
export function cycleStart(value) {
  if (value === undefined || value.trim() === '') return null;
  const parsed = Date.parse(value);
  if (Number.isNaN(parsed)) throw Error('Invalid agent cycle start timestamp');
  return Math.floor(parsed / 1000);
}

function main() {
  const clockTools = process.env.AGENT_CLOCK_TOOLS ?? 'true';
  if (!['true', 'false'].includes(clockTools)) throw Error('Invalid clock tool mode');
  const budget = agentDeadline({ startedAt: Number(process.env.AGENT_JOB_STARTED_AT), timeoutMinutes: Number(process.env.AGENT_JOB_TIMEOUT_MINUTES), cycleStartedAt: cycleStart(process.env.AGENT_CYCLE_STARTED_AT), clockTools: clockTools === 'true' });
  if (!process.env.GITHUB_ENV) throw Error('The workflow environment file is required');
  appendFileSync(process.env.GITHUB_ENV, `AGENT_TIME_BUDGET=${budget.text}\n`);
  console.log(budget.text);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) { console.error(`::error::${error.message}`); process.exitCode = 1; }
}
