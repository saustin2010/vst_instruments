#include "hank_engine.h"
#include "hank_curves.h"
#include <math.h>
#include <string.h>

namespace hank {

/* ------------------------------------------------------ sine lookup table */
/* 16 voices x 2 ops x 3 unison stacks x 128 frames is up to 12288 sines per
 * block; a table keeps that off libm. Filled once at load, never resized. */
static const int   SINE_BITS = 11;
static const int   SINE_SIZE = 1 << SINE_BITS;      /* 2048 */
static float       g_sine[SINE_SIZE + 1];
static bool        g_sine_ready = false;

void hank_init_tables() {
    if (g_sine_ready) return;
    for (int i = 0; i <= SINE_SIZE; i++)
        g_sine[i] = sinf(2.0f * (float)M_PI * (float)i / (float)SINE_SIZE);
    g_sine_ready = true;
}

/* Phases are counted in TURNS (0..1); modulation indices are in RADIANS.
 * Every index must cross that boundary exactly once. */
static const float INV_TWO_PI = 1.0f / (2.0f * (float)M_PI);

/* phase in turns (0..1), wrapped */
static inline float sine(float ph) {
    ph -= floorf(ph);
    float f = ph * (float)SINE_SIZE;
    int   i = (int)f;
#ifdef MPC_PORT
    /* MPC port (2026-10-02): a tiny negative phase minus its floor rounds to exactly 1.0 in float, so i hit SINE_SIZE
     * and read g_sine[SINE_SIZE + 1], one past the table (found with steve/tools/fuzz). Phase 1.0 is phase 0. */
    if (i >= SINE_SIZE) { i = 0; f = 0.0f; }
#endif
    float frac = f - (float)i;
    return g_sine[i] + (g_sine[i + 1] - g_sine[i]) * frac;
}

/* ------------------------------------------------------------------ Env */

void Env::noteOn(float a, float d, float s, float r) {
    const float sr = SAMPLE_RATE;
    /*
     * A plain exponential ADSR:
     *
     *   attack   a timed ramp from wherever the level is now to 1.0, eased out
     *            (fast then settling) -- its duration is the knob's time
     *            constant x ATTACK_SPAN, so the knob keeps its feel
     *   decay    exponential toward sustain, finishing within ENV_FLOOR of it
     *   release  exponential toward zero, finishing below ENV_FLOOR
     *
     * Starting the attack from the current level (not from zero) is what lets a
     * retriggered or stolen voice carry on without a step.
     *
     * SUSTAIN IS LINEAR, and was briefly squared. A macro sustain of 0.15
     * became 0.02, which killed the modulator outright and rendered every
     * high-ratio patch as a sine wave.
     */
    a_ = 1.0f / (curves::attackTau(a) * curves::ATTACK_SPAN * sr);   /* ramp step per sample */
    d_ = expf(-1.0f / (curves::decayTau(d)  * sr));                  /* per-sample decay factor */
    r_ = expf(-1.0f / (curves::releaseTau(r) * sr));
    sus_  = s;
    from_ = v_;
    ramp_ = 0.0f;
    stage_ = 1;
}

void Env::noteOff() { if (stage_ != 0) stage_ = 4; }

void Env::fastRelease(float tau, float sr) {
    if (stage_ == 0) return;
    r_ = expf(-1.0f / (tau * sr));
    stage_ = 4;
}

float Env::process() {
    switch (stage_) {
    case 1: {
        /* Eased-out ramp: 1 - (1 - t)^2 rises quickly and settles into 1.0. */
        ramp_ += a_;
        if (ramp_ >= 1.0f) { v_ = 1.0f; stage_ = 2; break; }
        const float u = 1.0f - ramp_;
        v_ = from_ + (1.0f - from_) * (1.0f - u * u);
        break;
    }
    case 2:
        v_ = sus_ + (v_ - sus_) * d_;
        if (v_ - sus_ <= curves::ENV_FLOOR) { v_ = sus_; stage_ = 3; }
        break;
    case 3:
        v_ = sus_;
        if (v_ <= 0.0f) stage_ = 0;
        break;
    case 4:
        /*
         * NO INFINITE HOLD. This read `if (hold_) break;`, with hold_ set when
         * the release control was at its maximum -- an "infinite release at
         * max" that made sense when release was its own raw parameter and
         * somebody had to ask for it.
         *
         * DECAY drives the release now, and it is a macro, so the top of an
         * ordinary knob was a note that could not be stopped. Reported by a
         * user as "if the decay goes to 100, then I press a note, I can't turn
         * it off" -- and note-off, all-notes-off and even a new preset could
         * not clear it. A feature reachable only by accident, whose effect is
         * indistinguishable from a hung voice, is not a feature.
         */
        v_ *= r_;
        if (v_ <= curves::ENV_FLOOR) { v_ = 0.0f; stage_ = 0; }
        break;
    default: v_ = 0.0f; break;
    }
    return v_;
}

/* ------------------------------------------------------------------ SVF */
/* Two cascaded TPT 2-pole state-variable sections = 4 poles, each able to
 * present LP/BP/HP; the morph blends LP->BP->HP across `mix`. */

void SVF::reset() { s1_ = s2_ = s3_ = s4_ = 0.0f; }

void SVF::set(float cutoff_hz, float k) {
    float g = tanf((float)M_PI * cutoff_hz / SAMPLE_RATE);
    g_ = g; k_ = k;
    a1_ = 1.0f / (1.0f + g * (g + k));
    a2_ = g * a1_;
    a3_ = g * a2_;
}

float SVF::tick(float x, float &s1, float &s2, float &lp, float &bp, float &hp) {
    float v3 = x - s2;
    float v1 = a1_ * s1 + a2_ * v3;
    float v2 = s2 + a2_ * s1 + a3_ * v3;
    s1 = 2.0f * v1 - s1;
    s2 = 2.0f * v2 - s2;
    lp = v2; bp = v1; hp = x - k_ * v1 - v2;
    return lp;
}

float SVF::process(float x, float mix) {
    float lp, bp, hp;
    tick(x, s1_, s2_, lp, bp, hp);
    float y1 = (mix < 0.5f) ? lp + (bp - lp) * (mix * 2.0f)
                            : bp + (hp - bp) * ((mix - 0.5f) * 2.0f);
    tick(y1, s3_, s4_, lp, bp, hp);
    return (mix < 0.5f) ? lp + (bp - lp) * (mix * 2.0f)
                        : bp + (hp - bp) * ((mix - 0.5f) * 2.0f);
}

/* ------------------------------------------------------------- macros --- */
/*
 * Map the eight macros onto the machine. Runs once per block, not per sample:
 * these are control-rate decisions and the slew downstream smooths them.
 *
 * Every line here is a judgement about what the instrument should do, which is
 * the point -- this layer IS the design. The raw fields it writes are outputs
 * while macro_mode is on.
 */
void applyMacros(Params &p) {
    const Macros &m = p.m;

    /* RATIO: a stepped list, so a turn lands on something usable. */
    p.op2_coarse = curves::ratioAt(m.ratio);
    p.op2_fine   = 0.0f;

    /* BRIGHT is the index. Feedback joins only in the last third, so the knob
     * is clean for most of its travel and grows teeth at the top rather than
     * being gritty everywhere. */
    p.op2_level = m.bright;
    const float bx = (m.bright - 0.66f) / 0.34f;
    const float bTop = bx < 0.0f ? 0.0f : (bx > 1.0f ? 1.0f : bx);

    /* BITE: feedback first, the shaper second, overlapping in the middle. So
     * the first half changes the SOURCE and the second half changes what is
     * done to it -- two different kinds of dirt from one knob. */
    const float fb = m.bite < 0.6f ? (m.bite / 0.6f) : 1.0f;
    p.op2_fbk = fb * 0.75f + bTop * 0.25f;
    const float dr = m.bite < 0.35f ? 0.0f : (m.bite - 0.35f) / 0.65f;
    p.dist = dr;

    /* Envelopes. Three knobs drive eight values, under the coupling rule in
     * hank_curves.h: the modulator is always shorter than the carrier. */
    p.op1_a = m.attack;
    p.op1_d = m.decay;
    p.op1_r = m.decay;                       /* release == decay */
    p.op1_s = m.sustain;
    /* A CONSTANT RATIO IN TIME -- see knobForTimeRatio. Scaling the knob
     * position instead fixed the exponent, so the modulator ran anywhere from
     * 1.6x to 11x faster than the carrier depending only on how long the note
     * was, which is what sorted the whole bank into growls and bells. */
    p.op2_a = curves::modAttackKnob(m.attack);
    p.op2_d = curves::modDecayKnob(m.decay);
    p.op2_r = curves::modDecayKnob(m.decay);
    p.op2_s = curves::MOD_SUSTAIN_FLOOR + m.sustain * curves::MOD_SUSTAIN_RATIO;

    /* PITCH is bipolar around centre, and its decay is a fraction of the
     * amp's -- a pitch bend that outlasts the note reads as being out of
     * tune, not as an envelope. */
    /* PITCH WAS A MACRO AND IS NOT ONE ANY MORE.
     *
     * Across the whole factory bank it sat at 0.5 -- off -- in all but two
     * presets, and the reported symptom was simply "pitch doesn't really do
     * anything". A 2-op FM voice already takes its attack character from the
     * modulator, so a pitch sweep on top is either inaudible or a special
     * effect, and it was holding an encoder on an instrument that had no
     * percussion, breath or noise anywhere on it.
     *
     * The pitch envelope itself is removed with it -- see Voice::render.
     * NOISE takes the encoder. Crush was the other candidate and it was
     * already a Setup parameter, so promoting it would have added a control
     * without adding a sound; it has since been removed too.
     */
    p.noise = m.noise;

    /* TONE: one sweep. Resonance rises in the middle and falls away again at
     * the top, so the knob is a tone control rather than a filter panel --
     * fully open should sound open, not peaky. */
    p.cutoff    = m.tone;
    p.mix       = 0.0f;                      /* low-pass */
    p.env_depth = 0.0f;
    const float peak = 1.0f - fabsf(m.tone - 0.55f) / 0.55f;
    p.res       = (peak < 0.0f ? 0.0f : peak) * 0.55f;
    p.filter_on = 1;
}

/* ---------------------------------------------------------------- Voice */

void Voice::reset() {
    note_ = -1; freq_ = target_freq_ = 0.0f; age_ = 0;
    ph1_ = ph2_ = fb_ = 0.0f;
    last_amp_ = 0.0f;
    svf_.reset();
    amp_ = Env(); mod_ = Env(); fenv_ = Env();
}

static inline float noteHz(float note) {
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

void Voice::noteOn(int note, float vel, const Params &p, bool legato) {
    note_ = note; vel_ = vel;
    target_freq_ = noteHz((float)note + p.pitch);
    if (!legato || freq_ <= 0.0f) freq_ = target_freq_;
    float gt = curves::glideTime(p.glide);
    glide_c_ = (gt <= 0.0f) ? 1.0f : 1.0f - expf(-1.0f / (gt * SAMPLE_RATE));

    /* STEALING A SOUNDING VOICE MUST NOT DISCONTINUE IT.
     *
     * A free voice is silent, so resetting its oscillator phases and clearing
     * the filter state costs nothing and gives every note the same attack
     * transient -- which is what those resets are FOR. Do the same to a voice
     * that is still audible and both are steps in the output: the phase jumps
     * mid-cycle, and the filter's stored energy vanishes between one sample and
     * the next. That is the click heard when more notes are played than the
     * patch has voices, and it grows with resonance, because the state thrown
     * away is larger.
     *
     * Nothing else on this path is discontinuous, which is why skipping the two
     * resets is the whole fix and no steal-fade is needed: the envelopes
     * retrigger from their CURRENT value rather than from zero (Env::noteOn
     * does not touch v_), and changing frequency moves the phase's DERIVATIVE,
     * not the phase.
     */
    const bool audible = active() && last_amp_ > 1.0e-4f;

    if (!audible) {
        if (p.op1_phase_reset) ph1_ = p.op1_phase;
        if (p.op2_phase_reset) ph2_ = p.op2_phase;
    }

    if (!legato) {
        amp_.noteOn(p.op1_a, p.op1_d, p.op1_s, p.op1_r);
        mod_.noteOn(p.op2_a, p.op2_d, p.op2_s, p.op2_r);
        fenv_.noteOn(p.f_a, p.f_d, p.f_s, p.f_r);
        if (!audible) svf_.reset();
    }
}

void Voice::noteOff() { amp_.noteOff(); mod_.noteOff(); fenv_.noteOff(); }

void Voice::fastRelease(float tau) {
    amp_.fastRelease(tau, SAMPLE_RATE);
    mod_.fastRelease(tau, SAMPLE_RATE);
    fenv_.fastRelease(tau, SAMPLE_RATE);
}

float Voice::render(const Params &p) {
    freq_ += (target_freq_ - freq_) * glide_c_;

    const float ae = amp_.process();
    /* Attack=0 is immediate: nothing here delays the first sample. */
    last_amp_ = ae;
    const float me = mod_.process();
    const float fe = fenv_.process();
    if (amp_.idle()) return 0.0f;

    /* LINEAR in the envelope's OUTPUT: the index follows the modulator
     * envelope directly, so the timbre falls exactly as the envelope does. */
    const float index0 = curves::levelToIndex(p.op2_level) * me;
    /* FEEDBACK IS ENVELOPED TOO, and by the SAME square as the index: both are
     * driven by the operator's own output level, and the level->depth laws are
     * non-linear. An operator feeds back its own OUTPUT, which carries the
     * modulator's envelope, so a patch whose modulator decays must lose its
     * feedback grit as it decays. Holding feedback constant kept the raspiest
     * part of the sound alive for the whole note. Linear in the envelope's
     * output, for the same reason as the index: the squaring lives in the
     * ADSR's sustain, not here. */
    const float fbk0  = curves::fbkToDepth(p.op2_fbk) * me;

    /* ONE oscillator pair per voice. Unison was three, at 3x the cost, and its
     * detune law was measured off another engine -- see
     * docs/plans/2026-09-07-eight-knob-redesign.md. Chain FX do a better job of
     * width than three detuned copies inside a monosynth voice, and there is no
     * encoder left for it. */
    /* THE PITCH ENVELOPE IS GONE WITH THE MACRO THAT DROVE IT. Nothing could
     * write p_env once PITCH became NOISE, so it was a per-sample envelope
     * and a branch computing a value that was always zero. */
    const float f = freq_;

    const float ratio = p.op2_coarse + p.op2_fine;
    const float modHz = f * ratio;
    const float roll  = curves::modRolloff(modHz);
    const float index = index0 * roll;
    const float fbk   = fbk0 * roll;

    ph2_ += modHz / SAMPLE_RATE;
    if (ph2_ >= 1.0f) ph2_ -= floorf(ph2_);
    const float m = sine(ph2_ + fb_ * fbk * INV_TWO_PI);
    fb_ = 0.5f * (fb_ + m);                /* averaged over two samples -- the classic DX-style damping of an operator's self-feedback */

    ph1_ += f / SAMPLE_RATE;
    if (ph1_ >= 1.0f) ph1_ -= floorf(ph1_);
    const float sum = sine(ph1_ + m * index * INV_TWO_PI);

    float out = sum * ae;

    /* AHEAD OF THE FILTER, ON THE MODULATOR'S ENVELOPE -- see noiseGain.
     * `me` decays faster than `ae` by construction, so this is an attack
     * transient by default and opens out into sustained noise as SUSTAIN
     * rises; being pre-filter is what gives TONE something to shape. The
     * carrier ducks by the same law, so the knob crossfades instead of
     * stacking -- see noiseDuck. */
    if (p.noise > 0.0001f) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float n = (float)(int32_t)rng_ * (1.0f / 2147483648.0f);
        out *= (1.0f - curves::noiseDuck(p.noise));
        out += n * curves::noiseGain(p.noise) * me;
    }
    out *= vel_;

    if (p.filter_on) {
        svf_.set(curves::cutoffHz(p.cutoff, p.env_depth, fe, p.keytrack != 0, note_),
                 curves::resToK(p.res));
        /* No output normalisation: the filter passes what it passes, so
         * resonance is heard as resonance rather than evened out. */
        out = svf_.process(out, p.mix);
    }
    return out;
}

/* --------------------------------------------------------------- Engine */

Engine::Engine() { hank_init_tables(); reset(); }

void Engine::reset() {
    for (int i = 0; i < MAX_VOICES; i++) v_[i].reset();
    dcx_[0] = dcx_[1] = dcy_[0] = dcy_[1] = 0.0f;
    memset(held_, 0, sizeof held_);
    clock_ = 0; bend_ = 0.0f; sustain_ = false;
    lastNote_ = -1;
}

void Engine::pitchBend(float semis) { bend_ = semis; }
void Engine::sustain(bool on) {
    sustain_ = on;
    if (!on) for (int n = 0; n < 128; n++)
        if (!held_[n]) for (int i = 0; i < MAX_VOICES; i++)
            if (v_[i].active() && v_[i].note() == n) v_[i].noteOff();
}

void Engine::noteOn(int note, int vel) {
    if (note < 0 || note > 127) return;

    /* MAP THE MACROS BEFORE THE VOICE READS THEM. An envelope is captured at
     * note-on, so mapping only in render() means every note is played with the
     * PREVIOUS block's values -- and the first note after a preset change with
     * the previous preset's. Heard as a kick that sustains because it took its
     * envelope from whatever was loaded before it. */
    if (p_.macro_mode) applyMacros(p_);

    /* LEGATO MEANS A NOTE IS STILL HELD, NOT "A VOICE IS STILL AUDIBLE".
     * Keying off Voice::active() treats a voice that is merely still in its
     * release -- including one left ringing by the previous patch -- as an
     * overlapping note, so the amp envelope is never retriggered and the new
     * note inherits the old one's decaying tail. The guide is explicit: in
     * mono the voice glides only "when two notes are overlapping". */
    int heldBefore = 0;
    for (int n = 0; n < 128; n++) if (held_[n]) heldBefore++;

    held_[note] = true;
    monoStackPush(note);
    const int limit = (p_.voice_count < 1) ? 1
                    : (p_.voice_count > MAX_VOICES ? MAX_VOICES : p_.voice_count);

    if (limit == 1) {                                  /* mono, legato glide */
        bool legato = (heldBefore > 0) && v_[0].active();
        v_[0].noteOn(note, vel / 127.0f, p_, legato);
        v_[0].setAge(++clock_);
        lastNote_ = note;
        return;
    }
    int slot = -1;
    for (int i = 0; i < limit; i++)
        if (!v_[i].active()) { slot = i; break; }
    if (slot < 0) {                                    /* steal the oldest */
        uint32_t best = 0xffffffffu;
        for (int i = 0; i < limit; i++)
            if (v_[i].age() < best) { best = v_[i].age(); slot = i; }
    }
    v_[slot].noteOn(note, vel / 127.0f, p_, false);
    v_[slot].setAge(++clock_);
    lastNote_ = note;
}

void Engine::noteOff(int note) {
    if (note < 0 || note > 127) return;
    held_[note] = false;
    monoStackRemove(note);
    if (sustain_) return;

    /*
     * MONO RETURNS TO THE NOTE STILL UNDER YOUR FINGER.
     *
     * Hold A, play B, release B: this released the voice because it was
     * playing B, without ever asking whether anything else was still held --
     * so the sound simply stopped while A was still down. Every monosynth
     * returns to A there, and it is the half of legato playing that makes
     * trills and grace notes work at all.
     *
     * LAST-NOTE priority, from a stack, rather than highest or lowest: it is
     * what "go back to the one I am still holding" means when more than one
     * is, and it is the only order that makes a run of overlapping notes
     * unwind the way it was played.
     *
     * The return is LEGATO -- pitch moves, envelopes do not restart. That is
     * forced by consistency, not taste: A->B was already legato (see noteOn),
     * so retriggering on the way back would make the same pair of fingers
     * produce two different envelopes depending on direction.
     */
    const int limit = (p_.voice_count < 1) ? 1
                    : (p_.voice_count > MAX_VOICES ? MAX_VOICES : p_.voice_count);
    if (limit == 1) {
        if (mono_n_ > 0 && v_[0].active() && v_[0].note() == note)
            v_[0].noteOn(mono_stack_[mono_n_ - 1], v_[0].velocity(), p_, true);
        else if (mono_n_ == 0)
            for (int i = 0; i < MAX_VOICES; i++)
                if (v_[i].active() && v_[i].note() == note) v_[i].noteOff();
        return;
    }

    for (int i = 0; i < MAX_VOICES; i++)
        if (v_[i].active() && v_[i].note() == note) v_[i].noteOff();
}

void Engine::monoStackRemove(int note) {
    int w = 0;
    for (int r = 0; r < mono_n_; r++)
        if (mono_stack_[r] != note) mono_stack_[w++] = mono_stack_[r];
    mono_n_ = w;
}

void Engine::monoStackPush(int note) {
    monoStackRemove(note);
    if (mono_n_ == MONO_STACK) {          /* drop the oldest, never the newest */
        for (int i = 1; i < MONO_STACK; i++) mono_stack_[i - 1] = mono_stack_[i];
        mono_n_--;
    }
    mono_stack_[mono_n_++] = note;
}

/*
 * A PRESET LOAD IS A NEW SOUND, SO THE OLD ONE HAS TO STOP.
 *
 * Without this a held note kept sounding while every parameter changed
 * underneath it, which is neither the old patch nor the new one -- and with a
 * long DECAY it outlived the change by seconds.
 *
 * A short RELEASE, not a mute. Zeroing the output is a click, which is the
 * defect this module has already been bitten by twice; ~8 ms is fast enough to
 * read as "it stopped" and slow enough to have no edge in it. The envelopes
 * are re-armed from scratch on the next note-on, so nothing is left behind.
 */
void Engine::releaseAll(float tau) {
    memset(held_, 0, sizeof held_);
    mono_n_ = 0;
    for (int i = 0; i < MAX_VOICES; i++)
        if (v_[i].active()) v_[i].fastRelease(tau);
}

void Engine::allNotesOff() {
    memset(held_, 0, sizeof held_);
    mono_n_ = 0;
    for (int i = 0; i < MAX_VOICES; i++) v_[i].noteOff();
}

/*
 * PARAMETER SLEW.
 *
 * Every continuous control is read straight out of Params on the sample it is
 * used, so a host write lands as a STEP: an encoder detent is an instantaneous
 * jump in cutoff, index, or output gain, and a run of them is the staircase you
 * hear as zipper noise. The controls that hurt most are the ones that are plain
 * multipliers (volume, level) and the ones that move a lot per detent (cutoff
 * over 144 semitones).
 *
 * Only the CONTINUOUS controls are slewed. Structural ones must not be: the
 * voice count, the filter and keytrack switches and the phase-reset flags are
 * decisions rather than quantities, and half of a decision is meaningless. The
 * ADSR times are excluded for a different reason -- they are read once, at
 * note-on, so slewing them would do nothing but blur which value a note got.
 *
 * WHEN NOTHING IS SOUNDING THIS SNAPS instead of ramping. Loading a preset sets
 * thirty-odd params at once, and ramping into them would make the first note
 * after a preset change sweep audibly through the old patch. It also keeps the
 * measurement harness honest: a render sets its params and then plays a note,
 * so with no voice active the slew is a no-op and the corpus numbers mean the
 * same thing before and after this existed.
 */
void Engine::slewParams(float c) {
    Params &d = sp_; const Params &t = p_;
    d.op2_coarse += (t.op2_coarse - d.op2_coarse) * c;
    d.op2_fine   += (t.op2_fine   - d.op2_fine)   * c;
    d.op2_level  += (t.op2_level  - d.op2_level)  * c;
    d.op2_fbk    += (t.op2_fbk    - d.op2_fbk)    * c;
    d.op2_phase  += (t.op2_phase  - d.op2_phase)  * c;
    d.op1_phase  += (t.op1_phase  - d.op1_phase)  * c;
    d.cutoff     += (t.cutoff     - d.cutoff)     * c;
    d.res        += (t.res        - d.res)        * c;
    d.mix        += (t.mix        - d.mix)        * c;
    d.env_depth  += (t.env_depth  - d.env_depth)  * c;
    d.unison     += (t.unison     - d.unison)     * c;
    d.glide      += (t.glide      - d.glide)      * c;
    d.pitch      += (t.pitch      - d.pitch)      * c;
    d.crush      += (t.crush      - d.crush)      * c;
    d.volume     += (t.volume     - d.volume)     * c;
    d.width      += (t.width      - d.width)      * c;
    d.dist       += (t.dist       - d.dist)       * c;
    /* NOISE WAS MISSING HERE AND THE SYMPTOM WAS "the knob does nothing".
     *
     * sp_ is what the voices actually render from, and a field absent from
     * both this list and the structural block above only ever reaches it via
     * `sp_ = p_`, which runs when NO voice is sounding. So the knob worked
     * perfectly from silence and was inert while anything was ringing -- and
     * with 8 voices and release tails, that is most of the time you spend
     * turning knobs. It measured fine on the bench for the same reason: every
     * rendered note starts from an idle engine. tools/hank-bench/slew_coverage.py
     * now fails on a Params float that appears in neither place. */
    d.noise      += (t.noise      - d.noise)      * c;
    d.preset_gain += (t.preset_gain - d.preset_gain) * c;
}

/* BITE -- ours, chosen by ear over three rendered palettes.
 *
 * FM already makes rich ODD harmonics, so a symmetric odd-order curve (tanh,
 * cubic) mostly adds more of what is there; that is why tanh sounds boring in
 * THIS instrument specifically. What FM lacks at integer ratios is EVEN
 * content, so that is what this supplies: tube asymmetry throughout, with an
 * octave arriving as the knob is pushed.
 *
 * ASYMMETRIC RAILS, NOT ASYMMETRIC SLOPE. A different slope per side stops
 * being asymmetric once both sides clip -- the curve becomes a square wave,
 * which is purely odd -- so the character evaporated exactly where it was
 * pushed hardest. A different rail HEIGHT survives any drive.
 *
 * The input must be unity-scale; see the call site. */
float Engine::biteShape(float x, float d) {
    const float o = curves::BITE_OCTAVE * d;
    const float g = 1.0f + curves::BITE_DRIVE * d;
    const float rect = 2.0f * fabsf(x) - 1.0f;
    const float m = (1.0f - o) * x + o * rect;
    float y = tanhf(g * m);
    if (y < 0.0f) y *= curves::BITE_RAIL;
    /* the asymmetry makes DC by construction; this removes it */
    const float b = (y - dcx_[0]) + dcy_[0] * 0.9971f;
    dcx_[0] = y; dcy_[0] = b;
    const float wet = b * curves::biteNorm(d);

    /*
     * MIXED IN BY d, SO THAT d = 0 IS THE DRY SIGNAL EXACTLY.
     *
     * It returned `wet` unconditionally, and at d = 0 that is tanh(x) -- which
     * is not x. The voice sum is unity-scale by construction, precisely where
     * tanh compresses hardest, so the moment the shaper switched on the output
     * dropped 2.83 dB and changed shape. Reported by a user as "a click on the
     * Bite knob between 35 and 36", and 0.35 is exactly where applyMacros
     * leaves dist at zero and starts raising it.
     *
     * Slewing cannot fix this and neither can moving the gate: the slew ramps
     * d, and the discontinuity is AT d = 0. Only the curve meeting the dry
     * signal there removes it, which is what this does -- and it removes the
     * shape change with the level step, not merely the level step.
     *
     * The cost is that the shaping is now quadratic in BITE rather than linear
     * (dr already ramps 0..1 across the knob, and this multiplies by it again),
     * so the middle of the travel is gentler. The top is untouched, which is
     * where the curve was chosen by ear.
     */
    return x + d * (wet - x);
}

void Engine::render(float *out_lr, int frames) {
    /* Macros are control-rate: mapped once per block, then slewed per sample. */
    if (p_.macro_mode) applyMacros(p_);

    const int   limit  = (p_.voice_count < 1) ? 1
                       : (p_.voice_count > MAX_VOICES ? MAX_VOICES : p_.voice_count);
    /* Snap rather than ramp when nothing is sounding -- a preset load writes
     * every param at once, and sweeping into them would make the first note
     * after a change audibly glide through the previous patch. */
    bool anyActive = false;
    for (int i = 0; i < limit; i++) if (v_[i].active()) { anyActive = true; break; }
    if (!sp_primed_ || !anyActive) { sp_ = p_; sp_primed_ = true; }

    const float slew_c = 1.0f - expf(-1.0f / (curves::PARAM_SLEW_TAU * SAMPLE_RATE));

    /* Structural, never slewed -- see slewParams. */
    sp_.voice_count      = p_.voice_count;
    sp_.filter_on        = p_.filter_on;
    sp_.keytrack         = p_.keytrack;
    sp_.op1_phase_reset  = p_.op1_phase_reset;
    sp_.op2_phase_reset  = p_.op2_phase_reset;
    sp_.op1_a = p_.op1_a; sp_.op1_d = p_.op1_d; sp_.op1_s = p_.op1_s; sp_.op1_r = p_.op1_r;
    sp_.op2_a = p_.op2_a; sp_.op2_d = p_.op2_d; sp_.op2_s = p_.op2_s; sp_.op2_r = p_.op2_r;
    sp_.f_a   = p_.f_a;   sp_.f_d   = p_.f_d;   sp_.f_s   = p_.f_s;   sp_.f_r   = p_.f_r;

    /* crushLevels is a powf, so it stays at block rate -- and a quantizer's
     * step count is inherently a staircase, so smoothing it per sample would
     * buy nothing. Everything else below is cheap enough to follow the slew
     * sample by sample, which is what actually removes the zipper. */
    const float levels  = curves::crushLevels(sp_.crush);
    const bool  docrush = sp_.crush > 0.0005f || p_.crush > 0.0005f;
    const bool  dodist  = sp_.dist  > 0.0005f || p_.dist  > 0.0005f;

    for (int n = 0; n < frames; n++) {
        slewParams(slew_c);

        /*
         * VOLUME IS THE MULTIPLIER IT SAYS IT IS. It was `volume * 2.0f`, so
         * 0.5 was unity and the top of the knob was +6 dB -- a control that
         * boosts above its own nominal has to be documented to be used, and
         * this one was not. A slot sitting at 0.997 was running at 1.994x and
         * clipping on a three-note chord while the bench measured the same
         * patch at -11 dBFS, because the bench used the shipped preset value
         * and the device used the slot's live one.
         *
         * 1.0 is unity and the knob can only attenuate. Level calibration is
         * preset_gain's job, which is the whole reason it exists.
         */
        const float vol    = sp_.volume * sp_.preset_gain;

        float s = 0.0f;
        for (int i = 0; i < limit; i++)
            if (v_[i].active()) s += v_[i].render(sp_);
        /* BITE BEFORE THE OUTPUT SCALE. Its rectifier is 2|x|-1, which only
         * means anything when |x| is around 1. Applying it after the 0.15
         * output scale fed it a signal 6.7x too small, so the -1 offset
         * dominated, the shaper flattened, and the knob lost 21 dB and went
         * silent at the top. Here the voice sum is unity-scale, which is what
         * the curve was chosen against. */
        if (dodist) s = biteShape(s, sp_.dist);

        /* A settled pure carrier peaks at 0.15 at velocity 127 and volume 0.5. */
        s *= 0.15f;

        if (docrush) { float q = levels; s = floorf(s * q + 0.5f) / q; }

        float l = s, r = s;   /* mono source; chain FX provide any width */

        out_lr[n * 2]     = curves::softClip(l * vol);
        out_lr[n * 2 + 1] = curves::softClip(r * vol);
    }
}

} /* namespace hank */
