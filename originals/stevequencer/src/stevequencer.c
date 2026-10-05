/* Stevequencer: a 16-step x 4-page (64-step) melodic step sequencer, played from MPC's Q-Links.
 *
 * A Schwung midi_fx_api_v1 module, run by the repo's MIDI FX adapter (mpc/schwung_midi_fx.c): its notes leave
 * through the plugin's own ALSA MIDI port, and another track takes that port as its MIDI input.
 *
 * Timing: every block, the song position (host get_beat_position, in quarter notes) and tempo give the step
 * boundaries that fall inside the block, so steps lock to MPC's bars (start mid-song, loop, locate) with no
 * clock counting. Swing, ratchets and note lengths are scheduled in samples on a small event queue.
 *
 * Parameters (params.json, built by mpc/gen.py): settings, triggers, play_step (the playing step, which the wrapper
 * reports to MPC for the step light) and s<1..64>_<pitch|length|on|velo|chance|ratchet>. Lists of values are option
 * lists, so MPC shows "C3", "150%", "x2": the wrapper sends an option's index and this engine turns it into the value
 * (value = base + mul * index, the tables below). The others (velocity, loop, MIDI channel) are plain numbers.
 * "state" is the whole pattern as one string (VST chunk). The browser prototype (design/prototype.html) runs the
 * same logic in JavaScript (without the MOD lanes).
 *
 * MOD lanes (s<n>_moda / s<n>_modb, appended after the first release): a value per step ("-" = none) that goes out as
 * a MIDI CC on the notes' channel at the step's start, just before its note, and only when it changes. On this repo's
 * instruments CC 20-35 move the first page's Q-Links (the wrapper's CC map), so a lane on CC 20 sweeps their first
 * control with no MIDI learn. A lane plays on every step of the loop, note or not. HOLD keeps the last value on a step
 * without one; RETURN sends the lane's BASE there. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/midi_fx_api_v1.h"

#define NSTEPS 64
#define PAGE 16
#define QMAX 256
#define PITCH_MIN 36   /* C1 (MPC note names: 60 = C3) */
#define PITCH_MAX 96   /* C6 */

static const host_api_v1_t *g_host;

static const int RATE_TICKS[] = {3, 4, 6, 8, 12, 16, 24, 48, 96};   /* 24 PPQN: 1/32 1/16T 1/16 1/8T 1/8 1/4T 1/4 1/2 1 */
#define NRATES 9
enum { DIR_FWD, DIR_REV, DIR_PEND, DIR_RANDOM, DIR_DRUNK, NDIRS };
static const uint16_t SCALES[] = {   /* bit n = n semitones above the root */
    0xFFF,                                                            /* chromatic */
    0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B,                  /* major, minor, dorian, phrygian, lydian, mixolydian, locrian */
    0x9AD, 0xAAD, 0x295, 0x4A9, 0x4E9, 0x555,                         /* harm minor, mel minor, penta maj, penta min, blues, whole tone */
};
#define NSCALES ((int)(sizeof SCALES / sizeof SCALES[0]))

#define NMOD 2   /* MOD A, MOD B */
typedef struct { int pitch, length, on, velo, chance, ratchet, mod[NMOD]; } step_t;   /* mod: 0..127, -1 = none */
static const step_t STEP_INIT = {60, 50, 1, 100, 100, 1, {-1, -1}};
static const step_t STEP_CLEAR = {60, 50, 0, 100, 100, 1, {-1, -1}};
enum { MOD_HOLD, MOD_RETURN };

enum { EV_ON, EV_OFF, EV_LIGHT, EV_CC };   /* EV_CC: note = controller, vel = value */
typedef struct { int64_t t; uint32_t id; uint8_t type, note, vel, ch; } ev_t;

typedef struct {
    step_t s[NSTEPS];
    int rate, swing, dir, gate, loop_start, loop_len, root, scale, transpose, key_tr, channel, step_light, audition;
    int key_offset, play_step;
    int mod_cc[NMOD], mod_mode[NMOD], mod_base[NMOD];   /* controller (0 = off), HOLD/RETURN, RETURN's value */
    int mod_last[NMOD];                                 /* value sent last (-1: none yet, send the next one) */
    step_t clip[PAGE];
    int have_clip;
    /* transport */
    int running;
    double last_beat;   /* song position (quarter notes) where the previous block ended */
    long last_n;        /* absolute step number fired last (DRUNK walks from it) */
    int walk;
    int64_t now;        /* samples since creation */
    uint32_t rng, next_id;
    ev_t q[QMAX];
    int nq;
    uint32_t playing[128];   /* note -> id of the note-on sounding (0: none) */
    uint8_t playing_ch[128];
    int64_t last_audition, quiet_until;
} sq_t;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t rnd(sq_t *q) { q->rng ^= q->rng << 13; q->rng ^= q->rng >> 17; q->rng ^= q->rng << 5; return q->rng; }
static int mod(long a, int m) { long r = a % m; return (int)(r < 0 ? r + m : r); }

