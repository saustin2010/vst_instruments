/* Minimal iterative radix-2 complex FFT — build-time only (mip construction),
 * never on the audio path. Replaces juce::dsp::FFT in the gin port.
 *
 * Unnormalized: forward X[k] = sum x[n] e^{-i2pi kn/N}; inverse without the
 * 1/N — callers scale bins themselves (wtBuild copies bins pre-scaled 1/N).
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace tb {

struct Cpx { float re = 0.0f, im = 0.0f; };

inline void fftRadix2(Cpx *a, int n, bool inverse)
{
    /* bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { Cpx t = a[i]; a[i] = a[j]; a[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = (inverse ? 2.0 : -2.0) * M_PI / len;
        const float wr = (float) std::cos(ang), wi = (float) std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                Cpx &u = a[i + k], &v = a[i + k + len / 2];
                float vr = v.re * cr - v.im * ci;
                float vi = v.re * ci + v.im * cr;
                v.re = u.re - vr; v.im = u.im - vi;
                u.re += vr;       u.im += vi;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

} // namespace tb
