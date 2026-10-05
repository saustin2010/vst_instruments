/* Stevequencer 16: a 16-step melodic step sequencer with eight modulation lanes, played from MPC's Q-Links.
 *
 * A Schwung midi_fx_api_v1 module, run by the repo's MIDI FX adapter (mpc/schwung_midi_fx.c): its notes and lane
 * values leave through the plugin's own ALSA MIDI port, and another track takes that port as its MIDI input.
 * The notes work as in Stevequencer (originals/stevequencer, 64 steps): pitch, length, on/off, velocity, chance and
 * ratchets per step, a scale, swing, play directions, transpose and key transpose.
 *
 * Timing: every block, the song position (host get_beat_position, quarter notes) and tempo give the step boundaries
 * that fall inside it, so steps lock to MPC's bars with no clock counting. Lanes are worked out TPS times a step
 * ("ticks"), so a slide or an LFO moves smoothly; a lane sends a value only when it changes.
 *
 * Lanes (l<n>_...): each has a value per step (l<n>_s<step>, "-" = none) and settings:
 *   DEST    OFF, CC (MIDI CC NUMBER, 1-119) or PARAM (parameter NUMBER of the instrument, as NRPN NUMBER - 1: this
 *           repo's plugins take NRPN n as their parameter n, any page; docs/parameter-numbers.md lists them)
 *   MODE    HOLD    a step without a value keeps the last one
 *           RETURN  a step without a value goes back to LOW
 *           SLIDE   values glide into the next value set, across the empty steps between (a ramp over 8 steps is
 *                   two values: the first and the ninth)
 *           LFO     a SHAPE between LOW and HIGH, one cycle every CYCLE lane steps; a step's own value overrides it
 *           FOLLOW  the SOURCE lane's value, mapped onto LOW..HIGH (HIGH below LOW turns it upside down): one lane's
 *                   values move several parameters together (a modulation group)
 *   RATE    the lane's speed against the notes (1/4X .. 4X), LENGTH its own loop (1-16 lane steps)
 * The values go out on the notes' MIDI channel, at the step's start just before its note.
 * "state" is the whole pattern as one string (VST chunk). */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "host/plugin_api_v1.h"
#include "host/midi_fx_api_v1.h"

#define NSTEPS 16
#define NLANES 8
#define TPS 8          /* lane ticks per step */
#define QMAX 512
#define PITCH_MIN 36   /* C1 (MPC note names: 60 = C3) */
#define PITCH_MAX 96   /* C6 */
#define NUM_MAX 512    /* highest PARAM number a lane can reach */

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

enum { DEST_OFF, DEST_CC, DEST_PARAM, NDESTS };
enum { M_HOLD, M_RETURN, M_SLIDE, M_LFO, M_FOLLOW, NMODES };
enum { SH_SINE, SH_TRI, SH_SAWUP, SH_SAWDN, SH_SQUARE, SH_RANDOM, NSHAPES };
static const int LRATE_NUM[] = {1, 1, 1, 2, 4}, LRATE_DEN[] = {4, 2, 1, 1, 1};   /* 1/4X 1/2X 1X 2X 4X */
#define NLRATES 5
static const int CYCLES[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64};   /* LFO cycle, in lane steps */
#define NCYCLES 12

typedef struct { int pitch, length, on, velo, chance, ratchet; } step_t;
static const step_t STEP_INIT = {60, 50, 1, 100, 100, 1};
static const step_t STEP_CLEAR = {60, 50, 0, 100, 100, 1};
typedef struct {
    int dest, num, mode, src, rate, len, low, high, shape, cycle;   /* settings (cycle: an index into CYCLES) */
    int val[NSTEPS];   /* 0..127, -1 = none */
    int cur;           /* the lane's value now (-1: none yet), sent or not (FOLLOW reads it) */
    int last;          /* the value sent last (-1: send the next one whatever it is) */
} lane_t;

enum { EV_ON, EV_OFF, EV_LIGHT, EV_CC };   /* EV_CC: note = controller, vel = value */
typedef struct { int64_t t; uint32_t id, seq; uint8_t type, note, vel, ch; } ev_t;

