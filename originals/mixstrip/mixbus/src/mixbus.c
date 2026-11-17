/* [UTIL] Mix Bus: plays one of the buses Mix Strip feeds, on its own MPC track (2026-10-25).
 *
 * Goes on an otherwise empty audio track (MPC runs its inserts with nothing recorded, as Percolator OUT showed).
 * BUS picks what it plays: CUE (every strip whose CUE is on, before its LEVEL and MUTE: route this track to the
 * headphones' output) or FX A / FX B (every strip's SEND A or SEND B, after its LEVEL and MUTE: put the effect in the
 * next insert slot, fully wet). LEVEL is the bus's own (0 dB three-quarters up). The track's own audio passes through.
 * CUE MODE (on the CUE bus): ADD lets several cues play at once; SOLO means a new cue turns the others off.
 *
 * The screen shows all 16 strips: each TRACK's level, MUTE and CUE. How the strips reach it: mixshm.h. */
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

#define MAXF 512
static const char *const BUS_NAME[MIX_TAPS] = {"CUE", "FX A", "FX B"};

typedef struct {
    mixshm_t *m;
    uint32_t id;
    int bus, cue_mode;   /* bus: 0 CUE, 1 FX A, 2 FX B; cue_mode: 0 ADD, 1 SOLO */
    float level, g;
    mixread_t rd[MIX_TRACKS];
    mixlisten_t L;
    mixin_t in;
    int listening;
    int16_t in_buf[2 * MAXF];
    int in_frames;
} bus_t;

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void *e_create(const char *dir) {
    (void)dir;
    bus_t *B = calloc(1, sizeof *B);
    if (!B) return NULL;
    B->level = 75.0f;   /* 0 dB */
    B->g = 1.0f;
    B->m = mix_open();
    if (B->m) B->id = mix_new_id(B->m);
    return B;
}

static void e_destroy(void *inst) {
    bus_t *B = inst;
    if (!B) return;
    mixin_close(&B->in);
    if (B->m) {
        mix_unlisten(B->m, B->id);
        mix_close(B->m);
    }
    free(B);
}

/* MPC sends an insert effect no MIDI; offline (no ALSA) a test drives the controller table through here */
static void e_midi(void *inst, const uint8_t *msg, int len) {
    bus_t *B = inst;
    if (B->m && len >= 3 && (msg[0] & 0xF0) == 0xB0 && !mixin_available()) mix_cc_in(B->m, msg[0] & 15, msg[1], msg[2]);
}

static void set_one(bus_t *B, const char *key, const char *val) {
    if (!strcmp(key, "bus")) B->bus = clampi(atoi(val), 0, MIX_TAPS - 1);
    else if (!strcmp(key, "level")) B->level = clampf((float)atof(val), 0, 100);
    else if (!strcmp(key, "cue_mode")) B->cue_mode = clampi(atoi(val), 0, 1);
}

static void e_set_param(void *inst, const char *key, const char *val) {
    bus_t *B = inst;
    if (strcmp(key, "state")) {
        set_one(B, key, val);
        return;
    }
    char kv[64];   /* "bus=0;level=75.00;cue_mode=0;" */
    for (const char *s = val; *s;) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                set_one(B, kv, eq);
            }
        }
        s += n;
        if (*s) s++;
    }
}

static int status(const bus_t *B, char *buf, int len) {   /* what this bus plays */
    if (!B->m) return snprintf(buf, len, "NO SHARED MEMORY");
    int strips = 0, on = 0, o = 0;
    uint32_t want = B->bus == 0 ? MIX_F_CUE : B->bus == 1 ? MIX_F_SEND_A : MIX_F_SEND_B;
    char list[64] = "";
    for (int t = 0; t < MIX_TRACKS; t++) {
        if (!MIX_LOAD(B->m->track[t].owner)) continue;
        strips++;
        if (!(MIX_LOAD(B->m->track[t].flags) & want)) continue;
        if (on++ < 6 && o < (int)sizeof list) o += snprintf(list + o, sizeof list - o, on > 1 ? " + %d" : "%d", t + 1);
    }
    if (!strips) return snprintf(buf, len, "NO MIX STRIP IN THIS PROJECT");
    if (B->bus == 0) {
        if (!on) return snprintf(buf, len, "NOTHING CUED (%d STRIP%s)", strips, strips > 1 ? "S" : "");
        return snprintf(buf, len, "CUE: TRACK %s%s", list, on > 6 ? " + MORE" : "");
    }
    return snprintf(buf, len, "%d OF %d STRIP%s SEND TO %s", on, strips, strips > 1 ? "S" : "", BUS_NAME[B->bus]);
}

