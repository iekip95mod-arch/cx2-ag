import { appendFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { readAssignment } from './bot-identities.mjs';
import { requestGitHub } from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const root = `repos/${repository}`;

async function ownedBranch(pr, api, assignment) {
  if (pr.state !== 'open' || pr.base?.ref !== 'main' || pr.base.repo?.full_name !== repository || pr.head?.repo?.full_name !== repository) return null;
  const provider = /^(codex|claude)\//.exec(pr.head.ref)?.[1];
  if (!provider) return null;
  let identity;
  try { identity = await assignment({ repository, provider, role: 'executor', pr: pr.number }, api); }
  catch (error) { if (error.message === 'No active bot assignment for this target') return null; throw error; }
  if (identity.branch !== pr.head.ref || (pr.user.login !== repository.split('/')[0] && (pr.user.type !== 'Bot' || pr.user.login !== identity.login || pr.user.id !== identity.userId))) return null;
  return identity;
}

export async function discoverBranches(api, assignment = readAssignment, number) {
  if (number !== undefined) {
    if (!Number.isSafeInteger(number) || number < 1) throw Error('Invalid branch discovery target');
    const pr = await api('GET', `${root}/pulls/${number}`);
    const identity = await ownedBranch(pr, api, assignment);
    return { include: identity ? [{ pr: pr.number, branch: identity.branch, provider: identity.provider, login: identity.login, app_id: identity.appId, secret_name: identity.secretName }] : [] };
  }
  const include = [];
  for (let page = 1; page <= 10; page++) {
    const pulls = await api('GET', `${root}/pulls?state=open&base=main&per_page=100&page=${page}`);
    for (const pr of pulls) {
      const identity = await ownedBranch(pr, api, assignment);
      if (identity) include.push({ pr: pr.number, branch: identity.branch, provider: identity.provider, login: identity.login, app_id: identity.appId, secret_name: identity.secretName });
    }
    if (pulls.length < 100) return { include };
  }
  throw Error('Open pull requests exceed the branch update lookup limit');
}

export async function updateBranch({ pr: number, login }, api, sleep, assignment = readAssignment) {
  if (!Number.isSafeInteger(number) || number < 1) throw Error('Invalid branch update target');
  const endpoint = `${root}/pulls/${number}`;
  const pr = await api('GET', endpoint);
  const identity = await ownedBranch(pr, api, assignment);
  if (!identity || identity.login !== login) return 'unowned';
  const main = (await api('GET', `${root}/git/ref/heads/main`)).object.sha;
  const comparison = await api('GET', `${root}/compare/${main}...${pr.head.sha}`);
  if (comparison.behind_by === 0) return 'current';
  if (!Number.isSafeInteger(comparison.behind_by) || comparison.behind_by < 1) throw Error('Unknown branch comparison');
  if (pr.mergeable === false) return 'conflict';
  try { await api('PUT', `${endpoint}/update-branch`, { expected_head_sha: pr.head.sha }); }
  catch (error) { if (error.status === 422) return 'changed-or-conflicted'; throw error; }
  for (let attempt = 0; attempt < 15; attempt++) {
    await sleep(1000);
    const current = await api('GET', endpoint);
    if (current.state !== 'open' || current.head.ref !== pr.head.ref || current.head.repo?.full_name !== repository) return 'superseded';
    if (current.head.sha === pr.head.sha) continue;
    const merged = await api('GET', `${root}/compare/${main}...${current.head.sha}`);
    if (merged.behind_by !== 0) return 'superseded';
    if (!current.draft) {
      const label = `${identity.provider}-review`;
      if (current.labels.some(entry => entry.name === label)) await api('DELETE', `${root}/issues/${number}/labels/${label}`, undefined, true);
      const latest = await api('GET', endpoint);
      if (latest.head.sha !== current.head.sha || latest.state !== 'open' || latest.draft) return 'superseded';
      await api('POST', `${root}/issues/${number}/labels`, { labels: [label] });
    }
    return 'updated';
  }
  throw Error('Branch update was accepted but its new head is not confirmed');
}

async function main() {
  if (process.env.GITHUB_REPOSITORY !== repository || !process.env.GH_TOKEN) throw Error('The repository and authentication are required');
  const api = (method, endpoint, body, missing) => requestGitHub(fetch, process.env.GH_TOKEN, method, endpoint, body, missing);
  if (process.argv.includes('--discover')) {
    const matrix = await discoverBranches(api, readAssignment, process.env.PR_NUMBER ? Number(process.env.PR_NUMBER) : undefined);
    appendFileSync(process.env.GITHUB_OUTPUT, `matrix=${JSON.stringify(matrix)}\ncount=${matrix.include.length}\n`);
  } else {
    const status = await updateBranch({ pr: Number(process.env.PR_NUMBER), login: process.env.EXECUTOR_LOGIN }, api, ms => new Promise(resolve => setTimeout(resolve, ms)));
    appendFileSync(process.env.GITHUB_STEP_SUMMARY, `PR #${process.env.PR_NUMBER}: ${status}.\n`);
  }
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(error.message); process.exitCode = 1; });
