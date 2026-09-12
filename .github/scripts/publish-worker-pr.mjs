import { execFileSync } from 'node:child_process';
import { readFileSync, realpathSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export async function publishDraft(context, git, api) {
  const { repository, issue, branch, login, title, directory, legacyOwner = '' } = context;
  if (repository !== 'iekip95mod-arch/cx2-ag' || !Number.isSafeInteger(issue) || issue < 1 || !['codex', 'claude'].some(provider => branch === `${provider}/issue-${issue}`)) throw Error('Invalid executor publication target');
  if (legacyOwner && legacyOwner !== 'iekip95mod-arch') throw Error('Unknown legacy publishing owner');
  if (!/^[a-z0-9-]+\[bot\]$/.test(login)) throw Error('A named bot publisher is required');
  if (git('rev-parse', '--show-toplevel') !== directory || git('branch', '--show-current') !== branch) throw Error('Publish only from the leased checkout and branch');
  if (!git('diff', '--name-only', 'origin/main...HEAD').trim()) return null;
  const original = await api('GET', `repos/${repository}/issues/${issue}`);
  if (original.state !== 'open' || original.pull_request) throw Error('The leased issue is no longer open');
  const pulls = await api('GET', `repos/${repository}/pulls?state=all&head=${encodeURIComponent(`${repository.split('/')[0]}:${branch}`)}&per_page=100`);
  if (pulls.length > 1 || pulls.some(pr => pr.state !== 'open')) throw Error('This branch already has a closed or duplicate PR. Resolve its lifecycle before publishing');
  const existing = pulls[0];
  const validatePull = pr => {
    if (!Number.isSafeInteger(pr.number) || pr.number < 1 || pr.head?.repo?.full_name !== repository || pr.head.ref !== branch) throw Error('The PR does not match the leased repository branch');
    if (pr.user.login !== login && pr.user.login !== legacyOwner) throw Error('The branch PR belongs to another publisher');
  };
  if (existing) validatePull(existing);
  git('push', 'origin', `HEAD:refs/heads/${branch}`);
  const pr = existing ?? await api('POST', `repos/${repository}/pulls`, { title, head: branch, base: 'main', draft: true, body: `Closes #${issue}\n\nWork is in progress. Validation and implementation details will be updated in this PR.` });
  validatePull(pr);
  await api('POST', `repos/${repository}/issues/${pr.number}/labels`, { labels: [`worker:${login.slice(0, -5)}`] });
  return pr;
}

async function main() {
  if (!process.env.GH_TOKEN) throw Error('The leased App installation token is required');
  const context = JSON.parse(readFileSync(process.argv[2], 'utf8'));
  context.directory = realpathSync(context.directory);
  const git = (...args) => execFileSync('git', args, { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
  const api = async (method, endpoint, body) => {
    const args = ['api', '--method', method, endpoint];
    if (body) args.push('--input', '-');
    return JSON.parse(execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }));
  };
  const pr = await publishDraft(context, git, api);
  if (pr) console.log(`Draft PR: ${pr.html_url}`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(() => { console.error('::error::The commit exists, but draft PR publication failed. Retry publication without creating another commit.'); process.exitCode = 1; });
