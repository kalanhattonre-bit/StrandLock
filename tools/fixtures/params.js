// tools/fixtures/params.js: checks docs/parameters.json against SLModel (PLUGIN-PLAN section 2). Writes no fixture.
// make-fixtures.js runs check() on every run, --check included, so a change to SLModel's defaults, clamps or lists
// that docs/parameters.json does not follow fails CI (Linux job). The frozen-release check is tools/check-params.js (M9).
'use strict';
const fs = require('fs');
const path = require('path');
const C = require('./common.js');

// inst fields that are plugin state, not parameters (PLUGIN-PLAN 2.2). Any other defaultInst() field must be a parameter.
const STATE_FIELDS = ['seed', 'mod1', 'mod2', 'key', 'scale', 'scaleLock'];
// choice parameters: the SLModel list their choices follow, by inst field
function lists(M) {
  return { engine: M.ENGINES, motion: M.MOTIONS, headTune: M.HEAD_TUNES, jump: M.RATES, 'refresh.mode': M.REFRESH_MODES,
    'refresh.rate': M.RATES, 'region.atEnd': M.AT_END, glideMode: M.GLIDE_MODES };
}
function getField(o, f) {
  const m = /^(\w+)(?:\[(\d+)\])?(?:\.(\w+))?$/.exec(f);
  if (!m) return undefined;
  let v = o[m[1]];
  if (m[2] != null) v = v == null ? undefined : v[+m[2]];
  if (m[3]) v = v == null ? undefined : v[m[3]];
  return v;
}
function patchFor(d, f, val) {
  const m = /^(\w+)(?:\[(\d+)\])?(?:\.(\w+))?$/.exec(f);
  if (m[2] != null) { const a = d[m[1]].slice(); a[+m[2]] = val; return { [m[1]]: a }; }
  if (m[3]) return { [m[1]]: { [m[3]]: val } };
  return { [m[1]]: val };
}

