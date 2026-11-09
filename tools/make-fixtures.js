#!/usr/bin/env node
// tools/make-fixtures.js: writes the golden fixtures (docs/PLUGIN-PLAN.md section 5) from the tested prototype.
//   node tools/make-fixtures.js                     write every fixture under Tests/fixtures/
//   node tools/make-fixtures.js --only model,engine write only those areas
//   node tools/make-fixtures.js --check             regenerate in memory, compare with the committed files, enforce
//                                                   the size budget; exit 1 on any difference (CI, Linux job)
//   node tools/make-fixtures.js --out DIR           write somewhere else (for example to compare two runs)
// Areas: one per module in tools/fixtures/ (common.js excepted), discovered, so adding an area never edits this file:
// model, sounds, support, engine, analysis, ui, state (state/prototype-*.json and state/import.json, M8), params (no
// files: checks docs/parameters.json against SLModel on every run). A module may export check() -> problem strings.
// Node only, no npm packages. The prototype in prototype-v4/ is read, never written. Made with Node 24 (CI pins it).
'use strict';
const fs = require('fs');
const path = require('path');
const C = require('./fixtures/common.js');

// the areas, in a fixed order first (only the log uses it), then any other module alphabetically
const FIRST = ['model', 'sounds', 'support', 'engine', 'analysis', 'ui', 'state', 'params'];
const FOUND = fs.readdirSync(path.join(__dirname, 'fixtures')).filter(n => /.js$/.test(n) && n !== 'common.js').map(n => n.slice(0, -3));
const AREAS = FIRST.filter(a => FOUND.includes(a)).concat(FOUND.filter(a => !FIRST.includes(a)).sort());
const NODE_MAJOR = 24;   // the Node major the committed fixtures were made with (plan 1.11: CI runs setup-node 24)
const BUDGET_TOTAL = 5000000, BUDGET_FILE = 1200000;   // bytes: total < 5 MB, any file < 1.2 MB (plan 5.4)

function parseArgs(argv) {
  const o = { only: null, check: false, out: path.join(C.ROOT, 'Tests', 'fixtures'), quiet: false };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--check') o.check = true;
    else if (a === '--quiet') o.quiet = true;
    else if (a === '--only') o.only = String(argv[++i] || '').split(',').map(s => s.trim()).filter(Boolean);
    else if (a.startsWith('--only=')) o.only = a.slice(7).split(',').map(s => s.trim()).filter(Boolean);
    else if (a === '--out') o.out = path.resolve(argv[++i]);
    else if (a.startsWith('--out=')) o.out = path.resolve(a.slice(6));
    else if (a === '--help' || a === '-h') { const ls = fs.readFileSync(__filename, 'utf8').split('\n').slice(1); console.log(ls.slice(0, ls.findIndex(l => !l.startsWith('//'))).map(l => l.replace(/^\/\/ ?/, '')).join('\n')); process.exit(0); }
    else { console.error('make-fixtures: unknown argument ' + a); process.exit(2); }
  }
  if (o.only) for (const a of o.only) if (!AREAS.includes(a)) { console.error('make-fixtures: unknown area ' + a + ' (areas: ' + AREAS.join(', ') + ')'); process.exit(2); }
  return o;
}

function generate(areas, log) {
  const texts = {};
  for (const area of areas) {
    const t0 = Date.now();   // timing for the log only; nothing generated depends on it
    const files = require('./fixtures/' + area + '.js')();
    for (const name of Object.keys(files)) {
      const f = files[name];
      texts[name] = f.raw != null ? f.raw : C.fixtureText(name, f);
      if (log) log(name, f.cases ? f.cases.length : null, Buffer.byteLength(texts[name]));
    }
    if (log) log(null, null, null, area, (Date.now() - t0) / 1000);
  }
  return texts;
}

function runChecks(areas) {
  const problems = [];
  for (const area of areas) {
    const mod = require('./fixtures/' + area + '.js');
    if (typeof mod.check === 'function') for (const p of mod.check()) problems.push(p);
  }
  return problems;
}

function main() {
  const o = parseArgs(process.argv.slice(2));
  const areas = o.only || AREAS;
  const major = +process.versions.node.split('.')[0];
  if (major !== NODE_MAJOR) console.log('note: running on Node ' + process.version + '; the fixtures were made with Node ' + NODE_MAJOR + '. Math.sin/exp/pow can differ between V8 versions, so a --check difference with no source change may come from that.');
  const rows = [];
  const log = (name, cases, bytes, area, secs) => {
    if (name) rows.push([name, cases, bytes]);
    else if (!o.quiet) console.log('  ' + area + ' done in ' + secs.toFixed(1) + ' s');
  };
  const texts = generate(areas, log);
  let total = 0, bad = 0;
  if (!o.quiet) console.log('\n' + 'file'.padEnd(34) + 'cases'.padStart(7) + 'bytes'.padStart(11));
  for (const [name, cases, bytes] of rows) {
    total += bytes;
    const over = bytes >= BUDGET_FILE;
    if (over) bad++;
    if (!o.quiet) console.log(name.padEnd(34) + (cases == null ? '-' : String(cases)).padStart(7) + String(bytes).padStart(11) + (over ? '  OVER the 1.2 MB file budget' : ''));
  }
  const all = !o.only;
  if (!o.quiet) console.log('total'.padEnd(41) + String(total).padStart(11) + ' bytes (' + (total / 1e6).toFixed(2) + ' MB' + (all ? '' : ', these areas only') + ')');
  if (all && total >= BUDGET_TOTAL) { console.log('OVER the 5 MB total budget'); bad++; }
  const problems = runChecks(areas);
  for (const p of problems) console.log('CHECK ' + p);
  if (problems.length) bad++;
  else if (!o.quiet && areas.includes('params')) console.log('docs/parameters.json agrees with SLModel');

  if (o.check) {
    let diff = 0;
    for (const name of Object.keys(texts)) {
      const file = path.join(o.out, name);
      if (!fs.existsSync(file)) { console.log('MISSING ' + name); diff++; continue; }
      const have = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');   // a Windows checkout may have CRLF
      if (have !== texts[name]) {
        let i = 0; while (i < have.length && have[i] === texts[name][i]) i++;
        const line = have.slice(0, i).split('\n').length;
        console.log('DIFFERS ' + name + ' (first difference on line ' + line + ')');
        diff++;
      }
    }
    if (all) {   // a committed fixture the generator no longer makes is stale
      const walk = d => fs.existsSync(d) ? fs.readdirSync(d, { withFileTypes: true }).flatMap(e => e.isDirectory() ? walk(path.join(d, e.name)) : [path.join(d, e.name)]) : [];
      for (const f of walk(o.out)) {
        const rel = path.relative(o.out, f).split(path.sep).join('/');
        if (/\.json$/.test(rel) && !texts[rel] && !/^state\/v\d/.test(rel)) { console.log('STALE ' + rel + ' (not made by the generator)'); diff++; }
      }
    }
    console.log(diff ? diff + ' fixture file(s) differ from what the prototype makes today' : 'fixtures match the prototype (' + Object.keys(texts).length + ' files)');
    process.exit(diff || bad ? 1 : 0);
  }
  for (const name of Object.keys(texts)) {
    const file = path.join(o.out, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, texts[name]);
  }
  if (!o.quiet) console.log('wrote ' + Object.keys(texts).length + ' files under ' + o.out);
  process.exit(bad ? 1 : 0);
}

main();
