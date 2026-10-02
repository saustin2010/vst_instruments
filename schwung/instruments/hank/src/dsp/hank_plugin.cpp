/*
 * Hank — Schwung plugin_api_v2 glue.
 *
 * EVERY entry point here runs on the SPI callback. No allocation after
 * create_instance, no file I/O, no logging. The preset bank is a static table
 * (hank_presets.h) precisely so that loading one is a memcpy of floats.
 */
#include "plugin_api_v1.h"
#include "hank_engine.h"
#include "hank_curves.h"
#include "hank_presets.h"
#include "hank_contract.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

using namespace hank;

struct Instance {
    Engine engine;
    int    preset = -1;
    char   err[128];
    float  fbuf[2 * 512];      /* render scratch; 128 frames typical, headroom */
};

static const host_api_v1_t *g_host = 0;
static plugin_api_v2_t      g_api;

/* ------------------------------------------------------------ parameters */
/* One table: name -> where it lives. Keeps set/get/state in agreement, which
 * is the thing that drifts when each is written out by hand. */

enum PType { P_FLOAT, P_INT, P_BOOL };

struct ParamDef {
    const char *key;
    PType       type;
    float       lo, hi;
    size_t      off;            /* offset into hank::Params */
};

#define POFF(m) offsetof(Params, m)

static const ParamDef PARAMS[] = {
    /* THE EIGHT. These are the instrument; everything below is the machine.
     * While macro_mode is on -- the default -- the raw fields are outputs of
     * applyMacros and writing to them does nothing that survives the next
     * block. Turn macro_mode off to drive the machine directly, which is what
     * the tuning page is for. */
    /* P_FLOAT even though the CONTRACT declares it an int enum: P_INT writes
     * through an int pointer, and m.ratio is a float field -- so P_INT here
     * reinterpreted the storage and the knob became completely inert, every
     * one of its 16 steps rendering identically. The contract type is what the
     * UI draws; this table is only how the value is stored. */
    { "ratio",        P_FLOAT, 0.0f, 15.0f, POFF(m.ratio) },
    { "bright",       P_FLOAT, 0.0f, 1.0f,  POFF(m.bright) },
    { "bite",         P_FLOAT, 0.0f, 1.0f,  POFF(m.bite) },
    { "attack",       P_FLOAT, 0.0f, 1.0f,  POFF(m.attack) },
    { "decay",        P_FLOAT, 0.0f, 1.0f,  POFF(m.decay) },
    { "sustain",      P_FLOAT, 0.0f, 1.0f,  POFF(m.sustain) },
    { "noise",        P_FLOAT, 0.0f, 1.0f,  POFF(m.noise) },
    /* Not on any page: a preset writes it, the player never sees it. */
    { "preset_gain",  P_FLOAT, 0.0f, 4.0f,  POFF(preset_gain) },
    { "tone",         P_FLOAT, 0.0f, 1.0f,  POFF(m.tone) },
    { "macro_mode",   P_BOOL,  0.0f, 1.0f,  POFF(macro_mode) },

    { "op2_coarse",   P_FLOAT, 0.5f, 32.0f, POFF(op2_coarse) },
    { "op2_fine",     P_FLOAT, 0.0f, 1.0f,  POFF(op2_fine) },
    { "op2_level",    P_FLOAT, 0.0f, 1.0f,  POFF(op2_level) },
    { "op2_fbk",      P_FLOAT, 0.0f, 1.0f,  POFF(op2_fbk) },
    { "op2_attack",   P_FLOAT, 0.0f, 1.0f,  POFF(op2_a) },
    { "op2_decay",    P_FLOAT, 0.0f, 1.0f,  POFF(op2_d) },
    { "op2_sustain",  P_FLOAT, 0.0f, 1.0f,  POFF(op2_s) },
    { "op2_release",  P_FLOAT, 0.0f, 1.0f,  POFF(op2_r) },
    { "op2_phase_reset", P_BOOL, 0.0f, 1.0f, POFF(op2_phase_reset) },
    { "op2_phase",    P_FLOAT, 0.0f, 1.0f,  POFF(op2_phase) },
    { "op1_attack",   P_FLOAT, 0.0f, 1.0f,  POFF(op1_a) },
    { "op1_decay",    P_FLOAT, 0.0f, 1.0f,  POFF(op1_d) },
    { "op1_sustain",  P_FLOAT, 0.0f, 1.0f,  POFF(op1_s) },
    { "op1_release",  P_FLOAT, 0.0f, 1.0f,  POFF(op1_r) },
    { "op1_phase_reset", P_BOOL, 0.0f, 1.0f, POFF(op1_phase_reset) },
    { "op1_phase",    P_FLOAT, 0.0f, 1.0f,  POFF(op1_phase) },
    { "cutoff",       P_FLOAT, 0.0f, 1.0f,  POFF(cutoff) },
    { "resonance",    P_FLOAT, 0.0f, 1.0f,  POFF(res) },
    { "filter_mix",   P_FLOAT, 0.0f, 1.0f,  POFF(mix) },
    { "filter_env",   P_FLOAT, 0.0f, 1.0f,  POFF(env_depth) },
    { "filter_attack",  P_FLOAT, 0.0f, 1.0f, POFF(f_a) },
    { "filter_decay",   P_FLOAT, 0.0f, 1.0f, POFF(f_d) },
    { "filter_sustain", P_FLOAT, 0.0f, 1.0f, POFF(f_s) },
    { "filter_release", P_FLOAT, 0.0f, 1.0f, POFF(f_r) },
    { "filter_on",    P_BOOL,  0.0f, 1.0f,  POFF(filter_on) },
    { "keytrack",     P_BOOL,  0.0f, 1.0f,  POFF(keytrack) },
    { "voice_count",  P_INT,   1.0f, 16.0f, POFF(voice_count) },
    { "unison",       P_FLOAT, 0.0f, 1.0f,  POFF(unison) },
    { "glide",        P_FLOAT, 0.0f, 1.0f,  POFF(glide) },
    { "pitch",        P_FLOAT, -96.0f, 96.0f, POFF(pitch) },
    { "crush",        P_FLOAT, 0.0f, 1.0f,  POFF(crush) },
    { "volume",       P_FLOAT, 0.0f, 1.0f,  POFF(volume) },
    { "width",        P_FLOAT, 0.0f, 1.0f,  POFF(width) },
    { "distortion",   P_FLOAT, 0.0f, 1.0f,  POFF(dist) },
};
static const int PARAM_COUNT = (int)(sizeof(PARAMS) / sizeof(PARAMS[0]));

