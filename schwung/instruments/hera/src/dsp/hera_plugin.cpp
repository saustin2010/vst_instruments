/*
 * Hera Juno-60 DSP Plugin for Move Anything
 *
 * Juno-60 emulation synthesizer based on Hera by jpcima.
 * GPL-3.0-or-later License - see LICENSE file.
 *
 * Based on Hera by Jean Pierre Cimalando
 * https://github.com/jpcima/Hera
 *
 * V2 API only - instance-based for multi-instance support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <algorithm>

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

/* Hera Engine includes */
#include "Engine/HeraEnvelope.h"
#include "Engine/HeraLFOWithEnvelope.h"
#include "Engine/HeraDCO.hxx"
#include "Engine/HeraVCF.h"
#include "Engine/HeraHPF.hxx"
#include "Engine/HeraVCA.hxx"
#include "Engine/HeraChorus.hxx"
#include "Engine/SmoothValue.h"
#include "Engine/HeraTables.h"
#include "Engine/FaustHelpers.h"
#include "param_helper.h"

/* =====================================================================
 * Constants
 * ===================================================================== */

#define MAX_VOICES 6
#define MAX_PRESETS 128
#define MAX_BLOCK_SIZE 256

/* Hera parameter indices (matching original Hera) */
enum {
    kHeraParamVCA,
    kHeraParamVCAType,
    kHeraParamPWMDepth,
    kHeraParamPWMMod,
    kHeraParamSawLevel,
    kHeraParamPulseLevel,
    kHeraParamSubLevel,
    kHeraParamNoiseLevel,
    kHeraParamPitchRange,
    kHeraParamPitchModDepth,
    kHeraParamVCFCutoff,
    kHeraParamVCFResonance,
    kHeraParamVCFEnvelopeModDepth,
    kHeraParamVCFLFOModDepth,
    kHeraParamVCFKeyboardModDepth,
    kHeraParamVCFBendDepth,
    kHeraParamAttack,
    kHeraParamDecay,
    kHeraParamSustain,
    kHeraParamRelease,
    kHeraParamLFOTriggerMode,
    kHeraParamLFORate,
    kHeraParamLFODelay,
    kHeraParamHPF,
    kHeraParamChorusI,
    kHeraParamChorusII,
    kHeraNumParameters,
};

enum { kHeraVCATypeEnvelope, kHeraVCATypeGate };
enum { kHeraPWMManual, kHeraPWMLFO, kHeraPWMEnvelope };
enum { kHeraLFOManual, kHeraLFOAuto };

/* Host API reference */
static const host_api_v1_t *g_host = NULL;

/* =====================================================================
 * Logging helper
 * ===================================================================== */

static void plugin_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[hera] %s", msg);
        g_host->log(buf);
    }
}

/* =====================================================================
 * Voice structure - one per polyphony voice
 * ===================================================================== */

struct HeraVoiceState {
    bool active;
    int note;               /* MIDI note (after octave transpose) */
    float frequency;        /* Hz */
    float velocity;         /* 0-1 */
    float pitchBendFactor;  /* frequency multiplier from pitch bend */

    /* Per-voice DSP */
    HeraDCO dco;
    HeraVCF vcf;
    HeraEnvelope normalEnvelope;
    HeraEnvelope gateEnvelope;
    OnePoleSmoothValue smoothPWMDepth;

    /* Per-voice parameters */
    int vcaType;
    int pwmMod;

    HeraVoiceState() : active(false), note(-1), frequency(440.0f),
                       velocity(0.0f), pitchBendFactor(1.0f),
                       vcaType(kHeraVCATypeEnvelope), pwmMod(kHeraPWMManual) {
        normalEnvelope.setSampleRate(MOVE_SAMPLE_RATE);
        gateEnvelope.setSampleRate(MOVE_SAMPLE_RATE);
        dco.init(MOVE_SAMPLE_RATE);

        /* Gate envelope: fast attack/release for gate mode */
        gateEnvelope.setAttack(0.00247f);
        gateEnvelope.setDecay(0.0057f);
        gateEnvelope.setSustain(0.98f);
        gateEnvelope.setRelease(0.0057f);

        smoothPWMDepth.setTimeConstant(10e-3f);
        smoothPWMDepth.setSampleRate(MOVE_SAMPLE_RATE);
    }

    void setSampleRate(float rate) {
        normalEnvelope.setSampleRate(rate);
        gateEnvelope.setSampleRate(rate);
        dco.classInit(rate);
        dco.instanceConstants(rate);
        dco.instanceClear();
        vcf.setSampleRate(rate);
        smoothPWMDepth.setSampleRate(rate);
    }

    HeraEnvelope& getCurrentEnvelope() {
        return (vcaType == kHeraVCATypeEnvelope) ? normalEnvelope : gateEnvelope;
    }

    bool isReleased() const {
        return (vcaType == kHeraVCATypeEnvelope) ?
            normalEnvelope.isReleased() : gateEnvelope.isReleased();
    }
};

/* =====================================================================
 * Preset structure
 * ===================================================================== */

struct HeraPreset {
    char name[64];
    float values[kHeraNumParameters];
};

/* =====================================================================
 * Parameter IDs (strings used in XML presets)
 * ===================================================================== */

