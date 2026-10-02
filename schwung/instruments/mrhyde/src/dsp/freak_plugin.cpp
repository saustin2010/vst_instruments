#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>

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

static const host_api_v1_t *g_host = NULL;

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

static inline int quantize_step(int v, int step) {
    if (step <= 1) return v;
    return (v / step) * step;
}

static inline int16_t float_to_i16(float v) {
    float s = clampf(v, -1.0f, 1.0f) * 32767.0f;
    int x = (int)lrintf(s);
    if (x < -32768) x = -32768;
    if (x > 32767) x = 32767;
    return (int16_t)x;
}

static void plugin_log(const char *msg) {
    if (!g_host || !g_host->log || !msg) return;
    char line[256];
    snprintf(line, sizeof(line), "[freak] %s", msg);
    g_host->log(line);
}

typedef struct {
    ppf_engine_t engine;
    ppf_params_t params;
    char module_dir[512];
    char last_error[256];
} freak_instance_t;

static void set_error(freak_instance_t *inst, const char *msg) {
    if (!inst) return;
    if (!msg) {
        inst->last_error[0] = '\0';
        return;
    }
    snprintf(inst->last_error, sizeof(inst->last_error), "%s", msg);
}

static int parse_boolish(const char *val, int *out) {
    if (!val || !out) return 0;
    if (strcmp(val, "1") == 0 ||
        strcasecmp(val, "on") == 0 ||
        strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "sync") == 0 ||
        strcasecmp(val, "clock") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(val, "0") == 0 ||
        strcasecmp(val, "off") == 0 ||
        strcasecmp(val, "false") == 0 ||
        strcasecmp(val, "free") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int parse_enum_or_int(const char *val, const char *const *names, int count, int *out) {
    if (!val || !out) return 0;
    char *endp = NULL;
    long iv = strtol(val, &endp, 10);
    if (endp && *endp == '\0') {
        *out = (int)iv;
        return 1;
    }
    for (int i = 0; i < count; ++i) {
        if (strcasecmp(val, names[i]) == 0) {
            *out = i;
            return 1;
        }
    }
    return 0;
}

static const char *const kOnOffNames[] = {"off", "on"};
static const char *const kLfoShapeNames[] = {"sine", "triangle", "saw", "square", "random", "smooth_random"};
static const char *const kCycleShapeNames[] = {"linear", "exponential", "logarithmic"};
static const char *const kRandomModeNames[] = {"sample_hold", "smooth", "drift"};
static const char *const kVoiceModeNames[] = {"mono", "poly", "mono_legato"};
static const char *const kFilterModeNames[] = {"lp", "bp", "hp"};
static const char *const kAssignTargetNames[] = {
    "off",
    "morph",
    "fm_amount",
    "lpg_decay",
    "lpg_color",
    "filter_resonance",
    "pitch",
    "harmonics",
    "timbre",
    "filter_cutoff",
    "volume",
    "pan",
    "detune",
    "spread"
};
constexpr int kAssignTargetCount = (int)(sizeof(kAssignTargetNames) / sizeof(kAssignTargetNames[0]));
static const float kSyncReferenceBpm = 120.0f;
static const char *const kSyncRateNames[] = {
    "16 bars",
    "8 bars",
    "4 bars",
    "2 bars",
    "1 bar",
    "1/2",
    "1/4",
    "1/8",
    "1/16",
    "1/32",
    "1/64"
};
static const float kSyncRateBars[] = {
    16.0f,
    8.0f,
    4.0f,
    2.0f,
    1.0f,
    0.5f,
    0.25f,
    0.125f,
    0.0625f,
    0.03125f,
    0.015625f
};
constexpr int kSyncRateCount = (int)(sizeof(kSyncRateNames) / sizeof(kSyncRateNames[0]));
static const char *const kModelNames[] = {
    "VA VCF",
    "Phase Distortion",
    "Six OP 1",
    "Six OP 2",
    "Six OP 3",
    "Wave Terrain",
    "String Machine",
    "Chiptune",
    "Virtual Analog",
    "Waveshaping",
    "FM 2-Op",
    "Granular Formant",
    "Harmonic",
    "Wavetable",
    "Chords",
    "Vocal Speech",
    "Swarm",
    "Noise",
    "Particle Noise",
    "Inharmonic String",
    "Modal Resonator",
    "Analog Bass Drum",
    "Analog Snare Drum",
    "Analog Hi-Hat"
};
constexpr int kModelCount = (int)(sizeof(kModelNames) / sizeof(kModelNames[0]));
/* Hidden from UI for now: Six OP 1/2/3, Chiptune, and drum engines. */
static const int kVisibleModelToEngine[] = {
    0,   /* VA VCF */
    1,   /* Phase Distortion */
    5,   /* Wave Terrain */
    6,   /* String Machine */
    8,   /* Virtual Analog */
    9,   /* Waveshaping */
    10,  /* FM 2-Op */
    11,  /* Granular Formant */
    12,  /* Harmonic */
    13,  /* Wavetable */
    14,  /* Chords */
    15,  /* Vocal Speech */
    16,  /* Swarm */
    17,  /* Noise */
    18,  /* Particle Noise */
    19,  /* Inharmonic String */
    20   /* Modal Resonator */
};
constexpr int kVisibleModelCount = (int)(sizeof(kVisibleModelToEngine) / sizeof(kVisibleModelToEngine[0]));

static int visible_model_to_engine_index(int visible_index) {
    int idx = clampi(visible_index, 0, kVisibleModelCount - 1);
    return kVisibleModelToEngine[idx];
}

static float sync_rate_hz_from_index(int index, float bpm) {
    int idx = clampi(index, 0, kSyncRateCount - 1);
    float clamped_bpm = clampf(bpm, 20.0f, 300.0f);
    float bar_seconds = 240.0f / clamped_bpm;
    float cycle_seconds = kSyncRateBars[idx] * bar_seconds;
    if (cycle_seconds <= 1e-6f) cycle_seconds = 1e-6f;
    return 1.0f / cycle_seconds;
}

static int nearest_sync_rate_index(float hz, float bpm) {
    float clamped = clampf(hz, 0.01f, 40.0f);
    int best = 0;
    float best_err = 1e9f;
    for (int i = 0; i < kSyncRateCount; ++i) {
        float ref = sync_rate_hz_from_index(i, bpm);
        float err = fabsf(ref - clamped);
        if (err < best_err) {
            best_err = err;
            best = i;
        }
    }
    return best;
}

static int write_sync_rate_text(float hz, char *buf, int buf_len) {
    if (!buf || buf_len <= 0) return -1;
    int idx = nearest_sync_rate_index(hz, kSyncReferenceBpm);
    return snprintf(buf, buf_len, "%s", kSyncRateNames[idx]);
}

static int write_rate_mode_text(int sync_on, char *buf, int buf_len) {
    return snprintf(buf, buf_len, "%s", sync_on ? "sync" : "free");
}

static int write_enum_text(const char *key, int value, char *buf, int buf_len) {
    if (!key || !buf || buf_len <= 0) return -1;

    const char *const *names = NULL;
    int count = 0;
    if (strcmp(key, "model") == 0) {
        names = kModelNames;
        count = kModelCount;
    } else if (strcmp(key, "lfo_shape") == 0) {
        names = kLfoShapeNames;
        count = 6;
    } else if (strcmp(key, "cycle_shape") == 0) {
        names = kCycleShapeNames;
        count = 3;
    } else if (strcmp(key, "random_mode") == 0) {
        names = kRandomModeNames;
        count = 3;
    } else if (strcmp(key, "voice_mode") == 0) {
        names = kVoiceModeNames;
        count = 3;
    } else if (strcmp(key, "filter_mode") == 0) {
        names = kFilterModeNames;
        count = 3;
    } else if (strcmp(key, "assign1_target") == 0 ||
               strcmp(key, "assign2_target") == 0) {
        names = kAssignTargetNames;
        count = kAssignTargetCount;
    } else if (strcmp(key, "lfo_rate_mode") == 0 ||
               strcmp(key, "lfo_sync") == 0 ||
               strcmp(key, "lfo_retrig") == 0 ||
               strcmp(key, "env_retrig") == 0 ||
               strcmp(key, "cycle_sync") == 0 ||
               strcmp(key, "cycle_retrig") == 0 ||
               strcmp(key, "cycle_bipolar") == 0 ||
               strcmp(key, "random_rate_mode") == 0 ||
               strcmp(key, "random_sync") == 0 ||
               strcmp(key, "random_retrig") == 0) {
        names = kOnOffNames;
        count = 2;
    } else {
        return -1;
    }

    int idx = clampi(value, 0, count - 1);
    return snprintf(buf, buf_len, "%s", names[idx]);
}

static char *read_text_file(const char *path, size_t *out_size) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz <= 0 || sz > (2 * 1024 * 1024)) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_size) *out_size = n;
    return buf;
}

