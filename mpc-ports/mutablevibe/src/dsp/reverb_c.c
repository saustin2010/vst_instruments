/*
 * reverb_c.c — FDN Hall Reverb v2
 *
 * Algorithmus: 8-Tap Feedback Delay Network (FDN)
 *   · Delay-Lines 68–136ms  — Resonanz-Modi < 15Hz → kein metallischer Klang
 *   · LFO-Modulation ±20 Samples — dekorreliert Resonanzen effektiv
 *   · Hadamard 8×8 Feedback-Matrix — gleichmässige, lückenlose Rückführung
 *   · 1-Pol LPF-Dämpfung — realistische Höhenabsorption
 *   · Frühe Reflexionen (8 Taps, 5–68ms) — Raumgefühl
 *   · 4× Input-Diffusion (Allpass) — sofortige Dichte
 *
 * Gleiche C-API wie Dattorro-Vorgänger → reverb.py unverändert.
 *
 * compile: gcc -O2 -ffast-math -shared -fPIC -o reverb_c.so reverb_c.c -lm
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FDN_N    8
#define ER_TAPS  8
#define DIFF_N   4
#define BUFSIZE  16384      /* 2^14, ~371ms bei 44100Hz */
#define MASK     (BUFSIZE - 1)

/* ── Ringpuffer ────────────────────────────────────────────────────────── */
typedef struct { float b[BUFSIZE]; int p; } Ring;

static inline void ring_w(Ring *r, float x)  { r->b[r->p++ & MASK] = x; }
static inline float ring_r(Ring *r, int n)   { return r->b[(r->p - 1 - n) & MASK]; }

static inline float ring_rf(Ring *r, float n) {
    int ia   = (int)n;
    float fr = n - (float)ia;
    return ring_r(r, ia) + fr * (ring_r(r, ia + 1) - ring_r(r, ia));
}

/* ── Allpass-Filter ────────────────────────────────────────────────────── */
typedef struct { Ring r; int n; float g; } APF;

static inline float apf_run(APF *a, float x) {
    float d = ring_r(&a->r, a->n);
    float u = x + a->g * d;
    ring_w(&a->r, u);
    return d - a->g * u;
}

/* ── FDN State ─────────────────────────────────────────────────────────── */
typedef struct {
    int   sr;

    /* Pre-Delay */
    Ring  pre;
    int   pre_n;

    /* Input-Helligkeit (1-Pol LPF, "bandwidth") */
    float bw_z;

    /* Input-Diffusion (4 Stufen) */
    APF   diff[DIFF_N];

    /* FDN */
    Ring  dl[FDN_N];
    int   dl_base[FDN_N];
    float gain[FDN_N];
    float damp_z[FDN_N];
    float damp_c;

    /* LFO */
    float lfo_ph[FDN_N];
    float lfo_dt[FDN_N];
    float lfo_depth;

    /* Frühe Reflexionen */
    Ring  er;
    int   er_tap[ER_TAPS];
    float er_g[ER_TAPS];

    /* DC-Block */
    float dc_x, dc_y;

    /* Wet-Hipass */
    float hipass;
    float whp_xL, whp_yL, whp_xR, whp_yR;

    float bandwidth, damping, decay, wet, dry;
} FDNReverb;

/* ── Tabellen ──────────────────────────────────────────────────────────── */

/*
 * FDN Delay-Längen: Primzahlen, 68–136ms bei 44100Hz.
 * Resonanz-Modi: 44100/3001=14.7Hz ... 44100/5987=7.4Hz — nicht hörbar.
 * (Alter Wert 907–1753 Samples = 25–49Hz → metallisch!)
 */
static const int BASE_DL[FDN_N] = { 3001, 3307, 3607, 4001, 4507, 4999, 5501, 5987 };

/* Frühe Reflexionen */
static const float ER_MS[ER_TAPS] = { 5.3f, 11.7f, 19.4f, 27.1f, 36.2f, 45.8f, 56.4f, 68.0f };
static const float ER_GN[ER_TAPS] = { 0.70f, 0.60f, 0.50f, 0.42f, 0.35f, 0.28f, 0.21f, 0.15f };

