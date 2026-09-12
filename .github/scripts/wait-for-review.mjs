import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';

export async function reviewState(read, repository, pr, sha) {
  const prefix = `repos/${repository}`;
  const current = await read(`${prefix}/pulls/${pr}`);
  if (current.head.sha !== sha || current.state !== 'open' || current.draft) throw Error('PR is superseded, closed or draft');
  const pages = await read(`${prefix}/actions/workflows/agent-review.yml/runs?head_sha=${sha}&event=pull_request&per_page=100`, true);
  const runs = pages.flatMap(page => page.workflow_runs).filter(run => run.head_sha === sha && run.pull_requests.some(pull => pull.number === pr)).sort((a, b) => b.id - a.id);
  if (!runs.length || runs.some(run => run.status !== 'completed')) return 'pending';
  const latest = runs[0];
  if (latest.conclusion !== 'success') throw Error('The current review workflow did not approve this PR');
  const jobs = await read(`${prefix}/actions/runs/${latest.id}/jobs?filter=latest&per_page=100`, true);
  const approval = jobs.flatMap(page => page.jobs).find(job => job.name === 'review-approved');
  if (approval?.status !== 'completed' || approval.conclusion !== 'success') throw Error('The review approval gate did not pass');
  return 'approved';
}

export async function waitForReview(read, repository, pr, sha, sleep, attempts = 90) {
  for (let attempt = 0; attempt < attempts; attempt++) {
    if (await reviewState(read, repository, pr, sha) === 'approved') return;
    if (attempt + 1 < attempts) await sleep(30000);
  }
  throw Error('Timed out waiting for reviewer approval. Request review, then rerun the failed CI jobs');
}

async function main() {
  if (process.env.GITHUB_EVENT_NAME !== 'pull_request') return;
  const repository = process.env.GITHUB_REPOSITORY;
  const pr = Number(process.env.PR_NUMBER);
  const sha = process.env.PR_HEAD_SHA;
  if (!/^[\w.-]+\/[\w.-]+$/.test(repository ?? '') || !Number.isSafeInteger(pr) || pr < 1 || !/^[a-f0-9]{40}$/.test(sha ?? '')) throw Error('Invalid PR review target');
  const execute = promisify(execFile);
  const read = async (endpoint, paginate = false) => {
    const { stdout } = await execute('gh', ['api', ...(paginate ? ['--paginate', '--slurp'] : []), endpoint], { timeout: 30000, maxBuffer: 8 * 1024 * 1024 });
    return JSON.parse(stdout);
  };
  await waitForReview(read, repository, pr, sha, ms => new Promise(resolve => setTimeout(resolve, ms)));
  console.log('Reviewer approval passed for this PR revision');
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) main().catch(error => { console.error(error.message); process.exitCode = 1; });
