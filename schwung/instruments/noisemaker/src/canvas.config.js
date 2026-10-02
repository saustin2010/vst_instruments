/* Noisemaker canvas config for schwung-canvaskit (../schwung-canvaskit).
 * SOURCE for src/canvas.js — regenerate after editing:
 *   node ../schwung-canvaskit/build.mjs src/canvas.config.js src/canvas.js
 *
 * Grouping mirrors the TAL NoiseMaker desktop panels (LFO1/LFO2/OSC1/OSC2/
 * MASTER, FILTER + AMP with their ADSR sliders, and the Chorus/Crush/Reverb/
 * Delay effect strip). Uses the kit's visual models where the synth does:
 * wavesel (osc + LFO waveform shape boxes), filterViz (filter-response curve).
 *
 * Wire contract (src/dsp/noisemaker_plugin.cpp): all params are native ints —
 * continuous 0..100 (K_PCT / K_BIPOLAR centered at 50), enums exchange the
 * OPTION INDEX both ways. No per-cell codecs needed. Two params ride wider
 * wires and so build their cells by hand: tune2 (0..255) and delay_fb
 * (0..200 = loop gain x100, K_FBGAIN). */

KIT_PARAM_MAX = 100;

/* enum labels — order MUST match the wrapper's PARAMS[] option order. */
const FILT     = ["LP24","LP18","LP12","LP6","HP24","BP24","Notch","SV-LP","SV-HP","SV-BP","Moog","Moog2"];
const FILT_SQ  = ["L24","L18","L12","L6","H24","B24","Ntc","SVL","SVH","SVB","Mog","Mg2"];
/* filterViz curve shape per filter type. */
const FILT_MODES = ["lp","lp","lp","lp","hp","bp","notch","lp","hp","bp","lp","lp"];
const LDST1    = ["None","Filter","Osc1","Osc2","PW","FM","LFO2","Osc1+2"];
const LDST1_SQ = ["Non","Flt","Os1","Os2","PW","FM","LF2","O12"];
/* NOT a copy of LDST1: the engine's setLfo2Destination puts PAN and VOLUME
 * where LFO1 has PW and FM (see the LDST2_OPTS note in the wrapper). */
const LDST2    = ["None","Filter","Osc1","Osc2","Pan","Volume","LFO1","Osc1+2"];
const LDST2_SQ = ["Non","Flt","Os1","Os2","Pan","Vol","LF1","O12"];
const FDST     = ["Off","Filter","Osc1","Osc2","PW","FM"];
const FDST_SQ  = ["Off","Flt","Os1","Os2","PW","FM"];
const PMODE    = ["Off","Auto","On"];
const OSC1_WAVES = ["Saw","Pulse","Noise"];
const OSC1_SHAPES = ["saw","square","swishy"];
const OSC2_WAVES = ["Saw","Pulse","Tri","Sine","Noise"];
const OSC2_SHAPES = ["saw","square","tri","sine","swishy"];
const LFO_WAVES  = ["Sin","Tri","Saw","Sqr","S+H","Rnd"];
const LFO_SHAPES = ["sine","tri","saw","square","sh","swishy"];
/* Envelope Editor (spline mod source) — order MUST match PARAMS[] option order. */
const ENVDST    = ["Off","Filter","Osc1","Osc2","Osc1+2","FM","RingMod","Volume"];
const ENVDST_SQ = ["Off","Flt","Os1","Os2","O12","FM","Rng","Vol"];
const ENVSPD    = ["x1","x2","x4","x8","x16","x32"];

/* ---- Macro value HUDs ---------------------------------------------------
 * Transient popup cards, the Echidna pattern: they surface only while their
 * own knob is held (s.lastKnob), sitting on top of the grid, and vanish on
 * release. The three macro knobs all send raw numbers that mean nothing on
 * their own — "68" is not a waveform, a semitone count or a time ratio — so
 * each card interprets its value. */

/* Icon per anchor, keyed by name. Hand-authored: which waveform to draw is a
 * canvas-side judgement the wrapper knows nothing about. Keys must match the
 * names in NM_WAVE_STOPS. */
const WAVE_GLYPHS = {
  "Sine": ["sine"], "FM": ["fm"], "Triangle": ["tri"], "Saw": ["saw"],
  "Dual Saw": ["saw", "saw"], "Square": ["square"], "Thin Pls": ["pulse"],
  "Pulse+Saw": ["pulse", "saw"], "Ring": ["ring"], "Sub Bass": ["saw", "sub"],
};

