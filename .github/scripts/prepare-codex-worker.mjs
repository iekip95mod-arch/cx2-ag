import { execFileSync } from 'node:child_process';
import { appendFileSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

async function resolveIssue(repository, number, api) {
  if (repository !== 'iekip95mod-arch/cx2-ag' || !/^[1-9][0-9]*$/.test(String(number))) throw Error('A repository issue number is required');
  const issue = await api('GET', `repos/${repository}/issues/${number}`);
  if (!issue.pull_request) return { issue, branch: `codex/issue-${number}` };
  const pr = await api('GET', `repos/${repository}/pulls/${number}`);
  if (pr.head.repo?.full_name !== repository || !pr.head.ref.startsWith('codex/')) throw Error('Only repository Codex pull requests can be resumed');
  return { issue, branch: pr.head.ref, author: pr.user.login };
}

export async function claimIssue({ repository, number, run, expectedBranch }, api) {
  if (!/^[1-9][0-9]*$/.test(String(run))) throw Error('A run ID is required');
  const { issue, branch, author } = await resolveIssue(repository, number, api);
  if (expectedBranch !== undefined && branch !== expectedBranch) throw Error('The branch changed after queue resolution. Request the worker again');
  const identity = await api('GET', 'user');
  if (identity.type !== 'User' || !identity.login || !Number.isSafeInteger(identity.id)) throw Error('CODEX_GITHUB_TOKEN must belong to an assignable GitHub user');
  if (issue.state !== 'open') throw Error('The issue or pull request is closed');
  if (issue.assignees.some(assignee => assignee.login !== identity.login)) throw Error('Another account already owns this issue');
  if (issue.labels.some(label => label.name === 'claude')) throw Error('This issue is already assigned to a Claude worker');
  if (author !== undefined && author !== identity.login) throw Error('Only this account\'s Codex pull requests can be resumed');
  const existing = await api('GET', `repos/${repository}/git/ref/heads/${branch}`, undefined, true);
  if (existing && !issue.pull_request && !issue.assignees.some(assignee => assignee.login === identity.login)) throw Error('An unclaimed branch already exists. Check its owner before resuming');
  if (!existing) {
    const main = await api('GET', `repos/${repository}/git/ref/heads/main`);
    await api('POST', `repos/${repository}/git/refs`, { ref: `refs/heads/${branch}`, sha: main.object.sha });
  }
  const assigned = await api('POST', `repos/${repository}/issues/${number}/assignees`, { assignees: [identity.login] });
  if (!assigned.assignees.some(assignee => assignee.login === identity.login)) throw Error('GitHub did not assign the issue to the publishing account');
  await api('POST', `repos/${repository}/issues/${number}/comments`, { body: `Codex worker claimed this issue.\n\nBranch: ${branch}\nRun: https://github.com/${repository}/actions/runs/${run}\n\nThis worker owns this issue only. Its changes require an independent Codex review and all protected checks before merging.` });
  return { branch, login: identity.login, email: `${identity.id}+${identity.login}@users.noreply.github.com`, issue };
}

async function main() {
  if (!process.env.GH_TOKEN) throw Error('Set CODEX_GITHUB_TOKEN to a fine-grained token restricted to this repository, with Contents, Issues and Pull requests read and write');
  const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
  const number = event.issue?.number ?? event.pull_request?.number ?? event.inputs?.issue_number;
  const api = async (method, endpoint, body, missing = false) => {
    const args = ['api', '--method', method, endpoint];
    if (body) args.push('--input', '-');
    try {
      return JSON.parse(execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }));
    } catch (error) {
      if (missing && String(error.stderr).includes('(HTTP 404)')) return null;
      throw Error(`GitHub ${method} ${endpoint} failed. Check the publishing token permissions`);
    }
  };
  if (process.argv.includes('--resolve')) {
    const branch = number ? (await resolveIssue(process.env.GITHUB_REPOSITORY, number, api)).branch : '';
    appendFileSync(process.env.GITHUB_OUTPUT, `branch=${branch}\n`);
    return;
  }
  if (!process.env.EXPECTED_BRANCH) throw Error('Resolve the branch queue before claiming an issue');
  const claim = await claimIssue({ repository: process.env.GITHUB_REPOSITORY, number, run: process.env.GITHUB_RUN_ID, expectedBranch: process.env.EXPECTED_BRANCH }, api);
  const directory = join(process.env.GITHUB_WORKSPACE, '.Internal/workspaces', `codex-issue-${number}`, 'tree');
  mkdirSync(join(directory, '..'), { recursive: true });
  execFileSync('gh', ['auth', 'setup-git']);
  execFileSync('git', ['fetch', 'origin', `refs/heads/${claim.branch}:refs/remotes/origin/${claim.branch}`]);
  execFileSync('git', ['worktree', 'add', '--track', '-b', claim.branch, directory, `origin/${claim.branch}`]);
  execFileSync('git', ['-C', directory, 'config', 'user.name', claim.login]);
  execFileSync('git', ['-C', directory, 'config', 'user.email', claim.email]);
  writeFileSync(join(process.env.RUNNER_TEMP, 'codex-issue.json'), JSON.stringify(claim.issue));
  appendFileSync(process.env.GITHUB_OUTPUT, `directory=${directory}\nbranch=${claim.branch}\nissue=${number}\n`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
