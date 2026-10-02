/*
 * nusaw_engine.cpp - NuSaw polyphonic synthesizer engine
 *
 * Detuned multi-voice sawtooth (7 voices per note) with:
 *   - PolyBLEP anti-aliased saw generation
 *   - Exponential detune spacing (1:3:6 ratio) for dense chorused core
 *   - Piecewise-linear detune curve for fine resolution at low values
 *   - Center-anchored mix law (center ~1.5x sides at full spread)
 *   - Non-linear spread curve (spread^1.5) with minimum floor
 *   - RMS-based gain normalization for consistent loudness
 *   - Random phase initialization on each note-on
 *   - Analog pitch drift (slow random walk per oscillator)
 *   - Stereo panning of detuned pairs (constant-power pan law)
 *   - Sine sub oscillator with configurable octave offset (-2, -1, 0)
 *   - 1-pole DC-blocking HPF after oscillator mix (stereo)
 *   - TPT/SVF resonant lowpass filter (stereo)
 *   - ADSR amp and filter envelopes
 *   - 8-voice polyphony with oldest-note stealing
 *   - One-pole parameter smoothing for detune/spread
 */

#include "nusaw_engine.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =====================================================================
 * Constants
 * ===================================================================== */

/* Maximum fractional detune (10% of base frequency at detune=1.0)
 * This is applied to the outermost pair; inner pairs are closer per spacing law */
#define DETUNE_K_MAX 0.10f

/* DC-blocking HPF cutoff ~20Hz: R = 1 - 2*pi*fc/fs */
#define HPF_R 0.99715f  /* 1 - 2*pi*20/44100 */

/* Parameter smoothing coefficient (~5ms at 44100Hz)
 * coeff = 1 - exp(-1/(0.005 * 44100)) ~ 0.00452 */
#define SMOOTH_COEFF 0.00452f

/* Side voice gain scaling: at spread=1.0, each side voice is at 0.667
 * so the center (1.0) is ~1.5x any individual side voice */
#define SIDE_GAIN_SCALE 0.667f

/* Minimum side voice presence (~1.5% floor at spread=0)
 * Ensures detuned voices never completely vanish */
#define SIDE_GAIN_FLOOR 0.015f

/* Analog pitch drift: slow random walk per oscillator
 * DRIFT_AMOUNT ~0.35 cents (0.02% of frequency)
 * DRIFT_COEFF for ~8Hz lowpass: 2*pi*8/44100 ~ 0.00114 */
#define DRIFT_AMOUNT 0.0002f
#define DRIFT_COEFF  0.00114f

/* Detune and pan coefficients are now computed dynamically in
 * nsaw_engine_update_osc_config() and stored in the engine struct.
 * Voice layout: [center, +c1, -c1, +c2, -c2, ..., +cM, -cM] */

/* =====================================================================
 * Helpers
 * ===================================================================== */

/* xorshift32 PRNG -- fast, good enough for phase randomization and drift */
static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Random float in [0, 1) from PRNG */
static inline float rand_float(uint32_t *state) {
    return (float)(xorshift32(state) & 0x7FFFFF) / (float)0x800000;
}

/* Convert 0.0-1.0 parameter to time in seconds (1ms to 10s, exponential) */
static inline float param_to_seconds(float p) {
    if (p < 0.001f) return 0.001f;
    return 0.001f * powf(10000.0f, p);
}