/* GENERATED from PARAMS[] by tools/gen_param_labels.mjs — DO NOT EDIT.
 * The full parameter names the host menus use. Fed to CONFIG.paramNames so the
 * canvas header shows "Cutoff" while the cell label stays "Cut".
 *
 * The kit can also resolve these at runtime from chain_params, but that read is
 * deferred to the first knob touch and cached for the session -- and
 * shadow_get_param returns null while the param bus is busy, which is exactly
 * what a knob touch makes it. One unlucky read and every header falls back to
 * the abbreviation until the module is reloaded, silently. Baking the map in
 * takes that failure mode out of the path. */
/* BEGIN GENERATED PARAM_NAMES */
const PARAM_NAMES = {
  "wave": "Wave",
  "tune2": "Osc2 Pitch",
  "fenv_time": "Filter Time",
  "aenv_time": "Amp Time",
  "volume": "Volume",
  "highpass": "High Pass",
  "osc1_wave": "Osc1 Wave",
  "osc2_wave": "Osc2 Wave",
  "osc1_vol": "Osc1 Level",
  "osc2_vol": "Osc2 Level",
  "osc3_vol": "Sub Level",
  "osc_tune": "Master Tune",
  "osc1_tune": "Osc1 Tune",
  "osc2_tune": "Osc2 Tune",
  "osc1_fine": "Osc1 Fine",
  "osc2_fine": "Osc2 Fine",
  "osc1_pw": "Osc1 PW",
  "osc1_phase": "Osc1 Phase",
  "osc2_phase": "Osc2 Phase",
  "osc2_fm": "Osc2 FM",
  "osc_sync": "Osc Sync",
  "ringmod": "Ring Mod",
  "detune": "Detune",
  "bitcrush": "Bitcrusher",
  "vintage": "Vintage",
  "filter_type": "Filter Type",
  "cutoff": "Cutoff",
  "resonance": "Resonance",
  "keyfollow": "Key Follow",
  "filter_env": "Filter Env",
  "filter_drive": "Filter Drive",
  "fenv_a": "Filter Attack",
  "fenv_d": "Filter Decay",
  "fenv_s": "Filter Sustain",
  "fenv_r": "Filter Release",
  "aenv_a": "Amp Attack",
  "aenv_d": "Amp Decay",
  "aenv_s": "Amp Sustain",
  "aenv_r": "Amp Release",
  "lfo1_wave": "LFO1 Wave",
  "lfo1_rate": "LFO1 Rate",
  "lfo1_amount": "LFO1 Amount",
  "lfo1_dest": "LFO1 Dest",
  "lfo1_sync": "LFO1 Sync",
  "lfo1_keytrig": "LFO1 KeyTrig",
  "lfo1_phase": "LFO1 Phase",
  "lfo2_wave": "LFO2 Wave",
  "lfo2_rate": "LFO2 Rate",
  "lfo2_amount": "LFO2 Amount",
  "lfo2_dest": "LFO2 Dest",
  "lfo2_sync": "LFO2 Sync",
  "lfo2_keytrig": "LFO2 KeyTrig",
  "lfo2_phase": "LFO2 Phase",
  "free_a": "Env3 Attack",
  "free_d": "Env3 Decay",
  "free_amt": "Env3 Amount",
  "free_dest": "Env3 Dest",
  "vel_vol": "Vel > Vol",
  "vel_env": "Vel > Env",
  "vel_cut": "Vel > Cutoff",
  "pw_cutoff": "Wheel > Cutoff",
  "pw_pitch": "Bend Range",
  "portamento": "Portamento",
  "porta_mode": "Porta Mode",
  "voices": "Voices",
  "chorus1": "Chorus I",
  "chorus2": "Chorus II",
  "reverb_wet": "Reverb Wet",
  "reverb_decay": "Reverb Decay",
  "reverb_pre": "Reverb PreDly",
  "reverb_hi": "Reverb HiCut",
  "reverb_lo": "Reverb LoCut",
  "delay_wet": "Delay Wet",
  "delay_time": "Delay Time",
  "delay_sync": "Delay Sync",
  "delay_fac_l": "Delay 2x L",
  "delay_fac_r": "Delay 2x R",
  "delay_fb": "Delay Feedbk",
  "delay_hi": "Delay HiCut",
  "delay_lo": "Delay LoCut",
  "env_amt": "Env Draw Amt",
  "env_speed": "Env Draw Speed",
  "env_dest": "Env Draw Dest",
};
/* END GENERATED PARAM_NAMES */

