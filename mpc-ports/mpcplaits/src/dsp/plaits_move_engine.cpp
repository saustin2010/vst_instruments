#include "plaits_move_engine.h"
#include <cstdlib>
#include <new>

#include <math.h>
#include <string.h>

#include <algorithm>
#include <array>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"

// mpc-vst-plaits: this bridge started as schwung-mrhyde's (see src/VENDORED.md). The parameter set, modulation
// (2 LFOs, 2 envelopes, cycle, random, MOD + ASSIGN grids), AMP modes, TRIG RATE, host tempo/transport, pitch
// bend and voice handling are this port's.

namespace {

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline uint32_t xorshift32(uint32_t &state) {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

static inline float rand_bipolar(uint32_t &state) {
    return ((float)(xorshift32(state) & 0x00FFFFFFu) / 16777216.0f) * 2.0f - 1.0f;
}

static inline float rand_unipolar(uint32_t &state) {
    return ((float)(xorshift32(state) & 0x00FFFFFFu) / 16777216.0f);
}

static inline float curve_pow(float x, float curve) {
    x = clampf(x, 0.0f, 1.0f);
    float c = clampf(curve, 0.1f, 4.0f);
    return powf(x, c);
}

static inline float pan_gain_left(float pan) {
    return sqrtf(0.5f * (1.0f - pan));
}

static inline float pan_gain_right(float pan) {
    return sqrtf(0.5f * (1.0f + pan));
}

// Plaits' own attenuverter response (voice.h ApplyModulations): roughly squared, with a small dead zone around
// the centre, so small amounts are fine and a full turn is about +-1. Used for every MOD / ASSIGN amount.
static inline float plaits_amount_curve(float a) {
    return a * std::max(fabsf(a) - 0.05f, 0.05f) * 1.05f;
}

// Clean below -2.5 dBFS, then a tanh knee up to full scale. MrHyde's tanhf() on the whole mix coloured even a
// single voice (about 0.3 dB of peak compression at one voice, much more on chords).
static inline float soft_clip(float x) {
    const float knee = 0.75f;
    float a = fabsf(x);
    if (a <= knee) return x;
    return copysignf(knee + (1.0f - knee) * tanhf((a - knee) / (1.0f - knee)), x);
}

enum {
    PPF_LFO_SINE = 0,
    PPF_LFO_TRIANGLE = 1,
    PPF_LFO_SAW = 2,
    PPF_LFO_SQUARE = 3,
    PPF_LFO_RANDOM = 4,
    PPF_LFO_SMOOTH_RANDOM = 5
};

enum {
    PPF_CYCLE_LINEAR = 0,
    PPF_CYCLE_EXPONENTIAL = 1,
    PPF_CYCLE_LOGARITHMIC = 2
};

enum {
    PPF_RANDOM_SAMPLE_HOLD = 0,
    PPF_RANDOM_SMOOTH = 1,
    PPF_RANDOM_DRIFT = 2
};

enum {
    PPF_VOICE_MONO = 0,
    PPF_VOICE_POLY = 1,
    PPF_VOICE_MONO_LEGATO = 2
};

enum {
    PPF_FILTER_LP = 0,
    PPF_FILTER_BP = 1,
    PPF_FILTER_HP = 2
};

enum {
    ENV_OFF = 0,
    ENV_ATTACK = 1,
    ENV_DECAY = 2,
    ENV_SUSTAIN = 3,
    ENV_RELEASE = 4
};

constexpr int kMaxEngines = 24;
constexpr int kVoiceRamBytes = 16384;
constexpr int kChunkFrames = 12;
// TRIG is a gate (high while the key is down) instead of MrHyde's 3-block pulse; see src/VENDORED.md.
constexpr int kStealFadeBlocks = 8;                               // ~2 ms fade-out before a stolen voice restarts
constexpr float kSilenceLevel = 1e-4f;                            // -80 dBFS, per voice before the mix gain
constexpr int kSilenceHoldSamples = PPF_SAMPLE_RATE / 10;         // released + this long below it -> voice free
constexpr int kMaxReleaseSamples = PPF_SAMPLE_RATE * 30;          // safety cap on a released voice's tail
constexpr int kMonoStackSize = 16;
constexpr float kFixedVoiceMixGain = 0.3535533905932738f;  // 1/sqrt(8)
constexpr float kDefaultBpm = 120.0f;
constexpr float kPitchModSemitones = 24.0f;                // a full MOD / ASSIGN amount on PITCH
static const float kPitchCompensationSemitones =
    12.0f * log2f(plaits::kCorrectedSampleRate / (float)PPF_SAMPLE_RATE);

// Sync divisions (module.json lfoN_div / random_div), in beats: 4 bars ... 1/32.
static const float kDivBeats[] = {16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f,
                                  1.0f / 6.0f, 0.125f};
constexpr int kDivCount = (int)(sizeof(kDivBeats) / sizeof(kDivBeats[0]));
// TRIG RATE (module.json trig_rate), in beats; index 0 is off.
static const float kTrigBeats[] = {0.0f, 1.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
constexpr int kTrigCount = (int)(sizeof(kTrigBeats) / sizeof(kTrigBeats[0]));

// Engines that shape their own amplitude (Plaits bypasses the LPG for them): 6-op FM x3, Chiptune (when clocked),
// inharmonic string, modal resonator, the three drums. In ENV mode they get ENV 1 as a post-VCA.
static inline bool self_enveloped(int engine) {
    return (engine >= 2 && engine <= 4) || engine == 7 || engine >= 19;
}

static float div_hz(int div, float bpm) {
    float beats = kDivBeats[clampi(div, 0, kDivCount - 1)];
    return clampf(bpm, 20.0f, 300.0f) / (60.0f * beats);
}

static float eval_lfo(int shape, float phase, float rand_hold, float rand_a, float rand_b) {
    phase -= floorf(phase);
    switch (shape) {
        case PPF_LFO_TRIANGLE:
            return 1.0f - 4.0f * fabsf(phase - 0.5f);
        case PPF_LFO_SAW:
            return 2.0f * phase - 1.0f;
        case PPF_LFO_SQUARE:
            return phase < 0.5f ? 1.0f : -1.0f;
        case PPF_LFO_RANDOM:
            return rand_hold;
        case PPF_LFO_SMOOTH_RANDOM:
            return lerpf(rand_a, rand_b, phase);
        case PPF_LFO_SINE:
        default:
            return sinf(phase * 6.28318530718f);
    }
}

static float shape_cycle_value(float v, int shape) {
    v = clampf(v, 0.0f, 1.0f);
    if (shape == PPF_CYCLE_EXPONENTIAL) return v * v;
    if (shape == PPF_CYCLE_LOGARITHMIC) return sqrtf(v);
    return v;
}

struct LfoState {
    float phase;
    float hold;
    float a;
    float b;

    void reset(uint32_t &rng) {
        phase = 0.0f;
        hold = rand_bipolar(rng);
        a = hold;
        b = rand_bipolar(rng);
    }

    // Advances by delta cycles; returns the value at the new phase (+ the PHASE knob's offset).
    float advance(float delta, int shape, float offset, uint32_t &rng) {
        phase += delta;
        if (phase >= 1.0f) {
            phase -= floorf(phase);
            hold = rand_bipolar(rng);
            a = b;
            b = rand_bipolar(rng);
        }
        return eval_lfo(shape, phase + offset, hold, a, b);
    }
};

struct EnvState {
    int stage;
    float value;
};

struct VoiceState {
    bool active;
    bool gate;
    int note;
    float velocity;
    float poly_aftertouch;
    float note_current;
    float note_target;
    int unison_slot;
    float pan_current;
    uint32_t age;
    bool trig_sent;          // TRIG level Plaits got on the last block
    int trig_gap_blocks;     // blocks to hold TRIG low so a restrike of a held voice makes a new edge
    bool just_triggered;
    float trig_count;        // TRIG RATE: samples since the last retrigger
    int silent_samples;
    int release_samples;

    // A steal fades the old sound out first; the new note waits here.
    int fade_blocks;
    bool pending;
    bool pending_released;
    int pending_note;
    int pending_slot;
    float pending_velocity;
    float pending_detune;
    float pending_pan;

    EnvState env[PPF_NUM_ENVS];
    LfoState lfo[PPF_NUM_LFOS];     // used when that LFO is PER VOICE
    float lfo_value[PPF_NUM_LFOS];

    float cycle_value;
    int cycle_dir;

    float random_value;
    float random_target;
    float random_phase;

    alignas(8) char ram[kVoiceRamBytes];   // mpc-vst-plaits: engines keep pointer tables in it
    stmlib::BufferAllocator allocator;
    plaits::Voice synth;
};

struct FilterState {
    float z1_l;
    float z2_l;
    float z1_r;
    float z2_r;
};

struct FilterCoefficients {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    bool bypass;
};

static inline FilterCoefficients make_filter_coefficients(int mode,
                                                          float cutoff,
                                                          float resonance) {
    FilterCoefficients c{};
    c.b0 = 1.0f;
    c.b1 = 0.0f;
    c.b2 = 0.0f;
    c.a1 = 0.0f;
    c.a2 = 0.0f;
    c.bypass = false;

    float cutoff_norm = clampf(cutoff, 0.0f, 1.0f);
    float resonance_norm = clampf(resonance, 0.0f, 1.0f);
    if (mode == PPF_FILTER_LP && cutoff_norm >= 0.999f && resonance_norm <= 1e-4f) {
        c.bypass = true;
        return c;
    }

    float hz = 20.0f * powf(1000.0f, cutoff_norm);
    hz = clampf(hz, 20.0f, 18000.0f);
    float q = 0.5f + resonance_norm * 19.5f;

    float omega = 2.0f * 3.14159265359f * hz / (float)PPF_SAMPLE_RATE;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * q);

    float b0 = 0.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha;

    if (mode == PPF_FILTER_HP) {
        b0 = (1.0f + cs) * 0.5f;
        b1 = -(1.0f + cs);
        b2 = (1.0f + cs) * 0.5f;
    } else if (mode == PPF_FILTER_BP) {
        b0 = sn * 0.5f;
        b1 = 0.0f;
        b2 = -sn * 0.5f;
    } else {
        b0 = (1.0f - cs) * 0.5f;
        b1 = 1.0f - cs;
        b2 = (1.0f - cs) * 0.5f;
    }

    float inv_a0 = 1.0f / a0;
    c.b0 = b0 * inv_a0;
    c.b1 = b1 * inv_a0;
    c.b2 = b2 * inv_a0;
    c.a1 = a1 * inv_a0;
    c.a2 = a2 * inv_a0;
    return c;
}

static inline float process_biquad_sample(float in,
                                          const FilterCoefficients &c,
                                          float &z1,
                                          float &z2) {
    if (c.bypass) return in;
    float out = c.b0 * in + z1;
    z1 = c.b1 * in - c.a1 * out + z2;
    z2 = c.b2 * in - c.a2 * out;
    return out;
}

}  // namespace

// Keep in sync with the defaults in src/module.json (a host may push those to every parameter).
void ppf_default_params(ppf_params_t *params) {
    if (!params) return;
    memset(params, 0, sizeof(*params));

    params->model = 8;
    params->pitch = 0.0f;
    params->harmonics = 0.5f;
    params->timbre = 0.5f;
    params->morph = 0.5f;
    params->aux_mix = 0.0f;

    params->amp_mode = PPF_AMP_GATE;
    params->lpg_color = 0.55f;
    params->lpg_decay = 0.35f;
    params->voice_mode = PPF_VOICE_POLY;
    params->polyphony = 4;
    params->unison = 1;
    params->detune = 0.1f;
    params->spread = 0.25f;
    params->bend_range = 2;
    params->velocity_curve = 1.0f;
    params->filter_mode = PPF_FILTER_LP;
    params->filter_cutoff = 1.0f;
    params->volume = 1.0f;

    for (int n = 0; n < PPF_NUM_ENVS; ++n) {
        params->env[n].attack_ms = 5;
        params->env[n].decay_ms = 200;
        params->env[n].sustain = n == 0 ? 0.7f : 0.0f;
        params->env[n].release_ms = 300;
        params->env[n].retrig = 1;
    }
    for (int n = 0; n < PPF_NUM_LFOS; ++n) {
        params->lfo[n].rate = 2.0f;
        params->lfo[n].div = 6;   // 1/8
    }
    params->cycle_attack_ms = 300;
    params->cycle_decay_ms = 300;
    params->cycle_bipolar = 1;
    params->random_rate = 3.0f;
    params->random_div = 8;       // 1/16
    params->random_slew = 0.3f;
    params->random_retrig = 1;

    params->asg_src[0] = PPF_SRC_LFO2;
    params->asg_src[1] = PPF_SRC_VELOCITY;
    params->asg_src[2] = PPF_SRC_AFTERTOUCH;
    params->asg_src[3] = PPF_SRC_MOD_WHEEL;
    params->asg_dst[0] = PPF_DST_FM;
    params->asg_dst[1] = PPF_DST_LPG_DECAY;
    params->asg_dst[2] = PPF_DST_CUTOFF;
    params->asg_dst[3] = PPF_DST_VOLUME;
    for (int r = 0; r < PPF_GRID; ++r) {
        for (int c = 0; c < PPF_GRID; ++c) {
            params->mod_on[r][c] = 1;
            params->asg_on[r][c] = 1;
        }
    }
}

struct ppf_engine_t::Impl {
    std::array<VoiceState, PPF_MAX_VOICES> voices;
    LfoState lfo[PPF_NUM_LFOS];     // the shared (not PER VOICE) LFOs
    FilterState filter_state;
    uint32_t rng_state;
    uint32_t age_counter;
    float bpm;
    float bend;
    float wheel;
    float pressure;

