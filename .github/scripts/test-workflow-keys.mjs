import { strict as assert } from "node:assert";
import test from "node:test";
import { duplicateKeys, scan } from "./workflow-keys.mjs";

test("a repeated key in one mapping is reported with both lines", () => {
  const found = duplicateKeys(["env:", "  GH_TOKEN: a", "  OTHER: b", "  GH_TOKEN: c"].join("\n"));
  assert.equal(found.length, 1);
  assert.equal(found[0].key, "GH_TOKEN");
  assert.equal(found[0].line, 4);
  assert.equal(found[0].first, 2);
});

test("the same key under two different parents is ordinary", () => {
  const found = duplicateKeys(["one:", "  env:", "    GH_TOKEN: a", "two:", "  env:", "    GH_TOKEN: b"].join("\n"));
  assert.deepEqual(found, []);
});

// The reviewer prompt is a block scalar holding free text, and free text holds colons. Reading it as
// keys reported the prompt against itself, which is a failure that trains the next person to ignore
// this check.
test("a block scalar is text rather than a mapping", () => {
  const found = duplicateKeys([
    "env:",
    "  PROMPT: |",
    "    REPO: one",
    "    REPO: two",
    "  AFTER: x",
  ].join("\n"));
  assert.deepEqual(found, []);
});

test("each list item starts its own mapping", () => {
  const found = duplicateKeys(["steps:", "  - name: a", "    run: x", "  - name: b", "    run: y"].join("\n"));
  assert.deepEqual(found, []);
});

test("a repeat inside one list item is still a repeat", () => {
  const found = duplicateKeys(["steps:", "  - name: a", "    run: x", "    run: y"].join("\n"));
  assert.equal(found.length, 1);
  assert.equal(found[0].key, "run");
});

test("a quoted key carrying a colon is read as one key", () => {
  const found = duplicateKeys(['on:', '  "a: b": 1', '  "a: b": 2'].join("\n"));
  assert.equal(found.length, 1);
  assert.equal(found[0].key, "a: b");
});

test("a comment after a block scalar marker does not hide it", () => {
  const found = duplicateKeys(["env:", "  PROMPT: | # why", "    REPO: one", "    REPO: two"].join("\n"));
  assert.deepEqual(found, []);
});

test("the workflows in this repository set no key twice", () => {
  assert.deepEqual(scan(new URL("../workflows", import.meta.url).pathname), []);
});
