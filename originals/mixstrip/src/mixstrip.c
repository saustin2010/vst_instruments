/* [UTIL] Mix Strip: a mixer strip for one MPC track, played from a MIDI controller (2026-10-25).
 *
 * Goes last in the track's inserts (a drum program's or a pad's will do). In order: FILTER (low-pass to the left,
 * high-pass to the right, off in the middle), then the CUE tap (pre-fader listen, to Mix Bus's CUE bus), then LEVEL
 * (off to +6 dB, 0 dB three-quarters up) and MUTE (a 5 ms fade), then SEND A and SEND B (post-fader taps to the FX
 * buses). The track's own output is never delayed; the taps reach Mix Bus a block or two later (mixshm.h).
 * KNOB 3 picks what the controller's third knob does on this strip: FILTER, or SEND KILL (left cuts both sends with a
 * 5 ms fade, so the effects' tails ring on; right brings them back; the filter is then off).
 *
 * TRACK (1-16) says which controller column it answers to: TRACK 1-8 are columns 1-8 of page 1, on MIDI CH, and TRACK
 * 9-16 the same columns of page 2, on MIDI CH + 1. A new strip takes the lowest TRACK no other strip holds, and the
 * MIDI CH and KNOBS last set on a strip. The controller's moves change the parameters themselves (mpc_engine_changed),
 * so MPC's screen and automation follow them. The chart is in mixshm.h (MIX_CC_*).
 *
 * With a Mix Desk in the project (2026-11-07) the desk says which column plays which TRACK (TRACK is then the MPC track
 * the strip is on) and the MIDI CH. In its MPC MIXER mode the desk sends each column's fader and MUTE to MPC's own
 * track fader and Track Mute, so the strip leaves LEVEL and MUTE alone (0 dB, never muted) and its sends follow the
 * fader the desk last sent for this TRACK (MPC's fader comes after the strip, so it can't hear it). The desk can also
 * make BUTTON 2 a SEND KILL (on = sends cut) instead of CUE. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mixshm.h"
#include "mixin.h"

/* the wrapper's engine interface (mpc-vst-plugins wrapper/engine.h) */
typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
    void (*process)(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames);   /* the release tools' effect call */
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);
void mpc_engine_input(void *inst, const int16_t *in_lr, int frames);
int mpc_engine_changed(void *inst, char *key, int len);

#define MAXF 512
#define SR 44100.0f
#define MUTE_STEP (1.0f / (0.005f * SR))   /* MUTE fades over 5 ms */
#define DETENT 8     /* FILTER on a relative knob rests at OFF for this many counts, and a turn back through stops there */
#define ABS_DEAD 4   /* FILTER on an absolute knob: OFF from 64 - 4 to 64 + 4 */
#define GLIDE 0.11f   /* the filter's cutoffs glide (in octaves) with a 25 ms time constant: no thump on a fast turn */

/* the parameters, in VST index order (params.json; append only) */
enum { P_STATUS, P_TRACK, P_LEVEL, P_MUTE, P_CUE, P_FILTER, P_SEND_A, P_SEND_B, P_MIDI_CH, P_KNOBS, P_KNOB3, P_SEND_KILL,
       P_N };
/* then six readouts, each control's CC for this TRACK: "cc_send_a" .. "cc_cue" */
static const char *const KEYS[P_N] = {"status", "track", "level", "mute", "cue", "filter", "send_a", "send_b",
                                      "midi_ch", "knobs", "knob3", "send_kill"};
/* the controller's six CCs per column, and the parameter each moves */
static const int CTL_CC[6] = {MIX_CC_SEND_A, MIX_CC_SEND_B, MIX_CC_FILTER, MIX_CC_LEVEL, MIX_CC_MUTE, MIX_CC_CUE};
static const int CTL_P[6] = {P_SEND_A, P_SEND_B, P_FILTER, P_LEVEL, P_MUTE, P_CUE};

typedef struct { float ic1, ic2; } svf_t;

