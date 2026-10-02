/*
 * Super Arp MIDI FX (MVP+)
 * Implements: rate/triplet/gate, rhythm preset+phase, progression (up/down/as_played),
 * linear octave-range cycling, reset-on-empty, retrigger.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "host/midi_fx_api_v1.h"
#include "host/plugin_api_v1.h"

#define MAX_ARP_NOTES 16
#define DEFAULT_BPM 120
#define SUPERARP_DEBUG_LOG 0
#define SUPERARP_LOG_PATH "/data/UserData/schwung/superarp.log"
#define CLOCK_START_GRACE_TICKS 1
#define CLOCK_ARP_OUTPUT_DELAY_TICKS 1
#define MAX_PATTERN_STEPS 16
#define MAX_PATTERN_NOTES 4
#define PROG_INDEX_MAX 5
#define MAX_PATTERN_TEXT 96
#define MAX_VOICES 64

typedef enum { RATE_1_32 = 0, RATE_1_16, RATE_1_8, RATE_1_4 } rate_t;
typedef enum {
    PROG_UP = 0, PROG_DOWN, PROG_AS_PLAYED, PROG_LEAP_INWARD, PROG_LEAP_OUTWARD,
    PROG_CHORD, PROG_PATTERN, PROG_RANDOM_PATTERN
} progression_mode_t;
typedef enum { MISSING_FOLD = 0, MISSING_WRAP, MISSING_CLAMP, MISSING_SKIP } missing_note_policy_t;
typedef enum { SYNC_INTERNAL = 0, SYNC_CLOCK } sync_mode_t;
typedef enum { TRIG_RETRIGGER = 0, TRIG_CONTINUOUS } trigger_mode_t;
typedef enum {
    OCT_RAND_P1 = 0,
    OCT_RAND_M1,
    OCT_RAND_PM1,
    OCT_RAND_P2,
    OCT_RAND_M2,
    OCT_RAND_PM2
} random_octave_range_t;
typedef enum {
    OCT_RANGE_0 = 0,
    OCT_RANGE_P1,
    OCT_RANGE_M1,
    OCT_RANGE_P2,
    OCT_RANGE_M2
} octave_range_mode_t;

typedef struct {
    int step_count;
    int step_sizes[MAX_PATTERN_STEPS];
    int steps[MAX_PATTERN_STEPS][MAX_PATTERN_NOTES];
} progression_pattern_t;

typedef struct {
    /* Full v1 parameter surface (some are retained/no-op in MVP). */
    rate_t rate;
    int bpm, triplet, gate, velocity_override, swing, phase_offset, latch, max_voices;
    octave_range_mode_t octave_range;
    sync_mode_t sync_mode;
    progression_mode_t progression_mode;
    int progression_seed;
    missing_note_policy_t missing_note_policy;
    int random_pattern_length;
    int random_pattern_chords;
    int random_pattern_chord_seed;
    int rhythm_seed;
    trigger_mode_t progression_trigger_mode;
    trigger_mode_t rhythm_trigger_mode;
    trigger_mode_t modifier_trigger_mode;
    int modifier_loop_length;
    int drop_amount, drop_seed;
    int velocity_random_amount, velocity_seed;
    int gate_random_amount, gate_seed;
    int random_octave_amount;
    random_octave_range_t random_octave_range;
    int random_octave_seed;
    int random_note_amount;
    int random_note_seed;
    char progression_pattern_value[MAX_PATTERN_TEXT];
    progression_pattern_t progression_pattern;
    char rhythm_pattern_value[MAX_PATTERN_TEXT];
    char chain_params_json[65536];
    int chain_params_len;

    uint8_t physical_notes[MAX_ARP_NOTES];
    int physical_count;
    uint8_t physical_as_played[MAX_ARP_NOTES];
    int physical_as_played_count;
    int note_set_dirty;
    uint8_t active_notes[MAX_ARP_NOTES];
    int active_count;
    uint8_t as_played[MAX_ARP_NOTES];
    int as_played_count;
    int latch_ready_replace;

    int progression_cursor, rhythm_cursor;
    uint64_t global_step_index;
    uint64_t modifier_step_counter;
    uint64_t progression_emit_index;
    int phrase_running;

    int sample_rate, timing_dirty, step_interval_base, samples_until_step, swing_phase;
    double step_interval_base_f, samples_until_step_f;
    uint64_t internal_sample_total;
    int clock_counter, clocks_per_step, clock_running;
    uint64_t clock_tick_total;
    int pending_step_triggers;
    int delayed_step_triggers;
    uint8_t voice_notes[MAX_VOICES];
    int voice_clock_left[MAX_VOICES];
    int voice_sample_left[MAX_VOICES];
    int voice_count;
    int internal_start_grace_armed;
    uint8_t last_input_velocity;
    FILE *debug_fp;
    uint64_t debug_seq;
} superarp_instance_t;

static const host_api_v1_t *g_host = NULL;

static int superarp_query_clock_status(const superarp_instance_t *inst) {
    if (g_host && g_host->get_clock_status) {
        return g_host->get_clock_status();
    }

    if (inst && inst->clock_running) {
        return MOVE_CLOCK_STATUS_RUNNING;
    }
    return MOVE_CLOCK_STATUS_STOPPED;
}

static int superarp_get_sync_warning(superarp_instance_t *inst, char *buf, int buf_len) {
    int status;

    if (!inst || !buf || buf_len < 1) return -1;
    if (inst->sync_mode != SYNC_CLOCK) {
        buf[0] = '\0';
        return 0;
    }

    status = superarp_query_clock_status(inst);
    if (status == MOVE_CLOCK_STATUS_UNAVAILABLE) {
        return snprintf(buf, buf_len, "Enable MIDI Clock Out in Move settings");
    }
    if (status == MOVE_CLOCK_STATUS_STOPPED) {
        return snprintf(buf, buf_len, "Clock out enabled, transport stopped");
    }

    buf[0] = '\0';
    return 0;
}

static const char *k_default_rhythm_pattern = "00";
static const char *k_default_progression_pattern = "1-2-3";

static void cache_chain_params_from_module_json(superarp_instance_t *inst, const char *module_dir) {
    char path[512];
    FILE *f;
    char *json = NULL;
    long size;
    const char *chain_params, *arr_start, *arr_end;
    int depth = 1;
    if (!inst || !module_dir || !module_dir[0]) return;
    snprintf(path, sizeof(path), "%s/module.json", module_dir);
    f = fopen(path, "rb");
    if (!f) return;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
    size = ftell(f);
    if (size <= 0 || size > 300000) { fclose(f); return; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return; }
    json = (char *)malloc((size_t)size + 1);
    if (!json) { fclose(f); return; }
    if (fread(json, 1, (size_t)size, f) != (size_t)size) { free(json); fclose(f); return; }
    json[size] = '\0';
    fclose(f);

    chain_params = strstr(json, "\"chain_params\"");
    if (!chain_params) { free(json); return; }
    arr_start = strchr(chain_params, '[');
    if (!arr_start) { free(json); return; }
    arr_end = arr_start + 1;
    while (*arr_end && depth > 0) {
        if (*arr_end == '[') depth++;
        else if (*arr_end == ']') depth--;
        arr_end++;
    }
    if (depth == 0) {
        int len = (int)(arr_end - arr_start);
        if (len > 0 && len < (int)sizeof(inst->chain_params_json)) {
            memcpy(inst->chain_params_json, arr_start, (size_t)len);
            inst->chain_params_json[len] = '\0';
            inst->chain_params_len = len;
        }
    }
    free(json);
}

#if SUPERARP_DEBUG_LOG
static void dlog(superarp_instance_t *inst, const char *fmt, ...) {
    va_list ap;
    if (!inst) return;
    if (!inst->debug_fp) {
        inst->debug_fp = fopen(SUPERARP_LOG_PATH, "a");
        if (!inst->debug_fp) return;
        setvbuf(inst->debug_fp, NULL, _IOLBF, 0);
    }
    fprintf(inst->debug_fp, "[%llu] ", (unsigned long long)inst->debug_seq++);
    va_start(ap, fmt);
    vfprintf(inst->debug_fp, fmt, ap);
    va_end(ap);
    fputc('\n', inst->debug_fp);
}
#else
static void dlog(superarp_instance_t *inst, const char *fmt, ...) {
    (void)inst; (void)fmt;
}
#endif

static int clamp_int(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }

