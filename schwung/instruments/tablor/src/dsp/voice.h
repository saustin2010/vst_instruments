/* Tablor voice — the per-voice signal path, structured after
 * WavetableVoice.cpp in the original (FigBug/Wavetable, BSD-3):
 *
 *   osc1/osc2 (unison WT) ─┬─ preFilter ── Filter ──┐
 *   sub, noise ────────────┴─ postFilter ───────────┴─ VCA ADSR ─ out
 *
 * Per-source pre/post-filter routing, per-voice filter ADSR, 3 per-voice
 * glide via noteSmoother. Parameters are read from the engine's pot
 * array each block through param_map.h; the mod matrix contributes offsets.
 */
#pragma once

#include "../ported/wt_oscillator.h"
#include "../ported/analog_tables.h"
#include "../ported/filter.h"
#include "../ported/adsr.h"
#include "param_map.h"
#include "params.h"

#include <cstdlib>
#include <cstring>

namespace tb {

inline float velocityToGain(float velocity, float sensitivity)
{
    float v = velocity * sensitivity + 1.0f - sensitivity;
    float g = v * std::pow(25.0f, v) * 0.04f;
    return std::clamp(g, 0.0f, 1.0f);
}

inline constexpr int kBlock = 128;

/* Everything a voice needs from the engine each block. */
/* Where a spare envelope can be pointed. The ORDER is the option order of
 * meN_dst in gen_params.py -- the DSP receives the selection as an index, so
 * the two lists are one contract and must move together. */
enum ModDst {
    MD_NONE = 0,
    MD_WT1_POS, MD_WT2_POS, MD_WT1_LEVEL, MD_WT2_LEVEL, MD_WT1_TUNE, MD_WT2_TUNE,
    MD_WT1_BEND, MD_WT2_BEND, MD_WT1_FORMANT, MD_WT2_FORMANT, MD_WT1_PAN, MD_WT2_PAN,
    MD_FLT_FREQ, MD_FLT_RES, MD_SUB_LEVEL, MD_NOISE_LEVEL, MD_AMP,
    MD_COUNT
};

inline constexpr int kModEnvs = 2;

struct VoiceContext {
    const float *pots = nullptr;            /* TB_PARAM_COUNT raw values   */
    const Wavetable *table1 = nullptr;
    const Wavetable *table2 = nullptr;
    AnalogTables *analog = nullptr;
    float modWheel = 0.0f;                  /* 0..1  */
    float aftertouch = 0.0f;                /* 0..1  */
    float pitchBendSemis = 0.0f;            /* +/- pb_range already applied */
    float bpm = 120.0f;
};

class Voice {
public:
    explicit Voice(AnalogTables &analog) : sub(analog), noise(analog) {}

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        osc1.setSampleRate(sr); osc2.setSampleRate(sr);
        sub.setSampleRate(sr); noise.setSampleRate(sr);
        filter.setSampleRate(sr);
        filterADSR.setSampleRate(sr);
        adsr.setSampleRate(sr);
        for (auto &e : modEnv) e.setSampleRate(sr);
        noteSmoother.setSampleRate(sr);
    }

    bool isActive() const  { return active; }
    bool isReleasing() const { return adsr.getState() == AnalogADSR::State::release; }
    int  currentNote() const { return midiNote; }
    uint32_t serialNumber() const { return serial; }
    float envOutput() const { return adsr.getOutput(); }

    void start(const VoiceContext &c, int note, float vel, uint32_t serial_,
               float glideFromNote)
    {
        midiNote = note;
        velocity = vel;
        serial = serial_;
        active = true;
        fastKill = false;

        osc1.setWavetable(c.table1);
        osc2.setWavetable(c.table2);

        const float *P = c.pots;
        const int glideMode = (int) P[TB_P_GLIDE_MODE];
        glissando  = glideMode == 1;
        portamento = glideMode == 2;
        if (glideFromNote >= 0.0f && (glissando || portamento)) {
            noteSmoother.setTime(potGlideTime(P[TB_P_GLIDE]));
            noteSmoother.setValueUnsmoothed(glideFromNote / 127.0f);
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }

        filter.reset();
        filterADSR.reset();
        for (auto &e : modEnv) e.reset();

        updateParams(c, 0);

        /* oscillator phases: free-running (the old retrig-off default —
         * the retrig switch was cut from the surface) */
        for (int o = 0; o < 2; o++) {
            auto &osc = o == 0 ? osc1 : osc2;
            float phases[8];
            for (int i = 0; i < 8; i++)
                phases[i] = (float) rand() / (float) RAND_MAX;
            osc.noteOn(phases, 8);
        }
        sub.noteOn(0.0f);
        noise.noteOn(0.0f);

        filterADSR.noteOn();
        for (auto &e : modEnv) e.noteOn();

        adsr.reset();
        adsr.noteOn();
    }

