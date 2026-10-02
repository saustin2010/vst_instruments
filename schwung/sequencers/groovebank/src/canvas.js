/*
 * canvas.js — Groove Bank Pattern View (Schwung canvas overlay).
 *
 * Groove Bank retriggers the chord you HOLD on the selected rhythm template.
 * The pattern is pure rhythm (one row); the pitches are your live hands.
 *
 * Controls:
 *   - jog turn          -> cycle patterns WITHIN the current genre
 *   - Shift + jog turn  -> switch genre (needs ctx.shiftHeld(); falls back to
 *                          plain pattern-cycle on a host without it)
 *   - Knob 1 turn       -> variant (shown in the title as [+], [++], ...)
 *   - Knob 2 turn       -> strum (bipolar: <0 down-strum, >0 up-strum, 0 tight)
 *   - Knob 3 turn       -> gate length (staccato <-> legato)
 *   - Knob 4 turn       -> accent depth (velocity spread; 0 = flat)
 *   - Knob 5 turn       -> latch on (CW) / off (CCW): held chord sticks
 *   - Knob 7 turn       -> swing (0..100)
 *   - Knob 8 turn       -> switch genre (geared)
 *   - jog click / Back  -> exit
 */

'use strict';

const CC_JOG = 14;
const CC_VARIANT = 71; /* K1 = variant (shown in the title) */
const CC_STRUM = 72;   /* K2 = strum (bipolar) */
const CC_GATE = 73;    /* K3 = gate length (staccato <-> legato) */
const CC_ACCENT = 74;  /* K4 = accent depth */
const CC_LATCH = 75;   /* K5 = latch (CW on / CCW off) */
const CC_SWING = 77;   /* K7 = swing */
const CC_GENRE = 78;   /* K8 = genre */
const SWING_STEP = 5;
const GATE_STEP = 5;
const STRUM_STEP = 5;
const ACCENT_STEP = 5;

/* K1 (variant) and K8 (genre) are geared way down on the endless encoder — one
 * step per 24 detents (~half a physical turn on Move's ~48-detent knobs). The
 * lists are short (a few variants; genres browsed a step at a time), so a
 * deliberate turn beats a twitchy flick that skips past the one you want. */
const GENRE_DETENTS_PER_STEP = 24;
const VARIANT_DETENTS_PER_STEP = 24;
let genreAccum = 0;
let variantAccum = 0;

const g = {
  count: 1, pattern: 0, steps: 16, name: '', genre: '',
  swing: 0, gate: 80, strum: 0, accent: 50, latch: 0,
  variant: 0, vcount: 1,
  row: '',
  genres: [],   /* [{name, start, count}] */
  rev: -1,
};

function gp(ctx, k) { const v = ctx.getParam(k); return v === undefined || v === null ? '' : v; }
function gpi(ctx, k, d) { const v = parseInt(gp(ctx, k), 10); return Number.isFinite(v) ? v : d; }

function parseGenres(str) {
  const arr = [];
  let start = 0;
  const parts = String(str || '').split('|');
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i];
    const idx = p.lastIndexOf(':');
    if (idx < 0) continue;
    const name = p.slice(0, idx);
    const count = parseInt(p.slice(idx + 1), 10) || 0;
    if (count <= 0) continue;
    arr.push({ name: name, start: start, count: count });
    start += count;
  }
  return arr;
}

function genreIndexOf(p) {
  for (let i = 0; i < g.genres.length; i++) {
    const ge = g.genres[i];
    if (p >= ge.start && p < ge.start + ge.count) return i;
  }
  return 0;
}

function load(ctx, force) {
  const rev = gpi(ctx, 'preview_rev', 0);
  if (force || rev !== g.rev) {
    g.rev = rev;
    g.count = Math.max(1, gpi(ctx, 'pattern_count', 1));
    g.pattern = gpi(ctx, 'pattern', 0);
    g.steps = Math.max(1, Math.min(32, gpi(ctx, 'steps', 16)));
    g.name = gp(ctx, 'pattern_name');
    g.genre = gp(ctx, 'pattern_genre');
    g.row = gp(ctx, 'row0');
  }
  if (!g.genres.length) g.genres = parseGenres(gp(ctx, 'genre_list'));
  g.swing = gpi(ctx, 'swing', g.swing);
  g.gate = gpi(ctx, 'gate', g.gate);
  g.strum = gpi(ctx, 'strum', g.strum);
  g.accent = gpi(ctx, 'accent', g.accent);
  g.latch = gpi(ctx, 'latch', g.latch);
  g.variant = gpi(ctx, 'variant', g.variant);
  g.vcount = Math.max(1, gpi(ctx, 'variant_count', 1));
}