/* op2_fine is not a Params field of its own -- it is the fractional part of
 * op2_ratio. Tracked separately so the two knobs stay independent. */
struct Extra { float fine; };
static Extra g_extra_default = { 0.0f };

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static const ParamDef *findParam(const char *key) {
    for (int i = 0; i < PARAM_COUNT; i++)
        if (strcmp(PARAMS[i].key, key) == 0) return &PARAMS[i];
    return 0;
}

static float *fieldPtr(Params &p, const ParamDef *d) {
    return (float *)((char *)&p + d->off);
}
static int *intFieldPtr(Params &p, const ParamDef *d) {
    return (int *)((char *)&p + d->off);
}

static const char *presetNameAt(const Instance *, int i) {
    return (i >= 0 && i < HANK_PRESET_COUNT) ? HANK_PRESETS[i].name : "";
}

static void applyPreset(Instance *in, int idx) {
    if (idx < 0 || idx >= HANK_PRESET_COUNT) return;
    const PresetDef &d = HANK_PRESETS[idx];
    /* Stop whatever is still sounding first -- see Engine::releaseAll. */
    in->engine.releaseAll(0.008f);
    Params &p = in->engine.params();
    /* A preset is the EIGHT plus the handful of setup values. The raw machine
     * is not stored: it is derived by applyMacros, so storing it would let a
     * preset disagree with its own macros. */
    p.m.ratio   = d.v[PF_RATIO];    p.m.bright  = d.v[PF_BRIGHT];
    p.m.bite    = d.v[PF_BITE];     p.m.tone    = d.v[PF_TONE];
    p.m.attack  = d.v[PF_ATTACK];   p.m.decay   = d.v[PF_DECAY];
    p.m.sustain = d.v[PF_SUSTAIN];  p.m.noise   = d.v[PF_NOISE];
    p.macro_mode  = (int)d.v[PF_MACRO_MODE];
    p.voice_count = (int)d.v[PF_VOICE_COUNT];
    p.glide  = d.v[PF_GLIDE];   p.pitch    = d.v[PF_PITCH];
    p.crush  = d.v[PF_CRUSH];   p.keytrack = (int)d.v[PF_KEYTRACK];
    p.volume = d.v[PF_VOLUME];  p.preset_gain = d.v[PF_PRESET_GAIN];
    applyMacros(p);             /* so a get_param right after a load agrees */
    in->preset = idx;
}