/* ---- scale ---------------------------------------------------------------------------------------------------- */
static int in_scale(const sq_t *q, int n) { return (SCALES[q->scale] >> mod(n - q->root, 12)) & 1; }
static int quant(const sq_t *q, int n) {   /* the nearest scale note; a tie goes down */
    for (int d = 0; d < 12; d++) {
        if (in_scale(q, n - d)) return n - d;
        if (in_scale(q, n + d)) return n + d;
    }
    return n;
}
static int quant_range(const sq_t *q, int n) {   /* quantized, kept inside the pitch range and the scale */
    int y = quant(q, n);
    if (y > PITCH_MAX) for (y = PITCH_MAX; !in_scale(q, y); y--) ;
    if (y < PITCH_MIN) for (y = PITCH_MIN; !in_scale(q, y); y++) ;
    return y;
}
/* A pitch set with a scale on is stored on the scale. A nudge of a semitone or two that lands on the note already
 * showing moves on to the next scale note that way, so a Q-Link never has dead ticks. */
static int pitch_value(const sq_t *q, int old, int x) {
    if (SCALES[q->scale] == 0xFFF) return x;
    int shown = quant_range(q, old), d = (x > shown) - (x < shown), y = quant_range(q, x);
    if (d && abs(x - shown) <= 2 && y == shown) {
        int m = shown + d;
        while (m >= PITCH_MIN && m <= PITCH_MAX && !in_scale(q, m)) m += d;
        y = (m >= PITCH_MIN && m <= PITCH_MAX) ? m : shown;
    }
    return y;
}

/* ---- the event queue -------------------------------------------------------------------------------------------- */
static void push(sq_t *q, int64_t t, int type, int note, int vel, int ch, uint32_t id) {
    if (q->nq >= QMAX) return;
    q->q[q->nq++] = (ev_t){t, id, (uint8_t)type, (uint8_t)note, (uint8_t)vel, (uint8_t)ch};
}
static void note(sq_t *q, int64_t t, int64_t dur, int n, int vel) {   /* a note-on and its note-off */
    if (q->nq > QMAX - 2) return;
    uint32_t id = ++q->next_id ? q->next_id : ++q->next_id;
    int ch = q->channel - 1;
    push(q, t, EV_ON, n, vel, ch, id);
    push(q, t + (dur < 64 ? 64 : dur), EV_OFF, n, 0, ch, id);
}
static void drop_pending_ons(sq_t *q) {   /* future note-ons, lights and CCs go; note-offs stay */
    int k = 0;
    for (int i = 0; i < q->nq; i++) if (q->q[i].type == EV_OFF) q->q[k++] = q->q[i];
    q->nq = k;
    for (int l = 0; l < NMOD; l++) q->mod_last[l] = -1;   /* a dropped CC may not have gone out: send the next one */
}
/* A MOD lane's CC, when the value differs from the one sent last. */
static void mod_out(sq_t *q, int64_t t, int l, int value) {
    if (q->mod_cc[l] <= 0 || value < 0 || value == q->mod_last[l]) return;
    q->mod_last[l] = value;
    push(q, t, EV_CC, q->mod_cc[l], value, q->channel - 1, 0);
}

