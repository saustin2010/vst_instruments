/* [UTIL] Mix Desk: the controller's mixer columns in one place, each on any track (2026-11-07).
 *
 * One desk per project, on the master's inserts (it passes its audio through untouched). Sixteen columns, the
 * controller's two pages (MIX_CC_*: columns 1-8 on MIDI CH, 9-16 on MIDI CH + 1): COLUMN n says which MPC track (1-16,
 * or OFF) that column plays. (2026-11-07: first 8 columns on tracks 1-8; the owner then asked for page 2 as well.) The Mix Strips
 * follow it (a strip's TRACK is then the MPC track it's on: mixstrip.c). Its screen shows each column's level, mute,
 * cue / kill and sends.
 *
 * LEVEL + MUTE:
 * - STRIPS: the strips do them, as they do with no desk.
 * - MPC MIXER: the desk sends each column's fader and MUTE from its own MIDI port (mixout.h) as fixed CCs per MPC track
 *   (MIX_OUT_*: track t's volume CC 101 + t on channel 1, its Track Mute the same CC on channel 2), which MPC's MIDI
 *   Learn ties to that track's fader and Track Mute, once, in a template project. The levels and mutes are then MPC's
 *   own, on its mixer and Track Mute pages, and a track with no strip can be on a column too. Nothing is sent until a
 *   fader or button moves (no jumps when a project loads or a column changes track). MUTE OUT: ON/OFF (127 muted,
 *   0 not), or PRESS (127 then 0 for each change) for a learned control that toggles on each press.
 * BUTTON 2 (every strip's second button): CUE, or SEND KILL (on cuts the strip's sends; for when a mixer does the cue).
 *
 * The desk reads the controller table like the strips do and can be the listener itself (mixshm.h, mixin.h). A second
 * desk waits, and takes over when the first goes. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mixshm.h"
#include "mixin.h"
#include "mixout.h"

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

#define MAXF 512
#define COLS MIX_COLS       /* the desk's columns: page 1 (1-8) and page 2 (9-16) */
#define TRACKS MIX_TRACKS   /* the MPC tracks a column can play: 1-16 */

typedef struct {
    mixshm_t *m;
    uint32_t id;
    int mode, kill, midi_ch, mute_out;   /* mode: 0 STRIPS, 1 MPC MIXER; kill: BUTTON 2 = SEND KILL; mute_out: 0 ON/OFF, 1 PRESS */
    int alloc[MIX_COLS];                 /* column -> TRACK 1-16, 0 = OFF */
    int holder;                          /* 1: this desk has the columns */
    int dirty;                           /* settings not yet published */
    uint32_t seq_l[COLS], seq_m[COLS];   /* the fader's and MUTE's CC counts last seen */
    int lv[COLS], mu[COLS];              /* per column, its fader's and MUTE's last value (-1: not moved yet) */
    mixlisten_t L;
    mixin_t in;
    int listening;
    mixout_t out;
    int16_t in_buf[2 * MAXF];
    int in_frames;
} desk_t;

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

/* "<prefix><n>" with n = 1..COLS: n - 1, else -1 */
static int key_col(const char *key, const char *prefix) {
    size_t n = strlen(prefix);
    if (strncmp(key, prefix, n) || key[n] < '1' || key[n] > '9') return -1;
    char *end;
    long c = strtol(key + n, &end, 10);
    return *end || c < 1 || c > COLS ? -1 : (int)c - 1;
}

static int col_ch(const desk_t *D, int c) { return D->midi_ch - 1 + c / 8; }   /* 0-based */

static void sync_seq(desk_t *D) {   /* what the controller sent before now isn't a move */
    if (!D->m) return;
    for (int c = 0; c < COLS; c++) {
        int ch = col_ch(D, c);
        D->seq_l[c] = MIX_LOAD(D->m->cc_seq[ch][MIX_CC_LEVEL + c % 8]);
        D->seq_m[c] = MIX_LOAD(D->m->cc_seq[ch][MIX_CC_MUTE + c % 8]);
    }
}

