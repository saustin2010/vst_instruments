"""Stitch mockup -> what an MPC skin needs (runs in the mpc-vst-html-art container: Playwright + Pillow).
   python3 extract.py <config.json> <out dir>
For each tab of the mockup: a 1280x628 background with the live parts hidden (knobs, names and values, switches,
sliders, steppers, canvases, plus the mockup's own header and Q-Link footer, which MPC draws itself), the position of
every control (with the parameter key candidates the mockup gives: data-*, ids, onclick/onmousedown arguments, the
label), and one knob filmstrip per distinct knob style, rendered from the mockup's own knob at each angle.
Writes <out>/bg_<tab>.png, <out>/knob_<n>.png and <out>/controls.json. convert.py turns that into a layout.conf."""
import hashlib, io, json, os, sys
from PIL import Image
from playwright.sync_api import sync_playwright

cfg = json.load(open(sys.argv[1]))
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
FRAMES = cfg.get("frames", 64)
SWEEP = cfg.get("sweep", 135)

JS_SETUP = r"""(cfg) => {
  // the 1280x628 plugin screen: cfg.root, else the outermost element that size (a few px wider counts; it may be the
  // body), else .mpc-chassis
  const root = (cfg.root && document.querySelector(cfg.root)) || [...document.querySelectorAll('body, body *')].find(e => {
      const r = e.getBoundingClientRect(); return Math.abs(r.width - 1280) <= 24 && r.height >= 600 && r.height <= 640; }) ||
    document.querySelector('.mpc-chassis');
  root.id = root.id || 'stitch-root';
  window.__root = root;
  // the mockup's own header (holds the tabs) and footer (Q-Link chips): MPC shows its own
  const kids = [...root.children];
  const before = new Map(kids.map(k => [k, k.getBoundingClientRect().height]));   // heights as designed
  for (const k of kids) {
    const t = k.innerText || '';
    if ((cfg.autodrop !== false && (k.querySelector(cfg.tabs) || /Q-?LINKS?/i.test(t.slice(0, 40)))) ||
        (cfg.drop || []).some(s => k.matches(s)))
      k.setAttribute('data-stitch-drop', '1');
  }
  for (const s of (cfg.drop || [])) document.querySelectorAll(s).forEach(e => e.setAttribute('data-stitch-drop', '1'));
  const st = document.createElement('style');
  st.textContent = `[data-stitch-drop]{display:none !important}
    #${root.id}{height:628px !important; min-height:628px !important; max-height:628px !important; overflow:hidden !important;
      width:1280px !important; min-width:1280px !important; max-width:1280px !important; box-sizing:border-box !important}
    .stitch-hide, .stitch-hide *{visibility:hidden !important}
    ${cfg.tabs || '.tab-btn'}{visibility:hidden !important; box-shadow:none !important}   /* an active tab's glow reaches into the screen */
    *{animation:none !important; transition:none !important}`;
  document.head.appendChild(st);
  // a grid frame (rows for header / content / footer) would put the content in the header's row once that's gone
  if (getComputedStyle(root).display.includes('grid') && kids.some(k => k.hasAttribute('data-stitch-drop')))
    root.style.cssText += ';display:flex !important; flex-direction:column !important;';
  // centre what's left in the 628 px the MPC gives a plugin (the header and footer took some of it)
  const body = [...root.children].find(k => !k.hasAttribute('data-stitch-drop') && k.getBoundingClientRect().height > 100);
  if (body && cfg.center !== false) {
    // keep the content at its designed height (a stretchy layout would otherwise spread into the freed space)
    const h0 = before.get(body) || body.getBoundingClientRect().height;
    if (h0 < 620) {
      body.style.height = h0 + 'px'; body.style.flex = 'none';
      body.style.marginTop = Math.round((628 - h0) / 2) + 'px';
    }
  }
  return true;
}"""

