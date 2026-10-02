/**
 * Denis — Schwung sound generator
 * Author: Vincent Fillion
 * License: MIT
 * Architecture: Monophonic, West Coast synthesis
 *
 * Inspired by Serge Modular La Bestia III:
 *   Complex Oscillator | NTO | SVF Filter (LP/BP/HP/Notch) | GTO+ | Wave Multiplier
 *
 * API: plugin_api_v2_t
 * Audio: 44100Hz, 128 frames/block, stereo interleaved int16 output
 *
 * Parameter categories (jog-wheel navigation):
 *   1. Oscillators — Osc1 Freq, Timbre, Osc2 Freq, Harmonics, Osc Mix, Noise Mix, Noise Type, Cutoff
 *   2. Control     — Fold Depth, Fold Type, Cutoff, Q, ADSR, Filter Type, Vel>Filter
 *   3. Modulation  — LFO Rate, S&H Rate, Env/Nz Depth, Rnd, Preset, Pad Mode, Portamento, Legato
 *
 * Modulation matrix: 4 rows (sources) x 8 columns (targets) = 32 pads
 *   Row 0: Envelope (ADSR output, bipolar -1..+1)
 *   Row 1: GTO Smooth (triangle LFO, 0.01-500Hz)
 *   Row 2: GTO Stepped (S&H random)
 *   Row 3: Filtered Noise (smooth random movement)
 *   Col 0-7: OSC1 Pitch, OSC1 Timbre, OSC2 Pitch, OSC2 Harmonics,
 *            Fold Depth, Fold Type, Filter Cutoff, Output Level
 *   Matrix is bipolar (-1..+1). Pad patching adds positive (velocity-based) on top.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#define SAMPLE_RATE   44100.0f
#define FRAMES_PER_BLOCK 128
#define PI            3.14159265359f
#define TWO_PI        6.28318530718f
#define DENORMAL_GUARD 1e-20f
#define NUM_PRESETS   30

/* ── Modulation matrix dimensions ────────────────────────────────────────────── */

#define MOD_ROWS 4
#define MOD_COLS 8

/* Source rows */
#define SRC_ENVELOPE     0
#define SRC_GTO_SMOOTH   1
#define SRC_GTO_STEPPED  2
#define SRC_NOISE        3

/* Target columns */
#define TGT_OSC1_PITCH    0
#define TGT_OSC1_TIMBRE   1
#define TGT_OSC2_PITCH    2
#define TGT_OSC2_HARMONICS 3
#define TGT_FOLD_DEPTH    4
#define TGT_FOLD_TYPE     5
#define TGT_FILTER_CUTOFF 6
#define TGT_OUTPUT_LEVEL  7

/* Move pad MIDI note mapping: pads 68-99, 4 rows x 8 cols, bottom-left to top-right */
#define PAD_NOTE_BASE 68

/* ── PolyBLEP residual for anti-aliased waveform discontinuities ─────────── */

static float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* ── Complex Oscillator (waveform morphing: sine->tri->saw->pulse) ───────── */

typedef struct {
    float phase;
    float freq;
} complex_osc_t;

static float complex_osc_sample(complex_osc_t *osc, float freq_hz, float timbre) {
    osc->freq = freq_hz;
    float dt = freq_hz / SAMPLE_RATE;
    osc->phase += dt;
    if (osc->phase >= 1.0f) osc->phase -= 1.0f;
    if (osc->phase < 0.0f) osc->phase += 1.0f;

    float p = osc->phase;
    float sine = sinf(p * TWO_PI);

    float tri;
    if (p < 0.25f)      tri = p * 4.0f;
    else if (p < 0.75f) tri = 2.0f - p * 4.0f;
    else                tri = p * 4.0f - 4.0f;

    float saw = 2.0f * p - 1.0f;
    saw -= poly_blep(p, dt);

    float pulse = (p < 0.5f) ? 1.0f : -1.0f;
    pulse -= poly_blep(p, dt);
    float p2 = p + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    pulse += poly_blep(p2, dt);

    /* Morph: 0=sine, 0.33=tri, 0.67=saw, 1.0=pulse */
    float out;
    if (timbre < 0.33f) {
        float blend = timbre / 0.33f;
        out = sine * (1.0f - blend) + tri * blend;
    } else if (timbre < 0.67f) {
        float blend = (timbre - 0.33f) / 0.34f;
        out = tri * (1.0f - blend) + saw * blend;
    } else {
        float blend = (timbre - 0.67f) / 0.33f;
        out = saw * (1.0f - blend) + pulse * blend;
    }
    return out * 0.8f;
}

/* ── NTO (New Timbral Oscillator — harmonic selection) ───────────────────── */

typedef struct {
    float phase;
    float freq;
} nto_t;

static float nto_sample(nto_t *nto, float freq_hz, float harmonics) {
    nto->freq = freq_hz;
    nto->phase += freq_hz / SAMPLE_RATE;
    if (nto->phase >= 1.0f) nto->phase -= 1.0f;
    if (nto->phase < 0.0f) nto->phase += 1.0f;

    float pa = nto->phase * TWO_PI;
    float out;

    if (harmonics < 0.33f) {
        float blend = harmonics / 0.33f;
        out = sinf(pa)
            + blend * 0.3f * sinf(3.0f * pa) / 3.0f
            + blend * 0.15f * sinf(5.0f * pa) / 5.0f;
    } else if (harmonics < 0.67f) {
        float blend = (harmonics - 0.33f) / 0.34f;
        float base = sinf(pa)
            + 0.3f * sinf(3.0f * pa) / 3.0f
            + 0.15f * sinf(5.0f * pa) / 5.0f;
        float even = 0.25f * sinf(2.0f * pa) / 2.0f
            + 0.15f * sinf(4.0f * pa) / 4.0f
            + 0.1f * sinf(6.0f * pa) / 6.0f;
        out = base + blend * even;
    } else {
        float blend = (harmonics - 0.67f) / 0.33f;
        out = sinf(pa)
            + 0.3f * sinf(2.0f * pa) / 2.0f
            + 0.2f * sinf(4.0f * pa) / 4.0f
            + 0.15f * sinf(6.0f * pa) / 6.0f
            + blend * 0.1f * sinf(8.0f * pa) / 8.0f;
    }
    return out * 0.8f;
}

/* ── GTO+ Dual Slope Generator (split LFO + S&H, LFO up to 500Hz) ───────── */

typedef struct {
    float smooth_phase;
    float stepped_value;
    uint32_t step_counter;
    uint32_t rng_state;
} gto_plus_t;

static float gto_random(gto_plus_t *gto) {
    gto->rng_state = (gto->rng_state * 1103515245u + 12345u) & 0x7fffffffu;
    return 2.0f * ((float)gto->rng_state / (float)0x7fffffff) - 1.0f;
}

static void gto_tick(gto_plus_t *gto, float lfo_rate_hz, float sh_rate_hz,
                     float *smooth_out, float *stepped_out) {
    /* Smooth side: triangle LFO at lfo_rate (0.01-500Hz) */
    gto->smooth_phase += lfo_rate_hz / SAMPLE_RATE;
    if (gto->smooth_phase >= 1.0f) gto->smooth_phase -= 1.0f;

    float p = gto->smooth_phase;
    float tri;
    if (p < 0.25f)      tri = p * 4.0f;
    else if (p < 0.75f) tri = 2.0f - p * 4.0f;
    else                tri = p * 4.0f - 4.0f;
    *smooth_out = tri;

    /* Stepped side: S&H at sh_rate (independent from LFO) */
    uint32_t period = (uint32_t)(SAMPLE_RATE / fmaxf(sh_rate_hz, 0.01f));
    gto->step_counter++;
    if (gto->step_counter >= period) {
        gto->stepped_value = gto_random(gto);
        gto->step_counter = 0;
    }
    *stepped_out = gto->stepped_value;
}

/* ── Wave Multiplier — Serge Triple Waveshaper ───────────────────────────── */

static float bipolar_fold_transfer(float x) {
    float phase = x * 0.25f + 0.25f;
    phase = phase - floorf(phase);
    return 4.0f * fabsf(phase - 0.5f) - 1.0f;
}

static float west_coast_fold_transfer(float x) {
    float sign = x >= 0.0f ? 1.0f : -1.0f;
    float ax = fabsf(x);
    if (ax < 0.7f) {
        return x * 1.0f;
    } else if (ax < 1.0f) {
        float t = (ax - 0.7f) / 0.3f;
        float y = 0.7f + 0.3f * (1.0f - t * t);
        return sign * y;
    } else if (ax < 1.5f) {
        float t = (ax - 1.0f) / 0.5f;
        float y = 1.0f - 0.6f * sinf(t * PI * 0.5f);
        return sign * y;
    } else if (ax < 2.2f) {
        float t = (ax - 1.5f) / 0.7f;
        float y = 0.4f + 0.5f * sinf(t * PI * 0.5f);
        return sign * y;
    } else if (ax < 3.0f) {
        float t = (ax - 2.2f) / 0.8f;
        float y = 0.9f - 0.4f * t;
        return sign * y;
    } else {
        return sign * (0.5f + 0.3f / (1.0f + (ax - 3.0f)));
    }
}

/* ── ADAA: Anti-Derivative Anti-Aliasing ─────────────────────────────────── */

typedef struct {
    float x_prev;
    float F_prev;
} adaa_state_t;

static float adaa_process(adaa_state_t *state, float x, float (*transfer)(float)) {
    float dx = x - state->x_prev;
    float y;
    if (fabsf(dx) < 1e-5f) {
        y = transfer(x);
    } else {
        float f_curr = transfer(x);
        float f_prev = transfer(state->x_prev);
        float x_mid = (x + state->x_prev) * 0.5f;
        float f_mid = transfer(x_mid);
        y = (f_prev + 4.0f * f_mid + f_curr) / 6.0f;
    }
    state->x_prev = x;
    return y;
}

/* ── Serge Triple Waveshaper: 3 cascaded stages ──────────────────────────── */

