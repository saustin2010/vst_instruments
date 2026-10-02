/*
 * moog_engine.c - RaffoSynth synthesizer engine
 *
 * Based on RaffoSynth by Nicolas Roulet and Julian Palladino
 * https://github.com/nicoroulet/RaffoSynth
 * MIT License
 *
 * Ported from LV2 to standalone C for Move Anything.
 * Monophonic synthesizer with:
 *   - 4 oscillators (triangle, sawtooth, square, pulse)
 *   - Moog-style ladder filter (low-pass with resonance)
 *   - Amplitude ADSR envelope
 *   - Filter ADSR envelope
 *   - Glide/portamento
 *   - LFO with pitch and filter modulation
 *   - Noise generator
 */

#include "moog_engine.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ===================================================================
 * Utility functions
 * =================================================================== */

/* Convert MIDI note to frequency in Hz */
static inline double note_to_hz(int note) {
    return 8.1758 * pow(1.0594630943593, note);
}

/* Convert frequency to period in samples */
static inline double hz_to_period(double hz, float sample_rate) {
    if (hz < 1.0) return sample_rate;
    return sample_rate / hz;
}

/* Clamp float to range */
static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Map 0.0-1.0 parameter to time in samples (exponential curve) */
static inline double param_to_time(float param, float sample_rate) {
    /* 0.0 -> ~1ms, 1.0 -> ~5s */
    double seconds = 0.001 + param * param * 5.0;
    return seconds * sample_rate;
}

/* Simple white noise generator (LFSR) */
static inline float noise_sample(uint32_t *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return (float)(int32_t)(*seed) / (float)0x7FFFFFFF;
}

/* ===================================================================
 * Oscillator waveform generators
 * Based on oscillators.c from RaffoSynth
 * =================================================================== */

static inline float osc_triangle(double counter, double period) {
    double phase = fmod(counter + period / 4.0, period) / period;
    return 4.0f * (float)(fabs(phase - 0.5) - 0.25);
}

static inline float osc_sawtooth(double counter, double period) {
    return 2.0f * (float)(fmod(counter, period) / period) - 1.0f;
}

static inline float osc_square(double counter, double period) {
    return (fmod(counter, period) / period < 0.5) ? 1.0f : -1.0f;
}

static inline float osc_pulse(double counter, double period) {
    return (fmod(counter, period) / period < 0.2) ? 1.0f : -1.0f;
}

static float generate_osc(moog_wave_t wave, double counter, double period) {
    switch (wave) {
        case WAVE_TRIANGLE: return osc_triangle(counter, period);
        case WAVE_SAWTOOTH: return osc_sawtooth(counter, period);
        case WAVE_SQUARE:   return osc_square(counter, period);
        case WAVE_PULSE:    return osc_pulse(counter, period);
        default:            return 0.0f;
    }
}

/* ===================================================================
 * Moog ladder filter
 * Based on equalizer.c from RaffoSynth, enhanced with proper
 * Moog-style 4-pole ladder resonance
 * =================================================================== */

static void moog_filter_process(float *output, float *prev, int frames,
                                float cutoff_hz, float resonance,
                                float sample_rate) {
    /* Compute filter coefficients for 4-pole Moog ladder filter */
    float fc = cutoff_hz / sample_rate;
    if (fc > 0.49f) fc = 0.49f;
    if (fc < 0.001f) fc = 0.001f;

    float f = fc * 1.16f;
    float fb = resonance * (1.0f - 0.15f * f * f);

    float b0 = prev[0];
    float b1 = prev[1];
    float b2 = prev[2];
    float b3 = prev[3];
    float b4 = prev[4];

    for (int i = 0; i < frames; i++) {
        float input = output[i] - b4 * fb;
        input *= 0.35013f * f * f * f * f;

        b1 = input + 0.3f * b0 + (1.0f - f) * b1;
        b0 = input;
        b2 = b1 + 0.3f * b1 + (1.0f - f) * b2;
        b3 = b2 + 0.3f * b2 + (1.0f - f) * b3;
        b4 = b3 + 0.3f * b3 + (1.0f - f) * b4;

        /* Clamp to prevent blowup */
        if (b4 > 4.0f) b4 = 4.0f;
        if (b4 < -4.0f) b4 = -4.0f;

        output[i] = b4;
    }

    prev[0] = b0;
    prev[1] = b1;
    prev[2] = b2;
    prev[3] = b3;
    prev[4] = b4;
}

