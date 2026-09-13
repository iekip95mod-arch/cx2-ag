import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export function diffAnchors(patch) {
  if (typeof patch !== 'string' || !patch) throw Error('Inline comments require an available text patch');
  const anchors = new Set();
  let oldLine = 0, newLine = 0, oldLeft = 0, newLeft = 0, hunks = 0;
  const lines = patch.split('\n');
  if (lines.at(-1) === '') lines.pop();
  for (const line of lines) {
    const hunk = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(?:.*)$/.exec(line);
    if (hunk) {
      if (oldLeft || newLeft) throw Error('Incomplete review patch hunk');
      const oldStart = Number(hunk[1]), newStart = Number(hunk[3]);
      if (hunks && (oldStart < oldLine || newStart < newLine)) throw Error('Unordered review patch hunks');
      oldLine = oldStart; newLine = newStart;
      oldLeft = Number(hunk[2] ?? 1); newLeft = Number(hunk[4] ?? 1);
      if (![oldLine, newLine, oldLeft, newLeft].every(Number.isSafeInteger) || (oldLeft && !oldLine) || (newLeft && !newLine)) throw Error('Invalid review patch hunk');
      hunks++;
    } else if (line === '\\ No newline at end of file' && hunks) {
      continue;
    } else if (hunks && line.startsWith('-') && oldLeft > 0) {
      anchors.add(`LEFT:${oldLine++}`); oldLeft--;
    } else if (hunks && line.startsWith('+') && newLeft > 0) {
      anchors.add(`RIGHT:${newLine++}`); newLeft--;
    } else if (hunks && line.startsWith(' ') && oldLeft > 0 && newLeft > 0) {
      anchors.add(`RIGHT:${newLine++}`); oldLine++; oldLeft--; newLeft--;
    } else {
      throw Error('Invalid review patch line');
    }
  }
  if (!hunks || oldLeft || newLeft) throw Error('Incomplete review patch');
  return anchors;
}

export function reviewRequest(review, files, head) {
  if (!review || !['APPROVED', 'CHANGES_REQUESTED', 'BLOCKED'].includes(review.verdict) || typeof review.body !== 'string' || !review.body.trim() || !Array.isArray(review.comments) || review.comments.length > 100) throw Error('Invalid structured review');
  if (review.verdict === 'BLOCKED' && review.comments.length) throw Error('A blocked review cannot hide code findings');
  const changed = new Map(files.map(file => [file.filename, file]));
  if (changed.size !== files.length || files.some(file => typeof file.filename !== 'string' || !file.filename)) throw Error('Invalid PR file listing');
  const patches = new Map();
  const comments = review.comments.map(comment => {
    if (!comment || typeof comment.path !== 'string' || !changed.has(comment.path) || !Number.isSafeInteger(comment.line) || comment.line < 1 || !['LEFT', 'RIGHT'].includes(comment.side) || typeof comment.body !== 'string' || !comment.body.trim()) throw Error('Invalid inline review comment');
    if (!patches.has(comment.path)) patches.set(comment.path, diffAnchors(changed.get(comment.path).patch));
    if (!patches.get(comment.path).has(`${comment.side}:${comment.line}`)) throw Error('Inline review anchor is outside the PR diff');
    return { path: comment.path, line: comment.line, side: comment.side, body: comment.body };
  });
  return { commit_id: head, event: review.verdict === 'APPROVED' ? 'APPROVE' : review.verdict === 'BLOCKED' ? 'COMMENT' : 'REQUEST_CHANGES', body: review.body + (review.verdict === 'BLOCKED' ? '\n\n<!-- review-blocked -->' : ''), comments };
}

export async function publishReview({ repository, pr, head, appSlug, login, review }, api) {
  if (repository !== 'iekip95mod-arch/cx2-ag' || !Number.isSafeInteger(pr) || pr < 1 || !/^[a-f0-9]{40}$/.test(head) || typeof appSlug !== 'string' || !/^[a-z0-9][a-z0-9-]*$/.test(appSlug) || login !== `${appSlug}[bot]`) throw Error('Invalid review publication identity or target');
  const endpoint = `repos/${repository}/pulls/${pr}`;
  const currentHead = pull => pull.state === 'open' && pull.draft === false && pull.head?.repo?.full_name === repository && pull.base?.repo?.full_name === repository && pull.head.sha === head && pull.user?.login !== login;
  const pull = await api('GET', endpoint);
  if (!currentHead(pull)) throw Error('PR is not ready for this review head');
  if (!Number.isSafeInteger(pull.changed_files) || pull.changed_files < 0 || pull.changed_files > 3000) throw Error('PR files exceed the review lookup limit');
  const files = [];
  for (let page = 1; files.length < pull.changed_files && page <= 30; page++) {
    const batch = await api('GET', `${endpoint}/files?per_page=100&page=${page}`);
    if (!Array.isArray(batch) || batch.length > 100) throw Error('Invalid PR file listing');
    files.push(...batch);
    if (batch.length < 100) break;
  }
  if (files.length !== pull.changed_files) throw Error('Incomplete PR file listing');
  const request = reviewRequest(review, files, head);
  if (!currentHead(await api('GET', endpoint))) throw Error('PR changed before review publication');
  const published = await api('POST', `${endpoint}/reviews`, request);
  if (!Number.isSafeInteger(published.id) || published.id < 1 || published.commit_id !== head || published.state !== (review.verdict === 'BLOCKED' ? 'COMMENTED' : review.verdict) || published.user?.login !== login || published.user.type !== 'Bot') throw Error('Review may exist but GitHub did not confirm its expected identity, head and verdict. Inspect it before retrying.');
  return published;
}

async function main() {
  if (!process.env.GH_TOKEN) throw Error('The assigned reviewer token is required');
  let review;
  try { review = JSON.parse(readFileSync(process.env.REVIEW_FILE, 'utf8')); } catch { throw Error('The structured review file could not be read'); }
  const api = async (method, endpoint, body) => {
    const args = ['api', '--method', method, endpoint];
    if (body) args.push('--input', '-');
    try {
      return JSON.parse(execFileSync('gh', args, { input: body ? JSON.stringify(body) : undefined, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'], maxBuffer: 16 * 1024 * 1024 }));
    } catch {
      throw Error(method === 'POST' ? 'Review publication was not confirmed. Inspect the PR before retrying.' : `GitHub ${method} ${endpoint} failed`);
    }
  };
  const published = await publishReview({ repository: process.env.REPO, pr: Number(process.env.PR), head: process.env.HEAD_SHA, appSlug: process.env.APP_SLUG, login: process.env.EXPECTED_LOGIN, review }, api);
  console.log(`Published ${published.state} review ${published.id} with ${review.comments.length} inline comments.`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
