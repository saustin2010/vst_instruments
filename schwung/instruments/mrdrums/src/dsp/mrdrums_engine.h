#ifndef MRDRUMS_ENGINE_H
#define MRDRUMS_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MRDRUMS_ENGINE_SAMPLE_RATE = 44100,
    MRDRUMS_ENGINE_MAX_VOICES = 64,
    MRDRUMS_ENGINE_PAD_COUNT = 16,
    MRDRUMS_ENGINE_PAD_NOTE_MIN = 36,
    MRDRUMS_ENGINE_PAD_NOTE_MAX = 51
};

typedef enum {
    MRDRUMS_PAD_MODE_GATE = 0,
    MRDRUMS_PAD_MODE_ONESHOT = 1
} mrdrums_pad_mode_t;

typedef struct {
    const float *sample_data;
    int sample_len;
    int sample_rate;

    float vol;
    float pan;
    float tune;
    float start;
    float attack_ms;
    float decay_ms;
    int choke_group;
    int mode;

    float rand_pan_amt;
    float rand_vol_amt;
    float rand_decay_amt;
    float chance_pct;
} mrdrums_pad_t;

typedef struct {
    int active;
    uint32_t age;
    int pad_index;
    int note;
    int mode;
    int choke_group;
    int gate;
    int releasing;

    const float *sample_data;
    int sample_len;
    float sample_pos;
    float sample_inc;

    float env;
    float attack_inc;
    float release_inc;
    float gain_l;
    float gain_r;
    int start_delay_samples;
} mrdrums_voice_t;

typedef struct {
    int sample_rate;
    int polyphony;
    uint32_t voice_age_counter;

    float master_vol;
    int vel_curve;
    float humanize_ms;
    uint32_t rand_seed;
    uint32_t rng_state;
    int rand_loop_steps;

    mrdrums_pad_t pads[MRDRUMS_ENGINE_PAD_COUNT];
    mrdrums_voice_t voices[MRDRUMS_ENGINE_MAX_VOICES];
} mrdrums_engine_t;

void mrdrums_engine_init(mrdrums_engine_t *engine);

int mrdrums_engine_note_to_pad(int note);

void mrdrums_engine_set_polyphony(mrdrums_engine_t *engine, int polyphony);
void mrdrums_engine_set_master_vol(mrdrums_engine_t *engine, float master_vol);

void mrdrums_engine_set_pad_sample(mrdrums_engine_t *engine,
                                   int pad_index,
                                   const float *sample_data,
                                   int sample_len,
                                   int sample_rate);
void mrdrums_engine_set_pad_mode(mrdrums_engine_t *engine, int pad_index, int mode);
void mrdrums_engine_set_pad_choke_group(mrdrums_engine_t *engine, int pad_index, int choke_group);
void mrdrums_engine_set_pad_decay_ms(mrdrums_engine_t *engine, int pad_index, float decay_ms);
void mrdrums_engine_set_pad_attack_ms(mrdrums_engine_t *engine, int pad_index, float attack_ms);
void mrdrums_engine_set_pad_vol(mrdrums_engine_t *engine, int pad_index, float vol);
void mrdrums_engine_set_pad_pan(mrdrums_engine_t *engine, int pad_index, float pan);
void mrdrums_engine_set_pad_tune(mrdrums_engine_t *engine, int pad_index, float tune);
void mrdrums_engine_set_pad_start(mrdrums_engine_t *engine, int pad_index, float start);
void mrdrums_engine_set_pad_chance_pct(mrdrums_engine_t *engine, int pad_index, float chance_pct);
void mrdrums_engine_set_pad_rand_pan_amt(mrdrums_engine_t *engine, int pad_index, float amt);
void mrdrums_engine_set_pad_rand_vol_amt(mrdrums_engine_t *engine, int pad_index, float amt);
void mrdrums_engine_set_pad_rand_decay_amt(mrdrums_engine_t *engine, int pad_index, float amt);

void mrdrums_engine_note_on(mrdrums_engine_t *engine, int note, int velocity);
void mrdrums_engine_note_off(mrdrums_engine_t *engine, int note);
void mrdrums_engine_all_notes_off(mrdrums_engine_t *engine);

int mrdrums_engine_active_voice_count(const mrdrums_engine_t *engine);
int mrdrums_engine_active_pad_voice_count(const mrdrums_engine_t *engine, int pad_index);

void mrdrums_engine_render(mrdrums_engine_t *engine, float *out_left, float *out_right, int frames);

#ifdef __cplusplus
}
#endif

#endif