/* Convert MIDI note to frequency (A4 = 440Hz) */
static inline float note_to_freq(int note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

/* PolyBLEP residual for anti-aliased sawtooth */
static inline float polyblep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* Piecewise-linear detune curve: maps [0,1] -> [0,1]
 * Three segments for musical response:
 *   [0.0, 0.1] -> [0.0, 0.02]   gentle -- subtle thickening
 *   [0.1, 0.5] -> [0.02, 0.25]  moderate -- pads, leads, stabs
 *   [0.5, 1.0] -> [0.25, 1.0]   steep -- dramatic wide detuning */
static inline float detune_curve(float x) {
    if (x < 0.1f) {
        /* Segment 1: slope = 0.02 / 0.1 = 0.2 */
        return x * 0.2f;
    } else if (x < 0.5f) {
        /* Segment 2: slope = (0.25 - 0.02) / (0.5 - 0.1) = 0.575 */
        return 0.02f + (x - 0.1f) * 0.575f;
    } else {
        /* Segment 3: slope = (1.0 - 0.25) / (0.5) = 1.5 */
        return 0.25f + (x - 0.5f) * 1.5f;
    }
}

/* =====================================================================
 * Oscillator configuration (runtime)
 * ===================================================================== */

void nsaw_engine_update_osc_config(nsaw_engine_t *engine, int num_oscs) {
    if (num_oscs < 3) num_oscs = 3;
    if (num_oscs > NSAW_MAX_OSC_VOICES) num_oscs = NSAW_MAX_OSC_VOICES;
    num_oscs |= 1;  /* ensure odd */

    engine->num_oscs = num_oscs;
    engine->num_pairs = (num_oscs - 1) / 2;
    int M = engine->num_pairs;

    /* Detune coefficients: triangular number spacing
     * coeff_k = k*(k+1) / (M*(M+1)), normalized to outermost=1.0
     * This generalizes the existing 1:3:6 ratio for M=3 */
    engine->detune_coeff[0] = 0.0f;  /* center */
    float denom = (float)(M * (M + 1));
    for (int k = 1; k <= M; k++) {
        float c = (float)(k * (k + 1)) / denom;
        engine->detune_coeff[2*k - 1] =  c;  /* +side */
        engine->detune_coeff[2*k]     = -c;  /* -side */
    }

    /* Pan positions: linear spread from center, capped at +/-0.55
     * pan_k = k/M * 0.55 */
    engine->pan_l[0] = 0.7071f;
    engine->pan_r[0] = 0.7071f;
    for (int k = 1; k <= M; k++) {
        float pan = (float)k / (float)M * 0.55f;
        float theta_pos = (1.0f + pan) * 0.5f * (float)M_PI * 0.5f;
        float theta_neg = (1.0f - pan) * 0.5f * (float)M_PI * 0.5f;
        /* +side voice */
        engine->pan_l[2*k - 1] = cosf(theta_pos);
        engine->pan_r[2*k - 1] = sinf(theta_pos);
        /* -side voice */
        engine->pan_l[2*k] = cosf(theta_neg);
        engine->pan_r[2*k] = sinf(theta_neg);
    }
}

/* =====================================================================
 * Engine init
 * ===================================================================== */

void nsaw_engine_init(nsaw_engine_t *engine) {
    memset(engine, 0, sizeof(nsaw_engine_t));
    engine->sample_rate = NSAW_SAMPLE_RATE;
    engine->voice_counter = 0;

    /* Seed PRNG (non-zero) */
    engine->rng_state = 0xDEADBEEF;

    /* Default parameter values */
    engine->cutoff = 0.7f;
    engine->resonance = 0.0f;
    engine->detune = 0.3f;
    engine->spread = 0.7f;
    engine->f_amount = 0.5f;
    engine->attack = 0.01f;
    engine->decay = 0.3f;
    engine->sustain = 0.7f;
    engine->release = 0.2f;
    engine->f_attack = 0.01f;
    engine->f_decay = 0.3f;
    engine->f_sustain = 0.3f;
    engine->f_release = 0.2f;
    engine->volume = 0.7f;
    engine->vel_sens = 0.5f;
    engine->bend_range = 0.167f;  /* ~2 semitones */
    engine->sub_level = 0.0f;
    engine->sub_octave = -1;
    engine->octave_transpose = 0;
    engine->current_bend = 0.0f;

    /* Initialize oscillator configuration */
    nsaw_engine_update_osc_config(engine, NSAW_DEFAULT_OSC_VOICES);

    /* Initialize smoothed params to match targets */
    engine->smooth_detune = engine->detune;
    engine->smooth_spread = engine->spread;
    engine->smooth_cutoff = engine->cutoff;

    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        engine->voices[i].active = 0;
        engine->voices[i].amp_env.stage = NSAW_ENV_OFF;
        engine->voices[i].filt_env.stage = NSAW_ENV_OFF;
    }
}

/* =====================================================================
 * Voice allocation
 * ===================================================================== */

static int find_free_voice(nsaw_engine_t *engine) {
    /* First: find an inactive voice */
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        if (!engine->voices[i].active && engine->voices[i].amp_env.stage == NSAW_ENV_OFF) {
            return i;
        }
    }
    /* Second: find a releasing voice (steal the oldest) */
    int oldest_idx = -1;
    uint32_t oldest_age = 0xFFFFFFFF;
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        if (engine->voices[i].amp_env.stage == NSAW_ENV_RELEASE && engine->voices[i].age < oldest_age) {
            oldest_age = engine->voices[i].age;
            oldest_idx = i;
        }
    }
    if (oldest_idx >= 0) return oldest_idx;

    /* Last resort: steal the oldest active voice */
    oldest_age = 0xFFFFFFFF;
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        if (engine->voices[i].age < oldest_age) {
            oldest_age = engine->voices[i].age;
            oldest_idx = i;
        }
    }
    return oldest_idx >= 0 ? oldest_idx : 0;
}

