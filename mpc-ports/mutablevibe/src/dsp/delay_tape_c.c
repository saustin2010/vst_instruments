/*
 * delay_tape_c.c — Airwindows-inspired Stereo Tape Delay
 *
 * Signal chain (feedback path):
 *   read (lerp) → tone LP → head bump (peak EQ ~60Hz) → DC blocker
 *                → tape sat (cubic soft-clip) → × feedback → write
 *
 * Flutter: sine LFO on read position → prevents comb-filter "phasing"
 * when dry + delay are summed.
 *
 * API: create/free/process, set_time_ms, set_feedback, set_tone,
 *      set_flutter, set_head_bump, set_hipass, flush
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CHANNELS    2
#define MAX_SECS    2.1f          /* 2.1 s max delay */
#define FLUTTER_MAX 13.0f         /* ±13 samples max flutter excursion */

typedef struct {
    float  samplerate;
    int    buf_size;              /* total samples (mono) */
    float *buf;                   /* interleaved stereo: [L,R, L,R, ...] */
    int    pos;                   /* write head */
    float  samp;                  /* current smoothed delay (samples) */

    /* Parameters */
    float  time_ms;
    float  feedback;
    float  tone;                  /* 0=dark .. 1=bright (LP cutoff) */
    float  flutter;               /* 0..1 depth */
    float  head_bump;             /* 0..1 amount */

    /* Tone LP state (stereo) */
    float  lp[CHANNELS];

    /* Head-bump biquad state (stereo, 2-sample history) */
    float  hb_x1[CHANNELS], hb_x2[CHANNELS];
    float  hb_y1[CHANNELS], hb_y2[CHANNELS];
    float  hb_b0, hb_b1, hb_b2;  /* peak EQ coeffs (fixed at 60Hz, Q=0.8) */
    float  hb_a1, hb_a2;

    /* DC blocker state (stereo) */
    float  dc_x[CHANNELS], dc_y[CHANNELS];

    /* Output Hipass (1-pol, auf Wet-Signal) */
    int    hp_on;
    float  hp_coef;
    float  hp_x[CHANNELS], hp_y[CHANNELS];

    /* Flutter LFO */
    double lfo_phase;
    double lfo_inc;               /* radians per sample */

} Delay;

/* ── Biquad peak-EQ coefficients (Audio EQ Cookbook) ─────────────────── */
static void calc_head_bump(Delay *d) {
    float f0    = 60.0f;
    float Q     = 0.8f;
    float dBg   = d->head_bump * 10.0f;           /* 0..10 dB */
    float A     = powf(10.0f, dBg / 40.0f);
    float w0    = 2.0f * (float)M_PI * f0 / d->samplerate;
    float alpha = sinf(w0) / (2.0f * Q);
    float cw    = cosf(w0);
    float a0    = 1.0f + alpha / A;
    d->hb_b0 = (1.0f + alpha * A) / a0;
    d->hb_b1 = (-2.0f * cw)       / a0;
    d->hb_b2 = (1.0f - alpha * A) / a0;
    d->hb_a1 = (-2.0f * cw)       / a0;
    d->hb_a2 = (1.0f - alpha / A) / a0;
}

