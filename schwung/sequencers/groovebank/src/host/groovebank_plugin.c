/*
 * groovebank_plugin.c — Groove Bank: retrigger the chord you HOLD on a library
 * of standard rhythm templates.
 *
 * API: midi_fx_api_v1_t  (entry point: move_midi_fx_init)
 *
 * A chainable MIDI FX, Schwung-only. You own the pitches, Groove Bank owns the
 * timing: hold a chord/note and the selected rhythm template retriggers your
 * held notes on Move's clock, into the Sound Generator loaded in the same slot.
 * (It drives the chain synth, like Super Arp — not Move's native track
 * instruments; see DESIGN.md §5.1.) Change your chord mid-pattern and the next
 * hit strikes what you now hold.
 *
 *   - SWALLOW-AND-RETRIGGER: incoming note-on/off are captured into a held
 *     register and NOT forwarded (the chain is inline-replace, so returning 0
 *     output messages makes the played note vanish); output is only the on-grid
 *     retriggers. Non-note channel messages (CC, bend, aftertouch) pass through.
 *   - TRANSPORT-ANCHORED, FREE-RUNNING: cur_step is a pure function of the clock
 *     count; holding new notes only changes which pitches the next active step
 *     fires. Nothing is enqueued, so fast pad input can never pile up.
 *   - REAL RE-ATTACK: each active step emits fresh note-on for every held pitch
 *     (killing any still-sounding copy first), so envelopes/LFOs restart.
 *
 * Controls: K1 gate (staccato<->legato), K2 strum (bipolar spread), K3 accent
 * (velocity depth), K7 swing, K8 genre. Row chars: . x A g -  (- = tie).
 *
 * Phase 1: clock-driven — transport must be running. (Strum's sub-step onset
 * spread rides tick(), but only in the active-audio window right after a hit, so
 * it never reopens the render-idle problem — see DESIGN.md §6.)
 *
 *   0xFA fires step 0 on the downbeat · 0xF8 advances (6 clocks / 16th),
 *   looping · 0xFC stop.
 */

#include "midi_fx_api_v1.h"
#include "plugin_api_v1.h"
#include "../dsp/grooves.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define MIDI_NOTE_ON   0x90u
#define MIDI_NOTE_OFF  0x80u

#define CLOCKS_PER_STEP 6u   /* 24 PPQN / 4 sixteenths */
#define OUT_CHANNEL     0u   /* the chain/slot rewrites the channel on output */

#define GB_MAX_HELD     16   /* held notes tracked (chord fingers)            */
#define GB_MAX_VOICES   32   /* sounding retriggers, incl. legato tails       */
#define GB_STRUM_MS_MAX 15   /* per-note strum delay (ms) at full strum       */

static const host_api_v1_t *g_host = NULL;

/* The groove bank is read-only and shared across all instances. */
static GrooveBank g_bank = { NULL, 0 };
static int        g_bank_loaded = 0;

typedef struct {
    uint8_t  active;
    uint8_t  note;
    uint16_t clocks_left;
} Voice;

/* A chord note whose strummed onset is still pending (drained in tick()). */
typedef struct {
    uint8_t  active;
    uint8_t  note;
    uint8_t  vel;
    uint16_t gate;
    int32_t  samples_left;
} StrumPending;

typedef struct {
    int     pattern;                 /* selected index into the bank          */
    uint8_t variant;                 /* selected variant (0 = base 'hit')     */
    uint8_t swing;                   /* 0..100, delays off-beat 16ths          */
    uint8_t gate;                    /* note length as % of a step (5..200)    */
    int8_t  strum;                   /* -100..+100: <0 down, >0 up, 0 tight    */
    uint8_t accent;                  /* 0..100 velocity spread (0 = flat)      */
    uint8_t latch;                   /* 1 = held chord sticks after release    */

    uint8_t cur_step;
    uint8_t clock_running;
    uint8_t midi_clocks_until_tick;

    uint32_t preview_revision;
    int      sample_rate;            /* captured from tick()                   */

    uint8_t held[GB_MAX_HELD];       /* register the groove plays (sorted asc)  */
    int     held_count;
    uint8_t phys[GB_MAX_HELD];       /* notes physically down (for latch replace) */
    int     phys_count;

    Voice        voices[GB_MAX_VOICES];
    StrumPending strum_q[GB_MAX_HELD];
} GrooveBankInstance;

