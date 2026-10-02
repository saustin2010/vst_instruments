/*
 * Braids Macro Oscillator DSP Plugin for Move Anything
 *
 * 47 synthesis algorithms based on Mutable Instruments Braids.
 * MIT License - see LICENSE file.
 *
 * Original DSP code by Emilie Gillet (Mutable Instruments)
 * https://github.com/pichenettes/eurorack/tree/master/braids
 *
 * V2 API only - instance-based for multi-instance support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>

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
}

/* Braids engine */
#include "braids/macro_oscillator.h"
#include "braids/envelope.h"
#include "braids/svf.h"

/* Constants */
#define MAX_VOICES 4
#define BRAIDS_BLOCK_SIZE 24

/*
 * Pitch correction for 44.1kHz operation.
 * Braids lookup tables are calibrated for 96kHz.
 * Offset = 12 * 128 * log2(96000/44100) = ~1724
 */
#define PITCH_CORRECTION 1724

/*
 * Envelope rate scaling for 44.1kHz operation.
 * Braids envelope LUTs are calibrated for 96kHz.
 * Scale factor = 44100/96000 ≈ 0.459375
 */
#define ENV_RATE_SCALE (44100.0f / 96000.0f)

/* =====================================================================
 * Simple ADSR envelope - replaces Braids' AR-only envelope
 * ===================================================================== */

struct SimpleADSR {
    enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
    Stage stage;
    float level;
    float attack_rate, decay_rate, sustain_level, release_rate;

    void init() { stage = IDLE; level = 0.0f; }

    void set_params(float a, float d, float s, float r) {
        auto time_to_rate = [](float p) -> float {
            float t = 0.001f + p * p * 10.0f;
            return 1.0f / (t * 44100.0f);
        };
        attack_rate = time_to_rate(a);
        decay_rate = time_to_rate(d);
        sustain_level = s;
        release_rate = time_to_rate(r);
    }

    void gate_on() { stage = ATTACK; }
    void gate_off() { if (stage != IDLE) stage = RELEASE; }
    bool is_active() const { return stage != IDLE; }

    float process() {
        switch (stage) {
            case ATTACK:
                level += attack_rate;
                if (level >= 1.0f) { level = 1.0f; stage = DECAY; }
                break;
            case DECAY:
                level -= decay_rate;
                if (level <= sustain_level) { level = sustain_level; stage = SUSTAIN; }
                break;
            case SUSTAIN:
                level = sustain_level;
                break;
            case RELEASE:
                level -= release_rate;
                if (level <= 0.0f) { level = 0.0f; stage = IDLE; }
                break;
            case IDLE:
                level = 0.0f;
                break;
        }
        return level;
    }
};

/* Shape names for display */
static const char* g_shape_names[] = {
    "CSAW", "MORPH", "/\\-_", "SINE^",  "BUZZ",
    "SQR<", "SAW<",  "SQsync", "SWsync",
    "3xSAW", "3xSQR", "3xTRI", "3xSIN", "3xRNG",
    "SWARM", "COMB",  "TOY",
    "ZLPF",  "ZPKF",  "ZBPF",  "ZHPF",
    "VOSIM", "VOWL",  "V.FOF",
    "HARM",
    "FM",    "FBFM",  "WTFM",
    "PLUK",  "BOWD",  "BLOW",  "FLUT",
    "BELL",  "DRUM",  "KICK",  "CYMB",  "SNAR",
    "WTBL",  "WMAP",  "WLIN",  "WTx4",
    "NOIS",  "TWNQ",  "CLKN",  "GRN",   "PART",
    "QPSK"
};
#define NUM_SHAPES ((int)braids::MACRO_OSC_SHAPE_LAST_ACCESSIBLE_FROM_META + 1)

/* Preset system */
#define MAX_PRESETS 64

/* Host API reference */
static const host_api_v1_t *g_host = NULL;

/* Parameter helper */
#include "param_helper.h"

/* Parameter indices for our values array */
enum BraidsParam {
    PARAM_ENGINE = 0,
    PARAM_TIMBRE,
    PARAM_COLOR,
    PARAM_ATTACK,
    PARAM_DECAY,
    PARAM_SUSTAIN,
    PARAM_RELEASE,
    PARAM_FM,
    PARAM_CUTOFF,
    PARAM_RESONANCE,
    PARAM_FILT_ENV,
    PARAM_F_ATTACK,
    PARAM_F_DECAY,
    PARAM_F_SUSTAIN,
    PARAM_F_RELEASE,
    PARAM_VOLUME,
    PARAM_COUNT
};

struct BraidsPreset {
    char name[64];
    float params[PARAM_COUNT];
    int octave_transpose;
};