# controls a design leaves out, added in its own markup before anything is read (convert.py expands the macros):
# {sel, nth: [keys]} tags the matches with data-param in order; {sel, attr} sets attributes; {sel, html, where} inserts
# (beforeend / afterbegin / beforebegin / afterend / replace) at the first match (every match with all: true);
# {sel, each: [html, ...]} sets each match's contents in order; {sel, clone: [{replace: [[a, b]]}]} adds copies of
# the match after it with strings swapped (a design that drew one of four identical pages); {sel, text} sets its text;
# {sel, unclass: regex} drops matching classes (a Tailwind rotate-45 baked into a knob);
# {sel, remove: true} deletes (a design's made-up status text, say)
JS_AUGMENT = r"""(ops) => {
  const out = [];
  for (const op of ops || []) {
    let els = [...document.querySelectorAll(op.sel)];
    if (!els.length) { out.push('augment: nothing matches ' + op.sel); continue; }
    if (op.nth) { els.forEach((e, i) => op.nth[i] && e.setAttribute('data-param', op.nth[i])); continue; }
    if (op.retext) {   // [[from, to], ...] in every text node under the match: maker badges, captions that mislead
      for (const e of els) {
        const w = document.createTreeWalker(e, NodeFilter.SHOW_TEXT);
        while (w.nextNode()) for (const [a, b] of op.retext) w.currentNode.textContent = w.currentNode.textContent.split(a).join(b);
      }
      continue;
    }
    if (op.clone) {   // copies of a page with its text/keys swapped: [{replace: [[from, to], ...]}, ...], added after it
      let last = els[0];
      for (const c of op.clone) {
        let h = els[0].outerHTML;
        for (const [a, b] of c.replace || []) h = h.split(a).join(b);
        last.insertAdjacentHTML('afterend', h); last = last.nextElementSibling;
      }
      continue;
    }
    if (op.each) { els.forEach((e, i) => op.each[i] !== undefined && (e.innerHTML = op.each[i])); }   // one per match
    if (op.each && !op.attr) continue;
    if (!op.all && !op.each) els = els.slice(0, 1);
    for (const e of els) {
      if (op.remove) { e.remove(); continue; }
      for (const [k, v] of Object.entries(op.attr || {})) e.setAttribute(k, v);
      if (op.unclass) { const re = new RegExp(op.unclass); [...e.classList].filter(c => re.test(c)).forEach(c => e.classList.remove(c)); }
      if (op.text !== undefined) e.textContent = op.text;
      if (op.html !== undefined) {
        if ((op.where || 'beforeend') === 'replace') e.outerHTML = op.html;
        else e.insertAdjacentHTML(op.where || 'beforeend', op.html);
      }
    }
  }
  return out;
}"""

