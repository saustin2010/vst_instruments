/*
 * hank_curves.h -- every parameter -> physical mapping, in one place.
 *
 * The shapes are deliberately plain: exponential for times, squared for depths,
 * linear for damping. The character of this synth lives in the macro layer
 * above -- what BRIGHT and BITE and DECAY actually move, and how the two
 * envelopes are coupled -- not in a set of curve coefficients nobody can read.
 *
 * Ranges are chosen for musical reach and then checked by measurement, which is
 * not the same as being fitted: tools/hank-bench/macro_sweep.py asks whether
 * each knob stays useful across its whole travel, and more than one constant
 * here was moved because it did not.
 *
 * MIT License
 */
#ifndef HANK_CURVES_H
#define HANK_CURVES_H

#include <math.h>

namespace hank {
namespace curves {

static const float SAMPLE_RATE_HZ = 44100.0f;

/* ---------------------------------------------------------- envelopes --- */
/*
 * Times are exponential in the control, which is how time is heard: equal
 * turns give equal RATIOS, so the useful range is spread evenly across the
 * knob instead of bunching at one end.
 *
 * THE RANGE IS THE WHOLE DESIGN, AND THE FIRST ONE WAS FAR TOO WIDE.
 *
 * It ran 1.5 ms to 8 s: 5333:1, 12.4 doublings, so the time DOUBLED every 8%
 * of travel and full decay took 55 s to reach -60 dB. The consequence was not
 * subtle. Nothing below ~0.25 was distinguishable from a click and nothing
 * above ~0.7 was usable in a bar, so all 32 factory presets ended up authored
 * between decay 0.26 and 0.66 -- 40% of one knob -- and every one of them was
 * a short-to-medium note. Half the instrument was unreachable, and the bank
 * sounded like it because of it.
 *
 * Decay and attack now have SEPARATE ranges, because they want different ones:
 * a 600 ms attack is a slow pad and a 600 ms decay is barely a pluck.
 */
/* 1.5 ms, not 0.5: below about a millisecond an envelope is a click rather
 * than a shape, so a shorter floor only wastes the bottom of the knob. */
static const float ENV_TAU_MIN  = 0.0015f;
static const float ENV_TAU_MAX  = 1.2f;      /* ~8 s to -60 dB: a long pad */
static const float ENV_TAU_SPAN = ENV_TAU_MAX / ENV_TAU_MIN;   /* 800:1 */

static const float ATK_TAU_MIN  = 0.0005f;   /* immediate */
static const float ATK_TAU_MAX  = 0.60f;     /* a slow swell */
static const float ATK_TAU_SPAN = ATK_TAU_MAX / ATK_TAU_MIN;   /* 1200:1 */

inline float expTau(float x, float lo, float span) {
    if (x <= 0.0f) return lo;
    if (x >= 1.0f) return lo * span;
    return lo * powf(span, x);
}

inline float envTau(float x) { return expTau(x, ENV_TAU_MIN, ENV_TAU_SPAN); }

/* The attack is a one-pole aimed past its target so it arrives in finite time
 * rather than crawling asymptotically; 1.25 puts the knee at a musical place. */
/* The attack ramp lasts this many of the knob's time constants -- the span
 * over which the knob's range was chosen -- and every segment ends within
 * ENV_FLOOR (-70 dB) of its target rather than crawling toward it forever.
 * -70 dB, not deeper: the voice ends when its envelope does, and on a
 * noise-led sound (a hat, a shaker) that end point is the length you hear. */
static const float ATTACK_SPAN = 1.609f;
static const float ENV_FLOOR   = 3.16e-4f;
inline float attackTau(float x)  { return expTau(x, ATK_TAU_MIN, ATK_TAU_SPAN); }
inline float decayTau(float x)   { return envTau(x); }
inline float releaseTau(float x) { return envTau(x); }

/*
 * A TIME RATIO IS AN OFFSET ON A LOG CONTROL, NOT A SCALE ON ITS POSITION.
 *
 * The modulator envelope was derived as `op2_d = decay * 0.72`, described as
 * "a little over half the carrier's time constant". It is nothing of the sort.
 * The control is exponential, so scaling the POSITION fixes the exponent and
 * lets the actual time ratio run away with the knob: measured across the old
 * range it went 1.6x at decay 0.2 to 11.1x at 1.0, and across the shipped bank
 * 1.9x to 4.9x.
 *
 * That is a timbre control drifting with a time control, and it produced
 * exactly two families of sound. Short patches got a modulator that barely
 * outlived the carrier -- sustained brightness, i.e. every "growl bass". Long
 * ones got a modulator gone almost immediately -- a sine tail, i.e. every
 * "bell", "mallet" and "sine bass". The bank was not short of ideas; it was
 * being funnelled into two.
 *
 * Offsetting the knob by log(ratio)/log(span) holds the ratio CONSTANT in
 * time, which is what "the modulator decays faster than the carrier" was
 * always supposed to mean. It clamps at the bottom, where both hit the floor
 * together and a short note is simply short -- which is correct.
 */
inline float knobForTimeRatio(float x, float ratio, float span) {
    const float y = x + logf(ratio) / logf(span);
    return y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
}

/* ---------------------------------------------------------------- FM ---- */
/*
 * Index in radians. Squared, so the low end -- where the timbre changes
 * fastest and most usefully -- gets most of the travel. 8 radians is already
 * extreme; past that an FM spectrum is mostly noise.
 */
/* 8 was too timid: at the default BRIGHT of 0.35 it gives 0.98 radians and the
 * whole bank measured at the fundamental -- barely FM at all. 14 puts the
 * default near 1.7 and the top of the knob at a genuinely extreme 14. */
static const float INDEX_MAX = 14.0f;
inline float levelToIndex(float x) { return x * x * INDEX_MAX; }

/* Operator self-feedback, same reasoning, smaller range: a little is grit and
 * a lot is noise, and the interesting part is all in the first third. */
static const float FBK_MAX = 6.0f;
inline float fbkToDepth(float x) { return x * x * FBK_MAX; }

/*
 * ANTI-ALIAS TAPER. A modulator running near Nyquist folds its sidebands back
 * down the spectrum as inharmonic mush, so the index is faded out as the
 * modulator climbs. A raised cosine from 7 kHz to Nyquist: inaudible where it
 * starts, silent where it would otherwise alias.
 */
static const float ROLLOFF_START_HZ = 7000.0f;
inline float modRolloff(float hz) {
    const float nyq = SAMPLE_RATE_HZ * 0.5f;
    if (hz <= ROLLOFF_START_HZ) return 1.0f;
    if (hz >= nyq) return 0.0f;
    const float t = (hz - ROLLOFF_START_HZ) / (nyq - ROLLOFF_START_HZ);
    return 0.5f * (1.0f + cosf((float)M_PI * t));
}

/* ------------------------------------------------------------ filter ---- */
/*
 * Cutoff is exponential over the audible range, and keytracking simply adds
 * the note so a patch keeps its character up the keyboard.
 */
/* 150 Hz, not 20. A tone control that reaches silence is not a tone control:
 * the bottom fifth of the knob was inaudible, and 20 Hz is below the
 * fundamental of nearly every note this will play. 320, because keytracking
 * then halves it again at the bottom of the keyboard. Dark, but always there. */
/* 600, and the keytrack is HALVED with it. At 320 with 1:1 tracking a C3 saw
 * its cutoff land on 160 Hz, and the filter is Q=0.5 at zero resonance -- soft
 * and lossy well below its nominal corner -- so the bottom of TONE cost 20 dB
 * and the first fifth of the knob was unusable. Half-tracking keeps the timbre
 * following the keyboard without putting the corner under the fundamental. */
static const float CUT_MIN_HZ = 600.0f;
static const float CUT_MAX_HZ = 18000.0f;

inline float cutoffHz(float cutoff01, float envAmt, float envVal,
                      bool keytrack, int note) {
    float x = cutoff01 + envVal * envAmt;
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    float hz = CUT_MIN_HZ * powf(CUT_MAX_HZ / CUT_MIN_HZ, x);
    if (keytrack) hz *= powf(2.0f, (float)(note - 60) / 24.0f);
    const float lim = SAMPLE_RATE_HZ * 0.5f - 1000.0f;
    if (hz > lim) hz = lim;
    if (hz < 8.0f)  hz = 8.0f;
    return hz;
}

/* Damping, k = 1/Q. 2.0 is well damped, 0.15 rings hard. Linear, because
 * resonance is heard roughly linearly in this range and a curve would only
 * hide where the interesting part is. */
inline float resToK(float r) {
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    return 2.0f - 1.85f * r;
}

/* --------------------------------------------------------------- BITE --- */
/*
 * Tube asymmetry throughout, with an octave arriving as the knob is pushed.
 * Chosen by ear over three rendered palettes; the reasoning, and the two
 * mistakes baked out of it, are in the design doc and at the call site.
 */
/* 0.42, not 0.50. At 0.50 the blend is 0.5x + |x| - 0.5, which is entirely
 * NON-NEGATIVE -- fully rectified -- so the saturator flattens it to a near
 * constant and the DC blocker then removes the whole signal. The knob went
 * silent in its last fifth. The palette that chose 0.50 hid this because every
 * render was normalised and its DC blocker was eight times slower. */
static const float BITE_OCTAVE = 0.42f;   /* octave share at full travel */
static const float BITE_RAIL   = 0.62f;   /* negative rail -- the asymmetry */

/* BITE must change TONE, not loudness. Without this the drive gain runs the
 * level up ~30 dB across the knob, so every gritty preset is also the loudest
 * one and the bank cannot be balanced. */
inline float biteNorm(float d) { return 1.0f / (1.0f + 0.6f * d); }

/* Drive growth. 10 saturated hard enough to flatten the signal at the top of
 * the knob; 5 keeps the shaper shaping instead of clipping to a square. */
static const float BITE_DRIVE = 5.0f;

/* -------------------------------------------------------------- other --- */
/* Bit depth, 16 down to 2. Linear in BITS, which is how crushing is heard. */
inline float crushLevels(float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return powf(2.0f, 16.0f - 14.0f * x);
}


/* Portamento, seconds. Squared for fine control at the short end. */
inline float glideTime(float x) { return x * x * 2.0f; }

/* Control slew. Long enough that an encoder detent ramps rather than steps,
 * short enough that a fast sweep still tracks the hand. */
static const float PARAM_SLEW_TAU = 0.012f;

/* ------------------------------------------------------------ macros --- */
/*
 * RATIO is a LIST, not a range. A continuous 0.5..32 spends most of its travel
 * on values that sound bad; fourteen chosen ones give a better hit rate and the
 * list itself is an editorial decision. Harmonic first, then a few deliberately
 * inharmonic for bells and metal.
 */
/* ASCENDING. The first version listed the harmonic ratios and then appended
 * the inharmonic ones, so turning the knob up went 5, 7, 3.7, 5.4, 9.2 -- a
 * jump backwards in pitch in the middle of the sweep. It reads as the control
 * being broken, which is fair, because it was. */
static const float RATIO_TABLE[] = {
    0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.7f, 4.0f,
    5.0f, 5.4f, 6.0f, 7.0f, 8.0f, 9.2f, 11.0f, 14.0f
};
static const int RATIO_COUNT = (int)(sizeof(RATIO_TABLE)/sizeof(RATIO_TABLE[0]));

/*
 * THE MACRO IS THE INDEX, NOT A 0..1 POSITION ALONG IT.
 *
 * It used to be a float 0..1 scaled into the table, which meant the cell and
 * the screen reader both said "0.28" for a ratio of 2.5 -- a number with no
 * meaning to anybody, on the one knob whose value is a nameable musical fact.
 * Declared as an int with the ratios as enum labels, the grid draws `2.5` and
 * the screen reader speaks it.
 */
inline float ratioAt(float idx) {
    int i = (int)(idx + 0.5f);
    if (i < 0) i = 0;
    if (i >= RATIO_COUNT) i = RATIO_COUNT - 1;
    return RATIO_TABLE[i];
}

/*
 * THE MODULATOR ALWAYS DECAYS FASTER THAN THE CARRIER.
 *
 * This is the one authored judgement the whole design rests on. In FM,
 * brightness has to fall away faster than loudness or it sounds wrong -- it is
 * why a real bell dulls as it fades. Exposing eight envelope controls, as a
 * conventional FM synth does, mostly gives people the freedom to get this
 * wrong. Coupling it means it cannot be got wrong, and the coupling ratio is
 * chosen rather than measured, which is what makes it ours.
 *
 * 0.72 in the exponential time mapping is a little over half the carrier's
 * time constant -- audible, without the timbre dying before the note does.
 */
static const float MOD_DECAY_TIME_RATIO  = 0.45f;  /* 2.2x faster, everywhere */
static const float MOD_ATTACK_TIME_RATIO = 0.70f;  /* and opens a little sooner */
/*
 * THE MODULATOR KEEPS A FLOOR, AND WITHOUT ONE EVERY PERCUSSIVE PATCH DECAYED
 * INTO THE SAME SINE.
 *
 * This was `op2_s = sustain * 0.80`, tying the modulator's sustain to the
 * amplitude's. It reads as sensible -- a short note should not hold its
 * brightness -- and it collapses the instrument. A percussive patch has
 * sustain 0, so the index reached ZERO a few tens of ms in, and everything
 * after that was a bare carrier sine no matter what RATIO, BRIGHT or BITE
 * said. Measured over the bank: thirteen presets whose ratios span 0.5 to 9.2
 * landed with their spectral centroid within 50 Hz of the fundamental. They
 * did not merely resemble each other, they converged on the same waveform.
 *
 * A floor of 0.30 keeps a steady-state timbre that RATIO still owns, while the
 * modulator's own decay from 1.0 down to it preserves the bright-attack-then-
 * mellow shape that a bell or an electric piano needs.
 */
static const float MOD_SUSTAIN_FLOOR     = 0.30f;
static const float MOD_SUSTAIN_RATIO     = 0.55f;  /* a LEVEL: a plain scale is right */

inline float modDecayKnob(float x)  { return knobForTimeRatio(x, MOD_DECAY_TIME_RATIO,  ENV_TAU_SPAN); }
inline float modAttackKnob(float x) { return knobForTimeRatio(x, MOD_ATTACK_TIME_RATIO, ATK_TAU_SPAN); }

/* ------------------------------------------------------------- NOISE ---- */
/*
 * NOISE IS ITS OWN VOICE, NOT A SEASONING ON THE MODULATOR.
 *
 * The obvious place is the modulator -- noise into phase gives inharmonic FM
 * for free. It is the wrong place: the modulator only reaches the output
 * through the index, so on a patch with BRIGHT down the noise knob would do
 * nothing at all, and a macro that depends on another macro is not a macro.
 *
 * It is mixed into the carrier instead, ahead of the filter, carrying the
 * MODULATOR's envelope -- which decays faster than the carrier by
 * construction, so the default shape is an attack transient (chiff, breath,
 * a stick), while raising SUSTAIN opens it out into sustained noise for hats
 * and snares. Ahead of the filter means TONE shapes it, which is also what
 * finally gives TONE something to do.
 *
 * Squared, because the interesting part is the bottom: a little is a
 * transient, a lot is a percussion instrument.
 */
/*
 * IT CROSSFADES, IT DOES NOT PILE ON.
 *
 * Added on top at 0.8 the noise was audible and could never take over: it
 * reached ~19% of the energy above 3 kHz while the carrier kept the rest, so
 * the top of the knob was "a sine with some hiss" rather than an instrument
 * you could build a hat or a snare out of. Simply raising the gain buys that
 * at the cost of a 7 dB climb across the knob, which is the loudness jump the
 * macro sweep exists to catch.
 *
 * Ducking the carrier by the same squared law lets noise genuinely win at the
 * top while total level moves about 2 dB across the whole travel.
 */
/* 4.0 is FITTED, not chosen: the noise is band-limited by the filter while the
 * carrier sine sits below it untouched, so the gain that balances them is a
 * measurement. At 1.6 the level fell 6.6 dB across the knob. */
inline float noiseGain(float x) { return x * x * 4.0f; }
inline float noiseDuck(float x) { return x * x * 0.75f; }

/* ------------------------------------------------------------ OUTPUT ---- */
/*
 * A SOFT KNEE, NOT A LIMITER.
 *
 * Voices sum linearly and nothing was watching the total. A single note peaks
 * at -11 dBFS, a triad at -2, and five notes sat pinned at the ceiling being
 * hard-clamped -- which is what "big bell clips" was. Bells and pads are the
 * worst case because their partials are sparse and low, so several voices add
 * very nearly in phase.
 *
 * A real limiter is the wrong tool here: it needs an attack and a release, and
 * getting those wrong on a polyphonic instrument gives you pumping, which is a
 * more annoying artefact than the one being fixed. This is stateless and
 * exactly linear below the knee, folding smoothly to an asymptote of 1.0 above
 * it. Value and slope are both continuous at the knee, so there is no audible
 * corner, and tanhf is only evaluated above it.
 *
 * THE KNEE IS PLACED FROM THE MEASUREMENT, and higher is not better. 0.8 was
 * tried on the theory that a triad should pass untouched, and it is worse: the
 * fold is asymptotic to 1.0, so a steeper one leaves dense chords sitting
 * 0.01 dB below full scale with no headroom for anything downstream. Seven
 * presets landed there.
 *
 * At 0.6 the worst five-note chord peaks at -0.13 dBFS. One note (-11 dBFS) is
 * far below the knee and passes exactly; a triad peaks around 0.76 and is
 * reduced by 0.09 dB, which is a hundredth of the smallest level change anyone
 * can hear. An earlier version of this comment claimed triads were untouched.
 * They are not -- they are shaped inaudibly, which is a different thing and
 * the one worth writing down.
 */
static const float CLIP_KNEE = 0.6f;
inline float softClip(float x) {
    const float a = fabsf(x);
    if (a <= CLIP_KNEE) return x;
    const float sgn = x < 0.0f ? -1.0f : 1.0f;
    const float over = (a - CLIP_KNEE) / (1.0f - CLIP_KNEE);
    return sgn * (CLIP_KNEE + (1.0f - CLIP_KNEE) * tanhf(over));
}

} /* namespace curves */
} /* namespace hank */
#endif