/* Input-Diffusion: 4 Allpässe (Längen ~3–20ms) */
static const int   DIFF_LEN[DIFF_N] = { 142, 379, 107, 277 };
static const float DIFF_G[DIFF_N]   = { 0.75f, 0.625f, 0.70f, 0.625f };

/* ── Gains aus RT60 ────────────────────────────────────────────────────── */
static void update_gains(FDNReverb *r) {
    /* decay 0.0 → RT60 0.5s, 0.999 → RT60 15s */
    float rt60 = 0.5f * powf(30.0f, r->decay);
    for (int i = 0; i < FDN_N; i++) {
        float len_s = r->dl_base[i] / (float)r->sr;
        r->gain[i]  = powf(10.0f, -3.0f * len_s / rt60);
    }
}

static void update_damp(FDNReverb *r) {
    r->damp_c = 1.0f - r->damping * 0.98f;
}

/* ── Hadamard 8×8 (in-place, normalisiert) ─────────────────────────────── */
static void hadamard8(float *v) {
    for (int j = 0; j < 4; j++) {
        float a = v[j], b = v[j+4]; v[j] = a+b; v[j+4] = a-b;
    }
    for (int i = 0; i < 8; i += 4)
        for (int j = 0; j < 2; j++) {
            float a = v[i+j], b = v[i+j+2]; v[i+j] = a+b; v[i+j+2] = a-b;
        }
    for (int i = 0; i < 8; i += 2) {
        float a = v[i], b = v[i+1]; v[i] = a+b; v[i+1] = a-b;
    }
    const float N = 0.35355339f; /* 1/sqrt(8) */
    for (int i = 0; i < 8; i++) v[i] *= N;
}

/* ── Konstruktor ───────────────────────────────────────────────────────── */
FDNReverb *reverb_create(int sr) {
    FDNReverb *r = calloc(1, sizeof(FDNReverb));
    if (!r) return NULL;
    r->sr = sr;
    float sc = (float)sr / 44100.0f;

    /* Input-Diffusion */
    for (int i = 0; i < DIFF_N; i++) {
        r->diff[i].n = (int)(DIFF_LEN[i] * sc);
        r->diff[i].g = DIFF_G[i];
    }

    /* FDN */
    for (int i = 0; i < FDN_N; i++) {
        r->dl_base[i] = (int)(BASE_DL[i] * sc);
        if (r->dl_base[i] < 8) r->dl_base[i] = 8;
        /* LFO: gestaffelte Phasen, Raten 0.20–0.90 Hz */
        r->lfo_ph[i] = (float)(i * M_PI / FDN_N);
        r->lfo_dt[i] = (float)(2.0 * M_PI * (0.20 + i * 0.10) / sr);
    }
    r->lfo_depth = 20.0f; /* ±20 Samples = ±0.4–0.7% → bricht Resonanzen effektiv auf */

    /* Frühe Reflexionen */
    for (int i = 0; i < ER_TAPS; i++) {
        r->er_tap[i] = (int)(ER_MS[i] * sr / 1000.0f);
        r->er_g[i]   = ER_GN[i];
    }

    r->bandwidth = 0.9995f;   /* fixed — input brightness not exposed */
    r->damping   = 0.0005f;
    r->decay     = 0.70f;
    r->wet       = 0.30f;
    r->dry       = 1.0f;
    r->hipass    = 0.0f;

    update_gains(r);
    update_damp(r);
    return r;
}

void reverb_free(FDNReverb *r) { free(r); }