function check() {
  const { M } = C.load();
  const file = path.join(C.ROOT, 'docs', 'parameters.json');
  const problems = [];
  const bad = (id, what) => problems.push('docs/parameters.json ' + id + ': ' + what);
  let P;
  try { P = JSON.parse(fs.readFileSync(file, 'utf8')); } catch (e) { return ['docs/parameters.json: cannot be read (' + e.message + ')']; }
  const d = M.defaultInst(), L = lists(M);
  const ids = new Set();
  P.forEach((p, i) => {
    if (p.index !== i) bad(p.id, 'index ' + p.index + ', expected ' + i);
    if (ids.has(p.id)) bad(p.id, 'duplicate id'); ids.add(p.id);
    for (const k of ['index', 'id', 'name', 'type', 'min', 'max', 'default', 'unit', 'instField', 'smoothing']) if (!(k in p)) bad(p.id, 'missing key ' + k);
    if (!['float', 'int', 'bool', 'choice'].includes(p.type)) bad(p.id, 'unknown type ' + p.type);
    if (p.type === 'float' && p.step !== 0) bad(p.id, 'a float parameter has step ' + p.step + ' (plan 2: continuous; the UI snaps knobs as the prototype does)');
  });
  // D8: the 8 macros come first, target-neutral ids, named with SLModel.MACRO_NAMES
  for (let i = 0; i < 8; i++) {
    const p = P[i];
    if (!p || p.id !== 'macro' + (i + 1)) bad(p ? p.id : '#' + i, 'parameter ' + i + ' must be macro' + (i + 1));
    else if (p.name !== M.MACRO_NAMES[i]) bad(p.id, 'name ' + p.name + ', SLModel.MACRO_NAMES says ' + M.MACRO_NAMES[i]);
  }
  const covered = new Set();
  for (const p of P) {
    const f = p.instField;
    if (f == null) continue;   // bypass
    const top = /^(\w+)/.exec(f)[1];
    covered.add(top);
    if (f.startsWith('vibSync')) {   // a plugin addition (8.2 #12): Free + the 21 rates
      if (JSON.stringify(p.rateIds) !== JSON.stringify([null].concat(M.RATES))) bad(p.id, 'rateIds must be [null, ...SLModel.RATES]');
      if (p.choices.length !== 22 || p.choices[0] !== 'Free') bad(p.id, 'choices must be Free + 21 rates');
      else M.RATES.forEach((r, j) => { if (p.choices[j + 1] !== M.rateLabel(r)) bad(p.id, 'choice ' + (j + 1) + ' is ' + p.choices[j + 1] + ', rateLabel says ' + M.rateLabel(r)); });
      continue;
    }
    const dv = getField(d, f);
    if (dv === undefined) { bad(p.id, 'instField ' + f + ' is not in SLModel.defaultInst()'); continue; }
    if (p.type === 'choice') {
      const list = L[f];
      if (!list) { bad(p.id, 'no SLModel list known for choice field ' + f); continue; }
      if (p.choices.length !== list.length) bad(p.id, p.choices.length + ' choices, SLModel has ' + list.length);
      if (p.max !== list.length - 1 || p.min !== 0) bad(p.id, 'range must be 0..' + (list.length - 1));
      if (list.indexOf(dv) !== p.default) bad(p.id, 'default ' + p.default + ', SLModel default ' + JSON.stringify(dv) + ' is index ' + list.indexOf(dv));
      if (list === M.RATES) {
        if (JSON.stringify(p.rateIds) !== JSON.stringify(M.RATES)) bad(p.id, 'rateIds must equal SLModel.RATES');
        list.forEach((r, j) => { if (p.choices[j] !== M.rateLabel(r)) bad(p.id, 'choice ' + j + ' is ' + p.choices[j] + ', rateLabel says ' + M.rateLabel(r)); });
      } else list.forEach((id, j) => { const c = String(p.choices[j] || '').toLowerCase(); if (!c.startsWith(id)) bad(p.id, 'choice ' + j + ' "' + p.choices[j] + '" does not name SLModel id ' + id); });
      // normalizeInst falls back to the default for an unknown value
      if (getField(M.normalizeInst(patchFor(d, f, '__unknown__')), f) !== dv) bad(p.id, 'normalizeInst does not fall back to the default');
      continue;
    }
    if (p.type === 'bool') {
      if ((dv ? 1 : 0) !== p.default) bad(p.id, 'default ' + p.default + ', SLModel ' + dv);
      if (p.min !== 0 || p.max !== 1) bad(p.id, 'a bool is 0..1');
      continue;
    }
    if (dv !== p.default) bad(p.id, 'default ' + p.default + ', SLModel ' + dv);
    const lo = getField(M.normalizeInst(patchFor(d, f, -1e9)), f), hi = getField(M.normalizeInst(patchFor(d, f, 1e9)), f);
    if (lo !== p.min || hi !== p.max) bad(p.id, 'range ' + p.min + '..' + p.max + ', normalizeInst clamps to ' + lo + '..' + hi);
    if (p.type === 'int') {
      const r = getField(M.normalizeInst(patchFor(d, f, p.min + 0.4)), f);
      if (p.step !== 1) bad(p.id, 'an int parameter has step 1');
      if (r !== p.min && f !== 'root' && f !== 'bendRange') bad(p.id, 'normalizeInst does not round ' + f + ' (int parameter)');
    }
  }
  for (const k of Object.keys(d)) if (!covered.has(k) && !STATE_FIELDS.includes(k)) bad(k, 'SLModel.defaultInst() field ' + k + ' is neither a parameter nor plan 2.2 state');
  for (const k of STATE_FIELDS) if (covered.has(k)) bad(k, 'plan 2.2 keeps ' + k + ' as plugin state, but a parameter maps to it');
  const last = P[P.length - 1];
  if (!last || last.id !== 'bypass' || last.instField !== null) bad(last ? last.id : '-', 'the last parameter must be the host bypass (instField null)');
  return problems;
}

module.exports = function params() { return {}; };   // no fixture files
module.exports.check = check;