static void *e_create(const char *dir) {
    (void)dir;
    desk_t *D = calloc(1, sizeof *D);
    if (!D) return NULL;
    D->midi_ch = 1;
    for (int c = 0; c < COLS; c++) {
        D->alloc[c] = c + 1;   /* column n plays track n: as with no desk */
        D->lv[c] = D->mu[c] = -1;
    }
    D->dirty = 1;
    D->m = mix_open();
    if (D->m) {
        D->id = mix_new_id(D->m);
        if (MIX_LOAD(D->m->prefs)) D->midi_ch = clampi((int)MIX_LOAD(D->m->midi_ch), 1, 15);   /* the strips' */
        sync_seq(D);
    }
    return D;
}

static void e_destroy(void *inst) {
    desk_t *D = inst;
    if (!D) return;
    mixin_close(&D->in);
    mixout_close(&D->out);
    if (D->m) {
        mix_unlisten(D->m, D->id);
        mix_desk_release(D->m, D->id);
        mix_close(D->m);
    }
    free(D);
}

/* MPC sends an insert effect no MIDI; offline (no ALSA) a test drives the controller table through here */
static void e_midi(void *inst, const uint8_t *msg, int len) {
    desk_t *D = inst;
    if (D->m && len >= 3 && (msg[0] & 0xF0) == 0xB0 && !mixin_available()) mix_cc_in(D->m, msg[0] & 15, msg[1], msg[2]);
}

static void set_one(desk_t *D, const char *key, const char *val) {
    int v = atoi(val);
    if (!strcmp(key, "mode")) D->mode = clampi(v, 0, 1);
    else if (!strcmp(key, "button2")) D->kill = clampi(v, 0, 1);
    else if (!strcmp(key, "mute_out")) D->mute_out = clampi(v, 0, 1);
    else if (!strcmp(key, "midi_ch")) {
        D->midi_ch = clampi(v, 1, 15);
        sync_seq(D);
        if (D->m) {   /* new strips start on it too */
            MIX_STORE(D->m->midi_ch, (uint32_t)D->midi_ch);
            MIX_STORE(D->m->prefs, 1u);
        }
    } else if (key_col(key, "col") >= 0) D->alloc[key_col(key, "col")] = clampi(v, 0, TRACKS);
    else return;
    D->dirty = 1;
}

static void e_set_param(void *inst, const char *key, const char *val) {
    desk_t *D = inst;
    if (strcmp(key, "state")) {
        set_one(D, key, val);
        return;
    }
    char kv[64];   /* "mode=1;button2=0;midi_ch=6;mute_out=0;col1=1;..." */
    for (const char *s = val; *s;) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                set_one(D, kv, eq);
            }
        }
        s += n;
        if (*s) s++;
    }
}

static int status(const desk_t *D, char *buf, int len) {   /* MPC shows it whole up to PARAM_TEXT_MAX - 1 */
    if (!D->m) return snprintf(buf, len, "NO SHARED MEMORY");
    if (!D->holder) return snprintf(buf, len, "ANOTHER MIX DESK HAS THE COLUMNS");
    if (!MIX_LOAD(D->m->controllers)) return snprintf(buf, len, "NO CONTROLLER FOUND (COLUMNS ON CH %d + %d)", D->midi_ch, D->midi_ch + 1);
    if (!D->mode) return snprintf(buf, len, "LEVEL + MUTE ON THE STRIPS (CH %d + %d)", D->midi_ch, D->midi_ch + 1);
    if (!D->out.seq) return snprintf(buf, len, "MPC MIXER: NO MIDI PORT");
    return snprintf(buf, len, "MPC MIXER: SENDING ON MIX DESK MIDI OUT");
}

static int send_text(float db, char *buf, int len) {   /* a send, short: OFF, 0, -12 */
    if (db <= -199.5f) return snprintf(buf, len, "OFF");
    return snprintf(buf, len, "%d", (int)lrintf(db));
}

