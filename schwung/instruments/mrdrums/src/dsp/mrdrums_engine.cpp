#include "mrdrums_engine.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

static int is_valid_pad(int pad_index) {
    return pad_index >= 1 && pad_index <= MRDRUMS_ENGINE_PAD_COUNT;
}

static mrdrums_pad_t *get_pad_mut(mrdrums_engine_t *engine, int pad_index) {
    if (!engine || !is_valid_pad(pad_index)) return NULL;
    return &engine->pads[pad_index - 1];
}

static const mrdrums_pad_t *get_pad(const mrdrums_engine_t *engine, int pad_index) {
    if (!engine || !is_valid_pad(pad_index)) return NULL;
    return &engine->pads[pad_index - 1];
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float rand01(uint32_t *state) {
    return (float)(xorshift32(state) & 0x00FFFFFFu) / 16777216.0f;
}

void mrdrums_engine_init(mrdrums_engine_t *engine) {
    if (!engine) return;
    memset(engine, 0, sizeof(*engine));

    engine->sample_rate = MRDRUMS_ENGINE_SAMPLE_RATE;
    engine->polyphony = 16;
    engine->voice_age_counter = 1;

    engine->master_vol = 1.0f;
    engine->vel_curve = 0;
    engine->humanize_ms = 0.0f;
    engine->rand_seed = 1u;
    engine->rng_state = engine->rand_seed;
    engine->rand_loop_steps = 16;

    for (int i = 0; i < MRDRUMS_ENGINE_PAD_COUNT; i++) {
        mrdrums_pad_t *p = &engine->pads[i];
        p->sample_data = NULL;
        p->sample_len = 0;
        p->sample_rate = MRDRUMS_ENGINE_SAMPLE_RATE;
        p->vol = 1.0f;
        p->pan = 0.0f;
        p->tune = 0.0f;
        p->start = 0.0f;
        p->attack_ms = 0.0f;
        p->decay_ms = 250.0f;
        p->choke_group = 0;
        p->mode = MRDRUMS_PAD_MODE_ONESHOT;
        p->rand_pan_amt = 0.0f;
        p->rand_vol_amt = 0.0f;
        p->rand_decay_amt = 0.0f;
        p->chance_pct = 100.0f;
    }
}

int mrdrums_engine_note_to_pad(int note) {
    if (note < MRDRUMS_ENGINE_PAD_NOTE_MIN || note > MRDRUMS_ENGINE_PAD_NOTE_MAX) {
        return 0;
    }
    return note - MRDRUMS_ENGINE_PAD_NOTE_MIN + 1;
}

void mrdrums_engine_set_polyphony(mrdrums_engine_t *engine, int polyphony) {
    if (!engine) return;
    engine->polyphony = clampi(polyphony, 1, MRDRUMS_ENGINE_MAX_VOICES);
}

void mrdrums_engine_set_master_vol(mrdrums_engine_t *engine, float master_vol) {
    if (!engine) return;
    engine->master_vol = clampf(master_vol, 0.0f, 2.0f);
}

void mrdrums_engine_set_pad_sample(mrdrums_engine_t *engine,
                                   int pad_index,
                                   const float *sample_data,
                                   int sample_len,
                                   int sample_rate) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->sample_data = sample_data;
    pad->sample_len = sample_len > 0 ? sample_len : 0;
    pad->sample_rate = sample_rate > 1000 ? sample_rate : MRDRUMS_ENGINE_SAMPLE_RATE;
}

void mrdrums_engine_set_pad_mode(mrdrums_engine_t *engine, int pad_index, int mode) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->mode = (mode == MRDRUMS_PAD_MODE_GATE) ? MRDRUMS_PAD_MODE_GATE : MRDRUMS_PAD_MODE_ONESHOT;
}

void mrdrums_engine_set_pad_choke_group(mrdrums_engine_t *engine, int pad_index, int choke_group) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->choke_group = clampi(choke_group, 0, MRDRUMS_ENGINE_PAD_COUNT);
}

void mrdrums_engine_set_pad_decay_ms(mrdrums_engine_t *engine, int pad_index, float decay_ms) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->decay_ms = clampf(decay_ms, 0.0f, 5000.0f);
}

void mrdrums_engine_set_pad_attack_ms(mrdrums_engine_t *engine, int pad_index, float attack_ms) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->attack_ms = clampf(attack_ms, 0.0f, 5000.0f);
}

void mrdrums_engine_set_pad_vol(mrdrums_engine_t *engine, int pad_index, float vol) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->vol = clampf(vol, 0.0f, 2.0f);
}

void mrdrums_engine_set_pad_pan(mrdrums_engine_t *engine, int pad_index, float pan) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->pan = clampf(pan, -1.0f, 1.0f);
}

void mrdrums_engine_set_pad_tune(mrdrums_engine_t *engine, int pad_index, float tune) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->tune = clampf(tune, -24.0f, 24.0f);
}