static const char* g_param_ids[kHeraNumParameters] = {
    "VCADepth",          /* kHeraParamVCA */
    "VCAType",           /* kHeraParamVCAType */
    "DCOPWMDepth",       /* kHeraParamPWMDepth */
    "DCOPWMMod",         /* kHeraParamPWMMod */
    "DCOSawLevel",       /* kHeraParamSawLevel */
    "DCOPulseLevel",     /* kHeraParamPulseLevel */
    "DCOSubLevel",       /* kHeraParamSubLevel */
    "DCONoiseLevel",     /* kHeraParamNoiseLevel */
    "DCORange",          /* kHeraParamPitchRange */
    "DCOPitchModDepth",  /* kHeraParamPitchModDepth */
    "VCFCutoff",         /* kHeraParamVCFCutoff */
    "VCFResonance",      /* kHeraParamVCFResonance */
    "VCFEnv",            /* kHeraParamVCFEnvelopeModDepth */
    "VCFLFO",            /* kHeraParamVCFLFOModDepth */
    "VCFKey",            /* kHeraParamVCFKeyboardModDepth */
    "VCFBendDepth",      /* kHeraParamVCFBendDepth */
    "ENVAttack",         /* kHeraParamAttack */
    "ENVDecay",          /* kHeraParamDecay */
    "ENVSustain",        /* kHeraParamSustain */
    "ENVRelease",        /* kHeraParamRelease */
    "LFOTrigMode",       /* kHeraParamLFOTriggerMode */
    "LFORate",           /* kHeraParamLFORate */
    "LFODelay",          /* kHeraParamLFODelay */
    "HPF",               /* kHeraParamHPF */
    "ChorusI",           /* kHeraParamChorusI */
    "ChorusII",          /* kHeraParamChorusII */
};

/* Default parameter values */
static const float g_param_defaults[kHeraNumParameters] = {
    0.5f,   /* VCA depth */
    0.0f,   /* VCA type (envelope) */
    0.5f,   /* PWM depth */
    0.0f,   /* PWM mod (manual) */
    1.0f,   /* Saw level */
    0.0f,   /* Pulse level */
    0.0f,   /* Sub level */
    0.0f,   /* Noise level */
    1.0f,   /* Pitch range (8') */
    0.0f,   /* Pitch mod depth */
    0.5f,   /* VCF cutoff */
    0.0f,   /* VCF resonance */
    0.0f,   /* VCF envelope mod depth */
    0.0f,   /* VCF LFO mod depth */
    0.0f,   /* VCF keyboard mod depth */
    0.0f,   /* VCF bend depth */
    0.0f,   /* Attack */
    0.0f,   /* Decay */
    0.0f,   /* Sustain */
    0.0f,   /* Release */
    1.0f,   /* LFO trigger mode (auto) */
    0.0f,   /* LFO rate */
    0.0f,   /* LFO delay */
    0.0f,   /* HPF */
    0.0f,   /* Chorus I */
    0.0f,   /* Chorus II */
};

/* Shadow UI parameter definitions for the param_helper */
static const param_def_t g_shadow_params[] = {
    /* DCO */
    {"saw_level",    "Saw Level",    PARAM_TYPE_FLOAT, kHeraParamSawLevel,    0.0f, 1.0f},
    {"pulse_level",  "Pulse Level",  PARAM_TYPE_FLOAT, kHeraParamPulseLevel,  0.0f, 1.0f},
    {"sub_level",    "Sub Level",    PARAM_TYPE_FLOAT, kHeraParamSubLevel,    0.0f, 1.0f},
    {"noise_level",  "Noise Level",  PARAM_TYPE_FLOAT, kHeraParamNoiseLevel,  0.0f, 1.0f},
    {"pwm_depth",    "PWM Depth",    PARAM_TYPE_FLOAT, kHeraParamPWMDepth,    0.0f, 1.0f},
    {"pwm_mod",      "PWM Mod",      PARAM_TYPE_INT,   kHeraParamPWMMod,      0.0f, 2.0f},
    {"pitch_range",  "Range",        PARAM_TYPE_INT,   kHeraParamPitchRange,  0.0f, 2.0f},
    {"pitch_mod",    "Pitch Mod",    PARAM_TYPE_FLOAT, kHeraParamPitchModDepth, 0.0f, 1.0f},

    /* VCF */
    {"vcf_cutoff",   "VCF Cutoff",   PARAM_TYPE_FLOAT, kHeraParamVCFCutoff,   0.0f, 1.0f},
    {"vcf_resonance","VCF Reso",     PARAM_TYPE_FLOAT, kHeraParamVCFResonance, 0.0f, 1.0f},
    {"vcf_env",      "VCF Env",      PARAM_TYPE_FLOAT, kHeraParamVCFEnvelopeModDepth, -1.0f, 1.0f},
    {"vcf_lfo",      "VCF LFO",      PARAM_TYPE_FLOAT, kHeraParamVCFLFOModDepth, 0.0f, 1.0f},
    {"vcf_key",      "VCF Key",      PARAM_TYPE_FLOAT, kHeraParamVCFKeyboardModDepth, 0.0f, 1.0f},
    {"vcf_bend",     "VCF Bend",     PARAM_TYPE_FLOAT, kHeraParamVCFBendDepth, 0.0f, 1.0f},

    /* VCA */
    {"vca_depth",    "VCA Depth",    PARAM_TYPE_FLOAT, kHeraParamVCA,         0.0f, 1.0f},
    {"vca_type",     "VCA Type",     PARAM_TYPE_INT,   kHeraParamVCAType,     0.0f, 1.0f},

    /* Envelope */
    {"attack",       "Attack",       PARAM_TYPE_FLOAT, kHeraParamAttack,      0.0f, 1.0f},
    {"decay",        "Decay",        PARAM_TYPE_FLOAT, kHeraParamDecay,       0.0f, 1.0f},
    {"sustain",      "Sustain",      PARAM_TYPE_FLOAT, kHeraParamSustain,     0.0f, 1.0f},
    {"release",      "Release",      PARAM_TYPE_FLOAT, kHeraParamRelease,     0.0f, 1.0f},

    /* LFO */
    {"lfo_rate",     "LFO Rate",     PARAM_TYPE_FLOAT, kHeraParamLFORate,     0.0f, 1.0f},
    {"lfo_delay",    "LFO Delay",    PARAM_TYPE_FLOAT, kHeraParamLFODelay,    0.0f, 1.0f},
    {"lfo_trigger",  "LFO Trigger",  PARAM_TYPE_INT,   kHeraParamLFOTriggerMode, 0.0f, 1.0f},

    /* HPF */
    {"hpf",          "HPF",          PARAM_TYPE_FLOAT, kHeraParamHPF,         0.0f, 1.0f},

    /* Chorus */
    {"chorus_i",     "Chorus I",     PARAM_TYPE_INT,   kHeraParamChorusI,     0.0f, 1.0f},
    {"chorus_ii",    "Chorus II",    PARAM_TYPE_INT,   kHeraParamChorusII,    0.0f, 1.0f},
};

