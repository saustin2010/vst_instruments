#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "host/plugin_api_v1.h"
#include "sh101_control.h"
#include "sh101_env.h"
#include "sh101_filter.h"
#include "sh101_lfo.h"
#include "sh101_osc.h"

typedef enum {
    SH101_GATE_MODE_GATE = 0,
    SH101_GATE_MODE_GATE_TRIG = 1,
    SH101_GATE_MODE_LFO = 2
} sh101_gate_mode_t;

typedef enum {
    SH101_VELOCITY_MODE_OFF = 0,
    SH101_VELOCITY_MODE_TRIGGER = 1,
    SH101_VELOCITY_MODE_ACTIVE_NOTE = 2
} sh101_velocity_mode_t;

typedef enum {
    SH101_PORTA_OFF = 0,
    SH101_PORTA_ON = 1,
    SH101_PORTA_AUTO = 2
} sh101_portamento_mode_t;

typedef enum {
    SH101_LFO_WAVE_TRI = 0,
    SH101_LFO_WAVE_RECT = 1,
    SH101_LFO_WAVE_RANDOM = 2,
    SH101_LFO_WAVE_NOISE = 3
} sh101_lfo_waveform_t;

typedef enum {
    SH101_VCA_MODE_GATE = 0,
    SH101_VCA_MODE_ENV = 1
} sh101_vca_mode_t;

#define SH101_MAX_EXTERNAL_PRESETS 512
#define SH101_MAX_PATH_LEN 512
#define SH101_MAX_NAME_LEN 96

typedef struct {
    char path[SH101_MAX_PATH_LEN];
    char name[SH101_MAX_NAME_LEN];
} sh101_external_preset_t;

typedef struct {
    sh101_control_t control;
    sh101_osc_t osc;
    sh101_env_t amp_env;
    sh101_env_t filt_env;
    sh101_filter_t filter;
    sh101_lfo_t lfo;

    float saw_level;
    float pulse_level;
    float sub_level;
    float noise_level;
    float pulse_width;
    int pwm_mode;
    float pwm_depth;
    float pwm_env_depth;

    float cutoff;
    float resonance;
    float env_amount;
    float filter_volume_correction;
    float key_follow;

    float lfo_pitch;
    float lfo_filter;
    float lfo_pwm;
    int lfo_waveform;
    int lfo_trigger;
    int lfo_sync;
    int lfo_invert;
    int lfo_pitch_snap;
    float lfo_sh_value;
    int lfo_gate_on;       /* tracks LFO gate state for LFO gate mode */
    int lfo_gate_off_count; /* number of gate-off transitions in LFO gate mode */

    float output_level;
    float velocity_sens;
    float filter_velocity_sens;
    float velocity_gain;
    float filter_velocity_gain;
    int retrigger_on_legato;
    float pitch_bend_semitones;
    float pitch_bend;
    float mod_wheel;
    uint32_t drift_rng;
    float drift_target_st;
    float drift_st;
    float fine_tune_cents;
    int current_preset;
    float glide_ms_param;
    int gate_trig_mode;
    int velocity_mode;
    int portamento_mode;
    int portamento_linear;
    int vca_mode;
    int sub_mode;
    int white_noise;
    int filter_env_full_range;
    int filter_env_polarity;
    int same_note_quirk;
    int trigger_count;
    int last_triggered_note;
    float adsr_declick;
    float self_osc_phase;
    float self_osc_level;  /* smoothed self-osc amplitude for gradual ramp-up/decay */
    float prev_cutoff;     /* previous frame's modulated cutoff for stability tracking */
    float dc_block;        /* DC-blocking filter state (models VCF→VCA coupling cap) */
    float active_velocity;
    float held_velocity[128];
    char import_name[96];
    int external_preset_count;
    char module_dir[SH101_MAX_PATH_LEN];
    sh101_external_preset_t external_presets[SH101_MAX_EXTERNAL_PRESETS];

    char last_error[160];
} sh101_instance_t;

static const host_api_v1_t *g_host = NULL;

typedef struct {
    const char *name;
    float saw;
    float pulse;
    float sub;
    float noise;
    float pulse_width;
    float pwm_depth;
    float cutoff;
    float resonance;
    float env_amount;
    float key_follow;
    float amp_a;
    float amp_d;
    float amp_s;
    float amp_r;
    float filt_a;
    float filt_d;
    float filt_s;
    float filt_r;
    float glide_ms;
    float lfo_rate_hz;
    float lfo_pitch;
    float lfo_filter;
    float lfo_pwm;
    float velocity_sens;
    float filter_velocity_sens;
    int priority;
    int retrigger_on_legato;
    float output_level;
} sh101_preset_t;

static const sh101_preset_t g_presets[] = {
    {
        .name = "Init",
        .saw = 0.50f, .pulse = 0.00f, .sub = 0.00f, .noise = 0.00f,
        .pulse_width = 0.50f, .pwm_depth = 0.00f,
        .cutoff = 1.00f, .resonance = 0.00f, .env_amount = 0.00f, .key_follow = 0.50f,
        .amp_a = 0.005f, .amp_d = 0.20f, .amp_s = 1.00f, .amp_r = 0.20f,
        .filt_a = 0.005f, .filt_d = 0.20f, .filt_s = 0.00f, .filt_r = 0.20f,
        .glide_ms = 0.0f, .lfo_rate_hz = 5.0f, .lfo_pitch = 0.00f, .lfo_filter = 0.00f, .lfo_pwm = 0.00f,
        .velocity_sens = 0.50f, .filter_velocity_sens = 0.00f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 1, .output_level = 0.85f
    },
    {
        .name = "Classic 101 Sub Bass",
        .saw = 0.12f, .pulse = 0.68f, .sub = 1.00f, .noise = 0.00f,
        .pulse_width = 0.50f, .pwm_depth = 0.03f,
        .cutoff = 0.24f, .resonance = 0.18f, .env_amount = 0.28f, .key_follow = 0.24f,
        .amp_a = 0.001f, .amp_d = 0.09f, .amp_s = 0.88f, .amp_r = 0.10f,
        .filt_a = 0.001f, .filt_d = 0.16f, .filt_s = 0.10f, .filt_r = 0.09f,
        .glide_ms = 0.0f, .lfo_rate_hz = 5.0f, .lfo_pitch = 0.00f, .lfo_filter = 0.00f, .lfo_pwm = 0.02f,
        .velocity_sens = 0.30f, .filter_velocity_sens = 0.12f, .priority = SH101_NOTE_PRIORITY_LOWEST,
        .retrigger_on_legato = 0, .output_level = 0.48f
    },
    {
        .name = "Acid-ish Square Bass",
        .saw = 0.00f, .pulse = 0.96f, .sub = 0.62f, .noise = 0.00f,
        .pulse_width = 0.43f, .pwm_depth = 0.08f,
        .cutoff = 0.30f, .resonance = 0.92f, .env_amount = 0.96f, .key_follow = 0.34f,
        .amp_a = 0.001f, .amp_d = 0.10f, .amp_s = 0.24f, .amp_r = 0.07f,
        .filt_a = 0.001f, .filt_d = 0.13f, .filt_s = 0.00f, .filt_r = 0.06f,
        .glide_ms = 45.0f, .lfo_rate_hz = 5.7f, .lfo_pitch = 0.00f, .lfo_filter = 0.02f, .lfo_pwm = 0.03f,
        .velocity_sens = 0.56f, .filter_velocity_sens = 0.28f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 1, .output_level = 0.64f
    },
    {
        .name = "Hollow PWM Lead",
        .saw = 0.22f, .pulse = 0.93f, .sub = 0.22f, .noise = 0.00f,
        .pulse_width = 0.56f, .pwm_depth = 0.72f,
        .cutoff = 0.58f, .resonance = 0.36f, .env_amount = 0.46f, .key_follow = 0.66f,
        .amp_a = 0.003f, .amp_d = 0.15f, .amp_s = 0.70f, .amp_r = 0.22f,
        .filt_a = 0.002f, .filt_d = 0.18f, .filt_s = 0.32f, .filt_r = 0.20f,
        .glide_ms = 14.0f, .lfo_rate_hz = 3.2f, .lfo_pitch = 0.04f, .lfo_filter = 0.05f, .lfo_pwm = 0.62f,
        .velocity_sens = 0.62f, .filter_velocity_sens = 0.34f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 0, .output_level = 0.64f
    },
    {
        .name = "Laser Zap Pitch Dive",
        .saw = 0.78f, .pulse = 0.18f, .sub = 0.08f, .noise = 0.06f,
        .pulse_width = 0.36f, .pwm_depth = 0.10f,
        .cutoff = 0.64f, .resonance = 0.58f, .env_amount = 0.82f, .key_follow = 0.40f,
        .amp_a = 0.001f, .amp_d = 0.09f, .amp_s = 0.00f, .amp_r = 0.05f,
        .filt_a = 0.001f, .filt_d = 0.06f, .filt_s = 0.00f, .filt_r = 0.04f,
        .glide_ms = 85.0f, .lfo_rate_hz = 12.0f, .lfo_pitch = 0.22f, .lfo_filter = 0.00f, .lfo_pwm = 0.00f,
        .velocity_sens = 0.58f, .filter_velocity_sens = 0.40f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 1, .output_level = 0.86f
    },
    {
        .name = "Resonant Pluck",
        .saw = 0.48f, .pulse = 0.52f, .sub = 0.20f, .noise = 0.00f,
        .pulse_width = 0.45f, .pwm_depth = 0.20f,
        .cutoff = 0.44f, .resonance = 0.86f, .env_amount = 0.88f, .key_follow = 0.48f,
        .amp_a = 0.001f, .amp_d = 0.11f, .amp_s = 0.04f, .amp_r = 0.08f,
        .filt_a = 0.001f, .filt_d = 0.09f, .filt_s = 0.00f, .filt_r = 0.08f,
        .glide_ms = 0.0f, .lfo_rate_hz = 5.3f, .lfo_pitch = 0.00f, .lfo_filter = 0.04f, .lfo_pwm = 0.18f,
        .velocity_sens = 0.64f, .filter_velocity_sens = 0.46f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 1, .output_level = 0.86f
    },
    {
        .name = "Self-Osc Sine Lead",
        .saw = 0.75f, .pulse = 0.02f, .sub = 0.00f, .noise = 0.00f,
        .pulse_width = 0.50f, .pwm_depth = 0.00f,
        .cutoff = 0.60f, .resonance = 1.14f, .env_amount = 0.06f, .key_follow = 1.00f,
        .amp_a = 0.002f, .amp_d = 0.16f, .amp_s = 0.80f, .amp_r = 0.22f,
        .filt_a = 0.002f, .filt_d = 0.14f, .filt_s = 0.78f, .filt_r = 0.22f,
        .glide_ms = 8.0f, .lfo_rate_hz = 5.0f, .lfo_pitch = 0.03f, .lfo_filter = 0.00f, .lfo_pwm = 0.00f,
        .velocity_sens = 0.42f, .filter_velocity_sens = 0.16f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 0, .output_level = 0.92f
    },
    {
        .name = "Noisy Snare Hit",
        .saw = 0.08f, .pulse = 0.06f, .sub = 0.00f, .noise = 0.96f,
        .pulse_width = 0.30f, .pwm_depth = 0.18f,
        .cutoff = 0.62f, .resonance = 0.44f, .env_amount = 0.66f, .key_follow = 0.08f,
        .amp_a = 0.001f, .amp_d = 0.36f, .amp_s = 0.00f, .amp_r = 0.18f,
        .filt_a = 0.001f, .filt_d = 0.07f, .filt_s = 0.00f, .filt_r = 0.05f,
        .glide_ms = 0.0f, .lfo_rate_hz = 9.0f, .lfo_pitch = 0.00f, .lfo_filter = 0.08f, .lfo_pwm = 0.00f,
        .velocity_sens = 0.72f, .filter_velocity_sens = 0.48f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 1, .output_level = 1.00f
    },
    {
        .name = "Portamento Mono Lead",
        .saw = 0.36f, .pulse = 0.76f, .sub = 0.20f, .noise = 0.00f,
        .pulse_width = 0.49f, .pwm_depth = 0.22f,
        .cutoff = 0.56f, .resonance = 0.30f, .env_amount = 0.42f, .key_follow = 0.74f,
        .amp_a = 0.002f, .amp_d = 0.17f, .amp_s = 0.68f, .amp_r = 0.24f,
        .filt_a = 0.003f, .filt_d = 0.20f, .filt_s = 0.38f, .filt_r = 0.20f,
        .glide_ms = 96.0f, .lfo_rate_hz = 4.6f, .lfo_pitch = 0.12f, .lfo_filter = 0.06f, .lfo_pwm = 0.22f,
        .velocity_sens = 0.54f, .filter_velocity_sens = 0.30f, .priority = SH101_NOTE_PRIORITY_LOWEST,
        .retrigger_on_legato = 0, .output_level = 0.62f
    },
    {
        .name = "Arp Pluck Clocked",
        .saw = 0.54f, .pulse = 0.24f, .sub = 0.30f, .noise = 0.02f,
        .pulse_width = 0.42f, .pwm_depth = 0.10f,
        .cutoff = 0.50f, .resonance = 0.68f, .env_amount = 0.74f, .key_follow = 0.52f,
        .amp_a = 0.001f, .amp_d = 0.08f, .amp_s = 0.00f, .amp_r = 0.04f,
        .filt_a = 0.001f, .filt_d = 0.10f, .filt_s = 0.02f, .filt_r = 0.05f,
        .glide_ms = 0.0f, .lfo_rate_hz = 6.4f, .lfo_pitch = 0.00f, .lfo_filter = 0.10f, .lfo_pwm = 0.06f,
        .velocity_sens = 0.66f, .filter_velocity_sens = 0.44f, .priority = SH101_NOTE_PRIORITY_LAST,
        .retrigger_on_legato = 1, .output_level = 0.86f
    },
    {
        .name = "Random Filter Burble",
        .saw = 0.42f, .pulse = 0.20f, .sub = 0.30f, .noise = 0.18f,
        .pulse_width = 0.57f, .pwm_depth = 0.26f,
        .cutoff = 0.34f, .resonance = 0.74f, .env_amount = 0.22f, .key_follow = 0.40f,
        .amp_a = 0.004f, .amp_d = 0.36f, .amp_s = 0.62f, .amp_r = 0.40f,
        .filt_a = 0.001f, .filt_d = 0.18f, .filt_s = 0.12f, .filt_r = 0.24f,
        .glide_ms = 22.0f, .lfo_rate_hz = 8.8f, .lfo_pitch = 0.00f, .lfo_filter = 0.84f, .lfo_pwm = 0.24f,
        .velocity_sens = 0.40f, .filter_velocity_sens = 0.26f, .priority = SH101_NOTE_PRIORITY_LOWEST,
        .retrigger_on_legato = 0, .output_level = 0.82f
    }
};