/* column c's readouts: lv (its level), st (its switches), sd (its sends) */
static int column_text(const desk_t *D, char kind, int c, char *buf, int len) {
    int t = D->alloc[c];
    if (!t) return snprintf(buf, len, kind == 'l' ? "-" : " ");
    const mixtrack_t *T = D->m ? &D->m->track[t - 1] : NULL;
    int strip = T && MIX_LOAD(T->owner);
    for (int k = 0; k < c; k++)
        if (D->alloc[k] == t && kind == 's') return snprintf(buf, len, "SAME AS %d", k + 1);
    if (kind == 'l') {
        if (D->mode) return D->lv[c] < 0 ? snprintf(buf, len, "FADER -") : snprintf(buf, len, "FADER %d", D->lv[c]);
        return strip ? mix_db_text(T->level_db, buf, len) : snprintf(buf, len, "NO STRIP");
    }
    if (kind == 's') {
        uint32_t f = strip ? MIX_LOAD(T->flags) : 0u;
        if (D->mode) f = (f & ~MIX_F_MUTE) | (D->mu[c] == 1 ? MIX_F_MUTE : 0u);
        int o = 0;
        if (f & MIX_F_MUTE) o += snprintf(buf + o, len - o, "MUTE");
        if ((f & MIX_F_CUE) && o < len) o += snprintf(buf + o, len - o, o ? "  CUE" : "CUE");
        if ((f & MIX_F_KILL) && o < len) o += snprintf(buf + o, len - o, o ? "  KILL" : "KILL");
        return o ? o : snprintf(buf, len, " ");
    }
    if (!strip) return snprintf(buf, len, D->mode ? "NO STRIP" : " ");
    char a[8], b[8];
    send_text(T->send_a_db, a, sizeof a);
    send_text(T->send_b_db, b, sizeof b);
    return snprintf(buf, len, "A %s  B %s", a, b);
}

static int e_get_param(void *inst, const char *key, char *buf, int len) {
    desk_t *D = inst;
    if (!strcmp(key, "status")) return status(D, buf, len);
    if (!strcmp(key, "mode")) return snprintf(buf, len, "%d", D->mode);
    if (!strcmp(key, "button2")) return snprintf(buf, len, "%d", D->kill);
    if (!strcmp(key, "midi_ch")) return snprintf(buf, len, "%d", D->midi_ch);
    if (!strcmp(key, "mute_out")) return snprintf(buf, len, "%d", D->mute_out);
    if (key_col(key, "col") >= 0) return snprintf(buf, len, "%d", D->alloc[key_col(key, "col")]);
    if (key_col(key, "lv") >= 0) return column_text(D, 'l', key_col(key, "lv"), buf, len);
    if (key_col(key, "st") >= 0) return column_text(D, 's', key_col(key, "st"), buf, len);
    if (key_col(key, "sd") >= 0) return column_text(D, 'd', key_col(key, "sd"), buf, len);
    if (!strcmp(key, "sent")) {   /* offline tests: what it has sent since the last look */
        int n = snprintf(buf, len, "%s", D->out.log);
        D->out.loglen = 0;
        D->out.log[0] = 0;
        return n ? n : snprintf(buf, len, " ");
    }
    if (!strcmp(key, "state")) {
        int o = snprintf(buf, len, "mode=%d;button2=%d;midi_ch=%d;mute_out=%d;", D->mode, D->kill, D->midi_ch, D->mute_out);
        for (int c = 0; c < COLS && o < len; c++) o += snprintf(buf + o, len - o, "col%d=%d;", c + 1, D->alloc[c]);
        return o;
    }
    return 0;
}

void mpc_engine_input(void *inst, const int16_t *in_lr, int frames) {
    desk_t *D = inst;
    if (frames > MAXF) frames = MAXF;
    memcpy(D->in_buf, in_lr, sizeof(int16_t) * 2 * frames);
    D->in_frames = frames;
}

