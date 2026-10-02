/**
 * Fizzik — Polyphonic simplex physical-modeling synth for Ableton Move (Schwung).
 * Author: Filliformes
 * License: MIT
 *
 * Inspired by MechanOdd (odoare / FX-Mechanics). DSP reimplemented independently
 * in C from the documented math and public references (J.O. Smith PASP digital
 * waveguides; Tolonen/Valimaki tension modulation; RBJ modal band-pass; modal
 * synthesis of plate/membrane/beam). No source copied.
 *
 * Architecture (per voice):
 *   Exciter (noise+mallet+crackle -> resonant LPF -> percussive AD)
 *     -> two resonators A,B (String/Beam/Plate/Membrane), excited in parallel
 *     -> cross-coupling (Couple knob, 1-sample delay, DC-block + tanh soft-limit)
 *     -> A/B balance -> amp env -> pan -> voice out
 *   Global: sum voices -> Space reverb -> master level -> stereo int16.
 *
 * API: plugin_api_v2_t. 44100 Hz, 128 frames/block, stereo interleaved int16 out.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>

/* ── Constants ───────────────────────────────────────────────────────────────── */

#define SR            44100.0f
#define SR_INV        (1.0f / SR)
#define TWO_PI        6.283185307179586f
#define PI            3.141592653589793f

#define MAX_VOICES    6
#define DELAY_MAX     2048      /* single-loop waveguide, min ~21.5 Hz */
#define DLY_MAX       33075     /* FX delay line, 0.75 s @ 44.1k */
#define N_ALLPASS     4         /* dispersion sections */
#define MAX_MODES     24        /* modal bank size */
#define MAX_CAND      256       /* modal candidate scratch */

#define REF_FREQ      261.626f  /* middle C — pitch normalization reference */

#define MODEL_STRING   0
#define MODEL_BEAM     1
#define MODEL_PLATE    2
#define MODEL_MEMBRANE 3

#define N_PRESETS     31

#define N_AT_PRESET 10
static const char *MODEL_NAMES[4]  = { "String", "Beam", "Plate", "Membrane" };
static const char *AT_PRESET_NAMES[N_AT_PRESET] = {
    "Off", "Gentle", "Brighten", "Bow", "Swell", "Vibrato", "Expressive", "Cello", "Wild", "Sforzato"
};
/* {bright, bow, cutoff, vib, bend, vrate, curve} — curve<0.5 soft, >0.5 hard */
static const float AT_PRESETS[N_AT_PRESET][7] = {
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.50f, 0.50f},   /* Off        */
    {0.40f,0.10f,0.25f,0.10f,0.0f, 0.50f, 0.40f},   /* Gentle     */
    {0.80f,0.0f, 0.50f,0.0f, 0.0f, 0.50f, 0.50f},   /* Brighten   */
    {0.30f,0.70f,0.20f,0.0f, 0.0f, 0.50f, 0.50f},   /* Bow        */
    {0.40f,0.50f,0.30f,0.15f,0.05f,0.45f, 0.60f},   /* Swell      */
    {0.10f,0.0f, 0.10f,0.70f,0.0f, 0.60f, 0.40f},   /* Vibrato    */
    {0.60f,0.40f,0.40f,0.30f,0.10f,0.50f, 0.50f},   /* Expressive */
    {0.50f,0.60f,0.30f,0.40f,0.10f,0.40f, 0.55f},   /* Cello      */
    {0.80f,0.60f,0.60f,0.50f,0.30f,0.55f, 0.70f},   /* Wild       */
    {0.90f,0.30f,0.70f,0.20f,0.20f,0.50f, 0.80f},   /* Sforzato   */
};
static const char *PRESET_NAMES[N_PRESETS] = {
    "AlienChurch", "BowedGlass", "CaveStrings", "CouncilsPiano", "DistortedBass",
    "FeedbackHarp", "JudgementAwaits", "OldResonances", "PreparedPiano", "RythmicBow",
    "SensitiveSkin", "Sharp", "ShockingPluck", "Slappy", "SurroundedByBells", "XyloStyle",
    "GlassKalimba", "IronLullaby", "TidalGong", "HollowReed", "StarlightPad",
    "BrokenMusicBox", "DeepDiveBass", "CopperTongue", "GhostSitar", "MarbleDrum",
    "WhisperHarp", "TitaniumBell", "FrozenLake", "PulseEngine", "Init"
};

/* Page-aware knob overlay: keys per page (index = current_page). */
static const char *PAGE_KEYS[9][8] = {
    { "preset","rnd_patch","rnd_exc","rnd_reson","cutoff","resonance","ftype","voicing" },
    { "exc_mix","exc_crackle","exc_color","exc_attack","exc_decay","exc_reso","vel_level","vel_color" },
    { "a_model","a_struct","a_decay","a_damp","a_pos","a_tone","a_tune","a_tension" },
    { "b_model","b_struct","b_decay","b_damp","b_pos","b_tone","b_tune","b_tension" },
    { "couple","balance","glide","amp_attack","amp_release","spread","drive","level" },
    { "rev_mix","rev_size","rev_damp","dly_mix","dly_time","dly_fb","dly_tone","width" },
    { "eq_tone","eq_body","cho_mix","cho_rate","cho_depth","comp_amt","lim_drive","lim_ceil" },
    { "lfo1_rate","lfo1_depth","lfo1_shape","lfo1_target","lfo2_rate","lfo2_depth","lfo2_shape","lfo2_target" },
    { "at_preset","at_bright","at_bow","at_cutoff","at_vib","at_bend","at_vrate","at_curve" }
};
static const int PAGE_NKNOBS[9] = { 8, 8, 8, 8, 8, 8, 8, 8, 8 };

/* ── Small helpers ───────────────────────────────────────────────────────────── */

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static inline float map_exp(float x, float lo, float hi) { /* x in 0..1 */
    return lo * powf(hi / lo, clampf(x, 0.0f, 1.0f));
}
static inline float note_to_freq(float note) {
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}
static inline float sanitize(float x) { return isfinite(x) ? x : 0.0f; }
static inline float soft_limit(float x) { /* ceiling ~+12 dBFS (coupling guard) */
    if (!isfinite(x)) return 0.0f;
    const float c = 4.0f;
    return c * tanhf(x * (1.0f / c));
}
static inline float out_limit(float x) {
    /* Soft SAFETY clip with real headroom — NOT the loudness ceiling. Linear
     * through normal program (poly chords reach ~0.7-1.3 here), bounds only
     * pathological peaks at ±2 before the lookahead brickwall, which is the
     * actual ceiling (lim_ceil, 0.5..0.99). The old 0.9·tanh(x/0.9) sheared
     * from ~0.4 up: audible distortion on poly + resonant filter that no
     * limiter setting could remove (it sat BEFORE the limiter). */
    if (!isfinite(x)) return 0.0f;
    return 2.0f * tanhf(x * 0.5f);
}

/* xorshift RNG -> [0,1) */
static inline float randf(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x ? x : 0x1234567u;
    return (float)(x & 0x00FFFFFF) / (float)0x01000000;
}
static inline float randbi(uint32_t *s) { return randf(s) * 2.0f - 1.0f; }
static inline int   rnd_i(uint32_t *s, int count) {   /* bounded 0..count-1 */
    int v = (int)(randf(s) * (float)count);
    return v < 0 ? 0 : (v >= count ? count - 1 : v);
}

/* ── Biquad (one modal mode, RBJ constant 0 dB band-pass) ────────────────────── */

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;

static inline float biquad_process(biquad_t *f, float x) {
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}
static void biquad_bandpass(biquad_t *f, float freq, float Q) {
    float w0 = TWO_PI * freq * SR_INV;
    float cw = cosf(w0), sw = sinf(w0);
    float alpha = sw / (2.0f * Q);
    float a0 = 1.0f + alpha;
    f->b0 =  alpha / a0;
    f->b1 =  0.0f;
    f->b2 = -alpha / a0;
    f->a1 = -2.0f * cw / a0;
    f->a2 = (1.0f - alpha) / a0;
}

/* ── TPT state-variable LPF (exciter color, resonant) ────────────────────────── */

typedef struct { float ic1, ic2; } svf_t;
static inline float svf_lp(svf_t *s, float x, float cutoff, float reso) {
    float g = tanf(PI * clampf(cutoff, 20.0f, 18000.0f) * SR_INV);
    float k = 2.0f - 1.9f * clampf(reso, 0.0f, 1.0f);   /* damping: reso->high Q */
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float v1 = a1 * s->ic1 + a2 * (x - s->ic2);
    float v2 = s->ic2 + g * v1;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    return v2;   /* lowpass */
}

/* ── DC blocker (one-pole highpass ~8 Hz), used in the coupling loop ─────────── */

typedef struct { float x1, y1; } dcblk_t;
static inline float dcblk(dcblk_t *d, float x) {
    if (!isfinite(x)) { d->x1 = d->y1 = 0.0f; return 0.0f; }
    float y = x - d->x1 + 0.9989f * d->y1;   /* R for ~8 Hz @ 44.1k */
    d->x1 = x; d->y1 = y;
    return y;
}

/* ── Waveguide string (single loop, dispersive, tension-modulated) ───────────── */

typedef struct {
    float dl[DELAY_MAX];
    int   wpos;
    float lp;                        /* loop one-pole LP state */
    float ap[N_ALLPASS];             /* dispersion allpass state */
    float len, target_len;           /* fractional loop length (smoothed) */
    float apc, target_apc;           /* dispersion coeff (smoothed) */
    float fb_gain, fb_cut, lpc;      /* pitch-normalized loop feedback + LP coeff */
    float pos;                       /* pickup position 0..1 */
    float energy;                    /* smoothed loop energy (tension) */
    float alpha;                     /* tension coupling */
    int   snap;                      /* jump geometry (note start) */
} waveguide_t;

static void wg_reset(waveguide_t *w) {
    memset(w->dl, 0, sizeof(w->dl));
    w->wpos = 0; w->lp = 0.0f; w->energy = 0.0f;
    memset(w->ap, 0, sizeof(w->ap));
    w->snap = 1;
}

/* Fractional read from the delay line, `age` samples behind the write head. */
static inline float wg_read(waveguide_t *w, float age) {
    if (age < 1.0f) age = 1.0f;
    if (age > DELAY_MAX - 2) age = DELAY_MAX - 2;
    float rp = (float)w->wpos - age;
    while (rp < 0.0f) rp += DELAY_MAX;
#ifdef MPC_PORT
    /* MPC port (2026-10-02): a tiny negative rp plus DELAY_MAX rounds to exactly DELAY_MAX in float, one past dl[]
     * (found with steve/tools/fuzz) */
    if (rp >= (float)DELAY_MAX) rp -= DELAY_MAX;
#endif
    int i0 = (int)rp;
    float frac = rp - (float)i0;
    int i1 = i0 + 1; if (i1 >= DELAY_MAX) i1 -= DELAY_MAX;
    return w->dl[i0] + frac * (w->dl[i1] - w->dl[i0]);
}

/* Group delay of a 1st-order allpass (a + z^-1)/(1 + a z^-1) at w. */
static inline float ap_group_delay(float a, float w) {
    return (1.0f - a * a) / (1.0f + 2.0f * a * cosf(w) + a * a);
}

/* Set target coefficients for the played frequency + knob values. */
static void wg_set(waveguide_t *w, float f0, float dispersion, float decay,
                   float tone, float damp, float tension) {
    f0 = clampf(f0, 21.5f, SR * 0.45f);
    float w0 = TWO_PI * f0 * SR_INV;
    float roundtrip = SR / f0;

    /* Feedback gain: pitch-normalized so T60 is constant across the range. */
    float g01 = 0.950f + 0.0495f * clampf(decay, 0.0f, 1.0f);   /* 0.950 .. 0.9995 */
    w->fb_gain = powf(g01, REF_FREQ / f0);

    /* Loop LP cutoff from Tone, reduced by Damp, scaled to a fixed harmonic ratio. */
    float cut = map_exp(tone, 500.0f, 15000.0f) * (1.0f - 0.75f * clampf(damp, 0.0f, 1.0f));
    cut = clampf(cut * (f0 / REF_FREQ), 80.0f, SR * 0.45f);
    w->fb_cut = cut;
    w->lpc = 1.0f - expf(-TWO_PI * cut * SR_INV);   /* precompute per block */

    /* Dispersion allpass coeff (negative -> stiffness/inharmonicity). */
    float ap = -0.85f * clampf(dispersion, 0.0f, 1.0f);
    float max_disp = 0.6f * roundtrip;
    float r = max_disp / (float)N_ALLPASS;
    float acap = fminf(0.0f, (1.0f - r) / (1.0f + r));
    if (ap < acap) ap = acap;
    w->target_apc = ap;

    float disp_delay = (float)N_ALLPASS * ap_group_delay(ap, w0);
    w->target_len = fmaxf(8.0f, roundtrip - disp_delay - 1.0f);

    /* pos (pickup) is set by reso_set() after this call. */
    w->alpha = map_exp(clampf(tension, 0.0f, 1.0f) + 1e-4f, 0.02f, 6.0f) - 0.02f; /* 0..~6 */

    if (w->snap) { w->len = w->target_len; w->apc = w->target_apc; w->snap = 0; }
}

static inline float wg_process(waveguide_t *w, float input) {
    /* Glide geometry (click-free). */
    w->len += 0.02f * (w->target_len - w->len);
    w->apc += 0.02f * (w->target_apc - w->apc);

    /* Tension modulation: raise pitch (shorten loop) with loop energy. */
    float k = 1.0f + w->alpha * w->energy;
    k = clampf(k, 1.0f, 4.0f);
    float Leff = w->len / sqrtf(k);

    float out = wg_read(w, Leff);
    w->energy += 0.0045f * (out * out - w->energy);   /* ~5 ms */

    /* Pickup-position comb (position-dependent timbre, pitch unaffected). */
    float tap = wg_read(w, clampf(w->pos, 0.02f, 0.98f) * Leff);
    float pickup = out - tap;

    /* Loop filter: one-pole LP toward fb_cut (coeff precomputed in wg_set). */
    w->lp += w->lpc * (out - w->lp) + 1e-20f;   /* denormal guard */
    float v = w->fb_gain * w->lp;

    /* Dispersion allpass chain. */
    for (int j = 0; j < N_ALLPASS; j++) {
        float in = v - w->apc * w->ap[j];
        float y  = w->apc * in + w->ap[j];
        w->ap[j] = in;
        v = y;
    }

    /* Write feedback + injected excitation. */
    w->dl[w->wpos] = sanitize(v + input);
    w->wpos++; if (w->wpos >= DELAY_MAX) w->wpos = 0;

    return pickup * 0.45f;   /* trim: waveguide runs hot vs modal */
}

/* ── Modal resonator (Beam / Plate / Membrane) ───────────────────────────────── */

typedef struct {
    biquad_t f[MAX_MODES];
    float    amp[MAX_MODES];
    float    mratio[MAX_MODES];    /* mode freq / f0 (pitch-invariant) */
    float    mq[MAX_MODES];        /* mode Q (pitch-invariant) */
    float    norm;                 /* RMS level normalization */
    int      n;
    /* change-detection signature */
    int   model;
    float f0, mstruct, mdecay, mdamp, mpos;
} modal_t;

static void modal_reset(modal_t *m) {
    for (int i = 0; i < MAX_MODES; i++) { m->f[i].z1 = m->f[i].z2 = 0.0f; }
    m->n = 0; m->norm = 1.0f;
    m->f0 = -1.0f;   /* force recompute */
}

/* Cheap pitch-only retune: rescale mode frequencies, keep filter states + norm
 * (no candidate regen, no re-measurement, NO state reset — so a ringing note
 * glides in pitch smoothly under vibrato/bend instead of glitching). */