#define SH101_PRESET_COUNT ((int)(sizeof(g_presets) / sizeof(g_presets[0])))

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void clear_error(sh101_instance_t *inst) {
    inst->last_error[0] = '\0';
}

static void set_errorf(sh101_instance_t *inst, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(inst->last_error, sizeof(inst->last_error), fmt, ap);
    va_end(ap);
}

static float depth_curve(float x) {
    x = clampf(x, 0.0f, 1.0f);
    return x * x * (2.0f - x);
}

static float rand_unit(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return (float)((*state >> 8) & 0x00FFFFFFu) / 16777215.0f;
}

static const char* find_bytes(const char *hay, size_t hay_len, const char *needle, size_t needle_len) {
    if (!hay || !needle || needle_len == 0 || hay_len < needle_len) return NULL;
    size_t max_i = hay_len - needle_len;
    for (size_t i = 0; i <= max_i; ++i) {
        if (memcmp(hay + i, needle, needle_len) == 0) return hay + i;
    }
    return NULL;
}

static int tal_extract_xml(const char *blob, size_t blob_len, const char **xml_start, size_t *xml_len) {
    static const char *k_start_a = "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <tal ";
    static const char *k_start_b = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><tal ";
    static const char *k_tal = "<tal ";
    static const char *k_end = "</tal>";
    const char *start = find_bytes(blob, blob_len, k_start_a, strlen(k_start_a));
    if (!start) start = find_bytes(blob, blob_len, k_start_b, strlen(k_start_b));
    if (!start) start = find_bytes(blob, blob_len, k_tal, strlen(k_tal));
    if (!start) return 0;

    size_t rem = blob_len - (size_t)(start - blob);
    const char *end = find_bytes(start, rem, k_end, strlen(k_end));
    if (!end) return 0;

    *xml_start = start;
    *xml_len = (size_t)(end - start) + strlen(k_end);
    return 1;
}

static int tal_attr_get_string(const char *xml, size_t xml_len, const char *key, char *out, size_t out_len) {
    const char *program = find_bytes(xml, xml_len, "<program ", strlen("<program "));
    if (!program || out_len == 0) return 0;
    const char *program_end = memchr(program, '>', xml_len - (size_t)(program - xml));
    if (!program_end) return 0;

    char pattern[96];
    snprintf(pattern, sizeof(pattern), " %s=\"", key);
    const char *attr = find_bytes(program, (size_t)(program_end - program), pattern, strlen(pattern));
    if (!attr) return 0;
    const char *val_start = attr + strlen(pattern);
    const char *val_end = memchr(val_start, '"', (size_t)(program_end - val_start));
    if (!val_end) return 0;

    size_t n = (size_t)(val_end - val_start);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, val_start, n);
    out[n] = '\0';
    return 1;
}

static float tal_attr_get_float(const char *xml, size_t xml_len, const char *key, float fallback) {
    char tmp[64];
    if (!tal_attr_get_string(xml, xml_len, key, tmp, sizeof(tmp))) return fallback;
    return strtof(tmp, NULL);
}

static float tal_time_from_norm(float value, float lo, float hi) {
    float n = clampf(value, 0.0f, 1.0f);
    if (n <= 0.0f) return lo;
    if (n >= 1.0f) return hi;
    return lo * powf(hi / lo, n);
}

static float tal_time_from_norm_power(float value, float lo, float hi, float exponent) {
    float n = clampf(value, 0.0f, 1.0f);
    return lo + powf(n, exponent) * (hi - lo);
}

static int tal_three_state(float value) {
    float n = clampf(value, 0.0f, 1.0f);
    if (n < 0.25f) return 0;
    if (n < 0.75f) return 1;
    return 2;
}

static int tal_adsr_mode_to_gate_mode(float mode) {
    int mode3 = tal_three_state(mode);
    if (mode3 == 0) return SH101_GATE_MODE_LFO;
    if (mode3 == 1) return SH101_GATE_MODE_GATE;
    return SH101_GATE_MODE_GATE_TRIG;
}

static int tal_portamento_mode(float mode) {
    int mode3 = tal_three_state(mode);
    if (mode3 == 0) return SH101_PORTA_AUTO;
    if (mode3 == 1) return SH101_PORTA_OFF;
    return SH101_PORTA_ON;
}

static int tal_lfo_waveform(float value) {
    float n = clampf(value, 0.0f, 1.0f);
    if (n < (1.0f / 6.0f)) return SH101_LFO_WAVE_TRI;
    if (n < 0.5f) return SH101_LFO_WAVE_RECT;
    if (n < (5.0f / 6.0f)) return SH101_LFO_WAVE_RANDOM;
    return SH101_LFO_WAVE_NOISE;
}

static int tal_dco_range_octaves(float value) {
    float n = clampf(value, 0.0f, 1.0f);
    if (n < (1.0f / 6.0f)) return -1; /* 16' */
    if (n < 0.5f) return 0;           /* 8'  */
    if (n < (5.0f / 6.0f)) return 1;  /* 4'  */
    return 2;                         /* 2'  */
}

static int import_vstpreset_path(sh101_instance_t *inst, const char *path);

static int load_file_blob(const char *path, char **blob_out, size_t *blob_len_out) {
    FILE *fp;
    long file_size;
    char *blob;
    size_t nread;

    if (!path || !blob_out || !blob_len_out) return 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    file_size = ftell(fp);
    if (file_size <= 0 || file_size > 32 * 1024 * 1024) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    blob = (char*)malloc((size_t)file_size);
    if (!blob) {
        fclose(fp);
        return 0;
    }
    nread = fread(blob, 1, (size_t)file_size, fp);
    fclose(fp);
    if (nread != (size_t)file_size) {
        free(blob);
        return 0;
    }

    *blob_out = blob;
    *blob_len_out = (size_t)file_size;
    return 1;
}

static int has_vstpreset_ext(const char *name) {
    size_t name_len;
    if (!name) return 0;
    name_len = strlen(name);
    if (name_len > 10 && strcmp(name + name_len - 10, ".vstpreset") == 0) return 1;
    if (name_len > 9 && strcmp(name + name_len - 9, ".bassline") == 0) return 1;
    return 0;
}

static void basename_no_ext(const char *path, char *out, size_t out_len) {
    const char *base = path;
    const char *slash = strrchr(path, '/');
    size_t n;
    if (slash) base = slash + 1;
    n = strlen(base);
    if (n > 10 && strcmp(base + n - 10, ".vstpreset") == 0) {
        n -= 10;
    } else if (n > 9 && strcmp(base + n - 9, ".bassline") == 0) {
        n -= 9;
    }
    if (n >= out_len) n = out_len - 1;
    memcpy(out, base, n);
    out[n] = '\0';
}

static int external_preset_name_cmp(const void *a, const void *b) {
    const sh101_external_preset_t *pa = (const sh101_external_preset_t*)a;
    const sh101_external_preset_t *pb = (const sh101_external_preset_t*)b;
    int by_name = strcmp(pa->name, pb->name);
    if (by_name != 0) return by_name;
    return strcmp(pa->path, pb->path);
}

static void scan_external_presets_recursive(sh101_instance_t *inst, const char *dir_path) {
    DIR *dir;
    struct dirent *ent;

    if (inst->external_preset_count >= SH101_MAX_EXTERNAL_PRESETS) return;
    dir = opendir(dir_path);
    if (!dir) return;

    while ((ent = readdir(dir)) != NULL) {
        char full[SH101_MAX_PATH_LEN];
        struct stat st;
        char *blob = NULL;
        size_t blob_len = 0;
        const char *xml = NULL;
        size_t xml_len = 0;
        sh101_external_preset_t *dst;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name) >= (int)sizeof(full)) continue;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_external_presets_recursive(inst, full);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !has_vstpreset_ext(ent->d_name)) continue;
        if (inst->external_preset_count >= SH101_MAX_EXTERNAL_PRESETS) break;
        if (!load_file_blob(full, &blob, &blob_len)) continue;
        if (!tal_extract_xml(blob, blob_len, &xml, &xml_len)) {
            free(blob);
            continue;
        }

        dst = &inst->external_presets[inst->external_preset_count];
        snprintf(dst->path, sizeof(dst->path), "%s", full);
        if (!tal_attr_get_string(xml, xml_len, "programname", dst->name, sizeof(dst->name))) {
            basename_no_ext(full, dst->name, sizeof(dst->name));
        }
        inst->external_preset_count += 1;
        free(blob);
    }
    closedir(dir);
}