/*
 * The trailing three fields are the parameter visualisation (docs/MODULES.md,
 * "Parameter visualisations"). Three groups here, each one the DSP really
 * drives rather than a set of similarly-named knobs: the amp envelope, the
 * filter envelope, and the filter's cutoff+resonance pair.
 *
 * Two params are left undeclared on purpose. `filt_env` is how much the filter
 * envelope moves the cutoff — a modulation depth, not a stage of either
 * envelope nor half of the filter pair, so no role fits it. `engine` is 47
 * cryptic algorithm abbreviations, and no waveform silhouette can honestly
 * stand for a macro-oscillator algorithm. Both fall through to plain knob
 * dials, which is the right answer; see the migration guide's "if you are
 * unsure whether a group is real, leave it undeclared."
 *
 * NOTE: the filter level's knob ORDER in ui_hierarchy is load-bearing for the
 * two filter graphics — see the comment there before reordering it.
 */
static const param_def_t g_shadow_params[] = {
    {"engine",    "Engine",    PARAM_TYPE_INT,   PARAM_ENGINE,    0.0f, (float)(NUM_SHAPES - 1), NULL,         NULL,        NULL},
    {"timbre",    "Timbre",    PARAM_TYPE_FLOAT, PARAM_TIMBRE,    0.0f, 1.0f, NULL,         NULL,        NULL},
    {"color",     "Color",     PARAM_TYPE_FLOAT, PARAM_COLOR,     0.0f, 1.0f, NULL,         NULL,        NULL},
    {"attack",    "Attack",    PARAM_TYPE_FLOAT, PARAM_ATTACK,    0.0f, 1.0f, "amp",        "attack",    NULL},
    {"decay",     "Decay",     PARAM_TYPE_FLOAT, PARAM_DECAY,     0.0f, 1.0f, "amp",        "decay",     NULL},
    {"sustain",   "Sustain",   PARAM_TYPE_FLOAT, PARAM_SUSTAIN,   0.0f, 1.0f, "amp",        "sustain",   NULL},
    {"release",   "Release",   PARAM_TYPE_FLOAT, PARAM_RELEASE,   0.0f, 1.0f, "amp",        "release",   NULL},
    {"fm",        "FM",        PARAM_TYPE_FLOAT, PARAM_FM,        0.0f, 1.0f, NULL,         NULL,        NULL},
    {"cutoff",    "Cutoff",    PARAM_TYPE_FLOAT, PARAM_CUTOFF,    0.0f, 1.0f, "filter",     "cutoff",    NULL},
    {"resonance", "Resonance", PARAM_TYPE_FLOAT, PARAM_RESONANCE, 0.0f, 1.0f, "filter",     "resonance", NULL},
    {"filt_env",  "Filt Env",  PARAM_TYPE_FLOAT, PARAM_FILT_ENV,  0.0f, 1.0f, NULL,         NULL,        NULL},
    {"f_attack",  "F.Attack",  PARAM_TYPE_FLOAT, PARAM_F_ATTACK,  0.0f, 1.0f, "filter_env", "attack",    NULL},
    {"f_decay",   "F.Decay",   PARAM_TYPE_FLOAT, PARAM_F_DECAY,   0.0f, 1.0f, "filter_env", "decay",     NULL},
    {"f_sustain", "F.Sustain", PARAM_TYPE_FLOAT, PARAM_F_SUSTAIN, 0.0f, 1.0f, "filter_env", "sustain",   NULL},
    {"f_release", "F.Release", PARAM_TYPE_FLOAT, PARAM_F_RELEASE, 0.0f, 1.0f, "filter_env", "release",   NULL},
    {"volume",    "Volume",    PARAM_TYPE_FLOAT, PARAM_VOLUME,    0.0f, 1.0f, NULL,         NULL,        "fader"},
};

/* =====================================================================
 * Voice structure - one Braids oscillator per voice
 * ===================================================================== */

struct BraidsVoice {
    braids::MacroOscillator osc;
    SimpleADSR amp_env;
    SimpleADSR filt_env;
    braids::Svf svf;
    int16_t osc_buffer[BRAIDS_BLOCK_SIZE];
    uint8_t sync_buffer[BRAIDS_BLOCK_SIZE];
    int note;
    int velocity;
    int active;
    int gate;
    int age;  /* For voice stealing - higher = older */
};

/* =====================================================================
 * Instance structure
 * ===================================================================== */

