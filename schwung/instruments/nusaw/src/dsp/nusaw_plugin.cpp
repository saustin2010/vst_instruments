/*
 * NuSaw DSP Plugin for Move Anything
 *
 * Polyphonic detuned multi-saw synthesizer with stereo panning,
 * analog drift, TPT/SVF resonant lowpass filter, ADSR amp and filter envelopes.
 *
 * V2 API - instance-based for multi-instance support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Include plugin API */
extern "C" {
#include <stdint.h>

#define MOVE_PLUGIN_API_VERSION 1
#define MOVE_SAMPLE_RATE 44100
#define MOVE_FRAMES_PER_BLOCK 128
#define MOVE_MIDI_SOURCE_INTERNAL 0
#define MOVE_MIDI_SOURCE_EXTERNAL 2

typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;
    int frames_per_block;
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
} host_api_v1_t;

#define MOVE_PLUGIN_API_VERSION_2 2

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

typedef plugin_api_v2_t* (*move_plugin_init_v2_fn)(const host_api_v1_t *host);
#define MOVE_PLUGIN_INIT_V2_SYMBOL "move_plugin_init_v2"

/* Engine */
#include "nusaw_engine.h"
}

/* Include param helper */
#include "param_helper.h"

/* Host API reference */
static const host_api_v1_t *g_host = NULL;

static void plugin_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[nusaw] %s", msg);
        g_host->log(buf);
    }
}

/* =====================================================================
 * Parameter definitions
 * ===================================================================== */

enum {
    P_CUTOFF = 0,
    P_RESONANCE,
    P_DETUNE,
    P_SPREAD,
    P_F_AMOUNT,
    P_ATTACK,
    P_DECAY,
    P_SUSTAIN,
    P_RELEASE,
    P_F_ATTACK,
    P_F_DECAY,
    P_F_SUSTAIN,
    P_F_RELEASE,
    P_VOLUME,
    P_VEL_SENS,
    P_BEND_RANGE,
    P_SUB_LEVEL,
    P_SUB_OCTAVE,
    P_SAW_COUNT,
    P_CHORUS_MIX,
    P_CHORUS_DEPTH,
    P_DELAY_TIME,
    P_DELAY_FBACK,
    P_DELAY_MIX,
    P_DELAY_TONE,
    P_COUNT
};

static const param_def_t g_shadow_params[] = {
    {"cutoff",      "Cutoff",       PARAM_TYPE_FLOAT, P_CUTOFF,     0.0f, 1.0f},
    {"resonance",   "Resonance",    PARAM_TYPE_FLOAT, P_RESONANCE,  0.0f, 1.0f},
    {"detune",      "Detune",       PARAM_TYPE_FLOAT, P_DETUNE,     0.0f, 1.0f},
    {"spread",      "Spread",       PARAM_TYPE_FLOAT, P_SPREAD,     0.0f, 1.0f},
    {"f_amount",    "Filt Env Amt", PARAM_TYPE_FLOAT, P_F_AMOUNT,   0.0f, 1.0f},
    {"attack",      "Attack",       PARAM_TYPE_FLOAT, P_ATTACK,     0.0f, 1.0f},
    {"decay",       "Decay",        PARAM_TYPE_FLOAT, P_DECAY,      0.0f, 1.0f},
    {"sustain",     "Sustain",      PARAM_TYPE_FLOAT, P_SUSTAIN,    0.0f, 1.0f},
    {"release",     "Release",      PARAM_TYPE_FLOAT, P_RELEASE,    0.0f, 1.0f},
    {"f_attack",    "F Attack",     PARAM_TYPE_FLOAT, P_F_ATTACK,   0.0f, 1.0f},
    {"f_decay",     "F Decay",      PARAM_TYPE_FLOAT, P_F_DECAY,    0.0f, 1.0f},
    {"f_sustain",   "F Sustain",    PARAM_TYPE_FLOAT, P_F_SUSTAIN,  0.0f, 1.0f},
    {"f_release",   "F Release",    PARAM_TYPE_FLOAT, P_F_RELEASE,  0.0f, 1.0f},
    {"volume",      "Volume",       PARAM_TYPE_FLOAT, P_VOLUME,     0.0f, 1.0f},
    {"vel_sens",    "Vel Sens",     PARAM_TYPE_FLOAT, P_VEL_SENS,   0.0f, 1.0f},
    {"bend_range",  "Bend Range",   PARAM_TYPE_FLOAT, P_BEND_RANGE, 0.0f, 1.0f},
    {"sub_level",   "Sub",          PARAM_TYPE_FLOAT, P_SUB_LEVEL,  0.0f, 1.0f},
    {"sub_octave",  "Sub Oct",      PARAM_TYPE_INT,   P_SUB_OCTAVE, -2.0f, 0.0f},
    {"saw_count",   "Saws",         PARAM_TYPE_INT,   P_SAW_COUNT,   3.0f, 25.0f},
    {"chorus_mix",  "Chorus",       PARAM_TYPE_FLOAT, P_CHORUS_MIX,  0.0f, 1.0f},
    {"chorus_depth","Chr Depth",    PARAM_TYPE_FLOAT, P_CHORUS_DEPTH,0.0f, 1.0f},
    {"delay_time",  "Dly Time",     PARAM_TYPE_FLOAT, P_DELAY_TIME,  0.0f, 1.0f},
    {"delay_fback", "Dly Fback",    PARAM_TYPE_FLOAT, P_DELAY_FBACK, 0.0f, 1.0f},
    {"delay_mix",   "Delay",        PARAM_TYPE_FLOAT, P_DELAY_MIX,   0.0f, 1.0f},
    {"delay_tone",  "Dly Tone",     PARAM_TYPE_FLOAT, P_DELAY_TONE,  0.0f, 1.0f},
};