typedef struct {
    step_t s[NSTEPS];
    lane_t lane[NLANES];
    int rate, swing, dir, gate, loop_start, loop_len, root, scale, transpose, key_tr, channel, step_light, audition;
    int key_offset, play_step;
    int nrpn_sel;       /* the NRPN number selected last on our channel (-1: none), to skip re-selecting it */
    /* transport */
    int running;
    double last_beat;   /* song position (quarter notes) where the previous block ended */
    long last_n;        /* absolute step number fired last (DRUNK walks from it) */
    int walk;
    int64_t now;        /* samples since creation */
    uint32_t rng, next_id, seq;
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
    q->q[q->nq++] = (ev_t){t, id, ++q->seq, (uint8_t)type, (uint8_t)note, (uint8_t)vel, (uint8_t)ch};
}
static void note(sq_t *q, int64_t t, int64_t dur, int n, int vel) {   /* a note-on and its note-off */
    if (q->nq > QMAX - 2) return;
    uint32_t id = ++q->next_id ? q->next_id : ++q->next_id;
    int ch = q->channel - 1;
    push(q, t, EV_ON, n, vel, ch, id);
    push(q, t + (dur < 64 ? 64 : dur), EV_OFF, n, 0, ch, id);
}
static void forget_sent(sq_t *q) {   /* the next value of every lane goes out whatever it is */
    for (int l = 0; l < NLANES; l++) q->lane[l].last = -1;
    q->nrpn_sel = -1;
}
static void drop_pending_ons(sq_t *q) {   /* future note-ons, lights and CCs go; note-offs stay */
    int k = 0;
    for (int i = 0; i < q->nq; i++) if (q->q[i].type == EV_OFF) q->q[k++] = q->q[i];
    q->nq = k;
    forget_sent(q);   /* a dropped CC may not have gone out */
}

/* ---- lanes -------------------------------------------------------------------------------------------------------- */
static uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static double shape(int sh, double ph, long cycle, int lane) {   /* 0..1 at phase ph (0..1) of cycle number `cycle` */
    switch (sh) {
    case SH_TRI: return ph < 0.5 ? 2 * ph : 2 - 2 * ph;
    case SH_SAWUP: return ph;
    case SH_SAWDN: return 1 - ph;
    case SH_SQUARE: return ph < 0.5 ? 1 : 0;
    case SH_RANDOM: return (hash((uint32_t)cycle * 2654435761U + (uint32_t)lane * 40503U) & 0xFFFF) / 65535.0;   /* a new value per cycle */
    default: return 0.5 - 0.5 * cos(2 * M_PI * ph);   /* sine, from its low point */
    }
}
static int scale_to(const lane_t *L, double u) { return clampi((int)lround(L->low + (L->high - L->low) * u), 0, 127); }