static uint32_t mix_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint32_t held_set_hash(const superarp_instance_t *inst) {
    uint32_t h = 2166136261u;
    int i;
    if (!inst) return h;
    for (i = 0; i < inst->active_count; i++) {
        h ^= (uint32_t)inst->active_notes[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t step_rand_u32(uint32_t seed, uint64_t step, uint32_t salt) {
    uint32_t lo = (uint32_t)(step & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)((step >> 32) & 0xFFFFFFFFu);
    return mix_u32(seed ^ lo ^ mix_u32(hi ^ salt) ^ salt);
}

static int rand_offset_signed(uint32_t r, int amount) {
    int span;
    if (amount <= 0) return 0;
    span = amount * 2 + 1;
    return (int)(r % (uint32_t)span) - amount;
}

static uint64_t modifier_step_index(const superarp_instance_t *inst) {
    int loop_len;
    if (!inst) return 0;
    loop_len = clamp_int(inst->modifier_loop_length, 0, 128);
    if (loop_len <= 0) return inst->modifier_step_counter;
    return inst->modifier_step_counter % (uint64_t)loop_len;
}

static uint32_t modifier_rng_hash(superarp_instance_t *inst, uint32_t held_hash) {
    int loop_len;
    if (!inst) return held_hash;
    loop_len = clamp_int(inst->modifier_loop_length, 0, 128);
    if (loop_len > 0) return 0u; /* Loop mode: modulation is step-locked, not note-set dependent. */
    return held_hash;
}

static int should_drop_step(superarp_instance_t *inst, uint32_t mod_hash, uint64_t mod_step) {
    uint32_t r;
    int amt;
    if (!inst) return 0;
    amt = clamp_int(inst->drop_amount, 0, 100);
    if (amt <= 0) return 0;
    if (amt >= 100) return 1;
    r = step_rand_u32((uint32_t)inst->drop_seed, mod_step, mod_hash ^ 0xD0A4u);
    return (int)(r % 100u) < amt;
}

static int step_velocity(superarp_instance_t *inst, uint64_t mod_step) {
    int base, amt, delta;
    uint32_t r;
    if (!inst) return 100;
    if (inst->velocity_override > 0) base = clamp_int(inst->velocity_override, 1, 127);
    else base = clamp_int((int)inst->last_input_velocity, 1, 127);
    amt = clamp_int(inst->velocity_random_amount, 0, 127);
    if (amt <= 0) return base;
    r = step_rand_u32((uint32_t)inst->velocity_seed, mod_step, 0xA11CEu);
    delta = rand_offset_signed(r, amt);
    return clamp_int(base + delta, 1, 127);
}

static int step_gate_pct(superarp_instance_t *inst, uint64_t mod_step) {
    int base, amt, delta;
    uint32_t r;
    if (!inst) return 80;
    base = clamp_int(inst->gate, 0, 1600);
    amt = clamp_int(inst->gate_random_amount, 0, 1600);
    if (amt <= 0) return base;
    r = step_rand_u32((uint32_t)inst->gate_seed, mod_step, 0x6A73u);
    delta = rand_offset_signed(r, amt);
    return clamp_int(base + delta, 0, 1600);
}

static int random_octave_offset(superarp_instance_t *inst, uint32_t mod_hash, uint64_t mod_step) {
    uint32_t r;
    int amt, mode;
    if (!inst) return 0;
    amt = clamp_int(inst->random_octave_amount, 0, 100);
    if (amt <= 0) return 0;
    r = step_rand_u32((uint32_t)inst->random_octave_seed ^ 0x0C7A9Eu, mod_step, mod_hash ^ 0x7F1Du);
    if ((int)(r % 100u) >= amt) return 0;
    mode = (int)inst->random_octave_range;
    switch (mode) {
        case OCT_RAND_P1: return 12;
        case OCT_RAND_M1: return -12;
        case OCT_RAND_PM1: return ((r >> 8) & 1u) ? 12 : -12;
        case OCT_RAND_P2: return ((r >> 10) & 1u) ? 12 : 24;   /* up to +2 oct */
        case OCT_RAND_M2: return ((r >> 10) & 1u) ? -12 : -24; /* up to -2 oct */
        case OCT_RAND_PM2: {
            int pick = (int)((r >> 10) % 4u);
            if (pick == 0) return -24;
            if (pick == 1) return -12;
            if (pick == 2) return 12;
            return 24;
        }
        default: return 0;
    }
}

static int octave_range_steps(const superarp_instance_t *inst) {
    if (!inst) return 0;
    switch (inst->octave_range) {
        case OCT_RANGE_P1: return 1;
        case OCT_RANGE_M1: return -1;
        case OCT_RANGE_P2: return 2;
        case OCT_RANGE_M2: return -2;
        case OCT_RANGE_0:
        default: return 0;
    }
}

static void apply_note_randomization(superarp_instance_t *inst, int *notes, int note_count,
                                     uint32_t mod_hash, uint64_t mod_step) {
    int i;
    int amt;
    if (!inst || !notes || note_count <= 0) return;
    if (inst->active_count < 2) return;
    amt = clamp_int(inst->random_note_amount, 0, 100);
    if (amt <= 0) return;

    /* Max intensity: deterministic non-zero circular remap (no fixed points). */
    if (amt >= 100) {
        int pool = inst->active_count;
        uint32_t r0 = step_rand_u32((uint32_t)inst->random_note_seed, mod_step, mod_hash ^ 0x7B1Du);
        int shift = 1 + (int)(r0 % (uint32_t)(pool - 1));
        for (i = 0; i < note_count; i++) {
            int j;
            int idx = -1;
            int cur = notes[i];
            for (j = 0; j < pool; j++) {
                if ((int)inst->active_notes[j] == cur) { idx = j; break; }
            }
            if (idx >= 0) {
                notes[i] = (int)inst->active_notes[(idx + shift) % pool];
            } else {
                /* Fallback: choose a deterministic alternate note. */
                int pick = (int)((r0 + (uint32_t)i * 131u) % (uint32_t)pool);
                if ((int)inst->active_notes[pick] == cur) pick = (pick + 1) % pool;
                notes[i] = (int)inst->active_notes[pick];
            }
        }
        return;
    }

    for (i = 0; i < note_count; i++) {
        uint32_t r = step_rand_u32((uint32_t)inst->random_note_seed, mod_step, mod_hash ^ (uint32_t)(0x91E3u + i * 131u));
        int cur = notes[i];
        if ((int)(r % 100u) >= amt) continue;
        /* Pick another held note if possible; otherwise keep current. */
        {
            int try_idx;
            int j;
            int pool = inst->active_count;
            int replaced = 0;
            for (j = 0; j < pool; j++) {
                try_idx = (int)((r >> 8) + (uint32_t)j) % pool;
                if ((int)inst->active_notes[try_idx] != cur) {
                    notes[i] = (int)inst->active_notes[try_idx];
                    replaced = 1;
                    break;
                }
            }
            if (!replaced) notes[i] = cur;
        }
    }
}

static int live_note_count(const superarp_instance_t *inst) {
    if (!inst) return 0;
    return inst->latch ? inst->active_count : inst->physical_count;
}

static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    char search[64];
    const char *pos, *colon, *end;
    int len;
    if (!json || !key || !out || out_len < 1) return 0;
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strstr(json, search);
    if (!pos) return 0;
    colon = strchr(pos, ':');
    if (!colon) return 0;
    while (*colon && (*colon == ':' || *colon == ' ' || *colon == '\t')) colon++;
    if (*colon != '"') return 0;
    colon++;
    end = strchr(colon, '"');
    if (!end) return 0;
    len = (int)(end - colon);
    if (len >= out_len) len = out_len - 1;
    strncpy(out, colon, len);
    out[len] = '\0';
    return len;
}

static int json_get_int(const char *json, const char *key, int *out) {
    char search[64];
    const char *pos, *colon;
    if (!json || !key || !out) return 0;
    snprintf(search, sizeof(search), "\"%s\"", key);
    pos = strstr(json, search);
    if (!pos) return 0;
    colon = strchr(pos, ':');
    if (!colon) return 0;
    colon++;
    while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
    *out = atoi(colon);
    return 1;
}

static int emit3(uint8_t out_msgs[][3], int out_lens[], int max_out, int *count, uint8_t s, uint8_t d1, uint8_t d2) {
    if (!out_msgs || !out_lens || !count || *count >= max_out) return 0;
    out_msgs[*count][0] = s;
    out_msgs[*count][1] = d1;
    out_msgs[*count][2] = d2;
    out_lens[*count] = 3;
    (*count)++;
    return 1;
}

static int arr_contains(const uint8_t *arr, int count, uint8_t n) {
    int i;
    for (i = 0; i < count; i++) if (arr[i] == n) return 1;
    return 0;
}

static void arr_add_sorted(uint8_t *arr, int *count, uint8_t n) {
    int i, j;
    if (!arr || !count || *count >= MAX_ARP_NOTES) return;
    for (i = 0; i < *count; i++) {
        if (arr[i] == n) return;
        if (arr[i] > n) break;
    }
    for (j = *count; j > i; j--) arr[j] = arr[j - 1];
    arr[i] = n;
    (*count)++;
}

static void arr_add_tail_unique(uint8_t *arr, int *count, uint8_t n) {
    if (!arr || !count || *count >= MAX_ARP_NOTES) return;
    if (arr_contains(arr, *count, n)) return;
    arr[*count] = n;
    (*count)++;
}

static void arr_remove(uint8_t *arr, int *count, uint8_t n) {
    int i, found = -1;
    if (!arr || !count) return;
    for (i = 0; i < *count; i++) if (arr[i] == n) { found = i; break; }
    if (found < 0) return;
    for (i = found; i < *count - 1; i++) arr[i] = arr[i + 1];
    (*count)--;
}

static void as_played_add(superarp_instance_t *inst, uint8_t n) {
    if (!inst || inst->as_played_count >= MAX_ARP_NOTES || arr_contains(inst->as_played, inst->as_played_count, n)) return;
    inst->as_played[inst->as_played_count++] = n;
}

static int parse_progression_pattern_string(const char *src, progression_pattern_t *out) {
    const char *p = src;
    int step = 0;
    if (!src || !out) return 0;
    memset(out, 0, sizeof(*out));
    while (*p && step < MAX_PATTERN_STEPS) {
        int note_count = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '(') {
            p++;
            while (*p && *p != ')') {
                int v = 0;
                if (*p < '0' || *p > '9') return 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                if (v < 1) return 0;
                if (note_count < MAX_PATTERN_NOTES) out->steps[step][note_count++] = v;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '-' || *p == ',') p++;
                while (*p == ' ' || *p == '\t') p++;
            }
            if (*p != ')') return 0;
            p++;
        } else {
            int v = 0;
            if (*p < '0' || *p > '9') return 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (v < 1) return 0;
            out->steps[step][0] = v;
            note_count = 1;
        }
        if (note_count <= 0) return 0;
        out->step_sizes[step] = note_count;
        step++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '-') p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    out->step_count = step;
    return step > 0;
}

static void set_progression_pattern_value(superarp_instance_t *inst, const char *value) {
    progression_pattern_t parsed;
    if (!inst) return;
    if (!value || !*value) value = k_default_progression_pattern;

    if (strcmp(value, "prg_pat_01") == 0) value = "1-2-3";
    else if (strcmp(value, "prg_pat_02") == 0) value = "1-5-4-2-3";
    else if (strcmp(value, "prg_pat_03") == 0) value = "1-(2-3)-4";

    if (!parse_progression_pattern_string(value, &parsed)) {
        value = k_default_progression_pattern;
        (void)parse_progression_pattern_string(value, &parsed);
    }
    strncpy(inst->progression_pattern_value, value, sizeof(inst->progression_pattern_value) - 1);
    inst->progression_pattern_value[sizeof(inst->progression_pattern_value) - 1] = '\0';
    inst->progression_pattern = parsed;
}

static void sanitize_rhythm_pattern(const char *value, char *out, int out_len) {
    int i = 0;
    const char *p = value;
    if (!out || out_len < 2) return;
    if (!p || !*p) p = k_default_rhythm_pattern;
    if (strcmp(p, "rhy_01") == 0) p = "00";
    else if (strcmp(p, "rhy_02") == 0) p = "0x0";
    else if (strcmp(p, "rhy_03") == 0) p = "0xx0";
    else if (strcmp(p, "rhy_04") == 0) p = "0x0xx";
    while (*p && i < out_len - 1) {
        if (*p == '0' || *p == 'x' || *p == 'X') out[i++] = (*p == 'X') ? 'x' : *p;
        p++;
    }
    if (i == 0) {
        out[0] = '0';
        i = 1;
    }
    out[i] = '\0';
}

static const char *get_rhythm_pattern(superarp_instance_t *inst, int *len_out) {
    const char *p;
    if (!inst) return k_default_rhythm_pattern;
    p = inst->rhythm_pattern_value[0] ? inst->rhythm_pattern_value : k_default_rhythm_pattern;
    if (len_out) *len_out = (int)strlen(p);
    return p;
}

static void clamp_phase(superarp_instance_t *inst) {
    int len = 0;
    if (!inst) return;
    (void)get_rhythm_pattern(inst, &len);
    if (len < 1) inst->phase_offset = 0;
    else inst->phase_offset = clamp_int(inst->phase_offset, 0, len - 1);
}

static void reset_progression_state(superarp_instance_t *inst) {
    if (!inst) return;
    inst->progression_cursor = (inst->progression_mode == PROG_DOWN) ? -1 : 0;
    inst->global_step_index = 0;
    inst->progression_emit_index = 0;
}

static void reset_rhythm_state(superarp_instance_t *inst) {
    if (!inst) return;
    inst->rhythm_cursor = 0;
}

static void reset_modifier_state(superarp_instance_t *inst) {
    if (!inst) return;
    inst->modifier_step_counter = 0;
}

static void reset_phrase(superarp_instance_t *inst, int force_modifier_reset) {
    if (!inst) return;
    reset_rhythm_state(inst);
    reset_progression_state(inst);
    if (force_modifier_reset || inst->modifier_trigger_mode == TRIG_RETRIGGER) {
        reset_modifier_state(inst);
    }
    /* Keep transport phase continuous across phrase resets. */
    inst->swing_phase = 0;
}

static void clear_active(superarp_instance_t *inst) {
    if (!inst) return;
    inst->active_count = 0;
    inst->as_played_count = 0;
}