function setPattern(ctx, p) {
  if (p === g.pattern) return;
  g.pattern = p;
  ctx.setParam('pattern', String(p));
  load(ctx, true);
}

function cyclePattern(ctx, delta) {
  const ge = g.genres[genreIndexOf(g.pattern)];
  if (!ge) { setPattern(ctx, (((g.pattern + delta) % g.count) + g.count) % g.count); return; }
  const off = ((g.pattern - ge.start + delta) % ge.count + ge.count) % ge.count;
  setPattern(ctx, ge.start + off);
}

function switchGenre(ctx, delta) {
  const n = g.genres.length;
  if (!n) return;
  let gi = (genreIndexOf(g.pattern) + delta) % n;
  if (gi < 0) gi += n;
  setPattern(ctx, g.genres[gi].start);
}

function setSwing(ctx, delta) {
  const s = Math.max(0, Math.min(100, g.swing + delta * SWING_STEP));
  if (s === g.swing) return;
  g.swing = s;
  ctx.setParam('swing', String(s));
}

function setGate(ctx, delta) {
  const v = Math.max(5, Math.min(200, g.gate + delta * GATE_STEP));
  if (v === g.gate) return;
  g.gate = v;
  ctx.setParam('gate', String(v));
}

function setStrum(ctx, delta) {
  const v = Math.max(-100, Math.min(100, g.strum + delta * STRUM_STEP));
  if (v === g.strum) return;
  g.strum = v;
  ctx.setParam('strum', String(v));
}

function setAccent(ctx, delta) {
  const v = Math.max(0, Math.min(100, g.accent + delta * ACCENT_STEP));
  if (v === g.accent) return;
  g.accent = v;
  ctx.setParam('accent', String(v));
}

function setLatch(ctx, on) {
  const v = on ? 1 : 0;
  if (v === g.latch) return;
  g.latch = v;
  ctx.setParam('latch', String(v));
}

function setVariant(ctx, delta) {
  const v = Math.max(0, Math.min(g.vcount - 1, g.variant + delta));   /* one flavor per detent */
  if (v === g.variant) return;
  g.variant = v;
  ctx.setParam('variant', String(v));
  load(ctx, true);   /* reload the selected variant's row for the grid */
}

