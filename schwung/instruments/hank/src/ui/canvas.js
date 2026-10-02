/*
 * Hank — the one module-supplied draw surface: "custom:hank_fm", the
 * operator waveform preview, in the OP2 Fine cell.
 *
 * WHY THIS IS CUSTOM AND THE OTHER GRAPHICS ARE NOT. The built-in viz
 * vocabulary already draws Hank's envelopes (`envelope`, three of them) and
 * its filter response (`filter`), and those are used as-is so they look
 * identical to every other module in the fleet. The built-in `waveform` viz
 * cannot do this one: it is an LFO SHAPE PICKER -- it reads a single enum key
 * and draws a named sine/tri/saw/pulse from a shape id. It has no way to
 * render a waveshape COMPUTED from ratio, brightness and bite, which is the
 * whole point of showing a modulator at all.
 *
 * THE FRAME IS NOT THE SCREEN. (0,0) is the top-left of whatever box we were
 * handed; ctx.width/ctx.height are its size. A knob cell is ~17x15. Nothing
 * here may name an absolute screen coordinate.
 *
 * NO READS. drawCell has no getParam. Every value arrives in `values`, which
 * is the whole page's value map -- this widget's siblings reach it because the
 * chain_params entry names them in viz.extra_keys.
 */

const TWO_PI = Math.PI * 2;

/* Same mapping as hank_curves.h. A preview that disagreed with the engine
 * would be worse than no preview. */
/* Same mappings as hank_curves.h and applyMacros. A preview that disagreed
 * with the engine would be worse than no preview. */
const RATIO_TABLE = [0.5, 1, 1.5, 2, 2.5, 3, 3.7, 4, 5, 5.4, 6, 7, 8, 9.2, 11, 14];
/* The parameter IS the index -- it is declared as an enum of the ratios, so
 * `values.ratio` arrives as 0..15, not as a 0..1 position along the table. */
function ratioAt(x) {
    let i = Math.round(x);
    if (i < 0) i = 0;
    if (i >= RATIO_TABLE.length) i = RATIO_TABLE.length - 1;
    return RATIO_TABLE[i];
}
function fbkToDepth(x) { return x * x * 6.0; }

/*
 * THE MODULATOR'S OWN WAVEFORM -- not the FM result.
 *
 * These four cells are OP2's controls, and OP2 never reaches the speaker: it
 * bends OP1's phase. Drawing the bent RESULT here was the obvious choice and
 * the wrong one, because at any modulation index a patch actually uses the
 * carrier's phase swings through several radians per pixel and the cell fills
 * with vertical bars -- true, and unreadable, and it does not isolate any of
 * the four knobs.
 *
 * What OP2's knobs shape IS this waveform: ratio sets how many cycles fit,
 * feedback deforms the sine towards a saw, and level scales how far it swings
 * (which is exactly how hard it bends OP1). So the picture answers "what is the
 * modulator doing", which is the question those knobs ask.
 */
/*
 * HOW MUCH TIME THE FRAME SHOWS.
 *
 * OP2 at ratio 32 is 32 cycles across ~120 px -- under 4 px each, which draws
 * as a solid block and tells you nothing. Rather than misreport the ratio by
 * drawing fewer cycles than there are, the window ZOOMS IN: past MAX_CYCLES we
 * show a shorter slice of the same waveform, so the shape stays true and only
 * the time span changes. Below that the frame is one full period of the note.
 */
const MAX_CYCLES = 6;
function windowTurns(ratio) {
    return (ratio > MAX_CYCLES) ? MAX_CYCLES / ratio : 1;
}