/* ── Bank helpers ────────────────────────────────────────────────────────── */

static const GroovePattern *pattern_at(int idx)
{
    if (g_bank.count <= 0) return NULL;
    if (idx < 0) idx = 0;
    if (idx >= g_bank.count) idx = g_bank.count - 1;
    return &g_bank.patterns[idx];
}
static uint8_t pattern_steps(const GrooveBankInstance *gi)
{
    const GroovePattern *p = pattern_at(gi->pattern);
    if (!p || p->steps < 1) return 16u;
    return p->steps > GB_MAX_STEPS ? GB_MAX_STEPS : p->steps;
}

/* Number of authored variants in the current pattern (>= 1). */
static uint8_t cur_variant_count(const GrooveBankInstance *gi)
{
    const GroovePattern *p = pattern_at(gi->pattern);
    return (p && p->variant_count >= 1) ? p->variant_count : 1;
}

/* The row for the selected variant (falls back to base if out of range). */
static const char *sel_row(const GrooveBankInstance *gi, const GroovePattern *p)
{
    int v = gi->variant;
    if (v < 0 || v >= p->variant_count) v = 0;
    return p->row[v];
}

/* Swing: 0..100 maps to 0..2 clocks of delay on the off-beat 16ths (same math
 * as Beat Bank). Off-beats (odd step) fire late, on-beats early. */
static uint8_t swing_clocks(const GrooveBankInstance *gi)
{
    return (uint8_t)((gi->swing * 2u + 50u) / 100u);
}
static uint8_t clocks_before_step(const GrooveBankInstance *gi, uint8_t step)
{
    uint8_t sc = swing_clocks(gi);
    if (step & 1u) return (uint8_t)(CLOCKS_PER_STEP + sc);
    return (uint8_t)(CLOCKS_PER_STEP > sc ? CLOCKS_PER_STEP - sc : 1u);
}

/* Gate length in clocks, from the gate% param (% of one step). >100% legato. */
static uint16_t gate_clocks(const GrooveBankInstance *gi)
{
    uint16_t gc = (uint16_t)((gi->gate * CLOCKS_PER_STEP + 50u) / 100u);
    return gc < 1u ? 1u : gc;
}

/* Velocity for a step char, scaled by the accent param. 0 = flat @100; at the
 * default 50 the spread is wide (A 127 / x 90 / g 38) so accents clearly pop
 * above normal hits, not just above ghosts; 100 = extreme. Rests / ties = 0. */
static uint8_t step_velocity(const GrooveBankInstance *gi, char c)
{
    int acc = gi->accent;   /* 0..100 */
    switch (c) {
        case 'A': case 'X': { int v = 100 + (27 * acc) / 50; return v > 127 ? 127 : (uint8_t)v; }
        case 'x':           { int v = 100 - (acc / 5);       return v < 1   ? 1   : (uint8_t)v; }
        case 'g': case 'o': { int v = 100 - (62 * acc) / 50; return v < 5   ? 5   : (uint8_t)v; }
        default:            return 0u;
    }
}

/* ── Held register ───────────────────────────────────────────────────────── */

static void held_add(GrooveBankInstance *gi, uint8_t note)
{
    int i;
    for (i = 0; i < gi->held_count; i++) if (gi->held[i] == note) return;
    if (gi->held_count >= GB_MAX_HELD) return;
    i = gi->held_count;
    while (i > 0 && gi->held[i - 1] > note) { gi->held[i] = gi->held[i - 1]; i--; }
    gi->held[i] = note;
    gi->held_count++;
}

static void held_remove(GrooveBankInstance *gi, uint8_t note)
{
    for (int i = 0; i < gi->held_count; i++) {
        if (gi->held[i] == note) {
            for (int j = i; j < gi->held_count - 1; j++) gi->held[j] = gi->held[j + 1];
            gi->held_count--;
            return;
        }
    }
}

/* Physically-down notes — tracked separately so latch can tell a fresh press
 * (nothing down) from adding to the current chord. */