typedef struct {
    char module_dir[256];
    BraidsVoice voices[MAX_VOICES];
    float params[PARAM_COUNT];
    int octave_transpose;
    int voice_counter;  /* For age tracking */

    /* Preset system */
    BraidsPreset presets[MAX_PRESETS];
    int preset_count;
    int current_preset;
    char preset_name[64];

    /* Render state: accumulate Braids 24-sample blocks into Move 128-sample blocks */
    int16_t render_buffer[MOVE_FRAMES_PER_BLOCK * 2]; /* stereo output */
} braids_instance_t;

/* =====================================================================
 * Utility functions
 * ===================================================================== */

static void plugin_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[braids] %s", msg);
        g_host->log(buf);
    }
}

/* Convert MIDI note to Braids pitch (128ths of semitone, C3 = 60*128 = 7680) */
static int16_t note_to_pitch(int note) {
    int16_t pitch = (int16_t)(note * 128);
    /* Apply pitch correction for 44.1kHz (tables calibrated for 96kHz) */
    pitch += PITCH_CORRECTION;
    return pitch;
}

/* =====================================================================
 * Voice management
 * ===================================================================== */

static int find_free_voice(braids_instance_t *inst) {
    /* First: find inactive voice */
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!inst->voices[i].active) return i;
    }
    /* Second: steal oldest voice */
    int oldest = 0;
    int oldest_age = inst->voices[0].age;
    for (int i = 1; i < MAX_VOICES; i++) {
        if (inst->voices[i].age < oldest_age) {
            oldest_age = inst->voices[i].age;
            oldest = i;
        }
    }
    return oldest;
}

static int find_voice_for_note(braids_instance_t *inst, int note) {
    /* Prefer gated voice (still held) over releasing voice */
    int releasing = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (inst->voices[i].active && inst->voices[i].note == note) {
            if (inst->voices[i].gate) return i;
            releasing = i;
        }
    }
    return releasing;
}

/* =====================================================================
 * Plugin API v2
 * ===================================================================== */

/* Helper to extract a JSON number value by key */
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

/* Helper to extract a JSON string value by key */
static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    pos += strlen(search);
    while (*pos == ' ') pos++;
    if (*pos != '"') return -1;
    pos++; /* skip opening quote */
    int i = 0;
    while (*pos && *pos != '"' && i < out_len - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return i;
}

/* Apply preset to instance parameters */
static void v2_apply_preset(braids_instance_t *inst, int preset_idx) {
    if (preset_idx < 0 || preset_idx >= inst->preset_count) return;

    BraidsPreset *p = &inst->presets[preset_idx];
    snprintf(inst->preset_name, sizeof(inst->preset_name), "%s", p->name);

    for (int i = 0; i < PARAM_COUNT; i++) {
        inst->params[i] = p->params[i];
    }
    inst->octave_transpose = p->octave_transpose;
}

