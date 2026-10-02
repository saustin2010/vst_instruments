/*
 * RaffoSynth DSP Plugin for Move Anything
 *
 * Monophonic synthesizer based on RaffoSynth.
 * Based on RaffoSynth by Nicolas Roulet and Julian Palladino
 * https://github.com/nicoroulet/RaffoSynth
 * Fork: https://github.com/zynthian/moog
 *
 * MIT License - see LICENSE file.
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

/* Moog engine */
#include "moog_engine.h"
}

/* Include param helper */
#include "param_helper.h"

/* Host API reference */
static const host_api_v1_t *g_host = NULL;

/* Logging helper */
static void plugin_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[rf] %s", msg);
        g_host->log(buf);
    }
}

/* =====================================================================
 * Parameter definitions
 * ===================================================================== */

/* Parameter indices */
enum {
    P_OSC1_WAVE = 0,
    P_OSC1_VOLUME,
    P_OSC1_RANGE,
    P_OSC2_WAVE,
    P_OSC2_VOLUME,
    P_OSC2_RANGE,
    P_OSC2_DETUNE,
    P_OSC3_WAVE,
    P_OSC3_VOLUME,
    P_OSC3_RANGE,
    P_OSC3_DETUNE,
    P_OSC4_WAVE,
    P_OSC4_VOLUME,
    P_OSC4_RANGE,
    P_OSC4_DETUNE,
    P_NOISE,
    P_FILTER_CUTOFF,
    P_FILTER_RESONANCE,
    P_FILTER_CONTOUR,
    P_FILTER_KEY_FOLLOW,
    P_AMP_ATTACK,
    P_AMP_DECAY,
    P_AMP_SUSTAIN,
    P_AMP_RELEASE,
    P_FILT_ATTACK,
    P_FILT_DECAY,
    P_FILT_SUSTAIN,
    P_FILT_RELEASE,
    P_GLIDE,
    P_MASTER_VOLUME,
    P_LFO_RATE,
    P_LFO_PITCH,
    P_LFO_FILTER,
    P_MOD_FILTER,
    P_MOD_PITCH,
    P_BEND_RANGE,
    P_VEL_SENS,
    P_COUNT
};

