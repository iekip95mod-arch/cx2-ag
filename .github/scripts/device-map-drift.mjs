// docs/device-map.md describes the calculator rather than the repository, so nothing about it is
// checkable by building. What is checkable is whether it was read: each section names the paths it
// describes on a `covers` line, and a pull request that changes one of those paths without touching
// the map has moved the ground the map stands on.
//
// This is the failure mode the map exists to prevent rather than a tidiness rule. An agent trusts a
// map absolutely, so a stale one produces confident wrong work, which is worse than no map at all.

import { fileURLToPath } from 'node:url';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';

export const MAP = 'docs/device-map.md';

// A covers line is an HTML comment so it does not render, and it sits under the heading it belongs to
// so the error can name the section an author has to revisit.
export function coverage(text) {
  const sections = [];
  let heading = null;
  for (const line of text.split('\n')) {
    const trimmed = line.trim();
    if (trimmed.startsWith('## ')) heading = trimmed.slice(3).trim();
    const open = '<!-- covers:';
    if (!trimmed.startsWith(open)) continue;
    const close = trimmed.indexOf('-->');
    if (close < 0) continue;
    const paths = trimmed.slice(open.length, close).split(',')
      .map(entry => entry.trim()).filter(Boolean);
    if (paths.length) sections.push({ heading: heading ?? '(no heading)', paths });
  }
  return sections;
}

// A covers entry is a path prefix, so naming a directory covers the files under it and naming a file
// covers exactly that file. Prefix matching is on a path boundary, so `nps/src/physics` does not
// swallow a sibling called `nps/src/physics-notes`.
export function covers(entry, file) {
  if (file === entry) return true;
  return file.startsWith(entry.endsWith('/') ? entry : entry + '/');
}

export function drifted(changed, sections) {
  if (changed.includes(MAP)) return [];
  const found = [];
  for (const section of sections) {
    const hits = changed.filter(file => section.paths.some(entry => covers(entry, file)));
    if (hits.length) found.push({ heading: section.heading, files: hits.sort() });
  }
  return found;
}

export function pullNumber(eventPath) {
  if (!eventPath) return null;
  try {
    return JSON.parse(readFileSync(eventPath, 'utf8')).pull_request?.number ?? null;
  } catch {
    return null;
  }
}

// The runner checks out a single commit with no history, so there is no origin/main and no merge
// base to diff against. Asking git was the first version of this and it failed on every pull request
// while passing locally, because a development checkout has the history that a CI one does not.
// GitHub already knows which files a pull request touches and answers without any history at all.
async function fromGitHub(repo, number, token) {
  const files = [];
  for (let page = 1; ; page++) {
    const url = `https://api.github.com/repos/${repo}/pulls/${number}/files?per_page=100&page=${page}`;
    const response = await fetch(url, {
      headers: { authorization: `Bearer ${token}`, accept: 'application/vnd.github+json' },
    });
    if (!response.ok) throw Error(`GitHub answered ${response.status} for the changed files of #${number}`);
    const batch = await response.json();
    for (const entry of batch) files.push(entry.filename);
    if (batch.length < 100) return files;
  }
}

async function changedFiles() {
  const repo = process.env.GITHUB_REPOSITORY;
  const token = process.env.GITHUB_TOKEN;
  const number = pullNumber(process.env.GITHUB_EVENT_PATH);
  if (repo && token && number) return fromGitHub(repo, number, token);
  // Outside Actions, a development checkout does have the history, which is how this is run by hand.
  const base = process.env.GITHUB_BASE_REF || (number ? null : 'main');
  if (!base) return null;
  const out = execFileSync('git', ['diff', '--name-only', `origin/${base}...HEAD`], { encoding: 'utf8' });
  return out.split('\n').map(line => line.trim()).filter(Boolean);
}

async function main() {
  const changed = await changedFiles();
  if (changed === null) {
    console.log('No pull request base in this event, so there is nothing to compare the map against.');
    return;
  }
  const sections = coverage(readFileSync(MAP, 'utf8'));
  if (sections.length === 0) throw Error(`${MAP} declares no covers lines, so this check sees nothing`);
  const found = drifted(changed, sections);
  if (found.length === 0) {
    console.log(`device map: ${sections.length} sections, nothing this branch changed is described by one.`);
    return;
  }
  for (const section of found) {
    console.log(`::error::This branch changes ${section.files.join(', ')}, which ${MAP} describes under "${section.heading}". Read that section and either update it or say in the pull request body why it is still true.`);
  }
  process.exitCode = 1;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
}
