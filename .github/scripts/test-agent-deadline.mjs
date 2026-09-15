import assert from 'node:assert/strict';
import test from 'node:test';
import { execFileSync, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { agentDeadline, cycleBudgetMinutes, cycleStart } from './agent-deadline.mjs';

const startedAt = 1789232400;
const scripts = dirname(fileURLToPath(import.meta.url));

test('budgets deduct startup time and reserve wrap-up and publication time', () => {
  for (const timeoutMinutes of [15, 30, 45]) {
    const budget = agentDeadline({ startedAt, timeoutMinutes, now: startedAt + 120 });
    assert.equal(budget.deadline, startedAt + timeoutMinutes * 60);
    assert.equal(budget.cutoff, budget.deadline - 300);
    assert.equal(budget.wrapup, budget.cutoff - 300);
    assert.equal(budget.remaining, (timeoutMinutes - 5) * 60 - 120);
    assert.ok(budget.text.includes(new Date(budget.cutoff * 1000).toISOString()));
    assert.match(budget.text, /Check the current UTC clock with date/);
    assert.match(budget.text, /Bound each command timeout/);
    assert.match(budget.text, /existing draft PR/);
    assert.match(budget.text, /verification gaps/);
    assert.match(budget.text, /Never invent successful checks or approval/);
  }
});

test('late starts wrap up immediately and exhausted or invalid clocks refuse execution', () => {
  assert.match(agentDeadline({ startedAt, timeoutMinutes: 30, now: startedAt + 1200 }).text, /already reached, wrap up now/);
  for (const now of [startedAt + 1500, startedAt + 1800]) assert.throws(() => agentDeadline({ startedAt, timeoutMinutes: 30, now }), /exhausted/);
  for (const values of [{ startedAt: 0 }, { now: startedAt - 1 }, { timeoutMinutes: 10 }, { timeoutMinutes: NaN }, { timeoutMinutes: 30.5 }]) assert.throws(() => agentDeadline({ startedAt, timeoutMinutes: 30, now: startedAt, ...values }), /Invalid/);
});

test('tool-free answers receive timestamps without conflicting command instructions', () => {
  const budget = agentDeadline({ startedAt, timeoutMinutes: 15, now: startedAt + 120, clockTools: false });
  assert.ok(budget.text.includes(new Date(budget.cutoff * 1000).toISOString()));
  assert.match(budget.text, /Finish your response before the cutoff/);
  assert.doesNotMatch(budget.text, /with date|command timeout|recheck after/);
  assert.match(budget.text, /supplied clock snapshot/);
});

test('actual CLI exports the computed prompt budget and refuses an exhausted fake clock', () => {
  const root = join(scripts, '../../.Internal/workspaces/deadline-tests'); mkdirSync(root, { recursive: true });
  for (const elapsed of [90, 1500]) for (const clockTools of [true, false]) {
    const directory = mkdtempSync(join(root, 'clock-')); const environment = join(directory, 'github-env');
    const clock = `data:text/javascript,${encodeURIComponent(`Date.now = () => ${(startedAt + elapsed) * 1000};`)}`;
    const run = spawnSync(process.execPath, ['--import', clock, join(scripts, 'agent-deadline.mjs')], { encoding: 'utf8', env: { ...process.env, AGENT_JOB_STARTED_AT: String(startedAt), AGENT_JOB_TIMEOUT_MINUTES: '30', AGENT_CLOCK_TOOLS: String(clockTools), GITHUB_ENV: environment } });
    assert.equal(run.status, elapsed < 1500 ? 0 : 1, run.stderr);
    if (elapsed < 1500) {
      const budget = agentDeadline({ startedAt, timeoutMinutes: 30, now: startedAt + elapsed, clockTools });
      assert.equal(readFileSync(environment, 'utf8'), `AGENT_TIME_BUDGET=${budget.text}\n`);
    } else {
      assert.equal(existsSync(environment), false);
      assert.match(run.stderr, /budget is exhausted/);
    }
  }
});

test('executor workflows capture start first and match their actual job timeout', () => {
  for (const name of ['agent-codex.yml', 'agent.yml']) {
    const workflow = join(scripts, '../workflows', name);
    const job = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0])["jobs"]["respond"])', workflow], { encoding: 'utf8' }));
    assert.equal(job['timeout-minutes'], 30);
    assert.equal(job.env.AGENT_JOB_TIMEOUT_MINUTES, job['timeout-minutes']);
    assert.equal(job.steps[0].run, 'echo "AGENT_JOB_STARTED_AT=$(date +%s)" >> "$GITHUB_ENV"');
    const budgetIndex = job.steps.findIndex(step => step.run === 'node .github/scripts/agent-deadline.mjs');
    const installIndex = job.steps.findIndex(step => /npm install -g/.test(step.run ?? ''));
    assert.ok(budgetIndex > installIndex);
    if (name === 'agent-codex.yml') {
      const promptIndex = job.steps.findIndex(step => step.name === 'Assemble the prompt');
      assert.ok(promptIndex > budgetIndex);
      assert.ok(job.steps[promptIndex].run.includes('"$AGENT_TIME_BUDGET"'));
    } else {
      const run = job.steps.find(step => step.name === 'Run the agent');
      assert.ok(run.with.prompt.includes('${{ env.AGENT_TIME_BUDGET }}'));
      const general = job.steps.find(step => step.name === 'Answer an unassigned request');
      assert.ok(general.run.includes('"$AGENT_TIME_BUDGET" "$TASK"'));
      assert.ok(general.run.includes("--allowedTools 'Read,Glob,Grep,Bash(date:*)'"));
    }
  }
});