/* =====================================================================
 * Instance structure
 * ===================================================================== */

typedef struct {
    char module_dir[256];

    /* Parameters */
    float params[kHeraNumParameters];

    /* Voices */
    HeraVoiceState voices[MAX_VOICES];

    /* Shared synth state */
    HeraLFOWithEnvelope lfo;
    HeraHPF hpFilter;
    HeraVCA vca;
    HeraChorus chorus;
    OnePoleSmoothValue smoothPitchModDepth;
    OnePoleSmoothValue smoothCutoff;
    OnePoleSmoothValue smoothResonance;
    OnePoleSmoothValue smoothVCFEnvModDepth;
    OnePoleSmoothValue smoothVCFLFOModDepth;
    OnePoleSmoothValue smoothVCFKeyboardModDepth;
    OnePoleSmoothValue smoothVCFBendDepth;
    float pitchFactor;
    int vcaType;
    int lfoMode;

    /* Shared buffers for rendering */
    float lfoBuffer[MAX_BLOCK_SIZE];
    float detuneBuffer[MAX_BLOCK_SIZE];
    float dcoBuffer[MAX_BLOCK_SIZE];
    float envelopeBuffer[MAX_BLOCK_SIZE];
    float gateBuffer[MAX_BLOCK_SIZE];
    float pwmModBuffer[MAX_BLOCK_SIZE];
    float cutoffOctavesBuffer[MAX_BLOCK_SIZE];
    float cutoffBuffer[MAX_BLOCK_SIZE];
    float resonanceBuffer[MAX_BLOCK_SIZE];
    float vcfEnvModBuffer[MAX_BLOCK_SIZE];
    float vcfLFODetuneOctavesBuffer[MAX_BLOCK_SIZE];
    float vcfKeyboardModBuffer[MAX_BLOCK_SIZE];
    float vcfBendDepthBuffer[MAX_BLOCK_SIZE];
    float mixBuffer[MAX_BLOCK_SIZE];
    float chorusOutL[MAX_BLOCK_SIZE];
    float chorusOutR[MAX_BLOCK_SIZE];

    /* Pitch bend state */
    float pitchBendSemitones;

    /* Preset state */
    HeraPreset presets[MAX_PRESETS];
    int preset_count;
    int current_preset;
    char preset_name[64];

    /* UI state */
    int octave_transpose;
    float output_gain;
    float volume;   /* User-controllable volume 0-1, default 0.8 */
} hera_instance_t;

/* =====================================================================
 * MIDI note to frequency
 * ===================================================================== */