/* ------------------------------------------------------------- lifecycle */

static void *v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir; (void)json_defaults;
    Instance *in = new Instance();          /* create_instance may allocate ONCE */
    in->err[0] = 0;
    applyPreset(in, 0);
    return in;
}

static void v2_destroy_instance(void *instance) {
    delete (Instance *)instance;
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    Instance *in = (Instance *)instance;
    if (!in || len < 1) return;
    const uint8_t st = msg[0] & 0xF0;
    switch (st) {
    case 0x90:
        if (len >= 3) {
            if (msg[2] > 0) in->engine.noteOn(msg[1], msg[2]);
            else            in->engine.noteOff(msg[1]);
        }
        break;
    case 0x80: if (len >= 3) in->engine.noteOff(msg[1]); break;
    case 0xB0:
        if (len >= 3) {
            if (msg[1] == 64)  in->engine.sustain(msg[2] >= 64);
            else if (msg[1] == 123 || msg[1] == 120) in->engine.allNotesOff();
        }
        break;
    case 0xE0:
        if (len >= 3) {
            int v = ((int)msg[2] << 7) | msg[1];
            in->engine.pitchBend(((float)(v - 8192) / 8192.0f) * 2.0f);  /* +/-2 st */
        }
        break;
    default: break;
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    Instance *in = (Instance *)instance;
    if (!in || !key || !val) return;
    Params &p = in->engine.params();

    if (strcmp(key, "preset") == 0) { applyPreset(in, atoi(val)); return; }
    if (strcmp(key, "all_notes_off") == 0) { in->engine.allNotesOff(); return; }
    /* Restore the engine's own defaults. create_instance loads preset 0, so any
     * parameter a caller does not set otherwise inherits that preset -- fine on
     * the device, a silent trap for a bench that sets params explicitly. */
    if (strcmp(key, "init") == 0) {
        in->engine.allNotesOff();
        p = Params();
        in->preset = -1;
        return;
    }
    /*
     * RATIO CHANGED MEANING, SO OLD SAVED VALUES MUST BE CONVERTED.
     *
     * It was a float 0..1 scaled into RATIO_TABLE; it is now the table INDEX,
     * 0..15, so the grid and the screen reader can say "2.5" instead of
     * "0.28". Every state blob and user preset written before that -- and this
     * module shipped with them -- stores the old form, and 0.28 read as an
     * index is 0, i.e. a ratio of 0.5. The patch would still load, still
     * sound, and silently be a different instrument. That is the failure mode
     * worth spending code on.
     *
     * A FRACTIONAL value can only be the old form: an index never has one. The
     * two forms overlap only at exactly 0 and 1, where old 0.0 and new 0 agree
     * anyway, and old 1.0 (the very top of the old knob) is the single value
     * that migrates wrong -- to index 1 instead of 15. No factory preset used
     * it; the old bank topped out at 0.97.
     */
    if (strcmp(key, "ratio") == 0 && val) {
        const float v = (float)atof(val);
        if (v > 0.0f && v < 1.0f) {
            Params &p = ((Instance *)instance)->engine.params();
            int idx = (int)(v * (float)curves::RATIO_COUNT);
            if (idx > curves::RATIO_COUNT - 1) idx = curves::RATIO_COUNT - 1;
            p.m.ratio = (float)idx;
            return;
        }
    }
    if (strcmp(key, "state") == 0) {
        /* Opaque User-Preset blob: "k=v;k=v;..." over the same param table. */
        const char *s = val;
        char name[48]; char num[32];
        while (*s) {
            int n = 0; while (*s && *s != '=' && n < 47) name[n++] = *s++;
            name[n] = 0; if (*s == '=') s++;
            int m = 0; while (*s && *s != ';' && m < 31) num[m++] = *s++;
            num[m] = 0; if (*s == ';') s++;
            if (n) v2_set_param(instance, name, num);
        }
        return;
    }
    const ParamDef *d = findParam(key);
    if (!d) return;
    float v = (float)atof(val);
    switch (d->type) {
    case P_BOOL:  *intFieldPtr(p, d) = (v >= 0.5f) ? 1 : 0; break;
    case P_INT:   *intFieldPtr(p, d) = (int)clampf(v, d->lo, d->hi); break;
    default:      *fieldPtr(p, d) = clampf(v, d->lo, d->hi); break;
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    Instance *in = (Instance *)instance;
    if (!in || !key || !buf || buf_len < 2) return 0;
    Params &p = in->engine.params();

    /* THE SHADOW UI ASKS THE COMPONENT FOR ITS CONTRACT, not module.json. A
     * module that serves neither of these gets no knob grid at all -- just the
     * preset row, which is exactly what a missing contract looks like on the
     * device. Both are static strings, so this is a memcpy on the SPI callback. */
    if (strcmp(key, "ui_hierarchy") == 0)
        return snprintf(buf, buf_len, "%s", HANK_UI_HIERARCHY);
    if (strcmp(key, "chain_params") == 0)
        return snprintf(buf, buf_len, "%s", HANK_CHAIN_PARAMS);

    if (strcmp(key, "preset") == 0)
        return snprintf(buf, buf_len, "%d", in->preset);
    if (strcmp(key, "preset_count") == 0)
        return snprintf(buf, buf_len, "%d", HANK_PRESET_COUNT);
    if (strcmp(key, "preset_name") == 0)
        return snprintf(buf, buf_len, "%s", presetNameAt(in, in->preset));
    if (strncmp(key, "preset_name_", 12) == 0) {
        int i = atoi(key + 12);
        return snprintf(buf, buf_len, "%s", presetNameAt(in, i));
    }

    if (strcmp(key, "state") == 0) {
        int n = 0;
        for (int i = 0; i < PARAM_COUNT && n < buf_len - 24; i++) {
            const ParamDef *d = &PARAMS[i];
            float v = (d->type == P_BOOL || d->type == P_INT)
                        ? (float)*intFieldPtr(p, d) : *fieldPtr(p, d);
            n += snprintf(buf + n, buf_len - n, "%s=%.5f;", d->key, v);
        }
        return n;
    }

    const ParamDef *d = findParam(key);
    if (!d) return 0;
    switch (d->type) {
    case P_BOOL:
    case P_INT:   return snprintf(buf, buf_len, "%d", *intFieldPtr(p, d));
    default:      return snprintf(buf, buf_len, "%.4f", *fieldPtr(p, d));
    }
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    Instance *in = (Instance *)instance;
    if (!in || !buf || buf_len < 1) return 0;
    return snprintf(buf, buf_len, "%s", in->err);
}

static void v2_render_block(void *instance, int16_t *out_lr, int frames) {
    Instance *in = (Instance *)instance;
    if (!in || !out_lr || frames <= 0) return;
    if (frames > 512) frames = 512;
    in->engine.render(in->fbuf, frames);
    for (int i = 0; i < frames * 2; i++) {
        float v = in->fbuf[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        out_lr[i] = (int16_t)(v * 32767.0f);
    }
}

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    memset(&g_api, 0, sizeof g_api);
    g_api.api_version      = MOVE_PLUGIN_API_VERSION_2;
    g_api.create_instance  = v2_create_instance;
    g_api.destroy_instance = v2_destroy_instance;
    g_api.on_midi          = v2_on_midi;
    g_api.set_param        = v2_set_param;
    g_api.get_param        = v2_get_param;
    g_api.get_error        = v2_get_error;
    g_api.render_block     = v2_render_block;
    return &g_api;
}