static void scan_external_presets(sh101_instance_t *inst) {
    char presets_dir[SH101_MAX_PATH_LEN];
    inst->external_preset_count = 0;
    if (inst->module_dir[0] == '\0') return;
    if (snprintf(presets_dir, sizeof(presets_dir), "%s/presets", inst->module_dir) >= (int)sizeof(presets_dir)) return;
    scan_external_presets_recursive(inst, presets_dir);
    if (inst->external_preset_count > 1) {
        qsort(inst->external_presets,
              (size_t)inst->external_preset_count,
              sizeof(inst->external_presets[0]),
              external_preset_name_cmp);
    }
}

static void sh101_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[sh101] %s", msg);
        g_host->log(buf);
    }
}

static float note_to_cutoff_hz(int note, float cutoff_norm, float key_follow) {
    float base = 30.0f + cutoff_norm * cutoff_norm * 15000.0f;
    float k = ((float)note - 60.0f) / 12.0f;
    float follow = powf(2.0f, k * key_follow);
    return clampf(base * follow, 20.0f, 18000.0f);
}

static float pick_active_note_velocity(const sh101_instance_t *inst) {
    int note = inst->control.current_note;
    if (note < 0 || note > 127) return 1.0f;
    return clampf(inst->held_velocity[note], 0.0f, 1.0f);
}

static void apply_velocity_response(sh101_instance_t *inst) {
    if (inst->velocity_mode == SH101_VELOCITY_MODE_OFF) {
        inst->active_velocity = 1.0f;
        inst->velocity_gain = 1.0f;
        inst->filter_velocity_gain = 1.0f;
        return;
    }

    float vel = clampf(inst->active_velocity, 0.0f, 1.0f);
    float shaped = powf(vel, 0.60f); /* Concave response keeps low-mid resolution musical. */
    float amp_floor = 1.0f - 0.75f * inst->velocity_sens;
    float filt_floor = 1.0f - 0.45f * inst->filter_velocity_sens;

    inst->velocity_gain = clampf(amp_floor + (1.0f - amp_floor) * shaped, 0.05f, 1.0f);
    inst->filter_velocity_gain = clampf(filt_floor + (1.0f - filt_floor) * shaped, 0.05f, 1.0f);
}

static void sync_priority_from_mode(sh101_instance_t *inst) {
    sh101_note_priority_t p = SH101_NOTE_PRIORITY_LOWEST;
    if (inst->gate_trig_mode == SH101_GATE_MODE_GATE_TRIG) {
        p = SH101_NOTE_PRIORITY_LAST;
    }
    sh101_control_set_priority(&inst->control, p);
}

static void sync_portamento_mode(sh101_instance_t *inst) {
    inst->control.glide_always = 0;
    inst->control.glide_linear = inst->portamento_linear ? 1 : 0;
    if (inst->portamento_mode == SH101_PORTA_OFF) {
        inst->control.glide_ms = 0.0f;
        return;
    }
    if (inst->portamento_mode == SH101_PORTA_ON) {
        inst->control.glide_always = 1;
    }
    inst->control.glide_ms = inst->glide_ms_param;
}

static float quantize_lfo_rate_sync(float rate_hz) {
    static const float k_rates[] = {
        0.125f, 0.1666667f, 0.25f, 0.3333333f, 0.5f,
        0.6666667f, 1.0f, 1.3333333f, 2.0f, 2.6666667f,
        4.0f, 5.3333333f, 8.0f, 10.6666667f, 16.0f, 21.3333333f, 32.0f
    };
    float best = k_rates[0];
    float best_d = fabsf(rate_hz - best);
    for (size_t i = 1; i < sizeof(k_rates) / sizeof(k_rates[0]); ++i) {
        float d = fabsf(rate_hz - k_rates[i]);
        if (d < best_d) {
            best_d = d;
            best = k_rates[i];
        }
    }
    return best;
}

static void sync_lfo_rate_mode(sh101_instance_t *inst) {
    float rate = clampf(inst->lfo.rate_hz, 0.02f, 40.0f);
    if (inst->lfo_sync) {
        rate = quantize_lfo_rate_sync(rate);
    }
    sh101_lfo_set_rate_hz(&inst->lfo, rate);
}

static void apply_tal_program_xml(sh101_instance_t *inst, const char *xml, size_t xml_len) {
    float dco_lfo = clampf(tal_attr_get_float(xml, xml_len, "dcolfovalue", 0.0f), 0.0f, 1.0f);
    int pwm_mode = tal_three_state(tal_attr_get_float(xml, xml_len, "dcopwmmode", 0.0f));
    float pwm_value = clampf(tal_attr_get_float(xml, xml_len, "dcopwmvalue", 0.5f), 0.0f, 1.0f);
    float pwm_depth = (pwm_mode == 2) ? pwm_value : 0.0f;
    float pwm_env_depth = (pwm_mode == 0) ? pwm_value : 0.0f;
    float adsr_mode = clampf(tal_attr_get_float(xml, xml_len, "adsrmode", 1.0f), 0.0f, 1.0f);

    float attack = tal_time_from_norm(tal_attr_get_float(xml, xml_len, "adsrattack", 0.0f), 0.001f, 4.0f);
    float decay = tal_time_from_norm_power(tal_attr_get_float(xml, xml_len, "adsrdecay", 0.0f), 0.001f, 6.0f, 2.2f);
    float sustain = clampf(tal_attr_get_float(xml, xml_len, "adsrsustain", 0.0f), 0.0f, 1.0f);
    float release = tal_time_from_norm_power(tal_attr_get_float(xml, xml_len, "adsrrelease", 0.0f), 0.001f, 8.0f, 2.2f);
    float adsr_declick = clampf(tal_attr_get_float(xml, xml_len, "adsrdecklick", 0.0f), 0.0f, 1.0f);
    attack = fmaxf(attack, 0.001f + adsr_declick * 0.004f);
    release = fmaxf(release, 0.001f + adsr_declick * 0.006f);

    float env_raw = clampf(tal_attr_get_float(xml, xml_len, "filterenvelopevalue", 0.0f), 0.0f, 1.0f);
    int env_full = (tal_attr_get_float(xml, xml_len, "filterenvelopevaluefullrange", 0.0f) >= 0.5f) ? 1 : 0;
    float env_amt = env_raw;
    int env_polarity = 0;
    if (env_full) {
        float centered = (env_raw - 0.5f) * 2.0f;
        env_amt = fabsf(centered);
        env_polarity = (centered < 0.0f) ? 1 : 0;
    }

    float velocity_sens = clampf(tal_attr_get_float(xml, xml_len, "controlvelocityvolume", 0.0f), 0.0f, 1.0f);
    float filter_velocity_sens = clampf(tal_attr_get_float(xml, xml_len, "controlvelocityenvelope", 0.0f), 0.0f, 1.0f);
    int velocity_mode = (fmaxf(velocity_sens, filter_velocity_sens) > 0.01f) ? SH101_VELOCITY_MODE_TRIGGER : SH101_VELOCITY_MODE_OFF;

    inst->portamento_mode = tal_portamento_mode(tal_attr_get_float(xml, xml_len, "portamentomode", 0.0f));
    inst->portamento_linear = (tal_attr_get_float(xml, xml_len, "portamentolinear", 0.0f) >= 0.5f) ? 1 : 0;

    {
        int tal_oct = (int)lroundf((clampf(tal_attr_get_float(xml, xml_len, "octavetranspose", 0.5f), 0.0f, 1.0f) - 0.5f) * 4.0f);
        int tal_range = tal_dco_range_octaves(tal_attr_get_float(xml, xml_len, "dcorange", 0.5f));
        int total_oct = tal_oct + tal_range;
        int octave_transpose = clamp_int(total_oct, -2, 2);
        int transpose = clamp_int((total_oct - octave_transpose) * 12, -24, 24);
        sh101_control_set_transpose(&inst->control, clamp_int(octave_transpose * 12 + transpose, -24, 24));
    }

    inst->saw_level = clampf(tal_attr_get_float(xml, xml_len, "sawvolume", 0.0f), 0.0f, 1.0f);
    inst->pulse_level = clampf(tal_attr_get_float(xml, xml_len, "pulsevolume", 0.0f), 0.0f, 1.0f);
    inst->sub_level = clampf(tal_attr_get_float(xml, xml_len, "suboscvolume", 0.0f), 0.0f, 1.0f);
    inst->sub_mode = tal_three_state(tal_attr_get_float(xml, xml_len, "suboscmode", 0.0f));
    inst->noise_level = clampf(tal_attr_get_float(xml, xml_len, "noisevolume", 0.0f), 0.0f, 1.0f);
    inst->white_noise = (tal_attr_get_float(xml, xml_len, "whitenoiseenabled", 0.0f) >= 0.5f) ? 1 : 0;
    inst->pulse_width = clampf(pwm_value, 0.05f, 0.95f);
    inst->pwm_mode = pwm_mode;
    inst->pwm_depth = pwm_depth;
    inst->pwm_env_depth = pwm_env_depth;

    inst->cutoff = sqrtf(clampf(tal_attr_get_float(xml, xml_len, "filtercutoff", 0.5f), 0.0f, 1.0f));
    inst->resonance = clampf(tal_attr_get_float(xml, xml_len, "filterresonance", 0.2f) * 1.2f, 0.0f, 1.2f);
    inst->env_amount = clampf(env_amt, 0.0f, 1.0f);
    inst->filter_volume_correction = clampf(tal_attr_get_float(xml, xml_len, "filtervolumecorrection", 0.0f), 0.0f, 1.0f);
    inst->filter_env_full_range = env_full;
    inst->filter_env_polarity = env_polarity;
    inst->key_follow = clampf(tal_attr_get_float(xml, xml_len, "filterkeyboardvalue", 0.5f), 0.0f, 1.0f);

    inst->glide_ms_param = clampf(tal_attr_get_float(xml, xml_len, "portamentointensity", 0.0f), 0.0f, 1.0f) * 500.0f;
    inst->control.glide_linear = inst->portamento_linear;
    sync_portamento_mode(inst);

    sh101_lfo_set_rate_hz(&inst->lfo, 0.02f + clampf(tal_attr_get_float(xml, xml_len, "lforate", 0.0f), 0.0f, 1.0f) * (40.0f - 0.02f));
    inst->lfo_waveform = tal_lfo_waveform(tal_attr_get_float(xml, xml_len, "lfowaveform", 0.0f));
    inst->lfo_trigger = (tal_attr_get_float(xml, xml_len, "lfotrigger", 0.0f) >= 0.5f) ? 1 : 0;
    inst->lfo_sync = (tal_attr_get_float(xml, xml_len, "lfosync", 0.0f) >= 0.5f) ? 1 : 0;
    inst->lfo_invert = (tal_attr_get_float(xml, xml_len, "lfoinverted", 0.0f) >= 0.5f) ? 1 : 0;
    inst->lfo_pitch_snap = (tal_attr_get_float(xml, xml_len, "dcolfovaluesnap", 0.0f) >= 0.5f) ? 1 : 0;
    inst->lfo_pitch = dco_lfo;
    inst->lfo_filter = clampf(tal_attr_get_float(xml, xml_len, "filtermodulationvalue", 0.0f), 0.0f, 1.0f);
    inst->lfo_pwm = pwm_depth;
    sync_lfo_rate_mode(inst);

    inst->velocity_sens = velocity_sens;
    inst->filter_velocity_sens = filter_velocity_sens;
    inst->velocity_mode = velocity_mode;
    inst->active_velocity = (velocity_mode == SH101_VELOCITY_MODE_OFF) ? 1.0f : inst->active_velocity;
    apply_velocity_response(inst);

    inst->gate_trig_mode = tal_adsr_mode_to_gate_mode(adsr_mode);
    inst->retrigger_on_legato = (inst->gate_trig_mode == SH101_GATE_MODE_GATE_TRIG) ? 1 : 0;
    sync_priority_from_mode(inst);
    inst->vca_mode = (tal_attr_get_float(xml, xml_len, "vcamode", 1.0f) >= 0.5f) ? SH101_VCA_MODE_ENV : SH101_VCA_MODE_GATE;
    inst->adsr_declick = adsr_declick;
    inst->fine_tune_cents = clampf((clampf(tal_attr_get_float(xml, xml_len, "masterfinetune", 0.5f), 0.0f, 1.0f) - 0.5f) * 200.0f, -100.0f, 100.0f);
    inst->output_level = clampf(tal_attr_get_float(xml, xml_len, "volume", 0.8f), 0.0f, 1.0f) * 0.80f;

    sh101_env_set_adsr(&inst->amp_env, attack, decay, sustain, release);
    sh101_env_set_adsr(&inst->filt_env, attack, decay, sustain, release);

    if (!tal_attr_get_string(xml, xml_len, "programname", inst->import_name, sizeof(inst->import_name))) {
        snprintf(inst->import_name, sizeof(inst->import_name), "Imported TAL Preset");
    }
}

