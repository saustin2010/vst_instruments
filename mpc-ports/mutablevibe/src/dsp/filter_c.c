/*
 * filter_c.c — 4-Pol resonanter Filter mit Morph LP/BP/HP
 *
 * TPT/ZDF State-Variable-Filter (Cytomic/Andrew Simper) als 2×2-Pol-Kaskade, Stereo.
 * g = tan(pi·fc/sr) und k = 1/Q werden pro Sample aus dem geglätteten Cutoff berechnet —
 * jeder Zwischenwert ist ein gültiges, stabiles Filter: kein Zap, keine instabilen
 * Zwischenpole, auch bei Full-Range-Sprüngen.
 *
 * fc_smooth: eingebauter 1-Pol-Cutoff-Smoother (τ ≈ 1 ms) gibt der Cutoff-Modulation
 * per-Sample-Auflösung innerhalb eines Blocks und dämpft den Rest-Zip bei schnellen
 * Env-Sweeps. filter_prime() setzt fc_smooth = hz, sodass kein initialer Sweep entsteht.
 *
 * BP-Normalisierung: k·v1 → 0-dB-Peak (RBJ-kompatibel).
 * Morph-Gewichte werden linear über den Block interpoliert.
 *
 * API identisch zur DF-II-Version (Drop-in-Ersatz).
 */

#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float samplerate;
    float cutoff;
    float resonance;
    float morph;
    float last_morph;
    int   slope;   /* 0 = 12 dB/oct (stage 1 only), 1 = 24 dB/oct (stage 1+2) */

    float fc_smooth;
    float smooth_coeff;   /* 1 - exp(-1 / (0.001 * sr)) */

    /* SVF-Zustände: 2 Stufen × stereo × 2 Integratoren */
    float ic1L1, ic2L1;
    float ic1R1, ic2R1;
    float ic1L2, ic2L2;
    float ic1R2, ic2R2;

    /* gecachte Morph-Gewichte */
    float lp_g, bp_g, hp_g;
} Filter;

static void calc_morph_gains(float m, float *lp_g, float *bp_g, float *hp_g)
{
    if (m <= 0.5f) { *lp_g = 1.0f - 2.0f*m; *bp_g = 2.0f*m;        *hp_g = 0.0f; }
    else            { *lp_g = 0.0f;           *bp_g = 2.0f - 2.0f*m; *hp_g = 2.0f*m - 1.0f; }
}

void *filter_create(float samplerate) {
    Filter *f = calloc(1, sizeof(Filter));
    if (!f) return NULL;
    f->samplerate   = samplerate;
    f->cutoff       = 18000.0f;
    f->fc_smooth    = 18000.0f;
    f->resonance    = 0.0f;
    f->morph        = 0.0f;
    f->last_morph   = 0.0f;
    f->slope        = 1;
    f->smooth_coeff = 1.0f - expf(-1.0f / (0.001f * samplerate));
    calc_morph_gains(0.0f, &f->lp_g, &f->bp_g, &f->hp_g);
    return f;
}

void filter_free(void *ptr) { free(ptr); }

void filter_set_cutoff(void *ptr, float hz)   { ((Filter*)ptr)->cutoff    = hz; }
void filter_set_resonance(void *ptr, float r)  { ((Filter*)ptr)->resonance = r;  }
void filter_set_morph(void *ptr, float m) {
    if (m < 0.0f) m = 0.0f;
    if (m > 1.0f) m = 1.0f;
    ((Filter*)ptr)->morph = m;
}
void filter_set_slope(void *ptr, int slope) {
    ((Filter*)ptr)->slope = (slope <= 0) ? 0 : 1;
}

void filter_flush(void *ptr) {
    Filter *f = (Filter*)ptr;
    f->ic1L1 = f->ic2L1 = f->ic1R1 = f->ic2R1 = 0.0f;
    f->ic1L2 = f->ic2L2 = f->ic1R2 = f->ic2R2 = 0.0f;
}

void filter_prime(void *ptr, float hz, float res, float morph) {
    Filter *f = (Filter*)ptr;
    float lim = f->samplerate * 0.46f;
    if (hz    < 20.f)  hz    = 20.f;   else if (hz    > lim) hz    = lim;
    if (res   < 0.f)   res   = 0.f;    else if (res   > 1.f) res   = 1.f;
    if (morph < 0.f)   morph = 0.f;    else if (morph > 1.f) morph = 1.f;
    filter_flush(ptr);
    f->cutoff     = hz;
    f->fc_smooth  = hz;   /* kein Sweep vom Smoother beim ersten Block nach Note-On */
    f->resonance  = res;
    f->morph      = morph;
    f->last_morph = morph;
    calc_morph_gains(morph, &f->lp_g, &f->bp_g, &f->hp_g);
}

/* Snapped fc_smooth ohne State-Flush — für Note-On mit Key-Tracking.
 * Verhindert den 1ms-Sweep vom alten zum neuen Cutoff, ohne einen Click
 * durch zeroing der Integrator-States zu erzeugen. */
void filter_set_fc_smooth(void *ptr, float hz) {
    Filter *f = (Filter*)ptr;
    if (!f) return;
    float lim = f->samplerate * 0.46f;
    if (hz < 20.f) hz = 20.f; else if (hz > lim) hz = lim;
    f->fc_smooth = hz;
    f->cutoff    = hz;
}