/* =====================================================================
 * Preset system
 * ===================================================================== */

#define MAX_PRESETS 48

struct NsawPreset {
    char name[32];
    float params[P_COUNT];
};

/*
 * Factory presets (27 presets)
 * Parameter order: cutoff, resonance, detune, spread, f_amount,
 *                  attack, decay, sustain, release,
 *                  f_attack, f_decay, f_sustain, f_release,
 *                  volume, vel_sens, bend_range, sub_level, sub_octave, saw_count,
 *                  chorus_mix, chorus_depth, delay_time, delay_fback, delay_mix, delay_tone
 *
 * Envelope time reference (param_to_seconds = 0.001 * 10000^p):
 *   0.00=1ms  0.25=10ms  0.35=25ms  0.40=40ms  0.42=50ms  0.45=63ms
 *   0.50=100ms  0.55=160ms  0.60=250ms  0.65=400ms  0.70=630ms
 *   0.75=1s  0.80=1.6s  0.85=2.5s  0.90=4s
 *
 * Cutoff reference (20 * 1000^p):
 *   0.25=112Hz  0.30=160Hz  0.35=224Hz  0.40=320Hz  0.45=450Hz
 *   0.50=632Hz  0.55=900Hz  0.60=1.3kHz  0.65=1.8kHz  0.70=2.5kHz
 *   0.75=3.6kHz  0.80=5kHz  0.85=7kHz  0.90=10kHz
 */