static void modal_retune(modal_t *m, float f0) {
    float nyq = SR * 0.49f;
    for (int k = 0; k < m->n; k++)
        biquad_bandpass(&m->f[k], clampf(m->mratio[k] * f0, 20.0f, nyq), m->mq[k]);
    m->f0 = f0;
}

typedef struct { float freq, amp; } cand_t;
static int cand_cmp(const void *a, const void *b) {
    float fa = ((const cand_t *)a)->freq, fb = ((const cand_t *)b)->freq;
    return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
}

/* dB-scaled damping: 0->1e-1 (dead), 1->1e-6 (permanent ring). */
static inline float decay_to_zeta(float d) {
    float dB = clampf(d, 0.0f, 1.0f) * 100.0f;
    return 0.1f * powf(10.0f, -dB / 20.0f);
}

static void modal_recompute(modal_t *m, int model, float f0,
                            float mstruct, float mdecay, float mdamp, float mpos) {
    m->model = model; m->f0 = f0; m->mstruct = mstruct;
    m->mdecay = mdecay; m->mdamp = mdamp; m->mpos = mpos;

    float nyq = SR * 0.49f;
    float zeta0 = decay_to_zeta(mdecay);
    float zeta1 = decay_to_zeta(1.0f - mdamp) * 0.5f;   /* HF damping slope */

    cand_t cand[MAX_CAND];
    int nc = 0;

    if (model == MODEL_BEAM) {
        float b = clampf(mstruct, 0.0f, 1.0f);            /* string->beam */
        float in = 0.15f, out = clampf(0.1f + 0.85f * mpos, 0.02f, 0.98f);
        for (int nn = 1; nn <= MAX_CAND && nc < MAX_CAND; nn++) {
            float fn = f0 * (float)nn * sqrtf((1.0f - b) + b * (float)nn * (float)nn);
            float phi = 2.0f * sinf(nn * PI * in) * sinf(nn * PI * out);
            cand[nc].freq = fn; cand[nc].amp = phi; nc++;
            if (fn > nyq) break;
        }
    } else {
        /* Plate / Membrane: 2D rectangular modes. */
        float aspect = 1.0f + 4.0f * clampf(mstruct, 0.0f, 1.0f);   /* 1..5 */
        float inX = 0.15f, inY = 0.15f;
        float outX = clampf(0.1f + 0.85f * mpos, 0.02f, 0.98f), outY = 0.55f;
        int kmax = (int)ceilf(sqrtf((float)MAX_MODES)) + 3;
        int mmax = kmax;
        int nmax = (int)ceilf(kmax * aspect) + 2;
        float dplate = 1.0f + 1.0f / (aspect * aspect);
        float dmemb  = sqrtf(dplate);
        for (int mm = 1; mm <= mmax && nc < MAX_CAND; mm++) {
            for (int nn = 1; nn <= nmax && nc < MAX_CAND; nn++) {
                float fm = (float)mm, fnn = (float)nn / aspect;
                float fr;
                if (model == MODEL_PLATE)
                    fr = f0 * (fm * fm + fnn * fnn) / dplate;
                else
                    fr = f0 * sqrtf(fm * fm + fnn * fnn) / dmemb;
                float phi = (2.0f * sinf(mm * PI * inX) * sinf(nn * PI * inY))
                          * (2.0f * sinf(mm * PI * outX) * sinf(nn * PI * outY));
                cand[nc].freq = fr; cand[nc].amp = phi; nc++;
            }
        }
    }

    qsort(cand, nc, sizeof(cand_t), cand_cmp);

    m->n = 0;
    for (int i = 0; i < nc && m->n < MAX_MODES; i++) {
        float fr = cand[i].freq;
        if (fr < 20.0f || fr > nyq) continue;
        float ratio = (f0 > 0.0f) ? (fr / f0) : 1.0f;
        /* Q capped at 2000 (long ring, no runaway); gain-comp capped modest. */
        float zeta = clampf(zeta0 + zeta1 * (ratio - 1.0f), 2.5e-4f, 0.99f);
        float Q = clampf(1.0f / (2.0f * zeta), 0.5f, 2000.0f);
        biquad_bandpass(&m->f[m->n], fr, Q);
        /* band-pass level compensation ~ sqrt(zetaRef/zeta) */
        float gc = clampf(sqrtf(0.02f / zeta), 0.5f, 16.0f);
        m->amp[m->n] = cand[i].amp * gc;
        m->mratio[m->n] = (f0 > 0.0f) ? fr / f0 : 1.0f;   /* pitch-invariant */
        m->mq[m->n] = Q;
        m->n++;
    }
    /* Auto-level: drive a fixed reference noise burst through the bank and
     * measure the output peak, then normalize to a target. This makes level
     * consistent across mode count / resonance / pitch — the single biggest
     * balance problem with modal banks (narrow high-Q modes catch little
     * energy from a short excitation, so amplitude-based normalization is
     * wildly off). */
    /* Measure norm on a scratch copy so the live (possibly ringing) filter
     * states are never disturbed. */
    float sz1[MAX_MODES], sz2[MAX_MODES];
    for (int k = 0; k < m->n; k++) { sz1[k] = m->f[k].z1; sz2[k] = m->f[k].z2;
                                     m->f[k].z1 = 0.0f; m->f[k].z2 = 0.0f; }
    uint32_t seed = 0x2545F491u;
    double sumsq = 0.0;
    const int MW = 4096;                 /* ~93 ms — captures the ring, not just the onset */
    for (int s = 0; s < MW; s++) {
        float in = 0.0f;
        if (s < 128) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                       in = (float)(seed & 0xFFFF) / 32768.0f - 1.0f; }
        float y = 0.0f;
        for (int k = 0; k < m->n; k++) y += m->amp[k] * biquad_process(&m->f[k], in);
        sumsq += (double)y * (double)y;
    }
    for (int k = 0; k < m->n; k++) { m->f[k].z1 = sz1[k]; m->f[k].z2 = sz2[k]; }
    float rms = (float)sqrt(sumsq / (double)MW);
    /* Normalize to a target RMS: window-RMS equalizes for decay length (long
     * rings measure high -> quieter; short plucks measure low -> louder). */
    m->norm = 0.18f / (rms + 1e-9f);
}

static inline float modal_process(modal_t *m, float input) {
    float y = 0.0f;
    for (int k = 0; k < m->n; k++)
        y += m->amp[k] * biquad_process(&m->f[k], input);
    return y * m->norm;
}

/* ── Resonator wrapper (model selectable) ────────────────────────────────────── */

typedef struct {
    int model;
    waveguide_t wg;
    modal_t     modal;
} resonator_t;

static void reso_reset(resonator_t *r) {
    wg_reset(&r->wg);
    modal_reset(&r->modal);
}

/* Update coefficients each block. f0 already includes the resonator's tune.
 * `rebuild_budget` rate-limits EXPENSIVE full modal rebuilds (qsort + 4096-sample
 * norm measurement) per render block: a smoothed struct-knob sweep with 6 held
 * modal voices would otherwise trigger up to 12 rebuilds in one 2.9 ms block
 * (CPU spike -> dropout). Over-budget rebuilds defer to the next block (params
 * are still gliding, so convergence just staggers). Initial builds (no mode set
 * yet, f0<0) bypass the budget — a silent resonator is worse than a spike. */
static void reso_set(resonator_t *r, int model, float f0, float mstruct,
                     float decay, float damp, float pos, float tone, float tension,
                     int *rebuild_budget) {
    if (model != r->model) {
        r->model = model;
        if (model == MODEL_STRING) wg_reset(&r->wg);
        else                        modal_reset(&r->modal);
    }
    if (model == MODEL_STRING) {
        wg_set(&r->wg, f0, mstruct, decay, tone, damp, tension);
        r->wg.pos = clampf(0.02f + 0.9f * pos, 0.02f, 0.98f);
    } else {
        modal_t *m = &r->modal;
        int initial = (m->f0 < 0.0f);
        int struct_dirty = initial
            || fabsf(m->mstruct - mstruct) > 1e-3f
            || fabsf(m->mdecay - decay)   > 1e-3f
            || fabsf(m->mdamp - damp)     > 1e-3f
            || fabsf(m->mpos - pos)       > 1e-3f;
        if (struct_dirty && (initial || *rebuild_budget > 0)) {
            if (!initial) (*rebuild_budget)--;
            modal_recompute(m, model, f0, mstruct, decay, damp, pos);   /* full rebuild */
        } else if (!initial && fabsf(m->f0 - f0) > 0.01f) {
            modal_retune(m, f0);   /* cheap pitch glide — no state reset, no re-measure */
        }
    }
}

static inline float reso_process(resonator_t *r, float input) {
    return (r->model == MODEL_STRING) ? wg_process(&r->wg, input)
                                      : modal_process(&r->modal, input);
}

/* ── Exciter ─────────────────────────────────────────────────────────────────── */

typedef struct {
    float env;
    int   stage;          /* 0 attack, 1 decay, 2 done */
    float mphase;         /* mallet single-cycle phase */
    int   mallet_on;
    svf_t svf;
    uint32_t rng;
} exciter_t;

static void exciter_note_on(exciter_t *e) {
    e->env = 0.0f; e->stage = 0; e->mphase = 0.0f; e->mallet_on = 1;
}

/* freq: note freq for the mallet cycle. Returns mono excitation sample. */
static inline float exciter_process(exciter_t *e, float freq, float mix, float crackle,
                                    float color_cut, float reso, float attack_ms,
                                    float decay_ms) {
    /* AD (one-shot burst). */
    if (e->stage == 0) {
        e->env += 1.0f / (attack_ms * 0.001f * SR);
        if (e->env >= 1.0f) { e->env = 1.0f; e->stage = 1; }
    } else if (e->stage == 1) {
        e->env -= 1.0f / (decay_ms * 0.001f * SR);
        if (e->env <= 0.0f) { e->env = 0.0f; e->stage = 2; }
    }
    if (e->stage == 2 && e->mallet_on == 0) return 0.0f;

    float noise = randbi(&e->rng);
    float mallet = 0.0f;
    if (e->mallet_on) {
        mallet = sinf(TWO_PI * e->mphase);
        e->mphase += freq * SR_INV;
        if (e->mphase >= 1.0f) e->mallet_on = 0;
    }
    float raw = (1.0f - mix) * noise + mix * mallet;

    /* Crackle impulses. */
    if (crackle > 0.0001f) {
        float p = crackle * 0.03f;
        if (randf(&e->rng) < p) raw += randbi(&e->rng) * 1.5f;
    }

    float filtered = svf_lp(&e->svf, raw, color_cut, reso);
    return filtered * e->env;
}

/* ── Voice ───────────────────────────────────────────────────────────────────── */

typedef struct {
    int    active;
    int    note;
    float  velocity;
    float  freq, freq_target;      /* glide */
    exciter_t exc;
    resonator_t A, B;
    dcblk_t dcA, dcB;
    float  prevA, prevB;           /* 1-sample coupling feedback (guarded) */
    float  amp_env;
    int    amp_stage;              /* 0 attack, 1 hold, 2 release, 3 off */
    float  pan_l, pan_r;
    int    silent;                 /* consecutive near-silent samples */
    int    held;
    float  pressure, pressure_sm;  /* poly aftertouch (target, smoothed) */
    float  _pr_shaped;             /* curve-shaped pressure (per block) */
    float  vib_phase;              /* per-voice vibrato LFO phase */
    uint32_t bow_rng;              /* re-excitation noise */
} voice_t;

/* ── Parameter block (also the preset layout) ────────────────────────────────── */

typedef struct {
    float exc_mix, exc_crackle, exc_color, exc_attack, exc_decay, exc_reso, vel_level, vel_color;
    int   a_model; float a_struct, a_decay, a_damp, a_pos, a_tone; int a_tune; float a_tension;
    int   b_model; float b_struct, b_decay, b_damp, b_pos, b_tone; int b_tune; float b_tension;
    /* NOTE: the 7th Voice slot is the preset's reverb mix (was "space"). */
    float couple, balance, glide, amp_attack, amp_release, spread, rev_mix, level;
    float makeup;   /* per-preset level compensation (not a knob) */
    /* FX extras (defaulted in apply_preset; zero-filled by the preset table). */
    float drive, rev_size, rev_damp, dly_mix, dly_time, dly_fb, dly_tone, width;
    /* ===== GLOBAL performance params — preserved across preset loads (see
     * apply_preset; everything from flt_cutoff to the end of the struct). ===== */
    float flt_cutoff, flt_reso; int flt_type, flt_voicing;
    /* Aftertouch (pressure) routing depths + response curve + AT preset. */
    float at_bright, at_bow, at_cutoff, at_vib, at_bend, at_vrate, at_curve; int at_preset;
    /* Two LFOs. */
    float lfo1_rate, lfo1_depth; int lfo1_shape, lfo1_target;
    float lfo2_rate, lfo2_depth; int lfo2_shape, lfo2_target;
    /* Master FX2: EQ (tone/body), chorus (mix/rate/depth), comp, limiter. */
    float eq_tone, eq_body, cho_mix, cho_rate, cho_depth, comp_amt, lim_drive, lim_ceil;
} params_t;
#define GLOBAL_PARAMS_OFF offsetof(params_t, flt_cutoff)

/* ── Simple stereo Schroeder reverb (Space) ──────────────────────────────────── */

#define NCOMB 4
#define NAP   2
static const int COMB_LEN[NCOMB] = { 1557, 1617, 1491, 1422 };
static const int AP_LEN[NAP]     = { 225, 556 };

typedef struct {
    float comb[NCOMB][1700];
    int   ci[NCOMB];
    float clp[NCOMB];
    float ap[NAP][600];
    int   ai[NAP];
} reverb_ch_t;

static float reverb_ch(reverb_ch_t *r, float x, float fb, float damp) {
    float acc = 0.0f;
    for (int c = 0; c < NCOMB; c++) {
        int L = COMB_LEN[c];
        float y = r->comb[c][r->ci[c]];
        r->clp[c] = y * (1.0f - damp) + r->clp[c] * damp + 1e-20f;   /* denormal guard */
        r->comb[c][r->ci[c]] = x + r->clp[c] * fb;
        r->ci[c]++; if (r->ci[c] >= L) r->ci[c] = 0;
        acc += y;
    }
    acc *= 0.25f;
    for (int a = 0; a < NAP; a++) {
        int L = AP_LEN[a];
        float bufv = r->ap[a][r->ai[a]];
        float y = -acc + bufv;
        r->ap[a][r->ai[a]] = acc + bufv * 0.5f;
        r->ai[a]++; if (r->ai[a] >= L) r->ai[a] = 0;
        acc = y;
    }
    return acc;
}

/* ── Global multimode filter ─────────────────────────────────────────────────── */
/* Clean-room from public VA-filter math: A. Simper's Cytomic trapezoidal SVF and
 * V. Zavalishin's ZDF transistor ladder ("The Art of VA Filter Design"). Two
 * engines, 12 voicings, LP/HP/BP/Notch. Self-oscillating, stable at 44.1k. */

#define N_FTYPE   4
#define N_VOICING 12
static const char *FTYPE_NAMES[N_FTYPE] = { "LP", "HP", "BP", "Notch" };
static const char *VOICING_NAMES[N_VOICING] = {
    "Clean SVF", "SEM", "MS-20", "Steiner", "Ladder 4P", "Ladder 2P",
    "Ladder 1P", "Prophet", "Oberheim", "Diode", "Sallen-Key", "Vintage"
};

typedef struct {
    float ic1, ic2;         /* Cytomic SVF integrator state */
    float z1, z2, z3, z4;   /* ZDF ladder one-pole states */
} filter_ch_t;

static inline void filter_reset(filter_ch_t *f) { f->ic1=f->ic2=f->z1=f->z2=f->z3=f->z4=0.0f; }

