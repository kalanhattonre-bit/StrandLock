'use strict';
// A small fake DOM: enough for sl-ui-panels.js (elements, events with capture/bubble, selects, inputs).
class Node_ {
  constructor() { this.parentNode = null; this.childNodes = []; this.listeners = []; }
  get firstChild() { return this.childNodes[0] || null; }
  appendChild(c) {
    if (c == null) throw new Error('appendChild(null)');
    if (c.parentNode) c.parentNode.removeChild(c);
    c.parentNode = this; this.childNodes.push(c); return c;
  }
  removeChild(c) { const i = this.childNodes.indexOf(c); if (i >= 0) this.childNodes.splice(i, 1); c.parentNode = null; return c; }
  append(...xs) { for (const x of xs) { if (x == null) throw new Error('append(' + x + ')'); this.appendChild(typeof x === 'string' || typeof x === 'number' ? new Text_(String(x)) : x); } }
  contains(n) { while (n) { if (n === this) return true; n = n.parentNode; } return false; }
  get textContent() { return this.childNodes.map(c => c.textContent).join(''); }
  set textContent(v) { this.childNodes.forEach(c => { c.parentNode = null; }); this.childNodes = []; if (v !== '' && v != null) this.appendChild(new Text_(String(v))); }
  addEventListener(type, fn, cap) { this.listeners.push({ type, fn, cap: cap === true || !!(cap && cap.capture) }); }
  removeEventListener(type, fn) { this.listeners = this.listeners.filter(l => !(l.type === type && l.fn === fn)); }
  dispatchEvent(ev) {
    ev.target = this;
    const path = []; let n = this.parentNode; while (n) { path.unshift(n); n = n.parentNode; }
    if (global.__fakeWindow) path.unshift(global.__fakeWindow);
    let stopped = false;
    ev.stopPropagation = () => { stopped = true; }; ev.stopImmediatePropagation = ev.stopPropagation;
    ev.preventDefault = () => { ev.defaultPrevented = true; };
    const run = (node, cap) => { for (const l of node.listeners.slice()) if (l.type === ev.type && (cap === null || l.cap === cap)) { ev.currentTarget = node; l.fn.call(node, ev); } };
    for (const nd of path) { run(nd, true); if (stopped) return; }
    run(this, null); if (stopped || ev.bubbles === false) return;
    for (const nd of path.slice().reverse()) { run(nd, false); if (stopped) return; }
  }
}
class Text_ extends Node_ { constructor(t) { super(); this.data = t; } get textContent() { return this.data; } set textContent(v) { this.data = String(v); } }
class ClassList { constructor(e) { this.e = e; } get list() { return this.e.className.split(/\s+/).filter(Boolean); }
  add(...c) { const l = this.list; for (const x of c) if (!l.includes(x)) l.push(x); this.e.className = l.join(' '); }
  remove(...c) { this.e.className = this.list.filter(x => !c.includes(x)).join(' '); }
  toggle(c, on) { const has = this.list.includes(c); const want = on === undefined ? !has : !!on; if (want) this.add(c); else this.remove(c); return want; }
  contains(c) { return this.list.includes(c); } }
class Element_ extends Node_ {
  constructor(tag) {
    super(); this.tagName = tag.toUpperCase(); this.className = ''; this.style = {}; this.dataset = {}; this.attrs = {};
    this.classList = new ClassList(this); this.disabled = false; this.hidden = false; this.title = ''; this._value = '';
    if (this.tagName === 'SELECT') this.options = [];
  }
  get id() { return this.attrs.id || ''; }
  setAttribute(k, v) { this.attrs[k] = String(v); if (k === 'class') this.className = String(v); if (k === 'title') this.title = String(v); if (k === 'hidden') this.hidden = true; if (k === 'disabled') this.disabled = true; if (k === 'value') this._value = String(v); if (k === 'type') this.type = String(v); if (k === 'min') this.min = +v; if (k === 'max') this.max = +v; if (k === 'step') this.step = +v; }
  getAttribute(k) { return this.attrs[k]; }
  set innerHTML(v) { if (v !== '') throw new Error('innerHTML only clears here'); this.textContent = ''; }
  get innerHTML() { return ''; }
  get children() { return this.childNodes.filter(c => c instanceof Element_); }
  get previousElementSibling() { if (!this.parentNode) return null; const k = this.parentNode.children; const i = k.indexOf(this); return i > 0 ? k[i - 1] : null; }
  get nextSibling() { if (!this.parentNode) return null; const k = this.parentNode.childNodes; return k[k.indexOf(this) + 1] || null; }
  matches(sel) { const parts = sel.trim().split(/\s+/); if (!simple(this, parts[parts.length - 1])) return false; let n = this.parentNode; for (let i = parts.length - 2; i >= 0; i--) { while (n && !(n instanceof Element_ && simple(n, parts[i]))) n = n.parentNode; if (!n) return false; n = n.parentNode; } return true; }
  closest(sel) { let n = this; while (n) { if (n instanceof Element_ && n.matches(sel)) return n; n = n.parentNode; } return null; }
  querySelectorAll(sel) { const out = []; const walk = n => { for (const c of n.children) { if (c.matches(sel)) out.push(c); walk(c); } }; walk(this); return out; }
  querySelector(sel) { return this.querySelectorAll(sel)[0] || null; }
  getBoundingClientRect() { return { left: 10, top: 10, right: 50, bottom: 30, width: 40, height: 20 }; }
  focus() { global.__fakeDoc.activeElement = this; }
  blur() { if (global.__fakeDoc.activeElement === this) { global.__fakeDoc.activeElement = global.__fakeDoc.body; } }
  select() {}
  click() { this.dispatchEvent({ type: 'click', bubbles: true, detail: 1 }); }
  get value() { if (this.tagName === 'SELECT') { const o = this.options.find(o => o.selected) || this.options[0]; return o ? o.value : ''; } return this._value; }
  set value(v) { if (this.tagName === 'SELECT') { let hit = false; for (const o of this.options) { o.selected = !hit && o.value === String(v); if (o.selected) hit = true; } } else this._value = String(v); }
  add(o) { this.options.push(o); if (this.options.length === 1) o.selected = true; }
  get selectedOptions() { return this.options.filter(o => o.selected); }
}
function simple(e, s) { if (s[0] === '.') return e.classList.contains(s.slice(1)); if (s[0] === '#') return e.id === s.slice(1); return e.tagName === s.toUpperCase(); }
class Option_ { constructor(text, value) { this.text = text; this.value = value == null ? text : String(value); this.selected = false; this.disabled = false; this.hidden = false; } }
function makeDom() {
  const document = { activeElement: null, body: new Element_('body'), ids: {} };
  document.createElement = t => new Element_(t);
  document.createTextNode = t => new Text_(t);
  document.getElementById = id => document.ids[id] || null;
  document.addEventListener = (t, f, c) => document.body.addEventListener(t, f, c);
  document.contains = n => document.body.contains(n);
  document.activeElement = document.body;
  const window = new Node_();
  window.document = document;
  global.__fakeDoc = document; global.__fakeWindow = window;
  const reg = (id, tag, parent) => { const e = new Element_(tag || 'div'); e.setAttribute('id', id); document.ids[id] = e; (parent || document.body).appendChild(e); return e; };
  return { document, window, reg, Option: Option_, Element_ };
}
module.exports = { makeDom };