/* Load a single .braids preset file */
static int load_braids_preset(braids_instance_t *inst, const char *path) {
    if (inst->preset_count >= MAX_PRESETS) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 4096) { fclose(f); return -1; }

    char *data = (char*)malloc(size + 1);
    if (!data) { fclose(f); return -1; }
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);

    BraidsPreset *p = &inst->presets[inst->preset_count];
    memset(p, 0, sizeof(BraidsPreset));

    /* Parse name */
    if (json_get_string(data, "name", p->name, sizeof(p->name)) < 0) {
        snprintf(p->name, sizeof(p->name), "Preset %d", inst->preset_count);
    }

    /* Parse engine (string name or number) */
    char engine_str[32];
    if (json_get_string(data, "engine", engine_str, sizeof(engine_str)) >= 0) {
        p->params[PARAM_ENGINE] = 0;
        for (int i = 0; i < NUM_SHAPES; i++) {
            if (strcmp(engine_str, g_shape_names[i]) == 0) {
                p->params[PARAM_ENGINE] = (float)i;
                break;
            }
        }
    } else {
        float fval;
        if (json_get_number(data, "engine", &fval) == 0) {
            p->params[PARAM_ENGINE] = fval;
        }
    }

    /* Parse float params */
    float fval;
    if (json_get_number(data, "timbre", &fval) == 0) p->params[PARAM_TIMBRE] = fval;
    else p->params[PARAM_TIMBRE] = 0.5f;
    if (json_get_number(data, "color", &fval) == 0) p->params[PARAM_COLOR] = fval;
    else p->params[PARAM_COLOR] = 0.5f;
    if (json_get_number(data, "attack", &fval) == 0) p->params[PARAM_ATTACK] = fval;
    else p->params[PARAM_ATTACK] = 0.0f;
    if (json_get_number(data, "decay", &fval) == 0) p->params[PARAM_DECAY] = fval;
    else p->params[PARAM_DECAY] = 0.5f;
    if (json_get_number(data, "sustain", &fval) == 0) p->params[PARAM_SUSTAIN] = fval;
    else p->params[PARAM_SUSTAIN] = 1.0f;
    if (json_get_number(data, "release", &fval) == 0) p->params[PARAM_RELEASE] = fval;
    else p->params[PARAM_RELEASE] = 0.3f;
    if (json_get_number(data, "fm", &fval) == 0) p->params[PARAM_FM] = fval;
    else p->params[PARAM_FM] = 0.0f;
    if (json_get_number(data, "cutoff", &fval) == 0) p->params[PARAM_CUTOFF] = fval;
    else p->params[PARAM_CUTOFF] = 1.0f;
    if (json_get_number(data, "resonance", &fval) == 0) p->params[PARAM_RESONANCE] = fval;
    else p->params[PARAM_RESONANCE] = 0.0f;
    if (json_get_number(data, "filt_env", &fval) == 0) p->params[PARAM_FILT_ENV] = fval;
    else p->params[PARAM_FILT_ENV] = 0.0f;
    if (json_get_number(data, "f_attack", &fval) == 0) p->params[PARAM_F_ATTACK] = fval;
    else p->params[PARAM_F_ATTACK] = 0.0f;
    if (json_get_number(data, "f_decay", &fval) == 0) p->params[PARAM_F_DECAY] = fval;
    else p->params[PARAM_F_DECAY] = 0.3f;
    if (json_get_number(data, "f_sustain", &fval) == 0) p->params[PARAM_F_SUSTAIN] = fval;
    else p->params[PARAM_F_SUSTAIN] = 0.0f;
    if (json_get_number(data, "f_release", &fval) == 0) p->params[PARAM_F_RELEASE] = fval;
    else p->params[PARAM_F_RELEASE] = 0.3f;
    if (json_get_number(data, "volume", &fval) == 0) p->params[PARAM_VOLUME] = fval;
    else p->params[PARAM_VOLUME] = 0.7f;

    /* Parse octave transpose */
    if (json_get_number(data, "octave_transpose", &fval) == 0) {
        p->octave_transpose = (int)fval;
        if (p->octave_transpose < -3) p->octave_transpose = -3;
        if (p->octave_transpose > 3) p->octave_transpose = 3;
    } else {
        p->octave_transpose = 0;
    }

    free(data);
    inst->preset_count++;
    return 0;
}

