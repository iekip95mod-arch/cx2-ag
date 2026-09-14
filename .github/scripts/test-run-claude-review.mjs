import assert from 'node:assert/strict';
import test from 'node:test';
import { runClaudeReview } from './run-claude-review.mjs';

const options = { startedAt: 1000, now: 1060, timeoutMinutes: 30, prompt: 'Review this revision', schema: '{"type":"object"}', model: 'opus', effort: 'high', allowedTools: 'Read,Bash(node --test:*)' };
const verdict = { verdict: 'APPROVED', body: 'Verified the regression', comments: [] };

const completedRun = { status: 0, stdout: JSON.stringify({ type: 'result', subtype: 'success', is_error: false, structured_output: verdict }) };

test('Claude CLI publishes only completed structured output within the remaining budget', () => {
  delete process.env.REVIEW_GH_TOKEN;
  const review = runClaudeReview(options, (binary, args, execution) => {
    assert.equal(binary, 'claude');
    assert.equal(args[args.indexOf('--model') + 1], 'opus');
    assert.equal(args[args.indexOf('--effort') + 1], 'high');
    assert.equal(args[args.indexOf('--max-turns') + 1], '256');
    assert.equal(args[args.indexOf('--json-schema') + 1], options.schema);
    assert.equal(args[args.indexOf('--allowedTools') + 1], options.allowedTools);
    assert.equal(execution.timeout, 1440000);
    assert.match(execution.input, /Review this revision/);
    assert.equal(execution.env.GH_TOKEN, '');
    assert.equal(execution.env.GITHUB_TOKEN, '');
    assert.equal(execution.env.ANTHROPIC_API_KEY, '');
    return { status: 0, stdout: JSON.stringify({ type: 'result', subtype: 'success', is_error: false, structured_output: verdict }) };
  });
  assert.deepEqual(JSON.parse(review), verdict);
});

test('Claude CLI refuses partial, failed, missing and malformed verdicts', () => {
  const completed = { type: 'result', subtype: 'success', is_error: false, structured_output: verdict };
  for (const envelope of [{ ...completed, is_error: true }, { ...completed, subtype: 'error_max_turns' }, { ...completed, structured_output: undefined }, { ...completed, structured_output: 'not an object' }]) {
    assert.throws(() => runClaudeReview(options, () => ({ status: 0, stdout: JSON.stringify(envelope) })), /complete structured review/);
  }
  assert.throws(() => runClaudeReview(options, () => ({ status: 1, stdout: JSON.stringify(completed) })), /execution failed/);
  assert.throws(() => runClaudeReview(options, () => ({ error: Error('timeout'), stdout: JSON.stringify(completed) })), /execution failed/);
  assert.throws(() => runClaudeReview(options, () => ({ status: 0, stdout: 'invalid' })), SyntaxError);
  assert.throws(() => runClaudeReview({ ...options, now: 2500 }, () => assert.fail('Expired execution')), /exhausted/);
});

test('Claude CLI hands the reviewer the review credential and no other secret', () => {
  process.env.REVIEW_GH_TOKEN = 'reviewer-github-token';
  try {
    runClaudeReview(options, (binary, args, execution) => {
      assert.equal(execution.env.GH_TOKEN, 'reviewer-github-token');
      assert.equal(execution.env.GITHUB_TOKEN, '');
      assert.equal(execution.env.ANTHROPIC_API_KEY, '');
      return completedRun;
    });
  } finally {
    delete process.env.REVIEW_GH_TOKEN;
  }
});
