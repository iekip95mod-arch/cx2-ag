#!/usr/bin/env node
import { appendFileSync, readFileSync } from 'node:fs';

const fixture = JSON.parse(readFileSync(process.env.FEEDBACK_FIXTURE, 'utf8'));
const args = process.argv.slice(2);
const body = args.includes('--input') ? JSON.parse(readFileSync(0, 'utf8')) : undefined;
appendFileSync(fixture.log, JSON.stringify({ args, body }) + '\n');
const [command, flag, method, endpoint] = args;
if (command !== 'api' || flag !== '--method') throw Error('Unexpected CLI arguments');
if (fixture.fail === endpoint) { console.error('Fixture API failure (HTTP 403)'); process.exit(1); }
if (method === 'POST' && endpoint.endsWith('/dispatches')) process.exit(0);
if (!Object.hasOwn(fixture.responses, endpoint)) throw Error(`Unexpected ${method} ${endpoint}`);
console.log(JSON.stringify(fixture.responses[endpoint]));