static const NsawPreset g_factory_presets[] = {

    /* ---- Starting Point ---- */

    /* 0: Init - bright default, dry (effects off, sensible defaults when enabled) */
    {"Init", {
        0.75f, 0.00f, 0.25f, 0.60f, 0.40f,
        0.00f, 0.55f, 0.70f, 0.55f,
        0.00f, 0.50f, 0.30f, 0.50f,
        0.70f, 0.50f, 0.167f, 0.00f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.66f, 0.35f, 0.00f, 0.55f
    }},

    /* ---- Anthemic Leads ---- */

    /* 1: Festival Lead - the big stadium sound, wide, bright, full */
    {"Festival Lead", {
        0.80f, 0.15f, 0.60f, 0.90f, 0.55f,
        0.00f, 0.55f, 0.70f, 0.55f,
        0.00f, 0.50f, 0.20f, 0.50f,
        0.75f, 0.40f, 0.167f, 0.25f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.70f, 0.35f, 0.18f, 0.50f
    }},

    /* 2: Sunrise Lead - warm, emotional, for melodic breakdowns */
    {"Sunrise Lead", {
        0.72f, 0.10f, 0.45f, 0.85f, 0.45f,
        0.00f, 0.55f, 0.72f, 0.55f,
        0.00f, 0.55f, 0.30f, 0.55f,
        0.72f, 0.35f, 0.167f, 0.30f, -1.0f, 7.0f,
        0.10f, 0.40f, 0.72f, 0.40f, 0.15f, 0.45f
    }},

    /* 3: Razor Lead - aggressive, hard-edged, high resonance */
    {"Razor Lead", {
        0.85f, 0.28f, 0.65f, 0.85f, 0.40f,
        0.00f, 0.50f, 0.65f, 0.50f,
        0.00f, 0.45f, 0.30f, 0.45f,
        0.78f, 0.50f, 0.167f, 0.20f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.60f, 0.30f, 0.12f, 0.60f
    }},

    /* 4: Dream Lead - airy, breathy, long delay trails */
    {"Dream Lead", {
        0.75f, 0.05f, 0.50f, 0.92f, 0.50f,
        0.15f, 0.60f, 0.68f, 0.60f,
        0.10f, 0.55f, 0.35f, 0.55f,
        0.68f, 0.30f, 0.167f, 0.20f, -1.0f, 7.0f,
        0.18f, 0.45f, 0.72f, 0.42f, 0.22f, 0.40f
    }},

    /* ---- Stabs ---- */

    /* 5: Big Stab - maximum impact chord hit */
    {"Big Stab", {
        0.82f, 0.18f, 0.55f, 0.92f, 0.75f,
        0.00f, 0.50f, 0.00f, 0.45f,
        0.00f, 0.45f, 0.00f, 0.40f,
        0.82f, 0.55f, 0.167f, 0.20f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.60f, 0.42f, 0.20f, 0.50f
    }},

    /* 6: Filtered Stab - dark to bright, dramatic filter sweep */
    {"Filtered Stab", {
        0.40f, 0.20f, 0.45f, 0.85f, 0.85f,
        0.00f, 0.55f, 0.05f, 0.50f,
        0.00f, 0.50f, 0.00f, 0.45f,
        0.78f, 0.50f, 0.167f, 0.25f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.66f, 0.45f, 0.18f, 0.45f
    }},

    /* ---- Existing Leads ---- */

    /* 7: Trance Lead - classic dotted-1/8 delay (~375ms), no chorus */
    {"Trance Lead", {
        0.78f, 0.15f, 0.30f, 0.75f, 0.55f,
        0.00f, 0.55f, 0.65f, 0.55f,
        0.00f, 0.50f, 0.25f, 0.50f,
        0.75f, 0.40f, 0.167f, 0.25f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.66f, 0.35f, 0.18f, 0.50f
    }},

    /* 8: Anthem - chorus for width, 1/4 note delay (~500ms) for epic space */
    {"Anthem", {
        0.72f, 0.10f, 0.40f, 0.90f, 0.50f,
        0.25f, 0.60f, 0.75f, 0.60f,
        0.20f, 0.55f, 0.35f, 0.55f,
        0.70f, 0.30f, 0.167f, 0.35f, -1.0f, 7.0f,
        0.22f, 0.50f, 0.70f, 0.30f, 0.12f, 0.45f
    }},

    /* ---- Pads ---- */

    /* 9: Anthem Pad - big warm harmonic support */
    {"Anthem Pad", {
        0.62f, 0.08f, 0.42f, 0.95f, 0.40f,
        0.65f, 0.60f, 0.85f, 0.70f,
        0.60f, 0.55f, 0.50f, 0.65f,
        0.65f, 0.20f, 0.167f, 0.30f, -1.0f, 9.0f,
        0.35f, 0.55f, 0.72f, 0.35f, 0.15f, 0.40f
    }},

    /* 10: Dark Pad - deep, moody, for breakdowns */
    {"Dark Pad", {
        0.48f, 0.12f, 0.50f, 0.90f, 0.30f,
        0.75f, 0.65f, 0.88f, 0.80f,
        0.70f, 0.60f, 0.55f, 0.75f,
        0.60f, 0.15f, 0.167f, 0.35f, -1.0f, 9.0f,
        0.30f, 0.60f, 0.75f, 0.45f, 0.20f, 0.30f
    }},

    /* 11: Glass Pad - bright, crystalline, shimmering (sub at unison) */
    {"Glass Pad", {
        0.78f, 0.05f, 0.35f, 0.88f, 0.35f,
        0.70f, 0.55f, 0.82f, 0.75f,
        0.65f, 0.50f, 0.55f, 0.70f,
        0.62f, 0.20f, 0.167f, 0.10f, 0.0f, 9.0f,
        0.40f, 0.65f, 0.73f, 0.40f, 0.18f, 0.55f
    }},

    /* 12: Evolving Pad - slow filter movement, shifting texture */
    {"Evolving Pad", {
        0.40f, 0.15f, 0.48f, 0.93f, 0.60f,
        0.80f, 0.70f, 0.80f, 0.85f,
        0.75f, 0.70f, 0.40f, 0.80f,
        0.60f, 0.15f, 0.167f, 0.25f, -1.0f, 9.0f,
        0.35f, 0.55f, 0.75f, 0.50f, 0.25f, 0.35f
    }},

    /* ---- Strings ---- */

    /* 13: Warm Strings - classic analog string machine (sub at unison) */
    {"Warm Strings", {
        0.63f, 0.00f, 0.18f, 0.75f, 0.25f,
        0.65f, 0.55f, 0.88f, 0.70f,
        0.60f, 0.50f, 0.60f, 0.65f,
        0.65f, 0.15f, 0.167f, 0.15f, 0.0f, 11.0f,
        0.45f, 0.55f, 0.70f, 0.25f, 0.08f, 0.40f
    }},

    /* 14: Bright Strings - upper-register orchestral character (sub at unison) */
    {"Bright Strings", {
        0.73f, 0.05f, 0.22f, 0.78f, 0.30f,
        0.60f, 0.55f, 0.85f, 0.68f,
        0.55f, 0.50f, 0.55f, 0.60f,
        0.65f, 0.20f, 0.167f, 0.10f, 0.0f, 11.0f,
        0.40f, 0.50f, 0.70f, 0.25f, 0.10f, 0.50f
    }},

    /* 15: Cinematic Strings - dark, wide, epic */
    {"Cinematic Strings", {
        0.55f, 0.08f, 0.25f, 0.82f, 0.20f,
        0.75f, 0.60f, 0.90f, 0.80f,
        0.70f, 0.55f, 0.65f, 0.75f,
        0.62f, 0.10f, 0.167f, 0.25f, -1.0f, 11.0f,
        0.38f, 0.60f, 0.75f, 0.35f, 0.15f, 0.35f
    }},

    /* ---- Bass ---- */

    /* 16: Trance Bass - punchy workhorse, dry */
    {"Trance Bass", {
        0.48f, 0.18f, 0.20f, 0.60f, 0.60f,
        0.00f, 0.50f, 0.65f, 0.45f,
        0.00f, 0.45f, 0.05f, 0.40f,
        0.80f, 0.55f, 0.167f, 0.45f, -1.0f, 5.0f,
        0.00f, 0.50f, 0.66f, 0.35f, 0.00f, 0.55f
    }},

    /* 17: Sub Bass - pure low-end foundation, dry (sub at -2 oct) */
    {"Sub Bass", {
        0.35f, 0.00f, 0.05f, 0.30f, 0.20f,
        0.00f, 0.55f, 0.80f, 0.50f,
        0.00f, 0.50f, 0.15f, 0.45f,
        0.80f, 0.30f, 0.167f, 0.60f, -2.0f, 5.0f,
        0.00f, 0.50f, 0.66f, 0.35f, 0.00f, 0.55f
    }},

    /* 18: Growl Bass - aggressive detuned texture, dry */
    {"Growl Bass", {
        0.52f, 0.30f, 0.55f, 0.88f, 0.50f,
        0.00f, 0.50f, 0.75f, 0.50f,
        0.00f, 0.45f, 0.10f, 0.40f,
        0.80f, 0.45f, 0.167f, 0.40f, -1.0f, 5.0f,
        0.00f, 0.50f, 0.66f, 0.35f, 0.00f, 0.55f
    }},

    /* 19: Pluck Bass - short percussive, rhythmic delay */
    {"Pluck Bass", {
        0.42f, 0.15f, 0.18f, 0.55f, 0.75f,
        0.00f, 0.45f, 0.00f, 0.42f,
        0.00f, 0.40f, 0.00f, 0.35f,
        0.82f, 0.60f, 0.167f, 0.40f, -1.0f, 5.0f,
        0.00f, 0.50f, 0.60f, 0.40f, 0.15f, 0.55f
    }},

    /* ---- Special ---- */

    /* 20: Arp Pluck - short, bright, for arpeggiated sequences */
    {"Arp Pluck", {
        0.72f, 0.08f, 0.30f, 0.70f, 0.60f,
        0.00f, 0.45f, 0.00f, 0.40f,
        0.00f, 0.40f, 0.00f, 0.35f,
        0.75f, 0.55f, 0.167f, 0.10f, -1.0f, 5.0f,
        0.00f, 0.50f, 0.60f, 0.50f, 0.20f, 0.55f
    }},

    /* 21: Hardstyle - dry aggressive lead, tight 1/8 delay for rhythm */
    {"Hardstyle", {
        0.82f, 0.25f, 0.60f, 0.85f, 0.30f,
        0.00f, 0.50f, 0.70f, 0.50f,
        0.00f, 0.45f, 0.40f, 0.45f,
        0.80f, 0.50f, 0.167f, 0.40f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.60f, 0.25f, 0.10f, 0.60f
    }},

    /* 22: Solo Saw - raw oscillator, completely dry */
    {"Solo Saw", {
        0.82f, 0.00f, 0.00f, 0.00f, 0.25f,
        0.00f, 0.55f, 0.80f, 0.55f,
        0.00f, 0.50f, 0.50f, 0.50f,
        0.70f, 0.50f, 0.167f, 0.00f, -1.0f, 3.0f,
        0.00f, 0.50f, 0.66f, 0.35f, 0.00f, 0.55f
    }},

    /* 23: Warm Lead - gentle chorus, dotted-1/8 delay (~375ms) for space */
    {"Warm Lead", {
        0.70f, 0.08f, 0.08f, 0.45f, 0.40f,
        0.00f, 0.55f, 0.65f, 0.55f,
        0.00f, 0.50f, 0.30f, 0.50f,
        0.70f, 0.50f, 0.25f, 0.20f, -1.0f, 7.0f,
        0.15f, 0.40f, 0.66f, 0.35f, 0.15f, 0.50f
    }},

    /* 24: Acid - dub-style dotted-1/8 delay (~375ms) with high feedback */
    {"Acid", {
        0.40f, 0.80f, 0.00f, 0.00f, 0.85f,
        0.00f, 0.60f, 0.50f, 0.50f,
        0.00f, 0.55f, 0.05f, 0.45f,
        0.75f, 0.65f, 0.167f, 0.20f, -1.0f, 7.0f,
        0.00f, 0.50f, 0.66f, 0.55f, 0.18f, 0.45f
    }},

    /* 25: Hoover - subtle chorus, dotted-1/8 delay (~375ms) for space */
    {"Hoover", {
        0.70f, 0.25f, 0.75f, 1.00f, 0.45f,
        0.00f, 0.55f, 0.70f, 0.55f,
        0.00f, 0.50f, 0.30f, 0.50f,
        0.70f, 0.40f, 0.25f, 0.30f, -1.0f, 7.0f,
        0.15f, 0.50f, 0.66f, 0.35f, 0.12f, 0.50f
    }},

    /* 26: Vapor - heavy chorus + long dreamy delay (~600ms), dark tone */
    {"Vapor", {
        0.55f, 0.15f, 0.50f, 0.90f, 0.35f,
        0.80f, 0.70f, 0.90f, 0.85f,
        0.75f, 0.65f, 0.70f, 0.80f,
        0.60f, 0.10f, 0.167f, 0.20f, -1.0f, 7.0f,
        0.30f, 0.65f, 0.78f, 0.50f, 0.30f, 0.30f
    }},
};

