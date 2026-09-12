#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { appendFileSync, readFileSync, writeFileSync } from 'node:fs';

const config = JSON.parse(readFileSync(process.env.WORKER_FIXTURE, 'utf8'));
const args = process.argv.slice(2);
const body = args.includes('--input') ? JSON.parse(readFileSync(0, 'utf8')) : undefined;
appendFileSync(config.log, JSON.stringify({ args, body }) + '\n');
function fail(message) { console.error(message); process.exit(1); }
function git(...args) { return execFileSync('git', ['--git-dir', config.remote, ...args], { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim(); }
if (args.join(' ') === 'auth setup-git') {
  if (config.authFailure) fail('Fixture authentication failure');
  process.exit(0);
}
const [command, flag, method, endpoint] = args;
if (command !== 'api' || flag !== '--method') fail('Unexpected GitHub CLI arguments');
if (endpoint === config.failEndpoint) fail(`Fixture API error (HTTP ${config.failStatus})`);
let reply;
if (method === 'GET' && endpoint.endsWith('/issues/42')) reply = config.issue;
else if (method === 'GET' && endpoint.endsWith(`/issues/${config.number}`)) reply = config.prIssue;
else if (method === 'GET' && endpoint === 'users/worker-amber[bot]') reply = { login: 'worker-amber[bot]', id: 7, type: 'Bot' };
else if (method === 'GET' && (endpoint.includes('/assignees/') || endpoint.includes('/labels/'))) fail('Not available (HTTP 404)');
else if (method === 'POST' && endpoint.endsWith('/labels')) reply = {};
else if (method === 'GET' && endpoint.endsWith(`/pulls/${config.number ?? 42}`)) reply = config.pr;
else if (method === 'GET' && endpoint.includes('/git/ref/heads/')) {
  const branch = endpoint.split('/git/ref/heads/')[1];
  try { reply = { object: { sha: git('rev-parse', '--verify', `refs/heads/${branch}`) } }; }
  catch { fail('Reference missing (HTTP 404)'); }
} else if (method === 'POST' && endpoint.endsWith('/git/refs')) {
  git('update-ref', body.ref, body.sha, '');
  reply = { ref: body.ref, object: { sha: body.sha } };
} else if (method === 'POST' && endpoint.endsWith('/assignees')) reply = { assignees: body.assignees.map(login => ({ login })) };
else if (method === 'POST' && endpoint.endsWith('/comments')) reply = { id: 1 };
else if (method === 'GET' && endpoint.includes('/pulls?')) reply = config.createdPr ? [config.createdPr] : [];
else if (method === 'POST' && endpoint.endsWith('/pulls')) {
  reply = { number: 90, html_url: 'https://github.com/iekip95mod-arch/cx2-ag/pull/90', state: 'open', user: { login: 'worker-amber[bot]' }, ...body, head: { repo: { full_name: 'iekip95mod-arch/cx2-ag' }, ref: body.head } };
  writeFileSync(process.env.WORKER_FIXTURE, JSON.stringify({ ...config, createdPr: reply }));
}
else fail(`Unexpected endpoint ${method} ${endpoint}`);
console.log(JSON.stringify(reply));