/* ---- playing ------------------------------------------------------------------------------------------------------ */
static int order(sq_t *q, long n) {   /* absolute step n -> its place in the loop, by DIRECTION */
    int L = q->loop_len;
    switch (q->dir) {
    case DIR_REV: return L - 1 - mod(n, L);
    case DIR_PEND: { if (L == 1) return 0; int per = 2 * (L - 1), m = mod(n, per); return m < L ? m : per - m; }
    case DIR_RANDOM: return (int)(rnd(q) % (uint32_t)L);
    case DIR_DRUNK:
        q->walk = (n == q->last_n + 1) ? mod(q->walk + ((rnd(q) & 1) ? 1 : -1), L) : 0;
        return q->walk % L;
    default: return mod(n, L);
    }
}
static int note_out(const sq_t *q, int i) {
    return clampi(quant_range(q, q->s[i].pitch) + q->transpose + (q->key_tr ? q->key_offset : 0), 0, 127);
}
/* Step boundary n (absolute, from the song start) falls at sample t; a step lasts len samples. */
static void fire(sq_t *q, long n, int64_t t, double len) {
    int off = order(q, n), i = (q->loop_start - 1 + off) % NSTEPS;
    q->last_n = n;
    if (n & 1) t += (int64_t)((2.0 * q->swing / 100.0 - 1.0) * len);   /* 66% = triplet feel, 75% = dotted */
    push(q, t, EV_LIGHT, i, 0, 0, 0);
    const step_t *s = &q->s[i];
    for (int l = 0; l < NMOD; l++)   /* before the note's chance: a lane plays on every step of the loop */
        mod_out(q, t, l, s->mod[l] >= 0 ? s->mod[l] : q->mod_mode[l] == MOD_RETURN ? q->mod_base[l] : -1);
    if (!s->on || (s->chance < 100 && (int)(rnd(q) % 100) >= s->chance)) return;
    int nn = note_out(q, i), r = s->ratchet;
    double gate = s->length / 100.0 * q->gate / 100.0, sub = len / r;
    for (int j = 0; j < r; j++) {
        double dur = r > 1 ? fmin(sub * gate, sub * 0.95) : len * gate;   /* over 100%: holds into the next step */
        note(q, t + (int64_t)(j * sub), (int64_t)dur, nn, s->velo);
    }
}

static void all_off(sq_t *q, uint8_t out[][3], int lens[], int max, int *k) {
    for (int n = 0; n < 128 && *k < max; n++)
        if (q->playing[n]) {
            out[*k][0] = (uint8_t)(0x80 | q->playing_ch[n]); out[*k][1] = (uint8_t)n; out[*k][2] = 0; lens[(*k)++] = 3;
            q->playing[n] = 0;
        }
}

/* Events due before `until`, oldest first (at the same time: note-offs, then CCs, then note-ons, so a note starts with
 * its step's MOD values), up to max messages. */
static int prio(int type) { return type == EV_OFF ? 0 : type == EV_CC ? 1 : 2; }
static int emit_due(sq_t *q, int64_t until, uint8_t out[][3], int lens[], int max) {
    int k = 0;
    for (;;) {
        int best = -1;
        for (int i = 0; i < q->nq; i++) {
            const ev_t *e = &q->q[i];
            if (e->t >= until) continue;
            if (best < 0 || e->t < q->q[best].t || (e->t == q->q[best].t && prio(e->type) < prio(q->q[best].type)))
                best = i;
        }
        if (best < 0) break;
        ev_t e = q->q[best];
        if (e.type == EV_ON && k + 2 > max) break;
        if ((e.type == EV_OFF || e.type == EV_CC) && k + 1 > max) break;
        q->q[best] = q->q[--q->nq];
        if (e.type == EV_CC) {
            out[k][0] = (uint8_t)(0xB0 | e.ch); out[k][1] = e.note; out[k][2] = e.vel; lens[k++] = 3;
        } else if (e.type == EV_LIGHT) {
            if (q->step_light) q->play_step = e.note + 1;
        } else if (e.type == EV_ON) {
            if (q->playing[e.note]) {   /* the same note again: end the old one first */
                out[k][0] = (uint8_t)(0x80 | q->playing_ch[e.note]); out[k][1] = e.note; out[k][2] = 0; lens[k++] = 3;
            }
            out[k][0] = (uint8_t)(0x90 | e.ch); out[k][1] = e.note; out[k][2] = e.vel; lens[k++] = 3;
            q->playing[e.note] = e.id;
            q->playing_ch[e.note] = e.ch;
        } else if (q->playing[e.note] == e.id) {   /* an off for a note since retriggered or cut does nothing */
            out[k][0] = (uint8_t)(0x80 | e.ch); out[k][1] = e.note; out[k][2] = 0; lens[k++] = 3;
            q->playing[e.note] = 0;
        }
    }
    return k;
}

