#include "mrdrums_params.h"

#include <stdio.h>
#include <string.h>

static const mrdrums_param_desc_t kGlobalParams[] = {
    {"g_master_vol", "Master Vol", "float", NULL, 0, 0.0f, 2.0f, 0.01f, 1.0f, NULL, NULL, NULL, NULL, NULL},
    {"g_polyphony", "Polyphony", "int", NULL, 0, 1.0f, 64.0f, 1.0f, 16.0f, NULL, NULL, NULL, NULL, NULL},
    {"g_vel_curve", "Velocity Curve", "enum", NULL, 0, 0.0f, 0.0f, 0.0f, 0.0f, "linear", "[\"linear\",\"soft\",\"hard\"]", NULL, NULL, NULL},
    {"g_humanize_ms", "Humanize", "float", NULL, 0, 0.0f, 50.0f, 0.1f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"g_rand_seed", "Random Seed", "int", NULL, 0, 0.0f, 2147483647.0f, 1.0f, 1.0f, NULL, NULL, NULL, NULL, NULL},
    {"g_rand_loop_steps", "Rand Loop Steps", "int", NULL, 0, 1.0f, 128.0f, 1.0f, 16.0f, NULL, NULL, NULL, NULL, NULL},
    {"ui_auto_select_pad", "Auto Select", "enum", NULL, 0, 0.0f, 0.0f, 0.0f, 0.0f, "on", "[\"off\",\"on\"]", NULL, NULL, NULL},
    {"ui_current_pad", "Current Pad", "int", NULL, 0, 1.0f, 16.0f, 1.0f, 1.0f, NULL, NULL, NULL, NULL, NULL},
    {"ui_pad_page", "Pad Page", "enum", NULL, 0, 0.0f, 0.0f, 0.0f, 0.0f, "main", "[\"main\",\"random\"]", NULL, NULL, NULL},
    {"ui_preset_path", "Load Preset", "filepath", NULL, 0, 0.0f, 0.0f, 0.0f, 0.0f, "", NULL, "/data", "[\".ablpreset\",\".json\"]", "/data/UserData/UserLibrary/Track Presets"},
};

static const mrdrums_pad_field_desc_t kPadFields[] = {
    {"sample_path", "Sample", "filepath", 0.0f, 0.0f, 0.0f, 0.0f, "", NULL, "/data", "[\".wav\",\".aif\",\".aiff\"]", "/data/UserData/UserLibrary/Samples"},
    {"vol", "Vol", "float", 0.0f, 2.0f, 0.01f, 1.0f, NULL, NULL, NULL, NULL, NULL},
    {"pan", "Pan", "float", -1.0f, 1.0f, 0.1f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"tune", "Tune", "float", -24.0f, 24.0f, 1.0f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"start", "Start", "float", 0.0f, 1.0f, 0.01f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"attack_ms", "Attack", "float", 0.0f, 5000.0f, 1.0f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"decay_ms", "Decay", "float", 0.0f, 5000.0f, 5.0f, 250.0f, NULL, NULL, NULL, NULL, NULL},
    {"choke_group", "Choke", "int", 0.0f, 16.0f, 1.0f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"mode", "Mode", "enum", 0.0f, 0.0f, 0.0f, 0.0f, "oneshot", "[\"gate\",\"oneshot\"]", NULL, NULL, NULL},
    {"rand_pan_amt", "Rand Pan", "float", 0.0f, 1.0f, 0.01f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"rand_vol_amt", "Rand Vol", "float", 0.0f, 1.0f, 0.01f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"rand_decay_amt", "Rand Decay", "float", 0.0f, 1.0f, 0.01f, 0.0f, NULL, NULL, NULL, NULL, NULL},
    {"chance_pct", "Chance", "float", 0.0f, 100.0f, 1.0f, 100.0f, NULL, NULL, NULL, NULL, NULL},
};

#define GLOBAL_COUNT ((int)(sizeof(kGlobalParams) / sizeof(kGlobalParams[0])))
#define PAD_FIELD_COUNT ((int)(sizeof(kPadFields) / sizeof(kPadFields[0])))

int mrdrums_make_pad_key(int pad_index, const char *suffix, char *out, size_t out_len) {
    if (!out || out_len < 8 || !suffix) return 0;
    if (pad_index < 1 || pad_index > MRDRUMS_PAD_COUNT) return 0;

    int n = snprintf(out, out_len, "p%02d_%s", pad_index, suffix);
    return n > 0 && (size_t)n < out_len;
}

int mrdrums_parse_pad_key(const char *key, int *pad_index, const char **suffix) {
    if (!key || strlen(key) < 6) return 0;
    if (key[0] != 'p') return 0;
    if (key[3] != '_') return 0;
    if (key[1] < '0' || key[1] > '9' || key[2] < '0' || key[2] > '9') return 0;

    int pad = (key[1] - '0') * 10 + (key[2] - '0');
    if (pad < 1 || pad > MRDRUMS_PAD_COUNT) return 0;
    const char *field_suffix = key + 4;
    if (!field_suffix[0]) return 0;

    if (pad_index) *pad_index = pad;
    if (suffix) *suffix = field_suffix;
    return 1;
}

const mrdrums_param_desc_t *mrdrums_global_params(int *count_out) {
    if (count_out) *count_out = GLOBAL_COUNT;
    return kGlobalParams;
}

const mrdrums_pad_field_desc_t *mrdrums_pad_fields(int *count_out) {
    if (count_out) *count_out = PAD_FIELD_COUNT;
    return kPadFields;
}

const mrdrums_param_desc_t *mrdrums_find_global_param(const char *key) {
    if (!key) return NULL;
    for (int i = 0; i < GLOBAL_COUNT; i++) {
        if (strcmp(key, kGlobalParams[i].key) == 0) {
            return &kGlobalParams[i];
        }
    }
    return NULL;
}

const mrdrums_param_desc_t *mrdrums_find_pad_param(const char *key) {
    static mrdrums_param_desc_t scratch;

    int pad = 0;
    const char *suffix = NULL;
    if (!mrdrums_parse_pad_key(key, &pad, &suffix)) return NULL;

    for (int i = 0; i < PAD_FIELD_COUNT; i++) {
        const mrdrums_pad_field_desc_t *f = &kPadFields[i];
        if (strcmp(suffix, f->suffix) != 0) continue;

        scratch.key = key;
        scratch.name = f->name;
        scratch.type = f->type;
        scratch.suffix = f->suffix;
        scratch.pad_index = pad;
        scratch.min_val = f->min_val;
        scratch.max_val = f->max_val;
        scratch.step = f->step;
        scratch.default_num = f->default_num;
        scratch.default_str = f->default_str;
        scratch.options_json = f->options_json;
        scratch.root = f->root;
        scratch.filter = f->filter;
        scratch.start_path = f->start_path;
        return &scratch;
    }

    return NULL;
}

const mrdrums_param_desc_t *mrdrums_find_param(const char *key) {
    const mrdrums_param_desc_t *global = mrdrums_find_global_param(key);
    if (global) return global;
    return mrdrums_find_pad_param(key);
}
