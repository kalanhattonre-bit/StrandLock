// Runs every tests/test-*.js with node, prints each file's output, then a summary.
// Exits non-zero if any file fails (non-zero exit code, or any FAIL line, or a crash).
'use strict';
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const dir = __dirname;
const only = process.argv.slice(2);
const files = fs.readdirSync(dir)
  .filter(f => /^test-.*\.js$/.test(f))
  .filter(f => !only.length || only.some(o => f.includes(o)))
  .sort();

const rows = [];
let bad = 0;
for (const f of files) {
  const t0 = Date.now();
  const r = spawnSync(process.execPath, [path.join(dir, f)], { cwd: path.join(dir, '..'), encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, timeout: 15 * 60 * 1000 });
  const ms = Date.now() - t0;
  const out = (r.stdout || '') + (r.stderr || '');
  process.stdout.write('\n===== ' + f + ' =====\n' + out + (out.endsWith('\n') ? '' : '\n'));
  const pass = (out.match(/^\s*\[?PASS\]?\b/gm) || []).length;
  const fail = (out.match(/^\s*\[?FAIL\]?\b/gm) || []).length;
  const code = r.status == null ? (r.signal || 'killed') : r.status;
  const ok = code === 0 && fail === 0 && !r.error;
  if (!ok) bad++;
  rows.push({ f, pass, fail, code, ms, ok, err: r.error ? String(r.error.message || r.error) : '' });
}

console.log('\n===== summary =====');
for (const r of rows) {
  console.log((r.ok ? 'OK   ' : 'FAIL ') + r.f.padEnd(28) + ' pass ' + String(r.pass).padStart(4) + '  fail ' + String(r.fail).padStart(3) +
    '  exit ' + String(r.code).padStart(3) + '  ' + (r.ms / 1000).toFixed(1) + ' s' + (r.err ? '  ' + r.err : ''));
}
const tp = rows.reduce((a, r) => a + r.pass, 0), tf = rows.reduce((a, r) => a + r.fail, 0);
console.log(`${files.length} file(s), ${tp} checks passed, ${tf} failed, ${bad} file(s) not OK`);
if (!files.length) console.log('no tests/test-*.js files found');
process.exit(bad ? 1 : 0);
