#!/usr/bin/env node
import { appendFileSync, readFileSync } from 'node:fs';

const args = process.argv.slice(2);
appendFileSync(process.env.BOT_TEST_LOG, JSON.stringify(args) + '\n');
if (args[0] === 'api' && args[2] === 'GET' && /^repos\/iekip95mod-arch\/cx2-ag\/issues\/\d+$/.test(args[3])) {
  console.log(JSON.stringify({ number: Number(args[3].split('/').at(-1)), state: 'open', assignees: [], labels: [] }));
} else if (args[0] === 'api' && args[2] === 'GET' && args[3] === 'repos/iekip95mod-arch/cx2-ag/contents/assignments.json?ref=bot-assignments') {
  if (process.env.BOT_TEST_ROSTER) {
    const roster = JSON.parse(readFileSync(process.env.BOT_TEST_ROSTER, 'utf8'));
    const assignments = roster.filter(bot => bot.role === 'reviewer' && bot.provider === process.env.BOT_TEST_PROVIDER).map((bot, index) => ({ key: `${bot.provider}/reviewer/issue-${index + 1}`, issue: index + 1, pr: null, branch: `${bot.provider}/issue-${index + 1}`, provider: bot.provider, role: 'reviewer', slug: bot.slug, released: false }));
    console.log(JSON.stringify({ sha: 'fixture-revision', content: Buffer.from(JSON.stringify({ version: 1, assignments })).toString('base64') }));
  } else {
  console.error('(HTTP 404)');
  process.exitCode = 1;
  }
} else {
  console.error('Unexpected GitHub request');
  process.exitCode = 1;
}