#define FACTORY_PRESET_COUNT (int)(sizeof(g_factory_presets) / sizeof(g_factory_presets[0]))

/* =====================================================================
 * Effects state
 * ===================================================================== */

#define CHORUS_BUF_SIZE 512
#define DELAY_MAX_SAMPLES 44100  /* 1 second at 44.1kHz */

typedef struct {
    /* Chorus */
    float chorus_buf[CHORUS_BUF_SIZE];
    int chorus_write_pos;
    float lfo1_phase, lfo2_phase;

    /* Delay */
    float *delay_buf_l;  /* heap: DELAY_MAX_SAMPLES floats */
    float *delay_buf_r;
    int delay_write_pos;
    float tone_z1_l, tone_z1_r;  /* one-pole filter state */
} nsaw_effects_t;

/* =====================================================================
 * Instance
 * ===================================================================== */

typedef struct {
    char module_dir[256];
    nsaw_engine_t engine;
    int current_preset;
    int preset_count;
    char preset_name[64];
    float params[P_COUNT];
    NsawPreset presets[MAX_PRESETS];
    int octave_transpose;
    nsaw_effects_t fx;
} nsaw_instance_t;

static void apply_params_to_engine(nsaw_instance_t *inst);
static void apply_preset(nsaw_instance_t *inst, int preset_idx);