static float midi_to_freq(int note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

/* =====================================================================
 * Apply a parameter value to the synth engine
 * ===================================================================== */

static void apply_param(hera_instance_t *inst, int param_idx, float value) {
    if (param_idx < 0 || param_idx >= kHeraNumParameters) return;

    inst->params[param_idx] = value;

    switch (param_idx) {
    case kHeraParamVCA:
        inst->vca.setAmount(value);
        break;
    case kHeraParamVCAType:
        inst->vcaType = (int)value;
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].vcaType = inst->vcaType;
        break;
    case kHeraParamPWMDepth:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].smoothPWMDepth.setTargetValue(value);
        break;
    case kHeraParamPWMMod:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].pwmMod = (int)value;
        break;
    case kHeraParamSawLevel:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].dco.setSawLevel(value);
        break;
    case kHeraParamPulseLevel:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].dco.setPulseLevel(value);
        break;
    case kHeraParamSubLevel:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].dco.setSubLevel(value);
        break;
    case kHeraParamNoiseLevel:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].dco.setNoiseLevel(value);
        break;
    case kHeraParamPitchRange: {
        const float factors[] = { 0.5f, 1.0f, 2.0f };
        int idx = std::max(0, std::min(2, (int)value));
        inst->pitchFactor = factors[idx];
        break;
    }
    case kHeraParamPitchModDepth:
        inst->smoothPitchModDepth.setTargetValue(value);
        break;
    case kHeraParamVCFCutoff:
        inst->smoothCutoff.setTargetValue(value);
        break;
    case kHeraParamVCFResonance:
        inst->smoothResonance.setTargetValue(value);
        break;
    case kHeraParamVCFEnvelopeModDepth:
        inst->smoothVCFEnvModDepth.setTargetValue(value);
        break;
    case kHeraParamVCFLFOModDepth:
        inst->smoothVCFLFOModDepth.setTargetValue(value);
        break;
    case kHeraParamVCFKeyboardModDepth:
        inst->smoothVCFKeyboardModDepth.setTargetValue(value);
        break;
    case kHeraParamVCFBendDepth:
        inst->smoothVCFBendDepth.setTargetValue(value);
        break;
    case kHeraParamAttack:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].normalEnvelope.setAttack(value);
        break;
    case kHeraParamDecay:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].normalEnvelope.setDecay(value);
        break;
    case kHeraParamSustain:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].normalEnvelope.setSustain(value);
        break;
    case kHeraParamRelease:
        for (int i = 0; i < MAX_VOICES; i++)
            inst->voices[i].normalEnvelope.setRelease(value);
        break;
    case kHeraParamLFOTriggerMode: {
        int newMode = (int)value;
        if (inst->lfoMode != newMode) {
            inst->lfo.shutdown();
            inst->lfoMode = newMode;
        }
        break;
    }
    case kHeraParamLFORate:
        inst->lfo.setFrequency(curveFromLfoRateSliderToFreq(value));
        break;
    case kHeraParamLFODelay:
        inst->lfo.setDelayDuration(curveFromLfoDelaySliderToDelay(value));
        inst->lfo.setAttackDuration(curveFromLfoDelaySliderToAttack(value));
        break;
    case kHeraParamHPF:
        inst->hpFilter.setAmount(value);
        break;
    case kHeraParamChorusI:
        inst->chorus.setChorusI(value);
        break;
    case kHeraParamChorusII:
        inst->chorus.setChorusII(value);
        break;
    }
}

/* =====================================================================
 * Preset loading from XML files
 * ===================================================================== */

/* Simple XML attribute parser */
static const char* find_xml_attr(const char *xml, const char *attr_name, char *buf, int buf_len) {
    char search[64];
    snprintf(search, sizeof(search), "%s=\"", attr_name);
    const char *pos = strstr(xml, search);
    if (!pos) return NULL;

    pos += strlen(search);
    const char *end = strchr(pos, '"');
    if (!end) return NULL;

    int len = end - pos;
    if (len >= buf_len) len = buf_len - 1;
    strncpy(buf, pos, len);
    buf[len] = '\0';
    return end + 1;
}

static int load_preset_xml(hera_instance_t *inst, const char *path, int preset_idx) {
    if (preset_idx >= MAX_PRESETS) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 65536) { fclose(f); return -1; }

    char *data = (char*)malloc(size + 1);
    if (!data) { fclose(f); return -1; }
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);

    HeraPreset *p = &inst->presets[preset_idx];
    memset(p, 0, sizeof(HeraPreset));

    /* Set defaults */
    for (int i = 0; i < kHeraNumParameters; i++)
        p->values[i] = g_param_defaults[i];

    /* Extract preset name from <PROGRAM name="..."/> */
    char name_buf[64];
    if (find_xml_attr(data, "name", name_buf, sizeof(name_buf))) {
        strncpy(p->name, name_buf, sizeof(p->name) - 1);
    } else {
        snprintf(p->name, sizeof(p->name), "Preset %d", preset_idx);
    }

    /* Extract parameter values from <PARAM id="..." value="..."/> */
    const char *pos = data;
    while ((pos = strstr(pos, "<PARAM ")) != NULL) {
        char id_buf[64], val_buf[32];
        if (find_xml_attr(pos, "id", id_buf, sizeof(id_buf)) &&
            find_xml_attr(pos, "value", val_buf, sizeof(val_buf))) {

            float val = atof(val_buf);
            /* Find matching parameter */
            for (int i = 0; i < kHeraNumParameters; i++) {
                if (strcmp(id_buf, g_param_ids[i]) == 0) {
                    p->values[i] = val;
                    break;
                }
            }
        }
        pos++;
    }

    free(data);
    return 0;
}

