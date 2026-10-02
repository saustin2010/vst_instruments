#include "mono_core.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MONO_WAVES 32
#define MONO_WAVE_LEN 512
#define MONO_FACTORY_WAVES (MONO_WAVES - MONO_USER_WAVES)
#define MONO_DELAY_SECONDS 2
#define MONO_TRACK_FX_SECONDS 1
#define MONO_CONTROL_INTERVAL 8

enum {
    TRIG_NOTE = 1,
    TRIG_AMP = 2,
    TRIG_FILTER = 4,
    TRIG_LFO = 8
};

typedef enum {
    ENV_OFF = 0,
    ENV_ATTACK,
    ENV_HOLD,
    ENV_DECAY,
    ENV_RELEASE
} env_stage_t;

typedef struct {
    float value;
    float release_step;
    int hold_left;
    env_stage_t stage;
} mono_env_t;

typedef struct {
    float low;
    float band;
} mono_svf_t;

typedef struct {
    float a1;
    float a2;
    float a3;
    float k;
} mono_svf_coeff_t;

typedef struct {
    float phase;
    float held;
    float slewed;
    uint32_t rng;
    uint32_t age_samples;
    int stopped;
} mono_lfo_t;

typedef struct {
    int8_t note;
    uint8_t velocity;
    uint8_t gate;
    uint8_t trig_mask;
    uint8_t probability;
    uint8_t retrig;
    uint8_t condition;
    uint8_t slide;
    int8_t micro;
    uint8_t tie;
    uint8_t accent;
    uint64_t lock_mask[MONO_LOCK_WORDS];
    uint8_t lock_values[MONO_PARAMS];
} mono_step_t;

typedef struct {
    uint8_t enabled;
    uint8_t latch;
    uint8_t mode;
    uint8_t rate;
    uint8_t octaves;
    uint8_t gate;
    uint8_t length;
    uint8_t velocity;
    int8_t offset[MONO_ARP_STEPS];
} mono_arp_settings_t;

typedef struct {
    uint8_t route_mode;
    uint8_t route_amount;
    uint8_t fx_type;
    uint8_t fx_amount;
    uint8_t fx_tone;
    uint8_t fx_feedback;
    uint8_t fx_mix;
    uint8_t level;
} mono_route_settings_t;

typedef struct {
    uint8_t start;
    uint8_t length;
    uint8_t repeats;
    int8_t transpose;
} mono_song_row_t;

typedef struct {
    int step;
    int frames;
    uint32_t cycle;
    int transpose;
    uint8_t valid;
    uint8_t early;
} mono_seq_event_t;

typedef struct {
    mono_machine_t machine;
    uint8_t base[MONO_PARAMS];
    uint8_t machine_params[MONO_MACHINE_COUNT][16];
    uint8_t machine_valid[MONO_MACHINE_COUNT];
    mono_step_t steps[MONO_STEPS];
    int mute;
    int solo;
    int seq_override;
    int seq_start;
    int seq_len;
    int seq_rotation;
    int seq_division;
    int keyboard_octave;
    mono_arp_settings_t arp;
    mono_route_settings_t route;
    uint8_t morph_a[MONO_PARAMS];
    uint8_t morph_b[MONO_PARAMS];
    uint8_t morph_valid;
    uint8_t morph_value;
    uint8_t morph_machine;
} mono_track_edit_t;

typedef struct {
    int pattern_start;
    int pattern_len;
    int play_order;
    int swing;
    int song_enabled;
    int song_length;
    mono_song_row_t song[MONO_SONG_ROWS];
    mono_track_edit_t track[MONO_MAX_TRACKS];
} mono_edit_snapshot_t;

typedef struct {
    mono_machine_t machine;
    uint8_t base[MONO_PARAMS];
    uint8_t machine_params[MONO_MACHINE_COUNT][16];
    uint8_t machine_valid[MONO_MACHINE_COUNT];
    uint8_t effective[MONO_PARAMS];
    uint8_t control[MONO_PARAMS];
    float smoothed[MONO_PARAMS];
    mono_step_t steps[MONO_STEPS];
    int mute;
    int solo;
    int seq_override;
    int seq_start;
    int seq_len;
    int seq_rotation;
    int seq_division;
    int keyboard_octave;
    mono_arp_settings_t arp;
    mono_route_settings_t route;
    uint8_t morph_a[MONO_PARAMS];
    uint8_t morph_b[MONO_PARAMS];
    uint8_t morph_valid;
    uint8_t morph_value;
    uint8_t morph_machine;
    int seq_cursor;
    int seq_direction;
    int seq_div_counter;
    int play_step;
    uint32_t seq_rng;
    uint32_t seq_due_count;
    int queued_cursor;
    int queued_direction;
    uint32_t queued_cycle;
    int queued_valid;
    int early_fired_step;
    mono_seq_event_t seq_event[2];

    int note;
    int last_note;
    int velocity;
    uint8_t held_notes[128];
    uint8_t held_velocity[128];
    uint32_t held_order[128];
    uint8_t physical_notes[128];
    uint32_t note_order;
    int gate_left;
    float freq;
    float target_freq;
    float smooth_coeff;
    int retrig_left;
    int retrig_countdown;
    int retrig_interval;
    int retrig_note;
    int retrig_velocity;
    int retrig_mask;
    int retrig_gate;
    float phase[10];
    float mod_phase[4];
    float fm_feedback;
    float fm_feedback2;
    uint32_t noise;
    uint32_t voice_age_samples;
    uint32_t sid_lfsr;
    float sid_noise_clock;
    double arp_frames;
    int arp_cursor;
    int arp_direction;
    int arp_pattern_pos;

    mono_env_t amp;
    mono_env_t filter_env;
    mono_lfo_t lfo[3];
    /* LFO-to-LFO modulation is fed into the next sample. This makes cross- and
     * self-modulation deterministic without recursively evaluating an LFO. */
    float lfo_param_mod[6 * MONO_PAGE_PARAMS];
    mono_svf_t hp[2];
    mono_svf_t lp[2];
    mono_svf_t eq;
    mono_svf_coeff_t hp_coeff;
    mono_svf_coeff_t lp_coeff;
    mono_svf_coeff_t eq_coeff;
    float filter_hp_hz;
    float filter_lp_hz;

    int srr_left;
    float srr_hold;
    float *delay;
    int delay_pos;
    float delay_lp[2];
    float delay_hp[2];
    float delay_hp_in[2];
    float delay_mod_phase;
    float *track_fx_buffer;
    int track_fx_pos;
    float track_fx_phase;
    float track_fx_env;
    float track_fx_lp[2];
    int track_fx_hold;
    float track_fx_held[2];
    uint32_t edit_rng;
} mono_track_t;

struct mono {
    const host_api_v1_t *host;
    int sample_rate;
    int track_count;
    int selected_track;
    int selected_page;
    int step_page;
    int pattern_start;
    int pattern_len;
    int play_order;
    int swing;
    int song_enabled;
    int song_length;
    int song_edit_row;
    int song_play_row;
    int song_repeat;
    int song_step_count;
    mono_song_row_t song[MONO_SONG_ROWS];
    int edit_step;
    int transport;
    int record_locks;
    int seq_step;
    int seq_direction;
    uint32_t seq_rng;
    int tick_in_step;
    int external_clock;
    int external_clock_age;
    double internal_frames;
    float bpm_override;
    float master;
    float smooth_coeff;
    int control_phase;

    /* MIDI CC control */
    uint8_t  cc_last[3];   /* last external CC accepted (duplicate guard) */
    uint64_t cc_last_frames;
    uint64_t cc_frames;    /* monotonic rendered-frame counter (guard clock) */
    /* Bumped ONLY by mono_cc_param, i.e. only when an external CC writes a
     * parameter. `revision` cannot serve this purpose: mono_set_param bumps it
     * too, so a UI watching `revision` would fire on its own knob writes and
     * re-read the value it just sent. Runtime only — never serialized. */
    uint32_t cc_revision;
    uint32_t revision;
    uint32_t note_events;
    uint32_t render_blocks;
    uint32_t nonzero_blocks;
    int render_peak;
    int lifetime_peak;
    uint32_t nonfinite_samples;
    int calibration_mode;
    int calibration_level;
    double calibration_phase;
    uint64_t calibration_frames;
    uint32_t calibration_noise;
    mono_edit_snapshot_t *undo_snapshot;
    mono_edit_snapshot_t *swap_snapshot;
    int undo_valid;
    mono_step_t step_clipboard;
    int step_clipboard_valid;
    mono_track_edit_t track_clipboard;
    int track_clipboard_valid;
    float time_2[128];
    float time_3[128];
    float time_4[128];
    float time_8[128];
    float time_12[128];
    float slew_coeff[128];
    float cutoff_g[128];
    float filter_ratio[128];
    float lfo_key_rate[128][128];
    float wavetable[MONO_WAVES][MONO_WAVE_LEN];
    uint8_t user_wave_mask;
    int16_t wave_upload[MONO_WAVE_LEN];
    uint8_t wave_upload_chunks;
    int wave_upload_slot;
    char user_wave_path[256];
    mono_track_t track[MONO_MAX_TRACKS];
};

static void sync_smoothed(mono_track_t *t);
static void process_sequence_events(mono_t *m, mono_track_t *t);
static void reset_arp_runtime(mono_track_t *t);

static void copy_track_edit(mono_track_edit_t *out, const mono_track_t *track) {
    out->machine = track->machine;
    memcpy(out->base, track->base, sizeof(out->base));
    memcpy(out->machine_params, track->machine_params, sizeof(out->machine_params));
    memcpy(out->machine_valid, track->machine_valid, sizeof(out->machine_valid));
    memcpy(out->steps, track->steps, sizeof(out->steps));
    out->mute = track->mute;
    out->solo = track->solo;
    out->seq_override = track->seq_override;
    out->seq_start = track->seq_start;
    out->seq_len = track->seq_len;
    out->seq_rotation = track->seq_rotation;
    out->seq_division = track->seq_division;
    out->keyboard_octave = track->keyboard_octave;
    out->arp = track->arp;
    out->route = track->route;
    memcpy(out->morph_a, track->morph_a, sizeof(out->morph_a));
    memcpy(out->morph_b, track->morph_b, sizeof(out->morph_b));
    out->morph_valid = track->morph_valid;
    out->morph_value = track->morph_value;
    out->morph_machine = track->morph_machine;
}

static void apply_track_edit(mono_track_t *track, const mono_track_edit_t *edit) {
    int octave_changed = track->keyboard_octave != edit->keyboard_octave;
    track->machine = edit->machine;
    memcpy(track->base, edit->base, sizeof(track->base));
    memcpy(track->machine_params, edit->machine_params, sizeof(track->machine_params));
    memcpy(track->machine_valid, edit->machine_valid, sizeof(track->machine_valid));
    memcpy(track->effective, track->base, sizeof(track->effective));
    memcpy(track->steps, edit->steps, sizeof(track->steps));
    track->mute = edit->mute;
    track->solo = edit->solo;
    track->seq_override = edit->seq_override;
    track->seq_start = edit->seq_start;
    track->seq_len = edit->seq_len;
    track->seq_rotation = edit->seq_rotation;
    track->seq_division = edit->seq_division;
    track->keyboard_octave = edit->keyboard_octave;
    track->arp = edit->arp;
    track->route = edit->route;
    memcpy(track->morph_a, edit->morph_a, sizeof(track->morph_a));
    memcpy(track->morph_b, edit->morph_b, sizeof(track->morph_b));
    track->morph_valid = edit->morph_valid;
    track->morph_value = edit->morph_value;
    track->morph_machine = edit->morph_machine;
    if (octave_changed) {
        memset(track->held_notes, 0, sizeof(track->held_notes));
        memset(track->held_velocity, 0, sizeof(track->held_velocity));
        memset(track->held_order, 0, sizeof(track->held_order));
        memset(track->physical_notes, 0, sizeof(track->physical_notes));
        track->note = -1;
        track->gate_left = 0;
        track->amp.value = 0.0f;
        track->amp.stage = ENV_OFF;
        reset_arp_runtime(track);
    }
    sync_smoothed(track);
}

static void fill_edit_snapshot(const mono_t *m, mono_edit_snapshot_t *snapshot) {
    snapshot->pattern_start = m->pattern_start;
    snapshot->pattern_len = m->pattern_len;
    snapshot->play_order = m->play_order;
    snapshot->swing = m->swing;
    snapshot->song_enabled = m->song_enabled;
    snapshot->song_length = m->song_length;
    memcpy(snapshot->song, m->song, sizeof(snapshot->song));
    for (int track = 0; track < m->track_count; ++track)
        copy_track_edit(&snapshot->track[track], &m->track[track]);
}

static void capture_undo(mono_t *m) {
    if (!m || !m->undo_snapshot) return;
    fill_edit_snapshot(m, m->undo_snapshot);
    m->undo_valid = 1;
}

static float fclamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int iclamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int step_has_lock(const mono_step_t *s, int pid) {
    return (s->lock_mask[pid / 64] & (UINT64_C(1) << (pid % 64))) != 0;
}

static int step_has_any_lock(const mono_step_t *s) {
    for (int word = 0; word < MONO_LOCK_WORDS; ++word)
        if (s->lock_mask[word]) return 1;
    return 0;
}

static int step_has_all_locks(const mono_step_t *s) {
    for (int pid = 0; pid < MONO_PARAMS; ++pid)
        if (!step_has_lock(s, pid)) return 0;
    return 1;
}

/* A note-less step still counts when it carries a tie: it extends the
 * previous note instead of triggering one. */
static int step_is_active(const mono_step_t *s) {
    return s->note >= 0 || s->trig_mask || s->tie || step_has_any_lock(s);
}

static int step_display_state(const mono_step_t *s) {
    return step_has_any_lock(s) ? 2 : (step_is_active(s) ? 1 : 0);
}

static void step_set_lock(mono_step_t *s, int pid) {
    s->lock_mask[pid / 64] |= UINT64_C(1) << (pid % 64);
}

static void step_clear_lock(mono_step_t *s, int pid) {
    s->lock_mask[pid / 64] &= ~(UINT64_C(1) << (pid % 64));
}

static void changed(mono_t *m) {
    if (m) ++m->revision;
}

static int is_lfo_destination_param(int pid) {
    return pid >= 32 && pid < MONO_PRIMARY_PARAMS && ((pid - 32) % 8) == 0;
}

static int lfo_control_mod_index(int pid) {
    if (pid >= 32 && pid < MONO_PRIMARY_PARAMS) return pid - 32;
    int shifted = pid - (MONO_SHIFT_BASE + 4 * MONO_PAGE_PARAMS);
    return shifted >= 0 && shifted < 3 * MONO_PAGE_PARAMS
        ? 3 * MONO_PAGE_PARAMS + shifted : -1;
}

static int is_smoothable_param(const mono_track_t *t, int pid) {
    /* Keep enumerated choices stepped so the glide never passes through an
     * unintended waveform/routing mode. Everything else gets a short zipper-
     * noise-free glide, including ordinary synth controls. */
    if (pid < 8) {
        if (t->machine == MONO_SWAVE_PULSE && pid == 6) return 0;
        if (t->machine == MONO_SID_6581 &&
            (pid == 2 || pid == 3 || pid == 4 || pid == 5)) return 0;
        if (t->machine == MONO_DIGIPRO_WAVE &&
            (pid == 0 || pid == 3 || pid == 4)) return 0;
        if (t->machine == MONO_FM_STATIC && (pid == 0 || pid == 4)) return 0;
        return 1;
    }
    if (pid < 32) return 1;
    if (pid >= 32 && pid < MONO_PRIMARY_PARAMS) {
        int offset = (pid - 32) % MONO_PAGE_PARAMS;
        return offset == 4 || offset == 6 || offset == 7;
    }
    if (pid >= MONO_SHIFT_BASE && pid < MONO_PARAMS) {
        int shift_page = (pid - MONO_SHIFT_BASE) / MONO_PAGE_PARAMS;
        int offset = (pid - MONO_SHIFT_BASE) % MONO_PAGE_PARAMS;
        if (shift_page == 0) {
            if (t->machine == MONO_SWAVE_PULSE && offset == 3) return 0;
            if (t->machine == MONO_DIGIPRO_WAVE && (offset == 0 || offset == 3)) return 0;
            if (t->machine == MONO_FM_STATIC && offset == 2) return 0;
        }
        if (shift_page == 2 && (offset == 4 || offset == 5)) return 0;
        if (shift_page >= 4 && offset == 5) return 0;
        return 1;
    }
    return 0;
}

static void sync_smoothed(mono_track_t *t) {
    for (int pid = 0; pid < MONO_PARAMS; ++pid) {
        t->smoothed[pid] = (float)t->effective[pid];
        t->control[pid] = t->effective[pid];
    }
}

static void reset_effective_to_base(mono_t *m) {
    for (int track = 0; track < m->track_count; ++track)
        memcpy(m->track[track].effective, m->track[track].base, MONO_PARAMS);
}

static void smooth_param_value(const mono_t *m, mono_track_t *t, int pid) {
    float target = (float)t->effective[pid];
    float coeff = t->smooth_coeff > 0.0f ? t->smooth_coeff : m->smooth_coeff;
    t->smoothed[pid] += (target - t->smoothed[pid]) * coeff;
    if (fabsf(target - t->smoothed[pid]) < 0.001f) t->smoothed[pid] = target;
    t->control[pid] = (uint8_t)iclamp((int)lrintf(t->smoothed[pid]), 0, 127);
}

static int lfo_destination_index(int value) {
    return iclamp(value, 0, MONO_LFO_DESTINATIONS - 1);
}

static int lfo_destination_param(int destination) {
    return destination >= 2 ? destination - 2 : -1;
}

static int migrate_legacy_lfo_destination(int value) {
    /* v2/v3 stored the original seven destinations as 0,16,...,96. */
    static const uint8_t migrated[7] = {
        0,                  /* Off */
        1,                  /* Pitch */
        2 + 16,             /* Filter Base */
        2 + 17,             /* Filter Width */
        2 + 13,             /* Volume */
        2 + 14,             /* Pan */
        2 + 28              /* Delay Time */
    };
    return migrated[iclamp(value / 16, 0, 6)];
}

static int appendf(char *buf, int buf_len, int *used, const char *fmt, ...) {
    if (*used < 0 || *used >= buf_len) return 0;
    va_list ap;
    va_start(ap, fmt);
    int wrote = vsnprintf(buf + *used, (size_t)(buf_len - *used), fmt, ap);
    va_end(ap);
    if (wrote < 0 || wrote >= buf_len - *used) return 0;
    *used += wrote;
    return 1;
}