JS_CONTROLS = r"""(cfg) => {
  const root = window.__root, R = root.getBoundingClientRect();
  const vis = e => { const r = e.getBoundingClientRect(); return r.width > 2 && r.height > 2 && e.offsetParent !== null; };
  const rect = e => { const r = e.getBoundingClientRect(); return [r.left - R.left, r.top - R.top, r.width, r.height]; };
  const lits = s => (s || '').match(/'([^']+)'|"([^"]+)"/g) || [];
  const keys = (e, unit) => {
    const c = [];
    for (const el of [e, unit, ...e.querySelectorAll('*')]) {
      if (!el || !el.dataset) continue;
      for (const a of ['key', 'param', 'knob', 'target', 'id']) if (el.dataset[a]) c.push(el.dataset[a]);
      if (el.id) c.push(el.id.replace(/^(knob|enum|val|seg|toggle|slider|btn|sw|sel)[-_]/i, ''));
      for (const a of ['onmousedown', 'onclick', 'oninput', 'onpointerdown'])
        if (el.getAttribute && el.getAttribute(a)) for (const l of lits(el.getAttribute(a))) c.push(l.slice(1, -1));
    }
    return c;
  };
  const txt = (unit, sel) => { if (!unit || !sel) return ''; const l = unit.querySelector(sel); return l ? l.innerText.trim() : ''; };
  const hide = e => e && e.classList.add('stitch-hide');
  const res = {knobs: [], enums: [], toggles: [], sliders: [], steppers: [], popups: [], canvases: [], pictures: [], buttons: []};
  const done = new Set();
  const order = new Map([...root.querySelectorAll('*')].map((e, i) => [e, i]));   // page order, for the Q-Links
  const take = (kind, spec, fn) => {
    for (const e of document.querySelectorAll(spec.item)) {
      if (!vis(e) || done.has(e) || !root.contains(e)) continue;
      done.add(e);
      const unit = spec.unit ? (e.closest(spec.unit) || e.parentElement) : e.parentElement;
      const o = {label: txt(unit, spec.label) || (spec.selfLabel ? e.innerText.trim() : ''), rect: rect(e),
                 keys: keys(e, spec.unit ? unit : null), spec: spec.name || kind, dom: order.get(e) || 0};
      if (spec.key) o.keys.unshift(spec.key);   // a control that names no parameter (e.g. a preset stepper)
      fn && fn(e, o, unit);
      res[kind].push(o);
      hide(e);
      for (const s of (spec.hide || [])) unit && unit.querySelectorAll(s).forEach(hide);
      if (spec.label && unit && !spec.keeplabel) unit.querySelectorAll(spec.label).forEach(hide);
      if (spec.value && unit) unit.querySelectorAll(spec.value).forEach(hide);
    }
  };
  for (const spec of [].concat(cfg.knob || [])) take('knobs', spec, (e, o) => {
    const body = spec.body ? (e.querySelector(spec.body) || e) : e;
    const br = body.getBoundingClientRect();
    o.body = rect(body);
    o.cx = br.left - R.left + br.width / 2; o.cy = br.top - R.top + br.height / 2;
    o.size = Math.max(body.offsetWidth || br.width, body.offsetHeight || br.height);   // unrotated size
    o.idx = window.__knobs.length; window.__knobs.push(e);
  });
  // switches, pop-ups and steppers keep the mockup's own title in the background (static text, its place and font);
  // knobs, sliders and toggles get MPC's live name label instead
  for (const k of ['enum', 'popup', 'stepper']) cfg[k] = [].concat(cfg[k] || []).map(sp => Object.assign({keeplabel: true}, sp));
  for (const spec of [].concat(cfg.enum || [])) take('enums', spec, (e, o) => {
    o.options = [...e.querySelectorAll(spec.opt)].map(x => x.innerText.trim());
    const rs = [...e.querySelectorAll(spec.opt)].map(x => x.getBoundingClientRect());
    o.dir = spec.dir || (rs.length > 1 && Math.abs(rs[1].top - rs[0].top) > Math.abs(rs[1].left - rs[0].left) ? 'v' : 'h');
    o.opt = rs.length ? [rs[0].width, rs[0].height] : [0, 0];
  });
  for (const spec of [].concat(cfg.toggle || [])) take('toggles', spec);
  for (const spec of [].concat(cfg.button || [])) take('buttons', spec);
  for (const spec of [].concat(cfg.slider || [])) take('sliders', spec);
  for (const spec of [].concat(cfg.stepper || [])) take('steppers', spec);
  for (const spec of [].concat(cfg.popup || [])) take('popups', spec);
  for (const sel of Object.keys(cfg.canvas || {})) for (const e of document.querySelectorAll(sel)) {
    if (!vis(e) || !root.contains(e)) continue;
    const box = (cfg.canvas[sel] || {}).fill ? e.closest(cfg.canvas[sel].fill) : null;   // grow into a parent panel
    res.canvases.push({sel, rect: rect(box || e)}); hide(e);
  }
  for (const s of (cfg.hide || [])) document.querySelectorAll(s).forEach(hide);
  // audition / preview buttons can't work on the MPC
  document.querySelectorAll('[class*=audition],[id*=audition],[id*=Audition],[data-purpose=audio-test-trigger]').forEach(hide);
  return res;
}"""

