import assert from 'node:assert/strict';
import test from 'node:test';
import { claimIssue } from './prepare-codex-worker.mjs';
import { loadRoster, resolveTarget } from './bot-identities.mjs';
import { reviewProvider } from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const bot = { login: 'cx2-ag-gemini-agate[bot]', slug: 'cx2-ag-gemini-agate', id: 17 };

test('Gemini selects its own reviewer before and after review-label cleanup', () => {
  const pr = { head: { ref: 'gemini/issue-42' }, labels: [] };
  assert.equal(reviewProvider(pr), 'gemini');
  assert.equal(reviewProvider({ ...pr, labels: [{ name: 'gemini-review' }] }), 'gemini');
  assert.throws(() => reviewProvider({ ...pr, labels: [{ name: 'gemini-review' }, { name: 'claude-review' }] }));
});

test('Gemini reserves canonical issue targets and refuses foreign provider branches', async () => {
  const api = async (method, endpoint) => {
    if (endpoint.includes('/contents/')) return null;
    if (endpoint.endsWith('/issues/42')) return { state: 'open' };
    if (endpoint.endsWith('/pulls/84')) return { head: { repo: { full_name: repository }, ref: 'claude/issue-42' } };
    throw Error(endpoint);
  };
  assert.equal((await resolveTarget({ repository, provider: 'gemini', role: 'executor', issue: 42 }, api)).branch, 'gemini/issue-42');
  await assert.rejects(resolveTarget({ repository, provider: 'gemini', role: 'executor', pr: 84 }, api), /another provider/);
});

test('all three providers reject an issue explicitly claimed by another provider', async () => {
  for (const provider of ['codex', 'claude', 'gemini']) for (const owner of ['codex', 'claude', 'gemini']) {
    const writes = [];
    const api = async (method, endpoint, body) => {
      if (method !== 'GET') { writes.push({ endpoint, body }); return {}; }
      if (endpoint.endsWith('/issues/42')) return { state: 'open', assignees: [], labels: [{ name: owner }] };
      if (endpoint.startsWith('users/')) return { type: 'Bot', login: bot.login, id: bot.id };
      if (endpoint.endsWith('/heads/main')) return { object: { sha: 'a'.repeat(40) } };
      return null;
    };
    const work = claimIssue({ repository, provider, number: 42, run: 123, expectedBranch: `${provider}/issue-42`, bot }, api);
    if (owner !== provider) {
      await assert.rejects(work, /another provider/);
      assert.equal(writes.length, 0);
    } else {
      assert.equal((await work).branch, `${provider}/issue-42`);
      assert.ok(writes.some(call => call.body.ref === `refs/heads/${provider}/issue-42`));
    }
  }
});

test('Gemini has twelve configured executors and twelve separate reviewers', () => {
  const roster = loadRoster();
  for (const role of ['executor', 'reviewer']) {
    const pool = roster.filter(bot => bot.provider === 'gemini' && bot.role === role);
    assert.equal(pool.length, 12);
    for (const bot of pool) assert.ok(bot.appId > 0 && bot.userId > 0 && bot.clientId);
  }
});