/* ===================================================================
 * Envelope generator
 * Based on envelope() from RaffoSynth with quadratic curves
 * =================================================================== */

static float envelope_process(moog_env_state_t *state, float *level,
                              float *attack_level, float *release_level,
                              double *env_counter,
                              float attack, float decay,
                              float sustain, float release,
                              float sample_rate) {
    double atk_time  = param_to_time(attack, sample_rate);
    double dec_time  = param_to_time(decay, sample_rate);
    double rel_time  = param_to_time(release, sample_rate);

    switch (*state) {
        case ENV_ATTACK: {
            double progress = *env_counter / atk_time;
            if (progress >= 1.0) {
                *level = 1.0f;
                *state = ENV_DECAY;
                *env_counter = 0;
            } else {
                /* Quadratic attack curve from current level to 1.0 */
                float start = *attack_level;
                float p = (float)(progress * progress);
                *level = start + (1.0f - start) * p;
            }
            (*env_counter)++;
            break;
        }
        case ENV_DECAY: {
            double progress = *env_counter / dec_time;
            if (progress >= 1.0) {
                *level = sustain;
                *state = ENV_SUSTAIN;
                *env_counter = 0;
            } else {
                /* Quadratic decay curve */
                float p = (float)(1.0 - progress);
                *level = sustain + (1.0f - sustain) * p * p;
            }
            (*env_counter)++;
            break;
        }
        case ENV_SUSTAIN:
            *level = sustain;
            break;
        case ENV_RELEASE: {
            double progress = *env_counter / rel_time;
            if (progress >= 1.0) {
                *level = 0.0f;
                *state = ENV_OFF;
                *env_counter = 0;
            } else {
                /* Quadratic release curve from captured start level */
                float p = (float)(1.0 - progress);
                *level = *release_level * p * p;
            }
            (*env_counter)++;
            break;
        }
        case ENV_OFF:
        default:
            *level = 0.0f;
            break;
    }

    return *level;
}

/* ===================================================================
 * Engine lifecycle
 * =================================================================== */

void moog_engine_init(moog_engine_t *engine) {
    memset(engine, 0, sizeof(moog_engine_t));

    engine->sample_rate = MOOG_SAMPLE_RATE;

    /* Default oscillator settings */
    engine->osc_wave[0] = WAVE_SAWTOOTH;
    engine->osc_wave[1] = WAVE_SAWTOOTH;
    engine->osc_wave[2] = WAVE_SAWTOOTH;
    engine->osc_wave[3] = WAVE_TRIANGLE;
    engine->osc_volume[0] = 0.8f;
    engine->osc_volume[1] = 0.0f;
    engine->osc_volume[2] = 0.0f;
    engine->osc_volume[3] = 0.0f;
    engine->osc_range[0] = 0;
    engine->osc_range[1] = 0;
    engine->osc_range[2] = -1;  /* One octave down */
    engine->osc_range[3] = 0;
    engine->osc2_detune = 0.0f;
    engine->osc3_detune = 0.0f;
    engine->osc4_detune = 0.5f;

    /* Filter - open with moderate resonance */
    engine->filter_cutoff = 0.7f;
    engine->filter_resonance = 0.2f;
    engine->filter_contour = 0.3f;
    engine->filter_key_follow = 0.0f;

    /* Amp envelope */
    engine->amp_attack = 0.01f;
    engine->amp_decay = 0.3f;
    engine->amp_sustain = 0.7f;
    engine->amp_release = 0.2f;

    /* Filter envelope */
    engine->filt_attack = 0.01f;
    engine->filt_decay = 0.3f;
    engine->filt_sustain = 0.3f;
    engine->filt_release = 0.2f;

    /* Other defaults */
    engine->glide = 0.0f;
    engine->master_volume = 0.7f;
    engine->noise_volume = 0.0f;
    engine->mod_to_filter = 0.5f;
    engine->mod_to_pitch = 0.5f;
    engine->bend_range = 0.167f; /* ~2 semitones */
    engine->velocity_sensitivity = 0.5f;
    engine->velocity = 1.0f;

    /* LFO defaults */
    engine->lfo_rate = 0.3f;
    engine->lfo_depth_pitch = 0.0f;
    engine->lfo_depth_filter = 0.0f;

    /* Noise seed */
    engine->noise_seed = 12345;

    /* Initialize period to middle C */
    engine->period = hz_to_period(note_to_hz(60), engine->sample_rate);
    engine->glide_period = engine->period;
}

