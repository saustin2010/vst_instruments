/*
 * Hank — 2-operator FM voice engine.
 *
 * Pure DSP: no Schwung dependency, no allocation after construction, no I/O.
 * That is what lets tools/hank-bench link it natively and measure it without a
 * device in the loop -- every curve here is fitted to those measurements.
 *
 * Architecture:
 *   OP2 (ratio, self-feedback, ADSR) -> phase-modulates -> OP1 (ratio 1, ADSR)
 *   + noise on the modulator envelope
 *   -> morphing SVF (LP->BP->HP, ADSR x depth, keytrack, bypassable)
 * then, on the summed bus: crush -> stereo width -> distortion -> volume.
 */
#ifndef HANK_ENGINE_H
#define HANK_ENGINE_H

#include <stdint.h>

namespace hank {

static const int   MAX_VOICES   = 16;
static const float SAMPLE_RATE  = 44100.0f;

/* All fields are normalized 0..1 unless noted. These are the values the UI and
 * the preset bank speak; every mapping to seconds/Hz/index lives in curves.h so
 * that Pass B can refit one file. */
/*
 * THE EIGHT MACROS.
 *
 * These are the instrument. The raw fields below them are the machine, and
 * `macro_mode` decides which is driving: with it on -- the default -- the
 * macros are mapped onto the raw fields every block and the raw fields are
 * outputs, not inputs. Turn it off and the raw fields are used directly, which
 * is what the tuning page is for.
 */
struct Macros {
    float ratio   = 4.0f;    /* INDEX into curves::RATIO_TABLE, not a 0..1 position */
    float bright  = 0.35f;   /* FM index, plus a little feedback at the top */
    float bite    = 0.0f;    /* feedback, then the shaper */
    float attack  = 0.0f;
    float decay   = 0.45f;   /* decay AND release */
    float sustain = 0.7f;
    float noise   = 0.0f;    /* noise into the carrier, on the mod envelope */
    float tone    = 0.8f;    /* one filter sweep */
};

struct Params {
    Macros m;
    int    macro_mode = 1;
    /* OP2 — the modulator */
    /* COARSE AND FINE ARE SEPARATE FIELDS ON PURPOSE. Packing them into one
     * ratio and recovering fine as `ratio - floor(ratio)` breaks at a coarse
     * value of 0.5: floor(0.5) is 0, so setting fine after it collapsed the
     * ratio to ZERO and the modulator sat at DC. */
    float op2_coarse   = 1.0f;   /* 0.5, or an integer 1..32 */
    float op2_fine     = 0.0f;   /* 0 .. 1, added to coarse */
    float op2_level    = 0.0f;
    float op2_fbk      = 0.0f;
    float op2_a = 0.0f, op2_d = 0.25f, op2_s = 1.0f, op2_r = 0.5f;
    int   op2_phase_reset = 1;
    float op2_phase    = 0.0f;

    /* OP1 — the carrier, ratio fixed at 1 */
    float op1_a = 0.0f, op1_d = 0.25f, op1_s = 1.0f, op1_r = 0.5f;
    int   op1_phase_reset = 1;
    float op1_phase    = 0.0f;

    /* Filter */
    float noise  = 0.0f;         /* 0..1, squared into a gain */
    /*
     * PRESET CALIBRATION, SEPARATE FROM THE VOLUME KNOB, BECAUSE ONE FIELD
     * CANNOT DO BOTH JOBS.
     *
     * Preset levels were fitted by writing `volume` per preset, which spread it
     * from 8.7 to 95 across the bank -- so the Volume knob meant something
     * different on every patch: acres of rope on one, a decibel on the next,
     * and no way to know which without turning it. Worse, `volume` is live
     * state, so a slot that had been played with it up kept that value and
     * re-shipping a better-calibrated bank changed nothing until the preset was
     * picked again. That is exactly how a big bell went on clipping on device
     * while measuring -11 dBFS on the bench.
     *
     * Calibration lives here, set only by loading a preset. `volume` goes back
     * to being a trim with 0.5 as unity on every patch in the bank.
     */
    float preset_gain = 1.0f;
    float cutoff = 1.0f, res = 0.0f, mix = 0.0f, env_depth = 0.0f;
    float f_a = 0.0f, f_d = 0.25f, f_s = 1.0f, f_r = 0.5f;
    int   filter_on = 1;
    int   keytrack  = 1;

    /* Voice */
    int   voice_count = 8;       /* 1 == mono with legato glide */
    float unison = 0.0f;
    float glide  = 0.0f;
    float pitch  = 0.0f;         /* semitones, -96 .. +96 */
    float crush  = 0.0f;