typedef struct {
    mixshm_t *m;
    uint32_t id;
    int track, mute, cue, midi_ch, knobs;   /* knobs: 0 RELATIVE, 1 ABSOLUTE */
    int knob3, kill;                      /* knob3: what the third knob is, 0 FILTER, 1 SEND KILL; kill: sends cut */
    float level, filter, send_a, send_b;
    int slot;                             /* the TRACK slot it holds (0-15), -1 none */
    int col;                              /* the column it follows (0-15: page 2 is 8-15), -1 none (the desk gave it none) */
    int ctl_ch, ctl_col;                  /* the controller channel and the column on its page last read */
    mixdesk_t D;                          /* the Mix Desk's settings (D.on = 0: no desk) */
    uint32_t desk_gen;
    uint32_t seq[6];
    int32_t rel[6];
    uint32_t changed;                     /* parameters the controller (or SOLO) changed, to report: bit per P_* */
    uint32_t solo_seen;
    int32_t detent;                       /* FILTER's relative counts while it rests at OFF */
    mixlisten_t L;
    mixin_t in;
    int listening;
    uint32_t cue_beat, cue_stale;         /* is a CUE bus playing? */
    float g_level, g_mute, g_cue, g_kill, g_a, g_b, g_fad, wet, lp_g, hp_g;   /* g_fad: MPC MIXER, the sends follow the fader */
    int quiet[MIX_TAPS];                  /* frames of silence written to each tap since it went quiet (mix_write) */
    float lp_oct, hp_oct;                 /* the cutoffs as they glide, log2 Hz */
    svf_t lp[2], hp[2];
    int16_t in_buf[2 * MAXF];
    int in_frames;
} strip_t;

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* FILTER -100..100: the low-pass's cutoff (20 kHz down to 60 Hz) and the high-pass's (10 Hz up to 8 kHz) */
static float lp_hz(float f) { return f < 0 ? 20000.0f * powf(60.0f / 20000.0f, -f / 100.0f) : 20000.0f; }
static float hp_hz(float f) { return f > 0 ? 10.0f * powf(8000.0f / 10.0f, f / 100.0f) : 10.0f; }
static float svf_g(float hz) { return tanf(3.14159265f * hz / SR); }
static float glide(float *oct, float hz) {   /* one block's glide towards hz; the coefficient to use */
    float to = log2f(hz), d = to - *oct;
    *oct = fabsf(d) < 0.001f ? to : *oct + d * GLIDE;
    return svf_g(exp2f(*oct));
}

#define SVF_K 1.1f   /* 1/Q: Q 0.9, a slight lift at the cutoff */
typedef struct { float a1, a2, a3; } svfc_t;
static svfc_t svf_coef(float g) {
    float a1 = 1.0f / (1.0f + g * (g + SVF_K));
    return (svfc_t){a1, g * a1, g * g * a1};
}
static inline float svf_run(svf_t *s, float x, const svfc_t *c, int high) {   /* Zavalishin's state-variable filter */
    float v3 = x - s->ic2, v1 = c->a1 * s->ic1 + c->a2 * v3, v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    return high ? x - SVF_K * v1 - v2 : v2;
}

static void svf_flush(svf_t *s) {   /* no denormals after silence */
    if (fabsf(s->ic1) < 1e-15f) s->ic1 = 0;
    if (fabsf(s->ic2) < 1e-15f) s->ic2 = 0;
}

/* ---- TRACK slots, the controller column and shared settings ---- */

static int col_of(const strip_t *S) {   /* the column this TRACK has: its own, or the desk's first one for it */
    if (!S->D.on) return S->track - 1;
    for (int c = 0; c < MIX_COLS; c++)
        if (S->D.alloc[c] == S->track) return c;
    return -1;
}

static int page_ch(const strip_t *S) { return (S->D.on ? S->D.ch : S->midi_ch) - 1 + S->col / 8; }   /* 0-based */

static void ctl_sync(strip_t *S) {   /* follow the column on its channel from here on (what came before stays) */
    S->col = col_of(S);
    if (!S->m || S->col < 0) return;
    int col = S->col % 8, ch = page_ch(S);
    for (int k = 0; k < 6; k++) {
        S->seq[k] = MIX_LOAD(S->m->cc_seq[ch][CTL_CC[k] + col]);
        S->rel[k] = MIX_LOAD(S->m->cc_rel[ch][CTL_CC[k] + col]);
    }
    S->ctl_ch = ch;
    S->ctl_col = col;
}

static void hold_slot(strip_t *S) {   /* hold TRACK's slot, letting go of another */
    if (!S->m) return;
    int want = S->track - 1;
    if (S->slot >= 0 && S->slot != want) {
        mix_release(S->m, S->slot, S->id);
        S->slot = -1;
    }
    if (S->slot < 0 && mix_claim(S->m, want, S->id)) {
        S->slot = want;
        memset(S->quiet, 0, sizeof S->quiet);   /* its rings hold someone else's audio: write them over */
    }
}