/* Sort helper for preset filenames (alphabetical order) */
static int preset_name_cmp(const void *a, const void *b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

/* Load all .braids presets from presets/ directory */
static void load_presets(braids_instance_t *inst) {
    char presets_dir[512];
    snprintf(presets_dir, sizeof(presets_dir), "%s/presets", inst->module_dir);

    DIR *dir = opendir(presets_dir);
    if (!dir) {
        char msg[256];
        snprintf(msg, sizeof(msg), "No presets directory: %s", presets_dir);
        plugin_log(msg);
        return;
    }

    /* Collect .braids filenames for sorted loading */
    char *filenames[MAX_PRESETS];
    int file_count = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && file_count < MAX_PRESETS) {
        const char *name = ent->d_name;
        int len = strlen(name);
        if (len > 7 && strcmp(name + len - 7, ".braids") == 0) {
            filenames[file_count] = strdup(name);
            if (filenames[file_count]) file_count++;
        }
    }
    closedir(dir);

    /* Sort alphabetically so numbering prefix controls order */
    qsort(filenames, file_count, sizeof(char*), preset_name_cmp);

    /* Load each preset */
    for (int i = 0; i < file_count; i++) {
        char path[768];
        snprintf(path, sizeof(path), "%s/%s", presets_dir, filenames[i]);
        load_braids_preset(inst, path);
        free(filenames[i]);
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Loaded %d presets", inst->preset_count);
    plugin_log(msg);
}

static void apply_params_to_voice(braids_instance_t *inst, BraidsVoice *v) {
    int shape = (int)inst->params[PARAM_ENGINE];
    if (shape < 0) shape = 0;
    if (shape >= NUM_SHAPES) shape = NUM_SHAPES - 1;
    v->osc.set_shape((braids::MacroOscillatorShape)shape);

    int16_t timbre = (int16_t)(inst->params[PARAM_TIMBRE] * 32767.0f);
    int16_t color = (int16_t)(inst->params[PARAM_COLOR] * 32767.0f);
    v->osc.set_parameters(timbre, color);

    /* SVF filter resonance (cutoff set per-sample in render for envelope modulation) */
    int16_t reso_val = (int16_t)(inst->params[PARAM_RESONANCE] * 32767.0f);
    v->svf.set_resonance(reso_val);

    /* ADSR envelopes */
    v->amp_env.set_params(
        inst->params[PARAM_ATTACK],
        inst->params[PARAM_DECAY],
        inst->params[PARAM_SUSTAIN],
        inst->params[PARAM_RELEASE]);
    v->filt_env.set_params(
        inst->params[PARAM_F_ATTACK],
        inst->params[PARAM_F_DECAY],
        inst->params[PARAM_F_SUSTAIN],
        inst->params[PARAM_F_RELEASE]);
}

/* v2 API: Create instance */
static void* v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    braids_instance_t *inst = (braids_instance_t*)calloc(1, sizeof(braids_instance_t));
    if (!inst) return NULL;

    strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);

    /* Default parameters */
    inst->params[PARAM_ENGINE] = 0.0f;
    inst->params[PARAM_TIMBRE] = 0.5f;
    inst->params[PARAM_COLOR] = 0.5f;
    inst->params[PARAM_ATTACK] = 0.0f;
    inst->params[PARAM_DECAY] = 0.5f;
    inst->params[PARAM_SUSTAIN] = 1.0f;
    inst->params[PARAM_RELEASE] = 0.3f;
    inst->params[PARAM_FM] = 0.0f;
    inst->params[PARAM_CUTOFF] = 1.0f;
    inst->params[PARAM_RESONANCE] = 0.0f;
    inst->params[PARAM_FILT_ENV] = 0.0f;
    inst->params[PARAM_F_ATTACK] = 0.0f;
    inst->params[PARAM_F_DECAY] = 0.3f;
    inst->params[PARAM_F_SUSTAIN] = 0.0f;
    inst->params[PARAM_F_RELEASE] = 0.3f;
    inst->params[PARAM_VOLUME] = 0.7f;
    inst->octave_transpose = 0;
    inst->voice_counter = 0;
    inst->preset_count = 0;
    inst->current_preset = 0;
    snprintf(inst->preset_name, sizeof(inst->preset_name), "Init");

    /* Init all voices */
    for (int i = 0; i < MAX_VOICES; i++) {
        inst->voices[i].osc.Init();
        inst->voices[i].amp_env.init();
        inst->voices[i].filt_env.init();
        inst->voices[i].svf.Init();
        inst->voices[i].active = 0;
        inst->voices[i].gate = 0;
        inst->voices[i].note = 0;
        inst->voices[i].velocity = 0;
        inst->voices[i].age = 0;
        memset(inst->voices[i].osc_buffer, 0, sizeof(inst->voices[i].osc_buffer));
        memset(inst->voices[i].sync_buffer, 0, sizeof(inst->voices[i].sync_buffer));
    }

    /* Load presets from disk */
    load_presets(inst);
    if (inst->preset_count > 0) {
        inst->current_preset = 0;
        v2_apply_preset(inst, 0);
    }

    plugin_log("Braids v2: Instance created");
    return inst;
}

/* v2 API: Destroy instance */
static void v2_destroy_instance(void *instance) {
    braids_instance_t *inst = (braids_instance_t*)instance;
    if (!inst) return;
    free(inst);
    plugin_log("Braids v2: Instance destroyed");
}

/* v2 API: MIDI handler */
static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    braids_instance_t *inst = (braids_instance_t*)instance;
    if (!inst || len < 2) return;
    (void)source;

    uint8_t status = msg[0] & 0xF0;
    uint8_t data1 = msg[1];
    uint8_t data2 = (len > 2) ? msg[2] : 0;

    int note = data1;
    if (status == 0x90 || status == 0x80) {
        note += inst->octave_transpose * 12;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
    }

    switch (status) {
        case 0x90: /* Note On */
            if (data2 > 0) {
                int vi = find_free_voice(inst);
                BraidsVoice *v = &inst->voices[vi];
                v->note = note;
                v->velocity = data2;
                v->active = 1;
                v->gate = 1;
                v->age = ++inst->voice_counter;
                v->osc.set_pitch(note_to_pitch(note));
                apply_params_to_voice(inst, v);
                v->osc.Strike();
                v->amp_env.gate_on();
                v->filt_env.gate_on();
            } else {
                /* Note On with velocity 0 = Note Off */
                int vi = find_voice_for_note(inst, note);
                if (vi >= 0) {
                    inst->voices[vi].gate = 0;
                    inst->voices[vi].amp_env.gate_off();
                    inst->voices[vi].filt_env.gate_off();
                }
            }
            break;

        case 0x80: /* Note Off */
            {
                int vi = find_voice_for_note(inst, note);
                if (vi >= 0) {
                    inst->voices[vi].gate = 0;
                    inst->voices[vi].amp_env.gate_off();
                    inst->voices[vi].filt_env.gate_off();
                }
            }
            break;

        case 0xB0: /* CC */
            switch (data1) {
                case 1: /* Mod wheel -> FM amount */
                    inst->params[PARAM_FM] = data2 / 127.0f;
                    break;
            }
            break;

        case 0xE0: /* Pitch bend */
            {
                int bend = ((data2 << 7) | data1) - 8192;
                float bend_semitones = (bend / 8192.0f) * 2.0f; /* +/- 2 semitones */
                /* Apply to all active voices */
                for (int i = 0; i < MAX_VOICES; i++) {
                    if (inst->voices[i].active) {
                        int16_t pitch = note_to_pitch(inst->voices[i].note);
                        pitch += (int16_t)(bend_semitones * 128.0f);
                        inst->voices[i].osc.set_pitch(pitch);
                    }
                }
            }
            break;
    }
}