static void sync_active_to_physical(superarp_instance_t *inst) {
    int i;
    if (!inst) return;
    clear_active(inst);
    for (i = 0; i < inst->physical_count; i++) {
        arr_add_sorted(inst->active_notes, &inst->active_count, inst->physical_notes[i]);
    }
    for (i = 0; i < inst->physical_as_played_count; i++) {
        uint8_t n = inst->physical_as_played[i];
        if (arr_contains(inst->active_notes, inst->active_count, n)) {
            as_played_add(inst, n);
        }
    }
}

static int steps_per_beat(rate_t r) {
    switch (r) {
        case RATE_1_4: return 1;
        case RATE_1_8: return 2;
        case RATE_1_16: return 4;
        case RATE_1_32:
        default: return 8;
    }
}

static void recalc_clock_timing(superarp_instance_t *inst) {
    int spb;
    int clocks;
    if (!inst) return;
    spb = steps_per_beat(inst->rate);
    if (spb < 1) spb = 1;
    clocks = 24 / spb; /* MIDI clock is 24 PPQN */
    if (clocks < 1) clocks = 1;
    if (inst->triplet) {
        clocks = (clocks * 2 + 1) / 3;
        if (clocks < 1) clocks = 1;
    }
    inst->clocks_per_step = clocks;
}

static void realign_clock_phase(superarp_instance_t *inst) {
    if (!inst) return;
    if (inst->clocks_per_step < 1) inst->clocks_per_step = 1;
    inst->clock_counter = (int)(inst->clock_tick_total % (uint64_t)inst->clocks_per_step);
    /*
     * Drop queued triggers from the previous division grid so the new rate
     * resumes on the host-anchored clock phase immediately.
     */
    inst->pending_step_triggers = 0;
    inst->delayed_step_triggers = 0;
}

static void recalc_timing(superarp_instance_t *inst, int sample_rate) {
    double step_samples;
    int spb;
    if (!inst || sample_rate <= 0) return;
    if (inst->bpm < 40) inst->bpm = 40;
    if (inst->bpm > 240) inst->bpm = 240;
    spb = steps_per_beat(inst->rate);
    step_samples = ((double)sample_rate * 60.0) / ((double)inst->bpm * (double)spb);
    if (inst->triplet) step_samples *= (2.0 / 3.0);
    if (step_samples < 1.0) step_samples = 1.0;
    inst->sample_rate = sample_rate;
    inst->step_interval_base_f = step_samples;
    inst->step_interval_base = (int)(step_samples + 0.5);
    if (inst->step_interval_base < 1) inst->step_interval_base = 1;
    if (inst->samples_until_step_f > inst->step_interval_base_f || inst->samples_until_step_f <= 0.0) {
        inst->samples_until_step_f = inst->step_interval_base_f;
    }
    inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
    if (inst->samples_until_step < 1) inst->samples_until_step = 1;
    inst->timing_dirty = 0;
}

static double next_step_interval(superarp_instance_t *inst) {
    double base, sw, d, out;
    if (!inst) return 1.0;
    base = inst->step_interval_base_f > 0.0 ? inst->step_interval_base_f : 1.0;
    if (inst->triplet) return base;
    sw = clamp_int(inst->swing, 0, 100);
    if (sw == 0) return base;
    d = (base * (double)sw) / 200.0;
    if (inst->swing_phase == 0) { out = base + d; inst->swing_phase = 1; }
    else { out = base - d; inst->swing_phase = 0; }
    if (out < 1.0) out = 1.0;
    return out;
}

static void realign_internal_phase(superarp_instance_t *inst) {
    double interval, rem, until_next;
    double total_f, q;
    if (!inst) return;
    interval = inst->step_interval_base_f > 0.0 ? inst->step_interval_base_f : 1.0;
    total_f = (double)inst->internal_sample_total;
    q = (double)((uint64_t)(total_f / interval));
    rem = total_f - (q * interval);
    while (rem >= interval) rem -= interval;
    while (rem < 0.0) rem += interval;
    if (rem < 1e-9) until_next = interval;
    else until_next = interval - rem;
    if (until_next < 1.0) until_next = 1.0;
    inst->samples_until_step_f = until_next;
    inst->samples_until_step = (int)(until_next + 0.5);
    if (inst->samples_until_step < 1) inst->samples_until_step = 1;
    inst->swing_phase = 0;
}

static void update_phrase_running(superarp_instance_t *inst) {
    if (!inst) return;
    if (live_note_count(inst) > 0) {
        if (!inst->phrase_running) {
            inst->phrase_running = 1;
            if (inst->rhythm_trigger_mode == TRIG_RETRIGGER) reset_rhythm_state(inst);
            if (inst->progression_trigger_mode == TRIG_RETRIGGER) reset_progression_state(inst);
            if (inst->modifier_trigger_mode == TRIG_RETRIGGER) reset_modifier_state(inst);
        }
    } else {
        if (inst->phrase_running) {
            inst->phrase_running = 0;
            if (inst->rhythm_trigger_mode == TRIG_RETRIGGER) reset_rhythm_state(inst);
            if (inst->progression_trigger_mode == TRIG_RETRIGGER) reset_progression_state(inst);
            if (inst->modifier_trigger_mode == TRIG_RETRIGGER) reset_modifier_state(inst);
        }
    }
}

static void set_latch(superarp_instance_t *inst, int en) {
    if (!inst) return;
    en = en ? 1 : 0;
    if (inst->latch == en) return;
    inst->latch = en;
    if (en) {
        if (inst->physical_count > 0) { sync_active_to_physical(inst); inst->latch_ready_replace = 0; }
        else inst->latch_ready_replace = 1;
    } else {
        inst->latch_ready_replace = 0;
        sync_active_to_physical(inst);
    }
    inst->note_set_dirty = 0;
    update_phrase_running(inst);
}

static int find_note_index(const uint8_t *arr, int count, uint8_t note) {
    int i;
    if (!arr || count <= 0) return -1;
    for (i = 0; i < count; i++) {
        if (arr[i] == note) return i;
    }
    return -1;
}

static int peek_next_note(superarp_instance_t *inst) {
    int idx, count;
    if (!inst || inst->active_count == 0) return -1;

    if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) {
        count = inst->as_played_count;
        idx = inst->progression_cursor;
        if (idx < 0 || idx >= count) idx = 0;
        return (int)inst->as_played[idx];
    }

    count = inst->active_count;
    idx = inst->progression_cursor;
    if (inst->progression_mode == PROG_DOWN) {
        if (idx < 0 || idx >= count) idx = count - 1;
    } else {
        if (idx < 0 || idx >= count) idx = 0;
    }
    return (int)inst->active_notes[idx];
}

static void restore_progression_cursor(superarp_instance_t *inst, int keep_note) {
    const uint8_t *src = NULL;
    int count = 0;
    int idx = -1;

    if (!inst || inst->active_count == 0) {
        if (inst) inst->progression_cursor = (inst->progression_mode == PROG_DOWN) ? -1 : 0;
        return;
    }

    if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) {
        src = inst->as_played;
        count = inst->as_played_count;
    } else {
        src = inst->active_notes;
        count = inst->active_count;
    }

    if (keep_note >= 0 && keep_note <= 127) {
        idx = find_note_index(src, count, (uint8_t)keep_note);
    }

    if (idx >= 0) {
        inst->progression_cursor = idx;
    } else {
        inst->progression_cursor = (inst->progression_mode == PROG_DOWN) ? (count - 1) : 0;
    }
}

static void apply_pending_note_set(superarp_instance_t *inst) {
    int keep_next = -1;
    int preserve_cursor;
    int note_based_mode;
    int old_cycle_count = 0;
    int new_cycle_count = 0;
    int cycle_count = 1;
    if (!inst || inst->latch || !inst->note_set_dirty) return;

    note_based_mode = (inst->progression_mode == PROG_UP ||
                       inst->progression_mode == PROG_DOWN ||
                       inst->progression_mode == PROG_AS_PLAYED);

    if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) old_cycle_count = inst->as_played_count;
    else old_cycle_count = inst->active_count;

    preserve_cursor = (inst->active_count > 0 &&
                       inst->physical_count > 0 &&
                       inst->global_step_index > 0 &&
                       inst->progression_mode != PROG_CHORD);
    if (preserve_cursor) keep_next = peek_next_note(inst);
    dlog(inst, "apply_pending start preserve=%d keep_next=%d old_cycle=%d pc=%d ac=%d asc=%d cursor=%d emit_idx=%llu",
         preserve_cursor, keep_next, old_cycle_count, inst->physical_count, inst->active_count, inst->as_played_count,
         inst->progression_cursor, (unsigned long long)inst->progression_emit_index);

    sync_active_to_physical(inst);
    if (inst->active_count <= 0) {
        inst->progression_cursor = (inst->progression_mode == PROG_DOWN) ? -1 : 0;
        inst->note_set_dirty = 0;
        return;
    }

    if (preserve_cursor) {
        if (note_based_mode) {
            restore_progression_cursor(inst, keep_next);
            if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) new_cycle_count = inst->as_played_count;
            else new_cycle_count = inst->active_count;

            /*
             * If we were cycling a single-note set and the player adds notes after
             * the first emit, move to the next slot so step 2 does not replay step 1.
             */
            if (old_cycle_count == 1 && new_cycle_count > 1) {
                if (inst->progression_mode == PROG_DOWN) {
                    inst->progression_cursor = (inst->progression_cursor - 1 + new_cycle_count) % new_cycle_count;
                } else {
                    inst->progression_cursor = (inst->progression_cursor + 1) % new_cycle_count;
                }
            }
            dlog(inst, "apply_pending preserve-done new_cycle=%d cursor=%d", new_cycle_count, inst->progression_cursor);
        } else {
            if (inst->progression_mode == PROG_PATTERN) {
                cycle_count = inst->progression_pattern.step_count > 0 ? inst->progression_pattern.step_count : 1;
            } else if (inst->progression_mode == PROG_RANDOM_PATTERN) {
                cycle_count = inst->random_pattern_length > 0 ? inst->random_pattern_length : 1;
            } else if (inst->progression_mode == PROG_LEAP_INWARD || inst->progression_mode == PROG_LEAP_OUTWARD) {
                cycle_count = inst->active_count > 0 ? inst->active_count : 1;
            } else {
                cycle_count = inst->active_count > 0 ? inst->active_count : 1;
            }
            if (cycle_count < 1) cycle_count = 1;
            if (inst->progression_cursor < 0) inst->progression_cursor = 0;
            else inst->progression_cursor %= cycle_count;
            dlog(inst, "apply_pending preserve-step cycle=%d cursor=%d", cycle_count, inst->progression_cursor);
        }
    } else {
        inst->progression_cursor = (inst->progression_mode == PROG_DOWN) ? -1 : 0;
        dlog(inst, "apply_pending reset cursor=%d", inst->progression_cursor);
    }
    inst->note_set_dirty = 0;
    dlog(inst, "apply_pending end pc=%d ac=%d asc=%d cursor=%d", inst->physical_count, inst->active_count, inst->as_played_count, inst->progression_cursor);
}