static void share_prefs(strip_t *S) {   /* a new strip starts with these */
    if (!S->m) return;
    MIX_STORE(S->m->midi_ch, (uint32_t)S->midi_ch);
    MIX_STORE(S->m->knobs, (uint32_t)S->knobs);
    MIX_STORE(S->m->prefs, 1u);
}

static void cue_on(strip_t *S) {   /* SOLO on the CUE bus: this one's cue turns the others off */
    if (S->m && MIX_LOAD(S->m->cue_mode) == 1) {
        MIX_STORE(S->m->solo_track, (uint32_t)S->track);
        S->solo_seen = __atomic_add_fetch(&S->m->solo_seq, 1, __ATOMIC_ACQ_REL);
    }
}

/* ---- the engine ---- */

static void *e_create(const char *dir) {
    (void)dir;
    strip_t *S = calloc(1, sizeof *S);
    if (!S) return NULL;
    S->track = 1;
    S->level = 75.0f;   /* 0 dB */
    S->midi_ch = 1;
    S->slot = -1;
    S->g_mute = S->g_kill = 1.0f;
    S->lp_oct = log2f(lp_hz(0));
    S->hp_oct = log2f(hp_hz(0));
    S->lp_g = svf_g(lp_hz(0));
    S->hp_g = svf_g(hp_hz(0));
    S->m = mix_open();
    if (S->m) {
        S->id = mix_new_id(S->m);
        for (int t = 0; t < MIX_TRACKS; t++)   /* the lowest TRACK no other strip holds */
            if (!MIX_LOAD(S->m->track[t].owner)) {
                S->track = t + 1;
                break;
            }
        if (MIX_LOAD(S->m->prefs)) {
            S->midi_ch = clampi((int)MIX_LOAD(S->m->midi_ch), 1, 15);
            S->knobs = clampi((int)MIX_LOAD(S->m->knobs), 0, 1);
        }
        S->solo_seen = MIX_LOAD(S->m->solo_seq);
        S->desk_gen = MIX_LOAD(S->m->desk_gen) - 1u;   /* read the desk's settings now */
        mix_desk_read(S->m, &S->desk_gen, &S->D);
        hold_slot(S);
    }
    ctl_sync(S);
    S->g_fad = 1.0f;
    S->g_level = mix_gain(mix_level_db(S->level));
    return S;
}

static void e_destroy(void *inst) {
    strip_t *S = inst;
    if (!S) return;
    mixin_close(&S->in);
    if (S->m) {
        mix_unlisten(S->m, S->id);
        if (S->slot >= 0) mix_release(S->m, S->slot, S->id);
        mix_close(S->m);
    }
    free(S);
}

/* MPC sends an insert effect no MIDI; offline (no ALSA) a test drives the controller table through here */
static void e_midi(void *inst, const uint8_t *msg, int len) {
    strip_t *S = inst;
    if (S->m && len >= 3 && (msg[0] & 0xF0) == 0xB0 && !mixin_available()) mix_cc_in(S->m, msg[0] & 15, msg[1], msg[2]);
}

static void set_one(strip_t *S, const char *key, const char *val) {
    float v = (float)atof(val);
    if (!strcmp(key, "track")) {
        S->track = clampi(atoi(val), 1, MIX_TRACKS);
        hold_slot(S);
        ctl_sync(S);
    } else if (!strcmp(key, "level")) S->level = clampf(v, 0, 100);
    else if (!strcmp(key, "mute")) S->mute = clampi(atoi(val), 0, 1);
    else if (!strcmp(key, "cue")) {
        int was = S->cue;
        S->cue = clampi(atoi(val), 0, 1);
        if (S->cue && !was) cue_on(S);
    } else if (!strcmp(key, "filter")) {
        S->filter = clampf(v, -100, 100);
        S->detent = 0;
    }
    else if (!strcmp(key, "send_a")) S->send_a = clampf(v, 0, 100);
    else if (!strcmp(key, "send_b")) S->send_b = clampf(v, 0, 100);
    else if (!strcmp(key, "midi_ch")) {
        S->midi_ch = clampi(atoi(val), 1, 15);
        share_prefs(S);
        ctl_sync(S);
    } else if (!strcmp(key, "knobs")) {
        S->knobs = clampi(atoi(val), 0, 1);
        share_prefs(S);
    } else if (!strcmp(key, "knob3")) S->knob3 = clampi(atoi(val), 0, 1);
    else if (!strcmp(key, "send_kill")) S->kill = clampi(atoi(val), 0, 1);
}