static int load_presets(hera_instance_t *inst) {
    char presets_dir[512];
    snprintf(presets_dir, sizeof(presets_dir), "%s/presets", inst->module_dir);

    inst->preset_count = 0;

    /* Load presets in order: Preset000.xml, Preset001.xml, ... */
    for (int i = 0; i < MAX_PRESETS; i++) {
        char path[600];
        snprintf(path, sizeof(path), "%s/Preset%03d.xml", presets_dir, i);

        FILE *test = fopen(path, "r");
        if (!test) break;
        fclose(test);

        if (load_preset_xml(inst, path, inst->preset_count) == 0) {
            inst->preset_count++;
        }
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Loaded %d presets", inst->preset_count);
    plugin_log(msg);

    return inst->preset_count;
}

static void apply_preset(hera_instance_t *inst, int preset_idx) {
    if (preset_idx < 0 || preset_idx >= inst->preset_count) return;

    HeraPreset *p = &inst->presets[preset_idx];
    inst->current_preset = preset_idx;
    snprintf(inst->preset_name, sizeof(inst->preset_name), "%s", p->name);

    for (int i = 0; i < kHeraNumParameters; i++) {
        apply_param(inst, i, p->values[i]);
    }
}

/* =====================================================================
 * Voice management
 * ===================================================================== */

static bool has_unreleased_voices(hera_instance_t *inst) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (inst->voices[i].active && !inst->voices[i].isReleased())
            return true;
    }
    return false;
}

static int find_free_voice(hera_instance_t *inst) {
    /* First: find an inactive voice */
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!inst->voices[i].active) return i;
    }

    /* Second: steal the oldest released voice */
    for (int i = 0; i < MAX_VOICES; i++) {
        if (inst->voices[i].isReleased()) return i;
    }

    /* Last resort: steal voice 0 */
    return 0;
}

static void note_on(hera_instance_t *inst, int note, float velocity) {
    int vi = find_free_voice(inst);
    HeraVoiceState &voice = inst->voices[vi];

    voice.active = true;
    voice.note = note;
    voice.frequency = midi_to_freq(note);
    voice.velocity = velocity;
    voice.vcaType = inst->vcaType;

    /* LFO auto-trigger */
    if (inst->lfoMode == kHeraLFOAuto) {
        if (!has_unreleased_voices(inst))
            inst->lfo.noteOn();
    }

    /* Start envelope */
    voice.getCurrentEnvelope().noteOn();

    /* Set DCO frequency */
    voice.dco.setFrequency(voice.frequency);
    flushSmoothValues(voice.dco);

    /* Reset PWM smoother */
    voice.smoothPWMDepth.setCurrentAndTargetValue(
        voice.smoothPWMDepth.getTargetValue());
}

static void note_off(hera_instance_t *inst, int note) {
    for (int i = 0; i < MAX_VOICES; i++) {
        HeraVoiceState &voice = inst->voices[i];
        if (voice.active && voice.note == note && !voice.isReleased()) {
            voice.getCurrentEnvelope().noteOff();

            /* LFO auto mode: check if all voices released */
            if (inst->lfoMode == kHeraLFOAuto) {
                if (!has_unreleased_voices(inst))
                    inst->lfo.noteOff();
            }
            break;
        }
    }
}

static void all_notes_off(hera_instance_t *inst) {
    for (int i = 0; i < MAX_VOICES; i++) {
        HeraVoiceState &voice = inst->voices[i];
        if (voice.active) {
            voice.getCurrentEnvelope().shutdown();
            voice.active = false;
            voice.note = -1;
        }
    }
}

/* =====================================================================
 * Audio rendering
 * ===================================================================== */

static void render_voice(hera_instance_t *inst, HeraVoiceState &voice,
                         float *output, int numSamples) {
    /* Process envelope */
    voice.normalEnvelope.processNextBlock(inst->envelopeBuffer, 0, numSamples);
    if (voice.vcaType != kHeraVCATypeEnvelope) {
        voice.gateEnvelope.processNextBlock(inst->gateBuffer, 0, numSamples);
    }

    /* Process PWM */
    const float *lfoIn = inst->lfoBuffer;
    const float *envelopeIn = inst->envelopeBuffer;
    float *pwmModOut = inst->pwmModBuffer;
    switch (voice.pwmMod) {
    default:
        for (int i = 0; i < numSamples; i++)
            pwmModOut[i] = voice.smoothPWMDepth.getNextValue();
        break;
    case kHeraPWMLFO:
        for (int i = 0; i < numSamples; i++)
            pwmModOut[i] = voice.smoothPWMDepth.getNextValue() * (lfoIn[i] * 0.5f + 0.5f);
        break;
    case kHeraPWMEnvelope:
        for (int i = 0; i < numSamples; i++)
            pwmModOut[i] = voice.smoothPWMDepth.getNextValue() * envelopeIn[i];
        break;
    }

    /* Process DCO */
    const float *detuneOut = inst->detuneBuffer;
    float *dcoOut = inst->dcoBuffer;
    compute(voice.dco, { detuneOut, pwmModOut }, { dcoOut }, numSamples);

    /* Process VCF */
    const float *modEnvelopeIn = inst->envelopeBuffer;
    const float *ampEnvelopeIn = (voice.vcaType == kHeraVCATypeEnvelope) ?
        inst->envelopeBuffer : inst->gateBuffer;
    const float *cutoffOctaves = inst->cutoffOctavesBuffer;
    float *cutoff = inst->cutoffBuffer;
    const float *resonance = inst->resonanceBuffer;
    const float *vcfEnvMod = inst->vcfEnvModBuffer;
    const float *vcfLFODetuneOctaves = inst->vcfLFODetuneOctavesBuffer;
    const float *vcfKeyboardMod = inst->vcfKeyboardModBuffer;
    const float *vcfBendDepth = inst->vcfBendDepthBuffer;

    float filterNoteFactor = (float)(voice.note - 60) * (1.0f / 12.0f);
    float pitchbendFactor = inst->pitchBendSemitones * (48.0f / (12.0f * 7.0f));

    for (int i = 0; i < numSamples; i++) {
        float envDetuneOctaves = vcfEnvMod[i] * modEnvelopeIn[i] * 12;
        float lfoDetuneOctaves = vcfLFODetuneOctaves[i] * ampEnvelopeIn[i];
        float keyboardDetuneOctaves = vcfKeyboardMod[i] * filterNoteFactor;
        float filterBendOctaves = vcfBendDepth[i] * pitchbendFactor;
        cutoff[i] = 7.8f * std::exp2(cutoffOctaves[i] + envDetuneOctaves +
                    lfoDetuneOctaves + keyboardDetuneOctaves + filterBendOctaves);
    }

    voice.vcf.processNextBlock(dcoOut, cutoff, resonance, numSamples);

    /* Mix into output — scale by velocity and divide by voice count for headroom */
    float noteVolume = voice.velocity * voice.velocity * (1.0f / MAX_VOICES);
    for (int i = 0; i < numSamples; i++) {
        output[i] += dcoOut[i] * ampEnvelopeIn[i] * noteVolume;
    }

    /* Check if voice should be deactivated */
    if (!voice.getCurrentEnvelope().isActive()) {
        voice.getCurrentEnvelope().reset();
        voice.gateEnvelope.reset();
        voice.dco.instanceClear();
        voice.vcf.reset();
        voice.active = false;
        voice.note = -1;
    }
}