function modWave(n, ratio, level, fbk, ph2) {
    const out = new Array(n);
    let fb = 0;
    ph2 = ph2 || 0;
    /* windowTurns WAS DEAD CODE. It was written, commented, and never called:
     * every frame drew exactly one carrier turn, so `ratio` modulator cycles
     * were crammed into ~120 px and anything above about 8 drew as a solid
     * block of vertical bars -- which is why the cell read as noise rather than
     * as a waveform, at every ratio a bass or metallic patch actually uses. */
    const turns = windowTurns(ratio);
    /* Run the feedback loop in briefly before sampling, so the first pixel
     * shows the settled shape rather than the loop starting from zero --
     * otherwise a high-feedback patch draws a ramp-in that is not in the audio. */
    for (let warm = 0; warm < 64; warm++) {
        const p = (warm / 64) * ratio + ph2;
        fb = 0.5 * (fb + Math.sin(TWO_PI * (p + fb * fbk / TWO_PI)));
    }
    for (let i = 0; i < n; i++) {
        const t = (i / (n - 1)) * turns;
        const m = Math.sin(TWO_PI * (t * ratio + ph2 + fb * fbk / TWO_PI));
        fb = 0.5 * (fb + m);
        /* HEIGHT IS NOT THE LEVEL. Scaling the trace by `bright` directly is
         * honest and illegible: real presets sit at 0.14 to 0.5, which is a
         * two-pixel wiggle in an eighteen-pixel cell -- drawn, and invisible.
         * The floor keeps the SHAPE readable, which is what the picture is
         * for, while the height still grows with the knob. */
        out[i] = m * (0.42 + 0.58 * level);
    }
    return out;
}

function num(v, dflt) {
    const n = Number(v);
    return Number.isFinite(n) ? n : dflt;
}

globalThis.canvas_overlay = {
    /*
     * BOTH SPELLINGS, AND THAT IS NOT REDUNDANT.
     *
     * `widgetKinds` (array) is the newer form and the one to prefer: the
     * singular is a string, so a module that later declares a second kind has
     * the second one silently DROPPED, and a dropped kind is not an error --
     * it falls through to a built-in dial. Right page, no log line.
     *
     * But Schwung 1.2.0, the current release, registers a widget only when
     * `typeof ov.widgetKind === "string"`. Declaring the array alone means the
     * wave does not register there at all and ratio/bright/bite come up as
     * three plain dials -- silently, for the same reason. Hosts that know
     * `widgetKinds` read the singular first and let an explicit entry of the
     * same name win, so spelling both is exactly compatible on new hosts and
     * load-bearing on old ones.
     */
    widgetKind: "custom:hank_wave",
    widgetKinds: ["custom:hank_wave"],

    drawCell(ctx, { values, group }) {
        /*
         * ONE PICTURE, DRAWN IN TWO PLACES: the carrier as modulated by OP2.
         * Page 1 spans the row that sets ratio/fine/level/fbk; page 2 spans the
         * phase cells, whose effect cannot be seen any other way. Both draw the
         * same thing because the same thing is what you are editing.
         */
        const w = ctx.width | 0, h = ctx.height | 0;
        if (w < 6 || h < 4) return;

        /*
         * NO ANSWER, NO PICTURE. A value that did not arrive must not be
         * defaulted into a confident-looking waveform -- a wrong picture reads
         * as a working one. Draw the frame's hint and stop.
         */
        const rk = Number(values ? values.ratio : NaN);
        if (!Number.isFinite(rk)) {
            if (w >= 12 && h >= 8) ctx.print(0, (h >> 1) - 4, "--", 1);
            return;
        }
        const level = num(values ? values.bright : 0, 0);
        const ratio = ratioAt(rk);
        /* BITE drives feedback over its first 60%, as applyMacros does. */
        const bite  = num(values ? values.bite : 0, 0);
        const fbk   = fbkToDepth(Math.min(bite / 0.6, 1.0) * 0.75);
        const ph2   = 0;

        const y0 = 1, hh = h - 2;
        const mid = y0 + (hh - 1) / 2;
        const amp = (hh - 1) / 2;
        const wave = modWave(w, ratio, level, fbk, ph2);

        /* A CENTRE LINE, so level 0 is still a picture. A silent modulator is
         * a flat trace, which is the truth and reads as one -- an empty cell
         * reads as a broken widget. */
        for (let x = 0; x < w; x += 2) ctx.fillRect(x, Math.round(mid), 1, 1, 1);

        /* Connect successive samples so a fast waveshape reads as a curve
         * rather than as scattered dots. */
        let py = mid - wave[0] * amp;
        for (let x = 1; x < w; x++) {
            const cy = mid - wave[x] * amp;
            let a = Math.round(py), b = Math.round(cy);
            if (a > b) { const t = a; a = b; b = t; }
            if (a < y0) a = y0;
            if (b > y0 + hh - 1) b = y0 + hh - 1;
            if (b >= a) ctx.fillRect(x, a, 1, b - a + 1, 1);
            py = cy;
        }
    },
};