static float fold_stage_upper(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static float fold_stage_middle(float x) {
    return west_coast_fold_transfer(x);
}

static float fold_stage_lower(float x) {
    float rect = fabsf(x);
    if (rect > 1.0f) {
        rect = bipolar_fold_transfer(rect);
        rect = fabsf(rect);
    }
    return (rect - 0.5f) * 2.0f;
}

typedef struct {
    adaa_state_t upper;
    adaa_state_t middle;
    adaa_state_t lower;
} wave_multiplier_t;

static float wave_multiplier_process(wave_multiplier_t *wm, float input,
                                     float depth, float fold_type) {
    if (depth < 0.001f) return input;

    float drive1 = 1.0f + depth * 4.0f;
    float s1 = adaa_process(&wm->upper, input * drive1, fold_stage_upper);

    float depth2 = fmaxf(0.0f, (depth - 0.15f) * 1.5f);
    depth2 = fminf(depth2, 1.0f);
    float drive2 = 1.0f + depth2 * 3.0f;
    float s2_raw = adaa_process(&wm->middle, s1 * drive2, fold_stage_middle);
    float s2 = s1 * (1.0f - depth2) + s2_raw * depth2;

    float depth3 = fmaxf(0.0f, (depth - 0.4f) * 1.8f);
    depth3 = fminf(depth3, 1.0f);
    float drive3 = 1.0f + depth3 * 2.5f;
    float s3_raw = adaa_process(&wm->lower, s2 * drive3, fold_stage_lower);
    float s3 = s2 * (1.0f - depth3) + s3_raw * depth3;

    float odd_weight, even_weight;
    if (fold_type < 0.5f) {
        odd_weight = 1.0f;
        even_weight = fold_type * 2.0f;
    } else {
        odd_weight = 2.0f * (1.0f - fold_type);
        even_weight = 1.0f;
    }

    float odd_path = s2;
    float even_path = s3;
    float out = odd_path * odd_weight * 0.5f + even_path * even_weight * 0.5f;
    float norm = 1.0f / (odd_weight * 0.5f + even_weight * 0.5f + 0.001f);
    out *= norm;

    float out2 = out * out;
    out = out * (27.0f + out2) / (27.0f + 9.0f * out2);
    return out;
}

/* ── SVF Filter (Serge Variable Q — LP/BP/HP/Notch) ─────────────────────── */
/*
 * Cytomic SVF (Andrew Simper TPT form) — unconditionally stable at all
 * frequencies and Q values. Provides simultaneous LP, BP, HP, Notch outputs.
 * Q range 0-4 (0%=flat, 100%=resonant, 400%=near self-oscillation)
 */

typedef struct {
    float ic1eq;
    float ic2eq;
} svf_t;

static float fast_tanh(float x) {
    if (x > 2.0f) return 1.0f;
    if (x < -2.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static float svf_process(svf_t *f, float input, float cutoff_hz, float q, int filter_type) {
    cutoff_hz = fmaxf(20.0f, fminf(20000.0f, cutoff_hz));
    q = fmaxf(0.0f, fminf(4.0f, q));

    float g = tanf(PI * fminf(cutoff_hz, SAMPLE_RATE * 0.49f) / SAMPLE_RATE);

    /* Q param 0-4 maps to SVF damping k: 2.0 (flat) → 0.1 (near self-osc) */
    float k = fmaxf(0.08f, 2.0f - q * 0.475f);

    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    float v3 = input - f->ic2eq;
    float v1 = a1 * f->ic1eq + a2 * v3;
    float v2 = f->ic2eq + a2 * f->ic1eq + a3 * v3;

    f->ic1eq = 2.0f * v1 - f->ic1eq + DENORMAL_GUARD;
    f->ic2eq = 2.0f * v2 - f->ic2eq + DENORMAL_GUARD;

    float lp = v2;
    float bp = v1;
    float hp = input - k * v1 - v2;
    float notch = input - k * v1;

    /* Gain compensation: LP/HP/Notch lose volume at high Q */
    float gc = 1.0f + q * 0.25f;

    switch (filter_type) {
        case 1:  return fast_tanh(bp);           /* Bandpass */
        case 2:  return fast_tanh(hp * gc);      /* Highpass */
        case 3:  return fast_tanh(notch * gc);   /* Notch */
        default: return fast_tanh(lp * gc);      /* Lowpass */
    }
}

/* ── ADSR Envelope ───────────────────────────────────────────────────────── */

typedef struct {
    float level;
    int   stage;  /* 0=attack 1=decay 2=sustain 3=release 4=off */
} adsr_t;

static float adsr_tick(adsr_t *env, float a, float d, float s, float r) {
    float rate;
    switch (env->stage) {
        case 0: /* attack — exponential curve (no click) */
            rate = 1.0f / fmaxf(a * SAMPLE_RATE, 1.0f);
            env->level += rate * (1.05f - env->level);
            if (env->level >= 0.99f) { env->level = 1.0f; env->stage = 1; }
            break;
        case 1: /* decay — linear */
            rate = (1.0f - s) / fmaxf(d * SAMPLE_RATE, 1.0f);
            env->level -= rate;
            if (env->level <= s) { env->level = s; env->stage = 2; }
            break;
        case 2: /* sustain */
            env->level = s;
            break;
        case 3: /* release — linear (fixed rate, not level-dependent) */
            rate = 1.0f / fmaxf(r * SAMPLE_RATE, 1.0f);
            env->level -= rate;
            if (env->level <= 0.001f) { env->level = 0.0f; env->stage = 4; }
            break;
        default:
            env->level = 0.0f;
            break;
    }
    return env->level;
}

/* ── Simple noise generators ─────────────────────────────────────────────── */

typedef struct {
    uint32_t rng;
    float pink_b0, pink_b1, pink_b2, pink_b3, pink_b4, pink_b5, pink_b6;
    float brown_state;
} noise_gen_t;

static inline float noise_white(noise_gen_t *n) {
    n->rng ^= n->rng << 13;
    n->rng ^= n->rng >> 17;
    n->rng ^= n->rng << 5;
    return (float)(int32_t)n->rng / 2147483648.0f;
}

static inline float noise_pink(noise_gen_t *n) {
    float w = noise_white(n);
    n->pink_b0 = 0.99886f * n->pink_b0 + w * 0.0555179f;
    n->pink_b1 = 0.99332f * n->pink_b1 + w * 0.0750759f;
    n->pink_b2 = 0.96900f * n->pink_b2 + w * 0.1538520f;
    n->pink_b3 = 0.86650f * n->pink_b3 + w * 0.3104856f;
    n->pink_b4 = 0.55000f * n->pink_b4 + w * 0.5329522f;
    n->pink_b5 = -0.7616f * n->pink_b5 - w * 0.0168980f;
    float pink = n->pink_b0 + n->pink_b1 + n->pink_b2 + n->pink_b3
               + n->pink_b4 + n->pink_b5 + n->pink_b6 + w * 0.5362f;
    n->pink_b6 = w * 0.115926f;
    return pink * 0.11f;
}

static inline float noise_brown(noise_gen_t *n) {
    float w = noise_white(n);
    n->brown_state += w * 0.02f;
    if (n->brown_state > 1.0f) n->brown_state = 1.0f;
    if (n->brown_state < -1.0f) n->brown_state = -1.0f;
    return n->brown_state;
}

/* ── Preset system ──────────────────────────────────────────────────────── */
/*
 * 30 presets with Québécois joual names.
 * Matrix is flat [32]: row*8+col, bipolar -1..+1.
 *   Row 0 (env):  [0..7]   = P1 Tm P2 Hr FD FT Cf Lv
 *   Row 1 (lfo):  [8..15]
 *   Row 2 (sh):   [16..23]
 *   Row 3 (nz):   [24..31]
 */

typedef struct {
    const char *name;
    /* Oscillators */
    float osc1_freq, osc1_timbre, osc2_pitch, osc2_harmonics;
    float osc_mix, noise_mix;
    int noise_type;
    /* Control */
    float fold_depth, fold_type, filter_cutoff, filter_q;
    int filter_type;
    float attack, decay, sustain, rel;
    /* Modulation */
    float lfo_rate, sh_rate, mod_depth_env, mod_depth_noise;
    float vel_to_filter, portamento;
    int legato;
    /* Matrix [32] flat */
    float mat[32];
} preset_t;

static const preset_t PRESETS[NUM_PRESETS] = {
    /* 0: Init — clean default */
    { "Init",
      0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 1,
      0.0f, 0.5f, 1.0f, 0.0f, 0, 0.01f, 0.3f, 0.7f, 0.2f,
      0.2f, 0.3f, 0.7f, 0.4f, 0.0f, 0.0f, 0,
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 1: Tabarnak — heavy aggressive wavefold lead */
    { "Tabarnak",
      0.5f, 0.8f, 0.0f, 0.7f, 0.3f, 0.1f, 0,
      0.85f, 0.3f, 0.45f, 2.5f, 1, 0.005f, 0.4f, 0.6f, 0.5f,
      2.5f, 1.2f, 0.9f, 0.3f, 0.6f, 0.0f, 0,
      {0,0,0,0, 0.4f,0,0.6f,0, 0,0.3f,0,0, 0,0,-0.2f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 2: Poutine Grasse — fat detuned bass */
    { "Poutine Grasse",
      0.45f, 0.35f, -1.0f, 0.2f, 0.6f, 0.05f, 2,
      0.15f, 0.5f, 0.35f, 1.0f, 0, 0.01f, 0.2f, 0.8f, 0.15f,
      0.3f, 0.15f, 0.6f, 0.2f, 0.4f, 0.0f, 0,
      {0,0,0,0, 0,0,0.5f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0.1f,0,0,0, 0,0,0,0} },

    /* 3: Elvis Gratton — cheesy over-the-top vibrato lead */
    { "Elvis Gratton",
      0.55f, 0.95f, 0.5f, 0.8f, 0.5f, 0.0f, 1,
      0.4f, 0.7f, 0.65f, 1.5f, 0, 0.02f, 0.5f, 0.5f, 0.3f,
      6.0f, 0.5f, 0.5f, 0.1f, 0.3f, 0.05f, 1,
      {0,0,0,0, 0,0,0.4f,0, 0.25f,0,0.2f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 4: Frette en Criss — cold icy highpass pad */
    { "Frette en Criss",
      0.5f, 0.1f, 0.5f, 0.9f, 0.4f, 0.15f, 0,
      0.1f, 0.5f, 0.3f, 2.0f, 2, 0.8f, 0.5f, 0.6f, 1.5f,
      0.08f, 0.2f, 0.3f, 0.5f, 0.0f, 0.2f, 1,
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,-0.15f, 0,0,0,0.3f, 0,0,0,0, 0,0,0,0, 0,0,0.2f,0} },

    /* 5: Slush Brune — dirty muddy texture */
    { "Slush Brune",
      0.48f, 0.6f, -0.5f, 0.4f, 0.55f, 0.25f, 2,
      0.3f, 0.4f, 0.25f, 1.2f, 0, 0.05f, 0.6f, 0.5f, 0.8f,
      0.15f, 0.08f, 0.4f, 0.6f, 0.2f, 0.0f, 0,
      {0,0,0,0, 0,0,0,0.2f, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, -0.15f,0,0.3f,0} },

    /* 6: Ti-Coune — thin nasal bandpass lead */
    { "Ti-Coune",
      0.5f, 0.4f, 1.0f, 0.6f, 0.2f, 0.0f, 1,
      0.0f, 0.5f, 0.5f, 3.5f, 1, 0.005f, 0.2f, 0.7f, 0.15f,
      5.0f, 0.5f, 0.5f, 0.1f, 0.3f, 0.03f, 1,
      {0,0,0,0, 0,0,0.3f,0, 0.15f,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 7: La Grande Seduction — massive full-spectrum drone */
    { "Grande Seduction",
      0.52f, 0.7f, -0.02f, 0.5f, 0.5f, 0.1f, 1,
      0.6f, 0.5f, 0.7f, 0.8f, 0, 0.5f, 0.8f, 0.7f, 1.0f,
      0.1f, 0.15f, 0.5f, 0.3f, 0.2f, 0.3f, 1,
      {0,0,0,0, 0,0,0.3f,0, 0,0,0,0, 0.2f,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0.08f,0, 0,0,0,0} },

    /* 8: Bon Cop Bad Cop — dual-osc aggressive */
    { "Bon Cop Bad Cop",
      0.5f, 0.65f, 0.02f, 0.3f, 0.5f, 0.05f, 0,
      0.5f, 0.6f, 0.55f, 1.8f, 0, 0.01f, 0.3f, 0.5f, 0.3f,
      0.5f, 2.0f, 0.7f, 0.2f, 0.5f, 0.0f, 0,
      {0,0,0,0, 0.3f,0,0.5f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,-0.2f,0,0, 0,0,0,0, 0,0,0,0} },

    /* 9: C.R.A.Z.Y. — random chaos modulation */
    { "C.R.A.Z.Y.",
      0.5f, 0.5f, 0.7f, 0.5f, 0.5f, 0.15f, 0,
      0.5f, 0.5f, 0.5f, 1.5f, 0, 0.01f, 0.2f, 0.3f, 0.4f,
      8.0f, 5.0f, 0.8f, 0.7f, 0.4f, 0.0f, 0,
      {0,0,0,0, 0.5f,0,0,0, 0.4f,0,0,0, 0,0.3f,0,0, 0,0.5f,0.6f,0, 0,0,0,0, 0,0,0,0, 0,0,0.4f,0} },

    /* 10: Depanneur — quick convenience pluck */
    { "Depanneur",
      0.5f, 0.3f, 0.0f, 0.4f, 0.3f, 0.05f, 0,
      0.2f, 0.5f, 0.6f, 0.5f, 0, 0.001f, 0.15f, 0.0f, 0.1f,
      0.5f, 0.3f, 0.8f, 0.1f, 0.7f, 0.0f, 0,
      {0,0,0,0, 0,0,0.7f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 11: Caline de Bine — sweet gentle pad */
    { "Caline de Bine",
      0.5f, 0.15f, 0.5f, 0.3f, 0.4f, 0.05f, 1,
      0.05f, 0.5f, 0.55f, 0.3f, 0, 0.6f, 0.5f, 0.8f, 1.2f,
      0.12f, 0.08f, 0.3f, 0.2f, 0.1f, 0.15f, 1,
      {0,0,0,0, 0,0,0,0, 0,0,0.08f,0, 0,0,0.15f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 12: Maudite Toune — catchy annoying lead */
    { "Maudite Toune",
      0.5f, 0.67f, 0.0f, 0.5f, 0.0f, 0.0f, 1,
      0.25f, 0.5f, 0.6f, 1.0f, 0, 0.005f, 0.2f, 0.6f, 0.15f,
      5.5f, 0.3f, 0.6f, 0.0f, 0.4f, 0.04f, 1,
      {0,0,0,0, 0,0,0.4f,0, 0.12f,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 13: Les Boys — hockey organ */
    { "Les Boys",
      0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 1,
      0.0f, 0.5f, 0.8f, 0.0f, 0, 0.01f, 0.1f, 1.0f, 0.05f,
      0.2f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0,
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 14: Nanane — candy bright sweet */
    { "Nanane",
      0.5f, 0.9f, 1.0f, 0.8f, 0.3f, 0.0f, 1,
      0.1f, 0.7f, 0.85f, 0.5f, 0, 0.01f, 0.25f, 0.4f, 0.2f,
      3.0f, 0.5f, 0.5f, 0.1f, 0.3f, 0.0f, 0,
      {0,0,0,0, 0,0,0.3f,0, 0,0.15f,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 15: Tiguidou — everything's perfect, balanced */
    { "Tiguidou",
      0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.05f, 1,
      0.2f, 0.5f, 0.6f, 0.8f, 0, 0.02f, 0.3f, 0.6f, 0.25f,
      0.5f, 0.3f, 0.5f, 0.3f, 0.3f, 0.02f, 0,
      {0,0,0,0, 0,0,0.25f,0, 0,0,0,0, 0,0,0.1f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 16: Pantoute — nothing at all, minimal sine */
    { "Pantoute",
      0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1,
      0.0f, 0.5f, 1.0f, 0.0f, 0, 0.01f, 0.5f, 0.5f, 0.5f,
      0.2f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0,
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 17: Asteure — punchy, immediate percussive hit */
    { "Asteure",
      0.5f, 0.7f, 0.0f, 0.5f, 0.3f, 0.1f, 0,
      0.6f, 0.4f, 0.55f, 1.5f, 0, 0.001f, 0.1f, 0.0f, 0.08f,
      0.5f, 0.3f, 0.9f, 0.2f, 0.8f, 0.0f, 0,
      {0,0,0,0, 0.3f,0,0.8f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 18: Enfirouape — complex deceptive modulation */
    { "Enfirouape",
      0.5f, 0.45f, 0.33f, 0.6f, 0.5f, 0.08f, 1,
      0.35f, 0.5f, 0.5f, 1.2f, 0, 0.1f, 0.4f, 0.5f, 0.6f,
      0.8f, 1.5f, 0.6f, 0.5f, 0.3f, 0.05f, 0,
      {0,0.3f,0,0, 0,0,0,0, 0,0,-0.15f,0.25f, 0,0,0,0, 0,0,0,0, 0,0,0.35f,0.2f, 0,0,0,0, 0.2f,0,0,0} },

    /* 19: Quetaine — kitschy vibrato */
    { "Quetaine",
      0.5f, 0.33f, 0.0f, 0.3f, 0.2f, 0.0f, 1,
      0.1f, 0.5f, 0.7f, 0.5f, 0, 0.01f, 0.3f, 0.7f, 0.3f,
      6.5f, 0.3f, 0.3f, 0.0f, 0.2f, 0.08f, 1,
      {0,0,0,0, 0,0,0,0, 0.35f,0,0.3f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 20: Cruiser l'Main — slow filter sweep pad */
    { "Cruiser l'Main",
      0.5f, 0.65f, -0.01f, 0.5f, 0.5f, 0.05f, 1,
      0.2f, 0.5f, 0.3f, 1.5f, 0, 1.5f, 0.8f, 0.4f, 2.0f,
      0.05f, 0.1f, 0.7f, 0.2f, 0.1f, 0.3f, 1,
      {0,0,0,0, 0,0,0.7f,0, 0,0,0,0, 0,0,0.2f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 21: Broue — excited, fast LFO everything */
    { "Broue",
      0.5f, 0.55f, 0.0f, 0.5f, 0.5f, 0.05f, 0,
      0.4f, 0.5f, 0.5f, 1.8f, 0, 0.01f, 0.2f, 0.5f, 0.2f,
      12.0f, 8.0f, 0.5f, 0.3f, 0.4f, 0.0f, 0,
      {0,0,0,0, 0,0,0,0, 0,0,0,0, 0.3f,0,0.5f,0, 0,0.4f,0.2f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 22: J'me Souviens — nostalgic warm pad */
    { "J'me Souviens",
      0.49f, 0.2f, -0.01f, 0.35f, 0.5f, 0.08f, 1,
      0.08f, 0.5f, 0.45f, 0.5f, 0, 0.8f, 0.6f, 0.7f, 1.5f,
      0.07f, 0.05f, 0.3f, 0.2f, 0.1f, 0.2f, 1,
      {0,0,0,0, 0,0,0.2f,0, 0,0,0,0, 0,0,0.1f,0, 0,0,0,0, 0,0,0,0, 0.05f,0,0,0, 0,0,0,0} },

    /* 23: Chez Schwartz — smoky warm tones */
    { "Chez Schwartz",
      0.5f, 0.5f, -0.5f, 0.4f, 0.6f, 0.12f, 2,
      0.25f, 0.4f, 0.35f, 1.0f, 0, 0.03f, 0.4f, 0.6f, 0.5f,
      0.2f, 0.15f, 0.5f, 0.4f, 0.3f, 0.05f, 0,
      {0,0,0,0, 0,0,0.35f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0.15f,0} },

    /* 24: Ratoureux — sneaky subtle modulation */
    { "Ratoureux",
      0.5f, 0.3f, 0.5f, 0.5f, 0.4f, 0.03f, 1,
      0.1f, 0.5f, 0.55f, 0.8f, 0, 0.05f, 0.3f, 0.6f, 0.4f,
      0.3f, 0.6f, 0.3f, 0.3f, 0.2f, 0.03f, 0,
      {0,0,0,0, 0,0,0,0, 0,0,0,0.08f, 0,0,0,0, 0,0,0,0, 0,0,0.15f,0, 0,0.1f,0,0, 0,0,0,0} },

    /* 25: Ostie d'Lead — aggressive screaming lead */
    { "Ostie d'Lead",
      0.5f, 0.85f, 0.0f, 0.7f, 0.2f, 0.05f, 0,
      0.7f, 0.3f, 0.5f, 2.0f, 0, 0.003f, 0.25f, 0.5f, 0.15f,
      5.0f, 1.0f, 0.8f, 0.2f, 0.7f, 0.03f, 1,
      {0,0,0,0, 0.2f,0,0.6f,0, 0.1f,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 26: Niaiseux — silly random bouncy */
    { "Niaiseux",
      0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 1,
      0.15f, 0.5f, 0.65f, 0.8f, 0, 0.005f, 0.15f, 0.3f, 0.1f,
      3.0f, 6.0f, 0.5f, 0.2f, 0.3f, 0.0f, 0,
      {0,0,0,0, 0,0,0,0.3f, 0,0,0,0, 0,0,0.3f,0, 0.5f,0,0.4f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0} },

    /* 27: Guerre des Tuques — battle: noise, harsh, fast */
    { "Guerre des Tuques",
      0.5f, 0.8f, 0.5f, 0.8f, 0.3f, 0.3f, 0,
      0.75f, 0.2f, 0.4f, 2.5f, 2, 0.001f, 0.08f, 0.2f, 0.1f,
      10.0f, 15.0f, 0.9f, 0.6f, 0.8f, 0.0f, 0,
      {0,0,0,0, 0,0,0.5f,0, 0,0,0,0, 0,0,0,-0.3f, 0,0,0,0, 0.4f,0,0,0, 0.3f,0,0,0, 0,0,0,0} },

    /* 28: Magane — worn out, broken, extreme */
    { "Magane",
      0.53f, 0.9f, 0.13f, 0.9f, 0.5f, 0.2f, 0,
      1.0f, 0.8f, 0.4f, 3.0f, 3, 0.01f, 0.3f, 0.4f, 0.5f,
      3.5f, 7.0f, 0.7f, 0.6f, 0.5f, 0.0f, 0,
      {0,0,0,0, 0.3f,0,0,0, 0,0,0,0, 0.5f,0,0,0, 0,0,0,0, 0,0.4f,0,0, 0,0,0,0, 0,0,-0.3f,0} },

    /* 29: Starbuck — rich abundant lush */
    { "Starbuck",
      0.48f, 0.5f, -0.02f, 0.6f, 0.5f, 0.06f, 1,
      0.3f, 0.5f, 0.65f, 0.6f, 0, 0.4f, 0.5f, 0.75f, 0.8f,
      0.15f, 0.2f, 0.4f, 0.3f, 0.2f, 0.1f, 1,
      {0,0,0,0, 0,0,0.25f,0, 0.05f,0,-0.05f,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0.1f, 0,0,0,0} },
};

/* ── Synth instance ──────────────────────────────────────────────────────── */

typedef struct {
    /* DSP modules */
    complex_osc_t    osc1;
    nto_t            osc2;
    gto_plus_t       gto;
    wave_multiplier_t wm;
    svf_t            filter;
    adsr_t           env;
    noise_gen_t      noise;

    /* Voice state */
    int   note_active;
    int   midi_note;
    float velocity;

    /* Portamento */
    float target_note;
    float current_note;

    /* Patch mode: when on, first pad = note, rest = mod matrix routing */
    int   patch_mode;         /* 0=play, 1=patch */
    int   patch_first_note;   /* MIDI note of the first pad held in patch mode (-1=none) */
    int   pad_base;           /* base MIDI note of the 4x8 pad grid (tracks octave shifts) */

    /* Analog character */
    float drift_phase1;
    float drift_phase2;
    float drift_value1;
    float drift_value2;
    float drift_target1;
    float drift_target2;
    uint32_t drift_rng;

    /* Modulation matrix — bipolar -1..+1 */
    float menu_matrix[MOD_ROWS][MOD_COLS];
    float pad_matrix[MOD_ROWS][MOD_COLS];
    float mod_matrix[MOD_ROWS][MOD_COLS];

    /* Modulation source outputs */
    float mod_src[MOD_ROWS];
    float mod_src_smooth[MOD_ROWS];

    /* Encoder parameters — TARGET values */
    float enc_osc1_freq;      /* 0-1, +/-1 octave detune */
    float enc_osc1_timbre;    /* 0-1 */
    float enc_osc2_pitch;     /* -2 to +2 octaves */
    float enc_osc2_harmonics; /* 0-1 */
    float enc_fold_depth;     /* 0-1 */
    float enc_fold_type;      /* 0-1 */
    float enc_filter_cutoff;  /* 0-1 (mapped to Hz in render) */
    float enc_filter_q;       /* 0-4 */

    /* Smoothed encoder values */
    float sm_osc1_freq;
    float sm_osc1_timbre;
    float sm_osc2_pitch;
    float sm_osc2_harmonics;
    float sm_fold_depth;
    float sm_fold_type;
    float sm_filter_cutoff;
    float sm_filter_q;
    float sm_osc_mix;
    float sm_noise_mix;

    /* Menu parameters */
    float lfo_rate;       /* Hz — LFO triangle rate (0.01-500) */
    float sh_rate;        /* Hz — S&H rate (0.01-500, independent) */
    float attack;         /* seconds */
    float decay;          /* seconds */
    float sustain;        /* 0-1 */
    float release;        /* seconds */
    float osc_mix;        /* 0-1 */
    float noise_mix;      /* 0-1 */
    int   noise_type;     /* 0=white, 1=pink, 2=brown */
    int   filter_type;    /* 0=LP, 1=BP, 2=HP, 3=Notch */
    float mod_depth_env;  /* 0-1 — envelope modulation source depth */
    float mod_depth_noise;/* 0-1 — noise modulation source depth */
    float vel_to_filter;  /* 0-1 — velocity to filter cutoff amount */

    /* Separate noise generator for modulation source */
    noise_gen_t noise_mod;
    float noise_mod_smooth;
    float portamento;     /* seconds */
    int   legato;         /* 0 or 1 */

    /* Preset */
    int current_preset;

    /* Pitch bend / mod wheel */
    float pitch_bend;
    float mod_wheel;

    /* Page tracking for knob overlay */
    int current_page;

    /* Shift key detection via shared memory */
    int shift_held;
    volatile uint8_t *shadow_ctrl;
    int shadow_ctrl_fd;
} serge_instance_t;

/* ── Apply preset ───────────────────────────────────────────────────────── */

static void apply_preset(serge_instance_t *inst, int idx) {
    if (idx < 0 || idx >= NUM_PRESETS) return;
    const preset_t *p = &PRESETS[idx];
    inst->current_preset = idx;

    inst->enc_osc1_freq      = p->osc1_freq;
    inst->enc_osc1_timbre    = p->osc1_timbre;
    inst->enc_osc2_pitch     = p->osc2_pitch;
    inst->enc_osc2_harmonics = p->osc2_harmonics;
    inst->osc_mix            = p->osc_mix;
    inst->noise_mix          = p->noise_mix;
    inst->noise_type         = p->noise_type;
    inst->enc_fold_depth     = p->fold_depth;
    inst->enc_fold_type      = p->fold_type;
    inst->enc_filter_cutoff  = p->filter_cutoff;
    inst->enc_filter_q       = p->filter_q;
    inst->filter_type        = p->filter_type;
    inst->attack             = p->attack;
    inst->decay              = p->decay;
    inst->sustain            = p->sustain;
    inst->release            = p->rel;
    inst->lfo_rate           = p->lfo_rate;
    inst->sh_rate            = p->sh_rate;
    inst->mod_depth_env      = p->mod_depth_env;
    inst->mod_depth_noise    = p->mod_depth_noise;
    inst->vel_to_filter      = p->vel_to_filter;
    inst->portamento         = p->portamento;
    inst->legato             = p->legato;

    /* Copy matrix */
    for (int r = 0; r < MOD_ROWS; r++)
        for (int c = 0; c < MOD_COLS; c++)
            inst->menu_matrix[r][c] = p->mat[r * MOD_COLS + c];

    /* Clear pad overlay */
    memset(inst->pad_matrix, 0, sizeof(inst->pad_matrix));

    /* Snap smoothed values to avoid slow interpolation to new preset */
    inst->sm_osc1_freq      = inst->enc_osc1_freq;
    inst->sm_osc1_timbre    = inst->enc_osc1_timbre;
    inst->sm_osc2_pitch     = inst->enc_osc2_pitch;
    inst->sm_osc2_harmonics = inst->enc_osc2_harmonics;
    inst->sm_fold_depth     = inst->enc_fold_depth;
    inst->sm_fold_type      = inst->enc_fold_type;
    inst->sm_filter_cutoff  = inst->enc_filter_cutoff;
    inst->sm_filter_q       = inst->enc_filter_q;
    inst->sm_osc_mix        = inst->osc_mix;
    inst->sm_noise_mix      = inst->noise_mix;
}

/* Shadow control shared memory for Shift detection */
#define SHM_SHADOW_CONTROL       "/schwung-control"
#define SHADOW_CTRL_SHIFT_OFFSET 21
#define SHADOW_CTRL_SIZE         64

static void read_shift_from_shm(serge_instance_t *inst) {
    if (!inst->shadow_ctrl) return;
    inst->shift_held = inst->shadow_ctrl[SHADOW_CTRL_SHIFT_OFFSET] ? 1 : 0;
}

static const void *g_host = NULL;

/* ── Frequency helpers ──────────────────────────────────────────────────── */

static float cv_to_freq(float cv) {
    return 261.63f * powf(2.0f, cv);
}

/* ── Compute modulated parameters (per-block: timbre, harmonics, fold, level) */
/*
 * Pitch and filter cutoff are computed per-sample in render_block
 * for audio-rate LFO modulation support.
 */

typedef struct {
    float osc1_timbre;
    float osc2_harmonics;
    float fold_depth;
    float fold_type;
    float output_level;
} modulated_params_t;

/* 10ms smoothing for most params */
#define SMOOTH_COEFF 0.25f
/* 20ms smoothing for filter cutoff (smoother sweeps) */
#define SMOOTH_COEFF_FILTER 0.135f

static void smooth_params(serge_instance_t *inst) {
    inst->sm_osc1_freq      += SMOOTH_COEFF * (inst->enc_osc1_freq      - inst->sm_osc1_freq);
    inst->sm_osc1_timbre    += SMOOTH_COEFF * (inst->enc_osc1_timbre    - inst->sm_osc1_timbre);
    inst->sm_osc2_pitch     += SMOOTH_COEFF * (inst->enc_osc2_pitch     - inst->sm_osc2_pitch);
    inst->sm_osc2_harmonics += SMOOTH_COEFF * (inst->enc_osc2_harmonics - inst->sm_osc2_harmonics);
    inst->sm_fold_depth     += SMOOTH_COEFF * (inst->enc_fold_depth     - inst->sm_fold_depth);
    inst->sm_fold_type      += SMOOTH_COEFF * (inst->enc_fold_type      - inst->sm_fold_type);
    inst->sm_filter_cutoff  += SMOOTH_COEFF_FILTER * (inst->enc_filter_cutoff - inst->sm_filter_cutoff);
    inst->sm_filter_q       += SMOOTH_COEFF * (inst->enc_filter_q       - inst->sm_filter_q);
    inst->sm_osc_mix        += SMOOTH_COEFF * (inst->osc_mix            - inst->sm_osc_mix);
    inst->sm_noise_mix      += SMOOTH_COEFF * (inst->noise_mix          - inst->sm_noise_mix);
}

static void compute_modulated_params(serge_instance_t *inst, modulated_params_t *p) {
    /* Modulation source snapshot (per-block, used for slow targets) */
    float src[MOD_ROWS];
    src[SRC_ENVELOPE]    = (inst->mod_src[SRC_ENVELOPE] * 2.0f - 1.0f) * inst->mod_depth_env;
    src[SRC_GTO_SMOOTH]  = inst->mod_src[SRC_GTO_SMOOTH];
    src[SRC_GTO_STEPPED] = inst->mod_src[SRC_GTO_STEPPED];
    src[SRC_NOISE]       = inst->mod_src[SRC_NOISE] * inst->mod_depth_noise;

    /* Col 1: OSC1 Timbre */
    p->osc1_timbre = inst->sm_osc1_timbre;
    for (int r = 0; r < MOD_ROWS; r++)
        p->osc1_timbre += src[r] * inst->mod_matrix[r][TGT_OSC1_TIMBRE] * 0.5f;
    p->osc1_timbre = fmaxf(0.0f, fminf(1.0f, p->osc1_timbre));

    /* Col 3: OSC2 Harmonics */
    p->osc2_harmonics = inst->sm_osc2_harmonics;
    for (int r = 0; r < MOD_ROWS; r++)
        p->osc2_harmonics += src[r] * inst->mod_matrix[r][TGT_OSC2_HARMONICS] * 0.5f;
    p->osc2_harmonics = fmaxf(0.0f, fminf(1.0f, p->osc2_harmonics));

    /* Col 4: Fold Depth */
    p->fold_depth = inst->sm_fold_depth;
    for (int r = 0; r < MOD_ROWS; r++)
        p->fold_depth += src[r] * inst->mod_matrix[r][TGT_FOLD_DEPTH] * 0.4f;
    p->fold_depth = fmaxf(0.0f, fminf(1.0f, p->fold_depth));

    /* Col 5: Fold Type */
    p->fold_type = inst->sm_fold_type;
    for (int r = 0; r < MOD_ROWS; r++)
        p->fold_type += src[r] * inst->mod_matrix[r][TGT_FOLD_TYPE] * 0.4f;
    p->fold_type = fmaxf(0.0f, fminf(1.0f, p->fold_type));

    /* Col 7: Output Level */
    p->output_level = 1.0f;
    for (int r = 0; r < MOD_ROWS; r++)
        p->output_level += src[r] * inst->mod_matrix[r][TGT_OUTPUT_LEVEL] * 0.6f;
    p->output_level = fmaxf(0.0f, fminf(1.5f, p->output_level));
}

/* ── Pad MIDI note to matrix [row, col] ──────────────────────────────────── */

static int compute_pad_base(int note) {
    if (note >= 68 && note < 100) return 68;
    for (int d = 1; d <= 5; d++) {
        int hi = 68 + d * 12;
        if (note >= hi && note < hi + 32) return hi;
        int lo = 68 - d * 12;
        if (note >= lo && note < lo + 32) return lo;
    }
    return 68;
}

static int pad_note_to_matrix(int note, int pad_base, int *row, int *col) {
    int idx = note - pad_base;
    if (idx < 0 || idx >= 32) return 0;
    *row = idx / 8;
    *col = idx % 8;
    return 1;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir; (void)json_defaults;
    serge_instance_t *inst = calloc(1, sizeof(serge_instance_t));
    if (!inst) return NULL;

    /* Apply Init preset as defaults */
    inst->current_preset = 0;

    /* Encoder defaults */
    inst->midi_note          = 60;
    inst->target_note        = 60.0f;
    inst->current_note       = 60.0f;
    inst->enc_osc1_freq      = 0.5f;
    inst->enc_osc1_timbre    = 0.5f;
    inst->enc_osc2_pitch     = 0.0f;
    inst->enc_osc2_harmonics = 0.5f;
    inst->enc_fold_depth     = 0.0f;
    inst->enc_fold_type      = 0.5f;
    inst->enc_filter_cutoff  = 1.0f;
    inst->enc_filter_q       = 0.0f;

    /* Initialize smoothed values to match targets */
    inst->sm_osc1_freq      = inst->enc_osc1_freq;
    inst->sm_osc1_timbre    = inst->enc_osc1_timbre;
    inst->sm_osc2_pitch     = inst->enc_osc2_pitch;
    inst->sm_osc2_harmonics = inst->enc_osc2_harmonics;
    inst->sm_fold_depth     = inst->enc_fold_depth;
    inst->sm_fold_type      = inst->enc_fold_type;
    inst->sm_filter_cutoff  = inst->enc_filter_cutoff;
    inst->sm_filter_q       = inst->enc_filter_q;

    /* Menu defaults */
    inst->lfo_rate    = 0.2f;
    inst->sh_rate     = 0.3f;
    inst->attack      = 0.01f;
    inst->decay       = 0.3f;
    inst->sustain     = 0.7f;
    inst->release     = 0.2f;
    inst->osc_mix     = 0.5f;
    inst->sm_osc_mix  = 0.5f;
    inst->noise_mix   = 0.0f;
    inst->sm_noise_mix = 0.0f;
    inst->noise_type  = 1;    /* pink noise */
    inst->filter_type = 0;    /* lowpass */
    inst->mod_depth_env   = 0.7f;
    inst->mod_depth_noise = 0.4f;
    inst->vel_to_filter   = 0.0f;
    inst->noise_mod.rng   = 0x12345678;
    inst->portamento  = 0.0f;
    inst->legato      = 0;
    inst->patch_mode  = 1;    /* default: Patch mode */

    /* Noise RNG seed */
    inst->noise.rng = 0xDEADBEEF;
    inst->patch_first_note = -1;
    inst->pad_base = 68;

    /* Analog drift */
    inst->drift_rng = 0xCAFEBABE;
    inst->drift_phase1 = 0.0f;
    inst->drift_phase2 = 0.37f;

    /* GTO RNG seed */
    inst->gto.rng_state = 42u;

    /* Map shadow control shared memory for Shift button detection */
    inst->shadow_ctrl_fd = open("/dev/shm" SHM_SHADOW_CONTROL, O_RDONLY);
    if (inst->shadow_ctrl_fd >= 0) {
        inst->shadow_ctrl = (volatile uint8_t *)mmap(NULL, SHADOW_CTRL_SIZE,
                                                      PROT_READ, MAP_SHARED,
                                                      inst->shadow_ctrl_fd, 0);
        if (inst->shadow_ctrl == MAP_FAILED) {
            inst->shadow_ctrl = NULL;
            close(inst->shadow_ctrl_fd);
            inst->shadow_ctrl_fd = -1;
        }
    } else {
        inst->shadow_ctrl = NULL;
        inst->shadow_ctrl_fd = -1;
    }

    return inst;
}

static void destroy_instance(void *instance) {
    serge_instance_t *inst = (serge_instance_t *)instance;
    if (inst) {
        if (inst->shadow_ctrl) {
            munmap((void *)inst->shadow_ctrl, SHADOW_CTRL_SIZE);
        }
        if (inst->shadow_ctrl_fd >= 0) {
            close(inst->shadow_ctrl_fd);
        }
        free(inst);
    }
}

/* ── MIDI ────────────────────────────────────────────────────────────────── */

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    serge_instance_t *inst = (serge_instance_t *)instance;
    if (len < 2) return;

    uint8_t status = msg[0] & 0xF0;
    uint8_t data1  = msg[1];
    uint8_t data2  = (len >= 3) ? msg[2] : 0;

    read_shift_from_shm(inst);

    if (status == 0x90 && data2 > 0) {
        /* Note On */
        if (inst->patch_mode) {
            int row, col;

            if (!inst->note_active) {
                /* First pad: detect octave, play note, hold it */
                inst->pad_base = compute_pad_base(data1);
                inst->patch_first_note = data1;
                inst->midi_note = data1;
                inst->target_note = (float)data1;
                /* Snap current_note if no portamento or first note */
                if (inst->portamento < 0.001f || !inst->note_active)
                    inst->current_note = (float)data1;
                inst->velocity  = data2 / 127.0f;
                inst->env.stage = 0;
                inst->note_active = 1;
            } else if (pad_note_to_matrix(data1, inst->pad_base, &row, &col)) {
                /* Subsequent pads while holding: matrix routing only, no sound */
                inst->pad_matrix[row][col] = sqrtf(data2 / 127.0f);
            }
            return;
        }
        /* PLAY MODE */
        int first_note = !inst->note_active;
        inst->midi_note = data1;
        inst->target_note = (float)data1;
        /* Snap pitch immediately if no portamento or if this is the first note */
        if (inst->portamento < 0.001f || first_note)
            inst->current_note = (float)data1;
        inst->velocity  = data2 / 127.0f;
        if (!inst->legato || !inst->note_active) {
            inst->env.stage = 0;
        }
        inst->note_active = 1;

    } else if (status == 0x80 || (status == 0x90 && data2 == 0)) {
        /* Note Off */
        if (inst->patch_mode) {
            if (data1 == inst->patch_first_note) {
                inst->patch_first_note = -1;
                inst->env.stage = 3;
                inst->note_active = 0;
                memset(inst->pad_matrix, 0, sizeof(inst->pad_matrix));
            } else {
                int row, col;
                if (pad_note_to_matrix(data1, inst->pad_base, &row, &col))
                    inst->pad_matrix[row][col] = 0.0f;
            }
            return;
        }
        if (data1 == inst->midi_note) {
            inst->env.stage = 3;
        }

    } else if (status == 0xA0) {
        /* Polyphonic aftertouch — update patch depth in real-time */
        if (inst->patch_mode) {
            int row, col;
            if (data2 > 0
                && pad_note_to_matrix(data1, inst->pad_base, &row, &col)
                && inst->pad_matrix[row][col] > 0.0f) {
                inst->pad_matrix[row][col] = sqrtf(data2 / 127.0f);
            }
        }

    } else if (status == 0xE0 && len >= 3) {
        /* Pitch bend */
        int bend = (data2 << 7) | data1;
        inst->pitch_bend = (bend - 8192) / 8192.0f * 2.0f;

    } else if (status == 0xB0) {
        if (data1 == 1) {
            inst->mod_wheel = data2 / 127.0f;
        } else if (data1 == 49) {
            inst->shift_held = (data2 >= 64) ? 1 : 0;
        }
    }
}

/* ── Parameters ──────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    float *ptr;
    float min, max, step;
    int display;  /* 0=percent, 1=raw float, 2=CV value */
} knob_def_t;

static const char *NOISE_TYPE_NAMES[3] = {"White", "Pink", "Brown"};
static const char *FILTER_TYPE_NAMES[4] = {"Lowpass", "Bandpass", "Highpass", "Notch"};

static void get_page_knob(serge_instance_t *inst, int page, int knob, knob_def_t *def) {
    def->name = ""; def->ptr = NULL; def->min = 0; def->max = 1; def->step = 0.01f; def->display = 0;
    switch (page) {
        case 0: /* Oscillators */
            switch (knob) {
                case 0: def->name = "Osc1 Freq"; def->ptr = &inst->enc_osc1_freq;      break;
                case 1: def->name = "Timbre";     def->ptr = &inst->enc_osc1_timbre;     break;
                case 2: def->name = "Osc2 Freq";  def->ptr = &inst->enc_osc2_pitch;      def->min = -2; def->max = 2; def->step = 0.02f; def->display = 2; break;
                case 3: def->name = "Harmonics";   def->ptr = &inst->enc_osc2_harmonics;  break;
                case 4: def->name = "Osc Mix";     def->ptr = &inst->osc_mix;             break;
                case 5: def->name = "Noise Mix";   def->ptr = &inst->noise_mix;           break;
                case 6: def->name = "Noise Type";  def->ptr = NULL; break;
                case 7: def->name = "Cutoff";      def->ptr = &inst->enc_filter_cutoff;   break;
            }
            break;
        case 1: /* Control (Wavefold + Filter + Envelope) */
            switch (knob) {
                case 0: def->name = "Fold Depth"; def->ptr = &inst->enc_fold_depth;    break;
                case 1: def->name = "Fold Type";  def->ptr = &inst->enc_fold_type;     break;
                case 2: def->name = "Cutoff";     def->ptr = &inst->enc_filter_cutoff; break;
                case 3: def->name = "Q";          def->ptr = &inst->enc_filter_q;      def->max = 4; break;
                case 4: def->name = "Attack";     def->ptr = &inst->attack;  def->min = 0.001f; def->max = 3; def->step = 0.01f; def->display = 1; break;
                case 5: def->name = "Decay";      def->ptr = &inst->decay;   def->min = 0.001f; def->max = 3; def->step = 0.01f; def->display = 1; break;
                case 6: def->name = "Sustain";    def->ptr = &inst->sustain; break;
                case 7: def->name = "Release";    def->ptr = &inst->release; def->min = 0.001f; def->max = 3; def->step = 0.01f; def->display = 1; break;
            }
            break;
        case 2: /* Modulation */
            switch (knob) {
                case 0: def->name = "LFO Rate";   def->ptr = &inst->lfo_rate;       def->min = 0.01f; def->max = 500; def->step = 0.1f; def->display = 1; break;
                case 1: def->name = "S&H Rate";   def->ptr = &inst->sh_rate;        def->min = 0.01f; def->max = 500; def->step = 0.1f; def->display = 1; break;
                case 2: def->name = "Env Depth";  def->ptr = &inst->mod_depth_env;   break;
                case 3: def->name = "Nz Depth";   def->ptr = &inst->mod_depth_noise; break;
                case 4: def->name = "Rnd Denis";  def->ptr = NULL; break;
                case 5: def->name = "Rnd Mod";    def->ptr = NULL; break;
                case 6: def->name = "Reset Matrix"; def->ptr = NULL; break;
                case 7: def->name = "Rnd Patch";  def->ptr = NULL; break;
            }
            break;
        case 3: case 4: case 5: case 6: { /* Matrix rows 0-3 */
            static const char *col_names[8] = {"->Pitch1","->Timbre","->Pitch2","->Harm","->Fold","->FType","->Cutoff","->Level"};
            int row = page - 3;
            if (knob < 8) {
                def->name = col_names[knob];
                def->ptr = &inst->menu_matrix[row][knob];
                def->min = -1; def->max = 1; /* bipolar */
            }
            break;
        }
    }
}

/* ── Randomize helpers ──────────────────────────────────────────────────── */

static float rand_f(uint32_t *rng) {
    *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
    return (float)(*rng & 0xFFFF) / 65536.0f;
}

static void randomize_serge(serge_instance_t *inst) {
    inst->enc_osc1_freq      = rand_f(&inst->drift_rng);
    inst->enc_osc1_timbre    = rand_f(&inst->drift_rng);
    inst->enc_osc2_pitch     = rand_f(&inst->drift_rng) * 4.0f - 2.0f;
    inst->enc_osc2_harmonics = rand_f(&inst->drift_rng);
    inst->osc_mix            = rand_f(&inst->drift_rng);
    inst->noise_mix          = rand_f(&inst->drift_rng) * 0.5f;
    inst->noise_type         = (int)(rand_f(&inst->drift_rng) * 3.0f);
    if (inst->noise_type > 2) inst->noise_type = 2;
    inst->lfo_rate       = 0.05f + rand_f(&inst->drift_rng) * 15.0f;
    inst->sh_rate        = 0.05f + rand_f(&inst->drift_rng) * 15.0f;
    inst->mod_depth_env   = 0.3f + rand_f(&inst->drift_rng) * 0.7f;
    inst->mod_depth_noise = rand_f(&inst->drift_rng) * 0.6f;
    inst->enc_filter_cutoff = 0.3f + rand_f(&inst->drift_rng) * 0.65f;
    inst->enc_filter_q     = rand_f(&inst->drift_rng) * 2.5f;
    inst->filter_type      = (int)(rand_f(&inst->drift_rng) * 4.0f);
    if (inst->filter_type > 3) inst->filter_type = 3;
    inst->vel_to_filter = rand_f(&inst->drift_rng) * 0.8f;
}

static void randomize_matrix(serge_instance_t *inst) {
    /* Weighted bipolar: 50% subtle (0-0.25), 25% moderate (0.25-0.5), 25% strong (0.5-0.75) */
    for (int r = 0; r < MOD_ROWS; r++)
        for (int c = 0; c < MOD_COLS; c++) {
            float roll = rand_f(&inst->drift_rng);
            float mag;
            if (roll < 0.5f)
                mag = rand_f(&inst->drift_rng) * 0.25f;
            else if (roll < 0.75f)
                mag = 0.25f + rand_f(&inst->drift_rng) * 0.25f;
            else
                mag = 0.5f + rand_f(&inst->drift_rng) * 0.25f;
            float sign = (rand_f(&inst->drift_rng) < 0.5f) ? 1.0f : -1.0f;
            inst->menu_matrix[r][c] = sign * mag;
        }
    inst->lfo_rate = 0.1f + rand_f(&inst->drift_rng) * 10.0f;
    inst->sh_rate  = 0.1f + rand_f(&inst->drift_rng) * 10.0f;
}

static void randomize_all(serge_instance_t *inst) {
    randomize_serge(inst);
    inst->enc_fold_depth     = rand_f(&inst->drift_rng);
    inst->enc_fold_type      = rand_f(&inst->drift_rng);
    inst->attack             = 0.001f + rand_f(&inst->drift_rng) * 1.5f;
    inst->decay              = 0.01f + rand_f(&inst->drift_rng) * 1.5f;
    inst->sustain            = rand_f(&inst->drift_rng);
    inst->release            = 0.01f + rand_f(&inst->drift_rng) * 1.49f;
    randomize_matrix(inst);
}

/* ── State persistence ───────────────────────────────────────────────────── */
/* Schwung saves a module by calling get_param("state") and expects a JSON
 * snapshot; it restores AFTER create_instance() via set_param("state", json).
 * It does NOT pass saved state through create_instance(json_defaults).
 * Every user-settable param must be listed here, or it silently resets to its
 * default on every set reload (the "doesn't save modified states" bug).
 *
 * "preset" is listed FIRST and always applied FIRST on restore: setting it
 * calls apply_preset() which overwrites the encoders, menu params, enums and
 * the whole menu_matrix, so a saved patch that differs from its base preset
 * would be clobbered unless every individual key is applied AFTER the preset. */

static const char *STATE_KEYS[] = {
    /* preset first (apply_preset overwrites everything below) */
    "preset",
    /* encoders (8) */
    "osc1_freq","osc1_timbre","osc2_pitch","osc2_harmonics",
    "fold_depth","fold_type","filter_cutoff","filter_q",
    /* menu floats (12) */
    "lfo_rate","sh_rate","attack","decay","sustain","release",
    "osc_mix","noise_mix","mod_depth_env","mod_depth_noise",
    "vel_to_filter","portamento",
    /* enums (4) */
    "noise_type","filter_type","legato","patch_mode",
    /* modulation matrix (32) */
    "mat_0_0","mat_0_1","mat_0_2","mat_0_3","mat_0_4","mat_0_5","mat_0_6","mat_0_7",
    "mat_1_0","mat_1_1","mat_1_2","mat_1_3","mat_1_4","mat_1_5","mat_1_6","mat_1_7",
    "mat_2_0","mat_2_1","mat_2_2","mat_2_3","mat_2_4","mat_2_5","mat_2_6","mat_2_7",
    "mat_3_0","mat_3_1","mat_3_2","mat_3_3","mat_3_4","mat_3_5","mat_3_6","mat_3_7"
};
#define STATE_KEY_COUNT ((int)(sizeof(STATE_KEYS)/sizeof(STATE_KEYS[0])))

/* Extract the quoted string value for "key". Searches for the fully-quoted key
 * ("osc_mix" never matches inside a longer key) and tolerates whitespace or
 * newlines after the colon: Schwung's autosave re-stringifies the slot JSON
 * pretty-printed ("key": "value"), so a strict "key":" search would miss and
 * params would quietly fall back to defaults. Values are always emitted quoted
 * by the state getter, so a single string extractor covers every param. */
static int denis_json_get_str(const char *json, const char *key,
                              char *out, int out_len) {
    char search[48];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ':' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    int len = (int)(end - p);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, (size_t)len);
    out[len] = '\0';
    return 0;
}

static void set_param(void *instance, const char *key, const char *val);

static void restore_state(void *instance, const char *json) {
    char vb[64];
    /* preset MUST be applied before the individual params it would overwrite */
    if (denis_json_get_str(json, "preset", vb, sizeof(vb)) == 0)
        set_param(instance, "preset", vb);
    for (int i = 0; i < STATE_KEY_COUNT; i++) {
        if (strcmp(STATE_KEYS[i], "preset") == 0) continue;  /* already applied */
        if (denis_json_get_str(json, STATE_KEYS[i], vb, sizeof(vb)) == 0)
            set_param(instance, STATE_KEYS[i], vb);
    }
}

static void set_param(void *instance, const char *key, const char *val) {
    serge_instance_t *inst = (serge_instance_t *)instance;
    if (!inst || !key || !val) return;
    float f = (float)atof(val);

    /* Bulk restore from Schwung's saved snapshot: hand each key to the
     * ordinary per-key handlers below so their clamping and enum matching
     * apply exactly as for a live knob turn. */
    if (strcmp(key, "state") == 0) {
        restore_state(instance, val);
        return;
    }

    /* Encoder params */
    if      (strcmp(key, "osc1_freq")      == 0) inst->enc_osc1_freq      = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "osc1_timbre")    == 0) inst->enc_osc1_timbre    = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "osc2_pitch")     == 0) inst->enc_osc2_pitch     = fmaxf(-2, fminf(2, f));
    else if (strcmp(key, "osc2_harmonics") == 0) inst->enc_osc2_harmonics = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "fold_depth")     == 0) inst->enc_fold_depth     = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "fold_type")      == 0) inst->enc_fold_type      = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "filter_cutoff")  == 0) inst->enc_filter_cutoff  = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "filter_q")       == 0) inst->enc_filter_q       = fmaxf(0, fminf(4, f));
    /* Menu params */
    else if (strcmp(key, "lfo_rate")       == 0) inst->lfo_rate    = fmaxf(0.01f, fminf(500, f));
    else if (strcmp(key, "sh_rate")        == 0) inst->sh_rate     = fmaxf(0.01f, fminf(500, f));
    else if (strcmp(key, "attack")         == 0) inst->attack      = fmaxf(0.001f, fminf(3, f));
    else if (strcmp(key, "decay")          == 0) inst->decay       = fmaxf(0.001f, fminf(3, f));
    else if (strcmp(key, "sustain")        == 0) inst->sustain     = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "release")        == 0) inst->release     = fmaxf(0.001f, fminf(3, f));
    else if (strcmp(key, "osc_mix")        == 0) inst->osc_mix     = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "noise_mix")      == 0) inst->noise_mix   = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "mod_depth_env")   == 0) inst->mod_depth_env   = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "mod_depth_noise") == 0) inst->mod_depth_noise = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "vel_to_filter")  == 0) inst->vel_to_filter = fmaxf(0, fminf(1, f));
    else if (strcmp(key, "noise_type") == 0) {
        int found_nt = 0;
        for (int i = 0; i < 3; i++) {
            if (strcmp(val, NOISE_TYPE_NAMES[i]) == 0) { inst->noise_type = i; found_nt = 1; break; }
        }
        if (!found_nt) inst->noise_type = (int)fmaxf(0, fminf(2, f));
    }
    else if (strcmp(key, "filter_type") == 0) {
        int found_ft = 0;
        for (int i = 0; i < 4; i++) {
            if (strcmp(val, FILTER_TYPE_NAMES[i]) == 0) { inst->filter_type = i; found_ft = 1; break; }
        }
        if (!found_ft) inst->filter_type = (int)fmaxf(0, fminf(3, f));
    }
    else if (strcmp(key, "portamento")     == 0) inst->portamento  = fmaxf(0, fminf(2, f));
    else if (strcmp(key, "legato") == 0) {
        if (strcmp(val, "On") == 0) inst->legato = 1;
        else if (strcmp(val, "Off") == 0) inst->legato = 0;
        else inst->legato = (int)f;
    }
    /* Preset */
    else if (strcmp(key, "preset") == 0) {
        for (int i = 0; i < NUM_PRESETS; i++) {
            if (strcmp(val, PRESETS[i].name) == 0) { apply_preset(inst, i); return; }
        }
        int idx = (int)f;
        if (idx >= 0 && idx < NUM_PRESETS) apply_preset(inst, idx);
    }
    /* Page tracking — handle both _level and current_level */
    else if (strcmp(key, "_level") == 0 || strcmp(key, "current_level") == 0) {
        if (strcmp(val, "Oscillators") == 0)       inst->current_page = 0;
        else if (strcmp(val, "Control") == 0)       inst->current_page = 1;
        else if (strcmp(val, "Mod & Patch") == 0)    inst->current_page = 2;
        else if (strcmp(val, "Mat: Env") == 0)      inst->current_page = 3;
        else if (strcmp(val, "Mat: LFO") == 0)      inst->current_page = 4;
        else if (strcmp(val, "Mat: S&H") == 0)      inst->current_page = 5;
        else if (strcmp(val, "Mat: Nz") == 0)       inst->current_page = 6;
        else if (strcmp(val, "Denis") == 0)          inst->current_page = 0;
        else if (strcmp(val, "root") == 0)           inst->current_page = 0;
        else                                         inst->current_page = 0;
    }

    else if (strcmp(key, "patch_mode") == 0) {
        if (strcmp(val, "Patch") == 0) inst->patch_mode = 1;
        else if (strcmp(val, "Play") == 0) inst->patch_mode = 0;
        else inst->patch_mode = (int)f;
        memset(inst->pad_matrix, 0, sizeof(inst->pad_matrix));
    }
    else if (strcmp(key, "matrix_reset") == 0) {
        if (f > 0.01f) {
            memset(inst->menu_matrix, 0, sizeof(inst->menu_matrix));
            memset(inst->pad_matrix, 0, sizeof(inst->pad_matrix));
        }
    }
    else if (strcmp(key, "rnd_denis") == 0) { if (f > 0.01f) randomize_serge(inst); }
    else if (strcmp(key, "rnd_mod")   == 0) { if (f > 0.01f) randomize_matrix(inst); }
    else if (strcmp(key, "rnd_patch") == 0) { if (f > 0.01f) randomize_all(inst); }

    /* Matrix params: mat_R_C (R=0-3, C=0-7) — bipolar -1..+1 */
    else if (strncmp(key, "mat_", 4) == 0 && strlen(key) == 7) {
        int r = key[4] - '0', c = key[6] - '0';
        if (r >= 0 && r < MOD_ROWS && c >= 0 && c < MOD_COLS)
            inst->menu_matrix[r][c] = fmaxf(-1.0f, fminf(1.0f, f));
    }

    /* ---- Knob overlay: knob_N_adjust (page-aware) ---- */
    else if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_adjust")) {
        int knob_num = atoi(key + 5) - 1;
        if (knob_num >= 0 && knob_num < 8) {
            int delta = atoi(val);
            knob_def_t def;
            get_page_knob(inst, inst->current_page, knob_num, &def);
            if (def.ptr) {
                /* Adaptive step for wide-range rate params */
                float step = def.step;
                if (def.max >= 100.0f) {
                    float v = fabsf(*def.ptr);
                    if (v > 100.0f) step = fmaxf(step, 5.0f);
                    else if (v > 10.0f) step = fmaxf(step, 1.0f);
                }
                *def.ptr = fmaxf(def.min, fminf(def.max, *def.ptr + delta * step));
            } else {
                /* Enum / action knobs */
                if (inst->current_page == 0 && knob_num == 6) {
                    int nt = inst->noise_type + delta;
                    if (nt > 2) nt = 0; if (nt < 0) nt = 2;
                    inst->noise_type = nt;
                } else if (inst->current_page == 2 && knob_num == 4) {
                    if (delta != 0) randomize_serge(inst);
                } else if (inst->current_page == 2 && knob_num == 5) {
                    if (delta != 0) randomize_matrix(inst);
                } else if (inst->current_page == 2 && knob_num == 6) {
                    if (delta != 0) {
                        memset(inst->menu_matrix, 0, sizeof(inst->menu_matrix));
                        memset(inst->pad_matrix, 0, sizeof(inst->pad_matrix));
                    }
                } else if (inst->current_page == 2 && knob_num == 7) {
                    if (delta != 0) randomize_all(inst);
                }
            }
        }
    }
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    serge_instance_t *inst = (serge_instance_t *)instance;
    if (!inst || !key || !buf || buf_len < 1) return -1;

    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Denis");
    }

    if (strcmp(key, "chain_params") == 0) {
        static const char *cp =
            "["
            "{\"key\":\"osc1_freq\",\"name\":\"Osc1 Freq\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"osc1_timbre\",\"name\":\"Timbre\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"osc2_pitch\",\"name\":\"Osc2 Freq\",\"type\":\"float\",\"min\":-2,\"max\":2,\"step\":0.01},"
            "{\"key\":\"osc2_harmonics\",\"name\":\"Harmonics\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"osc_mix\",\"name\":\"Osc Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"fold_depth\",\"name\":\"Fold Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"fold_type\",\"name\":\"Fold Type\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"filter_cutoff\",\"name\":\"Cutoff\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"filter_q\",\"name\":\"Q\",\"type\":\"float\",\"min\":0,\"max\":4,\"step\":0.01},"
            "{\"key\":\"filter_type\",\"name\":\"Filter Type\",\"type\":\"enum\",\"options\":[\"Lowpass\",\"Bandpass\",\"Highpass\",\"Notch\"]},"
            "{\"key\":\"vel_to_filter\",\"name\":\"Vel>Filter\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"float\",\"min\":0.001,\"max\":3,\"step\":0.01},"
            "{\"key\":\"decay\",\"name\":\"Decay\",\"type\":\"float\",\"min\":0.001,\"max\":3,\"step\":0.01},"
            "{\"key\":\"sustain\",\"name\":\"Sustain\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"release\",\"name\":\"Release\",\"type\":\"float\",\"min\":0.001,\"max\":3,\"step\":0.01},"
            "{\"key\":\"noise_mix\",\"name\":\"Noise Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"noise_type\",\"name\":\"Noise Type\",\"type\":\"enum\",\"options\":[\"White\",\"Pink\",\"Brown\"]},"
            "{\"key\":\"lfo_rate\",\"name\":\"LFO Rate\",\"type\":\"float\",\"min\":0.01,\"max\":500,\"step\":0.01},"
            "{\"key\":\"sh_rate\",\"name\":\"S&H Rate\",\"type\":\"float\",\"min\":0.01,\"max\":500,\"step\":0.01},"
            "{\"key\":\"mod_depth_env\",\"name\":\"Env Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mod_depth_noise\",\"name\":\"Nz Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"enum\",\"options\":[\"Init\",\"Tabarnak\",\"Poutine Grasse\",\"Elvis Gratton\",\"Frette en Criss\",\"Slush Brune\",\"Ti-Coune\",\"Grande Seduction\",\"Bon Cop Bad Cop\",\"C.R.A.Z.Y.\",\"Depanneur\",\"Caline de Bine\",\"Maudite Toune\",\"Les Boys\",\"Nanane\",\"Tiguidou\",\"Pantoute\",\"Asteure\",\"Enfirouape\",\"Quetaine\",\"Cruiser l'Main\",\"Broue\",\"J'me Souviens\",\"Chez Schwartz\",\"Ratoureux\",\"Ostie d'Lead\",\"Niaiseux\",\"Guerre des Tuques\",\"Magane\",\"Starbuck\"]},"
            "{\"key\":\"portamento\",\"name\":\"Portamento\",\"type\":\"float\",\"min\":0,\"max\":2,\"step\":0.01},"
            "{\"key\":\"legato\",\"name\":\"Legato\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
            "{\"key\":\"patch_mode\",\"name\":\"Pad Mode\",\"type\":\"enum\",\"options\":[\"Play\",\"Patch\"]},"
            "{\"key\":\"matrix_reset\",\"name\":\"Reset Matrix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"rnd_denis\",\"name\":\"Rnd Denis\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"rnd_mod\",\"name\":\"Rnd Mod\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"rnd_patch\",\"name\":\"Rnd Patch\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_0\",\"name\":\"Env->Pitch1\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_1\",\"name\":\"Env->Timbre\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_2\",\"name\":\"Env->Pitch2\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_3\",\"name\":\"Env->Harm\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_4\",\"name\":\"Env->Fold\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_5\",\"name\":\"Env->FType\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_6\",\"name\":\"Env->Cutof\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_0_7\",\"name\":\"Env->Level\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_0\",\"name\":\"LFO->Pitch1\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_1\",\"name\":\"LFO->Timbre\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_2\",\"name\":\"LFO->Pitch2\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_3\",\"name\":\"LFO->Harm\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_4\",\"name\":\"LFO->Fold\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_5\",\"name\":\"LFO->FType\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_6\",\"name\":\"LFO->Cutof\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_1_7\",\"name\":\"LFO->Level\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_0\",\"name\":\"S&H->Pitch1\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_1\",\"name\":\"S&H->Timbre\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_2\",\"name\":\"S&H->Pitch2\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_3\",\"name\":\"S&H->Harm\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_4\",\"name\":\"S&H->Fold\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_5\",\"name\":\"S&H->FType\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_6\",\"name\":\"S&H->Cutof\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_2_7\",\"name\":\"S&H->Level\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_0\",\"name\":\"Nz->Pitch1\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_1\",\"name\":\"Nz->Timbre\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_2\",\"name\":\"Nz->Pitch2\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_3\",\"name\":\"Nz->Harm\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_4\",\"name\":\"Nz->Fold\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_5\",\"name\":\"Nz->FType\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_6\",\"name\":\"Nz->Cutof\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mat_3_7\",\"name\":\"Nz->Level\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01}"
            "]";
        int len = (int)strlen(cp);
        if (len >= buf_len) return -1;
        memcpy(buf, cp, len + 1);
        return len;
    }

    if (strcmp(key, "ui_hierarchy") == 0) {
        static const char *hier =
            "{\"modes\":null,\"levels\":{"
            "\"root\":{\"name\":\"Denis\","
            "\"knobs\":[\"osc1_freq\",\"osc1_timbre\",\"osc2_pitch\",\"osc2_harmonics\",\"osc_mix\",\"noise_mix\",\"noise_type\",\"filter_cutoff\"],"
            "\"params\":[{\"level\":\"Oscillators\",\"label\":\"Oscillators\"},{\"level\":\"Control\",\"label\":\"Control\"},{\"level\":\"Mod & Patch\",\"label\":\"Mod & Patch\"},{\"level\":\"Mat: Env\",\"label\":\"Mat: Env\"},{\"level\":\"Mat: LFO\",\"label\":\"Mat: LFO\"},{\"level\":\"Mat: S&H\",\"label\":\"Mat: S&H\"},{\"level\":\"Mat: Nz\",\"label\":\"Mat: Nz\"}]},"
            "\"Oscillators\":{\"label\":\"Oscillators\","
            "\"knobs\":[\"osc1_freq\",\"osc1_timbre\",\"osc2_pitch\",\"osc2_harmonics\",\"osc_mix\",\"noise_mix\",\"noise_type\",\"filter_cutoff\"],"
            "\"params\":[\"osc1_freq\",\"osc1_timbre\",\"osc2_pitch\",\"osc2_harmonics\",\"osc_mix\",\"noise_mix\",\"noise_type\"]},"
            "\"Control\":{\"label\":\"Control\","
            "\"knobs\":[\"fold_depth\",\"fold_type\",\"filter_cutoff\",\"filter_q\",\"attack\",\"decay\",\"sustain\",\"release\"],"
            "\"params\":[\"fold_depth\",\"fold_type\",\"filter_cutoff\",\"filter_q\",\"attack\",\"decay\",\"sustain\",\"release\",\"filter_type\",\"vel_to_filter\"]},"
            "\"Mod & Patch\":{\"label\":\"Mod & Patch\","
            "\"knobs\":[\"lfo_rate\",\"sh_rate\",\"mod_depth_env\",\"mod_depth_noise\",\"rnd_denis\",\"rnd_mod\",\"matrix_reset\",\"rnd_patch\"],"
            "\"params\":[\"lfo_rate\",\"sh_rate\",\"mod_depth_env\",\"mod_depth_noise\",\"rnd_denis\",\"rnd_mod\",\"matrix_reset\",\"rnd_patch\",\"preset\",\"patch_mode\",\"portamento\",\"legato\"]},"
            "\"Mat: Env\":{\"label\":\"Mat: Env\","
            "\"knobs\":[\"mat_0_0\",\"mat_0_1\",\"mat_0_2\",\"mat_0_3\",\"mat_0_4\",\"mat_0_5\",\"mat_0_6\",\"mat_0_7\"],"
            "\"params\":[\"mat_0_0\",\"mat_0_1\",\"mat_0_2\",\"mat_0_3\",\"mat_0_4\",\"mat_0_5\",\"mat_0_6\",\"mat_0_7\"]},"
            "\"Mat: LFO\":{\"label\":\"Mat: LFO\","
            "\"knobs\":[\"mat_1_0\",\"mat_1_1\",\"mat_1_2\",\"mat_1_3\",\"mat_1_4\",\"mat_1_5\",\"mat_1_6\",\"mat_1_7\"],"
            "\"params\":[\"mat_1_0\",\"mat_1_1\",\"mat_1_2\",\"mat_1_3\",\"mat_1_4\",\"mat_1_5\",\"mat_1_6\",\"mat_1_7\"]},"
            "\"Mat: S&H\":{\"label\":\"Mat: S&H\","
            "\"knobs\":[\"mat_2_0\",\"mat_2_1\",\"mat_2_2\",\"mat_2_3\",\"mat_2_4\",\"mat_2_5\",\"mat_2_6\",\"mat_2_7\"],"
            "\"params\":[\"mat_2_0\",\"mat_2_1\",\"mat_2_2\",\"mat_2_3\",\"mat_2_4\",\"mat_2_5\",\"mat_2_6\",\"mat_2_7\"]},"
            "\"Mat: Nz\":{\"label\":\"Mat: Nz\","
            "\"knobs\":[\"mat_3_0\",\"mat_3_1\",\"mat_3_2\",\"mat_3_3\",\"mat_3_4\",\"mat_3_5\",\"mat_3_6\",\"mat_3_7\"],"
            "\"params\":[\"mat_3_0\",\"mat_3_1\",\"mat_3_2\",\"mat_3_3\",\"mat_3_4\",\"mat_3_5\",\"mat_3_6\",\"mat_3_7\"]}"
            "}}";
        int len = (int)strlen(hier);
        if (len >= buf_len) return -1;
        memcpy(buf, hier, len + 1);
        return len;
    }

    /* ── state: full snapshot for Schwung autosave / set reload ── */
    /* Every value is emitted as a quoted string (raw float text or the enum
     * option name, built by calling this same get_param per key so the
     * representation is identical to a live read) so it survives any
     * re-serialization unchanged and feeds straight back into set_param. */
    if (strcmp(key, "state") == 0) {
        if (buf_len < 2) return -1;
        int n = 0;
        buf[n++] = '{';
        for (int i = 0; i < STATE_KEY_COUNT; i++) {
            char vb[48];
            if (get_param(inst, STATE_KEYS[i], vb, sizeof(vb)) < 0) continue;
            int w = snprintf(buf + n, (size_t)(buf_len - n), "%s\"%s\":\"%s\"",
                             (n > 1) ? "," : "", STATE_KEYS[i], vb);
            if (w < 0 || w >= buf_len - n) return -1;   /* would overflow */
            n += w;
        }
        if (n + 1 >= buf_len) return -1;
        buf[n++] = '}';
        buf[n] = '\0';
        return n;
    }

    /* Individual param getters */
    if (strcmp(key, "osc1_freq")      == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_osc1_freq);
    if (strcmp(key, "osc1_timbre")    == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_osc1_timbre);
    if (strcmp(key, "osc2_pitch")     == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_osc2_pitch);
    if (strcmp(key, "osc2_harmonics") == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_osc2_harmonics);
    if (strcmp(key, "fold_depth")     == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_fold_depth);
    if (strcmp(key, "fold_type")      == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_fold_type);
    if (strcmp(key, "filter_cutoff")  == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_filter_cutoff);
    if (strcmp(key, "filter_q")       == 0) return snprintf(buf, buf_len, "%.4f", inst->enc_filter_q);
    if (strcmp(key, "lfo_rate")       == 0) return snprintf(buf, buf_len, "%.4f", inst->lfo_rate);
    if (strcmp(key, "sh_rate")        == 0) return snprintf(buf, buf_len, "%.4f", inst->sh_rate);
    if (strcmp(key, "attack")         == 0) return snprintf(buf, buf_len, "%.4f", inst->attack);
    if (strcmp(key, "decay")          == 0) return snprintf(buf, buf_len, "%.4f", inst->decay);
    if (strcmp(key, "sustain")        == 0) return snprintf(buf, buf_len, "%.4f", inst->sustain);
    if (strcmp(key, "release")        == 0) return snprintf(buf, buf_len, "%.4f", inst->release);
    if (strcmp(key, "osc_mix")        == 0) return snprintf(buf, buf_len, "%.4f", inst->osc_mix);
    if (strcmp(key, "portamento")     == 0) return snprintf(buf, buf_len, "%.4f", inst->portamento);
    if (strcmp(key, "noise_mix")      == 0) return snprintf(buf, buf_len, "%.4f", inst->noise_mix);
    if (strcmp(key, "mod_depth_env")   == 0) return snprintf(buf, buf_len, "%.4f", inst->mod_depth_env);
    if (strcmp(key, "mod_depth_noise") == 0) return snprintf(buf, buf_len, "%.4f", inst->mod_depth_noise);
    if (strcmp(key, "vel_to_filter")  == 0) return snprintf(buf, buf_len, "%.4f", inst->vel_to_filter);
    if (strcmp(key, "noise_type")     == 0) return snprintf(buf, buf_len, "%s", NOISE_TYPE_NAMES[inst->noise_type]);
    if (strcmp(key, "filter_type")    == 0) return snprintf(buf, buf_len, "%s", FILTER_TYPE_NAMES[inst->filter_type]);
    if (strcmp(key, "legato")         == 0) return snprintf(buf, buf_len, "%s", inst->legato ? "On" : "Off");
    if (strcmp(key, "preset")         == 0) return snprintf(buf, buf_len, "%s", PRESETS[inst->current_preset].name);

    if (strcmp(key, "patch_mode") == 0)
        return snprintf(buf, buf_len, "%s", inst->patch_mode ? "Patch" : "Play");
    if (strcmp(key, "matrix_reset") == 0)
        return snprintf(buf, buf_len, "%.4f", 0.0f);
    if (strcmp(key, "rnd_denis") == 0)
        return snprintf(buf, buf_len, "%.4f", 0.0f);
    if (strcmp(key, "rnd_mod") == 0)
        return snprintf(buf, buf_len, "%.4f", 0.0f);
    if (strcmp(key, "rnd_patch") == 0)
        return snprintf(buf, buf_len, "%.4f", 0.0f);

    /* Matrix params: mat_R_C */
    if (strncmp(key, "mat_", 4) == 0 && strlen(key) == 7) {
        int r = key[4] - '0', c = key[6] - '0';
        if (r >= 0 && r < MOD_ROWS && c >= 0 && c < MOD_COLS)
            return snprintf(buf, buf_len, "%.4f", inst->menu_matrix[r][c]);
        return -1;
    }

    /* ---- Knob overlay: knob_N_name (page-aware) ---- */
    if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_name")) {
        int knob_num = atoi(key + 5) - 1;
        if (knob_num >= 0 && knob_num < 8) {
            knob_def_t def;
            get_page_knob(inst, inst->current_page, knob_num, &def);
            if (def.name[0]) return snprintf(buf, buf_len, "%s", def.name);
        }
        return -1;
    }

    /* ---- Knob overlay: knob_N_value (page-aware) ---- */
    if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_value")) {
        int knob_num = atoi(key + 5) - 1;
        if (knob_num >= 0 && knob_num < 8) {
            knob_def_t def;
            get_page_knob(inst, inst->current_page, knob_num, &def);
            if (def.ptr) {
                /* Bipolar matrix knobs: show signed percent */
                if (def.min < 0.0f) {
                    return snprintf(buf, buf_len, "%+d%%", (int)(*def.ptr * 100.0f));
                }
                switch (def.display) {
                    case 1: return snprintf(buf, buf_len, "%.2f", *def.ptr);
                    case 2: return snprintf(buf, buf_len, "%.2f", *def.ptr);
                    default: return snprintf(buf, buf_len, "%d%%", (int)(*def.ptr * 100.0f));
                }
            }
            /* Enum / action knobs */
            if (inst->current_page == 0 && knob_num == 6)
                return snprintf(buf, buf_len, "%s", NOISE_TYPE_NAMES[inst->noise_type]);
            if (inst->current_page == 2 && knob_num == 4)
                return snprintf(buf, buf_len, "Turn:rnd");
            if (inst->current_page == 2 && knob_num == 5)
                return snprintf(buf, buf_len, "Turn:rnd mod");
            if (inst->current_page == 2 && knob_num == 6)
                return snprintf(buf, buf_len, "Turn:reset");
            if (inst->current_page == 2 && knob_num == 7)
                return snprintf(buf, buf_len, "Turn:rnd all");
        }
        return -1;
    }

    return -1;
}

/* ── Audio processing ────────────────────────────────────────────────────── */
/*
 * Per-sample modulation for OSC1 pitch, OSC2 pitch, and filter cutoff.
 * Per-block modulation for timbre, harmonics, fold, output level.
 * This enables audio-rate LFO modulation (up to 500Hz) for metallic FM timbres.
 */

static void render_block(void *instance, int16_t *out_lr, int frames) {
    serge_instance_t *inst = (serge_instance_t *)instance;

    read_shift_from_shm(inst);
    smooth_params(inst);

    /* Merge matrix layers: menu (bipolar) + pad (positive additive) */
    int any_pad_active = 0;
    float max_pad_vel = 0.0f;
    for (int r = 0; r < MOD_ROWS; r++)
        for (int c = 0; c < MOD_COLS; c++) {
            inst->mod_matrix[r][c] = fmaxf(-1.0f, fminf(1.0f,
                inst->menu_matrix[r][c] + inst->pad_matrix[r][c]));
            if (inst->pad_matrix[r][c] > 0.0f) {
                any_pad_active = 1;
                if (inst->pad_matrix[r][c] > max_pad_vel)
                    max_pad_vel = inst->pad_matrix[r][c];
            }
        }

    /* Pad velocity gently scales GTO speed */
    float gto_speed_mult = 1.0f;
    if (any_pad_active) {
        float v = sqrtf(max_pad_vel);
        gto_speed_mult = 1.0f + v * 1.5f;
    }

    /* Per-block modulated params (slow targets) */
    modulated_params_t mp;
    compute_modulated_params(inst, &mp);

    /* Analog drift */
    {
        inst->drift_phase1 += 0.003f;
        inst->drift_phase2 += 0.0023f;
        if (inst->drift_phase1 >= 1.0f) {
            inst->drift_phase1 -= 1.0f;
            inst->drift_rng ^= inst->drift_rng << 13;
            inst->drift_rng ^= inst->drift_rng >> 17;
            inst->drift_rng ^= inst->drift_rng << 5;
            inst->drift_target1 = ((float)(int32_t)inst->drift_rng / 2147483648.0f) * 6.0f;
        }
        if (inst->drift_phase2 >= 1.0f) {
            inst->drift_phase2 -= 1.0f;
            inst->drift_rng ^= inst->drift_rng << 13;
            inst->drift_rng ^= inst->drift_rng >> 17;
            inst->drift_rng ^= inst->drift_rng << 5;
            inst->drift_target2 = ((float)(int32_t)inst->drift_rng / 2147483648.0f) * 6.0f;
        }
        inst->drift_value1 += 0.002f * (inst->drift_target1 - inst->drift_value1);
        inst->drift_value2 += 0.002f * (inst->drift_target2 - inst->drift_value2);
    }
    float drift1_ratio = powf(2.0f, inst->drift_value1 / 1200.0f);
    float drift2_ratio = powf(2.0f, inst->drift_value2 / 1200.0f);

    /* Per-block frequency prep */
    float detune_oct = (inst->sm_osc1_freq - 0.5f) * 2.0f;
    float bend_semi = inst->pitch_bend;

    /* Pre-compute per-sample mod weights for fast targets */
    float w_pitch1[MOD_ROWS], w_pitch2[MOD_ROWS], w_cutoff[MOD_ROWS];
    for (int r = 0; r < MOD_ROWS; r++) {
        w_pitch1[r] = inst->mod_matrix[r][TGT_OSC1_PITCH];
        w_pitch2[r] = inst->mod_matrix[r][TGT_OSC2_PITCH];
        w_cutoff[r] = inst->mod_matrix[r][TGT_FILTER_CUTOFF] * 6000.0f;
    }

    /* Base filter cutoff (encoder + velocity-to-filter) */
    float cutoff_shaped = inst->sm_filter_cutoff * inst->sm_filter_cutoff;
    float base_cutoff = 30.0f * powf(20000.0f / 30.0f, cutoff_shaped)
                      + inst->velocity * inst->vel_to_filter * 6000.0f;

    for (int i = 0; i < frames; i++) {
        float sample = 0.0f;

        /* GTO+ tick with independent LFO and S&H rates */
        float gto_smooth, gto_stepped;
        gto_tick(&inst->gto, inst->lfo_rate * gto_speed_mult,
                 inst->sh_rate * gto_speed_mult, &gto_smooth, &gto_stepped);

        /* Portamento — slew current_note toward target_note */
        if (inst->portamento > 0.001f) {
            float coeff = 1.0f / (inst->portamento * SAMPLE_RATE + 1.0f);
            float diff = inst->target_note - inst->current_note;
            if (fabsf(diff) < 0.001f)
                inst->current_note = inst->target_note;
            else
                inst->current_note += coeff * diff;
        } else {
            inst->current_note = inst->target_note;
        }

        if (inst->note_active || inst->env.stage == 3) {
            /* Per-sample mod source values */
            float src[MOD_ROWS];
            src[SRC_ENVELOPE]    = (inst->mod_src[SRC_ENVELOPE] * 2.0f - 1.0f) * inst->mod_depth_env;
            src[SRC_GTO_SMOOTH]  = inst->mod_src[SRC_GTO_SMOOTH];
            src[SRC_GTO_STEPPED] = inst->mod_src[SRC_GTO_STEPPED];
            src[SRC_NOISE]       = inst->mod_src[SRC_NOISE] * inst->mod_depth_noise;

            /* OSC1 frequency (per-sample: portamento + pitch modulation) */
            float midi_hz = 440.0f * powf(2.0f, (inst->current_note - 69.0f) / 12.0f);
            float mod_oct = 0.0f;
            for (int r = 0; r < MOD_ROWS; r++)
                mod_oct += src[r] * w_pitch1[r];
            float osc1_hz = midi_hz * powf(2.0f, detune_oct + bend_semi / 12.0f + mod_oct) * drift1_ratio;
            osc1_hz = fmaxf(10.0f, fminf(20000.0f, osc1_hz));

            /* OSC2 frequency (per-sample) */
            float midi_cv = (inst->current_note - 60.0f) / 12.0f;
            float osc2_cv = midi_cv + inst->sm_osc2_pitch + bend_semi / 12.0f;
            for (int r = 0; r < MOD_ROWS; r++)
                osc2_cv += src[r] * w_pitch2[r];
            osc2_cv = fmaxf(-5.0f, fminf(7.0f, osc2_cv));
            float osc2_hz = cv_to_freq(osc2_cv) * drift2_ratio;
            osc2_hz = fmaxf(10.0f, fminf(20000.0f, osc2_hz));

            /* Filter cutoff (per-sample: real-time mod sources) */
            float cutoff_hz = base_cutoff;
            for (int r = 0; r < MOD_ROWS; r++)
                cutoff_hz += src[r] * w_cutoff[r];
            cutoff_hz = fmaxf(20.0f, fminf(20000.0f, cutoff_hz));

            /* Oscillators */
            float s1 = complex_osc_sample(&inst->osc1, osc1_hz, mp.osc1_timbre);
            float s2 = nto_sample(&inst->osc2, osc2_hz, mp.osc2_harmonics);

            /* Noise */
            float noise_val = 0.0f;
            switch (inst->noise_type) {
                case 0: noise_val = noise_white(&inst->noise) * 0.3f;  break;
                case 1: noise_val = noise_pink(&inst->noise)  * 1.12f; break;
                case 2: noise_val = noise_brown(&inst->noise)  * 2.4f; break;
            }

            /* Mix oscillators then crossfade with noise */
            float osc_mixed = s1 * (1.0f - inst->sm_osc_mix) + s2 * inst->sm_osc_mix;
            float nm_scaled = inst->sm_noise_mix * 0.5f;
            float mixed = osc_mixed * (1.0f - nm_scaled) + noise_val * nm_scaled;

            /* Wave Multiplier */
            float folded = wave_multiplier_process(&inst->wm, mixed, mp.fold_depth, mp.fold_type);

            /* SVF Filter (LP/BP/HP/Notch) — per-sample cutoff for audio-rate mod */
            float filtered = svf_process(&inst->filter, folded,
                                         cutoff_hz, inst->sm_filter_q,
                                         inst->filter_type);

            /* ADSR envelope */
            float env = adsr_tick(&inst->env, inst->attack, inst->decay,
                                  inst->sustain, inst->release);

            /* VCA */
            sample = filtered * env * inst->velocity * mp.output_level * 0.7f;

            /* Update envelope mod source */
            inst->mod_src[SRC_ENVELOPE] = env;

            if (inst->env.stage == 4) {
                inst->note_active = 0;
            }
        }

        /* These sources always update (even when no note is playing) */
        inst->mod_src[SRC_GTO_SMOOTH]  = gto_smooth;
        inst->mod_src[SRC_GTO_STEPPED] = gto_stepped;

        /* Filtered noise source: white noise through ~7Hz lowpass */
        float noise_mod_raw = noise_white(&inst->noise_mod);
        inst->noise_mod_smooth += 0.001f * (noise_mod_raw - inst->noise_mod_smooth);
        inst->mod_src[SRC_NOISE] = inst->noise_mod_smooth * 3.0f;

        /* Smooth mod sources (used for display, always tracks real sources) */
        for (int r = 0; r < MOD_ROWS; r++)
            inst->mod_src_smooth[r] += 0.01f * (inst->mod_src[r] - inst->mod_src_smooth[r]);

        /* Analog-style soft saturation */
        sample = fast_tanh(sample * 1.1f) * 0.9f;

        /* Write stereo int16 */
        sample = fmaxf(-1.0f, fminf(1.0f, sample));
        int16_t s = (int16_t)(sample * 32767.0f);
        out_lr[i * 2]     = s;
        out_lr[i * 2 + 1] = s;
    }
}

/* ── API v2 export ───────────────────────────────────────────────────────── */

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

__attribute__((visibility("default")))
plugin_api_v2_t* move_plugin_init_v2(const void *host) {
    g_host = host;
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
