/**
 * yin.h — YIN pitch tracker, clean-room implementation.
 *
 * Reference (math only):
 *   A. de Cheveigné & H. Kawahara, "YIN, a fundamental frequency estimator
 *   for speech and music", J. Acoust. Soc. Am. 111(4), April 2002.
 *
 * Algorithm:
 *   1. Difference function:        d(τ) = Σ (x[i] - x[i+τ])², i ∈ [0, W)
 *   2. Cumulative mean normalized: d'(τ) = d(τ) · τ / Σⱼ₌₁..τ d(j)
 *   3. Absolute threshold: smallest τ where d'(τ) < threshold AND is a local
 *      minimum (descend further while d' keeps dropping).
 *   4. Parabolic interpolation around the chosen τ for sub-sample accuracy.
 *
 * Tuning notes:
 *   - Threshold 0.10..0.15 per the paper. We default to 0.15 (slightly more
 *     forgiving for noisy/non-harmonic input like the MS-20 ESP path).
 *   - If no τ crosses threshold, fall back to the global minimum and reject
 *     entirely if its CMNDF value > 0.5 (likely no pitch present).
 *
 * License: MIT, original derivation.
 */

#ifndef APHEX_YIN_H
#define APHEX_YIN_H

#include <math.h>

/**
 * yin_analyze — single-shot pitch detection on a circular buffer.
 *
 * @param buf         Circular sample buffer of length buf_len (must be power of 2)
 * @param buf_pos     Most-recent write position (next sample would land here)
 * @param buf_mask    buf_len - 1 (e.g. 2047 for a 2048-sample buffer)
 * @param window      Analysis window length W (e.g. 1024)
 * @param tau_min     Minimum lag (sets max detectable freq = sr / tau_min)
 * @param tau_max     Maximum lag (sets min detectable freq = sr / tau_max).
 *                    Buffer must be ≥ window + tau_max samples.
 * @param threshold   YIN absolute threshold, typically 0.10..0.15
 * @param scratch     Caller-provided scratch buffer of length tau_max,
 *                    used to hold CMNDF values
 * @param out_tau     If a pitch is found, the refined (sub-sample) period;
 *                    otherwise unchanged
 * @return            1 if pitch found, 0 if no convincing pitch
 */
static inline int yin_analyze(
    const float *buf, int buf_pos, int buf_mask,
    int window, int tau_min, int tau_max,
    float threshold,
    float *scratch,
    float *out_tau)
{
    /* Start of the analysis window — far enough back that buf[start+W+tau_max] is valid */
    int start = (buf_pos - (window + tau_max)) & buf_mask;

    /* Step 1+2 fused: compute d(τ) and CMNDF in one pass. */
    float running_sum = 0.0f;
    scratch[0] = 1.0f;  /* By definition d'(0)=1 in YIN */
    for (int tau = 1; tau < tau_max; tau++) {
        float d = 0.0f;
        for (int j = 0; j < window; j++) {
            float a = buf[(start + j) & buf_mask];
            float b = buf[(start + j + tau) & buf_mask];
            float diff = a - b;
            d += diff * diff;
        }
        running_sum += d;
        scratch[tau] = (running_sum > 1e-9f)
            ? d * (float)tau / running_sum
            : 1.0f;
    }

    /* Step 3: smallest τ in [tau_min, tau_max-1) where d' < threshold and is
     * a local minimum (walk down while still descending). */
    int best_tau = -1;
    for (int tau = tau_min; tau < tau_max - 1; tau++) {
        if (scratch[tau] < threshold) {
            while (tau + 1 < tau_max - 1 && scratch[tau + 1] < scratch[tau]) {
                tau++;
            }
            best_tau = tau;
            break;
        }
    }
    if (best_tau < 0) {
        /* Fallback: global minimum, but reject if unconvincing */
        float best_v = scratch[tau_min];
        best_tau = tau_min;
        for (int tau = tau_min + 1; tau < tau_max; tau++) {
            if (scratch[tau] < best_v) {
                best_v = scratch[tau];
                best_tau = tau;
            }
        }
        if (best_v > 0.5f) return 0;
    }

    /* Step 4: parabolic interpolation around best_tau */
    if (best_tau <= 0 || best_tau >= tau_max - 1) {
        *out_tau = (float)best_tau;
    } else {
        float s0 = scratch[best_tau - 1];
        float s1 = scratch[best_tau];
        float s2 = scratch[best_tau + 1];
        float denom = 2.0f * (s0 - 2.0f * s1 + s2);
        float tau_refined = (float)best_tau;
        if (fabsf(denom) > 1e-9f) {
            tau_refined += (s0 - s2) / denom;
        }
        *out_tau = tau_refined;
    }
    return 1;
}

#endif /* APHEX_YIN_H */