static void e_set_param(void *inst, const char *key, const char *val) {
    strip_t *S = inst;
    if (strcmp(key, "state")) {
        set_one(S, key, val);
        return;
    }
    char kv[64];   /* "track=3;level=75.00;..." */
    for (const char *s = val; *s;) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                set_one(S, kv, eq);
            }
        }
        s += n;
        if (*s) s++;
    }
}

static int status(const strip_t *S, char *buf, int len) {   /* MPC shows it whole up to PARAM_TEXT_MAX - 1 */
    if (!S->m) return snprintf(buf, len, "NO SHARED MEMORY: A PLAIN STRIP");
    if (S->slot < 0) return snprintf(buf, len, "TRACK %d IS ON ANOTHER STRIP", S->track);
    if (S->cue && S->cue_stale > MIX_STALE) return snprintf(buf, len, "CUE IS ON: ADD A MIX BUS ON CUE");
    if (S->col < 0) return snprintf(buf, len, "TRACK %d HAS NO COLUMN ON MIX DESK", S->track);
    int n = (int)MIX_LOAD(S->m->controllers), ch = page_ch(S) + 1;
    if (S->D.on) {   /* the desk numbers its columns 1-16 */
        if (!n) return snprintf(buf, len, "DESK COLUMN %d, CH %d: NO CONTROLLER", S->col + 1, ch);
        if (S->D.mpc) return snprintf(buf, len, "DESK COLUMN %d, LEVEL + MUTE ON MPC", S->col + 1);
        return snprintf(buf, len, "DESK COLUMN %d ON CH %d", S->col + 1, ch);
    }
    if (!n) return snprintf(buf, len, "COLUMN %d, CH %d: NO CONTROLLER", S->col % 8 + 1, ch);
    return snprintf(buf, len, "COLUMN %d ON CH %d", S->col % 8 + 1, ch);
}

static int filter_text(float f, char *buf, int len) {
    if (fabsf(f) < 1.0f) return snprintf(buf, len, "OFF");
    float hz = f < 0 ? lp_hz(f) : hp_hz(f);
    if (hz >= 1000.0f) return snprintf(buf, len, "%s %.1f kHz", f < 0 ? "LP" : "HP", hz / 1000.0f);
    return snprintf(buf, len, "%s %d Hz", f < 0 ? "LP" : "HP", (int)(hz + 0.5f));
}

static int e_get_param(void *inst, const char *key, char *buf, int len) {
    strip_t *S = inst;
    if (!strcmp(key, "status")) return status(S, buf, len);
    if (!strcmp(key, "track")) return snprintf(buf, len, "%d", S->track);
    if (!strcmp(key, "level")) return snprintf(buf, len, "%.2f", S->level);
    if (!strcmp(key, "mute")) return snprintf(buf, len, "%d", S->mute);
    if (!strcmp(key, "cue")) return snprintf(buf, len, "%d", S->cue);
    if (!strcmp(key, "filter")) return snprintf(buf, len, "%.2f", S->filter);
    if (!strcmp(key, "send_a")) return snprintf(buf, len, "%.2f", S->send_a);
    if (!strcmp(key, "send_b")) return snprintf(buf, len, "%.2f", S->send_b);
    if (!strcmp(key, "midi_ch")) return snprintf(buf, len, "%d", S->midi_ch);
    if (!strcmp(key, "knobs")) return snprintf(buf, len, "%d", S->knobs);
    if (!strcmp(key, "knob3")) return snprintf(buf, len, "%d", S->knob3);
    if (!strcmp(key, "send_kill")) return snprintf(buf, len, "%d", S->kill);
    if (!strcmp(key, "level_display")) return mix_db_text(mix_level_db(S->level), buf, len);
    if (!strcmp(key, "send_a_display")) return mix_db_text(mix_send_db(S->send_a), buf, len);
    if (!strcmp(key, "send_b_display")) return mix_db_text(mix_send_db(S->send_b), buf, len);
    if (!strcmp(key, "filter_display")) return filter_text(S->filter, buf, len);
    if (!strncmp(key, "cc_", 3))
        for (int k = 0; k < 6; k++)
            if (!strcmp(key + 3, KEYS[CTL_P[k]]))
                return S->col < 0 ? snprintf(buf, len, "-") : snprintf(buf, len, "CC %d", CTL_CC[k] + S->col % 8);
    if (!strcmp(key, "state"))
        return snprintf(buf, len, "track=%d;level=%.2f;mute=%d;cue=%d;filter=%.2f;send_a=%.2f;send_b=%.2f;midi_ch=%d;"
                        "knobs=%d;knob3=%d;send_kill=%d;", S->track, S->level, S->mute, S->cue, S->filter, S->send_a,
                        S->send_b, S->midi_ch, S->knobs, S->knob3, S->kill);
    return 0;
}