/* GENERATED from NM_WAVE_STOPS by tools/gen_wave_anchors.mjs — DO NOT EDIT.
 * Position and name are the wrapper's to define; duplicating them by hand here
 * meant the HUD would silently lie the moment an anchor was re-ordered. */
/* BEGIN GENERATED WAVE_ANCHORS */
const WAVE_ANCHOR_POS = [
  { at: 1, name: "Sine" },
  { at: 12, name: "FM" },
  { at: 23, name: "Triangle" },
  { at: 34, name: "Saw" },
  { at: 45, name: "Dual Saw" },
  { at: 56, name: "Square" },
  { at: 67, name: "Thin Pls" },
  { at: 78, name: "Pulse+Saw" },
  { at: 89, name: "Ring" },
  { at: 100, name: "Sub Bass" },
];
/* END GENERATED WAVE_ANCHORS */

const WAVE_ANCHORS = WAVE_ANCHOR_POS.map(function (a) {
  return { at: a.at, name: a.name, glyph: WAVE_GLYPHS[a.name] || ["saw"] };
});

/* Shapes the kit's shapeSample doesn't cover. `fm` draws a phase-modulated
 * sine (the FM zone's actual mechanism), `ring` a carrier times a higher
 * modulator, `pulse` a narrow-duty pulse, `sub` a square drawn at half the
 * cycle count so it reads an octave down. */
function waveSample(kind, t) {
  const ph = t - Math.floor(t);
  if (kind === "pulse") return ph < 0.18 ? 1 : -1;
  if (kind === "fm")    return Math.sin(2 * Math.PI * t + 2.6 * Math.sin(2 * Math.PI * t * 2));
  if (kind === "ring")  return Math.sin(2 * Math.PI * t) * Math.sin(2 * Math.PI * t * 5);
  if (kind === "sub")   return ph < 0.5 ? 1 : -1;
  return shapeSample(kind, t);
}

function waveTrace(ctx, x, y, w, h, kind) {
  const baseY = y + Math.round(h / 2);
  const amp = Math.max(2, Math.round(h / 2) - 1);
  const cyc = kind === "sub" ? 1 : 2;          // sub = one cycle where others show two
  let px = x, py = Math.round(baseY - waveSample(kind, 0) * amp);
  for (let i = 1; i <= w; i++) {
    const yy = Math.round(baseY - waveSample(kind, (i / w) * cyc) * amp);
    plotLine(ctx, px, py, x + i, yy, 1);
    px = x + i; py = yy;
  }
}

/* One anchor's icon, centred on cx: a single trace, or two stacked half-height
 * traces for the combo anchors (dual saw, pulse+saw, saw+sub). */
function waveGlyph(ctx, cx, cy, w, h, glyph) {
  const x = Math.round(cx - w / 2);
  if (glyph.length === 1) return waveTrace(ctx, x, Math.round(cy - h / 2), w, h, glyph[0]);
  const hh = Math.floor((h - 2) / 2);
  waveTrace(ctx, x, Math.round(cy - h / 2),          w, hh, glyph[0]);
  waveTrace(ctx, x, Math.round(cy - h / 2) + hh + 2, w, hh, glyph[1]);
}

/* tune2 rides a native 8-bit 0..255 wire rather than the config-wide 0..100,
 * because the fine-detune windows need the resolution (see nm_tune2_semis in
 * the wrapper). uni() would hardcode KIT_PARAM_MAX, so build the cell by hand
 * — the kit reads cell.min/cell.max everywhere, so a wider cell is fine. */
function tune2Cell() {
  return { key: "tune2", label: "Tun2", kind: "unipolar", min: 0, max: 255, step: 1, sens: KIT_SENS };
}

