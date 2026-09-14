import assert from 'node:assert/strict';
import test from 'node:test';
import { execFileSync, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { agentDeadline } from './agent-deadline.mjs';

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
    } else if (jobName === 'codex-review') {
      assert.ok(job.steps.find(step => step.name === modelStep).run.includes('$AGENT_TIME_BUDGET'));
    } else {
      assert.equal(job.steps[budgetIndex].env.AGENT_CLOCK_TOOLS, 'false');
      assert.ok(job.steps.find(step => step.name === 'Include the deadline in the question').run.includes('"$AGENT_TIME_BUDGET"'));
      for (const provider of ['Codex', 'Claude']) assert.ok(job.steps.find(step => step.name === `Answer with ${provider}`).run.includes('review-discussion-timed-prompt.txt'));
    }
  }
});