static void note_on(superarp_instance_t *inst, uint8_t note, uint8_t vel) {
    int keep_next;
    int note_based_mode;
    int old_cycle_count = 0;
    int new_cycle_count = 0;
    if (!inst) return;
    note_based_mode = (inst->progression_mode == PROG_UP ||
                       inst->progression_mode == PROG_DOWN ||
                       inst->progression_mode == PROG_AS_PLAYED);
    if (note_based_mode) {
        if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) old_cycle_count = inst->as_played_count;
        else old_cycle_count = inst->active_count;
    }
    keep_next = peek_next_note(inst);
    dlog(inst, "note_on state-before note=%u vel=%u pc=%d ac=%d asc=%d keep_next=%d cursor=%d emit_idx=%llu",
         note, vel, inst->physical_count, inst->active_count, inst->as_played_count, keep_next, inst->progression_cursor,
         (unsigned long long)inst->progression_emit_index);
    if (vel > 0) inst->last_input_velocity = vel;
    arr_add_sorted(inst->physical_notes, &inst->physical_count, note);
    arr_add_tail_unique(inst->physical_as_played, &inst->physical_as_played_count, note);
    if (inst->latch) {
        if (inst->latch_ready_replace) { clear_active(inst); inst->latch_ready_replace = 0; reset_phrase(inst, 0); }
        arr_add_sorted(inst->active_notes, &inst->active_count, note);
        as_played_add(inst, note);
        if (note_based_mode) {
            restore_progression_cursor(inst, keep_next);
            if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) new_cycle_count = inst->as_played_count;
            else new_cycle_count = inst->active_count;
            if (inst->global_step_index > 0 && old_cycle_count == 1 && new_cycle_count > 1) {
                if (inst->progression_mode == PROG_DOWN) {
                    inst->progression_cursor = (inst->progression_cursor - 1 + new_cycle_count) % new_cycle_count;
                } else {
                    inst->progression_cursor = (inst->progression_cursor + 1) % new_cycle_count;
                }
                dlog(inst, "note_on latch single->multi advance new_cycle=%d cursor=%d",
                     new_cycle_count, inst->progression_cursor);
            }
        }
    } else {
        inst->note_set_dirty = 1;
    }
    update_phrase_running(inst);
    dlog(inst, "note_on state-after note=%u pc=%d ac=%d asc=%d dirty=%d cursor=%d phrase=%d",
         note, inst->physical_count, inst->active_count, inst->as_played_count, inst->note_set_dirty, inst->progression_cursor, inst->phrase_running);
}

static void note_off(superarp_instance_t *inst, uint8_t note) {
    int keep_next;
    int note_based_mode;
    if (!inst) return;
    note_based_mode = (inst->progression_mode == PROG_UP ||
                       inst->progression_mode == PROG_DOWN ||
                       inst->progression_mode == PROG_AS_PLAYED);
    keep_next = peek_next_note(inst);
    arr_remove(inst->physical_notes, &inst->physical_count, note);
    arr_remove(inst->physical_as_played, &inst->physical_as_played_count, note);
    if (inst->latch) {
        if (inst->physical_count == 0) inst->latch_ready_replace = 1;
        if (note_based_mode) restore_progression_cursor(inst, keep_next);
    } else {
        if (inst->physical_count == 0) {
            clear_active(inst);
            inst->note_set_dirty = 0;
        } else {
            inst->note_set_dirty = 1;
        }
    }
    update_phrase_running(inst);
}

static int progression_cycle_len(superarp_instance_t *inst) {
    int pattern_len;
    if (!inst) return 1;
    if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) {
        return inst->as_played_count;
    }
    if (inst->progression_mode == PROG_PATTERN) {
        pattern_len = inst->progression_pattern.step_count;
        return pattern_len > 0 ? pattern_len : 1;
    }
    if (inst->progression_mode == PROG_RANDOM_PATTERN) {
        return inst->random_pattern_length > 0 ? inst->random_pattern_length : 1;
    }
    if (inst->active_count > 0) return inst->active_count;
    return 1;
}

static int map_missing_index(int idx1, int held, missing_note_policy_t policy) {
    int period, n;
    if (held <= 0 || idx1 < 1) return -1;
    if (idx1 <= held) return idx1;
    switch (policy) {
        case MISSING_WRAP:
            return ((idx1 - 1) % held) + 1;
        case MISSING_CLAMP:
            return held;
        case MISSING_SKIP:
            return -1;
        case MISSING_FOLD:
        default:
            if (held == 1) return 1;
            period = 2 * held - 2;
            n = ((idx1 - 1) % period) + 1;
            if (n <= held) return n;
            return period - n + 2;
    }
}

static int add_unique_note(int *notes, int count, int note) {
    int i;
    for (i = 0; i < count; i++) if (notes[i] == note) return count;
    if (count >= MAX_ARP_NOTES) return count;
    notes[count] = note;
    return count + 1;
}

static int apply_octave_range(superarp_instance_t *inst, int note, int *applied_octave_out) {
    int out = note;
    int oct = 0;
    int oct_count;
    int cycle_len;
    int range_steps;
    if (applied_octave_out) *applied_octave_out = 0;
    if (!inst) return clamp_int(out, 0, 127);

    range_steps = octave_range_steps(inst);
    oct_count = abs(range_steps) + 1; /* 0 => only base octave */
    if (oct_count > 1) {
        cycle_len = progression_cycle_len(inst);
        if (cycle_len < 1) cycle_len = 1;
        oct = (int)((inst->progression_emit_index / (uint64_t)cycle_len) % (uint64_t)oct_count);
        if (range_steps < 0) oct = -oct;
    }
    out += oct * 12;
    if (applied_octave_out) *applied_octave_out = oct;
    return clamp_int(out, 0, 127);
}

static int next_note(superarp_instance_t *inst) {
    int idx;
    int count;
    int out_note;
    if (!inst || inst->active_count == 0) return -1;

    if (inst->progression_mode == PROG_DOWN) {
        count = inst->active_count;
        idx = inst->progression_cursor;
        if (idx < 0 || idx >= count) idx = count - 1;
        inst->progression_cursor = idx - 1;
        if (inst->progression_cursor < 0) inst->progression_cursor = count - 1;
        out_note = (int)inst->active_notes[idx];
        dlog(inst, "next_note down idx=%d out=%d next_cursor=%d count=%d", idx, out_note, inst->progression_cursor, count);
        return out_note;
    }

    if (inst->progression_mode == PROG_AS_PLAYED && inst->as_played_count > 0) {
        count = inst->as_played_count;
        idx = inst->progression_cursor;
        if (idx < 0 || idx >= count) idx = 0;
        inst->progression_cursor = (idx + 1) % count;
        out_note = (int)inst->as_played[idx];
        dlog(inst, "next_note as_played idx=%d out=%d next_cursor=%d count=%d", idx, out_note, inst->progression_cursor, count);
        return out_note;
    }

    /* Fallback behavior for unimplemented modes uses UP over sorted active notes. */
    idx = inst->progression_cursor;
    if (idx < 0 || idx >= inst->active_count) idx = 0;
    inst->progression_cursor = (idx + 1) % inst->active_count;
    out_note = (int)inst->active_notes[idx];
    dlog(inst, "next_note up idx=%d out=%d next_cursor=%d count=%d", idx, out_note, inst->progression_cursor, inst->active_count);
    return out_note;
}

static int leap_inward_index(int step, int count) {
    if (count <= 0) return -1;
    step %= count;
    if ((step & 1) == 0) return step / 2;
    return count - 1 - (step / 2);
}

static int leap_outward_index(int step, int count) {
    int idx;
    if (count <= 0) return -1;
    step %= count;
    if (count & 1) {
        int center = count / 2;
        if (step == 0) idx = center;
        else {
            int k = (step + 1) / 2;
            idx = center + ((step & 1) ? -k : k);
        }
    } else {
        int low = count / 2 - 1;
        int high = count / 2;
        if (step == 0) idx = low;
        else if (step & 1) idx = high + ((step - 1) / 2);
        else idx = low - (step / 2);
    }
    return clamp_int(idx, 0, count - 1);
}

static int next_step_notes(superarp_instance_t *inst, int *notes, int max_notes) {
    const progression_pattern_t *pat;
    uint32_t r;
    int count = 0;
    int step, idx, i, req_idx, mapped, h, pat_len;
    if (!inst || !notes || max_notes <= 0 || inst->active_count <= 0) return 0;

    if (inst->progression_mode == PROG_CHORD) {
        for (i = 0; i < inst->active_count && count < max_notes; i++) {
            notes[count++] = (int)inst->active_notes[i];
        }
        dlog(inst, "next_step chord count=%d", count);
        return count;
    }

    if (inst->progression_mode == PROG_LEAP_INWARD || inst->progression_mode == PROG_LEAP_OUTWARD) {
        h = inst->active_count;
        step = inst->progression_cursor;
        if (step < 0) step = 0;
        idx = (inst->progression_mode == PROG_LEAP_INWARD) ? leap_inward_index(step, h) : leap_outward_index(step, h);
        inst->progression_cursor = (step + 1) % h;
        notes[0] = (int)inst->active_notes[idx];
        dlog(inst, "next_step leap mode=%d step=%d idx=%d out=%d next_cursor=%d",
             (int)inst->progression_mode, step, idx, notes[0], inst->progression_cursor);
        return 1;
    }

    if (inst->progression_mode == PROG_PATTERN) {
        h = inst->active_count;
        pat = &inst->progression_pattern;
        pat_len = pat->step_count > 0 ? pat->step_count : 1;
        step = inst->progression_cursor;
        if (step < 0) step = 0;
        step %= pat_len;
        for (i = 0; i < pat->step_sizes[step] && count < max_notes; i++) {
            req_idx = pat->steps[step][i];
            mapped = map_missing_index(req_idx, h, inst->missing_note_policy);
            if (mapped < 1) continue;
            count = add_unique_note(notes, count, (int)inst->active_notes[mapped - 1]);
        }
        inst->progression_cursor = (step + 1) % pat_len;
        dlog(inst, "next_step pattern step=%d out_count=%d next_cursor=%d", step, count, inst->progression_cursor);
        return count;
    }

    if (inst->progression_mode == PROG_RANDOM_PATTERN) {
        uint32_t seed_base;
        uint32_t seed_chord;
        uint32_t seed_combo;
        int target_notes;
        int chord_amt;
        int tries;
        h = inst->active_count;
        pat_len = inst->random_pattern_length > 0 ? inst->random_pattern_length : 1;
        step = inst->progression_cursor;
        if (step < 0) step = 0;
        step %= pat_len;
        chord_amt = clamp_int(inst->random_pattern_chords, 0, 100);
        seed_base = (uint32_t)inst->progression_seed;
        seed_chord = (uint32_t)inst->random_pattern_chord_seed;
        seed_combo = mix_u32(seed_base ^ mix_u32(seed_chord ^ 0xA5A5A5A5u));
        target_notes = 1 + (2 * chord_amt) / 100;
        r = mix_u32(seed_combo ^ (uint32_t)(step * 2246822519u) ^ 0x4A17u);
        if ((int)(r % 100u) < ((2 * chord_amt) % 100)) target_notes++;
        if (target_notes < 1) target_notes = 1;
        if (target_notes > 3) target_notes = 3;

        tries = 0;
        while (count < target_notes && tries < 24) {
            r = mix_u32(seed_combo ^
                        (uint32_t)(step * 2246822519u) ^
                        (uint32_t)((tries + 1) * 3266489917u));
            req_idx = 1 + (int)(r % (uint32_t)PROG_INDEX_MAX);
            mapped = map_missing_index(req_idx, h, inst->missing_note_policy);
            if (mapped >= 1) {
                count = add_unique_note(notes, count, (int)inst->active_notes[mapped - 1]);
            }
            tries++;
        }
        inst->progression_cursor = (step + 1) % pat_len;
        if (count <= 0) {
            dlog(inst, "next_step rand_pat skip step=%d next_cursor=%d", step, inst->progression_cursor);
            return 0;
        }
        dlog(inst, "next_step rand_pat out_count=%d step=%d next_cursor=%d",
             count, step, inst->progression_cursor);
        return count;
    }

    notes[0] = next_note(inst);
    return notes[0] >= 0 ? 1 : 0;
}