static const param_def_t g_shadow_params[] = {
    /* Oscillator 1 */
    {"osc1_wave",     "Osc1 Wave",     PARAM_TYPE_INT,   P_OSC1_WAVE,     0.0f, 3.0f},
    {"osc1_volume",   "Osc1 Volume",   PARAM_TYPE_FLOAT, P_OSC1_VOLUME,   0.0f, 1.0f},
    {"osc1_range",    "Osc1 Range",    PARAM_TYPE_INT,   P_OSC1_RANGE,   -2.0f, 2.0f},

    /* Oscillator 2 */
    {"osc2_wave",     "Osc2 Wave",     PARAM_TYPE_INT,   P_OSC2_WAVE,     0.0f, 3.0f},
    {"osc2_volume",   "Osc2 Volume",   PARAM_TYPE_FLOAT, P_OSC2_VOLUME,   0.0f, 1.0f},
    {"osc2_range",    "Osc2 Range",    PARAM_TYPE_INT,   P_OSC2_RANGE,   -2.0f, 2.0f},
    {"osc2_detune",   "Osc2 Detune",   PARAM_TYPE_FLOAT, P_OSC2_DETUNE,   0.0f, 1.0f},

    /* Oscillator 3 */
    {"osc3_wave",     "Osc3 Wave",     PARAM_TYPE_INT,   P_OSC3_WAVE,     0.0f, 3.0f},
    {"osc3_volume",   "Osc3 Volume",   PARAM_TYPE_FLOAT, P_OSC3_VOLUME,   0.0f, 1.0f},
    {"osc3_range",    "Osc3 Range",    PARAM_TYPE_INT,   P_OSC3_RANGE,   -2.0f, 2.0f},
    {"osc3_detune",   "Osc3 Detune",   PARAM_TYPE_FLOAT, P_OSC3_DETUNE,   0.0f, 1.0f},

    /* Oscillator 4 */
    {"osc4_wave",     "Osc4 Wave",     PARAM_TYPE_INT,   P_OSC4_WAVE,     0.0f, 3.0f},
    {"osc4_volume",   "Osc4 Volume",   PARAM_TYPE_FLOAT, P_OSC4_VOLUME,   0.0f, 1.0f},
    {"osc4_range",    "Osc4 Range",    PARAM_TYPE_INT,   P_OSC4_RANGE,   -2.0f, 2.0f},
    {"osc4_detune",   "Osc4 Detune",   PARAM_TYPE_FLOAT, P_OSC4_DETUNE,   0.0f, 1.0f},

    /* Noise */
    {"noise",         "Noise",         PARAM_TYPE_FLOAT, P_NOISE,         0.0f, 1.0f},

    /* Filter */
    {"cutoff",        "Cutoff",        PARAM_TYPE_FLOAT, P_FILTER_CUTOFF,     0.0f, 1.0f},
    {"resonance",     "Resonance",     PARAM_TYPE_FLOAT, P_FILTER_RESONANCE,  0.0f, 1.0f},
    {"contour",       "Contour",       PARAM_TYPE_FLOAT, P_FILTER_CONTOUR,    0.0f, 1.0f},
    {"key_follow",    "Key Follow",    PARAM_TYPE_FLOAT, P_FILTER_KEY_FOLLOW, 0.0f, 1.0f},

    /* Amp Envelope */
    {"attack",        "Attack",        PARAM_TYPE_FLOAT, P_AMP_ATTACK,    0.0f, 1.0f},
    {"decay",         "Decay",         PARAM_TYPE_FLOAT, P_AMP_DECAY,     0.0f, 1.0f},
    {"sustain",       "Sustain",       PARAM_TYPE_FLOAT, P_AMP_SUSTAIN,   0.0f, 1.0f},
    {"release",       "Release",       PARAM_TYPE_FLOAT, P_AMP_RELEASE,   0.0f, 1.0f},

    /* Filter Envelope */
    {"f_attack",      "F Attack",      PARAM_TYPE_FLOAT, P_FILT_ATTACK,   0.0f, 1.0f},
    {"f_decay",       "F Decay",       PARAM_TYPE_FLOAT, P_FILT_DECAY,    0.0f, 1.0f},
    {"f_sustain",     "F Sustain",     PARAM_TYPE_FLOAT, P_FILT_SUSTAIN,  0.0f, 1.0f},
    {"f_release",     "F Release",     PARAM_TYPE_FLOAT, P_FILT_RELEASE,  0.0f, 1.0f},

    /* Performance */
    {"glide",         "Glide",         PARAM_TYPE_FLOAT, P_GLIDE,         0.0f, 1.0f},
    {"volume",        "Volume",        PARAM_TYPE_FLOAT, P_MASTER_VOLUME, 0.0f, 1.0f},

    /* LFO */
    {"lfo_rate",      "LFO Rate",      PARAM_TYPE_FLOAT, P_LFO_RATE,      0.0f, 1.0f},
    {"lfo_pitch",     "LFO>Pitch",     PARAM_TYPE_FLOAT, P_LFO_PITCH,     0.0f, 1.0f},
    {"lfo_filter",    "LFO>Filter",    PARAM_TYPE_FLOAT, P_LFO_FILTER,    0.0f, 1.0f},

    /* Modulation */
    {"mod_filter",    "Mod>Filter",    PARAM_TYPE_FLOAT, P_MOD_FILTER,    0.0f, 1.0f},
    {"mod_pitch",     "Mod>Pitch",     PARAM_TYPE_FLOAT, P_MOD_PITCH,     0.0f, 1.0f},
    {"bend_range",    "Bend Range",    PARAM_TYPE_FLOAT, P_BEND_RANGE,    0.0f, 1.0f},
    {"vel_sens",      "Vel Sens",      PARAM_TYPE_FLOAT, P_VEL_SENS,      0.0f, 1.0f},
};

/* =====================================================================
 * Preset system
 * ===================================================================== */

#define MAX_PRESETS 32