/* ── Setter ────────────────────────────────────────────────────────────── */
void reverb_set_bandwidth(FDNReverb *r, float v) { r->bandwidth = v; }
void reverb_set_modulate (FDNReverb *r, float v) { r->lfo_depth = v * 50.0f; }
void reverb_set_damping  (FDNReverb *r, float v) { r->damping = v; update_damp(r); }
void reverb_set_decay    (FDNReverb *r, float v) { r->decay = v; update_gains(r); }
void reverb_set_wet      (FDNReverb *r, float v) { r->wet = v; }
void reverb_set_dry      (FDNReverb *r, float v) { r->dry = v; }
void reverb_set_hipass   (FDNReverb *r, float v) { r->hipass = v; }
void reverb_set_predelay (FDNReverb *r, float ms) {
    int n = (int)(ms * r->sr / 1000.0f);
    r->pre_n = (n < 0) ? 0 : (n > BUFSIZE/2 ? BUFSIZE/2 : n);
}

/* ── Verarbeitung ──────────────────────────────────────────────────────── */
void reverb_process(FDNReverb *r, float *in, float *out, int n_frames) {
    for (int i = 0; i < n_frames; i++) {
        float inL = in[i*2], inR = in[i*2+1];
        float mono = 0.5f * (inL + inR);

        /* DC-Block */
        { float y = mono - r->dc_x + 0.995f * r->dc_y;
          r->dc_x = mono; r->dc_y = y; mono = y; }

        /* Pre-Delay */
        ring_w(&r->pre, mono);
        float sig = r->pre_n ? ring_r(&r->pre, r->pre_n) : mono;

        /* Eingangs-Helligkeit */
        r->bw_z = r->bandwidth * sig + (1.0f - r->bandwidth) * r->bw_z;
        sig = r->bw_z;

        /* Input-Diffusion (4 Allpass-Stufen) */
        for (int k = 0; k < DIFF_N; k++)
            sig = apf_run(&r->diff[k], sig);

        /* Frühe Reflexionen */
        ring_w(&r->er, sig);
        float erL = 0.0f, erR = 0.0f;
        for (int k = 0; k < ER_TAPS; k++) {
            float s = ring_r(&r->er, r->er_tap[k]) * r->er_g[k];
            if (k & 1) erR += s; else erL += s;
        }

        /* FDN: Delay-Lines lesen (LFO-moduliert, linear interpoliert) */
        float v[FDN_N];
        for (int k = 0; k < FDN_N; k++) {
            r->lfo_ph[k] += r->lfo_dt[k];
            if (r->lfo_ph[k] > (float)(2.0*M_PI))
                r->lfo_ph[k] -= (float)(2.0*M_PI);
            float age = (float)r->dl_base[k] + r->lfo_depth * sinf(r->lfo_ph[k]);
            if (age < 1.0f) age = 1.0f;
            v[k] = ring_rf(&r->dl[k], age);
        }

        /* Output: vor Feedback (Lines 0/2/4/6 → L, 1/3/5/7 → R) */
        float fdnL = (v[0] + v[2] + v[4] + v[6]) * 0.40f;
        float fdnR = (v[1] + v[3] + v[5] + v[7]) * 0.40f;

        /* Dämpfungs-LPF + Feedback-Gain */
        for (int k = 0; k < FDN_N; k++) {
            r->damp_z[k] = r->damp_c * v[k] + (1.0f - r->damp_c) * r->damp_z[k];
            v[k] = r->damp_z[k] * r->gain[k];
        }

        /* Hadamard: mischt alle 8 Lines gleichmässig */
        hadamard8(v);

        /* Zurückschreiben */
        for (int k = 0; k < FDN_N; k++)
            ring_w(&r->dl[k], sig + v[k]);

        /* Mix: ER + FDN */
        float wetL = erL * 0.35f + fdnL;
        float wetR = erR * 0.35f + fdnR;

        /* Wet-Hipass */
        if (r->hipass > 0.0f) {
            float c = 1.0f - r->hipass * 0.98f;
            float tL = wetL - r->whp_xL + c * r->whp_yL;
            float tR = wetR - r->whp_xR + c * r->whp_yR;
            r->whp_xL = wetL; r->whp_yL = tL;
            r->whp_xR = wetR; r->whp_yR = tR;
            wetL = tL; wetR = tR;
        }

        out[i*2]   = r->dry * inL + r->wet * wetL;
        out[i*2+1] = r->dry * inR + r->wet * wetR;
    }
}