/* =====================================================================
 * Parameter application
 * ===================================================================== */

static void apply_params_to_engine(nsaw_instance_t *inst) {
    nsaw_engine_t *e = &inst->engine;

    e->cutoff      = inst->params[P_CUTOFF];
    e->resonance   = inst->params[P_RESONANCE];
    e->detune      = inst->params[P_DETUNE];
    e->spread      = inst->params[P_SPREAD];
    e->f_amount    = inst->params[P_F_AMOUNT];
    e->attack      = inst->params[P_ATTACK];
    e->decay       = inst->params[P_DECAY];
    e->sustain     = inst->params[P_SUSTAIN];
    e->release     = inst->params[P_RELEASE];
    e->f_attack    = inst->params[P_F_ATTACK];
    e->f_decay     = inst->params[P_F_DECAY];
    e->f_sustain   = inst->params[P_F_SUSTAIN];
    e->f_release   = inst->params[P_F_RELEASE];
    e->volume      = inst->params[P_VOLUME];
    e->vel_sens    = inst->params[P_VEL_SENS];
    e->bend_range  = inst->params[P_BEND_RANGE];
    e->sub_level   = inst->params[P_SUB_LEVEL];
    e->sub_octave  = (int)roundf(inst->params[P_SUB_OCTAVE]);

    int new_saw_count = (int)roundf(inst->params[P_SAW_COUNT]);
    new_saw_count |= 1;  /* ensure odd */
    if (new_saw_count != e->num_oscs) {
        nsaw_engine_update_osc_config(e, new_saw_count);
    }
}

static void apply_preset(nsaw_instance_t *inst, int preset_idx) {
    if (preset_idx < 0 || preset_idx >= inst->preset_count) return;

    NsawPreset *p = &inst->presets[preset_idx];
    memcpy(inst->params, p->params, sizeof(float) * P_COUNT);
    snprintf(inst->preset_name, sizeof(inst->preset_name), "%s", p->name);
    inst->current_preset = preset_idx;

    apply_params_to_engine(inst);
}

/* =====================================================================
 * JSON helper
 * ===================================================================== */

static int json_get_number(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    pos += strlen(search);
    while (*pos == ' ') pos++;
    *out = (float)atof(pos);
    return 0;
}

/* =====================================================================
 * Plugin API v2
 * ===================================================================== */

static void* v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    nsaw_instance_t *inst = (nsaw_instance_t*)calloc(1, sizeof(nsaw_instance_t));
    if (!inst) return NULL;

    strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);

    /* Initialize engine */
    nsaw_engine_init(&inst->engine);

    /* Load factory presets */
    inst->preset_count = FACTORY_PRESET_COUNT;
    for (int i = 0; i < FACTORY_PRESET_COUNT; i++) {
        memcpy(&inst->presets[i], &g_factory_presets[i], sizeof(NsawPreset));
    }

    /* Initialize effects */
    memset(&inst->fx, 0, sizeof(nsaw_effects_t));
    inst->fx.delay_buf_l = (float*)calloc(DELAY_MAX_SAMPLES, sizeof(float));
    inst->fx.delay_buf_r = (float*)calloc(DELAY_MAX_SAMPLES, sizeof(float));

    /* Apply first preset */
    apply_preset(inst, 0);

    plugin_log("NuSaw v2: Instance created (stereo + fx)");
    return inst;
}