/* delay_fb rides a 0..200 wire, not the config-wide 0..100: the wrapper's
 * K_FBGAIN exposes the delay's LOOP GAIN x100 rather than TAL's raw knob
 * position, so 0..99 is guaranteed to decay and 100..200 reaches the blooming
 * / self-oscillating region TAL's factory FX patches use. See the K_FBGAIN
 * block in src/dsp/noisemaker_plugin.cpp for the whole story.
 * Hand-built for the same reason as tune2Cell: uni() hardcodes KIT_PARAM_MAX. */
function delayFbCell() {
  return { key: "delay_fb", label: "Fbk", kind: "unipolar", min: 0, max: 200, step: 1, sens: KIT_SENS };
}

/* Mirror of nm_tune2_semis() in src/dsp/noisemaker_plugin.cpp — MUST match. */
function tune2Semis(raw) {
  const CENTERS = [0, 128, 255], SEMIS = [0, 12, 24], DSTEPS = 8, DMAX = 0.2;
  for (let i = 0; i < 3; i++) {
    const steps = raw - CENTERS[i];
    if (Math.abs(steps) <= DSTEPS) return SEMIS[i] + (steps / DSTEPS) * DMAX;
  }
  return Math.round((raw * 24) / 255);
}

/* Downward caret aimed at a position on the scale below it. */
function hudCaret(ctx, cx, baseY) {
  ctx.fillRect(cx - 2, baseY,     5, 1, 1);
  ctx.fillRect(cx - 1, baseY + 1, 3, 1, 1);
  ctx.setPixel(cx, baseY + 2, 1);
}

/* WAVE: the oscillator configuration you are on, drawn as its waveform —
 * and, since the travel between two anchors is a level crossfade, the two
 * bracketing icons with a track and caret showing how far across you are.
 * Sits on a wider card than hudCard's: the icons are the whole point and
 * they need the room (the same reason Echidna's wave card is near-fullscreen). */
function waveHud(ctx, cells, s) {
  const cell = s.lastKnob >= 0 ? cells[s.lastKnob] : null;
  if (!cell || cell.key !== "wave") return;
  const raw = getRaw(ctx, cell);

  const x = 2, y = 10, w = ctx.width - 4, h = 51;
  ctx.fillRect(x, y, w, h, 0);                   // card sits ON TOP of the grid
  ctx.drawRect(x, y, w, h, 1);
  ctx.print(x + 3, y + 2, "WAVE", 1);
  const vtxt = raw <= 0 ? "OFF" : String(raw);
  ctx.print(x + w - 3 - ctx.measureText(vtxt), y + 2, vtxt, 1);
  ctx.fillRect(x + 1, y + 9, w - 2, 1, 1);

  if (raw <= 0) {
    const t = "PRESET OSC SETUP";
    ctx.print(x + Math.round((w - ctx.measureText(t)) / 2), y + 24, t, 1);
    return;
  }

  let i = 0;
  while (i < WAVE_ANCHORS.length - 2 && raw > WAVE_ANCHORS[i + 1].at) i++;
  const a = WAVE_ANCHORS[i], b = WAVE_ANCHORS[i + 1];
  const span = b.at - a.at;
  const f = span > 0 ? (raw - a.at) / span : 0;

  const GW = 34, GH = 20;                        // icon box
  const cy = y + 26;                             // icon row centre
  const nameY = y + 40;                          // caption row

  /* Sitting on an anchor: one icon, centred, named. */
  if (f < 0.02 || f > 0.98) {
    const an = f < 0.02 ? a : b;
    waveGlyph(ctx, x + Math.round(w / 2), cy, GW + 14, GH, an.glyph);
    ctx.print(x + Math.round((w - ctx.measureText(an.name)) / 2), nameY, an.name, 1);
    return;
  }

  /* Between: both icons, track + caret in the gap, each name under its icon. */
  const lcx = x + 4 + GW / 2, rcx = x + w - 4 - GW / 2;
  waveGlyph(ctx, lcx, cy, GW, GH, a.glyph);
  waveGlyph(ctx, rcx, cy, GW, GH, b.glyph);

  const t0 = Math.round(lcx + GW / 2 + 5), t1 = Math.round(rcx - GW / 2 - 5);
  plotLine(ctx, t0, cy, t1, cy, 1);
  ctx.fillRect(t0, cy - 2, 1, 5, 1);             // bounded span, not a stray line
  ctx.fillRect(t1, cy - 2, 1, 5, 1);
  hudCaret(ctx, Math.round(t0 + f * (t1 - t0)), cy - 7);

  /* Captions get the full half-width: "PULSE+SAW" is 9 chars and silently
   * clipped to "PULSE+SA" at anything tighter. */
  const capW = Math.floor(w / 2) - 5;
  ctx.print(x + 3, nameY, fitText(ctx, a.name, capW), 1);
  const bn = fitText(ctx, b.name, capW);
  ctx.print(x + w - 3 - ctx.measureText(bn), nameY, bn, 1);
}

