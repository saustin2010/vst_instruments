/**
 * k35.h — Korg-35 filter pair (LPF + HPF), clean-room implementation.
 *
 * Sallen-Key topology with positive feedback for resonance and soft saturation
 * for character. Both filters self-oscillate near K=2.0 (resonance=1.0) and
 * exhibit the gritty asymmetric response that defines the MS-10 / MS-20 sound.
 *
 * APPROACH
 *   1. Resolve the delay-less feedback loop algebraically (linear case) using
 *      Zavalishin TPT one-pole forms with each stage as `y_LP = G·x + z·(1-G)`.
 *   2. Use that linear prediction to compute the feedback signal (HPF1 out for
 *      the LPF, HPF3 out for the HPF).
 *   3. Apply soft saturation (Padé tanh) ONLY to the feedback signal.
 *   4. Recompute the loop input u with the saturated feedback, then run the
 *      three integrators with that u.
 *
 *   This keeps unity DC gain at K=0, lets the filter self-oscillate cleanly
 *   at K≈2, and confines saturation to the resonance path.
 *
 * REFERENCES (math only — no code copied from any GPL/copyleft source)
 *   - Will Pirkle, AN-5: VA Korg35 LPF v3.5
 *   - Will Pirkle, AN-7 v2.0: VA Korg35 HPF Simplified
 *   - Vadim Zavalishin, "The Art of VA Filter Design" (TPT framework)
 *
 * License: MIT (matches the parent Aphex module).
 */

#ifndef APHEX_K35_H
#define APHEX_K35_H

#include <math.h>

#ifndef APHEX_K35_PI
#define APHEX_K35_PI 3.14159265358979f
#endif

typedef struct {
    float z1, z2, z3;   /* TPT integrator states (LP-side memories of each one-pole) */
} k35_t;

static inline void k35_reset(k35_t *f) {
    f->z1 = 0.0f;
    f->z2 = 0.0f;
    f->z3 = 0.0f;
}

/* Soft saturation curve — Padé tanh with extended linear range.
 * Slope ≈ 1.0 at origin (preserves loop gain at small signals so resonance can
 * "sing"), saturates gradually as amplitude grows. The /0.7 scaling stretches
 * the linear region so self-oscillation builds up cleanly before being
 * compressed. Compare plain tanh which compresses too hard at |x|≈1. */
static inline float k35_tanh(float x) {
    float xs = x * 0.7f;
    float x2 = xs * xs;
    return xs * (27.0f + x2) / (27.0f + 9.0f * x2) * (1.0f / 0.7f);
}

/* ============================================================================
 *  K35 LOWPASS — 12 dB/oct, derived from Pirkle AN-5
 * ============================================================================
 *
 *  Topology (the resonance feedback HPF reads from LPF1, NOT LPF2 — this is
 *  the critical detail that places phase=0 at cutoff and gives K=2 self-osc):
 *
 *      in ──▶[+]──▶ LPF1 ──┬──▶ LPF2 ──▶ y_out
 *             ▲            │
 *             │            ▼
 *             └── K · NLP ◀── HPF1
 *
 *  Loop transfer (analog): T(s) = K · s/(1+s)². Phase=0 at ω=ωc, |T|=K/2 there,
 *  so self-osc at K=2.
 *
 *  Each TPT 1-pole has the affine form (g = tan(π·fc/fs), G = g/(1+g)):
 *      y_LP = G·x + z·(1-G)
 *      y_HP = (1-G)·(x - z)
 *      state update: v = (x-z)·G, y = v+z, z := y+v
 *
 *  Linear loop solution (substituting LPF1 output into HPF1):
 *      u_linear = (in + K·(1-G)·(S1 - z3)) / (1 - K·(1-G)·G)
 *
 *  We use that to predict y_hp1 (the resonance feedback signal), apply tanh
 *  to that signal alone, then recompute u from saturated feedback.
 */
