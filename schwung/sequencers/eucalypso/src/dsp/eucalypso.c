/*
 * Eucalypso MIDI FX
 *
 * First implementation pass:
 * - shared transport-anchored step counter
 * - 4 Euclidean lanes using steps/pulses/rotation
 * - held/scale note register
 * - deterministic timing independent of note input timing
 *
 * The current UI surface exposes more parameters than this DSP uses.
 * Unused parameters are still stored and serialized so the module remains
 * compatible with the UI while the lane engine is built out.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "host/midi_fx_api_v1.h"
#include "host/plugin_api_v1.h"

#define MAX_LANES 4
#define MAX_HELD_NOTES 16
#define MAX_REGISTER_NOTES 24
#define MAX_VOICES 64
#define DEFAULT_BPM 120
#define DEFAULT_SAMPLE_RATE 44100
#define SCALE_BASE_NOTE 60
#define DRUMPAD_BASE_NOTE 36
#define DRUMPAD_COUNT 16
#define CLOCK_START_GRACE_TICKS 2
#define EUCALYPSO_DEBUG_LOG 1
#define EUCALYPSO_LOG_PATH "/data/UserData/schwung/eucalypso.log"

typedef enum {
    PLAY_HOLD = 0,
    PLAY_LATCH
} play_mode_t;

typedef enum {
    RETRIG_RESTART = 0,
    RETRIG_CONT
} retrigger_mode_t;

typedef enum {
    SYNC_INTERNAL = 0,
    SYNC_CLOCK
} sync_mode_t;

typedef enum {
    RATE_1_32 = 0,
    RATE_1_16T,
    RATE_1_16,
    RATE_1_8T,
    RATE_1_8,
    RATE_1_4T,
    RATE_1_4,
    RATE_1_2,
    RATE_1_1
} rate_t;

typedef enum {
    REGISTER_HELD = 0,
    REGISTER_SCALE,
    REGISTER_DRUMPAD
} register_mode_t;

typedef enum {
    HELD_UP = 0,
    HELD_DOWN,
    HELD_PLAYED,
    HELD_RAND
} held_order_t;

typedef enum {
    MISSING_SKIP = 0,
    MISSING_FOLD,
    MISSING_WRAP,
    MISSING_RANDOM
} missing_note_policy_t;

typedef enum {
    SCALE_MAJOR = 0,
    SCALE_NATURAL_MINOR,
    SCALE_HARMONIC_MINOR,
    SCALE_MELODIC_MINOR,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_LOCRIAN,
    SCALE_PENTATONIC_MAJOR,
    SCALE_PENTATONIC_MINOR,
    SCALE_BLUES,
    SCALE_WHOLE_TONE,
    SCALE_CHROMATIC
} scale_mode_t;

typedef struct {
    int enabled;
    int steps;
    int pulses;
    int rotation;
    int drop;
    int drop_seed;
    int note;
    int n_rnd;
    int n_seed;
    int octave;
    int oct_rnd;
    int oct_seed;
    int oct_rng;
    int velocity;
    int gate;
} lane_t;

typedef struct {
    play_mode_t play_mode;
    retrigger_mode_t retrigger_mode;
    rate_t rate;
    sync_mode_t sync_mode;
    int bpm;
    int swing;
    int max_voices;
    int global_velocity;
    int global_v_rnd;
    int global_gate;
    int global_g_rnd;
    int global_rnd_seed;
    int rand_cycle;
    register_mode_t register_mode;
    held_order_t held_order;
    int held_order_seed;
    scale_mode_t scale_mode;
    int scale_rng;
    int root_note;
    int octave;
    missing_note_policy_t missing_note_policy;
    int missing_note_seed;
    lane_t lanes[MAX_LANES];

    uint8_t physical_notes[MAX_HELD_NOTES];
    int physical_count;
    uint8_t physical_as_played[MAX_HELD_NOTES];
    int physical_as_played_count;
    uint8_t active_notes[MAX_HELD_NOTES];
    int active_count;
    uint8_t active_as_played[MAX_HELD_NOTES];
    int active_as_played_count;
    int latch_ready_replace;

    int sample_rate;
    int timing_dirty;
    int step_interval_base;
    int samples_until_step;
    double step_interval_base_f;
    double samples_until_step_f;
    uint64_t internal_sample_total;
    int swing_phase;

    int clock_counter;
    int clocks_per_step;
    int clock_running;
    int midi_transport_started;
    int suppress_initial_note_restart;
    int clock_start_grace_armed;
    int internal_start_grace_armed;
    uint64_t clock_tick_total;
    int pending_step_triggers;

    uint64_t anchor_step;
    uint64_t phrase_anchor_step;
    int phrase_restart_pending;
    int preview_step_pending;
    uint64_t preview_step_id;

    uint8_t voice_notes[MAX_VOICES];
    int voice_clock_left[MAX_VOICES];
    int voice_sample_left[MAX_VOICES];
    int voice_count;

    FILE *debug_fp;
    uint64_t debug_seq;

    char chain_params_json[65536];
    int chain_params_len;
} eucalypso_instance_t;

typedef struct {
    const int *intervals;
    int count;
} scale_def_t;

static const host_api_v1_t *g_host = NULL;

static const int k_scale_major[] = { 0, 2, 4, 5, 7, 9, 11 };
static const int k_scale_natural_minor[] = { 0, 2, 3, 5, 7, 8, 10 };
static const int k_scale_harmonic_minor[] = { 0, 2, 3, 5, 7, 8, 11 };
static const int k_scale_melodic_minor[] = { 0, 2, 3, 5, 7, 9, 11 };
static const int k_scale_dorian[] = { 0, 2, 3, 5, 7, 9, 10 };
static const int k_scale_phrygian[] = { 0, 1, 3, 5, 7, 8, 10 };
static const int k_scale_lydian[] = { 0, 2, 4, 6, 7, 9, 11 };
static const int k_scale_mixolydian[] = { 0, 2, 4, 5, 7, 9, 10 };
static const int k_scale_locrian[] = { 0, 1, 3, 5, 6, 8, 10 };
static const int k_scale_pentatonic_major[] = { 0, 2, 4, 7, 9 };
static const int k_scale_pentatonic_minor[] = { 0, 3, 5, 7, 10 };
static const int k_scale_blues[] = { 0, 3, 5, 6, 7, 10 };
static const int k_scale_whole_tone[] = { 0, 2, 4, 6, 8, 10 };
static const int k_scale_chromatic[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static int eucalypso_query_clock_status(const eucalypso_instance_t *inst) {
    if (g_host && g_host->get_clock_status) {
        return g_host->get_clock_status();
    }

    if (inst && inst->clock_running) {
        return MOVE_CLOCK_STATUS_RUNNING;
    }
    return MOVE_CLOCK_STATUS_STOPPED;
}

static int eucalypso_get_sync_warning(eucalypso_instance_t *inst, char *buf, int buf_len) {
    int status;

    if (!inst || !buf || buf_len < 1) return -1;
    if (inst->sync_mode != SYNC_CLOCK) {
        buf[0] = '\0';
        return 0;
    }

    status = eucalypso_query_clock_status(inst);
    if (status == MOVE_CLOCK_STATUS_UNAVAILABLE) {
        return snprintf(buf, buf_len, "Enable MIDI Clock Out in Move settings");
    }
    if (status == MOVE_CLOCK_STATUS_STOPPED) {
        return snprintf(buf, buf_len, "Clock out enabled, transport stopped");
    }

    buf[0] = '\0';
    return 0;
}

#if EUCALYPSO_DEBUG_LOG
static void dlog(eucalypso_instance_t *inst, const char *fmt, ...) {
    va_list ap;
    if (!inst) return;
    if (!inst->debug_fp) {
        inst->debug_fp = fopen(EUCALYPSO_LOG_PATH, "a");
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
static void dlog(eucalypso_instance_t *inst, const char *fmt, ...) {
    (void)inst;
    (void)fmt;
}
#endif

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint32_t mix_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint32_t next_u32(uint32_t *state) {
    *state = mix_u32(*state + 0x9e3779b9U);
    return *state;
}

static uint32_t step_rand_u32(uint32_t seed, uint64_t step, uint32_t salt) {
    uint32_t lo = (uint32_t)(step & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)((step >> 32) & 0xFFFFFFFFu);
    uint32_t s = seed ? seed : 1u;
    return mix_u32(s ^ lo ^ mix_u32(hi ^ salt) ^ salt);
}

static int rand_offset_signed(uint32_t r, int amount) {
    int span;
    if (amount <= 0) return 0;
    span = amount * 2 + 1;
    return (int)(r % (uint32_t)span) - amount;
}

static int chance_hit(uint32_t r, int pct) {
    pct = clamp_int(pct, 0, 100);
    if (pct <= 0) return 0;
    if (pct >= 100) return 1;
    return (int)(r % 100u) < pct;
}

static uint64_t rand_cycle_step(const eucalypso_instance_t *inst, uint64_t rhythm_step) {
    int cycle;
    if (!inst) return rhythm_step;
    cycle = clamp_int(inst->rand_cycle, 1, 128);
    return rhythm_step % (uint64_t)cycle;
}

static uint32_t global_lane_seed(const eucalypso_instance_t *inst, int lane_idx, uint32_t offset) {
    uint32_t seed = 1u;
    if (inst) seed = (uint32_t)(inst->global_rnd_seed + 1);
    return seed + (uint32_t)((lane_idx + 1) * 1000) + offset;
}

static uint32_t missing_note_seed(const eucalypso_instance_t *inst, int lane_idx) {
    uint32_t seed = 1u;
    if (inst) seed = (uint32_t)(inst->missing_note_seed + 1);
    return seed + (uint32_t)((lane_idx + 1) * 1000) + 0x6000u;
}

static uint32_t active_note_hash(const eucalypso_instance_t *inst) {
    uint32_t h = 2166136261u;
    int i;
    if (!inst) return h;
    for (i = 0; i < inst->active_count; i++) {
        h ^= (uint32_t)inst->active_notes[i];
        h *= 16777619u;
    }
    return h;
}

static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    char search[64];
    const char *pos;
    const char *colon;
    const char *end;
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
    const char *pos;
    const char *colon;
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

static int emit3(uint8_t out_msgs[][3], int out_lens[], int max_out, int *count,
                 uint8_t s, uint8_t d1, uint8_t d2) {
    if (!out_msgs || !out_lens || !count || *count >= max_out) return 0;
    out_msgs[*count][0] = s;
    out_msgs[*count][1] = d1;
    out_msgs[*count][2] = d2;
    out_lens[*count] = 3;
    (*count)++;
    return 1;
}

static int appendf(char *buf, int buf_len, int *pos, const char *fmt, ...) {
    va_list ap;
    int wrote;
    if (!buf || !pos || *pos >= buf_len) return 0;
    va_start(ap, fmt);
    wrote = vsnprintf(buf + *pos, (size_t)(buf_len - *pos), fmt, ap);
    va_end(ap);
    if (wrote < 0) return 0;
    if (*pos + wrote >= buf_len) {
        *pos = buf_len;
        return 0;
    }
    *pos += wrote;
    return 1;
}

static int arr_contains(const uint8_t *arr, int count, uint8_t note) {
    int i;
    for (i = 0; i < count; i++) {
        if (arr[i] == note) return 1;
    }
    return 0;
}

static void arr_add_sorted(uint8_t *arr, int *count, uint8_t note) {
    int i;
    int j;
    if (!arr || !count || *count >= MAX_HELD_NOTES) return;
    for (i = 0; i < *count; i++) {
        if (arr[i] == note) return;
        if (arr[i] > note) break;
    }
    for (j = *count; j > i; j--) arr[j] = arr[j - 1];
    arr[i] = note;
    (*count)++;
}

static void arr_add_tail_unique(uint8_t *arr, int *count, uint8_t note) {
    if (!arr || !count || *count >= MAX_HELD_NOTES) return;
    if (arr_contains(arr, *count, note)) return;
    arr[*count] = note;
    (*count)++;
}

static void arr_remove(uint8_t *arr, int *count, uint8_t note) {
    int i;
    int found = -1;
    if (!arr || !count) return;
    for (i = 0; i < *count; i++) {
        if (arr[i] == note) {
            found = i;
            break;
        }
    }
    if (found < 0) return;
    for (i = found; i < *count - 1; i++) arr[i] = arr[i + 1];
    (*count)--;
}

static void clear_active(eucalypso_instance_t *inst) {
    if (!inst) return;
    inst->active_count = 0;
    inst->active_as_played_count = 0;
}

static void sync_active_to_physical(eucalypso_instance_t *inst) {
    int i;
    if (!inst) return;
    clear_active(inst);
    for (i = 0; i < inst->physical_count; i++) {
        arr_add_sorted(inst->active_notes, &inst->active_count, inst->physical_notes[i]);
    }
    for (i = 0; i < inst->physical_as_played_count; i++) {
        uint8_t note = inst->physical_as_played[i];
        if (arr_contains(inst->active_notes, inst->active_count, note)) {
            arr_add_tail_unique(inst->active_as_played, &inst->active_as_played_count, note);
        }
    }
}

static void set_play_mode(eucalypso_instance_t *inst, play_mode_t mode) {
    if (!inst || inst->play_mode == mode) return;
    inst->play_mode = mode;
    if (mode == PLAY_HOLD) {
        inst->latch_ready_replace = 0;
        sync_active_to_physical(inst);
    } else {
        if (inst->physical_count > 0) {
            sync_active_to_physical(inst);
            inst->latch_ready_replace = 0;
        } else {
            inst->latch_ready_replace = 1;
        }
    }
}

static void note_on(eucalypso_instance_t *inst, uint8_t note) {
    int replacing_latched_set;
    if (!inst) return;
    replacing_latched_set = (inst->play_mode == PLAY_LATCH && inst->latch_ready_replace) ? 1 : 0;
    arr_add_sorted(inst->physical_notes, &inst->physical_count, note);
    arr_add_tail_unique(inst->physical_as_played, &inst->physical_as_played_count, note);
    if (inst->play_mode == PLAY_LATCH) {
        if (inst->latch_ready_replace) {
            clear_active(inst);
            inst->latch_ready_replace = 0;
        }
        arr_add_sorted(inst->active_notes, &inst->active_count, note);
        arr_add_tail_unique(inst->active_as_played, &inst->active_as_played_count, note);
        if (replacing_latched_set &&
            inst->retrigger_mode == RETRIG_RESTART &&
            inst->active_count > 0) {
            inst->phrase_restart_pending = 1;
            dlog(inst, "phrase restart armed latch-replace anchor=%llu",
                 (unsigned long long)inst->anchor_step);
        }
    } else {
        sync_active_to_physical(inst);
    }
}

static void note_off(eucalypso_instance_t *inst, uint8_t note) {
    if (!inst) return;
    arr_remove(inst->physical_notes, &inst->physical_count, note);
    arr_remove(inst->physical_as_played, &inst->physical_as_played_count, note);
    if (inst->play_mode == PLAY_LATCH) {
        if (inst->physical_count == 0) inst->latch_ready_replace = 1;
    } else {
        sync_active_to_physical(inst);
    }
}

static uint64_t rhythm_step_id(const eucalypso_instance_t *inst, uint64_t anchor_step) {
    if (!inst) return anchor_step;
    if (inst->retrigger_mode == RETRIG_RESTART) {
        if (anchor_step >= inst->phrase_anchor_step) return anchor_step - inst->phrase_anchor_step;
        return 0;
    }
    return anchor_step;
}

static scale_def_t get_scale_def(scale_mode_t mode) {
    switch (mode) {
        case SCALE_NATURAL_MINOR: return (scale_def_t){ k_scale_natural_minor, 7 };
        case SCALE_HARMONIC_MINOR: return (scale_def_t){ k_scale_harmonic_minor, 7 };
        case SCALE_MELODIC_MINOR: return (scale_def_t){ k_scale_melodic_minor, 7 };
        case SCALE_DORIAN: return (scale_def_t){ k_scale_dorian, 7 };
        case SCALE_PHRYGIAN: return (scale_def_t){ k_scale_phrygian, 7 };
        case SCALE_LYDIAN: return (scale_def_t){ k_scale_lydian, 7 };
        case SCALE_MIXOLYDIAN: return (scale_def_t){ k_scale_mixolydian, 7 };
        case SCALE_LOCRIAN: return (scale_def_t){ k_scale_locrian, 7 };
        case SCALE_PENTATONIC_MAJOR: return (scale_def_t){ k_scale_pentatonic_major, 5 };
        case SCALE_PENTATONIC_MINOR: return (scale_def_t){ k_scale_pentatonic_minor, 5 };
        case SCALE_BLUES: return (scale_def_t){ k_scale_blues, 6 };
        case SCALE_WHOLE_TONE: return (scale_def_t){ k_scale_whole_tone, 6 };
        case SCALE_CHROMATIC: return (scale_def_t){ k_scale_chromatic, 12 };
        case SCALE_MAJOR:
        default: return (scale_def_t){ k_scale_major, 7 };
    }
}

static int build_scale_register(const eucalypso_instance_t *inst, int *notes, int max_notes) {
    int i;
    int count;
    int base;
    scale_def_t scale;
    if (!inst || !notes || max_notes <= 0) return 0;
    scale = get_scale_def(inst->scale_mode);
    count = clamp_int(inst->scale_rng, 1, MAX_REGISTER_NOTES);
    if (count > max_notes) count = max_notes;
    base = SCALE_BASE_NOTE + clamp_int(inst->root_note, 0, 11);
    for (i = 0; i < count; i++) {
        int degree = i % scale.count;
        int oct = i / scale.count;
        notes[i] = clamp_int(base + scale.intervals[degree] + oct * 12, 0, 127);
    }
    return count;
}

static int build_drumpad_register(const eucalypso_instance_t *inst, int *notes, int max_notes) {
    int i;
    int count;
    if (!inst || !notes || max_notes <= 0) return 0;
    count = clamp_int(inst->scale_rng, 1, DRUMPAD_COUNT);
    if (count > max_notes) count = max_notes;
    for (i = 0; i < count; i++) {
        notes[i] = DRUMPAD_BASE_NOTE + i;
    }
    return count;
}

static void shuffle_notes(int *notes, int count, uint32_t seed) {
    int i;
    uint32_t state = seed ? seed : 1u;
    for (i = count - 1; i > 0; i--) {
        int j = (int)(next_u32(&state) % (uint32_t)(i + 1));
        int tmp = notes[i];
        notes[i] = notes[j];
        notes[j] = tmp;
    }
}

static int build_held_register(const eucalypso_instance_t *inst, int *notes, int max_notes) {
    int i;
    int count;
    if (!inst || !notes || max_notes <= 0) return 0;
    count = inst->active_count;
    if (count > max_notes) count = max_notes;
    if (count <= 0) return 0;

    if (inst->held_order == HELD_PLAYED && inst->active_as_played_count > 0) {
        int out = 0;
        for (i = 0; i < inst->active_as_played_count && out < count; i++) {
            uint8_t note = inst->active_as_played[i];
            if (arr_contains(inst->active_notes, inst->active_count, note)) {
                notes[out++] = note;
            }
        }
        return out;
    }

    if (inst->held_order == HELD_DOWN) {
        for (i = 0; i < count; i++) {
            notes[i] = inst->active_notes[count - 1 - i];
        }
        return count;
    }

    for (i = 0; i < count; i++) notes[i] = inst->active_notes[i];
    if (inst->held_order == HELD_RAND) {
        shuffle_notes(notes, count, (uint32_t)inst->held_order_seed ^ active_note_hash(inst));
    }
    return count;
}

static int build_register(const eucalypso_instance_t *inst, int *notes, int max_notes) {
    if (!inst || !notes || max_notes <= 0) return 0;
    if (inst->register_mode == REGISTER_DRUMPAD) {
        return build_drumpad_register(inst, notes, max_notes);
    }
    if (inst->register_mode == REGISTER_SCALE) {
        return build_scale_register(inst, notes, max_notes);
    }
    return build_held_register(inst, notes, max_notes);
}

static int lane_gate_enabled_for_step(const eucalypso_instance_t *inst, int lane_idx) {
    int gate_note;
    if (!inst) return 0;
    if (inst->register_mode != REGISTER_DRUMPAD) return 1;
    gate_note = DRUMPAD_BASE_NOTE + clamp_int(lane_idx, 0, MAX_LANES - 1);
    return arr_contains(inst->active_notes, inst->active_count, (uint8_t)gate_note);
}

static int octave_offset_count(int oct_rng) {
    switch (clamp_int(oct_rng, 0, 5)) {
        case 0:
        case 1:
            return 2;
        case 2:
            return 3;
        case 3:
        case 4:
            return 3;
        case 5:
        default:
            return 5;
    }
}

static int octave_offset_value(int oct_rng, int idx) {
    static const int k_offsets_p1[] = { 0, 1 };
    static const int k_offsets_m1[] = { -1, 0 };
    static const int k_offsets_pm1[] = { -1, 0, 1 };
    static const int k_offsets_p2[] = { 0, 1, 2 };
    static const int k_offsets_m2[] = { -2, -1, 0 };
    static const int k_offsets_pm2[] = { -2, -1, 0, 1, 2 };
    const int *offsets = k_offsets_pm1;
    int count = octave_offset_count(oct_rng);
    switch (clamp_int(oct_rng, 0, 5)) {
        case 0: offsets = k_offsets_p1; break;
        case 1: offsets = k_offsets_m1; break;
        case 2: offsets = k_offsets_pm1; break;
        case 3: offsets = k_offsets_p2; break;
        case 4: offsets = k_offsets_m2; break;
        case 5:
        default: offsets = k_offsets_pm2; break;
    }
    idx = clamp_int(idx, 0, count - 1);
    return offsets[idx];
}

static int fold_index(int idx, int count) {
    int period;
    if (count <= 1) return 0;
    period = (count - 1) * 2;
    idx %= period;
    if (idx < 0) idx += period;
    if (idx >= count) idx = period - idx;
    return idx;
}

static int resolve_register_index(const eucalypso_instance_t *inst, int lane_idx,
                                  int requested_idx, int reg_count, uint64_t rhythm_step) {
    uint64_t cycle_step;
    if (reg_count <= 0) return -1;
    if (requested_idx >= 0 && requested_idx < reg_count) return requested_idx;
    switch (inst ? inst->missing_note_policy : MISSING_SKIP) {
        case MISSING_FOLD:
            return fold_index(requested_idx, reg_count);
        case MISSING_WRAP: {
            int idx = requested_idx % reg_count;
            if (idx < 0) idx += reg_count;
            return idx;
        }
        case MISSING_RANDOM: {
            uint32_t r;
            cycle_step = rand_cycle_step(inst, rhythm_step);
            r = step_rand_u32(missing_note_seed(inst, lane_idx), cycle_step, 0x6000u);
            return (int)(r % (uint32_t)reg_count);
        }
        case MISSING_SKIP:
        default:
            return -1;
    }
}

static int select_lane_note(const eucalypso_instance_t *inst, const lane_t *lane,
                            int lane_idx, uint64_t rhythm_step) {
    int register_notes[MAX_REGISTER_NOTES];
    int reg_count;
    int idx;
    int base_idx;
    int note;
    uint64_t cycle_step;
    if (!inst || !lane) return -1;
    reg_count = build_register(inst, register_notes, MAX_REGISTER_NOTES);
    if (reg_count <= 0) return -1;
    cycle_step = rand_cycle_step(inst, rhythm_step);
    base_idx = clamp_int(lane->note, 1, MAX_REGISTER_NOTES) - 1;
    base_idx = resolve_register_index(inst, lane_idx, base_idx, reg_count, rhythm_step);
    if (base_idx < 0) return -1;
    idx = base_idx;
    if (lane->n_rnd > 0 && reg_count > 1) {
        uint32_t r = step_rand_u32((uint32_t)(lane->n_seed + 1), cycle_step, 0x2000u + (uint32_t)lane_idx);
        if (chance_hit(r, lane->n_rnd)) {
            idx = (int)((r >> 8) % (uint32_t)(reg_count - 1));
            if (idx >= base_idx) idx++;
        }
    }
    note = register_notes[idx];
    note += clamp_int(inst->octave, -3, 3) * 12;
    note += clamp_int(lane->octave, -3, 3) * 12;
    if (lane->oct_rnd > 0) {
        uint32_t r = step_rand_u32((uint32_t)(lane->oct_seed + 1), cycle_step, 0x3000u + (uint32_t)lane_idx);
        if (chance_hit(r, lane->oct_rnd)) {
            int count = octave_offset_count(lane->oct_rng);
            int pick = (int)((r >> 8) % (uint32_t)count);
            note += octave_offset_value(lane->oct_rng, pick) * 12;
        }
    }
    return clamp_int(note, 0, 127);
}

static const char *rate_to_string(rate_t rate) {
    switch (rate) {
        case RATE_1_32: return "1/32";
        case RATE_1_16T: return "1/16T";
        case RATE_1_16: return "1/16";
        case RATE_1_8T: return "1/8T";
        case RATE_1_8: return "1/8";
        case RATE_1_4T: return "1/4T";
        case RATE_1_4: return "1/4";
        case RATE_1_2: return "1/2";
        case RATE_1_1:
        default: return "1";
    }
}

static const char *sync_to_string(sync_mode_t mode) {
    return mode == SYNC_CLOCK ? "clock" : "internal";
}

static const char *play_mode_to_string(play_mode_t mode) {
    return mode == PLAY_LATCH ? "latch" : "hold";
}

static const char *retrigger_to_string(retrigger_mode_t mode) {
    return mode == RETRIG_CONT ? "cont" : "restart";
}

static const char *register_mode_to_string(register_mode_t mode) {
    if (mode == REGISTER_SCALE) return "scale";
    if (mode == REGISTER_DRUMPAD) return "drumpad";
    return "held";
}

static const char *held_order_to_string(held_order_t mode) {
    switch (mode) {
        case HELD_DOWN: return "down";
        case HELD_PLAYED: return "played";
        case HELD_RAND: return "rand";
        case HELD_UP:
        default: return "up";
    }
}

static const char *missing_note_policy_to_string(missing_note_policy_t mode) {
    switch (mode) {
        case MISSING_FOLD: return "fold";
        case MISSING_WRAP: return "wrap";
        case MISSING_RANDOM: return "random";
        case MISSING_SKIP:
        default: return "skip";
    }
}

static const char *scale_mode_to_string(scale_mode_t mode) {
    switch (mode) {
        case SCALE_NATURAL_MINOR: return "natural_minor";
        case SCALE_HARMONIC_MINOR: return "harmonic_minor";
        case SCALE_MELODIC_MINOR: return "melodic_minor";
        case SCALE_DORIAN: return "dorian";
        case SCALE_PHRYGIAN: return "phrygian";
        case SCALE_LYDIAN: return "lydian";
        case SCALE_MIXOLYDIAN: return "mixolydian";
        case SCALE_LOCRIAN: return "locrian";
        case SCALE_PENTATONIC_MAJOR: return "pentatonic_major";
        case SCALE_PENTATONIC_MINOR: return "pentatonic_minor";
        case SCALE_BLUES: return "blues";
        case SCALE_WHOLE_TONE: return "whole_tone";
        case SCALE_CHROMATIC: return "chromatic";
        case SCALE_MAJOR:
        default: return "major";
    }
}

static rate_t parse_rate(const char *val) {
    if (!val) return RATE_1_16;
    if (strcmp(val, "1/32") == 0) return RATE_1_32;
    if (strcmp(val, "1/16T") == 0) return RATE_1_16T;
    if (strcmp(val, "1/8T") == 0) return RATE_1_8T;
    if (strcmp(val, "1/8") == 0) return RATE_1_8;
    if (strcmp(val, "1/4T") == 0) return RATE_1_4T;
    if (strcmp(val, "1/4") == 0) return RATE_1_4;
    if (strcmp(val, "1/2") == 0) return RATE_1_2;
    if (strcmp(val, "1") == 0) return RATE_1_1;
    return RATE_1_16;
}

static double rate_notes_per_beat(rate_t rate) {
    switch (rate) {
        case RATE_1_32: return 8.0;
        case RATE_1_16T: return 6.0;
        case RATE_1_16: return 4.0;
        case RATE_1_8T: return 3.0;
        case RATE_1_8: return 2.0;
        case RATE_1_4T: return 1.5;
        case RATE_1_4: return 1.0;
        case RATE_1_2: return 0.5;
        case RATE_1_1:
        default: return 0.25;
    }
}

static void recalc_clock_timing(eucalypso_instance_t *inst) {
    double npb;
    int clocks;
    if (!inst) return;
    npb = rate_notes_per_beat(inst->rate);
    if (npb <= 0.0) npb = 4.0;
    clocks = (int)(24.0 / npb + 0.5);
    if (clocks < 1) clocks = 1;
    inst->clocks_per_step = clocks;
}

static void recalc_internal_timing(eucalypso_instance_t *inst, int sample_rate) {
    double npb;
    double step_samples;
    if (!inst || sample_rate <= 0) return;
    inst->bpm = clamp_int(inst->bpm, 40, 240);
    npb = rate_notes_per_beat(inst->rate);
    if (npb <= 0.0) npb = 4.0;
    step_samples = ((double)sample_rate * 60.0) / ((double)inst->bpm * npb);
    if (step_samples < 1.0) step_samples = 1.0;
    inst->sample_rate = sample_rate;
    inst->step_interval_base_f = step_samples;
    inst->step_interval_base = (int)(step_samples + 0.5);
    if (inst->step_interval_base < 1) inst->step_interval_base = 1;
    if (inst->samples_until_step_f <= 0.0 || inst->samples_until_step_f > inst->step_interval_base_f) {
        inst->samples_until_step_f = inst->step_interval_base_f;
    }
    inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
    if (inst->samples_until_step < 1) inst->samples_until_step = 1;
    inst->timing_dirty = 0;
}

static double next_internal_interval(eucalypso_instance_t *inst) {
    double base;
    double delta;
    int swing;
    if (!inst) return 1.0;
    base = inst->step_interval_base_f > 0.0 ? inst->step_interval_base_f : 1.0;
    swing = clamp_int(inst->swing, 0, 100);
    if (swing <= 0) return base;
    delta = (base * (double)swing) / 200.0;
    if (inst->swing_phase == 0) {
        inst->swing_phase = 1;
        return base + delta;
    }
    inst->swing_phase = 0;
    if (base - delta < 1.0) return 1.0;
    return base - delta;
}

static void realign_clock_phase(eucalypso_instance_t *inst) {
    if (!inst) return;
    if (inst->clocks_per_step < 1) inst->clocks_per_step = 1;
    inst->pending_step_triggers = 0;
}

static void realign_internal_phase(eucalypso_instance_t *inst) {
    double interval;
    double total;
    double rem;
    double until_next;
    if (!inst) return;
    interval = inst->step_interval_base_f > 0.0 ? inst->step_interval_base_f : 1.0;
    total = (double)inst->internal_sample_total;
    rem = total;
    while (rem >= interval) rem -= interval;
    while (rem < 0.0) rem += interval;
    until_next = rem < 1e-9 ? interval : interval - rem;
    if (until_next < 1.0) until_next = 1.0;
    inst->samples_until_step_f = until_next;
    inst->samples_until_step = (int)(until_next + 0.5);
    if (inst->samples_until_step < 1) inst->samples_until_step = 1;
    inst->swing_phase = 0;
}

static void voice_remove_at(eucalypso_instance_t *inst, int idx) {
    int i;
    if (!inst || idx < 0 || idx >= inst->voice_count) return;
    for (i = idx; i < inst->voice_count - 1; i++) {
        inst->voice_notes[i] = inst->voice_notes[i + 1];
        inst->voice_clock_left[i] = inst->voice_clock_left[i + 1];
        inst->voice_sample_left[i] = inst->voice_sample_left[i + 1];
    }
    inst->voice_count--;
}

static int voice_note_off(eucalypso_instance_t *inst, int idx,
                          uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    uint8_t note;
    if (!inst || idx < 0 || idx >= inst->voice_count) return 0;
    note = inst->voice_notes[idx];
    if (!emit3(out_msgs, out_lens, max_out, count, 0x80, note, 0)) return 0;
    voice_remove_at(inst, idx);
    return 1;
}

static int flush_all_voices(eucalypso_instance_t *inst,
                            uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int emitted = 0;
    if (!inst || !count) return 0;
    while (inst->voice_count > 0) {
        if (!voice_note_off(inst, 0, out_msgs, out_lens, max_out, count)) break;
        emitted++;
    }
    return emitted;
}

static int kill_voice_notes(eucalypso_instance_t *inst, uint8_t note,
                            uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int i = 0;
    int killed = 0;
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

static void voice_add(eucalypso_instance_t *inst, uint8_t note, int gate_pct) {
    int idx;
    if (!inst || inst->voice_count >= MAX_VOICES) return;
    idx = inst->voice_count++;
    inst->voice_notes[idx] = note;
    inst->voice_clock_left[idx] = 0;
    inst->voice_sample_left[idx] = 0;
    gate_pct = clamp_int(gate_pct, 0, 1600);
    if (inst->sync_mode == SYNC_CLOCK) {
        int clocks = (inst->clocks_per_step * gate_pct) / 100;
        if (clocks < 1) clocks = 1;
        inst->voice_clock_left[idx] = clocks;
    } else {
        int samples = (inst->step_interval_base * gate_pct) / 100;
        if (samples < 1) samples = 1;
        inst->voice_sample_left[idx] = samples;
    }
}

static int advance_voice_timers_clock(eucalypso_instance_t *inst,
                                      uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int i = 0;
    int emitted = 0;
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

static int advance_voice_timers_samples(eucalypso_instance_t *inst, int frames,
                                        uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int i = 0;
    int emitted = 0;
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

static int schedule_note(eucalypso_instance_t *inst, int note, int velocity, int gate_pct,
                         uint8_t out_msgs[][3], int out_lens[], int max_out, int *count) {
    int voice_limit;
    uint8_t out_note;
    if (!inst || !count) return 0;
    out_note = (uint8_t)clamp_int(note, 0, 127);
    velocity = clamp_int(velocity, 1, 127);
    gate_pct = clamp_int(gate_pct, 0, 1600);
    voice_limit = clamp_int(inst->max_voices, 1, MAX_VOICES);

    (void)kill_voice_notes(inst, out_note, out_msgs, out_lens, max_out, count);
    while (inst->voice_count >= voice_limit) {
        if (!voice_note_off(inst, 0, out_msgs, out_lens, max_out, count)) return 0;
    }
    if (!emit3(out_msgs, out_lens, max_out, count, 0x90, out_note, (uint8_t)velocity)) return 0;
    if (gate_pct <= 0) {
        return emit3(out_msgs, out_lens, max_out, count, 0x80, out_note, 0);
    }
    voice_add(inst, out_note, gate_pct);
    return 1;
}

static int euclidean_trigger(uint64_t anchor_step, int steps, int pulses, int rotation) {
    int pos;
    if (steps <= 0) return 0;
    pulses = clamp_int(pulses, 0, steps);
    if (pulses <= 0) return 0;
    if (pulses >= steps) return 1;
    pos = (int)(anchor_step % (uint64_t)steps);
    rotation %= steps;
    if (rotation < 0) rotation += steps;
    pos = (pos + rotation) % steps;
    return ((pos * pulses) % steps) < pulses;
}

static int lane_velocity(const eucalypso_instance_t *inst, const lane_t *lane,
                         int lane_idx, uint64_t rhythm_step) {
    int velocity;
    if (!inst || !lane) return 100;
    velocity = lane->velocity > 0 ? lane->velocity : inst->global_velocity;
    velocity = clamp_int(velocity, 1, 127);
    if (inst->global_v_rnd > 0) {
        uint64_t cycle_step = rand_cycle_step(inst, rhythm_step);
        uint32_t r = step_rand_u32(global_lane_seed(inst, lane_idx, 0x4000u), cycle_step, 0x4000u);
        velocity += rand_offset_signed(r, inst->global_v_rnd);
    }
    return clamp_int(velocity, 1, 127);
}

static int lane_gate(const eucalypso_instance_t *inst, const lane_t *lane,
                     int lane_idx, uint64_t rhythm_step) {
    int gate;
    if (!inst || !lane) return 100;
    gate = lane->gate > 0 ? lane->gate : inst->global_gate;
    gate = clamp_int(gate, 0, 1600);
    if (inst->global_g_rnd > 0) {
        uint64_t cycle_step = rand_cycle_step(inst, rhythm_step);
        uint32_t r = step_rand_u32(global_lane_seed(inst, lane_idx, 0x5000u), cycle_step, 0x5000u);
        gate += rand_offset_signed(r, inst->global_g_rnd);
    }
    return clamp_int(gate, 0, 1600);
}

static int lane_should_drop(const eucalypso_instance_t *inst, const lane_t *lane,
                            int lane_idx, uint64_t rhythm_step) {
    uint32_t r;
    if (!inst || !lane) return 0;
    if (lane->drop <= 0) return 0;
    r = step_rand_u32((uint32_t)(lane->drop_seed + 1), rand_cycle_step(inst, rhythm_step),
                      0x1000u + (uint32_t)lane_idx);
    return chance_hit(r, lane->drop);
}

static int emit_anchor_step(eucalypso_instance_t *inst, uint64_t step_id,
                            uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int count = 0;
    int lane_idx;
    uint64_t rhythm_step;
    if (!inst || max_out < 1) return 0;

    if (inst->active_count <= 0) {
        dlog(inst, "emit_anchor_step skip step=%llu reason=no_active_notes", (unsigned long long)step_id);
        return 0;
    }
    rhythm_step = rhythm_step_id(inst, step_id);
    dlog(inst, "emit_anchor_step start step=%llu rhythm_step=%llu active=%d pending=%d",
         (unsigned long long)step_id, (unsigned long long)rhythm_step,
         inst->active_count, inst->pending_step_triggers);
    for (lane_idx = 0; lane_idx < MAX_LANES && count < max_out; lane_idx++) {
        lane_t *lane = &inst->lanes[lane_idx];
        int note;
        if (!lane->enabled) continue;
        if (!lane_gate_enabled_for_step(inst, lane_idx)) continue;
        if (!euclidean_trigger(rhythm_step, clamp_int(lane->steps, 1, 128),
                               clamp_int(lane->pulses, 0, 128),
                               lane->rotation)) {
            continue;
        }
        if (lane_should_drop(inst, lane, lane_idx, rhythm_step)) {
            dlog(inst, "emit_anchor_step lane=%d dropped step=%llu rhythm_step=%llu",
                 lane_idx + 1, (unsigned long long)step_id, (unsigned long long)rhythm_step);
            continue;
        }
        note = select_lane_note(inst, lane, lane_idx, rhythm_step);
        if (note < 0) continue;
        dlog(inst, "emit_anchor_step lane=%d note=%d step=%llu rhythm_step=%llu",
             lane_idx + 1, note, (unsigned long long)step_id, (unsigned long long)rhythm_step);
        (void)schedule_note(inst, note,
                            lane_velocity(inst, lane, lane_idx, rhythm_step),
                            lane_gate(inst, lane, lane_idx, rhythm_step),
                            out_msgs, out_lens, max_out, &count);
    }
    dlog(inst, "emit_anchor_step end step=%llu out=%d", (unsigned long long)step_id, count);
    return count;
}

static int run_anchor_step(eucalypso_instance_t *inst,
                           uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int count;
    uint64_t step_id;
    if (!inst || max_out < 1) return 0;
    step_id = inst->anchor_step;
    if (inst->phrase_restart_pending && inst->active_count > 0) {
        inst->phrase_anchor_step = step_id;
        inst->phrase_restart_pending = 0;
        dlog(inst, "phrase restart step=%llu", (unsigned long long)step_id);
    }
    count = emit_anchor_step(inst, step_id, out_msgs, out_lens, max_out);
    inst->anchor_step++;
    return count;
}

static int process_clock_tick(eucalypso_instance_t *inst,
                              uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int count = 0;
    if (!inst || max_out < 1) return 0;
    (void)advance_voice_timers_clock(inst, out_msgs, out_lens, max_out, &count);
    inst->clock_tick_total++;
    if (inst->clocks_per_step < 1) inst->clocks_per_step = 1;
    inst->clock_counter = (int)(inst->clock_tick_total % (uint64_t)inst->clocks_per_step);
    if (inst->clock_counter == 0) {
        inst->pending_step_triggers++;
        dlog(inst, "clock boundary tick_total=%llu pending=%d",
             (unsigned long long)inst->clock_tick_total, inst->pending_step_triggers);
    }
    dlog(inst, "clock tick tick_total=%llu cc=%d pending=%d immediate_out=%d",
         (unsigned long long)inst->clock_tick_total, inst->clock_counter, inst->pending_step_triggers, count);
    return count;
}

static int handle_transport_stop(eucalypso_instance_t *inst,
                                 uint8_t out_msgs[][3], int out_lens[], int max_out) {
    int count = 0;
    if (!inst) return 0;
    (void)flush_all_voices(inst, out_msgs, out_lens, max_out, &count);
    inst->pending_step_triggers = 0;
    inst->clock_counter = 0;
    inst->clock_tick_total = 0;
    inst->anchor_step = 0;
    inst->phrase_anchor_step = 0;
    inst->phrase_restart_pending = 0;
    inst->preview_step_pending = 0;
    inst->preview_step_id = 0;
    inst->midi_transport_started = 0;
    inst->suppress_initial_note_restart = 0;
    inst->clock_start_grace_armed = 0;
    inst->internal_start_grace_armed = 0;
    inst->internal_sample_total = 0;
    inst->samples_until_step_f = inst->step_interval_base_f > 0.0 ? inst->step_interval_base_f : 1.0;
    inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
    if (inst->samples_until_step < 1) inst->samples_until_step = 1;
    inst->swing_phase = 0;
    inst->clock_running = (inst->sync_mode == SYNC_CLOCK) ? 0 : 1;
    inst->clock_start_grace_armed = 0;
    inst->physical_count = 0;
    inst->physical_as_played_count = 0;
    clear_active(inst);
    inst->latch_ready_replace = inst->play_mode == PLAY_LATCH ? 1 : 0;
    return count;
}

static void cache_chain_params_from_module_json(eucalypso_instance_t *inst, const char *module_dir) {
    char path[512];
    FILE *f;
    char *json = NULL;
    long size;
    const char *chain_params;
    const char *arr_start;
    const char *arr_end;
    int depth = 1;
    if (!inst || !module_dir || !module_dir[0]) return;
    snprintf(path, sizeof(path), "%s/module.json", module_dir);
    f = fopen(path, "rb");
    if (!f) return;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    size = ftell(f);
    if (size <= 0 || size > 300000) {
        fclose(f);
        return;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return;
    }
    json = (char *)malloc((size_t)size + 1);
    if (!json) {
        fclose(f);
        return;
    }
    if (fread(json, 1, (size_t)size, f) != (size_t)size) {
        free(json);
        fclose(f);
        return;
    }
    json[size] = '\0';
    fclose(f);

    chain_params = strstr(json, "\"chain_params\"");
    if (!chain_params) {
        free(json);
        return;
    }
    arr_start = strchr(chain_params, '[');
    if (!arr_start) {
        free(json);
        return;
    }
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

static void apply_default_state(eucalypso_instance_t *inst) {
    int i;
    if (!inst) return;
    memset(inst, 0, sizeof(*inst));
    inst->play_mode = PLAY_HOLD;
    inst->retrigger_mode = RETRIG_CONT;
    inst->rate = RATE_1_16;
    inst->sync_mode = SYNC_INTERNAL;
    inst->bpm = DEFAULT_BPM;
    inst->swing = 0;
    inst->max_voices = 8;
    inst->global_velocity = 100;
    inst->global_v_rnd = 0;
    inst->global_gate = 100;
    inst->global_g_rnd = 0;
    inst->global_rnd_seed = 0;
    inst->rand_cycle = 16;
    inst->register_mode = REGISTER_HELD;
    inst->held_order = HELD_UP;
    inst->held_order_seed = 0;
    inst->scale_mode = SCALE_MAJOR;
    inst->scale_rng = 8;
    inst->root_note = 0;
    inst->octave = 0;
    inst->missing_note_policy = MISSING_SKIP;
    inst->missing_note_seed = 0;
    for (i = 0; i < MAX_LANES; i++) {
        lane_t *lane = &inst->lanes[i];
        lane->enabled = (i == 0) ? 1 : 0;
        lane->steps = 16;
        lane->pulses = 4;
        lane->rotation = 0;
        lane->drop = 0;
        lane->drop_seed = 0;
        lane->note = i + 1;
        lane->n_rnd = 0;
        lane->n_seed = 0;
        lane->octave = 0;
        lane->oct_rnd = 0;
        lane->oct_seed = 0;
        lane->oct_rng = 2;
        lane->velocity = 0;
        lane->gate = 0;
    }
    inst->sample_rate = 0;
    inst->timing_dirty = 1;
    inst->step_interval_base = 1;
    inst->samples_until_step = 1;
    inst->step_interval_base_f = 1.0;
    inst->samples_until_step_f = 1.0;
    inst->clock_counter = 0;
    inst->clock_running = 1;
    inst->midi_transport_started = 0;
    inst->suppress_initial_note_restart = 0;
    inst->clock_start_grace_armed = 0;
    inst->internal_start_grace_armed = 0;
    inst->clocks_per_step = 6;
    inst->phrase_anchor_step = 0;
    inst->phrase_restart_pending = 0;
    recalc_clock_timing(inst);
}

static void *eucalypso_create_instance(const char *module_dir, const char *config_json) {
    eucalypso_instance_t *inst;
    (void)config_json;
    inst = (eucalypso_instance_t *)calloc(1, sizeof(eucalypso_instance_t));
    if (!inst) return NULL;
    apply_default_state(inst);
    cache_chain_params_from_module_json(inst, module_dir);
    dlog(inst, "create sync=%d cps=%d", (int)inst->sync_mode, inst->clocks_per_step);
    return inst;
}

static void eucalypso_destroy_instance(void *instance) {
    eucalypso_instance_t *inst = (eucalypso_instance_t *)instance;
    if (!inst) return;
    dlog(inst, "destroy");
    if (inst->debug_fp) {
        fclose(inst->debug_fp);
        inst->debug_fp = NULL;
    }
    free(inst);
}

static int parse_lane_key(const char *key, int *lane_idx, const char **suffix) {
    int lane_num;
    int consumed = 0;
    if (!key || !lane_idx || !suffix) return 0;
    if (sscanf(key, "lane%d_%n", &lane_num, &consumed) != 1) return 0;
    if (lane_num < 1 || lane_num > MAX_LANES) return 0;
    *lane_idx = lane_num - 1;
    *suffix = key + consumed;
    return 1;
}

static void normalize_lane(lane_t *lane) {
    if (!lane) return;
    lane->steps = clamp_int(lane->steps, 1, 128);
    lane->pulses = clamp_int(lane->pulses, 0, lane->steps);
}

static void set_lane_param(lane_t *lane, const char *suffix, const char *val) {
    if (!lane || !suffix || !val) return;
    if (strcmp(suffix, "enabled") == 0) lane->enabled = strcmp(val, "on") == 0 ? 1 : 0;
    else if (strcmp(suffix, "steps") == 0) lane->steps = clamp_int(atoi(val), 1, 128);
    else if (strcmp(suffix, "pulses") == 0) lane->pulses = clamp_int(atoi(val), 0, 128);
    else if (strcmp(suffix, "rotation") == 0) lane->rotation = clamp_int(atoi(val), 0, 127);
    else if (strcmp(suffix, "drop") == 0) lane->drop = clamp_int(atoi(val), 0, 100);
    else if (strcmp(suffix, "drop_seed") == 0) lane->drop_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(suffix, "note") == 0) lane->note = clamp_int(atoi(val), 1, 24);
    else if (strcmp(suffix, "n_rnd") == 0) lane->n_rnd = clamp_int(atoi(val), 0, 100);
    else if (strcmp(suffix, "n_seed") == 0) lane->n_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(suffix, "octave") == 0) lane->octave = clamp_int(atoi(val), -3, 3);
    else if (strcmp(suffix, "oct_rnd") == 0) lane->oct_rnd = clamp_int(atoi(val), 0, 100);
    else if (strcmp(suffix, "oct_seed") == 0) lane->oct_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(suffix, "oct_rng") == 0) {
        if (strcmp(val, "+1") == 0) lane->oct_rng = 0;
        else if (strcmp(val, "-1") == 0) lane->oct_rng = 1;
        else if (strcmp(val, "+-1") == 0) lane->oct_rng = 2;
        else if (strcmp(val, "+2") == 0) lane->oct_rng = 3;
        else if (strcmp(val, "-2") == 0) lane->oct_rng = 4;
        else if (strcmp(val, "+-2") == 0) lane->oct_rng = 5;
    }
    else if (strcmp(suffix, "velocity") == 0) lane->velocity = clamp_int(atoi(val), 0, 127);
    else if (strcmp(suffix, "gate") == 0) lane->gate = clamp_int(atoi(val), 0, 1600);
    normalize_lane(lane);
}

static int get_lane_param(const lane_t *lane, const char *suffix, char *buf, int buf_len) {
    if (!lane || !suffix || !buf || buf_len < 1) return -1;
    if (strcmp(suffix, "enabled") == 0) return snprintf(buf, buf_len, "%s", lane->enabled ? "on" : "off");
    if (strcmp(suffix, "steps") == 0) return snprintf(buf, buf_len, "%d", lane->steps);
    if (strcmp(suffix, "pulses") == 0) return snprintf(buf, buf_len, "%d", lane->pulses);
    if (strcmp(suffix, "rotation") == 0) return snprintf(buf, buf_len, "%d", lane->rotation);
    if (strcmp(suffix, "drop") == 0) return snprintf(buf, buf_len, "%d", lane->drop);
    if (strcmp(suffix, "drop_seed") == 0) return snprintf(buf, buf_len, "%d", lane->drop_seed);
    if (strcmp(suffix, "note") == 0) return snprintf(buf, buf_len, "%d", lane->note);
    if (strcmp(suffix, "n_rnd") == 0) return snprintf(buf, buf_len, "%d", lane->n_rnd);
    if (strcmp(suffix, "n_seed") == 0) return snprintf(buf, buf_len, "%d", lane->n_seed);
    if (strcmp(suffix, "octave") == 0) return snprintf(buf, buf_len, "%d", lane->octave);
    if (strcmp(suffix, "oct_rnd") == 0) return snprintf(buf, buf_len, "%d", lane->oct_rnd);
    if (strcmp(suffix, "oct_seed") == 0) return snprintf(buf, buf_len, "%d", lane->oct_seed);
    if (strcmp(suffix, "oct_rng") == 0) {
        static const char *names[] = { "+1", "-1", "+-1", "+2", "-2", "+-2" };
        int idx = clamp_int(lane->oct_rng, 0, 5);
        return snprintf(buf, buf_len, "%s", names[idx]);
    }
    if (strcmp(suffix, "velocity") == 0) return snprintf(buf, buf_len, "%d", lane->velocity);
    if (strcmp(suffix, "gate") == 0) return snprintf(buf, buf_len, "%d", lane->gate);
    return -1;
}

static void eucalypso_set_param(void *instance, const char *key, const char *val) {
    eucalypso_instance_t *inst = (eucalypso_instance_t *)instance;
    int lane_idx;
    const char *suffix;
    if (!inst || !key || !val) return;

    if (parse_lane_key(key, &lane_idx, &suffix)) {
        set_lane_param(&inst->lanes[lane_idx], suffix, val);
        return;
    }

    if (strcmp(key, "play_mode") == 0) set_play_mode(inst, strcmp(val, "latch") == 0 ? PLAY_LATCH : PLAY_HOLD);
    else if (strcmp(key, "retrigger_mode") == 0) inst->retrigger_mode = strcmp(val, "cont") == 0 ? RETRIG_CONT : RETRIG_RESTART;
    else if (strcmp(key, "rate") == 0) {
        inst->rate = parse_rate(val);
        inst->timing_dirty = 1;
        recalc_clock_timing(inst);
        if (inst->sync_mode == SYNC_CLOCK) realign_clock_phase(inst);
        else if (inst->sample_rate > 0) {
            recalc_internal_timing(inst, inst->sample_rate);
            realign_internal_phase(inst);
        }
    }
    else if (strcmp(key, "sync") == 0) {
        inst->sync_mode = strcmp(val, "clock") == 0 ? SYNC_CLOCK : SYNC_INTERNAL;
        if (inst->sync_mode == SYNC_CLOCK) {
            recalc_clock_timing(inst);
            realign_clock_phase(inst);
            inst->clock_running = 1;
        } else {
            inst->clock_running = 1;
            if (inst->sample_rate > 0) {
                recalc_internal_timing(inst, inst->sample_rate);
                realign_internal_phase(inst);
            }
        }
    }
    else if (strcmp(key, "bpm") == 0) {
        inst->bpm = clamp_int(atoi(val), 40, 240);
        inst->timing_dirty = 1;
        if (inst->sync_mode == SYNC_INTERNAL && inst->sample_rate > 0) {
            recalc_internal_timing(inst, inst->sample_rate);
            realign_internal_phase(inst);
        }
    }
    else if (strcmp(key, "swing") == 0) inst->swing = clamp_int(atoi(val), 0, 100);
    else if (strcmp(key, "max_voices") == 0) inst->max_voices = clamp_int(atoi(val), 1, MAX_VOICES);
    else if (strcmp(key, "global_velocity") == 0) inst->global_velocity = clamp_int(atoi(val), 1, 127);
    else if (strcmp(key, "global_v_rnd") == 0) inst->global_v_rnd = clamp_int(atoi(val), 0, 127);
    else if (strcmp(key, "global_gate") == 0) inst->global_gate = clamp_int(atoi(val), 1, 1600);
    else if (strcmp(key, "global_g_rnd") == 0) inst->global_g_rnd = clamp_int(atoi(val), 0, 1600);
    else if (strcmp(key, "global_rnd_seed") == 0) inst->global_rnd_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "rand_cycle") == 0) inst->rand_cycle = clamp_int(atoi(val), 1, 128);
    else if (strcmp(key, "register_mode") == 0) {
        if (strcmp(val, "scale") == 0) inst->register_mode = REGISTER_SCALE;
        else if (strcmp(val, "drumpad") == 0) inst->register_mode = REGISTER_DRUMPAD;
        else inst->register_mode = REGISTER_HELD;
    }
    else if (strcmp(key, "held_order") == 0) {
        if (strcmp(val, "down") == 0) inst->held_order = HELD_DOWN;
        else if (strcmp(val, "played") == 0) inst->held_order = HELD_PLAYED;
        else if (strcmp(val, "rand") == 0) inst->held_order = HELD_RAND;
        else inst->held_order = HELD_UP;
    }
    else if (strcmp(key, "held_order_seed") == 0) inst->held_order_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "missing_note_policy") == 0) {
        if (strcmp(val, "fold") == 0) inst->missing_note_policy = MISSING_FOLD;
        else if (strcmp(val, "wrap") == 0) inst->missing_note_policy = MISSING_WRAP;
        else if (strcmp(val, "random") == 0) inst->missing_note_policy = MISSING_RANDOM;
        else inst->missing_note_policy = MISSING_SKIP;
    }
    else if (strcmp(key, "missing_note_seed") == 0) inst->missing_note_seed = clamp_int(atoi(val), 0, 65535);
    else if (strcmp(key, "scale_mode") == 0) {
        if (strcmp(val, "natural_minor") == 0) inst->scale_mode = SCALE_NATURAL_MINOR;
        else if (strcmp(val, "harmonic_minor") == 0) inst->scale_mode = SCALE_HARMONIC_MINOR;
        else if (strcmp(val, "melodic_minor") == 0) inst->scale_mode = SCALE_MELODIC_MINOR;
        else if (strcmp(val, "dorian") == 0) inst->scale_mode = SCALE_DORIAN;
        else if (strcmp(val, "phrygian") == 0) inst->scale_mode = SCALE_PHRYGIAN;
        else if (strcmp(val, "lydian") == 0) inst->scale_mode = SCALE_LYDIAN;
        else if (strcmp(val, "mixolydian") == 0) inst->scale_mode = SCALE_MIXOLYDIAN;
        else if (strcmp(val, "locrian") == 0) inst->scale_mode = SCALE_LOCRIAN;
        else if (strcmp(val, "pentatonic_major") == 0) inst->scale_mode = SCALE_PENTATONIC_MAJOR;
        else if (strcmp(val, "pentatonic_minor") == 0) inst->scale_mode = SCALE_PENTATONIC_MINOR;
        else if (strcmp(val, "blues") == 0) inst->scale_mode = SCALE_BLUES;
        else if (strcmp(val, "whole_tone") == 0) inst->scale_mode = SCALE_WHOLE_TONE;
        else if (strcmp(val, "chromatic") == 0) inst->scale_mode = SCALE_CHROMATIC;
        else inst->scale_mode = SCALE_MAJOR;
    }
    else if (strcmp(key, "scale_rng") == 0) inst->scale_rng = clamp_int(atoi(val), 1, 24);
    else if (strcmp(key, "root_note") == 0) inst->root_note = clamp_int(atoi(val), 0, 11);
    else if (strcmp(key, "octave") == 0) inst->octave = clamp_int(atoi(val), -3, 3);
    else if (strcmp(key, "state") == 0) {
        char s[64];
        int i;
        int parsed;
        if (json_get_string(val, "play_mode", s, sizeof(s))) eucalypso_set_param(inst, "play_mode", s);
        if (json_get_string(val, "retrigger_mode", s, sizeof(s))) eucalypso_set_param(inst, "retrigger_mode", s);
        if (json_get_string(val, "rate", s, sizeof(s))) eucalypso_set_param(inst, "rate", s);
        if (json_get_string(val, "sync", s, sizeof(s))) eucalypso_set_param(inst, "sync", s);
        if (json_get_int(val, "bpm", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "bpm", s); }
        if (json_get_int(val, "swing", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "swing", s); }
        if (json_get_int(val, "max_voices", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "max_voices", s); }
        if (json_get_int(val, "global_velocity", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "global_velocity", s); }
        if (json_get_int(val, "global_v_rnd", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "global_v_rnd", s); }
        if (json_get_int(val, "global_gate", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "global_gate", s); }
        if (json_get_int(val, "global_g_rnd", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "global_g_rnd", s); }
        if (json_get_int(val, "global_rnd_seed", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "global_rnd_seed", s); }
        if (json_get_int(val, "rand_cycle", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "rand_cycle", s); }
        if (json_get_string(val, "register_mode", s, sizeof(s))) eucalypso_set_param(inst, "register_mode", s);
        if (json_get_string(val, "held_order", s, sizeof(s))) eucalypso_set_param(inst, "held_order", s);
        if (json_get_int(val, "held_order_seed", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "held_order_seed", s); }
        if (json_get_string(val, "missing_note_policy", s, sizeof(s))) eucalypso_set_param(inst, "missing_note_policy", s);
        if (json_get_int(val, "missing_note_seed", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "missing_note_seed", s); }
        if (json_get_string(val, "scale_mode", s, sizeof(s))) eucalypso_set_param(inst, "scale_mode", s);
        if (json_get_int(val, "scale_rng", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "scale_rng", s); }
        if (json_get_int(val, "root_note", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "root_note", s); }
        if (json_get_int(val, "octave", &parsed)) { snprintf(s, sizeof(s), "%d", parsed); eucalypso_set_param(inst, "octave", s); }
        for (i = 0; i < MAX_LANES; i++) {
            static const char *lane_fields[] = {
                "enabled", "steps", "pulses", "rotation", "drop", "drop_seed",
                "note", "n_rnd", "n_seed", "octave", "oct_rnd", "oct_seed",
                "oct_rng", "velocity", "gate"
            };
            int f;
            for (f = 0; f < (int)(sizeof(lane_fields) / sizeof(lane_fields[0])); f++) {
                char k[64];
                snprintf(k, sizeof(k), "lane%d_%s", i + 1, lane_fields[f]);
                if (strcmp(lane_fields[f], "enabled") == 0 || strcmp(lane_fields[f], "oct_rng") == 0) {
                    if (json_get_string(val, k, s, sizeof(s))) eucalypso_set_param(inst, k, s);
                } else if (json_get_int(val, k, &parsed)) {
                    snprintf(s, sizeof(s), "%d", parsed);
                    eucalypso_set_param(inst, k, s);
                }
            }
        }
    }
}

static int eucalypso_get_param(void *instance, const char *key, char *buf, int buf_len) {
    eucalypso_instance_t *inst = (eucalypso_instance_t *)instance;
    int lane_idx;
    const char *suffix;
    int pos = 0;
    int i;
    if (!inst || !key || !buf || buf_len < 1) return -1;

    if (parse_lane_key(key, &lane_idx, &suffix)) {
        return get_lane_param(&inst->lanes[lane_idx], suffix, buf, buf_len);
    }

    if (strcmp(key, "play_mode") == 0) return snprintf(buf, buf_len, "%s", play_mode_to_string(inst->play_mode));
    if (strcmp(key, "retrigger_mode") == 0) return snprintf(buf, buf_len, "%s", retrigger_to_string(inst->retrigger_mode));
    if (strcmp(key, "rate") == 0) return snprintf(buf, buf_len, "%s", rate_to_string(inst->rate));
    if (strcmp(key, "sync") == 0) return snprintf(buf, buf_len, "%s", sync_to_string(inst->sync_mode));
    if (strcmp(key, "error") == 0) return eucalypso_get_sync_warning(inst, buf, buf_len);
    if (strcmp(key, "bpm") == 0) return snprintf(buf, buf_len, "%d", inst->bpm);
    if (strcmp(key, "swing") == 0) return snprintf(buf, buf_len, "%d", inst->swing);
    if (strcmp(key, "max_voices") == 0) return snprintf(buf, buf_len, "%d", inst->max_voices);
    if (strcmp(key, "global_velocity") == 0) return snprintf(buf, buf_len, "%d", inst->global_velocity);
    if (strcmp(key, "global_v_rnd") == 0) return snprintf(buf, buf_len, "%d", inst->global_v_rnd);
    if (strcmp(key, "global_gate") == 0) return snprintf(buf, buf_len, "%d", inst->global_gate);
    if (strcmp(key, "global_g_rnd") == 0) return snprintf(buf, buf_len, "%d", inst->global_g_rnd);
    if (strcmp(key, "global_rnd_seed") == 0) return snprintf(buf, buf_len, "%d", inst->global_rnd_seed);
    if (strcmp(key, "rand_cycle") == 0) return snprintf(buf, buf_len, "%d", inst->rand_cycle);
    if (strcmp(key, "register_mode") == 0) return snprintf(buf, buf_len, "%s", register_mode_to_string(inst->register_mode));
    if (strcmp(key, "held_order") == 0) return snprintf(buf, buf_len, "%s", held_order_to_string(inst->held_order));
    if (strcmp(key, "held_order_seed") == 0) return snprintf(buf, buf_len, "%d", inst->held_order_seed);
    if (strcmp(key, "missing_note_policy") == 0) return snprintf(buf, buf_len, "%s", missing_note_policy_to_string(inst->missing_note_policy));
    if (strcmp(key, "missing_note_seed") == 0) return snprintf(buf, buf_len, "%d", inst->missing_note_seed);
    if (strcmp(key, "scale_mode") == 0) return snprintf(buf, buf_len, "%s", scale_mode_to_string(inst->scale_mode));
    if (strcmp(key, "scale_rng") == 0) return snprintf(buf, buf_len, "%d", inst->scale_rng);
    if (strcmp(key, "root_note") == 0) return snprintf(buf, buf_len, "%d", inst->root_note);
    if (strcmp(key, "octave") == 0) return snprintf(buf, buf_len, "%d", inst->octave);
    if (strcmp(key, "name") == 0) return snprintf(buf, buf_len, "Eucalypso");
    if (strcmp(key, "bank_name") == 0) return snprintf(buf, buf_len, "Factory");
    if (strcmp(key, "chain_params") == 0) {
        if (inst->chain_params_len > 0) return snprintf(buf, buf_len, "%s", inst->chain_params_json);
        return -1;
    }

    if (strcmp(key, "state") == 0) {
        if (!appendf(buf, buf_len, &pos, "{")) return -1;
        if (!appendf(buf, buf_len, &pos,
                     "\"play_mode\":\"%s\",\"retrigger_mode\":\"%s\",\"rate\":\"%s\",\"sync\":\"%s\","
                     "\"bpm\":%d,\"swing\":%d,\"max_voices\":%d,"
                     "\"global_velocity\":%d,\"global_v_rnd\":%d,\"global_gate\":%d,\"global_g_rnd\":%d,"
                     "\"global_rnd_seed\":%d,\"rand_cycle\":%d,"
                     "\"register_mode\":\"%s\",\"held_order\":\"%s\",\"held_order_seed\":%d,"
                     "\"missing_note_policy\":\"%s\",\"missing_note_seed\":%d,"
                     "\"scale_mode\":\"%s\",\"scale_rng\":%d,\"root_note\":%d,\"octave\":%d",
                     play_mode_to_string(inst->play_mode),
                     retrigger_to_string(inst->retrigger_mode),
                     rate_to_string(inst->rate),
                     sync_to_string(inst->sync_mode),
                     inst->bpm, inst->swing, inst->max_voices,
                     inst->global_velocity, inst->global_v_rnd, inst->global_gate, inst->global_g_rnd,
                     inst->global_rnd_seed, inst->rand_cycle,
                     register_mode_to_string(inst->register_mode),
                     held_order_to_string(inst->held_order),
                     inst->held_order_seed,
                     missing_note_policy_to_string(inst->missing_note_policy),
                     inst->missing_note_seed,
                     scale_mode_to_string(inst->scale_mode),
                     inst->scale_rng, inst->root_note, inst->octave)) {
            return -1;
        }
        for (i = 0; i < MAX_LANES; i++) {
            lane_t *lane = &inst->lanes[i];
            const char *oct_rng_names[] = { "+1", "-1", "+-1", "+2", "-2", "+-2" };
            int oct_rng = clamp_int(lane->oct_rng, 0, 5);
            if (!appendf(buf, buf_len, &pos,
                         ",\"lane%d_enabled\":\"%s\",\"lane%d_steps\":%d,\"lane%d_pulses\":%d,\"lane%d_rotation\":%d,"
                         "\"lane%d_drop\":%d,\"lane%d_drop_seed\":%d,\"lane%d_note\":%d,"
                         "\"lane%d_n_rnd\":%d,\"lane%d_n_seed\":%d,"
                         "\"lane%d_octave\":%d,\"lane%d_oct_rnd\":%d,\"lane%d_oct_seed\":%d,"
                         "\"lane%d_oct_rng\":\"%s\",\"lane%d_velocity\":%d,\"lane%d_gate\":%d",
                         i + 1, lane->enabled ? "on" : "off",
                         i + 1, lane->steps,
                         i + 1, lane->pulses,
                         i + 1, lane->rotation,
                         i + 1, lane->drop,
                         i + 1, lane->drop_seed,
                         i + 1, lane->note,
                         i + 1, lane->n_rnd,
                         i + 1, lane->n_seed,
                         i + 1, lane->octave,
                         i + 1, lane->oct_rnd,
                         i + 1, lane->oct_seed,
                         i + 1, oct_rng_names[oct_rng],
                         i + 1, lane->velocity,
                         i + 1, lane->gate)) {
                return -1;
            }
        }
        if (!appendf(buf, buf_len, &pos, "}")) return -1;
        return pos;
    }

    return -1;
}

static int eucalypso_process_midi(void *instance, const uint8_t *in_msg, int in_len,
                                  uint8_t out_msgs[][3], int out_lens[], int max_out) {
    eucalypso_instance_t *inst = (eucalypso_instance_t *)instance;
    uint8_t status;
    uint8_t type;
    int count = 0;
    if (!inst || !in_msg || in_len < 1) return 0;

    status = in_msg[0];
    type = status & 0xF0;

    if (inst->sync_mode == SYNC_CLOCK) {
        if (status == 0xFA) {
            inst->clock_running = 1;
            inst->midi_transport_started = 1;
            inst->suppress_initial_note_restart = 1;
            inst->clock_start_grace_armed = 0;
            inst->internal_start_grace_armed = 0;
            inst->clock_counter = 0;
            inst->clock_tick_total = 0;
            inst->pending_step_triggers = 1;
            inst->anchor_step = 0;
            inst->phrase_anchor_step = 0;
            inst->phrase_restart_pending = (inst->retrigger_mode == RETRIG_RESTART) ? 1 : 0;
            inst->preview_step_pending = 0;
            inst->preview_step_id = 0;
            inst->swing_phase = 0;
            dlog(inst, "MIDI Start cc=%d pending=%d anchor=%llu",
                 inst->clock_counter, inst->pending_step_triggers, (unsigned long long)inst->anchor_step);
            return 0;
        }
        if (status == 0xFB) {
            inst->clock_running = 1;
            inst->midi_transport_started = 1;
            inst->suppress_initial_note_restart = 1;
            inst->clock_start_grace_armed = 0;
            inst->internal_start_grace_armed = 0;
            dlog(inst, "MIDI Continue cc=%d pending=%d anchor=%llu",
                 inst->clock_counter, inst->pending_step_triggers, (unsigned long long)inst->anchor_step);
            return 0;
        }
        if (status == 0xFC) {
            dlog(inst, "MIDI Stop");
            return handle_transport_stop(inst, out_msgs, out_lens, max_out);
        }
        if (status == 0xF8) {
            if (!inst->clock_running) return 0;
            return process_clock_tick(inst, out_msgs, out_lens, max_out);
        }
    } else {
        if (status == 0xFA || status == 0xFB) {
            if (inst->timing_dirty || inst->sample_rate <= 0) {
                recalc_internal_timing(inst, inst->sample_rate > 0 ? inst->sample_rate : DEFAULT_SAMPLE_RATE);
            }
            inst->clock_running = 1;
            inst->midi_transport_started = 1;
            inst->suppress_initial_note_restart = 1;
            inst->clock_start_grace_armed = 0;
            inst->internal_start_grace_armed = 0;
            inst->internal_sample_total = 0;
            inst->samples_until_step_f = 0.0;
            inst->samples_until_step = 0;
            inst->anchor_step = 0;
            inst->phrase_anchor_step = 0;
            inst->phrase_restart_pending = (inst->retrigger_mode == RETRIG_RESTART) ? 1 : 0;
            inst->preview_step_pending = 0;
            inst->preview_step_id = 0;
            inst->swing_phase = 0;
            dlog(inst, "%s anchor=%llu", status == 0xFA ? "MIDI Start (internal)" : "MIDI Continue (internal)",
                 (unsigned long long)inst->anchor_step);
            return 0;
        }
        if (status == 0xFC) {
            dlog(inst, "MIDI Stop (internal)");
            return handle_transport_stop(inst, out_msgs, out_lens, max_out);
        }
    }

    if ((type == 0x90 || type == 0x80) && in_len >= 3) {
        uint8_t note = in_msg[1];
        uint8_t vel = in_msg[2];
        int live_before = inst->active_count;
        if (type == 0x90 && vel > 0) {
            dlog(inst, "NOTE_ON note=%u vel=%u cc=%d pending=%d active_before=%d anchor=%llu",
                 note, vel, inst->clock_counter, inst->pending_step_triggers, live_before,
                 (unsigned long long)inst->anchor_step);
            note_on(inst, note);
            if (live_before == 0 && inst->active_count > 0) {
                inst->suppress_initial_note_restart = 0;
                if (inst->retrigger_mode == RETRIG_RESTART) {
                    inst->phrase_restart_pending = 1;
                    dlog(inst, "phrase restart armed anchor=%llu",
                         (unsigned long long)inst->anchor_step);
                }
            }
        } else {
            dlog(inst, "NOTE_OFF note=%u cc=%d pending=%d active=%d anchor=%llu",
                 note, inst->clock_counter, inst->pending_step_triggers, inst->active_count,
                 (unsigned long long)inst->anchor_step);
            note_off(inst, note);
        }
        return 0;
    }

    if (max_out < 1) return 0;
    out_msgs[0][0] = in_msg[0];
    out_msgs[0][1] = in_len > 1 ? in_msg[1] : 0;
    out_msgs[0][2] = in_len > 2 ? in_msg[2] : 0;
    out_lens[0] = in_len > 3 ? 3 : in_len;
    count = 1;
    return count;
}

static int eucalypso_tick(void *instance, int frames, int sample_rate,
                          uint8_t out_msgs[][3], int out_lens[], int max_out) {
    eucalypso_instance_t *inst = (eucalypso_instance_t *)instance;
    int count = 0;
    if (!inst || frames < 0 || max_out < 1) return 0;

    if (inst->timing_dirty || inst->sample_rate != sample_rate) {
        recalc_internal_timing(inst, sample_rate);
    }

    if (inst->sync_mode == SYNC_INTERNAL) {
        (void)advance_voice_timers_samples(inst, frames, out_msgs, out_lens, max_out, &count);
        if (count >= max_out) return count;
        if (!inst->clock_running) return count;

        inst->internal_sample_total += (uint64_t)frames;
        inst->samples_until_step_f -= (double)frames;
        while (inst->samples_until_step_f <= 0.0 && count < max_out) {
            count += run_anchor_step(inst, out_msgs + count, out_lens + count, max_out - count);
            inst->samples_until_step_f += next_internal_interval(inst);
            if (inst->samples_until_step_f < 1.0) inst->samples_until_step_f = 1.0;
        }
        inst->samples_until_step = (int)(inst->samples_until_step_f + 0.5);
        if (inst->samples_until_step < 1) inst->samples_until_step = 1;
        return count;
    }

    if (inst->pending_step_triggers > 0) {
        dlog(inst, "tick drain start pending=%d anchor=%llu", inst->pending_step_triggers,
             (unsigned long long)inst->anchor_step);
        while (inst->pending_step_triggers > 0 && count < max_out) {
            count += run_anchor_step(inst, out_msgs + count, out_lens + count, max_out - count);
            inst->pending_step_triggers--;
            dlog(inst, "tick drain step done pending=%d out=%d anchor=%llu",
                 inst->pending_step_triggers, count, (unsigned long long)inst->anchor_step);
        }
    }
    return count;
}

static midi_fx_api_v1_t g_api = {
    .api_version = MIDI_FX_API_VERSION,
    .create_instance = eucalypso_create_instance,
    .destroy_instance = eucalypso_destroy_instance,
    .process_midi = eucalypso_process_midi,
    .tick = eucalypso_tick,
    .set_param = eucalypso_set_param,
    .get_param = eucalypso_get_param
};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