/* OSC2 PITCH: a bare 0..255 is meaningless for pitch, so interpret it — the
 * interval by name, plus the detune in cents whenever the knob sits inside one
 * of the fine windows around unison / +1 oct / +2 oct. */
function tune2Hud(ctx, cells, s) {
  const cell = s.lastKnob >= 0 ? cells[s.lastKnob] : null;
  if (!cell || cell.key !== "tune2") return;
  const raw = getRaw(ctx, cell);
  const semi = tune2Semis(raw);
  const st = Math.round(semi);
  const cents = Math.round((semi - st) * 100);   // 0 outside the windows
  const name = st === 0 ? "UNISON" : st === 12 ? "+1 OCT"
             : st === 24 ? "+2 OCT" : "+" + st + " ST";
  const body = hudCard(ctx, "OSC2 PITCH", String(raw));
  ctx.print(body.x + 2, body.y, name, 1);
  if (cents) {
    const d = (cents > 0 ? "+" : "") + cents + "C";
    ctx.print(body.x + body.w - 2 - ctx.measureText(d), body.y, d, 1);
  }

  const sx = body.x + 4, sw = body.w - 8, sy = body.y + body.h - 9;
  ctx.fillRect(sx, sy, sw, 1, 1);
  for (let i = 0; i <= 24; i++) {                // one tick per semitone
    const oct = i % 12 === 0;
    ctx.fillRect(Math.round(sx + (sw * i) / 24), sy - (oct ? 6 : 2), 1, oct ? 6 : 2, 1);
  }
  ctx.print(sx - 1, sy + 2, "0", 1);
  ctx.print(sx + Math.round(sw / 2) - 1, sy + 2, "1", 1);
  ctx.print(sx + sw - 3, sy + 2, "2", 1);
  /* Caret tracks the CONTINUOUS semitone value, so it visibly creeps off an
   * octave tick while you are inside a fine window. */
  hudCaret(ctx, Math.round(sx + (sw * Math.max(0, Math.min(24, semi))) / 24), sy - 9);
}

/* FEG / AEG: the envelope-time macros. These are NOT multipliers -- the
 * wrapper drags the A/D/R knob positions instead (nm_env_time_shift), so the
 * readout is a signed shift, not a ratio. See the body comment. */
function envTimeHud(ctx, cells, s) {
  const cell = s.lastKnob >= 0 ? cells[s.lastKnob] : null;
  if (!cell || (cell.key !== "fenv_time" && cell.key !== "aenv_time")) return;
  const raw = getRaw(ctx, cell);                 // 0..100, 50 = neutral
  const body = hudCard(ctx, cell.key === "fenv_time" ? "FILTER TIME" : "AMP TIME", String(raw));

  /* Shows the SHIFT, not a ratio: the macro drags the A/D/R sliders together
   * (see nm_env_time_shift in the wrapper), so a multiplier readout would be
   * a lie -- and would read "x6" on a gate envelope that cannot move at all.
   * ATTACK is the exception: it holds a time-independent floor
   * (nm_env_time_shift_attack), so a zero attack stays instant at any setting
   * and the shift shown here applies fully only to D and R. */
  const d = Math.round((raw - 50) * 2);
  ctx.print(body.x + 2, body.y, d === 0 ? "NEUTRAL" : (d > 0 ? "+" : "") + d, 1);
  const tag = raw === 50 ? "" : (raw < 50 ? "SHORTER" : "LONGER");
  if (tag) ctx.print(body.x + body.w - 2 - ctx.measureText(tag), body.y, tag, 1);

  /* Centre-out bar: fills from the detent toward whichever end you are on.
   * The travel is LINEAR in the shift -- d = (raw-50)*2, applied to the knob
   * travel remaining -- so the bar length is the shift amount, not a ratio. */
  const sx = body.x + 3, sw = body.w - 6, sy = body.y + body.h - 6, mid = sx + Math.round(sw / 2);
  ctx.drawRect(sx, sy, sw, 5, 1);
  ctx.fillRect(mid, sy - 2, 1, 2, 1);            // centre detent mark
  const px = Math.round(sx + (sw - 1) * (raw / 100));
  if (px >= mid) ctx.fillRect(mid, sy + 1, Math.max(1, px - mid), 3, 1);
  else           ctx.fillRect(px,  sy + 1, Math.max(1, mid - px), 3, 1);
}

