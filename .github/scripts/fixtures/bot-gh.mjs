#!/usr/bin/env node
import { appendFileSync } from 'node:fs';

const args = process.argv.slice(2);
appendFileSync(process.env.BOT_TEST_LOG, JSON.stringify(args) + '\n');
if (args[0] === 'api' && args[2] === 'GET' && args[3] === 'repos/iekip95mod-arch/cx2-ag/issues/42') {
  console.log(JSON.stringify({ number: 42, state: 'open', assignees: [], labels: [] }));
} else {
  console.error('Unexpected GitHub request');
  process.exitCode = 1;
}
