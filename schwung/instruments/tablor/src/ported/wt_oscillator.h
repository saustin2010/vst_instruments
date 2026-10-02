/* Wavetable oscillator — de-JUCE'd port of gin::WTOscillator and
 * gin::WTVoicedStereoOscillator (BSD-3, (c) Roland Rabien).
 *
 * Faithful to the original's behaviour:
 *  - three process paths (simple / phase-distorted / table-crossfade)
 *  - table switches only at phase wrap; one-cycle crossfade after a switch
 *  - bend (phase distortion), formant, asym, fold post-processing
 *  - per-channel DC blocker (10 Hz one-pole)
 *  - unison wrapper: evenly-spread detune, +/-spread pan, 1/sqrt(v) gain
 *
 * Differences (output-identical): the mip level and the formant exp() are
 * hoisted per block (note and formant are constant within a block); no SIMD.
 */
#pragma once

#include "wavetable.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tb {

namespace math {
    inline float pow2(float x) { return x * x; }
    inline float pow4(float x) { return pow2(pow2(x)); }
    inline float pow8(float x) { return pow2(pow4(x)); }
    inline float lerp(float a, float b, float t) { return a + t * (b - a); }
}

class DCBlocker {
public:
    void setSampleRate(float sr) { sampleRate = sr; recalc(); }
    void setCutoff(float hz)     { cutoff = hz; recalc(); }
    inline float process(float x) { z = x * a + z * b; return z; }
    void reset() { z = 0.0f; }
private:
    void recalc()
    {
        b = std::exp(-2.0f * (float) M_PI * cutoff / sampleRate);
        a = 1.0f - b;
    }
    float sampleRate = 44100.0f, cutoff = 10.0f;
    float a = 0.0f, b = 1.0f, z = 0.0f;
};

class WTOscillator {
public:
    struct Params {
        float leftGain  = 1.0f;
        float rightGain = 1.0f;
        float position  = 0.0f;   /* 0..1 across the table's frames */
        float bend      = 0.0f;   /* -1..1 phase distortion */
        float formant   = 0.0f;   /*  0..1 spectral shift */
        float asym      = 0.0f;
        float fold      = 0.0f;
    };

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        blockerL.setSampleRate((float) sr);
        blockerR.setSampleRate((float) sr);
        blockerL.setCutoff(10.0f);
        blockerR.setCutoff(10.0f);
    }

    void setWavetable(const Wavetable *t) { wt = t; }
    void setBlockDC(bool b) { blockDC = b; }

    void noteOn(float p = -1.0f)
    {
        phase = (p >= 0.0f) ? p : (float) rand() / (float) RAND_MAX;
        tableIndex = -1;
        lastTableIndex = -1;
        blockerL.reset();
        blockerR.reset();
    }

    void processAdding(float note, const Params &params, float *l, float *r, int n)
    {
        if (!wt || wt->size() == 0) return;

        if (tableIndex != lastTableIndex && lastTableIndex >= 0)
            processCrossfade(note, params, l, r, n);
        else if (params.bend == 0.0f && params.formant == 0.0f)
            processSimple(note, params, l, r, n);
        else
            processComplex(note, params, l, r, n);

        if (blockDC) {
            for (int i = 0; i < n; i++) {
                l[i] -= blockerL.process(l[i]);
                r[i] -= blockerR.process(r[i]);
            }
        }
    }