/* DELAY FBK: the knob is already the loop gain (K_FBGAIN in the wrapper), but
 * a gain only means something once you know how long it rings. Below 100 the
 * delay line is contractive on its own, so the tail is guaranteed to end and
 * we can bound the repeat count. At 100 and above it is not, and what happens
 * next is entirely down to the high cut — TalEq sits INSIDE the feedback loop
 * and its insertion loss is what actually kills the tail up there.
 *
 * Measured (off-device, cuts wide open): the tail keeps decaying past a
 * coefficient of 1.0 and only crosses into true sustain around 1.03; engaging
 * the high cut pushes that well past 1.4. So the readout deliberately does NOT
 * promise "infinite" at 100 — it names the high cut as the deciding control,
 * which is both true and the thing you would reach for. */
function delayFbHud(ctx, cells, s) {
  const cell = s.lastKnob >= 0 ? cells[s.lastKnob] : null;
  if (!cell || cell.key !== "delay_fb") return;
  const raw = getRaw(ctx, cell);                 // 0..200 = loop gain x100
  const g = raw / 100;
  const body = hudCard(ctx, "DELAY FBK", String(raw));

  ctx.print(body.x + 2, body.y, "X" + g.toFixed(2), 1);
  /* -60 dB is the usual "gone" threshold. This is an UPPER BOUND: it counts
   * the delay line's own loss only, so the cuts can only make it shorter. */
  let tag;
  if (g <= 0)     tag = "SILENT";
  else if (g >= 1) tag = "HI CUT ONLY";
  else {
    /* "RPTS", not "REPEATS": at three digits the long form runs into the
     * gain on the left (the previewer's 5x5 font is wider than the device's,
     * so fitting there is the conservative check). */
    const n = Math.round(60 / (-20 * Math.log(g) / Math.LN10));
    tag = "MAX " + (n > 999 ? "999" : String(n)) + " RPTS";
  }
  ctx.print(body.x + body.w - 2 - ctx.measureText(tag), body.y, tag, 1);

  const sx = body.x + 3, sw = body.w - 6, sy = body.y + body.h - 6;
  ctx.drawRect(sx, sy, sw, 5, 1);
  const unity = Math.round(sx + (sw - 1) * 0.5);
  ctx.fillRect(unity, sy - 3, 1, 3, 1);          // where the line stops decaying alone
  const px = Math.round(sx + (sw - 1) * (raw / 200));
  ctx.fillRect(sx + 1, sy + 1, Math.max(1, px - sx), 3, 1);
}