static int extract_json_value(const char *json,
                              const char *field,
                              char opener,
                              char closer,
                              char *out,
                              int out_len) {
    if (!json || !field || !out || out_len <= 0) return -1;

    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char *pos = strstr(json, needle);
    if (!pos) return -1;
    pos = strchr(pos + strlen(needle), ':');
    if (!pos) return -1;
    pos++;
    while (*pos && isspace((unsigned char)*pos)) pos++;
    if (*pos != opener) return -1;

    const char *start = pos;
    int depth = 0;
    int in_string = 0;
    int escaped = 0;
    while (*pos) {
        char c = *pos++;
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (c == '\\') {
                escaped = 1;
            } else if (c == '"') {
                in_string = 0;
            }
            continue;
        }

        if (c == '"') {
            in_string = 1;
            continue;
        }
        if (c == opener) depth++;
        if (c == closer) {
            depth--;
            if (depth == 0) {
                int len = (int)(pos - start);
                if (len >= out_len) return -1;
                memcpy(out, start, (size_t)len);
                out[len] = '\0';
                return len;
            }
        }
    }

    return -1;
}

static int json_get_number(const char *json, const char *key, float *out) {
    if (!json || !key || !out) return -1;
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    char *endp = NULL;
    double v = strtod(p, &endp);
    if (!endp || endp == p) return -1;
    *out = (float)v;
    return 0;
}

static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    if (!json || !key || !out || out_len <= 1) return -1;
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < out_len - 1) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return (*p == '"') ? 0 : -1;
}

static int get_param_from_module_json(const freak_instance_t *inst,
                                      const char *field,
                                      char opener,
                                      char closer,
                                      char *buf,
                                      int buf_len) {
    if (!inst || !field || !buf || buf_len <= 0) return -1;
    char json_path[768];
    const char *dir = inst->module_dir[0] ? inst->module_dir : ".";
    snprintf(json_path, sizeof(json_path), "%s/module.json", dir);
    size_t sz = 0;
    char *json = read_text_file(json_path, &sz);
    (void)sz;
    if (!json) return -1;

    int out = extract_json_value(json, field, opener, closer, buf, buf_len);
    free(json);
    return out;
}

static int has_active_mod_amounts(const ppf_mod_amounts_t &m) {
    const float eps = 1e-6f;
    return fabsf(m.lfo) > eps ||
           fabsf(m.env) > eps ||
           fabsf(m.cycle_env) > eps ||
           fabsf(m.random) > eps ||
           fabsf(m.velocity) > eps ||
           fabsf(m.poly_aftertouch) > eps;
}

