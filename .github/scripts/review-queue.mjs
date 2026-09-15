import { appendFileSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { releaseDeadLeases, reviewerCapacity } from './bot-identities.mjs';
import { requestGitHub, reviewProvider } from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';
const root = `repos/${repository}`;
const workflows = ['agent-review.yml', 'agent-review-request.yml'];

async function pages(api, endpoint, key) {
  const rows = [];
  for (let page = 1; page <= 10; page++) {
    const batch = await api('GET', `${endpoint}${endpoint.includes('?') ? '&' : '?'}per_page=100&page=${page}`);
    const entries = key ? batch[key] : batch;
    if (!Array.isArray(entries)) throw Error('Invalid review queue response');
    rows.push(...entries);
    if (entries.length < 100) return rows;
  }
  throw Error('Review queue exceeds the lookup limit');
}

function matches(run, pr, workflow) {
  return run.event === 'pull_request' && run.path === `.github/workflows/${workflow}` &&
    run.head_repository?.full_name === repository && run.pull_requests?.length === 1 &&
    run.pull_requests[0].number === pr.number && run.head_branch === pr.head.ref;
}

export async function cancelObsoleteReviews(number, api) {
  const pr = await api('GET', `${root}/pulls/${number}`);
  if (pr.head?.repo?.full_name !== repository) return [];
  const cancelled = [];
  for (const workflow of workflows) {
    const runs = await pages(api, `${root}/actions/workflows/${workflow}/runs?event=pull_request&branch=${encodeURIComponent(pr.head.ref)}`, 'workflow_runs');
    for (const candidate of runs) {
      if (!matches(candidate, pr, workflow) || candidate.status === 'completed') continue;
      const current = await api('GET', `${root}/pulls/${number}`);
      const run = await api('GET', `${root}/actions/runs/${candidate.id}`);
      if (current.head?.repo?.full_name !== repository || current.head.ref !== pr.head.ref ||
          !matches(run, current, workflow) || run.status === 'completed' ||
          (current.state === 'open' && !current.draft && run.head_sha === current.head.sha)) continue;
      try { await api('POST', `${root}/actions/runs/${run.id}/cancel`); }
      catch (error) { if (error.status !== 409) throw error; }
      cancelled.push(run.id);
    }
  }
  return cancelled;
}

export async function retryWaitingReviews(api, capacity = reviewerCapacity, reap = releaseDeadLeases) {
  await reap(repository, api).catch(() => {});
  const available = await capacity(repository, api);
  const pulls = await pages(api, `${root}/pulls?state=open&base=main`);
  const retried = [];
  for (const pr of pulls.sort((a, b) => a.number - b.number)) {
    if (!/^(codex|claude|gemini)\/issue-[1-9][0-9]*$/.test(pr.head.ref) || pr.draft || pr.head.repo?.full_name !== repository) continue;
    const provider = reviewProvider({ ...pr, labels: pr.labels ?? [] });
    if (!available[provider]) continue;
    const runs = (await pages(api, `${root}/actions/workflows/agent-review-request.yml/runs?event=pull_request&head_sha=${pr.head.sha}`, 'workflow_runs'))
      .filter(run => matches(run, pr, 'agent-review-request.yml') && run.head_sha === pr.head.sha && run.display_title === `Assign reviewer for PR #${pr.number} (requested)`)
      .sort((a, b) => Date.parse(b.run_started_at) - Date.parse(a.run_started_at) || b.id - a.id);
    const latest = runs[0];
    if (!latest || runs.some(run => run.status !== 'completed')) continue;
    const jobs = await pages(api, `${root}/actions/runs/${latest.id}/attempts/${latest.run_attempt}/jobs`, 'jobs');
    const waiting = jobs.some(job => job.steps?.some(step => step.name === 'Wait for reviewer capacity' && step.conclusion === 'success'));
    if (!waiting) continue;
    const current = await api('GET', `${root}/pulls/${pr.number}`);
    const run = await api('GET', `${root}/actions/runs/${latest.id}`);
    if (reviewProvider({ ...current, labels: current.labels ?? [] }) !== provider || current.state !== 'open' || current.draft || current.head.sha !== pr.head.sha || current.head.ref !== pr.head.ref || current.head.repo?.full_name !== repository || run.run_attempt !== latest.run_attempt || run.status !== 'completed') continue;
    await api('POST', `${root}/actions/runs/${latest.id}/rerun`);
    available[provider]--;
    retried.push(pr.number);
  }
  return retried;
}

async function main() {
  if (process.env.GITHUB_REPOSITORY !== repository || !process.env.GH_TOKEN) throw Error('Trusted repository authentication is required');
  const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
  const api = (method, endpoint, body, missing) => requestGitHub(fetch, process.env.GH_TOKEN, method, endpoint, body, missing);
  const cancelled = event.pull_request ? await cancelObsoleteReviews(event.pull_request.number, api) : [];
  const retry = ['push', 'issues', 'workflow_dispatch', 'workflow_run'].includes(process.env.GITHUB_EVENT_NAME) || event.action === 'closed';
  const retried = retry ? await retryWaitingReviews(api) : [];
  appendFileSync(process.env.GITHUB_STEP_SUMMARY, `Cancelled obsolete review runs: ${cancelled.join(', ') || 'none'}.\nRetried waiting PRs: ${retried.join(', ') || 'none'}.\n`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(error.message); process.exitCode = 1; });