JS_GRID_ON = r"""(a) => {
  const root = window.__root;
  for (const k of root.children) if (!k.hasAttribute('data-stitch-drop') && !(a.keep && k.matches(a.keep))) k.setAttribute('data-stitch-off', '1');
  if (!document.getElementById('stitch-off-css')) {
    const st = document.createElement('style'); st.id = 'stitch-off-css';
    st.textContent = '[data-stitch-off]{display:none !important}'; document.head.appendChild(st);
  }
  if (getComputedStyle(root).position === 'static') root.style.position = 'relative';
  root.insertAdjacentHTML('beforeend', a.html);
  return true;
}"""

JS_GRID_OFF = r"""() => { document.querySelectorAll('.stitch-grid-page').forEach(e => e.remove());
  document.querySelectorAll('[data-stitch-off]').forEach(e => e.removeAttribute('data-stitch-off')); }"""

JS_KNOB_SOLO = r"""(i) => {
  const e = window.__knobs[i];
  document.querySelectorAll('.stitch-solo').forEach(x => x.classList.remove('stitch-solo'));
  if (!document.getElementById('stitch-solo-css')) {
    const st = document.createElement('style'); st.id = 'stitch-solo-css';
    st.textContent = `body.stitch-solo-on *{visibility:hidden !important} body.stitch-solo-on .stitch-solo,
      body.stitch-solo-on .stitch-solo *{visibility:visible !important}
      body.stitch-solo-on, html:has(body.stitch-solo-on){background:transparent !important}`;
    document.head.appendChild(st);
  }
  e.classList.remove('stitch-hide'); e.querySelectorAll('.stitch-hide').forEach(x => x.classList.remove('stitch-hide'));
  e.classList.add('stitch-solo'); document.body.classList.add('stitch-solo-on');
  // the element that turns: the first one (itself included) whose inline transform has a rotate()
  const all = [e, ...e.querySelectorAll('*')];
  let rot = all.find(x => /rotate\(/.test(x.style.transform || ''));
  if (!rot) rot = (window.__rotSel && e.querySelector(window.__rotSel)) || e;
  window.__rot = rot; window.__rotTpl = rot.style.transform || '';
  // a signature of the knob's look (structure + classes + size), so identical knobs share a filmstrip
  const r = {width: e.offsetWidth, height: e.offsetHeight};   // layout size: a turned knob's bounding box grows
  return e.outerHTML.replace(/ (style|data-[a-z-]+|id|onmousedown|onclick)="[^"]*"/g, '').replace(/>[^<]*</g, '><') + ':' +
         [...e.querySelectorAll('*')].map(x => x.className).join('|') + ':' + Math.round(r.width) + 'x' + Math.round(r.height);
}"""

JS_KNOB_ANGLE = r"""(deg) => {
  const t = window.__rotTpl;
  window.__rot.style.transform = /rotate\(/.test(t) ? t.replace(/rotate\([^)]*\)/, 'rotate(' + deg + 'deg)') : (t + ' rotate(' + deg + 'deg)');
}"""

JS_KNOB_DONE = r"""() => { document.body.classList.remove('stitch-solo-on');
  document.querySelectorAll('.stitch-solo').forEach(x => { x.classList.remove('stitch-solo'); x.classList.add('stitch-hide'); });
  window.__rot.style.transform = window.__rotTpl; }"""