static int step_is_trigger(superarp_instance_t *inst) {
    const char *p;
    int len = 0, idx;
    char c;
    if (!inst) return 0;
    p = get_rhythm_pattern(inst, &len);
    if (!p || len < 1) return 1;
    idx = (inst->rhythm_cursor + inst->phase_offset) % len;
    if (idx < 0) idx = 0;
    c = p[idx];
    inst->rhythm_cursor = (inst->rhythm_cursor + 1) % len;
    return c == '0';
}

static void voice_remove_at(superarp_instance_t *inst, int idx) {
    int i;
    if (!inst || idx < 0 || idx >= inst->voice_count) return;
    for (i = idx; i < inst->voice_count - 1; i++) {
        inst->voice_notes[i] = inst->voice_notes[i + 1];
        inst->voice_clock_left[i] = inst->voice_clock_left[i + 1];
        inst->voice_sample_left[i] = inst->voice_sample_left[i + 1];
    }
    inst->voice_count--;
}

static int voice_note_off(superarp_instance_t *inst, int idx,
                          uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    uint8_t note;
    if (!inst || !count || idx < 0 || idx >= inst->voice_count) return 0;
    note = inst->voice_notes[idx];
    if (!emit3(out_msgs, out_lens, max_out, count, 0x80, note, 0)) return 0;
    voice_remove_at(inst, idx);
    return 1;
}

static int flush_all_voices(superarp_instance_t *inst,
                            uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int emitted = 0;
    if (!inst || !count) return 0;
    while (inst->voice_count > 0) {
        if (!voice_note_off(inst, 0, out_msgs, out_lens, max_out, count)) break;
        emitted++;
    }
    return emitted;
}

static int handle_transport_stop(superarp_instance_t *inst,
                                 uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int count = 0;
    if (!inst) return 0;

    /*
     * Panic first: ask downstream synths to stop all sounding notes.
     * Super Arp emits notes on channel 1 (0x90/0x80 base status), so use 0xB0.
     */
    if (max_out > 0) {
        (void)emit3(out_msgs, out_lens, max_out, &count, 0xB0, 123, 0); /* CC123: All Notes Off */
    }

    /* Explicitly flush tracked voices too (for recipients that ignore CC123). */
    if (inst->voice_count > 0 && count < max_out) {
        (void)flush_all_voices(inst, out_msgs + count, out_lens + count, max_out - count, &count);
    }

    /* Clear held/latch state so phrase does not keep running after transport stop. */
    inst->physical_count = 0;
    inst->physical_as_played_count = 0;
    clear_active(inst);
    inst->note_set_dirty = 0;
    inst->latch_ready_replace = inst->latch ? 1 : 0;
    inst->phrase_running = 0;
    reset_phrase(inst, 1);

    return count;
}

static int kill_voice_notes(superarp_instance_t *inst, uint8_t note,
                            uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int i = 0, killed = 0;
    if (!inst || !count) return 0;
    while (i < inst->voice_count) {
        if (inst->voice_notes[i] == note) {
            if (!voice_note_off(inst, i, out_msgs, out_lens, max_out, count)) break;
            killed++;
        } else {
            i++;
        }
    }
    return killed;
}

static void voice_add(superarp_instance_t *inst, uint8_t note, int gate_pct) {
    int idx, clocks, samples;
    if (!inst || inst->voice_count >= MAX_VOICES) return;
    idx = inst->voice_count++;
    inst->voice_notes[idx] = note;
    inst->voice_clock_left[idx] = 0;
    inst->voice_sample_left[idx] = 0;

    if (inst->sync_mode == SYNC_CLOCK) {
        clocks = (inst->clocks_per_step * gate_pct) / 100;
        if (clocks < 1) clocks = 1;
        inst->voice_clock_left[idx] = clocks;
    } else {
        samples = (inst->step_interval_base * gate_pct) / 100;
        if (samples < 1) samples = 1;
        inst->voice_sample_left[idx] = samples;
    }
}

static int advance_voice_timers_clock(superarp_instance_t *inst,
                                      uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int i = 0, emitted = 0;
    if (!inst || !count) return 0;
    while (i < inst->voice_count) {
        if (inst->voice_clock_left[i] > 0) inst->voice_clock_left[i]--;
        if (inst->voice_clock_left[i] <= 0) {
            if (!voice_note_off(inst, i, out_msgs, out_lens, max_out, count)) break;
            emitted++;
        } else {
            i++;
        }
    }
    return emitted;
}

static int advance_voice_timers_samples(superarp_instance_t *inst, int frames,
                                        uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int i = 0, emitted = 0;
    if (!inst || !count) return 0;
    while (i < inst->voice_count) {
        if (inst->voice_sample_left[i] > 0) inst->voice_sample_left[i] -= frames;
        if (inst->voice_sample_left[i] <= 0) {
            if (!voice_note_off(inst, i, out_msgs, out_lens, max_out, count)) break;
            emitted++;
        } else {
            i++;
        }
    }
    return emitted;
}

static int schedule_notes(superarp_instance_t *inst, const int *notes, int note_count,
                          int velocity, int gate_pct,
                          uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int i;
    int voice_limit;
    int uniq[MAX_ARP_NOTES];
    int uniq_count = 0;
    uint8_t vel;
    if (!inst || !notes || note_count <= 0 || !count) return 0;
    vel = (uint8_t)clamp_int(velocity, 1, 127);
    gate_pct = clamp_int(gate_pct, 0, 1600);
    voice_limit = clamp_int(inst->max_voices, 1, MAX_VOICES);

    for (i = 0; i < note_count && i < MAX_ARP_NOTES; i++) {
        uniq_count = add_unique_note(uniq, uniq_count, clamp_int(notes[i], 0, 127));
    }

    /*
     * Retrigger rule: end all currently sounding instances of target pitches first,
     * then emit fresh note-ons. A key cannot be physically pressed twice at once.
     */
    for (i = 0; i < uniq_count; i++) {
        uint8_t n = (uint8_t)uniq[i];
        (void)kill_voice_notes(inst, n, out_msgs, out_lens, max_out, count);
    }

    for (i = 0; i < uniq_count; i++) {
        uint8_t n = (uint8_t)uniq[i];
        while (inst->voice_count >= voice_limit) {
            if (!voice_note_off(inst, 0, out_msgs, out_lens, max_out, count)) return 0; /* oldest-first steal */
        }
        if (!emit3(out_msgs, out_lens, max_out, count, 0x90, n, vel)) return 0;
        if (gate_pct <= 0) {
            if (!emit3(out_msgs, out_lens, max_out, count, 0x80, n, 0)) return 0;
        } else {
            voice_add(inst, n, gate_pct);
        }
    }
    return 1;
}

static void advance_step_counters(superarp_instance_t *inst) {
    if (!inst) return;
    inst->global_step_index++;
    inst->modifier_step_counter++;
}

static int run_step(superarp_instance_t *inst, uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int count = 0, notes[MAX_ARP_NOTES], final_notes[MAX_ARP_NOTES], note_count, final_count, i, ok;
    int vel, gate_pct, oct_rand;
    uint32_t hhash, mod_hash;
    uint64_t mod_step;
    if (!inst || max_out < 1) return 0;
    dlog(inst, "run_step start gs=%llu cursor=%d dirty=%d pc=%d ac=%d asc=%d emit_idx=%llu",
         (unsigned long long)inst->global_step_index, inst->progression_cursor, inst->note_set_dirty,
         inst->physical_count, inst->active_count, inst->as_played_count, (unsigned long long)inst->progression_emit_index);
    if (!step_is_trigger(inst)) {
        dlog(inst, "run_step rest gs=%llu", (unsigned long long)inst->global_step_index);
        advance_step_counters(inst);
        return 0;
    }
    apply_pending_note_set(inst);
    mod_step = modifier_step_index(inst);
    hhash = held_set_hash(inst);
    mod_hash = modifier_rng_hash(inst, hhash);
    if (should_drop_step(inst, mod_hash, mod_step)) {
        dlog(inst, "run_step drop gs=%llu", (unsigned long long)inst->global_step_index);
        advance_step_counters(inst);
        return 0;
    }
    vel = step_velocity(inst, mod_step);
    gate_pct = step_gate_pct(inst, mod_step);
    oct_rand = random_octave_offset(inst, mod_hash, mod_step);
    note_count = next_step_notes(inst, notes, MAX_ARP_NOTES);
    apply_note_randomization(inst, notes, note_count, mod_hash, mod_step);
    if (note_count <= 0) {
        if (inst->progression_trigger_mode == TRIG_CONTINUOUS) {
            inst->progression_cursor++;
            inst->progression_emit_index++;
        }
        dlog(inst, "run_step no-note gs=%llu", (unsigned long long)inst->global_step_index);
        advance_step_counters(inst);
        return 0;
    }
    final_count = 0;
    for (i = 0; i < note_count; i++) {
        int out_note = apply_octave_range(inst, notes[i], NULL);
        out_note = clamp_int(out_note + oct_rand, 0, 127);
        final_count = add_unique_note(final_notes, final_count, out_note);
    }
    if (final_count <= 0) {
        advance_step_counters(inst);
        return 0;
    }
    ok = schedule_notes(inst, final_notes, final_count, vel, gate_pct, out_msgs, out_lens, max_out, &count);
    if (ok) inst->progression_emit_index++;
    dlog(inst, "run_step emit=%d note0=%d count=%d vel=%d gate=%d gs=%llu pending=%d",
         ok ? 1 : 0, final_notes[0], final_count, vel, gate_pct,
         (unsigned long long)inst->global_step_index, inst->pending_step_triggers);
    advance_step_counters(inst);
    return count;
}

static int process_clock_tick(superarp_instance_t *inst, uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int count = 0;
    if (!inst || max_out < 1) return 0;
    if (CLOCK_ARP_OUTPUT_DELAY_TICKS == 1 && inst->delayed_step_triggers > 0) {
        inst->pending_step_triggers += inst->delayed_step_triggers;
        dlog(inst, "clock delayed->pending +%d -> %d",
             inst->delayed_step_triggers, inst->pending_step_triggers);
        inst->delayed_step_triggers = 0;
    }
    (void)advance_voice_timers_clock(inst, out_msgs, out_lens, max_out, &count);

    inst->clock_tick_total++;
    inst->clock_counter = (int)(inst->clock_tick_total % (uint64_t)inst->clocks_per_step);
    if (inst->clock_counter == 0) {
        if (CLOCK_ARP_OUTPUT_DELAY_TICKS > 0) {
            inst->delayed_step_triggers++;
            dlog(inst, "clock boundary delayed_step++ -> %d", inst->delayed_step_triggers);
        } else {
            inst->pending_step_triggers++;
            dlog(inst, "clock boundary pending_step++ -> %d", inst->pending_step_triggers);
        }
    }
    return count;
}

static int enforce_voice_limit(superarp_instance_t *inst,
                               uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int limit;
    int emitted = 0;
    if (!inst || !count) return 0;
    limit = clamp_int(inst->max_voices, 1, MAX_VOICES);
    while (inst->voice_count > limit) {
        if (!voice_note_off(inst, 0, out_msgs, out_lens, max_out, count)) break;
        emitted++;
    }
    return emitted;
}