static int e_get_param(void *inst, const char *key, char *buf, int len) {
    bus_t *B = inst;
    if (!strcmp(key, "status")) return status(B, buf, len);
    if (!strcmp(key, "bus")) return snprintf(buf, len, "%d", B->bus);
    if (!strcmp(key, "level")) return snprintf(buf, len, "%.2f", B->level);
    if (!strcmp(key, "cue_mode")) return snprintf(buf, len, "%d", B->cue_mode);
    if (!strcmp(key, "level_display")) return mix_db_text(mix_level_db(B->level), buf, len);
    if (!strcmp(key, "state")) return snprintf(buf, len, "bus=%d;level=%.2f;cue_mode=%d;", B->bus, B->level, B->cue_mode);
    /* the overview: t1..t16 a strip's level, f1..f16 its MUTE and CUE */
    if ((key[0] == 't' || key[0] == 'f') && key[1] >= '1' && key[1] <= '9') {
        int t = atoi(key + 1) - 1;
        if (t < 0 || t >= MIX_TRACKS) return 0;
        const mixtrack_t *T = B->m ? &B->m->track[t] : NULL;
        if (!T || !MIX_LOAD(T->owner)) return snprintf(buf, len, key[0] == 't' ? "-" : " ");
        /* Mix Desk's MPC MIXER: the level and mute are MPC's own (the desk's last word to MPC for this TRACK) */
        int mpc = MIX_LOAD(B->m->desk) && MIX_LOAD(B->m->desk_mpc);
        if (key[0] == 't') return mpc ? snprintf(buf, len, "ON MPC") : mix_db_text(T->level_db, buf, len);
        uint32_t f = MIX_LOAD(T->flags);
        if (mpc && __atomic_load_n(&B->m->mpc_mute[t], __ATOMIC_ACQUIRE) == 1) f |= MIX_F_MUTE;
        int o = 0;
        if (f & MIX_F_MUTE) o += snprintf(buf + o, len - o, "MUTE");
        if ((f & MIX_F_CUE) && o < len) o += snprintf(buf + o, len - o, o ? "  CUE" : "CUE");
        if ((f & MIX_F_KILL) && o < len) o += snprintf(buf + o, len - o, o ? "  KILL" : "KILL");
        return o ? o : snprintf(buf, len, " ");
    }
    return 0;
}

void mpc_engine_input(void *inst, const int16_t *in_lr, int frames) {
    bus_t *B = inst;
    if (frames > MAXF) frames = MAXF;
    memcpy(B->in_buf, in_lr, sizeof(int16_t) * 2 * frames);
    B->in_frames = frames;
}

static void shared_tick(bus_t *B, int frames) {
    mixshm_t *m = B->m;
    int l = mix_listen(m, B->id, &B->L);   /* the controllers: read them here if no one else is */
    if (l && !B->listening) {
        mixin_open(&B->in, "Mix Bus");
        B->listening = 1;
    } else if (!l && B->listening) {
        mixin_close(&B->in);
        B->listening = 0;
    }
    if (l) {
        uint8_t msg[3];
        int n;
        mixin_tick(&B->in, frames);
        while ((n = mixin_read(&B->in, msg)) > 0)
            if (n == 3 && (msg[0] & 0xF0) == 0xB0) mix_cc_in(m, msg[0] & 15, msg[1], msg[2]);
        if (B->in.seq) MIX_STORE(m->controllers, (uint32_t)B->in.ports);
    }
    __atomic_add_fetch(&m->bus_beat[B->bus], 1, __ATOMIC_ACQ_REL);
    if (B->bus == 0) MIX_STORE(m->cue_mode, (uint32_t)B->cue_mode);
}

static inline int16_t out_sample(float x) {
    int v = (int)lrintf(x * 32768.0f);
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void e_render(void *inst, int16_t *out, int frames) {
    bus_t *B = inst;
    if (frames > MAXF) frames = MAXF;
    float acc[2 * MAXF];
    memset(acc, 0, sizeof(float) * 2 * frames);
    if (B->m) {
        shared_tick(B, frames);
        if (frames <= MIX_N / 4)
            for (int t = 0; t < MIX_TRACKS; t++) mix_read(&B->m->track[t], &B->rd[t], B->bus, acc, frames);
    }
    float g = mix_gain(mix_level_db(B->level)), dg = (g - B->g) / (float)frames;
    int thru = B->in_frames == frames;
    for (int i = 0; i < frames; i++) {
        B->g += dg;
        for (int c = 0; c < 2; c++)
            out[2 * i + c] = out_sample(acc[2 * i + c] * B->g + (thru ? B->in_buf[2 * i + c] / 32768.0f : 0.0f));
    }
    B->g = g;
    B->in_frames = 0;
}

/* sd88me's newer wrapper (the release build) hands an effect its block through process(): this block's input, then
 * render on it. This repo's wrapper calls mpc_engine_input and render instead and never reads this field. */
static void e_process(void *inst, const int16_t *in_lr, int16_t *out, int frames) {
    mpc_engine_input(inst, in_lr, frames);
    e_render(inst, out, frames);
}

static const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render, e_process};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
