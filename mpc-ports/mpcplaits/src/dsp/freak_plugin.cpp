#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// mpc-vst-plaits: schwung-mrhyde's plugin_api_v2 entry point (see src/VENDORED.md), rewritten around one parameter
// table that mirrors src/module.json. MrHyde's Schwung-only UI metadata (ui_hierarchy, dynamic chain_params) is
// gone: MPC OS never asks for it.

extern "C" {
#define MOVE_PLUGIN_API_VERSION 1
#define MOVE_PLUGIN_API_VERSION_2 2
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

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;
}

#include "plaits_move_engine.h"

namespace {

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int16_t float_to_i16(float v) {
    float s = clampf(v, -1.0f, 1.0f) * 32767.0f;
    int x = (int)lrintf(s);
    if (x < -32768) x = -32768;
    if (x > 32767) x = 32767;
    return (int16_t)x;
}

typedef struct {
    ppf_engine_t engine;
    ppf_params_t params;
    char last_error[256];
    // Sustain pedal (CC 64): keys released while it is down stay on until it comes up.
    bool sustain;
    bool sustained[128];
    float notes_last;   // NOTES: the last position the host asked for (-1 = none yet), see set_param
} freak_instance_t;

// ---- the parameter table: one entry per src/module.json chain_params key (except the wrapper-only
// model_prev / model_next). Enums travel as option indices, in module.json's option order.
enum ParamType { P_FLOAT, P_INT, P_ENUM };

struct ParamDef {
    char key[24];
    ParamType type;
    size_t offset;   // into ppf_params_t
    float lo, hi;    // enums: 0 .. count - 1
};

constexpr int kMaxParams = 192;
static ParamDef g_defs[kMaxParams];
static int g_ndefs = 0;

static void add(ParamType type, size_t offset, float lo, float hi, const char *fmt, int a = 0, int b = 0) {
    if (g_ndefs >= kMaxParams) abort();   // the table outgrew kMaxParams: fail loudly, not by dropping parameters
    ParamDef &d = g_defs[g_ndefs++];
    snprintf(d.key, sizeof d.key, fmt, a, b);
    d.type = type;
    d.offset = offset;
    d.lo = lo;
    d.hi = hi;
}

static void build_param_table() {
    if (g_ndefs) return;
#define OFF(f) offsetof(ppf_params_t, f)
    add(P_ENUM, OFF(model), 0, 23, "model");
    add(P_FLOAT, OFF(pitch), -48, 48, "pitch");
    add(P_FLOAT, OFF(harmonics), 0, 1, "harmonics");
    add(P_FLOAT, OFF(timbre), 0, 1, "timbre");
    add(P_FLOAT, OFF(morph), 0, 1, "morph");
    add(P_FLOAT, OFF(timbre_att), -1, 1, "timbre_att");
    add(P_FLOAT, OFF(fm_att), -1, 1, "fm_att");
    add(P_FLOAT, OFF(morph_att), -1, 1, "morph_att");
    add(P_FLOAT, OFF(aux_mix), 0, 1, "aux_mix");

    add(P_ENUM, OFF(amp_mode), 0, 3, "amp_mode");
    add(P_FLOAT, OFF(lpg_color), 0, 1, "lpg_color");
    add(P_FLOAT, OFF(lpg_decay), 0, 1, "lpg_decay");
    add(P_ENUM, OFF(trig_rate), 0, 7, "trig_rate");
    add(P_ENUM, OFF(voice_mode), 0, 2, "voice_mode");
    add(P_INT, OFF(polyphony), 1, 8, "polyphony");
    add(P_INT, OFF(unison), 1, 8, "unison");
    add(P_FLOAT, OFF(detune), 0, 1, "detune");
    add(P_FLOAT, OFF(spread), 0, 1, "spread");
    add(P_INT, OFF(glide_ms), 0, 2000, "glide_ms");
    add(P_INT, OFF(bend_range), 0, 24, "bend_range");
    add(P_FLOAT, OFF(velocity_curve), 0.1f, 4, "velocity_curve");
    add(P_FLOAT, OFF(poly_aftertouch_curve), -1, 1, "poly_aftertouch_curve");
    add(P_ENUM, OFF(filter_mode), 0, 2, "filter_mode");
    add(P_FLOAT, OFF(filter_cutoff), 0, 1, "filter_cutoff");
    add(P_FLOAT, OFF(filter_resonance), 0, 1, "filter_resonance");
    add(P_FLOAT, OFF(volume), 0, 2, "volume");
    add(P_FLOAT, OFF(pan), -1, 1, "pan");

    for (int n = 0; n < PPF_NUM_ENVS; ++n) {
        size_t base = OFF(env) + n * sizeof(ppf_env_params_t);
        add(P_INT, base + offsetof(ppf_env_params_t, attack_ms), 0, 5000, "env%d_attack", n + 1);
        add(P_INT, base + offsetof(ppf_env_params_t, decay_ms), 0, 5000, "env%d_decay", n + 1);
        add(P_FLOAT, base + offsetof(ppf_env_params_t, sustain), 0, 1, "env%d_sustain", n + 1);
        add(P_INT, base + offsetof(ppf_env_params_t, release_ms), 0, 5000, "env%d_release", n + 1);
        add(P_ENUM, base + offsetof(ppf_env_params_t, retrig), 0, 1, "env%d_retrig", n + 1);
    }
    for (int n = 0; n < PPF_NUM_LFOS; ++n) {
        size_t base = OFF(lfo) + n * sizeof(ppf_lfo_params_t);
        add(P_ENUM, base + offsetof(ppf_lfo_params_t, shape), 0, 5, "lfo%d_shape", n + 1);
        add(P_FLOAT, base + offsetof(ppf_lfo_params_t, rate), 0.01f, 40, "lfo%d_rate", n + 1);
        add(P_ENUM, base + offsetof(ppf_lfo_params_t, sync), 0, 1, "lfo%d_sync", n + 1);
        add(P_ENUM, base + offsetof(ppf_lfo_params_t, div), 0, 10, "lfo%d_div", n + 1);
        add(P_ENUM, base + offsetof(ppf_lfo_params_t, retrig), 0, 1, "lfo%d_retrig", n + 1);
        add(P_ENUM, base + offsetof(ppf_lfo_params_t, per_voice), 0, 1, "lfo%d_per_voice", n + 1);
        add(P_FLOAT, base + offsetof(ppf_lfo_params_t, phase), 0, 1, "lfo%d_phase", n + 1);
    }
    add(P_INT, OFF(cycle_attack_ms), 1, 5000, "cycle_attack_ms");
    add(P_INT, OFF(cycle_decay_ms), 1, 5000, "cycle_decay_ms");
    add(P_ENUM, OFF(cycle_shape), 0, 2, "cycle_shape");
    add(P_ENUM, OFF(cycle_bipolar), 0, 1, "cycle_bipolar");
    add(P_ENUM, OFF(cycle_retrig), 0, 1, "cycle_retrig");
    add(P_ENUM, OFF(random_mode), 0, 2, "random_mode");
    add(P_FLOAT, OFF(random_rate), 0.01f, 40, "random_rate");
    add(P_ENUM, OFF(random_sync), 0, 1, "random_sync");
    add(P_ENUM, OFF(random_div), 0, 10, "random_div");
    add(P_FLOAT, OFF(random_slew), 0, 1, "random_slew");
    add(P_ENUM, OFF(random_retrig), 0, 1, "random_retrig");

    static const char *const src_keys[PPF_GRID] = {"lfo1", "env2", "cyc", "rnd"};
    static const char *const dst_keys[PPF_GRID] = {"pitch", "harm", "timbre", "morph"};
    for (int r = 0; r < PPF_GRID; ++r) {
        for (int c = 0; c < PPF_GRID; ++c) {
            char fmt[32];
            snprintf(fmt, sizeof fmt, "mod_%s_%s", src_keys[r], dst_keys[c]);
            add(P_FLOAT, OFF(mod_fixed) + (r * PPF_GRID + c) * sizeof(float), -1, 1, fmt);
            snprintf(fmt, sizeof fmt, "mod_%s_%s_on", src_keys[r], dst_keys[c]);
            add(P_ENUM, OFF(mod_on) + (r * PPF_GRID + c) * sizeof(int), 0, 1, fmt);
        }
    }
    for (int n = 0; n < PPF_GRID; ++n) {
        add(P_ENUM, OFF(asg_src) + n * sizeof(int), 0, PPF_SRC_COUNT - 1, "asg_src%d", n + 1);
        add(P_ENUM, OFF(asg_dst) + n * sizeof(int), 0, PPF_DST_COUNT - 1, "asg_dst%d", n + 1);
    }
    for (int r = 0; r < PPF_GRID; ++r) {
        for (int c = 0; c < PPF_GRID; ++c) {
            add(P_FLOAT, OFF(asg_amt) + (r * PPF_GRID + c) * sizeof(float), -1, 1, "asg_%d_%d", r + 1, c + 1);
            add(P_ENUM, OFF(asg_on) + (r * PPF_GRID + c) * sizeof(int), 0, 1, "asg_%d_%d_on", r + 1, c + 1);
        }
    }
#undef OFF
}

static const ParamDef *find_param(const char *key) {
    for (int i = 0; i < g_ndefs; ++i) {
        if (strcmp(g_defs[i].key, key) == 0) return &g_defs[i];
    }
    return NULL;
}

static int set_value(ppf_params_t *p, const ParamDef *d, const char *val) {
    char *end = NULL;
    float v = strtof(val, &end);
    if (end == val) return 0;
    char *field = (char *)p + d->offset;
    if (d->type == P_FLOAT) {
        *(float *)field = clampf(v, d->lo, d->hi);
    } else {
        *(int *)field = (int)lrintf(clampf(v, d->lo, d->hi));
    }
    return 1;
}

static int get_value(const ppf_params_t *p, const ParamDef *d, char *buf, int buf_len) {
    const char *field = (const char *)p + d->offset;
    if (d->type == P_FLOAT) return snprintf(buf, buf_len, "%.6g", *(const float *)field);
    return snprintf(buf, buf_len, "%d", *(const int *)field);
}

static void set_error(freak_instance_t *inst, const char *msg) {
    snprintf(inst->last_error, sizeof(inst->last_error), "%s", msg ? msg : "");
}

// The chunk MPC saves with a project: {"key":value,...} over the whole table.
static int build_state_json(const freak_instance_t *inst, char *buf, int buf_len) {
    int off = snprintf(buf, buf_len, "{");
    for (int i = 0; i < g_ndefs && off < buf_len; ++i) {
        char value[32];
        get_value(&inst->params, &g_defs[i], value, sizeof value);
        off += snprintf(buf + off, buf_len - off, "%s\"%s\":%s", i ? "," : "", g_defs[i].key, value);
    }
    if (off < buf_len) off += snprintf(buf + off, buf_len - off, "}");
    return off < buf_len ? off : -1;
}

static void apply_state_json(freak_instance_t *inst, const char *json) {
    for (int i = 0; i < g_ndefs; ++i) {
        char pattern[32];
        snprintf(pattern, sizeof pattern, "\"%.23s\"", g_defs[i].key);
        const char *at = strstr(json, pattern);
        if (!at) continue;
        at += strlen(pattern);
        while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r') at++;
        if (*at != ':') continue;
        set_value(&inst->params, &g_defs[i], at + 1);
    }
}

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    build_param_table();
    freak_instance_t *inst = new freak_instance_t();
    ppf_default_params(&inst->params);
    inst->sustain = false;
    memset(inst->sustained, 0, sizeof inst->sustained);
    inst->notes_last = -1.0f;
    if (json_defaults && json_defaults[0]) apply_state_json(inst, json_defaults);
    inst->engine.init();
    inst->engine.set_params(inst->params);
    set_error(inst, NULL);
    return inst;
}