static float midi_freq(float note) {
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

static float pnorm(int p) {
    return (float)iclamp(p, 0, 127) / 127.0f;
}

static float time_from_param(int p, float max_seconds) {
    if (p <= 0) return 0.0f;
    return 0.002f * powf(max_seconds / 0.002f, pnorm(p));
}

static void build_control_tables(mono_t *m) {
    for (int p = 0; p < 128; ++p) {
        m->time_2[p] = time_from_param(p, 2.0f);
        m->time_3[p] = time_from_param(p, 3.0f);
        m->time_4[p] = time_from_param(p, 4.0f);
        m->time_8[p] = time_from_param(p, 8.0f);
        m->time_12[p] = time_from_param(p, 12.0f);
        float slew = m->time_2[p];
        m->slew_coeff[p] = slew > 0.0f
            ? 1.0f - expf(-1.0f / (slew * m->sample_rate)) : 1.0f;
        float hz = 18.0f * powf(1000.0f, pnorm(p));
        hz = fclamp(hz, 15.0f, m->sample_rate * 0.45f);
        m->cutoff_g[p] = tanf((float)M_PI * hz / m->sample_rate);
        /* Monomachine BASE/WDTH advance one octave per eight parameter
         * steps. Keep the ratio table out of the realtime control path. */
        m->filter_ratio[p] = powf(2.0f, p / 8.0f);
        for (int note = 0; note < 128; ++note) {
            float key_scale = ((int)p - 64) / 64.0f;
            m->lfo_key_rate[p][note] =
                powf(2.0f, ((note - 60) / 12.0f) * key_scale);
        }
    }
}

static float bpm_now(const mono_t *m) {
    if (m->bpm_override >= 30.0f) return m->bpm_override;
    if (m->host && m->host->get_bpm) {
        float bpm = m->host->get_bpm();
        if (bpm >= 30.0f && bpm <= 400.0f) return bpm;
    }
    return 120.0f;
}

static uint32_t xrnd(uint32_t *state) {
    uint32_t x = *state ? *state : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float phase_step(float *phase, float hz, int sample_rate) {
    float old = *phase;
    *phase += hz / (float)sample_rate;
    *phase -= floorf(*phase);
    return *phase < old ? 1.0f : 0.0f;
}

static float saw(float p) { return 2.0f * p - 1.0f; }

static float tri(float p) {
    return 1.0f - 4.0f * fabsf(p - 0.5f);
}

static float pulse(float p, float width) {
    return p < width ? 1.0f : -1.0f;
}

/* Two-sample polynomial band-limiting for discontinuous oscillators. It is
 * deliberately local and allocation-free so six stacked voices remain cheap
 * enough for Move's realtime thread. */
static float poly_blep(float phase, float dt) {
    dt = fclamp(dt, 1.0e-6f, 0.5f);
    if (phase < dt) {
        float x = phase / dt;
        return x + x - x * x - 1.0f;
    }
    if (phase > 1.0f - dt) {
        float x = (phase - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

static float saw_blep(float phase, float dt) {
    return saw(phase) - poly_blep(phase, dt);
}

static float pulse_blep(float phase, float width, float dt) {
    float v = pulse(phase, width);
    v += poly_blep(phase, dt);
    float edge = phase - width;
    if (edge < 0.0f) edge += 1.0f;
    v -= poly_blep(edge, dt);
    return v;
}

static void generate_wavetables(mono_t *m) {
    for (int w = 0; w < MONO_WAVES; ++w) {
        float peak = 0.0f;
        for (int i = 0; i < MONO_WAVE_LEN; ++i) {
            float ph = (float)i / MONO_WAVE_LEN;
            float v = 0.0f;
            int harmonics = 2 + (w % 14);
            for (int h = 1; h <= harmonics; ++h) {
                float amp;
                switch (w / 8) {
                case 0: amp = 1.0f / h; break;
                case 1: amp = (h & 1) ? 1.0f / h : 0.0f; break;
                case 2: amp = (h & 1) ? 1.0f / (h * h) : 0.0f; break;
                default: amp = sinf((float)(w + 1) * h * 0.731f) / h; break;
                }
                v += amp * sinf(2.0f * (float)M_PI * h * ph + w * 0.173f);
            }
            m->wavetable[w][i] = v;
            if (fabsf(v) > peak) peak = fabsf(v);
        }
        if (peak < 1e-6f) peak = 1.0f;
        for (int i = 0; i < MONO_WAVE_LEN; ++i) {
            float v = m->wavetable[w][i] / peak;
            m->wavetable[w][i] = roundf(v * 2047.0f) / 2047.0f;
        }
    }
}

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t mask;
    int16_t samples[MONO_USER_WAVES][MONO_WAVE_LEN];
} mono_wave_bank_file_t;

static void load_user_waves(mono_t *m) {
    if (!m || !m->user_wave_path[0]) return;
    FILE *file = fopen(m->user_wave_path, "rb");
    if (!file) return;
    mono_wave_bank_file_t *bank = malloc(sizeof(*bank));
    if (!bank) { fclose(file); return; }
    int ok = fread(bank, sizeof(*bank), 1, file) == 1 &&
             !memcmp(bank->magic, "MONOWAV1", 8) && bank->version == 1;
    fclose(file);
    if (ok) {
        m->user_wave_mask = (uint8_t)(bank->mask & 0xffu);
        for (int slot = 0; slot < MONO_USER_WAVES; ++slot) {
            if (!(m->user_wave_mask & (1u << slot))) continue;
            float peak = 1.0e-6f;
            for (int i = 0; i < MONO_WAVE_LEN; ++i)
                if (abs(bank->samples[slot][i]) > peak)
                    peak = (float)abs(bank->samples[slot][i]);
            for (int i = 0; i < MONO_WAVE_LEN; ++i)
                m->wavetable[MONO_FACTORY_WAVES + slot][i] =
                    bank->samples[slot][i] / peak;
        }
    }
    free(bank);
}

static int save_user_waves(const mono_t *m) {
    if (!m || !m->user_wave_path[0]) return 0;
    mono_wave_bank_file_t *bank = calloc(1, sizeof(*bank));
    if (!bank) return 0;
    memcpy(bank->magic, "MONOWAV1", 8);
    bank->version = 1;
    bank->mask = m->user_wave_mask;
    for (int slot = 0; slot < MONO_USER_WAVES; ++slot)
        for (int i = 0; i < MONO_WAVE_LEN; ++i)
            bank->samples[slot][i] = (int16_t)lrintf(fclamp(
                m->wavetable[MONO_FACTORY_WAVES + slot][i], -1.0f, 1.0f) * 2047.0f);
    char temporary[sizeof(m->user_wave_path) + 8];
    int wrote = snprintf(temporary, sizeof(temporary), "%s.tmp", m->user_wave_path);
    if (wrote <= 0 || wrote >= (int)sizeof(temporary)) { free(bank); return 0; }
    FILE *file = fopen(temporary, "wb");
    int ok = file && fwrite(bank, sizeof(*bank), 1, file) == 1;
    if (file && fclose(file) != 0) ok = 0;
    if (ok) ok = rename(temporary, m->user_wave_path) == 0;
    else remove(temporary);
    free(bank);
    return ok;
}

static void commit_user_wave(mono_t *m, int slot) {
    if (!m || slot < 0 || slot >= MONO_USER_WAVES) return;
    float mean = 0.0f, peak = 1.0e-6f;
    for (int i = 0; i < MONO_WAVE_LEN; ++i) mean += m->wave_upload[i];
    mean /= MONO_WAVE_LEN;
    for (int i = 0; i < MONO_WAVE_LEN; ++i) {
        float centered = m->wave_upload[i] - mean;
        if (fabsf(centered) > peak) peak = fabsf(centered);
    }
    for (int i = 0; i < MONO_WAVE_LEN; ++i) {
        float normalized = (m->wave_upload[i] - mean) / peak;
        m->wavetable[MONO_FACTORY_WAVES + slot][i] =
            roundf(fclamp(normalized, -1.0f, 1.0f) * 2047.0f) / 2047.0f;
    }
    m->user_wave_mask |= (uint8_t)(1u << slot);
    (void)save_user_waves(m);
}

static void clear_step(mono_step_t *s) {
    memset(s, 0, sizeof(*s));
    s->note = -1;
    s->velocity = 100;
    s->gate = 100;
    s->probability = 127;
    s->retrig = 1;
}

static void performance_defaults(mono_track_t *t) {
    memset(&t->arp, 0, sizeof(t->arp));
    memset(&t->route, 0, sizeof(t->route));
    t->arp.rate = 3; /* 1/16 */
    t->arp.octaves = 1;
    t->arp.gate = 92;
    t->arp.length = MONO_ARP_STEPS;
    t->route.level = 64;
    t->keyboard_octave = 0;
    t->morph_valid = 0;
    t->morph_value = 0;
    t->morph_machine = 0;
}

static void common_defaults(mono_track_t *t) {
    static const uint8_t amp[8] = { 0, 96, 48, 28, 12, 110, 64, 0 };
    static const uint8_t flt[8] = { 0, 127, 0, 0, 0, 48, 64, 32 };
    static const uint8_t fx[8]  = { 64, 64, 127, 0, 48, 32, 0, 127 };
    static const uint8_t lfo[8] = { 0, 0, 0, 32, 32, 0, 0, 0 };
    static const uint8_t amp_shift[8] = { 64, 64, 64, 127, 64, 127, 64, 64 };
    static const uint8_t filter_shift[8] = { 127, 64, 127, 0, 127, 127, 127, 0 };
    static const uint8_t effect_shift[8] = { 64, 127, 0, 0, 0, 0, 32, 0 };
    static const uint8_t lfo_shift[8] = { 0, 0, 0, 64, 0, 127, 0, 64 };
    memcpy(t->base + 8, amp, 8);
    memcpy(t->base + 16, flt, 8);
    memcpy(t->base + 24, fx, 8);
    for (int p = 4; p < 7; ++p) memcpy(t->base + p * 8, lfo, 8);
    memcpy(t->base + MONO_SHIFT_BASE + MONO_PAGE_PARAMS, amp_shift, 8);
    memcpy(t->base + MONO_SHIFT_BASE + 2 * MONO_PAGE_PARAMS, filter_shift, 8);
    memcpy(t->base + MONO_SHIFT_BASE + 3 * MONO_PAGE_PARAMS, effect_shift, 8);
    for (int p = 4; p < 7; ++p)
        memcpy(t->base + MONO_SHIFT_BASE + p * MONO_PAGE_PARAMS, lfo_shift, 8);
}

static void machine_defaults(mono_track_t *t, mono_machine_t machine) {
    static const uint8_t defs[MONO_MACHINE_COUNT][8] = {
        { 76, 18, 32, 0, 0, 0, 0, 64 },  /* SWAVE SAW */
        { 76, 18, 32, 0, 64, 0, 1, 64 }, /* SWAVE PULSE */
        { 20, 35, 55, 72, 64, 50, 48, 64 },
        { 64, 0, 1, 30, 0, 0, 64, 64 },
        { 0, 0, 0, 1, 0, 64, 0, 64 },
        { 34, 64, 10, 72, 46, 42, 72, 64 }
    };
    static const uint8_t alt_defs[MONO_MACHINE_COUNT][MONO_ALT_PARAMS] = {
        { 0, 21, 64, 0, 0, 0, 0, 0 },       /* BRITE STACK SUBPW SUBOCT */
        { 64, 21, 0, 0, 0, 0, 0, 0 },       /* ASYM STACK SUB SYNC */
        { 127, 127, 127, 127, 0, 0, 0, 0 }, /* per-voice levels */
        { 0, 0, 0, 0, 0, 0, 0, 0 },         /* RING SUB MODMIX CHAOS */
        { 0, 0, 64, 64, 0, 0, 0, 0 },       /* WAVE2 BLEND DETUNE OCTAVE */
        { 64, 0, 34, 0, 0, 0, 0, 0 }        /* OP2FIN OP2FB OP3RAT OP3LVL */
    };
    t->machine = machine;
    memcpy(t->base, defs[machine], 8);
    memcpy(t->base + MONO_ALT_BASE, alt_defs[machine], MONO_ALT_PARAMS);
    memcpy(t->effective, t->base, MONO_PARAMS);
    sync_smoothed(t);
}

static void remember_machine(mono_track_t *t) {
    int machine = iclamp(t->machine, 0, MONO_MACHINE_COUNT - 1);
    memcpy(t->machine_params[machine], t->base, 8);
    memcpy(t->machine_params[machine] + 8, t->base + MONO_ALT_BASE, 8);
    t->machine_valid[machine] = 1;
}

static void recall_machine(mono_track_t *t, int machine) {
    machine = iclamp(machine, 0, MONO_MACHINE_COUNT - 1);
    if (machine == (int)t->machine) return;
    remember_machine(t);
    machine_defaults(t, (mono_machine_t)machine);
    if (t->machine_valid[machine]) {
        memcpy(t->base, t->machine_params[machine], 8);
        memcpy(t->base + MONO_ALT_BASE, t->machine_params[machine] + 8, 8);
        memcpy(t->effective, t->base, MONO_PARAMS);
        sync_smoothed(t);
    }
    remember_machine(t);
}

typedef struct {
    const char *name;
    uint8_t machine;
    uint8_t synth[8];
    uint8_t alt[4];
    uint8_t attack, decay, release, distortion;
    uint8_t filter_base, filter_width, resonance, delay_send;
} mono_patch_def_t;

/* Original factory-independent starting points. These are intentionally
 * compact recipes rather than copied sound data. */
static const mono_patch_def_t patch_library[] = {
    {"Chrome Bass", 0, {92,18,18,42,26,10,0,52}, {24,28,64,0}, 0,42,22,28, 0,78,18,4},
    {"Wide Current",0, {108,48,74,0,12,0,0,64}, {18,64,64,0}, 8,78,54,16, 0,122,8,18},
    {"Hollow Wire",1, {88,24,28,0,76,52,127,64}, {82,34,8,0}, 0,50,30,20, 0,96,14,8},
    {"PWM Basin",  1, {110,42,52,18,58,88,127,54}, {54,50,22,0}, 12,86,66,10, 0,118,6,20},
    {"Glass Choir",2, {39,58,74,26,58,74,50,64}, {127,94,80,70}, 18,92,70,8, 0,127,4,28},
    {"Just Fifths",2, {124,127,3,82,48,38,62,64}, {127,112,82,0}, 4,72,44,12, 0,112,10,16},
    {"Arcade Lead",3, {68,36,127,36,42,0,76,64}, {12,8,14,0}, 0,48,24,34, 0,96,18,6},
    {"Dust Pulse", 3, {38,84,0,110,0,0,52,52}, {30,24,4,22}, 2,62,34,48, 0,84,24,10},
    {"Scan Bell",  4, {52,28,82,127,64,78,0,64}, {76,42,70,64}, 0,96,86,8, 0,127,4,26},
    {"Circuit Reed",4, {24,76,48,0,0,64,0,60}, {12,28,58,64}, 4,68,46,22, 0,102,12,12},
    {"Metal Key",  5, {72,64,48,104,84,90,106,64}, {66,24,54,16}, 0,76,50,14, 0,116,8,22},
    {"Soft Operator",5,{56,64,6,70,56,28,40,64}, {64,0,34,0}, 12,98,74,4, 0,126,2,18}
};

#define MONO_PATCH_COUNT ((int)(sizeof(patch_library) / sizeof(patch_library[0])))

static void invalidate_morph(mono_track_t *t) {
    t->morph_valid = 0;
    t->morph_value = 0;
}

static void initialize_track_sound(mono_track_t *t, int machine) {
    common_defaults(t);
    machine_defaults(t, (mono_machine_t)iclamp(machine, 0, MONO_MACHINE_COUNT - 1));
    memcpy(t->effective, t->base, MONO_PARAMS);
    memset(t->machine_valid, 0, sizeof(t->machine_valid));
    remember_machine(t);
    invalidate_morph(t);
    sync_smoothed(t);
}

static void apply_library_patch(mono_track_t *t, int index) {
    index = iclamp(index, 0, MONO_PATCH_COUNT - 1);
    const mono_patch_def_t *patch = &patch_library[index];
    initialize_track_sound(t, patch->machine);
    memcpy(t->base, patch->synth, sizeof(patch->synth));
    memcpy(t->base + MONO_ALT_BASE, patch->alt, sizeof(patch->alt));
    t->base[8] = patch->attack;
    t->base[10] = patch->decay;
    t->base[11] = patch->release;
    t->base[12] = patch->distortion;
    t->base[16] = patch->filter_base;
    t->base[17] = patch->filter_width;
    t->base[19] = patch->resonance;
    t->base[27] = patch->delay_send;
    memcpy(t->effective, t->base, MONO_PARAMS);
    remember_machine(t);
    sync_smoothed(t);
}

static int random_between(uint32_t *rng, int low, int high) {
    if (high <= low) return low;
    return low + (int)(xrnd(rng) % (uint32_t)(high - low + 1));
}

static void randomize_track_sound(mono_track_t *t) {
    int machine = t->machine;
    initialize_track_sound(t, machine);
    uint32_t *rng = &t->edit_rng;
    for (int i = 0; i < 8; ++i) t->base[i] = (uint8_t)random_between(rng, 8, 119);
    t->base[7] = (uint8_t)random_between(rng, 52, 76);
    if (machine == MONO_SWAVE_PULSE) t->base[6] = (uint8_t)((xrnd(rng) & 1u) ? 127 : 0);
    if (machine == MONO_SWAVE_ENSEMBLE)
        for (int i = 0; i < 3; ++i) t->base[i] = (uint8_t)random_between(rng, 0, 127);
    if (machine == MONO_SID_6581) {
        t->base[2] = (uint8_t)((xrnd(rng) & 1u) ? 127 : 0);
        t->base[3] = (uint8_t)(random_between(rng, 0, 4) * 25);
        t->base[4] = (uint8_t)(random_between(rng, 0, 3) * 32);
        t->base[5] = (uint8_t)((xrnd(rng) & 1u) ? 127 : 0);
    } else if (machine == MONO_DIGIPRO_WAVE) {
        t->base[0] = (uint8_t)random_between(rng, 0, 127);
        t->base[3] = (uint8_t)((xrnd(rng) & 1u) ? 127 : 0);
        t->base[4] = (uint8_t)(random_between(rng, 0, 2) * 64);
    } else if (machine == MONO_FM_STATIC) {
        t->base[0] = (uint8_t)(random_between(rng, 8, 26) * 4);
        t->base[4] = (uint8_t)(random_between(rng, 8, 26) * 4);
    }
    for (int i = 0; i < 4; ++i)
        t->base[MONO_ALT_BASE + i] = (uint8_t)random_between(rng, 0, 96);
    t->base[8] = (uint8_t)random_between(rng, 0, 28);
    t->base[10] = (uint8_t)random_between(rng, 30, 108);
    t->base[11] = (uint8_t)random_between(rng, 12, 92);
    t->base[12] = (uint8_t)random_between(rng, 0, 42);
    t->base[16] = (uint8_t)random_between(rng, 0, 24);
    t->base[17] = (uint8_t)random_between(rng, 70, 127);
    t->base[18] = (uint8_t)random_between(rng, 0, 32);
    t->base[19] = (uint8_t)random_between(rng, 0, 32);
    t->base[27] = (uint8_t)random_between(rng, 0, 36);
    memcpy(t->effective, t->base, MONO_PARAMS);
    remember_machine(t);
    sync_smoothed(t);
}

static void capture_morph_endpoint(mono_track_t *t, int endpoint) {
    if (endpoint == 0) {
        memcpy(t->morph_a, t->base, MONO_PARAMS);
        t->morph_machine = (uint8_t)t->machine;
        t->morph_valid |= 1;
    } else {
        if ((t->morph_valid & 1) && t->morph_machine != (uint8_t)t->machine)
            t->morph_valid = 0;
        memcpy(t->morph_b, t->base, MONO_PARAMS);
        t->morph_machine = (uint8_t)t->machine;
        t->morph_valid |= 2;
    }
}

static void apply_morph(mono_track_t *t, int value) {
    t->morph_value = (uint8_t)iclamp(value, 0, 127);
    if (t->morph_valid != 3) return;
    if (t->machine != (mono_machine_t)t->morph_machine)
        machine_defaults(t, (mono_machine_t)t->morph_machine);
    for (int pid = 0; pid < MONO_PARAMS; ++pid) {
        int next = is_smoothable_param(t, pid)
            ? t->morph_a[pid] + ((int)t->morph_b[pid] - t->morph_a[pid]) * value / 127
            : (value < 64 ? t->morph_a[pid] : t->morph_b[pid]);
        t->base[pid] = (uint8_t)iclamp(next, 0,
            is_lfo_destination_param(pid) ? MONO_LFO_DESTINATIONS - 1 : 127);
        t->effective[pid] = t->base[pid];
    }
    remember_machine(t);
}

static void init_track(mono_track_t *t, int index, int delay_frames) {
    memset(t, 0, sizeof(*t));
    t->machine = MONO_SWAVE_SAW;
    t->note = -1;
    t->last_note = 48 + index * 5;
    t->seq_start = 0;
    t->seq_len = 16;
    t->seq_division = 1;
    t->seq_cursor = -1;
    t->seq_direction = 1;
    t->queued_cursor = -1;
    t->queued_direction = 1;
    t->early_fired_step = -1;
    t->play_step = -1;
    t->seq_rng = 0x9e3779b9u ^ (uint32_t)(index * 0x85ebca6bu);
    t->noise = 0x1234567u ^ (uint32_t)(index * 0x9e3779b9u);
    t->edit_rng = 0xa511e9b3u ^ (uint32_t)(index * 0x27d4eb2du);
    t->sid_lfsr = (0x7ffff8u ^ (uint32_t)(index * 0x5bd1e995u)) & 0x7fffffu;
    if (!t->sid_lfsr) t->sid_lfsr = 0x7ffff8u;
    for (int i = 0; i < MONO_STEPS; ++i) clear_step(&t->steps[i]);
    common_defaults(t);
    machine_defaults(t, MONO_SWAVE_SAW);
    remember_machine(t);
    memcpy(t->effective, t->base, MONO_PARAMS);
    for (int i = 0; i < 3; ++i) t->lfo[i].rng = t->noise + (uint32_t)i * 7919u;
    performance_defaults(t);
    t->arp_direction = 1;
    t->delay = calloc((size_t)delay_frames * 2, sizeof(float));
    int sample_rate = delay_frames / MONO_DELAY_SECONDS;
    t->track_fx_buffer = calloc((size_t)MONO_TRACK_FX_SECONDS * sample_rate * 2,
                                sizeof(float));
}

mono_t *mono_create(const host_api_v1_t *host, int track_count) {
    return mono_create_with_storage(host, track_count, NULL);
}

mono_t *mono_create_with_storage(const host_api_v1_t *host, int track_count,
                                 const char *user_wave_path) {
    mono_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->host = host;
    m->undo_snapshot = calloc(1, sizeof(*m->undo_snapshot));
    m->swap_snapshot = calloc(1, sizeof(*m->swap_snapshot));
    if (!m->undo_snapshot || !m->swap_snapshot) {
        mono_destroy(m);
        return NULL;
    }
    m->sample_rate = host && host->sample_rate > 0 ? host->sample_rate : MOVE_SAMPLE_RATE;
    m->track_count = iclamp(track_count, 1, MONO_MAX_TRACKS);
    m->pattern_start = 0;
    m->pattern_len = 16;
    m->play_order = MONO_PLAY_FORWARD;
    m->swing = 0;
    m->song_length = 1;
    m->song_edit_row = 0;
    for (int row = 0; row < MONO_SONG_ROWS; ++row) {
        m->song[row].start = 0;
        m->song[row].length = 16;
        m->song[row].repeats = 1;
        m->song[row].transpose = 0;
    }
    m->edit_step = 0;
    m->seq_step = -1;
    m->seq_direction = 1;
    m->seq_rng = 0x51f15e5du;
    m->master = m->track_count > 1 ? 0.34f : 0.7f;
    m->calibration_level = 32;
    m->calibration_noise = 0x8f3a21b7u;
    m->smooth_coeff = 1.0f - expf(-MONO_CONTROL_INTERVAL /
                                  (0.030f * m->sample_rate));
    build_control_tables(m);
    generate_wavetables(m);
    if (user_wave_path && *user_wave_path)
        snprintf(m->user_wave_path, sizeof(m->user_wave_path), "%s", user_wave_path);
    load_user_waves(m);
    int delay_frames = m->sample_rate * MONO_DELAY_SECONDS;
    for (int i = 0; i < m->track_count; ++i) {
        init_track(&m->track[i], i, delay_frames);
        m->track[i].smooth_coeff = m->smooth_coeff;
        if (!m->track[i].delay || !m->track[i].track_fx_buffer) {
            mono_destroy(m);
            return NULL;
        }
    }
    return m;
}

void mono_destroy(mono_t *m) {
    if (!m) return;
    for (int i = 0; i < MONO_MAX_TRACKS; ++i) {
        free(m->track[i].delay);
        free(m->track[i].track_fx_buffer);
    }
    free(m->undo_snapshot);
    free(m->swap_snapshot);
    free(m);
}

static void env_trigger(mono_env_t *e, int attack_param, int sample_rate) {
    e->hold_left = 0;
    e->release_step = 0.0f;
    if (attack_param <= 0) {
        e->value = 1.0f;
        e->stage = ENV_HOLD;
    } else {
        e->stage = ENV_ATTACK;
        if (e->value < 0.0f || e->value > 1.0f) e->value = 0.0f;
    }
    (void)sample_rate;
}

static void env_release(const mono_t *m, mono_env_t *e, int release_param) {
    if (e->stage == ENV_OFF) return;
    float sec = m->time_8[iclamp(release_param, 0, 127)];
    if (sec <= 0.0f) {
        e->value = 0.0f;
        e->stage = ENV_OFF;
    } else {
        e->release_step = e->value / (sec * m->sample_rate);
        e->stage = ENV_RELEASE;
    }
}

static float amp_env_tick(const mono_t *m, mono_env_t *e, const uint8_t *p) {
    switch (e->stage) {
    case ENV_ATTACK: {
        float sec = m->time_4[p[0]];
        e->value += sec <= 0.0f ? 1.0f : 1.0f / (sec * m->sample_rate);
        if (e->value >= 1.0f) {
            e->value = 1.0f;
            e->hold_left = (int)(m->time_4[p[1]] * m->sample_rate);
            e->stage = ENV_HOLD;
        }
        break;
    }
    case ENV_HOLD:
        if (e->hold_left <= 0)
            e->hold_left = (int)(m->time_4[p[1]] * m->sample_rate);
        if (--e->hold_left <= 0) e->stage = ENV_DECAY;
        break;
    case ENV_DECAY: {
        float sec = m->time_12[p[2]];
        e->value -= sec <= 0.0f ? 1.0f : 1.0f / (sec * m->sample_rate);
        if (e->value <= 0.0f) { e->value = 0.0f; e->stage = ENV_OFF; }
        break;
    }
    case ENV_RELEASE:
        e->value -= e->release_step;
        if (e->value <= 0.0f) { e->value = 0.0f; e->stage = ENV_OFF; }
        break;
    default: break;
    }
    return e->value;
}

static float filter_env_tick(const mono_t *m, mono_env_t *e, const uint8_t *p) {
    if (e->stage == ENV_ATTACK) {
        float sec = m->time_4[p[4]];
        e->value += sec <= 0.0f ? 1.0f : 1.0f / (sec * m->sample_rate);
        if (e->value >= 1.0f) { e->value = 1.0f; e->stage = ENV_DECAY; }
    } else if (e->stage == ENV_DECAY || e->stage == ENV_HOLD) {
        float sec = m->time_8[p[5]];
        e->value -= sec <= 0.0f ? 1.0f : 1.0f / (sec * m->sample_rate);
        if (e->value <= 0.0f) { e->value = 0.0f; e->stage = ENV_OFF; }
    }
    return e->value;
}

static void lfo_trigger(mono_lfo_t *l, const uint8_t *p) {
    l->age_samples = 0;
    l->slewed = 0.0f;
    int mode = (p[1] * 5) / 128;
    if (mode != 0) {
        l->phase = pnorm(p[7]);
        l->stopped = 0;
        if (mode == 2) l->held = 0.0f;
    }
}

static float lfo_tick(const mono_t *m, mono_lfo_t *l, const uint8_t *p,
                      const uint8_t *x, float bpm, int velocity, int note) {
    int wave = (p[2] * 5) / 128;
    int mode = (p[1] * 5) / 128;
    static const float mults[8] = { 0.125f, 0.25f, 0.5f, 1, 2, 4, 8, 16 };
    int mi = (p[3] * 8) / 128;
    int key = iclamp(x[7], 0, 127);
    float key_rate = m->lfo_key_rate[key][iclamp(note, 0, 127)];
    float hz = (bpm / 60.0f) * mults[mi] * (0.125f + 3.875f * pnorm(p[4])) * key_rate;
    float old = l->phase;
    if (!l->stopped) {
        l->phase += hz / m->sample_rate;
        if (l->phase >= 1.0f) l->phase -= 1.0f;
    }
    if (l->phase < old && wave == 4)
        l->held = ((xrnd(&l->rng) >> 8) / 8388607.5f) - 1.0f;
    float symmetry = fclamp(0.5f + ((int)x[3] - 64) / 128.0f, 0.02f, 0.98f);
    float shaped_phase = l->phase < symmetry
        ? 0.5f * l->phase / symmetry
        : 0.5f + 0.5f * (l->phase - symmetry) / (1.0f - symmetry);
    float v;
    switch (wave) {
    case 1: v = saw(shaped_phase); break;
    case 2: v = tri(shaped_phase); break;
    case 3: v = shaped_phase < 0.5f ? 1.0f : -1.0f; break;
    case 4: v = l->held; break;
    default: v = sinf(2.0f * (float)M_PI * shaped_phase); break;
    }
    if (mode == 2) {
        if (old == pnorm(p[7])) l->held = v;
        v = l->held;
    } else if (mode == 3 && l->phase < old) {
        l->stopped = 1;
    } else if (mode == 4 && old < 0.5f && l->phase >= 0.5f) {
        l->stopped = 1;
    }
    int interlace = p[5];
    if (interlace && ((int)(l->phase * (2 + interlace / 8)) & 1)) v = 0.0f;
    if (x[4]) {
        int levels = 2 + x[4] / 4;
        v = roundf(v * (levels - 1)) / (levels - 1);
    }
    if (x[5] < 64) v = 0.5f * (v + 1.0f);
    l->slewed += (v - l->slewed) * m->slew_coeff[x[2]];
    float delay = m->time_4[x[1]];
    float fade = m->time_8[x[0]];
    float age = l->age_samples++ / (float)m->sample_rate;
    float fade_gain = age < delay ? 0.0f
        : (fade > 0.0f ? fclamp((age - delay) / fade, 0.0f, 1.0f) : 1.0f);
    float velocity_mix = pnorm(x[6]);
    float velocity_gain = 1.0f - velocity_mix + velocity_mix * velocity / 127.0f;
    return l->slewed * pnorm(p[6]) * fade_gain * velocity_gain;
}

static void track_pitch(mono_track_t *t, int note, int velocity) {
    t->note = note;
    t->last_note = note;
    t->velocity = iclamp(velocity, 1, 127);
    t->target_freq = midi_freq(note);
    if (t->freq <= 0.0f || t->effective[15] == 0) t->freq = t->target_freq;
}

static int most_recent_held_note(const mono_track_t *t) {
    uint32_t newest = 0;
    int selected = -1;
    for (int note = 0; note < 128; ++note) {
        if (t->held_notes[note] && (selected < 0 || t->held_order[note] > newest)) {
            newest = t->held_order[note];
            selected = note;
        }
    }
    return selected;
}

static void hold_note(mono_track_t *t, int note, int velocity) {
    note = iclamp(note, 0, 127);
    if (++t->note_order == 0) {
        /* A wrap would only happen after billions of note-ons. Preserve a
         * deterministic last-note ordering instead of letting zero sort old. */
        t->note_order = 1;
        memset(t->held_order, 0, sizeof(t->held_order));
    }
    t->held_notes[note] = 1;
    t->held_velocity[note] = (uint8_t)iclamp(velocity, 1, 127);
    t->held_order[note] = t->note_order;
}

static void track_trigger(mono_t *m, mono_track_t *t, int note,
                          int velocity, int trig_mask, int gate) {
    if ((trig_mask & TRIG_NOTE) && note >= 0) {
        track_pitch(t, note, velocity);
        t->voice_age_samples = 0;
        /* PWRS and WPRS restart their internal sweeps from the panel value on
         * note-on. The main oscillator phase remains free-running. */
        if (t->machine == MONO_SID_6581 && t->effective[2] >= 64)
            t->mod_phase[1] = 0.0f;
        if (t->machine == MONO_SWAVE_PULSE && t->effective[6] >= 64)
            t->mod_phase[0] = 0.0f;
        if (t->machine == MONO_DIGIPRO_WAVE && t->effective[3] >= 64)
            t->mod_phase[0] = 0.0f;
    }
    if (trig_mask & TRIG_AMP) env_trigger(&t->amp, t->effective[8], m->sample_rate);
    if (trig_mask & TRIG_FILTER) {
        t->filter_env.value = 0.0f;
        t->filter_env.stage = ENV_ATTACK;
    }
    if (trig_mask & TRIG_LFO)
        for (int i = 0; i < 3; ++i) lfo_trigger(&t->lfo[i], t->effective + (4 + i) * 8);
    if (gate > 0) {
        float frames_per_step = m->sample_rate * 60.0f / (bpm_now(m) * 4.0f);
        t->gate_left = (int)(frames_per_step * fclamp(gate / 127.0f, 0.03f, 1.0f));
    } else {
        /* A played note belongs to the performer until note-off. Sequencer
         * trigs pass their positive gate value and retain step-length gates. */
        t->gate_left = 0;
    }
}

static int note_set_count(const uint8_t *notes) {
    int count = 0;
    for (int note = 0; note < 128; ++note) count += notes[note] != 0;
    return count;
}

static void reset_arp_runtime(mono_track_t *t) {
    t->arp_frames = 0.0;
    t->arp_cursor = -1;
    t->arp_direction = 1;
    t->arp_pattern_pos = 0;
}

static double arp_interval_frames(const mono_t *m, const mono_track_t *t,
                                  float bpm) {
    static const float steps_per_beat[8] = {1, 2, 3, 4, 6, 8, 12, 16};
    int rate = iclamp(t->arp.rate, 0, 7);
    return m->sample_rate * 60.0 / (fmaxf(30.0f, bpm) * steps_per_beat[rate]);
}

static int arp_pick_note(mono_track_t *t) {
    int notes[128], count = 0;
    for (int note = 0; note < 128; ++note)
        if (t->held_notes[note]) notes[count++] = note;
    if (count == 0) return -1;
    if (t->arp.mode == 4) { /* played order */
        for (int i = 0; i < count - 1; ++i)
            for (int j = i + 1; j < count; ++j)
                if (t->held_order[notes[j]] < t->held_order[notes[i]]) {
                    int swap = notes[i]; notes[i] = notes[j]; notes[j] = swap;
                }
    }
    int octaves = iclamp(t->arp.octaves, 1, 4);
    int total = count * octaves;
    int position;
    switch (t->arp.mode) {
    case 1: /* down */
        t->arp_cursor = t->arp_cursor < 0 ? total - 1 : (t->arp_cursor - 1 + total) % total;
        position = t->arp_cursor;
        break;
    case 2: /* pendulum */
        if (t->arp_cursor < 0) { t->arp_cursor = 0; t->arp_direction = 1; }
        else if (total > 1) {
            int next = t->arp_cursor + t->arp_direction;
            if (next >= total) { t->arp_direction = -1; next = total - 2; }
            else if (next < 0) { t->arp_direction = 1; next = 1; }
            t->arp_cursor = next;
        }
        position = t->arp_cursor;
        break;
    case 3: /* random */
        position = (int)(xrnd(&t->edit_rng) % (uint32_t)total);
        t->arp_cursor = position;
        break;
    case 5: { /* converge low/high toward the center */
        t->arp_cursor = (t->arp_cursor + 1) % total;
        int half = t->arp_cursor / 2;
        position = (t->arp_cursor & 1) ? total - 1 - half : half;
        break;
    }
    default: /* up / played order */
        t->arp_cursor = (t->arp_cursor + 1) % total;
        position = t->arp_cursor;
        break;
    }
    int source = notes[position % count];
    int octave = position / count;
    int length = iclamp(t->arp.length, 1, MONO_ARP_STEPS);
    int offset = t->arp.offset[t->arp_pattern_pos % length];
    t->arp_pattern_pos = (t->arp_pattern_pos + 1) % length;
    if (!t->arp.velocity && t->held_velocity[source])
        t->velocity = t->held_velocity[source];
    return iclamp(source + octave * 12 + offset, 0, 127);
}

static void arp_tick(mono_t *m, mono_track_t *t, float bpm) {
    if (!t->arp.enabled) return;
    if (note_set_count(t->held_notes) == 0) {
        t->arp_frames = 0.0;
        return;
    }
    if (t->arp_frames > 0.0) {
        t->arp_frames -= 1.0;
        return;
    }
    double interval = arp_interval_frames(m, t, bpm);
    int note = arp_pick_note(t);
    if (note >= 0) {
        int velocity = t->arp.velocity ? t->arp.velocity
                                       : iclamp(t->velocity, 1, 127);
        memcpy(t->effective, t->base, MONO_PARAMS);
        track_trigger(m, t, note, velocity, 15, 0);
        t->gate_left = (int)fmax(1.0, interval * fclamp(t->arp.gate / 127.0f,
                                                        0.03f, 1.0f));
    }
    t->arp_frames += interval;
}

void mono_note_on(mono_t *m, int track, int note, int velocity) {
    if (!m || track < 0 || track >= m->track_count) return;
    mono_track_t *t = &m->track[track];
    /* While clocked, keep the current automation targets under incoming Move
     * notes. Stopped performance notes still start from the base patch. */
    if (!m->transport) memcpy(t->effective, t->base, MONO_PARAMS);
    note = iclamp(note, 0, 127);
    if (t->arp.enabled) {
        if (t->arp.latch && note_set_count(t->physical_notes) == 0) {
            memset(t->held_notes, 0, sizeof(t->held_notes));
            memset(t->held_order, 0, sizeof(t->held_order));
            reset_arp_runtime(t);
        }
        t->physical_notes[note] = 1;
        hold_note(t, note, velocity);
        if (note_set_count(t->held_notes) == 1) reset_arp_runtime(t);
        ++m->note_events;
        changed(m);
        return;
    }
    hold_note(t, note, velocity);
    track_trigger(m, t, note, velocity, 15, 0);
    ++m->note_events;
    changed(m);
}

void mono_note_off(mono_t *m, int track, int note) {
    if (!m || track < 0 || track >= m->track_count) return;
    mono_track_t *t = &m->track[track];
    if (t->arp.enabled) {
        if (note < 0) memset(t->physical_notes, 0, sizeof(t->physical_notes));
        else if (note < 128) t->physical_notes[note] = 0;
        if (!t->arp.latch) {
            if (note < 0) {
                memset(t->held_notes, 0, sizeof(t->held_notes));
                memset(t->held_order, 0, sizeof(t->held_order));
            } else if (note < 128) {
                t->held_notes[note] = 0;
                t->held_order[note] = 0;
            }
            if (note_set_count(t->held_notes) == 0) {
                env_release(m, &t->amp, t->effective[11]);
                t->note = -1;
                t->gate_left = 0;
                reset_arp_runtime(t);
            }
        }
        changed(m);
        return;
    }
    if (note < 0) {
        memset(t->held_notes, 0, sizeof(t->held_notes));
        memset(t->held_order, 0, sizeof(t->held_order));
    } else if (note < 128) {
        t->held_notes[note] = 0;
        t->held_order[note] = 0;
    }
    if (note < 0 || t->note == note) {
        int fallback = most_recent_held_note(t);
        if (fallback >= 0) {
            /* Return to the previous held key without retriggering envelopes;
             * portamento still controls the pitch transition. */
            track_pitch(t, fallback, t->held_velocity[fallback]);
        } else {
            env_release(m, &t->amp, t->effective[11]);
            t->note = -1;
            t->gate_left = 0;
        }
    }
    changed(m);
}

static float osc_swavesaw(mono_t *m, mono_track_t *t, float freq) {
    const uint8_t *p = t->effective;
    const uint8_t *a = t->effective + MONO_ALT_BASE;
    float stack = fclamp(1.0f + ((int)a[1] - 21) * (2.0f / 106.0f), 0.6f, 3.0f);
    float width = 0.009f * pnorm(p[1]) * stack;
    float ext = width * 2.0f;
    phase_step(&t->phase[0], freq, m->sample_rate);
    float v = saw_blep(t->phase[0], freq / m->sample_rate);
    v += sinf(4.0f * (float)M_PI * t->phase[0]) * pnorm(a[0]) * 0.45f;
    float ul = pnorm(p[0]);
    float ux = ul * pnorm(p[2]);
    const float ratios[4] = { 1.0f - width, 1.0f + width, 1.0f - ext, 1.0f + ext };
    for (int i = 0; i < 4; ++i) {
        float voice_freq = freq * ratios[i];
        phase_step(&t->phase[1 + i], voice_freq, m->sample_rate);
        v += saw_blep(t->phase[1 + i], voice_freq / m->sample_rate) *
             (i < 2 ? ul : ux);
    }
    phase_step(&t->phase[5], freq * 0.5f, m->sample_rate);
    phase_step(&t->phase[6], freq * 0.5f, m->sample_rate);
    phase_step(&t->phase[7], freq * 0.25f, m->sample_rate);
    float sub_pw = fclamp(0.5f + ((int)a[2] - 64) * (0.9f / 127.0f), 0.05f, 0.95f);
    v += pulse_blep(t->phase[5], sub_pw, freq * 0.5f / m->sample_rate) * pnorm(p[3]);
    v += sinf(2.0f * (float)M_PI * t->phase[6]) * pnorm(p[4]);
    v += sinf(2.0f * (float)M_PI * t->phase[7]) * pnorm(p[5]);
    phase_step(&t->phase[8], freq * 0.125f, m->sample_rate);
    float sub_oct = pnorm(a[3]);
    v += sinf(2.0f * (float)M_PI * t->phase[8]) * sub_oct;
    return v / (1.0f + 2.0f * ul + 2.0f * ux + pnorm(p[3]) +
                pnorm(p[4]) + pnorm(p[5]) + sub_oct);
}

static float osc_swavepulse(mono_t *m, mono_track_t *t, float freq) {
    const uint8_t *p = t->effective;
    const uint8_t *a = t->effective + MONO_ALT_BASE;
    float det = 0.009f * pnorm(p[1]);
    float pw = 0.5f + ((int)p[4] - 64) * (0.46f / 64.0f);
    if (p[5]) {
        float sweep_hz = 0.04f * powf(180.0f, pnorm(p[5]));
        phase_step(&t->mod_phase[0], sweep_hz, m->sample_rate);
        pw += sinf(2.0f * (float)M_PI * t->mod_phase[0]) *
              pnorm(p[5]) * 0.46f;
    }
    pw = fclamp(pw, 0.03f, 0.97f);
    float v = 0.0f;
    float ratios[5] = { 1.0f, 1.0f - det, 1.0f + det,
                        1.0f - 2.0f * det, 1.0f + 2.0f * det };
    float stack = pnorm(a[1]);
    float levels[5] = { 1.0f, pnorm(p[0]), pnorm(p[0]), stack, stack };
    float sum = 0.0f;
    if (a[3] && phase_step(&t->mod_phase[2], freq * (1.0f + 7.0f * pnorm(a[3])),
                            m->sample_rate))
        for (int i = 0; i < 5; ++i) t->phase[i] = 0.0f;
    float asym = ((int)a[0] - 64) / 64.0f * 0.25f;
    for (int i = 0; i < 5; ++i) {
        phase_step(&t->phase[i], freq * ratios[i], m->sample_rate);
        float voice_pw = fclamp(pw + asym * (i * 0.25f), 0.03f, 0.97f);
        v += pulse_blep(t->phase[i], voice_pw,
                        freq * ratios[i] / m->sample_rate) * levels[i];
        sum += levels[i];
    }
    v += saw_blep(t->phase[0], freq / m->sample_rate) * asym * sum * 0.35f;
    phase_step(&t->phase[5], freq * 0.5f, m->sample_rate);
    phase_step(&t->phase[6], freq * 0.25f, m->sample_rate);
    v += sinf(2.0f * (float)M_PI * t->phase[5]) * pnorm(p[2]);
    v += sinf(2.0f * (float)M_PI * t->phase[6]) * pnorm(p[3]);
    sum += pnorm(p[2]) + pnorm(p[3]);
    float extra_sub = pnorm(a[2]);
    phase_step(&t->phase[7], freq * 0.5f, m->sample_rate);
    v += pulse_blep(t->phase[7], 0.5f,
                    freq * 0.5f / m->sample_rate) * extra_sub;
    sum += extra_sub;
    return v / fmaxf(sum, 1.0f);
}

static float chord_offset(int p) {
    if (p < 4) return -1000.0f;
    if (p >= 112) {
        static const float just_ratios[4] = { 6.0f / 5.0f, 5.0f / 4.0f,
                                              4.0f / 3.0f, 3.0f / 2.0f };
        int index = iclamp((p - 112) / 4, 0, 3);
        return 12.0f * log2f(just_ratios[index]);
    }
    return roundf((p - 4) * 24.0f / 107.0f);
}

static float ensemble_wave(float phase, float shape, float pw, float dt) {
    float saw_v = saw_blep(phase, dt);
    float square_v = pulse_blep(phase, pw, dt);
    float spike_width = 0.015f + 0.235f * (1.0f - pw);
    float spike_v = pulse_blep(phase, spike_width, dt);
    return shape < 0.5f
        ? saw_v + (square_v - saw_v) * (shape * 2.0f)
        : square_v + (spike_v - square_v) * ((shape - 0.5f) * 2.0f);
}

static float osc_ensemble(mono_t *m, mono_track_t *t, float freq) {
    const uint8_t *p = t->effective;
    float shape = pnorm(p[3]);
    float pw = fclamp(0.05f + 0.90f * pnorm(p[4]), 0.05f, 0.95f);
    float chorus_level = pnorm(p[5]);
    float chorus_width = 0.006f * pnorm(p[6]);
    float v = 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float semi = i == 0 ? 0.0f : chord_offset(p[i - 1]);
        if (semi < -100.0f) continue;
        float voice_freq = freq * powf(2.0f, semi / 12.0f);
        float spread = chorus_width * (i - 1.5f) / 1.5f;
        float main_freq = voice_freq * (1.0f - 0.5f * spread);
        float chorus_freq = voice_freq * (1.0f + spread);
        phase_step(&t->phase[i], main_freq, m->sample_rate);
        phase_step(&t->phase[4 + i], chorus_freq, m->sample_rate);
        float main_v = ensemble_wave(t->phase[i], shape, pw,
                                     main_freq / m->sample_rate);
        float chorus_v = ensemble_wave(t->phase[4 + i], shape, pw,
                                       chorus_freq / m->sample_rate);
        float level = pnorm(t->effective[MONO_ALT_BASE + i]);
        v += (main_v + chorus_v * chorus_level) * level;
        sum += level * (1.0f + chorus_level);
    }
    return v / fmaxf(sum, 1.0f);
}

static float previous_track_frequency(const mono_t *m, const mono_track_t *t,
                                      float fallback) {
    int index = (int)(t - m->track);
    if (index > 0) {
        const mono_track_t *previous = &m->track[index - 1];
        if (previous->freq > 0.0f) return previous->freq;
        if (previous->target_freq > 0.0f) return previous->target_freq;
    }
    return fallback;
}

static void sid_noise_step(mono_track_t *t) {
    uint32_t feedback = ((t->sid_lfsr >> 22) ^ (t->sid_lfsr >> 17)) & 1u;
    t->sid_lfsr = ((t->sid_lfsr << 1) | feedback) & 0x7fffffu;
    if (!t->sid_lfsr) t->sid_lfsr = 0x7ffff8u;
}

static float sid_noise_value(const mono_track_t *t) {
    static const uint8_t taps[12] = { 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0 };
    unsigned word = 0;
    for (int bit = 0; bit < 12; ++bit)
        word |= ((t->sid_lfsr >> taps[bit]) & 1u) << bit;
    return word / 2047.5f - 1.0f;
}

static float osc_sid(mono_t *m, mono_track_t *t, float freq) {
    const uint8_t *p = t->effective;
    const uint8_t *a = t->effective + MONO_ALT_BASE;
    float selected_mod_freq = midi_freq((float)p[6]);
    float mod_freq = p[5] >= 64
        ? previous_track_frequency(m, t, selected_mod_freq)
        : selected_mod_freq;
    int mod_wrap = (int)phase_step(&t->mod_phase[0], mod_freq, m->sample_rate);
    int mode = (p[4] * 4) / 128;
    if ((mode == 2 || mode == 3) && mod_wrap) t->phase[0] = 0.0f;
    phase_step(&t->phase[0], freq, m->sample_rate);
    float digital_phase = floorf(t->phase[0] * 16777216.0f) / 16777216.0f;
    phase_step(&t->mod_phase[1], 0.05f + freq / 512.0f, m->sample_rate);
    float pw_sweep = sinf(2.0f * (float)M_PI * t->mod_phase[1]) *
                     pnorm(p[1]) * 0.46f;
    float pw = fclamp(0.02f + 0.96f * pnorm(p[0]) + pw_sweep,
                      0.02f, 0.98f);
    int wave = (p[3] * 5) / 128;
    int ring_enabled = mode == 1 || mode == 3;
    float ring_phase = digital_phase;
    if (ring_enabled && t->mod_phase[0] >= 0.5f)
        ring_phase = fmodf(ring_phase + 0.5f, 1.0f);
    float tri_v = tri(ring_phase);
    float saw_v = saw(digital_phase);
    float pulse_v = pulse(digital_phase, pw);
    t->sid_noise_clock += freq * 32.0f / m->sample_rate;
    while (t->sid_noise_clock >= 1.0f) {
        sid_noise_step(t);
        t->sid_noise_clock -= 1.0f;
    }
    float v;
    switch (wave) {
    case 0: v = tri_v; break;
    case 1: v = saw_v; break;
    case 2: v = pulse_v; break;
    case 3: {
        /* The SID combined waveform is not a linear crossfade. A soft digital
         * AND keeps the hollow, level-dependent character without copying a
         * chip lookup table. */
        float gate = 0.5f * (pulse_v + 1.0f);
        v = tanhf((0.70f * saw_v + 0.30f * tri_v) * gate * 2.2f);
        break;
    }
    default: v = sid_noise_value(t); break;
    }
    float mod = sinf(2.0f * (float)M_PI * t->mod_phase[0]);
    float ring = pnorm(a[0]);
    v += (v * (t->mod_phase[0] < 0.5f ? -1.0f : 1.0f) - v) * ring;
    float sub = pnorm(a[1]);
    phase_step(&t->phase[1], freq * 0.5f, m->sample_rate);
    v = (v + pulse_blep(t->phase[1], 0.5f,
                        freq * 0.5f / m->sample_rate) * sub) / (1.0f + sub);
    v += mod * pnorm(a[2]) * 0.5f;
    if (a[3]) {
        float chaos = ((int32_t)xrnd(&t->noise)) / 2147483648.0f;
        float mix = pnorm(a[3]) * 0.45f;
        v += (chaos - v) * mix;
    }
    return roundf(v * 2047.0f) / 2047.0f;
}

static float table_read(const float *tab, float phase) {
    float x = phase * MONO_WAVE_LEN;
    int i = (int)x & (MONO_WAVE_LEN - 1);
    int j = (i + 1) & (MONO_WAVE_LEN - 1);
    float f = x - floorf(x);
    return tab[i] + (tab[j] - tab[i]) * f;
}

static float osc_digipro(mono_t *m, mono_track_t *t, float freq) {
    const uint8_t *p = t->effective;
    int wave = (p[0] * MONO_WAVES) / 128;
    int next = (wave + 1) % MONO_WAVES;
    float morph = pnorm(p[1]);
    if (p[2]) {
        float sweep_hz = 0.015f * powf(1200.0f, pnorm(p[2]));
        phase_step(&t->mod_phase[0], sweep_hz, m->sample_rate);
        float sweep = t->mod_phase[0] < 0.5f
            ? t->mod_phase[0] * 2.0f
            : 2.0f - t->mod_phase[0] * 2.0f;
        morph += (1.0f - morph) * sweep;
    }
    int sync_mode = (p[4] * 3) / 128;
    if (sync_mode) {
        float selected = midi_freq((float)p[5]);
        float sync_hz = sync_mode == 2
            ? previous_track_frequency(m, t, selected) : selected;
        if (phase_step(&t->mod_phase[1], sync_hz, m->sample_rate))
            t->phase[0] = 0.0f;
    }
    phase_step(&t->phase[0], freq, m->sample_rate);
    float a = table_read(m->wavetable[wave], t->phase[0]);
    float b = table_read(m->wavetable[next], t->phase[0]);
    float primary = a + (b - a) * morph;
    int wave2 = (t->effective[MONO_ALT_BASE] * MONO_WAVES) / 128;
    float detune = powf(2.0f, ((int)t->effective[MONO_ALT_BASE + 2] - 64) /
                                      (64.0f * 12.0f));
    int octave = (int)lrintf(((int)t->effective[MONO_ALT_BASE + 3] - 64) / 32.0f);
    phase_step(&t->phase[1], freq * detune * powf(2.0f, (float)octave), m->sample_rate);
    float secondary = table_read(m->wavetable[wave2], t->phase[1]);
    float blend = pnorm(t->effective[MONO_ALT_BASE + 1]);
    return primary + (secondary - primary) * blend;
}

static float fm_listed_ratio(int parameter) {
    static const float ratios[32] = {
        1.0f / 64.0f, 1.0f / 32.0f, 1.0f / 16.0f, 3.0f / 32.0f,
        1.0f / 8.0f, 3.0f / 16.0f, 1.0f / 4.0f, 5.0f / 16.0f,
        3.0f / 8.0f, 7.0f / 16.0f, 1.0f / 2.0f, 5.0f / 8.0f,
        3.0f / 4.0f, 7.0f / 8.0f, 1.0f, 1.25f,
        1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f,
        6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 12.0f, 16.0f, 24.0f
    };
    return ratios[(iclamp(parameter, 0, 127) * 32) / 128];
}

static float fm_combined_envelope(const mono_t *m, const mono_track_t *t,
                                  int parameter) {
    float amount = pnorm(parameter);
    if (amount <= 0.0f) return 0.0f;
    float age = t->voice_age_samples / (float)m->sample_rate;
    float decay = 0.008f + 3.0f * amount * amount;
    float transient = 1.0f / (1.0f + age / decay);
    transient *= transient;
    float sustain = fclamp((amount - 0.55f) / 0.45f, 0.0f, 1.0f);
    float initial = fclamp(amount / 0.55f, 0.0f, 1.0f);
    return sustain + (1.0f - sustain) * initial * transient;
}

static float osc_fm(mono_t *m, mono_track_t *t, float freq) {
    const uint8_t *p = t->effective;
    const uint8_t *a = t->effective + MONO_ALT_BASE;
    float fine = powf(2.0f, ((int)p[1] - 64) / (64.0f * 12.0f));
    phase_step(&t->mod_phase[0], freq * fm_listed_ratio(p[0]) * fine,
               m->sample_rate);
    float fine2 = powf(2.0f, ((int)a[0] - 64) / (64.0f * 12.0f));
    phase_step(&t->mod_phase[1], freq * fm_listed_ratio(p[4]) * fine2,
               m->sample_rate);
    float fb = pnorm(p[2]) * 3.5f;
    float m1 = sinf(2.0f * (float)M_PI * t->mod_phase[0] + t->fm_feedback * fb);
    float volume2 = pnorm(p[5]);
    float volume_feedback = fclamp((volume2 - 0.65f) / 0.35f, 0.0f, 1.0f) * 2.5f;
    float m2 = sinf(2.0f * (float)M_PI * t->mod_phase[1] +
                     t->fm_feedback2 * (volume_feedback + pnorm(a[1]) * 2.0f));
    phase_step(&t->mod_phase[2], freq * fm_listed_ratio(a[2]), m->sample_rate);
    float m3 = sinf(2.0f * (float)M_PI * t->mod_phase[2]);
    float tone = 0.15f + 1.35f * pnorm(p[6]);
    float index = fm_combined_envelope(m, t, p[3]) * 8.0f * tone;
    float index2 = volume2 * 8.0f * tone;
    phase_step(&t->phase[0], freq, m->sample_rate);
    float v = sinf(2.0f * (float)M_PI * t->phase[0] + m1 * index +
                   m2 * index2 + m3 * pnorm(a[3]) * 8.0f * tone);
    t->fm_feedback = m1;
    t->fm_feedback2 = m2;
    float drive = pnorm(p[6]) * 2.0f;
    return drive > 0.001f ? tanhf(v * (1.0f + drive)) / tanhf(1.0f + drive) : v;
}

static float secondary_finish(mono_track_t *t, float v) {
    const uint8_t *a = t->effective + MONO_ALT_BASE;
    if (a[5]) {
        float drive = 1.0f + pnorm(a[5]) * 12.0f;
        v = tanhf(v * drive) / tanhf(drive);
    }
    if (a[6]) {
        int bits = 16 - (a[6] * 12) / 127;
        float levels = (float)((1u << bits) - 1u);
        v = roundf(v * levels) / levels;
    }
    if (a[7]) {
        float noise = ((int32_t)xrnd(&t->noise)) / 2147483648.0f;
        float mix = pnorm(a[7]) * 0.35f;
        v += (noise - v) * mix;
    }
    return v;
}

static float oscillator(mono_t *m, mono_track_t *t, float freq) {
    float v;
    switch (t->machine) {
    case MONO_SWAVE_PULSE: v = osc_swavepulse(m, t, freq); break;
    case MONO_SWAVE_ENSEMBLE: v = osc_ensemble(m, t, freq); break;
    case MONO_SID_6581: v = osc_sid(m, t, freq); break;
    case MONO_DIGIPRO_WAVE: v = osc_digipro(m, t, freq); break;
    case MONO_FM_STATIC: v = osc_fm(m, t, freq); break;
    default: v = osc_swavesaw(m, t, freq); break;
    }
    return secondary_finish(t, v);
}

static void svf_coeff(const mono_t *m, mono_svf_coeff_t *c,
                      float cutoff_param, float resonance) {
    int cutoff = iclamp((int)lrintf(cutoff_param), 0, 127);
    float g = m->cutoff_g[cutoff];
    c->k = 2.0f - 1.95f * fclamp(resonance, 0.0f, 1.0f);
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

static void svf_coeff_hz(const mono_t *m, mono_svf_coeff_t *c,
                         float cutoff_hz, float resonance) {
    float hz = fclamp(cutoff_hz, 12.0f, m->sample_rate * 0.45f);
    float g = tanf((float)M_PI * hz / m->sample_rate);
    c->k = 2.0f - 1.95f * fclamp(resonance, 0.0f, 1.0f);
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

static float filter_ratio_at(const mono_t *m, float parameter) {
    float p = fclamp(parameter, 0.0f, 127.0f);
    int lo = (int)p;
    int hi = lo < 127 ? lo + 1 : lo;
    return m->filter_ratio[lo] +
           (m->filter_ratio[hi] - m->filter_ratio[lo]) * (p - lo);
}

static float svf(mono_svf_t *s, float in, const mono_svf_coeff_t *c,
                 int highpass) {
    /* Topology-preserving state-variable filter. The former Chamberlin
     * integrator became unstable at the default wide-open cutoff, poisoning
     * its state with NaNs after a few blocks and silencing every later note. */
    float v3 = in - s->low;
    float band = c->a1 * s->band + c->a2 * v3;
    float low = s->low + c->a2 * s->band + c->a3 * v3;
    s->band = 2.0f * band - s->band;
    s->low = 2.0f * low - s->low;
    float high = in - c->k * band - low;
    if (!isfinite(high) || !isfinite(low) || !isfinite(s->band) ||
        !isfinite(s->low)) {
        s->band = 0.0f;
        s->low = 0.0f;
        return 0.0f;
    }
    return highpass ? high : low;
}

static void process_track_fx(mono_t *m, mono_track_t *t, float *left, float *right) {
    int type = iclamp(t->route.fx_type, 0, 6);
    float mix = pnorm(t->route.fx_mix);
    float amount = pnorm(t->route.fx_amount);
    float tone = pnorm(t->route.fx_tone);
    float feedback = pnorm(t->route.fx_feedback) * 0.94f;
    float dry_l = *left, dry_r = *right;
    float wet_l = dry_l, wet_r = dry_r;
    int frames = m->sample_rate * MONO_TRACK_FX_SECONDS;
    phase_step(&t->track_fx_phase, 0.03f + 8.0f * amount, m->sample_rate);
    if (type == 1 || type == 2 || type == 4) {
        float mod = sinf(2.0f * (float)M_PI * t->track_fx_phase);
        int delay;
        if (type == 1) delay = (int)((0.006f + 0.018f * (0.5f + 0.5f * mod) * amount) * m->sample_rate);
        else if (type == 2) delay = (int)((0.0005f + 0.006f * (0.5f + 0.5f * mod) * amount) * m->sample_rate);
        else delay = (int)((0.055f + 0.34f * tone) * m->sample_rate);
        delay = iclamp(delay, 1, frames - 1);
        int read = t->track_fx_pos - delay;
        if (read < 0) read += frames;
        float dl = t->track_fx_buffer[read * 2];
        float dr = t->track_fx_buffer[read * 2 + 1];
        float damping = 0.03f + tone * 0.45f;
        t->track_fx_lp[0] += damping * (dl - t->track_fx_lp[0]);
        t->track_fx_lp[1] += damping * (dr - t->track_fx_lp[1]);
        dl = t->track_fx_lp[0]; dr = t->track_fx_lp[1];
        if (type == 1) {
            wet_l = 0.65f * dry_l + 0.70f * dl;
            wet_r = 0.65f * dry_r + 0.70f * dr;
        } else if (type == 2) {
            wet_l = dry_l + dl;
            wet_r = dry_r + dr;
        } else {
            wet_l = dl;
            wet_r = dr;
        }
        float write_fb = type == 1 ? feedback * 0.25f : feedback;
        t->track_fx_buffer[t->track_fx_pos * 2] = dry_l + dl * write_fb;
        t->track_fx_buffer[t->track_fx_pos * 2 + 1] = dry_r + dr * write_fb;
    } else if (type == 3) {
        float carrier = sinf(2.0f * (float)M_PI * t->track_fx_phase);
        wet_l = dry_l * carrier;
        wet_r = dry_r * carrier;
    } else if (type == 5) {
        float detector = fmaxf(fabsf(dry_l), fabsf(dry_r));
        float coeff = detector > t->track_fx_env ? 0.12f : 0.0015f + tone * 0.01f;
        t->track_fx_env += (detector - t->track_fx_env) * coeff;
        float threshold = 0.55f - amount * 0.48f;
        float gain = t->track_fx_env > threshold
            ? threshold / fmaxf(t->track_fx_env, 1.0e-5f) : 1.0f;
        wet_l = tanhf(dry_l * gain * (1.0f + amount * 5.0f));
        wet_r = tanhf(dry_r * gain * (1.0f + amount * 5.0f));
    } else if (type == 6) {
        int hold = 1 + (int)(amount * 31.0f);
        if (t->track_fx_hold-- <= 0) {
            int bits = 4 + (int)lrintf(tone * 8.0f);
            float levels = (float)((1u << bits) - 1u);
            t->track_fx_held[0] = roundf(dry_l * levels) / levels;
            t->track_fx_held[1] = roundf(dry_r * levels) / levels;
            t->track_fx_hold = hold;
        }
        wet_l = t->track_fx_held[0]; wet_r = t->track_fx_held[1];
    }
    if (++t->track_fx_pos >= frames) t->track_fx_pos = 0;
    float level = powf(2.0f, ((int)t->route.level - 64) / 32.0f);
    *left = (dry_l + (wet_l - dry_l) * mix) * level;
    *right = (dry_r + (wet_r - dry_r) * mix) * level;
}

static float render_track(mono_t *m, mono_track_t *t, float *right,
                          float bpm, int control_tick,
                          float neighbor_left, float neighbor_right) {
    process_sequence_events(m, t);
    if (t->retrig_left > 0 && --t->retrig_countdown <= 0) {
        track_trigger(m, t, t->retrig_note, t->retrig_velocity,
                      t->retrig_mask, t->retrig_gate);
        t->retrig_left--;
        t->retrig_countdown += t->retrig_interval;
    }
    uint8_t targets[MONO_PARAMS];
    uint8_t unmodulated[MONO_PARAMS];
    uint8_t modulated[MONO_PARAMS];
    float next_lfo_mod[6 * MONO_PAGE_PARAMS] = {0};
    int target_pid[3] = {-1, -1, -1};
    float target_delta[3] = {0};
    memcpy(targets, t->effective, sizeof(targets));
    if (control_tick) {
        for (int pid = 0; pid < MONO_PARAMS; ++pid) {
            if (is_smoothable_param(t, pid)) smooth_param_value(m, t, pid);
            else {
                t->smoothed[pid] = (float)t->effective[pid];
                t->control[pid] = t->effective[pid];
            }
        }
    }
    memcpy(unmodulated, t->control, sizeof(unmodulated));
    memcpy(modulated, unmodulated, sizeof(modulated));

    /* LFO controls receive the previous sample's modulation while the current
     * outputs are evaluated. All other destinations are applied immediately. */
    for (int pid = 0; pid < MONO_PARAMS; ++pid) {
        int mod_index = lfo_control_mod_index(pid);
        if (mod_index < 0) continue;
        int value = (int)lrintf(unmodulated[pid] + t->lfo_param_mod[mod_index]);
        int max = is_lfo_destination_param(pid) ? MONO_LFO_DESTINATIONS - 1 : 127;
        modulated[pid] = (uint8_t)iclamp(value, 0, max);
    }

    float pitch_mod = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const uint8_t *lp = modulated + (4 + i) * 8;
        const uint8_t *lx = modulated + MONO_SHIFT_BASE + (4 + i) * MONO_PAGE_PARAMS;
        int dest = lfo_destination_index(lp[0]);
        float v = lfo_tick(m, &t->lfo[i], lp, lx, bpm,
                           t->velocity, t->note >= 0 ? t->note : t->last_note);
        if (dest == 1) {
            pitch_mod += v * 24.0f;
        } else {
            int pid = lfo_destination_param(dest);
            if (pid >= 0 && pid < MONO_PARAMS) {
                target_pid[i] = pid;
                target_delta[i] = v * 64.0f;
            }
        }
    }

    /* At most three params are targeted per sample. Accumulate duplicate
     * routes without scanning all 112 params in the realtime path. */
    for (int i = 0; i < 3; ++i) {
        int pid = target_pid[i];
        if (pid < 0) continue;
        int already_applied = 0;
        float delta = 0.0f;
        for (int j = 0; j < 3; ++j) {
            if (target_pid[j] == pid) {
                if (j < i) already_applied = 1;
                delta += target_delta[j];
            }
        }
        if (already_applied) continue;
        int value = (int)lrintf(unmodulated[pid] + delta);
        int max = is_lfo_destination_param(pid) ? MONO_LFO_DESTINATIONS - 1 : 127;
        modulated[pid] = (uint8_t)iclamp(value, 0, max);
        int mod_index = lfo_control_mod_index(pid);
        if (mod_index >= 0) next_lfo_mod[mod_index] = delta;
    }
    memcpy(t->lfo_param_mod, next_lfo_mod, sizeof(next_lfo_mod));
    memcpy(t->effective, modulated, sizeof(modulated));

    if (t->gate_left > 0 && --t->gate_left == 0) {
        env_release(m, &t->amp, t->effective[11]);
        t->note = -1;
    }

    float port_sec = m->time_3[t->effective[15]];
    if (port_sec > 0.0f)
        t->freq += (t->target_freq - t->freq) / fmaxf(1.0f, port_sec * m->sample_rate);
    else
        t->freq = t->target_freq;
    float tune = ((int)t->effective[7] - 64) / 64.0f;
    float freq = t->freq * powf(2.0f, (pitch_mod + tune) / 12.0f);
    const uint8_t *alt = t->effective + MONO_ALT_BASE;
    if (alt[4]) {
        phase_step(&t->mod_phase[3], 0.05f + 0.45f * pnorm(alt[4]), m->sample_rate);
        float drift = sinf(2.0f * (float)M_PI * t->mod_phase[3]) *
                      pnorm(alt[4]) * 0.125f;
        freq *= powf(2.0f, drift / 12.0f);
    }
    float neighbor = 0.5f * (neighbor_left + neighbor_right);
    float route_amount = pnorm(t->route.route_amount);
    if (t->route.route_mode == 4)
        freq *= powf(2.0f, neighbor * route_amount * 2.0f);
    float x = oscillator(m, t, fclamp(freq, 1.0f, m->sample_rate * 0.45f));
    if (t->route.route_mode == 1)
        x = (x + neighbor * route_amount) / (1.0f + route_amount);
    else if (t->route.route_mode == 2)
        x = neighbor * route_amount;
    else if (t->route.route_mode == 3)
        x += (x * neighbor * 2.0f - x) * route_amount;
    if (t->voice_age_samples < UINT32_MAX) ++t->voice_age_samples;

    const uint8_t *ap = t->effective + 8;
    const uint8_t *fp = t->effective + 16;
    const uint8_t *ep = t->effective + 24;
    const uint8_t *ax = t->effective + MONO_SHIFT_BASE + MONO_PAGE_PARAMS;
    const uint8_t *fx = t->effective + MONO_SHIFT_BASE + 2 * MONO_PAGE_PARAMS;
    const uint8_t *ex = t->effective + MONO_SHIFT_BASE + 3 * MONO_PAGE_PARAMS;
    env_stage_t amp_stage = t->amp.stage;
    float aenv = amp_env_tick(m, &t->amp, ap);
    int curve_param = amp_stage == ENV_ATTACK ? ax[0]
        : (amp_stage == ENV_RELEASE ? ax[2] : ax[1]);
    float curve_power = powf(4.0f, ((int)curve_param - 64) / 64.0f);
    aenv = powf(fclamp(aenv, 0.0f, 1.0f), curve_power);
    aenv = 1.0f - pnorm(ax[5]) + aenv * pnorm(ax[5]);
    float fenv = filter_env_tick(m, &t->filter_env, fp) * pnorm(fx[2]);
    float velocity_sensitivity = pnorm(ax[3]);
    float vel = 1.0f - velocity_sensitivity +
                velocity_sensitivity * t->velocity / 127.0f;
    int performance_note = t->note >= 0 ? t->note : t->last_note;
    float key_level = ((int)ax[4] - 64) / 64.0f;
    float key_gain = powf(2.0f, ((performance_note - 60) / 12.0f) * key_level);
    float trim_gain = powf(2.0f, ((int)ax[7] - 64) / 32.0f);
    float drive = 1.0f + pnorm(ap[4]) * 15.0f;
    x = tanhf(x * drive) / tanhf(drive);
    x *= aenv * vel * key_gain * trim_gain;

    float filter_dry = x;
    if (fx[3]) {
        float filter_drive = 1.0f + pnorm(fx[3]) * 12.0f;
        x = tanhf(x * filter_drive) / tanhf(filter_drive);
    }
    float filter_velocity = ((int)fx[1] - 64) *
                            ((int)t->velocity - 64) / 127.0f;
    float base = fp[0] + (((int)fp[6] - 64) * fenv) + filter_velocity;
    float width = fp[1] + (((int)fp[7] - 64) * fenv);
    base = fclamp(base, 0.0f, 127.0f);
    width = fclamp(width, 0.0f, 127.0f);
    if (control_tick) {
        /* BASE is the high-pass edge and WDTH is the octave gap to the
         * low-pass edge. At full key tracking, BASE=0 begins two octaves
         * below the played note and each eight steps raises an edge by one
         * octave, matching the documented Monomachine response. */
        float tracked_note = 60.0f + (performance_note - 60) * pnorm(fx[0]);
        float reference_hz = midi_freq(tracked_note - 24.0f);
        t->filter_hp_hz = fclamp(reference_hz * filter_ratio_at(m, base),
                                 12.0f, m->sample_rate * 0.45f);
        t->filter_lp_hz = fclamp(t->filter_hp_hz * filter_ratio_at(m, width),
                                 12.0f, m->sample_rate * 0.45f);
        svf_coeff_hz(m, &t->hp_coeff, t->filter_hp_hz, pnorm(fp[2]));
        svf_coeff_hz(m, &t->lp_coeff, t->filter_lp_hz, pnorm(fp[3]));
    }
    x = svf(&t->hp[0], x, &t->hp_coeff, 1);
    if (fx[4] >= 64)
        x = svf(&t->hp[1], x, &t->hp_coeff, 1);
    x = svf(&t->lp[0], x, &t->lp_coeff, 0);
    if (fx[5] >= 64)
        x = svf(&t->lp[1], x, &t->lp_coeff, 0);
    x = filter_dry + (x - filter_dry) * pnorm(fx[6]);
    if (fx[7]) {
        float saturation = 1.0f + pnorm(fx[7]) * 10.0f;
        x = tanhf(x * saturation) / tanhf(saturation);
    }

    float eq_q = fclamp(0.4f + ((int)ex[0] - 64) * (0.35f / 64.0f), 0.05f, 0.95f);
    if (control_tick) svf_coeff(m, &t->eq_coeff, ep[0], eq_q);
    float band = svf(&t->eq, x, &t->eq_coeff, 0);
    float eq_wet = x + band * (((int)ep[1] - 64) / 64.0f);
    x += (eq_wet - x) * pnorm(ex[1]);

    int hold = 1 + (127 - ep[2]) / 4;
    if (t->srr_left-- <= 0) {
        t->srr_hold = roundf(x * 2047.0f) / 2047.0f;
        t->srr_left = hold;
    }
    x = t->srr_hold;
    if (ex[2]) {
        int bits = 16 - (ex[2] * 12) / 127;
        float levels = (float)((1u << bits) - 1u);
        x = roundf(x * levels) / levels;
    }

    float volume = pnorm(ap[5]);
    float pan_key = ((performance_note - 60) / 24.0f) *
                    (((int)ax[6] - 64) / 64.0f);
    float pan = fclamp(((int)ap[6] - 64) / 64.0f + pan_key, -1.0f, 1.0f);
    float lg = sqrtf(0.5f * (1.0f - pan));
    float rg = sqrtf(0.5f * (1.0f + pan));
    float left = x * volume * lg;
    *right = x * volume * rg;

    int delay_frames = m->sample_rate * MONO_DELAY_SECONDS;
    float delay_p = ep[4];
    int offset = (int)((0.015f + 1.82f * delay_p / 127.0f) * m->sample_rate);
    offset = iclamp(offset, 1, delay_frames - 1);
    phase_step(&t->delay_mod_phase, 0.02f + 5.0f * pnorm(ex[6]), m->sample_rate);
    int mod_frames = (int)(sinf(2.0f * (float)M_PI * t->delay_mod_phase) *
                           pnorm(ex[7]) * 0.025f * m->sample_rate);
    int rp_l = t->delay_pos - iclamp(offset + mod_frames, 1, delay_frames - 1);
    int rp_r = t->delay_pos - iclamp(offset - mod_frames, 1, delay_frames - 1);
    if (rp_l < 0) rp_l += delay_frames;
    if (rp_r < 0) rp_r += delay_frames;
    float dl = t->delay[rp_l * 2];
    float dr = t->delay[rp_r * 2 + 1];
    float hp_a = pnorm(ep[6]) * 0.995f;
    float lp_a = 0.02f + pnorm(ep[7]) * 0.975f;
    float d[2] = { dl, dr };
    for (int c = 0; c < 2; ++c) {
        float h = hp_a * (t->delay_hp[c] + d[c] - t->delay_hp_in[c]);
        t->delay_hp_in[c] = d[c];
        t->delay_hp[c] = h;
        t->delay_lp[c] += lp_a * (h - t->delay_lp[c]);
        d[c] = t->delay_lp[c];
    }
    if (ex[5]) {
        float delay_drive = 1.0f + pnorm(ex[5]) * 12.0f;
        d[0] = tanhf(d[0] * delay_drive) / tanhf(delay_drive);
        d[1] = tanhf(d[1] * delay_drive) / tanhf(delay_drive);
    }
    float send = pnorm(ep[3]);
    float fb = fclamp(pnorm(ep[5]) * 1.08f, 0.0f, 1.08f);
    float ping = pnorm(ex[3]);
    float fb_l = d[0] + (d[1] - d[0]) * ping;
    float fb_r = d[1] + (d[0] - d[1]) * ping;
    t->delay[t->delay_pos * 2] = left * send + fb_l * fb;
    t->delay[t->delay_pos * 2 + 1] = *right * send + fb_r * fb;
    if (++t->delay_pos >= delay_frames) t->delay_pos = 0;
    float duck = 1.0f - pnorm(ex[4]) * aenv;
    left += dl * send * duck;
    *right += dr * send * duck;
    process_track_fx(m, t, &left, right);
    memcpy(t->effective, targets, sizeof(targets));
    return left;
}

static void reset_sequence_cursors(mono_t *m) {
    m->seq_step = -1;
    m->seq_direction = 1;
    for (int ti = 0; ti < m->track_count; ++ti) {
        mono_track_t *t = &m->track[ti];
        t->seq_cursor = -1;
        t->seq_direction = 1;
        t->seq_div_counter = 0;
        t->play_step = -1;
        t->seq_due_count = 0;
        t->queued_cursor = -1;
        t->queued_direction = 1;
        t->queued_cycle = 0;
        t->queued_valid = 0;
        t->early_fired_step = -1;
        memset(t->seq_event, 0, sizeof(t->seq_event));
        t->retrig_left = 0;
        t->smooth_coeff = m->smooth_coeff;
        reset_arp_runtime(t);
    }
    m->song_play_row = 0;
    m->song_repeat = 0;
    m->song_step_count = 0;
}

/* Changing the global length is a live performance edit, not a transport
 * restart. Keep the current step/cursor when it still belongs to the resized
 * window and discard only look-ahead work calculated with the old length.
 * Tracks with their own timing window and active song playback are unaffected. */
static void reconcile_pattern_length_edit(mono_t *m) {
    if (!m->transport || m->song_enabled) return;
    int first = m->pattern_start;
    int end = first + m->pattern_len;
    for (int ti = 0; ti < m->track_count; ++ti) {
        mono_track_t *t = &m->track[ti];
        if (t->seq_override) continue;
        int next_cursor = -1;
        if (m->play_order != MONO_PLAY_RANDOM) {
            if (t->seq_cursor < 0 || t->seq_cursor >= m->pattern_len) {
                next_cursor = m->play_order == MONO_PLAY_REVERSE
                    ? m->pattern_len - 1 : 0;
            } else if (m->play_order == MONO_PLAY_REVERSE) {
                next_cursor = t->seq_cursor > 0
                    ? t->seq_cursor - 1 : m->pattern_len - 1;
            } else if (m->play_order == MONO_PLAY_PENDULUM) {
                if (m->pattern_len <= 1) next_cursor = 0;
                else {
                    next_cursor = t->seq_cursor + t->seq_direction;
                    if (next_cursor >= m->pattern_len)
                        next_cursor = m->pattern_len - 2;
                    else if (next_cursor < 0) next_cursor = 1;
                }
            } else {
                next_cursor = (t->seq_cursor + 1) % m->pattern_len;
            }
        }
        int next_step = next_cursor < 0 ? -1
            : first + (next_cursor + t->seq_rotation) % m->pattern_len;
        if (t->queued_valid && t->seq_due_count > 0) --t->seq_due_count;
        t->queued_valid = 0;
        if (t->early_fired_step != next_step) t->early_fired_step = -1;
        if (t->seq_cursor >= m->pattern_len) {
            t->seq_cursor = -1;
            t->seq_direction = 1;
        }
        for (int event = 0; event < 2; ++event) {
            mono_seq_event_t *queued = &t->seq_event[event];
            if (queued->valid &&
                (queued->step < first || queued->step >= end ||
                 (queued->early && queued->step != next_step)))
                queued->valid = 0;
        }
    }
}

static void apply_edit_snapshot(mono_t *m, const mono_edit_snapshot_t *snapshot) {
    m->pattern_start = snapshot->pattern_start;
    m->pattern_len = snapshot->pattern_len;
    m->play_order = snapshot->play_order;
    m->swing = snapshot->swing;
    m->song_enabled = snapshot->song_enabled;
    m->song_length = snapshot->song_length;
    memcpy(m->song, snapshot->song, sizeof(m->song));
    for (int track = 0; track < m->track_count; ++track)
        apply_track_edit(&m->track[track], &snapshot->track[track]);
    reset_sequence_cursors(m);
}

static void perform_undo(mono_t *m) {
    if (!m->undo_valid || !m->undo_snapshot || !m->swap_snapshot) return;
    fill_edit_snapshot(m, m->swap_snapshot);
    apply_edit_snapshot(m, m->undo_snapshot);
    mono_edit_snapshot_t *swap = m->undo_snapshot;
    m->undo_snapshot = m->swap_snapshot;
    m->swap_snapshot = swap;
    changed(m);
}

static int advance_track_cursor(mono_t *m, mono_track_t *t, int length) {
    if (t->seq_cursor < 0 || t->seq_cursor >= length) {
        if (m->play_order == MONO_PLAY_REVERSE) t->seq_cursor = length - 1;
        else if (m->play_order == MONO_PLAY_RANDOM)
            t->seq_cursor = (int)(xrnd(&t->seq_rng) % (uint32_t)length);
        else { t->seq_cursor = 0; t->seq_direction = 1; }
    } else if (m->play_order == MONO_PLAY_REVERSE) {
        if (--t->seq_cursor < 0) t->seq_cursor = length - 1;
    } else if (m->play_order == MONO_PLAY_PENDULUM) {
        if (length <= 1) t->seq_cursor = 0;
        else {
            int next = t->seq_cursor + t->seq_direction;
            if (next >= length) { t->seq_direction = -1; next = length - 2; }
            else if (next < 0) { t->seq_direction = 1; next = 1; }
            t->seq_cursor = next;
        }
    } else if (m->play_order == MONO_PLAY_RANDOM) {
        t->seq_cursor = (int)(xrnd(&t->seq_rng) % (uint32_t)length);
    } else if (++t->seq_cursor >= length) t->seq_cursor = 0;
    return t->seq_cursor;
}

static int condition_allows(int condition, uint32_t cycle) {
    switch (condition) {
    case 1: return (cycle % 2) == 0; /* 1:2 */
    case 2: return (cycle % 2) == 1; /* 2:2 */
    case 3: return (cycle % 4) == 0; /* 1:4 */
    case 4: return (cycle % 4) == 1; /* 2:4 */
    case 5: return (cycle % 4) == 2; /* 3:4 */
    case 6: return (cycle % 4) == 3; /* 4:4 */
    default: return 1;
    }
}

static int preview_track_cursor(mono_t *m, mono_track_t *t, int length,
                                int cursor, int *direction) {
    if (m->play_order == MONO_PLAY_REVERSE) {
        if (--cursor < 0) cursor = length - 1;
    } else if (m->play_order == MONO_PLAY_PENDULUM) {
        if (length <= 1) cursor = 0;
        else {
            int next = cursor + *direction;
            if (next >= length) { *direction = -1; next = length - 2; }
            else if (next < 0) { *direction = 1; next = 1; }
            cursor = next;
        }
    } else if (m->play_order == MONO_PLAY_RANDOM) {
        cursor = (int)(xrnd(&t->seq_rng) % (uint32_t)length);
    } else if (++cursor >= length) cursor = 0;
    return cursor;
}

static void fire_sequence_step(mono_t *m, mono_track_t *t, int step_index,
                               uint32_t cycle, int transpose) {
    if (step_index < 0 || step_index >= MONO_STEPS) return;
    mono_step_t *s = &t->steps[step_index];
    t->play_step = step_index;
    if (!step_is_active(s)) return;
    if (!condition_allows(s->condition, cycle) ||
        (s->probability < 127 && (xrnd(&t->seq_rng) & 127u) >= s->probability))
        return;
    memcpy(t->effective, t->base, MONO_PARAMS);
    for (int p = 0; p < MONO_PARAMS; ++p)
        if (step_has_lock(s, p)) t->effective[p] = s->lock_values[p];
    int note = s->note >= 0 ? iclamp(s->note + transpose, 0, 127) : -1;
    int velocity = iclamp(s->velocity + (s->accent * 32) / 127, 1, 127);
    if (s->tie && t->amp.stage != ENV_OFF) {
        if (note >= 0) track_pitch(t, note, velocity);
        /* The previous gate may already have closed at the step boundary;
         * a tie resumes the decay stage instead of fading through release. */
        if (t->amp.stage == ENV_RELEASE) t->amp.stage = ENV_DECAY;
        float frames_per_step = m->sample_rate * 60.0f / (bpm_now(m) * 4.0f);
        t->gate_left = (int)(frames_per_step * iclamp(t->seq_division, 1, 8) *
                             fclamp(s->gate / 127.0f, 0.5f, 1.2f));
    } else {
        int mask = s->trig_mask ? s->trig_mask : (note >= 0 ? 15 : 0);
        track_trigger(m, t, note, velocity, mask, s->gate);
    }
    float slide_seconds = s->slide ? 0.03f + m->time_2[s->slide] : 0.03f;
    t->smooth_coeff = 1.0f - expf(-MONO_CONTROL_INTERVAL /
                                  (slide_seconds * m->sample_rate));
    int retrigs = s->tie ? 1 : iclamp(s->retrig, 1, 8);
    if (retrigs > 1) {
        float frames_per_step = m->sample_rate * 60.0f / (bpm_now(m) * 4.0f);
        t->retrig_interval = (int)fmaxf(1.0f, frames_per_step / retrigs);
        t->retrig_countdown = t->retrig_interval;
        t->retrig_left = retrigs - 1;
        t->retrig_note = note;
        t->retrig_velocity = velocity;
        t->retrig_mask = s->trig_mask ? s->trig_mask : (note >= 0 ? 15 : 0);
        t->retrig_gate = s->gate;
    } else t->retrig_left = 0;
}

static void queue_sequence_event(mono_t *m, mono_track_t *t, int step,
                                 int frames, uint32_t cycle, int transpose,
                                 int early) {
    if (frames <= 0) {
        if (early) t->early_fired_step = step;
        fire_sequence_step(m, t, step, cycle, transpose);
        return;
    }
    for (int i = 0; i < 2; ++i) {
        if (t->seq_event[i].valid) continue;
        t->seq_event[i].step = step;
        t->seq_event[i].frames = frames;
        t->seq_event[i].cycle = cycle;
        t->seq_event[i].transpose = transpose;
        t->seq_event[i].early = (uint8_t)(early != 0);
        t->seq_event[i].valid = 1;
        return;
    }
}

static void process_sequence_events(mono_t *m, mono_track_t *t) {
    for (int i = 0; i < 2; ++i) {
        mono_seq_event_t *event = &t->seq_event[i];
        if (!event->valid || --event->frames > 0) continue;
        int step = event->step;
        uint32_t cycle = event->cycle;
        int transpose = event->transpose;
        if (event->early) t->early_fired_step = step;
        event->valid = 0;
        fire_sequence_step(m, t, step, cycle, transpose);
    }
}

static int flush_sequence_event_for_step(mono_t *m, mono_track_t *t, int step) {
    for (int i = 0; i < 2; ++i) {
        mono_seq_event_t *event = &t->seq_event[i];
        if (!event->valid || event->step != step) continue;
        uint32_t cycle = event->cycle;
        int transpose = event->transpose;
        event->valid = 0;
        fire_sequence_step(m, t, step, cycle, transpose);
        return 1;
    }
    return 0;
}

static void reset_track_sequence_positions(mono_t *m) {
    m->seq_step = -1;
    m->seq_direction = 1;
    for (int ti = 0; ti < m->track_count; ++ti) {
        mono_track_t *t = &m->track[ti];
        t->seq_cursor = -1;
        t->seq_direction = 1;
        t->seq_div_counter = 0;
        t->queued_valid = 0;
        t->early_fired_step = -1;
        memset(t->seq_event, 0, sizeof(t->seq_event));
    }
}

static void advance_song_row(mono_t *m) {
    if (!m->song_enabled) return;
    mono_song_row_t *row = &m->song[iclamp(m->song_play_row, 0, m->song_length - 1)];
    if (++m->song_repeat >= iclamp(row->repeats, 1, 16)) {
        m->song_repeat = 0;
        if (++m->song_play_row >= m->song_length) m->song_play_row = 0;
    }
    m->song_step_count = 0;
    reset_track_sequence_positions(m);
}

void mono_advance_step(mono_t *m) {
    if (!m) return;
    if (m->song_enabled && m->song_step_count >=
        iclamp(m->song[m->song_play_row].length, 1, MONO_STEPS))
        advance_song_row(m);
    mono_song_row_t *song_row = &m->song[iclamp(m->song_play_row, 0, m->song_length - 1)];
    int first = m->song_enabled ? song_row->start : m->pattern_start;
    int active_len = m->song_enabled ? song_row->length : m->pattern_len;
    first = iclamp(first, 0, MONO_STEPS - 1);
    active_len = iclamp(active_len, 1, MONO_STEPS - first);
    int end = first + active_len;
    if (m->seq_step < first || m->seq_step >= end) {
        if (m->play_order == MONO_PLAY_REVERSE)
            m->seq_step = end - 1;
        else if (m->play_order == MONO_PLAY_RANDOM)
            m->seq_step = first + (int)(xrnd(&m->seq_rng) % (uint32_t)active_len);
        else {
            m->seq_step = first;
            m->seq_direction = 1;
        }
    } else if (m->play_order == MONO_PLAY_REVERSE) {
        if (--m->seq_step < first) m->seq_step = end - 1;
    } else if (m->play_order == MONO_PLAY_PENDULUM) {
        if (active_len <= 1) {
            m->seq_step = first;
        } else {
            int next = m->seq_step + m->seq_direction;
            if (next >= end) {
                m->seq_direction = -1;
                next = end - 2;
            } else if (next < first) {
                m->seq_direction = 1;
                next = first + 1;
            }
            m->seq_step = next;
        }
    } else if (m->play_order == MONO_PLAY_RANDOM) {
        m->seq_step = first + (int)(xrnd(&m->seq_rng) % (uint32_t)active_len);
    } else {
        if (++m->seq_step >= end) m->seq_step = first;
    }
    if (m->song_enabled) ++m->song_step_count;
    int transpose = m->song_enabled ? song_row->transpose : 0;
    for (int ti = 0; ti < m->track_count; ++ti) {
        mono_track_t *t = &m->track[ti];
        int division = iclamp(t->seq_division, 1, 8);
        if (t->seq_div_counter++ % division) continue;
        int track_start = t->seq_override ? t->seq_start : first;
        int track_len = t->seq_override ? t->seq_len : active_len;
        track_start = iclamp(track_start, 0, MONO_STEPS - 1);
        track_len = iclamp(track_len, 1, MONO_STEPS - track_start);
        uint32_t cycle;
        int cursor;
        if (t->queued_valid) {
            cursor = t->queued_cursor;
            t->seq_cursor = cursor;
            t->seq_direction = t->queued_direction;
            cycle = t->queued_cycle;
            t->queued_valid = 0;
        } else {
            cursor = advance_track_cursor(m, t, track_len);
            cycle = t->seq_due_count++ / (uint32_t)track_len;
        }
        int rotated = (cursor + t->seq_rotation) % track_len;
        int step_index = track_start + rotated;
        mono_step_t *s = &t->steps[step_index];
        int frames_per_track_step = (int)(m->sample_rate * 60.0f /
            (bpm_now(m) * 4.0f) * division);
        if (flush_sequence_event_for_step(m, t, step_index))
            t->early_fired_step = -1;
        else if (t->early_fired_step == step_index) t->early_fired_step = -1;
        else {
            int delay = s->micro > 0 ? frames_per_track_step * s->micro / 48 : 0;
            queue_sequence_event(m, t, step_index, delay, cycle, transpose, 0);
        }

        int next_direction = t->seq_direction;
        int next_cursor = preview_track_cursor(m, t, track_len, cursor, &next_direction);
        t->queued_cursor = next_cursor;
        t->queued_direction = next_direction;
        t->queued_cycle = t->seq_due_count++ / (uint32_t)track_len;
        t->queued_valid = 1;
        int next_rotated = (next_cursor + t->seq_rotation) % track_len;
        int next_step = track_start + next_rotated;
        mono_step_t *next = &t->steps[next_step];
        if (next->micro < 0) {
            int delay = frames_per_track_step + frames_per_track_step * next->micro / 48;
            queue_sequence_event(m, t, next_step, delay, t->queued_cycle,
                                 transpose, 1);
        }
    }
}

static void internal_clock_tick(mono_t *m, float bpm) {
    if (!m->transport) return;
    if (m->external_clock) {
        if (++m->external_clock_age < m->sample_rate) return;
        /* Clock cables/settings can change during a performance. After one
         * silent second, resume from project tempo instead of freezing. */
        m->external_clock = 0;
        m->internal_frames = 0;
    }
    double frames_per_step = m->sample_rate * 60.0 / (bpm * 4.0);
    int active_start = m->song_enabled
        ? m->song[iclamp(m->song_play_row, 0, m->song_length - 1)].start
        : m->pattern_start;
    int relative = m->seq_step - active_start;
    float swing = 0.45f * pnorm(m->swing);
    frames_per_step *= (relative & 1) ? 1.0 - swing : 1.0 + swing;
    m->internal_frames += 1.0;
    if (m->internal_frames >= frames_per_step) {
        m->internal_frames -= frames_per_step;
        mono_advance_step(m);
    }
}

void mono_render(mono_t *m, int16_t *out_lr, int frames) {
    if (!m || !out_lr || frames <= 0) return;
    m->cc_frames += (uint64_t)frames;
    int peak = 0;
    float bpm = bpm_now(m);
    int any_solo = 0;
    for (int t = 0; t < m->track_count; ++t) any_solo |= m->track[t].solo;
    for (int i = 0; i < frames; ++i) {
        int control_tick = m->control_phase == 0;
        internal_clock_tick(m, bpm);
        float l = 0.0f, r = 0.0f;
        float neighbor_l = 0.0f, neighbor_r = 0.0f;
        for (int t = 0; t < m->track_count; ++t) {
            arp_tick(m, &m->track[t], bpm);
            float tr = 0.0f;
            float tl = render_track(m, &m->track[t], &tr, bpm, control_tick,
                                    neighbor_l, neighbor_r);
            neighbor_l = tl;
            neighbor_r = tr;
            int audible = any_solo ? m->track[t].solo : !m->track[t].mute;
            if (audible) { l += tl; r += tr; }
        }
        l = tanhf(l * m->master);
        r = tanhf(r * m->master);
        if (m->calibration_mode) {
            float level = 0.25f * pnorm(m->calibration_level);
            float signal = 0.0f;
            switch (m->calibration_mode) {
            case 1: /* 440 Hz reference sine */
                signal = sinf(2.0f * (float)M_PI * (float)m->calibration_phase);
                m->calibration_phase += 440.0 / m->sample_rate;
                break;
            case 2: { /* ten-second logarithmic 20 Hz to 20 kHz sweep */
                double position = (m->calibration_frames %
                    (uint64_t)(m->sample_rate * 10)) / (double)(m->sample_rate * 10);
                double hz = 20.0 * pow(1000.0, position);
                signal = sinf(2.0f * (float)M_PI * (float)m->calibration_phase);
                m->calibration_phase += hz / m->sample_rate;
                break;
            }
            case 3: /* sample-accurate impulse once per second */
                signal = m->calibration_frames % (uint64_t)m->sample_rate == 0 ? 1.0f : 0.0f;
                break;
            case 4: /* deterministic white noise */
                signal = ((int32_t)xrnd(&m->calibration_noise) / 2147483648.0f);
                break;
            case 5: /* polarity/channel test: alternate sides every second */
                signal = sinf(2.0f * (float)M_PI * (float)m->calibration_phase);
                m->calibration_phase += 1000.0 / m->sample_rate;
                break;
            default: break;
            }
            m->calibration_phase -= floor(m->calibration_phase);
            signal *= level;
            if (m->calibration_mode == 5) {
                int side = (int)(m->calibration_frames / (uint64_t)m->sample_rate) & 1;
                l = side ? 0.0f : signal;
                r = side ? -signal : 0.0f;
            } else l = r = signal;
            ++m->calibration_frames;
        }
        if (!isfinite(l)) {
            l = 0.0f;
            ++m->nonfinite_samples;
        }
        if (!isfinite(r)) {
            r = 0.0f;
            ++m->nonfinite_samples;
        }
        out_lr[i * 2] = (int16_t)lrintf(fclamp(l, -1.0f, 1.0f) * 32767.0f);
        out_lr[i * 2 + 1] = (int16_t)lrintf(fclamp(r, -1.0f, 1.0f) * 32767.0f);
        int al = out_lr[i * 2] < 0 ? -out_lr[i * 2] : out_lr[i * 2];
        int ar = out_lr[i * 2 + 1] < 0 ? -out_lr[i * 2 + 1] : out_lr[i * 2 + 1];
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
        if (++m->control_phase >= MONO_CONTROL_INTERVAL) m->control_phase = 0;
    }
    m->render_peak = peak;
    ++m->render_blocks;
    if (peak > 0) ++m->nonzero_blocks;
    if (peak > m->lifetime_peak) m->lifetime_peak = peak;
}

/* --- MIDI CC control ----------------------------------------------- */
/* An external controller reaches every sound parameter of every track:
 * the CC's MIDI channel picks the track exactly like note input
 * (channel % track_count), the CC number picks the parameter:
 *   CC 8..63    base params 0..55   (SYNTH AMP FLT FX LFO1 LFO2 LFO3, x8)
 *   CC 64..119  Shift bank params   (same page order)
 * CC 0-7 and 120+ are ignored (mod wheel, bank select, channel mode).
 * Values write through the same path as the UI: base+effective update,
 * machine memory, and live param-lock recording when the sequencer runs.
 * Selector params (LFO destinations) scale 0-127 across their range. */
static void mono_cc_param(mono_t *m, int track, int cc, int v) {
    int pid;
    if (cc >= 8 && cc < 8 + MONO_PRIMARY_PARAMS) pid = cc - 8;
    else if (cc >= 64 && cc < 64 + MONO_SHIFT_PARAMS)
        pid = MONO_SHIFT_BASE + (cc - 64);
    else return;
    mono_track_t *t = &m->track[track];
    int max = is_lfo_destination_param(pid) ? MONO_LFO_DESTINATIONS - 1 : 127;
    int val = max == 127 ? v : (v * max + 63) / 127;
    t->base[pid] = (uint8_t)iclamp(val, 0, max);
    t->effective[pid] = t->base[pid];
    if (pid < 8 || (pid >= MONO_ALT_BASE && pid < MONO_ALT_BASE + 8))
        remember_machine(t);
    int record_step = t->play_step >= 0 ? t->play_step : m->seq_step;
    if (m->record_locks && m->transport && record_step >= 0) {
        mono_step_t *step = &t->steps[record_step];
        step_set_lock(step, pid);
        step->lock_values[pid] = t->base[pid];
    }
    /* Tell the editors a value moved under them. Only external CC lands here,
     * so a UI can follow this counter without chasing its own writes. */
    ++m->cc_revision;
    changed(m);
}

void mono_on_midi(mono_t *m, const uint8_t *msg, int len, int source) {
    if (!m || !msg || len < 1) return;
    uint8_t st = msg[0];
    if (st == 0xF8) {
        m->external_clock = 1;
        m->external_clock_age = 0;
        int active_start = m->song_enabled
            ? m->song[iclamp(m->song_play_row, 0, m->song_length - 1)].start
            : m->pattern_start;
        int relative = m->seq_step - active_start;
        int swing_ticks = (int)lrintf(3.0f * pnorm(m->swing));
        int ticks = (relative & 1) ? 6 - swing_ticks : 6 + swing_ticks;
        if (m->transport && ++m->tick_in_step >= ticks) {
            m->tick_in_step = 0;
            mono_advance_step(m);
        }
        return;
    }
    if (st == 0xFA) {
        m->transport = 1;
        reset_sequence_cursors(m);
        m->tick_in_step = 0;
        m->internal_frames = 0;
        mono_advance_step(m);
        return;
    }
    if (st == 0xFB) { m->transport = 1; return; }
    if (st == 0xFC) {
        m->transport = 0;
        reset_effective_to_base(m);
        return;
    }
    if (len < 3) return;
    int kind = st & 0xF0;
    int channel = st & 0x0F;
    int track = m->track_count == 1 ? 0 : channel % m->track_count;
    int note = msg[1];
    if (kind == 0xB0) {
        /* External CC control only — internal CCs are Move's own encoders.
         * A channel-matched chain slot can deliver one external CC twice
         * (channel dispatch + FX broadcast): identical messages within
         * ~2 blocks drop. */
        if ((source == MOVE_MIDI_SOURCE_EXTERNAL ||
             source == MOVE_MIDI_SOURCE_FX_BROADCAST) &&
            !(st == m->cc_last[0] && msg[1] == m->cc_last[1] &&
              msg[2] == m->cc_last[2] &&
              m->cc_frames - m->cc_last_frames <= 256)) {
            m->cc_last[0] = st;
            m->cc_last[1] = msg[1];
            m->cc_last[2] = msg[2];
            m->cc_last_frames = m->cc_frames;
            mono_cc_param(m, track, msg[1], msg[2]);
        }
        return;
    }
    if (m->track_count > 1 && source == MOVE_MIDI_SOURCE_INTERNAL) {
        /* Overtake receives raw Move pad note IDs. Let the DSP own the lower
         * three rows directly so sound never depends on an asynchronous JS
         * parameter round-trip; the top row remains reserved for controls. */
        if (note < 68 || note >= 92) return;
        track = m->selected_track;
        note = iclamp(48 + note - 68 + m->track[track].keyboard_octave * 12,
                      0, 127);
    }
    if (kind == 0x90 && msg[2] > 0) mono_note_on(m, track, note, msg[2]);
    else if (kind == 0x80 || (kind == 0x90 && msg[2] == 0)) mono_note_off(m, track, note);
}

static int param_id(const mono_t *m, const char *key) {
    int a, b;
    if (sscanf(key, "syn%d", &a) == 1 && a >= 1 && a <= 8) return a - 1;
    if (sscanf(key, "syn%d", &a) == 1 && a >= 9 && a <= 16)
        return MONO_ALT_BASE + a - 9;
    if (sscanf(key, "alt%d", &a) == 1 && a >= 1 && a <= MONO_ALT_PARAMS)
        return MONO_SHIFT_BASE + m->selected_page * MONO_PAGE_PARAMS + a - 1;
    if (sscanf(key, "amp%d", &a) == 1 && a >= 1 && a <= 8) return 8 + a - 1;
    if (sscanf(key, "amp%d", &a) == 1 && a >= 9 && a <= 16)
        return MONO_SHIFT_BASE + MONO_PAGE_PARAMS + a - 9;
    if (sscanf(key, "flt%d", &a) == 1 && a >= 1 && a <= 8) return 16 + a - 1;
    if (sscanf(key, "flt%d", &a) == 1 && a >= 9 && a <= 16)
        return MONO_SHIFT_BASE + 2 * MONO_PAGE_PARAMS + a - 9;
    if (sscanf(key, "fx%d", &a) == 1 && a >= 1 && a <= 8) return 24 + a - 1;
    if (sscanf(key, "fx%d", &a) == 1 && a >= 9 && a <= 16)
        return MONO_SHIFT_BASE + 3 * MONO_PAGE_PARAMS + a - 9;
    if (sscanf(key, "lfo%d_%d", &a, &b) == 2 && a >= 1 && a <= 3 && b >= 1 && b <= 8)
        return (3 + a) * 8 + b - 1;
    if (sscanf(key, "lfo%d_%d", &a, &b) == 2 && a >= 1 && a <= 3 && b >= 9 && b <= 16)
        return MONO_SHIFT_BASE + (3 + a) * MONO_PAGE_PARAMS + b - 9;
    if (sscanf(key, "p%d", &a) == 1 && a >= 1 && a <= 8)
        return m->selected_page * 8 + a - 1;
    return -1;
}

static void select_machine(mono_track_t *t, int machine) {
    recall_machine(t, machine);
}

static void reset_track_runtime(mono_t *m, mono_track_t *t, int index) {
    t->note = -1;
    t->last_note = 48 + index * 5;
    t->velocity = 0;
    memset(t->held_notes, 0, sizeof(t->held_notes));
    memset(t->held_velocity, 0, sizeof(t->held_velocity));
    memset(t->held_order, 0, sizeof(t->held_order));
    memset(t->physical_notes, 0, sizeof(t->physical_notes));
    t->note_order = 0;
    t->gate_left = 0;
    t->freq = 0.0f;
    t->target_freq = 0.0f;
    memset(t->phase, 0, sizeof(t->phase));
    memset(t->mod_phase, 0, sizeof(t->mod_phase));
    t->fm_feedback = 0.0f;
    t->fm_feedback2 = 0.0f;
    t->voice_age_samples = 0;
    t->sid_lfsr = (0x7ffff8u ^ (uint32_t)(index * 0x5bd1e995u)) & 0x7fffffu;
    if (!t->sid_lfsr) t->sid_lfsr = 0x7ffff8u;
    t->sid_noise_clock = 0.0f;
    memset(&t->amp, 0, sizeof(t->amp));
    memset(&t->filter_env, 0, sizeof(t->filter_env));
    for (int i = 0; i < 3; ++i) {
        t->lfo[i].phase = 0.0f;
        t->lfo[i].held = 0.0f;
        t->lfo[i].stopped = 0;
    }
    memset(t->lfo_param_mod, 0, sizeof(t->lfo_param_mod));
    memset(t->hp, 0, sizeof(t->hp));
    memset(t->lp, 0, sizeof(t->lp));
    memset(&t->eq, 0, sizeof(t->eq));
    t->srr_left = 0;
    t->srr_hold = 0.0f;
    t->delay_pos = 0;
    memset(t->delay_lp, 0, sizeof(t->delay_lp));
    memset(t->delay_hp, 0, sizeof(t->delay_hp));
    memset(t->delay_hp_in, 0, sizeof(t->delay_hp_in));
    t->delay_mod_phase = 0.0f;
    t->seq_cursor = -1;
    t->seq_direction = 1;
    t->seq_div_counter = 0;
    t->play_step = -1;
    t->seq_due_count = 0;
    t->queued_cursor = -1;
    t->queued_direction = 1;
    t->queued_cycle = 0;
    t->queued_valid = 0;
    t->early_fired_step = -1;
    memset(t->seq_event, 0, sizeof(t->seq_event));
    reset_arp_runtime(t);
    if (t->delay)
        memset(t->delay, 0,
               (size_t)m->sample_rate * MONO_DELAY_SECONDS * 2 * sizeof(float));
    t->track_fx_pos = 0;
    t->track_fx_phase = 0.0f;
    t->track_fx_env = 0.0f;
    memset(t->track_fx_lp, 0, sizeof(t->track_fx_lp));
    t->track_fx_hold = 0;
    memset(t->track_fx_held, 0, sizeof(t->track_fx_held));
    if (t->track_fx_buffer)
        memset(t->track_fx_buffer, 0,
               (size_t)m->sample_rate * MONO_TRACK_FX_SECONDS * 2 * sizeof(float));
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int read_hex(const char **cursor, const char *end, int digits,
                    uint64_t *value) {
    if (digits <= 0 || end - *cursor < digits) return 0;
    uint64_t result = 0;
    for (int i = 0; i < digits; ++i) {
        int digit = hex_digit((*cursor)[i]);
        if (digit < 0) return 0;
        result = (result << 4) | (uint64_t)digit;
    }
    *cursor += digits;
    *value = result;
    return 1;
}

static const char state64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int state64_digit(char c) {
    const char *p = strchr(state64, c);
    return p ? (int)(p - state64) : -1;
}

static int mask_has_param(const uint64_t *mask, int pid) {
    return (mask[pid / 64] & (UINT64_C(1) << (pid % 64))) != 0;
}

/* State v8 and earlier exposed the Pulse panel as UNIL, UNIW, UNIX, PW,
 * PWAD, PWRS, unused, TUNE. Version 9 follows the documented machine panel:
 * UNIL, UNIW, SUB1, SUB2, PW, PWAD, PWRS, TUNE. Preserve the controls that
 * had a matching meaning and initialize the two newly exposed sine subs. */
static void migrate_legacy_pulse_panel(uint8_t *params) {
    uint8_t old[8];
    memcpy(old, params, sizeof(old));
    params[2] = 32;
    params[3] = 0;
    params[4] = old[3];
    params[5] = old[4];
    params[6] = old[5];
    params[7] = old[7];
}

static void migrate_legacy_pulse_locks(uint64_t *mask, uint8_t *values) {
    int had_pw = mask_has_param(mask, 3);
    int had_pwad = mask_has_param(mask, 4);
    int had_pwrs = mask_has_param(mask, 5);
    uint8_t pw = values[3], pwad = values[4], pwrs = values[5];
    for (int pid = 2; pid <= 6; ++pid) {
        mask[pid / 64] &= ~(UINT64_C(1) << (pid % 64));
        values[pid] = 0;
    }
    if (had_pw) {
        mask[0] |= UINT64_C(1) << 4;
        values[4] = pw;
    }
    if (had_pwad) {
        mask[0] |= UINT64_C(1) << 5;
        values[5] = pwad;
    }
    if (had_pwrs) {
        mask[0] |= UINT64_C(1) << 6;
        values[6] = pwrs;
    }
}

static int read_v5_mask(const char **cursor, const char *end, uint64_t *mask) {
    memset(mask, 0, MONO_LOCK_WORDS * sizeof(*mask));
    int chars = (MONO_PARAMS + 5) / 6;
    if (end - *cursor < chars) return 0;
    for (int chunk = 0; chunk < chars; ++chunk) {
        int encoded = state64_digit(*(*cursor)++);
        if (encoded < 0) return 0;
        for (int bit = 0; bit < 6; ++bit) {
            int pid = chunk * 6 + bit;
            if (pid < MONO_PARAMS && (encoded & (1 << bit)))
                mask[pid / 64] |= UINT64_C(1) << (pid % 64);
        }
    }
    return 1;
}

static int read_v5_lock_values(const char **cursor, const char *end,
                               const uint64_t *mask, uint8_t *values) {
    int locked = 0;
    for (int pid = 0; pid < MONO_PARAMS; ++pid)
        if (mask_has_param(mask, pid)) ++locked;
    int chars = (locked * 7 + 5) / 6;
    if (end - *cursor < chars) return 0;
    uint32_t bits = 0;
    int bit_count = 0, consumed = 0;
    for (int pid = 0; pid < MONO_PARAMS; ++pid) {
        if (!mask_has_param(mask, pid)) continue;
        while (bit_count < 7) {
            if (consumed >= chars) return 0;
            int encoded = state64_digit((*cursor)[consumed++]);
            if (encoded < 0) return 0;
            bits |= (uint32_t)encoded << bit_count;
            bit_count += 6;
        }
        values[pid] = (uint8_t)(bits & 127u);
        bits >>= 7;
        bit_count -= 7;
    }
    *cursor += chars;
    return consumed == chars;
}

static int append_v5_mask(char *buf, int buf_len, int *used,
                          const mono_step_t *step) {
    for (int first = 0; first < MONO_PARAMS; first += 6) {
        int encoded = 0;
        for (int bit = 0; bit < 6 && first + bit < MONO_PARAMS; ++bit)
            if (step_has_lock(step, first + bit)) encoded |= 1 << bit;
        if (!appendf(buf, buf_len, used, "%c", state64[encoded])) return 0;
    }
    return 1;
}

static int append_v5_lock_values(char *buf, int buf_len, int *used,
                                 const mono_step_t *step) {
    uint32_t bits = 0;
    int bit_count = 0;
    for (int pid = 0; pid < MONO_PARAMS; ++pid) {
        if (!step_has_lock(step, pid)) continue;
        bits |= (uint32_t)step->lock_values[pid] << bit_count;
        bit_count += 7;
        while (bit_count >= 6) {
            if (!appendf(buf, buf_len, used, "%c", state64[bits & 63u])) return 0;
            bits >>= 6;
            bit_count -= 6;
        }
    }
    if (bit_count > 0)
        if (!appendf(buf, buf_len, used, "%c", state64[bits & 63u])) return 0;
    return 1;
}

/* Validate first, then call again with apply=1. The compact payload keeps a
 * fully populated six-track, 64-step pattern below the chain host's 16 KiB
 * state ceiling in the normal (sparse-lock) case. */
static int compact_state_pass(mono_t *m, const char *data, const char *end,
                              int version, int saved_params,
                              int legacy_destinations, int apply) {
    const char *p = data;
    unsigned seen_tracks = 0;
    while (p < end) {
        char record = *p++;
        if (record == 'G') {
            uint64_t enabled64, length64;
            mono_song_row_t rows[MONO_SONG_ROWS];
            if (version < 10 ||
                !read_hex(&p, end, 2, &enabled64) || enabled64 > 1 ||
                !read_hex(&p, end, 2, &length64) ||
                length64 < 1 || length64 > MONO_SONG_ROWS) return 0;
            for (int row = 0; row < MONO_SONG_ROWS; ++row) {
                uint64_t start64, row_length64, repeats64, transpose64;
                if (!read_hex(&p, end, 2, &start64) || start64 >= MONO_STEPS ||
                    !read_hex(&p, end, 2, &row_length64) || row_length64 < 1 ||
                    row_length64 > MONO_STEPS - start64 ||
                    !read_hex(&p, end, 2, &repeats64) || repeats64 < 1 ||
                    repeats64 > 16 ||
                    !read_hex(&p, end, 2, &transpose64) || transpose64 > 48)
                    return 0;
                rows[row].start = (uint8_t)start64;
                rows[row].length = (uint8_t)row_length64;
                rows[row].repeats = (uint8_t)repeats64;
                rows[row].transpose = (int8_t)((int)transpose64 - 24);
            }
            if (apply) {
                m->song_enabled = (int)enabled64;
                m->song_length = (int)length64;
                memcpy(m->song, rows, sizeof(rows));
            }
            continue;
        }
        uint64_t tr64;
        if (!read_hex(&p, end, 1, &tr64) || tr64 >= MONO_MAX_TRACKS) return 0;
        int tr = (int)tr64;
        if (record == 'T') {
            uint64_t machine64, override64 = 0, start64 = 0, len64 = 16;
            uint64_t rotation64 = 0, division64 = 1, mute64 = 0, solo64 = 0;
            uint64_t keyboard_octave64 = 4;
            mono_arp_settings_t arp = {0};
            mono_route_settings_t route = {0};
            arp.rate = 3;
            arp.octaves = 1;
            arp.gate = 92;
            arp.length = MONO_ARP_STEPS;
            route.level = 64;
            uint8_t params[MONO_PARAMS];
            if (!read_hex(&p, end, 2, &machine64) ||
                machine64 >= MONO_MACHINE_COUNT) return 0;
            if (version >= 6 &&
                (!read_hex(&p, end, 2, &override64) || override64 > 1 ||
                 !read_hex(&p, end, 2, &start64) || start64 >= MONO_STEPS ||
                 !read_hex(&p, end, 2, &len64) || len64 < 1 ||
                    len64 > MONO_STEPS - start64 ||
                 !read_hex(&p, end, 2, &rotation64) || rotation64 >= MONO_STEPS ||
                 !read_hex(&p, end, 2, &division64) || division64 < 1 || division64 > 8))
                return 0;
            if (version >= 8) {
                if (!read_hex(&p, end, 2, &mute64) || mute64 > 1 ||
                    !read_hex(&p, end, 2, &solo64)) return 0;
                if (version >= 11) {
                    if (solo64 > 17) return 0;
                    keyboard_octave64 = solo64 >> 1;
                    solo64 &= 1;
                } else if (solo64 > 1) return 0;
            }
            if (version >= 10) {
                uint64_t fields[8];
                for (int i = 0; i < 8; ++i)
                    if (!read_hex(&p, end, 2, &fields[i])) return 0;
                if (fields[0] > 1 || fields[1] > 1 || fields[2] > 5 ||
                    fields[3] > 7 || fields[4] < 1 || fields[4] > 4 ||
                    fields[5] < 1 || fields[5] > 127 ||
                    fields[6] < 1 || fields[6] > MONO_ARP_STEPS ||
                    fields[7] > 127) return 0;
                arp.enabled = (uint8_t)fields[0];
                arp.latch = (uint8_t)fields[1];
                arp.mode = (uint8_t)fields[2];
                arp.rate = (uint8_t)fields[3];
                arp.octaves = (uint8_t)fields[4];
                arp.gate = (uint8_t)fields[5];
                arp.length = (uint8_t)fields[6];
                arp.velocity = (uint8_t)fields[7];
                for (int i = 0; i < MONO_ARP_STEPS; ++i) {
                    uint64_t offset64;
                    if (!read_hex(&p, end, 2, &offset64) || offset64 > 48) return 0;
                    arp.offset[i] = (int8_t)((int)offset64 - 24);
                }
                for (int i = 0; i < 8; ++i)
                    if (!read_hex(&p, end, 2, &fields[i]) || fields[i] > 127)
                        return 0;
                if (fields[0] > 4 || fields[2] > 6) return 0;
                route.route_mode = (uint8_t)fields[0];
                route.route_amount = (uint8_t)fields[1];
                route.fx_type = (uint8_t)fields[2];
                route.fx_amount = (uint8_t)fields[3];
                route.fx_tone = (uint8_t)fields[4];
                route.fx_feedback = (uint8_t)fields[5];
                route.fx_mix = (uint8_t)fields[6];
                route.level = (uint8_t)fields[7];
            }
            for (int i = 0; i < saved_params; ++i) {
                uint64_t param;
                if (!read_hex(&p, end, 2, &param) || param > 127) return 0;
                if (legacy_destinations && is_lfo_destination_param(i))
                    param = (uint64_t)migrate_legacy_lfo_destination((int)param);
                params[i] = (uint8_t)param;
            }
            if (version < 9 && machine64 == MONO_SWAVE_PULSE)
                migrate_legacy_pulse_panel(params);
            if (tr < m->track_count) {
                seen_tracks |= 1u << tr;
                if (apply) {
                    mono_track_t *t = &m->track[tr];
                    /* Older state records do not contain the secondary banks.
                     * Reset those controls before applying the saved prefix so
                     * recalling an old patch cannot inherit values from the
                     * patch that happened to be loaded before it. */
                    if (saved_params < MONO_PARAMS) common_defaults(t);
                    machine_defaults(t, (mono_machine_t)machine64);
                    memcpy(t->base, params, (size_t)saved_params);
                    memcpy(t->effective, t->base, MONO_PARAMS);
                    memset(t->machine_valid, 0, sizeof(t->machine_valid));
                    t->mute = (int)mute64;
                    t->solo = (int)solo64;
                    t->seq_override = (int)override64;
                    t->seq_start = (int)start64;
                    t->seq_len = (int)len64;
                    t->seq_rotation = (int)rotation64;
                    t->seq_division = (int)division64;
                    t->keyboard_octave = (int)keyboard_octave64 - 4;
                    t->arp = arp;
                    t->route = route;
                    invalidate_morph(t);
                    remember_machine(t);
                    sync_smoothed(t);
                }
            }
            continue;
        }
        if (record == 'M') {
            uint64_t machine64;
            uint8_t machine_params[16];
            if (!read_hex(&p, end, 2, &machine64) || machine64 >= MONO_MACHINE_COUNT)
                return 0;
            for (int i = 0; i < 16; ++i) {
                uint64_t param;
                if (!read_hex(&p, end, 2, &param) || param > 127) return 0;
                machine_params[i] = (uint8_t)param;
            }
            if (version < 9 && machine64 == MONO_SWAVE_PULSE)
                migrate_legacy_pulse_panel(machine_params);
            if (apply && tr < m->track_count) {
                mono_track_t *t = &m->track[tr];
                for (int i = 0; i < 16; ++i)
                    t->machine_params[machine64][i] = machine_params[i];
                t->machine_valid[machine64] = 1;
            }
            continue;
        }
        if (record == 'A' || record == 'B') {
            uint64_t machine64, value64;
            uint8_t endpoint[MONO_PARAMS];
            if (version < 10 ||
                !read_hex(&p, end, 2, &machine64) ||
                machine64 >= MONO_MACHINE_COUNT ||
                !read_hex(&p, end, 2, &value64) || value64 > 127) return 0;
            for (int pid = 0; pid < MONO_PARAMS; ++pid) {
                uint64_t param64;
                if (!read_hex(&p, end, 2, &param64) || param64 > 127 ||
                    (is_lfo_destination_param(pid) &&
                     param64 >= MONO_LFO_DESTINATIONS)) return 0;
                endpoint[pid] = (uint8_t)param64;
            }
            if (apply && tr < m->track_count) {
                mono_track_t *t = &m->track[tr];
                memcpy(record == 'A' ? t->morph_a : t->morph_b,
                       endpoint, sizeof(endpoint));
                t->morph_valid |= record == 'A' ? 1 : 2;
                t->morph_machine = (uint8_t)machine64;
                t->morph_value = (uint8_t)value64;
            }
            continue;
        }
        if (record == 'S') {
            uint64_t step64, note64, velocity64, gate64, trig64;
            uint64_t probability64 = 127, retrig64 = 1, condition64 = 0, slide64 = 0;
            uint64_t micro64 = 24, tie64 = 0, accent64 = 0;
            int dense_locks = 0;
            uint64_t mask[MONO_LOCK_WORDS] = {0};
            uint8_t lock_values[MONO_PARAMS] = {0};
            if (!read_hex(&p, end, 2, &step64) || step64 >= MONO_STEPS ||
                !read_hex(&p, end, 2, &note64) ||
                (note64 != 255 && note64 > 127) ||
                !read_hex(&p, end, 2, &velocity64) || velocity64 > 127 ||
                !read_hex(&p, end, 2, &gate64) || gate64 > 127 ||
                !read_hex(&p, end, 2, &trig64) || trig64 > 15) return 0;
            if (version >= 7) {
                if (p >= end) return 0;
                int behavior = state64_digit(*p++);
                if (behavior < 0 || behavior > 15) return 0;
                if ((behavior & 1) &&
                    (!read_hex(&p, end, 2, &probability64) || probability64 > 127)) return 0;
                if ((behavior & 2) &&
                    (!read_hex(&p, end, 2, &retrig64) || retrig64 < 1 || retrig64 > 8)) return 0;
                if ((behavior & 4) &&
                    (!read_hex(&p, end, 2, &condition64) || condition64 > 6)) return 0;
                if ((behavior & 8) &&
                    (!read_hex(&p, end, 2, &slide64) || slide64 > 127)) return 0;
            }
            if (version >= 10) {
                if (p >= end) return 0;
                int extended = state64_digit(*p++);
                if (extended < 0 || extended > 15) return 0;
                tie64 = (uint64_t)((extended & 2) != 0);
                dense_locks = (extended & 8) != 0;
                if ((extended & 1) &&
                    (!read_hex(&p, end, 2, &micro64) || micro64 > 48)) return 0;
                if ((extended & 4) &&
                    (!read_hex(&p, end, 2, &accent64) || accent64 > 127)) return 0;
            }
            if (version >= 5) {
                if (dense_locks) {
                    for (int pid = 0; pid < MONO_PARAMS; ++pid)
                        mask[pid / 64] |= UINT64_C(1) << (pid % 64);
                    if (!read_v5_lock_values(&p, end, mask, lock_values)) return 0;
                } else if (!read_v5_mask(&p, end, mask) ||
                           !read_v5_lock_values(&p, end, mask, lock_values)) return 0;
            } else {
                uint64_t legacy_mask;
                int mask_digits = version >= 3 ? 16 : 14;
                if (!read_hex(&p, end, mask_digits, &legacy_mask)) return 0;
                mask[0] = legacy_mask;
                for (int i = 0; i < saved_params; ++i) {
                    if (!mask_has_param(mask, i)) continue;
                    uint64_t lock_value;
                    if (!read_hex(&p, end, 2, &lock_value) || lock_value > 127) return 0;
                    if (legacy_destinations && is_lfo_destination_param(i))
                        lock_value = (uint64_t)migrate_legacy_lfo_destination((int)lock_value);
                    lock_values[i] = (uint8_t)lock_value;
                }
            }
            if (apply && version < 9 && tr < m->track_count &&
                m->track[tr].machine == MONO_SWAVE_PULSE)
                migrate_legacy_pulse_locks(mask, lock_values);
            mono_step_t *step = tr < m->track_count
                ? &m->track[tr].steps[(int)step64] : NULL;
            if (apply && step) {
                step->note = note64 == 255 ? -1 : (int8_t)note64;
                step->velocity = (uint8_t)velocity64;
                step->gate = (uint8_t)gate64;
                step->trig_mask = (uint8_t)trig64;
                step->probability = (uint8_t)probability64;
                step->retrig = (uint8_t)retrig64;
                step->condition = (uint8_t)condition64;
                step->slide = (uint8_t)slide64;
                step->micro = (int8_t)((int)micro64 - 24);
                step->tie = (uint8_t)tie64;
                step->accent = (uint8_t)accent64;
                memcpy(step->lock_mask, mask, sizeof(mask));
                memcpy(step->lock_values, lock_values, sizeof(lock_values));
            }
            continue;
        }
        return 0;
    }
    unsigned wanted = (1u << m->track_count) - 1u;
    return p == end && (seen_tracks & wanted) == wanted;
}

static int json_int(const char *json, const char *key, int fallback) {
    const char *p = strstr(json, key);
    return p ? atoi(p + strlen(key)) : fallback;
}

static float json_float(const char *json, const char *key, float fallback) {
    const char *p = strstr(json, key);
    return p ? strtof(p + strlen(key), NULL) : fallback;
}

static void restore_state(mono_t *m, const char *json) {
    const char *tag = strstr(json, "\"data\":\"");
    if (!tag) return; /* Ignore the old display-only v1 state safely. */
    int version = json_int(json, "\"v\":", 0);
    if (version < 2 || version > 11) return;
    int saved_params = version >= 5 ? MONO_PARAMS
        : (version >= 3 ? 64 : MONO_PRIMARY_PARAMS);
    int legacy_destinations = version < 4;
    const char *data = tag + strlen("\"data\":\"");
    const char *end = strchr(data, '"');
    if (!end || !compact_state_pass(m, data, end, version, saved_params,
                                    legacy_destinations, 0)) return;

    for (int tr = 0; tr < m->track_count; ++tr) {
        for (int step = 0; step < MONO_STEPS; ++step)
            clear_step(&m->track[tr].steps[step]);
        reset_track_runtime(m, &m->track[tr], tr);
    }
    m->song_enabled = 0;
    m->song_length = 1;
    m->song_edit_row = 0;
    for (int row = 0; row < MONO_SONG_ROWS; ++row) {
        m->song[row].start = 0;
        m->song[row].length = 16;
        m->song[row].repeats = 1;
        m->song[row].transpose = 0;
    }
    if (!compact_state_pass(m, data, end, version, saved_params,
                            legacy_destinations, 1)) return;

    m->selected_track = iclamp(json_int(json, "\"track\":", 0),
                               0, m->track_count - 1);
    m->selected_page = iclamp(json_int(json, "\"page\":", 0),
                              0, MONO_PAGES - 1);
    m->step_page = iclamp(json_int(json, "\"step_page\":", 0), 0, 3);
    m->edit_step = iclamp(json_int(json, "\"edit_step\":", 0), 0, MONO_STEPS - 1);
    m->song_edit_row = iclamp(json_int(json, "\"song_edit_row\":", 0),
                              0, MONO_SONG_ROWS - 1);
    m->pattern_start = iclamp(json_int(json, "\"pattern_start\":", 0),
                              0, MONO_STEPS - 1);
    m->pattern_len = iclamp(json_int(json, "\"pattern_len\":", 16),
                            1, MONO_STEPS - m->pattern_start);
    m->play_order = iclamp(json_int(json, "\"play_order\":", MONO_PLAY_FORWARD),
                           MONO_PLAY_FORWARD, MONO_PLAY_MODE_COUNT - 1);
    m->swing = iclamp(json_int(json, "\"swing\":", 0), 0, 127);
    m->master = fclamp(json_float(json, "\"master\":", 100.0f) / 100.0f,
                       0.0f, 2.0f);
    m->bpm_override = fclamp(json_float(json, "\"bpm_override\":", 0.0f),
                             0.0f, 400.0f);
    m->transport = 0;
    m->record_locks = 0;
    m->calibration_mode = 0;
    m->calibration_phase = 0.0;
    m->calibration_frames = 0;
    reset_sequence_cursors(m);
    m->tick_in_step = 0;
    m->external_clock = 0;
    m->external_clock_age = 0;
    m->internal_frames = 0.0;
    changed(m);
}

static void set_lock(mono_t *m, const char *val, int clear) {
    int tr = m->selected_track, step = -1, pid = -1, value = 0;
    int n = sscanf(val, "%d:%d:%d:%d", &tr, &step, &pid, &value);
    if (n < 4) {
        tr = m->selected_track;
        n = sscanf(val, "%d:%d:%d", &step, &pid, &value);
    }
    if ((clear && n < 2) || (!clear && n < 3)) return;
    if (tr < 0 || tr >= m->track_count || step < 0 || step >= MONO_STEPS ||
        pid < 0 || pid >= MONO_PARAMS) return;
    capture_undo(m);
    mono_step_t *s = &m->track[tr].steps[step];
    if (clear) step_clear_lock(s, pid);
    else {
        step_set_lock(s, pid);
        int max = is_lfo_destination_param(pid) ? MONO_LFO_DESTINATIONS - 1 : 127;
        s->lock_values[pid] = (uint8_t)iclamp(value, 0, max);
    }
}

void mono_set_param(mono_t *m, const char *key, const char *val) {
    if (!m || !key || !val) return;
    if (!strcmp(key, "rui_set")) {
        const char *separator = strchr(val, ':');
        size_t key_len = separator ? (size_t)(separator - val) : 0;
        char routed_key[32];
        if (!separator || key_len == 0 || key_len >= sizeof(routed_key)) return;
        memcpy(routed_key, val, key_len);
        routed_key[key_len] = '\0';
        if (!strcmp(routed_key, "rui_set")) return;
        mono_set_param(m, routed_key, separator + 1);
        return;
    }
    if (!strcmp(key, "state")) { restore_state(m, val); return; }
    int v = atoi(val);
    if (!strcmp(key, "track")) { m->selected_track = iclamp(v, 0, m->track_count - 1); changed(m); return; }
    if (!strcmp(key, "page")) { m->selected_page = iclamp(v, 0, MONO_PAGES - 1); changed(m); return; }
    if (!strcmp(key, "step_page")) { m->step_page = iclamp(v, 0, 3); changed(m); return; }
    if (!strcmp(key, "pattern_start")) {
        capture_undo(m);
        m->pattern_start = iclamp(v, 0, MONO_STEPS - 1);
        m->pattern_len = iclamp(m->pattern_len, 1, MONO_STEPS - m->pattern_start);
        reset_sequence_cursors(m);
        changed(m);
        return;
    }
    if (!strcmp(key, "pattern_len")) {
        capture_undo(m);
        m->pattern_len = iclamp(v, 1, MONO_STEPS - m->pattern_start);
        reconcile_pattern_length_edit(m);
        changed(m);
        return;
    }
    if (!strcmp(key, "play_order")) {
        capture_undo(m);
        m->play_order = iclamp(v, MONO_PLAY_FORWARD, MONO_PLAY_MODE_COUNT - 1);
        reset_sequence_cursors(m);
        changed(m);
        return;
    }
    if (!strcmp(key, "swing")) { capture_undo(m); m->swing = iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "bpm_override")) { m->bpm_override = fclamp(strtof(val, NULL), 0, 400); changed(m); return; }
    if (!strcmp(key, "master")) { m->master = fclamp(strtof(val, NULL) / 100.0f, 0, 2); changed(m); return; }
    if (!strcmp(key, "calibration_mode")) {
        m->calibration_mode = iclamp(v, 0, 5);
        m->calibration_phase = 0.0;
        m->calibration_frames = 0;
        changed(m);
        return;
    }
    if (!strcmp(key, "calibration_level")) {
        m->calibration_level = iclamp(v, 0, 127);
        changed(m);
        return;
    }
    if (!strcmp(key, "calibration_reset") && v) {
        m->render_peak = 0;
        m->lifetime_peak = 0;
        m->render_blocks = 0;
        m->nonzero_blocks = 0;
        m->nonfinite_samples = 0;
        m->calibration_phase = 0.0;
        m->calibration_frames = 0;
        changed(m);
        return;
    }
    if (!strcmp(key, "record")) { m->record_locks = v != 0; changed(m); return; }
    if (!strcmp(key, "transport")) {
        if (v && !m->transport) {
            reset_sequence_cursors(m);
            m->internal_frames = 0;
            mono_advance_step(m);
        }
        m->transport = v != 0;
        if (!m->transport) {
            m->external_clock = 0;
            m->external_clock_age = 0;
            reset_effective_to_base(m);
        }
        changed(m);
        return;
    }
    mono_track_t *t = &m->track[m->selected_track];
    if (!strcmp(key, "patch_init") && v) {
        capture_undo(m);
        initialize_track_sound(t, t->machine);
        changed(m);
        return;
    }
    if (!strcmp(key, "patch_load")) {
        capture_undo(m);
        apply_library_patch(t, v);
        changed(m);
        return;
    }
    if (!strcmp(key, "patch_randomize") && v) {
        capture_undo(m);
        randomize_track_sound(t);
        changed(m);
        return;
    }
    if (!strcmp(key, "morph_capture_a") && v) {
        capture_undo(m);
        capture_morph_endpoint(t, 0);
        changed(m);
        return;
    }
    if (!strcmp(key, "morph_capture_b") && v) {
        capture_undo(m);
        capture_morph_endpoint(t, 1);
        changed(m);
        return;
    }
    if (!strcmp(key, "morph")) {
        capture_undo(m);
        apply_morph(t, v);
        changed(m);
        return;
    }
    if (!strcmp(key, "arp_enabled")) {
        capture_undo(m);
        t->arp.enabled = v != 0;
        reset_arp_runtime(t);
        if (!t->arp.enabled) {
            memset(t->held_notes, 0, sizeof(t->held_notes));
            t->note = -1;
            env_release(m, &t->amp, t->effective[11]);
        }
        changed(m);
        return;
    }
    if (!strcmp(key, "arp_latch")) {
        capture_undo(m);
        t->arp.latch = v != 0;
        if (!t->arp.latch)
            for (int note = 0; note < 128; ++note)
                if (!t->physical_notes[note]) t->held_notes[note] = 0;
        changed(m);
        return;
    }
    if (!strcmp(key, "arp_mode")) { capture_undo(m); t->arp.mode = (uint8_t)iclamp(v, 0, 5); reset_arp_runtime(t); changed(m); return; }
    if (!strcmp(key, "arp_rate")) { capture_undo(m); t->arp.rate = (uint8_t)iclamp(v, 0, 7); reset_arp_runtime(t); changed(m); return; }
    if (!strcmp(key, "arp_octaves")) { capture_undo(m); t->arp.octaves = (uint8_t)iclamp(v, 1, 4); reset_arp_runtime(t); changed(m); return; }
    if (!strcmp(key, "arp_gate")) { capture_undo(m); t->arp.gate = (uint8_t)iclamp(v, 1, 127); changed(m); return; }
    if (!strcmp(key, "arp_length")) { capture_undo(m); t->arp.length = (uint8_t)iclamp(v, 1, MONO_ARP_STEPS); reset_arp_runtime(t); changed(m); return; }
    if (!strcmp(key, "arp_velocity")) { capture_undo(m); t->arp.velocity = (uint8_t)iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "arp_offset")) {
        int arp_step, offset;
        if (sscanf(val, "%d:%d", &arp_step, &offset) == 2 &&
            arp_step >= 0 && arp_step < MONO_ARP_STEPS) {
            capture_undo(m);
            t->arp.offset[arp_step] = (int8_t)iclamp(offset, -24, 24);
            changed(m);
        }
        return;
    }
    if (!strcmp(key, "arp_clear") && v) {
        memset(t->held_notes, 0, sizeof(t->held_notes));
        memset(t->held_velocity, 0, sizeof(t->held_velocity));
        memset(t->held_order, 0, sizeof(t->held_order));
        t->note = -1;
        env_release(m, &t->amp, t->effective[11]);
        reset_arp_runtime(t);
        changed(m);
        return;
    }
    if (!strcmp(key, "route_mode")) { capture_undo(m); t->route.route_mode = (uint8_t)iclamp(v, 0, 4); changed(m); return; }
    if (!strcmp(key, "route_amount")) { capture_undo(m); t->route.route_amount = (uint8_t)iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "track_fx_type")) { capture_undo(m); t->route.fx_type = (uint8_t)iclamp(v, 0, 6); changed(m); return; }
    if (!strcmp(key, "track_fx_amount")) { capture_undo(m); t->route.fx_amount = (uint8_t)iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "track_fx_tone")) { capture_undo(m); t->route.fx_tone = (uint8_t)iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "track_fx_feedback")) { capture_undo(m); t->route.fx_feedback = (uint8_t)iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "track_fx_mix")) { capture_undo(m); t->route.fx_mix = (uint8_t)iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "track_level")) { capture_undo(m); t->route.level = (uint8_t)iclamp(v, 0, 127); changed(m); return; }
    if (!strcmp(key, "song_enabled")) { capture_undo(m); m->song_enabled = v != 0; reset_sequence_cursors(m); changed(m); return; }
    if (!strcmp(key, "song_length")) { capture_undo(m); m->song_length = iclamp(v, 1, MONO_SONG_ROWS); m->song_edit_row = iclamp(m->song_edit_row, 0, m->song_length - 1); reset_sequence_cursors(m); changed(m); return; }
    if (!strcmp(key, "song_edit_row")) { m->song_edit_row = iclamp(v, 0, MONO_SONG_ROWS - 1); changed(m); return; }
    mono_song_row_t *song_row = &m->song[m->song_edit_row];
    if (!strcmp(key, "song_start")) { capture_undo(m); song_row->start = (uint8_t)iclamp(v, 0, MONO_STEPS - 1); song_row->length = (uint8_t)iclamp(song_row->length, 1, MONO_STEPS - song_row->start); reset_sequence_cursors(m); changed(m); return; }
    if (!strcmp(key, "song_row_length")) { capture_undo(m); song_row->length = (uint8_t)iclamp(v, 1, MONO_STEPS - song_row->start); reset_sequence_cursors(m); changed(m); return; }
    if (!strcmp(key, "song_repeats")) { capture_undo(m); song_row->repeats = (uint8_t)iclamp(v, 1, 16); reset_sequence_cursors(m); changed(m); return; }
    if (!strcmp(key, "song_transpose")) { capture_undo(m); song_row->transpose = (int8_t)iclamp(v, -24, 24); changed(m); return; }
    if (!strcmp(key, "wave_begin")) {
        int slot = iclamp(v, 0, MONO_USER_WAVES - 1);
        m->wave_upload_slot = slot;
        m->wave_upload_chunks = 0;
        memset(m->wave_upload, 0, sizeof(m->wave_upload));
        changed(m);
        return;
    }
    if (!strcmp(key, "wave_chunk")) {
        int slot, offset, consumed = 0;
        if (sscanf(val, "%d:%d:%n", &slot, &offset, &consumed) == 2 &&
            slot == m->wave_upload_slot && offset >= 0 && offset < MONO_WAVE_LEN &&
            offset % 64 == 0) {
            const char *hex = val + consumed;
            int samples = 0;
            while (samples < 64 && offset + samples < MONO_WAVE_LEN &&
                   hex[samples * 3] && hex[samples * 3 + 1] && hex[samples * 3 + 2]) {
                const char *cursor = hex + samples * 3;
                uint64_t encoded;
                if (!read_hex(&cursor, cursor + 3, 3, &encoded)) return;
                m->wave_upload[offset + samples] = (int16_t)((int)encoded - 2048);
                ++samples;
            }
            if (samples == 64 && hex[samples * 3] == '\0') {
                m->wave_upload_chunks |= (uint8_t)(1u << (offset / 64));
                changed(m);
            }
        }
        return;
    }
    if (!strcmp(key, "wave_commit")) {
        int slot = iclamp(v, 0, MONO_USER_WAVES - 1);
        if (slot == m->wave_upload_slot && m->wave_upload_chunks == 0xffu) {
            commit_user_wave(m, slot);
            changed(m);
        }
        return;
    }
    if (!strcmp(key, "wave_clear")) {
        int slot = iclamp(v, 0, MONO_USER_WAVES - 1);
        m->user_wave_mask &= (uint8_t)~(1u << slot);
        (void)save_user_waves(m);
        generate_wavetables(m);
        load_user_waves(m);
        changed(m);
        return;
    }
    if (!strcmp(key, "edit_step")) {
        m->edit_step = iclamp(v, 0, MONO_STEPS - 1); changed(m); return;
    }
    mono_step_t *edited_step = &t->steps[m->edit_step];
    if (!strcmp(key, "step_note")) {
        capture_undo(m); edited_step->note = (int8_t)iclamp(v, -1, 127);
        if (edited_step->note >= 0 && edited_step->trig_mask == 0) edited_step->trig_mask = 15;
        changed(m); return;
    }
    if (!strcmp(key, "step_velocity")) {
        capture_undo(m); edited_step->velocity = (uint8_t)iclamp(v, 1, 127); changed(m); return;
    }
    if (!strcmp(key, "step_gate")) {
        capture_undo(m); edited_step->gate = (uint8_t)iclamp(v, 1, 127); changed(m); return;
    }
    if (!strcmp(key, "step_trig")) {
        capture_undo(m); edited_step->trig_mask = (uint8_t)iclamp(v, 0, 15); changed(m); return;
    }
    if (!strcmp(key, "step_probability")) {
        capture_undo(m); edited_step->probability = (uint8_t)iclamp(v, 0, 127); changed(m); return;
    }
    if (!strcmp(key, "step_retrig")) {
        capture_undo(m); edited_step->retrig = (uint8_t)iclamp(v, 1, 8); changed(m); return;
    }
    if (!strcmp(key, "step_condition")) {
        capture_undo(m); edited_step->condition = (uint8_t)iclamp(v, 0, 6); changed(m); return;
    }
    if (!strcmp(key, "step_slide")) {
        capture_undo(m); edited_step->slide = (uint8_t)iclamp(v, 0, 127); changed(m); return;
    }
    if (!strcmp(key, "step_micro")) {
        capture_undo(m); edited_step->micro = (int8_t)iclamp(v, -23, 23); changed(m); return;
    }
    if (!strcmp(key, "step_tie")) {
        capture_undo(m); edited_step->tie = (uint8_t)(v != 0); changed(m); return;
    }
    if (!strcmp(key, "step_accent")) {
        capture_undo(m); edited_step->accent = (uint8_t)iclamp(v, 0, 127); changed(m); return;
    }
    if (!strcmp(key, "track_follow")) {
        capture_undo(m);
        t->seq_override = v ? 0 : 1;
        reset_sequence_cursors(m); changed(m); return;
    }
    if (!strcmp(key, "track_start")) {
        capture_undo(m);
        t->seq_override = 1;
        t->seq_start = iclamp(v, 0, MONO_STEPS - 1);
        t->seq_len = iclamp(t->seq_len, 1, MONO_STEPS - t->seq_start);
        reset_sequence_cursors(m); changed(m); return;
    }
    if (!strcmp(key, "track_len")) {
        capture_undo(m);
        t->seq_override = 1;
        t->seq_len = iclamp(v, 1, MONO_STEPS - t->seq_start);
        reset_sequence_cursors(m); changed(m); return;
    }
    if (!strcmp(key, "track_rotate")) {
        capture_undo(m);
        t->seq_rotation = iclamp(v, 0, MONO_STEPS - 1);
        reset_sequence_cursors(m); changed(m); return;
    }
    if (!strcmp(key, "track_div")) {
        capture_undo(m);
        t->seq_division = iclamp(v, 1, 8);
        reset_sequence_cursors(m); changed(m); return;
    }
    if (!strcmp(key, "keyboard_octave")) {
        int next = iclamp(v, -4, 4);
        if (next == t->keyboard_octave) return;
        capture_undo(m);
        t->keyboard_octave = next;
        /* Changing the pad map while notes are held would make their note-off
         * events resolve to the new octave. Release the old map atomically so
         * octave changes can never leave a performance or arp note stuck. */
        memset(t->held_notes, 0, sizeof(t->held_notes));
        memset(t->held_velocity, 0, sizeof(t->held_velocity));
        memset(t->held_order, 0, sizeof(t->held_order));
        memset(t->physical_notes, 0, sizeof(t->physical_notes));
        t->note = -1;
        t->gate_left = 0;
        env_release(m, &t->amp, t->effective[11]);
        reset_arp_runtime(t);
        changed(m);
        return;
    }
    if (!strcmp(key, "track_mute")) { capture_undo(m); t->mute = v != 0; changed(m); return; }
    if (!strcmp(key, "track_solo")) { capture_undo(m); t->solo = v != 0; changed(m); return; }
    if (!strcmp(key, "track_mute_toggle")) {
        int tr = iclamp(v, 0, m->track_count - 1);
        capture_undo(m); m->track[tr].mute = !m->track[tr].mute; changed(m); return;
    }
    if (!strcmp(key, "track_solo_toggle")) {
        int tr = iclamp(v, 0, m->track_count - 1);
        capture_undo(m); m->track[tr].solo = !m->track[tr].solo; changed(m); return;
    }
    if (!strcmp(key, "machine")) { capture_undo(m); select_machine(t, v); changed(m); return; }
    int pid = param_id(m, key);
    if (pid >= 0) {
        int max = is_lfo_destination_param(pid) ? MONO_LFO_DESTINATIONS - 1 : 127;
        t->base[pid] = (uint8_t)iclamp(v, 0, max);
        t->effective[pid] = t->base[pid];
        if (pid < 8 || (pid >= MONO_ALT_BASE && pid < MONO_ALT_BASE + 8))
            remember_machine(t);
        int record_step = t->play_step >= 0 ? t->play_step : m->seq_step;
        if (m->record_locks && m->transport && record_step >= 0) {
            mono_step_t *step = &t->steps[record_step];
            step_set_lock(step, pid);
            step->lock_values[pid] = t->base[pid];
        }
        changed(m);
        return;
    }
    if (!strcmp(key, "toggle_step")) {
        int step = iclamp(v, 0, MONO_STEPS - 1);
        capture_undo(m);
        mono_step_t *s = &t->steps[step];
        if (step_is_active(s)) clear_step(s);
        else { s->note = (int8_t)t->last_note; s->velocity = 100; s->gate = 100; s->trig_mask = 15; }
        changed(m);
        return;
    }
    if (!strcmp(key, "set_step")) {
        int step, note, vel, gate, mask;
        if (sscanf(val, "%d:%d:%d:%d:%d", &step, &note, &vel, &gate, &mask) == 5 &&
            step >= 0 && step < MONO_STEPS) {
            capture_undo(m);
            mono_step_t *s = &t->steps[step];
            s->note = (int8_t)iclamp(note, -1, 127);
            s->velocity = (uint8_t)iclamp(vel, 1, 127);
            s->gate = (uint8_t)iclamp(gate, 1, 127);
            s->trig_mask = (uint8_t)iclamp(mask, 0, 15);
            changed(m);
        }
        return;
    }
    if (!strcmp(key, "lock")) { set_lock(m, val, 0); changed(m); return; }
    if (!strcmp(key, "unlock")) { set_lock(m, val, 1); changed(m); return; }
    if (!strcmp(key, "clear_pattern") && v) {
        capture_undo(m);
        for (int ti = 0; ti < m->track_count; ++ti)
            for (int s = 0; s < MONO_STEPS; ++s) clear_step(&m->track[ti].steps[s]);
        changed(m);
        return;
    }
    if (!strcmp(key, "copy_step")) {
        int tr = m->selected_track, step = v;
        if (sscanf(val, "%d:%d", &tr, &step) < 2) tr = m->selected_track;
        if (tr >= 0 && tr < m->track_count && step >= 0 && step < MONO_STEPS) {
            m->step_clipboard = m->track[tr].steps[step];
            m->step_clipboard_valid = 1;
        }
        return;
    }
    if (!strcmp(key, "paste_step")) {
        int tr = m->selected_track, step = v;
        if (sscanf(val, "%d:%d", &tr, &step) < 2) tr = m->selected_track;
        if (m->step_clipboard_valid && tr >= 0 && tr < m->track_count &&
            step >= 0 && step < MONO_STEPS) {
            capture_undo(m);
            m->track[tr].steps[step] = m->step_clipboard;
            changed(m);
        }
        return;
    }
    if (!strcmp(key, "clear_step")) {
        int tr = m->selected_track, step = v;
        if (sscanf(val, "%d:%d", &tr, &step) < 2) tr = m->selected_track;
        if (tr >= 0 && tr < m->track_count && step >= 0 && step < MONO_STEPS) {
            capture_undo(m); clear_step(&m->track[tr].steps[step]); changed(m);
        }
        return;
    }
    if (!strcmp(key, "copy_track")) {
        int tr = iclamp(v, 0, m->track_count - 1);
        copy_track_edit(&m->track_clipboard, &m->track[tr]);
        m->track_clipboard_valid = 1;
        return;
    }
    if (!strcmp(key, "paste_track")) {
        int tr = iclamp(v, 0, m->track_count - 1);
        if (m->track_clipboard_valid) {
            capture_undo(m); apply_track_edit(&m->track[tr], &m->track_clipboard);
            reset_sequence_cursors(m); changed(m);
        }
        return;
    }
    if (!strcmp(key, "undo") && v) { perform_undo(m); return; }
    if (!strcmp(key, "note_on")) {
        int tr = m->selected_track, note = 60, vel = 100;
        if (sscanf(val, "%d:%d:%d", &tr, &note, &vel) < 3) {
            tr = m->selected_track;
            sscanf(val, "%d:%d", &note, &vel);
        }
        mono_note_on(m, tr, note, vel);
        return;
    }
    if (!strcmp(key, "note_off")) {
        int tr = m->selected_track, note = v;
        if (sscanf(val, "%d:%d", &tr, &note) < 2) tr = m->selected_track;
        mono_note_off(m, tr, note);
        return;
    }
}