const CONFIG = {
  name: "Noisemaker",
  overlays: [waveHud, tune2Hud, envTimeHud, delayFbHud],

  banks: [
    /* ---- MACROS (first bank; the page you land on) ----
     * wave / fenv_time / aenv_time are the wrapper's macro params (see the
     * NM_M_* block in noisemaker_plugin.cpp); the rest are aliases of ordinary
     * params that also live on their own pages, exactly as Echidna's Macros
     * bank re-exposes the Typhon panel knobs.
     *   Wave — 0 is an OFF detent (the preset's own osc setup stands), then 10
     *          anchor configurations with a level crossfade between each pair.
     *   Tun2 — osc2 pitch in semitones, UPWARD only 0..24 (Echidna's tune2
     *          convention: 0 unison, 12 = +1 oct, 24 = +2). Adds to whatever
     *          interval the Wave macro is applying.
     *   FEG/AEG — envelope time scale, 50 = x1, x0.1 .. x6. Shown as bipolar
     *          (the kit has no ratio cell): left = shorter, right = longer. */
    { label: "Macros", knobs: [
        uni("wave", "Wave"), tune2Cell(),
        uni("cutoff", "Cut"), uni("resonance", "Res"),
        uni("filter_env", "Cont"), bip("fenv_time", "FEG"), bip("aenv_time", "AEG"),
        uni("volume", "Vol")] },

    /* ---- OSC 1 ---- */
    { label: "Osc 1", knobs: [
        wavesel("osc1_wave", "Wave", OSC1_WAVES, OSC1_SHAPES),
        bip("osc1_tune", "Tune"), bip("osc1_fine", "Fine"),
        uni("osc1_phase", "Phse"), uni("osc1_pw", "PW"), uni("osc1_vol", "Lvl")] },

    /* ---- OSC 2 ---- */
    { label: "Osc 2", knobs: [
        wavesel("osc2_wave", "Wave", OSC2_WAVES, OSC2_SHAPES),
        bip("osc2_tune", "Tune"), bip("osc2_fine", "Fine"),
        uni("osc2_phase", "Phse"), uni("osc2_fm", "FM"), uni("osc2_vol", "Lvl")] },

    /* ---- MASTER ---- */
    { label: "Master", knobs: [
        uni("osc3_vol", "Sub"), uni("volume", "Vol"), uni("highpass", "HP"),
        uni("vintage", "Nois"), uni("detune", "Detn"), uni("ringmod", "Ring"),
        tog("osc_sync", "Sync"), bip("osc_tune", "MTun")] },

    /* ---- FILTER (+ response curve) ---- */
    { label: "Filter", knobs: [
        enumc("filter_type", "Type", FILT, FILT_SQ),
        uni("cutoff", "Cut"), uni("resonance", "Res"), uni("keyfollow", "Key"),
        uni("filter_env", "Cont"), uni("filter_drive", "Driv"), blank(), blank()],
      filterViz: { cell: 6, cutoffKey: "cutoff", resoKey: "resonance",
                   mode: { key: "filter_type", modes: FILT_MODES } } },

    /* ---- Filter Env (+ its time macro) ---- */
    { label: "Filter Env", env: true, knobs: [
        fader("fenv_a", "A"), fader("fenv_d", "D"), fader("fenv_s", "S"), fader("fenv_r", "R"),
        bip("fenv_time", "Time"), blank(), blank(), blank()] },

    /* ---- Amp Env (+ its time macro). Env3/Free moved to its own bank to
     * make room: the time macro belongs next to the sliders it moves. ---- */
    { label: "Amp Env", env: true, knobs: [
        fader("aenv_a", "A"), fader("aenv_d", "D"), fader("aenv_s", "S"), fader("aenv_r", "R"),
        bip("aenv_time", "Time"), blank(), blank(), blank()] },

    /* ---- Env 3 (free AD envelope) ---- */
    { label: "Env 3 (Free)", knobs: [
        uni("free_a", "Atk"), uni("free_d", "Dec"), bip("free_amt", "Amt"),
        enumc("free_dest", "Dest", FDST, FDST_SQ),
        blank(), blank(), blank(), blank()] },

    /* ---- LFO 1 ---- */
    { label: "LFO 1", knobs: [
        wavesel("lfo1_wave", "Wave", LFO_WAVES, LFO_SHAPES),
        uni("lfo1_rate", "Rate"), uni("lfo1_amount", "Amt"), uni("lfo1_phase", "Phse"),
        enumc("lfo1_dest", "Dest", LDST1, LDST1_SQ),
        tog("lfo1_sync", "Sync"), tog("lfo1_keytrig", "Trig")] },

    /* ---- LFO 2 ---- */
    { label: "LFO 2", knobs: [
        wavesel("lfo2_wave", "Wave", LFO_WAVES, LFO_SHAPES),
        uni("lfo2_rate", "Rate"), uni("lfo2_amount", "Amt"), uni("lfo2_phase", "Phse"),
        enumc("lfo2_dest", "Dest", LDST2, LDST2_SQ),
        tog("lfo2_sync", "Sync"), tog("lfo2_keytrig", "Trig")] },

    /* ---- Voicing / Velocity / Wheel ---- */
    { label: "Voice / Vel", knobs: [
        count("voices", "Vcs", 1, 6), uni("portamento", "Port"),
        enumc("porta_mode", "PMde", PMODE),
        uni("vel_vol", "VVol"), uni("vel_env", "VEnv"), uni("vel_cut", "VCut"),
        uni("pw_cutoff", "WCut"), uni("pw_pitch", "Bend")] },

    /* ---- Chorus / Crush / Reverb ---- */
    { label: "Chorus / Reverb", knobs: [
        tog("chorus1", "Ch1"), tog("chorus2", "Ch2"), uni("bitcrush", "Crsh"),
        uni("reverb_wet", "RWet"), uni("reverb_decay", "RSiz"), uni("reverb_pre", "RPre"),
        uni("reverb_hi", "RHi"), uni("reverb_lo", "RLo")] },

    /* ---- Delay ---- */
    { label: "Delay", knobs: [
        uni("delay_wet", "Wet"), uni("delay_time", "Time"), delayFbCell(),
        tog("delay_sync", "Sync"), tog("delay_fac_l", "2xL"), tog("delay_fac_r", "2xR"),
        uni("delay_hi", "HiCt"), uni("delay_lo", "LoCt")] },

    /* ---- Env Draw (spline mod source; shape fixed per preset) ---- */
    { label: "Env Draw", knobs: [
        enumc("env_dest", "Dest", ENVDST, ENVDST_SQ),
        uni("env_amt", "Amt"), enumc("env_speed", "Spd", ENVSPD),
        blank(), blank(), blank(), blank(), blank()] },
  ],

  sections: [
    { name: "MACROS", bank: 0 },
    { name: "OSC 1", bank: 1 }, { name: "OSC 2", bank: 2 }, { name: "MASTER", bank: 3 },
    { name: "FILTER", bank: 4 }, { name: "FILT ENV", bank: 5 }, { name: "AMP ENV", bank: 6 },
    { name: "ENV 3", bank: 7 },
    { name: "LFO 1", bank: 8 }, { name: "LFO 2", bank: 9 }, { name: "VOICE", bank: 10 },
    { name: "REVERB", bank: 11 }, { name: "DELAY", bank: 12 }, { name: "ENV DRAW", bank: 13 },
  ],

  icons: ["global","sawpulse","sawpulse","pulse","lp","envf","enva","env","sine","sine","bend","pan","routes","envf"],

  /* Off-device defaults only (previewer/tests); on device every read is live. */
  defaults: {
    wave: 0, tune2: 0, fenv_time: 50, aenv_time: 50,
    osc1_wave: 0, osc1_tune: 50, osc1_fine: 50, osc1_phase: 0, osc1_pw: 50, osc1_vol: 80,
    osc2_wave: 0, osc2_tune: 50, osc2_fine: 50, osc2_phase: 0, osc2_fm: 0, osc2_vol: 0,
    osc3_vol: 0, volume: 50, highpass: 0, vintage: 0, detune: 20, ringmod: 0, osc_sync: 0, osc_tune: 50,
    filter_type: 0, cutoff: 60, resonance: 12, keyfollow: 30, filter_env: 35, filter_drive: 0,
    fenv_a: 0, fenv_d: 45, fenv_s: 30, fenv_r: 30,
    aenv_a: 0, aenv_d: 50, aenv_s: 85, aenv_r: 28, free_a: 0, free_d: 0, free_amt: 50, free_dest: 0,
    lfo1_wave: 0, lfo1_rate: 30, lfo1_amount: 0, lfo1_phase: 0, lfo1_dest: 0, lfo1_sync: 0, lfo1_keytrig: 0,
    lfo2_wave: 0, lfo2_rate: 30, lfo2_amount: 0, lfo2_phase: 0, lfo2_dest: 0, lfo2_sync: 0, lfo2_keytrig: 0,
    voices: 6, portamento: 0, porta_mode: 0, vel_vol: 0, vel_env: 0, vel_cut: 0, pw_cutoff: 0, pw_pitch: 20,
    /* bitcrush is INVERTED: 100 == OFF, 0 == 1-bit destruction. Not a typo. */
    chorus1: 0, chorus2: 0, bitcrush: 100, reverb_wet: 0, reverb_decay: 0, reverb_pre: 0, reverb_hi: 100, reverb_lo: 0,
    delay_wet: 0, delay_time: 30, delay_fb: 40, delay_sync: 0, delay_fac_l: 0, delay_fac_r: 0, delay_hi: 100, delay_lo: 0,
    env_dest: 0, env_amt: 0, env_speed: 0,
  },

  paramNames: PARAM_NAMES,

  testExports: { FILT, LDST1, LDST2, FDST, PMODE },
};