int mpc_engine_changed(void *inst, char *key, int len) {
    strip_t *S = inst;
    if (!S->changed) return 0;
    int p = __builtin_ctz(S->changed);
    S->changed &= S->changed - 1;
    snprintf(key, len, "%s", KEYS[p]);
    return 1;
}

void mpc_engine_input(void *inst, const int16_t *in_lr, int frames) {
    strip_t *S = inst;
    if (frames > MAXF) frames = MAXF;
    memcpy(S->in_buf, in_lr, sizeof(int16_t) * 2 * frames);
    S->in_frames = frames;
}

/* FILTER from the controller, with a detent at OFF (the middle is hard to find on an endless knob): a relative knob
 * stops at OFF on its way through and needs DETENT counts to leave it; an absolute one reads OFF around 64. */
static float filter_turn(strip_t *S, int32_t d) {
    const float step = 200.0f / 127.0f;
    if (S->filter == 0.0f) {
        S->detent += d;
        if (S->detent >= -DETENT && S->detent <= DETENT) return 0.0f;
        int32_t past = S->detent > 0 ? S->detent - DETENT : S->detent + DETENT;
        S->detent = 0;
        return clampf((float)past * step, -100, 100);
    }
    float to = S->filter + (float)d * step;
    if ((S->filter > 0.0f) != (to > 0.0f) || to == 0.0f) {   /* through the middle: stop at OFF */
        S->detent = 0;
        return 0.0f;
    }
    return clampf(to, -100, 100);
}

static float filter_abs(int v) {
    int c = v - 64;
    if (c >= -ABS_DEAD && c <= ABS_DEAD) return 0.0f;
    return c > 0 ? (float)(c - ABS_DEAD) * 100.0f / (63 - ABS_DEAD) : (float)(c + ABS_DEAD) * 100.0f / (64 - ABS_DEAD);
}

/* The controller's moves on this strip's column since the last block. */
static void read_controller(strip_t *S) {
    if (S->col < 0) return;
    int ch = S->ctl_ch, col = S->ctl_col;
    for (int k = 0; k < 6; k++) {
        int cc = CTL_CC[k] + col;
        uint32_t s = MIX_LOAD(S->m->cc_seq[ch][cc]);
        if (s == S->seq[k]) continue;
        S->seq[k] = s;
        int v = S->m->cc_val[ch][cc];
        int32_t r = MIX_LOAD(S->m->cc_rel[ch][cc]), d = r - S->rel[k];
        S->rel[k] = r;
        int p = CTL_P[k] == P_FILTER && S->knob3 ? P_SEND_KILL : CTL_P[k];   /* the third knob: FILTER or SEND KILL */
        if ((p == P_LEVEL || p == P_MUTE) && S->D.mpc) continue;   /* MPC MIXER: the desk sends them to MPC */
        if (p == P_CUE && S->D.kill) {                              /* BUTTON 2 = SEND KILL: on cuts the sends */
            S->kill = v >= 64;
            S->changed |= 1u << P_SEND_KILL;
            continue;
        }
        switch (p) {
        case P_SEND_A: S->send_a = clampf(S->knobs ? v * 100.0f / 127.0f : S->send_a + d * 100.0f / 127.0f, 0, 100); break;
        case P_SEND_B: S->send_b = clampf(S->knobs ? v * 100.0f / 127.0f : S->send_b + d * 100.0f / 127.0f, 0, 100); break;
        case P_FILTER: S->filter = S->knobs ? filter_abs(v) : filter_turn(S, d); break;
        case P_SEND_KILL: S->kill = S->knobs ? v < 64 : d < 0 ? 1 : d > 0 ? 0 : S->kill; break;   /* left kills */
        case P_LEVEL: S->level = v * 100.0f / 127.0f; break;
        case P_MUTE: S->mute = v >= 64; break;
        case P_CUE: {
            int was = S->cue;
            S->cue = v >= 64;
            if (S->cue && !was) cue_on(S);
            break;
        }
        }
        S->changed |= 1u << p;
    }
}