static void* superarp_create_instance(const char *module_dir, const char *config_json) {
    superarp_instance_t *inst;
    (void)config_json;
    inst = calloc(1, sizeof(superarp_instance_t));
    if (!inst) return NULL;
    inst->rate = RATE_1_16;
    inst->bpm = DEFAULT_BPM;
    inst->triplet = 0;
    inst->gate = 80;
    inst->velocity_override = 0;
    inst->swing = 0;
    inst->phase_offset = 0;
    inst->latch = 0;
    inst->max_voices = 8;
    inst->octave_range = OCT_RANGE_0;
    inst->progression_mode = PROG_UP;
    inst->progression_seed = 1;
    inst->missing_note_policy = MISSING_FOLD;
    inst->random_pattern_length = 8;
    inst->random_pattern_chords = 0;
    inst->random_pattern_chord_seed = 1;
    inst->rhythm_seed = 1;
    inst->progression_trigger_mode = TRIG_RETRIGGER;
    inst->rhythm_trigger_mode = TRIG_RETRIGGER;
    inst->modifier_trigger_mode = TRIG_RETRIGGER;
    inst->modifier_loop_length = 16;
    inst->drop_amount = 0;
    inst->drop_seed = 1;
    inst->velocity_random_amount = 0;
    inst->velocity_seed = 1;
    inst->gate_random_amount = 0;
    inst->gate_seed = 1;
    inst->random_octave_amount = 0;
    inst->random_octave_range = OCT_RAND_P1;
    inst->random_octave_seed = 1;
    inst->random_note_amount = 0;
    inst->random_note_seed = 1;
    inst->sync_mode = SYNC_INTERNAL;
    inst->sample_rate = 0;
    inst->timing_dirty = 1;
    inst->step_interval_base = 1;
    inst->samples_until_step = 0;
    inst->step_interval_base_f = 1.0;
    inst->samples_until_step_f = 0.0;
    inst->internal_sample_total = 0;
    inst->swing_phase = 0;
    inst->clock_counter = 0;
    inst->clocks_per_step = 6;
    inst->clock_running = 1;
    inst->clock_tick_total = 0;
    inst->pending_step_triggers = 0;
    inst->delayed_step_triggers = 0;
    inst->voice_count = 0;
    inst->internal_start_grace_armed = 0;
    inst->last_input_velocity = 100;
    inst->chain_params_json[0] = '\0';
    inst->chain_params_len = 0;
    set_progression_pattern_value(inst, k_default_progression_pattern);
    sanitize_rhythm_pattern(k_default_rhythm_pattern, inst->rhythm_pattern_value, sizeof(inst->rhythm_pattern_value));
    cache_chain_params_from_module_json(inst, module_dir);
    clamp_phase(inst);
    recalc_clock_timing(inst);
    dlog(inst, "create sync=%d cps=%d", (int)inst->sync_mode, inst->clocks_per_step);
    return inst;
}

static void superarp_destroy_instance(void *instance) {
    superarp_instance_t *inst = (superarp_instance_t *)instance;
    if (!inst) return;
    dlog(inst, "destroy");
    if (inst->debug_fp) {
        fclose(inst->debug_fp);
        inst->debug_fp = NULL;
    }
    free(inst);
}

static int superarp_process_midi(void *instance, const uint8_t *in_msg, int in_len,
                                 uint8_t out_msgs[][3], int out_lens[], int max_out) {
    superarp_instance_t *inst = (superarp_instance_t *)instance;
    int count = 0;
    int live_before = 0;
    uint8_t status, type;
    if (!inst || !in_msg || in_len < 1) return 0;
    status = in_msg[0];
    type = status & 0xF0;

    if (inst->sync_mode == SYNC_CLOCK) {
        if (status == 0xFA) { /* Start */
            inst->clock_running = 1;
            inst->clock_counter = 0;
            inst->clock_tick_total = 0;
            inst->pending_step_triggers = 0;
            inst->delayed_step_triggers = 0;
            reset_phrase(inst, 1);
            dlog(inst, "MIDI Start");
            return 0;
        }
        if (status == 0xFB) { /* Continue */
            inst->clock_running = 1;
            inst->pending_step_triggers = 0;
            inst->delayed_step_triggers = 0;
            dlog(inst, "MIDI Continue");
            return 0;
        }
        if (status == 0xFC) { /* Stop */
            inst->clock_running = 0;
            inst->clock_counter = 0;
            inst->pending_step_triggers = 0;
            inst->delayed_step_triggers = 0;
            dlog(inst, "MIDI Stop");
            return handle_transport_stop(inst, out_msgs, out_lens, max_out);
        }
        if (status == 0xF8) { /* Clock tick */
            if (!inst->clock_running) return 0;
            count = process_clock_tick(inst, out_msgs, out_lens, max_out);
            dlog(inst, "MIDI F8 cc=%d pending=%d immediate_out=%d", inst->clock_counter, inst->pending_step_triggers, count);
            return count;
        }
    } else if (inst->sync_mode == SYNC_INTERNAL) {
        if (status == 0xFA || status == 0xFB) { /* Start / Continue */
            if (inst->timing_dirty && inst->sample_rate > 0) recalc_timing(inst, inst->sample_rate);
            inst->swing_phase = 0;
            inst->internal_sample_total = 0;
            inst->samples_until_step_f = inst->step_interval_base_f > 0.0 ? inst->step_interval_base_f : 1.0;
            inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
            if (inst->samples_until_step < 1) inst->samples_until_step = 1;
            inst->internal_start_grace_armed = 1;
            reset_phrase(inst, 1);
            dlog(inst, status == 0xFA ? "MIDI Start (internal reset)" : "MIDI Continue (internal reset)");
            return 0;
        }
        if (status == 0xFC) { /* Stop */
            inst->internal_start_grace_armed = 0;
            dlog(inst, "MIDI Stop (internal)");
            return handle_transport_stop(inst, out_msgs, out_lens, max_out);
        }
    }

    if ((type == 0x90 || type == 0x80) && in_len >= 3) {
        uint8_t note = in_msg[1];
        uint8_t vel = in_msg[2];
        live_before = live_note_count(inst);
        if (type == 0x90 && vel > 0) {
            dlog(inst, "NOTE_ON note=%u vel=%u cc=%d pending=%d", note, vel, inst->clock_counter, inst->pending_step_triggers);
            note_on(inst, note, vel);
            if (inst->sync_mode == SYNC_CLOCK &&
                inst->clock_running &&
                live_before == 0 &&
                live_note_count(inst) > 0 &&
                inst->pending_step_triggers == 0 &&
                inst->delayed_step_triggers == 0 &&
                inst->clock_counter <= CLOCK_START_GRACE_TICKS &&
                max_out > 0) {
                dlog(inst, "NOTE_ON grace-hit cc=%d -> immediate run_step", inst->clock_counter);
                return run_step(inst, out_msgs, out_lens, max_out);
            }
            if (inst->sync_mode == SYNC_INTERNAL &&
                inst->internal_start_grace_armed &&
                live_before == 0 &&
                live_note_count(inst) > 0 &&
                max_out > 0) {
                inst->internal_start_grace_armed = 0;
                dlog(inst, "NOTE_ON internal-start grace-hit -> immediate run_step");
                count = run_step(inst, out_msgs, out_lens, max_out);
                inst->samples_until_step_f = next_step_interval(inst);
                if (inst->samples_until_step_f < 1.0) inst->samples_until_step_f = 1.0;
                inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
                if (inst->samples_until_step < 1) inst->samples_until_step = 1;
                return count;
            }
        } else {
            dlog(inst, "NOTE_OFF note=%u cc=%d pending=%d", note, inst->clock_counter, inst->pending_step_triggers);
            note_off(inst, note);
        }
        return 0;
    }
    if (max_out < 1) return 0;
    out_msgs[0][0] = in_msg[0];
    out_msgs[0][1] = in_len > 1 ? in_msg[1] : 0;
    out_msgs[0][2] = in_len > 2 ? in_msg[2] : 0;
    out_lens[0] = in_len > 3 ? 3 : in_len;
    return 1;
}

static int superarp_tick(void *instance, int frames, int sample_rate,
                         uint8_t out_msgs[][3], int out_lens[], int max_out) {
    superarp_instance_t *inst = (superarp_instance_t *)instance;
    int count = 0;
    int live_count;
    if (!inst || frames < 0 || max_out < 1) return 0;
    if (inst->timing_dirty || inst->sample_rate != sample_rate) recalc_timing(inst, sample_rate);
    (void)enforce_voice_limit(inst, out_msgs, out_lens, max_out, &count);
    if (count >= max_out) return count;
    if (inst->sync_mode == SYNC_INTERNAL) {
        (void)advance_voice_timers_samples(inst, frames, out_msgs, out_lens, max_out, &count);
        if (count >= max_out) return count;
    }

    live_count = live_note_count(inst);
    if (live_count == 0) {
        int keep_progressing = (inst->progression_trigger_mode == TRIG_CONTINUOUS) || (inst->rhythm_trigger_mode == TRIG_CONTINUOUS);
        if (inst->voice_count > 0) {
            (void)flush_all_voices(inst, out_msgs, out_lens, max_out, &count);
        }
        if (inst->sync_mode == SYNC_CLOCK && !keep_progressing) {
            inst->pending_step_triggers = 0;
            inst->delayed_step_triggers = 0;
        }
        if (!inst->latch) {
            clear_active(inst);
            inst->note_set_dirty = 0;
        }
        inst->phrase_running = 0;
        if (!keep_progressing) return count;
    }

    if (!inst->phrase_running && live_count > 0) inst->phrase_running = 1;
    if (inst->sync_mode == SYNC_CLOCK) {
        if (inst->pending_step_triggers > 0) {
            dlog(inst, "tick drain start pending=%d", inst->pending_step_triggers);
        }
        while (inst->pending_step_triggers > 0 && count < max_out) {
            count += run_step(inst, out_msgs + count, out_lens + count, max_out - count);
            inst->pending_step_triggers--;
            dlog(inst, "tick drain step done pending=%d out=%d", inst->pending_step_triggers, count);
        }
        return count;
    }

    inst->internal_sample_total += (uint64_t)frames;
    inst->samples_until_step_f -= (double)frames;
    while (inst->samples_until_step_f <= 0.0 && count < max_out) {
        count += run_step(inst, out_msgs + count, out_lens + count, max_out - count);
        inst->samples_until_step_f += next_step_interval(inst);
        if (inst->samples_until_step_f < 1.0) inst->samples_until_step_f = 1.0;
    }
    inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
    if (inst->samples_until_step < 1) inst->samples_until_step = 1;
    return count;
}

static void set_enum_rate(superarp_instance_t *inst, const char *v) {
    if (strcmp(v, "1/32") == 0) inst->rate = RATE_1_32;
    else if (strcmp(v, "1/16") == 0) inst->rate = RATE_1_16;
    else if (strcmp(v, "1/8") == 0) inst->rate = RATE_1_8;
    else if (strcmp(v, "1/4") == 0) inst->rate = RATE_1_4;
}

static void set_enum_octave_range(superarp_instance_t *inst, const char *v) {
    int n;
    if (!inst || !v) return;
    if (strcmp(v, "0") == 0) inst->octave_range = OCT_RANGE_0;
    else if (strcmp(v, "+1") == 0) inst->octave_range = OCT_RANGE_P1;
    else if (strcmp(v, "-1") == 0) inst->octave_range = OCT_RANGE_M1;
    else if (strcmp(v, "+2") == 0) inst->octave_range = OCT_RANGE_P2;
    else if (strcmp(v, "-2") == 0) inst->octave_range = OCT_RANGE_M2;
    else {
        n = atoi(v); /* backward compatibility with legacy int states */
        if (n <= 0) inst->octave_range = OCT_RANGE_0;
        else if (n == 1) inst->octave_range = OCT_RANGE_P1;
        else inst->octave_range = OCT_RANGE_P2;
    }
}