    Impl() : rng_state(0x93A15F3Du), age_counter(1), bpm(kDefaultBpm), bend(0.0f), wheel(0.0f), pressure(0.0f) {}

    // Held keys in mono/legato, oldest first: releasing the sounding key goes back to the one before it.
    int mono_notes[kMonoStackSize];
    float mono_velocities[kMonoStackSize];
    int mono_count;

    void init_voices() {
        for (auto &v : voices) {
            v.active = false;
            v.gate = false;
            v.note = 0;
            v.velocity = 0.0f;
            v.poly_aftertouch = 0.0f;
            v.note_current = 0.0f;
            v.note_target = 0.0f;
            v.unison_slot = 0;
            v.pan_current = 0.0f;
            v.age = 0;
            v.trig_sent = false;
            v.trig_gap_blocks = 0;
            v.just_triggered = false;
            v.trig_count = 0.0f;
            v.silent_samples = 0;
            v.release_samples = 0;
            v.fade_blocks = 0;
            v.pending = false;
            v.pending_released = false;
            v.allocator.Init(v.ram, sizeof(v.ram));
            v.synth.Init(&v.allocator);
            for (auto &e : v.env) {
                e.stage = ENV_OFF;
                e.value = 0.0f;
            }
            for (int n = 0; n < PPF_NUM_LFOS; ++n) {
                v.lfo[n].reset(rng_state);
                v.lfo_value[n] = 0.0f;
            }
            v.cycle_value = 0.0f;
            v.cycle_dir = 1;
            v.random_value = 0.0f;
            v.random_phase = 0.0f;
            v.random_target = rand_bipolar(rng_state);
        }
        mono_count = 0;
        for (auto &l : lfo) l.reset(rng_state);
        filter_state.z1_l = 0.0f;
        filter_state.z2_l = 0.0f;
        filter_state.z1_r = 0.0f;
        filter_state.z2_r = 0.0f;
    }