private:
    inline int posToIndex(float position) const
    {
        int i = (int) ((float) wt->size() * position);
        return std::min(wt->size() - 1, i);
    }

    /* Position as a CONTINUOUS place in the table: the two frames either side
     * and how far between them.
     *
     * The oscillator used to snap to one frame and change it at a phase wrap.
     * That is fine when position is a hand-turned knob, but under modulation
     * it is not: an envelope sweeping the whole table across a 45 ms attack
     * crosses ~25 frames per cycle, so every wrap jumped a long way through
     * the table at once and left a step. Reported as the sound being "a
     * little crunchy around the edges" -- the edges being exactly where an
     * envelope moves. Blending the two neighbours removes the steps entirely
     * and is what Serum and Vital do. */
    inline void posToFrames(float position, const FrameTable *&a,
                            const FrameTable *&b, float &frac) const
    {
        const int last = wt->size() - 1;
        float fp = position * (float) last;
        if (fp < 0.0f) fp = 0.0f;
        if (fp > (float) last) fp = (float) last;
        int i0 = (int) fp;
        if (i0 > last) i0 = last;
        int i1 = i0 < last ? i0 + 1 : last;
        frac = fp - (float) i0;
        a = wt->frame(i0);
        b = wt->frame(i1);
    }

    inline float freqDelta(float note) const
    {
        float freq = (float) std::min(sampleRate / 2.0,
                                      440.0 * std::pow(2.0, (note - 69.0) / 12.0));
        return freq / (float) sampleRate;
    }

    inline void postProcess(const Params &p, float &v) const
    {
        if (p.asym > 0.0f)
            v = math::lerp(v, math::pow4(v - 1.0f) * -1.0f + 1.0f, math::pow2(p.asym));
        if (p.fold > 0.0f) {
            const float fold = math::pow2(math::pow2(1.0f - p.fold)) * 1.5f;
            v = v - (std::max(v, fold) - fold) * 2.0f - (std::min(v, -fold) + fold) * 2.0f;
        }
    }

    /* gin's bendDistortion + formantDistortion, formant exp hoisted. */
    inline float distort(float ph, float bend, float formantMul) const
    {
        if (bend != 0.0f) {
            const float addDist = std::clamp(bend, 0.0f, 1.0f);
            const float subDist = std::clamp(bend, -1.0f, 0.0f);
            const float sub = math::pow8(ph);
            const float add = math::pow8(1.0f - ph);
            const float addMix = math::lerp(ph, 1.0f - add, addDist);
            const float subMix = math::lerp(ph, sub, -subDist);
            ph = std::min(kAlmostOne, addMix + subMix - ph);
        }
        if (formantMul != 1.0f)
            ph = std::min(kAlmostOne, ph * formantMul);
        return ph;
    }

    void processSimple(float note, const Params &p, float *l, float *r, int n)
    {
        if (tableIndex == -1 || tableIndex >= wt->size())
            tableIndex = posToIndex(p.position);

        const float delta = freqDelta(note);
        const FrameTable *fa = nullptr, *fb = nullptr;
        float frac = 0.0f;
        posToFrames(p.position, fa, fb, frac);

        while (n > 0) {
            int todo = std::min(n, (int) ((1.0f - phase) / delta) + 1);
            n -= todo;
            for (; todo > 0; todo--) {
                const float ph = std::min(kAlmostOne, phase);
                float s = fa->processLinear(note, ph);
                if (frac > 0.0f)
                    s += (fb->processLinear(note, ph) - s) * frac;
                postProcess(p, s);
                *l++ += s * p.leftGain;
                *r++ += s * p.rightGain;
                phase += delta;
            }
            while (phase >= 1.0f) phase -= 1.0f;
        }
        lastTableIndex = tableIndex = posToIndex(p.position);
    }

    void processComplex(float note, const Params &p, float *l, float *r, int n)
    {
        if (tableIndex == -1 || tableIndex >= wt->size())
            tableIndex = posToIndex(p.position);

        const float delta = freqDelta(note);
        const float formantMul = (p.formant != 0.0f)
            ? std::exp(p.formant * 1.60943791243f) : 1.0f;
        const FrameTable *fa = nullptr, *fb = nullptr;
        float frac = 0.0f;
        posToFrames(p.position, fa, fb, frac);

        while (n > 0) {
            int todo = std::min(n, (int) ((1.0f - phase) / delta) + 1);
            n -= todo;
            for (; todo > 0; todo--) {
                float ph = distort(std::min(kAlmostOne, phase), p.bend, formantMul);
                float s = fa->processLinear(note, ph);
                if (frac > 0.0f)
                    s += (fb->processLinear(note, ph) - s) * frac;
                postProcess(p, s);
                *l++ += s * p.leftGain;
                *r++ += s * p.rightGain;
                phase += delta;
            }
            while (phase >= 1.0f) phase -= 1.0f;
        }
        lastTableIndex = tableIndex = posToIndex(p.position);
    }

    void processCrossfade(float note, const Params &p, float *l, float *r, int n)
    {
        if (tableIndex == -1 || tableIndex >= wt->size())
            tableIndex = posToIndex(p.position);
        if (lastTableIndex == -1 || lastTableIndex >= wt->size())
            lastTableIndex = posToIndex(p.position);

        const float delta = freqDelta(note);
        const float formantMul = (p.formant != 0.0f)
            ? std::exp(p.formant * 1.60943791243f) : 1.0f;
        const FrameTable *ft1 = wt->frame(tableIndex);
        const FrameTable *ft2 = wt->frame(lastTableIndex);

        while (n > 0) {
            int todo = std::min(n, (int) ((1.0f - phase) / delta) + 1);
            n -= todo;
            for (; todo > 0; todo--) {
                float ph = distort(std::min(kAlmostOne, phase), p.bend, formantMul);
                float s1 = ft1->processLinear(note, ph);
                float s2 = ft2->processLinear(note, ph);
                float s = s1 * phase + s2 * (1.0f - phase);  /* old->new over one cycle */
                postProcess(p, s);
                *l++ += s * p.leftGain;
                *r++ += s * p.rightGain;
                phase += delta;
            }
            while (phase >= 1.0f) {
                phase -= 1.0f;
                lastTableIndex = tableIndex;
                tableIndex = posToIndex(p.position);
                ft1 = ft2 = wt->frame(tableIndex);
            }
        }
    }

    static constexpr float kAlmostOne = 0.99999994f; /* < 1.0f in float */

    const Wavetable *wt = nullptr;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    int tableIndex = -1, lastTableIndex = -1;
    bool blockDC = true;
    DCBlocker blockerL, blockerR;
};