static int tick(void *inst, int frames, int sr, uint8_t out[][3], int lens[], int max) {
    sq_t *q = inst;
    int k = 0;
    int running = g_host && g_host->get_clock_status && g_host->get_clock_status() == MOVE_CLOCK_STATUS_RUNNING;
    double end = running && g_host->get_beat_position ? g_host->get_beat_position() : -1;
    if (running && end >= 0) {
        double bpm = g_host->get_bpm ? g_host->get_bpm() : 120.0;
        if (!(bpm >= 20 && bpm <= 400)) bpm = 120.0;
        double spb = sr * 60.0 / bpm, start = end - frames / spb;
        if (!q->running || fabs(start - q->last_beat) > 1e-4) {   /* started, or MPC looped / located */
            if (q->running) drop_pending_ons(q);
            else { q->last_n = -1000000; for (int l = 0; l < NMOD; l++) q->mod_last[l] = -1; }
            q->last_beat = start;
        }
        q->running = 1;
        double sb = RATE_TICKS[q->rate] / 24.0;   /* a step, in quarter notes */
        long n = (long)floor(q->last_beat / sb);
        if (n * sb < q->last_beat) n++;
        for (; n * sb < end; n++) fire(q, n, q->now + (int64_t)((n * sb - start) * spb), sb * spb);
        q->last_beat = end;
    } else if (q->running) {   /* stopped: everything off, the step light out */
        q->running = 0;
        q->nq = 0;
        q->play_step = 0;
        all_off(q, out, lens, max, &k);
    }
    k += emit_due(q, q->now + frames, out + k, lens + k, max - k);
    q->now += frames;
    return k;
}

/* ---- edits ---------------------------------------------------------------------------------------------------------- */
static void random_step(sq_t *q, int i) {   /* new notes and gates, in the scale, C2..C4 from the root */
    step_t *s = &q->s[i];
    s->on = rnd(q) % 100 < 75;
    s->pitch = quant_range(q, 48 + q->root + (int)(rnd(q) % 25));
    s->velo = 80 + (int)(rnd(q) % 48);
}
static void loop_steps(const sq_t *q, int *idx) {
    for (int k = 0; k < q->loop_len; k++) idx[k] = (q->loop_start - 1 + k) % NSTEPS;
}
static int trigger(sq_t *q, const char *key) {
    int p;
    char what[16];
    if (sscanf(key, "p%d_%15s", &p, what) == 2 && p >= 1 && p <= 4) {
        step_t *pg = &q->s[(p - 1) * PAGE];
        if (!strcmp(what, "copy")) { memcpy(q->clip, pg, sizeof q->clip); q->have_clip = 1; }
        else if (!strcmp(what, "paste")) { if (q->have_clip) memcpy(pg, q->clip, sizeof q->clip); }
        else if (!strcmp(what, "clear")) { for (int i = 0; i < PAGE; i++) pg[i] = STEP_CLEAR; }
        else if (!strcmp(what, "random")) { for (int i = 0; i < PAGE; i++) random_step(q, (p - 1) * PAGE + i); }
        else return 0;
        return 1;
    }
    int idx[NSTEPS], L = q->loop_len;
    loop_steps(q, idx);
    if (!strcmp(key, "random_all")) { for (int k = 0; k < L; k++) random_step(q, idx[k]); return 1; }
    if (!strcmp(key, "clear_all")) { for (int i = 0; i < NSTEPS; i++) q->s[i] = STEP_CLEAR; return 1; }
    if (!strcmp(key, "rotate_left") || !strcmp(key, "rotate_right")) {
        step_t tmp[NSTEPS];
        int sh = key[7] == 'l' ? 1 : L - 1;
        for (int k = 0; k < L; k++) tmp[k] = q->s[idx[(k + sh) % L]];
        for (int k = 0; k < L; k++) q->s[idx[k]] = tmp[k];
        return 1;
    }
    return 0;
}

static void audition(sq_t *q, int i) {   /* a step's note when it's edited with the transport stopped */
    if (q->running || !q->audition || q->now < q->quiet_until || q->now - q->last_audition < 44100 / 8) return;
    if (!q->s[i].on) return;
    q->last_audition = q->now;
    note(q, q->now, 44100 / 4, note_out(q, i), q->s[i].velo);
}
static void audition_mod(sq_t *q, int l, int value) {   /* a MOD value edited while stopped: its CC, so the target moves */
    if (q->running || !q->audition || q->now < q->quiet_until) return;
    mod_out(q, q->now, l, value);
}