static int import_vstpreset_path(sh101_instance_t *inst, const char *path) {
    char *blob;
    size_t blob_len;
    const char *xml;
    size_t xml_len;

    if (!path || path[0] == '\0') {
        set_errorf(inst, "import_vstpreset_path: empty path");
        return 0;
    }
    if (!load_file_blob(path, &blob, &blob_len)) {
        set_errorf(inst, "import_vstpreset_path: cannot read '%s'", path);
        return 0;
    }
    if (!tal_extract_xml(blob, blob_len, &xml, &xml_len)) {
        free(blob);
        set_errorf(inst, "import_vstpreset_path: TAL XML not found");
        return 0;
    }

    apply_tal_program_xml(inst, xml, xml_len);
    free(blob);
    clear_error(inst);
    return 1;
}

static void trigger_envelopes(sh101_instance_t *inst, int hard_reset) {
    if (hard_reset) {
        /* adsr_declick controls how aggressively we reset the envelope on
           retrigger.  0 = instant zero (original behaviour, clicks at low
           levels), 1 = keep full current value (soft retrigger, click-free).
           Intermediate values blend between the two. */
        float keep = clampf(inst->adsr_declick, 0.0f, 1.0f);
        inst->amp_env.value  *= keep;
        inst->filt_env.value *= keep;
        inst->self_osc_phase = 0.0f;
    }
    sh101_env_gate_on(&inst->amp_env, 1.0f);
    sh101_env_gate_on(&inst->filt_env, 1.0f);
    inst->trigger_count += 1;
}

static int should_note_on_trigger(sh101_instance_t *inst, int note, int was_gate) {
    if (inst->gate_trig_mode == SH101_GATE_MODE_GATE) {
        return (!was_gate || inst->retrigger_on_legato);
    }
    if (inst->gate_trig_mode == SH101_GATE_MODE_GATE_TRIG) {
        if (inst->same_note_quirk && note == inst->last_triggered_note) {
            return 0;
        }
        return 1;
    }
    /* SH101_GATE_MODE_LFO */
    return !was_gate;
}

static void apply_preset(sh101_instance_t *inst, int preset_index) {
    int total = SH101_PRESET_COUNT + inst->external_preset_count;
    int i;
    const sh101_preset_t *p;

    if (total <= 0) return;
    i = clamp_int(preset_index, 0, total - 1);
    if (i >= SH101_PRESET_COUNT) {
        const sh101_external_preset_t *ext = &inst->external_presets[i - SH101_PRESET_COUNT];
        if (import_vstpreset_path(inst, ext->path)) {
            inst->current_preset = i;
            snprintf(inst->import_name, sizeof(inst->import_name), "%s", ext->name);
        }
        return;
    }
    p = &g_presets[i];

    inst->saw_level = p->saw;
    inst->pulse_level = p->pulse;
    inst->sub_level = p->sub;
    inst->noise_level = p->noise;
    inst->pulse_width = p->pulse_width;
    inst->pwm_mode = 2;
    inst->pwm_depth = p->pwm_depth;
    inst->pwm_env_depth = 0.0f;
    inst->cutoff = p->cutoff;
    inst->resonance = p->resonance;
    inst->env_amount = p->env_amount;
    inst->filter_volume_correction = 0.0f;
    inst->key_follow = p->key_follow;
    inst->lfo_pitch = p->lfo_pitch;
    inst->lfo_filter = p->lfo_filter;
    inst->lfo_pwm = p->lfo_pwm;
    inst->lfo_waveform = SH101_LFO_WAVE_TRI;
    inst->lfo_trigger = 0;
    inst->lfo_sync = 0;
    inst->lfo_invert = 0;
    inst->lfo_pitch_snap = 0;
    inst->lfo_sh_value = 0.0f;
    inst->lfo_gate_on = 0;
    inst->lfo_gate_off_count = 0;
    inst->output_level = p->output_level;
    inst->velocity_sens = p->velocity_sens;
    inst->filter_velocity_sens = p->filter_velocity_sens;
    inst->retrigger_on_legato = p->retrigger_on_legato ? 1 : 0;
    inst->fine_tune_cents = 0.0f;
    inst->glide_ms_param = p->glide_ms;
    inst->gate_trig_mode = (p->priority == SH101_NOTE_PRIORITY_LAST) ? SH101_GATE_MODE_GATE_TRIG : SH101_GATE_MODE_GATE;
    inst->velocity_mode = SH101_VELOCITY_MODE_OFF;
    inst->portamento_mode = SH101_PORTA_AUTO;
    inst->portamento_linear = 0;
    inst->vca_mode = SH101_VCA_MODE_ENV;
    inst->sub_mode = 0;
    inst->white_noise = 0;
    inst->filter_env_full_range = 0;
    inst->filter_env_polarity = 0;
    inst->same_note_quirk = 0;
    inst->adsr_declick = 0.65f;
    inst->self_osc_phase = 0.0f;
    inst->dc_block = 0.0f;
    inst->active_velocity = 1.0f;
    inst->velocity_gain = 1.0f;
    inst->filter_velocity_gain = 1.0f;
    inst->current_preset = i;
    snprintf(inst->import_name, sizeof(inst->import_name), "%s", p->name);

    sync_priority_from_mode(inst);
    sync_portamento_mode(inst);
    sh101_env_set_adsr(&inst->amp_env, p->amp_a, p->amp_d, p->amp_s, p->amp_r);
    sh101_env_set_adsr(&inst->filt_env, p->filt_a, p->filt_d, p->filt_s, p->filt_r);
    sh101_lfo_set_rate_hz(&inst->lfo, p->lfo_rate_hz);
    sync_lfo_rate_mode(inst);
    apply_velocity_response(inst);
}

static void reset_voice(sh101_instance_t *inst) {
    float amp_a = inst->amp_env.attack_s;
    float amp_d = inst->amp_env.decay_s;
    float amp_s = inst->amp_env.sustain;
    float amp_r = inst->amp_env.release_s;
    float filt_a = inst->filt_env.attack_s;
    float filt_d = inst->filt_env.decay_s;
    float filt_s = inst->filt_env.sustain;
    float filt_r = inst->filt_env.release_s;

    sh101_env_init(&inst->amp_env, inst->control.sample_rate);
    sh101_env_set_adsr(&inst->amp_env, amp_a, amp_d, amp_s, amp_r);
    sh101_env_init(&inst->filt_env, inst->control.sample_rate);
    sh101_env_set_adsr(&inst->filt_env, filt_a, filt_d, filt_s, filt_r);
    sh101_filter_init(&inst->filter, inst->control.sample_rate);
    inst->prev_cutoff = inst->cutoff;
}

static void init_defaults(sh101_instance_t *inst, float sr) {
    sh101_control_init(&inst->control, sr);
    sh101_osc_init(&inst->osc, sr, 0x1234abcd);
    sh101_env_init(&inst->amp_env, sr);
    sh101_env_init(&inst->filt_env, sr);
    sh101_filter_init(&inst->filter, sr);
    sh101_lfo_init(&inst->lfo, sr);

    inst->saw_level = 0.0f;
    inst->pulse_level = 0.0f;
    inst->sub_level = 0.0f;
    inst->noise_level = 0.0f;
    inst->pulse_width = 0.5f;
    inst->pwm_mode = 1;
    inst->pwm_depth = 0.0f;
    inst->pwm_env_depth = 0.0f;

    inst->cutoff = 0.5f;
    inst->resonance = 0.2f;
    inst->env_amount = 0.4f;
    inst->filter_volume_correction = 0.0f;
    inst->key_follow = 0.5f;

    inst->lfo_pitch = 0.0f;
    inst->lfo_filter = 0.0f;
    inst->lfo_pwm = 0.0f;
    inst->lfo_waveform = SH101_LFO_WAVE_TRI;
    inst->lfo_trigger = 0;
    inst->lfo_sync = 0;
    inst->lfo_invert = 0;
    inst->lfo_pitch_snap = 0;
    inst->lfo_sh_value = 0.0f;
    inst->lfo_gate_on = 0;
    inst->lfo_gate_off_count = 0;

    inst->output_level = 0.8f;
    inst->velocity_sens = 0.7f;
    inst->filter_velocity_sens = 0.25f;
    inst->velocity_gain = 1.0f;
    inst->filter_velocity_gain = 1.0f;
    inst->retrigger_on_legato = 0;
    inst->pitch_bend_semitones = 2.0f;
    inst->pitch_bend = 0.0f;
    inst->mod_wheel = 0.0f;
    inst->drift_rng = 0x31415926u;
    inst->drift_target_st = 0.0f;
    inst->drift_st = 0.0f;
    inst->fine_tune_cents = 0.0f;
    inst->current_preset = 0;
    inst->glide_ms_param = 0.0f;
    inst->gate_trig_mode = SH101_GATE_MODE_GATE;
    inst->velocity_mode = SH101_VELOCITY_MODE_OFF;
    inst->portamento_mode = SH101_PORTA_AUTO;
    inst->portamento_linear = 0;
    inst->vca_mode = SH101_VCA_MODE_ENV;
    inst->sub_mode = 0;
    inst->white_noise = 0;
    inst->filter_env_full_range = 0;
    inst->filter_env_polarity = 0;
    inst->same_note_quirk = 0;
    inst->adsr_declick = 0.65f;
    inst->self_osc_phase = 0.0f;
    inst->dc_block = 0.0f;
    inst->trigger_count = 0;
    inst->last_triggered_note = -1;
    inst->active_velocity = 1.0f;
    memset(inst->held_velocity, 0, sizeof(inst->held_velocity));

    apply_preset(inst, 0);
    sh101_filter_set_params(&inst->filter, 1600.0f, inst->resonance, 1.2f);
}