static inline float k35_lpf_tick(k35_t *f, float in, float cutoff_hz, float reso, float sr) {
    /* Map normalized resonance 0..1 → K 0..1.99 (clamp below 2.0 self-osc singularity). */
    float K = reso < 0.0f ? 0.0f : (reso > 1.0f ? 1.0f : reso);
    K *= 1.99f;

    /* Clamp cutoff away from Nyquist. */
    float fc = cutoff_hz;
    float fc_max = sr * 0.49f;
    if (fc > fc_max) fc = fc_max;
    if (fc < 1.0f)   fc = 1.0f;

    float g = tanf(APHEX_K35_PI * fc / sr);
    float G = g / (1.0f + g);
    float H = 1.0f - G;             /* H = 1/(1+g) */

    float S1 = f->z1 * H;           /* LPF1 zero-input output */

    /* === Predictor: linear loop solution === */
    float feed_S    = K * H * (S1 - f->z3);
    float denom     = 1.0f - K * H * G;
    float u_lin     = (in + feed_S) / denom;
    float y_lp1_pre = G * u_lin + S1;                       /* LPF1 prediction */
    float y_hp1_pre = H * (y_lp1_pre - f->z3);              /* HPF1 = feedback */

    /* === Corrector: saturate ONLY the feedback signal === */
    float fb = K * k35_tanh(y_hp1_pre);
    float u  = in + fb;

    /* === State updates with corrected u === */
    /* LPF1 */
    float v1 = (u - f->z1) * G;
    float y1 = v1 + f->z1;
    f->z1    = y1 + v1;

    /* LPF2 — input is LPF1 output (y1) */
    float v2 = (y1 - f->z2) * G;
    float y2 = v2 + f->z2;          /* ← K35 LPF output */
    f->z2    = y2 + v2;

    /* HPF1's internal LP state — input is y1 (LPF1 output), not y2 */
    float v3 = (y1 - f->z3) * G;
    float y3 = v3 + f->z3;
    f->z3    = y3 + v3;

    return y2;
}

/* ============================================================================
 *  K35 HIGHPASS — 6 dB/oct on bass side, 12 dB/oct above resonance.
 *  Derived from Pirkle AN-7 v2.0 simplified topology.
 * ============================================================================
 *
 *  Topology:
 *
 *      in ──▶[+]──▶ HPF1 ──▶ y_out
 *             ▲              │
 *             │              ▼
 *             └── K · NLP ◀── HPF3 ◀── LPF1
 *
 *  Using y = HPF1(u) = (1-G)·(u - z1):
 *      u_linear = (in + K·(1-G)·((1-G)·z2 - z3 - (1-G)·G·z1)) / (1 - K·(1-G)²·G)
 *
 *  Predict y_pre, then feedback path:
 *      a_pre = LPF1(y_pre) = G·y_pre + z2·(1-G)
 *      b_pre = HPF3(a_pre) = (1-G)·(a_pre - z3)
 *  Saturate b_pre, recompute u, run the integrators.
 */
static inline float k35_hpf_tick(k35_t *f, float in, float cutoff_hz, float reso, float sr) {
    float K = reso < 0.0f ? 0.0f : (reso > 1.0f ? 1.0f : reso);
    K *= 1.99f;

    float fc = cutoff_hz;
    float fc_max = sr * 0.49f;
    if (fc > fc_max) fc = fc_max;
    if (fc < 1.0f)   fc = 1.0f;

    float g = tanf(APHEX_K35_PI * fc / sr);
    float G = g / (1.0f + g);
    float H = 1.0f - G;

    /* === Predictor: linear loop solution === */
    float num   = in + K * H * (H * f->z2 - f->z3 - H * G * f->z1);
    float denom = 1.0f - K * H * H * G;
    float u_lin = num / denom;
    float y_pre = H * (u_lin - f->z1);              /* predicted HPF1 output */
    float a_pre = G * y_pre + f->z2 * H;            /* predicted LPF1 output */
    float b_pre = H * (a_pre - f->z3);              /* predicted HPF3 output = feedback */

    /* === Corrector: saturate feedback only === */
    float fb = K * k35_tanh(b_pre);
    float u  = in + fb;

    /* === Output and state updates === */
    float y = H * (u - f->z1);                      /* K35 HPF output */

    float v1 = (u - f->z1) * G;
    float y1 = v1 + f->z1;
    f->z1    = y1 + v1;

    float v2 = (y - f->z2) * G;
    float y2 = v2 + f->z2;
    f->z2    = y2 + v2;

    float v3 = (y2 - f->z3) * G;
    float y3 = v3 + f->z3;
    f->z3    = y3 + v3;

    return y;
}

#endif /* APHEX_K35_H */