/* Soft clip function */
static void soft_clip(float *buffer, int numSamples) {
    const LerpTable &clip = curveSoftClipTanh3;
    for (int i = 0; i < numSamples; i++)
        buffer[i] = clip(buffer[i]);
}

/* =====================================================================
 * JSON helpers
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
 * Plugin API v2 implementation
 * ===================================================================== */

static void* v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    hera_instance_t *inst = new hera_instance_t();
    if (!inst) return NULL;

    memset(inst->module_dir, 0, sizeof(inst->module_dir));
    strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);
    inst->output_gain = 1.0f;
    inst->volume = 0.8f;
    inst->pitchFactor = 1.0f;
    inst->vcaType = kHeraVCATypeEnvelope;
    inst->lfoMode = kHeraLFOAuto;
    inst->pitchBendSemitones = 0.0f;
    inst->octave_transpose = 0;
    inst->current_preset = 0;
    inst->preset_count = 0;
    snprintf(inst->preset_name, sizeof(inst->preset_name), "Init");

    /* Initialize LFO */
    inst->lfo.setSampleRate(MOVE_SAMPLE_RATE);
    inst->lfo.setType(HeraLFO::Sine);

    /* Initialize smoothers */
    inst->smoothPitchModDepth.setTimeConstant(10e-3f);
    inst->smoothPitchModDepth.setSampleRate(MOVE_SAMPLE_RATE);
    inst->smoothCutoff.setTimeConstant(10e-3f);
    inst->smoothCutoff.setSampleRate(MOVE_SAMPLE_RATE);
    inst->smoothCutoff.setCurrentAndTargetValue(1.0f);
    inst->smoothResonance.setTimeConstant(10e-3f);
    inst->smoothResonance.setSampleRate(MOVE_SAMPLE_RATE);
    inst->smoothVCFEnvModDepth.setTimeConstant(10e-3f);
    inst->smoothVCFEnvModDepth.setSampleRate(MOVE_SAMPLE_RATE);
    inst->smoothVCFLFOModDepth.setTimeConstant(10e-3f);
    inst->smoothVCFLFOModDepth.setSampleRate(MOVE_SAMPLE_RATE);
    inst->smoothVCFKeyboardModDepth.setTimeConstant(10e-3f);
    inst->smoothVCFKeyboardModDepth.setSampleRate(MOVE_SAMPLE_RATE);
    inst->smoothVCFBendDepth.setTimeConstant(10e-3f);
    inst->smoothVCFBendDepth.setSampleRate(MOVE_SAMPLE_RATE);

    /* Initialize effects */
    inst->hpFilter.init(MOVE_SAMPLE_RATE);
    inst->vca.init(MOVE_SAMPLE_RATE);
    inst->chorus.init(MOVE_SAMPLE_RATE);

    /* Initialize voices */
    for (int i = 0; i < MAX_VOICES; i++) {
        inst->voices[i].setSampleRate(MOVE_SAMPLE_RATE);
    }

    /* Set default parameters */
    for (int i = 0; i < kHeraNumParameters; i++) {
        apply_param(inst, i, g_param_defaults[i]);
    }

    /* Load presets */
    if (load_presets(inst) > 0) {
        inst->current_preset = 0;
        apply_preset(inst, 0);
    }

    plugin_log("Hera v2: Instance created");
    return inst;
}