static void* v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;

    float sr = (g_host && g_host->sample_rate > 0) ? (float)g_host->sample_rate : 44100.0f;

    sh101_instance_t *inst = (sh101_instance_t*)calloc(1, sizeof(*inst));
    if (!inst) return NULL;
    snprintf(inst->module_dir, sizeof(inst->module_dir), "%s", (module_dir && module_dir[0]) ? module_dir : ".");
    init_defaults(inst, sr);
    scan_external_presets(inst);
    return inst;
}

static void v2_destroy_instance(void *instance) {
    free(instance);
}

static void handle_note_on(sh101_instance_t *inst, int note, int velocity) {
    if (note < 0 || note > 127) return;

    int was_gate = inst->control.gate;
    float vel = clampf((float)velocity / 127.0f, 0.0f, 1.0f);
    inst->held_velocity[note] = vel;

    sh101_control_note_on(&inst->control, note, velocity);
    if (inst->lfo_trigger) {
        inst->lfo.phase = 0.0f;
    }
    inst->lfo_gate_on = 0;
    inst->lfo_gate_off_count = 0;

    if (inst->velocity_mode == SH101_VELOCITY_MODE_ACTIVE_NOTE) {
        inst->active_velocity = pick_active_note_velocity(inst);
        apply_velocity_response(inst);
    }

    if (!should_note_on_trigger(inst, note, was_gate)) {
        return;
    }

    if (inst->velocity_mode == SH101_VELOCITY_MODE_TRIGGER) {
        inst->active_velocity = vel;
        apply_velocity_response(inst);
    } else if (inst->velocity_mode == SH101_VELOCITY_MODE_OFF) {
        apply_velocity_response(inst);
    }

    {
        int hard_reset = (inst->gate_trig_mode != SH101_GATE_MODE_GATE) || was_gate;
        trigger_envelopes(inst, hard_reset);
    }
    inst->last_triggered_note = note;
}

static void handle_note_off(sh101_instance_t *inst, int note) {
    if (note < 0 || note > 127) return;

    int was_held = inst->control.held[note];
    inst->held_velocity[note] = 0.0f;
    sh101_control_note_off(&inst->control, note);

    if (inst->velocity_mode == SH101_VELOCITY_MODE_ACTIVE_NOTE) {
        inst->active_velocity = pick_active_note_velocity(inst);
        apply_velocity_response(inst);
    }

    if (!inst->control.gate) {
        sh101_env_gate_off(&inst->amp_env);
        sh101_env_gate_off(&inst->filt_env);
    } else if (!was_held) {
        /* Received note-off for a note that was not held, yet the gate is
           still on.  This typically happens when the octave or transpose
           changes mid-note, causing the note-off MIDI number to differ from
           the note-on.  Release everything to prevent stuck notes. */
        sh101_control_all_notes_off(&inst->control);
        memset(inst->held_velocity, 0, sizeof(inst->held_velocity));
        inst->last_triggered_note = -1;
        inst->active_velocity = 1.0f;
        apply_velocity_response(inst);
        sh101_env_gate_off(&inst->amp_env);
        sh101_env_gate_off(&inst->filt_env);
    }
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    sh101_instance_t *inst = (sh101_instance_t*)instance;
    if (!inst || len < 1) return;

    uint8_t status = msg[0] & 0xF0;
    uint8_t d1 = (len > 1) ? msg[1] : 0;
    uint8_t d2 = (len > 2) ? msg[2] : 0;

    if (status == 0x90 && d2 > 0) {
        handle_note_on(inst, d1, d2);
        return;
    }
    if (status == 0x80 || (status == 0x90 && d2 == 0)) {
        handle_note_off(inst, d1);
        return;
    }
    if (status == 0xB0) {
        if (d1 == 1) {
            inst->mod_wheel = (float)d2 / 127.0f;
        } else if (d1 == 123) {
            sh101_control_all_notes_off(&inst->control);
            memset(inst->held_velocity, 0, sizeof(inst->held_velocity));
            inst->last_triggered_note = -1;
            inst->active_velocity = 1.0f;
            apply_velocity_response(inst);
            reset_voice(inst);
        }
        return;
    }
    if (status == 0xE0 && len > 2) {
        int bend = ((int)d2 << 7) | d1;
        inst->pitch_bend = ((float)bend - 8192.0f) / 8192.0f;
    }
}

/* ---------- minimal JSON number parser for state restore ---------- */
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

static const char *state_param_keys[] = {
    "saw", "pulse", "sub", "sub_mode", "noise", "white_noise",
    "pulse_width", "pwm_mode", "pwm_depth", "pwm_env_depth",
    "cutoff", "resonance", "env_amt", "filter_volume_correction",
    "filter_env_full_range", "filter_env_polarity", "key_follow",
    "lfo_rate", "lfo_waveform", "lfo_trigger", "lfo_sync", "lfo_invert",
    "lfo_pitch_snap", "lfo_pitch", "lfo_filter", "lfo_pwm",
    "velocity_sens", "filter_velocity_sens",
    "attack", "decay", "sustain", "release",
    "f_attack", "f_decay", "f_sustain", "f_release",
    "retrigger", "gate_trig_mode", "vca_mode", "velocity_mode",
    "portamento_mode", "portamento_linear", "same_note_quirk", "adsr_declick",
    "glide", "hold", "priority", "transpose", "octave_transpose", "fine_tune",
    "volume", "bend_range",
    NULL
};

/* Parse a value that may be a numeric index ("0", "1") or an option label
   ("Off", "On", "Auto") from the chain UI.  Returns the index. */