    /* Pitch envelope -- bipolar depth, and its own decay. It is what an FM
     * kick, zap or blip is actually made of. Instant attack and no sustain by
     * construction: a pitch envelope that lingers is a detune. */

    /* Out */
    float volume = 1.0f;   /* unity; the knob only attenuates */
    float width  = 0.0f;
    float dist   = 0.0f;
};

void applyMacros(Params &p);

/* ------------------------------------------------------------------ ADSR */

class Env {
public:
    void  noteOn(float a, float d, float s, float r);
    void  noteOff();
    /* Cut short from wherever it is, without a click -- see Engine::releaseAll. */
    void  fastRelease(float tau, float sr);
    float process();
    bool  idle() const { return stage_ == 0; }
    float value() const { return v_; }
private:
    int   stage_ = 0;            /* 0 idle, 1 atk, 2 dec, 3 sus, 4 rel */
    float v_ = 0.0f;
    float ramp_ = 0.0f;          /* attack ramp position, 0..1 in TIME */
    float from_ = 0.0f;          /* level the attack started from */
    float a_ = 0.0f, d_ = 0.0f, s_ = 1.0f, r_ = 0.0f;  /* per-sample rates */
    float sus_ = 1.0f;
};

/* --------------------------------------------------- 4-pole morphing SVF */

class SVF {
public:
    void  reset();
    void  set(float cutoff_hz, float k);
    float process(float x, float mix);
private:
    float g_ = 0.0f, k_ = 2.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float s1_ = 0.0f, s2_ = 0.0f, s3_ = 0.0f, s4_ = 0.0f;
    float tick(float x, float &s1, float &s2, float &lp, float &bp, float &hp);
};

/* ----------------------------------------------------------------- Voice */

class Voice {
public:
    void  reset();
    void  noteOn(int note, float vel, const Params &p, bool legato);
    void  noteOff();
    /* Cut short from wherever it is, without a click -- see Engine::releaseAll. */
    void  fastRelease(float tau);
    bool  active() const { return !amp_.idle(); }
    int   note() const { return note_; }
    uint32_t age() const { return age_; }
    /* For the mono return: the note we go back to keeps the velocity it was
     * played with, not the velocity of the note being let go. */
    float velocity() const { return vel_; }
    void  setAge(uint32_t a) { age_ = a; }
    float render(const Params &p);
private:
    int   note_ = -1;
    float vel_  = 1.0f;
    uint32_t age_ = 0;
    float freq_ = 0.0f, target_freq_ = 0.0f, glide_c_ = 1.0f;
    float ph1_ = 0.0f, ph2_ = 0.0f, fb_ = 0.0f;
    /* Per-voice, so two voices are never handed the same noise. */
    uint32_t rng_ = 0x2545f491u;
    Env   amp_, mod_, fenv_;
    float last_amp_ = 0.0f;  /* preserves sounding voices' phase on retrigger */
    SVF   svf_;
};

/* ---------------------------------------------------------------- Engine */

class Engine {
public:
    Engine();
    void reset();
    void noteOn(int note, int vel);
    void noteOff(int note);
    void allNotesOff();
    /* Every sounding voice into a short release. A preset load is a new sound,
     * so the old one has to stop -- but stopping it by muting is a click. */
    void releaseAll(float tau);
    void sustain(bool on);
    void pitchBend(float semitones);
    Params &params() { return p_; }
    /* Renders `frames` stereo samples into interleaved float [-1,1]. */
    void render(float *out_lr, int frames);
private:
    void slewParams(float c);
    float biteShape(float x, float d);
public:
private:
    Params  p_;
    /* SLEWED COPY of the continuous controls -- see Engine::slewParams.
     * p_ is the target the host writes; sp_ is what the DSP actually reads,
     * so a knob detent ramps instead of stepping. */
    Params  sp_;
    bool    sp_primed_ = false;
    Voice   v_[MAX_VOICES];
    /* Held notes in the order they were pressed -- mono last-note priority. */
    static const int MONO_STACK = 16;
    int      mono_stack_[MONO_STACK] = {0};
    int      mono_n_ = 0;
    void     monoStackPush(int note);
    void     monoStackRemove(int note);

    uint32_t clock_ = 0;
    float   bend_ = 0.0f;
    bool    sustain_ = false;
    bool    held_[128] = {false};
    /* stereo width: short modulated delay, fixed-size, no allocation */
    /* distortion DC blocker state, per channel */
    /* BITE's DC blocker, one per channel. The shaper is asymmetric by design,
     * so it makes DC by construction and this is what removes it. */
    float   dcx_[2] = {0}, dcy_[2] = {0};
    int     lastNote_ = -1;
};

} /* namespace hank */
#endif