static void v2_destroy_instance(void *instance) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) return;
    free(inst->fx.delay_buf_l);
    free(inst->fx.delay_buf_r);
    free(inst);
    plugin_log("NuSaw v2: Instance destroyed");
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst || len < 2) return;
    (void)source;

    uint8_t status = msg[0] & 0xF0;
    uint8_t data1 = msg[1];
    uint8_t data2 = (len > 2) ? msg[2] : 0;

    switch (status) {
        case 0x90:
            if (data2 > 0) {
                nsaw_engine_note_on(&inst->engine, data1, data2 / 127.0f);
            } else {
                nsaw_engine_note_off(&inst->engine, data1);
            }
            break;
        case 0x80:
            nsaw_engine_note_off(&inst->engine, data1);
            break;
        case 0xB0:
            switch (data1) {
                case 123: /* All notes off */
                    nsaw_engine_all_notes_off(&inst->engine);
                    break;
            }
            break;
        case 0xE0: { /* Pitch bend */
            int bend = ((data2 << 7) | data1) - 8192;
            nsaw_engine_pitch_bend(&inst->engine, bend / 8192.0f);
            break;
        }
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) return;

    /* State restore from patch save */
    if (strcmp(key, "state") == 0) {
        float fval;

        if (json_get_number(val, "preset", &fval) == 0) {
            int idx = (int)fval;
            if (idx >= 0 && idx < inst->preset_count) {
                apply_preset(inst, idx);
            }
        }

        if (json_get_number(val, "octave_transpose", &fval) == 0) {
            inst->octave_transpose = (int)fval;
            inst->engine.octave_transpose = inst->octave_transpose;
        }

        /* Restore individual params */
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            if (json_get_number(val, g_shadow_params[i].key, &fval) == 0) {
                if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
                if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
                inst->params[g_shadow_params[i].index] = fval;
            }
        }
        apply_params_to_engine(inst);
        return;
    }

    if (strcmp(key, "preset") == 0) {
        int idx = atoi(val);
        if (idx >= 0 && idx < inst->preset_count) {
            apply_preset(inst, idx);
        }
    }
    else if (strcmp(key, "octave_transpose") == 0) {
        inst->octave_transpose = atoi(val);
        if (inst->octave_transpose < -3) inst->octave_transpose = -3;
        if (inst->octave_transpose > 3) inst->octave_transpose = 3;
        inst->engine.octave_transpose = inst->octave_transpose;
    }
    else if (strcmp(key, "all_notes_off") == 0) {
        nsaw_engine_all_notes_off(&inst->engine);
    }
    else {
        /* Named parameter access */
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            if (strcmp(key, g_shadow_params[i].key) == 0) {
                float fval = (float)atof(val);
                if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
                if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
                inst->params[g_shadow_params[i].index] = fval;
                apply_params_to_engine(inst);
                return;
            }
        }
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) return -1;

    if (strcmp(key, "preset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->current_preset);
    }
    if (strcmp(key, "preset_count") == 0) {
        return snprintf(buf, buf_len, "%d", inst->preset_count);
    }
    if (strcmp(key, "preset_name") == 0) {
        return snprintf(buf, buf_len, "%s", inst->preset_name);
    }
    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "NuSaw");
    }
    if (strcmp(key, "octave_transpose") == 0) {
        return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    }

    /* Named parameter access via helper */
    int result = param_helper_get(g_shadow_params, PARAM_DEF_COUNT(g_shadow_params),
                                  inst->params, key, buf, buf_len);
    if (result >= 0) return result;

    /* UI hierarchy for shadow parameter editor */
    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hierarchy = "{"
            "\"modes\":null,"
            "\"levels\":{"
                "\"root\":{"
                    "\"list_param\":\"preset\","
                    "\"count_param\":\"preset_count\","
                    "\"name_param\":\"preset_name\","
                    "\"children\":\"main\","
                    "\"knobs\":[\"cutoff\",\"resonance\",\"detune\",\"spread\",\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":[]"
                "},"
                "\"main\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"detune\",\"spread\",\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":["
                        "{\"level\":\"oscillator\",\"label\":\"Oscillator\"},"
                        "{\"level\":\"filter\",\"label\":\"Filter\"},"
                        "{\"level\":\"filt_env\",\"label\":\"Filter Env\"},"
                        "{\"level\":\"amp_env\",\"label\":\"Amp Env\"},"
                        "{\"level\":\"chorus\",\"label\":\"Chorus\"},"
                        "{\"level\":\"delay\",\"label\":\"Delay\"},"
                        "{\"level\":\"performance\",\"label\":\"Performance\"}"
                    "]"
                "},"
                "\"oscillator\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"detune\",\"spread\",\"saw_count\",\"sub_level\",\"sub_octave\"],"
                    "\"params\":[\"detune\",\"spread\",\"saw_count\",\"sub_level\",\"sub_octave\"]"
                "},"
                "\"filter\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"f_amount\"],"
                    "\"params\":[\"cutoff\",\"resonance\",\"f_amount\"]"
                "},"
                "\"filt_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\",\"f_amount\"],"
                    "\"params\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\",\"f_amount\"]"
                "},"
                "\"amp_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":[\"attack\",\"decay\",\"sustain\",\"release\"]"
                "},"
                "\"chorus\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"chorus_mix\",\"chorus_depth\"],"
                    "\"params\":[\"chorus_mix\",\"chorus_depth\"]"
                "},"
                "\"delay\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"delay_time\",\"delay_fback\",\"delay_mix\",\"delay_tone\"],"
                    "\"params\":[\"delay_time\",\"delay_fback\",\"delay_mix\",\"delay_tone\"]"
                "},"
                "\"performance\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"volume\",\"vel_sens\",\"bend_range\",\"octave_transpose\"],"
                    "\"params\":[\"volume\",\"vel_sens\",\"bend_range\",\"octave_transpose\"]"
                "}"
            "}"
        "}";
        int len = strlen(hierarchy);
        if (len < buf_len) {
            strcpy(buf, hierarchy);
            return len;
        }
        return -1;
    }

    /* State serialization for patch save/load */
    if (strcmp(key, "state") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset,
            "{\"preset\":%d,\"octave_transpose\":%d",
            inst->current_preset, inst->octave_transpose);

        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            float val = inst->params[g_shadow_params[i].index];
            offset += snprintf(buf + offset, buf_len - offset,
                ",\"%s\":%.4f", g_shadow_params[i].key, val);
        }

        offset += snprintf(buf + offset, buf_len - offset, "}");
        return offset;
    }

    /* Chain params metadata */
    if (strcmp(key, "chain_params") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset,
            "[{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":9999},"
            "{\"key\":\"octave_transpose\",\"name\":\"Octave\",\"type\":\"int\",\"min\":-3,\"max\":3}");

        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params) && offset < buf_len - 100; i++) {
            offset += snprintf(buf + offset, buf_len - offset,
                ",{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g}",
                g_shadow_params[i].key,
                g_shadow_params[i].name[0] ? g_shadow_params[i].name : g_shadow_params[i].key,
                g_shadow_params[i].type == PARAM_TYPE_INT ? "int" : "float",
                g_shadow_params[i].min_val,
                g_shadow_params[i].max_val);
        }
        offset += snprintf(buf + offset, buf_len - offset, "]");
        return offset;
    }

    return -1;
}