/* v2 API: Set parameter */
static void v2_set_param(void *instance, const char *key, const char *val) {
    braids_instance_t *inst = (braids_instance_t*)instance;
    if (!inst) return;

    /* State restore from patch save */
    if (strcmp(key, "state") == 0) {
        char logbuf[256];
        snprintf(logbuf, sizeof(logbuf), "set_param state: %.200s", val ? val : "(null)");
        plugin_log(logbuf);

        float fval;

        /* Restore preset first (sets all params to preset values) */
        if (json_get_number(val, "preset", &fval) == 0) {
            int idx = (int)fval;
            if (idx >= 0 && idx < inst->preset_count) {
                inst->current_preset = idx;
                v2_apply_preset(inst, idx);
            }
        }

        /* Then override with any saved param values (user tweaks on top of preset) */
        if (json_get_number(val, "octave_transpose", &fval) == 0) {
            inst->octave_transpose = (int)fval;
            if (inst->octave_transpose < -3) inst->octave_transpose = -3;
            if (inst->octave_transpose > 3) inst->octave_transpose = 3;
        }
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            if (json_get_number(val, g_shadow_params[i].key, &fval) == 0) {
                if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
                if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
                inst->params[g_shadow_params[i].index] = fval;
            }
        }
        return;
    }

    if (strcmp(key, "octave_transpose") == 0) {
        inst->octave_transpose = atoi(val);
        if (inst->octave_transpose < -3) inst->octave_transpose = -3;
        if (inst->octave_transpose > 3) inst->octave_transpose = 3;
        return;
    }

    /* Preset selection */
    if (strcmp(key, "preset") == 0) {
        int idx = atoi(val);
        if (idx >= 0 && idx < inst->preset_count && idx != inst->current_preset) {
            /* Kill all active voices to avoid hanging notes with mismatched params */
            for (int i = 0; i < MAX_VOICES; i++) {
                inst->voices[i].active = 0;
                inst->voices[i].gate = 0;
                inst->voices[i].amp_env.init();
                inst->voices[i].filt_env.init();
            }
            inst->current_preset = idx;
            v2_apply_preset(inst, idx);
        }
        return;
    }

    /* Engine: accept name string or numeric index */
    if (strcmp(key, "engine") == 0) {
        /* Try name lookup first */
        for (int i = 0; i < NUM_SHAPES; i++) {
            if (strcmp(val, g_shape_names[i]) == 0) {
                inst->params[PARAM_ENGINE] = (float)i;
                return;
            }
        }
        /* Fall through to numeric */
        float v = (float)atof(val);
        if (v < 0) v = 0;
        if (v >= NUM_SHAPES) v = NUM_SHAPES - 1;
        inst->params[PARAM_ENGINE] = v;
        return;
    }

    /* Named parameter access */
    float fval = (float)atof(val);
    for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
        if (strcmp(key, g_shadow_params[i].key) == 0) {
            if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
            if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
            inst->params[g_shadow_params[i].index] = fval;
            return;
        }
    }
}