/* =====================================================================
 * MIDI handlers
 * ===================================================================== */

void nsaw_engine_note_on(nsaw_engine_t *engine, int note, float velocity) {
    int vi = find_free_voice(engine);
    nsaw_voice_t *v = &engine->voices[vi];

    v->active = 1;
    v->note = note;
    v->velocity = velocity;
    v->freq = note_to_freq(note + engine->octave_transpose * 12);
    v->age = engine->voice_counter++;

    /* Random phase initialization and zero drift state */
    for (int j = 0; j < engine->num_oscs; j++) {
        v->phase[j] = rand_float(&engine->rng_state);
        v->drift[j] = 0.0f;
    }

    /* Sub oscillator starts at zero for clean attack */
    v->sub_phase = 0.0f;

    /* Reset DC-blocking HPF state (stereo) */
    v->hpf_x_prev_l = 0.0f;
    v->hpf_y_prev_l = 0.0f;
    v->hpf_x_prev_r = 0.0f;
    v->hpf_y_prev_r = 0.0f;

    /* Reset lowpass filter state (stereo) */
    v->ic1eq_l = 0.0f;
    v->ic2eq_l = 0.0f;
    v->ic1eq_r = 0.0f;
    v->ic2eq_r = 0.0f;

    /* Trigger envelopes (smooth retrigger: start from current level) */
    v->amp_env.stage = NSAW_ENV_ATTACK;
    v->filt_env.stage = NSAW_ENV_ATTACK;
}

void nsaw_engine_note_off(nsaw_engine_t *engine, int note) {
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        nsaw_voice_t *v = &engine->voices[i];
        if (v->active && v->note == note && v->amp_env.stage != NSAW_ENV_RELEASE) {
            v->active = 0;
            v->amp_env.stage = NSAW_ENV_RELEASE;
            v->filt_env.stage = NSAW_ENV_RELEASE;
        }
    }
}

void nsaw_engine_pitch_bend(nsaw_engine_t *engine, float bend) {
    engine->current_bend = bend;
}

void nsaw_engine_all_notes_off(nsaw_engine_t *engine) {
    for (int i = 0; i < NSAW_MAX_VOICES; i++) {
        nsaw_voice_t *v = &engine->voices[i];
        v->active = 0;
        v->amp_env.stage = NSAW_ENV_OFF;
        v->amp_env.level = 0.0f;
        v->filt_env.stage = NSAW_ENV_OFF;
        v->filt_env.level = 0.0f;
        v->ic1eq_l = 0.0f;
        v->ic2eq_l = 0.0f;
        v->ic1eq_r = 0.0f;
        v->ic2eq_r = 0.0f;
        v->hpf_x_prev_l = 0.0f;
        v->hpf_y_prev_l = 0.0f;
        v->hpf_x_prev_r = 0.0f;
        v->hpf_y_prev_r = 0.0f;
    }
}

/* =====================================================================
 * Envelope processing (per sample)
 * ===================================================================== */

static inline void process_envelope(nsaw_envelope_t *env,
                                     float attack_rate, float decay_coeff,
                                     float sustain_level, float release_coeff) {
    switch (env->stage) {
        case NSAW_ENV_ATTACK:
            env->level += attack_rate;
            if (env->level >= 1.0f) {
                env->level = 1.0f;
                env->stage = NSAW_ENV_DECAY;
            }
            break;
        case NSAW_ENV_DECAY:
            env->level = sustain_level + (env->level - sustain_level) * decay_coeff;
            if (env->level <= sustain_level + 0.0001f) {
                env->level = sustain_level;
                env->stage = NSAW_ENV_SUSTAIN;
            }
            break;
        case NSAW_ENV_SUSTAIN:
            env->level = sustain_level;
            break;
        case NSAW_ENV_RELEASE:
            env->level *= release_coeff;
            if (env->level < 0.0001f) {
                env->level = 0.0f;
                env->stage = NSAW_ENV_OFF;
            }
            break;
        case NSAW_ENV_OFF:
        default:
            env->level = 0.0f;
            break;
    }
}

/* =====================================================================
 * Render block (stereo)
 * ===================================================================== */