static void destroy_instance(void *instance) {
    delete (freak_instance_t *)instance;
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !msg || len < 1) return;

    uint8_t status = msg[0] & 0xF0;
    if ((status == 0x90 || status == 0x80 || status == 0xA0 || status == 0xB0 || status == 0xE0) && len < 3) return;
    if (status == 0xD0 && len < 2) return;

    if (status == 0x90 && (msg[2] & 0x7F) != 0) {
        int note = msg[1] & 0x7F;
        inst->sustained[note] = false;
        inst->engine.note_on(note, (float)(msg[2] & 0x7F) / 127.0f);
    } else if (status == 0x80 || status == 0x90) {
        int note = msg[1] & 0x7F;
        if (inst->sustain) {
            inst->sustained[note] = true;
        } else {
            inst->engine.note_off(note);
        }
    } else if (status == 0xB0) {
        int cc = msg[1] & 0x7F;
        int value = msg[2] & 0x7F;
        if (cc == 1) {
            inst->engine.mod_wheel((float)value / 127.0f);
        } else if (cc == 64) {
            bool down = value >= 64;
            if (inst->sustain && !down) {
                for (int n = 0; n < 128; ++n) {
                    if (inst->sustained[n]) {
                        inst->sustained[n] = false;
                        inst->engine.note_off(n);
                    }
                }
            }
            inst->sustain = down;
        } else if (cc == 121) {   // reset all controllers
            inst->engine.mod_wheel(0.0f);
            inst->engine.pitch_bend(0.0f);
            inst->engine.channel_pressure(0.0f);
        } else if (cc == 120 || cc == 123) {   // all sound off / all notes off
            inst->engine.all_notes_off();
            inst->sustain = false;
            memset(inst->sustained, 0, sizeof(inst->sustained));
        }
    } else if (status == 0xA0) {
        inst->engine.poly_aftertouch(msg[1] & 0x7F, (float)(msg[2] & 0x7F) / 127.0f);
    } else if (status == 0xD0) {
        inst->engine.channel_pressure((float)(msg[1] & 0x7F) / 127.0f);
    } else if (status == 0xE0) {
        int bend = ((msg[2] & 0x7F) << 7 | (msg[1] & 0x7F)) - 8192;
        inst->engine.pitch_bend((float)bend / (bend > 0 ? 8191.0f : 8192.0f));
    }
}