/* v2 API: Get parameter */
static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    braids_instance_t *inst = (braids_instance_t*)instance;
    if (!inst) return -1;

    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Braids");
    }
    if (strcmp(key, "octave_transpose") == 0) {
        return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    }

    /* Preset browser */
    if (strcmp(key, "preset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->current_preset);
    }
    if (strcmp(key, "preset_count") == 0) {
        return snprintf(buf, buf_len, "%d", inst->preset_count);
    }
    if (strcmp(key, "preset_name") == 0) {
        return snprintf(buf, buf_len, "%s", inst->preset_name);
    }

    /* Engine: return name string for enum display */
    if (strcmp(key, "engine") == 0 || strcmp(key, "engine_name") == 0) {
        int shape = (int)inst->params[PARAM_ENGINE];
        if (shape < 0) shape = 0;
        if (shape >= NUM_SHAPES) shape = NUM_SHAPES - 1;
        return snprintf(buf, buf_len, "%s", g_shape_names[shape]);
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
                    "\"children\":null,"
                    /* Root's knob order is load-bearing, for the same reason
                     * the filter level's is: a viz group is only drawn when its
                     * roles sit contiguously on one row (a page is two rows of
                     * four). attack/decay/sustain/release are the four roles of
                     * the "amp" group, so they are placed at slots 4-7 — all of
                     * row 1 — and the page draws the amp envelope as a curve.
                     * cutoff takes slot 3 to close row 0. Moving any of the
                     * four, or splitting them across the row boundary, silently
                     * turns the curve back into four plain dials.
                     *
                     * The row is at the 8-knob cap, so filt_env gives up its
                     * slot; it stays reachable on the Filter level at slot 6. */
                    "\"knobs\":[\"engine\",\"timbre\",\"color\",\"cutoff\",\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":["
                        "{\"level\":\"oscillator\",\"label\":\"Oscillator\"},"
                        "{\"level\":\"envelope\",\"label\":\"Amp Envelope\"},"
                        "{\"level\":\"filter\",\"label\":\"Filter\"},"
                        "{\"level\":\"global\",\"label\":\"Global\"}"
                    "]"
                "},"
                "\"oscillator\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"engine\",\"timbre\",\"color\",\"fm\"],"
                    "\"params\":[\"engine\",\"timbre\",\"color\",\"fm\"]"
                "},"
                "\"envelope\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":[\"attack\",\"decay\",\"sustain\",\"release\"]"
                "},"
                /* f_attack..f_release first so the four land in row 0
                 * (slots 0-3) as one contiguous run — a viz envelope group
                 * can only be drawn when its roles sit together on one row
                 * (docs/MODULES.md "Parameter visualisations"). cutoff and
                 * resonance follow at slots 4-5 (row 1), the filter group's
                 * own contiguous pair; filt_env stands alone at slot 6. */
                "\"filter\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\",\"cutoff\",\"resonance\",\"filt_env\"],"
                    "\"params\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\",\"cutoff\",\"resonance\",\"filt_env\"]"
                "},"
                "\"global\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"volume\",\"octave_transpose\"],"
                    "\"params\":[\"volume\",\"octave_transpose\"]"
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
            "{\"preset\":%d,\"octave_transpose\":%d", inst->current_preset, inst->octave_transpose);
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            float val = inst->params[g_shadow_params[i].index];
            if (g_shadow_params[i].type == PARAM_TYPE_INT) {
                offset += snprintf(buf + offset, buf_len - offset,
                    ",\"%s\":%d", g_shadow_params[i].key, (int)val);
            } else {
                offset += snprintf(buf + offset, buf_len - offset,
                    ",\"%s\":%.4f", g_shadow_params[i].key, val);
            }
        }
        offset += snprintf(buf + offset, buf_len - offset, "}");
        return offset;
    }

    /* Chain params metadata */
    if (strcmp(key, "chain_params") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset, "[");

        /* Engine as enum with all algorithm names */
        offset += snprintf(buf + offset, buf_len - offset,
            "{\"key\":\"engine\",\"name\":\"Algorithm\",\"type\":\"enum\",\"options\":[");
        for (int i = 0; i < NUM_SHAPES && offset < buf_len - 50; i++) {
            if (i > 0) offset += snprintf(buf + offset, buf_len - offset, ",");
            /* Write JSON-escaped string (backslash needs escaping) */
            buf[offset++] = '"';
            for (const char *p = g_shape_names[i]; *p && offset < buf_len - 10; p++) {
                if (*p == '\\' || *p == '"') buf[offset++] = '\\';
                buf[offset++] = *p;
            }
            buf[offset++] = '"';
            buf[offset] = '\0';
        }
        offset += snprintf(buf + offset, buf_len - offset, "]}");

        /*
         * Remaining params. Hand-assembled rather than via
         * param_helper_chain_params_json() because param_def_t has no room for
         * the percentage unit/display_format these carry — but the viz field
         * still comes from param_helper_viz_json(), reading the same table the
         * params themselves come from, so there is no second list of keys to
         * keep in step.
         */
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params) &&
                        offset < buf_len - PARAM_HELPER_ENTRY_MARGIN; i++) {
            if (strcmp(g_shadow_params[i].key, "engine") == 0) continue;  /* Already handled */
            /* Float params with 0-1 range get percentage display */
            int is_pct = (g_shadow_params[i].type == PARAM_TYPE_FLOAT &&
                          g_shadow_params[i].min_val == 0.0f &&
                          g_shadow_params[i].max_val == 1.0f);
            offset += snprintf(buf + offset, buf_len - offset,
                ",{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%g,\"max\":%g%s",
                g_shadow_params[i].key,
                g_shadow_params[i].name[0] ? g_shadow_params[i].name : g_shadow_params[i].key,
                g_shadow_params[i].type == PARAM_TYPE_INT ? "int" : "float",
                g_shadow_params[i].min_val,
                g_shadow_params[i].max_val,
                is_pct ? ",\"unit\":\"%\",\"display_format\":\"%.0f\"" : "");
            int vn = param_helper_viz_json(&g_shadow_params[i], buf + offset, buf_len - offset);
            if (vn < 0) return -1;
            offset += vn;
            offset += snprintf(buf + offset, buf_len - offset, "}");
        }

        /* Octave transpose */
        offset += snprintf(buf + offset, buf_len - offset,
            ",{\"key\":\"octave_transpose\",\"name\":\"Octave\",\"type\":\"int\",\"min\":-3,\"max\":3}");

        offset += snprintf(buf + offset, buf_len - offset, "]");
        return offset;
    }

    return -1;
}