void moog_engine_reset(moog_engine_t *engine) {
    engine->amp_env_state = ENV_OFF;
    engine->amp_env_level = 0.0f;
    engine->amp_env_counter = 0;
    engine->filt_env_state = ENV_OFF;
    engine->filt_env_level = 0.0f;
    engine->filt_env_counter = 0;
    engine->gate_on = 0;
    engine->current_note = -1;
    engine->key_stack_count = 0;
    engine->counter = 0;
    memset(engine->filter_prev, 0, sizeof(engine->filter_prev));
    memset(engine->last_val, 0, sizeof(engine->last_val));
}

/* ===================================================================
 * MIDI handlers
 * =================================================================== */

void moog_engine_note_on(moog_engine_t *engine, int note, float velocity) {
    /* Add note to key stack */
    if (engine->key_stack_count < MOOG_MAX_KEYS) {
        engine->key_stack[engine->key_stack_count++] = note;
    }

    /* Calculate target period for this note */
    int effective_note = note + engine->octave_transpose * 12;
    if (effective_note < 0) effective_note = 0;
    if (effective_note > 127) effective_note = 127;

    double target_period = hz_to_period(note_to_hz(effective_note), engine->sample_rate);

    if (engine->gate_on && engine->glide > 0.001f) {
        /* Glide to new note */
        engine->glide_period = target_period;
    } else {
        /* Immediate pitch change */
        engine->period = target_period;
        engine->glide_period = target_period;
    }

    engine->current_note = note;
    engine->velocity = velocity;

    if (engine->gate_on) {
        /* Legato: gate already on, just change pitch - don't retrigger envelopes */
    } else {
        /* New note: trigger envelopes from current level (smooth retrigger) */
        engine->gate_on = 1;
        engine->amp_env_attack_level = engine->amp_env_level;
        engine->amp_env_state = ENV_ATTACK;
        engine->amp_env_counter = 0;
        engine->filt_env_attack_level = engine->filt_env_level;
        engine->filt_env_state = ENV_ATTACK;
        engine->filt_env_counter = 0;
    }
}

void moog_engine_note_off(moog_engine_t *engine, int note) {
    /* Remove note from key stack */
    for (int i = 0; i < engine->key_stack_count; i++) {
        if (engine->key_stack[i] == note) {
            /* Shift remaining notes down */
            for (int j = i; j < engine->key_stack_count - 1; j++) {
                engine->key_stack[j] = engine->key_stack[j + 1];
            }
            engine->key_stack_count--;
            break;
        }
    }

    if (engine->key_stack_count > 0) {
        /* Play the most recent remaining note (last note priority) */
        int new_note = engine->key_stack[engine->key_stack_count - 1];
        int effective_note = new_note + engine->octave_transpose * 12;
        if (effective_note < 0) effective_note = 0;
        if (effective_note > 127) effective_note = 127;

        double target_period = hz_to_period(note_to_hz(effective_note), engine->sample_rate);
        engine->current_note = new_note;

        if (engine->glide > 0.001f) {
            engine->glide_period = target_period;
        } else {
            engine->period = target_period;
            engine->glide_period = target_period;
        }
    } else {
        /* No notes held - release */
        engine->gate_on = 0;
        if (engine->amp_env_state != ENV_OFF) {
            engine->amp_env_release_level = engine->amp_env_level;
            engine->amp_env_state = ENV_RELEASE;
            engine->amp_env_counter = 0;
        }
        if (engine->filt_env_state != ENV_OFF) {
            engine->filt_env_release_level = engine->filt_env_level;
            engine->filt_env_state = ENV_RELEASE;
            engine->filt_env_counter = 0;
        }
    }
}

