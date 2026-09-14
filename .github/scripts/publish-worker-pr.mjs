import { execFileSync } from 'node:child_process';
import { readFileSync, realpathSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export async function publishDraft(context, git, api) {
  const { repository, issue, branch, login, title, directory, legacyOwner = '' } = context;
  if (repository !== 'iekip95mod-arch/cx2-ag' || !Number.isSafeInteger(issue) || issue < 1 || !['codex', 'claude', 'gemini'].some(provider => branch === `${provider}/issue-${issue}`)) throw Error('Invalid executor publication target');
  if (legacyOwner && legacyOwner !== 'iekip95mod-arch') throw Error('Unknown legacy publishing owner');
  if (!/^[a-z0-9-]+\[bot\]$/.test(login)) throw Error('A named bot publisher is required');
  if (git('rev-parse', '--show-toplevel') !== directory || git('branch', '--show-current') !== branch) throw Error('Publish only from the leased checkout and branch');
  if (!git('diff', '--name-only', 'origin/main...HEAD').trim()) return null;
  const original = await api('GET', `repos/${repository}/issues/${issue}`);
  if (original.pull_request) throw Error('The lease does not identify an issue');
  const pulls = await api('GET', `repos/${repository}/pulls?state=all&head=${encodeURIComponent(`${repository.split('/')[0]}:${branch}`)}&per_page=100`);
  if (pulls.length > 1) throw Error('This branch already has duplicate PRs. Resolve its lifecycle before publishing');
  const existing = pulls[0];
  const validatePull = pr => {
    if (!Number.isSafeInteger(pr.number) || pr.number < 1 || pr.head?.repo?.full_name !== repository || pr.head.ref !== branch) throw Error('The PR does not match the leased repository branch');
    if (pr.user.login !== login && pr.user.login !== legacyOwner) throw Error('The branch PR belongs to another publisher');
  };
  if (existing) validatePull(existing);
  if (original.state === 'closed' && existing?.state === 'closed' && Number.isFinite(Date.parse(existing.merged_at)) && existing.head.sha === git('rev-parse', 'HEAD')) return null;
  if (original.state !== 'open' || (existing && existing.state !== 'open')) throw Error('The issue or PR is closed without a verified completed merge');
  git('push', 'origin', `HEAD:refs/heads/${branch}`);
  const pr = existing ?? await api('POST', `repos/${repository}/pulls`, { title, head: branch, base: 'main', draft: true, body: `Closes #${issue}\n\nWork is in progress. Validation and implementation details will be updated in this PR.` });
  validatePull(pr);
  await api('POST', `repos/${repository}/issues/${pr.number}/labels`, { labels: [`worker:${login.slice(0, -5)}`] });
  const marker = `Executor lease: ${branch}`;
  const comments = await api('GET', `repos/${repository}/issues/${pr.number}/comments?per_page=100`);
  if (!comments.some(comment => comment.user?.login === login && comment.body?.split('\n').includes(marker))) await api('POST', `repos/${repository}/issues/${pr.number}/comments`, { body: `${login} is implementing issue #${issue} on ${branch}. This PR tracks that issue and keeps its existing executor identity through review fixes.\n\n${marker}` });
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
    try {
      return JSON.parse(execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }));
    } catch (error) { throw Object.assign(Error('GitHub publication request failed'), { status: Number(/\(HTTP (\d+)\)/.exec(String(error.stderr))?.[1]) }); }
  };
  const pr = await publishDraft(context, git, api);
  if (pr) console.log(`Draft PR: ${pr.html_url}`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(() => { console.error('::error::The commit exists, but draft PR publication failed. Retry publication without creating another commit.'); process.exitCode = 1; });