static void shared_tick(strip_t *S, int frames) {
    mixshm_t *m = S->m;
    /* the controllers: read them here if no one else is */
    int l = mix_listen(m, S->id, &S->L);
    if (l && !S->listening) {
        mixin_open(&S->in, "Mix Strip");
        S->listening = 1;
    } else if (!l && S->listening) {
        mixin_close(&S->in);
        S->listening = 0;
    }
    if (l) {
        uint8_t msg[3];
        int n;
        mixin_tick(&S->in, frames);
        while ((n = mixin_read(&S->in, msg)) > 0)
            if (n == 3 && (msg[0] & 0xF0) == 0xB0) mix_cc_in(m, msg[0] & 15, msg[1], msg[2]);
        if (S->in.seq) MIX_STORE(m->controllers, (uint32_t)S->in.ports);
    }
    if (S->slot < 0) hold_slot(S);   /* the strip that had this TRACK may have gone */
    int was_kill = S->D.kill;
    if (mix_desk_read(m, &S->desk_gen, &S->D)) {   /* the desk came, went or changed: maybe another column */
        ctl_sync(S);
        if (S->D.kill && !was_kill && S->cue) {   /* BUTTON 2 is SEND KILL now: no cue left on that can't be turned off */
            S->cue = 0;
            S->changed |= 1u << P_CUE;
        }
    }
    read_controller(S);
    uint32_t s = MIX_LOAD(m->solo_seq);   /* SOLO: another strip's cue went on */
    if (s != S->solo_seen) {
        S->solo_seen = s;
        if (S->cue && (int)MIX_LOAD(m->solo_track) != S->track && MIX_LOAD(m->cue_mode) == 1) {
            S->cue = 0;
            S->changed |= 1u << P_CUE;
        }
    }
    uint32_t b = MIX_LOAD(m->bus_beat[0]);   /* is a CUE bus playing? */
    if (b != S->cue_beat) {
        S->cue_beat = b;
        S->cue_stale = 0;
    } else if (S->cue_stale <= MIX_STALE) S->cue_stale++;
}