static void set_enum_prog_mode(superarp_instance_t *inst, const char *v) {
    progression_mode_t old;
    if (!inst || !v) return;
    old = inst->progression_mode;
    if (strcmp(v, "up") == 0) inst->progression_mode = PROG_UP;
    else if (strcmp(v, "down") == 0) inst->progression_mode = PROG_DOWN;
    else if (strcmp(v, "as_played") == 0) inst->progression_mode = PROG_AS_PLAYED;
    else if (strcmp(v, "leap_inward") == 0) inst->progression_mode = PROG_LEAP_INWARD;
    else if (strcmp(v, "leap_outward") == 0) inst->progression_mode = PROG_LEAP_OUTWARD;
    else if (strcmp(v, "chord") == 0) inst->progression_mode = PROG_CHORD;
    else if (strcmp(v, "pattern") == 0) inst->progression_mode = PROG_PATTERN;
    else if (strcmp(v, "random_pattern") == 0) inst->progression_mode = PROG_RANDOM_PATTERN;

    if (inst->progression_mode != old) {
        inst->progression_cursor = (inst->progression_mode == PROG_DOWN) ? -1 : 0;
        inst->progression_emit_index = 0;
    }
}

static void set_enum_missing(superarp_instance_t *inst, const char *v) {
    if (strcmp(v, "fold") == 0) inst->missing_note_policy = MISSING_FOLD;
    else if (strcmp(v, "wrap") == 0) inst->missing_note_policy = MISSING_WRAP;
    else if (strcmp(v, "clamp") == 0) inst->missing_note_policy = MISSING_CLAMP;
    else if (strcmp(v, "skip") == 0) inst->missing_note_policy = MISSING_SKIP;
}

static void set_enum_pattern_preset(superarp_instance_t *inst, const char *v) {
    set_progression_pattern_value(inst, v);
}

static void set_enum_rhythm(superarp_instance_t *inst, const char *v) {
    if (!inst) return;
    sanitize_rhythm_pattern(v, inst->rhythm_pattern_value, sizeof(inst->rhythm_pattern_value));
}

static void set_enum_trigger_mode(trigger_mode_t *dst, const char *v) {
    if (!dst || !v) return;
    if (strcmp(v, "continuous") == 0) *dst = TRIG_CONTINUOUS;
    else *dst = TRIG_RETRIGGER;
}

static void set_enum_random_octave_range(superarp_instance_t *inst, const char *v) {
    if (!inst || !v) return;
    if (strcmp(v, "+1") == 0 || strcmp(v, "0") == 0) inst->random_octave_range = OCT_RAND_P1; /* migrate old "0" */
    else if (strcmp(v, "-1") == 0) inst->random_octave_range = OCT_RAND_M1;
    else if (strcmp(v, "+-1") == 0) inst->random_octave_range = OCT_RAND_PM1;
    else if (strcmp(v, "+2") == 0) inst->random_octave_range = OCT_RAND_P2;
    else if (strcmp(v, "-2") == 0) inst->random_octave_range = OCT_RAND_M2;
    else if (strcmp(v, "+-2") == 0) inst->random_octave_range = OCT_RAND_PM2;
}