/* ---- state (the VST chunk) ------------------------------------------------------------------------------------------ */
/* SQ2: the settings, the MOD settings (cc, mode, base per lane), then 15 hex digits per step (pitch, length, on, velo,
 * chance, ratchet, MOD A, MOD B; ff = no MOD value). SQ1 (before the MOD lanes) still loads, without MOD values. */
#define STATE_HEAD "%d %d %d %d %d %d %d %d %d %d %d %d %d "
static int get_state(sq_t *q, char *buf, int len) {
    int n = snprintf(buf, len, "SQ2 " STATE_HEAD "%d %d %d %d %d %d ", q->rate, q->swing, q->dir, q->gate, q->loop_start,
                     q->loop_len, q->root, q->scale, q->transpose, q->key_tr, q->channel, q->step_light, q->audition,
                     q->mod_cc[0], q->mod_mode[0], q->mod_base[0], q->mod_cc[1], q->mod_mode[1], q->mod_base[1]);
    for (int i = 0; i < NSTEPS && n > 0 && n < len; i++) {
        const step_t *s = &q->s[i];
        n += snprintf(buf + n, len - n, "%02x%03x%x%02x%02x%x%02x%02x", s->pitch, s->length, s->on, s->velo, s->chance,
                      s->ratchet, s->mod[0] < 0 ? 0xff : s->mod[0], s->mod[1] < 0 ? 0xff : s->mod[1]);
    }
    return n > 0 && n < len ? n : -1;
}
static void set_state(sq_t *q, const char *v) {
    int g[19], used = 0, v2 = !strncmp(v, "SQ2 ", 4);
    if (v2 ? sscanf(v + 4, STATE_HEAD "%d %d %d %d %d %d %n", &g[0], &g[1], &g[2], &g[3], &g[4], &g[5], &g[6], &g[7],
                    &g[8], &g[9], &g[10], &g[11], &g[12], &g[13], &g[14], &g[15], &g[16], &g[17], &g[18], &used) < 19
           : strncmp(v, "SQ1 ", 4) || sscanf(v + 4, STATE_HEAD "%n", &g[0], &g[1], &g[2], &g[3], &g[4], &g[5], &g[6],
                                             &g[7], &g[8], &g[9], &g[10], &g[11], &g[12], &used) < 13)
        return;
    if (!used) return;
    used += 4;
    q->rate = clampi(g[0], 0, NRATES - 1); q->swing = clampi(g[1], 50, 75); q->dir = clampi(g[2], 0, NDIRS - 1);
    q->gate = clampi(g[3], 10, 200); q->loop_start = clampi(g[4], 1, NSTEPS); q->loop_len = clampi(g[5], 1, NSTEPS);
    q->root = clampi(g[6], 0, 11); q->scale = clampi(g[7], 0, NSCALES - 1); q->transpose = clampi(g[8], -24, 24);
    q->key_tr = !!g[9]; q->channel = clampi(g[10], 1, 16); q->step_light = !!g[11]; q->audition = !!g[12];
    for (int l = 0; l < NMOD; l++) {
        q->mod_cc[l] = v2 ? clampi(g[13 + 3 * l], 0, 119) : 20 + l;
        q->mod_mode[l] = v2 ? clampi(g[14 + 3 * l], 0, 1) : MOD_HOLD;
        q->mod_base[l] = v2 ? clampi(g[15 + 3 * l], 0, 127) : 64;
        q->mod_last[l] = -1;
    }
    const char *p = v + used;
    int per = v2 ? 15 : 11;
    for (int i = 0; i < NSTEPS && (int)strlen(p) >= per; i++, p += per) {
        unsigned a, b, c, d, e, f, ma = 0xff, mb = 0xff;
        if (sscanf(p, "%2x%3x%1x%2x%2x%1x", &a, &b, &c, &d, &e, &f) != 6) break;
        if (v2 && sscanf(p + 11, "%2x%2x", &ma, &mb) != 2) break;
        q->s[i] = (step_t){clampi((int)a, PITCH_MIN, PITCH_MAX), clampi((int)b / 5 * 5, 5, 400), !!c,
                           clampi((int)d, 1, 127), clampi((int)e / 5 * 5, 0, 100), clampi((int)f, 1, 4),
                           {ma > 127 ? -1 : (int)ma, mb > 127 ? -1 : (int)mb}};
    }
    q->key_offset = 0;
    q->quiet_until = q->now + 44100;   /* a project loading: no auditions */
}

