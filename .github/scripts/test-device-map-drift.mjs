import { strict as assert } from "node:assert";
import { existsSync, readFileSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { MAP, coverage, covers, declaredMapLines, declares, drifted, pullNumber } from "./device-map-drift.mjs";

const SAMPLE = [
  "# Device map",
  "",
  "## The bridge",
  "",
  "<!-- covers: nps/src/platform/nspire/lua_module.cc -->",
  "",
  "prose",
  "",
  "## What renders",
  "",
  "<!-- covers: nps/lua/nps_v4.lua, nps/lua/ti_info.lua -->",
  "",
  "prose",
].join("\n");

test("a covers line is read with the heading above it", () => {
  const sections = coverage(SAMPLE);
  assert.equal(sections.length, 2);
  assert.equal(sections[0].heading, "The bridge");
  assert.deepEqual(sections[1].paths, ["nps/lua/nps_v4.lua", "nps/lua/ti_info.lua"]);
});

test("changing a covered file without the map is drift, and names its section", () => {
  const found = drifted(["nps/lua/ti_info.lua"], coverage(SAMPLE));
  assert.equal(found.length, 1);
  assert.equal(found[0].heading, "What renders");
  assert.deepEqual(found[0].files, ["nps/lua/ti_info.lua"]);
});

test("changing a covered file and the map together is not drift", () => {
  assert.deepEqual(drifted(["nps/lua/ti_info.lua", MAP], coverage(SAMPLE)), []);
});

test("changing a file nothing covers is not drift", () => {
  assert.deepEqual(drifted(["nps/src/steps/schema.cc"], coverage(SAMPLE)), []);
});

// A directory entry has to cover what is under it without swallowing a sibling whose name merely
// starts the same way, which is the difference between a path boundary and a string prefix.
// The error names a body sentence as the alternative to editing the map. It said so for a while
// without reading the body at all, which cost #366 a CI cycle per push and could only be escaped by
// touching the file the message called optional.
test("a body line naming the section clears that section and nothing else", () => {
  const body = "Some prose.\n\nDevice map: What renders, unchanged because the menu is untouched.\n";
  assert.deepEqual(drifted(["nps/lua/ti_info.lua"], coverage(SAMPLE), body), []);
  const other = drifted(["nps/src/platform/nspire/lua_module.cc"], coverage(SAMPLE), body);
  assert.equal(other.length, 1);
  assert.equal(other[0].heading, "The bridge");
});

test("the marker is folded, and a line that only mentions the map clears nothing", () => {
  assert.deepEqual(declaredMapLines("DEVICE MAP: The Bridge is fine"), ["the bridge is fine"]);
  assert.equal(declares(declaredMapLines("DEVICE MAP: The Bridge is fine"), "The bridge"), true);
  assert.equal(declares(declaredMapLines("I read the device map and it is fine"), "The bridge"), false);
  assert.equal(declares(declaredMapLines("Device map:\nThe bridge"), "The bridge"), false);
});

test("no body is the same answer the check gave before it read one", () => {
  assert.equal(drifted(["nps/lua/ti_info.lua"], coverage(SAMPLE)).length, 1);
  assert.equal(drifted(["nps/lua/ti_info.lua"], coverage(SAMPLE), null).length, 1);
  assert.equal(drifted(["nps/lua/ti_info.lua"], coverage(SAMPLE), "").length, 1);
});

test("a directory entry covers its contents and not a lookalike sibling", () => {
  assert.equal(covers("tools/nsptool", "tools/nsptool/nsptool.c"), true);
  assert.equal(covers("tools/nsptool", "tools/nsptool"), true);
  assert.equal(covers("tools/nsptool", "tools/nsptool-old/main.c"), false);
});

test("the map in this repository parses and every section names at least one path", () => {
  const sections = coverage(readFileSync(MAP, "utf8"));
  assert.ok(sections.length >= 5, `expected several covered sections, found ${sections.length}`);
  for (const section of sections) {
    assert.ok(section.paths.length > 0, `${section.heading} declares no paths`);
  }
});

// Which pull request this is decides whether the check asks GitHub or asks git, and the first version
// asked git on every run. That passed here and failed on every pull request, because the runner
// checks out one commit and a development checkout does not.
test("the pull request number is read from the event file, and its absence is not an error", () => {
  const path = join(tmpdir(), `device-map-event-${process.pid}.json`);
  try {
    writeFileSync(path, JSON.stringify({ pull_request: { number: 384 } }));
    assert.equal(pullNumber(path), 384);
    writeFileSync(path, JSON.stringify({ ref: "refs/heads/main" }));
    assert.equal(pullNumber(path), null, "a push event carries no pull request");
    writeFileSync(path, "not json at all");
    assert.equal(pullNumber(path), null, "an unreadable event must not throw");
  } finally {
    rmSync(path, { force: true });
  }
  assert.equal(pullNumber(undefined), null);
  assert.equal(pullNumber(join(tmpdir(), "no-such-event-file.json")), null);
});

// Every path the map claims to describe has to exist, or the check silently stops guarding it and
// the section it belongs to rots unnoticed. This is the failure the whole file exists to prevent, so
// it is asserted rather than trusted.
test("every path the map covers exists in this tree", () => {
  for (const section of coverage(readFileSync(MAP, "utf8"))) {
    for (const path of section.paths) {
      assert.ok(existsSync(path), `${MAP} covers "${path}" under "${section.heading}", which does not exist`);
    }
  }
});
