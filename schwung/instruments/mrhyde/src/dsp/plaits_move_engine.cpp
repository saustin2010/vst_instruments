#include "plaits_move_engine.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <array>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"

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

static inline float semitones_to_ratio(float semitones) {
    return powf(2.0f, semitones * (1.0f / 12.0f));
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
    PPF_ASSIGN_NONE = 0,
    PPF_ASSIGN_MORPH = 1,
    PPF_ASSIGN_FM_AMOUNT = 2,
    PPF_ASSIGN_LPG_DECAY = 3,
    PPF_ASSIGN_LPG_COLOR = 4,
    PPF_ASSIGN_FILTER_RESONANCE = 5,
    PPF_ASSIGN_PITCH = 6,
    PPF_ASSIGN_HARMONICS = 7,
    PPF_ASSIGN_TIMBRE = 8,
    PPF_ASSIGN_FILTER_CUTOFF = 9,
    PPF_ASSIGN_VOLUME = 10,
    PPF_ASSIGN_PAN = 11,
    PPF_ASSIGN_DETUNE = 12,
    PPF_ASSIGN_SPREAD = 13
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
constexpr int kMaxTriggerBlocks = 3;
constexpr float kFixedVoiceMixGain = 0.3535533905932738f;  // 1/sqrt(8)
constexpr float kSyncReferenceBpm = 120.0f;
static const float kPitchCompensationSemitones =
    12.0f * log2f(plaits::kCorrectedSampleRate / (float)PPF_SAMPLE_RATE);

static const float kSyncRateBars[] = {
    16.0f,
    8.0f,
    4.0f,
    2.0f,
    1.0f,
    0.5f,
    0.25f,
    0.125f,
    0.0625f,
    0.03125f,
    0.015625f
};

constexpr int kSyncRateCount = (int)(sizeof(kSyncRateBars) / sizeof(kSyncRateBars[0]));

static int note_release_samples_from_decay(float decay_value) {
    /* Follow Plaits short_decay scaling; keep a small safety tail so voice deactivation never truncates audio. */
    float decay = clampf(decay_value, 0.0f, 1.0f);
    float short_decay = (200.0f * (float)kChunkFrames) / (float)PPF_SAMPLE_RATE *
                        semitones_to_ratio(-96.0f * decay);
    float coeff = clampf(short_decay, 1e-6f, 0.999f);
    float blocks_to_minus_80db = logf(1e-4f) / logf(1.0f - coeff);
    if (!isfinite(blocks_to_minus_80db) || blocks_to_minus_80db < 1.0f) {
        blocks_to_minus_80db = 1.0f;
    }
    float release_ms = (blocks_to_minus_80db * (float)kChunkFrames * 1000.0f) / (float)PPF_SAMPLE_RATE;
    release_ms = release_ms * 1.15f + 120.0f;
    release_ms = clampf(release_ms, 20.0f, 25000.0f);
    int release_samples = (int)(release_ms * 0.001f * (float)PPF_SAMPLE_RATE);
    if (release_samples < kChunkFrames) release_samples = kChunkFrames;
    return release_samples;
}

static int note_release_samples(const ppf_params_t &params) {
    return note_release_samples_from_decay(params.lpg_decay);
}

static float sync_rate_hz_from_index(int index, float bpm) {
    int idx = clampi(index, 0, kSyncRateCount - 1);
    float clamped_bpm = clampf(bpm, 20.0f, 300.0f);
    float bar_seconds = 240.0f / clamped_bpm;
    float cycle_seconds = kSyncRateBars[idx] * bar_seconds;
    if (cycle_seconds <= 1e-6f) cycle_seconds = 1e-6f;
    return 1.0f / cycle_seconds;
}

static float quantize_sync_rate_hz(float hz, float bpm) {
    float clamped = clampf(hz, 0.01f, 40.0f);
    int best = 0;
    float best_err = 1e9f;
    for (int i = 0; i < kSyncRateCount; ++i) {
        float ref = sync_rate_hz_from_index(i, bpm);
        float err = fabsf(ref - clamped);
        if (err < best_err) {
            best_err = err;
            best = i;
        }
    }
    return sync_rate_hz_from_index(best, bpm);
}

static float eval_lfo(int shape,
                      float phase,
                      float rand_hold,
                      float rand_a,
                      float rand_b) {
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
    int trigger_blocks;
    int release_samples_remaining;
    int release_samples_total;
    float last_lpg_decay;

    int env_stage;
    float env_value;

    float cycle_value;
    int cycle_dir;

    float random_value;
    float random_target;
    float random_phase;

    char ram[kVoiceRamBytes];
    stmlib::BufferAllocator allocator;
    plaits::Voice synth;
};

struct GlobalModState {
    float lfo_phase;
    float lfo_rand_hold;
    float lfo_rand_a;
    float lfo_rand_b;
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

void ppf_default_params(ppf_params_t *params) {
    if (!params) return;
    memset(params, 0, sizeof(*params));

    params->model = 8;
    params->pitch = 0.0f;
    params->harmonics = 0.5f;
    params->timbre = 0.5f;
    params->morph = 0.5f;
    params->fm_amount = 0.0f;
    params->aux_mix = 0.0f;
    params->volume = 1.0f;
    params->pan = 0.0f;
    params->filter_mode = PPF_FILTER_LP;
    params->filter_cutoff = 1.0f;
    params->filter_resonance = 0.0f;

    params->lpg_decay = 0.35f;
    params->lpg_color = 0.55f;
    params->assign1_target = PPF_ASSIGN_NONE;
    params->assign2_target = PPF_ASSIGN_NONE;

    params->lfo_shape = PPF_LFO_SINE;
    params->lfo_rate = 2.0f;
    params->lfo_sync = 0;
    params->lfo_retrig = 0;
    params->lfo_phase = 0.0f;

    params->env_attack_ms = 5;
    params->env_decay_ms = 200;
    params->env_sustain = 0.0f;
    params->env_release_ms = 300;
    params->env_retrig = 1;

    params->cycle_attack_ms = 300;
    params->cycle_decay_ms = 300;
    params->cycle_shape = PPF_CYCLE_LINEAR;
    params->cycle_sync = 0;
    params->cycle_retrig = 0;
    params->cycle_bipolar = 1;

    params->random_mode = PPF_RANDOM_SAMPLE_HOLD;
    params->random_rate = 3.0f;
    params->random_sync = 0;
    params->random_slew = 0.3f;
    params->random_retrig = 1;

    params->velocity_curve = 1.0f;
    params->poly_aftertouch_curve = 0.0f;

    params->voice_mode = PPF_VOICE_POLY;
    params->polyphony = 4;
    params->unison = 1;
    params->detune = 0.1f;
    params->spread = 0.25f;
    params->glide_ms = 0;
}

struct ppf_engine_t::Impl {
    std::array<VoiceState, PPF_MAX_VOICES> voices;
    GlobalModState global_mod;
    FilterState filter_state;
    uint32_t rng_state;
    uint32_t age_counter;

    Impl() : rng_state(0x93A15F3Du), age_counter(1) {
        memset(&global_mod, 0, sizeof(global_mod));
    }

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
            v.trigger_blocks = 0;
            v.release_samples_remaining = 0;
            v.release_samples_total = 0;
            v.last_lpg_decay = 0.35f;
            v.allocator.Init(v.ram, sizeof(v.ram));
            v.synth.Init(&v.allocator);
            v.env_stage = ENV_OFF;
            v.env_value = 0.0f;
            v.cycle_value = 0.0f;
            v.cycle_dir = 1;
            v.random_value = 0.0f;
            v.random_phase = 0.0f;
            v.random_target = rand_bipolar(rng_state);
        }
        global_mod.lfo_phase = 0.0f;
        global_mod.lfo_rand_hold = rand_bipolar(rng_state);
        global_mod.lfo_rand_a = rand_bipolar(rng_state);
        global_mod.lfo_rand_b = rand_bipolar(rng_state);
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

    int pick_voice_to_steal(int budget) {
        int oldest = 0;
        uint32_t oldest_age = 0xffffffffu;
        for (int i = 0; i < budget; ++i) {
            if (!voices[i].active) return i;
            if (voices[i].age < oldest_age) {
                oldest_age = voices[i].age;
                oldest = i;
            }
        }
        return oldest;
    }

    void trigger_voice(VoiceState &v, int note, int unison_slot, float velocity, float detune, float pan, bool retrig_env) {
        bool was_active = v.active && v.gate;
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
        v.trigger_blocks = kMaxTriggerBlocks;
        v.release_samples_remaining = 0;
        v.release_samples_total = 0;

        if (retrig_env || v.env_stage == ENV_OFF) {
            v.env_stage = ENV_ATTACK;
        }
        if (v.cycle_dir == 0) v.cycle_dir = 1;
    }

    void release_matching_note(int note, int budget) {
        for (int i = 0; i < budget; ++i) {
            VoiceState &v = voices[i];
            if (!v.active) continue;
            if (v.note == note) {
                int release_samples = note_release_samples_from_decay(v.last_lpg_decay);
                v.gate = false;
                v.release_samples_remaining = release_samples;
                v.release_samples_total = release_samples;
                if (v.env_stage != ENV_OFF) {
                    v.env_stage = ENV_RELEASE;
                }
            }
        }
    }

    void clear_inactive(int budget) {
        for (int i = 0; i < budget; ++i) {
            VoiceState &v = voices[i];
            if (!v.gate && v.env_stage == ENV_OFF && v.release_samples_remaining <= 0) {
                v.active = false;
            }
        }
    }
};

ppf_engine_t::ppf_engine_t() : impl_(new Impl()) {
    ppf_default_params(&params_);
    init();
}

ppf_engine_t::~ppf_engine_t() {
    delete impl_;
    impl_ = nullptr;
}

void ppf_engine_t::init() {
    impl_->init_voices();
}

void ppf_engine_t::set_params(const ppf_params_t &params) {
    params_ = params;
    params_.model = clampi(params_.model, 0, kMaxEngines - 1);
    params_.harmonics = clampf(params_.harmonics, 0.0f, 1.0f);
    params_.timbre = clampf(params_.timbre, 0.0f, 1.0f);
    params_.morph = clampf(params_.morph, 0.0f, 1.0f);
    params_.fm_amount = clampf(params_.fm_amount, 0.0f, 1.0f);
    params_.aux_mix = clampf(params_.aux_mix, 0.0f, 1.0f);
    params_.volume = clampf(params_.volume, 0.0f, 2.0f);
    params_.pan = clampf(params_.pan, -1.0f, 1.0f);
    params_.filter_mode = clampi(params_.filter_mode, 0, 2);
    params_.filter_cutoff = clampf(params_.filter_cutoff, 0.0f, 1.0f);
    params_.filter_resonance = clampf(params_.filter_resonance, 0.0f, 1.0f);
    params_.lpg_decay = clampf(params_.lpg_decay, 0.0f, 1.0f);
    params_.lpg_color = clampf(params_.lpg_color, 0.0f, 1.0f);
    params_.assign1_target = clampi(params_.assign1_target, PPF_ASSIGN_NONE, PPF_ASSIGN_SPREAD);
    params_.assign2_target = clampi(params_.assign2_target, PPF_ASSIGN_NONE, PPF_ASSIGN_SPREAD);
    params_.polyphony = clampi(params_.polyphony, 1, PPF_MAX_VOICES);
    params_.unison = clampi(params_.unison, 1, PPF_MAX_VOICES);
    params_.voice_mode = clampi(params_.voice_mode, 0, 2);
    params_.lfo_shape = clampi(params_.lfo_shape, 0, 5);
    params_.lfo_rate = clampf(params_.lfo_rate, 0.01f, 40.0f);
    params_.cycle_shape = clampi(params_.cycle_shape, 0, 2);
    params_.random_mode = clampi(params_.random_mode, 0, 2);
    params_.random_rate = clampf(params_.random_rate, 0.01f, 40.0f);
    params_.env_attack_ms = clampi(params_.env_attack_ms, 0, 5000);
    params_.env_decay_ms = clampi(params_.env_decay_ms, 0, 5000);
    params_.env_release_ms = clampi(params_.env_release_ms, 0, 5000);
    params_.cycle_attack_ms = clampi(params_.cycle_attack_ms, 1, 5000);
    params_.cycle_decay_ms = clampi(params_.cycle_decay_ms, 1, 5000);
    params_.env_sustain = clampf(params_.env_sustain, 0.0f, 1.0f);
    params_.velocity_curve = clampf(params_.velocity_curve, 0.1f, 4.0f);
    params_.poly_aftertouch_curve = clampf(params_.poly_aftertouch_curve, -1.0f, 1.0f);
    params_.glide_ms = clampi(params_.glide_ms, 0, 2000);
    params_.glide_ms = (params_.glide_ms / 5) * 5;
    params_.spread = clampf(params_.spread, 0.0f, 1.0f);
    params_.detune = clampf(params_.detune, 0.0f, 1.0f);
}

void ppf_engine_t::note_on(int note, float velocity) {
    int budget = impl_->active_voice_budget(params_);
    int unison = clampi(params_.unison, 1, budget);
    bool mono_mode = params_.voice_mode != PPF_VOICE_POLY;
    bool legato = params_.voice_mode == PPF_VOICE_MONO_LEGATO;

    if (params_.lfo_retrig) {
        impl_->global_mod.lfo_phase = params_.lfo_phase;
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
        bool retrig = !(legato && any_held) && params_.env_retrig;
        for (int i = 0; i < unison; ++i) {
            float center = 0.5f * (float)(unison - 1);
            float detune_units = ((float)i - center);
            float detune = detune_units * params_.detune * 0.6f;
            float pan = 0.0f;
            if (unison > 1) {
                pan = ((float)i / (float)(unison - 1)) * 2.0f - 1.0f;
                pan *= params_.spread;
            }
            impl_->trigger_voice(impl_->voices[i], note, i, velocity, detune, pan, retrig);
            impl_->voices[i].last_lpg_decay = params_.lpg_decay;
        }
        for (int i = unison; i < budget; ++i) {
            impl_->voices[i].active = false;
            impl_->voices[i].gate = false;
            impl_->voices[i].env_stage = ENV_OFF;
        }
        return;
    }

    std::array<int, PPF_MAX_VOICES> same_note_indices{};
    int same_note_count = 0;
    for (int i = 0; i < budget; ++i) {
        const VoiceState &v = impl_->voices[i];
        if (v.active && v.note == note) {
            same_note_indices[same_note_count++] = i;
        }
    }
    for (int i = unison; i < same_note_count; ++i) {
        VoiceState &v = impl_->voices[same_note_indices[i]];
        v.gate = false;
        v.env_stage = ENV_RELEASE;
    }
    if (same_note_count > unison) same_note_count = unison;

    std::array<bool, PPF_MAX_VOICES> used{};
    used.fill(false);
    auto pick_retrigger_or_steal = [&]() -> int {
        for (int i = 0; i < same_note_count; ++i) {
            int idx = same_note_indices[i];
            if (!used[idx]) return idx;
        }
        int oldest = 0;
        uint32_t oldest_age = 0xffffffffu;
        for (int i = 0; i < budget; ++i) {
            if (used[i]) continue;
            if (!impl_->voices[i].active) return i;
            if (impl_->voices[i].age < oldest_age) {
                oldest_age = impl_->voices[i].age;
                oldest = i;
            }
        }
        return oldest;
    };

    for (int i = 0; i < unison; ++i) {
        int idx = pick_retrigger_or_steal();
        used[idx] = true;
        float center = 0.5f * (float)(unison - 1);
        float detune_units = ((float)i - center);
        float detune = detune_units * params_.detune * 0.6f;
        float pan = 0.0f;
        if (unison > 1) {
            pan = ((float)i / (float)(unison - 1)) * 2.0f - 1.0f;
            pan *= params_.spread;
        }
        impl_->trigger_voice(impl_->voices[idx], note, i, velocity, detune, pan, params_.env_retrig != 0);
        impl_->voices[idx].last_lpg_decay = params_.lpg_decay;
    }
}

void ppf_engine_t::note_off(int note) {
    impl_->release_matching_note(note, impl_->active_voice_budget(params_));
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

void ppf_engine_t::all_notes_off() {
    int budget = impl_->active_voice_budget(params_);
    for (int i = 0; i < budget; ++i) {
        int release_samples = note_release_samples_from_decay(impl_->voices[i].last_lpg_decay);
        impl_->voices[i].gate = false;
        impl_->voices[i].release_samples_remaining = release_samples;
        impl_->voices[i].release_samples_total = release_samples;
        impl_->voices[i].env_stage = ENV_RELEASE;
    }
}

void ppf_engine_t::render(float *out_l, float *out_r, int frames) {
    if (!out_l || !out_r || frames <= 0) return;
    for (int i = 0; i < frames; ++i) {
        out_l[i] = 0.0f;
        out_r[i] = 0.0f;
    }

    int budget = impl_->active_voice_budget(params_);
    float lfo_rate = clampf(params_.lfo_rate, 0.01f, 40.0f);
    if (params_.lfo_sync) {
        lfo_rate = quantize_sync_rate_hz(lfo_rate, kSyncReferenceBpm);
    }
    float lfo_inc = lfo_rate / (float)PPF_SAMPLE_RATE;

    for (int frame_pos = 0; frame_pos < frames; frame_pos += kChunkFrames) {
        int chunk = std::min(kChunkFrames, frames - frame_pos);
        impl_->global_mod.lfo_phase += lfo_inc * (float)chunk;
        if (impl_->global_mod.lfo_phase >= 1.0f) {
            impl_->global_mod.lfo_phase -= floorf(impl_->global_mod.lfo_phase);
            if (params_.lfo_shape == PPF_LFO_RANDOM) {
                impl_->global_mod.lfo_rand_hold = rand_bipolar(impl_->rng_state);
            } else if (params_.lfo_shape == PPF_LFO_SMOOTH_RANDOM) {
                impl_->global_mod.lfo_rand_a = impl_->global_mod.lfo_rand_b;
                impl_->global_mod.lfo_rand_b = rand_bipolar(impl_->rng_state);
            }
        }
        float lfo = eval_lfo(params_.lfo_shape,
                             impl_->global_mod.lfo_phase,
                             impl_->global_mod.lfo_rand_hold,
                             impl_->global_mod.lfo_rand_a,
                             impl_->global_mod.lfo_rand_b);
        float cutoff_mod_sum = 0.0f;
        float resonance_mod_sum = 0.0f;
        int cutoff_mod_count = 0;

        for (int vi = 0; vi < budget; ++vi) {
            VoiceState &v = impl_->voices[vi];
            if (!v.active && v.env_stage == ENV_OFF) continue;

            float note_glide = 1.0f;
            if (params_.glide_ms > 0) {
                float glide_samples = ((float)params_.glide_ms * 0.001f) * (float)PPF_SAMPLE_RATE;
                note_glide = clampf((float)chunk / glide_samples, 0.0f, 1.0f);
            }
            v.note_current += (v.note_target - v.note_current) * note_glide;

            if (v.gate && (params_.env_retrig || v.env_stage == ENV_OFF)) {
                if (v.env_stage == ENV_OFF) v.env_stage = ENV_ATTACK;
            }
            if (!v.gate && v.env_stage != ENV_OFF) {
                v.env_stage = ENV_RELEASE;
            }
            if (!v.gate && v.release_samples_remaining > 0) {
                v.release_samples_remaining -= chunk;
                if (v.release_samples_remaining < 0) v.release_samples_remaining = 0;
            }

            auto step_time = [chunk](float ms) -> float {
                float samples = std::max(1.0f, ms * 0.001f * (float)PPF_SAMPLE_RATE);
                return clampf((float)chunk / samples, 0.0f, 1.0f);
            };

            float atk_step = step_time((float)std::max(1, params_.env_attack_ms));
            float dec_step = step_time((float)std::max(1, params_.env_decay_ms));
            float rel_step = step_time((float)std::max(1, params_.env_release_ms));

            switch (v.env_stage) {
                case ENV_ATTACK:
                    v.env_value += (1.0f - v.env_value) * atk_step;
                    if (v.env_value >= 0.999f) {
                        v.env_value = 1.0f;
                        v.env_stage = ENV_DECAY;
                    }
                    break;
                case ENV_DECAY:
                    v.env_value += (params_.env_sustain - v.env_value) * dec_step;
                    if (fabsf(v.env_value - params_.env_sustain) < 1e-3f) {
                        v.env_value = params_.env_sustain;
                        v.env_stage = ENV_SUSTAIN;
                    }
                    break;
                case ENV_SUSTAIN:
                    v.env_value = params_.env_sustain;
                    break;
                case ENV_RELEASE:
                    v.env_value += (0.0f - v.env_value) * rel_step;
                    if (v.env_value <= 1e-4f) {
                        v.env_value = 0.0f;
                        v.env_stage = ENV_OFF;
                    }
                    break;
                case ENV_OFF:
                default:
                    v.env_value = 0.0f;
                    break;
            }

            float cycle_up = step_time((float)std::max(1, params_.cycle_attack_ms));
            float cycle_dn = step_time((float)std::max(1, params_.cycle_decay_ms));
            if (params_.cycle_retrig && v.trigger_blocks == kMaxTriggerBlocks) {
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

            float rand_rate = clampf(params_.random_rate, 0.01f, 40.0f);
            if (params_.random_sync) {
                rand_rate = quantize_sync_rate_hz(rand_rate, kSyncReferenceBpm);
            }
            v.random_phase += rand_rate * ((float)chunk / (float)PPF_SAMPLE_RATE);
            while (v.random_phase >= 1.0f) {
                v.random_phase -= 1.0f;
                v.random_target = rand_bipolar(impl_->rng_state);
                if (params_.random_mode == PPF_RANDOM_SAMPLE_HOLD) {
                    v.random_value = v.random_target;
                }
            }
            if (params_.random_mode == PPF_RANDOM_SMOOTH) {
                float slew = clampf(params_.random_slew, 0.0f, 1.0f);
                float c = 0.02f + (1.0f - slew) * 0.35f;
                v.random_value += (v.random_target - v.random_value) * c;
            } else if (params_.random_mode == PPF_RANDOM_DRIFT) {
                float step = (0.002f + rand_rate * 0.0002f) * ((float)chunk / 12.0f);
                v.random_value += rand_bipolar(impl_->rng_state) * step;
                v.random_value = clampf(v.random_value, -1.0f, 1.0f);
            }

            float cycle = shape_cycle_value(v.cycle_value, params_.cycle_shape);
            if (params_.cycle_bipolar) cycle = cycle * 2.0f - 1.0f;

            ppf_mod_sources_t src{};
            src.lfo = lfo;
            src.env = v.env_value;
            src.cycle_env = cycle;
            src.random = v.random_value;
            src.velocity = curve_pow(v.velocity, params_.velocity_curve);
            src.poly_aftertouch = ppf_apply_bipolar_curve(v.poly_aftertouch, params_.poly_aftertouch_curve);

            float pitch = ppf_apply_destination_modulation(
                params_.pitch, src, params_.pitch_mod, -96.0f, 96.0f);
            float harmonics = ppf_apply_destination_modulation(
                params_.harmonics, src, params_.harmonics_mod, 0.0f, 1.0f);
            float timbre = ppf_apply_destination_modulation(
                params_.timbre, src, params_.timbre_mod, 0.0f, 1.0f);
            float morph = params_.morph;
            float fm_amount = params_.fm_amount;
            float lpg = params_.lpg_decay;
            float lpg_color = params_.lpg_color;
            float volume = params_.volume;
            float pan_offset = params_.pan;
            float detune = params_.detune;
            float spread = params_.spread;

            cutoff_mod_sum += ppf_modulation_sum(src, params_.cutoff_mod);
            ++cutoff_mod_count;

            auto apply_assign_target = [&](int target, float amount) {
                switch (target) {
                    case PPF_ASSIGN_MORPH:
                        morph += amount;
                        break;
                    case PPF_ASSIGN_FM_AMOUNT:
                        fm_amount += amount;
                        break;
                    case PPF_ASSIGN_LPG_DECAY:
                        lpg += amount;
                        break;
                    case PPF_ASSIGN_LPG_COLOR:
                        lpg_color += amount;
                        break;
                    case PPF_ASSIGN_FILTER_RESONANCE:
                        resonance_mod_sum += amount;
                        break;
                    case PPF_ASSIGN_PITCH:
                        pitch += amount;
                        break;
                    case PPF_ASSIGN_HARMONICS:
                        harmonics += amount;
                        break;
                    case PPF_ASSIGN_TIMBRE:
                        timbre += amount;
                        break;
                    case PPF_ASSIGN_FILTER_CUTOFF:
                        cutoff_mod_sum += amount;
                        break;
                    case PPF_ASSIGN_VOLUME:
                        volume += amount;
                        break;
                    case PPF_ASSIGN_PAN:
                        pan_offset += amount;
                        break;
                    case PPF_ASSIGN_DETUNE:
                        detune += amount;
                        break;
                    case PPF_ASSIGN_SPREAD:
                        spread += amount;
                        break;
                    case PPF_ASSIGN_NONE:
                    default:
                        break;
                }
            };

            apply_assign_target(params_.assign1_target, ppf_modulation_sum(src, params_.assign1_mod));
            apply_assign_target(params_.assign2_target, ppf_modulation_sum(src, params_.assign2_mod));

            pitch = clampf(pitch, -96.0f, 96.0f);
            harmonics = clampf(harmonics, 0.0f, 1.0f);
            timbre = clampf(timbre, 0.0f, 1.0f);
            morph = clampf(morph, 0.0f, 1.0f);
            fm_amount = clampf(fm_amount, 0.0f, 1.0f);
            lpg = clampf(lpg, 0.0f, 1.0f);
            lpg_color = clampf(lpg_color, 0.0f, 1.0f);
            volume = clampf(volume, 0.0f, 2.0f);
            pan_offset = clampf(pan_offset, -1.0f, 1.0f);
            detune = clampf(detune, 0.0f, 1.0f);
            spread = clampf(spread, 0.0f, 1.0f);

            int unison = clampi(params_.unison, 1, budget);
            int slot = clampi(v.unison_slot, 0, unison - 1);
            float center = 0.5f * (float)(unison - 1);
            float detune_units = ((float)slot - center);
            float detune_offset = detune_units * detune * 0.6f;
            float spread_pan = 0.0f;
            if (unison > 1) {
                spread_pan = ((float)slot / (float)(unison - 1)) * 2.0f - 1.0f;
                spread_pan *= spread;
            }
            v.note_target = (float)v.note + detune_offset;
            float pan = clampf(spread_pan + pan_offset, -1.0f, 1.0f);
            v.pan_current = pan;
            v.last_lpg_decay = lpg;

            plaits::Patch patch{};
            // Align pitch to host sample-rate when running Plaits core outside
            // its original 47.872kHz corrected hardware clock domain.
            patch.note = v.note_current + pitch + kPitchCompensationSemitones;
            patch.harmonics = harmonics;
            patch.timbre = timbre;
            patch.morph = morph;
            patch.frequency_modulation_amount = fm_amount;
            patch.timbre_modulation_amount = 0.0f;
            patch.morph_modulation_amount = 0.0f;
            patch.engine = params_.model;
            patch.decay = lpg;
            patch.lpg_colour = lpg_color;

            plaits::Modulations mods{};
            mods.engine = 0.0f;
            mods.note = 0.0f;
            mods.frequency = 0.0f;
            mods.harmonics = 0.0f;
            mods.timbre = 0.0f;
            mods.morph = 0.0f;
            mods.trigger = (v.trigger_blocks > 0) ? 1.0f : 0.0f;
            mods.level = clampf(v.velocity, 0.0f, 1.0f) * (v.gate ? 1.0f : 0.0f) * volume;
            mods.frequency_patched = false;
            mods.timbre_patched = false;
            mods.morph_patched = false;
            mods.trigger_patched = true;
            mods.level_patched = true;

            plaits::Voice::Frame tmp[kChunkFrames];
            v.synth.Render(patch, mods, tmp, (size_t)chunk);

            float g_l = pan_gain_left(pan);
            float g_r = pan_gain_right(pan);
            float aux_mix = params_.aux_mix;
            for (int j = 0; j < chunk; ++j) {
                float out_main = (float)tmp[j].out / 32768.0f;
                float out_aux = (float)tmp[j].aux / 32768.0f;
                float blended = lerpf(out_main, out_aux, aux_mix);
                float mono = blended * kFixedVoiceMixGain;
                out_l[frame_pos + j] += mono * g_l;
                out_r[frame_pos + j] += mono * g_r;
            }

            if (v.trigger_blocks > 0) {
                v.trigger_blocks--;
            }
        }

        float cutoff = params_.filter_cutoff;
        float resonance = params_.filter_resonance;
        if (cutoff_mod_count > 0) {
            float inv_count = 1.0f / (float)cutoff_mod_count;
            cutoff += cutoff_mod_sum * inv_count;
            resonance += resonance_mod_sum * inv_count;
        }
        cutoff = clampf(cutoff, 0.0f, 1.0f);
        resonance = clampf(resonance, 0.0f, 1.0f);
        FilterCoefficients filter_coeffs = make_filter_coefficients(
            params_.filter_mode,
            cutoff,
            resonance);

        for (int j = 0; j < chunk; ++j) {
            int idx = frame_pos + j;
            out_l[idx] = process_biquad_sample(
                out_l[idx],
                filter_coeffs,
                impl_->filter_state.z1_l,
                impl_->filter_state.z2_l);
            out_r[idx] = process_biquad_sample(
                out_r[idx],
                filter_coeffs,
                impl_->filter_state.z1_r,
                impl_->filter_state.z2_r);
            out_l[idx] = tanhf(out_l[idx]);
            out_r[idx] = tanhf(out_r[idx]);
        }

        impl_->clear_inactive(budget);
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

int ppf_engine_t::debug_release_samples_total_for_note(int note) const {
    int budget = impl_->active_voice_budget(params_);
    int best = -1;
    for (int i = 0; i < budget; ++i) {
        const VoiceState &v = impl_->voices[i];
        if (!v.active || v.note != note) continue;
        if (v.release_samples_total > best) best = v.release_samples_total;
    }
    return best;
}

float ppf_engine_t::debug_pitch_compensation_semitones() const {
    return kPitchCompensationSemitones;
}
#endif