static void set_param(void *instance, const char *key, const char *val) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !key || !val) return;
    set_error(inst, NULL);

    if (strcmp(key, "state") == 0) {
        apply_state_json(inst, val);
    } else if (strcmp(key, "lfo_bpm") == 0) {   // the wrapper's HAS_LFO_BPM: host tempo
        inst->engine.set_tempo(strtof(val, NULL));
        return;
    } else if (strcmp(key, "transport") == 0) {   // the wrapper's HAS_TRANSPORT: play / jump back
        if (atoi(val)) inst->engine.transport_start();
        return;
    } else if (strcmp(key, "all_notes_off") == 0) {
        if (atoi(val)) inst->engine.all_notes_off();
        return;
    } else if (strcmp(key, "polyphony") == 0 && strtof(val, NULL) != 0.0f) {
        // NOTES is shown as text, so the wrapper passes it as a plain number without its whole-number settling
        // (vst2_wrap.c settle()): do the same here. Round toward the way the knob moves -- from the host's last
        // position while it sweeps, else from the current value -- so a data-wheel tick still moves one note and a
        // drag or Q-Link sweep doesn't flicker between two values.
        float v = clampf(strtof(val, NULL), 1.0f, 8.0f), cur = (float)inst->params.polyphony;
        float last = inst->notes_last;
        inst->notes_last = v;
        if (fabsf(v - roundf(v)) > 0.001f) {
            float dir = (last >= 0.0f && fabsf(v - last) < 0.5f) ? v - last : v - cur;
            v = dir > 0 ? ceilf(v - 0.001f) : dir < 0 ? floorf(v + 0.001f) : cur;
        }
        inst->params.polyphony = (int)lrintf(clampf(v, 1.0f, 8.0f));
    } else {
        const ParamDef *d = find_param(key);
        if (!d || !set_value(&inst->params, d, val)) {
            char line[128];
            snprintf(line, sizeof(line), "unknown/invalid param: %s=%s", key, val);
            set_error(inst, line);
            return;
        }
    }
    inst->engine.set_params(inst->params);
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;
    if (strcmp(key, "name") == 0) return snprintf(buf, buf_len, "MPC Plaits");
    if (strcmp(key, "state") == 0) return build_state_json(inst, buf, buf_len);
    // NOTES ("display": "string"): the setting, then the notes that can really sound when UNISON caps it, e.g.
    // "4 (2)". The wrapper reads the leading number back as the value.
    if (strcmp(key, "polyphony") == 0) {
        int notes = inst->params.polyphony, real = inst->engine.effective_notes();
        if (inst->params.voice_mode == 1 && real < notes) return snprintf(buf, buf_len, "%d (%d)", notes, real);
        return snprintf(buf, buf_len, "%d", notes);
    }
    const ParamDef *d = find_param(key);
    if (!d) return -1;
    return get_value(&inst->params, d, buf, buf_len);
}

static int get_error(void *instance, char *buf, int buf_len) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !buf || buf_len <= 0) return -1;
    return snprintf(buf, buf_len, "%s", inst->last_error);
}

static void render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !out_interleaved_lr || frames <= 0) return;

    int offset = 0;
    float left[PPF_MAX_RENDER];
    float right[PPF_MAX_RENDER];

    while (offset < frames) {
        int n = frames - offset;
        if (n > PPF_MAX_RENDER) n = PPF_MAX_RENDER;

        inst->engine.render(left, right, n);

        for (int i = 0; i < n; ++i) {
            out_interleaved_lr[(offset + i) * 2 + 0] = float_to_i16(left[i]);
            out_interleaved_lr[(offset + i) * 2 + 1] = float_to_i16(right[i]);
        }

        offset += n;
    }
}

static plugin_api_v2_t g_api_v2 = {
    MOVE_PLUGIN_API_VERSION_2,
    &create_instance,
    &destroy_instance,
    &on_midi,
    &set_param,
    &get_param,
    &get_error,
    &render_block
};

}  // namespace

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    (void)host;
    build_param_table();
    return &g_api_v2;
}