void nsaw_engine_render(nsaw_engine_t *engine, float *out_left, float *out_right, int frames) {
    if (frames > NSAW_MAX_RENDER) frames = NSAW_MAX_RENDER;

    float sr = engine->sample_rate;

    /* --- Precompute envelope coefficients --- */

    float amp_attack_rate = 1.0f / (param_to_seconds(engine->attack) * sr);
    float amp_decay_coeff = expf(-4.0f / (param_to_seconds(engine->decay) * sr));
    float amp_sustain = engine->sustain;
    float amp_release_coeff = expf(-4.0f / (param_to_seconds(engine->release) * sr));

    float filt_attack_rate = 1.0f / (param_to_seconds(engine->f_attack) * sr);
    float filt_decay_coeff = expf(-4.0f / (param_to_seconds(engine->f_decay) * sr));
    float filt_sustain = engine->f_sustain;
    float filt_release_coeff = expf(-4.0f / (param_to_seconds(engine->f_release) * sr));

    /* --- Filter parameters --- */

    /* Resonance: Q from 0.5 to 20 */
    float q = 0.5f + engine->resonance * 19.5f;

    /* Filter envelope amount in octaves (0 to 8) */
    float f_env_octaves = engine->f_amount * 8.0f;

    /* --- Pitch bend --- */

    float bend_semitones = engine->current_bend * engine->bend_range * 12.0f;
    float bend_ratio = powf(2.0f, bend_semitones / 12.0f);

    /* --- Master volume with polyphony headroom --- */

    float master_vol = engine->volume * 0.3f;

    /* --- Clear output --- */

    memset(out_left, 0, frames * sizeof(float));
    memset(out_right, 0, frames * sizeof(float));

    /* --- Process each polyphonic voice --- */

    for (int vi = 0; vi < NSAW_MAX_VOICES; vi++) {
        nsaw_voice_t *v = &engine->voices[vi];
        if (v->amp_env.stage == NSAW_ENV_OFF) continue;

        float f0 = v->freq * bend_ratio;
        float vel_gain = 1.0f - engine->vel_sens + engine->vel_sens * v->velocity;

        for (int n = 0; n < frames; n++) {

            /* --- Parameter smoothing (per-sample one-pole) --- */
            engine->smooth_detune += (engine->detune - engine->smooth_detune) * SMOOTH_COEFF;
            engine->smooth_spread += (engine->spread - engine->smooth_spread) * SMOOTH_COEFF;
            engine->smooth_cutoff += (engine->cutoff - engine->smooth_cutoff) * SMOOTH_COEFF;

            float cur_detune = engine->smooth_detune;
            float cur_spread = engine->smooth_spread;

            /* Cutoff: exponential mapping 20Hz to 20kHz (smoothed) */
            float base_cutoff_hz = 20.0f * powf(1000.0f, engine->smooth_cutoff);
            if (base_cutoff_hz > 20000.0f) base_cutoff_hz = 20000.0f;

            /* --- Detune scaling ---
             * Piecewise-linear curve maps detune param to [0,1],
             * then D = f0 * k_max * curve(detune).
             * k_max = 0.10 (10% max detune for outermost pair) */
            float D = f0 * DETUNE_K_MAX * detune_curve(cur_detune);
            float dInc = D / sr;

            /* Base phase increment */
            float inc0 = f0 / sr;

            /* --- Non-linear spread curve ---
             * spread^1.5 gives gentler onset (subtle at low, dramatic at high)
             * Computed as spread * sqrt(spread) to avoid powf
             * Floor ensures detuned voices never completely vanish */
            float gs = cur_spread * sqrtf(cur_spread) * SIDE_GAIN_SCALE;
            if (gs < SIDE_GAIN_FLOOR) gs = SIDE_GAIN_FLOOR;

            /* RMS normalization: consistent loudness regardless of spread
             * Total energy = 1^2 + N_sides * gs^2; norm = 1/sqrt(total)
             * Works correctly with stereo panning (constant-power preserves total energy) */
            float norm = 1.0f / sqrtf(1.0f + (float)(engine->num_oscs - 1) * gs * gs);

            /* --- Generate and mix all oscillator voices (stereo) --- */

            float osc_mix_l = 0.0f;
            float osc_mix_r = 0.0f;

            for (int j = 0; j < engine->num_oscs; j++) {
                /* Analog pitch drift: one-pole lowpass filtered white noise
                 * Creates slow, independent pitch wander per oscillator (~0.35 cents) */
                float noise = rand_float(&engine->rng_state) * 2.0f - 1.0f;
                v->drift[j] += (noise - v->drift[j]) * DRIFT_COEFF;
                float drift_mult = 1.0f + v->drift[j] * DRIFT_AMOUNT;

                /* Per-voice increment: inc[j] = (inc0 + coeff[j] * dInc) * drift */
                float inc_j = (inc0 + engine->detune_coeff[j] * dInc) * drift_mult;
                if (inc_j < 0.0f) inc_j = 0.0f;  /* Safety clamp */

                /* Advance and wrap phase */
                v->phase[j] += inc_j;
                if (v->phase[j] >= 1.0f) v->phase[j] -= 1.0f;

                /* Naive sawtooth: map phase [0,1) to [-1,+1) */
                float saw = 2.0f * v->phase[j] - 1.0f;

                /* PolyBLEP anti-aliasing */
                saw -= polyblep(v->phase[j], inc_j);

                /* Apply gain (center=1.0, sides=gs) and stereo pan */
                float gain = (j == 0) ? 1.0f : gs;
                osc_mix_l += saw * gain * engine->pan_l[j];
                osc_mix_r += saw * gain * engine->pan_r[j];
            }

            /* RMS-based normalization for consistent loudness */
            osc_mix_l *= norm;
            osc_mix_r *= norm;

            /* --- Sub oscillator (sine, center-panned) --- */
            if (engine->sub_level > 0.001f) {
                float sub_mult = (engine->sub_octave == -2) ? 0.25f :
                                 (engine->sub_octave == -1) ? 0.5f : 1.0f;
                float sub_inc = inc0 * sub_mult;
                v->sub_phase += sub_inc;
                if (v->sub_phase >= 1.0f) v->sub_phase -= 1.0f;
                float sub = sinf(v->sub_phase * 2.0f * (float)M_PI) * engine->sub_level;
                osc_mix_l += sub * 0.7071f;  /* center pan */
                osc_mix_r += sub * 0.7071f;
            }

            /* --- Post-mix DC-blocking HPF (stereo) ---
             * y[n] = x[n] - x[n-1] + R * y[n-1]
             * 1-pole highpass, cutoff ~20Hz */
            float hpf_l = osc_mix_l - v->hpf_x_prev_l + HPF_R * v->hpf_y_prev_l;
            v->hpf_x_prev_l = osc_mix_l;
            v->hpf_y_prev_l = hpf_l;

            float hpf_r = osc_mix_r - v->hpf_x_prev_r + HPF_R * v->hpf_y_prev_r;
            v->hpf_x_prev_r = osc_mix_r;
            v->hpf_y_prev_r = hpf_r;

            /* --- Process envelopes --- */

            process_envelope(&v->amp_env, amp_attack_rate, amp_decay_coeff,
                           amp_sustain, amp_release_coeff);
            process_envelope(&v->filt_env, filt_attack_rate, filt_decay_coeff,
                           filt_sustain, filt_release_coeff);

            /* --- Resonant lowpass filter with envelope modulation (stereo) --- */

            float mod_cutoff_hz = base_cutoff_hz * powf(2.0f, v->filt_env.level * f_env_octaves);
            if (mod_cutoff_hz > 20000.0f) mod_cutoff_hz = 20000.0f;
            if (mod_cutoff_hz < 20.0f) mod_cutoff_hz = 20.0f;

            /* TPT/SVF coefficients (shared between L and R) */
            float g = tanf((float)M_PI * mod_cutoff_hz / sr);
            float k = 1.0f / q;
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;
            float a3 = g * a2;

            /* L channel SVF */
            float t3_l = hpf_l - v->ic2eq_l;
            float t1_l = a1 * v->ic1eq_l + a2 * t3_l;
            float t2_l = v->ic2eq_l + a2 * v->ic1eq_l + a3 * t3_l;
            v->ic1eq_l = 2.0f * t1_l - v->ic1eq_l;
            v->ic2eq_l = 2.0f * t2_l - v->ic2eq_l;

            /* R channel SVF */
            float t3_r = hpf_r - v->ic2eq_r;
            float t1_r = a1 * v->ic1eq_r + a2 * t3_r;
            float t2_r = v->ic2eq_r + a2 * v->ic1eq_r + a3 * t3_r;
            v->ic1eq_r = 2.0f * t1_r - v->ic1eq_r;
            v->ic2eq_r = 2.0f * t2_r - v->ic2eq_r;

            /* --- Apply amp envelope and velocity --- */

            float amp = v->amp_env.level * vel_gain * master_vol;
            out_left[n]  += t2_l * amp;
            out_right[n] += t2_r * amp;
        }
    }
}