/* Bounded Padé tanh for filter drive / feedback saturation. */
static inline float ftanh(float x) {
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* One channel, one sample. g = tan(pi*fc/SR) (precomputed per block). */
static inline float filter_process(filter_ch_t *f, float x, float g, float reso,
                                   int type, int voicing) {
    reso = clampf(reso, 0.0f, 1.0f);

    /* SVF-family voicings: Clean(0) SEM(1) MS-20(2) Steiner(3) Sallen-Key(10). */
    if (voicing==0 || voicing==1 || voicing==2 || voicing==3 || voicing==10) {
        float kmin, krange, drive = 1.0f, trim;
        /* trim: measured level-match to Clean SVF on sustained 4-voice program
         * (scripts/test_voicing_levels.c) — the input tanh drive of the driven
         * voicings loses 2-4 dB there, masked before by the 0.9-tanh squash. */
        switch (voicing) {
            case 1:  kmin=0.20f; krange=1.65f; trim=0.68f; break;              /* SEM: soft res  */
            case 2:  kmin=0.030f; krange=1.97f; drive=1.8f; trim=0.96f; break; /* MS-20: scream  */
            case 3:  kmin=0.10f; krange=1.85f; drive=1.2f; trim=0.82f; break;  /* Steiner        */
            case 10: kmin=0.050f; krange=1.90f; drive=1.5f; trim=0.89f; break; /* Sallen-Key K35 */
            default: kmin=0.060f; krange=1.94f; trim=0.68f; break;             /* Clean          */
        }
        float k = kmin + krange * (1.0f - reso);      /* reso up -> k down -> more Q */
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1, a3 = g * a2;
        float xin = (drive > 1.01f) ? ftanh(x * drive) * (1.0f / drive) : x;
        float v3 = xin - f->ic2;
        float v1 = a1 * f->ic1 + a2 * v3;
        float v2 = f->ic2 + a2 * f->ic1 + a3 * v3;
        f->ic1 = 2.0f * v1 - f->ic1 + 1e-20f;
        f->ic2 = 2.0f * v2 - f->ic2;
        float lp = v2;
        float band = k * v1;                    /* unity-peak bandpass (Q-independent) */
        float hp = xin - k * v1 - lp;
        float notch = xin - k * v1;
        if (voicing==2 || voicing==10) {        /* MS-20 / K35 diode-clipped resonance */
            band = ftanh(band * 1.4f);
            hp   = ftanh(hp * 1.2f);
        }
        /* Resonant-peak compensation: the SVF peak grows as Q=1/k (up to +24 dB
         * at kmin) with no passband loss to hide it — driving reso+cutoff on a
         * poly signal slammed the output stage. Trim LP/HP once Q exceeds 1
         * (moderate settings untouched); band is unity-peak and the notch has
         * no peak, so both stay as they were. */
        float rtrim = (k < 1.0f) ? 1.0f / (1.0f + 0.15f * (1.0f / k - 1.0f)) : 1.0f;
        switch (type) { case 0: return lp*trim*rtrim; case 1: return hp*trim*rtrim; case 2: return band*trim; default: return notch*trim; }
    }

    /* Ladder-family voicings (ZDF, tanh feedback -> bounded self-oscillation).
     * trim: measured level-match to Clean SVF on 4-voice program (see
     * scripts/test_voicing_levels.c) — the input tanh drive compresses loud
     * poly signals, which the old 0.9-tanh output squash used to mask. */
    float G = g / (1.0f + g);
    int poles; float kmax, drive, trim;
    switch (voicing) {
        case 5:  poles=2; kmax=3.6f; drive=1.2f;  trim=0.82f; break;  /* Ladder 2-pole */
        case 6:  poles=1; kmax=2.6f; drive=1.1f;  trim=0.80f; break;  /* Ladder 1-pole */
        case 7:  poles=4; kmax=3.8f; drive=1.05f; trim=0.79f; break;  /* Prophet (SSM) */
        case 8:  poles=4; kmax=4.0f; drive=1.15f; trim=0.81f; break;  /* Oberheim      */
        case 9:  poles=4; kmax=4.3f; drive=1.7f;  trim=0.94f; break;  /* Diode / 303   */
        case 11: poles=4; kmax=4.0f; drive=2.4f;  trim=1.13f; break;  /* Vintage       */
        default: poles=4; kmax=4.0f; drive=1.3f;  trim=0.84f; break;  /* Ladder 4-pole */
    }
    float k = kmax * reso;
    float xin = (drive > 1.01f) ? ftanh(x * drive) * (1.0f / drive) : x;
    float G2 = G*G, G3 = G2*G, G4 = G3*G;
    float S1 = f->z1*(1.0f-G), S2 = f->z2*(1.0f-G), S3 = f->z3*(1.0f-G), S4 = f->z4*(1.0f-G);
    float Sigma = G3*S1 + G2*S2 + G*S3 + S4;
    float y4lin = (G4 * xin + Sigma) / (1.0f + k * G4);
    float u = xin - k * ftanh(y4lin);              /* saturated feedback = stable self-osc */
    /* cascade of TPT one-poles with input u */
    float v, y1, y2, y3, y4;
    v = (u  - f->z1) * G; y1 = v + f->z1; f->z1 = y1 + v + 1e-20f;
    v = (y1 - f->z2) * G; y2 = v + f->z2; f->z2 = y2 + v;
    v = (y2 - f->z3) * G; y3 = v + f->z3; f->z3 = y3 + v;
    v = (y3 - f->z4) * G; y4 = v + f->z4; f->z4 = y4 + v;

    float lp = (poles==1) ? y1 : (poles==2) ? y2 : y4;
    float hp = (poles<=2) ? (u - 2.0f*y1 + y2) : (u - 4.0f*y1 + 6.0f*y2 - 4.0f*y3 + y4);
    float bp = (poles<=2) ? (2.0f*(y1 - y2)) : (4.0f*(y2 - 2.0f*y3 + y4));
    float comp = 1.0f + 0.5f * k;                  /* restore level lost to resonance */
    switch (type) {
        case 0: return lp * comp * trim;
        case 1: return hp * trim;
        case 2: return bp * trim;
        default: return (u - bp) * trim;           /* notch */
    }
}

/* ── Modulation LFOs ─────────────────────────────────────────────────────────── */

#define N_LFO_SHAPE 5
#define N_LFO_TGT   8
static const char *LFO_SHAPE_NAMES[N_LFO_SHAPE] = { "Sine", "Tri", "Saw", "Square", "S&H" };
static const char *LFO_TGT_NAMES[N_LFO_TGT] =
    { "Off", "Cutoff", "Pitch", "Couple", "Balance", "Tension", "Tone", "Reso" };

typedef struct { float phase, sh; uint32_t rng; } lfo_t;

/* Advance one block, return bipolar value [-1,1]. rate in Hz. */
static float lfo_block(lfo_t *l, float rate, int shape, int frames) {
    l->phase += rate * (float)frames * SR_INV;
    while (l->phase >= 1.0f) { l->phase -= 1.0f; l->sh = randbi(&l->rng); }
    float ph = l->phase;
    switch (shape) {
        case 0: return sinf(TWO_PI * ph);
        case 1: return 4.0f * fabsf(ph - 0.5f) - 1.0f;   /* tri */
        case 2: return 2.0f * ph - 1.0f;                 /* saw */
        case 3: return ph < 0.5f ? 1.0f : -1.0f;         /* square */
        default: return l->sh;                            /* S&H */
    }
}

/* ── Master FX2 blocks: tilt EQ, chorus, glue comp, lookahead limiter ────────── */

#define CHO_MAX 2048     /* ~46 ms chorus buffer */
#define LIM_LA  96       /* ~2.2 ms lookahead */

/* Tilt EQ + low-body shelf. State = two one-poles per channel. */
static inline float eq_process(float x, float *lp1, float *lp2, float tone, float body) {
    *lp1 += 0.065f * (x - *lp1) + 1e-20f;    /* ~500 Hz split */
    *lp2 += 0.022f * (x - *lp2) + 1e-20f;    /* ~150 Hz body */
    float low = *lp1, high = x - *lp1;
    float tilt = (tone - 0.5f) * 2.0f;       /* -1 dark .. +1 bright */
    float y = low * (1.0f - tilt) + high * (1.0f + tilt);
    y += (body - 0.5f) * 1.4f * *lp2;        /* low weight */
    return y;
}

/* ── Instance ────────────────────────────────────────────────────────────────── */

typedef struct {
    params_t p;
    voice_t  v[MAX_VOICES];
    int      current_page;
    int      preset_idx;
    uint32_t rng;
    reverb_ch_t rvL, rvR;
    /* Stereo delay line. */
    float    dlyL[DLY_MAX], dlyR[DLY_MAX];
    int      dly_pos;
    float    dly_lpL, dly_lpR;
    float    dly_time_smooth;
    filter_ch_t fltL, fltR;         /* global multimode filter (post-synth, pre-FX) */
    params_t sm;                    /* 20 ms-smoothed shadow of p (analog-style) */
    /* Modulation + master FX2 state. */
    lfo_t    lfo1, lfo2;
    float    eq_lpL, eq_lpR, eq_lp2L, eq_lp2R;
    float    choL[CHO_MAX], choR[CHO_MAX]; int cho_pos; float cho_phase;
    float    comp_env;
    float    limL[LIM_LA], limR[LIM_LA]; int lim_pos; float lim_gain;
    /* Randomize/preset duck: fade out -> hold silent while the (deferred)
     * randomize/preset change is applied -> fade back in — never clicks. */
    float    rnd_gain; int rnd_phase, pending_rnd, rnd_hold, pending_preset;
    /* Reverb send DC guard (one-pole HPF state). */
    float    rev_send_lp;
    /* Polyphony compensation gain (n_eff^-0.35, smoothed; see render_block). */
    float    poly_gain;
    /* Hidden metering (offline preset-gain calibration): pre-clamp peak/RMS. */
    double   meter_sumsq; float meter_peak; long meter_cnt;
} fizzik_t;

/* ── Presets ─────────────────────────────────────────────────────────────────── */
/* {exc_mix,crackle,color,attack,decay,reso,velL,velC,
    aMdl,aStr,aDec,aDmp,aPos,aTone,aTune,aTens,
    bMdl,bStr,bDec,bDmp,bPos,bTone,bTune,bTens,
    couple,balance,glide,ampA,ampR,spread,space,level, makeup} — makeup baked from offline meter (peak->0.8) */
static const params_t PRESETS[N_PRESETS] = {
  /* AlienChurch */    {0.2f,0.1f,0.7f,0.05f,0.4f,0.3f,0.3f,0.4f, MODEL_MEMBRANE,0.6f,0.9f,0.5f,0.5f,0.7f,0,0.0f, MODEL_PLATE,0.4f,0.85f,0.4f,0.4f,0.6f,7,0.0f, 0.35f,0.5f,0.2f,0.3f,0.7f,0.6f,0.6f,0.6f, 0.325f},
  /* BowedGlass */     {0.0f,0.0f,0.5f,0.4f,0.6f,0.2f,0.2f,0.3f, MODEL_PLATE,0.3f,0.95f,0.6f,0.4f,0.6f,0,0.0f, MODEL_PLATE,0.5f,0.9f,0.5f,0.5f,0.7f,12,0.0f, 0.5f,0.5f,0.1f,0.5f,0.6f,0.5f,0.5f,0.55f, 0.537f},
  /* CaveStrings */    {0.1f,0.05f,0.6f,0.1f,0.5f,0.4f,0.4f,0.3f, MODEL_STRING,0.2f,0.85f,0.4f,0.15f,0.6f,0,0.2f, MODEL_STRING,0.1f,0.8f,0.5f,0.25f,0.55f,-12,0.15f, 0.3f,0.5f,0.15f,0.05f,0.5f,0.6f,0.7f,0.6f, 0.763f},
  /* CouncilsPiano */  {0.6f,0.0f,0.75f,0.01f,0.15f,0.2f,0.6f,0.5f, MODEL_STRING,0.35f,0.75f,0.6f,0.2f,0.7f,0,0.25f, MODEL_BEAM,0.15f,0.7f,0.6f,0.5f,0.65f,12,0.0f, 0.15f,0.35f,0.0f,0.02f,0.4f,0.4f,0.35f,0.6f, 2.275f},
  /* DistortedBass */  {0.4f,0.2f,0.4f,0.01f,0.2f,0.6f,0.7f,0.4f, MODEL_STRING,0.0f,0.7f,0.3f,0.3f,0.4f,-12,0.6f, MODEL_STRING,0.05f,0.65f,0.4f,0.35f,0.35f,-12,0.7f, 0.55f,0.5f,0.05f,0.02f,0.35f,0.3f,0.2f,0.7f, 1.595f},
  /* FeedbackHarp */   {0.2f,0.1f,0.65f,0.02f,0.25f,0.5f,0.5f,0.5f, MODEL_STRING,0.15f,0.92f,0.5f,0.4f,0.7f,0,0.3f, MODEL_STRING,0.2f,0.9f,0.55f,0.6f,0.6f,7,0.35f, 0.62f,0.5f,0.1f,0.03f,0.5f,0.55f,0.5f,0.55f, 2.398f},
  /* JudgementAwaits */{0.15f,0.15f,0.55f,0.08f,0.5f,0.35f,0.3f,0.4f, MODEL_PLATE,0.7f,0.85f,0.6f,0.45f,0.5f,-12,0.0f, MODEL_MEMBRANE,0.5f,0.83f,0.5f,0.5f,0.45f,0,0.0f, 0.38f,0.5f,0.2f,0.2f,0.75f,0.6f,0.75f,0.6f, 0.359f},
  /* OldResonances */  {0.1f,0.1f,0.5f,0.1f,0.55f,0.3f,0.35f,0.35f, MODEL_MEMBRANE,0.4f,0.85f,0.55f,0.5f,0.6f,0,0.0f, MODEL_PLATE,0.6f,0.8f,0.45f,0.4f,0.55f,5,0.0f, 0.4f,0.5f,0.15f,0.15f,0.65f,0.6f,0.65f,0.6f, 0.322f},
  /* PreparedPiano */  {0.5f,0.35f,0.7f,0.01f,0.18f,0.3f,0.6f,0.5f, MODEL_STRING,0.4f,0.7f,0.55f,0.25f,0.7f,0,0.2f, MODEL_MEMBRANE,0.3f,0.6f,0.6f,0.4f,0.5f,12,0.0f, 0.4f,0.45f,0.0f,0.02f,0.4f,0.45f,0.35f,0.62f, 0.666f},
  /* RythmicBow */     {0.05f,0.05f,0.55f,0.3f,0.5f,0.3f,0.3f,0.4f, MODEL_STRING,0.25f,0.9f,0.45f,0.3f,0.6f,0,0.25f, MODEL_PLATE,0.4f,0.85f,0.5f,0.45f,0.6f,7,0.0f, 0.55f,0.5f,0.1f,0.3f,0.55f,0.5f,0.5f,0.55f, 0.451f},
  /* SensitiveSkin */  {0.3f,0.2f,0.6f,0.02f,0.3f,0.45f,0.55f,0.5f, MODEL_MEMBRANE,0.5f,0.75f,0.5f,0.5f,0.55f,0,0.0f, MODEL_STRING,0.15f,0.8f,0.5f,0.35f,0.6f,0,0.3f, 0.45f,0.5f,0.05f,0.05f,0.45f,0.55f,0.45f,0.6f, 0.441f},
  /* Sharp */          {0.5f,0.05f,0.85f,0.005f,0.1f,0.3f,0.7f,0.6f, MODEL_BEAM,0.6f,0.65f,0.7f,0.3f,0.8f,0,0.0f, MODEL_BEAM,0.8f,0.6f,0.75f,0.5f,0.75f,12,0.0f, 0.2f,0.5f,0.0f,0.01f,0.3f,0.5f,0.3f,0.6f, 4.036f},
  /* ShockingPluck */  {0.55f,0.1f,0.7f,0.005f,0.12f,0.4f,0.75f,0.6f, MODEL_STRING,0.3f,0.72f,0.6f,0.2f,0.75f,0,0.4f, MODEL_STRING,0.2f,0.68f,0.6f,0.3f,0.7f,0,0.45f, 0.25f,0.5f,0.0f,0.01f,0.35f,0.5f,0.3f,0.65f, 1.141f},
  /* Slappy */         {0.35f,0.25f,0.6f,0.005f,0.1f,0.5f,0.7f,0.5f, MODEL_MEMBRANE,0.3f,0.55f,0.6f,0.4f,0.45f,0,0.0f, MODEL_STRING,0.1f,0.6f,0.5f,0.3f,0.4f,-12,0.5f, 0.3f,0.45f,0.0f,0.01f,0.3f,0.45f,0.25f,0.68f, 0.327f},
  /* SurroundedByBells*/{0.4f,0.05f,0.8f,0.01f,0.3f,0.25f,0.5f,0.5f, MODEL_PLATE,0.8f,0.92f,0.55f,0.5f,0.75f,12,0.0f, MODEL_PLATE,0.5f,0.9f,0.6f,0.45f,0.7f,19,0.0f, 0.35f,0.5f,0.1f,0.02f,0.7f,0.65f,0.7f,0.55f, 0.237f},
  /* XyloStyle */      {0.6f,0.0f,0.85f,0.005f,0.12f,0.2f,0.65f,0.55f, MODEL_BEAM,0.9f,0.6f,0.7f,0.35f,0.8f,0,0.0f, MODEL_BEAM,0.7f,0.55f,0.75f,0.45f,0.75f,12,0.0f, 0.15f,0.5f,0.0f,0.01f,0.35f,0.5f,0.3f,0.62f, 1.411f},
  /* GlassKalimba */   {0.45f,0.05f,0.78f,0.005f,0.13f,0.25f,0.65f,0.55f, MODEL_BEAM,0.5f,0.62f,0.45f,0.6f,0.78f,12,0.0f, MODEL_PLATE,0.3f,0.6f,0.5f,0.5f,0.72f,12,0.0f, 0.2f,0.45f,0.0f,0.01f,0.4f,0.55f,0.45f,0.6f, 0.465f},
  /* IronLullaby */    {0.05f,0.02f,0.55f,0.35f,0.55f,0.25f,0.3f,0.35f, MODEL_PLATE,0.5f,0.9f,0.55f,0.5f,0.55f,0,0.0f, MODEL_MEMBRANE,0.4f,0.86f,0.55f,0.5f,0.5f,7,0.0f, 0.4f,0.5f,0.15f,0.35f,0.75f,0.6f,0.6f,0.55f, 0.380f},
  /* TidalGong */      {0.1f,0.03f,0.5f,0.05f,0.5f,0.3f,0.35f,0.35f, MODEL_MEMBRANE,0.7f,0.85f,0.6f,0.4f,0.45f,-12,0.0f, MODEL_PLATE,0.6f,0.83f,0.55f,0.45f,0.5f,-5,0.0f, 0.33f,0.5f,0.2f,0.05f,0.8f,0.65f,0.7f,0.5f, 1.483f},
  /* HollowReed */     {0.05f,0.02f,0.55f,0.3f,0.5f,0.3f,0.3f,0.4f, MODEL_PLATE,0.4f,0.88f,0.45f,0.6f,0.6f,0,0.0f, MODEL_STRING,0.2f,0.86f,0.5f,0.6f,0.62f,12,0.1f, 0.45f,0.5f,0.1f,0.3f,0.5f,0.5f,0.5f,0.55f, 0.539f},
  /* StarlightPad */   {0.1f,0.05f,0.6f,0.3f,0.5f,0.3f,0.3f,0.4f, MODEL_MEMBRANE,0.5f,0.86f,0.6f,0.5f,0.55f,12,0.0f, MODEL_PLATE,0.5f,0.85f,0.55f,0.55f,0.6f,12,0.0f, 0.3f,0.5f,0.2f,0.4f,0.78f,0.7f,0.75f,0.5f, 0.405f},
  /* BrokenMusicBox */ {0.55f,0.08f,0.8f,0.005f,0.12f,0.2f,0.6f,0.55f, MODEL_BEAM,0.7f,0.62f,0.45f,0.7f,0.8f,12,0.0f, MODEL_BEAM,0.85f,0.58f,0.5f,0.6f,0.75f,13,0.0f, 0.15f,0.5f,0.0f,0.01f,0.35f,0.55f,0.45f,0.6f, 10.000f},
  /* DeepDiveBass */   {0.4f,0.1f,0.42f,0.008f,0.2f,0.5f,0.7f,0.4f, MODEL_STRING,0.05f,0.66f,0.35f,0.4f,0.45f,-24,0.4f, MODEL_STRING,0.1f,0.6f,0.4f,0.4f,0.4f,-12,0.3f, 0.3f,0.45f,0.05f,0.01f,0.35f,0.3f,0.2f,0.6f, 1.448f},
  /* CopperTongue */   {0.45f,0.05f,0.6f,0.006f,0.14f,0.3f,0.65f,0.5f, MODEL_BEAM,0.4f,0.72f,0.5f,0.55f,0.62f,-12,0.0f, MODEL_PLATE,0.35f,0.7f,0.55f,0.5f,0.58f,0,0.0f, 0.35f,0.5f,0.0f,0.01f,0.45f,0.5f,0.5f,0.6f, 0.513f},
  /* GhostSitar */     {0.35f,0.12f,0.68f,0.008f,0.2f,0.45f,0.6f,0.5f, MODEL_STRING,0.3f,0.82f,0.4f,0.72f,0.7f,0,0.55f, MODEL_STRING,0.35f,0.8f,0.45f,0.68f,0.66f,7,0.5f, 0.4f,0.5f,0.05f,0.01f,0.5f,0.55f,0.5f,0.55f, 3.688f},
  /* MarbleDrum */     {0.35f,0.15f,0.6f,0.005f,0.1f,0.4f,0.7f,0.45f, MODEL_MEMBRANE,0.3f,0.55f,0.55f,0.45f,0.5f,0,0.0f, MODEL_MEMBRANE,0.5f,0.5f,0.55f,0.4f,0.45f,-7,0.0f, 0.3f,0.5f,0.0f,0.01f,0.3f,0.45f,0.35f,0.6f, 0.408f},
  /* WhisperHarp */    {0.15f,0.05f,0.62f,0.01f,0.3f,0.4f,0.55f,0.5f, MODEL_STRING,0.15f,0.82f,0.45f,0.7f,0.65f,12,0.1f, MODEL_STRING,0.1f,0.8f,0.5f,0.65f,0.6f,0,0.05f, 0.25f,0.5f,0.05f,0.01f,0.45f,0.6f,0.55f,0.55f, 0.881f},
  /* TitaniumBell */   {0.4f,0.03f,0.82f,0.008f,0.2f,0.25f,0.55f,0.5f, MODEL_PLATE,0.75f,0.9f,0.5f,0.55f,0.8f,12,0.0f, MODEL_PLATE,0.6f,0.88f,0.48f,0.5f,0.75f,24,0.0f, 0.3f,0.5f,0.1f,0.01f,0.7f,0.6f,0.65f,0.5f, 4.257f},
  /* FrozenLake */     {0.1f,0.05f,0.6f,0.3f,0.5f,0.3f,0.3f,0.4f, MODEL_MEMBRANE,0.6f,0.85f,0.5f,0.5f,0.55f,12,0.0f, MODEL_PLATE,0.5f,0.85f,0.48f,0.55f,0.62f,7,0.0f, 0.3f,0.5f,0.25f,0.4f,0.7f,0.7f,0.8f,0.5f, 0.267f},
  /* PulseEngine */    {0.35f,0.15f,0.6f,0.008f,0.18f,0.45f,0.65f,0.5f, MODEL_STRING,0.2f,0.75f,0.35f,0.5f,0.6f,0,0.4f, MODEL_BEAM,0.5f,0.7f,0.45f,0.5f,0.6f,-12,0.0f, 0.55f,0.5f,0.0f,0.01f,0.4f,0.5f,0.3f,0.55f, 0.894f},
  /* Init (blank neutral patch — twin plain strings, no coupling, no FX) */
  /* Init */           {0.5f,0.0f,0.5f,0.01f,0.25f,0.3f,0.5f,0.3f, MODEL_STRING,0.2f,0.8f,0.5f,0.3f,0.6f,0,0.0f, MODEL_STRING,0.2f,0.8f,0.5f,0.3f,0.6f,0,0.0f, 0.0f,0.5f,0.0f,0.01f,0.4f,0.0f,0.0f,0.6f, 6.36f},
};

/* ── Parameter descriptor table (float/int fields of params_t) ───────────────── */

typedef struct { const char *key; const char *name; int isint; float mn, mx, step; size_t off; } pdesc_t;
#define PF(k,nm,mn,mx,st,fld) { k, nm, 0, mn, mx, st, offsetof(params_t, fld) }
#define PI_(k,nm,mn,mx,st,fld){ k, nm, 1, mn, mx, st, offsetof(params_t, fld) }

static const pdesc_t PDESC[] = {
    PF("exc_mix","Exc Mix",0,1,0.01f,exc_mix), PF("exc_crackle","Crackle",0,1,0.01f,exc_crackle),
    PF("exc_color","Color",0,1,0.01f,exc_color), PF("exc_attack","Attack",0,1,0.01f,exc_attack),
    PF("exc_decay","Decay",0,1,0.01f,exc_decay), PF("exc_reso","Exc Reso",0,1,0.01f,exc_reso),
    PF("vel_level","Vel Level",0,1,0.01f,vel_level), PF("vel_color","Vel Color",0,1,0.01f,vel_color),

    PI_("a_model","Model A",0,3,1,a_model), PF("a_struct","Structure A",0,1,0.01f,a_struct),
    PF("a_decay","Decay A",0,1,0.01f,a_decay), PF("a_damp","Damp A",0,1,0.01f,a_damp),
    PF("a_pos","Position A",0,1,0.01f,a_pos), PF("a_tone","Tone A",0,1,0.01f,a_tone),
    PI_("a_tune","Tune A",-24,24,1,a_tune), PF("a_tension","Tension A",0,1,0.01f,a_tension),

    PI_("b_model","Model B",0,3,1,b_model), PF("b_struct","Structure B",0,1,0.01f,b_struct),
    PF("b_decay","Decay B",0,1,0.01f,b_decay), PF("b_damp","Damp B",0,1,0.01f,b_damp),
    PF("b_pos","Position B",0,1,0.01f,b_pos), PF("b_tone","Tone B",0,1,0.01f,b_tone),
    PI_("b_tune","Tune B",-24,24,1,b_tune), PF("b_tension","Tension B",0,1,0.01f,b_tension),

    PF("couple","Couple",0,1,0.01f,couple), PF("balance","Balance",0,1,0.01f,balance),
    PF("glide","Glide",0,1,0.01f,glide), PF("amp_attack","Amp Atk",0,1,0.01f,amp_attack),
    PF("amp_release","Amp Rel",0,1,0.01f,amp_release), PF("spread","Spread",0,1,0.01f,spread),
    PF("drive","Drive",0,1,0.01f,drive), PF("level","Level",0,1,0.01f,level),

    PF("rev_mix","Reverb",0,1,0.01f,rev_mix), PF("rev_size","Rev Size",0,1,0.01f,rev_size),
    PF("rev_damp","Rev Damp",0,1,0.01f,rev_damp), PF("dly_mix","Delay",0,1,0.01f,dly_mix),
    PF("dly_time","Dly Time",0,1,0.01f,dly_time), PF("dly_fb","Dly Fbk",0,1,0.01f,dly_fb),
    PF("dly_tone","Dly Tone",0,1,0.01f,dly_tone), PF("width","Width",0,1,0.01f,width),

    PF("cutoff","Cutoff",0,1,0.01f,flt_cutoff), PF("resonance","Resonance",0,1,0.01f,flt_reso),
    PI_("ftype","Filter Type",0,N_FTYPE-1,1,flt_type), PI_("voicing","Voicing",0,N_VOICING-1,1,flt_voicing),

    PI_("at_preset","AT Preset",0,9,1,at_preset), PF("at_bright","AT Bright",0,1,0.01f,at_bright),
    PF("at_bow","AT Bow",0,1,0.01f,at_bow), PF("at_cutoff","AT Cutoff",0,1,0.01f,at_cutoff),
    PF("at_vib","AT Vib",0,1,0.01f,at_vib), PF("at_bend","AT Bend",0,1,0.01f,at_bend),
    PF("at_vrate","AT Vib Rate",0,1,0.01f,at_vrate), PF("at_curve","AT Curve",0,1,0.01f,at_curve),

    PF("lfo1_rate","LFO1 Rate",0,1,0.01f,lfo1_rate), PF("lfo1_depth","LFO1 Depth",0,1,0.01f,lfo1_depth),
    PI_("lfo1_shape","LFO1 Shape",0,N_LFO_SHAPE-1,1,lfo1_shape), PI_("lfo1_target","LFO1 Target",0,N_LFO_TGT-1,1,lfo1_target),
    PF("lfo2_rate","LFO2 Rate",0,1,0.01f,lfo2_rate), PF("lfo2_depth","LFO2 Depth",0,1,0.01f,lfo2_depth),
    PI_("lfo2_shape","LFO2 Shape",0,N_LFO_SHAPE-1,1,lfo2_shape), PI_("lfo2_target","LFO2 Target",0,N_LFO_TGT-1,1,lfo2_target),

    PF("eq_tone","Tone",0,1,0.01f,eq_tone), PF("eq_body","Body",0,1,0.01f,eq_body),
    PF("cho_mix","Chorus",0,1,0.01f,cho_mix), PF("cho_rate","Cho Rate",0,1,0.01f,cho_rate),
    PF("cho_depth","Cho Depth",0,1,0.01f,cho_depth), PF("comp_amt","Glue",0,1,0.01f,comp_amt),
    PF("lim_drive","Lim Drive",0,1,0.01f,lim_drive), PF("lim_ceil","Lim Ceil",0,1,0.01f,lim_ceil),
};
static const int N_PDESC = (int)(sizeof(PDESC) / sizeof(PDESC[0]));

static const pdesc_t *find_pdesc(const char *key) {
    for (int i = 0; i < N_PDESC; i++)
        if (strcmp(PDESC[i].key, key) == 0) return &PDESC[i];
    return NULL;
}
static inline float *pf_ptr(fizzik_t *inst, const pdesc_t *d) {
    return (float *)((char *)&inst->p + d->off);
}
static inline int *pi_ptr(fizzik_t *inst, const pdesc_t *d) {
    return (int *)((char *)&inst->p + d->off);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────────── */

static void apply_preset(fizzik_t *inst, int idx) {
    idx = (idx < 0) ? 0 : (idx >= N_PRESETS ? N_PRESETS - 1 : idx);
    /* Preserve the whole GLOBAL region (filter, aftertouch, LFOs, master FX2)
     * across preset changes — presets only carry the voice/patch sound. */
    char gsave[sizeof(params_t) - GLOBAL_PARAMS_OFF];
    memcpy(gsave, (char *)&inst->p + GLOBAL_PARAMS_OFF, sizeof(gsave));
    inst->p = PRESETS[idx];
    memcpy((char *)&inst->p + GLOBAL_PARAMS_OFF, gsave, sizeof(gsave));
    if (inst->p.makeup < 1e-6f) inst->p.makeup = 1.0f;
    /* FX aren't in the positional preset table — give them musical defaults
     * (rev_mix comes from the preset's own value above). */
    inst->p.drive    = 0.0f;
    inst->p.rev_size = 0.55f;
    inst->p.rev_damp = 0.40f;
    inst->p.dly_mix  = 0.0f;
    inst->p.dly_time = 0.38f;
    inst->p.dly_fb   = 0.35f;
    inst->p.dly_tone = 0.5f;
    inst->p.width    = 0.5f;
    inst->preset_idx = idx;
}

/* Aftertouch preset -> the seven at_* depth params. */
static void apply_at_preset(fizzik_t *inst, int idx) {
    idx = (idx < 0) ? 0 : (idx >= N_AT_PRESET ? N_AT_PRESET - 1 : idx);
    const float *a = AT_PRESETS[idx];
    inst->p.at_bright=a[0]; inst->p.at_bow=a[1]; inst->p.at_cutoff=a[2];
    inst->p.at_vib=a[3]; inst->p.at_bend=a[4]; inst->p.at_vrate=a[5]; inst->p.at_curve=a[6];
    inst->p.at_preset = idx;
}

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir; (void)json_defaults;
    fizzik_t *inst = (fizzik_t *)calloc(1, sizeof(fizzik_t));
    if (!inst) return NULL;

    inst->rng = 0xC0FFEEu;
    /* Global defaults. Filter: open, LP, Clean SVF (transparent). */
    inst->p.flt_cutoff = 1.0f; inst->p.flt_reso = 0.0f;
    inst->p.flt_type = 0; inst->p.flt_voicing = 0;
    apply_at_preset(inst, 6);              /* Expressive aftertouch by default */
    inst->p.lfo1_rate = 0.3f; inst->p.lfo1_depth = 0.0f; inst->p.lfo1_shape = 0; inst->p.lfo1_target = 0;
    inst->p.lfo2_rate = 0.2f; inst->p.lfo2_depth = 0.0f; inst->p.lfo2_shape = 1; inst->p.lfo2_target = 0;
    inst->p.eq_tone = 0.5f; inst->p.eq_body = 0.5f;
    inst->p.cho_mix = 0.0f; inst->p.cho_rate = 0.35f; inst->p.cho_depth = 0.4f;
    /* Hearing safety: default the brickwall limiter to a low ceiling (~0.67
     * linear, about -3.5 dB) so the synth can't get ear-splitting on headphones,
     * even at high resonance / screechy patches. User can raise it if they want. */
    inst->p.comp_amt = 0.0f; inst->p.lim_drive = 0.0f; inst->p.lim_ceil = 0.35f;
    inst->lim_gain = 1.0f;
    inst->poly_gain = 1.0f;
    inst->rnd_gain = 1.0f;   /* rnd_phase / pending_rnd = 0 (idle) from calloc */
    inst->pending_preset = -1;   /* calloc 0 would mean "preset 0 pending" */
    inst->lfo1.rng = 0x9E3779B9u; inst->lfo2.rng = 0x85EBCA6Bu;
    apply_preset(inst, 2);   /* CaveStrings — pleasant default */
    inst->sm = inst->p;      /* prime smoothed shadow (no startup glide) */
    filter_reset(&inst->fltL); filter_reset(&inst->fltR);
    inst->current_page = 0;
    inst->dly_time_smooth = 16000.0f;

    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &inst->v[i];
        v->exc.rng = 0x1000u + (uint32_t)i * 2654435761u;
        v->bow_rng = 0x7F4A7C15u + (uint32_t)i * 40503u;
        v->A.model = -1; v->B.model = -1;
        reso_reset(&v->A); reso_reset(&v->B);
        v->pan_l = v->pan_r = 0.70710678f;
    }
    return inst;
}