static int append_star_to_mod_level(char *json, int cap, const char *level, int active) {
    if (!json || cap <= 0 || !level || !active) return 0;

    char level_compact[128];
    char level_spaced[128];
    snprintf(level_compact, sizeof(level_compact), "\"level\":\"%s\"", level);
    snprintf(level_spaced, sizeof(level_spaced), "\"level\": \"%s\"", level);

    char *level_pos = strstr(json, level_compact);
    if (!level_pos) level_pos = strstr(json, level_spaced);
    if (!level_pos) return 0;

    char *scan = json;
    char *label_key = NULL;
    while (true) {
        char *next = strstr(scan, "\"label\"");
        if (!next || next >= level_pos) break;
        label_key = next;
        scan = next + 7;
    }
    if (!label_key) return 0;

    char *colon = strchr(label_key, ':');
    if (!colon || colon >= level_pos) return 0;
    char *value = colon + 1;
    while (*value && isspace((unsigned char)*value)) value++;
    if (*value != '"' || value >= level_pos) return 0;
    value++;

    char *label_end = value;
    while (*label_end && *label_end != '"' && label_end < level_pos) label_end++;
    if (*label_end != '"' || label_end >= level_pos) return 0;
    if (label_end > value && label_end[-1] == '*') return 1;

    size_t len = strlen(json);
    size_t index = (size_t)(label_end - json);
    if (len + 1 >= (size_t)cap) return -1;
    memmove(label_end + 1, label_end, len - index + 1);
    *label_end = '*';
    return 1;
}

static int get_ui_hierarchy_with_mod_stars(const freak_instance_t *inst, char *buf, int buf_len) {
    int rc = get_param_from_module_json(inst, "ui_hierarchy", '{', '}', buf, buf_len);
    if (rc < 0) return rc;

    int assign1_active = inst->params.assign1_target != 0 && has_active_mod_amounts(inst->params.assign1_mod);
    int assign2_active = inst->params.assign2_target != 0 && has_active_mod_amounts(inst->params.assign2_mod);
    int pitch_active = has_active_mod_amounts(inst->params.pitch_mod);
    int harmonics_active = has_active_mod_amounts(inst->params.harmonics_mod);
    int timbre_active = has_active_mod_amounts(inst->params.timbre_mod);
    int cutoff_active = has_active_mod_amounts(inst->params.cutoff_mod);

    if (append_star_to_mod_level(buf, buf_len, "assign1_mod", assign1_active) < 0) return -1;
    if (append_star_to_mod_level(buf, buf_len, "assign2_mod", assign2_active) < 0) return -1;
    if (append_star_to_mod_level(buf, buf_len, "pitch_mod", pitch_active) < 0) return -1;
    if (append_star_to_mod_level(buf, buf_len, "harmonics_mod", harmonics_active) < 0) return -1;
    if (append_star_to_mod_level(buf, buf_len, "timbre_mod", timbre_active) < 0) return -1;
    if (append_star_to_mod_level(buf, buf_len, "cutoff_mod", cutoff_active) < 0) return -1;

    return (int)strlen(buf);
}

static int find_param_object_bounds(char *json,
                                    const char *key,
                                    size_t *out_start,
                                    size_t *out_end) {
    if (!json || !key || !out_start || !out_end) return -1;
    char key_compact[96];
    char key_spaced[96];
    snprintf(key_compact, sizeof(key_compact), "\"key\":\"%s\"", key);
    snprintf(key_spaced, sizeof(key_spaced), "\"key\": \"%s\"", key);

    char *hit = strstr(json, key_compact);
    if (!hit) hit = strstr(json, key_spaced);
    if (!hit) return -1;

    char *obj_start = hit;
    while (obj_start > json && *obj_start != '{') obj_start--;
    if (*obj_start != '{') return -1;

    int depth = 0;
    int in_string = 0;
    int escaped = 0;
    for (char *p = obj_start; *p; ++p) {
        char c = *p;
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (c == '\\') {
                escaped = 1;
            } else if (c == '"') {
                in_string = 0;
            }
            continue;
        }
        if (c == '"') {
            in_string = 1;
            continue;
        }
        if (c == '{') depth++;
        if (c == '}') {
            depth--;
            if (depth == 0) {
                *out_start = (size_t)(obj_start - json);
                *out_end = (size_t)(p - json) + 1;
                return 0;
            }
        }
    }
    return -1;
}

static int replace_json_range(char *json,
                              int cap,
                              size_t start,
                              size_t end,
                              const char *replacement) {
    if (!json || cap <= 0 || !replacement) return -1;
    size_t len = strlen(json);
    if (start >= end || end > len) return -1;

    size_t old_len = end - start;
    size_t new_len = strlen(replacement);

    if (new_len > old_len) {
        size_t grow = new_len - old_len;
        if (len + grow >= (size_t)cap) return -1;
        memmove(json + start + new_len, json + end, len - end + 1);
    } else if (new_len < old_len) {
        memmove(json + start + new_len, json + end, len - end + 1);
    }

    memcpy(json + start, replacement, new_len);
    return 0;
}

static int rewrite_rate_param_metadata(char *json,
                                       int cap,
                                       const char *key,
                                       int sync_on,
                                       float current_hz) {
    if (!json || cap <= 0 || !key) return -1;
    size_t start = 0;
    size_t end = 0;
    if (find_param_object_bounds(json, key, &start, &end) < 0) return -1;

    char replacement[512];
    if (sync_on) {
        int idx = nearest_sync_rate_index(current_hz, kSyncReferenceBpm);
        snprintf(replacement, sizeof(replacement),
                 "{\"key\":\"%s\",\"name\":\"Rate\",\"type\":\"enum\","
                 "\"options\":[\"16 bars\",\"8 bars\",\"4 bars\",\"2 bars\",\"1 bar\","
                 "\"1/2\",\"1/4\",\"1/8\",\"1/16\",\"1/32\",\"1/64\"],\"default\":%d}",
                 key, idx);
    } else {
        snprintf(replacement, sizeof(replacement),
                 "{\"key\":\"%s\",\"name\":\"Rate\",\"type\":\"float\",\"min\":0.01,\"max\":40.0,"
                 "\"default\":%.6g,\"step\":0.01}",
                 key, clampf(current_hz, 0.01f, 40.0f));
    }
    return replace_json_range(json, cap, start, end, replacement);
}

static int get_chain_params_dynamic(const freak_instance_t *inst, char *buf, int buf_len) {
    int rc = get_param_from_module_json(inst, "chain_params", '[', ']', buf, buf_len);
    if (rc < 0) return rc;
    if (rewrite_rate_param_metadata(buf, buf_len, "lfo_rate", inst->params.lfo_sync, inst->params.lfo_rate) < 0) return -1;
    if (rewrite_rate_param_metadata(buf, buf_len, "random_rate", inst->params.random_sync, inst->params.random_rate) < 0) return -1;
    return (int)strlen(buf);
}