static inline int16_t out_sample(float x) {
    int v = (int)lrintf(x * 32768.0f);
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void e_render(void *inst, int16_t *out, int frames) {
    strip_t *S = inst;
    if (frames > MAXF) frames = MAXF;
    if (S->m) shared_tick(S, frames);
    int mpc = S->D.mpc;   /* MPC MIXER: MPC's fader and Track Mute, after this strip, do LEVEL and MUTE */
    float fad = 1.0f;     /* ... and the sends follow the fader the desk last sent for this TRACK */
    if (mpc) {
        uint8_t f = __atomic_load_n(&S->m->fader[S->track - 1], __ATOMIC_ACQUIRE);
        if (f != MIX_UNKNOWN) fad = mix_gain(mix_level_db((float)(f & 127) * 100.0f / 127.0f));
    }
    float lvl = mpc ? 1.0f : mix_gain(mix_level_db(S->level)), ga = mix_gain(mix_send_db(S->send_a)),
          gb = mix_gain(mix_send_db(S->send_b)), filter = S->knob3 ? 0.0f : S->filter;   /* SEND KILL: no filter */
    float wet = clampf((fabsf(filter) - 1.0f) / 4.0f, 0, 1);
    float lpg = glide(&S->lp_oct, lp_hz(filter)), hpg = glide(&S->hp_oct, hp_hz(filter)), inv = 1.0f / (float)frames;
    float dl = (lvl - S->g_level) * inv, da = (ga - S->g_a) * inv, db = (gb - S->g_b) * inv, dw = (wet - S->wet) * inv,
          dfad = (fad - S->g_fad) * inv;
    float mute_to = S->mute && !mpc ? 0.0f : 1.0f, cue_to = S->cue ? 1.0f : 0.0f;
    int filt = wet > 0.0f || S->wet > 0.0f;   /* the filter runs only while it's in the sound */
    float kill_to = S->kill ? 0.0f : 1.0f;
    int sends = !S->kill || S->g_kill > 0.0f;
    int tap_cue = S->cue || S->g_cue > 0.0f, tap_a = sends && (ga > 0.0f || S->g_a > 0.0f),
        tap_b = sends && (gb > 0.0f || S->g_b > 0.0f);
    /* the filters' coefficients at the block's start and end, in between a straight line: no division per sample */
    svfc_t lc = svf_coef(S->lp_g), hc = svf_coef(S->hp_g), le = svf_coef(lpg), he = svf_coef(hpg);
    svfc_t ld = {(le.a1 - lc.a1) * inv, (le.a2 - lc.a2) * inv, (le.a3 - lc.a3) * inv},
           hd = {(he.a1 - hc.a1) * inv, (he.a2 - hc.a2) * inv, (he.a3 - hc.a3) * inv};
    float cue[2 * MAXF], sa[2 * MAXF], sb[2 * MAXF];
    int thru = S->in_frames == frames;
    if (!filt) memset(S->lp, 0, sizeof S->lp), memset(S->hp, 0, sizeof S->hp);   /* fresh when it comes back */
    for (int i = 0; i < frames; i++) {
        S->g_level += dl, S->g_a += da, S->g_b += db, S->g_fad += dfad;
        if (S->g_mute != mute_to) S->g_mute = mute_to > S->g_mute ? fminf(mute_to, S->g_mute + MUTE_STEP) : fmaxf(mute_to, S->g_mute - MUTE_STEP);
        if (S->g_cue != cue_to) S->g_cue = cue_to > S->g_cue ? fminf(cue_to, S->g_cue + MUTE_STEP) : fmaxf(cue_to, S->g_cue - MUTE_STEP);
        if (S->g_kill != kill_to) S->g_kill = kill_to > S->g_kill ? fminf(kill_to, S->g_kill + MUTE_STEP) : fmaxf(kill_to, S->g_kill - MUTE_STEP);
        float post = S->g_level * S->g_mute, send = post * S->g_kill * S->g_fad;
        if (filt) {
            S->wet += dw;
            lc.a1 += ld.a1, lc.a2 += ld.a2, lc.a3 += ld.a3, hc.a1 += hd.a1, hc.a2 += hd.a2, hc.a3 += hd.a3;
        }
        for (int c = 0; c < 2; c++) {
            float y = thru ? S->in_buf[2 * i + c] / 32768.0f : 0.0f;
            if (filt) y += S->wet * (svf_run(&S->hp[c], svf_run(&S->lp[c], y, &lc, 0), &hc, 1) - y);
            float o = y * post, os = y * send;
            cue[2 * i + c] = y * S->g_cue;
            sa[2 * i + c] = os * S->g_a;
            sb[2 * i + c] = os * S->g_b;
            out[2 * i + c] = out_sample(o);
        }
    }
    S->g_level = lvl, S->g_a = ga, S->g_b = gb, S->g_fad = fad, S->wet = wet, S->lp_g = lpg, S->hp_g = hpg;   /* no drift */
    for (int c = 0; c < 2; c++) {
        svf_flush(&S->lp[c]);
        svf_flush(&S->hp[c]);
    }
    S->in_frames = 0;
    if (S->m && S->slot >= 0) {
        mixtrack_t *T = &S->m->track[S->slot];
        const float *taps[MIX_TAPS] = {tap_cue ? cue : NULL, tap_a ? sa : NULL, tap_b ? sb : NULL};
        mix_write(T, taps, S->quiet, frames);
        T->level_db = mpc ? 0.0f : mix_level_db(S->level);
        T->send_a_db = mix_send_db(S->send_a);
        T->send_b_db = mix_send_db(S->send_b);
        MIX_STORE(T->col, (uint32_t)(S->col + 1));
        MIX_STORE(T->flags, (S->mute && !mpc ? MIX_F_MUTE : 0u) | (S->cue ? MIX_F_CUE : 0u) | (S->kill ? MIX_F_KILL : 0u) |
                            (S->send_a > 0 && !S->kill ? MIX_F_SEND_A : 0u) | (S->send_b > 0 && !S->kill ? MIX_F_SEND_B : 0u));
    }
}

/* sd88me's newer wrapper (the release build) hands an effect its block through process(): this block's input, then
 * render on it. This repo's wrapper calls mpc_engine_input and render instead and never reads this field. */
static void e_process(void *inst, const int16_t *in_lr, int16_t *out, int frames) {
    mpc_engine_input(inst, in_lr, frames);
    e_render(inst, out, frames);
}

static const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render, e_process};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