void mrdrums_engine_set_pad_start(mrdrums_engine_t *engine, int pad_index, float start) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->start = clampf(start, 0.0f, 1.0f);
}

void mrdrums_engine_set_pad_chance_pct(mrdrums_engine_t *engine, int pad_index, float chance_pct) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->chance_pct = clampf(chance_pct, 0.0f, 100.0f);
}

void mrdrums_engine_set_pad_rand_pan_amt(mrdrums_engine_t *engine, int pad_index, float amt) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->rand_pan_amt = clampf(amt, 0.0f, 1.0f);
}

void mrdrums_engine_set_pad_rand_vol_amt(mrdrums_engine_t *engine, int pad_index, float amt) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->rand_vol_amt = clampf(amt, 0.0f, 1.0f);
}

void mrdrums_engine_set_pad_rand_decay_amt(mrdrums_engine_t *engine, int pad_index, float amt) {
    mrdrums_pad_t *pad = get_pad_mut(engine, pad_index);
    if (!pad) return;
    pad->rand_decay_amt = clampf(amt, 0.0f, 1.0f);
}

static void kill_choke_group(mrdrums_engine_t *engine, int choke_group) {
    if (!engine || choke_group <= 0) return;
    for (int i = 0; i < MRDRUMS_ENGINE_MAX_VOICES; i++) {
        mrdrums_voice_t *v = &engine->voices[i];
        if (v->active && v->choke_group == choke_group) {
            v->active = 0;
        }
    }
}

static int find_voice_slot(mrdrums_engine_t *engine) {
    int poly = clampi(engine->polyphony, 1, MRDRUMS_ENGINE_MAX_VOICES);

    for (int i = 0; i < poly; i++) {
        if (!engine->voices[i].active) {
            return i;
        }
    }

    int oldest_idx = 0;
    uint32_t oldest_age = engine->voices[0].age;
    for (int i = 1; i < poly; i++) {
        if (engine->voices[i].age < oldest_age) {
            oldest_age = engine->voices[i].age;
            oldest_idx = i;
        }
    }
    return oldest_idx;
}

static float apply_velocity_curve(float vel, int vel_curve) {
    vel = clampf(vel, 0.0f, 1.0f);
    switch (vel_curve) {
        case 1:  /* soft */
            return sqrtf(vel);
        case 2:  /* hard */
            return vel * vel;
        default: /* linear */
            return vel;
    }
}

void mrdrums_engine_note_on(mrdrums_engine_t *engine, int note, int velocity) {
    if (!engine) return;

    int pad_index = mrdrums_engine_note_to_pad(note);
    if (pad_index <= 0) return;

    const mrdrums_pad_t *pad = get_pad(engine, pad_index);
    if (!pad || !pad->sample_data || pad->sample_len < 2) return;

    uint32_t rng = engine->rng_state ? engine->rng_state : (engine->rand_seed ? engine->rand_seed : 1u);
    float chance_roll = rand01(&rng) * 100.0f;
    if (chance_roll > clampf(pad->chance_pct, 0.0f, 100.0f)) return;

    kill_choke_group(engine, pad->choke_group);

    int slot = find_voice_slot(engine);
    mrdrums_voice_t *v = &engine->voices[slot];
    memset(v, 0, sizeof(*v));

    v->active = 1;
    v->age = engine->voice_age_counter++;
    v->pad_index = pad_index;
    v->note = note;
    v->mode = pad->mode;
    v->choke_group = pad->choke_group;
    v->gate = 1;
    v->sample_data = pad->sample_data;
    v->sample_len = pad->sample_len;

    float start = clampf(pad->start, 0.0f, 1.0f);
    v->sample_pos = start * (float)(pad->sample_len - 1);

    float pitch_ratio = powf(2.0f, pad->tune / 12.0f);
    float src_to_out = (float)pad->sample_rate / (float)engine->sample_rate;
    v->sample_inc = clampf(src_to_out * pitch_ratio, 0.05f, 8.0f);

    float vel = apply_velocity_curve((float)velocity / 127.0f, engine->vel_curve);
    float gain = vel * clampf(pad->vol, 0.0f, 2.0f) * engine->master_vol;

    float pan = clampf(pad->pan, -1.0f, 1.0f);
    if (pad->rand_pan_amt > 0.0f) {
        float rand_pan = rand01(&rng) * 2.0f - 1.0f;
        pan = clampf(pan + rand_pan * pad->rand_pan_amt, -1.0f, 1.0f);
    }

    if (pad->rand_vol_amt > 0.0f) {
        float rand_vol = rand01(&rng) * 2.0f - 1.0f;
        gain *= clampf(1.0f + rand_vol * pad->rand_vol_amt, 0.0f, 2.0f);
    }

    float release_ms = clampf(pad->decay_ms, 0.0f, 5000.0f);
    if (pad->rand_decay_amt > 0.0f) {
        float rand_decay = rand01(&rng) * 2.0f - 1.0f;
        release_ms = clampf(release_ms * (1.0f + rand_decay * pad->rand_decay_amt), 0.0f, 5000.0f);
    }

    float theta = (pan + 1.0f) * 0.25f * (float)M_PI;
    v->gain_l = cosf(theta) * gain;
    v->gain_r = sinf(theta) * gain;

    if (pad->attack_ms <= 0.0f) {
        v->env = 1.0f;
        v->attack_inc = 0.0f;
    } else {
        v->env = 0.0f;
        float attack_samples = pad->attack_ms * 0.001f * (float)engine->sample_rate;
        if (attack_samples < 1.0f) attack_samples = 1.0f;
        v->attack_inc = 1.0f / attack_samples;
    }

    if (release_ms <= 0.0f) {
        v->release_inc = 1.0f;
    } else {
        float release_samples = release_ms * 0.001f * (float)engine->sample_rate;
        if (release_samples < 1.0f) release_samples = 1.0f;
        v->release_inc = 1.0f / release_samples;
    }

    v->start_delay_samples = 0;
    if (engine->humanize_ms > 0.0f) {
        float max_delay = engine->humanize_ms * 0.001f * (float)engine->sample_rate;
        if (max_delay > 0.0f) {
            int max_delay_samples = (int)max_delay;
            int delay = (int)(rand01(&rng) * (float)(max_delay_samples + 1));
            v->start_delay_samples = clampi(delay, 0, max_delay_samples);
        }
    }

    engine->rng_state = rng;
}