/* =====================================================================
 * Chorus processing (Juno-style)
 * ===================================================================== */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void process_chorus(nsaw_effects_t *fx, float *left, float *right, int frames,
                           float mix, float depth) {
    if (mix < 0.001f) return;

    /* Juno-60 LFO rates */
    const float lfo1_rate = 0.513f;
    const float lfo2_rate = 0.863f;
    const float lfo1_inc = lfo1_rate / 44100.0f;
    const float lfo2_inc = lfo2_rate / 44100.0f;

    /* Juno-60 delay range: 1.66ms to 5.35ms */
    const float delay_center = (1.66f + 5.35f) * 0.5f * 44.1f;  /* ~154 samples */
    const float delay_range = (5.35f - 1.66f) * 0.5f * 44.1f * depth;  /* ~81 samples * depth */

    /* Equal-power crossfade coefficients */
    float dry_gain = sqrtf(1.0f - mix);
    float wet_gain = sqrtf(mix);

    for (int i = 0; i < frames; i++) {
        /* Mono input to chorus */
        float mono_in = (left[i] + right[i]) * 0.5f;

        /* Write to circular buffer */
        fx->chorus_buf[fx->chorus_write_pos] = mono_in;
        fx->chorus_write_pos = (fx->chorus_write_pos + 1) & (CHORUS_BUF_SIZE - 1);

        /* Triangle LFOs (0 to 1 range) */
        float tri1 = 2.0f * fabsf(2.0f * fx->lfo1_phase - 1.0f) - 1.0f;  /* -1 to 1 */
        float tri2 = 2.0f * fabsf(2.0f * fx->lfo2_phase - 1.0f) - 1.0f;

        /* Advance LFO phases */
        fx->lfo1_phase += lfo1_inc;
        if (fx->lfo1_phase >= 1.0f) fx->lfo1_phase -= 1.0f;
        fx->lfo2_phase += lfo2_inc;
        if (fx->lfo2_phase >= 1.0f) fx->lfo2_phase -= 1.0f;

        /* Combined modulation (mode I+II: blend both LFOs) */
        float mod_l = (tri1 + tri2) * 0.5f;
        float mod_r = (-tri1 + tri2) * 0.5f;  /* Inverted LFO1 for stereo */

        /* Delay times in samples */
        float delay_l = delay_center + mod_l * delay_range;
        float delay_r = delay_center + mod_r * delay_range;

        /* Linear interpolation read from circular buffer */
        float read_pos_l = (float)fx->chorus_write_pos - delay_l;
        float read_pos_r = (float)fx->chorus_write_pos - delay_r;
        if (read_pos_l < 0.0f) read_pos_l += CHORUS_BUF_SIZE;
        if (read_pos_r < 0.0f) read_pos_r += CHORUS_BUF_SIZE;

        int idx_l = (int)read_pos_l;
        float frac_l = read_pos_l - idx_l;
        int next_l = (idx_l + 1) & (CHORUS_BUF_SIZE - 1);
        idx_l &= (CHORUS_BUF_SIZE - 1);
        float wet_l = fx->chorus_buf[idx_l] + frac_l * (fx->chorus_buf[next_l] - fx->chorus_buf[idx_l]);

        int idx_r = (int)read_pos_r;
        float frac_r = read_pos_r - idx_r;
        int next_r = (idx_r + 1) & (CHORUS_BUF_SIZE - 1);
        idx_r &= (CHORUS_BUF_SIZE - 1);
        float wet_r = fx->chorus_buf[idx_r] + frac_r * (fx->chorus_buf[next_r] - fx->chorus_buf[idx_r]);

        /* Equal-power mix */
        left[i]  = left[i]  * dry_gain + wet_l * wet_gain;
        right[i] = right[i] * dry_gain + wet_r * wet_gain;
    }
}

/* =====================================================================
 * Delay processing (stereo ping-pong)
 * ===================================================================== */