static void destroy_instance(void *instance) { free(instance); }

/* ── MIDI ────────────────────────────────────────────────────────────────────── */

static void voice_start(fizzik_t *inst, voice_t *v, int note, float vel) {
    v->active = 1; v->held = 1;
    v->note = note; v->velocity = vel;
    v->freq_target = note_to_freq((float)note);
    v->freq = v->freq_target;
    v->amp_env = 0.0f; v->amp_stage = 0;
    v->prevA = v->prevB = 0.0f;
    v->pressure = 0.0f; v->pressure_sm = 0.0f; v->vib_phase = 0.0f;
    v->silent = 0;
    v->dcA.x1 = v->dcA.y1 = v->dcB.x1 = v->dcB.y1 = 0.0f;
    exciter_note_on(&v->exc);
    v->A.wg.snap = 1; v->B.wg.snap = 1;
    reso_reset(&v->A); reso_reset(&v->B);
    /* Stereo spread by voice index. */
    float sp = clampf(inst->p.spread, 0.0f, 1.0f);
    float pan = ((float)(note % 7) / 6.0f - 0.5f) * sp;   /* -0.5..0.5 */
    v->pan_l = cosf((pan * 0.5f + 0.5f) * (PI * 0.5f));
    v->pan_r = sinf((pan * 0.5f + 0.5f) * (PI * 0.5f));
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    fizzik_t *inst = (fizzik_t *)instance;
    if (len < 2) return;
    uint8_t status = msg[0] & 0xF0;
    /* Channel aftertouch is a 2-byte message (status + pressure) — handle it
     * before requiring a 3rd byte. */
    if (status == 0xD0) {
        float pr = msg[1] / 127.0f;
        for (int i = 0; i < MAX_VOICES; i++)
            if (inst->v[i].active && inst->v[i].held) inst->v[i].pressure = pr;
        return;
    }
    if (len < 3) return;
    int note = msg[1];
    int vel  = msg[2];

    if (status == 0x90 && vel > 0) {
        /* Find a free voice; else steal the QUIETEST (prefer released voices) —
         * stealing a loud ringing voice hard-cuts it to zero in one sample. */
        int slot = -1;
        for (int i = 0; i < MAX_VOICES; i++) if (!inst->v[i].active) { slot = i; break; }
        if (slot < 0) {
            float best = 1e9f;
            for (int i = 0; i < MAX_VOICES; i++) {
                float score = inst->v[i].amp_env + (inst->v[i].held ? 10.0f : 0.0f);
                if (score < best) { best = score; slot = i; }
            }
        }
        voice_start(inst, &inst->v[slot], note, vel / 127.0f);
    } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
        for (int i = 0; i < MAX_VOICES; i++)
            if (inst->v[i].active && inst->v[i].held && inst->v[i].note == note) {
                inst->v[i].held = 0;
                if (inst->v[i].amp_stage < 2) inst->v[i].amp_stage = 2;   /* release */
            }
    } else if (status == 0xA0) {          /* polyphonic aftertouch (per note) */
        float pr = vel / 127.0f;
        for (int i = 0; i < MAX_VOICES; i++)
            if (inst->v[i].active && inst->v[i].held && inst->v[i].note == note)
                inst->v[i].pressure = pr;
    }
}

