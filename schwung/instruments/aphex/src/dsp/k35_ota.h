/**
 * k35_ota.h — Late MS-20 / LM13600-OTA filter pair (LPF + HPF).
 *
 * Sibling to k35.h. Where the early KORG-35 is a Sallen-Key with diodes in
 * the FORWARD path (in-band, asymmetric, "creamy fuzz" break-up at self-osc),
 * the late MS-20 LM13600-based filter has:
 *
 *   1. Diodes (saturation) in the FEEDBACK LOOP — symmetric clipping around
 *      the resonance return path rather than asymmetric in-band saturation.
 *   2. A unity-gain BUFFER between the two LP stages — so LPF2 sees a clean
 *      voltage source instead of loading LPF1 (the buffer is what disqualifies
 *      it from being a strict Sallen-Key topology per Stinchcombe 2014).
 *   3. Resonance develops more linearly across the Peak knob travel, where
 *      K35 only really kicks in past ~3 o'clock.
 *   4. Cleaner self-oscillation tone — howl/whistle vs K35's broken-up fuzz.
 *   5. Lower noise floor (cleaner gain stages).
 *
 * REFERENCES (math only, no code copied):
 *   - T. Stinchcombe, "A Study of the Korg MS10 & MS20 Filters" (2014),
 *     timstinchcombe.co.uk/synth/MS20_study.pdf
 *   - R. Schmitz, "Late MS-20 Filter" schematic, schmitzbits.de/ms20.html
 *   - Korg KLM-307 daughterboard schematics (publicly available)
 *
 * License: MIT, original derivation.
 */

#ifndef APHEX_K35_OTA_H
#define APHEX_K35_OTA_H

#include <math.h>

#ifndef APHEX_K35_OTA_PI
#define APHEX_K35_OTA_PI 3.14159265358979f
#endif

/* Reuse the same state struct as k35.h — both filters need 3 integrators,
 * and this lets aphex.c keep one state field that works for either rev. */
typedef struct {
    float z1, z2, z3;
} k35_ota_t;

static inline void k35_ota_reset(k35_ota_t *f) {
    f->z1 = 0.0f;
    f->z2 = 0.0f;
    f->z3 = 0.0f;
}

/* Symmetric soft clip — the OTA-based circuit is more linear in its NLP
 * than the K35's asymmetric forward-path diodes. Standard Padé tanh, no
 * scaling tricks: gives the cleaner "howl" character at self-osc. */
static inline float k35_ota_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ============================================================================
 *  OTA LOWPASS — 12 dB/oct, late-MS-20 / KLM-307 daughterboard topology
 * ============================================================================
 *
 *  Topology:
 *
 *      in ──▶[+]──▶ LPF1 ──▶ [BUFFER] ──▶ LPF2 ──▶ y_out
 *             ▲                                │
 *             │                                ▼
 *             └── K · NLP(symmetric) ◀──── HPF1
 *
 *  The buffer between LPF1 and LPF2 means LPF2's input is just y1 (the buffer
 *  is unity-gain, no loading), so the cascaded transfer is two independent
 *  one-poles in series — cleaner than K35 where the second pole loads the first.
 *
 *  Saturation lives ON the feedback path itself (after HPF1, before the K
 *  multiplier reaches the input mix), so distortion is confined to the
 *  resonance return, not the through-signal. This produces the cleaner self-osc.
 *
 *  Linear loop solution (with HPF1 reading from y1 = LPF1 out, like K35 LPF):
 *      u_lin = (in + K·(1-G)·(S1 - z3)) / (1 - K·(1-G)·G)
 *
 *  Then we APPLY tanh to the predicted feedback, recompute u, run integrators.
 *  Crucially, K maps more LINEARLY here — resonance grows steadily through the
 *  knob travel, instead of K35's perceptually-late-onset curve.
 */
static inline float k35_ota_lpf_tick(k35_ota_t *f, float in, float cutoff_hz, float reso, float sr) {
    /* OTA-style K mapping: more linear, hits self-osc near reso=0.95.
     * Where K35 maps reso² to K (peak develops late), OTA maps closer to
     * sqrt(reso) so the perceived resonance grows earlier. */
    float r = reso < 0.0f ? 0.0f : (reso > 1.0f ? 1.0f : reso);
    /* sqrt-shaped curve gives more action in the bottom 2/3 of the knob */
    float K = sqrtf(r) * 1.99f;

    float fc = cutoff_hz;
    float fc_max = sr * 0.49f;
    if (fc > fc_max) fc = fc_max;
    if (fc < 1.0f)   fc = 1.0f;

    float g = tanf(APHEX_K35_OTA_PI * fc / sr);
    float G = g / (1.0f + g);
    float H = 1.0f - G;

    float S1 = f->z1 * H;

    /* === Predictor: linear loop (same form as K35 LPF) === */
    float feed_S    = K * H * (S1 - f->z3);
    float denom     = 1.0f - K * H * G;
    float u_lin     = (in + feed_S) / denom;
    float y_lp1_pre = G * u_lin + S1;
    float y_hp1_pre = H * (y_lp1_pre - f->z3);

    /* === Corrector: symmetric tanh on the feedback signal === */
    float fb = K * k35_ota_tanh(y_hp1_pre);
    float u  = in + fb;

    /* === State updates === */
    /* LPF1 */
    float v1 = (u - f->z1) * G;
    float y1 = v1 + f->z1;
    f->z1    = y1 + v1;

    /* BUFFER (unity gain, no state) → LPF2 input is just y1.
     * In K35 this would be loaded — here it's not, which is the
     * "cleaner" inter-stage relationship. */
    float v2 = (y1 - f->z2) * G;
    float y2 = v2 + f->z2;          /* ← OTA LPF output */
    f->z2    = y2 + v2;

    /* HPF1's internal LP state — input is y1 (LPF1 output) */
    float v3 = (y1 - f->z3) * G;
    float y3 = v3 + f->z3;
    f->z3    = y3 + v3;

    return y2;
}

/* ============================================================================
 *  OTA HIGHPASS — same simplified topology as K35 HPF (Pirkle AN-7v2)
 *  with symmetric NLP and OTA-style K mapping.
 * ============================================================================
 *
 *  The HPF circuit on the late MS-20 is structurally similar to the early
 *  one — Korg only redesigned the LPF when moving to OTAs. But for sonic
 *  consistency between the two REV switches, we mirror the K mapping and
 *  NLP symmetry of the OTA LPF here.
 */
static inline float k35_ota_hpf_tick(k35_ota_t *f, float in, float cutoff_hz, float reso, float sr) {
    float r = reso < 0.0f ? 0.0f : (reso > 1.0f ? 1.0f : reso);
    float K = sqrtf(r) * 1.99f;

    float fc = cutoff_hz;
    float fc_max = sr * 0.49f;
    if (fc > fc_max) fc = fc_max;
    if (fc < 1.0f)   fc = 1.0f;

    float g = tanf(APHEX_K35_OTA_PI * fc / sr);
    float G = g / (1.0f + g);
    float H = 1.0f - G;

    /* Predictor — same form as k35.h */
    float num   = in + K * H * (H * f->z2 - f->z3 - H * G * f->z1);
    float denom = 1.0f - K * H * H * G;
    float u_lin = num / denom;
    float y_pre = H * (u_lin - f->z1);
    float a_pre = G * y_pre + f->z2 * H;
    float b_pre = H * (a_pre - f->z3);

    /* Corrector — symmetric tanh on feedback */
    float fb = K * k35_ota_tanh(b_pre);
    float u  = in + fb;

    float y = H * (u - f->z1);

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

#endif /* APHEX_K35_OTA_H */
