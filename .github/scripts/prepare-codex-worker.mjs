import { execFileSync } from 'node:child_process';
import { appendFileSync, chmodSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

async function resolveIssue(repository, number, api, provider = 'codex') {
  if (repository !== 'iekip95mod-arch/cx2-ag' || !/^[1-9][0-9]*$/.test(String(number))) throw Error('A repository issue number is required');
  const issue = await api('GET', `repos/${repository}/issues/${number}`);
  if (!['codex', 'claude', 'gemini'].includes(provider)) throw Error('Unknown executor provider');
  if (!issue.pull_request) return { issue, number: Number(number), branch: `${provider}/issue-${number}` };
  const pr = await api('GET', `repos/${repository}/pulls/${number}`);
  const match = pr.head.ref.match(new RegExp(`^${provider}/issue-([1-9][0-9]*)$`));
  if (pr.head.repo?.full_name !== repository || !match) throw Error('Only canonical repository executor pull requests can be resumed');
  if (issue.state !== 'open') throw Error('The pull request is closed');
  const original = await api('GET', `repos/${repository}/issues/${match[1]}`);
  if (original.pull_request) throw Error('The branch must identify an issue');
  return { issue: original, number: Number(match[1]), branch: pr.head.ref, author: pr.user.login };
}

export async function claimIssue({ repository, number, run, expectedBranch, provider = 'codex', bot, legacyOwner = '' }, api) {
  if (!/^[1-9][0-9]*$/.test(String(run))) throw Error('A run ID is required');
  if (!bot || !Number.isSafeInteger(bot.id) || bot.id < 1 || !/^[a-z0-9-]+$/.test(bot.slug) || bot.login !== `${bot.slug}[bot]`) throw Error('A leased GitHub App identity is required');
  if (legacyOwner && legacyOwner !== 'iekip95mod-arch') throw Error('Unknown legacy publishing owner');
  const { issue, number: issueNumber, branch, author } = await resolveIssue(repository, number, api, provider);
  if (expectedBranch !== undefined && branch !== expectedBranch) throw Error('The branch changed after queue resolution. Request the worker again');
  const identity = await api('GET', `users/${bot.login}`);
  if (identity.type !== 'Bot' || identity.login !== bot.login || identity.id !== bot.id) throw Error('The leased GitHub App identity does not match GitHub');
  if (issue.state !== 'open') throw Error('The issue or pull request is closed');
  if (issue.assignees.some(assignee => assignee.login !== identity.login && assignee.login !== legacyOwner)) throw Error('Another account already owns this issue');
  if (issue.labels.some(label => ['codex', 'claude', 'gemini'].includes(label.name) && label.name !== provider)) throw Error('This issue is already assigned to another provider');
  if (author !== undefined && author !== identity.login && author !== legacyOwner) throw Error('Only the leased bot\'s pull requests can be resumed');
  const existing = await api('GET', `repos/${repository}/git/ref/heads/${branch}`, undefined, true);
  if (!expectedBranch) throw Error('A durable branch lease is required');
  if (!existing) {
    const main = await api('GET', `repos/${repository}/git/ref/heads/main`);
    await api('POST', `repos/${repository}/git/refs`, { ref: `refs/heads/${branch}`, sha: main.object.sha });
  }
  const label = `worker:${bot.slug}`;
  const existingLabel = await api('GET', `repos/${repository}/labels/${label}`, undefined, true);
  if (!existingLabel) await api('POST', `repos/${repository}/labels`, { name: label, color: '5319e7', description: 'Executor identity reserved by the durable issue lease' });
  await api('POST', `repos/${repository}/issues/${issueNumber}/labels`, { labels: [label] });
  await api('POST', `repos/${repository}/issues/${issueNumber}/comments`, { body: `${identity.login} holds the ${provider} executor lease for this issue.\n\nExecutor lease: ${branch}\nBranch: ${branch}\nRun: https://github.com/${repository}/actions/runs/${run}\n\nThis worker owns this issue only. Its changes require an independent ${provider} review and all protected checks before merging.` });
  return { branch, login: identity.login, userId: identity.id, email: `${identity.id}+${identity.login}@users.noreply.github.com`, issue, number: issueNumber };
}

async function main() {
  if (!process.env.GH_TOKEN) throw Error('A leased GitHub App installation token is required');
  const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
  const number = event.issue?.number ?? event.pull_request?.number ?? event.inputs?.issue_number ?? event.client_payload?.issue_number;
  const provider = process.env.BOT_PROVIDER ?? 'codex';
  const api = async (method, endpoint, body, missing = false) => {
    const args = ['api', '--method', method, endpoint];
    if (body) args.push('--input', '-');
    try {
      const reply = execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] });
      return reply.trim() ? JSON.parse(reply) : {};
    } catch (error) {
      if (missing && String(error.stderr).includes('(HTTP 404)')) return null;
      throw Object.assign(Error(`GitHub ${method} ${endpoint} failed. Check the publishing token permissions`), { status: Number(/\(HTTP (\d+)\)/.exec(String(error.stderr))?.[1]) });
    }
  };
  if (process.argv.includes('--resolve')) {
    const branch = number ? (await resolveIssue(process.env.GITHUB_REPOSITORY, number, api, provider)).branch : '';
    appendFileSync(process.env.GITHUB_OUTPUT, `branch=${branch}\n`);
    return;
  }
  if (!process.env.EXPECTED_BRANCH) throw Error('Resolve the branch queue before claiming an issue');
  const claim = await claimIssue({ repository: process.env.GITHUB_REPOSITORY, number, run: process.env.GITHUB_RUN_ID, expectedBranch: process.env.EXPECTED_BRANCH, provider,
    bot: { login: process.env.BOT_LOGIN, id: Number(process.env.BOT_USER_ID), slug: process.env.BOT_APP_SLUG }, legacyOwner: process.env.LEGACY_OWNER }, api);
  const directory = join(process.env.GITHUB_WORKSPACE, '.Internal/workspaces', `${provider}-issue-${claim.number}`, 'tree');
  mkdirSync(join(directory, '..'), { recursive: true });
  execFileSync('gh', ['auth', 'setup-git']);
  execFileSync('git', ['fetch', 'origin', `refs/heads/${claim.branch}:refs/remotes/origin/${claim.branch}`]);
  execFileSync('git', ['worktree', 'add', '--track', '-b', claim.branch, directory, `origin/${claim.branch}`]);
  execFileSync('git', ['-C', directory, 'config', 'user.name', claim.login]);
  execFileSync('git', ['-C', directory, 'config', 'user.email', claim.email]);
  writeFileSync(join(process.env.RUNNER_TEMP, 'codex-issue.json'), JSON.stringify(claim.issue));
  const publication = join(process.env.RUNNER_TEMP, 'worker-publication.json');
  writeFileSync(publication, JSON.stringify({ repository: process.env.GITHUB_REPOSITORY, issue: claim.number, branch: claim.branch, login: claim.login, userId: claim.userId, title: claim.issue.title, directory, legacyOwner: process.env.LEGACY_OWNER ?? '' }));
  const hooks = join(process.env.RUNNER_TEMP, 'worker-hooks');
  mkdirSync(hooks, { recursive: true });
  const quote = value => `'${value.replaceAll("'", "'\\''")}'`;
  writeFileSync(join(hooks, 'post-commit'), `#!/bin/sh\nexec ${quote(process.execPath)} ${quote(fileURLToPath(new URL('./publish-worker-pr.mjs', import.meta.url)))} ${quote(publication)}\n`);
  chmodSync(join(hooks, 'post-commit'), 0o755);
  if (provider === 'claude') {
    const executable = join(process.env.RUNNER_TEMP, 'claude-worker');
    writeFileSync(executable, `#!/bin/sh\ncd ${quote(directory)} || exit 1\nexec claude "$@"\n`);
    chmodSync(executable, 0o755);
  }
  execFileSync('git', ['config', 'extensions.worktreeConfig', 'true']);
  execFileSync('git', ['-C', directory, 'config', '--worktree', 'core.hooksPath', hooks]);
  appendFileSync(process.env.GITHUB_OUTPUT, `directory=${directory}\nbranch=${claim.branch}\nissue=${claim.number}\n`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