function draw(ctx) {
  const gi = genreIndexOf(g.pattern);
  const ge = g.genres[gi] || { name: g.genre, start: 0, count: g.count };
  const pos = (g.pattern - ge.start) + 1;

  /* Title: "1 <name>[++]   x/y" — K1 morphs the variant, shown as + marks
   * (base = no marks, variant 1 = [+], 2 = [++], ...). x/y = spot in genre. */
  const plus = g.variant > 0 ? '[' + '+'.repeat(g.variant) + ']' : '';
  const nm = (g.name || '').slice(0, 13 - plus.length);
  ctx.print(0, 0, '1 ' + nm + plus, 1);
  ctx.print(104, 0, pos + '/' + ge.count, 1);

  /* Single rhythm row rendered as a wide step grid. The pitches are your hands,
   * so there's one lane; each cell shows hit / accent / ghost / tie. */
  const steps = g.steps;
  const row = g.row || '';
  const gridX = 4, gridW = 120;
  const cellW = Math.max(2, Math.floor(gridW / steps));
  const baseY = 34;          /* baseline of the row (shrunk to fit 3-row footer) */
  const fullH = 13;          /* normal hit height                            */

  /* beat ticks (every 4 steps) as faint anchors */
  for (let s = 0; s < steps; s += 4) {
    const x = gridX + s * cellW;
    ctx.setPixel(x, baseY + 2, 1);
  }

  for (let s = 0; s < steps && s < row.length; s++) {
    const c = row[s];
    const x = gridX + s * cellW;
    const w = Math.max(1, cellW - 1);
    if (c === '.') continue;
    if (c === '-') {                       /* tie: thin sustain bar at mid    */
      ctx.fillRect(x, baseY - Math.floor(fullH / 2), w, 1, 1);
      continue;
    }
    if (c === 'g') {                       /* ghost: short block              */
      const h = Math.floor(fullH / 2);
      ctx.fillRect(x, baseY - h, w, h, 1);
    } else if (c === 'A') {                 /* accent: full + top cap          */
      ctx.fillRect(x, baseY - fullH, w, fullH, 1);
      ctx.fillRect(x, baseY - fullH - 2, w, 1, 1);
    } else {                                /* x normal hit                    */
      ctx.fillRect(x, baseY - fullH, w, fullH, 1);
    }
  }

  /* Footer: knobs in numeric reading order, 2 columns x 3 rows —
   *   2 Strum   3 Gate
   *   4 Accent  5 Latch
   *   7 Swing   8 <genre>
   * All rows sit above y63 so nothing clips off the 64px panel. */
  const st = (g.strum > 0 ? '+' : '') + g.strum;
  ctx.print(0, 40,  '2 Strum ' + st, 1);
  ctx.print(68, 40, '3 Gate ' + g.gate, 1);
  ctx.print(0, 48,  '4 Accent ' + g.accent, 1);
  ctx.print(68, 48, '5 ' + (g.latch ? 'LATCH' : 'latch'), 1);   /* caps = on */
  ctx.print(0, 56,  '7 Swing ' + g.swing, 1);
  ctx.print(68, 56, '8 ' + (ge.name || '').slice(0, 7), 1);
}

globalThis.canvas_overlay = {
  onOpen(ctx) { g.genres = []; genreAccum = 0; variantAccum = 0; load(ctx, true); },
  tick(ctx) { load(ctx, false); },
  draw(ctx) { draw(ctx); return true; },
  onMidi(ctx, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 3) return;
    const type = d[0] & 0xF0, b1 = d[1], b2 = d[2];
    if (type !== 0xB0 || b2 === 0) return;   /* encoders: 1..63 = +, 64..127 = - */
    const dir = b2 < 64 ? 1 : -1;
    if (b1 === CC_JOG) {   /* shift+jog = genre, plain jog = pattern (shift falls back if host lacks it) */
      const shifted = typeof ctx.shiftHeld === 'function' && ctx.shiftHeld();
      (shifted ? switchGenre : cyclePattern)(ctx, dir);
      return;
    }
    if (b1 === CC_STRUM)  { setStrum(ctx, dir); return; }
    if (b1 === CC_GATE)   { setGate(ctx, dir); return; }
    if (b1 === CC_ACCENT) { setAccent(ctx, dir); return; }
    if (b1 === CC_LATCH)  { setLatch(ctx, dir > 0); return; }   /* CW on, CCW off */
    if (b1 === CC_SWING)  { setSwing(ctx, dir); return; }
    if (b1 === CC_VARIANT) {                        /* geared, like genre */
      if (dir * variantAccum < 0) variantAccum = 0;
      variantAccum += dir;
      while (variantAccum >= VARIANT_DETENTS_PER_STEP)  { setVariant(ctx,  1); variantAccum -= VARIANT_DETENTS_PER_STEP; }
      while (variantAccum <= -VARIANT_DETENTS_PER_STEP) { setVariant(ctx, -1); variantAccum += VARIANT_DETENTS_PER_STEP; }
      return;
    }
    if (b1 === CC_GENRE) {
      if (dir * genreAccum < 0) genreAccum = 0;   /* reversing drops stale travel */
      genreAccum += dir;
      while (genreAccum >= GENRE_DETENTS_PER_STEP)  { switchGenre(ctx,  1); genreAccum -= GENRE_DETENTS_PER_STEP; }
      while (genreAccum <= -GENRE_DETENTS_PER_STEP) { switchGenre(ctx, -1); genreAccum += GENRE_DETENTS_PER_STEP; }
      return;
    }
  },
  onClose() {},
  onExit() {},
};