int mono_get_param(mono_t *m, const char *key, char *buf, int buf_len) {
    if (!m || !key || !buf || buf_len <= 0) return -1;
    if (!strcmp(key, "track")) return snprintf(buf, (size_t)buf_len, "%d", m->selected_track);
    if (!strcmp(key, "page")) return snprintf(buf, (size_t)buf_len, "%d", m->selected_page);
    if (!strcmp(key, "step_page")) return snprintf(buf, (size_t)buf_len, "%d", m->step_page);
    if (!strcmp(key, "pattern_start")) return snprintf(buf, (size_t)buf_len, "%d", m->pattern_start);
    if (!strcmp(key, "pattern_len")) return snprintf(buf, (size_t)buf_len, "%d", m->pattern_len);
    if (!strcmp(key, "play_order")) return snprintf(buf, (size_t)buf_len, "%d", m->play_order);
    if (!strcmp(key, "swing")) return snprintf(buf, (size_t)buf_len, "%d", m->swing);
    if (!strcmp(key, "transport")) return snprintf(buf, (size_t)buf_len, "%d", m->transport);
    if (!strcmp(key, "record")) return snprintf(buf, (size_t)buf_len, "%d", m->record_locks);
    if (!strcmp(key, "can_undo")) return snprintf(buf, (size_t)buf_len, "%d", m->undo_valid);
    if (!strcmp(key, "can_paste_step")) return snprintf(buf, (size_t)buf_len, "%d", m->step_clipboard_valid);
    if (!strcmp(key, "can_paste_track")) return snprintf(buf, (size_t)buf_len, "%d", m->track_clipboard_valid);
    if (!strcmp(key, "track_states")) {
        int n = 0;
        for (int i = 0; i < m->track_count; ++i) {
            int state = (m->track[i].mute ? 1 : 0) | (m->track[i].solo ? 2 : 0);
            int wrote = snprintf(buf + n, (size_t)(buf_len - n), "%s%d", i ? "," : "", state);
            if (wrote < 0 || wrote >= buf_len - n) break;
            n += wrote;
        }
        return n;
    }
    if (!strcmp(key, "play_step")) return snprintf(buf, (size_t)buf_len, "%d", m->seq_step);
    if (!strcmp(key, "bpm")) return snprintf(buf, (size_t)buf_len, "%.1f", bpm_now(m));
    if (!strcmp(key, "bpm_override")) return snprintf(buf, (size_t)buf_len, "%.1f", m->bpm_override);
    if (!strcmp(key, "master")) return snprintf(buf, (size_t)buf_len, "%.0f", m->master * 100.0f);
    if (!strcmp(key, "calibration_mode")) return snprintf(buf, (size_t)buf_len, "%d", m->calibration_mode);
    if (!strcmp(key, "calibration_level")) return snprintf(buf, (size_t)buf_len, "%d", m->calibration_level);
    if (!strcmp(key, "calibration_metrics"))
        return snprintf(buf, (size_t)buf_len, "%u:%u:%d:%d:%u",
                        m->render_blocks, m->nonzero_blocks, m->render_peak,
                        m->lifetime_peak, m->nonfinite_samples);
    mono_track_t *t = &m->track[m->selected_track];
    mono_step_t *edited_step = &t->steps[m->edit_step];
    if (!strcmp(key, "patch_count")) return snprintf(buf, (size_t)buf_len, "%d", MONO_PATCH_COUNT);
    if (!strcmp(key, "patch_names")) {
        int n = 0;
        for (int i = 0; i < MONO_PATCH_COUNT; ++i) {
            int wrote = snprintf(buf + n, (size_t)(buf_len - n), "%s%s",
                                 i ? "|" : "", patch_library[i].name);
            if (wrote < 0 || wrote >= buf_len - n) break;
            n += wrote;
        }
        return n;
    }
    if (!strcmp(key, "morph")) return snprintf(buf, (size_t)buf_len, "%d", t->morph_value);
    if (!strcmp(key, "morph_valid")) return snprintf(buf, (size_t)buf_len, "%d", t->morph_valid);
    if (!strcmp(key, "morph_status")) return snprintf(buf, (size_t)buf_len, "%d:%d:%d", t->morph_valid, t->morph_value, t->morph_machine);
    if (!strcmp(key, "arp_enabled")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.enabled);
    if (!strcmp(key, "arp_latch")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.latch);
    if (!strcmp(key, "arp_mode")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.mode);
    if (!strcmp(key, "arp_rate")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.rate);
    if (!strcmp(key, "arp_octaves")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.octaves);
    if (!strcmp(key, "arp_gate")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.gate);
    if (!strcmp(key, "arp_length")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.length);
    if (!strcmp(key, "arp_velocity")) return snprintf(buf, (size_t)buf_len, "%d", t->arp.velocity);
    if (!strcmp(key, "arp_offsets")) {
        int n = 0;
        for (int i = 0; i < MONO_ARP_STEPS; ++i) {
            int wrote = snprintf(buf + n, (size_t)(buf_len - n), "%s%d",
                                 i ? "," : "", t->arp.offset[i]);
            if (wrote < 0 || wrote >= buf_len - n) break;
            n += wrote;
        }
        return n;
    }
    if (!strcmp(key, "route_mode")) return snprintf(buf, (size_t)buf_len, "%d", t->route.route_mode);
    if (!strcmp(key, "route_amount")) return snprintf(buf, (size_t)buf_len, "%d", t->route.route_amount);
    if (!strcmp(key, "track_fx_type")) return snprintf(buf, (size_t)buf_len, "%d", t->route.fx_type);
    if (!strcmp(key, "track_fx_amount")) return snprintf(buf, (size_t)buf_len, "%d", t->route.fx_amount);
    if (!strcmp(key, "track_fx_tone")) return snprintf(buf, (size_t)buf_len, "%d", t->route.fx_tone);
    if (!strcmp(key, "track_fx_feedback")) return snprintf(buf, (size_t)buf_len, "%d", t->route.fx_feedback);
    if (!strcmp(key, "track_fx_mix")) return snprintf(buf, (size_t)buf_len, "%d", t->route.fx_mix);
    if (!strcmp(key, "track_level")) return snprintf(buf, (size_t)buf_len, "%d", t->route.level);
    if (!strcmp(key, "user_wave_mask")) return snprintf(buf, (size_t)buf_len, "%d", m->user_wave_mask);
    if (!strcmp(key, "wave_upload_status")) return snprintf(buf, (size_t)buf_len, "%d:%d", m->wave_upload_slot, m->wave_upload_chunks);
    if (!strcmp(key, "song_enabled")) return snprintf(buf, (size_t)buf_len, "%d", m->song_enabled);
    if (!strcmp(key, "song_length")) return snprintf(buf, (size_t)buf_len, "%d", m->song_length);
    if (!strcmp(key, "song_edit_row")) return snprintf(buf, (size_t)buf_len, "%d", m->song_edit_row);
    if (!strcmp(key, "song_play_row")) return snprintf(buf, (size_t)buf_len, "%d", m->song_play_row);
    mono_song_row_t *song_row = &m->song[m->song_edit_row];
    if (!strcmp(key, "song_start")) return snprintf(buf, (size_t)buf_len, "%d", song_row->start);
    if (!strcmp(key, "song_row_length")) return snprintf(buf, (size_t)buf_len, "%d", song_row->length);
    if (!strcmp(key, "song_repeats")) return snprintf(buf, (size_t)buf_len, "%d", song_row->repeats);
    if (!strcmp(key, "song_transpose")) return snprintf(buf, (size_t)buf_len, "%d", song_row->transpose);
    if (!strcmp(key, "song_rows")) {
        int n = 0;
        for (int row = 0; row < MONO_SONG_ROWS; ++row) {
            int wrote = snprintf(buf + n, (size_t)(buf_len - n), "%s%d:%d:%d:%d",
                                 row ? ";" : "", m->song[row].start,
                                 m->song[row].length, m->song[row].repeats,
                                 m->song[row].transpose);
            if (wrote < 0 || wrote >= buf_len - n) break;
            n += wrote;
        }
        return n;
    }
    if (!strcmp(key, "edit_step")) return snprintf(buf, (size_t)buf_len, "%d", m->edit_step);
    if (!strcmp(key, "step_note")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->note);
    if (!strcmp(key, "step_velocity")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->velocity);
    if (!strcmp(key, "step_gate")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->gate);
    if (!strcmp(key, "step_trig")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->trig_mask);
    if (!strcmp(key, "step_probability")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->probability);
    if (!strcmp(key, "step_retrig")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->retrig);
    if (!strcmp(key, "step_condition")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->condition);
    if (!strcmp(key, "step_slide")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->slide);
    if (!strcmp(key, "step_micro")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->micro);
    if (!strcmp(key, "step_tie")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->tie);
    if (!strcmp(key, "step_accent")) return snprintf(buf, (size_t)buf_len, "%d", edited_step->accent);
    if (!strcmp(key, "track_follow")) return snprintf(buf, (size_t)buf_len, "%d", !t->seq_override);
    if (!strcmp(key, "track_start")) return snprintf(buf, (size_t)buf_len, "%d",
        t->seq_override ? t->seq_start : m->pattern_start);
    if (!strcmp(key, "track_len")) return snprintf(buf, (size_t)buf_len, "%d",
        t->seq_override ? t->seq_len : m->pattern_len);
    if (!strcmp(key, "track_rotate")) return snprintf(buf, (size_t)buf_len, "%d", t->seq_rotation);
    if (!strcmp(key, "track_div")) return snprintf(buf, (size_t)buf_len, "%d", t->seq_division);
    if (!strcmp(key, "track_mute")) return snprintf(buf, (size_t)buf_len, "%d", t->mute);
    if (!strcmp(key, "track_solo")) return snprintf(buf, (size_t)buf_len, "%d", t->solo);
    if (!strcmp(key, "track_play_step")) return snprintf(buf, (size_t)buf_len, "%d", t->play_step);
    if (!strcmp(key, "keyboard_octave")) return snprintf(buf, (size_t)buf_len, "%d", t->keyboard_octave);
    if (!strcmp(key, "machine")) return snprintf(buf, (size_t)buf_len, "%d", t->machine);
    /* One read that tells an editor whether anything moved under it. The Move
     * UIs poll this instead of "record" — same single round-trip on the ~23 ms
     * param channel, but it also carries the external-CC counter, so a CC edit
     * can be followed without a periodic blind refresh. */
    if (!strcmp(key, "ui_poll"))
        return snprintf(buf, (size_t)buf_len, "%u:%d", m->cc_revision, m->record_locks);
    /* The selected page's eight primary values and its eight Shift-bank
     * values, in ONE read. Refreshing them individually is sixteen blocking
     * round-trips; following a CC sweep at that price would claim the whole
     * channel. Same layout param_id() gives p1..p8 and alt1..alt8. */
    if (!strcmp(key, "page_values")) {
        int n = 0;
        for (int i = 0; i < 2 * MONO_PAGE_PARAMS; ++i) {
            int bank = i / MONO_PAGE_PARAMS, slot = i % MONO_PAGE_PARAMS;
            int vid = bank ? MONO_SHIFT_BASE + m->selected_page * MONO_PAGE_PARAMS + slot
                           : m->selected_page * MONO_PAGE_PARAMS + slot;
            const char *sep = i == 0 ? "" : (slot == 0 ? "|" : ",");
            int wrote = snprintf(buf + n, (size_t)(buf_len - n), "%s%d", sep, t->base[vid]);
            if (wrote < 0 || wrote >= buf_len - n) break;
            n += wrote;
        }
        return n;
    }
    int pid = param_id(m, key);
    if (pid >= 0) {
        return snprintf(buf, (size_t)buf_len, "%d", t->base[pid]);
    }
    if (!strcmp(key, "steps")) {
        int n = 0;
        int first = m->step_page * 16;
        for (int i = 0; i < 16; ++i) {
            mono_step_t *s = &t->steps[first + i];
            int state = step_display_state(s);
            int wrote = snprintf(buf + n, (size_t)(buf_len - n), "%s%d", i ? "," : "", state);
            if (wrote < 0 || wrote >= buf_len - n) break;
            n += wrote;
        }
        return n;
    }
    if (!strcmp(key, "all_steps")) {
        int n = 0;
        for (int i = 0; i < MONO_STEPS; ++i) {
            mono_step_t *s = &t->steps[i];
            int state = step_display_state(s);
            int wrote = snprintf(buf + n, (size_t)(buf_len - n), "%s%d", i ? "," : "", state);
            if (wrote < 0 || wrote >= buf_len - n) break;
            n += wrote;
        }
        return n;
    }
    if (!strcmp(key, "status"))
        return snprintf(buf, (size_t)buf_len, "%d:%d:%.1f:%d:%d:%d:%d:%d:%d:%d",
                        m->transport, t->play_step, bpm_now(m), m->selected_track,
                        m->selected_page, t->machine, m->pattern_len, m->record_locks,
                        m->pattern_start, m->play_order);
    if (!strcmp(key, "rui_poll"))
        return snprintf(buf, (size_t)buf_len, "%u:%d:%d:%.0f:%d",
                        m->revision, m->transport, m->seq_step, bpm_now(m),
                        m->record_locks);
    /* Field 5 is the external-CC counter, appended for the overtake editor so
     * it follows CC edits without paying a second round-trip. Append-only:
     * web_ui.html splits this tuple and indexes fields 0-3. */
    if (!strcmp(key, "rui_play"))
        return snprintf(buf, (size_t)buf_len, "%d:%d:%.0f:%d:%u",
                        m->transport, t->play_step, bpm_now(m), m->record_locks,
                        m->cc_revision);
    if (!strcmp(key, "debug"))
        return snprintf(buf, (size_t)buf_len,
                        "%u:%d:%d:%u:%u:%u:%d:%d:%d:%d:%d:%d:%d",
                        m->note_events, m->render_peak, m->lifetime_peak,
                        m->render_blocks, m->nonzero_blocks, m->nonfinite_samples,
                        m->sample_rate, t->note, t->velocity, t->amp.stage,
                        (int)lrintf(t->amp.value * 1000.0f),
                        (int)lrintf(t->freq * 10.0f), t->base[13]);
    if (!strcmp(key, "state")) {
        char step_csv[96];
        char all_step_csv[192];
        char track_state_csv[24];
        char arp_offset_csv[80];
        char song_row_csv[256];
        int sn = 0;
        int first = m->step_page * 16;
        for (int i = 0; i < 16 && sn < (int)sizeof(step_csv); ++i) {
            mono_step_t *s = &t->steps[first + i];
            int state = step_display_state(s);
            int wrote = snprintf(step_csv + sn, sizeof(step_csv) - (size_t)sn,
                                 "%s%d", i ? "," : "", state);
            if (wrote < 0 || wrote >= (int)sizeof(step_csv) - sn) break;
            sn += wrote;
        }
        int an = 0;
        for (int i = 0; i < MONO_STEPS && an < (int)sizeof(all_step_csv); ++i) {
            mono_step_t *s = &t->steps[i];
            int state = step_display_state(s);
            int wrote = snprintf(all_step_csv + an, sizeof(all_step_csv) - (size_t)an,
                                 "%s%d", i ? "," : "", state);
            if (wrote < 0 || wrote >= (int)sizeof(all_step_csv) - an) break;
            an += wrote;
        }
        int tn = 0;
        for (int i = 0; i < m->track_count && tn < (int)sizeof(track_state_csv); ++i) {
            int state = (m->track[i].mute ? 1 : 0) | (m->track[i].solo ? 2 : 0);
            int wrote = snprintf(track_state_csv + tn, sizeof(track_state_csv) - (size_t)tn,
                                 "%s%d", i ? "," : "", state);
            if (wrote < 0 || wrote >= (int)sizeof(track_state_csv) - tn) break;
            tn += wrote;
        }
        int on = 0;
        for (int i = 0; i < MONO_ARP_STEPS && on < (int)sizeof(arp_offset_csv); ++i) {
            int wrote = snprintf(arp_offset_csv + on, sizeof(arp_offset_csv) - (size_t)on,
                                 "%s%d", i ? "," : "", t->arp.offset[i]);
            if (wrote < 0 || wrote >= (int)sizeof(arp_offset_csv) - on) break;
            on += wrote;
        }
        int rn = 0;
        for (int row = 0; row < MONO_SONG_ROWS && rn < (int)sizeof(song_row_csv); ++row) {
            int wrote = snprintf(song_row_csv + rn, sizeof(song_row_csv) - (size_t)rn,
                                 "%s%d:%d:%d:%d", row ? ";" : "",
                                 m->song[row].start, m->song[row].length,
                                 m->song[row].repeats, m->song[row].transpose);
            if (wrote < 0 || wrote >= (int)sizeof(song_row_csv) - rn) break;
            rn += wrote;
        }
        int n = 0;
        int shift_base = MONO_SHIFT_BASE + m->selected_page * MONO_PAGE_PARAMS;
        if (!appendf(buf, buf_len, &n,
                     "{\"v\":11,\"track\":%d,\"page\":%d,\"step_page\":%d,"
                     "\"pattern_start\":%d,\"pattern_len\":%d,\"play_order\":%d,\"swing\":%d,"
                     "\"master\":%.0f,\"bpm_override\":%.1f,"
                     "\"machine\":%d,\"track_follow\":%d,\"track_start\":%d,"
                     "\"track_len\":%d,\"track_rotate\":%d,\"track_div\":%d,"
                     "\"track_mute\":%d,\"track_solo\":%d,"
                     "\"edit_step\":%d,\"step_note\":%d,\"step_velocity\":%d,"
                     "\"step_gate\":%d,\"step_trig\":%d,\"step_probability\":%d,"
                     "\"step_retrig\":%d,\"step_condition\":%d,\"step_slide\":%d,"
                     "\"step_micro\":%d,\"step_tie\":%d,\"step_accent\":%d,"
                     "\"arp_enabled\":%d,\"arp_latch\":%d,\"arp_mode\":%d,"
                     "\"arp_rate\":%d,\"arp_octaves\":%d,\"arp_gate\":%d,"
                     "\"arp_length\":%d,\"arp_velocity\":%d,\"arp_offsets\":\"%s\","
                     "\"route_mode\":%d,\"route_amount\":%d,\"track_fx_type\":%d,"
                     "\"track_fx_amount\":%d,\"track_fx_tone\":%d,"
                     "\"track_fx_feedback\":%d,\"track_fx_mix\":%d,\"track_level\":%d,"
                     "\"morph_valid\":%d,\"morph\":%d,\"user_wave_mask\":%d,"
                     "\"song_enabled\":%d,\"song_length\":%d,\"song_edit_row\":%d,"
                     "\"song_start\":%d,\"song_row_length\":%d,\"song_repeats\":%d,"
                     "\"song_transpose\":%d,\"song_rows\":\"%s\","
                     "\"calibration_mode\":%d,\"calibration_level\":%d,"
                     "\"p1\":%d,\"p2\":%d,\"p3\":%d,\"p4\":%d,"
                     "\"p5\":%d,\"p6\":%d,\"p7\":%d,\"p8\":%d,"
                     "\"alt1\":%d,\"alt2\":%d,\"alt3\":%d,\"alt4\":%d,"
                     "\"alt5\":%d,\"alt6\":%d,\"alt7\":%d,\"alt8\":%d,"
                     "\"record\":%d,\"track_states\":\"%s\","
                     "\"debug\":\"%u:%d:%d\","
                     "\"steps\":\"%s\",\"all_steps\":\"%s\",\"data\":\"",
                     m->selected_track, m->selected_page, m->step_page,
                     m->pattern_start, m->pattern_len, m->play_order, m->swing,
                     m->master * 100.0f, m->bpm_override,
                     t->machine, !t->seq_override,
                     t->seq_override ? t->seq_start : m->pattern_start,
                     t->seq_override ? t->seq_len : m->pattern_len,
                     t->seq_rotation, t->seq_division, t->mute, t->solo,
                     m->edit_step, edited_step->note, edited_step->velocity,
                     edited_step->gate, edited_step->trig_mask,
                     edited_step->probability, edited_step->retrig,
                     edited_step->condition, edited_step->slide,
                     edited_step->micro, edited_step->tie, edited_step->accent,
                     t->arp.enabled, t->arp.latch, t->arp.mode, t->arp.rate,
                     t->arp.octaves, t->arp.gate, t->arp.length, t->arp.velocity,
                     arp_offset_csv,
                     t->route.route_mode, t->route.route_amount, t->route.fx_type,
                     t->route.fx_amount, t->route.fx_tone, t->route.fx_feedback,
                     t->route.fx_mix, t->route.level,
                     t->morph_valid, t->morph_value, m->user_wave_mask,
                     m->song_enabled, m->song_length, m->song_edit_row,
                     m->song[m->song_edit_row].start,
                     m->song[m->song_edit_row].length,
                     m->song[m->song_edit_row].repeats,
                     m->song[m->song_edit_row].transpose,
                     song_row_csv,
                     m->calibration_mode, m->calibration_level,
                     t->base[m->selected_page * 8], t->base[m->selected_page * 8 + 1],
                     t->base[m->selected_page * 8 + 2], t->base[m->selected_page * 8 + 3],
                     t->base[m->selected_page * 8 + 4], t->base[m->selected_page * 8 + 5],
                     t->base[m->selected_page * 8 + 6], t->base[m->selected_page * 8 + 7],
                     t->base[shift_base], t->base[shift_base + 1],
                     t->base[shift_base + 2], t->base[shift_base + 3],
                     t->base[shift_base + 4], t->base[shift_base + 5],
                     t->base[shift_base + 6], t->base[shift_base + 7],
                     m->record_locks, track_state_csv,
                     m->note_events, m->render_peak, m->lifetime_peak,
                     step_csv, all_step_csv)) return -1;
        if (!appendf(buf, buf_len, &n, "G%02X%02X",
                     m->song_enabled, m->song_length)) return -1;
        for (int row = 0; row < MONO_SONG_ROWS; ++row)
            if (!appendf(buf, buf_len, &n, "%02X%02X%02X%02X",
                         m->song[row].start, m->song[row].length,
                         m->song[row].repeats, m->song[row].transpose + 24)) return -1;
        for (int tr = 0; tr < m->track_count; ++tr) {
            const mono_track_t *saved = &m->track[tr];
            /* State v11 packs the signed -4..+4 keyboard octave beside the
             * solo flag so existing dense patterns stay within the host's
             * fixed state buffer. Bit 0 remains solo for migration. */
            if (!appendf(buf, buf_len, &n, "T%X%02X%02X%02X%02X%02X%02X%02X%02X",
                         tr, saved->machine, saved->seq_override, saved->seq_start,
                         saved->seq_len, saved->seq_rotation, saved->seq_division,
                         saved->mute,
                         saved->solo | ((saved->keyboard_octave + 4) << 1))) return -1;
            if (!appendf(buf, buf_len, &n,
                         "%02X%02X%02X%02X%02X%02X%02X%02X",
                         saved->arp.enabled, saved->arp.latch, saved->arp.mode,
                         saved->arp.rate, saved->arp.octaves, saved->arp.gate,
                         saved->arp.length, saved->arp.velocity)) return -1;
            for (int step = 0; step < MONO_ARP_STEPS; ++step)
                if (!appendf(buf, buf_len, &n, "%02X", saved->arp.offset[step] + 24))
                    return -1;
            if (!appendf(buf, buf_len, &n,
                         "%02X%02X%02X%02X%02X%02X%02X%02X",
                         saved->route.route_mode, saved->route.route_amount,
                         saved->route.fx_type, saved->route.fx_amount,
                         saved->route.fx_tone, saved->route.fx_feedback,
                         saved->route.fx_mix, saved->route.level)) return -1;
            for (int pid = 0; pid < MONO_PARAMS; ++pid)
                if (!appendf(buf, buf_len, &n, "%02X", saved->base[pid])) return -1;
            for (int machine = 0; machine < MONO_MACHINE_COUNT; ++machine) {
                if (!saved->machine_valid[machine]) continue;
                if (!appendf(buf, buf_len, &n, "M%X%02X", tr, machine)) return -1;
                for (int i = 0; i < 16; ++i)
                    if (!appendf(buf, buf_len, &n, "%02X",
                                 saved->machine_params[machine][i])) return -1;
            }
            if (saved->morph_valid & 1) {
                if (!appendf(buf, buf_len, &n, "A%X%02X%02X", tr,
                             saved->morph_machine, saved->morph_value)) return -1;
                for (int pid = 0; pid < MONO_PARAMS; ++pid)
                    if (!appendf(buf, buf_len, &n, "%02X", saved->morph_a[pid])) return -1;
            }
            if (saved->morph_valid & 2) {
                if (!appendf(buf, buf_len, &n, "B%X%02X%02X", tr,
                             saved->morph_machine, saved->morph_value)) return -1;
                for (int pid = 0; pid < MONO_PARAMS; ++pid)
                    if (!appendf(buf, buf_len, &n, "%02X", saved->morph_b[pid])) return -1;
            }
            for (int si = 0; si < MONO_STEPS; ++si) {
                const mono_step_t *step = &saved->steps[si];
                if (!step_is_active(step)) continue;
                if (!appendf(buf, buf_len, &n,
                             "S%X%02X%02X%02X%02X%02X",
                             tr, si, (unsigned)(uint8_t)step->note,
                             step->velocity, step->gate, step->trig_mask)) return -1;
                int behavior = (step->probability != 127 ? 1 : 0) |
                    (step->retrig != 1 ? 2 : 0) |
                    (step->condition != 0 ? 4 : 0) |
                    (step->slide != 0 ? 8 : 0);
                if (!appendf(buf, buf_len, &n, "%c", state64[behavior])) return -1;
                if ((behavior & 1) && !appendf(buf, buf_len, &n, "%02X", step->probability)) return -1;
                if ((behavior & 2) && !appendf(buf, buf_len, &n, "%02X", step->retrig)) return -1;
                if ((behavior & 4) && !appendf(buf, buf_len, &n, "%02X", step->condition)) return -1;
                if ((behavior & 8) && !appendf(buf, buf_len, &n, "%02X", step->slide)) return -1;
                int extended = (step->micro != 0 ? 1 : 0) |
                    (step->tie ? 2 : 0) | (step->accent ? 4 : 0) |
                    (step_has_all_locks(step) ? 8 : 0);
                if (!appendf(buf, buf_len, &n, "%c", state64[extended])) return -1;
                if ((extended & 1) && !appendf(buf, buf_len, &n, "%02X", step->micro + 24)) return -1;
                if ((extended & 4) && !appendf(buf, buf_len, &n, "%02X", step->accent)) return -1;
                if (!(extended & 8) && !append_v5_mask(buf, buf_len, &n, step)) return -1;
                if (!append_v5_lock_values(buf, buf_len, &n, step)) return -1;
            }
        }
        if (!appendf(buf, buf_len, &n, "\"}")) return -1;
        return n;
    }
    return -1;
}