with sync_playwright() as pw:
    b = pw.chromium.launch()
    pg = b.new_page(viewport={"width": 1400, "height": 900})
    pg.set_content(open(cfg["file"]).read(), wait_until="networkidle")
    pg.wait_for_timeout(600)
    if cfg.get("augment"):
        for m in pg.evaluate(JS_AUGMENT, cfg["augment"]):
            print(m)
        pg.wait_for_timeout(500)   # Tailwind (CDN) styles the new markup
    pg.evaluate(JS_SETUP, cfg)
    pg.evaluate("() => { window.__knobs = []; window.__rotSel = %s; }" % json.dumps(cfg.get("rot", "")))
    tabs = [t for t in pg.query_selector_all(cfg["tabs"])] or [None]   # no tabs: a one-page design
    result = {"tabs": [], "strips": {}}
    sigs = {}
    def capture(ti, name):
        pg.evaluate("() => document.querySelectorAll('.stitch-hide').forEach(x => x.classList.remove('stitch-hide'))")
        ctl = pg.evaluate(JS_CONTROLS, cfg)
        root = pg.query_selector("#" + pg.evaluate("() => window.__root.id"))
        root.screenshot(path=os.path.join(out, "bg_%d.png" % ti))
        # knob filmstrips, one per distinct look
        for k in ctl["knobs"]:
            sig = pg.evaluate(JS_KNOB_SOLO, k["idx"])
            if sig not in sigs:
                n = len(sigs)
                s = int(round(k["size"])) + 10
                x0, y0 = k["cx"] - s / 2, k["cy"] - s / 2
                rb = pg.evaluate("() => { const r = window.__root.getBoundingClientRect(); return [r.left, r.top]; }")
                frames = []
                for f in range(FRAMES):
                    pg.evaluate(JS_KNOB_ANGLE, -SWEEP + 2 * SWEEP * f / (FRAMES - 1))
                    png = pg.screenshot(clip={"x": rb[0] + x0, "y": rb[1] + y0, "width": s, "height": s}, omit_background=True)
                    frames.append(Image.open(io.BytesIO(png)).convert("RGBA"))
                strip = Image.new("RGBA", (s, s * FRAMES), (0, 0, 0, 0))
                for f, im in enumerate(frames):
                    strip.paste(im, (0, f * s))
                fn = "knob_%d.png" % n
                strip.save(os.path.join(out, fn), optimize=True)
                sigs[sig] = fn
                result["strips"][fn] = {"size": s, "frames": FRAMES}
            k["strip"] = sigs[sig]
            pg.evaluate(JS_KNOB_DONE)
        result["tabs"].append({"name": name, "bg": "bg_%d.png" % ti, **ctl})
        print("tab %d %-16s knobs %d enums %d toggles %d sliders %d steppers %d popups %d canvases %d" % (
            ti, name, len(ctl["knobs"]), len(ctl["enums"]), len(ctl["toggles"]), len(ctl["sliders"]),
            len(ctl["steppers"]), len(ctl["popups"]), len(ctl["canvases"])))

    for ti, tab in enumerate(tabs):
        if tab is None:
            name = cfg.get("page", "MAIN")
        else:
            name = " ".join(tab.evaluate("""e => { const c = e.cloneNode(true);   // icon ligatures ("person") aren't the name
                c.querySelectorAll('[class*=material-symbols], .material-icons').forEach(x => x.remove());
                return c.textContent; }""").split())   # (hidden: innerText would be empty)
            tab.evaluate("e => e.click()")   # the header is hidden, so click it from script
        pg.wait_for_timeout(300)
        capture(ti, name)
    for gi, gp in enumerate(cfg.get("grid_pages", [])):   # pages the design didn't draw, in its style (convert.py grid_page)
        pg.evaluate(JS_GRID_ON, {"html": gp["html"], "keep": cfg.get("grid_keep") or ""})
        pg.wait_for_timeout(700)   # Tailwind styles the new markup
        capture(len(tabs) + gi, gp["name"])
        pg.evaluate(JS_GRID_OFF)
    json.dump(result, open(os.path.join(out, "controls.json"), "w"), indent=1)
    print("strips:", len(sigs))
    b.close()