static void phys_add(GrooveBankInstance *gi, uint8_t note)
{
    for (int i = 0; i < gi->phys_count; i++) if (gi->phys[i] == note) return;
    if (gi->phys_count >= GB_MAX_HELD) return;
    gi->phys[gi->phys_count++] = note;
}

static void phys_remove(GrooveBankInstance *gi, uint8_t note)
{
    for (int i = 0; i < gi->phys_count; i++) {
        if (gi->phys[i] == note) {
            for (int j = i; j < gi->phys_count - 1; j++) gi->phys[j] = gi->phys[j + 1];
            gi->phys_count--;
            return;
        }
    }
}

/* ── MIDI emit + voice bookkeeping ───────────────────────────────────────── */

static int emit(uint8_t status, uint8_t note, uint8_t vel,
                uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    if (count >= max_out) return count;
    out_msgs[count][0] = status; out_msgs[count][1] = note; out_msgs[count][2] = vel;
    out_lens[count] = 3;
    return count + 1;
}

static int kill_voice(GrooveBankInstance *gi, uint8_t note,
                      uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    for (int v = 0; v < GB_MAX_VOICES; v++) {
        if (gi->voices[v].active && gi->voices[v].note == note) {
            count = emit((uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), note, 0, out_msgs, out_lens, max_out, count);
            gi->voices[v].active = 0;
        }
    }
    return count;
}

static void voice_add(GrooveBankInstance *gi, uint8_t note, uint16_t clocks)
{
    for (int v = 0; v < GB_MAX_VOICES; v++) {
        if (!gi->voices[v].active) {
            gi->voices[v].active = 1;
            gi->voices[v].note = note;
            gi->voices[v].clocks_left = clocks;
            return;
        }
    }
}

static void clear_strum(GrooveBankInstance *gi)
{
    for (int i = 0; i < GB_MAX_HELD; i++) gi->strum_q[i].active = 0;
}

static void strum_enqueue(GrooveBankInstance *gi, uint8_t note, uint8_t vel,
                          uint16_t gate, int32_t samples)
{
    for (int i = 0; i < GB_MAX_HELD; i++) {
        if (!gi->strum_q[i].active) {
            gi->strum_q[i].active = 1;
            gi->strum_q[i].note = note;
            gi->strum_q[i].vel = vel;
            gi->strum_q[i].gate = gate;
            gi->strum_q[i].samples_left = samples;
            return;
        }
    }
}

static int flush_all(GrooveBankInstance *gi, uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    for (int v = 0; v < GB_MAX_VOICES; v++) {
        if (gi->voices[v].active) {
            count = emit((uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), gi->voices[v].note, 0, out_msgs, out_lens, max_out, count);
            gi->voices[v].active = 0;
            if (count >= max_out) break;
        }
    }
    return count;
}

static int advance_voice_clocks(GrooveBankInstance *gi, uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    for (int v = 0; v < GB_MAX_VOICES; v++) {
        Voice *vc = &gi->voices[v];
        if (!vc->active) continue;
        if (vc->clocks_left > 0) vc->clocks_left--;
        if (vc->clocks_left == 0) {
            count = emit((uint8_t)(MIDI_NOTE_OFF | OUT_CHANNEL), vc->note, 0, out_msgs, out_lens, max_out, count);
            vc->active = 0;
            if (count >= max_out) break;
        }
    }
    return count;
}

/* Count consecutive ties '-' immediately following step k (no wrap). */
static int count_ties(const char *row, uint8_t steps, uint8_t k)
{
    int n = 0;
    int len = (int)strlen(row);
    for (int i = k + 1; i < steps && i < len; i++) {
        if (row[i] == '-') n++;
        else break;
    }
    return n;
}

/* Fire the current step: retrigger all held notes if it's a hit. Tight (strum=0)
 * fires the whole chord now; strummed spreads the onsets — first note now, the
 * rest queued for tick(). A tie step is a no-op (covered by the prior hit's
 * look-ahead gate). */