    int active_voice_budget(const ppf_params_t &params) const {
        int poly = clampi(params.polyphony, 1, PPF_MAX_VOICES);
        int uni = clampi(params.unison, 1, PPF_MAX_VOICES);
        int budget = poly * uni;
        if (budget > PPF_MAX_VOICES) budget = PPF_MAX_VOICES;
        if (params.voice_mode != PPF_VOICE_POLY) budget = uni;
        return clampi(budget, 1, PPF_MAX_VOICES);
    }

    // A free voice, else the oldest released one, else the oldest held one. MrHyde took the oldest of all, so a
    // released voice still ringing out kept its slot while held keys were stolen.
    int pick_voice(int budget, const bool *used) const {
        int released = -1, held = -1;
        uint32_t released_age = 0xffffffffu, held_age = 0xffffffffu;
        for (int i = 0; i < budget; ++i) {
            if (used[i]) continue;
            const VoiceState &v = voices[i];
            if (!v.active) return i;
            if (!v.gate && !v.pending) {
                if (v.age < released_age) { released_age = v.age; released = i; }
            } else if (v.age < held_age) {
                held_age = v.age;
                held = i;
            }
        }
        if (released >= 0) return released;
        return held >= 0 ? held : 0;
    }

    // restrike: give Plaits a new TRIG edge (a new LPG strike / excitation) and restart what retriggers on a
    // note. Legato leaves it out.
    void trigger_voice(const ppf_params_t &params, VoiceState &v, int note, int unison_slot, float velocity,
                       float detune, float pan, bool restrike) {
        bool was_active = v.active && v.gate;
        bool fresh = !v.active;
        v.active = true;
        v.gate = true;
        v.note = note;
        v.unison_slot = unison_slot;
        v.velocity = clampf(velocity, 0.0f, 1.0f);
        v.age = age_counter++;
        v.note_target = (float)note + detune;
        if (!was_active) {
            v.note_current = v.note_target;
        }
        v.pan_current = clampf(pan, -1.0f, 1.0f);
        v.silent_samples = 0;
        v.release_samples = 0;

        for (int n = 0; n < PPF_NUM_ENVS; ++n) {
            if ((restrike && params.env[n].retrig) || v.env[n].stage == ENV_OFF) v.env[n].stage = ENV_ATTACK;
        }
        if (restrike) {
            if (v.trig_sent) v.trig_gap_blocks = 1;
            v.just_triggered = true;
            v.trig_count = 0.0f;
            for (int n = 0; n < PPF_NUM_LFOS; ++n) {
                if (!params.lfo[n].per_voice) continue;
                if (params.lfo[n].retrig) v.lfo[n].reset(rng_state);
                else if (fresh) v.lfo[n].phase = rand_unipolar(rng_state);
            }
            if (params.random_retrig) {
                v.random_phase = 0.0f;
                v.random_target = rand_bipolar(rng_state);
                if (params.random_mode == PPF_RANDOM_SAMPLE_HOLD) v.random_value = v.random_target;
            }
        }
        if (v.cycle_dir == 0) v.cycle_dir = 1;
    }

