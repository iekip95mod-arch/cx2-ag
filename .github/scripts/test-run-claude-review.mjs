import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { runClaudeReview } from './run-claude-review.mjs';

const options = { startedAt: 1000, now: 1060, timeoutMinutes: 30, prompt: 'Review this revision', schema: '{"type":"object"}', model: 'opus', effort: 'high', allowedTools: 'Read,Bash(node --test:*)' };
const verdict = { verdict: 'APPROVED', body: 'Verified the regression', comments: [] };

process.env.GH_TOKEN = 'assigned-reviewer-token';

test('Claude CLI reads GitHub through the assigned reviewer token and publishes only completed structured output within the remaining budget', () => {
  const review = runClaudeReview(options, (binary, args, execution) => {
    assert.equal(binary, 'claude');
    assert.equal(args[args.indexOf('--model') + 1], 'opus');
    assert.equal(args[args.indexOf('--effort') + 1], 'high');
    assert.equal(args[args.indexOf('--max-turns') + 1], '256');
    assert.equal(args[args.indexOf('--json-schema') + 1], options.schema);
    assert.equal(args[args.indexOf('--allowedTools') + 1], options.allowedTools);
    assert.equal(execution.timeout, 1440000);
    assert.match(execution.input, /Review this revision/);
    assert.equal(execution.env.GH_TOKEN, 'assigned-reviewer-token');
    assert.equal(execution.env.GITHUB_TOKEN, '');
    assert.equal(execution.env.ANTHROPIC_API_KEY, '');
    assert.deepEqual(execution.stdio, ['pipe', 'pipe', 'inherit']);
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

test('A killed CLI records its exit status and signal where a reaped job leaves them readable', () => {
  const summary = join(mkdtempSync(join(tmpdir(), 'claude-review-summary-')), 'summary.md');
  process.env.GITHUB_STEP_SUMMARY = summary;
  try {
    assert.throws(() => runClaudeReview(options, () => ({ status: null, signal: 'SIGTERM', stdout: '' })), /exit status none, signal SIGTERM/);
    assert.match(readFileSync(summary, 'utf8'), /exit status none, signal SIGTERM/);
    assert.throws(() => runClaudeReview(options, () => ({ error: Error('spawn failed'), status: null, stdout: '' })), /error spawn failed/);
    assert.match(readFileSync(summary, 'utf8'), /exit status none, signal none, error spawn failed/);
  } finally {
    delete process.env.GITHUB_STEP_SUMMARY;
  }
});
