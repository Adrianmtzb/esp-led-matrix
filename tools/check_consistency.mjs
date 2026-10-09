#!/usr/bin/env node
// Fails when the effect list drifts between the firmware registry, shared/effects.json, the MCP
// enum and the landing demo, so no client offers an effect the firmware does not accept.
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => readFileSync(join(root, p), 'utf8');

function firmwareEffects() {
  const src = read('firmware/ledmatrix/effects.cpp');
  const table = src.match(/const EffectDef EFFECTS\[\] = \{([\s\S]*?)\n\};/);
  if (!table) throw new Error('EFFECTS registry not found in effects.cpp');
  // Skip entries under board #if blocks: those are not available everywhere.
  let depth = 0;
  const names = [];
  for (const line of table[1].split('\n')) {
    const t = line.trim();
    if (t.startsWith('#if')) depth++;
    else if (t.startsWith('#endif')) depth--;
    else if (depth === 0) {
      const m = t.match(/^\{"([a-z0-9_]+)"/);
      if (m) names.push(m[1]);
    }
  }
  return names;
}

function arrayLiteral(file, name) {
  const m = read(file).match(new RegExp(`const ${name} = \\[([^\\]]*)\\]`));
  if (!m) throw new Error(`${name} not found in ${file}`);
  return [...m[1].matchAll(/'([^']+)'/g)].map((x) => x[1]);
}

const lists = {
  'firmware/ledmatrix/effects.cpp': firmwareEffects(),
  'shared/effects.json': JSON.parse(read('shared/effects.json')).effects.map((e) => e.name),
  'mcp/server.mjs': arrayLiteral('mcp/server.mjs', 'EFFECTS'),
  'docs/index.html': arrayLiteral('docs/index.html', 'DEMO_EFFECTS'),
};

const [refName, ref] = Object.entries(lists)[0];
let ok = true;
for (const [file, names] of Object.entries(lists)) {
  if (names.join(',') !== ref.join(',')) {
    ok = false;
    console.error(`effects in ${file} differ from ${refName}:\n  ${names.join(', ')}\n  ${ref.join(', ')}`);
  }
}
const shared = JSON.parse(read('shared/effects.json')).effects;
for (const e of shared) {
  if (!e.es || !e.en || !/^#[0-9a-f]{6}$/.test(e.color)) {
    ok = false;
    console.error(`shared/effects.json: ${e.name} needs es, en and a #rrggbb color`);
  }
}
if (!ok) process.exit(1);
console.log(`effects consistent (${ref.length}) across ${Object.keys(lists).length} files`);