/* ── Parameters ──────────────────────────────────────────────────────────────── */

static void rnd_exciter(fizzik_t *inst) {
    uint32_t *s = &inst->rng;
    /* Constrained so the burst always injects adequate energy (a weak exciter =
     * quiet patch regardless of makeup): short attack that actually reaches full,
     * moderate+ decay, not-too-dark color, moderate mix. Keeps variety, kills the
     * near-silent outliers. */
    inst->p.exc_mix    = 0.15f + 0.7f * randf(s);          /* 0.15..0.85 */
    inst->p.exc_crackle = randf(s) * randf(s) * 0.3f;      /* occasional, low — broadband HF clicks */
    inst->p.exc_color  = 0.32f + 0.38f * randf(s);         /* 0.32..0.70: bright noise = screech fuel */
    inst->p.exc_attack = randf(s) * randf(s) * 0.3f;       /* short: reaches full amplitude */
    inst->p.exc_decay  = 0.35f + 0.5f * randf(s);          /* 0.35..0.85: enough energy */
    inst->p.exc_reso   = randf(s) * 0.35f;                 /* a resonant exciter peak can whistle */
    inst->p.vel_level  = randf(s) * 0.6f; inst->p.vel_color = randf(s) * 0.6f;
}
static void rnd_one_reso(fizzik_t *inst, int isB) {
    uint32_t *s = &inst->rng;
    int mdl = (int)(randf(s) * 4.0f); if (mdl > 3) mdl = 3;
    /* Tuned to avoid screechy, high-pitched results: cap structure (no extreme
     * inharmonic/dense high modes), keep a damping floor (HF actually decays),
     * modest decay, darker tone, and bias tune DOWN so resonators never ring an
     * octave above the played note. */
    float str = randf(s) * randf(s) * 0.7f;              /* skew low: 0..0.7, mostly < 0.35 */
    /* Beam AND Plate have quadratic frequency laws (f ~ n^2) — their high partials
     * scream fast, so cap structure hard for both. Membrane (sqrt law) is mellow. */
    if (mdl == MODEL_BEAM || mdl == MODEL_PLATE) str *= 0.45f;
    float dec = 0.52f + 0.28f * randf(s);                /* 0.52..0.80: sustained -> consistent loudness */
    /* Damp is THE modal brightness control (tone isn't used by modal models):
     * give modal models a higher damping floor so HF partials always decay, but
     * keep the waveguide lower (its damp cuts output level, not just screech). */
    float dmp = (mdl == MODEL_STRING) ? 0.25f + 0.35f * randf(s)   /* 0.25..0.60 */
                                      : 0.40f + 0.30f * randf(s);  /* 0.40..0.70 */
    float pos = 0.12f + 0.33f * randf(s);                /* 0.12..0.45: high pos combs to silence */
    float tone = 0.15f + 0.27f * randf(s);               /* 0.15..0.42: never piercing (waveguide LP) */
    int tune = (int)(randf(s) * 13.0f) - 12;             /* -12..0: never above the played note */
    float tens = (mdl <= MODEL_BEAM) ? randf(s) * 0.35f : 0.0f;
    if (!isB) { inst->p.a_model=mdl; inst->p.a_struct=str; inst->p.a_decay=dec; inst->p.a_damp=dmp;
                inst->p.a_pos=pos; inst->p.a_tone=tone; inst->p.a_tune=tune; inst->p.a_tension=tens; }
    else      { inst->p.b_model=mdl; inst->p.b_struct=str; inst->p.b_decay=dec; inst->p.b_damp=dmp;
                inst->p.b_pos=pos; inst->p.b_tone=tone; inst->p.b_tune=tune; inst->p.b_tension=tens; }
}
static void rnd_reson(fizzik_t *inst) { rnd_one_reso(inst, 0); rnd_one_reso(inst, 1); }
static void rnd_patch(fizzik_t *inst) {
    rnd_exciter(inst); rnd_reson(inst);
    inst->p.couple = randf(&inst->rng) * 0.45f;          /* keep coupling from self-oscillating */
    inst->p.balance = 0.35f + 0.3f * randf(&inst->rng);
    /* Random patches read hot, worst with high coupling (feedback reinforces both
     * resonators). Scale makeup down with the coupling amount to flatten the loud
     * tail, but keep a floor so nothing comes out too quiet (volume consistency). */
    inst->p.makeup = 1.7f - inst->p.couple * 1.0f;       /* louder mean; limiter caps the loud tail */
}

/* Rnd All — randomize EVERYTHING: voice + per-voice FX + global filter + LFOs +
 * aftertouch + master EQ/chorus/comp. The output LIMITER is deliberately left at
 * its safe setting (never randomized) for hearing safety; resonance/drive are
 * skewed low to avoid screech. */
/* Request a randomize: fade out first (render applies it while silent, then fades
 * back in) so the abrupt parameter/model change is never audible as a click.
 * which: 1=patch, 2=exciter, 3=reson, 4=all. */
static void trigger_rnd(fizzik_t *inst, int which) {
    /* Speed filter: ignore re-fires while a crossfade is already running. Turning
     * the knob fast jumps many numbers -> many fires; without this the duck keeps
     * restarting and re-gates the output (the "click"). One randomize per ~110ms
     * crossfade; slow +/-1 turns each still fire cleanly. */
    if (inst->rnd_phase != 0) return;
    inst->pending_rnd = which; inst->rnd_phase = 1;
}
/* Preset changes get the same click guard: update the index (display / knob
 * chaining) immediately, defer the actual param apply to the silent point of
 * the duck. Rapid scrolling just retargets the pending index — the latest one
 * wins when the fade bottoms out. */
static void trigger_preset(fizzik_t *inst, int idx) {
    idx = (idx < 0) ? 0 : (idx >= N_PRESETS ? N_PRESETS - 1 : idx);
    inst->preset_idx = idx;
    inst->pending_preset = idx;
    if (inst->rnd_phase == 0 || inst->rnd_phase == 2) inst->rnd_phase = 1;
}
/* Clear each voice's cross-coupling feedback + DC-blocker so the newly-tuned
 * resonators don't get a transient kick from the old loop state (a click source
 * that survives the output fade). Only for resonator-changing randomizes. */
static void reset_voice_coupling(fizzik_t *inst) {
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &inst->v[i];
        v->prevA = v->prevB = 0.0f;
        v->dcA.x1 = v->dcA.y1 = 0.0f; v->dcB.x1 = v->dcB.y1 = 0.0f;
    }
}
static void apply_pending_rnd(fizzik_t *inst) {
    int had_preset = (inst->pending_preset >= 0);
    if (had_preset) { apply_preset(inst, inst->pending_preset); inst->pending_preset = -1; }
    int which = inst->pending_rnd;
    if      (which == 1) rnd_patch(inst);
    else if (which == 2) rnd_exciter(inst);
    else if (which == 3) rnd_reson(inst);
    if (had_preset || (which != 0 && which != 2))
        reset_voice_coupling(inst);   /* exciter doesn't touch resonators */
    inst->pending_rnd = 0;
}

/* Set one param field from a numeric string. */
static void set_field(fizzik_t *inst, const pdesc_t *d, const char *val) {
    if (d->isint) *pi_ptr(inst, d) = (int)clampf((float)atoi(val), d->mn, d->mx);
    else          *pf_ptr(inst, d) = clampf((float)atof(val), d->mn, d->mx);
}

/* Model enum accepts a name or an index. */
static int parse_model(const char *val) {
    for (int i = 0; i < 4; i++) if (strcmp(val, MODEL_NAMES[i]) == 0) return i;
    int idx = atoi(val); return (idx < 0) ? 0 : (idx > 3) ? 3 : idx;
}
/* Generic enum parse: name match, else clamped index. */
static int parse_enum(const char *val, const char **names, int count) {
    for (int i = 0; i < count; i++) if (strcmp(val, names[i]) == 0) return i;
    int idx = atoi(val); return (idx < 0) ? 0 : (idx >= count) ? count - 1 : idx;
}

