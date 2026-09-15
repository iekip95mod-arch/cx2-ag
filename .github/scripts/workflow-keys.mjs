// A repeated key in one workflow mapping is rejected by GitHub before any job starts, so the
// workflow does not fail, it disappears: runs are created against the triggering push and finish
// with no jobs and no log. That is what a duplicate GH_TOKEN in agent-review.yml did, and it took
// the review gate down for every branch until somebody read a run with zero jobs.
//
// Scanned a line at a time rather than parsed, because a YAML library resolves the duplicate away
// by keeping the last one, which is exactly the thing that has to be visible here.

import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

const BLOCK_SCALAR = ["|", "|-", "|+", ">", ">-", ">+"];

function indentOf(line) {
  let count = 0;
  while (count < line.length && line[count] === " ") count += 1;
  return count;
}

// The key is everything up to the colon that ends it, which a quoted key may contain.
function keyOf(text) {
  if (text[0] === '"' || text[0] === "'") {
    const quote = text[0];
    const close = text.indexOf(quote, 1);
    if (close < 0) return null;
    return text[close + 1] === ":" ? text.slice(1, close) : null;
  }
  for (let i = 0; i < text.length; i += 1) {
    if (text[i] !== ":") continue;
    if (i + 1 === text.length || text[i + 1] === " ") return text.slice(0, i);
    return null;
  }
  return null;
}

function valueOf(text, key) {
  const rest = text.slice(text.indexOf(":", key.length) + 1).trim();
  const hash = rest.indexOf(" #");
  return (hash < 0 ? rest : rest.slice(0, hash)).trim();
}

export function duplicateKeys(source) {
  const found = [];
  const stack = [];
  const seen = new Map();
  let scalar = null;
  const lines = source.split("\n");
  for (let n = 0; n < lines.length; n += 1) {
    const line = lines[n];
    if (scalar !== null) {
      if (line.trim() === "" || indentOf(line) > scalar) continue;
      scalar = null;
    }
    const trimmed = line.trim();
    if (trimmed === "" || trimmed[0] === "#") continue;

    let indent = indentOf(line);
    let text = trimmed;
    let item = false;
    while (text.startsWith("- ") || text === "-") {
      indent += 2;
      text = text === "-" ? "" : text.slice(2).trim();
      item = true;
    }

    while (stack.length && stack[stack.length - 1].indent > indent) stack.pop();
    if (item) {
      while (stack.length && stack[stack.length - 1].indent >= indent) stack.pop();
    }
    if (!stack.length || stack[stack.length - 1].indent < indent) {
      stack.push({ indent, keys: new Set() });
    }

    if (text === "") continue;
    const key = keyOf(text);
    if (key === null) continue;

    const frame = stack[stack.length - 1];
    if (frame.keys.has(key)) {
      found.push({ key, line: n + 1, first: seen.get(`${frame.indent}:${key}`) });
    } else {
      frame.keys.add(key);
      seen.set(`${frame.indent}:${key}`, n + 1);
    }

    if (BLOCK_SCALAR.includes(valueOf(text, key))) scalar = indent;
  }
  return found;
}

export function scan(directory) {
  const reports = [];
  for (const name of readdirSync(directory).sort()) {
    if (!name.endsWith(".yml") && !name.endsWith(".yaml")) continue;
    const path = join(directory, name);
    for (const hit of duplicateKeys(readFileSync(path, "utf8"))) {
      reports.push(`${path}:${hit.line}: '${hit.key}' is already set at line ${hit.first}`);
    }
  }
  return reports;
}

if (process.argv[1] && import.meta.url.endsWith(process.argv[1].split("/").pop())) {
  const reports = scan(process.argv[2] || ".github/workflows");
  if (reports.length) {
    console.error("a repeated key stops GitHub loading the workflow at all:");
    for (const report of reports) console.error(`  ${report}`);
    process.exit(1);
  }
  console.log("workflow keys: no mapping sets the same key twice");
}
