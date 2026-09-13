import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { loadRoster } from './bot-identities.mjs';

test('App authentication references only literal secrets from the selected pool', () => {
  const pools = { 'agent-codex': [['codex', 'executor']], agent: [['claude', 'executor']], 'agent-review': [['claude', 'reviewer'], ['codex', 'reviewer']], 'agent-review-request': [[null, 'reviewer']], 'agent-review-discussion': [[null, 'reviewer']] };
  for (const [workflow, expected] of Object.entries(pools)) {
    const source = readFileSync(new URL(`../workflows/${workflow}.yml`, import.meta.url), 'utf8');
    assert.doesNotMatch(source, /secrets\s*\[|toJSON\(secrets\)/i, workflow);
    const selections = [...source.matchAll(/private-key: (.+)/g)];
    assert.equal(selections.length, expected.length);
    for (let index = 0; index < expected.length; index++) {
      const [provider, role] = expected[index];
      const pool = loadRoster().filter(bot => (!provider || bot.provider === provider) && bot.role === role);
      const expression = selections[index][1].slice(3, -3);
      assert.deepEqual([...expression.matchAll(/secrets\.([A-Z0-9_]+)/g)].map(match => match[1]).sort(), pool.map(bot => bot.secretName).sort());
      for (const name of [...pool.map(bot => bot.secretName), 'UNKNOWN_SECRET']) {
        const translated = expression.replace(/(?:needs|steps)\.[a-z_-]+\.outputs\.secret_name/g, JSON.stringify(name)).replace(/secrets\.([A-Z0-9_]+)/g, (_, key) => JSON.stringify(`value:${key}`));
        assert.equal(Function(`return (${translated})`)(), name === 'UNKNOWN_SECRET' ? '' : `value:${name}`);
      }
    }
  }
});