test('the pull request budget is shared across jobs and overrides a later job limit', () => {
  // A reviewer starting with 30 minutes of the cycle left must be told 30 minutes, not its own 120.
  const cycleStartedAt = startedAt - (cycleBudgetMinutes - 30) * 60;
  const bound = agentDeadline({ startedAt, timeoutMinutes: 120, cycleStartedAt, now: startedAt });
  assert.equal(bound.boundByCycle, true);
  assert.equal(bound.cycleDeadline, cycleStartedAt + cycleBudgetMinutes * 60);
  assert.equal(bound.deadline, bound.cycleDeadline);
  assert.equal(bound.jobDeadline, startedAt + 120 * 60);
  assert.equal(bound.remaining, 30 * 60 - 300);
  assert.match(bound.text, /the cutoff above is the pull request budget rather than your own job/);
  assert.ok(bound.text.includes(new Date(bound.jobDeadline * 1000).toISOString()));

  // A fresh cycle leaves the job limit in charge, and the prompt still names the shared budget.
  const free = agentDeadline({ startedAt, timeoutMinutes: 30, cycleStartedAt: startedAt, now: startedAt });
  assert.equal(free.boundByCycle, false);
  assert.equal(free.deadline, free.jobDeadline);
  assert.match(free.text, /This job limit falls first/);

  // With no cycle supplied the prompt says nothing about one, so a dispatch run is unchanged.
  assert.doesNotMatch(agentDeadline({ startedAt, timeoutMinutes: 30, now: startedAt }).text, /shares one/);
});

test('a spent pull request budget still lets the job run, so a branch cannot be stranded', () => {
  // Refusing here would stop the reviewer returning a verdict, so the PR could never be approved and
  // therefore never merge. The budget has to bound new work without preventing the work finishing.
  const spent = startedAt - cycleBudgetMinutes * 60;
  const over = agentDeadline({ startedAt, timeoutMinutes: 120, cycleStartedAt: spent, now: startedAt });
  assert.equal(over.overBudget, true);
  assert.equal(over.boundByCycle, false);
  assert.equal(over.deadline, over.jobDeadline, 'a spent cycle falls back to the job limit rather than refusing');
  assert.ok(over.remaining > 0);
  assert.match(over.text, /over its allowance/);
  assert.match(over.text, /still returns its verdict/);

  // The job's own limit is the one that can still refuse, because that clock really has run out.
  assert.throws(() => agentDeadline({ startedAt, timeoutMinutes: 30, now: startedAt + 1500 }), /execution budget is exhausted/);
  // Four hours is the ceiling, so a job asking for more is refused rather than granted.
  for (const values of [{ cycleMinutes: cycleBudgetMinutes + 1 }, { cycleMinutes: 10 }, { cycleMinutes: 60.5 }, { cycleStartedAt: startedAt + 1 }, { cycleStartedAt: 0 }])
    assert.throws(() => agentDeadline({ startedAt, timeoutMinutes: 30, cycleStartedAt: startedAt, now: startedAt, ...values }), /Invalid agent cycle/);
  assert.equal(cycleBudgetMinutes, 240);
});