static void set_param(void *instance, const char *key, const char *val) {
    fizzik_t *inst = (fizzik_t *)instance;
    if (!inst || !key || !val) return;

    /* Page navigation. */
    if (strcmp(key, "_level") == 0 || strcmp(key, "current_level") == 0) {
        if      (strcmp(val, "Patch") == 0 || strcmp(val, "root") == 0 || strcmp(val, "Fizzik") == 0) inst->current_page = 0;
        else if (strcmp(val, "Exciter") == 0) inst->current_page = 1;
        else if (strcmp(val, "ResonA") == 0) inst->current_page = 2;
        else if (strcmp(val, "ResonB") == 0) inst->current_page = 3;
        else if (strcmp(val, "Voice")  == 0) inst->current_page = 4;
        else if (strcmp(val, "FX")     == 0) inst->current_page = 5;
        else if (strcmp(val, "FX2")    == 0) inst->current_page = 6;
        else if (strcmp(val, "Mod")    == 0) inst->current_page = 7;
        else if (strcmp(val, "Touch")  == 0) inst->current_page = 8;
        return;
    }

    /* Knob overlay: knob_N_adjust (page-aware). */
    if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_adjust")) {
        int idx = atoi(key + 5) - 1;
        if (idx < 0 || idx >= PAGE_NKNOBS[inst->current_page]) return;
        const char *pk = PAGE_KEYS[inst->current_page][idx];
        if (!pk || !pk[0]) return;
        int delta = atoi(val);
        /* triggers / preset handled by name below */
        if (strcmp(pk, "preset") == 0) {
            trigger_preset(inst, inst->preset_idx + delta);   /* click-guarded apply */
            return;
        }
        if (strcmp(pk, "at_preset") == 0) {
            apply_at_preset(inst, inst->p.at_preset + delta); return;
        }
        if (strncmp(pk, "rnd_", 4) == 0) {
            if (delta != 0) { if (!strcmp(pk,"rnd_patch")) trigger_rnd(inst,1);
                              else if (!strcmp(pk,"rnd_exc")) trigger_rnd(inst,2);
                              else if (!strcmp(pk,"rnd_reson")) trigger_rnd(inst,3); }
            return;
        }
        const pdesc_t *d = find_pdesc(pk);
        if (d) {
            if (d->isint) *pi_ptr(inst, d) = (int)clampf((float)(*pi_ptr(inst, d) + delta), d->mn, d->mx);
            else          *pf_ptr(inst, d) = clampf(*pf_ptr(inst, d) + (float)delta * d->step, d->mn, d->mx);
        }
        return;
    }

    /* Hidden calibration hook: override makeup gain. */
    if (strcmp(key, "__makeup") == 0) { inst->p.makeup = (float)atof(val); return; }

    /* Triggers. Schwung's trigger-enum (options exactly ["idle","trigger"]) sends
     * "trigger" once per fire gesture and auto-resets itself — so fire on "trigger"
     * (or any nonzero), never on "idle". */
    if (strncmp(key, "rnd_", 4) == 0) {
        if (strcmp(val, "trigger") == 0 || atoi(val) != 0) {
            if (!strcmp(key,"rnd_patch")) trigger_rnd(inst,1);
            else if (!strcmp(key,"rnd_exc")) trigger_rnd(inst,2);
            else if (!strcmp(key,"rnd_reson")) trigger_rnd(inst,3);
        }
        return;
    }
    if (strcmp(key, "preset") == 0) {
        /* By-key preset select (menu edit / host state restore). Apply
         * IMMEDIATELY — the knob path's deferred apply would fire at the next
         * render block, AFTER the host has restored the individual params, and
         * stomp a saved patch back to the factory preset. Re-selecting the
         * already-current preset is a no-op for the same reason: on restore the
         * host echoes the saved preset name back after "state" has set it. */
        int idx = -1;
        for (int i = 0; i < N_PRESETS; i++) if (strcmp(val, PRESET_NAMES[i]) == 0) { idx = i; break; }
        if (idx < 0) {
            char *end; long li = strtol(val, &end, 10);
            if (end == val) return;   /* neither a preset name nor an index: ignore */
            idx = (int)li;
        }
        idx = (idx < 0) ? 0 : (idx >= N_PRESETS ? N_PRESETS - 1 : idx);
        if (idx == inst->preset_idx) return;
        apply_preset(inst, idx);
        reset_voice_coupling(inst);
        if (inst->rnd_phase == 0 || inst->rnd_phase == 2) inst->rnd_phase = 1;  /* duck masks the jump */
        return;
    }
    if (strcmp(key, "a_model") == 0) { inst->p.a_model = parse_model(val); return; }
    if (strcmp(key, "b_model") == 0) { inst->p.b_model = parse_model(val); return; }
    if (strcmp(key, "ftype")   == 0) { inst->p.flt_type = parse_enum(val, FTYPE_NAMES, N_FTYPE); return; }
    if (strcmp(key, "voicing") == 0) { inst->p.flt_voicing = parse_enum(val, VOICING_NAMES, N_VOICING); return; }
    if (strcmp(key, "at_preset") == 0)   { apply_at_preset(inst, parse_enum(val, AT_PRESET_NAMES, N_AT_PRESET)); return; }
    if (strcmp(key, "lfo1_shape") == 0)  { inst->p.lfo1_shape = parse_enum(val, LFO_SHAPE_NAMES, N_LFO_SHAPE); return; }
    if (strcmp(key, "lfo2_shape") == 0)  { inst->p.lfo2_shape = parse_enum(val, LFO_SHAPE_NAMES, N_LFO_SHAPE); return; }
    if (strcmp(key, "lfo1_target") == 0) { inst->p.lfo1_target = parse_enum(val, LFO_TGT_NAMES, N_LFO_TGT); return; }
    if (strcmp(key, "lfo2_target") == 0) { inst->p.lfo2_target = parse_enum(val, LFO_TGT_NAMES, N_LFO_TGT); return; }

    /* State restore: newline-separated key=value. */
    if (strcmp(key, "state") == 0) {
        const char *pp = val;
        while (*pp) {
            const char *eol = strchr(pp, '\n'); if (!eol) eol = pp + strlen(pp);
            const char *eq = (const char *)memchr(pp, '=', eol - pp);
            if (eq && eq > pp) {
                char k[48], v[64];
                int kl = (int)(eq - pp), vl = (int)(eol - eq - 1);
                if (kl > 0 && kl < (int)sizeof(k) && vl >= 0 && vl < (int)sizeof(v)) {
                    memcpy(k, pp, kl); k[kl] = 0; memcpy(v, eq + 1, vl); v[vl] = 0;
                    if (strcmp(k, "state") != 0) set_param(inst, k, v);
                }
            }
            if (!*eol) break;
            pp = eol + 1;
        }
        return;
    }

    /* Regular fields. */
    const pdesc_t *d = find_pdesc(key);
    if (d) set_field(inst, d, val);
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    fizzik_t *inst = (fizzik_t *)instance;
    if (!inst || !key) return -1;

    if (strcmp(key, "name") == 0) return snprintf(buf, buf_len, "Fizzik");

    /* Hidden calibration meter: "peak rms", resets on read. */
    if (strcmp(key, "__meter") == 0) {
        double rms = inst->meter_cnt ? sqrt(inst->meter_sumsq / (double)inst->meter_cnt) : 0.0;
        int r = snprintf(buf, buf_len, "%.5f %.5f", inst->meter_peak, rms);
        inst->meter_peak = 0.0f; inst->meter_sumsq = 0.0; inst->meter_cnt = 0;
        return r;
    }

    if (strcmp(key, "chain_params") == 0) {
        return snprintf(buf, buf_len,
          "["
          /* preset FIRST (an ordered per-key restore must apply it before the
           * individual params); rnd_* are access:"write" triggers — the host
           * renders a fire-on button, never scrubs the value. */
          "{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"enum\",\"options\":[\"AlienChurch\",\"BowedGlass\",\"CaveStrings\",\"CouncilsPiano\",\"DistortedBass\",\"FeedbackHarp\",\"JudgementAwaits\",\"OldResonances\",\"PreparedPiano\",\"RythmicBow\",\"SensitiveSkin\",\"Sharp\",\"ShockingPluck\",\"Slappy\",\"SurroundedByBells\",\"XyloStyle\",\"GlassKalimba\",\"IronLullaby\",\"TidalGong\",\"HollowReed\",\"StarlightPad\",\"BrokenMusicBox\",\"DeepDiveBass\",\"CopperTongue\",\"GhostSitar\",\"MarbleDrum\",\"WhisperHarp\",\"TitaniumBell\",\"FrozenLake\",\"PulseEngine\",\"Init\"]},"
          "{\"key\":\"rnd_patch\",\"name\":\"Rnd Patch\",\"type\":\"enum\",\"options\":[\"idle\",\"trigger\"],\"access\":\"write\"},"
          "{\"key\":\"rnd_exc\",\"name\":\"Rnd Exciter\",\"type\":\"enum\",\"options\":[\"idle\",\"trigger\"],\"access\":\"write\"},"
          "{\"key\":\"rnd_reson\",\"name\":\"Rnd Reson\",\"type\":\"enum\",\"options\":[\"idle\",\"trigger\"],\"access\":\"write\"},"
          "{\"key\":\"exc_mix\",\"name\":\"Exc Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"exc_crackle\",\"name\":\"Crackle\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"exc_color\",\"name\":\"Color\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"exc_attack\",\"name\":\"Attack\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"exc_decay\",\"name\":\"Decay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"exc_reso\",\"name\":\"Exc Reso\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"vel_level\",\"name\":\"Vel Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"vel_color\",\"name\":\"Vel Color\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"a_model\",\"name\":\"Model A\",\"type\":\"enum\",\"options\":[\"String\",\"Beam\",\"Plate\",\"Membrane\"]},"
          "{\"key\":\"a_struct\",\"name\":\"Structure A\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"a_decay\",\"name\":\"Decay A\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"a_damp\",\"name\":\"Damp A\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"a_pos\",\"name\":\"Position A\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"a_tone\",\"name\":\"Tone A\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"a_tune\",\"name\":\"Tune A\",\"type\":\"int\",\"min\":-24,\"max\":24,\"step\":1},"
          "{\"key\":\"a_tension\",\"name\":\"Tension A\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"b_model\",\"name\":\"Model B\",\"type\":\"enum\",\"options\":[\"String\",\"Beam\",\"Plate\",\"Membrane\"]},"
          "{\"key\":\"b_struct\",\"name\":\"Structure B\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"b_decay\",\"name\":\"Decay B\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"b_damp\",\"name\":\"Damp B\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"b_pos\",\"name\":\"Position B\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"b_tone\",\"name\":\"Tone B\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"b_tune\",\"name\":\"Tune B\",\"type\":\"int\",\"min\":-24,\"max\":24,\"step\":1},"
          "{\"key\":\"b_tension\",\"name\":\"Tension B\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"couple\",\"name\":\"Couple\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"balance\",\"name\":\"Balance\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"glide\",\"name\":\"Glide\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"amp_attack\",\"name\":\"Amp Atk\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"amp_release\",\"name\":\"Amp Rel\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"spread\",\"name\":\"Spread\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"drive\",\"name\":\"Drive\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"level\",\"name\":\"Level\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"rev_mix\",\"name\":\"Reverb\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"rev_size\",\"name\":\"Rev Size\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"rev_damp\",\"name\":\"Rev Damp\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"dly_mix\",\"name\":\"Delay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"dly_time\",\"name\":\"Dly Time\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"dly_fb\",\"name\":\"Dly Fbk\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"dly_tone\",\"name\":\"Dly Tone\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"width\",\"name\":\"Width\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"cutoff\",\"name\":\"Cutoff\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"resonance\",\"name\":\"Resonance\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"ftype\",\"name\":\"Filter Type\",\"type\":\"enum\",\"options\":[\"LP\",\"HP\",\"BP\",\"Notch\"]},"
          "{\"key\":\"voicing\",\"name\":\"Voicing\",\"type\":\"enum\",\"options\":[\"Clean SVF\",\"SEM\",\"MS-20\",\"Steiner\",\"Ladder 4P\",\"Ladder 2P\",\"Ladder 1P\",\"Prophet\",\"Oberheim\",\"Diode\",\"Sallen-Key\",\"Vintage\"]},"
          "{\"key\":\"eq_tone\",\"name\":\"Tone\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"eq_body\",\"name\":\"Body\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"cho_mix\",\"name\":\"Chorus\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"cho_rate\",\"name\":\"Cho Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"cho_depth\",\"name\":\"Cho Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"comp_amt\",\"name\":\"Glue\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"lim_drive\",\"name\":\"Lim Drive\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"lim_ceil\",\"name\":\"Lim Ceil\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          /* Declared viz groups: both LFOs draw the animated LFO graphic
           * (the detector alone only ever resolves ONE group per page). */
          "{\"key\":\"lfo1_rate\",\"name\":\"LFO1 Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"viz\":{\"group\":\"lfo1\",\"role\":\"rate\",\"kind\":\"lfo\"}},"
          "{\"key\":\"lfo1_depth\",\"name\":\"LFO1 Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"viz\":{\"group\":\"lfo1\",\"role\":\"depth\"}},"
          "{\"key\":\"lfo1_shape\",\"name\":\"LFO1 Shape\",\"type\":\"enum\",\"options\":[\"Sine\",\"Tri\",\"Saw\",\"Square\",\"S&H\"],\"viz\":{\"group\":\"lfo1\",\"role\":\"shape\"}},"
          "{\"key\":\"lfo1_target\",\"name\":\"LFO1 Target\",\"type\":\"enum\",\"options\":[\"Off\",\"Cutoff\",\"Pitch\",\"Couple\",\"Balance\",\"Tension\",\"Tone\",\"Reso\"]},"
          "{\"key\":\"lfo2_rate\",\"name\":\"LFO2 Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"viz\":{\"group\":\"lfo2\",\"role\":\"rate\",\"kind\":\"lfo\"}},"
          "{\"key\":\"lfo2_depth\",\"name\":\"LFO2 Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"viz\":{\"group\":\"lfo2\",\"role\":\"depth\"}},"
          "{\"key\":\"lfo2_shape\",\"name\":\"LFO2 Shape\",\"type\":\"enum\",\"options\":[\"Sine\",\"Tri\",\"Saw\",\"Square\",\"S&H\"],\"viz\":{\"group\":\"lfo2\",\"role\":\"shape\"}},"
          "{\"key\":\"lfo2_target\",\"name\":\"LFO2 Target\",\"type\":\"enum\",\"options\":[\"Off\",\"Cutoff\",\"Pitch\",\"Couple\",\"Balance\",\"Tension\",\"Tone\",\"Reso\"]},"
          "{\"key\":\"at_preset\",\"name\":\"AT Preset\",\"type\":\"enum\",\"options\":[\"Off\",\"Gentle\",\"Brighten\",\"Bow\",\"Swell\",\"Vibrato\",\"Expressive\",\"Cello\",\"Wild\",\"Sforzato\"]},"
          "{\"key\":\"at_bright\",\"name\":\"AT Bright\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"at_bow\",\"name\":\"AT Bow\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"at_cutoff\",\"name\":\"AT Cutoff\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"at_vib\",\"name\":\"AT Vib\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"at_bend\",\"name\":\"AT Bend\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"at_vrate\",\"name\":\"AT Vib Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
          "{\"key\":\"at_curve\",\"name\":\"AT Curve\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01}"
          "]");
    }

    if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, buf_len,
          "{\"modes\":null,\"levels\":{"
          "\"root\":{\"name\":\"Fizzik\",\"knobs\":[\"preset\",\"rnd_patch\",\"rnd_exc\",\"rnd_reson\",\"cutoff\",\"resonance\",\"ftype\",\"voicing\"],"
          "\"params\":[\"preset\",\"rnd_patch\",\"rnd_exc\",\"rnd_reson\",\"cutoff\",\"resonance\",\"ftype\",\"voicing\",{\"level\":\"Exciter\",\"label\":\"Exciter\"},{\"level\":\"ResonA\",\"label\":\"Reson A\"},{\"level\":\"ResonB\",\"label\":\"Reson B\"},{\"level\":\"Voice\",\"label\":\"Voice\"},{\"level\":\"FX\",\"label\":\"FX\"},{\"level\":\"FX2\",\"label\":\"FX 2\"},{\"level\":\"Mod\",\"label\":\"Mod\"},{\"level\":\"Touch\",\"label\":\"Aftertouch\"}]},"
          "\"Exciter\":{\"name\":\"Exciter\",\"knobs\":[\"exc_mix\",\"exc_crackle\",\"exc_color\",\"exc_attack\",\"exc_decay\",\"exc_reso\",\"vel_level\",\"vel_color\"],\"params\":[\"exc_mix\",\"exc_crackle\",\"exc_color\",\"exc_attack\",\"exc_decay\",\"exc_reso\",\"vel_level\",\"vel_color\"]},"
          "\"ResonA\":{\"name\":\"Reson A\",\"knobs\":[\"a_model\",\"a_struct\",\"a_decay\",\"a_damp\",\"a_pos\",\"a_tone\",\"a_tune\",\"a_tension\"],\"params\":[\"a_model\",\"a_struct\",\"a_decay\",\"a_damp\",\"a_pos\",\"a_tone\",\"a_tune\",\"a_tension\"]},"
          "\"ResonB\":{\"name\":\"Reson B\",\"knobs\":[\"b_model\",\"b_struct\",\"b_decay\",\"b_damp\",\"b_pos\",\"b_tone\",\"b_tune\",\"b_tension\"],\"params\":[\"b_model\",\"b_struct\",\"b_decay\",\"b_damp\",\"b_pos\",\"b_tone\",\"b_tune\",\"b_tension\"]},"
          "\"Voice\":{\"name\":\"Voice\",\"knobs\":[\"couple\",\"balance\",\"glide\",\"amp_attack\",\"amp_release\",\"spread\",\"drive\",\"level\"],\"params\":[\"couple\",\"balance\",\"glide\",\"amp_attack\",\"amp_release\",\"spread\",\"drive\",\"level\"]},"
          "\"FX\":{\"name\":\"FX\",\"knobs\":[\"rev_mix\",\"rev_size\",\"rev_damp\",\"dly_mix\",\"dly_time\",\"dly_fb\",\"dly_tone\",\"width\"],\"params\":[\"rev_mix\",\"rev_size\",\"rev_damp\",\"dly_mix\",\"dly_time\",\"dly_fb\",\"dly_tone\",\"width\"]},"
          "\"FX2\":{\"name\":\"FX 2\",\"knobs\":[\"eq_tone\",\"eq_body\",\"cho_mix\",\"cho_rate\",\"cho_depth\",\"comp_amt\",\"lim_drive\",\"lim_ceil\"],\"params\":[\"eq_tone\",\"eq_body\",\"cho_mix\",\"cho_rate\",\"cho_depth\",\"comp_amt\",\"lim_drive\",\"lim_ceil\"]},"
          "\"Mod\":{\"name\":\"Mod\",\"knobs\":[\"lfo1_rate\",\"lfo1_depth\",\"lfo1_shape\",\"lfo1_target\",\"lfo2_rate\",\"lfo2_depth\",\"lfo2_shape\",\"lfo2_target\"],\"params\":[\"lfo1_rate\",\"lfo1_depth\",\"lfo1_shape\",\"lfo1_target\",\"lfo2_rate\",\"lfo2_depth\",\"lfo2_shape\",\"lfo2_target\"]},"
          "\"Touch\":{\"name\":\"Aftertouch\",\"knobs\":[\"at_preset\",\"at_bright\",\"at_bow\",\"at_cutoff\",\"at_vib\",\"at_bend\",\"at_vrate\",\"at_curve\"],\"params\":[\"at_preset\",\"at_bright\",\"at_bow\",\"at_cutoff\",\"at_vib\",\"at_bend\",\"at_vrate\",\"at_curve\"]}"
          "}}");
    }

    /* Knob overlay label/value (page-aware). */
    if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_name")) {
        int idx = atoi(key + 5) - 1;
        if (idx < 0 || idx >= PAGE_NKNOBS[inst->current_page]) return 0;
        const char *pk = PAGE_KEYS[inst->current_page][idx];
        if (strcmp(pk, "preset") == 0) return snprintf(buf, buf_len, "Preset");
        if (strcmp(pk, "rnd_patch") == 0) return snprintf(buf, buf_len, "Rnd Patch");
        if (strcmp(pk, "rnd_exc") == 0) return snprintf(buf, buf_len, "Rnd Exc");
        if (strcmp(pk, "rnd_reson") == 0) return snprintf(buf, buf_len, "Rnd Reson");
        const pdesc_t *d = find_pdesc(pk);
        return snprintf(buf, buf_len, "%s", d ? d->name : pk);
    }
    if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_value")) {
        int idx = atoi(key + 5) - 1;
        if (idx < 0 || idx >= PAGE_NKNOBS[inst->current_page]) return 0;
        const char *pk = PAGE_KEYS[inst->current_page][idx];
        if (strcmp(pk, "preset") == 0) return snprintf(buf, buf_len, "%s", PRESET_NAMES[inst->preset_idx]);
        if (strncmp(pk, "rnd_", 4) == 0) return snprintf(buf, buf_len, "0");
        if (strcmp(pk, "a_model") == 0) return snprintf(buf, buf_len, "%s", MODEL_NAMES[inst->p.a_model & 3]);
        if (strcmp(pk, "b_model") == 0) return snprintf(buf, buf_len, "%s", MODEL_NAMES[inst->p.b_model & 3]);
        if (strcmp(pk, "ftype")   == 0) return snprintf(buf, buf_len, "%s", FTYPE_NAMES[inst->p.flt_type % N_FTYPE]);
        if (strcmp(pk, "voicing") == 0) return snprintf(buf, buf_len, "%s", VOICING_NAMES[inst->p.flt_voicing % N_VOICING]);
        if (strcmp(pk, "at_preset") == 0) return snprintf(buf, buf_len, "%s", AT_PRESET_NAMES[inst->p.at_preset % N_AT_PRESET]);
        if (strcmp(pk, "lfo1_shape") == 0) return snprintf(buf, buf_len, "%s", LFO_SHAPE_NAMES[inst->p.lfo1_shape % N_LFO_SHAPE]);
        if (strcmp(pk, "lfo2_shape") == 0) return snprintf(buf, buf_len, "%s", LFO_SHAPE_NAMES[inst->p.lfo2_shape % N_LFO_SHAPE]);
        if (strcmp(pk, "lfo1_target") == 0) return snprintf(buf, buf_len, "%s", LFO_TGT_NAMES[inst->p.lfo1_target % N_LFO_TGT]);
        if (strcmp(pk, "lfo2_target") == 0) return snprintf(buf, buf_len, "%s", LFO_TGT_NAMES[inst->p.lfo2_target % N_LFO_TGT]);
        const pdesc_t *d = find_pdesc(pk);
        if (d) {
            if (d->isint) return snprintf(buf, buf_len, "%d", *pi_ptr(inst, d));
            return snprintf(buf, buf_len, "%d%%", (int)(*pf_ptr(inst, d) * 100.0f));
        }
        return 0;
    }

    /* State serialize. */
    if (strcmp(key, "state") == 0) {
        int n = 0;
        if (buf_len < 128) return -1;   /* never truncate state */
        /* Preset FIRST (restore applies it before the individual params, which
         * then overwrite it — user tweaks survive), then the baked makeup gain
         * (not in PDESC; without it a restored patch keeps the default preset's
         * level compensation — up to ~26 dB off). */
        n += snprintf(buf + n, buf_len - n, "preset=%s\n", PRESET_NAMES[inst->preset_idx]);
        n += snprintf(buf + n, buf_len - n, "__makeup=%.5f\n", (double)inst->p.makeup);
        for (int i = 0; i < N_PDESC; i++) {
            if (n > buf_len - 48) break;   /* guard: never pass snprintf a negative size */
            const pdesc_t *d = &PDESC[i];
            if (d->isint) n += snprintf(buf + n, buf_len - n, "%s=%d\n", d->key, *pi_ptr(inst, d));
            else          n += snprintf(buf + n, buf_len - n, "%s=%.5f\n", d->key, (double)*pf_ptr(inst, d));
        }
        return n;
    }

    /* Enums by key -> string names. */
    if (strcmp(key, "a_model") == 0) return snprintf(buf, buf_len, "%s", MODEL_NAMES[inst->p.a_model & 3]);
    if (strcmp(key, "b_model") == 0) return snprintf(buf, buf_len, "%s", MODEL_NAMES[inst->p.b_model & 3]);
    if (strcmp(key, "ftype")   == 0) return snprintf(buf, buf_len, "%s", FTYPE_NAMES[inst->p.flt_type % N_FTYPE]);
    if (strcmp(key, "voicing") == 0) return snprintf(buf, buf_len, "%s", VOICING_NAMES[inst->p.flt_voicing % N_VOICING]);
    if (strcmp(key, "at_preset") == 0) return snprintf(buf, buf_len, "%s", AT_PRESET_NAMES[inst->p.at_preset % N_AT_PRESET]);
    if (strcmp(key, "lfo1_shape") == 0) return snprintf(buf, buf_len, "%s", LFO_SHAPE_NAMES[inst->p.lfo1_shape % N_LFO_SHAPE]);
    if (strcmp(key, "lfo2_shape") == 0) return snprintf(buf, buf_len, "%s", LFO_SHAPE_NAMES[inst->p.lfo2_shape % N_LFO_SHAPE]);
    if (strcmp(key, "lfo1_target") == 0) return snprintf(buf, buf_len, "%s", LFO_TGT_NAMES[inst->p.lfo1_target % N_LFO_TGT]);
    if (strcmp(key, "lfo2_target") == 0) return snprintf(buf, buf_len, "%s", LFO_TGT_NAMES[inst->p.lfo2_target % N_LFO_TGT]);
    if (strcmp(key, "preset")  == 0) return snprintf(buf, buf_len, "%s", PRESET_NAMES[inst->preset_idx]);
    if (strncmp(key, "rnd_", 4) == 0) return snprintf(buf, buf_len, "0");   /* int trigger idle (Aphex-style) */

    /* Regular fields: raw values (raw for round-trip persistence). */
    const pdesc_t *d = find_pdesc(key);
    if (d) {
        if (d->isint) return snprintf(buf, buf_len, "%d", *pi_ptr(inst, d));
        return snprintf(buf, buf_len, "%.5f", (double)*pf_ptr(inst, d));
    }

    return -1;
}

