// A minimal DOM for driving sl-ui-sample.js in node (no npm packages available).
'use strict';
class ClassList {
  constructor(n) { this.n = n; }
  _get() { return (this.n.getAttribute('class') || '').split(/\s+/).filter(Boolean); }
  _set(a) { this.n.setAttribute('class', a.join(' ')); }
  add(...c) { const a = this._get(); for (const x of c) if (!a.includes(x)) a.push(x); this._set(a); }
  remove(...c) { this._set(this._get().filter(x => !c.includes(x))); }
  contains(c) { return this._get().includes(c); }
  toggle(c, force) { const has = this.contains(c); const want = force === undefined ? !has : !!force; if (want && !has) this.add(c); if (!want && has) this.remove(c); return want; }
}
class Style { setProperty(k, v) { this[k] = v; } getPropertyValue(k) { return this[k] || ''; } }
class Node {
  constructor(doc, tag, ns) {
    this.ownerDocument = doc; this.tagName = tag ? tag.toUpperCase() : '#text'; this.namespaceURI = ns || null;
    this.childNodes = []; this.parentNode = null; this.attrs = new Map(); this.listeners = {}; this.style = new Style();
    this.classList = new ClassList(this); this.dataset = {}; this._text = ''; this.nodeType = tag ? 1 : 3;
    this._value = ''; this.disabled = false; this.options = [];
  }
  get children() { return this.childNodes.filter(c => c.nodeType === 1); }
  get firstChild() { return this.childNodes[0] || null; }
  get nextSibling() { const p = this.parentNode; if (!p) return null; const i = p.childNodes.indexOf(this); return p.childNodes[i + 1] || null; }
  setAttribute(k, v) { this.attrs.set(k, String(v)); if (k === 'value' && this.tagName === 'INPUT') this._value = String(v); }
  getAttribute(k) { return this.attrs.has(k) ? this.attrs.get(k) : null; }
  removeAttribute(k) { this.attrs.delete(k); }
  hasAttribute(k) { return this.attrs.has(k); }
  get id() { return this.getAttribute('id') || ''; } set id(v) { this.setAttribute('id', v); }
  get className() { return this.getAttribute('class') || ''; } set className(v) { this.setAttribute('class', v); }
  get title() { return this.getAttribute('title') || ''; } set title(v) { this.setAttribute('title', v); }
  get hidden() { return this.hasAttribute('hidden'); } set hidden(v) { if (v) this.setAttribute('hidden', ''); else this.removeAttribute('hidden'); }
  get textContent() { return this.nodeType === 3 ? this._text : this.childNodes.map(c => c.textContent).join(''); }
  set textContent(v) { if (this.nodeType === 3) { this._text = String(v); return; } this.childNodes.forEach(c => { c.parentNode = null; }); this.childNodes = []; if (v !== '' && v != null) this.appendChild(this.ownerDocument.createTextNode(String(v))); }
  set innerHTML(v) { if (v !== '') throw new Error('minidom: innerHTML only clears'); this.textContent = ''; this.options = []; }
  get innerHTML() { return ''; }
  get value() { if (this.tagName === 'SELECT') return this._value; return this._value; }
  set value(v) { if (this.tagName === 'SELECT') { if (this.options.some(o => o.value === String(v))) this._value = String(v); } else this._value = String(v); }
  get selectedOptions() { return this.options.filter(o => o.value === this._value); }
  add(opt) { this.options.push(opt); this.appendChild(opt); if (this.options.length === 1) this._value = opt.value; }
  appendChild(c) { if (c.parentNode) c.remove(); c.parentNode = this; this.childNodes.push(c); return c; }
  append(...cs) { for (const c of cs) this.appendChild(typeof c === 'string' ? this.ownerDocument.createTextNode(c) : c); }
  remove() { const p = this.parentNode; if (p) { p.childNodes.splice(p.childNodes.indexOf(this), 1); this.parentNode = null; } }
  replaceWith(n) { const p = this.parentNode; if (!p) return; const i = p.childNodes.indexOf(this); if (n.parentNode) n.remove(); p.childNodes.splice(i, 1, n); n.parentNode = p; this.parentNode = null; }
  contains(n) { for (let x = n; x; x = x.parentNode) if (x === this) return true; return false; }
  closest(sel) { for (let x = this; x && x.nodeType === 1; x = x.parentNode) if (matches(x, sel)) return x; return null; }
  addEventListener(t, f) { (this.listeners[t] || (this.listeners[t] = [])).push(f); }
  removeEventListener(t, f) { const a = this.listeners[t]; if (a) { const i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } }
  focus() { this.ownerDocument.activeElement = this; }
  blur() { if (this.ownerDocument.activeElement === this) this.ownerDocument.activeElement = null; }
  select() {}
  click() { dispatch(this, 'click', {}); }
  setPointerCapture() {} releasePointerCapture() {}
  getBoundingClientRect() { return { left: 10, top: 10, right: 60, bottom: 30, width: 50, height: 20 }; }
  get offsetWidth() { return 50; } get offsetHeight() { return 20; }
}
function matches(n, sel) {
  if (sel.startsWith('.')) return n.classList.contains(sel.slice(1));
  if (sel.startsWith('#')) return n.id === sel.slice(1);
  return n.tagName === sel.toUpperCase();
}
function walk(n, f) { f(n); for (const c of n.childNodes) walk(c, f); }
function dispatch(node, type, props) {
  const ev = Object.assign({ type, target: node, button: 0, clientX: 0, clientY: 0, pointerId: 1, detail: 1, shiftKey: false, ctrlKey: false, metaKey: false, key: '',
    defaultPrevented: false, _stop: false, preventDefault() { this.defaultPrevented = true; }, stopPropagation() { this._stop = true; } }, props || {});
  for (let x = node; x; x = x.parentNode) {
    ev.currentTarget = x;
    for (const f of [...(x.listeners[type] || [])]) f(ev);
    if (ev._stop || ['mouseenter', 'mouseleave'].includes(type)) return ev;
  }
  const doc = node.ownerDocument;
  for (const f of [...(doc.listeners[type] || [])]) f(ev);
  return ev;
}
function makeDocument() {
  const doc = { listeners: {}, activeElement: null };
  doc.createElement = t => new Node(doc, t);
  doc.createElementNS = (ns, t) => new Node(doc, t, ns);
  doc.createTextNode = t => { const n = new Node(doc, null); n._text = String(t); return n; };
  doc.documentElement = new Node(doc, 'html');
  doc.body = new Node(doc, 'body');
  doc.documentElement.appendChild(doc.body);
  doc.getElementById = id => { let f = null; walk(doc.documentElement, n => { if (!f && n.nodeType === 1 && n.id === id) f = n; }); return f; };
  doc.querySelectorAll = sel => { const out = []; walk(doc.documentElement, n => { if (n.nodeType === 1 && matches(n, sel)) out.push(n); }); return out; };
  doc.addEventListener = (t, f) => { (doc.listeners[t] || (doc.listeners[t] = [])).push(f); };
  return doc;
}
function Option(text, value) { const o = new Node(Option.doc, 'option'); o.textContent = text; o.value = value === undefined ? text : String(value); return o; }
function findAll(root, pred) { const out = []; walk(root, n => { if (n.nodeType === 1 && pred(n)) out.push(n); }); return out; }
module.exports = { makeDocument, dispatch, Option, findAll, walk };
