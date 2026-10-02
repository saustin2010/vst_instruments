/* Pot -> musical range mappings (the ER-99 doctrine: the UI shows pot
 * positions 0..127; every curve lives here, in one place).
 */
#pragma once

#include <cmath>

namespace tb {

inline float pot01(float v)        { return v / 127.0f; }                 /* 0..1 */
inline float potBipolar(float v)   { return (v - 64.0f) / 63.0f; }        /* -1..1 (64=0) */
inline float potSquared(float v)   { float x = pot01(v); return x * x; }  /* levels */

/* Envelope stage time: 1 ms .. 10 s, exponential. */
inline float potEnvTime(float v)
{
    return std::pow(10.0f, pot01(v) * 4.0f - 3.0f);
}

/* ATTACK gets its own curve, weighted to the low end the way the original
 * does: it declares attack as a JUCE range with skew 0.2, which is x^5, so a
 * quarter turn there is ~60 ms. The shared exponential above put a quarter
 * turn at 10 ms and put the whole 1-40 ms range inside the first third of the
 * knob -- reported as "0 to 50 have a fair amount of clickiness, I'm having to
 * use the LP filter to attenuate the click".
 *
 * Same shape as the original, our 10 s ceiling rather than its 60 s (a minute
 * of attack is not a thing anyone reaches for on this box, and it would cost
 * half the knob). The 0.5 ms floor keeps a truly instant attack available at 0
 * for percussive sounds. */
inline float potAttackTime(float v)
{
    const float x = pot01(v);
    const float x5 = x * x * x * x * x;
    return 0.0005f + 10.0f * x5;
}

/* LFO rate: 0.02 .. 20 Hz, exponential. */
inline float potLfoRate(float v)
{
    return 0.02f * std::pow(1000.0f, pot01(v));
}

/* Filter cutoff in MIDI-note units (the original's parameter space):
 * pot 0..127 -> note 8..135  (~13 Hz .. ~20 kHz). */
inline float potFilterNote(float v)
{
    return 8.0f + pot01(v) * 127.0f;
}

/* Glide time: 1 ms .. 2 s, exponential. */
inline float potGlideTime(float v)
{
    return 0.001f * std::pow(2000.0f, pot01(v));
}

/* Unison detune across the stack, semitones (0..1 like the original's range). */
inline float potDetune(float v)    { return pot01(v); }

} // namespace tb