static void superarp_set_param(void *instance, const char *key, const char *val) {
    superarp_instance_t *inst = (superarp_instance_t *)instance;
    int i;
    if (!inst || !key || !val) return;
    if (strcmp(key, "rate") == 0) { set_enum_rate(inst, val); inst->timing_dirty = 1; recalc_clock_timing(inst); }
    else if (strcmp(key, "bpm") == 0) { inst->bpm = clamp_int(atoi(val), 40, 240); inst->timing_dirty = 1; }
    else if (strcmp(key, "triplet") == 0) { inst->triplet = strcmp(val, "on") == 0 ? 1 : 0; inst->timing_dirty = 1; recalc_clock_timing(inst); }
    else if (strcmp(key, "gate") == 0) inst->gate = clamp_int(atoi(val), 0, 1600);
    else if (strcmp(key, "velocity") == 0) inst->velocity_override = clamp_int(atoi(val), 0, 127);
    else if (strcmp(key, "sync") == 0) {
        if (strcmp(val, "clock") == 0) {
            if (inst->sync_mode != SYNC_CLOCK) {
                inst->sync_mode = SYNC_CLOCK;
                inst->clock_running = 1;
                inst->clock_counter = 0;
                inst->clock_tick_total = 0;
                inst->pending_step_triggers = 0;
                inst->delayed_step_triggers = 0;
            }
            recalc_clock_timing(inst);
        } else {
            if (inst->sync_mode != SYNC_INTERNAL) {
                inst->sync_mode = SYNC_INTERNAL;
                inst->clock_counter = 0;
                inst->clock_tick_total = 0;
                inst->pending_step_triggers = 0;
                inst->delayed_step_triggers = 0;
                inst->internal_sample_total = 0;
                if (inst->sample_rate > 0) recalc_timing(inst, inst->sample_rate);
                inst->samples_until_step_f = inst->step_interval_base_f > 0.0 ? inst->step_interval_base_f : 1.0;
                inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
                if (inst->samples_until_step < 1) inst->samples_until_step = 1;
                inst->swing_phase = 0;
            }
        }
    }
    else if (strcmp(key, "swing") == 0) inst->swing = clamp_int(atoi(val), 0, 100);
    else if (strcmp(key, "phase_offset") == 0) { inst->phase_offset = atoi(val); clamp_phase(inst); }
    else if (strcmp(key, "latch") == 0) set_latch(inst, strcmp(val, "on") == 0);
    else if (strcmp(key, "max_voices") == 0) inst->max_voices = clamp_int(atoi(val), 1, MAX_VOICES);
    else if (strcmp(key, "octave_range") == 0) set_enum_octave_range(inst, val);
    else if (strcmp(key, "progression_mode") == 0) set_enum_prog_mode(inst, val);
    else if (strcmp(key, "progression_seed") == 0) inst->progression_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "progression_trigger") == 0) set_enum_trigger_mode(&inst->progression_trigger_mode, val);
    else if (strcmp(key, "missing_note_policy") == 0) set_enum_missing(inst, val);
    else if (strcmp(key, "pattern_preset") == 0) set_enum_pattern_preset(inst, val);
    else if (strcmp(key, "random_pattern_length") == 0) inst->random_pattern_length = clamp_int(atoi(val), 1, 32);
    else if (strcmp(key, "random_pattern_chords") == 0) inst->random_pattern_chords = clamp_int(atoi(val), 0, 100);
    else if (strcmp(key, "random_pattern_chord_seed") == 0) inst->random_pattern_chord_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "rhythm_trigger") == 0) set_enum_trigger_mode(&inst->rhythm_trigger_mode, val);
    else if (strcmp(key, "rhythm_preset") == 0) { set_enum_rhythm(inst, val); clamp_phase(inst); }
    else if (strcmp(key, "rhythm_seed") == 0) inst->rhythm_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "drop_amount") == 0) inst->drop_amount = clamp_int(atoi(val), 0, 100);
    else if (strcmp(key, "drop_seed") == 0) inst->drop_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "velocity_random_amount") == 0) inst->velocity_random_amount = clamp_int(atoi(val), 0, 127);
    else if (strcmp(key, "velocity_seed") == 0) inst->velocity_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "gate_random_amount") == 0) inst->gate_random_amount = clamp_int(atoi(val), 0, 1600);
    else if (strcmp(key, "gate_seed") == 0) inst->gate_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "modifier_trigger") == 0) set_enum_trigger_mode(&inst->modifier_trigger_mode, val);
    else if (strcmp(key, "modifier_loop_length") == 0) inst->modifier_loop_length = clamp_int(atoi(val), 0, 128);
    else if (strcmp(key, "random_octave_amount") == 0) inst->random_octave_amount = clamp_int(atoi(val), 0, 100);
    else if (strcmp(key, "random_octave_range") == 0) set_enum_random_octave_range(inst, val);
    else if (strcmp(key, "random_octave_seed") == 0) inst->random_octave_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "random_note_amount") == 0) inst->random_note_amount = clamp_int(atoi(val), 0, 100);
    else if (strcmp(key, "random_note_seed") == 0) inst->random_note_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "state") == 0) {
        char s[64], b[16];
        if (json_get_string(val, "rate", s, sizeof(s))) superarp_set_param(inst, "rate", s);
        if (json_get_int(val, "bpm", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "bpm", b); }
        if (json_get_string(val, "triplet", s, sizeof(s))) superarp_set_param(inst, "triplet", s);
        if (json_get_int(val, "gate", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "gate", b); }
        if (json_get_int(val, "velocity", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "velocity", b); }
        if (json_get_string(val, "sync", s, sizeof(s))) superarp_set_param(inst, "sync", s);
        if (json_get_int(val, "swing", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "swing", b); }
        if (json_get_int(val, "phase_offset", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "phase_offset", b); }
        if (json_get_string(val, "latch", s, sizeof(s))) superarp_set_param(inst, "latch", s);
        if (json_get_int(val, "max_voices", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "max_voices", b); }
        if (json_get_string(val, "octave_range", s, sizeof(s))) superarp_set_param(inst, "octave_range", s);
        else if (json_get_int(val, "octave_range", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "octave_range", b); }
        if (json_get_string(val, "progression_mode", s, sizeof(s))) superarp_set_param(inst, "progression_mode", s);
        if (json_get_int(val, "progression_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "progression_seed", b); }
        if (json_get_string(val, "progression_trigger", s, sizeof(s))) superarp_set_param(inst, "progression_trigger", s);
        if (json_get_string(val, "missing_note_policy", s, sizeof(s))) superarp_set_param(inst, "missing_note_policy", s);
        if (json_get_string(val, "pattern_preset", s, sizeof(s))) superarp_set_param(inst, "pattern_preset", s);
        if (json_get_int(val, "random_pattern_length", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "random_pattern_length", b); }
        if (json_get_int(val, "random_pattern_chords", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "random_pattern_chords", b); }
        if (json_get_int(val, "random_pattern_chord_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "random_pattern_chord_seed", b); }
        if (json_get_string(val, "rhythm_trigger", s, sizeof(s))) superarp_set_param(inst, "rhythm_trigger", s);
        if (json_get_string(val, "rhythm_preset", s, sizeof(s))) superarp_set_param(inst, "rhythm_preset", s);
        if (json_get_int(val, "rhythm_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "rhythm_seed", b); }
        if (json_get_int(val, "drop_amount", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "drop_amount", b); }
        if (json_get_int(val, "drop_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "drop_seed", b); }
        if (json_get_int(val, "velocity_random_amount", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "velocity_random_amount", b); }
        if (json_get_int(val, "velocity_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "velocity_seed", b); }
        if (json_get_int(val, "gate_random_amount", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "gate_random_amount", b); }
        if (json_get_int(val, "gate_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "gate_seed", b); }
        if (json_get_string(val, "modifier_trigger", s, sizeof(s))) superarp_set_param(inst, "modifier_trigger", s);
        if (json_get_int(val, "modifier_loop_length", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "modifier_loop_length", b); }
        if (json_get_int(val, "random_octave_amount", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "random_octave_amount", b); }
        if (json_get_string(val, "random_octave_range", s, sizeof(s))) superarp_set_param(inst, "random_octave_range", s);
        if (json_get_int(val, "random_octave_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "random_octave_seed", b); }
        if (json_get_int(val, "random_note_amount", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "random_note_amount", b); }
        if (json_get_int(val, "random_note_seed", &i)) { snprintf(b, sizeof(b), "%d", i); superarp_set_param(inst, "random_note_seed", b); }
    }

    if ((strcmp(key, "rate") == 0 || strcmp(key, "triplet") == 0) &&
        inst->sync_mode == SYNC_CLOCK && inst->clock_running) {
        realign_clock_phase(inst);
        dlog(inst, "clock realign rate/triplet cps=%d cc=%d total=%llu",
             inst->clocks_per_step, inst->clock_counter,
             (unsigned long long)inst->clock_tick_total);
    }

    if ((strcmp(key, "rate") == 0 || strcmp(key, "triplet") == 0 || strcmp(key, "bpm") == 0) &&
        inst->sync_mode == SYNC_INTERNAL && inst->sample_rate > 0) {
        recalc_timing(inst, inst->sample_rate);
        realign_internal_phase(inst);
        dlog(inst, "internal realign key=%s base=%.4f until=%.4f total=%llu",
             key, inst->step_interval_base_f, inst->samples_until_step_f,
             (unsigned long long)inst->internal_sample_total);
    }
}

static int superarp_get_param(void *instance, const char *key, char *buf, int buf_len) {
    superarp_instance_t *inst = (superarp_instance_t *)instance;
    const char *rate = "1/16", *triplet = inst && inst->triplet ? "on" : "off", *latch = inst && inst->latch ? "on" : "off";
    const char *sync = inst && inst->sync_mode == SYNC_CLOCK ? "clock" : "internal";
    const char *pm = "up", *miss = "fold";
    const char *pat, *rhy;
    const char *prog_trigger, *rhy_trigger, *mod_trigger;
    const char *oct_range = "+1";
    const char *glob_oct = "0";
    if (!inst || !key || !buf || buf_len < 1) return -1;
    pat = inst->progression_pattern_value[0] ? inst->progression_pattern_value : k_default_progression_pattern;
    rhy = inst->rhythm_pattern_value[0] ? inst->rhythm_pattern_value : k_default_rhythm_pattern;
    prog_trigger = inst->progression_trigger_mode == TRIG_CONTINUOUS ? "continuous" : "retrigger";
    rhy_trigger = inst->rhythm_trigger_mode == TRIG_CONTINUOUS ? "continuous" : "retrigger";
    mod_trigger = inst->modifier_trigger_mode == TRIG_CONTINUOUS ? "continuous" : "retrigger";
    if (inst->rate == RATE_1_32) rate = "1/32"; else if (inst->rate == RATE_1_8) rate = "1/8"; else if (inst->rate == RATE_1_4) rate = "1/4";
    if (inst->progression_mode == PROG_DOWN) pm = "down";
    else if (inst->progression_mode == PROG_AS_PLAYED) pm = "as_played";
    else if (inst->progression_mode == PROG_LEAP_INWARD) pm = "leap_inward";
    else if (inst->progression_mode == PROG_LEAP_OUTWARD) pm = "leap_outward";
    else if (inst->progression_mode == PROG_CHORD) pm = "chord";
    else if (inst->progression_mode == PROG_PATTERN) pm = "pattern";
    else if (inst->progression_mode == PROG_RANDOM_PATTERN) pm = "random_pattern";
    if (inst->octave_range == OCT_RANGE_P1) glob_oct = "+1";
    else if (inst->octave_range == OCT_RANGE_M1) glob_oct = "-1";
    else if (inst->octave_range == OCT_RANGE_P2) glob_oct = "+2";
    else if (inst->octave_range == OCT_RANGE_M2) glob_oct = "-2";
    if (inst->random_octave_range == OCT_RAND_P1) oct_range = "+1";
    else if (inst->random_octave_range == OCT_RAND_M1) oct_range = "-1";
    else if (inst->random_octave_range == OCT_RAND_PM1) oct_range = "+-1";
    else if (inst->random_octave_range == OCT_RAND_P2) oct_range = "+2";
    else if (inst->random_octave_range == OCT_RAND_M2) oct_range = "-2";
    else if (inst->random_octave_range == OCT_RAND_PM2) oct_range = "+-2";
    if (inst->missing_note_policy == MISSING_WRAP) miss = "wrap";
    else if (inst->missing_note_policy == MISSING_CLAMP) miss = "clamp";
    else if (inst->missing_note_policy == MISSING_SKIP) miss = "skip";

    if (strcmp(key, "rate") == 0) return snprintf(buf, buf_len, "%s", rate);
    if (strcmp(key, "bpm") == 0) return snprintf(buf, buf_len, "%d", inst->bpm);
    if (strcmp(key, "triplet") == 0) return snprintf(buf, buf_len, "%s", triplet);
    if (strcmp(key, "gate") == 0) return snprintf(buf, buf_len, "%d", inst->gate);
    if (strcmp(key, "velocity") == 0) return snprintf(buf, buf_len, "%d", inst->velocity_override);
    if (strcmp(key, "sync") == 0) return snprintf(buf, buf_len, "%s", sync);
    if (strcmp(key, "error") == 0) return superarp_get_sync_warning(inst, buf, buf_len);
    if (strcmp(key, "swing") == 0) return snprintf(buf, buf_len, "%d", inst->swing);
    if (strcmp(key, "phase_offset") == 0) return snprintf(buf, buf_len, "%d", inst->phase_offset);
    if (strcmp(key, "latch") == 0) return snprintf(buf, buf_len, "%s", latch);
    if (strcmp(key, "max_voices") == 0) return snprintf(buf, buf_len, "%d", inst->max_voices);
    if (strcmp(key, "octave_range") == 0) return snprintf(buf, buf_len, "%s", glob_oct);
    if (strcmp(key, "progression_mode") == 0) return snprintf(buf, buf_len, "%s", pm);
    if (strcmp(key, "progression_seed") == 0) return snprintf(buf, buf_len, "%d", inst->progression_seed);
    if (strcmp(key, "progression_trigger") == 0) return snprintf(buf, buf_len, "%s", prog_trigger);
    if (strcmp(key, "missing_note_policy") == 0) return snprintf(buf, buf_len, "%s", miss);
    if (strcmp(key, "pattern_preset") == 0) return snprintf(buf, buf_len, "%s", pat);
    if (strcmp(key, "random_pattern_length") == 0) return snprintf(buf, buf_len, "%d", inst->random_pattern_length);
    if (strcmp(key, "random_pattern_chords") == 0) return snprintf(buf, buf_len, "%d", inst->random_pattern_chords);
    if (strcmp(key, "random_pattern_chord_seed") == 0) return snprintf(buf, buf_len, "%d", inst->random_pattern_chord_seed);
    if (strcmp(key, "rhythm_trigger") == 0) return snprintf(buf, buf_len, "%s", rhy_trigger);
    if (strcmp(key, "rhythm_preset") == 0) return snprintf(buf, buf_len, "%s", rhy);
    if (strcmp(key, "rhythm_seed") == 0) return snprintf(buf, buf_len, "%d", inst->rhythm_seed);
    if (strcmp(key, "drop_amount") == 0) return snprintf(buf, buf_len, "%d", inst->drop_amount);
    if (strcmp(key, "drop_seed") == 0) return snprintf(buf, buf_len, "%d", inst->drop_seed);
    if (strcmp(key, "velocity_random_amount") == 0) return snprintf(buf, buf_len, "%d", inst->velocity_random_amount);
    if (strcmp(key, "velocity_seed") == 0) return snprintf(buf, buf_len, "%d", inst->velocity_seed);
    if (strcmp(key, "gate_random_amount") == 0) return snprintf(buf, buf_len, "%d", inst->gate_random_amount);
    if (strcmp(key, "gate_seed") == 0) return snprintf(buf, buf_len, "%d", inst->gate_seed);
    if (strcmp(key, "modifier_trigger") == 0) return snprintf(buf, buf_len, "%s", mod_trigger);
    if (strcmp(key, "modifier_loop_length") == 0) return snprintf(buf, buf_len, "%d", inst->modifier_loop_length);
    if (strcmp(key, "random_octave_amount") == 0) return snprintf(buf, buf_len, "%d", inst->random_octave_amount);
    if (strcmp(key, "random_octave_range") == 0) return snprintf(buf, buf_len, "%s", oct_range);
    if (strcmp(key, "random_octave_seed") == 0) return snprintf(buf, buf_len, "%d", inst->random_octave_seed);
    if (strcmp(key, "random_note_amount") == 0) return snprintf(buf, buf_len, "%d", inst->random_note_amount);
    if (strcmp(key, "random_note_seed") == 0) return snprintf(buf, buf_len, "%d", inst->random_note_seed);
    if (strcmp(key, "name") == 0) return snprintf(buf, buf_len, "Super Arp");
    if (strcmp(key, "bank_name") == 0) return snprintf(buf, buf_len, "Factory");
    if (strcmp(key, "chain_params") == 0) {
        if (inst->chain_params_len > 0) return snprintf(buf, buf_len, "%s", inst->chain_params_json);
        return -1;
    }
    if (strcmp(key, "state") == 0) {
        return snprintf(buf, buf_len,
            "{\"rate\":\"%s\",\"bpm\":%d,\"triplet\":\"%s\",\"gate\":%d,\"velocity\":%d,\"sync\":\"%s\",\"swing\":%d,\"phase_offset\":%d,\"latch\":\"%s\",\"max_voices\":%d,"
            "\"octave_range\":\"%s\",\"progression_mode\":\"%s\",\"progression_seed\":%d,\"progression_trigger\":\"%s\",\"missing_note_policy\":\"%s\","
            "\"pattern_preset\":\"%s\",\"random_pattern_length\":%d,\"random_pattern_chords\":%d,\"random_pattern_chord_seed\":%d,\"rhythm_trigger\":\"%s\",\"rhythm_preset\":\"%s\",\"rhythm_seed\":%d,"
            "\"drop_amount\":%d,\"drop_seed\":%d,\"velocity_random_amount\":%d,\"velocity_seed\":%d,"
            "\"gate_random_amount\":%d,\"gate_seed\":%d,\"modifier_trigger\":\"%s\",\"modifier_loop_length\":%d,"
            "\"random_octave_amount\":%d,\"random_octave_range\":\"%s\",\"random_octave_seed\":%d,"
            "\"random_note_amount\":%d,\"random_note_seed\":%d}",
            rate, inst->bpm, triplet, inst->gate, inst->velocity_override, sync, inst->swing, inst->phase_offset, latch, inst->max_voices,
            glob_oct, pm, inst->progression_seed, prog_trigger, miss, pat, inst->random_pattern_length, inst->random_pattern_chords, inst->random_pattern_chord_seed,
            rhy_trigger, rhy, inst->rhythm_seed, inst->drop_amount, inst->drop_seed, inst->velocity_random_amount,
            inst->velocity_seed, inst->gate_random_amount, inst->gate_seed, mod_trigger, inst->modifier_loop_length,
            inst->random_octave_amount, oct_range, inst->random_octave_seed,
            inst->random_note_amount, inst->random_note_seed
        );
    }
    return -1;
}

static midi_fx_api_v1_t g_api = {
    .api_version = MIDI_FX_API_VERSION,
    .create_instance = superarp_create_instance,
    .destroy_instance = superarp_destroy_instance,
    .process_midi = superarp_process_midi,
    .tick = superarp_tick,
    .set_param = superarp_set_param,
    .get_param = superarp_get_param
};

midi_fx_api_v1_t* move_midi_fx_init(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