/* ── Cubic soft-clip (polynomial, Airwindows style) ───────────────────── */
static inline float soft_clip(float x) {
    if      (x >  1.0f) return  1.0f;
    else if (x < -1.0f) return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

/* ── Public API ──────────────────────────────────────────────────────── */

Delay* delay_create(int samplerate) {
    Delay *d = (Delay*)calloc(1, sizeof(Delay));
    if (!d) return NULL;
    d->samplerate = (float)samplerate;
    d->buf_size   = (int)(samplerate * MAX_SECS) + (int)FLUTTER_MAX + 4;
    d->buf        = (float*)calloc(d->buf_size * CHANNELS, sizeof(float));
    if (!d->buf) { free(d); return NULL; }

    d->time_ms    = 375.0f;
    d->feedback   = 0.40f;
    d->tone       = 0.55f;
    d->flutter    = 0.35f;
    d->head_bump  = 0.40f;

    d->samp       = d->time_ms * samplerate / 1000.0f;

    d->lfo_inc    = 2.0 * M_PI * 4.8 / samplerate;  /* 4.8 Hz flutter LFO */
    d->lfo_phase  = 0.0;

    calc_head_bump(d);
    return d;
}

void delay_free(Delay *d) {
    if (d) { free(d->buf); free(d); }
}

void delay_flush(Delay *d) {
    if (!d) return;
    memset(d->buf, 0, d->buf_size * CHANNELS * sizeof(float));
    memset(d->lp,  0, sizeof(d->lp));
    memset(d->hb_x1, 0, sizeof(d->hb_x1));
    memset(d->hb_x2, 0, sizeof(d->hb_x2));
    memset(d->hb_y1, 0, sizeof(d->hb_y1));
    memset(d->hb_y2, 0, sizeof(d->hb_y2));
    memset(d->dc_x, 0, sizeof(d->dc_x));
    memset(d->dc_y, 0, sizeof(d->dc_y));
    memset(d->hp_x, 0, sizeof(d->hp_x));
    memset(d->hp_y, 0, sizeof(d->hp_y));
    d->samp      = d->time_ms * d->samplerate / 1000.0f;
    d->lfo_phase = 0.0;
}

void delay_set_time_ms  (Delay *d, float v) { if(d) d->time_ms   = v; }
void delay_set_feedback (Delay *d, float v) { if(d) d->feedback  = v; }
void delay_set_tone     (Delay *d, float v) { if(d) d->tone      = v; }
void delay_set_flutter  (Delay *d, float v) { if(d) d->flutter   = v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
void delay_set_head_bump(Delay *d, float v) {
    if (!d) return;
    d->head_bump = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    calc_head_bump(d);
}

void delay_set_hipass(Delay *d, float v) {
    /* v: 0..1  (0.29 ≈ 2000 Hz).  0 = aus.
       coef = 1 - v*0.98  (gleiche Formel wie reverb hipass) */
    if (!d) return;
    if (v <= 0.001f) { d->hp_on = 0; return; }
    d->hp_on   = 1;
    d->hp_coef = 1.0f - v * 0.98f;
}

void delay_process(Delay *d, const float *in, float *out, int n) {
    const float target = fmaxf(2.0f,
        fminf((float)(d->buf_size - (int)FLUTTER_MAX - 4),
              d->time_ms * d->samplerate / 1000.0f));
    const float R_DC    = 0.9995f;
    const float flutter_samp = d->flutter * FLUTTER_MAX;

    for (int i = 0; i < n; i++) {
        /* ── Tape-Speed Smoother (per sample, α=0.0016 → ~14ms Glide) ── */
        d->samp += 0.0016f * (target - d->samp);

        /* ── Flutter LFO ────────────────────────────────────────── */
        float lfo = (float)sin(d->lfo_phase) * flutter_samp;
        d->lfo_phase += d->lfo_inc;
        if (d->lfo_phase > 2.0 * M_PI) d->lfo_phase -= 2.0 * M_PI;

        /* ── Fractional read position (lerp) ─────────────────────── */
        float read_f = d->samp + lfo;
        int   read_i = (int)read_f;
        float frac   = read_f - (float)read_i;

        int r0 = d->pos - read_i;
        if (r0 < 0) r0 += d->buf_size;
        int r1 = r0 - 1;
        if (r1 < 0) r1 += d->buf_size;

        for (int ch = 0; ch < CHANNELS; ch++) {
            float del = d->buf[r0 * CHANNELS + ch] * (1.0f - frac)
                      + d->buf[r1 * CHANNELS + ch] * frac;

            /* ── Tone LP (1-pole IIR) ─────────────────────────── */
            d->lp[ch] += d->tone * (del - d->lp[ch]);
            float sig = d->lp[ch];

            /* ── Head Bump (biquad peak EQ ~60Hz) ─────────────── */
            if (d->head_bump > 0.001f) {
                float y = d->hb_b0 * sig
                        + d->hb_b1 * d->hb_x1[ch]
                        + d->hb_b2 * d->hb_x2[ch]
                        - d->hb_a1 * d->hb_y1[ch]
                        - d->hb_a2 * d->hb_y2[ch];
                d->hb_x2[ch] = d->hb_x1[ch]; d->hb_x1[ch] = sig;
                d->hb_y2[ch] = d->hb_y1[ch]; d->hb_y1[ch] = y;
                sig = y;
            }

            /* ── DC Blocker (1-pole HP) ───────────────────────── */
            float dc_y  = sig - d->dc_x[ch] + R_DC * d->dc_y[ch];
            d->dc_x[ch] = sig;
            d->dc_y[ch] = dc_y;
            sig = dc_y;

            /* ── Tape Saturation ──────────────────────────────── */
            sig = soft_clip(sig);

            /* ── Output Hipass (auf Wet-Signal) ──────────────── */
            if (d->hp_on) {
                float hp_in = sig;
                float hp_y  = hp_in - d->hp_x[ch] + d->hp_coef * d->hp_y[ch];
                d->hp_x[ch] = hp_in;
                d->hp_y[ch] = hp_y;
                sig = hp_y;
            }

            /* ── Output & Feedback write ─────────────────────── */
            out[i * CHANNELS + ch] = sig;
            d->buf[d->pos * CHANNELS + ch] = in[i * CHANNELS + ch]
                                            + sig * d->feedback;
        }

        d->pos = (d->pos + 1) % d->buf_size;
    }
}