static int parse_enum(const char *val, const char *const *opts, int count) {
    char *endptr;
    float f = strtof(val, &endptr);
    if (endptr != val) return clamp_int((int)f, 0, count - 1);
    for (int i = 0; i < count; i++) {
        if (strcmp(val, opts[i]) == 0) return i;
    }
    return 0;
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    sh101_instance_t *inst = (sh101_instance_t*)instance;
    if (!inst || !key || !val) return;

    /* ---------- state restore: JSON blob with preset + param overrides ---------- */
    if (strcmp(key, "state") == 0) {
        float fv;
        char vbuf[32];
        int i;
        /* Apply preset first to set base state */
        if (json_get_number(val, "preset", &fv) == 0) {
            apply_preset(inst, (int)fv);
        }
        /* Override with individual params from state */
        for (i = 0; state_param_keys[i]; i++) {
            if (json_get_number(val, state_param_keys[i], &fv) == 0) {
                snprintf(vbuf, sizeof(vbuf), "%.6f", (double)fv);
                v2_set_param(instance, state_param_keys[i], vbuf);
            }
        }
        return;
    }

    float f = strtof(val, NULL);

    if (strcmp(key, "saw") == 0) inst->saw_level = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "pulse") == 0) inst->pulse_level = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "sub") == 0) inst->sub_level = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "sub_mode") == 0) { static const char *const o[] = {"-2 Oct 50% PW","-2 Oct","-1 Oct"}; inst->sub_mode = parse_enum(val, o, 3); }
    else if (strcmp(key, "noise") == 0) inst->noise_level = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "white_noise") == 0) { static const char *const o[] = {"Off","On"}; inst->white_noise = parse_enum(val, o, 2); }
    else if (strcmp(key, "pulse_width") == 0) inst->pulse_width = clampf(f, 0.05f, 0.95f);
    else if (strcmp(key, "pwm_mode") == 0) { static const char *const o[] = {"Env","Manual","LFO"}; inst->pwm_mode = parse_enum(val, o, 3); }
    else if (strcmp(key, "pwm_depth") == 0) inst->pwm_depth = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "pwm_env_depth") == 0) inst->pwm_env_depth = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "cutoff") == 0) inst->cutoff = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "resonance") == 0) inst->resonance = clampf(f, 0.0f, 1.2f);
    else if (strcmp(key, "env_amt") == 0) inst->env_amount = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "filter_volume_correction") == 0) inst->filter_volume_correction = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "filter_env_full_range") == 0) { static const char *const o[] = {"Off","On"}; inst->filter_env_full_range = parse_enum(val, o, 2); }
    else if (strcmp(key, "filter_env_polarity") == 0) { static const char *const o[] = {"Positive","Negative"}; inst->filter_env_polarity = parse_enum(val, o, 2); }
    else if (strcmp(key, "key_follow") == 0) inst->key_follow = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "lfo_rate") == 0) { sh101_lfo_set_rate_hz(&inst->lfo, clampf(f, 0.02f, 40.0f)); sync_lfo_rate_mode(inst); }
    else if (strcmp(key, "lfo_waveform") == 0) { static const char *const o[] = {"Tri","Rect","Random","Noise"}; inst->lfo_waveform = parse_enum(val, o, 4); }
    else if (strcmp(key, "lfo_trigger") == 0) { static const char *const o[] = {"Free","Retrig"}; inst->lfo_trigger = parse_enum(val, o, 2); }
    else if (strcmp(key, "lfo_sync") == 0) { static const char *const o[] = {"Free","Sync"}; inst->lfo_sync = parse_enum(val, o, 2); sync_lfo_rate_mode(inst); }
    else if (strcmp(key, "lfo_invert") == 0) { static const char *const o[] = {"Off","On"}; inst->lfo_invert = parse_enum(val, o, 2); }
    else if (strcmp(key, "lfo_pitch_snap") == 0) { static const char *const o[] = {"Off","On"}; inst->lfo_pitch_snap = parse_enum(val, o, 2); }
    else if (strcmp(key, "lfo_pitch") == 0) inst->lfo_pitch = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "lfo_filter") == 0) inst->lfo_filter = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "lfo_pwm") == 0) inst->lfo_pwm = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "velocity_sens") == 0) { inst->velocity_sens = clampf(f, 0.0f, 1.0f); apply_velocity_response(inst); }
    else if (strcmp(key, "filter_velocity_sens") == 0) { inst->filter_velocity_sens = clampf(f, 0.0f, 1.0f); apply_velocity_response(inst); }
    else if (strcmp(key, "retrigger") == 0) { static const char *const o[] = {"Legato","Trig"}; inst->retrigger_on_legato = parse_enum(val, o, 2); }
    else if (strcmp(key, "gate_trig_mode") == 0) { static const char *const o[] = {"Gate","Gate+Trig","LFO"}; inst->gate_trig_mode = parse_enum(val, o, 3); sync_priority_from_mode(inst); }
    else if (strcmp(key, "vca_mode") == 0) { static const char *const o[] = {"Gate","Envelope"}; inst->vca_mode = parse_enum(val, o, 2); }
    else if (strcmp(key, "velocity_mode") == 0) {
        static const char *const o[] = {"Off","Trigger","Active"};
        inst->velocity_mode = parse_enum(val, o, 3);
        if (inst->velocity_mode == SH101_VELOCITY_MODE_ACTIVE_NOTE) {
            inst->active_velocity = pick_active_note_velocity(inst);
        } else if (inst->velocity_mode == SH101_VELOCITY_MODE_OFF) {
            inst->active_velocity = 1.0f;
        }
        apply_velocity_response(inst);
    }
    else if (strcmp(key, "portamento_mode") == 0) { static const char *const o[] = {"Off","On","Auto"}; inst->portamento_mode = parse_enum(val, o, 3); sync_portamento_mode(inst); }
    else if (strcmp(key, "portamento_linear") == 0) { static const char *const o[] = {"Expo","Linear"}; inst->portamento_linear = parse_enum(val, o, 2); sync_portamento_mode(inst); }
    else if (strcmp(key, "same_note_quirk") == 0) {
        static const char *const o[] = {"Off","On"};
        inst->same_note_quirk = parse_enum(val, o, 2);
        if (!inst->same_note_quirk) inst->last_triggered_note = -1;
    }
    else if (strcmp(key, "adsr_declick") == 0) inst->adsr_declick = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "attack") == 0) { sh101_env_set_adsr(&inst->amp_env, clampf(f, 0.001f, 4.0f), inst->amp_env.decay_s, inst->amp_env.sustain, inst->amp_env.release_s); }
    else if (strcmp(key, "decay") == 0) { sh101_env_set_adsr(&inst->amp_env, inst->amp_env.attack_s, clampf(f, 0.001f, 6.0f), inst->amp_env.sustain, inst->amp_env.release_s); }
    else if (strcmp(key, "sustain") == 0) { sh101_env_set_adsr(&inst->amp_env, inst->amp_env.attack_s, inst->amp_env.decay_s, clampf(f, 0.0f, 1.0f), inst->amp_env.release_s); }
    else if (strcmp(key, "release") == 0) { sh101_env_set_adsr(&inst->amp_env, inst->amp_env.attack_s, inst->amp_env.decay_s, inst->amp_env.sustain, clampf(f, 0.001f, 8.0f)); }
    else if (strcmp(key, "f_attack") == 0) { sh101_env_set_adsr(&inst->filt_env, clampf(f, 0.001f, 4.0f), inst->filt_env.decay_s, inst->filt_env.sustain, inst->filt_env.release_s); }
    else if (strcmp(key, "f_decay") == 0) { sh101_env_set_adsr(&inst->filt_env, inst->filt_env.attack_s, clampf(f, 0.001f, 6.0f), inst->filt_env.sustain, inst->filt_env.release_s); }
    else if (strcmp(key, "f_sustain") == 0) { sh101_env_set_adsr(&inst->filt_env, inst->filt_env.attack_s, inst->filt_env.decay_s, clampf(f, 0.0f, 1.0f), inst->filt_env.release_s); }
    else if (strcmp(key, "f_release") == 0) { sh101_env_set_adsr(&inst->filt_env, inst->filt_env.attack_s, inst->filt_env.decay_s, inst->filt_env.sustain, clampf(f, 0.001f, 8.0f)); }
    else if (strcmp(key, "glide") == 0) { inst->glide_ms_param = clampf(f, 0.0f, 500.0f); sync_portamento_mode(inst); }
    else if (strcmp(key, "hold") == 0) { static const char *const o[] = {"Off","On"}; sh101_control_set_hold(&inst->control, parse_enum(val, o, 2)); }
    else if (strcmp(key, "priority") == 0) { static const char *const o[] = {"Last","Low"}; sh101_control_set_priority(&inst->control, parse_enum(val, o, 2) ? SH101_NOTE_PRIORITY_LOWEST : SH101_NOTE_PRIORITY_LAST); }
    else if (strcmp(key, "transpose") == 0) sh101_control_set_transpose(&inst->control, (int)f);
    else if (strcmp(key, "octave_transpose") == 0) sh101_control_set_transpose(&inst->control, (int)f * 12);
    else if (strcmp(key, "fine_tune") == 0) inst->fine_tune_cents = clampf(f, -100.0f, 100.0f);
    else if (strcmp(key, "volume") == 0) inst->output_level = clampf(f, 0.0f, 1.0f);
    else if (strcmp(key, "bend_range") == 0) inst->pitch_bend_semitones = clampf(f, 0.0f, 12.0f);
    else if (strcmp(key, "preset") == 0) apply_preset(inst, (int)f);
    else if (strcmp(key, "rescan_presets") == 0) {
        if (f >= 0.5f) {
            scan_external_presets(inst);
            if (inst->current_preset >= (SH101_PRESET_COUNT + inst->external_preset_count)) {
                inst->current_preset = 0;
            }
        }
    }
    else if (strcmp(key, "import_vstpreset_path") == 0) { (void)import_vstpreset_path(inst, val); }
    else if (strcmp(key, "reset_trigger_count") == 0) {
        if (f >= 0.5f) inst->trigger_count = 0;
    }
    else if (strcmp(key, "all_notes_off") == 0) {
        sh101_control_all_notes_off(&inst->control);
        memset(inst->held_velocity, 0, sizeof(inst->held_velocity));
        inst->last_triggered_note = -1;
        if (inst->velocity_mode == SH101_VELOCITY_MODE_OFF) inst->active_velocity = 1.0f;
        else inst->active_velocity = pick_active_note_velocity(inst);
        apply_velocity_response(inst);
        reset_voice(inst);
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    sh101_instance_t *inst = (sh101_instance_t*)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    /* ---------- state save: serialize all params to JSON ---------- */
    if (strcmp(key, "state") == 0) {
        int n = 0, sz = buf_len;
        #define SA(fmt, ...) do { if (n < sz) n += snprintf(buf + n, (size_t)(sz - n), fmt, __VA_ARGS__); } while (0)
        SA("{\"preset\":%d", inst->current_preset);
        SA(",\"saw\":%.6f", (double)inst->saw_level);
        SA(",\"pulse\":%.6f", (double)inst->pulse_level);
        SA(",\"sub\":%.6f", (double)inst->sub_level);
        SA(",\"sub_mode\":%d", inst->sub_mode);
        SA(",\"noise\":%.6f", (double)inst->noise_level);
        SA(",\"white_noise\":%d", inst->white_noise);
        SA(",\"pulse_width\":%.6f", (double)inst->pulse_width);
        SA(",\"pwm_mode\":%d", inst->pwm_mode);
        SA(",\"pwm_depth\":%.6f", (double)inst->pwm_depth);
        SA(",\"pwm_env_depth\":%.6f", (double)inst->pwm_env_depth);
        SA(",\"cutoff\":%.6f", (double)inst->cutoff);
        SA(",\"resonance\":%.6f", (double)inst->resonance);
        SA(",\"env_amt\":%.6f", (double)inst->env_amount);
        SA(",\"filter_volume_correction\":%.6f", (double)inst->filter_volume_correction);
        SA(",\"filter_env_full_range\":%d", inst->filter_env_full_range);
        SA(",\"filter_env_polarity\":%d", inst->filter_env_polarity);
        SA(",\"key_follow\":%.6f", (double)inst->key_follow);
        SA(",\"lfo_rate\":%.6f", (double)inst->lfo.rate_hz);
        SA(",\"lfo_waveform\":%d", inst->lfo_waveform);
        SA(",\"lfo_trigger\":%d", inst->lfo_trigger);
        SA(",\"lfo_sync\":%d", inst->lfo_sync);
        SA(",\"lfo_invert\":%d", inst->lfo_invert);
        SA(",\"lfo_pitch_snap\":%d", inst->lfo_pitch_snap);
        SA(",\"lfo_pitch\":%.6f", (double)inst->lfo_pitch);
        SA(",\"lfo_filter\":%.6f", (double)inst->lfo_filter);
        SA(",\"lfo_pwm\":%.6f", (double)inst->lfo_pwm);
        SA(",\"velocity_sens\":%.6f", (double)inst->velocity_sens);
        SA(",\"filter_velocity_sens\":%.6f", (double)inst->filter_velocity_sens);
        SA(",\"attack\":%.6f", (double)inst->amp_env.attack_s);
        SA(",\"decay\":%.6f", (double)inst->amp_env.decay_s);
        SA(",\"sustain\":%.6f", (double)inst->amp_env.sustain);
        SA(",\"release\":%.6f", (double)inst->amp_env.release_s);
        SA(",\"f_attack\":%.6f", (double)inst->filt_env.attack_s);
        SA(",\"f_decay\":%.6f", (double)inst->filt_env.decay_s);
        SA(",\"f_sustain\":%.6f", (double)inst->filt_env.sustain);
        SA(",\"f_release\":%.6f", (double)inst->filt_env.release_s);
        SA(",\"retrigger\":%d", inst->retrigger_on_legato);
        SA(",\"gate_trig_mode\":%d", inst->gate_trig_mode);
        SA(",\"vca_mode\":%d", inst->vca_mode);
        SA(",\"velocity_mode\":%d", inst->velocity_mode);
        SA(",\"portamento_mode\":%d", inst->portamento_mode);
        SA(",\"portamento_linear\":%d", inst->portamento_linear);
        SA(",\"same_note_quirk\":%d", inst->same_note_quirk);
        SA(",\"adsr_declick\":%.6f", (double)inst->adsr_declick);
        SA(",\"glide\":%.6f", (double)inst->glide_ms_param);
        SA(",\"hold\":%d", inst->control.hold_enabled);
        SA(",\"priority\":%d", (int)inst->control.priority);
        SA(",\"transpose\":%d", inst->control.transpose);
        SA(",\"octave_transpose\":%d", inst->control.transpose / 12);
        SA(",\"fine_tune\":%.6f", (double)inst->fine_tune_cents);
        SA(",\"volume\":%.6f", (double)inst->output_level);
        SA(",\"bend_range\":%.6f", (double)inst->pitch_bend_semitones);
        if (n < sz) buf[n++] = '}';
        if (n < sz) buf[n] = '\0'; else buf[sz - 1] = '\0';
        #undef SA
        return n;
    }

    #define RETF(v) do { return snprintf(buf, (size_t)buf_len, "%.6f", (double)(v)); } while (0)
    #define RETI(v) do { return snprintf(buf, (size_t)buf_len, "%d", (int)(v)); } while (0)
    #define RETE(idx, opts, cnt) do { return snprintf(buf, (size_t)buf_len, "%s", (opts)[clamp_int((int)(idx), 0, (cnt)-1)]); } while (0)

    if (strcmp(key, "saw") == 0) RETF(inst->saw_level);
    if (strcmp(key, "pulse") == 0) RETF(inst->pulse_level);
    if (strcmp(key, "sub") == 0) RETF(inst->sub_level);
    if (strcmp(key, "sub_mode") == 0) { static const char *const o[] = {"-2 Oct 50% PW","-2 Oct","-1 Oct"}; RETE(inst->sub_mode, o, 3); }
    if (strcmp(key, "noise") == 0) RETF(inst->noise_level);
    if (strcmp(key, "white_noise") == 0) { static const char *const o[] = {"Off","On"}; RETE(inst->white_noise, o, 2); }
    if (strcmp(key, "pulse_width") == 0) RETF(inst->pulse_width);
    if (strcmp(key, "pwm_mode") == 0) { static const char *const o[] = {"Env","Manual","LFO"}; RETE(inst->pwm_mode, o, 3); }
    if (strcmp(key, "pwm_depth") == 0) RETF(inst->pwm_depth);
    if (strcmp(key, "pwm_env_depth") == 0) RETF(inst->pwm_env_depth);
    if (strcmp(key, "cutoff") == 0) RETF(inst->cutoff);
    if (strcmp(key, "resonance") == 0) RETF(inst->resonance);
    if (strcmp(key, "env_amt") == 0) RETF(inst->env_amount);
    if (strcmp(key, "filter_volume_correction") == 0) RETF(inst->filter_volume_correction);
    if (strcmp(key, "filter_env_full_range") == 0) { static const char *const o[] = {"Off","On"}; RETE(inst->filter_env_full_range, o, 2); }
    if (strcmp(key, "filter_env_polarity") == 0) { static const char *const o[] = {"Positive","Negative"}; RETE(inst->filter_env_polarity, o, 2); }
    if (strcmp(key, "key_follow") == 0) RETF(inst->key_follow);
    if (strcmp(key, "lfo_rate") == 0) RETF(inst->lfo.rate_hz);
    if (strcmp(key, "lfo_waveform") == 0) { static const char *const o[] = {"Tri","Rect","Random","Noise"}; RETE(inst->lfo_waveform, o, 4); }
    if (strcmp(key, "lfo_trigger") == 0) { static const char *const o[] = {"Free","Retrig"}; RETE(inst->lfo_trigger, o, 2); }
    if (strcmp(key, "lfo_sync") == 0) { static const char *const o[] = {"Free","Sync"}; RETE(inst->lfo_sync, o, 2); }
    if (strcmp(key, "lfo_invert") == 0) { static const char *const o[] = {"Off","On"}; RETE(inst->lfo_invert, o, 2); }
    if (strcmp(key, "lfo_pitch_snap") == 0) { static const char *const o[] = {"Off","On"}; RETE(inst->lfo_pitch_snap, o, 2); }
    if (strcmp(key, "lfo_pitch") == 0) RETF(inst->lfo_pitch);
    if (strcmp(key, "lfo_filter") == 0) RETF(inst->lfo_filter);
    if (strcmp(key, "lfo_pwm") == 0) RETF(inst->lfo_pwm);
    if (strcmp(key, "velocity_sens") == 0) RETF(inst->velocity_sens);
    if (strcmp(key, "filter_velocity_sens") == 0) RETF(inst->filter_velocity_sens);
    if (strcmp(key, "attack") == 0) RETF(inst->amp_env.attack_s);
    if (strcmp(key, "decay") == 0) RETF(inst->amp_env.decay_s);
    if (strcmp(key, "sustain") == 0) RETF(inst->amp_env.sustain);
    if (strcmp(key, "release") == 0) RETF(inst->amp_env.release_s);
    if (strcmp(key, "f_attack") == 0) RETF(inst->filt_env.attack_s);
    if (strcmp(key, "f_decay") == 0) RETF(inst->filt_env.decay_s);
    if (strcmp(key, "f_sustain") == 0) RETF(inst->filt_env.sustain);
    if (strcmp(key, "f_release") == 0) RETF(inst->filt_env.release_s);
    if (strcmp(key, "retrigger") == 0) { static const char *const o[] = {"Legato","Trig"}; RETE(inst->retrigger_on_legato, o, 2); }
    if (strcmp(key, "gate_trig_mode") == 0) { static const char *const o[] = {"Gate","Gate+Trig","LFO"}; RETE(inst->gate_trig_mode, o, 3); }
    if (strcmp(key, "vca_mode") == 0) { static const char *const o[] = {"Gate","Envelope"}; RETE(inst->vca_mode, o, 2); }
    if (strcmp(key, "velocity_mode") == 0) { static const char *const o[] = {"Off","Trigger","Active"}; RETE(inst->velocity_mode, o, 3); }
    if (strcmp(key, "portamento_mode") == 0) { static const char *const o[] = {"Off","On","Auto"}; RETE(inst->portamento_mode, o, 3); }
    if (strcmp(key, "portamento_linear") == 0) { static const char *const o[] = {"Expo","Linear"}; RETE(inst->portamento_linear, o, 2); }
    if (strcmp(key, "same_note_quirk") == 0) { static const char *const o[] = {"Off","On"}; RETE(inst->same_note_quirk, o, 2); }
    if (strcmp(key, "adsr_declick") == 0) RETF(inst->adsr_declick);
    if (strcmp(key, "trigger_count") == 0) RETI(inst->trigger_count);
    if (strcmp(key, "active_velocity") == 0) RETF(inst->active_velocity);
    if (strcmp(key, "current_note") == 0) RETI(inst->control.current_note);
    if (strcmp(key, "glide") == 0) RETF(inst->glide_ms_param);
    if (strcmp(key, "hold") == 0) { static const char *const o[] = {"Off","On"}; RETE(inst->control.hold_enabled, o, 2); }
    if (strcmp(key, "priority") == 0) { static const char *const o[] = {"Last","Low"}; RETE((int)inst->control.priority, o, 2); }
    if (strcmp(key, "transpose") == 0) RETI(inst->control.transpose);
    if (strcmp(key, "octave_transpose") == 0) RETI(inst->control.transpose / 12);
    if (strcmp(key, "fine_tune") == 0) RETF(inst->fine_tune_cents);
    if (strcmp(key, "volume") == 0) RETF(inst->output_level);
    if (strcmp(key, "bend_range") == 0) RETF(inst->pitch_bend_semitones);
    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hierarchy = "{"
            "\"modes\":null,"
            "\"levels\":{"
                "\"root\":{"
                    "\"list_param\":\"preset\","
                    "\"count_param\":\"preset_count\","
                    "\"name_param\":\"preset_name\","
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"env_amt\",\"attack\",\"decay\",\"sustain\",\"release\",\"volume\"],"
                    "\"params\":["
                        "{\"level\":\"oscillator\",\"label\":\"Oscillator\"},"
                        "{\"level\":\"filter\",\"label\":\"Filter\"},"
                        "{\"level\":\"amp_env\",\"label\":\"Amp Envelope\"},"
                        "{\"level\":\"filt_env\",\"label\":\"Filter Envelope\"},"
                        "{\"level\":\"modulation\",\"label\":\"Modulation\"},"
                        "{\"level\":\"performance\",\"label\":\"Performance\"},"
                        "{\"level\":\"advanced\",\"label\":\"Advanced\"}"
                    "]"
                "},"
                "\"oscillator\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"saw\",\"pulse\",\"sub\",\"noise\"],"
                    "\"params\":[\"saw\",\"pulse\",\"sub\",\"noise\",\"sub_mode\",\"white_noise\",\"pulse_width\",\"pwm_mode\",\"pwm_depth\",\"pwm_env_depth\"]"
                "},"
                "\"filter\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"cutoff\",\"resonance\",\"env_amt\",\"key_follow\"],"
                    "\"params\":[\"cutoff\",\"resonance\",\"env_amt\",\"key_follow\",\"filter_velocity_sens\"]"
                "},"
                "\"amp_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"attack\",\"decay\",\"sustain\",\"release\"],"
                    "\"params\":[\"attack\",\"decay\",\"sustain\",\"release\",\"velocity_sens\"]"
                "},"
                "\"filt_env\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\"],"
                    "\"params\":[\"f_attack\",\"f_decay\",\"f_sustain\",\"f_release\"]"
                "},"
                "\"modulation\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"lfo_rate\",\"lfo_pitch\",\"lfo_filter\",\"lfo_pwm\"],"
                    "\"params\":[\"lfo_rate\",\"lfo_waveform\",\"lfo_trigger\",\"lfo_sync\",\"lfo_invert\",\"lfo_pitch_snap\",\"lfo_pitch\",\"lfo_filter\",\"lfo_pwm\"]"
                "},"
                "\"performance\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"glide\",\"portamento_mode\",\"transpose\",\"octave_transpose\"],"
                    "\"params\":[\"glide\",\"portamento_mode\",\"portamento_linear\",\"retrigger\",\"hold\",\"transpose\",\"octave_transpose\",\"fine_tune\"]"
                "},"
                "\"advanced\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"gate_trig_mode\",\"priority\",\"velocity_mode\",\"same_note_quirk\"],"
                    "\"params\":[\"gate_trig_mode\",\"vca_mode\",\"adsr_declick\",\"priority\",\"velocity_mode\",\"same_note_quirk\",\"filter_env_full_range\",\"filter_env_polarity\",\"filter_volume_correction\"]"
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

    if (strcmp(key, "import_name") == 0) return snprintf(buf, (size_t)buf_len, "%s", inst->import_name);
    if (strcmp(key, "preset") == 0) RETI(inst->current_preset);
    if (strcmp(key, "preset_count") == 0) RETI(SH101_PRESET_COUNT + inst->external_preset_count);
    if (strcmp(key, "preset_name") == 0) {
        int total = SH101_PRESET_COUNT + inst->external_preset_count;
        int p;
        if (total <= 0) return snprintf(buf, (size_t)buf_len, "No Presets");
        p = clamp_int(inst->current_preset, 0, total - 1);
        if (p < SH101_PRESET_COUNT) {
            return snprintf(buf, (size_t)buf_len, "%s", g_presets[p].name);
        }
        return snprintf(buf,
                        (size_t)buf_len,
                        "%s",
                        inst->external_presets[p - SH101_PRESET_COUNT].name);
    }

    return -1;
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    sh101_instance_t *inst = (sh101_instance_t*)instance;
    if (!inst || !buf || buf_len <= 0) return 0;
    if (inst->last_error[0] == '\0') return 0;
    return snprintf(buf, (size_t)buf_len, "%s", inst->last_error);
}