#define SET_FLOAT_FIELD(NAME, FIELD, LO, HI) \
    if (strcmp(key, NAME) == 0) { \
        inst->params.FIELD = clampf(fv, LO, HI); \
        return 1; \
    }

#define SET_INT_FIELD(NAME, FIELD, LO, HI) \
    if (strcmp(key, NAME) == 0) { \
        inst->params.FIELD = clampi(iv, LO, HI); \
        return 1; \
    }

#define GET_FLOAT_FIELD(NAME, FIELD) \
    if (strcmp(key, NAME) == 0) return snprintf(buf, buf_len, "%.6g", inst->params.FIELD)

#define GET_INT_FIELD(NAME, FIELD) \
    if (strcmp(key, NAME) == 0) return snprintf(buf, buf_len, "%d", inst->params.FIELD)

#define GET_ENUM_FIELD(NAME, FIELD) \
    if (strcmp(key, NAME) == 0) return write_enum_text(NAME, inst->params.FIELD, buf, buf_len)

static const char *const kStateKeys[] = {
    "model",
    "pitch",
    "harmonics",
    "timbre",
    "morph",
    "fm_amount",
    "aux_mix",
    "volume",
    "pan",
    "filter_mode",
    "filter_cutoff",
    "filter_resonance",
    "lpg_decay",
    "lpg_color",
    "pitch_mod_lfo_amt",
    "pitch_mod_env_amt",
    "pitch_mod_cycle_env_amt",
    "pitch_mod_random_amt",
    "pitch_mod_velocity_amt",
    "pitch_mod_poly_aftertouch_amt",
    "harmonics_mod_lfo_amt",
    "harmonics_mod_env_amt",
    "harmonics_mod_cycle_env_amt",
    "harmonics_mod_random_amt",
    "harmonics_mod_velocity_amt",
    "harmonics_mod_poly_aftertouch_amt",
    "timbre_mod_lfo_amt",
    "timbre_mod_env_amt",
    "timbre_mod_cycle_env_amt",
    "timbre_mod_random_amt",
    "timbre_mod_velocity_amt",
    "timbre_mod_poly_aftertouch_amt",
    "cutoff_mod_lfo_amt",
    "cutoff_mod_env_amt",
    "cutoff_mod_cycle_env_amt",
    "cutoff_mod_random_amt",
    "cutoff_mod_velocity_amt",
    "cutoff_mod_poly_aftertouch_amt",
    "assign1_target",
    "assign1_mod_lfo_amt",
    "assign1_mod_env_amt",
    "assign1_mod_cycle_env_amt",
    "assign1_mod_random_amt",
    "assign1_mod_velocity_amt",
    "assign1_mod_poly_aftertouch_amt",
    "assign2_target",
    "assign2_mod_lfo_amt",
    "assign2_mod_env_amt",
    "assign2_mod_cycle_env_amt",
    "assign2_mod_random_amt",
    "assign2_mod_velocity_amt",
    "assign2_mod_poly_aftertouch_amt",
    "lfo_shape",
    "lfo_rate",
    "lfo_rate_mode",
    "lfo_retrig",
    "lfo_phase",
    "env_attack_ms",
    "env_decay_ms",
    "env_sustain",
    "env_release_ms",
    "env_retrig",
    "cycle_attack_ms",
    "cycle_decay_ms",
    "cycle_shape",
    "cycle_sync",
    "cycle_retrig",
    "cycle_bipolar",
    "random_mode",
    "random_rate",
    "random_rate_mode",
    "random_slew",
    "random_retrig",
    "velocity_curve",
    "poly_aftertouch_curve",
    "voice_mode",
    "polyphony",
    "unison",
    "detune",
    "spread",
    "glide_ms"
};
constexpr int kStateKeyCount = (int)(sizeof(kStateKeys) / sizeof(kStateKeys[0]));