void filter_process(void *ptr, float *in, float *out, int frames) {
    if (!ptr || frames <= 0) return;
    Filter *f = (Filter*)ptr;

    float lim = f->samplerate * 0.46f;
    float fc_target = f->cutoff;
    if (fc_target < 20.0f) fc_target = 20.0f;
    if (fc_target > lim)   fc_target = lim;

    float k      = 1.0f / (0.2f + f->resonance * 2.5f);
    float smooth = f->smooth_coeff;
    float pi_sr  = (float)M_PI / f->samplerate;
    float fc_s   = f->fc_smooth;

    /* Morph: Startwerte und Zielwerte für lineare Interpolation über den Block */
    float o_lp_g = f->lp_g, o_bp_g = f->bp_g, o_hp_g = f->hp_g;
    float n_lp_g, n_bp_g, n_hp_g;
    if (f->morph != f->last_morph) {
        calc_morph_gains(f->morph, &n_lp_g, &n_bp_g, &n_hp_g);
        f->lp_g = n_lp_g; f->bp_g = n_bp_g; f->hp_g = n_hp_g;
        f->last_morph = f->morph;
    } else {
        n_lp_g = o_lp_g; n_bp_g = o_bp_g; n_hp_g = o_hp_g;
    }

    float ic1L1 = f->ic1L1, ic2L1 = f->ic2L1;
    float ic1R1 = f->ic1R1, ic2R1 = f->ic2R1;
    float ic1L2 = f->ic1L2, ic2L2 = f->ic2L2;
    float ic1R2 = f->ic1R2, ic2R2 = f->ic2R2;

    int use12 = (f->slope == 0);

    float inv_n = 1.0f / (float)frames;

    for (int i = 0; i < frames; i++) {
        /* Cutoff-Smoother: fc_s nähert sich fc_target per Sample */
        fc_s += smooth * (fc_target - fc_s);

        /* SVF-Koeffizienten aus geglättetem Cutoff */
        float g  = tanf(pi_sr * fc_s);
        float gk = g + k;
        float a1 = 1.0f / (1.0f + g * gk);
        float a2 = g * a1;
        float a3 = g * a2;

        /* Morph-Blend linear über Block */
        float t    = (float)(i + 1) * inv_n;
        float lp_g = o_lp_g + t * (n_lp_g - o_lp_g);
        float bp_g = o_bp_g + t * (n_bp_g - o_bp_g);
        float hp_g = o_hp_g + t * (n_hp_g - o_hp_g);

        float xL = in[i*2], xR = in[i*2+1];

        /* Stage 1 — Left */
        {
            float v3 = xL - ic2L1;
            float v1 = a1*ic1L1 + a2*v3;
            float v2 = ic2L1 + a2*ic1L1 + a3*v3;
            ic1L1 = 2.0f*v1 - ic1L1;
            ic2L1 = 2.0f*v2 - ic2L1;
            /* HP = Eingang − k·BP − LP; BP normiert: k·v1 → 0 dB Peak */
            xL = lp_g*v2 + bp_g*(k*v1) + hp_g*(xL - k*v1 - v2);
        }
        /* Stage 1 — Right */
        {
            float v3 = xR - ic2R1;
            float v1 = a1*ic1R1 + a2*v3;
            float v2 = ic2R1 + a2*ic1R1 + a3*v3;
            ic1R1 = 2.0f*v1 - ic1R1;
            ic2R1 = 2.0f*v2 - ic2R1;
            xR = lp_g*v2 + bp_g*(k*v1) + hp_g*(xR - k*v1 - v2);
        }
        float stage1L = xL, stage1R = xR;
        /* Stage 2 always runs (even when bypassed for 12 dB/oct output) so its
         * states stay warm — switching slope live never produces a state-reset click. */
        /* Stage 2 — Left */
        {
            float v3 = xL - ic2L2;
            float v1 = a1*ic1L2 + a2*v3;
            float v2 = ic2L2 + a2*ic1L2 + a3*v3;
            ic1L2 = 2.0f*v1 - ic1L2;
            ic2L2 = 2.0f*v2 - ic2L2;
            float stage2L = lp_g*v2 + bp_g*(k*v1) + hp_g*(xL - k*v1 - v2);
            out[i*2] = use12 ? stage1L : stage2L;
        }
        /* Stage 2 — Right */
        {
            float v3 = xR - ic2R2;
            float v1 = a1*ic1R2 + a2*v3;
            float v2 = ic2R2 + a2*ic1R2 + a3*v3;
            ic1R2 = 2.0f*v1 - ic1R2;
            ic2R2 = 2.0f*v2 - ic2R2;
            float stage2R = lp_g*v2 + bp_g*(k*v1) + hp_g*(xR - k*v1 - v2);
            out[i*2+1] = use12 ? stage1R : stage2R;
        }
    }

    f->ic1L1 = ic1L1; f->ic2L1 = ic2L1;
    f->ic1R1 = ic1R1; f->ic2R1 = ic2R1;
    f->ic1L2 = ic1L2; f->ic2L2 = ic2L2;
    f->ic1R2 = ic1R2; f->ic2R2 = ic2R2;
    f->fc_smooth = fc_s;
}