/* v2 API: Render audio */
static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    braids_instance_t *inst = (braids_instance_t*)instance;
    if (!inst) {
        memset(out_interleaved_lr, 0, frames * 4);
        return;
    }

    float gain = inst->params[PARAM_VOLUME] / (float)MAX_VOICES;
    float fm_amount = inst->params[PARAM_FM];
    float base_cutoff = inst->params[PARAM_CUTOFF];
    float filt_env_amount = inst->params[PARAM_FILT_ENV];
    int use_filter = (base_cutoff < 0.99f || inst->params[PARAM_RESONANCE] > 0.01f
                      || filt_env_amount > 0.01f);

    /* Clear output */
    memset(out_interleaved_lr, 0, frames * 4);

    /* Render each active voice */
    for (int vi = 0; vi < MAX_VOICES; vi++) {
        BraidsVoice *v = &inst->voices[vi];
        if (!v->active) continue;

        /* Update oscillator parameters */
        apply_params_to_voice(inst, v);

        /* Apply FM from mod wheel to pitch */
        int16_t pitch = note_to_pitch(v->note);
        if (fm_amount > 0.001f) {
            pitch += (int16_t)(fm_amount * 1536.0f); /* Up to 12 semitones */
        }
        v->osc.set_pitch(pitch);

        /* Render in 24-sample blocks */
        int rendered = 0;
        while (rendered < frames) {
            int block_size = BRAIDS_BLOCK_SIZE;
            if (rendered + block_size > frames) {
                block_size = frames - rendered;
            }

            /* Render oscillator */
            memset(v->sync_buffer, 0, sizeof(v->sync_buffer));
            v->osc.Render(v->sync_buffer, v->osc_buffer, block_size);

            /* Apply envelope and mix to output */
            for (int s = 0; s < block_size; s++) {
                /* Process ADSR envelopes */
                float amp = v->amp_env.process();
                float filt_val = v->filt_env.process();

                /* Check if amplitude envelope has finished */
                if (!v->gate && !v->amp_env.is_active()) {
                    v->active = 0;
                    break;
                }

                /* Apply amplitude envelope to oscillator output */
                int32_t sample = v->osc_buffer[s];
                sample = (int32_t)(sample * amp);

                /* Apply SVF filter with envelope modulation */
                if (use_filter) {
                    float mod_cutoff = base_cutoff + filt_val * filt_env_amount;
                    if (mod_cutoff > 1.0f) mod_cutoff = 1.0f;
                    int16_t cutoff_freq = (int16_t)(mod_cutoff * 127.0f) << 7;
                    v->svf.set_frequency(cutoff_freq);
                    sample = v->svf.Process(sample);
                }

                /* Velocity scaling */
                sample = (sample * v->velocity) / 127;

                /* Mix to stereo output (accumulate) */
                int idx = (rendered + s) * 2;
                int32_t left = out_interleaved_lr[idx] + (int32_t)(sample * gain);
                int32_t right = out_interleaved_lr[idx + 1] + (int32_t)(sample * gain);

                if (left > 32767) left = 32767;
                if (left < -32768) left = -32768;
                if (right > 32767) right = 32767;
                if (right < -32768) right = -32768;

                out_interleaved_lr[idx] = (int16_t)left;
                out_interleaved_lr[idx + 1] = (int16_t)right;
            }

            if (!v->active) break;
            rendered += block_size;
        }
    }
}

/* No external assets required */
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