void moog_engine_pitch_bend(moog_engine_t *engine, float bend) {
    engine->pitch_bend = bend;
}

void moog_engine_mod_wheel(moog_engine_t *engine, float amount) {
    engine->mod_wheel = amount;
}

void moog_engine_all_notes_off(moog_engine_t *engine) {
    engine->key_stack_count = 0;
    engine->gate_on = 0;
    engine->amp_env_state = ENV_OFF;
    engine->amp_env_level = 0.0f;
    engine->filt_env_state = ENV_OFF;
    engine->filt_env_level = 0.0f;
    engine->current_note = -1;
}

/* ===================================================================
 * Audio rendering
 * =================================================================== */

void moog_engine_render(moog_engine_t *engine, float *output, int frames) {
    float sr = engine->sample_rate;

    /* Compute pitch bend multiplier */
    float bend_semitones = engine->pitch_bend * engine->bend_range * 12.0f;
    double bend_ratio = pow(2.0, bend_semitones / 12.0);

    /* Compute glide rate */
    double glide_rate = 1.0;
    if (engine->glide > 0.001f) {
        double glide_time = engine->glide * engine->glide * 2.0 * sr;
        glide_rate = 1.0 / (1.0 + glide_time / (double)frames);
    }

    /* LFO */
    float lfo_freq = 0.1f + engine->lfo_rate * engine->lfo_rate * 20.0f; /* 0.1 - 20 Hz */
    float lfo_inc = lfo_freq / sr;

    for (int i = 0; i < frames; i++) {
        /* Update glide */
        if (fabs(engine->period - engine->glide_period) > 0.01) {
            engine->period += (engine->glide_period - engine->period) * glide_rate;
        }

        /* Current period with pitch bend and mod */
        double current_period = engine->period / bend_ratio;

        /* LFO */
        engine->lfo_phase += lfo_inc;
        if (engine->lfo_phase >= 1.0f) engine->lfo_phase -= 1.0f;
        float lfo_val = sinf(engine->lfo_phase * 2.0f * (float)M_PI);

        /* Apply mod wheel modulation to LFO depths */
        float pitch_mod = lfo_val * engine->lfo_depth_pitch * engine->mod_to_pitch * engine->mod_wheel;

        /* Pitch modulation */
        if (fabsf(pitch_mod) > 0.0001f) {
            double mod_ratio = pow(2.0, pitch_mod * 2.0 / 12.0);
            current_period /= mod_ratio;
        }

        /* Process envelopes */
        float amp_env = envelope_process(&engine->amp_env_state, &engine->amp_env_level,
                                         &engine->amp_env_attack_level,
                                         &engine->amp_env_release_level,
                                         &engine->amp_env_counter,
                                         engine->amp_attack, engine->amp_decay,
                                         engine->amp_sustain, engine->amp_release,
                                         sr);

        float filt_env = envelope_process(&engine->filt_env_state, &engine->filt_env_level,
                                          &engine->filt_env_attack_level,
                                          &engine->filt_env_release_level,
                                          &engine->filt_env_counter,
                                          engine->filt_attack, engine->filt_decay,
                                          engine->filt_sustain, engine->filt_release,
                                          sr);

        /* Apply velocity sensitivity */
        float vel_scale = 1.0f - engine->velocity_sensitivity + engine->velocity_sensitivity * engine->velocity;

        /* Generate oscillator samples */
        float sample = 0.0f;

        for (int osc = 0; osc < 4; osc++) {
            if (engine->osc_volume[osc] < 0.001f) continue;

            /* Calculate oscillator period with range offset */
            double osc_period = current_period;
            int range = engine->osc_range[osc];
            if (range != 0) {
                osc_period *= pow(2.0, -(double)range);
            }

            /* Apply detune for osc 2, 3, and 4 */
            if (osc == 1 && fabsf(engine->osc2_detune) > 0.001f) {
                double detune_cents = (engine->osc2_detune - 0.5f) * 100.0; /* -50 to +50 cents */
                osc_period *= pow(2.0, -detune_cents / 1200.0);
            }
            if (osc == 2 && fabsf(engine->osc3_detune) > 0.001f) {
                double detune_cents = (engine->osc3_detune - 0.5f) * 100.0;
                osc_period *= pow(2.0, -detune_cents / 1200.0);
            }
            if (osc == 3 && fabsf(engine->osc4_detune - 0.5f) > 0.001f) {
                double detune_cents = (engine->osc4_detune - 0.5f) * 100.0;
                osc_period *= pow(2.0, -detune_cents / 1200.0);
            }

            if (osc_period < 2.0) osc_period = 2.0;

            float osc_sample = generate_osc(engine->osc_wave[osc], engine->counter, osc_period);
            sample += osc_sample * engine->osc_volume[osc];
        }

        /* Add noise */
        if (engine->noise_volume > 0.001f) {
            sample += noise_sample(&engine->noise_seed) * engine->noise_volume;
        }

        /* Apply amplitude envelope and velocity */
        sample *= amp_env * vel_scale;

        /* Per-sample filter processing for smooth envelope tracking */
        {
            /* Filter cutoff with per-sample envelope modulation */
            float base_cutoff = engine->filter_cutoff;
            float filt_env_mod = filt_env * engine->filter_contour;

            /* Key tracking */
            float key_track = 0.0f;
            if (engine->current_note >= 0) {
                key_track = (engine->current_note - 60) / 127.0f * engine->filter_key_follow;
            }

            /* LFO filter modulation (lfo_val already computed above) */
            float lfo_filt = lfo_val * engine->lfo_depth_filter * engine->mod_to_filter * 0.3f;

            float cutoff_normalized = clampf(base_cutoff + filt_env_mod + key_track + lfo_filt, 0.0f, 1.0f);

            /* Map normalized cutoff to Hz (exponential: 20Hz to 20kHz) */
            float cutoff_hz = 20.0f * powf(1000.0f, cutoff_normalized);

            /* Inline single-sample Moog ladder filter */
            float fc = cutoff_hz / sr;
            if (fc > 0.49f) fc = 0.49f;
            if (fc < 0.001f) fc = 0.001f;

            float f = fc * 1.16f;
            float fb = engine->filter_resonance * (1.0f - 0.15f * f * f);

            float input = sample - engine->filter_prev[4] * fb;
            input *= 0.35013f * f * f * f * f;

            engine->filter_prev[1] = input + 0.3f * engine->filter_prev[0] + (1.0f - f) * engine->filter_prev[1];
            engine->filter_prev[0] = input;
            engine->filter_prev[2] = engine->filter_prev[1] + 0.3f * engine->filter_prev[1] + (1.0f - f) * engine->filter_prev[2];
            engine->filter_prev[3] = engine->filter_prev[2] + 0.3f * engine->filter_prev[2] + (1.0f - f) * engine->filter_prev[3];
            engine->filter_prev[4] = engine->filter_prev[3] + 0.3f * engine->filter_prev[3] + (1.0f - f) * engine->filter_prev[4];

            /* Clamp to prevent blowup */
            if (engine->filter_prev[4] > 4.0f) engine->filter_prev[4] = 4.0f;
            if (engine->filter_prev[4] < -4.0f) engine->filter_prev[4] = -4.0f;

            sample = engine->filter_prev[4];
        }

        output[i] = sample * engine->master_volume;

        engine->counter += 1.0;
    }
}
