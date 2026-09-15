import assert from 'node:assert/strict';
import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { declaredOrder, undeclaredCollisions } from './pr-sequence.mjs';

const scripts = dirname(fileURLToPath(import.meta.url));
const pull = (number, files, body = '') => ({ number, files, body });

test('a declaration is read wherever it appears in the description', () => {
  assert.deepEqual([...declaredOrder('After #250')], [250]);
  assert.deepEqual([...declaredOrder('Closes #12\n\nBefore #7 because it rewrites the same block.')], [7]);
  assert.deepEqual([...declaredOrder('after #3 and Before #4')].sort(), [3, 4]);
  // Prose that merely mentions a pull request is not an ordering, and neither is a bare number.
  for (const body of ['', 'Closes #250', 'This supersedes #250', 'After the #250 work lands', 'After #'])
    assert.deepEqual([...declaredOrder(body)], [], body);
});

test('two pull requests touching one file collide until either side declares the order', () => {
  const subject = pull(348, ['a.yml', 'b.mjs']);
  const other = pull(250, ['b.mjs', 'c.rb']);
  assert.deepEqual(undeclaredCollisions(subject, [subject, other]), [{ number: 250, shared: ['b.mjs'] }]);

  // Either description can carry it, because the order is a fact about the pair.
  assert.deepEqual(undeclaredCollisions({ ...subject, body: 'After #250' }, [subject, other]), []);
  assert.deepEqual(undeclaredCollisions(subject, [subject, { ...other, body: 'Before #348' }]), []);

  // A declaration naming some other pull request does not clear this one.
  assert.deepEqual(undeclaredCollisions({ ...subject, body: 'After #999' }, [subject, other]), [{ number: 250, shared: ['b.mjs'] }]);
});

test('disjoint pull requests never collide and a pull request never collides with itself', () => {
  const subject = pull(1, ['only-mine.cc']);
  assert.deepEqual(undeclaredCollisions(subject, [subject, pull(2, ['theirs.cc']), pull(3, [])]), []);
  assert.deepEqual(undeclaredCollisions(subject, [subject, { ...subject }]), []);
});

test('every shared file is named and several collisions are reported in one pass', () => {
  const subject = pull(348, ['agent-review.yml', 'agent.yml', 'AGENTS.md']);
  const found = undeclaredCollisions(subject, [
    subject,
    pull(339, ['AGENTS.md', 'agent.yml', 'agent-review.yml']),
    pull(250, ['agent-review.yml']),
    pull(282, ['universal_domain.cc']),
  ]);
  assert.deepEqual(found, [
    { number: 250, shared: ['agent-review.yml'] },
    { number: 339, shared: ['AGENTS.md', 'agent-review.yml', 'agent.yml'] },
  ]);
});

test('the fast gate runs the sequencing check with permission to read other pull requests', () => {
  const workflow = join(scripts, '../workflows/check.yml');
  const job = JSON.parse(execFileSync('ruby', ['-ryaml', '-rjson', '-e', 'puts JSON.generate(YAML.load_file(ARGV[0])["jobs"]["fast"])', workflow], { encoding: 'utf8' }));
  const step = job.steps.find(entry => (entry.run ?? '').includes('pr-sequence.mjs'));
  assert.ok(step, 'the fast gate must run the sequencing check');
  // Reading another pull request's files needs a scope the workflow default does not carry.
  assert.equal(job.permissions['pull-requests'], 'read');
  assert.ok(step.env.GH_TOKEN, 'the check needs a token');
  assert.equal(step.env.PR_NUMBER, '${{ github.event.pull_request.number }}');
});