/* ── Audio ───────────────────────────────────────────────────────────────────── */

static void render_block(void *instance, int16_t *out_lr, int frames) {
    fizzik_t *inst = (fizzik_t *)instance;
    params_t *p = &inst->p;   /* targets; discrete params (models/tunes/enums) read here */

    /* Analog-style 20 ms smoothing of every continuous parameter. Discrete
     * (int) params are copied straight through. `s` = smoothed shadow. */
    const float scoef = 0.135f;
    for (int i = 0; i < N_PDESC; i++) {
        const pdesc_t *d = &PDESC[i];
        if (d->isint) *(int *)((char *)&inst->sm + d->off) = *(int *)((char *)&inst->p + d->off);
        else {
            float *sv = (float *)((char *)&inst->sm + d->off);
            *sv += scoef * (*(float *)((char *)&inst->p + d->off) - *sv);
        }
    }
    params_t *s = &inst->sm;

    /* Map global params once per block (continuous from `s`, discrete from `p`). */
    float couple      = clampf(s->couple, 0.0f, 1.0f);
    float balance     = clampf(s->balance, 0.0f, 1.0f);
    float glide_ms    = map_exp(s->glide + 1e-4f, 1.0f, 500.0f) * (s->glide < 1e-3f ? 0.0f : 1.0f);
    float glide_coef  = (glide_ms > 0.5f) ? expf(-1.0f / (glide_ms * 0.001f * SR)) : 0.0f;
    float amp_atk_ms  = map_exp(s->amp_attack, 0.5f, 200.0f);
    float amp_rel_ms  = map_exp(s->amp_release, 20.0f, 3000.0f);
    float atk_inc     = 1.0f / (amp_atk_ms * 0.001f * SR);
    float rel_coef    = expf(-1.0f / (amp_rel_ms * 0.001f * SR));
    float level_gain  = s->level * s->level * 0.525f * p->makeup;  /* 0.75x master + headroom */
    /* Global filter. */
    float flt_fc   = map_exp(clampf(s->flt_cutoff, 0.0f, 1.0f), 30.0f, 18000.0f);
    float flt_g    = tanf(PI * clampf(flt_fc, 20.0f, 19500.0f) * SR_INV);
    float flt_res  = clampf(s->flt_reso, 0.0f, 1.0f);
    int   flt_type = p->flt_type % N_FTYPE;
    int   flt_voi  = p->flt_voicing % N_VOICING;
    /* FX params. */
    float rev_mix   = clampf(s->rev_mix, 0.0f, 1.0f);
    float rev_fb    = 0.60f + 0.38f * clampf(s->rev_size, 0.0f, 1.0f);   /* 0.60..0.98 */
    float rev_damp  = 0.05f + 0.60f * clampf(s->rev_damp, 0.0f, 1.0f);   /* 0.05..0.65 */
    float dly_mix   = clampf(s->dly_mix, 0.0f, 1.0f);
    float dly_fb    = clampf(s->dly_fb, 0.0f, 1.0f) * 0.85f;
    float dly_cut   = map_exp(clampf(s->dly_tone, 0.0f, 1.0f), 800.0f, 12000.0f);
    float dly_lpc   = 1.0f - expf(-TWO_PI * dly_cut * SR_INV);
    float dly_target = clampf(map_exp(s->dly_time, 30.0f, 700.0f) * 0.001f * SR, 64.0f, (float)(DLY_MAX - 4));
    float drive     = clampf(s->drive, 0.0f, 1.0f);
    float drive_g   = 1.0f + drive * 4.0f;
    float drive_cmp = 1.0f / (1.0f + drive * 1.2f);
    float width     = clampf(s->width, 0.0f, 1.0f);

    /* Exciter param mapping. */
    float exc_mix    = clampf(s->exc_mix, 0.0f, 1.0f);
    float exc_crk    = clampf(s->exc_crackle, 0.0f, 1.0f);
    float exc_atk_ms = map_exp(s->exc_attack, 0.2f, 40.0f);
    float exc_dec_ms = map_exp(s->exc_decay, 2.0f, 400.0f);
    float exc_reso   = clampf(s->exc_reso, 0.0f, 1.0f);
    float vel_level  = clampf(s->vel_level, 0.0f, 1.0f);
    float vel_color  = clampf(s->vel_color, 0.0f, 1.0f);

    /* ── Two LFOs → global mod targets ── */
    float lfo1v = lfo_block(&inst->lfo1, map_exp(s->lfo1_rate + 1e-4f, 0.05f, 20.0f), p->lfo1_shape, frames) * s->lfo1_depth;
    float lfo2v = lfo_block(&inst->lfo2, map_exp(s->lfo2_rate + 1e-4f, 0.05f, 20.0f), p->lfo2_shape, frames) * s->lfo2_depth;
    float mod_cut=0, mod_res=0, mod_cpl=0, mod_bal=0, mod_pit=0, mod_ten=0, mod_ton=0;
    for (int li = 0; li < 2; li++) {
        float lv = li ? lfo2v : lfo1v; int tg = li ? p->lfo2_target : p->lfo1_target;
        switch (tg) {
            case 1: mod_cut += lv * 0.5f; break;   case 2: mod_pit += lv * 4.0f; break;
            case 3: mod_cpl += lv * 0.5f; break;   case 4: mod_bal += lv * 0.5f; break;
            case 5: mod_ten += lv * 0.5f; break;   case 6: mod_ton += lv * 0.5f; break;
            case 7: mod_res += lv * 0.5f; break;   default: break;
        }
    }
    /* ── Aftertouch depths + peak pressure (drives the global cutoff mod) ── */
    float at_br=s->at_bright, at_bw=s->at_bow, at_ct=s->at_cutoff, at_vb=s->at_vib, at_bd=s->at_bend;
    float at_vhz=map_exp(s->at_vrate + 1e-4f, 3.0f, 9.0f), at_cv=s->at_curve;
    float maxpress = 0.0f;
    for (int vi = 0; vi < MAX_VOICES; vi++)
        if (inst->v[vi].active && inst->v[vi].pressure_sm > maxpress) maxpress = inst->v[vi].pressure_sm;

    /* Fold global mods into filter / couple / balance. */
    couple  = clampf(couple  + mod_cpl, 0.0f, 1.0f);
    balance = clampf(balance + mod_bal, 0.0f, 1.0f);
    flt_fc  = map_exp(clampf(s->flt_cutoff + mod_cut + at_ct * maxpress, 0.0f, 1.0f), 30.0f, 18000.0f);
    flt_g   = tanf(PI * clampf(flt_fc, 20.0f, 19500.0f) * SR_INV);
    flt_res = clampf(flt_res + mod_res, 0.0f, 1.0f);

    /* ── Master FX2 param mapping ── */
    float eq_tone = clampf(s->eq_tone, 0.0f, 1.0f), eq_body = clampf(s->eq_body, 0.0f, 1.0f);
    float cho_mix = clampf(s->cho_mix, 0.0f, 1.0f);
    float cho_inc = map_exp(s->cho_rate + 1e-4f, 0.05f, 6.0f) * SR_INV;
    float cho_base = 0.012f * SR, cho_amp = clampf(s->cho_depth, 0.0f, 1.0f) * 0.006f * SR;
    float comp_amt = clampf(s->comp_amt, 0.0f, 1.0f);
    float comp_thr = 1.0f - comp_amt * 0.7f, comp_mkup = 1.0f + comp_amt * 1.2f;
    float lim_drv  = 1.0f + clampf(s->lim_drive, 0.0f, 1.0f) * 3.0f;
    float lim_ceil = 0.5f + clampf(s->lim_ceil, 0.0f, 1.0f) * 0.49f;
    /* Release 150 ms: at the old 60 ms the gain recovered WITHIN a bass
     * period during sustained limiting — heard as distortion on chords. */
    float lim_att = expf(-1.0f / (0.001f * SR)), lim_rel = expf(-1.0f / (0.15f * SR));

    /* Polyphony compensation: single notes are calibrated to peak ~0.21, so a
     * raw 4-6 voice sum reaches 0.7-2.0 — parked the whole back end (soft clip
     * + brickwall) in constant heavy gain reduction, i.e. chords distorted in
     * every voicing. Scale the voice bus by n_eff^-0.35 (n_eff = summed amp
     * envelopes, so releasing tails count fractionally): 1 voice = unity, a
     * 4-voice chord sits ~4 dB down — under the ceiling, limiter back to a
     * guard. Smoothed ~45 ms below so note on/off never steps the gain. */
    float env_sum = 0.0f;
    for (int vi = 0; vi < MAX_VOICES; vi++)
        if (inst->v[vi].active) env_sum += inst->v[vi].amp_env;
    float n_eff = (env_sum < 1.0f) ? 1.0f : (env_sum > (float)MAX_VOICES ? (float)MAX_VOICES : env_sum);
    float pg_target = powf(n_eff, -0.35f);

    int rebuild_budget = 2;   /* max expensive modal rebuilds this block (see reso_set) */

    for (int n = 0; n < frames; n++) {
        float mixL = 0.0f, mixR = 0.0f;

        for (int vi = 0; vi < MAX_VOICES; vi++) {
            voice_t *v = &inst->v[vi];
            if (!v->active) continue;

            /* Smooth pad pressure (~28 ms). */
            v->pressure_sm += 0.0008f * (v->pressure - v->pressure_sm);

            /* Glide toward target. */
            if (glide_coef > 0.0f) v->freq += (v->freq_target - v->freq) * (1.0f - glide_coef);
            else                   v->freq = v->freq_target;

            /* Per-block resonator retune (with aftertouch + LFO mods). */
            if (n == 0) {
                float pr = v->pressure_sm;
                pr = (at_cv < 0.5f) ? powf(pr, 1.0f + (0.5f - at_cv) * 3.0f)
                                    : powf(pr, 1.0f / (1.0f + (at_cv - 0.5f) * 3.0f));
                v->vib_phase += at_vhz * (float)frames * SR_INV;
                if (v->vib_phase >= 1.0f) v->vib_phase -= 1.0f;
                float vib = sinf(TWO_PI * v->vib_phase) * at_vb * pr;
                float semi = mod_pit + at_bd * pr * 2.0f + vib;          /* bend up to 2 st, vib up to 1 st */
                float pfac = powf(2.0f, semi / 12.0f);
                float fA = v->freq * pfac * powf(2.0f, (float)p->a_tune / 12.0f);
                float fB = v->freq * pfac * powf(2.0f, (float)p->b_tune / 12.0f);
                float aTone = clampf(s->a_tone + mod_ton + at_br * pr * 0.5f, 0.0f, 1.0f);
                float bTone = clampf(s->b_tone + mod_ton + at_br * pr * 0.5f, 0.0f, 1.0f);
                float aTens = clampf(s->a_tension + mod_ten, 0.0f, 1.0f);
                float bTens = clampf(s->b_tension + mod_ten, 0.0f, 1.0f);
                reso_set(&v->A, p->a_model & 3, fA, s->a_struct, s->a_decay, s->a_damp, s->a_pos, aTone, aTens, &rebuild_budget);
                reso_set(&v->B, p->b_model & 3, fB, s->b_struct, s->b_decay, s->b_damp, s->b_pos, bTone, bTens, &rebuild_budget);
                v->_pr_shaped = pr;
            }

            /* Exciter (velocity links) + aftertouch bow re-excitation. */
            float vcolor = clampf(s->exc_color + vel_color * (v->velocity - 0.5f), 0.0f, 1.0f);
            float color_cut = map_exp(vcolor, 300.0f, 16000.0f);
            float exc = exciter_process(&v->exc, v->freq, exc_mix, exc_crk, color_cut, exc_reso, exc_atk_ms, exc_dec_ms);
            float vgain = 1.0f - vel_level * (1.0f - v->velocity);
            exc *= vgain;
            exc += randbi(&v->bow_rng) * at_bw * v->_pr_shaped * 0.35f;   /* bow */

            /* Coupled resonators (1-sample cross feedback). */
            float inA = exc + couple * v->prevB;
            float inB = exc + couple * v->prevA;
            float rawA = reso_process(&v->A, inA);
            float rawB = reso_process(&v->B, inB);
            float gA = soft_limit(dcblk(&v->dcA, sanitize(rawA)));
            float gB = soft_limit(dcblk(&v->dcB, sanitize(rawB)));
            v->prevA = gA; v->prevB = gB;

            float mixed = (1.0f - balance) * gA + balance * gB;

            /* Amp envelope. */
            switch (v->amp_stage) {
                case 0: v->amp_env += atk_inc; if (v->amp_env >= 1.0f) { v->amp_env = 1.0f; v->amp_stage = 1; } break;
                case 1: break;
                case 2: v->amp_env *= rel_coef; if (v->amp_env < 0.0003f) { v->amp_env = 0.0f; } break;
                default: break;
            }

            float vout = mixed * v->amp_env;

            /* Silence detection to free the voice (only after release). */
            float a = fabsf(vout);
            if (v->amp_stage == 2 && a < 0.0008f) { if (++v->silent > 2205) v->active = 0; }
            else v->silent = 0;

            mixL += vout * v->pan_l;
            mixR += vout * v->pan_r;
        }

        /* Polyphony compensation (pre-filter, so the voicings' input drive
         * saturation sees the same level poly as mono). */
        inst->poly_gain += 0.0005f * (pg_target - inst->poly_gain);
        mixL *= inst->poly_gain;
        mixR *= inst->poly_gain;

        /* Global multimode filter (post-synth, pre-FX). */
        mixL = filter_process(&inst->fltL, mixL, flt_g, flt_res, flt_type, flt_voi);
        mixR = filter_process(&inst->fltR, mixR, flt_g, flt_res, flt_type, flt_voi);

        /* Drive — dry/wet blend so it fades in from 0 (no click at engage). */
        if (drive > 0.0001f) {
            mixL += drive * (ftanh(mixL * drive_g) * drive_cmp - mixL);
            mixR += drive * (ftanh(mixR * drive_g) * drive_cmp - mixR);
        }

        /* Tilt/body EQ. */
        mixL = eq_process(mixL, &inst->eq_lpL, &inst->eq_lp2L, eq_tone, eq_body);
        mixR = eq_process(mixR, &inst->eq_lpR, &inst->eq_lp2R, eq_tone, eq_body);

        /* Stereo chorus (modulated short delays, L/R phase-offset for width). */
        if (cho_mix > 0.001f) {
            inst->cho_phase += cho_inc; if (inst->cho_phase >= 1.0f) inst->cho_phase -= 1.0f;
            float ph = inst->cho_phase;
            float dL = cho_base + cho_amp * (0.5f + 0.5f * sinf(TWO_PI * ph));
            float dR = cho_base + cho_amp * (0.5f + 0.5f * sinf(TWO_PI * (ph + 0.25f)));
            float rpL = (float)inst->cho_pos - dL; while (rpL < 0.0f) rpL += CHO_MAX;
            float rpR = (float)inst->cho_pos - dR; while (rpR < 0.0f) rpR += CHO_MAX;
            int iL = (int)rpL, iR = (int)rpR;
            float fL = rpL - iL, fR = rpR - iR;
            int iL1 = (iL + 1) % CHO_MAX, iR1 = (iR + 1) % CHO_MAX;
            float wL = inst->choL[iL] + fL * (inst->choL[iL1] - inst->choL[iL]);
            float wR = inst->choR[iR] + fR * (inst->choR[iR1] - inst->choR[iR]);
            inst->choL[inst->cho_pos] = mixL; inst->choR[inst->cho_pos] = mixR;
            inst->cho_pos++; if (inst->cho_pos >= CHO_MAX) inst->cho_pos = 0;
            mixL += cho_mix * (wL - mixL);
            mixR += cho_mix * (wR - mixR);
        }

        /* Stereo ping-pong delay (always runs to keep the tail continuous). */
        inst->dly_time_smooth += 0.0006f * (dly_target - inst->dly_time_smooth);
        float rp = (float)inst->dly_pos - inst->dly_time_smooth;
        while (rp < 0.0f) rp += DLY_MAX;
        int di0 = (int)rp; float dfr = rp - (float)di0;
        int di1 = di0 + 1; if (di1 >= DLY_MAX) di1 -= DLY_MAX;
        float dwL = inst->dlyL[di0] + dfr * (inst->dlyL[di1] - inst->dlyL[di0]);
        float dwR = inst->dlyR[di0] + dfr * (inst->dlyR[di1] - inst->dlyR[di0]);
        inst->dly_lpL += dly_lpc * (dwL - inst->dly_lpL) + 1e-20f;
        inst->dly_lpR += dly_lpc * (dwR - inst->dly_lpR) + 1e-20f;
        inst->dlyL[inst->dly_pos] = mixL + inst->dly_lpR * dly_fb;   /* cross = ping-pong */
        inst->dlyR[inst->dly_pos] = mixR + inst->dly_lpL * dly_fb;
        inst->dly_pos++; if (inst->dly_pos >= DLY_MAX) inst->dly_pos = 0;
        float sigL = mixL + dwL * dly_mix;
        float sigR = mixR + dwR * dly_mix;

        /* Reverb (mono send, stereo return). HPF ~25 Hz on the send: at fb 0.98
         * the comb DC gain is ~50x, so any DC leak would pump the tail. */
        if (rev_mix > 0.001f) {
            float send = (sigL + sigR) * 0.5f;
            inst->rev_send_lp += 0.0035f * (send - inst->rev_send_lp) + 1e-20f;
            send -= inst->rev_send_lp;
            float wL = reverb_ch(&inst->rvL, send, rev_fb, rev_damp);
            float wR = reverb_ch(&inst->rvR, send, rev_fb, rev_damp);
            sigL += wL * rev_mix;
            sigR += wR * rev_mix;
        }

        /* Stereo width (M/S). */
        if (fabsf(width - 0.5f) > 0.001f) {
            float mid = (sigL + sigR) * 0.5f;
            float sid = (sigL - sigR) * 0.5f * (width * 2.0f);
            sigL = mid + sid; sigR = mid - sid;
        }

        /* Glue compressor (feedforward, stereo-linked, ~4:1). */
        if (comp_amt > 0.001f) {
            float pk = fabsf(sigL) > fabsf(sigR) ? fabsf(sigL) : fabsf(sigR);
            inst->comp_env += ((pk > inst->comp_env) ? 0.01f : 0.0006f) * (pk - inst->comp_env);
            float cg = comp_mkup;
            if (inst->comp_env > comp_thr)
                cg *= (comp_thr + (inst->comp_env - comp_thr) * 0.25f) / (inst->comp_env + 1e-9f);
            sigL *= cg; sigR *= cg;
        }

        /* Randomize duck state machine: fade out (~18 ms) -> hold silent (~22 ms)
         * while the deferred randomize is applied and its transient settles ->
         * fade back in (~70 ms). Long, fully-masked crossfade = no click. */
        if (inst->rnd_phase == 1) {
            inst->rnd_gain += 0.005f * (0.0f - inst->rnd_gain);
            if (inst->rnd_gain < 0.02f) {
                inst->rnd_gain = 0.0f;
                apply_pending_rnd(inst);
                inst->rnd_hold = 960;                            /* ~22 ms of silence */
                inst->rnd_phase = 3;
            }
        } else if (inst->rnd_phase == 3) {
            inst->rnd_gain = 0.0f;
            if (--inst->rnd_hold <= 0) inst->rnd_phase = 2;
        } else if (inst->rnd_phase == 2) {
            inst->rnd_gain += 0.0014f * (1.0f - inst->rnd_gain);
            if (inst->rnd_gain > 0.995f) { inst->rnd_gain = 1.0f; inst->rnd_phase = 0; }
        }
        float rgain = inst->rnd_gain;

        float outL = sigL * level_gain * rgain;
        float outR = sigR * level_gain * rgain;

        /* Hidden pre-clamp metering for offline calibration. */
        float ap = fabsf(outL) > fabsf(outR) ? fabsf(outL) : fabsf(outR);
        if (ap > inst->meter_peak) inst->meter_peak = ap;
        inst->meter_sumsq += (double)outL * outL + (double)outR * outR;
        inst->meter_cnt += 2;

        /* Warm tanh soft-clip, then a lookahead brickwall limiter. */
        outL = out_limit(outL);
        outR = out_limit(outR);
        float dL = outL * lim_drv, dR = outR * lim_drv;
        float lpk = fabsf(dL) > fabsf(dR) ? fabsf(dL) : fabsf(dR);
        float ltarget = (lpk > lim_ceil && lpk > 0.0f) ? lim_ceil / lpk : 1.0f;
        if (ltarget < inst->lim_gain) inst->lim_gain = lim_att * inst->lim_gain + (1.0f - lim_att) * ltarget;
        else                          inst->lim_gain = lim_rel * inst->lim_gain + (1.0f - lim_rel) * ltarget;
        float delL = inst->limL[inst->lim_pos], delR = inst->limR[inst->lim_pos];
        inst->limL[inst->lim_pos] = dL; inst->limR[inst->lim_pos] = dR;
        inst->lim_pos++; if (inst->lim_pos >= LIM_LA) inst->lim_pos = 0;
        outL = clampf(delL * inst->lim_gain, -lim_ceil, lim_ceil);
        outR = clampf(delR * inst->lim_gain, -lim_ceil, lim_ceil);
        int32_t sl = (int32_t)(clampf(outL, -1.0f, 1.0f) * 32767.0f);
        int32_t sr = (int32_t)(clampf(outR, -1.0f, 1.0f) * 32767.0f);
        out_lr[n * 2]     = (int16_t)sl;
        out_lr[n * 2 + 1] = (int16_t)sr;
    }
}

/* ── API v2 export ───────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t api_version;
    void* (*create_instance)(const char *, const char *);
    void  (*destroy_instance)(void *);
    void  (*on_midi)(void *, const uint8_t *, int, int);
    void  (*set_param)(void *, const char *, const char *);
    int   (*get_param)(void *, const char *, char *, int);
    int   (*get_error)(void *, char *, int);
    void  (*render_block)(void *, int16_t *, int);
} plugin_api_v2_t;

plugin_api_v2_t* move_plugin_init_v2(const void *host) {
    (void)host;
    static plugin_api_v2_t api = {
        .api_version      = 2,
        .create_instance  = create_instance,
        .destroy_instance = destroy_instance,
        .on_midi          = on_midi,
        .set_param        = set_param,
        .get_param        = get_param,
        .get_error        = NULL,
        .render_block     = render_block,
    };
    return &api;
}
