/* [FX] Incipience OUT: an insert effect that plays one of the main Incipience's OUT 1-11 on its own track (an audio
 * track will do), so an instrument gets that track's fader, inserts and output without a second Incipience. How the
 * OUTs reach it is in tapbus.h (the same file as Incipience's: keep the two the same). Percolator OUT, made stereo and
 * eleven wide.
 *
 * OUT picks which OUT; THRU (on to start with) adds the track's own audio (off: the OUT alone). STATUS says what's
 * coming in. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tapbus.h"

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

static const char *const INST[TAP_OUTS] = {"KICK", "MBRANE", "CL HAT", "OP HAT", "CLAP", "RIM", "CRASH", "RIDE", "X1",
                                           "X2", "FM"};

typedef struct {
    tapbus_t *bus;
    tapread_t tr;
    int out, thru;
    int16_t in[2 * MAXF];   /* the track's own audio for the next block (mpc_engine_input) */
    int in_frames;
} fx_t;

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

static void *e_create(const char *dir) {
    (void)dir;
    fx_t *F = calloc(1, sizeof *F);
    if (F) {
        F->bus = tapbus_open();
        F->thru = 1;
    }
    return F;
}

static void e_destroy(void *inst) {
    fx_t *F = inst;
    if (!F) return;
    tapbus_close(F->bus);
    free(F);
}

static void e_midi(void *inst, const uint8_t *msg, int len) { (void)inst, (void)msg, (void)len; }

static void set_one(fx_t *F, const char *key, const char *val) {
    if (!strcmp(key, "out")) F->out = clampi(atoi(val), 0, TAP_OUTS - 1);
    else if (!strcmp(key, "thru")) F->thru = clampi(atoi(val), 0, 1);
}

static void e_set_param(void *inst, const char *key, const char *val) {
    fx_t *F = inst;
    if (strcmp(key, "state")) {
        set_one(F, key, val);
        return;
    }
    char kv[64];   /* "out=1;thru=0;" */
    for (const char *s = val; *s;) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                set_one(F, kv, eq);
            }
        }
        s += n;
        if (*s) s++;
    }
}

/* what this track gets: the instruments the main Incipience sends to its OUT, or why there's nothing (MPC shows about
 * 24 characters) */
static int status(const fx_t *F, char *buf, int len) {
    if (!F->bus) return snprintf(buf, len, "NO SHARED MEMORY");
    if (!TB_LOAD(F->bus->owner)) return snprintf(buf, len, "NO INCIPIENCE PLAYING");
    uint32_t m = TB_LOAD(F->bus->inst[F->out]);
    if (!m) return snprintf(buf, len, "NOTHING SENT TO OUT %d", F->out + 1);
    int o = 0, k = 0;
    for (int i = 0; i < TAP_OUTS && o < len; i++)
        if (m >> i & 1) {
            if (++k > 3) return o + snprintf(buf + o, len - o, " +%d", __builtin_popcount(m) - 3);
            o += snprintf(buf + o, len - o, k > 1 ? " + %s" : "%s", INST[i]);
        }
    return o;
}

static int e_get_param(void *inst, const char *key, char *buf, int len) {
    fx_t *F = inst;
    if (!strcmp(key, "out")) return snprintf(buf, len, "%d", F->out);
    if (!strcmp(key, "thru")) return snprintf(buf, len, "%d", F->thru);
    if (!strcmp(key, "status")) return status(F, buf, len);
    if (!strcmp(key, "state")) return snprintf(buf, len, "out=%d;thru=%d;", F->out, F->thru);
    return 0;
}

void mpc_engine_input(void *inst, const int16_t *in_lr, int frames) {
    fx_t *F = inst;
    if (frames > MAXF) frames = MAXF;
    memcpy(F->in, in_lr, sizeof(int16_t) * 2 * frames);
    F->in_frames = frames;
}

static inline float sat(float x) {   /* Incipience's (src/dsp.h) */
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline int16_t out_sample(float x) {   /* the same soft ceiling as Incipience's */
    float ax = x < 0 ? -x : x;
    if (ax > 0.9f) {
        float s = 0.9f + 0.1f * sat((ax - 0.9f) * 10.0f);
        x = x < 0 ? -s : s;
    }
    int v = (int)(x * 32767.0f + (x < 0 ? -0.5f : 0.5f));
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void e_render(void *inst, int16_t *out, int frames) {
    fx_t *F = inst;
    float l[TAP_N / 4], r[TAP_N / 4];
    int n = frames > TAP_N / 4 ? TAP_N / 4 : frames;
    int got = tapbus_read(F->bus, &F->tr, F->out, l, r, n);
    int thru = F->thru && F->in_frames == frames;
    for (int i = 0; i < frames; i++) {
        float a = got && i < n ? l[i] : 0.0f, b = got && i < n ? r[i] : 0.0f;
        if (thru) {
            a += F->in[2 * i] / 32768.0f;
            b += F->in[2 * i + 1] / 32768.0f;
        }
        out[2 * i] = out_sample(a);
        out[2 * i + 1] = out_sample(b);
    }
    F->in_frames = 0;
}

/* sd88me's newer wrapper (the release build) hands an effect its block through process(): this block's input, then
 * render on it. This repo's wrapper calls mpc_engine_input and render instead and never reads this field. */
static void e_process(void *inst, const int16_t *in_lr, int16_t *out, int frames) {
    mpc_engine_input(inst, in_lr, frames);
    e_render(inst, out, frames);
}

static const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render, e_process};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
