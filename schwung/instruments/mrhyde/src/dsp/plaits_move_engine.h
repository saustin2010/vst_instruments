#ifndef MOVE_EVERYTHING_PLAITS_MOVE_ENGINE_H
#define MOVE_EVERYTHING_PLAITS_MOVE_ENGINE_H

#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PPF_SAMPLE_RATE 44100
#define PPF_MAX_RENDER 256
#define PPF_MAX_VOICES 8

typedef struct {
    float lfo;
    float env;
    float cycle_env;
    float random;
    float velocity;
    float poly_aftertouch;
} ppf_mod_sources_t;

typedef struct {
    float lfo;
    float env;
    float cycle_env;
    float random;
    float velocity;
    float poly_aftertouch;
} ppf_mod_amounts_t;

static inline float ppf_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float ppf_apply_destination_modulation(float base_value,
                                                     const ppf_mod_sources_t sources,
                                                     const ppf_mod_amounts_t amounts,
                                                     float min_value,
                                                     float max_value) {
    float v = base_value;
    v += sources.lfo * amounts.lfo;
    v += sources.env * amounts.env;
    v += sources.cycle_env * amounts.cycle_env;
    v += sources.random * amounts.random;
    v += sources.velocity * amounts.velocity;
    v += sources.poly_aftertouch * amounts.poly_aftertouch;
    return ppf_clampf(v, min_value, max_value);
}

static inline float ppf_modulation_sum(const ppf_mod_sources_t sources,
                                       const ppf_mod_amounts_t amounts) {
    float v = 0.0f;
    v += sources.lfo * amounts.lfo;
    v += sources.env * amounts.env;
    v += sources.cycle_env * amounts.cycle_env;
    v += sources.random * amounts.random;
    v += sources.velocity * amounts.velocity;
    v += sources.poly_aftertouch * amounts.poly_aftertouch;
    return v;
}

static inline float ppf_apply_bipolar_curve(float x, float curve) {
    x = ppf_clampf(x, 0.0f, 1.0f);
    float c = ppf_clampf(curve, -1.0f, 1.0f);
    if (c < 0.0f) {
        float exp_amount = 1.0f + (-c) * 3.0f;
        return powf(x, exp_amount);
    }
    if (c > 0.0f) {
        float exp_amount = 1.0f + c * 3.0f;
        return 1.0f - powf(1.0f - x, exp_amount);
    }
    return x;
}

typedef struct {
    int model;
    float pitch;
    float harmonics;
    float timbre;
    float morph;
    float fm_amount;
    float aux_mix;
    float volume;
    float pan;
    int filter_mode;
    float filter_cutoff;
    float filter_resonance;

    float lpg_decay;
    float lpg_color;

    ppf_mod_amounts_t pitch_mod;
    ppf_mod_amounts_t harmonics_mod;
    ppf_mod_amounts_t timbre_mod;
    ppf_mod_amounts_t cutoff_mod;
    int assign1_target;
    ppf_mod_amounts_t assign1_mod;
    int assign2_target;
    ppf_mod_amounts_t assign2_mod;

    int lfo_shape;
    float lfo_rate;
    int lfo_sync;
    int lfo_retrig;
    float lfo_phase;

    int env_attack_ms;
    int env_decay_ms;
    float env_sustain;
    int env_release_ms;
    int env_retrig;

    int cycle_attack_ms;
    int cycle_decay_ms;
    int cycle_shape;
    int cycle_sync;
    int cycle_retrig;
    int cycle_bipolar;

    int random_mode;
    float random_rate;
    int random_sync;
    float random_slew;
    int random_retrig;

    float velocity_curve;
    float poly_aftertouch_curve;

    int voice_mode;
    int polyphony;
    int unison;
    float detune;
    float spread;
    int glide_ms;

} ppf_params_t;

void ppf_default_params(ppf_params_t *params);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class ppf_engine_t {
public:
    ppf_engine_t();
    ~ppf_engine_t();

    void init();
    void set_params(const ppf_params_t &params);
    const ppf_params_t &params() const { return params_; }

    void note_on(int note, float velocity);
    void note_off(int note);
    void poly_aftertouch(int note, float pressure);
    void all_notes_off();

    void render(float *out_l, float *out_r, int frames);

#ifdef TEST
    int debug_active_voice_count() const;
    int debug_active_note_count(int note) const;
    int debug_voice_active_engine(int voice_index) const;
    float debug_voice_note_target(int voice_index) const;
    float debug_voice_pan(int voice_index) const;
    int debug_release_samples_total_for_note(int note) const;
    float debug_pitch_compensation_semitones() const;
#endif

private:
    struct Impl;
    Impl *impl_;
    ppf_params_t params_;
};
#endif

#endif