// The Claude reviewer does not read AGENT_TIME_BUDGET. It calls agentDeadline itself, so the cycle
// has to reach it through its own environment or the longest job in the loop escapes the cap.
test('the Claude review runner is given the cycle and the workflow supplies it', async () => {
  const { runClaudeReview } = await import('./run-claude-review.mjs');
  const spent = startedAt - cycleBudgetMinutes * 60;
  let seen = '';
  const execute = (_cmd, _args, options) => { seen = options.input; return { status: 0, stdout: JSON.stringify({ type: 'result', subtype: 'success', is_error: false, structured_output: { verdict: 'APPROVE' } }) }; };
  runClaudeReview({ startedAt, timeoutMinutes: 120, cycleStartedAt: spent, now: startedAt, prompt: 'body', schema: '{}', model: 'sonnet', effort: 'high', allowedTools: '' }, execute);
  assert.match(seen, /shares one 240 minute budget/, 'the reviewer prompt must carry the shared budget');
  assert.match(seen, /over its allowance/, 'a spent cycle must reach the reviewer rather than being invisible to it');

  const job = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0])["jobs"]["review"])', join(scripts, '../workflows/agent-review.yml')], { encoding: 'utf8' }));
  const step = job.steps.find(entry => entry.name === 'Review the pull request');
  assert.equal(step.env.AGENT_CYCLE_STARTED_AT, '${{ github.event.pull_request.created_at }}');
});

test('the cycle start reads the pull request timestamp the workflow has', () => {
  // date -u -r 1789474640 prints this timestamp, so the constant is checked outside the code under test.
  assert.equal(cycleStart('2026-09-15T12:17:20Z'), 1789474640);
  for (const absent of [undefined, '', '   ']) assert.equal(cycleStart(absent), null);
  assert.throws(() => cycleStart('not a time'), /Invalid agent cycle start timestamp/);
});