int mono_debug_effective_param(mono_t *m, int track, int pid) {
    if (!m || track < 0 || track >= m->track_count || pid < 0 || pid >= MONO_PARAMS)
        return -1;
    return m->track[track].effective[pid];
}

float mono_debug_smoothed_param(mono_t *m, int track, int pid) {
    if (!m || track < 0 || track >= m->track_count || pid < 0 || pid >= MONO_PARAMS)
        return -1.0f;
    return m->track[track].smoothed[pid];
}

void mono_debug_render_oscillator(mono_t *m, int track, float frequency,
                                  float *out, int frames) {
    if (!m || !out || track < 0 || track >= m->track_count || frames <= 0) return;
    mono_track_t *t = &m->track[track];
    float hz = fclamp(frequency, 1.0f, m->sample_rate * 0.45f);
    for (int i = 0; i < frames; ++i) {
        out[i] = oscillator(m, t, hz);
        if (t->voice_age_samples < UINT32_MAX) ++t->voice_age_samples;
    }
}

void mono_debug_filter_cutoffs(mono_t *m, int track, float *highpass_hz,
                               float *lowpass_hz) {
    if (!m || track < 0 || track >= m->track_count) return;
    if (highpass_hz) *highpass_hz = m->track[track].filter_hp_hz;
    if (lowpass_hz) *lowpass_hz = m->track[track].filter_lp_hz;
}

int mono_debug_user_wave_mask(const mono_t *m) {
    return m ? m->user_wave_mask : 0;
}