static void v2_render_block(void *instance, int16_t *out_lr, int frames) {
    sh101_instance_t *inst = (sh101_instance_t*)instance;
    if (!inst || !out_lr || frames <= 0) return;

    for (int i = 0; i < frames; ++i) {
        sh101_control_tick_pitch(&inst->control);

        float phase_before = inst->lfo.phase;
        float lfo = sh101_lfo_process(&inst->lfo);
        int lfo_cycle_wrap = (inst->lfo.phase < phase_before) ? 1 : 0;
        if (inst->lfo_waveform == SH101_LFO_WAVE_RECT) {
            lfo = (inst->lfo.phase < 0.5f) ? 1.0f : -1.0f;
        } else if (inst->lfo_waveform == SH101_LFO_WAVE_RANDOM) {
            if (lfo_cycle_wrap) {
                inst->lfo_sh_value = rand_unit(&inst->drift_rng) * 2.0f - 1.0f;
            }
            lfo = inst->lfo_sh_value;
        } else if (inst->lfo_waveform == SH101_LFO_WAVE_NOISE) {
            lfo = rand_unit(&inst->drift_rng) * 2.0f - 1.0f;
        }
        if (inst->lfo_invert) {
            lfo = -lfo;
        }

        if (inst->gate_trig_mode == SH101_GATE_MODE_LFO && inst->control.gate) {
            int lfo_positive = (lfo > 0.0f) ? 1 : 0;
            if (inst->lfo_gate_off_count >= 3) {
                /* After 3 gate-off transitions the voice has decayed to
                   inaudible levels (matching TAL's internal voice management
                   which kills near-silent voices).  Keep envelopes released
                   and VCA closed until the next note-on. */
                if (inst->lfo_gate_on) {
                    sh101_env_gate_off(&inst->amp_env);
                    sh101_env_gate_off(&inst->filt_env);
                    inst->lfo_gate_on = 0;
                }
            } else if (lfo_positive && !inst->lfo_gate_on) {
                /* LFO positive edge: retrigger envelopes */
                if (inst->velocity_mode == SH101_VELOCITY_MODE_ACTIVE_NOTE) {
                    inst->active_velocity = pick_active_note_velocity(inst);
                    apply_velocity_response(inst);
                }
                trigger_envelopes(inst, 1);
                inst->lfo_gate_on = 1;
            } else if (!lfo_positive && inst->lfo_gate_on) {
                /* LFO negative edge: release envelopes */
                sh101_env_gate_off(&inst->amp_env);
                sh101_env_gate_off(&inst->filt_env);
                inst->lfo_gate_on = 0;
                inst->lfo_gate_off_count++;
            }
        }

        /* Slow random walk drift keeps the pitch center alive without obvious detune. */
        inst->drift_target_st += (rand_unit(&inst->drift_rng) - 0.5f) * 0.00008f;
        inst->drift_target_st = clampf(inst->drift_target_st, -0.10f, 0.10f);
        inst->drift_st += (inst->drift_target_st - inst->drift_st) * 0.0012f;

        float env_amp = sh101_env_process(&inst->amp_env);
        float vca_amp = env_amp;
        float env_filt = sh101_env_process(&inst->filt_env);
        if (inst->vca_mode == SH101_VCA_MODE_GATE) {
            vca_amp = inst->control.gate ? 1.0f : 0.0f;
            /* In LFO gate mode, the LFO controls the VCA gate — the VCA opens
               during the positive half of the LFO cycle and closes otherwise.
               After the voice has cycled through 3 gate-offs, the VCA stays
               closed (the voice is effectively silent by that point). */
            if (inst->gate_trig_mode == SH101_GATE_MODE_LFO && inst->control.gate) {
                vca_amp = (inst->lfo_gate_off_count >= 3) ? 0.0f
                        : (lfo > 0.0f) ? 1.0f : 0.0f;
            }
        }

        float mw_shape = depth_curve(inst->mod_wheel);
        float pitch_depth = depth_curve(inst->lfo_pitch) * (1.0f + 1.4f * mw_shape);
        float filter_depth = depth_curve(inst->lfo_filter) * (1.0f + 1.2f * mw_shape);
        float pwm_depth = clampf(inst->pwm_depth + inst->lfo_pwm, 0.0f, 1.0f);
        float pwm_mod_depth = depth_curve(pwm_depth) * (1.0f + 0.6f * mw_shape);

        float pitch_mod_st = lfo * pitch_depth * 0.85f;
        if (inst->lfo_pitch_snap) {
            pitch_mod_st = roundf(pitch_mod_st * 12.0f) / 12.0f;
        }
        float bend_st = inst->pitch_bend * inst->pitch_bend_semitones;
        float drift_st = inst->drift_st;
        float fine_st = inst->fine_tune_cents / 100.0f;
        float freq = inst->control.pitch_current_hz * powf(2.0f, (pitch_mod_st + bend_st + drift_st + fine_st) / 12.0f);

        float pwm_lfo = (inst->pwm_mode == 2) ? (lfo * pwm_mod_depth * 0.42f) : 0.0f;
        float pwm_env = (inst->pwm_mode == 0) ? ((env_amp * 2.0f - 1.0f) * inst->pwm_env_depth * 0.45f) : 0.0f;
        float pwm = clampf(inst->pulse_width + pwm_lfo + pwm_env, 0.05f, 0.95f);

        float osc = sh101_osc_render(&inst->osc,
                                     freq,
                                     pwm,
                                     inst->saw_level,
                                     inst->pulse_level,
                                     inst->sub_level,
                                     inst->noise_level,
                                     inst->sub_mode,
                                     inst->white_noise
                                         ? (inst->cutoff < 0.85f ? 0.85f : 1.0f)
                                         : 0.0f);

        int note_for_filter = (inst->control.current_note < 0) ? 60 : inst->control.current_note;
        float env_delta = inst->env_amount * env_filt;
        if (inst->filter_env_full_range && !inst->filter_env_polarity)
            env_delta *= 2.0f;
        if (inst->filter_env_polarity) env_delta = -env_delta;
        float cutoff_raw = inst->cutoff
                         + env_delta
                         + (inst->filter_velocity_gain - 1.0f) * 0.6f
                         + lfo * filter_depth * 0.50f;
        float cutoff = clampf(cutoff_raw, 0.0f, 1.0f);

        /* Per-sample cutoff noise models analog component drift — reduces
           autocorrelation to match TAL's built-in analog modeling (~0.4-0.5 vs our 0.95+).
           When the oscillator already carries noise, the cutoff jitter is reduced to
           avoid double-randomizing the signal (which would push autocorr too low). */
        float noise_atten = 1.0f - inst->noise_level * 0.75f;
        float cutoff_noise = (rand_unit(&inst->drift_rng) - 0.5f) * 0.025f * noise_atten;
        float cutoff_hz = note_to_cutoff_hz(note_for_filter, cutoff + cutoff_noise, inst->key_follow);
        sh101_filter_set_params(&inst->filter, cutoff_hz, inst->resonance, 1.3f);

        float filtered = sh101_filter_process(&inst->filter, osc);
        /* Filter transparency: our Euler-integration 4-pole filter caps g at
           0.35, making it opaque above ~2.5 kHz even at max cutoff.  The real
           CEM3320 is essentially transparent at max cutoff.  Blend in the raw
           oscillator signal at high cutoff to restore high-frequency content
           (especially broadband noise that the filter otherwise removes). */
        if (cutoff > 0.85f) {
            /* Base bypass ramps up to 50% at max cutoff.  When noise is a
               significant part of the oscillator mix, the bypass increases
               further (up to 85%) because the filter's g cap removes more
               high-frequency noise than the real CEM3320 would. */
            float osc_total = inst->saw_level + inst->pulse_level
                            + inst->sub_level + inst->noise_level;
            float noise_share = (osc_total > 0.01f)
                              ? (inst->noise_level / osc_total) : 0.0f;
            float cutoff_ramp = clampf((cutoff - 0.85f) * 6.67f, 0.0f, 1.0f);
            float max_bypass = 0.85f + noise_share * 0.12f;
            float bypass = clampf(0.50f * cutoff_ramp
                                + noise_share * 0.80f * cutoff_ramp,
                                  0.0f, max_bypass);
            filtered = filtered * (1.0f - bypass) + osc * bypass;
        }
        /* Synthetic self-oscillation for presets with no oscillator signal and
           high resonance.  Models the CEM3320 ladder filter's natural tendency
           to self-oscillate at the resonant frequency.  The build-up speed is
           cutoff-dependent: low cutoff = slow energy circulation in the filter
           loop = slow build-up, matching TAL's behavior (50-200ms). */
        {
            float self_osc_sig = 0.0f;
            float self_amp_target = 0.0f;
            if (fmaxf(env_amp, env_filt) > 0.01f &&
                (inst->saw_level + inst->pulse_level + inst->sub_level + inst->noise_level) < 0.0005f &&
                inst->resonance > 1.02f) {
                float res_drive = clampf((inst->resonance - 1.0f) / 0.20f, 0.0f, 1.0f);
                /* Self-osc amplitude rises with cutoff — matches analog filter
                   where higher cutoff = more energy in the feedback loop. */
                float cutoff_amp = clampf(cutoff * 0.80f, 0.0f, 0.60f);
                /* Suppress for extreme cutoff overshoot (beyond Nyquist). */
                if (cutoff_raw > 1.4f) {
                    float lfo_swing = filter_depth * 0.50f;
                    float rolloff = clampf((1.8f - cutoff_raw) / 0.4f, 0.0f, 1.0f);
                    float lfo_sustain = clampf(lfo_swing * 5.0f, 0.0f, 1.0f);
                    cutoff_amp *= fmaxf(rolloff, lfo_sustain);
                }
                /* Self-oscillation character: analog filter resonance produces
                   a clean sinusoid.  Noise is added only by specific modulators:
                   - NOISE LFO wf (per-sample random cutoff → chaotic)
                   - Fullrange bipolar envelope (extreme cutoff swings)
                   - Large envelope sweeps (rapid FM → mildly chaotic) */
                float chaos = 0.04f;
                if (inst->lfo_waveform == SH101_LFO_WAVE_NOISE)
                    chaos += inst->lfo_filter * 0.50f;
                if (inst->filter_env_full_range)
                    chaos += 0.35f;
                chaos += inst->env_amount * inst->env_amount * 0.42f;
                chaos = clampf(chaos, 0.0f, 0.80f);
                float note_hz = sh101_midi_note_to_hz(note_for_filter);
                float self_freq = note_hz * powf(2.0f, (cutoff - 0.45f) * 2.6f);
                self_freq = clampf(self_freq, 20.0f, 8000.0f);
                inst->self_osc_phase += clampf(self_freq / inst->control.sample_rate, 0.0f, 0.45f);
                if (inst->self_osc_phase >= 1.0f) inst->self_osc_phase -= floorf(inst->self_osc_phase);
                float tone = sinf(inst->self_osc_phase * 6.28318530718f);
                float noise = rand_unit(&inst->drift_rng) * 2.0f - 1.0f;
                self_osc_sig = tone * (1.0f - chaos) + noise * chaos;
                /* Envelope sweep suppression: when a non-fullrange envelope
                   sweeps the cutoff WITHOUT chaotic modulation (no LFO filter
                   mod, low chaos), the smooth cutoff change disrupts the filter
                   feedback loop — it can't build coherent self-oscillation.
                   Presets with NOISE LFO, high LFO filter mod, or fullrange env
                   create chaotic oscillation that persists through sweeps. */
                float env_suppress = 1.0f;
                if (chaos < 0.25f && !inst->filter_env_full_range
                    && inst->lfo_filter < 0.1f) {
                    env_suppress = clampf(1.0f - inst->env_amount * inst->env_amount * 3.0f,
                                          0.0f, 1.0f);
                }
                self_amp_target = cutoff_amp * res_drive * env_suppress;
            }
            /* Cutoff-dependent ramp speed: low base cutoff = slow energy
               circulation in the filter loop = slow build-up. */
            float ramp_speed = 0.0003f + inst->cutoff * 0.003f;
            inst->self_osc_level += (self_amp_target - inst->self_osc_level) * ramp_speed;
            filtered += self_osc_sig * inst->self_osc_level;
        }
        /* CEM3320/IR3109 filters have a wider passband at high Q than a pure Moog
           ladder — broadband noise leaks around the resonant peak.  Simulate this by
           mixing a small amount of unfiltered noise past the filter, scaled by both
           noise level and resonance above 0.8. */
        if (inst->noise_level > 0.001f && inst->resonance > 0.8f) {
            float leak_scale = inst->noise_level * 0.10f
                             * clampf((inst->resonance - 0.8f) * 2.5f, 0.0f, 1.0f);
            float leak_noise = (rand_unit(&inst->drift_rng) * 2.0f - 1.0f) * leak_scale;
            filtered += leak_noise;
        }
        filtered *= 1.0f + inst->filter_volume_correction * inst->resonance * 0.45f;
        /* Euler-integration loss compensation: each filter stage loses
           energy per sample proportional to g, making the resonant peak
           weaker than the analog CEM3320/IR3109 at high Q.  Apply a
           makeup gain that increases with both resonance (more loss at
           higher Q) and cutoff (higher g = more loss per stage).  The
           cutoff scaling keeps low-cutoff presets (fully closed filter)
           from getting over-boosted. */
        if (inst->resonance > 0.9f) {
            float res_factor = clampf((inst->resonance - 0.9f) / 0.3f, 0.0f, 1.0f);
            float res_boost = 1.0f + res_factor * (0.5f + inst->cutoff * 2.0f);
            filtered *= res_boost;
        }
        /* DC-blocking highpass (~0.35 Hz) models the coupling capacitor between
           the VCF output and the VCA input.  Removes pulse-wave DC offset that
           would otherwise pass through the lowpass filter and inflate the signal
           level at extreme PWM duty cycles.  The very low cutoff (~450ms time
           constant) ensures transient/percussive sounds are unaffected. */
        {
            inst->dc_block += (filtered - inst->dc_block) * 0.00005f;
            filtered -= inst->dc_block;
        }
        float amp = filtered * vca_amp * inst->velocity_gain * inst->output_level;
        amp = clampf(amp, -1.0f, 1.0f);

        int16_t s = (int16_t)(amp * 32767.0f);
        out_lr[i * 2] = s;
        out_lr[i * 2 + 1] = s;
    }
}

static plugin_api_v2_t g_api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = v2_create_instance,
    .destroy_instance = v2_destroy_instance,
    .on_midi = v2_on_midi,
    .set_param = v2_set_param,
    .get_param = v2_get_param,
    .get_error = v2_get_error,
    .render_block = v2_render_block
};

plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    sh101_log("init v2");
    return &g_api;
}