struct MoogPreset {
    char name[32];
    float params[P_COUNT];
};

/* Factory presets
 * Presets 1-9 converted from LV2 presets by Brian at linuxsynths.com
 * Original presets: https://linuxsynths.com/RaffosynthPatchesDemos/RaffosynthPatches01.tar.gz
 *
 * Parameter order matches the enum:
 *   osc1: wave, volume, range                     (3 values)
 *   osc2: wave, volume, range, detune             (4 values)
 *   osc3: wave, volume, range, detune             (4 values)
 *   osc4: wave, volume, range, detune             (4 values)
 *   noise                                         (1 value)
 *   filter: cutoff, resonance, contour, key_follow (4 values)
 *   amp env: attack, decay, sustain, release       (4 values)
 *   filt env: attack, decay, sustain, release      (4 values)
 *   glide, master_volume                           (2 values)
 *   lfo: rate, pitch_depth, filter_depth           (3 values)
 *   mod_filter, mod_pitch, bend_range, vel_sens    (4 values)
 *                                          Total: 37 (P_COUNT)
 */
static const MoogPreset g_factory_presets[] = {
    /* 0: Init */
    {"Init", {
        1, 0.7f, -1,  /* osc1: wave, vol, range */
        1, 0.5f, -1, 0.48f,  /* osc2: wave, vol, range, detune */
        1, 0.4f, -2, 0.52f,  /* osc3: wave, vol, range, detune */
        0, 0.0f, 0, 0.5f,  /* osc4: off */
        0.0f,  /* noise */
        0.7f, 0.2f, 0.3f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.01f, 0.3f, 0.7f, 0.2f,  /* amp: A, D, S, R */
        0.01f, 0.3f, 0.3f, 0.2f,  /* filt: A, D, S, R */
        0.0f, 0.7f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 1: Soloist - single bright saw */
    {"Soloist", {
        1, 0.7f, -1,  /* osc1: wave, vol, range */
        0, 0.0f, 0, 0.64f,  /* osc2: off */
        1, 0.0f, -1, 0.5f,  /* osc3: off */
        0, 0.0f, -2, 0.66f,  /* osc4: off */
        0.0f,  /* noise */
        0.886f, 1.0f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.178f, 0.573f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.051f, 0.402f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 2: Duet - saw + triangle */
    {"Duet", {
        1, 0.7f, -1,  /* osc1: wave, vol, range */
        0, 0.863f, 0, 0.64f,  /* osc2: wave, vol, range, detune */
        1, 0.0f, -1, 0.5f,  /* osc3: off */
        0, 0.0f, -2, 0.66f,  /* osc4: off */
        0.0f,  /* noise */
        0.886f, 1.0f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.178f, 0.573f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.051f, 0.402f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 3: Trio - saw + triangle + square */
    {"Trio", {
        1, 0.7f, 0,  /* osc1: wave, vol, range */
        0, 0.863f, 1, 0.64f,  /* osc2: wave, vol, range, detune */
        2, 0.151f, -1, 0.5f,  /* osc3: wave, vol, range, detune */
        0, 0.0f, -2, 0.66f,  /* osc4: off */
        0.0f,  /* noise */
        0.886f, 0.014f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.178f, 0.573f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.051f, 0.402f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 4: Quartet - all four oscillators */
    {"Quartet", {
        1, 0.7f, 0,  /* osc1: wave, vol, range */
        0, 0.863f, 1, 0.64f,  /* osc2: wave, vol, range, detune */
        2, 0.151f, -1, 0.5f,  /* osc3: wave, vol, range, detune */
        3, 0.466f, 1, 0.66f,  /* osc4: pulse, vol, range, detune */
        0.0f,  /* noise */
        0.886f, 0.014f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.178f, 0.573f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.051f, 0.402f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 5: SonataFlair - detuned three-saw, dark filter */
    {"SonataFlair", {
        1, 0.904f, -1,  /* osc1: wave, vol, range */
        1, 0.644f, -1, 0.32f,  /* osc2: wave, vol, range, detune */
        1, 0.795f, -1, 0.5f,  /* osc3: wave, vol, range, detune */
        0, 0.0f, -2, 0.34f,  /* osc4: off */
        0.0f,  /* noise */
        0.527f, 0.521f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.195f, 0.445f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.0f, 0.466f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 6: SonataFlairSub - SonataFlair with sub oscillator */
    {"SonataFlairSub", {
        1, 0.904f, -1,  /* osc1: wave, vol, range */
        1, 0.644f, -1, 0.32f,  /* osc2: wave, vol, range, detune */
        1, 0.795f, -1, 0.5f,  /* osc3: wave, vol, range, detune */
        1, 0.767f, -2, 0.34f,  /* osc4: saw, vol, range, detune */
        0.0f,  /* noise */
        0.527f, 0.521f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.195f, 0.445f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.0f, 0.466f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 7: AngrySweep - resonant filter sweep */
    {"AngrySweep", {
        1, 0.7f, -1,  /* osc1: wave, vol, range */
        2, 0.644f, -1, 0.32f,  /* osc2: wave, vol, range, detune */
        1, 0.795f, -1, 0.18f,  /* osc3: wave, vol, range, detune */
        0, 0.0f, -2, 0.34f,  /* osc4: off */
        0.0f,  /* noise */
        0.629f, 1.0f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.178f, 0.573f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.051f, 0.402f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 8: SquarePulse - pulse + square, resonant */
    {"SquarePulse", {
        3, 0.7f, -1,  /* osc1: wave, vol, range */
        2, 0.644f, -1, 0.64f,  /* osc2: wave, vol, range, detune */
        1, 0.0f, -1, 0.5f,  /* osc3: off */
        0, 0.0f, -2, 0.66f,  /* osc4: off */
        0.0f,  /* noise */
        0.704f, 0.973f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.178f, 0.573f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.051f, 0.402f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 9: Whisper - soft resonant three-osc */
    {"Whisper", {
        1, 0.7f, -1,  /* osc1: wave, vol, range */
        2, 0.644f, -1, 0.32f,  /* osc2: wave, vol, range, detune */
        1, 0.795f, -1, 0.18f,  /* osc3: wave, vol, range, detune */
        0, 0.0f, -2, 0.34f,  /* osc4: off */
        0.0f,  /* noise */
        0.718f, 1.0f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.301f, 0.723f, 0.282f,  /* amp: A, D, S, R */
        0.178f, 0.546f, 0.16f, 0.316f,  /* filt: A, D, S, R */
        0.051f, 0.293f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 10: CookedPasta - percussive pulses */
    {"CookedPasta", {
        3, 0.7f, -1,  /* osc1: wave, vol, range */
        3, 0.644f, -1, 1.0f,  /* osc2: wave, vol, range, detune */
        0, 0.795f, 1, 1.0f,  /* osc3: wave, vol, range, detune */
        0, 0.0f, -2, 1.0f,  /* osc4: off, detune clamped */
        0.0f,  /* noise */
        0.73f, 0.849f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.528f, 0.0f, 0.374f,  /* amp: A, D, S, R */
        0.084f, 0.315f, 0.27f, 0.319f,  /* filt: A, D, S, R */
        0.035f, 0.293f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 11: CookedPasta2 - sustained pulses */
    {"CookedPasta2", {
        3, 0.699f, -1,  /* osc1: wave, vol, range */
        3, 0.644f, -1, 1.0f,  /* osc2: wave, vol, range, detune */
        0, 0.795f, 1, 1.0f,  /* osc3: wave, vol, range, detune */
        0, 0.0f, -2, 1.0f,  /* osc4: off, detune clamped */
        0.0f,  /* noise */
        0.73f, 0.849f, 0.5f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.027f, 0.528f, 0.41f, 0.374f,  /* amp: A, D, S, R */
        0.084f, 0.315f, 0.27f, 0.319f,  /* filt: A, D, S, R */
        0.035f, 0.247f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 12: Classic Bass */
    {"Classic Bass", {
        1, 0.9f, -1,  /* osc1: wave, vol, range */
        1, 0.7f, -1, 0.53f,  /* osc2: wave, vol, range, detune */
        0, 0.0f, 0, 0.5f,  /* osc3: off */
        0, 0.0f, 0, 0.5f,  /* osc4: off */
        0.0f,  /* noise */
        0.35f, 0.5f, 0.6f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.0f, 0.2f, 0.7f, 0.1f,  /* amp: A, D, S, R */
        0.0f, 0.15f, 0.0f, 0.1f,  /* filt: A, D, S, R */
        0.0f, 0.7f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.5f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
    /* 13: Sub Bass */
    {"Sub Bass", {
        0, 0.9f, -2,  /* osc1: wave, vol, range */
        1, 0.25f, -1, 0.5f,  /* osc2: wave, vol, range, detune */
        0, 0.0f, 0, 0.5f,  /* osc3: off */
        0, 0.0f, 0, 0.5f,  /* osc4: off */
        0.0f,  /* noise */
        0.25f, 0.0f, 0.3f, 0.0f,  /* filter: cutoff, reso, contour, key_follow */
        0.0f, 0.4f, 0.9f, 0.2f,  /* amp: A, D, S, R */
        0.0f, 0.2f, 0.0f, 0.2f,  /* filt: A, D, S, R */
        0.0f, 0.8f,  /* glide, volume */
        0.3f, 0.0f, 0.0f,  /* lfo: rate, pitch, filter */
        0.5f, 0.5f, 0.167f, 0.2f  /* mod_filt, mod_pitch, bend, vel_sens */
    }},
};

#define FACTORY_PRESET_COUNT (int)(sizeof(g_factory_presets) / sizeof(g_factory_presets[0]))

/* =====================================================================
 * Instance
 * ===================================================================== */

typedef struct {
    char module_dir[256];
    moog_engine_t engine;
    int current_preset;
    int preset_count;
    char preset_name[64];
    float params[P_COUNT];
    MoogPreset presets[MAX_PRESETS];
    float output_gain;
    int octave_transpose;
} moog_instance_t;

/* Forward declarations */
static void apply_params_to_engine(moog_instance_t *inst);
static void apply_preset(moog_instance_t *inst, int preset_idx);

/* =====================================================================
 * Parameter application
 * ===================================================================== */

static void apply_params_to_engine(moog_instance_t *inst) {
    moog_engine_t *e = &inst->engine;

    e->osc_wave[0]       = (moog_wave_t)(int)inst->params[P_OSC1_WAVE];
    e->osc_volume[0]     = inst->params[P_OSC1_VOLUME];
    e->osc_range[0]      = (int)inst->params[P_OSC1_RANGE];

    e->osc_wave[1]       = (moog_wave_t)(int)inst->params[P_OSC2_WAVE];
    e->osc_volume[1]     = inst->params[P_OSC2_VOLUME];
    e->osc_range[1]      = (int)inst->params[P_OSC2_RANGE];
    e->osc2_detune       = inst->params[P_OSC2_DETUNE];

    e->osc_wave[2]       = (moog_wave_t)(int)inst->params[P_OSC3_WAVE];
    e->osc_volume[2]     = inst->params[P_OSC3_VOLUME];
    e->osc_range[2]      = (int)inst->params[P_OSC3_RANGE];
    e->osc3_detune       = inst->params[P_OSC3_DETUNE];

    e->osc_wave[3]       = (moog_wave_t)(int)inst->params[P_OSC4_WAVE];
    e->osc_volume[3]     = inst->params[P_OSC4_VOLUME];
    e->osc_range[3]      = (int)inst->params[P_OSC4_RANGE];
    e->osc4_detune       = inst->params[P_OSC4_DETUNE];

    e->noise_volume      = inst->params[P_NOISE];

    e->filter_cutoff     = inst->params[P_FILTER_CUTOFF];
    e->filter_resonance  = inst->params[P_FILTER_RESONANCE];
    e->filter_contour    = inst->params[P_FILTER_CONTOUR];
    e->filter_key_follow = inst->params[P_FILTER_KEY_FOLLOW];

    e->amp_attack        = inst->params[P_AMP_ATTACK];
    e->amp_decay         = inst->params[P_AMP_DECAY];
    e->amp_sustain       = inst->params[P_AMP_SUSTAIN];
    e->amp_release       = inst->params[P_AMP_RELEASE];

    e->filt_attack       = inst->params[P_FILT_ATTACK];
    e->filt_decay        = inst->params[P_FILT_DECAY];
    e->filt_sustain      = inst->params[P_FILT_SUSTAIN];
    e->filt_release      = inst->params[P_FILT_RELEASE];

    e->glide             = inst->params[P_GLIDE];
    e->master_volume     = inst->params[P_MASTER_VOLUME];

    e->lfo_rate          = inst->params[P_LFO_RATE];
    e->lfo_depth_pitch   = inst->params[P_LFO_PITCH];
    e->lfo_depth_filter  = inst->params[P_LFO_FILTER];

    e->mod_to_filter     = inst->params[P_MOD_FILTER];
    e->mod_to_pitch      = inst->params[P_MOD_PITCH];
    e->bend_range        = inst->params[P_BEND_RANGE];
    e->velocity_sensitivity = inst->params[P_VEL_SENS];
}

static void apply_preset(moog_instance_t *inst, int preset_idx) {
    if (preset_idx < 0 || preset_idx >= inst->preset_count) return;

    MoogPreset *p = &inst->presets[preset_idx];
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

    moog_instance_t *inst = (moog_instance_t*)calloc(1, sizeof(moog_instance_t));
    if (!inst) return NULL;

    strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);
    inst->output_gain = 0.35f;

    /* Initialize engine */
    moog_engine_init(&inst->engine);

    /* Load factory presets */
    inst->preset_count = FACTORY_PRESET_COUNT;
    for (int i = 0; i < FACTORY_PRESET_COUNT; i++) {
        memcpy(&inst->presets[i], &g_factory_presets[i], sizeof(MoogPreset));
    }

    /* Apply first preset */
    apply_preset(inst, 0);

    plugin_log("RaffoSynth v2: Instance created");
    return inst;
}

static void v2_destroy_instance(void *instance) {
    moog_instance_t *inst = (moog_instance_t*)instance;
    if (!inst) return;
    free(inst);
    plugin_log("RaffoSynth v2: Instance destroyed");
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    moog_instance_t *inst = (moog_instance_t*)instance;
    if (!inst || len < 2) return;
    (void)source;

    uint8_t status = msg[0] & 0xF0;
    uint8_t data1 = msg[1];
    uint8_t data2 = (len > 2) ? msg[2] : 0;

    switch (status) {
        case 0x90:
            if (data2 > 0) {
                moog_engine_note_on(&inst->engine, data1, data2 / 127.0f);
            } else {
                moog_engine_note_off(&inst->engine, data1);
            }
            break;
        case 0x80:
            moog_engine_note_off(&inst->engine, data1);
            break;
        case 0xB0:
            switch (data1) {
                case 1: /* Mod wheel */
                    moog_engine_mod_wheel(&inst->engine, data2 / 127.0f);
                    break;
                case 64: /* Sustain */
                    break;
                case 123: /* All notes off */
                    moog_engine_all_notes_off(&inst->engine);
                    break;
            }
            break;
        case 0xE0: { /* Pitch bend */
            int bend = ((data2 << 7) | data1) - 8192;
            moog_engine_pitch_bend(&inst->engine, bend / 8192.0f);
            break;
        }
        case 0xD0: /* Channel aftertouch -> filter cutoff modulation */
            /* Map aftertouch to slight filter opening */
            break;
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    moog_instance_t *inst = (moog_instance_t*)instance;
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
        moog_engine_all_notes_off(&inst->engine);
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
    moog_instance_t *inst = (moog_instance_t*)instance;
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
        return snprintf(buf, buf_len, "RaffoSynth");
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
                    "\"knobs\":[\"cutoff\",\"resonance\",\"contour\",\"attack\",\"decay\",\"sustain\",\"release\",\"octave_transpose\"],"
                    "\"params\":[]"
                "},"
                "\"main\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"contour\",\"attack\",\"decay\",\"sustain\",\"release\",\"octave_transpose\"],"
                    "\"params\":["
                        "{\"level\":\"osc1\",\"label\":\"Oscillator 1\"},"
                        "{\"level\":\"osc2\",\"label\":\"Oscillator 2\"},"
                        "{\"level\":\"osc3\",\"label\":\"Oscillator 3\"},"
                        "{\"level\":\"osc4\",\"label\":\"Oscillator 4\"},"
                        "{\"level\":\"mixer\",\"label\":\"Mixer\"},"
                        "{\"level\":\"filter\",\"label\":\"Filter\"},"
                        "{\"level\":\"filt_env\",\"label\":\"Filter Env\"},"
                        "{\"level\":\"amp_env\",\"label\":\"Amp Env\"},"
                        "{\"level\":\"lfo\",\"label\":\"LFO\"},"
                        "{\"level\":\"performance\",\"label\":\"Performance\"}"
                    "]"
                "},"
                "\"osc1\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"osc1_wave\",\"osc1_volume\",\"osc1_range\"],"
                    "\"params\":[\"osc1_wave\",\"osc1_volume\",\"osc1_range\"]"
                "},"
                "\"osc2\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"osc2_wave\",\"osc2_volume\",\"osc2_range\",\"osc2_detune\"],"
                    "\"params\":[\"osc2_wave\",\"osc2_volume\",\"osc2_range\",\"osc2_detune\"]"
                "},"
                "\"osc3\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"osc3_wave\",\"osc3_volume\",\"osc3_range\",\"osc3_detune\"],"
                    "\"params\":[\"osc3_wave\",\"osc3_volume\",\"osc3_range\",\"osc3_detune\"]"
                "},"
                "\"osc4\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"osc4_wave\",\"osc4_volume\",\"osc4_range\",\"osc4_detune\"],"
                    "\"params\":[\"osc4_wave\",\"osc4_volume\",\"osc4_range\",\"osc4_detune\"]"
                "},"
                "\"mixer\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"osc1_volume\",\"osc2_volume\",\"osc3_volume\",\"osc4_volume\",\"noise\",\"volume\"],"
                    "\"params\":[\"osc1_volume\",\"osc2_volume\",\"osc3_volume\",\"osc4_volume\",\"noise\",\"volume\"]"
                "},"
                "\"filter\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"contour\",\"key_follow\"],"
                    "\"params\":[\"cutoff\",\"resonance\",\"contour\",\"key_follow\"]"
                "},"
                "\"filt_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\"],"
                    "\"params\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\"]"
                "},"
                "\"amp_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":[\"attack\",\"decay\",\"sustain\",\"release\"]"
                "},"
                "\"lfo\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"lfo_rate\",\"lfo_pitch\",\"lfo_filter\"],"
                    "\"params\":[\"lfo_rate\",\"lfo_pitch\",\"lfo_filter\"]"
                "},"
                "\"performance\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"glide\",\"mod_filter\",\"mod_pitch\",\"bend_range\",\"vel_sens\",\"octave_transpose\"],"
                    "\"params\":[\"glide\",\"mod_filter\",\"mod_pitch\",\"bend_range\",\"vel_sens\",\"octave_transpose\"]"
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

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    moog_instance_t *inst = (moog_instance_t*)instance;
    if (!inst) {
        memset(out_interleaved_lr, 0, frames * 4);
        return;
    }

    /* Render mono audio */
    float mono_buf[256];
    if (frames > 256) frames = 256;

    moog_engine_render(&inst->engine, mono_buf, frames);

    /* Convert to stereo int16 with soft clipping */
    float gain = inst->output_gain;
    for (int i = 0; i < frames; i++) {
        float sample = mono_buf[i] * gain;

        /* Soft clip via tanh to avoid harsh digital clipping */
        if (sample > 0.9f || sample < -0.9f) {
            sample = tanhf(sample);
        }

        int32_t s = (int32_t)(sample * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;

        out_interleaved_lr[i * 2]     = (int16_t)s;
        out_interleaved_lr[i * 2 + 1] = (int16_t)s;
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