static int fire_step(GrooveBankInstance *gi, uint8_t out_msgs[][3], int out_lens[], int max_out, int count)
{
    const GroovePattern *p = pattern_at(gi->pattern);
    uint8_t steps = pattern_steps(gi);
    uint8_t step = gi->cur_step;
    const char *row;
    char c;

    if (!p) return count;
    if (step >= steps) step = 0;
    row = sel_row(gi, p);

    c = (step < (uint8_t)strlen(row)) ? row[step] : '.';
    if (c != '-') {
        uint8_t vel = step_velocity(gi, c);
        if (vel && gi->held_count > 0) {
            int ties = count_ties(row, steps, step);
            uint16_t gc = (uint16_t)(gate_clocks(gi) + (uint16_t)ties * CLOCKS_PER_STEP);
            int n = gi->held_count;
            int mag = gi->strum < 0 ? -gi->strum : gi->strum;
            int32_t per = 0;

            clear_strum(gi);   /* drop any un-fired strum from the previous step */

            if (mag > 0 && n > 1) {
                int sr = gi->sample_rate > 0 ? gi->sample_rate : 44100;
                per = (int32_t)(((int64_t)mag * GB_STRUM_MS_MAX * sr) / 100 / 1000);
                /* fit: keep the whole strum inside ~80% of a step (needs bpm) */
                if (g_host && g_host->get_bpm) {
                    float bpm = g_host->get_bpm();
                    if (bpm > 1.0f) {
                        int32_t step_s = (int32_t)((float)sr * 60.0f / bpm / 4.0f);
                        int32_t cap = step_s * 8 / 10;
                        if ((int32_t)(n - 1) * per > cap) per = cap / (n - 1);
                    }
                }
            }

            for (int k = 0; k < n; k++) {
                /* order: up-strum (strum >= 0) low->high; down-strum high->low */
                int idx = (gi->strum < 0) ? (n - 1 - k) : k;
                uint8_t note = gi->held[idx];
                if (per == 0 || k == 0) {
                    count = kill_voice(gi, note, out_msgs, out_lens, max_out, count);
                    if (count >= max_out) break;
                    count = emit((uint8_t)(MIDI_NOTE_ON | OUT_CHANNEL), note, vel, out_msgs, out_lens, max_out, count);
                    voice_add(gi, note, gc);
                    if (count >= max_out) break;
                } else {
                    strum_enqueue(gi, note, vel, gc, (int32_t)k * per);
                }
            }
        }
    }

    gi->cur_step = (uint8_t)((step + 1) % steps);
    return count;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

static void *gb_create_instance(const char *module_dir, const char *config_json)
{
    GrooveBankInstance *gi;
    (void)config_json;

    if (!g_bank_loaded) {
        gb_bank_init(&g_bank, module_dir);
        g_bank_loaded = 1;
    }

    gi = (GrooveBankInstance *)calloc(1, sizeof(GrooveBankInstance));
    if (!gi) return NULL;

    gi->pattern = 0;
    gi->variant = 0;
    gi->swing = 0;
    gi->gate = 80;
    gi->strum = 0;
    gi->accent = 50;
    gi->latch = 0;
    gi->phys_count = 0;
    gi->cur_step = 0;
    gi->clock_running = 0;
    gi->midi_clocks_until_tick = CLOCKS_PER_STEP;
    gi->preview_revision = 1;
    gi->sample_rate = 44100;
    gi->held_count = 0;
    return gi;
}

static void gb_destroy_instance(void *instance) { free(instance); }

/* ── MIDI processing ─────────────────────────────────────────────────────── */

static int gb_process_midi(void *instance, const uint8_t *in_msg, int in_len,
                           uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    GrooveBankInstance *gi = (GrooveBankInstance *)instance;
    uint8_t hi, status;
    if (!gi || in_len == 0) return 0;

    hi = in_msg[0];

    /* ── transport / clock (single-byte real-time) ── */
    if (hi == 0xFAu) {                              /* Start */
        int count = flush_all(gi, out_msgs, out_lens, max_out, 0);
        clear_strum(gi);
        gi->cur_step = 0;
        gi->clock_running = 1;
        if (count < max_out)
            count = fire_step(gi, out_msgs, out_lens, max_out, count);
        gi->midi_clocks_until_tick = clocks_before_step(gi, gi->cur_step);
        return count;
    }
    if (hi == 0xFBu) { gi->clock_running = 1; return 0; }   /* Continue */
    if (hi == 0xF8u) {                                       /* Clock tick */
        int count = 0;
        if (!gi->clock_running) return 0;
        count = advance_voice_clocks(gi, out_msgs, out_lens, max_out, count);
        if (count >= max_out) return count;
        if (gi->midi_clocks_until_tick > 0) gi->midi_clocks_until_tick--;
        if (gi->midi_clocks_until_tick == 0) {
            count = fire_step(gi, out_msgs, out_lens, max_out, count);   /* loops */
            gi->midi_clocks_until_tick = clocks_before_step(gi, gi->cur_step);
        }
        return count;
    }
    if (hi == 0xFCu) {                              /* Stop */
        gi->clock_running = 0;
        clear_strum(gi);
        return flush_all(gi, out_msgs, out_lens, max_out, 0);
    }

    /* ── channel voice messages ── */
    status = hi & 0xF0u;
    if (status == MIDI_NOTE_ON && in_len >= 3 && in_msg[2] > 0) {
        /* latch: a fresh press (nothing physically down) replaces the latched chord */
        if (gi->latch && gi->phys_count == 0) gi->held_count = 0;
        phys_add(gi, in_msg[1]);
        held_add(gi, in_msg[1]);                   /* capture — swallow */
        return 0;
    }
    if (status == MIDI_NOTE_OFF || (status == MIDI_NOTE_ON && (in_len < 3 || in_msg[2] == 0))) {
        phys_remove(gi, in_msg[1]);
        if (!gi->latch) held_remove(gi, in_msg[1]); /* latch keeps it in the register */
        return 0;
    }
    /* CC / program change / aftertouch / pitch-bend → pass through for
     * expressive & MPE synths downstream (applies to the sounding retriggers). */
    if (status == 0xA0u || status == 0xB0u || status == 0xC0u ||
        status == 0xD0u || status == 0xE0u) {
        if (max_out < 1) return 0;
        out_msgs[0][0] = in_msg[0];
        out_msgs[0][1] = in_len > 1 ? in_msg[1] : 0;
        out_msgs[0][2] = in_len > 2 ? in_msg[2] : 0;
        out_lens[0] = in_len;
        return 1;
    }
    return 0;
}

/* ── Tick: drain strummed onsets; stop cleanly if the clock vanishes ──────── */

static int gb_tick(void *instance, int frames, int sample_rate,
                   uint8_t out_msgs[][3], int out_lens[], int max_out)
{
    GrooveBankInstance *gi = (GrooveBankInstance *)instance;
    int count = 0;
    if (!gi) return 0;

    if (sample_rate > 0) gi->sample_rate = sample_rate;

    /* Fire strummed chord onsets whose sub-step delay has elapsed. Safe use of
     * tick(): these only exist in the ms right after a hit (audio flowing). */
    for (int i = 0; i < GB_MAX_HELD && count < max_out; i++) {
        StrumPending *sp = &gi->strum_q[i];
        if (!sp->active) continue;
        sp->samples_left -= frames;
        if (sp->samples_left <= 0) {
            count = kill_voice(gi, sp->note, out_msgs, out_lens, max_out, count);
            if (count >= max_out) break;
            count = emit((uint8_t)(MIDI_NOTE_ON | OUT_CHANNEL), sp->note, sp->vel, out_msgs, out_lens, max_out, count);
            voice_add(gi, sp->note, sp->gate);
            sp->active = 0;
        }
    }

    if (g_host && g_host->get_clock_status) {
        int status = g_host->get_clock_status();
        if ((status == MOVE_CLOCK_STATUS_STOPPED ||
             status == MOVE_CLOCK_STATUS_UNAVAILABLE) && gi->clock_running) {
            gi->clock_running = 0;
            clear_strum(gi);
            count = flush_all(gi, out_msgs, out_lens, max_out, count);
        }
    }
    return count;
}

/* ── Parameter I/O ───────────────────────────────────────────────────────── */

static int parse_int(const char *s, int lo, int hi, int dflt)
{
    int v;
    if (!s) return dflt;
    v = atoi(s);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int json_field_int(const char *json, const char *quoted_key, int dflt)
{
    const char *p = json ? strstr(json, quoted_key) : NULL;
    if (!p) return dflt;
    p = strchr(p + strlen(quoted_key), ':');
    return p ? atoi(p + 1) : dflt;
}

static void gb_set_param(void *instance, const char *key, const char *val)
{
    GrooveBankInstance *gi = (GrooveBankInstance *)instance;
    if (!gi || !key || !val) return;

    if (strcmp(key, "pattern") == 0) {
        int hi = g_bank.count > 0 ? g_bank.count - 1 : 0;
        int idx = parse_int(val, 0, hi, 0);
        if (idx != gi->pattern) {
            uint8_t vc;
            gi->pattern = idx;
            if (gi->cur_step >= pattern_steps(gi)) gi->cur_step = 0;
            vc = cur_variant_count(gi);
            if (gi->variant >= vc) gi->variant = (uint8_t)(vc - 1);  /* keep flavor if valid */
            gi->preview_revision++;
        }
        return;
    }
    if (strcmp(key, "variant") == 0) {
        uint8_t vc = cur_variant_count(gi);
        uint8_t v = (uint8_t)parse_int(val, 0, vc - 1, 0);
        if (v != gi->variant) { gi->variant = v; gi->preview_revision++; }
        return;
    }
    if (strcmp(key, "swing") == 0)  { gi->swing  = (uint8_t)parse_int(val, 0, 100, 0);   return; }
    if (strcmp(key, "gate") == 0)   { gi->gate   = (uint8_t)parse_int(val, 5, 200, 80);  return; }
    if (strcmp(key, "strum") == 0)  { gi->strum  = (int8_t) parse_int(val, -100, 100, 0); return; }
    if (strcmp(key, "accent") == 0) { gi->accent = (uint8_t)parse_int(val, 0, 100, 50);  return; }
    if (strcmp(key, "latch") == 0) {
        uint8_t on = (uint8_t)parse_int(val, 0, 1, 0);
        if (!on && gi->latch) {   /* turning OFF releases the latched chord */
            gi->held_count = 0;
            for (int i = 0; i < gi->phys_count; i++) held_add(gi, gi->phys[i]);
        }
        gi->latch = on;
        return;
    }

    if (strcmp(key, "state") == 0) {          /* chain autosave / patch restore */
        int hi = g_bank.count > 0 ? g_bank.count - 1 : 0;
        gi->pattern = clampi(json_field_int(val, "\"pattern\"", gi->pattern), 0, hi);
        if (gi->cur_step >= pattern_steps(gi)) gi->cur_step = 0;
        gi->preview_revision++;
        gi->swing  = (uint8_t)clampi(json_field_int(val, "\"swing\"",  gi->swing),  0, 100);
        gi->gate   = (uint8_t)clampi(json_field_int(val, "\"gate\"",   gi->gate),   5, 200);
        gi->strum  = (int8_t) clampi(json_field_int(val, "\"strum\"",  gi->strum), -100, 100);
        gi->accent = (uint8_t)clampi(json_field_int(val, "\"accent\"", gi->accent), 0, 100);
        gi->latch  = (uint8_t)clampi(json_field_int(val, "\"latch\"",  gi->latch), 0, 1);
        {
            uint8_t vc = cur_variant_count(gi);
            gi->variant = (uint8_t)clampi(json_field_int(val, "\"variant\"", gi->variant), 0, vc - 1);
        }
        return;
    }
}

static int indexed_key(const char *key, const char *prefix)
{
    size_t pl = strlen(prefix);
    if (strncmp(key, prefix, pl) != 0 || key[pl] != '@') return -1;
    return atoi(key + pl + 1);
}

static int gb_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    GrooveBankInstance *gi = (GrooveBankInstance *)instance;
    const GroovePattern *p;
    int gk;
    if (!gi || !key || !buf || buf_len <= 0) return -1;
    p = pattern_at(gi->pattern);

    if (strcmp(key, "pattern") == 0)       return snprintf(buf, buf_len, "%d", gi->pattern);
    if (strcmp(key, "pattern_count") == 0) return snprintf(buf, buf_len, "%d", g_bank.count);
    if (strcmp(key, "pattern_name") == 0)  return snprintf(buf, buf_len, "%s", p ? p->name : "");
    if (strcmp(key, "pattern_label") == 0) return snprintf(buf, buf_len, "%s  %s", p ? p->name : "", p ? p->genre : "");
    if (strcmp(key, "pattern_genre") == 0) return snprintf(buf, buf_len, "%s", p ? p->genre : "");
    if (strcmp(key, "steps") == 0)         return snprintf(buf, buf_len, "%u", pattern_steps(gi));
    if (strcmp(key, "play_step") == 0)     return snprintf(buf, buf_len, "%u", gi->cur_step);
    if (strcmp(key, "playing") == 0)       return snprintf(buf, buf_len, "%u", gi->clock_running);
    if (strcmp(key, "held_count") == 0)    return snprintf(buf, buf_len, "%d", gi->held_count);
    if (strcmp(key, "preview_rev") == 0)   return snprintf(buf, buf_len, "%u", gi->preview_revision);
    if (strcmp(key, "swing") == 0)         return snprintf(buf, buf_len, "%u", gi->swing);
    if (strcmp(key, "gate") == 0)          return snprintf(buf, buf_len, "%u", gi->gate);
    if (strcmp(key, "strum") == 0)         return snprintf(buf, buf_len, "%d", gi->strum);
    if (strcmp(key, "accent") == 0)        return snprintf(buf, buf_len, "%u", gi->accent);
    if (strcmp(key, "latch") == 0)         return snprintf(buf, buf_len, "%u", gi->latch);
    if (strcmp(key, "variant") == 0)       return snprintf(buf, buf_len, "%u", gi->variant);
    if (strcmp(key, "variant_count") == 0) return snprintf(buf, buf_len, "%u", cur_variant_count(gi));
    if (strcmp(key, "row0") == 0)          return snprintf(buf, buf_len, "%s", p ? sel_row(gi, p) : "");
    if (strcmp(key, "state") == 0)
        return snprintf(buf, buf_len,
                        "{\"pattern\":%d,\"variant\":%u,\"swing\":%u,\"gate\":%u,\"strum\":%d,\"accent\":%u,\"latch\":%u}",
                        gi->pattern, gi->variant, gi->swing, gi->gate, gi->strum, gi->accent, gi->latch);

    if (strcmp(key, "genre_list") == 0) {
        int off = 0, i = 0;
        while (i < g_bank.count && off < buf_len - 1) {
            const char *gn = g_bank.patterns[i].genre;
            int c = 0;
            while (i + c < g_bank.count && strcmp(g_bank.patterns[i + c].genre, gn) == 0) c++;
            off += snprintf(buf + off, buf_len - off, "%s%s:%d", i > 0 ? "|" : "", gn, c);
            i += c;
        }
        return off;
    }

    gk = indexed_key(key, "name");
    if (gk >= 0) { const GroovePattern *q = pattern_at(gk); return snprintf(buf, buf_len, "%s", q ? q->name : ""); }
    gk = indexed_key(key, "genre");
    if (gk >= 0) { const GroovePattern *q = pattern_at(gk); return snprintf(buf, buf_len, "%s", q ? q->genre : ""); }

    if (strcmp(key, "sync_warn") == 0) {
        if (g_host && g_host->get_clock_status) {
            int status = g_host->get_clock_status();
            if (status == MOVE_CLOCK_STATUS_UNAVAILABLE) return snprintf(buf, buf_len, "Enable MIDI Clock Out");
            if (status == MOVE_CLOCK_STATUS_STOPPED)     return snprintf(buf, buf_len, "press Play");
        }
        return snprintf(buf, buf_len, "%s", "");
    }
    return -1;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

static midi_fx_api_v1_t g_api = {
    .api_version      = MIDI_FX_API_VERSION,
    .create_instance  = gb_create_instance,
    .destroy_instance = gb_destroy_instance,
    .process_midi     = gb_process_midi,
    .tick             = gb_tick,
    .set_param        = gb_set_param,
    .get_param        = gb_get_param,
};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