    // Stealing a voice that is sounding another note: fade it out first, then start the note (finish_fade).
    void start_voice(const ppf_params_t &params, VoiceState &v, int note, int unison_slot, float velocity,
                     float detune, float pan) {
        if (!v.active || (v.note == note && !v.pending)) {
            trigger_voice(params, v, note, unison_slot, velocity, detune, pan, true);
            return;
        }
        if (!v.pending) v.fade_blocks = kStealFadeBlocks;
        v.pending = true;
        v.pending_released = false;
        v.pending_note = note;
        v.pending_slot = unison_slot;
        v.pending_velocity = velocity;
        v.pending_detune = detune;
        v.pending_pan = pan;
        v.age = age_counter++;
    }

    void finish_fade(const ppf_params_t &params, VoiceState &v) {
        v.gate = false;
        if (!v.pending) {
            v.active = false;
            for (auto &e : v.env) e.stage = ENV_OFF;
            return;
        }
        v.pending = false;
        v.active = false;   // a fresh start: no glide from the stolen note
        trigger_voice(params, v, v.pending_note, v.pending_slot, v.pending_velocity, v.pending_detune,
                      v.pending_pan, true);
        if (v.pending_released) release_voice(v);
    }

    static void release_voice(VoiceState &v) {
        v.gate = false;
        for (auto &e : v.env) {
            if (e.stage != ENV_OFF) e.stage = ENV_RELEASE;
        }
    }

    void release_matching_note(int note, int budget) {
        for (int i = 0; i < budget; ++i) {
            VoiceState &v = voices[i];
            if (!v.active) continue;
            if (v.pending) {
                if (v.pending_note == note) v.pending_released = true;
            } else if (v.note == note) {
                release_voice(v);
            }
        }
    }

    // A released voice is freed once its output has stayed below -80 dB for 100 ms. MrHyde freed it after a time
    // computed from LPG DECAY, which cut the tails of engines that decay on their own (strings, modal, 6-op, drums).
    void clear_inactive(int budget) {
        for (int i = 0; i < budget; ++i) {
            VoiceState &v = voices[i];
            if (!v.active || v.gate || v.pending || v.fade_blocks > 0) continue;
            if (v.silent_samples >= kSilenceHoldSamples || v.release_samples >= kMaxReleaseSamples) {
                v.active = false;
                for (auto &e : v.env) e.stage = ENV_OFF;
            }
        }
    }

    void mono_push(int note, float velocity) {
        mono_remove(note);
        if (mono_count == kMonoStackSize) {
            memmove(mono_notes, mono_notes + 1, sizeof(int) * (kMonoStackSize - 1));
            memmove(mono_velocities, mono_velocities + 1, sizeof(float) * (kMonoStackSize - 1));
            --mono_count;
        }
        mono_notes[mono_count] = note;
        mono_velocities[mono_count] = velocity;
        ++mono_count;
    }

    void mono_remove(int note) {
        for (int i = 0; i < mono_count; ++i) {
            if (mono_notes[i] != note) continue;
            memmove(mono_notes + i, mono_notes + i + 1, sizeof(int) * (mono_count - i - 1));
            memmove(mono_velocities + i, mono_velocities + i + 1, sizeof(float) * (mono_count - i - 1));
            --mono_count;
            return;
        }
    }