/* ------------------------------------------------------------------ */
/* Unison wrapper — gin::VoicedStereoOscillator, WT flavour            */
/* ------------------------------------------------------------------ */

struct VoicedWTParams {
    int   voices = 1;
    float pan = 0.0f;       /* -1..1 */
    float spread = 0.0f;    /*  0..1 */
    float detune = 0.0f;    /* semitones across the stack */
    float gain = 1.0f;
    /* WT params forwarded to every voice */
    float position = 0.0f, bend = 0.0f, formant = 0.0f, asym = 0.0f, fold = 0.0f;
};

class VoicedWTOscillator {
public:
    explicit VoicedWTOscillator(int maxVoices = 8) : oscs((size_t) maxVoices) {}

    void setSampleRate(double sr) { for (auto &o : oscs) o.setSampleRate(sr); }
    void setWavetable(const Wavetable *t) { for (auto &o : oscs) o.setWavetable(t); }

    void noteOn(const float *phases, int n)
    {
        for (int i = 0; i < (int) oscs.size(); i++)
            oscs[(size_t) i].noteOn(i < n ? phases[i] : -1.0f);
    }
    void noteOn(float phase = -1.0f)
    {
        for (auto &o : oscs) o.noteOn(phase);
    }

    void processAdding(float note, const VoicedWTParams &vp, float *l, float *r, int n)
    {
        WTOscillator::Params p;
        p.position = vp.position; p.bend = vp.bend; p.formant = vp.formant;
        p.asym = vp.asym; p.fold = vp.fold;

        const int voices = std::clamp(vp.voices, 1, (int) oscs.size());
        if (voices == 1) {
            p.leftGain  = vp.gain * (1.0f - vp.pan);
            p.rightGain = vp.gain * (1.0f + vp.pan);
            oscs[0].processAdding(note, p, l, r, n);
            return;
        }

        const float baseNote  = note - vp.detune / 2.0f;
        const float noteDelta = vp.detune / (float) (voices - 1);
        const float basePan   = vp.pan - vp.spread;
        const float panDelta  = (vp.spread * 2.0f) / (float) (voices - 1);
        const float comp      = 1.0f / std::sqrt((float) voices);

        for (int i = 0; i < voices; i++) {
            float pan = std::clamp(basePan + panDelta * (float) i, -1.0f, 1.0f);
            p.leftGain  = vp.gain * (1.0f - pan) * comp;
            p.rightGain = vp.gain * (1.0f + pan) * comp;
            oscs[(size_t) i].processAdding(baseNote + noteDelta * (float) i, p, l, r, n);
        }
    }

private:
    std::vector<WTOscillator> oscs;
};

} // namespace tb