/* The value lane l plays at tick m (absolute, from the song start), -1 = nothing new (HOLD on an empty step). */
static int lane_value(const sq_t *q, int l, long m) {
    const lane_t *L = &q->lane[l];
    if (L->mode == M_FOLLOW) {
        int src = q->lane[L->src].cur;
        return L->src == l || src < 0 ? -1 : scale_to(L, src / 127.0);
    }
    double x = (double)m * LRATE_NUM[L->rate] / ((double)LRATE_DEN[L->rate] * TPS);   /* lane steps, from 0 */
    long xi = (long)floor(x);
    double f = x - xi;
    int len = L->len, p = mod(xi, len);
    switch (L->mode) {
    case M_RETURN: return L->val[p] >= 0 ? L->val[p] : L->low;
    case M_SLIDE: {   /* from the last value set at or before p to the next one after it */
        int a = -1, da = 0, b = -1, db = 0;
        for (int k = 0; k < len && a < 0; k++) if (L->val[mod(p - k, len)] >= 0) { a = mod(p - k, len); da = k; }
        if (a < 0) return -1;
        for (int k = 1; k <= len && b < 0; k++) if (L->val[mod(p + k, len)] >= 0) { b = mod(p + k, len); db = k; }
        return clampi((int)lround(L->val[a] + (L->val[b] - L->val[a]) * (da + f) / (da + db)), 0, 127);
    }
    case M_LFO: {
        if (L->val[p] >= 0) return L->val[p];   /* a step's own value locks the LFO there */
        double c = x / CYCLES[L->cycle];
        long ci = (long)floor(c);
        return scale_to(L, shape(L->shape, c - ci, ci, l));
    }
    default: return L->val[p];   /* HOLD */
    }
}
/* A lane's value as MIDI at sample t: a CC, or an NRPN (CC 99, 98: the number, once; CC 6: the value, 7 bits). */
static void lane_send(sq_t *q, int64_t t, int l, int v) {
    lane_t *L = &q->lane[l];
    if (v < 0) return;
    L->cur = v;
    if (L->dest == DEST_OFF || v == L->last) return;
    L->last = v;
    int ch = q->channel - 1;
    if (L->dest == DEST_CC) { push(q, t, EV_CC, clampi(L->num, 1, 119), v, ch, 0); return; }
    int n = clampi(L->num, 1, NUM_MAX) - 1;
    if (n != q->nrpn_sel) {
        push(q, t, EV_CC, 99, n >> 7, ch, 0);
        push(q, t, EV_CC, 98, n & 127, ch, 0);
        q->nrpn_sel = n;
    }
    push(q, t, EV_CC, 6, v, ch, 0);
}
static void lanes_at(sq_t *q, long m, int64_t t) {   /* every lane at tick m; FOLLOW lanes after the ones they follow */
    for (int pass = 0; pass < 2; pass++)
        for (int l = 0; l < NLANES; l++)
            if ((q->lane[l].mode == M_FOLLOW) == pass) lane_send(q, t, l, lane_value(q, l, m));
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
    lanes_at(q, n * TPS, t);   /* the lanes first: a note starts with its step's values */
    const step_t *s = &q->s[i];
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

/* Events due before `until`, oldest first; at the same time note-offs, then CCs (in the order they were made: an NRPN's
 * three CCs stay together), then note-ons, so a note starts with its step's lane values. Up to max messages. */
static int prio(int type) { return type == EV_OFF ? 0 : type == EV_CC ? 1 : 2; }
static int before(const ev_t *a, const ev_t *b) {
    if (a->t != b->t) return a->t < b->t;
    if (prio(a->type) != prio(b->type)) return prio(a->type) < prio(b->type);
    return a->seq < b->seq;
}
static int emit_due(sq_t *q, int64_t until, uint8_t out[][3], int lens[], int max) {
    int k = 0;
    for (;;) {
        int best = -1;
        for (int i = 0; i < q->nq; i++)
            if (q->q[i].t < until && (best < 0 || before(&q->q[i], &q->q[best]))) best = i;
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
            else { q->last_n = -1000000; forget_sent(q); }
            q->last_beat = start;
        }
        q->running = 1;
        double sb = RATE_TICKS[q->rate] / 24.0, tb = sb / TPS;   /* a step and a lane tick, in quarter notes */
        long m = (long)floor(q->last_beat / tb);
        if (m * tb < q->last_beat) m++;
        for (; m * tb < end; m++) {
            long n = m / TPS;
            int j = (int)(m % TPS);
            double len = sb * spb;
            int64_t t = q->now + (int64_t)((m * tb - start) * spb);
            if (j == 0) { fire(q, n, t, len); continue; }   /* a step: its notes, and the lanes on its start */
            if (n & 1) {   /* inside a swung step: its ticks squeezed between its late start and the next step */
                double off = (2.0 * q->swing / 100.0 - 1.0) * len;
                t = q->now + (int64_t)((n * TPS * tb - start) * spb + off + j * (len - off) / TPS);
            }
            lanes_at(q, m, t);   /* between steps: slides and LFOs move on */
        }
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
static void audition_lane(sq_t *q, int l, int v) {   /* a lane value edited while stopped: sent, so the target moves */
    if (q->running || !q->audition || q->now < q->quiet_until) return;
    lane_send(q, q->now, l, v);
}

/* ---- state (the VST chunk) ------------------------------------------------------------------------------------------ */
/* S16 1: the 13 settings, 10 per lane, then 11 hex digits per step (pitch, length, on, velo, chance, ratchet) and 2 per
 * lane value (ff = none). */
#define NSET 13
static int get_state(sq_t *q, char *buf, int len) {
    int set[NSET] = {q->rate, q->swing, q->dir, q->gate, q->loop_start, q->loop_len, q->root, q->scale, q->transpose,
                     q->key_tr, q->channel, q->step_light, q->audition};
    int n = snprintf(buf, len, "S16 1");
    for (int k = 0; k < NSET && n > 0 && n < len; k++) n += snprintf(buf + n, len - n, " %d", set[k]);
    for (int l = 0; l < NLANES && n > 0 && n < len; l++) {
        const lane_t *L = &q->lane[l];
        n += snprintf(buf + n, len - n, " %d %d %d %d %d %d %d %d %d %d", L->dest, L->num, L->mode, L->src, L->rate,
                      L->len, L->low, L->high, L->shape, L->cycle);
    }
    if (n > 0 && n < len) n += snprintf(buf + n, len - n, " ");
    for (int i = 0; i < NSTEPS && n > 0 && n < len; i++) {
        const step_t *s = &q->s[i];
        n += snprintf(buf + n, len - n, "%02x%03x%x%02x%02x%x", s->pitch, s->length, s->on, s->velo, s->chance, s->ratchet);
    }
    for (int l = 0; l < NLANES && n > 0 && n < len; l++)
        for (int i = 0; i < NSTEPS && n > 0 && n < len; i++)
            n += snprintf(buf + n, len - n, "%02x", q->lane[l].val[i] < 0 ? 0xff : q->lane[l].val[i]);
    return n > 0 && n < len ? n : -1;
}
static void set_state(sq_t *q, const char *v) {
    if (strncmp(v, "S16 1 ", 6)) return;
    const char *p = v + 6;
    int g[NSET + NLANES * 10], used;
    for (int k = 0; k < NSET + NLANES * 10; k++) {
        if (sscanf(p, "%d%n", &g[k], &used) != 1) return;
        p += used;
    }
    while (*p == ' ') p++;
    q->rate = clampi(g[0], 0, NRATES - 1); q->swing = clampi(g[1], 50, 75); q->dir = clampi(g[2], 0, NDIRS - 1);
    q->gate = clampi(g[3], 10, 200); q->loop_start = clampi(g[4], 1, NSTEPS); q->loop_len = clampi(g[5], 1, NSTEPS);
    q->root = clampi(g[6], 0, 11); q->scale = clampi(g[7], 0, NSCALES - 1); q->transpose = clampi(g[8], -24, 24);
    q->key_tr = !!g[9]; q->channel = clampi(g[10], 1, 16); q->step_light = !!g[11]; q->audition = !!g[12];
    for (int l = 0; l < NLANES; l++) {
        lane_t *L = &q->lane[l];
        const int *h = g + NSET + 10 * l;
        L->dest = clampi(h[0], 0, NDESTS - 1); L->num = clampi(h[1], 1, NUM_MAX); L->mode = clampi(h[2], 0, NMODES - 1);
        L->src = clampi(h[3], 0, NLANES - 1); L->rate = clampi(h[4], 0, NLRATES - 1); L->len = clampi(h[5], 1, NSTEPS);
        L->low = clampi(h[6], 0, 127); L->high = clampi(h[7], 0, 127); L->shape = clampi(h[8], 0, NSHAPES - 1);
        L->cycle = clampi(h[9], 0, NCYCLES - 1); L->cur = -1;
    }
    for (int i = 0; i < NSTEPS && strlen(p) >= 11; i++, p += 11) {
        unsigned a, b, c, d, e, f;
        if (sscanf(p, "%2x%3x%1x%2x%2x%1x", &a, &b, &c, &d, &e, &f) != 6) return;
        q->s[i] = (step_t){clampi((int)a, PITCH_MIN, PITCH_MAX), clampi((int)b / 5 * 5, 5, 400), !!c,
                           clampi((int)d, 1, 127), clampi((int)e / 5 * 5, 0, 100), clampi((int)f, 1, 4)};
    }
    for (int l = 0; l < NLANES; l++)
        for (int i = 0; i < NSTEPS && strlen(p) >= 2; i++, p += 2) {
            unsigned x;
            if (sscanf(p, "%2x", &x) != 1) return;
            q->lane[l].val[i] = x > 127 ? -1 : (int)x;
        }
    forget_sent(q);
    q->key_offset = 0;
    q->quiet_until = q->now + 44100;   /* a project loading: no auditions */
}

/* ---- parameters ------------------------------------------------------------------------------------------------------ */
typedef struct { const char *k; int base, mul, lo, hi; } conv_t;   /* value = base + mul * index, lo..hi in values */
static const conv_t STEP_CONV[] = {
    {"pitch", PITCH_MIN, 1, PITCH_MIN, PITCH_MAX}, {"length", 5, 5, 5, 400}, {"on", 0, 1, 0, 1},
    {"velo", 0, 1, 1, 127}, {"chance", 0, 5, 0, 100}, {"ratchet", 1, 1, 1, 4},
};
static const conv_t LANE_CONV[] = {   /* the lane settings; "cycle" arrives as its length in steps (params.json "values") */
    {"dest", 0, 1, 0, NDESTS - 1}, {"num", 0, 1, 1, NUM_MAX}, {"mode", 0, 1, 0, NMODES - 1}, {"src", 0, 1, 0, NLANES - 1},
    {"rate", 0, 1, 0, NLRATES - 1}, {"len", 0, 1, 1, NSTEPS}, {"low", 0, 1, 0, 127}, {"high", 0, 1, 0, 127},
    {"shape", 0, 1, 0, NSHAPES - 1}, {"cycle", 0, 1, 1, 64},
};
#define NCONV(t) (sizeof t / sizeof t[0])
static const conv_t *conv_of(const conv_t *t, size_t n, const char *k) {
    for (size_t i = 0; i < n; i++) if (!strcmp(t[i].k, k)) return &t[i];
    return NULL;
}
static int from_index(const conv_t *c, double v) { return clampi(c->base + c->mul * (int)lround(v), c->lo, c->hi); }
static int to_index(const conv_t *c, int value) { return (value - c->base) / c->mul; }
static int cycle_index(int steps) {   /* the nearest cycle length offered */
    int best = 0;
    for (int k = 1; k < NCYCLES; k++) if (abs(CYCLES[k] - steps) < abs(CYCLES[best] - steps)) best = k;
    return best;
}

static int step_key(const char *key, int *i, const char **attr) {   /* "s12_pitch" -> 11, "pitch" */
    int n, used = 0;
    if (key[0] != 's' || sscanf(key + 1, "%d_%n", &n, &used) != 1 || !used || n < 1 || n > NSTEPS) return 0;
    *i = n - 1;
    *attr = key + 1 + used;
    return 1;
}
static int lane_key(const char *key, int *l, int *i, const char **attr) {   /* "l3_s5" -> 2, 4; "l3_mode" -> 2, -1, "mode" */
    int n, s, used = 0;
    if (key[0] != 'l' || sscanf(key + 1, "%d_%n", &n, &used) != 1 || !used || n < 1 || n > NLANES) return 0;
    *l = n - 1;
    *attr = key + 1 + used;
    *i = -1;
    if (sscanf(*attr, "s%d", &s) == 1 && s >= 1 && s <= NSTEPS) *i = s - 1;
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
    };
    for (size_t k = 0; k < sizeof T / sizeof T[0]; k++)
        if (!strcmp(key, T[k].c.k)) { *c = &T[k].c; return (int *)((char *)q + T[k].off); }
    return NULL;
}
static int *lane_field(lane_t *L, const char *attr) {
    static const char *const F[] = {"dest", "num", "mode", "src", "rate", "len", "low", "high", "shape", "cycle"};
    int *f[] = {&L->dest, &L->num, &L->mode, &L->src, &L->rate, &L->len, &L->low, &L->high, &L->shape, &L->cycle};
    for (int k = 0; k < 10; k++) if (!strcmp(attr, F[k])) return f[k];
    return NULL;
}

static void set_param(void *inst, const char *key, const char *val) {
    sq_t *q = inst;
    int i, l, *p;
    const char *attr;
    const conv_t *c;
    if (!strcmp(key, "state")) { set_state(q, val); return; }
    double v = atof(val);
    if (step_key(key, &i, &attr)) {
        step_t *s = &q->s[i];
        if (!(c = conv_of(STEP_CONV, NCONV(STEP_CONV), attr))) return;
        int x = from_index(c, v);
        if (!strcmp(attr, "pitch")) { int y = pitch_value(q, s->pitch, x); if (y != s->pitch) { s->pitch = y; audition(q, i); } }
        else if (!strcmp(attr, "length")) s->length = x;
        else if (!strcmp(attr, "on")) { if (x != s->on) { s->on = x; audition(q, i); } }
        else if (!strcmp(attr, "velo")) s->velo = x;
        else if (!strcmp(attr, "chance")) s->chance = x;
        else if (!strcmp(attr, "ratchet")) s->ratchet = x;
        return;
    }
    if (lane_key(key, &l, &i, &attr)) {
        lane_t *L = &q->lane[l];
        if (i >= 0) {   /* a step's value: the value itself, -1 = none */
            int x = clampi((int)lround(v), -1, 127);
            if (x != L->val[i]) { L->val[i] = x; if (x >= 0 && L->mode != M_FOLLOW) audition_lane(q, l, x); }
            return;
        }
        if (!(c = conv_of(LANE_CONV, NCONV(LANE_CONV), attr)) || !(p = lane_field(L, attr))) return;
        int old = *p;
        *p = !strcmp(attr, "cycle") ? cycle_index((int)lround(v)) : from_index(c, v);
        if (*p != old) { L->last = -1; if (p == &L->dest || p == &L->num) q->nrpn_sel = -1; }   /* send the next value whatever it is */
        return;
    }
    if ((p = setting(q, key, &c))) {
        *p = from_index(c, v);
        if (p == &q->channel) q->nrpn_sel = -1;
        if (p == &q->key_tr && !*p) q->key_offset = 0;
        if (p == &q->step_light && !*p) q->play_step = 0;
        return;
    }
    if (v > 0.5) trigger(q, key);   /* triggers fire on 1; the wrapper's release back to 0 does nothing */
}

static const char *const MODE_NAMES[] = {"HOLD", "RETURN", "SLIDE", "LFO", "FOLLOW"};
static int get_param(void *inst, const char *key, char *buf, int len) {
    sq_t *q = inst;
    int i, l, *p;
    const char *attr;
    const conv_t *c;
    if (!strcmp(key, "state")) return get_state(q, buf, len);
    if (!strcmp(key, "play_step")) return snprintf(buf, len, "%d", q->play_step);
    if (step_key(key, &i, &attr)) {
        const step_t *s = &q->s[i];
        if (!(c = conv_of(STEP_CONV, NCONV(STEP_CONV), attr))) return -1;
        int v = !strcmp(attr, "pitch") ? quant_range(q, s->pitch) : !strcmp(attr, "length") ? s->length
              : !strcmp(attr, "on") ? s->on : !strcmp(attr, "velo") ? s->velo : !strcmp(attr, "chance") ? s->chance
              : s->ratchet;
        return snprintf(buf, len, "%d", to_index(c, v));
    }
    if (lane_key(key, &l, &i, &attr)) {
        lane_t *L = &q->lane[l];
        if (i >= 0) return snprintf(buf, len, "%d", L->val[i]);
        if (!strcmp(attr, "num_display"))   /* dynamic_display: what the number means for this DEST */
            return L->dest == DEST_CC ? snprintf(buf, len, "CC %d", clampi(L->num, 1, 119))
                 : L->dest == DEST_PARAM ? snprintf(buf, len, "P%d", L->num) : snprintf(buf, len, "-");
        if (!strcmp(attr, "summary"))   /* the MOD page's lane line: "CC 20 · SLIDE" */
            return L->dest == DEST_OFF ? snprintf(buf, len, "OFF · %s", MODE_NAMES[L->mode])
                 : snprintf(buf, len, "%s%d · %s", L->dest == DEST_CC ? "CC " : "P", L->dest == DEST_CC ? clampi(L->num, 1, 119) : L->num,
                            MODE_NAMES[L->mode]);
        if (!(c = conv_of(LANE_CONV, NCONV(LANE_CONV), attr)) || !(p = lane_field(L, attr))) return -1;
        return snprintf(buf, len, "%d", !strcmp(attr, "cycle") ? CYCLES[*p] : to_index(c, *p));
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
    q->rate = 2; q->swing = 50; q->dir = DIR_FWD; q->gate = 100; q->loop_start = 1; q->loop_len = NSTEPS;
    q->channel = 1; q->step_light = 1; q->audition = 1;
    for (int l = 0; l < NLANES; l++) {
        lane_t *L = &q->lane[l];
        *L = (lane_t){l < 2 ? DEST_CC : DEST_OFF, l < 2 ? 20 + l : 1, M_HOLD, 0, 2, NSTEPS, 0, 127, SH_SINE, 7, {0}, -1, -1};
        for (int i = 0; i < NSTEPS; i++) L->val[i] = -1;
    }
    q->nrpn_sel = -1;
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