    void mono_play(const ppf_params_t &params, int budget, int unison, int note, float velocity, bool restrike) {
        for (int i = 0; i < unison; ++i) {
            float center = 0.5f * (float)(unison - 1);
            float detune = ((float)i - center) * params.detune * 0.6f;
            float pan = 0.0f;
            if (unison > 1) {
                pan = ((float)i / (float)(unison - 1)) * 2.0f - 1.0f;
                pan *= params.spread;
            }
            voices[i].pending = false;
            voices[i].fade_blocks = 0;
            trigger_voice(params, voices[i], note, i, velocity, detune, pan, restrike);
        }
        for (int i = unison; i < budget; ++i) {
            voices[i].active = false;
            voices[i].gate = false;
            for (auto &e : voices[i].env) e.stage = ENV_OFF;
        }
    }
};

// mpc-vst-plaits: Plaits assumes its RAM starts zeroed (it does on the module: .bss), and several engines'
// Init() leave state untouched (e.g. FMEngine's downsampler taps). Inside a long-running host the heap is
// reused, so that state could start as NaN: it went through the LPG filter, stuck there, and silenced every
// LPG engine on that voice (FM 2-Op and after). Build Impl in zeroed memory, as the module would.
ppf_engine_t::ppf_engine_t() : impl_(nullptr) {
    void *mem = std::calloc(1, sizeof(Impl));
    if (!mem) throw std::bad_alloc();
    impl_ = new (mem) Impl();
    ppf_default_params(&params_);
    init();
}

ppf_engine_t::~ppf_engine_t() {
    impl_->~Impl();
    std::free(impl_);
    impl_ = nullptr;
}

void ppf_engine_t::init() {
    impl_->init_voices();
}

void ppf_engine_t::set_params(const ppf_params_t &params) {
    if (params.voice_mode != params_.voice_mode) impl_->mono_count = 0;
    params_ = params;
    ppf_params_t &p = params_;
    p.model = clampi(p.model, 0, kMaxEngines - 1);
    p.pitch = clampf(p.pitch, -48.0f, 48.0f);
    p.harmonics = clampf(p.harmonics, 0.0f, 1.0f);
    p.timbre = clampf(p.timbre, 0.0f, 1.0f);
    p.morph = clampf(p.morph, 0.0f, 1.0f);
    p.timbre_att = clampf(p.timbre_att, -1.0f, 1.0f);
    p.fm_att = clampf(p.fm_att, -1.0f, 1.0f);
    p.morph_att = clampf(p.morph_att, -1.0f, 1.0f);
    p.aux_mix = clampf(p.aux_mix, 0.0f, 1.0f);
    p.amp_mode = clampi(p.amp_mode, PPF_AMP_GATE, PPF_AMP_DRONE);
    p.lpg_color = clampf(p.lpg_color, 0.0f, 1.0f);
    p.lpg_decay = clampf(p.lpg_decay, 0.0f, 1.0f);
    p.trig_rate = clampi(p.trig_rate, 0, kTrigCount - 1);
    p.voice_mode = clampi(p.voice_mode, 0, 2);
    p.polyphony = clampi(p.polyphony, 1, PPF_MAX_VOICES);
    p.unison = clampi(p.unison, 1, PPF_MAX_VOICES);
    p.detune = clampf(p.detune, 0.0f, 1.0f);
    p.spread = clampf(p.spread, 0.0f, 1.0f);
    p.glide_ms = clampi(p.glide_ms, 0, 2000);
    p.bend_range = clampi(p.bend_range, 0, 24);
    p.velocity_curve = clampf(p.velocity_curve, 0.1f, 4.0f);
    p.poly_aftertouch_curve = clampf(p.poly_aftertouch_curve, -1.0f, 1.0f);
    p.filter_mode = clampi(p.filter_mode, 0, 2);
    p.filter_cutoff = clampf(p.filter_cutoff, 0.0f, 1.0f);
    p.filter_resonance = clampf(p.filter_resonance, 0.0f, 1.0f);
    p.volume = clampf(p.volume, 0.0f, 2.0f);
    p.pan = clampf(p.pan, -1.0f, 1.0f);
    for (auto &e : p.env) {
        e.attack_ms = clampi(e.attack_ms, 0, 5000);
        e.decay_ms = clampi(e.decay_ms, 0, 5000);
        e.sustain = clampf(e.sustain, 0.0f, 1.0f);
        e.release_ms = clampi(e.release_ms, 0, 5000);
        e.retrig = e.retrig ? 1 : 0;
    }
    for (auto &l : p.lfo) {
        l.shape = clampi(l.shape, 0, 5);
        l.rate = clampf(l.rate, 0.01f, 40.0f);
        l.sync = l.sync ? 1 : 0;
        l.div = clampi(l.div, 0, kDivCount - 1);
        l.retrig = l.retrig ? 1 : 0;
        l.per_voice = l.per_voice ? 1 : 0;
        l.phase = clampf(l.phase, 0.0f, 1.0f);
    }
    p.cycle_attack_ms = clampi(p.cycle_attack_ms, 1, 5000);
    p.cycle_decay_ms = clampi(p.cycle_decay_ms, 1, 5000);
    p.cycle_shape = clampi(p.cycle_shape, 0, 2);
    p.random_mode = clampi(p.random_mode, 0, 2);
    p.random_rate = clampf(p.random_rate, 0.01f, 40.0f);
    p.random_div = clampi(p.random_div, 0, kDivCount - 1);
    p.random_slew = clampf(p.random_slew, 0.0f, 1.0f);
    for (int r = 0; r < PPF_GRID; ++r) {
        p.asg_src[r] = clampi(p.asg_src[r], 0, PPF_SRC_COUNT - 1);
        p.asg_dst[r] = clampi(p.asg_dst[r], 0, PPF_DST_COUNT - 1);
        for (int c = 0; c < PPF_GRID; ++c) {
            p.mod_fixed[r][c] = clampf(p.mod_fixed[r][c], -1.0f, 1.0f);
            p.asg_amt[r][c] = clampf(p.asg_amt[r][c], -1.0f, 1.0f);
            p.mod_on[r][c] = p.mod_on[r][c] ? 1 : 0;
            p.asg_on[r][c] = p.asg_on[r][c] ? 1 : 0;
        }
    }
}

int ppf_engine_t::effective_notes() const {
    if (params_.voice_mode != PPF_VOICE_POLY) return 1;
    int budget = impl_->active_voice_budget(params_);
    return std::max(1, budget / clampi(params_.unison, 1, PPF_MAX_VOICES));
}

void ppf_engine_t::note_on(int note, float velocity) {
    int budget = impl_->active_voice_budget(params_);
    int unison = clampi(params_.unison, 1, budget);
    bool mono_mode = params_.voice_mode != PPF_VOICE_POLY;
    bool legato = params_.voice_mode == PPF_VOICE_MONO_LEGATO;

    for (int n = 0; n < PPF_NUM_LFOS; ++n) {
        if (params_.lfo[n].retrig && !params_.lfo[n].per_voice) impl_->lfo[n].reset(impl_->rng_state);
    }

    if (mono_mode) {
        bool any_held = false;
        for (int i = 0; i < budget; ++i) {
            const VoiceState &v = impl_->voices[i];
            if (v.active && v.gate) {
                any_held = true;
                break;
            }
        }
        impl_->mono_push(note, velocity);
        impl_->mono_play(params_, budget, unison, note, velocity, !(legato && any_held));
        return;
    }

    std::array<int, PPF_MAX_VOICES> same_note_indices{};
    int same_note_count = 0;
    for (int i = 0; i < budget; ++i) {
        const VoiceState &v = impl_->voices[i];
        if (v.active && !v.pending && v.note == note) {
            same_note_indices[same_note_count++] = i;
        }
    }
    for (int i = unison; i < same_note_count; ++i) {
        impl_->release_voice(impl_->voices[same_note_indices[i]]);
    }
    if (same_note_count > unison) same_note_count = unison;

    std::array<bool, PPF_MAX_VOICES> used{};
    used.fill(false);
    int same_used = 0;
    for (int i = 0; i < unison; ++i) {
        int idx = same_used < same_note_count ? same_note_indices[same_used++] : impl_->pick_voice(budget, used.data());
        used[idx] = true;
        float center = 0.5f * (float)(unison - 1);
        float detune = ((float)i - center) * params_.detune * 0.6f;
        float pan = 0.0f;
        if (unison > 1) {
            pan = ((float)i / (float)(unison - 1)) * 2.0f - 1.0f;
            pan *= params_.spread;
        }
        impl_->start_voice(params_, impl_->voices[idx], note, i, velocity, detune, pan);
    }
}

void ppf_engine_t::note_off(int note) {
    int budget = impl_->active_voice_budget(params_);
    if (params_.voice_mode != PPF_VOICE_POLY) {
        // Releasing the sounding key goes back to the last key still held (MONO restrikes, LEGATO only changes
        // pitch); MrHyde went silent.
        Impl &m = *impl_;
        bool was_top = m.mono_count > 0 && m.mono_notes[m.mono_count - 1] == note;
        m.mono_remove(note);
        if (was_top && m.mono_count > 0) {
            int unison = clampi(params_.unison, 1, budget);
            m.mono_play(params_, budget, unison, m.mono_notes[m.mono_count - 1],
                        m.mono_velocities[m.mono_count - 1], params_.voice_mode == PPF_VOICE_MONO);
            return;
        }
        if (m.mono_count > 0) return;
    }
    impl_->release_matching_note(note, budget);
}

void ppf_engine_t::poly_aftertouch(int note, float pressure) {
    int budget = impl_->active_voice_budget(params_);
    for (int i = 0; i < budget; ++i) {
        VoiceState &v = impl_->voices[i];
        if (v.active && v.note == note) {
            v.poly_aftertouch = clampf(pressure, 0.0f, 1.0f);
        }
    }
}

void ppf_engine_t::channel_pressure(float pressure) {
    impl_->pressure = clampf(pressure, 0.0f, 1.0f);
}

void ppf_engine_t::pitch_bend(float amount) {
    impl_->bend = clampf(amount, -1.0f, 1.0f);
}

void ppf_engine_t::mod_wheel(float amount) {
    impl_->wheel = clampf(amount, 0.0f, 1.0f);
}

void ppf_engine_t::set_tempo(float bpm) {
    if (bpm > 0.0f) impl_->bpm = clampf(bpm, 20.0f, 300.0f);
}

// The MPC started playing (or jumped back): shared LFOs, free-running per-voice LFOs and the random source start
// from the top, so synced modulation lines up with the sequence.
void ppf_engine_t::transport_start() {
    for (auto &l : impl_->lfo) l.phase = 0.0f;
    for (auto &v : impl_->voices) {
        for (int n = 0; n < PPF_NUM_LFOS; ++n) {
            if (!params_.lfo[n].retrig) v.lfo[n].phase = 0.0f;
        }
        v.random_phase = 0.0f;
    }
}

void ppf_engine_t::all_notes_off() {
    int budget = impl_->active_voice_budget(params_);
    for (int i = 0; i < budget; ++i) {
        VoiceState &v = impl_->voices[i];
        v.pending = false;
        impl_->release_voice(v);
    }
    impl_->mono_count = 0;
}

void ppf_engine_t::render(float *out_l, float *out_r, int frames) {
    if (!out_l || !out_r || frames <= 0) return;
    for (int i = 0; i < frames; ++i) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    const ppf_params_t &P = params_;
    Impl &I = *impl_;
    int budget = I.active_voice_budget(P);
    float lfo_hz[PPF_NUM_LFOS];
    for (int n = 0; n < PPF_NUM_LFOS; ++n) {
        lfo_hz[n] = P.lfo[n].sync ? div_hz(P.lfo[n].div, I.bpm) : P.lfo[n].rate;
    }
    float random_hz = P.random_sync ? div_hz(P.random_div, I.bpm) : P.random_rate;
    float trig_step = P.trig_rate > 0 ? kTrigBeats[P.trig_rate] * 60.0f / I.bpm * (float)PPF_SAMPLE_RATE : 0.0f;

    for (int frame_pos = 0; frame_pos < frames; frame_pos += kChunkFrames) {
        int chunk = std::min(kChunkFrames, frames - frame_pos);
        float dt = (float)chunk / (float)PPF_SAMPLE_RATE;

        float shared_lfo[PPF_NUM_LFOS];
        for (int n = 0; n < PPF_NUM_LFOS; ++n) {
            shared_lfo[n] = I.lfo[n].advance(lfo_hz[n] * dt, P.lfo[n].shape, P.lfo[n].phase, I.rng_state);
        }
        float cutoff_mod_sum = 0.0f;
        float resonance_mod_sum = 0.0f;
        int cutoff_mod_count = 0;

        for (int vi = 0; vi < budget; ++vi) {
            VoiceState &v = I.voices[vi];
            if (!v.active) continue;

            float note_glide = 1.0f;
            if (P.glide_ms > 0) {
                float glide_samples = ((float)P.glide_ms * 0.001f) * (float)PPF_SAMPLE_RATE;
                note_glide = clampf((float)chunk / glide_samples, 0.0f, 1.0f);
            }
            v.note_current += (v.note_target - v.note_current) * note_glide;

            // TRIG RATE: while the key is held, retrigger in time with the MPC (a new TRIG edge, and the envelopes
            // that retrigger on notes). Chiptune steps its arpeggio; drums, modal and 6-op ratchet; PING re-plucks.
            if (trig_step > 0.0f && v.gate && !v.pending && P.amp_mode != PPF_AMP_DRONE) {
                v.trig_count += (float)chunk;
                if (v.trig_count >= trig_step) {
                    v.trig_count -= trig_step * floorf(v.trig_count / trig_step);
                    if (v.trig_sent) v.trig_gap_blocks = 1;
                    for (int n = 0; n < PPF_NUM_ENVS; ++n) {
                        if (P.env[n].retrig) v.env[n].stage = ENV_ATTACK;
                    }
                }
            }

            auto step_time = [chunk](float ms) -> float {
                float samples = std::max(1.0f, ms * 0.001f * (float)PPF_SAMPLE_RATE);
                return clampf((float)chunk / samples, 0.0f, 1.0f);
            };

            for (int n = 0; n < PPF_NUM_ENVS; ++n) {
                EnvState &e = v.env[n];
                const ppf_env_params_t &ep = P.env[n];
                if (!v.gate && e.stage != ENV_OFF) e.stage = ENV_RELEASE;
                switch (e.stage) {
                    case ENV_ATTACK:
                        e.value += (1.0f - e.value) * step_time((float)std::max(1, ep.attack_ms));
                        if (e.value >= 0.999f) {
                            e.value = 1.0f;
                            e.stage = ENV_DECAY;
                        }
                        break;
                    case ENV_DECAY:
                        e.value += (ep.sustain - e.value) * step_time((float)std::max(1, ep.decay_ms));
                        if (fabsf(e.value - ep.sustain) < 1e-3f) {
                            e.value = ep.sustain;
                            e.stage = ENV_SUSTAIN;
                        }
                        break;
                    case ENV_SUSTAIN:
                        e.value = ep.sustain;
                        break;
                    case ENV_RELEASE:
                        e.value += (0.0f - e.value) * step_time((float)std::max(1, ep.release_ms));
                        if (e.value <= 1e-4f) {
                            e.value = 0.0f;
                            e.stage = ENV_OFF;
                        }
                        break;
                    case ENV_OFF:
                    default:
                        e.value = 0.0f;
                        break;
                }
            }

            float cycle_up = step_time((float)std::max(1, P.cycle_attack_ms));
            float cycle_dn = step_time((float)std::max(1, P.cycle_decay_ms));
            if (P.cycle_retrig && v.just_triggered) {
                v.cycle_value = 0.0f;
                v.cycle_dir = 1;
            }
            if (v.cycle_dir > 0) {
                v.cycle_value += cycle_up;
                if (v.cycle_value >= 1.0f) {
                    v.cycle_value = 1.0f;
                    v.cycle_dir = -1;
                }
            } else {
                v.cycle_value -= cycle_dn;
                if (v.cycle_value <= 0.0f) {
                    v.cycle_value = 0.0f;
                    v.cycle_dir = 1;
                }
            }

            v.random_phase += random_hz * dt;
            while (v.random_phase >= 1.0f) {
                v.random_phase -= 1.0f;
                v.random_target = rand_bipolar(I.rng_state);
                if (P.random_mode == PPF_RANDOM_SAMPLE_HOLD) {
                    v.random_value = v.random_target;
                }
            }
            if (P.random_mode == PPF_RANDOM_SMOOTH) {
                float c = 0.02f + (1.0f - P.random_slew) * 0.35f;
                v.random_value += (v.random_target - v.random_value) * c;
            } else if (P.random_mode == PPF_RANDOM_DRIFT) {
                float step = (0.002f + random_hz * 0.0002f) * ((float)chunk / 12.0f);
                v.random_value += rand_bipolar(I.rng_state) * step;
                v.random_value = clampf(v.random_value, -1.0f, 1.0f);
            }

            for (int n = 0; n < PPF_NUM_LFOS; ++n) {
                v.lfo_value[n] = P.lfo[n].per_voice
                    ? v.lfo[n].advance(lfo_hz[n] * dt, P.lfo[n].shape, P.lfo[n].phase, I.rng_state)
                    : shared_lfo[n];
            }

            float cycle = shape_cycle_value(v.cycle_value, P.cycle_shape);
            if (P.cycle_bipolar) cycle = cycle * 2.0f - 1.0f;
            float velocity = curve_pow(v.velocity, P.velocity_curve);

            float src[PPF_SRC_COUNT] = {};
            src[PPF_SRC_LFO1] = v.lfo_value[0];
            src[PPF_SRC_LFO2] = v.lfo_value[1];
            src[PPF_SRC_ENV1] = v.env[0].value;
            src[PPF_SRC_ENV2] = v.env[1].value;
            src[PPF_SRC_CYCLE] = cycle;
            src[PPF_SRC_RANDOM] = v.random_value;
            src[PPF_SRC_VELOCITY] = velocity;
            src[PPF_SRC_AFTERTOUCH] =
                ppf_apply_bipolar_curve(std::max(v.poly_aftertouch, I.pressure), P.poly_aftertouch_curve);
            src[PPF_SRC_MOD_WHEEL] = I.wheel;

            // MOD page (fixed) + ASSIGN page (chosen sources/destinations), each amount on Plaits' curve.
            float dst[PPF_DST_COUNT] = {};
            for (int r = 0; r < PPF_GRID; ++r) {
                for (int c = 0; c < PPF_GRID; ++c) {
                    float a = P.mod_on[r][c] ? P.mod_fixed[r][c] : 0.0f;
                    if (a != 0.0f) dst[kPpfFixedDests[c]] += plaits_amount_curve(a) * src[kPpfFixedSources[r]];
                }
                int s = P.asg_src[r];
                if (s == PPF_SRC_OFF) continue;
                for (int c = 0; c < PPF_GRID; ++c) {
                    float a = P.asg_on[r][c] ? P.asg_amt[r][c] : 0.0f;
                    if (a != 0.0f && P.asg_dst[c] != PPF_DST_OFF) dst[P.asg_dst[c]] += plaits_amount_curve(a) * src[s];
                }
            }

            cutoff_mod_sum += dst[PPF_DST_CUTOFF];
            resonance_mod_sum += dst[PPF_DST_RESONANCE];
            ++cutoff_mod_count;

            float volume = clampf(P.volume + dst[PPF_DST_VOLUME], 0.0f, 2.0f);
            float aux_mix = clampf(P.aux_mix + dst[PPF_DST_AUX_MIX], 0.0f, 1.0f);

            int unison = clampi(P.unison, 1, budget);
            int slot = clampi(v.unison_slot, 0, unison - 1);
            float center = 0.5f * (float)(unison - 1);
            float detune_offset = ((float)slot - center) * P.detune * 0.6f;
            float spread_pan = 0.0f;
            if (unison > 1) {
                spread_pan = ((float)slot / (float)(unison - 1)) * 2.0f - 1.0f;
                spread_pan *= P.spread;
            }
            v.note_target = (float)v.note + detune_offset;
            float pan = clampf(spread_pan + P.pan + dst[PPF_DST_PAN], -1.0f, 1.0f);
            v.pan_current = pan;

            plaits::Patch patch{};
            // MIDI note + FREQUENCY (+-48) + pitch mods and bend reach far below anything the module plays; at very
            // low notes Plaits' oscillators blow up and later engines index their tables with the result (a crash
            // on the MPC). Keep to C0..C9 -- Plaits itself caps the top at 120. See src/VENDORED.md.
            float pitch = P.pitch + dst[PPF_DST_PITCH] * kPitchModSemitones + I.bend * (float)P.bend_range;
            patch.note = clampf(v.note_current + pitch + kPitchCompensationSemitones, 12.0f, 120.0f);
            // HARMONICS / TIMBRE / MORPH go in unclamped: Plaits adds its own envelope modulation and limits to
            // 0..1 once, at the end, as the module does with its knob + CV.
            patch.harmonics = P.harmonics + dst[PPF_DST_HARMONICS];
            patch.timbre = P.timbre + dst[PPF_DST_TIMBRE];
            patch.morph = P.morph + dst[PPF_DST_MORPH];
            patch.frequency_modulation_amount = clampf(P.fm_att + dst[PPF_DST_FM], -1.0f, 1.0f);
            patch.timbre_modulation_amount = P.timbre_att;
            patch.morph_modulation_amount = P.morph_att;
            patch.engine = P.model;
            patch.decay = clampf(P.lpg_decay + dst[PPF_DST_LPG_DECAY], 0.0f, 1.0f);
            patch.lpg_colour = clampf(P.lpg_color + dst[PPF_DST_LPG_COLOR], 0.0f, 1.0f);

            // Low for a block before a restrike of a held voice, and while a stolen voice fades out.
            v.trig_sent = v.gate && v.trig_gap_blocks == 0 && v.fade_blocks == 0;

            plaits::Modulations mods{};
            mods.frequency_patched = false;
            mods.timbre_patched = false;
            mods.morph_patched = false;
            mods.trigger_patched = true;
            mods.level_patched = true;
            mods.trigger = v.trig_sent ? 1.0f : 0.0f;
            float gate = v.gate ? 1.0f : 0.0f;
            float post = 1.0f;   // per-voice gain after Plaits
            switch (P.amp_mode) {
                case PPF_AMP_PING:
                    // LEVEL unpatched: TRIG strikes the LPG, which rings out with DECAY however long the key is held.
                    // Plaits' accent is then fixed, so velocity becomes a plain gain.
                    mods.level_patched = false;
                    post = velocity;
                    break;
                case PPF_AMP_ENV:
                    if (self_enveloped(P.model)) {
                        mods.level = velocity * gate;
                        post = v.env[0].value;
                    } else {
                        mods.level = velocity * v.env[0].value;
                    }
                    break;
                case PPF_AMP_DRONE:
                    // TRIG and LEVEL unpatched: the module free-running (LPG bypassed, String / Modal excited by
                    // dust), with ENV 1 as a VCA so keys still start and stop notes.
                    mods.trigger_patched = false;
                    mods.level_patched = false;
                    mods.trigger = 0.0f;
                    post = velocity * v.env[0].value;
                    break;
                case PPF_AMP_GATE:
                default:
                    mods.level = velocity * gate;
                    break;
            }

            plaits::Voice::Frame tmp[kChunkFrames];
            v.synth.Render(patch, mods, tmp, (size_t)chunk);

            float g_l = pan_gain_left(pan);
            float g_r = pan_gain_right(pan);
            float gain = kFixedVoiceMixGain * volume, gain_step = 0.0f;
            if (v.fade_blocks > 0) {
                gain_step = -gain / (float)(kStealFadeBlocks * chunk);
                gain *= (float)v.fade_blocks / (float)kStealFadeBlocks;
            }
            float peak = 0.0f;
            for (int j = 0; j < chunk; ++j) {
                float out_main = (float)tmp[j].out / 32768.0f;
                float out_aux = (float)tmp[j].aux / 32768.0f;
                float blended = lerpf(out_main, out_aux, aux_mix) * post;
                peak = std::max(peak, fabsf(blended));
                float mono = blended * gain;
                gain += gain_step;
                out_l[frame_pos + j] += mono * g_l;
                out_r[frame_pos + j] += mono * g_r;
            }

            v.just_triggered = false;
            if (v.trig_gap_blocks > 0) v.trig_gap_blocks--;
            if (v.gate) {
                v.silent_samples = 0;
                v.release_samples = 0;
            } else {
                v.silent_samples = peak < kSilenceLevel ? v.silent_samples + chunk : 0;
                v.release_samples += chunk;
            }
            if (v.fade_blocks > 0 && --v.fade_blocks == 0) {
                I.finish_fade(P, v);
            }
        }

        float cutoff = P.filter_cutoff;
        float resonance = P.filter_resonance;
        if (cutoff_mod_count > 0) {
            float inv_count = 1.0f / (float)cutoff_mod_count;
            cutoff += cutoff_mod_sum * inv_count;
            resonance += resonance_mod_sum * inv_count;
        }
        FilterCoefficients filter_coeffs = make_filter_coefficients(
            P.filter_mode,
            clampf(cutoff, 0.0f, 1.0f),
            clampf(resonance, 0.0f, 1.0f));

        for (int j = 0; j < chunk; ++j) {
            int idx = frame_pos + j;
            out_l[idx] = soft_clip(process_biquad_sample(out_l[idx], filter_coeffs, I.filter_state.z1_l, I.filter_state.z2_l));
            out_r[idx] = soft_clip(process_biquad_sample(out_r[idx], filter_coeffs, I.filter_state.z1_r, I.filter_state.z2_r));
        }

        I.clear_inactive(budget);
    }
}

#ifdef TEST
int ppf_engine_t::debug_active_voice_count() const {
    int budget = impl_->active_voice_budget(params_);
    int count = 0;
    for (int i = 0; i < budget; ++i) {
        if (impl_->voices[i].active) count++;
    }
    return count;
}

int ppf_engine_t::debug_active_note_count(int note) const {
    int budget = impl_->active_voice_budget(params_);
    int count = 0;
    for (int i = 0; i < budget; ++i) {
        const VoiceState &v = impl_->voices[i];
        if (v.active && v.note == note) count++;
    }
    return count;
}

int ppf_engine_t::debug_voice_active_engine(int voice_index) const {
    int budget = impl_->active_voice_budget(params_);
    if (voice_index < 0 || voice_index >= budget) return -1;
    const VoiceState &v = impl_->voices[voice_index];
    if (!v.active) return -1;
    return v.synth.active_engine();
}

float ppf_engine_t::debug_voice_note_target(int voice_index) const {
    int budget = impl_->active_voice_budget(params_);
    if (voice_index < 0 || voice_index >= budget) return 0.0f;
    const VoiceState &v = impl_->voices[voice_index];
    return v.note_target;
}

float ppf_engine_t::debug_voice_pan(int voice_index) const {
    int budget = impl_->active_voice_budget(params_);
    if (voice_index < 0 || voice_index >= budget) return 0.0f;
    const VoiceState &v = impl_->voices[voice_index];
    return v.pan_current;
}

float ppf_engine_t::debug_pitch_compensation_semitones() const {
    return kPitchCompensationSemitones;
}

float ppf_engine_t::debug_lfo_phase(int lfo) const {
    return impl_->lfo[clampi(lfo, 0, PPF_NUM_LFOS - 1)].phase;
}
#endif
