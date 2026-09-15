import { fileURLToPath } from 'node:url';
import { requestGitHub } from './wait-for-review.mjs';

const repository = 'iekip95mod-arch/cx2-ag';

// A body is arbitrary prose, so this scans for the two declarations rather than matching a pattern
// over the whole text. Case is folded because an executor writes the line by hand.
export function declaredOrder(body) {
  const text = (body ?? '').toLowerCase();
  const found = new Set();
  for (const word of ['after #', 'before #']) {
    for (let at = text.indexOf(word); at >= 0; at = text.indexOf(word, at + word.length)) {
      let end = at + word.length;
      let digits = '';
      while (end < text.length && text[end] >= '0' && text[end] <= '9') digits += text[end++];
      if (digits) found.add(Number(digits));
    }
  }
  return found;
}

// Two open pull requests editing one file are a collision whichever order they land in, and the
// second one to merge inherits the conflict. Either side may declare the order, because a
// declaration is a statement about the pair rather than about its author.
export function undeclaredCollisions(subject, others) {
  const declared = declaredOrder(subject.body);
  const collisions = [];
  for (const other of others) {
    if (other.number === subject.number) continue;
    const shared = subject.files.filter(file => other.files.includes(file));
    if (shared.length === 0) continue;
    if (declared.has(other.number) || declaredOrder(other.body).has(subject.number)) continue;
    collisions.push({ number: other.number, shared: shared.sort() });
  }
  return collisions.sort((left, right) => left.number - right.number);
}

async function pages(token, endpoint) {
  const collected = [];
  for (let page = 1; ; page++) {
    const batch = await requestGitHub(fetch, token, 'GET', `${endpoint}?per_page=100&page=${page}`);
    if (!Array.isArray(batch) || batch.length === 0) return collected;
    collected.push(...batch);
    // A short page is the last one, and the loop is bounded so a repeating page cannot spin forever.
    if (batch.length < 100 || page >= 20) return collected;
  }
}

async function main() {
  const number = Number(process.env.PR_NUMBER);
  if (!process.env.PR_NUMBER) {
    console.log('No pull request in this event, so there is no ordering to check.');
    return;
  }
  if (!Number.isSafeInteger(number) || number < 1) throw Error('Invalid pull request number');
  const token = process.env.GH_TOKEN;
  if (!token) throw Error('A token is required to read the open pull requests');
  const open = await pages(token, `repos/${repository}/pulls`);
  const described = await Promise.all(open.map(async pull => ({
    number: pull.number,
    body: pull.body ?? '',
    files: (await pages(token, `repos/${repository}/pulls/${pull.number}/files`)).map(file => file.filename),
  })));
  const subject = described.find(pull => pull.number === number);
  if (!subject) throw Error(`Pull request ${number} is not open`);
  const collisions = undeclaredCollisions(subject, described);
  if (collisions.length === 0) {
    console.log(`No undeclared file collisions with the ${described.length - 1} other open pull requests.`);
    return;
  }
  for (const collision of collisions) {
    console.log(`::error::PR #${number} and PR #${collision.number} both change ${collision.shared.join(', ')}. Add a line saying After #${collision.number} or Before #${collision.number} to one of the two descriptions, so the merge order is recorded rather than decided by whoever is faster.`);
  }
  process.exitCode = 1;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  main().catch(error => { console.error(`::error::${error.message}`); process.exitCode = 1; });
}
