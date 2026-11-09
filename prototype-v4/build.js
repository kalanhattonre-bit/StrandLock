// StrandLock prototype v4: build.js
// Writes two files from the same parts:
//   dist/prototype-v4-strandlock.html  a full document (doctype, html, head, body) that opens by double-click
//   dist/artifact.html                 the same content without doctype/html/head/body, for a claude.ai Artifact:
//                                      <title>, the viewport-fit meta, <style>, then the markup and the scripts
// Order: src/shell.html; every src/sl-ui*.css (as <style>, sorted); scripts sl-model.js, sl-sounds.js, sl-analysis.js,
// sl-engine.js, sl-ui.js, then every other src/sl-ui-*.js (sorted by name). Every inline script is syntax-checked
// with new Function. Nothing is fetched from the network: everything is inline.
// Usage: node build.js
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = __dirname, SRC = path.join(ROOT, 'src'), DIST = path.join(ROOT, 'dist');
const MAX_BYTES = 3 * 1024 * 1024;
const read = f => fs.readFileSync(path.join(SRC, f), 'utf8').replace(/^﻿/, '');
const kb = n => (n / 1024).toFixed(1) + ' KB';
let problems = 0;
const fail = msg => { console.error('ERROR ' + msg); problems++; };

// ---- shell: <!--SL:HEAD--> head part <!--SL:STYLES--> <!--SL:BODY--> body part <!--SL:SCRIPTS-->
const shell = read('shell.html');
const mark = name => { const i = shell.indexOf('<!--SL:' + name + '-->'); if (i < 0) fail('shell.html is missing <!--SL:' + name + '-->'); return i; };
const iHead = mark('HEAD'), iStyles = mark('STYLES'), iBody = mark('BODY'), iScripts = mark('SCRIPTS');
if (problems) process.exit(1);
if (!(iHead < iStyles && iStyles < iBody && iBody < iScripts)) { fail('shell.html markers are out of order'); process.exit(1); }
const headPart = shell.slice(iHead + '<!--SL:HEAD-->'.length, iStyles).trim();
const bodyPart = shell.slice(iBody + '<!--SL:BODY-->'.length, iScripts).trim();
const tailPart = shell.slice(iScripts + '<!--SL:SCRIPTS-->'.length).trim();
if (!headPart.startsWith('<title>StrandLock v4</title>')) fail('the head part must start with <title>StrandLock v4</title>');

// ---- extra stylesheets
const files = fs.readdirSync(SRC);
const cssFiles = files.filter(f => /^sl-ui.*\.css$/.test(f)).sort();
const styles = cssFiles.map(f => {
  const css = read(f);
  if (/<\/style/i.test(css)) fail(f + ' contains </style');
  return '<style data-src="' + f + '">\n' + css.trim() + '\n</style>';
});

// ---- scripts
const CORE = ['sl-model.js', 'sl-sounds.js', 'sl-analysis.js', 'sl-engine.js', 'sl-ui.js'];
const extra = files.filter(f => /^sl-ui-.*\.js$/.test(f)).sort();
const order = CORE.concat(extra);
const sizes = [];
const scripts = order.map(f => {
  if (!fs.existsSync(path.join(SRC, f))) { fail('missing src/' + f); return ''; }
  let js = read(f);
  try { new Function(js); } catch (e) { fail('syntax error in ' + f + ': ' + e.message); }
  if (/<\/script/i.test(js)) js = js.replace(/<\/script/gi, '<\\/script');   // never close the inline <script> early
  if (/\bimport\s*\(|^\s*import\s|^\s*export\s/m.test(js)) fail(f + ' uses import/export (plain scripts only)');
  sizes.push([f, Buffer.byteLength(js)]);
  return '<script data-src="' + f + '">\n' + js + '\n</script>';
});
if (problems) process.exit(1);

// ---- network check: nothing may load from the web
const inner = [headPart, styles.join('\n'), bodyPart, scripts.join('\n'), tailPart].join('\n');
const net = inner.match(/\b(?:src|href)\s*=\s*["']\s*(?:https?:)?\/\//gi) || [];
const imp = inner.match(/@import\s+url\(\s*["']?(?:https?:)?\/\//gi) || [];
if (net.length || imp.length) fail('found a network reference: ' + net.concat(imp).join(', '));

// ---- assemble
const artifact = [headPart, styles.join('\n'), bodyPart, scripts.join('\n'), tailPart].filter(Boolean).join('\n') + '\n';
const full = '<!DOCTYPE html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n' +
  '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n' +
  '<link rel="icon" href="data:,">\n' +   // no favicon request when the file is served or double-clicked (head only, so not in artifact.html)
  headPart + '\n' + styles.join('\n') + '\n</head>\n<body>\n' + bodyPart + '\n' + scripts.join('\n') + '\n' + tailPart + '\n</body>\n</html>\n';
if (/<!doctype|<html[\s>]|<\/html>|<head[\s>]|<\/head>|<body[\s>]|<\/body>/i.test(artifact.replace(/<script[\s\S]*?<\/script>/gi, ''))) fail('artifact.html must not contain doctype/html/head/body tags');
if (!artifact.startsWith('<title>StrandLock v4</title>\n<meta name="claude:viewport-fit-sides" content="cover">\n<style')) fail('artifact.html must start with the title, the viewport-fit meta, then <style>');
if (/<link[\s>]/i.test(artifact.replace(/<script[\s\S]*?<\/script>/gi, ''))) fail('artifact.html must not carry <link> elements (they belong in a head)');
for (const [name, txt] of [['prototype-v4-strandlock.html', full], ['artifact.html', artifact]]) {
  const n = Buffer.byteLength(txt);
  if (n > MAX_BYTES) fail(name + ' is ' + kb(n) + ', over the 3 MB limit');
}
if (problems) process.exit(1);

fs.mkdirSync(DIST, { recursive: true });
fs.writeFileSync(path.join(DIST, 'prototype-v4-strandlock.html'), full);
fs.writeFileSync(path.join(DIST, 'artifact.html'), artifact);

console.log('StrandLock v4 build');
console.log('  shell.html'.padEnd(26) + kb(Buffer.byteLength(headPart + bodyPart + tailPart)).padStart(10));
cssFiles.forEach((f, i) => console.log(('  ' + f).padEnd(26) + kb(Buffer.byteLength(styles[i])).padStart(10)));
for (const [f, n] of sizes) console.log(('  ' + f).padEnd(26) + kb(n).padStart(10) + '   syntax OK');
console.log('  dist/prototype-v4-strandlock.html'.padEnd(40) + kb(Buffer.byteLength(full)).padStart(10));
console.log('  dist/artifact.html'.padEnd(40) + kb(Buffer.byteLength(artifact)).padStart(10));