/* The controller's faders and MUTEs since the last block: to MPC (MPC MIXER), and kept for the screen. */
static void columns(desk_t *D) {
    mixshm_t *m = D->m;
    for (int c = 0; c < COLS; c++) {
        int ch = col_ch(D, c), t = D->alloc[c], to_mpc = D->mode && t;
        int cl = MIX_CC_LEVEL + c % 8, cm = MIX_CC_MUTE + c % 8;
        uint32_t s = MIX_LOAD(m->cc_seq[ch][cl]);
        if (s != D->seq_l[c]) {
            D->seq_l[c] = s;
            int v = m->cc_val[ch][cl] & 127;
            D->lv[c] = v;
            if (to_mpc) {
                mixout_cc(&D->out, MIX_OUT_VOL_CH - 1, MIX_OUT_CC + t - 1, v);
                __atomic_store_n(&m->fader[t - 1], (uint8_t)v, __ATOMIC_RELEASE);
            }
        }
        s = MIX_LOAD(m->cc_seq[ch][cm]);
        if (s != D->seq_m[c]) {
            D->seq_m[c] = s;
            int on = (m->cc_val[ch][cm] & 127) >= 64;
            /* PRESS: each message from a toggle button is one press, which flips MPC's mute whatever the button's own
             * light says (the desk can't read MPC's mute, so it counts the flips from "not muted") */
            if (to_mpc && D->mute_out) on = D->mu[c] != 1;
            D->mu[c] = on;
            if (to_mpc) {
                if (!D->mute_out) mixout_cc(&D->out, MIX_OUT_MUTE_CH - 1, MIX_OUT_CC + t - 1, on ? 127 : 0);
                else {
                    mixout_cc(&D->out, MIX_OUT_MUTE_CH - 1, MIX_OUT_CC + t - 1, 127);
                    mixout_cc(&D->out, MIX_OUT_MUTE_CH - 1, MIX_OUT_CC + t - 1, 0);
                }
                __atomic_store_n(&m->mpc_mute[t - 1], (uint8_t)on, __ATOMIC_RELEASE);
            }
        }
    }
}

static void shared_tick(desk_t *D, int frames) {
    mixshm_t *m = D->m;
    int l = mix_listen(m, D->id, &D->L);   /* the controllers: read them here if no one else is */
    if (l && !D->listening) {
        mixin_open(&D->in, "Mix Desk listener");
        D->listening = 1;
    } else if (!l && D->listening) {
        mixin_close(&D->in);
        D->listening = 0;
    }
    if (l) {
        uint8_t msg[3];
        int n;
        mixin_tick(&D->in, frames);
        while ((n = mixin_read(&D->in, msg)) > 0)
            if (n == 3 && (msg[0] & 0xF0) == 0xB0) mix_cc_in(m, msg[0] & 15, msg[1], msg[2]);
        if (D->in.seq) MIX_STORE(m->controllers, (uint32_t)D->in.ports);
    }
    if (!D->holder) {
        if (!mix_desk_claim(m, D->id)) return;   /* another desk has them */
        D->holder = 1;
        D->dirty = 1;
        sync_seq(D);
    }
    if (D->dirty) {
        D->dirty = 0;
        mix_desk_publish(m, D->mode, D->kill, D->midi_ch, D->alloc);
    }
    if (D->mode) mixout_open(&D->out, "Mix Desk");   /* once */
    columns(D);
}

static void e_render(void *inst, int16_t *out, int frames) {
    desk_t *D = inst;
    if (frames > MAXF) frames = MAXF;
    if (D->m) shared_tick(D, frames);
    if (D->in_frames == frames) memcpy(out, D->in_buf, sizeof(int16_t) * 2 * frames);   /* the audio passes through */
    else memset(out, 0, sizeof(int16_t) * 2 * frames);
    D->in_frames = 0;
}

/* sd88me's newer wrapper (the release build) hands an effect its block through process(): this block's input, then
 * render on it. This repo's wrapper calls mpc_engine_input and render instead and never reads this field. */
static void e_process(void *inst, const int16_t *in_lr, int16_t *out, int frames) {
    mpc_engine_input(inst, in_lr, frames);
    e_render(inst, out, frames);
}

static const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render, e_process};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