// Every worker and reviewer job has to receive the cycle start, and it has to receive it from
// somewhere its own triggers actually populate. Reading github.event.pull_request in a workflow that
// never fires on a pull request yields an empty string and silently disables the shared budget, which
// is what an assertion over the expression text alone cannot see.
test('every worker and reviewer job reads the cycle start from a source its triggers populate', () => {
  const load = (file, expression) => JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', expression, join(scripts, '../workflows', file)], { encoding: 'utf8' }));
  const jobs = [['agent.yml', 'respond'], ['agent-codex.yml', 'respond'], ['agent-gemini.yml', 'respond'],
                ['agent-review.yml', 'review'], ['agent-review.yml', 'codex-review'], ['agent-review.yml', 'gemini-review']];
  for (const [file, jobName] of jobs) {
    const workflow = load(file, 'puts JSON.generate(YAML.load_file(ARGV[0]))');
    const job = workflow.jobs[jobName];
    const triggers = Object.keys(workflow.on ?? workflow.true ?? {});
    const budget = job.steps.find(step => step.run === 'node .github/scripts/agent-deadline.mjs');
    const source = budget?.env?.AGENT_CYCLE_STARTED_AT;
    assert.ok(source, `${file}: the ${jobName} job must receive AGENT_CYCLE_STARTED_AT`);
    // One pull_request trigger is not enough. The job runs for every trigger the workflow declares,
    // so the event may only be trusted when no other kind of trigger can start it.
    if (triggers.every(trigger => trigger === 'pull_request')) {
      assert.ok(source.includes('github.event.pull_request.created_at'), `${file}: ${jobName} only fires on pull_request, so it can read the event`);
      continue;
    }
    // No pull_request trigger, so the event carries none and the value has to be resolved instead.
    assert.ok(!source.includes('github.event.pull_request'), `${file}: ${jobName} never fires on a pull_request, so the event cannot supply the cycle start`);
    const reference = source.slice(source.indexOf('needs.') + 'needs.'.length, source.indexOf(' }}'));
    const [producer, outputs, output] = reference.split('.');
    assert.ok(source.includes('needs.') && outputs === 'outputs' && output, `${file}: ${jobName} must take the cycle start from a resolved job output`);
    assert.ok(job.needs.includes(producer), `${file}: ${jobName} must declare needs on ${producer}`);
    assert.ok(workflow.jobs[producer].outputs?.[output], `${file}: the ${producer} job must publish ${output}`);
    const step = workflow.jobs[producer].steps.find(entry => entry.id === 'cycle');
    assert.ok(step?.run.includes('gh pr list'), `${file}: ${producer} must look the pull request up from the resolved branch`);
    assert.equal(step.env.BRANCH, '${{ steps.target.outputs.branch }}');
  }
});

test('review and discussion deadlines precede inference and match each job limit', () => {
  for (const [name, jobName, modelStep] of [['agent-review.yml', 'review', 'Review the pull request'], ['agent-review.yml', 'codex-review', 'Review with subscription login'], ['agent-review-discussion.yml', 'answer', 'Answer with Codex']]) {
    const workflow = join(scripts, '../workflows', name);
    const job = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0])["jobs"][ARGV[1]])', workflow, jobName], { encoding: 'utf8' }));
    assert.equal(job.steps[0].run, 'echo "AGENT_JOB_STARTED_AT=$(date +%s)" >> "$GITHUB_ENV"');
    const budgetIndex = job.steps.findIndex(step => step.run === 'node .github/scripts/agent-deadline.mjs');
    assert.equal(job.steps[budgetIndex].env.AGENT_JOB_TIMEOUT_MINUTES, job['timeout-minutes']);
    assert.ok(budgetIndex < job.steps.findIndex(step => step.name === modelStep));
    if (jobName === 'review') {
      const model = job.steps.find(step => step.name === modelStep);
      assert.ok(model.env.REVIEW_PROMPT.includes('${{ env.AGENT_TIME_BUDGET }}'));
      assert.ok(model.env.REVIEW_ALLOWED_TOOLS.includes('Bash(date:*)'));
      // The allowed gh readers need a credential, and publication stays out of the model's hands.
      assert.equal(model.env.GH_TOKEN, '${{ steps.bot.outputs.token }}');
      for (const reader of ['gh pr view', 'gh pr diff', 'gh issue view', 'gh issue list', 'gh issue create']) assert.ok(model.env.REVIEW_ALLOWED_TOOLS.includes(`Bash(${reader}:*)`));
      for (const publisher of ['gh pr review', 'gh api', 'gh pr merge']) assert.ok(!model.env.REVIEW_ALLOWED_TOOLS.includes(`Bash(${publisher}`));
    } else if (jobName === 'codex-review') {
      assert.ok(job.steps.find(step => step.name === modelStep).run.includes('$AGENT_TIME_BUDGET'));
    } else {
      assert.equal(job.steps[budgetIndex].env.AGENT_CLOCK_TOOLS, 'false');
      assert.ok(job.steps.find(step => step.name === 'Include the deadline in the question').run.includes('"$AGENT_TIME_BUDGET"'));
      for (const provider of ['Codex', 'Claude']) assert.ok(job.steps.find(step => step.name === `Answer with ${provider}`).run.includes('review-discussion-timed-prompt.txt'));
    }
  }
});