static int set_param_internal(freak_instance_t *inst, const char *key, const char *val) {
    if (!inst || !key || !val) return 0;

    char *endp = NULL;
    float fv = strtof(val, &endp);
    int has_float = (endp && *endp == '\0');
    int iv = has_float ? (int)lrintf(fv) : 0;

    if (strcmp(key, "all_notes_off") == 0) {
        int on = 0;
        if (!parse_boolish(val, &on)) on = has_float ? (fv != 0.0f) : 0;
        if (on) inst->engine.all_notes_off();
        return 1;
    }

    if (strcmp(key, "model") == 0) {
        char *int_end = NULL;
        long raw = strtol(val, &int_end, 10);
        if (int_end && *int_end == '\0') {
            inst->params.model = visible_model_to_engine_index((int)raw);
            return 1;
        }
        if (!parse_enum_or_int(val, kModelNames, kModelCount, &iv)) return 0;
        inst->params.model = clampi(iv, 0, kModelCount - 1);
        return 1;
    }

    if (!has_float) {
        if (strcmp(key, "lfo_shape") == 0) {
            if (!parse_enum_or_int(val, kLfoShapeNames, 6, &iv)) return 0;
            inst->params.lfo_shape = clampi(iv, 0, 5);
            return 1;
        }
        if (strcmp(key, "cycle_shape") == 0) {
            if (!parse_enum_or_int(val, kCycleShapeNames, 3, &iv)) return 0;
            inst->params.cycle_shape = clampi(iv, 0, 2);
            return 1;
        }
        if (strcmp(key, "random_mode") == 0) {
            if (!parse_enum_or_int(val, kRandomModeNames, 3, &iv)) return 0;
            inst->params.random_mode = clampi(iv, 0, 2);
            return 1;
        }
        if (strcmp(key, "voice_mode") == 0) {
            if (!parse_enum_or_int(val, kVoiceModeNames, 3, &iv)) return 0;
            inst->params.voice_mode = clampi(iv, 0, 2);
            return 1;
        }
        if (strcmp(key, "filter_mode") == 0) {
            if (!parse_enum_or_int(val, kFilterModeNames, 3, &iv)) return 0;
            inst->params.filter_mode = clampi(iv, 0, 2);
            return 1;
        }
        if (strcmp(key, "assign1_target") == 0) {
            if (!parse_enum_or_int(val, kAssignTargetNames, kAssignTargetCount, &iv)) return 0;
            inst->params.assign1_target = clampi(iv, 0, kAssignTargetCount - 1);
            return 1;
        }
        if (strcmp(key, "assign2_target") == 0) {
            if (!parse_enum_or_int(val, kAssignTargetNames, kAssignTargetCount, &iv)) return 0;
            inst->params.assign2_target = clampi(iv, 0, kAssignTargetCount - 1);
            return 1;
        }
        if (strcmp(key, "lfo_rate") == 0) {
            if (!parse_enum_or_int(val, kSyncRateNames, kSyncRateCount, &iv)) return 0;
            inst->params.lfo_rate = sync_rate_hz_from_index(iv, kSyncReferenceBpm);
            return 1;
        }
        if (strcmp(key, "random_rate") == 0) {
            if (!parse_enum_or_int(val, kSyncRateNames, kSyncRateCount, &iv)) return 0;
            inst->params.random_rate = sync_rate_hz_from_index(iv, kSyncReferenceBpm);
            return 1;
        }
        if (strcmp(key, "lfo_rate_mode") == 0 || strcmp(key, "lfo_sync") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.lfo_sync = iv;
            return 1;
        }
        if (strcmp(key, "lfo_retrig") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.lfo_retrig = iv;
            return 1;
        }
        if (strcmp(key, "env_retrig") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.env_retrig = iv;
            return 1;
        }
        if (strcmp(key, "cycle_sync") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.cycle_sync = iv;
            return 1;
        }
        if (strcmp(key, "cycle_retrig") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.cycle_retrig = iv;
            return 1;
        }
        if (strcmp(key, "cycle_bipolar") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.cycle_bipolar = iv;
            return 1;
        }
        if (strcmp(key, "random_rate_mode") == 0 || strcmp(key, "random_sync") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.random_sync = iv;
            return 1;
        }
        if (strcmp(key, "random_retrig") == 0) {
            if (!parse_boolish(val, &iv)) return 0;
            inst->params.random_retrig = iv;
            return 1;
        }
    }

    if (!has_float) return 0;

    SET_FLOAT_FIELD("pitch", pitch, -48.0f, 48.0f);
    SET_FLOAT_FIELD("harmonics", harmonics, 0.0f, 1.0f);
    SET_FLOAT_FIELD("timbre", timbre, 0.0f, 1.0f);
    SET_FLOAT_FIELD("morph", morph, 0.0f, 1.0f);
    SET_FLOAT_FIELD("fm_amount", fm_amount, 0.0f, 1.0f);
    SET_FLOAT_FIELD("aux_mix", aux_mix, 0.0f, 1.0f);
    SET_FLOAT_FIELD("volume", volume, 0.0f, 2.0f);
    SET_FLOAT_FIELD("pan", pan, -1.0f, 1.0f);
    SET_INT_FIELD("filter_mode", filter_mode, 0, 2);
    SET_FLOAT_FIELD("filter_cutoff", filter_cutoff, 0.0f, 1.0f);
    SET_FLOAT_FIELD("filter_resonance", filter_resonance, 0.0f, 1.0f);
    SET_FLOAT_FIELD("lpg_decay", lpg_decay, 0.0f, 1.0f);
    SET_FLOAT_FIELD("lpg_color", lpg_color, 0.0f, 1.0f);

    SET_FLOAT_FIELD("pitch_mod_lfo_amt", pitch_mod.lfo, -48.0f, 48.0f);
    SET_FLOAT_FIELD("pitch_mod_env_amt", pitch_mod.env, -48.0f, 48.0f);
    SET_FLOAT_FIELD("pitch_mod_cycle_env_amt", pitch_mod.cycle_env, -48.0f, 48.0f);
    SET_FLOAT_FIELD("pitch_mod_random_amt", pitch_mod.random, -48.0f, 48.0f);
    SET_FLOAT_FIELD("pitch_mod_velocity_amt", pitch_mod.velocity, -48.0f, 48.0f);
    SET_FLOAT_FIELD("pitch_mod_poly_aftertouch_amt", pitch_mod.poly_aftertouch, -48.0f, 48.0f);

    SET_FLOAT_FIELD("harmonics_mod_lfo_amt", harmonics_mod.lfo, -1.0f, 1.0f);
    SET_FLOAT_FIELD("harmonics_mod_env_amt", harmonics_mod.env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("harmonics_mod_cycle_env_amt", harmonics_mod.cycle_env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("harmonics_mod_random_amt", harmonics_mod.random, -1.0f, 1.0f);
    SET_FLOAT_FIELD("harmonics_mod_velocity_amt", harmonics_mod.velocity, -1.0f, 1.0f);
    SET_FLOAT_FIELD("harmonics_mod_poly_aftertouch_amt", harmonics_mod.poly_aftertouch, -1.0f, 1.0f);

    SET_FLOAT_FIELD("timbre_mod_lfo_amt", timbre_mod.lfo, -1.0f, 1.0f);
    SET_FLOAT_FIELD("timbre_mod_env_amt", timbre_mod.env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("timbre_mod_cycle_env_amt", timbre_mod.cycle_env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("timbre_mod_random_amt", timbre_mod.random, -1.0f, 1.0f);
    SET_FLOAT_FIELD("timbre_mod_velocity_amt", timbre_mod.velocity, -1.0f, 1.0f);
    SET_FLOAT_FIELD("timbre_mod_poly_aftertouch_amt", timbre_mod.poly_aftertouch, -1.0f, 1.0f);

    SET_FLOAT_FIELD("cutoff_mod_lfo_amt", cutoff_mod.lfo, -1.0f, 1.0f);
    SET_FLOAT_FIELD("cutoff_mod_env_amt", cutoff_mod.env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("cutoff_mod_cycle_env_amt", cutoff_mod.cycle_env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("cutoff_mod_random_amt", cutoff_mod.random, -1.0f, 1.0f);
    SET_FLOAT_FIELD("cutoff_mod_velocity_amt", cutoff_mod.velocity, -1.0f, 1.0f);
    SET_FLOAT_FIELD("cutoff_mod_poly_aftertouch_amt", cutoff_mod.poly_aftertouch, -1.0f, 1.0f);

    SET_INT_FIELD("assign1_target", assign1_target, 0, kAssignTargetCount - 1);
    SET_FLOAT_FIELD("assign1_mod_lfo_amt", assign1_mod.lfo, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign1_mod_env_amt", assign1_mod.env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign1_mod_cycle_env_amt", assign1_mod.cycle_env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign1_mod_random_amt", assign1_mod.random, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign1_mod_velocity_amt", assign1_mod.velocity, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign1_mod_poly_aftertouch_amt", assign1_mod.poly_aftertouch, -1.0f, 1.0f);

    SET_INT_FIELD("assign2_target", assign2_target, 0, kAssignTargetCount - 1);
    SET_FLOAT_FIELD("assign2_mod_lfo_amt", assign2_mod.lfo, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign2_mod_env_amt", assign2_mod.env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign2_mod_cycle_env_amt", assign2_mod.cycle_env, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign2_mod_random_amt", assign2_mod.random, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign2_mod_velocity_amt", assign2_mod.velocity, -1.0f, 1.0f);
    SET_FLOAT_FIELD("assign2_mod_poly_aftertouch_amt", assign2_mod.poly_aftertouch, -1.0f, 1.0f);

    SET_INT_FIELD("lfo_shape", lfo_shape, 0, 5);
    if (strcmp(key, "lfo_rate") == 0) {
        float hz = clampf(fv, 0.01f, 40.0f);
        if (inst->params.lfo_sync) {
            bool looks_like_index = fabsf((float)iv - fv) < 1e-6f && iv >= 0 && iv < kSyncRateCount;
            int idx = looks_like_index ? iv : nearest_sync_rate_index(hz, kSyncReferenceBpm);
            hz = sync_rate_hz_from_index(idx, kSyncReferenceBpm);
        }
        inst->params.lfo_rate = hz;
        return 1;
    }
    if (strcmp(key, "lfo_rate_mode") == 0 || strcmp(key, "lfo_sync") == 0) {
        inst->params.lfo_sync = clampi(iv, 0, 1);
        return 1;
    }
    SET_INT_FIELD("lfo_retrig", lfo_retrig, 0, 1);
    SET_FLOAT_FIELD("lfo_phase", lfo_phase, 0.0f, 1.0f);

    SET_INT_FIELD("env_attack_ms", env_attack_ms, 0, 5000);
    SET_INT_FIELD("env_decay_ms", env_decay_ms, 0, 5000);
    SET_FLOAT_FIELD("env_sustain", env_sustain, 0.0f, 1.0f);
    SET_INT_FIELD("env_release_ms", env_release_ms, 0, 5000);
    SET_INT_FIELD("env_retrig", env_retrig, 0, 1);

    SET_INT_FIELD("cycle_attack_ms", cycle_attack_ms, 1, 5000);
    SET_INT_FIELD("cycle_decay_ms", cycle_decay_ms, 1, 5000);
    SET_INT_FIELD("cycle_shape", cycle_shape, 0, 2);
    SET_INT_FIELD("cycle_sync", cycle_sync, 0, 1);
    SET_INT_FIELD("cycle_retrig", cycle_retrig, 0, 1);
    SET_INT_FIELD("cycle_bipolar", cycle_bipolar, 0, 1);

    SET_INT_FIELD("random_mode", random_mode, 0, 2);
    if (strcmp(key, "random_rate") == 0) {
        float hz = clampf(fv, 0.01f, 40.0f);
        if (inst->params.random_sync) {
            bool looks_like_index = fabsf((float)iv - fv) < 1e-6f && iv >= 0 && iv < kSyncRateCount;
            int idx = looks_like_index ? iv : nearest_sync_rate_index(hz, kSyncReferenceBpm);
            hz = sync_rate_hz_from_index(idx, kSyncReferenceBpm);
        }
        inst->params.random_rate = hz;
        return 1;
    }
    if (strcmp(key, "random_rate_mode") == 0 || strcmp(key, "random_sync") == 0) {
        inst->params.random_sync = clampi(iv, 0, 1);
        return 1;
    }
    SET_FLOAT_FIELD("random_slew", random_slew, 0.0f, 1.0f);
    SET_INT_FIELD("random_retrig", random_retrig, 0, 1);

    SET_FLOAT_FIELD("velocity_curve", velocity_curve, 0.1f, 4.0f);
    SET_FLOAT_FIELD("poly_aftertouch_curve", poly_aftertouch_curve, -1.0f, 1.0f);

    SET_INT_FIELD("voice_mode", voice_mode, 0, 2);
    SET_INT_FIELD("polyphony", polyphony, 1, 8);
    SET_INT_FIELD("unison", unison, 1, 8);
    SET_FLOAT_FIELD("detune", detune, 0.0f, 1.0f);
    SET_FLOAT_FIELD("spread", spread, 0.0f, 1.0f);
    if (strcmp(key, "glide_ms") == 0) {
        int g = clampi(iv, 0, 2000);
        inst->params.glide_ms = quantize_step(g, 5);
        return 1;
    }

    return 0;
}

static int get_param_internal(const freak_instance_t *inst, const char *key, char *buf, int buf_len) {
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    GET_ENUM_FIELD("model", model);
    GET_FLOAT_FIELD("pitch", pitch);
    GET_FLOAT_FIELD("harmonics", harmonics);
    GET_FLOAT_FIELD("timbre", timbre);
    GET_FLOAT_FIELD("morph", morph);
    GET_FLOAT_FIELD("fm_amount", fm_amount);
    GET_FLOAT_FIELD("aux_mix", aux_mix);
    GET_FLOAT_FIELD("volume", volume);
    GET_FLOAT_FIELD("pan", pan);
    GET_ENUM_FIELD("filter_mode", filter_mode);
    GET_FLOAT_FIELD("filter_cutoff", filter_cutoff);
    GET_FLOAT_FIELD("filter_resonance", filter_resonance);
    GET_FLOAT_FIELD("lpg_decay", lpg_decay);
    GET_FLOAT_FIELD("lpg_color", lpg_color);

    GET_FLOAT_FIELD("pitch_mod_lfo_amt", pitch_mod.lfo);
    GET_FLOAT_FIELD("pitch_mod_env_amt", pitch_mod.env);
    GET_FLOAT_FIELD("pitch_mod_cycle_env_amt", pitch_mod.cycle_env);
    GET_FLOAT_FIELD("pitch_mod_random_amt", pitch_mod.random);
    GET_FLOAT_FIELD("pitch_mod_velocity_amt", pitch_mod.velocity);
    GET_FLOAT_FIELD("pitch_mod_poly_aftertouch_amt", pitch_mod.poly_aftertouch);

    GET_FLOAT_FIELD("harmonics_mod_lfo_amt", harmonics_mod.lfo);
    GET_FLOAT_FIELD("harmonics_mod_env_amt", harmonics_mod.env);
    GET_FLOAT_FIELD("harmonics_mod_cycle_env_amt", harmonics_mod.cycle_env);
    GET_FLOAT_FIELD("harmonics_mod_random_amt", harmonics_mod.random);
    GET_FLOAT_FIELD("harmonics_mod_velocity_amt", harmonics_mod.velocity);
    GET_FLOAT_FIELD("harmonics_mod_poly_aftertouch_amt", harmonics_mod.poly_aftertouch);

    GET_FLOAT_FIELD("timbre_mod_lfo_amt", timbre_mod.lfo);
    GET_FLOAT_FIELD("timbre_mod_env_amt", timbre_mod.env);
    GET_FLOAT_FIELD("timbre_mod_cycle_env_amt", timbre_mod.cycle_env);
    GET_FLOAT_FIELD("timbre_mod_random_amt", timbre_mod.random);
    GET_FLOAT_FIELD("timbre_mod_velocity_amt", timbre_mod.velocity);
    GET_FLOAT_FIELD("timbre_mod_poly_aftertouch_amt", timbre_mod.poly_aftertouch);

    GET_FLOAT_FIELD("cutoff_mod_lfo_amt", cutoff_mod.lfo);
    GET_FLOAT_FIELD("cutoff_mod_env_amt", cutoff_mod.env);
    GET_FLOAT_FIELD("cutoff_mod_cycle_env_amt", cutoff_mod.cycle_env);
    GET_FLOAT_FIELD("cutoff_mod_random_amt", cutoff_mod.random);
    GET_FLOAT_FIELD("cutoff_mod_velocity_amt", cutoff_mod.velocity);
    GET_FLOAT_FIELD("cutoff_mod_poly_aftertouch_amt", cutoff_mod.poly_aftertouch);

    GET_ENUM_FIELD("assign1_target", assign1_target);
    GET_FLOAT_FIELD("assign1_mod_lfo_amt", assign1_mod.lfo);
    GET_FLOAT_FIELD("assign1_mod_env_amt", assign1_mod.env);
    GET_FLOAT_FIELD("assign1_mod_cycle_env_amt", assign1_mod.cycle_env);
    GET_FLOAT_FIELD("assign1_mod_random_amt", assign1_mod.random);
    GET_FLOAT_FIELD("assign1_mod_velocity_amt", assign1_mod.velocity);
    GET_FLOAT_FIELD("assign1_mod_poly_aftertouch_amt", assign1_mod.poly_aftertouch);

    GET_ENUM_FIELD("assign2_target", assign2_target);
    GET_FLOAT_FIELD("assign2_mod_lfo_amt", assign2_mod.lfo);
    GET_FLOAT_FIELD("assign2_mod_env_amt", assign2_mod.env);
    GET_FLOAT_FIELD("assign2_mod_cycle_env_amt", assign2_mod.cycle_env);
    GET_FLOAT_FIELD("assign2_mod_random_amt", assign2_mod.random);
    GET_FLOAT_FIELD("assign2_mod_velocity_amt", assign2_mod.velocity);
    GET_FLOAT_FIELD("assign2_mod_poly_aftertouch_amt", assign2_mod.poly_aftertouch);

    GET_ENUM_FIELD("lfo_shape", lfo_shape);
    if (strcmp(key, "lfo_rate") == 0) {
        if (inst->params.lfo_sync) {
            return write_sync_rate_text(inst->params.lfo_rate, buf, buf_len);
        }
        return snprintf(buf, buf_len, "%.6g", clampf(inst->params.lfo_rate, 0.01f, 40.0f));
    }
    if (strcmp(key, "lfo_rate_mode") == 0 || strcmp(key, "lfo_sync") == 0) {
        return write_rate_mode_text(inst->params.lfo_sync, buf, buf_len);
    }
    GET_ENUM_FIELD("lfo_retrig", lfo_retrig);
    GET_FLOAT_FIELD("lfo_phase", lfo_phase);

    GET_INT_FIELD("env_attack_ms", env_attack_ms);
    GET_INT_FIELD("env_decay_ms", env_decay_ms);
    GET_FLOAT_FIELD("env_sustain", env_sustain);
    GET_INT_FIELD("env_release_ms", env_release_ms);
    GET_ENUM_FIELD("env_retrig", env_retrig);

    GET_INT_FIELD("cycle_attack_ms", cycle_attack_ms);
    GET_INT_FIELD("cycle_decay_ms", cycle_decay_ms);
    GET_ENUM_FIELD("cycle_shape", cycle_shape);
    GET_ENUM_FIELD("cycle_sync", cycle_sync);
    GET_ENUM_FIELD("cycle_retrig", cycle_retrig);
    GET_ENUM_FIELD("cycle_bipolar", cycle_bipolar);

    GET_ENUM_FIELD("random_mode", random_mode);
    if (strcmp(key, "random_rate") == 0) {
        if (inst->params.random_sync) {
            return write_sync_rate_text(inst->params.random_rate, buf, buf_len);
        }
        return snprintf(buf, buf_len, "%.6g", clampf(inst->params.random_rate, 0.01f, 40.0f));
    }
    if (strcmp(key, "random_rate_mode") == 0 || strcmp(key, "random_sync") == 0) {
        return write_rate_mode_text(inst->params.random_sync, buf, buf_len);
    }
    GET_FLOAT_FIELD("random_slew", random_slew);
    GET_ENUM_FIELD("random_retrig", random_retrig);

    GET_FLOAT_FIELD("velocity_curve", velocity_curve);
    GET_FLOAT_FIELD("poly_aftertouch_curve", poly_aftertouch_curve);

    GET_ENUM_FIELD("voice_mode", voice_mode);
    GET_INT_FIELD("polyphony", polyphony);
    GET_INT_FIELD("unison", unison);
    GET_FLOAT_FIELD("detune", detune);
    GET_FLOAT_FIELD("spread", spread);
    GET_INT_FIELD("glide_ms", glide_ms);

    return -1;
}

static int state_value_should_be_string(const char *value) {
    if (!value || !value[0]) return 1;
    char *endp = NULL;
    strtod(value, &endp);
    return !(endp && *endp == '\0');
}

static void apply_state_json(freak_instance_t *inst, const char *json) {
    if (!inst || !json || !json[0]) return;

    char val_buf[128];
    char num_buf[64];
    for (int i = 0; i < kStateKeyCount; ++i) {
        const char *key = kStateKeys[i];
        float num = 0.0f;
        if (json_get_number(json, key, &num) == 0) {
            snprintf(num_buf, sizeof(num_buf), "%.9g", num);
            set_param_internal(inst, key, num_buf);
            continue;
        }
        if (json_get_string(json, key, val_buf, sizeof(val_buf)) == 0) {
            set_param_internal(inst, key, val_buf);
        }
    }
}

static int build_state_json(const freak_instance_t *inst, char *buf, int buf_len) {
    if (!inst || !buf || buf_len <= 2) return -1;

    int off = 0;
    int first = 1;
    off += snprintf(buf + off, buf_len - off, "{");
    for (int i = 0; i < kStateKeyCount; ++i) {
        char value[128];
        if (get_param_internal(inst, kStateKeys[i], value, sizeof(value)) < 0) {
            continue;
        }
        if (!first) off += snprintf(buf + off, buf_len - off, ",");
        first = 0;

        if (state_value_should_be_string(value)) {
            off += snprintf(buf + off, buf_len - off, "\"%s\":\"%s\"", kStateKeys[i], value);
        } else {
            off += snprintf(buf + off, buf_len - off, "\"%s\":%s", kStateKeys[i], value);
        }

        if (off >= buf_len - 2) return -1;
    }
    off += snprintf(buf + off, buf_len - off, "}");
    return (off < buf_len) ? off : -1;
}

static void *create_instance(const char *module_dir, const char *json_defaults) {
    freak_instance_t *inst = new freak_instance_t();
    ppf_default_params(&inst->params);
    inst->module_dir[0] = '\0';
    if (module_dir && module_dir[0]) {
        snprintf(inst->module_dir, sizeof(inst->module_dir), "%s", module_dir);
    }

    if (json_defaults && json_defaults[0]) {
        apply_state_json(inst, json_defaults);
        char nested_state[16384];
        if (extract_json_value(json_defaults, "state", '{', '}', nested_state, sizeof(nested_state)) > 0) {
            apply_state_json(inst, nested_state);
        }
    }

    inst->engine.init();
    inst->engine.set_params(inst->params);
    set_error(inst, NULL);
    return inst;
}

static void destroy_instance(void *instance) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    delete inst;
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !msg || len < 1) return;

    uint8_t status = msg[0] & 0xF0;
    if ((status == 0x90 || status == 0x80 || status == 0xA0) && len < 3) return;

    if (status == 0x90) {
        int note = msg[1] & 0x7F;
        int vel = msg[2] & 0x7F;
        if (vel == 0) {
            inst->engine.note_off(note);
        } else {
            inst->engine.note_on(note, (float)vel / 127.0f);
        }
    } else if (status == 0x80) {
        int note = msg[1] & 0x7F;
        inst->engine.note_off(note);
    } else if (status == 0xA0) {
        int note = msg[1] & 0x7F;
        float pressure = (float)(msg[2] & 0x7F) / 127.0f;
        inst->engine.poly_aftertouch(note, pressure);
    }
}

static void set_param(void *instance, const char *key, const char *val) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "state") == 0) {
        apply_state_json(inst, val);
        inst->engine.set_params(inst->params);
        set_error(inst, NULL);
        return;
    }

    set_error(inst, NULL);
    if (!set_param_internal(inst, key, val)) {
        char line[256];
        snprintf(line, sizeof(line), "unknown/invalid param: %s=%s", key, val);
        set_error(inst, line);
        return;
    }

    inst->engine.set_params(inst->params);
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;
    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "MrHyde");
    }
    if (strcmp(key, "state") == 0) {
        return build_state_json(inst, buf, buf_len);
    }
    if (strcmp(key, "ui_hierarchy") == 0) {
        return get_ui_hierarchy_with_mod_stars(inst, buf, buf_len);
    }
    if (strcmp(key, "chain_params") == 0) {
        return get_chain_params_dynamic(inst, buf, buf_len);
    }
    return get_param_internal(inst, key, buf, buf_len);
}

static int get_error(void *instance, char *buf, int buf_len) {
    freak_instance_t *inst = (freak_instance_t *)instance;
    if (!inst || !buf || buf_len <= 0) return -1;
    if (inst->last_error[0] == '\0') {
        buf[0] = '\0';
        return 0;
    }
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
    g_host = host;
    plugin_log("init");
    return &g_api_v2;
}