/* ---- parameters ------------------------------------------------------------------------------------------------------ */
typedef struct { const char *k; int base, mul, lo, hi; } conv_t;   /* value = base + mul * index, lo..hi in values */
static const conv_t STEP_CONV[] = {
    {"pitch", PITCH_MIN, 1, PITCH_MIN, PITCH_MAX}, {"length", 5, 5, 5, 400}, {"on", 0, 1, 0, 1},
    {"velo", 0, 1, 1, 127}, {"chance", 0, 5, 0, 100}, {"ratchet", 1, 1, 1, 4},
    {"moda", 0, 1, -1, 127}, {"modb", 0, 1, -1, 127},   /* the value itself (params.json "values"), -1 = "-" (none) */
};
static const conv_t *conv_of(const conv_t *t, size_t n, const char *k) {
    for (size_t i = 0; i < n; i++) if (!strcmp(t[i].k, k)) return &t[i];
    return NULL;
}
static int from_index(const conv_t *c, double v) { return clampi(c->base + c->mul * (int)lround(v), c->lo, c->hi); }
static int to_index(const conv_t *c, int value) { return (value - c->base) / c->mul; }

static int step_key(const char *key, int *i, const char **attr) {   /* "s12_pitch" -> 11, "pitch" */
    int n, used = 0;
    if (key[0] != 's' || sscanf(key + 1, "%d_%n", &n, &used) != 1 || !used || n < 1 || n > NSTEPS) return 0;
    *i = n - 1;
    *attr = key + 1 + used;
    return 1;
}
static int *setting(sq_t *q, const char *key, const conv_t **c) {
    static const struct { conv_t c; size_t off; } T[] = {
        {{"rate", 0, 1, 0, NRATES - 1}, offsetof(sq_t, rate)}, {{"swing", 50, 1, 50, 75}, offsetof(sq_t, swing)},
        {{"direction", 0, 1, 0, NDIRS - 1}, offsetof(sq_t, dir)}, {{"gate", 10, 5, 10, 200}, offsetof(sq_t, gate)},
        {{"loop_start", 0, 1, 1, NSTEPS}, offsetof(sq_t, loop_start)}, {{"loop_len", 0, 1, 1, NSTEPS}, offsetof(sq_t, loop_len)},
        {{"root", 0, 1, 0, 11}, offsetof(sq_t, root)}, {{"scale", 0, 1, 0, NSCALES - 1}, offsetof(sq_t, scale)},
        {{"transpose", -24, 1, -24, 24}, offsetof(sq_t, transpose)}, {{"key_transpose", 0, 1, 0, 1}, offsetof(sq_t, key_tr)},
        {{"channel", 0, 1, 1, 16}, offsetof(sq_t, channel)}, {{"step_light", 0, 1, 0, 1}, offsetof(sq_t, step_light)},
        {{"audition", 0, 1, 0, 1}, offsetof(sq_t, audition)},
        {{"moda_cc", 0, 1, 0, 119}, offsetof(sq_t, mod_cc[0])}, {{"modb_cc", 0, 1, 0, 119}, offsetof(sq_t, mod_cc[1])},
        {{"moda_mode", 0, 1, 0, 1}, offsetof(sq_t, mod_mode[0])}, {{"modb_mode", 0, 1, 0, 1}, offsetof(sq_t, mod_mode[1])},
        {{"moda_base", 0, 1, 0, 127}, offsetof(sq_t, mod_base[0])}, {{"modb_base", 0, 1, 0, 127}, offsetof(sq_t, mod_base[1])},
    };
    for (size_t k = 0; k < sizeof T / sizeof T[0]; k++)
        if (!strcmp(key, T[k].c.k)) { *c = &T[k].c; return (int *)((char *)q + T[k].off); }
    return NULL;
}

