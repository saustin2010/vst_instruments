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
#define PPF_NUM_LFOS 2
#define PPF_NUM_ENVS 2
#define PPF_GRID 4

/* Modulation sources, in the order of module.json's ASSIGN source list. */
enum {
    PPF_SRC_OFF = 0,
    PPF_SRC_LFO1,
    PPF_SRC_LFO2,
    PPF_SRC_ENV1,
    PPF_SRC_ENV2,
    PPF_SRC_CYCLE,
    PPF_SRC_RANDOM,
    PPF_SRC_VELOCITY,
    PPF_SRC_AFTERTOUCH,
    PPF_SRC_MOD_WHEEL,
    PPF_SRC_COUNT
};

/* Modulation destinations, in the order of module.json's ASSIGN destination list. */
enum {
    PPF_DST_OFF = 0,
    PPF_DST_PITCH,
    PPF_DST_HARMONICS,
    PPF_DST_TIMBRE,
    PPF_DST_MORPH,
    PPF_DST_FM,
    PPF_DST_LPG_DECAY,
    PPF_DST_LPG_COLOR,
    PPF_DST_CUTOFF,
    PPF_DST_RESONANCE,
    PPF_DST_VOLUME,
    PPF_DST_PAN,
    PPF_DST_AUX_MIX,
    PPF_DST_COUNT
};

/* The MOD page's fixed grid: rows are these sources, columns these destinations. */
static const int kPpfFixedSources[PPF_GRID] = {PPF_SRC_LFO1, PPF_SRC_ENV2, PPF_SRC_CYCLE, PPF_SRC_RANDOM};
static const int kPpfFixedDests[PPF_GRID] = {PPF_DST_PITCH, PPF_DST_HARMONICS, PPF_DST_TIMBRE, PPF_DST_MORPH};

enum { PPF_AMP_GATE = 0, PPF_AMP_PING, PPF_AMP_ENV, PPF_AMP_DRONE };

static inline float ppf_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
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
    int shape;
    float rate;        /* Hz, when not synced */
    int sync;
    int div;           /* index into the sync divisions (module.json lfoN_div) */
    int retrig;
    int per_voice;
    float phase;
} ppf_lfo_params_t;

typedef struct {
    int attack_ms;
    int decay_ms;
    float sustain;
    int release_ms;
    int retrig;
} ppf_env_params_t;

typedef struct {
    /* PLAITS page */
    int model;
    float pitch;
    float harmonics;
    float timbre;
    float morph;
    float timbre_att;  /* the module's attenuverters: internal decay envelope -> TIMBRE / FREQUENCY / MORPH */
    float fm_att;
    float morph_att;
    float aux_mix;

    /* VOICE page */
    int amp_mode;
    float lpg_color;
    float lpg_decay;
    int trig_rate;     /* 0 = off, else index into the retrigger divisions */
    int voice_mode;
    int polyphony;
    int unison;
    float detune;
    float spread;
    int glide_ms;
    int bend_range;
    float velocity_curve;
    float poly_aftertouch_curve;
    int filter_mode;
    float filter_cutoff;
    float filter_resonance;
    float volume;
    float pan;

    /* ENV / LFO pages */
    ppf_env_params_t env[PPF_NUM_ENVS];
    ppf_lfo_params_t lfo[PPF_NUM_LFOS];
    int cycle_attack_ms;
    int cycle_decay_ms;
    int cycle_shape;
    int cycle_bipolar;
    int cycle_retrig;
    int random_mode;
    float random_rate;
    int random_sync;
    int random_div;
    float random_slew;
    int random_retrig;

    /* MOD page (fixed sources x fixed destinations) and ASSIGN page (chosen sources x chosen destinations) */
    float mod_fixed[PPF_GRID][PPF_GRID];
    int mod_on[PPF_GRID][PPF_GRID];      /* each cell's on/off switch: off mutes it without moving its knob */
    int asg_src[PPF_GRID];
    int asg_dst[PPF_GRID];
    float asg_amt[PPF_GRID][PPF_GRID];
    int asg_on[PPF_GRID][PPF_GRID];
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
    void channel_pressure(float pressure);
    void pitch_bend(float amount);     /* -1..1, scaled by bend_range */
    void mod_wheel(float amount);      /* 0..1 */
    void set_tempo(float bpm);
    void transport_start();
    void all_notes_off();

    /* How many notes can sound at once: NOTES, capped by UNISON (8 voices in all). */
    int effective_notes() const;

    void render(float *out_l, float *out_r, int frames);

#ifdef TEST
    int debug_active_voice_count() const;
    int debug_active_note_count(int note) const;
    int debug_voice_active_engine(int voice_index) const;
    float debug_voice_note_target(int voice_index) const;
    float debug_voice_pan(int voice_index) const;
    float debug_pitch_compensation_semitones() const;
    float debug_lfo_phase(int lfo) const;
#endif

private:
    struct Impl;
    Impl *impl_;
    ppf_params_t params_;
};
#endif

#endif