    /* Mono-mode retrigger without phase reset. */
    void retrigger(const VoiceContext &c, int note, float vel)
    {
        const float *P = c.pots;
        midiNote = note;
        velocity = vel;
        if (glissando || portamento) {
            noteSmoother.setTime(potGlideTime(P[TB_P_GLIDE]));
            noteSmoother.setValue((float) note / 127.0f);
        } else {
            noteSmoother.setValueUnsmoothed((float) note / 127.0f);
        }
        bool legato = P[TB_P_LEGATO] > 0.5f;
        if (!legato) {              /* env retrig switches cut: always retrig */
            filterADSR.noteOn();
        for (auto &e : modEnv) e.noteOn();
            adsr.noteOn();
        }
    }

    void stop(bool allowTail)
    {
        adsr.noteOff();
        filterADSR.noteOff();
        for (auto &e : modEnv) e.noteOff();
        if (!allowTail) {
            active = false;
        }
    }

    void kill()
    {
        fastKill = true;
        adsr.noteOff(); filterADSR.noteOff();
        for (auto &e : modEnv) e.noteOff();
    }

    void render(const VoiceContext &c, float *outL, float *outR, int n)
    {
        if (!active) return;

        /* Re-point at the context's tables every block: the loader may have
         * swapped them since noteStarted, and the block's shared_ptr is the
         * only thing keeping a table alive. A held raw pointer would dangle.
         * A swap mid-note lands as a waveform step at this block's first
         * sample — fade the block in from silence (2.9 ms) to mask it. */
        bool swapped = (lastTable1 && c.table1 != lastTable1) ||
                       (lastTable2 && c.table2 != lastTable2);
        lastTable1 = c.table1;
        lastTable2 = c.table2;
        osc1.setWavetable(c.table1);
        osc2.setWavetable(c.table2);

        updateParams(c, n);

        float preL[kBlock] = {}, preR[kBlock] = {};
        float postL[kBlock] = {}, postR[kBlock] = {};

        const float *P = c.pots;

        /* Routing switches cut: every source runs through the filter. */
        if (oscGain[0] > 0.0f)
            osc1.processAdding(oscNote[0], vp1, preL, preR, n);
        if (oscGain[1] > 0.0f)
            osc2.processAdding(oscNote[1], vp2, preL, preR, n);
        if (subParams.leftGain + subParams.rightGain > 0.0f)
            sub.processAdding(subNote, subParams, preL, preR, n);
        if (noiseParams.leftGain + noiseParams.rightGain > 0.0f)
            noise.processAdding(60.0f, noiseParams, preL, preR, n);

        /* velocity */
        float vgain = velocityToGain(velocity, ampVelTrack) * ampModGain;
        for (int i = 0; i < n; i++) {
            preL[i] *= vgain;  preR[i] *= vgain;
            postL[i] *= vgain; postR[i] *= vgain;
        }

#ifdef TB_DEBUG_FILTER
        {
            float pre = 0; for (int i = 0; i < n; i++) pre += preL[i] * preL[i];
            filter.process(preL, preR, n);
            float post = 0; for (int i = 0; i < n; i++) post += preL[i] * preL[i];
            static int dbg = 0;
            if (dbg++ % 100 == 0)
                printf("[voice] filt f=%.1f preRMS=%.4f postRMS=%.4f gain0=%.2f\n",
                       filter.getFrequency(), std::sqrt(pre / n), std::sqrt(post / n),
                       oscGain[0]);
        }
#else
        filter.process(preL, preR, n);
#endif

        for (int i = 0; i < n; i++) {
            postL[i] += preL[i];
            postR[i] += preR[i];
        }

        adsr.processMultiplying(postL, postR, n);

        if (swapped) {
            for (int i = 0; i < n; i++) {
                float g = (float) i / (float) n;
                postL[i] *= g;
                postR[i] *= g;
            }
        }

        if (adsr.getState() == AnalogADSR::State::idle)
            active = false;

        for (int i = 0; i < n; i++) {
            outL[i] += postL[i];
            outR[i] += postR[i];
        }

        noteSmoother.process(n);
    }

private:
        void updateParams(const VoiceContext &c, int blockSize)
    {
        const float *P = c.pots;

        /* ---- envelopes ---- */
        filterADSR.setAttack(potAttackTime(P[TB_P_FLT_A]));
        filterADSR.setDecay(potEnvTime(P[TB_P_FLT_D]));
        filterADSR.setSustainLevel(pot01(P[TB_P_FLT_S]));
        filterADSR.setRelease(potEnvTime(P[TB_P_FLT_R]));
        if (blockSize > 0) filterADSR.process(blockSize);

        adsr.setAttack(potAttackTime(P[TB_P_VCA_A]));
        adsr.setDecay(potEnvTime(P[TB_P_VCA_D]));
        adsr.setSustainLevel(pot01(P[TB_P_VCA_S]));
        adsr.setRelease(fastKill ? 0.01f : potEnvTime(P[TB_P_VCA_R]));

        /* ---- the two spare envelopes, and where they point ----
         * Each contributes env * amount to ONE destination. Amount is bipolar
         * so a sweep can fall as well as rise, which is the shape that makes a
         * wavetable read as a struck instrument. */
        for (int i = 0; i < MD_COUNT; i++) modOff[i] = 0.0f;
        {
            static const struct { int a, d, s, r, dst, amt; } k[kModEnvs] = {
                { TB_P_ME1_A, TB_P_ME1_D, TB_P_ME1_S, TB_P_ME1_R,
                  TB_P_ME1_DST, TB_P_ME1_AMT },
                { TB_P_ME2_A, TB_P_ME2_D, TB_P_ME2_S, TB_P_ME2_R,
                  TB_P_ME2_DST, TB_P_ME2_AMT },
            };
            for (int i = 0; i < kModEnvs; i++) {
                modEnv[i].setAttack(potAttackTime(P[k[i].a]));
                modEnv[i].setDecay(potEnvTime(P[k[i].d]));
                modEnv[i].setSustainLevel(pot01(P[k[i].s]));
                modEnv[i].setRelease(potEnvTime(P[k[i].r]));
                if (blockSize > 0) modEnv[i].process(blockSize);
                const int dst = (int) P[k[i].dst];
                if (dst > MD_NONE && dst < MD_COUNT)
                    modOff[dst] += modEnv[i].getOutput() * potBipolar(P[k[i].amt]);
            }
        }

        /* ---- oscillators ---- */
        float baseNote = noteSmoother.getCurrentValue() * 127.0f;
        if (glissando) baseNote = (float) (int) (baseNote + 0.5f);
        baseNote += c.pitchBendSemis;

        for (int o = 0; o < 2; o++) {
            const int kTable  = o == 0 ? TB_P_WT1_TABLE : TB_P_WT2_TABLE;
            const int kPos    = o == 0 ? TB_P_WT1_POS : TB_P_WT2_POS;
            const int kLevel  = o == 0 ? TB_P_WT1_LEVEL : TB_P_WT2_LEVEL;
            const int kTune   = o == 0 ? TB_P_WT1_TUNE : TB_P_WT2_TUNE;
            const int kUni    = o == 0 ? TB_P_WT1_UNI : TB_P_WT2_UNI;
            const int kDet    = o == 0 ? TB_P_WT1_DETUNE : TB_P_WT2_DETUNE;
            const int kSpread = o == 0 ? TB_P_WT1_SPREAD : TB_P_WT2_SPREAD;
            const int kPan    = o == 0 ? TB_P_WT1_PAN : TB_P_WT2_PAN;
            const int kBend   = o == 0 ? TB_P_WT1_BEND : TB_P_WT2_BEND;
            const int kForm   = o == 0 ? TB_P_WT1_FORMANT : TB_P_WT2_FORMANT;
            (void) kTable;

            const int dPos  = o == 0 ? MD_WT1_POS : MD_WT2_POS;
            const int dLvl  = o == 0 ? MD_WT1_LEVEL : MD_WT2_LEVEL;
            const int dTun  = o == 0 ? MD_WT1_TUNE : MD_WT2_TUNE;
            const int dBnd  = o == 0 ? MD_WT1_BEND : MD_WT2_BEND;
            const int dFrm  = o == 0 ? MD_WT1_FORMANT : MD_WT2_FORMANT;
            const int dPan  = o == 0 ? MD_WT1_PAN : MD_WT2_PAN;

            VoicedWTParams &vp = o == 0 ? vp1 : vp2;

            /* tune modulation reads as an octave at full amount */
            oscNote[o] = baseNote + P[kTune] + modOff[dTun] * 12.0f;

            vp.voices   = std::clamp((int) P[kUni], 1, 4);
            vp.position = std::clamp(pot01(P[kPos]) + modOff[dPos], 0.0f, 1.0f);
            vp.gain     = std::clamp(potSquared(P[kLevel]) + modOff[dLvl], 0.0f, 1.0f);
            vp.pan      = std::clamp(potBipolar(P[kPan]) + modOff[dPan], -1.0f, 1.0f);
            vp.detune   = potDetune(P[kDet]);
            vp.spread   = pot01(P[kSpread]);
            vp.bend     = std::clamp(potBipolar(P[kBend]) + modOff[dBnd], -1.0f, 1.0f);
            vp.formant  = std::clamp(potBipolar(P[kForm]) + modOff[dFrm], -1.0f, 1.0f);
            oscGain[o] = vp.gain;
        }

        /* ---- sub + noise ---- */
        subNote = baseNote + P[TB_P_SUB_TUNE];
        float subLvl = std::clamp(potSquared(P[TB_P_SUB_LEVEL]) + modOff[MD_SUB_LEVEL],
                                  0.0f, 1.0f);
        float subPan = 0.0f;                 /* pan control cut: centered */
        switch ((int) P[TB_P_SUB_WAVE]) {
        case 0: subParams.wave = Wave::sine; break;
        case 1: subParams.wave = Wave::triangle; break;
        case 2: subParams.wave = Wave::sawUp; break;
        case 3: subParams.wave = Wave::pulse; subParams.pw = 0.50f; break;
        case 4: subParams.wave = Wave::pulse; subParams.pw = 0.25f; break;
        default: subParams.wave = Wave::pulse; subParams.pw = 0.125f; break;
        }
        subParams.leftGain  = subLvl * (1.0f - subPan);
        subParams.rightGain = subLvl * (1.0f + subPan);

        float nzLvl = std::clamp(potSquared(P[TB_P_NOISE_LEVEL]) + modOff[MD_NOISE_LEVEL],
                                 0.0f, 1.0f);
        float nzPan = 0.0f;                  /* pan control cut: centered */
        noiseParams.wave = (int) P[TB_P_NOISE_TYPE] == 0 ? Wave::whiteNoise : Wave::pinkNoise;
        noiseParams.leftGain  = nzLvl * (1.0f - nzPan);
        noiseParams.rightGain = nzLvl * (1.0f + nzPan);

        /* ---- amp ---- */
        ampVelTrack = pot01(P[TB_P_VCA_VEL]);
        ampModGain = std::clamp(1.0f + modOff[MD_AMP], 0.0f, 2.0f);

        /* ---- filter (the original's law, verbatim) ---- */
        {
            const float filterWidth = 12.0f * std::log2(20000.0f / 440.0f) + 69.0f; /* midi(20k) */
            float env = filterADSR.getOutput();
            float sens = pot01(P[TB_P_FLT_VEL]);
            sens = velocity * sens + 1.0f - sens;

            float n = potFilterNote(P[TB_P_FLT_FREQ]);
            n += ((float) midiNote - 60.0f) * pot01(P[TB_P_FLT_KEY]);
            n += env * sens * potBipolar(P[TB_P_FLT_ENV]) * filterWidth;
            n += modOff[MD_FLT_FREQ] * filterWidth;

            float f = midiNoteToHz(n);
            float maxFreq = std::min(20000.0f, (float) (sampleRate / 2.0));
            f = std::clamp(f, 4.0f, maxFreq);

            float res = std::clamp(pot01(P[TB_P_FLT_RES]) + modOff[MD_FLT_RES],
                                   0.0f, 1.0f);
            float q = kFilterQ / (1.0f - res * 0.99f);

            static const Filter::Type kTypes[8] = {
                Filter::lowpass, Filter::lowpass, Filter::highpass, Filter::highpass,
                Filter::bandpass, Filter::bandpass, Filter::notch, Filter::notch };
            int t = (int) P[TB_P_FLT_TYPE] & 7;
            filter.setType(kTypes[t]);
            filter.setSlope((t & 1) ? Filter::db24 : Filter::db12);
            filter.setParams(f, q);
        }
    }

    VoicedWTOscillator osc1 { 4 }, osc2 { 4 };
    VoicedWTParams vp1, vp2;
    StereoOscillator sub, noise;
    StereoOscillator::Params subParams, noiseParams;
    Filter filter;
    AnalogADSR filterADSR, adsr;
    AnalogADSR modEnv[kModEnvs];        /* the two spare, routable envelopes */
    float modOff[MD_COUNT] = {};        /* their summed offset per destination */
    ValueSmoother<float> noteSmoother;
    const Wavetable *lastTable1 = nullptr, *lastTable2 = nullptr;

    double sampleRate = 44100.0;
    float oscNote[2] = { 60.0f, 60.0f }, oscGain[2] = {};
    float subNote = 48.0f;
    float velocity = 0.0f, ampVelTrack = 1.0f, ampModGain = 1.0f;
    int midiNote = -1;
    uint32_t serial = 0;
    bool active = false, fastKill = false, glissando = false, portamento = false;
};

} // namespace tb