void mrdrums_engine_note_off(mrdrums_engine_t *engine, int note) {
    if (!engine) return;
    for (int i = 0; i < MRDRUMS_ENGINE_MAX_VOICES; i++) {
        mrdrums_voice_t *v = &engine->voices[i];
        if (!v->active || v->note != note) continue;
        if (v->mode == MRDRUMS_PAD_MODE_GATE) {
            v->gate = 0;
            v->releasing = 1;
        }
    }
}

void mrdrums_engine_all_notes_off(mrdrums_engine_t *engine) {
    if (!engine) return;
    for (int i = 0; i < MRDRUMS_ENGINE_MAX_VOICES; i++) {
        engine->voices[i].active = 0;
    }
}

int mrdrums_engine_active_voice_count(const mrdrums_engine_t *engine) {
    if (!engine) return 0;
    int count = 0;
    for (int i = 0; i < MRDRUMS_ENGINE_MAX_VOICES; i++) {
        if (engine->voices[i].active) count++;
    }
    return count;
}

int mrdrums_engine_active_pad_voice_count(const mrdrums_engine_t *engine, int pad_index) {
    if (!engine || !is_valid_pad(pad_index)) return 0;
    int count = 0;
    for (int i = 0; i < MRDRUMS_ENGINE_MAX_VOICES; i++) {
        if (!engine->voices[i].active) continue;
        if (engine->voices[i].pad_index == pad_index) count++;
    }
    return count;
}

void mrdrums_engine_render(mrdrums_engine_t *engine, float *out_left, float *out_right, int frames) {
    if (!engine || !out_left || !out_right || frames <= 0) return;

    for (int i = 0; i < frames; i++) {
        out_left[i] = 0.0f;
        out_right[i] = 0.0f;
    }

    for (int vi = 0; vi < MRDRUMS_ENGINE_MAX_VOICES; vi++) {
        mrdrums_voice_t *v = &engine->voices[vi];
        if (!v->active || !v->sample_data || v->sample_len < 2) continue;

        for (int i = 0; i < frames; i++) {
            if (!v->active) break;

            if (v->start_delay_samples > 0) {
                v->start_delay_samples--;
                continue;
            }

            if (v->sample_pos >= (float)(v->sample_len - 1)) {
                v->active = 0;
                break;
            }

            if (v->env < 1.0f && v->attack_inc > 0.0f && !v->releasing) {
                v->env += v->attack_inc;
                if (v->env > 1.0f) v->env = 1.0f;
            }

            /* Oneshot uses decay as an automatic release envelope after attack. */
            if (v->mode == MRDRUMS_PAD_MODE_ONESHOT && !v->releasing && v->env >= 1.0f) {
                v->releasing = 1;
            }

            if (v->releasing) {
                v->env -= v->release_inc;
                if (v->env <= 0.0f) {
                    v->active = 0;
                    break;
                }
            }

            int idx = (int)v->sample_pos;
            if (idx < 0) idx = 0;
            if (idx >= v->sample_len - 1) idx = v->sample_len - 2;

            float frac = v->sample_pos - (float)idx;
            float s0 = v->sample_data[idx];
            float s1 = v->sample_data[idx + 1];
            float sample = s0 + (s1 - s0) * frac;

            float value = sample * v->env;
            out_left[i] += value * v->gain_l;
            out_right[i] += value * v->gain_r;

            v->sample_pos += v->sample_inc;
            if (v->mode == MRDRUMS_PAD_MODE_ONESHOT && v->sample_pos >= (float)(v->sample_len - 1)) {
                v->active = 0;
                break;
            }
        }
    }
}