static void v2_destroy_instance(void *instance) {
    hera_instance_t *inst = (hera_instance_t*)instance;
    if (!inst) return;
    delete inst;
    plugin_log("Hera v2: Instance destroyed");
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    hera_instance_t *inst = (hera_instance_t*)instance;
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
    case 0x90:
        if (data2 > 0) {
            note_on(inst, note, data2 / 127.0f);
        } else {
            note_off(inst, note);
        }
        break;
    case 0x80:
        note_off(inst, note);
        break;
    case 0xB0:
        switch (data1) {
        case 1: /* Mod wheel - not used by Juno-60 */
            break;
        case 64:
            /* Sustain pedal - hold notes */
            /* The Juno-60 doesn't have sustain pedal support in the original,
               but we could add it. For now, ignore. */
            break;
        case 120: /* All Sound Off */
        case 123: /* All Notes Off */
            all_notes_off(inst);
            break;
        }
        break;
    case 0xE0: {
        int bend = ((data2 << 7) | data1) - 8192;
        inst->pitchBendSemitones = (bend / 8192.0f) * 7.0f;

        /* Update all active voice frequencies */
        float bendFactor = std::exp2(inst->pitchBendSemitones / 12.0f);
        for (int i = 0; i < MAX_VOICES; i++) {
            if (inst->voices[i].active) {
                float baseFreq = midi_to_freq(inst->voices[i].note);
                inst->voices[i].dco.setFrequency(baseFreq * bendFactor);
            }
        }
        break;
    }
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    hera_instance_t *inst = (hera_instance_t*)instance;
    if (!inst) return;

    /* State restore from patch save */
    if (strcmp(key, "state") == 0) {
        float fval;

        if (json_get_number(val, "preset", &fval) == 0) {
            int idx = (int)fval;
            if (idx >= 0 && idx < inst->preset_count) {
                inst->current_preset = idx;
                apply_preset(inst, idx);
            }
        }

        if (json_get_number(val, "octave_transpose", &fval) == 0) {
            inst->octave_transpose = (int)fval;
            if (inst->octave_transpose < -3) inst->octave_transpose = -3;
            if (inst->octave_transpose > 3) inst->octave_transpose = 3;
        }

        /* (mpc port: the state saves "volume" but the original never read it back) */
        if (json_get_number(val, "volume", &fval) == 0) {
            inst->volume = fval;
            if (inst->volume < 0.0f) inst->volume = 0.0f;
            if (inst->volume > 1.0f) inst->volume = 1.0f;
        }

        /* Restore all shadow params */
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            if (json_get_number(val, g_shadow_params[i].key, &fval) == 0) {
                if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
                if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
                apply_param(inst, g_shadow_params[i].index, fval);
            }
        }
        return;
    }

    if (strcmp(key, "preset") == 0) {
        int idx = atoi(val);
        if (idx >= 0 && idx < inst->preset_count && idx != inst->current_preset) {
            all_notes_off(inst);
            inst->current_preset = idx;
            apply_preset(inst, idx);
        }
    }
    else if (strcmp(key, "volume") == 0) {
        inst->volume = (float)atof(val);
        if (inst->volume < 0.0f) inst->volume = 0.0f;
        if (inst->volume > 1.0f) inst->volume = 1.0f;
    }
    else if (strcmp(key, "octave_transpose") == 0) {
        inst->octave_transpose = atoi(val);
        if (inst->octave_transpose < -3) inst->octave_transpose = -3;
        if (inst->octave_transpose > 3) inst->octave_transpose = 3;
    }
    else if (strcmp(key, "all_notes_off") == 0) {
        all_notes_off(inst);
    }
    else {
        /* Named parameter access via helper (for shadow UI) */
        float fval = (float)atof(val);
        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            if (strcmp(key, g_shadow_params[i].key) == 0) {
                if (fval < g_shadow_params[i].min_val) fval = g_shadow_params[i].min_val;
                if (fval > g_shadow_params[i].max_val) fval = g_shadow_params[i].max_val;
                apply_param(inst, g_shadow_params[i].index, fval);
                return;
            }
        }
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    hera_instance_t *inst = (hera_instance_t*)instance;
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
        return snprintf(buf, buf_len, "Hera");
    }
    if (strcmp(key, "volume") == 0) {
        return snprintf(buf, buf_len, "%.3f", inst->volume);
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
                    "\"children\":null,"
                    "\"knobs\":[\"volume\",\"vcf_cutoff\",\"vcf_resonance\",\"vcf_env\",\"attack\",\"decay\",\"sustain\",\"octave_transpose\"],"
                    "\"params\":["
                        "{\"level\":\"dco\",\"label\":\"DCO\"},"
                        "{\"level\":\"vcf\",\"label\":\"VCF\"},"
                        "{\"level\":\"vca\",\"label\":\"VCA\"},"
                        "{\"level\":\"env\",\"label\":\"Envelope\"},"
                        "{\"level\":\"lfo\",\"label\":\"LFO\"},"
                        "{\"level\":\"effects\",\"label\":\"Effects\"}"
                    "]"
                "},"
                "\"dco\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"saw_level\",\"pulse_level\",\"sub_level\",\"noise_level\",\"pwm_depth\",\"pwm_mod\",\"pitch_range\",\"pitch_mod\"],"
                    "\"params\":[\"saw_level\",\"pulse_level\",\"sub_level\",\"noise_level\",\"pwm_depth\",\"pwm_mod\",\"pitch_range\",\"pitch_mod\"]"
                "},"
                "\"vcf\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"vcf_cutoff\",\"vcf_resonance\",\"vcf_env\",\"vcf_lfo\",\"vcf_key\",\"vcf_bend\"],"
                    "\"params\":[\"vcf_cutoff\",\"vcf_resonance\",\"vcf_env\",\"vcf_lfo\",\"vcf_key\",\"vcf_bend\"]"
                "},"
                "\"vca\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"vca_depth\",\"vca_type\"],"
                    "\"params\":[\"vca_depth\",\"vca_type\"]"
                "},"
                "\"env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":[\"attack\",\"decay\",\"sustain\",\"release\"]"
                "},"
                "\"lfo\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"lfo_rate\",\"lfo_delay\",\"lfo_trigger\"],"
                    "\"params\":[\"lfo_rate\",\"lfo_delay\",\"lfo_trigger\"]"
                "},"
                "\"effects\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"hpf\",\"chorus_i\",\"chorus_ii\"],"
                    "\"params\":[\"hpf\",\"chorus_i\",\"chorus_ii\"]"
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
            "{\"preset\":%d,\"volume\":%.4f,\"octave_transpose\":%d",
            inst->current_preset, inst->volume, inst->octave_transpose);
        if (offset >= buf_len) return -1;

        for (int i = 0; i < (int)PARAM_DEF_COUNT(g_shadow_params); i++) {
            float val = inst->params[g_shadow_params[i].index];
            offset += snprintf(buf + offset, buf_len - offset,
                ",\"%s\":%.4f", g_shadow_params[i].key, val);
            if (offset >= buf_len) return -1;
        }

        offset += snprintf(buf + offset, buf_len - offset, "}");
        if (offset >= buf_len) return -1;
        return offset;
    }

    /* Chain params metadata */
    if (strcmp(key, "chain_params") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset,
            "[{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":9999},"
            "{\"key\":\"volume\",\"name\":\"Volume\",\"type\":\"float\",\"min\":0,\"max\":1},"
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
    hera_instance_t *inst = (hera_instance_t*)instance;
    if (!inst) {
        memset(out_interleaved_lr, 0, frames * 4);
        return;
    }

    if (frames > MAX_BLOCK_SIZE) frames = MAX_BLOCK_SIZE;

    /* Clear mix buffer */
    memset(inst->mixBuffer, 0, frames * sizeof(float));

    /* Process LFO */
    inst->lfo.processBlock(inst->lfoBuffer, frames);

    /* Process detune (pitch modulation from LFO) */
    float detuneFactor = inst->pitchFactor;
    for (int i = 0; i < frames; i++) {
        inst->detuneBuffer[i] = detuneFactor *
            std::exp2(inst->lfoBuffer[i] * 0.25f *
                      inst->smoothPitchModDepth.getNextValue());
    }

    /* Process cutoff and resonance smoothing */
    for (int i = 0; i < frames; i++) {
        float cutoff = inst->smoothCutoff.getNextValue();
        float resonance = inst->smoothResonance.getNextValue();

        float cutoffDetuneOctaves = cutoff * (200.0f / 12.0f);
        float resonanceDetuneOctaves = resonance * 0.5f;

        inst->cutoffOctavesBuffer[i] = cutoffDetuneOctaves + resonanceDetuneOctaves;
        inst->resonanceBuffer[i] = resonance;
        inst->vcfEnvModBuffer[i] = inst->smoothVCFEnvModDepth.getNextValue();
        inst->vcfLFODetuneOctavesBuffer[i] = inst->smoothVCFLFOModDepth.getNextValue() *
            inst->lfoBuffer[i] * 3.0f;
        inst->vcfKeyboardModBuffer[i] = inst->smoothVCFKeyboardModDepth.getNextValue();
        inst->vcfBendDepthBuffer[i] = inst->smoothVCFBendDepth.getNextValue();
    }

    /* Render all active voices into mix buffer */
    for (int v = 0; v < MAX_VOICES; v++) {
        if (inst->voices[v].active) {
            render_voice(inst, inst->voices[v], inst->mixBuffer, frames);
        }
    }

    /* Apply HPF (mono, in-place) */
    {
        float *inPtr[1] = { inst->mixBuffer };
        float *outPtr[1] = { inst->mixBuffer };
        inst->hpFilter.compute(frames, inPtr, outPtr);
    }

    /* Apply VCA (mono, in-place) */
    {
        float *inPtr[1] = { inst->mixBuffer };
        float *outPtr[1] = { inst->mixBuffer };
        inst->vca.compute(frames, inPtr, outPtr);
    }

    /* Soft clip */
    soft_clip(inst->mixBuffer, frames);

    /* Apply chorus (mono -> stereo) */
    {
        float *inPtr[1] = { inst->mixBuffer };
        float *outPtr[2] = { inst->chorusOutL, inst->chorusOutR };
        inst->chorus.compute(frames, inPtr, outPtr);
    }

    /* Convert to int16 stereo interleaved */
    float gain = inst->output_gain * inst->volume;
    for (int i = 0; i < frames; i++) {
        float left = inst->chorusOutL[i] * gain;
        float right = inst->chorusOutR[i] * gain;

        int32_t l = (int32_t)(left * 32767.0f);
        int32_t r = (int32_t)(right * 32767.0f);

        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;

        out_interleaved_lr[i * 2] = (int16_t)l;
        out_interleaved_lr[i * 2 + 1] = (int16_t)r;
    }
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    (void)buf;
    (void)buf_len;
    return 0;
}

/* =====================================================================
 * Plugin API v2 table and entry point
 * ===================================================================== */

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