static void process_delay(nsaw_effects_t *fx, float *left, float *right, int frames,
                          float time_param, float feedback, float mix, float tone_param) {
    if (mix < 0.001f && feedback < 0.001f) return;
    if (!fx->delay_buf_l || !fx->delay_buf_r) return;

    /* Time: exponential mapping 20ms to 1000ms -> 20 * 50^p ms */
    float delay_ms = 20.0f * powf(50.0f, time_param);
    if (delay_ms > 1000.0f) delay_ms = 1000.0f;
    float delay_samples = delay_ms * 44.1f;
    if (delay_samples >= DELAY_MAX_SAMPLES - 1) delay_samples = DELAY_MAX_SAMPLES - 2;

    /* Feedback capped at 95% */
    if (feedback > 0.95f) feedback = 0.95f;

    /* Tone filter: one-pole lowpass, 500Hz to 12kHz */
    float tone_freq = 500.0f * powf(24.0f, tone_param);  /* 500 * 24^p */
    if (tone_freq > 12000.0f) tone_freq = 12000.0f;
    float tone_coeff = 1.0f - expf(-2.0f * (float)M_PI * tone_freq / 44100.0f);

    for (int i = 0; i < frames; i++) {
        /* Read from delay buffer with linear interpolation */
        float read_pos = (float)fx->delay_write_pos - delay_samples;
        if (read_pos < 0.0f) read_pos += DELAY_MAX_SAMPLES;

        int idx = (int)read_pos;
        float frac = read_pos - idx;
        int next = idx + 1;
        if (next >= DELAY_MAX_SAMPLES) next = 0;
        if (idx < 0) idx += DELAY_MAX_SAMPLES;

        float tap_l = fx->delay_buf_l[idx] + frac * (fx->delay_buf_l[next] - fx->delay_buf_l[idx]);
        float tap_r = fx->delay_buf_r[idx] + frac * (fx->delay_buf_r[next] - fx->delay_buf_r[idx]);

        /* Apply tone filter to wet signal */
        fx->tone_z1_l += tone_coeff * (tap_l - fx->tone_z1_l);
        fx->tone_z1_r += tone_coeff * (tap_r - fx->tone_z1_r);
        tap_l = fx->tone_z1_l;
        tap_r = fx->tone_z1_r;

        /* Cross-channel feedback (ping-pong): L feeds R, R feeds L */
        float fb_l = left[i] + tap_r * feedback;
        float fb_r = right[i] + tap_l * feedback;

        /* Soft saturate feedback to prevent runaway */
        if (fb_l > 1.0f || fb_l < -1.0f) fb_l = tanhf(fb_l);
        if (fb_r > 1.0f || fb_r < -1.0f) fb_r = tanhf(fb_r);

        /* Write to delay buffer */
        fx->delay_buf_l[fx->delay_write_pos] = fb_l;
        fx->delay_buf_r[fx->delay_write_pos] = fb_r;
        fx->delay_write_pos++;
        if (fx->delay_write_pos >= DELAY_MAX_SAMPLES) fx->delay_write_pos = 0;

        /* Mix: linear dry/wet */
        left[i]  = left[i]  * (1.0f - mix) + tap_l * mix;
        right[i] = right[i] * (1.0f - mix) + tap_r * mix;
    }
}

/* =====================================================================
 * Render
 * ===================================================================== */

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    nsaw_instance_t *inst = (nsaw_instance_t*)instance;
    if (!inst) {
        memset(out_interleaved_lr, 0, frames * 4);
        return;
    }

    /* Render stereo audio */
    float left_buf[256], right_buf[256];
    if (frames > 256) frames = 256;

    nsaw_engine_render(&inst->engine, left_buf, right_buf, frames);

    /* Apply effects: chorus → delay */
    process_chorus(&inst->fx, left_buf, right_buf, frames,
                   inst->params[P_CHORUS_MIX], inst->params[P_CHORUS_DEPTH]);
    process_delay(&inst->fx, left_buf, right_buf, frames,
                  inst->params[P_DELAY_TIME], inst->params[P_DELAY_FBACK],
                  inst->params[P_DELAY_MIX], inst->params[P_DELAY_TONE]);

    /* Convert to interleaved int16 with soft clipping */
    for (int i = 0; i < frames; i++) {
        float l = left_buf[i];
        float r = right_buf[i];

        /* Soft clip via tanh */
        if (l > 0.9f || l < -0.9f) l = tanhf(l);
        if (r > 0.9f || r < -0.9f) r = tanhf(r);

        int32_t sl = (int32_t)(l * 32767.0f);
        int32_t sr_val = (int32_t)(r * 32767.0f);
        if (sl > 32767) sl = 32767;
        if (sl < -32768) sl = -32768;
        if (sr_val > 32767) sr_val = 32767;
        if (sr_val < -32768) sr_val = -32768;

        out_interleaved_lr[i * 2]     = (int16_t)sl;
        out_interleaved_lr[i * 2 + 1] = (int16_t)sr_val;
    }
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    (void)buf;
    (void)buf_len;
    return 0;
}

/* v2 API table */
static plugin_api_v2_t g_plugin_api_v2;

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;

    memset(&g_plugin_api_v2, 0, sizeof(g_plugin_api_v2));
    g_plugin_api_v2.api_version = MOVE_PLUGIN_API_VERSION_2;
    g_plugin_api_v2.create_instance = v2_create_instance;
    g_plugin_api_v2.destroy_instance = v2_destroy_instance;
    g_plugin_api_v2.on_midi = v2_on_midi;
    g_plugin_api_v2.set_param = v2_set_param;
    g_plugin_api_v2.get_param = v2_get_param;
    g_plugin_api_v2.get_error = v2_get_error;
    g_plugin_api_v2.render_block = v2_render_block;

    return &g_plugin_api_v2;
}
