#!/usr/bin/env node
import { readFileSync } from 'node:fs';
const [command, endpoint] = process.argv.slice(2);
if (command !== 'api') throw Error('Unexpected history command');
const responses = JSON.parse(readFileSync(process.env.HISTORY_FIXTURE, 'utf8'));
if (!Object.hasOwn(responses, endpoint)) throw Error('Unexpected history endpoint');
console.log(JSON.stringify(responses[endpoint]));