static void set_param(void *inst, const char *key, const char *val) {
    sq_t *q = inst;
    int i, *p;
    const char *attr;
    const conv_t *c;
    if (!strcmp(key, "state")) { set_state(q, val); return; }
    double v = atof(val);
    if (step_key(key, &i, &attr)) {
        step_t *s = &q->s[i];
        if (!(c = conv_of(STEP_CONV, sizeof STEP_CONV / sizeof STEP_CONV[0], attr))) return;
        int x = from_index(c, v);
        if (!strcmp(attr, "pitch")) { int y = pitch_value(q, s->pitch, x); if (y != s->pitch) { s->pitch = y; audition(q, i); } }
        else if (!strcmp(attr, "length")) s->length = x;
        else if (!strcmp(attr, "on")) { if (x != s->on) { s->on = x; audition(q, i); } }
        else if (!strcmp(attr, "velo")) s->velo = x;
        else if (!strcmp(attr, "chance")) s->chance = x;
        else if (!strcmp(attr, "ratchet")) s->ratchet = x;
        else if (!strncmp(attr, "mod", 3)) { int l = attr[3] == 'b'; if (x != s->mod[l]) { s->mod[l] = x; audition_mod(q, l, x); } }
        return;
    }
    if ((p = setting(q, key, &c))) {
        int old = *p;
        *p = from_index(c, v);
        for (int l = 0; l < NMOD; l++)   /* another controller or mode: the lane's next value goes out whatever it is */
            if ((p == &q->mod_cc[l] || p == &q->mod_mode[l] || p == &q->mod_base[l]) && *p != old) q->mod_last[l] = -1;
        if (p == &q->key_tr && !*p) q->key_offset = 0;
        if (p == &q->step_light && !*p) q->play_step = 0;
        return;
    }
    if (v > 0.5) trigger(q, key);   /* triggers fire on 1; the wrapper's release back to 0 does nothing */
}

static int get_param(void *inst, const char *key, char *buf, int len) {
    sq_t *q = inst;
    int i, *p;
    const char *attr;
    const conv_t *c;
    if (!strcmp(key, "state")) return get_state(q, buf, len);
    if (!strcmp(key, "play_step")) return snprintf(buf, len, "%d", q->play_step);
    if (step_key(key, &i, &attr)) {
        const step_t *s = &q->s[i];
        if (!(c = conv_of(STEP_CONV, sizeof STEP_CONV / sizeof STEP_CONV[0], attr))) return -1;
        int v = !strcmp(attr, "pitch") ? quant_range(q, s->pitch) : !strcmp(attr, "length") ? s->length
              : !strcmp(attr, "on") ? s->on : !strcmp(attr, "velo") ? s->velo : !strcmp(attr, "chance") ? s->chance
              : !strcmp(attr, "ratchet") ? s->ratchet : s->mod[attr[3] == 'b'];
        return snprintf(buf, len, "%d", to_index(c, v));
    }
    if ((p = setting(q, key, &c))) return snprintf(buf, len, "%d", to_index(c, *p));
    return snprintf(buf, len, "0");   /* triggers */
}

/* Notes played on Stevequencer's own track: with KEY TRANSP on, the last one transposes the sequence from C3. */
static int process_midi(void *inst, const uint8_t *m, int len, uint8_t out[][3], int lens[], int max) {
    sq_t *q = inst;
    (void)out; (void)lens; (void)max;
    if (len >= 3 && (m[0] & 0xF0) == 0x90 && m[2] > 0 && q->key_tr) q->key_offset = clampi(m[1] - 60, -24, 24);
    return 0;
}

static void *create_instance(const char *dir, const char *json) {
    (void)dir; (void)json;
    sq_t *q = calloc(1, sizeof *q);
    if (!q) return NULL;
    for (int i = 0; i < NSTEPS; i++) q->s[i] = STEP_INIT;
    q->rate = 2; q->swing = 50; q->dir = DIR_FWD; q->gate = 100; q->loop_start = 1; q->loop_len = PAGE;
    q->channel = 1; q->step_light = 1; q->audition = 1;
    for (int l = 0; l < NMOD; l++) { q->mod_cc[l] = 20 + l; q->mod_mode[l] = MOD_HOLD; q->mod_base[l] = 64; q->mod_last[l] = -1; }
    q->rng = (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)q ^ 0x9E3779B9u;
    if (!q->rng) q->rng = 1;
    q->quiet_until = 44100;   /* MPC sets every parameter as it opens a project: no auditions for a second */
    q->last_audition = -44100;
    return q;
}
static void destroy_instance(void *inst) { free(inst); }

static midi_fx_api_v1_t API = {MIDI_FX_API_VERSION, create_instance, destroy_instance, process_midi, tick,
                               set_param, get_param};

midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host) {
    g_host = host;
    return &API;
}
