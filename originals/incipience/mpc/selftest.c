/* Incipience self-test (development): KEYS CH and pitch bend, the MIDI MOD lanes, the mod matrix, the saved state's
 * size, OUT 1-11 through [FX] Incipience OUT loaded as a library of its own (as MPC loads it), and the matrix's cost.
 * Built natively, without the wrapper (from originals/incipience):
 *   cc -O1 -g -fsanitize=address,undefined -Isrc mpc/selftest.c src/incipience.c src/voices.c src/fx.c src/tables.c \
 *      src/samples.c src/kits.c -lm -lpthread -ldl -o build/selftest
 *   cc -O2 -shared -fPIC -Iincipienceout/src incipienceout/src/incipience_out.c -o build/incipienceout.so
 *   build/selftest build/incipienceout.so [bench]
 * Prints a line per check and PASSED at the end (exit 1 on the first failure). */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "incipience.h"

typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
    void (*process)(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);
int mpc_engine_changed(void *inst, char *key, int len);

#define B 128
static const mpc_engine_t *E;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); exit(1); } } while (0)

static void midi(void *I, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    E->midi(I, m, 3);
}
static void set(void *I, const char *k, const char *v) { E->set_param(I, k, v); }
static void seti(void *I, const char *k, int v) {
    char s[16];
    snprintf(s, sizeof s, "%d", v);
    E->set_param(I, k, s);
}

/* the kick as a plain sine: no sweep, no click, a long decay */
static void *sine_kick(void) {
    void *I = E->create("");
    const char *kv[][2] = {{"kd_tune", "0"}, {"kd_pitch", "64"}, {"kd_harm", "0"}, {"kd_attack", "0"}, {"kd_decay", "127"},
                           {"kd_compr", "0"}, {"kd_eq", "0"}, {"kd_lfint", "0"}, {"kd_evamt", "0"}, {"kd_rsend", "0"},
                           {"kd_dsend", "0"}, {"kd_pan", "64"}};
    for (size_t i = 0; i < sizeof kv / sizeof kv[0]; i++) set(I, kv[i][0], kv[i][1]);
    return I;
}

static int16_t *run(void *I, int blocks) {
    int16_t *o = calloc((size_t)blocks * B * 2, sizeof *o);
    for (int b = 0; b < blocks; b++) E->render(I, o + 2 * B * b, B);
    return o;
}

static float freq(const int16_t *o, int from, int to) {   /* rising zero crossings of the left channel */
    int n = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; i++)
        if (o[2 * (i - 1)] < 0 && o[2 * i] >= 0) {
            if (first < 0) first = i;
            last = i;
            n++;
        }
    return n > 1 ? (n - 1) * 44100.0f / (float)(last - first) : 0.0f;
}

static float kick_hz_played(int status, int note, int bend, int bend_ch) {   /* bend: -1 none */
    void *I = sine_kick();
    if (bend >= 0) midi(I, 0xE0 | bend_ch, bend & 127, bend >> 7);
    midi(I, status, note, 100);
    int16_t *o = run(I, 200);
    float f = freq(o, 4410, 22050);
    free(o);
    E->destroy(I);
    return f;
}

static int near(float a, float b, float tol) { return fabsf(a - b) <= tol * fabsf(b); }

static double energy(const int16_t *o, int n) {
    double e = 0;
    for (int i = 0; i < 2 * n; i++) e += (double)o[i] * o[i];
    return e;
}

int main(int argc, char **argv) {
    E = mpc_engine();
    char buf[8192];

    /* ---- KEYS CH: pitched on the instrument's channel, C3 = as tuned; pitch bend on it; ALL; NOTE as before ---- */
    float f0 = kick_hz_played(0x90, 36, -1, 0);
    CHECK(f0 > 90 && f0 < 115, "kick as tuned %.1f Hz", f0);
    float f60 = kick_hz_played(0x91, 60, -1, 0), f72 = kick_hz_played(0x91, 72, -1, 0), f48 = kick_hz_played(0x91, 48, -1, 0);
    CHECK(near(f60, f0, 0.01f), "KEYS CH C3 %.2f vs %.2f", f60, f0);
    CHECK(near(f72, 2 * f0, 0.01f) && near(f48, f0 / 2, 0.01f), "octaves %.2f %.2f (%.2f)", f72, f48, f0);
    float fb = kick_hz_played(0x91, 60, 16383, 1);
    CHECK(near(fb, f0 * powf(2.0f, 2.0f / 12.0f), 0.01f), "bend +2: %.2f vs %.2f", fb, f0 * powf(2.0f, 2.0f / 12.0f));
    CHECK(near(kick_hz_played(0x91, 60, 16383, 2), f0, 0.01f), "a bend on another channel moved it");
    CHECK(near(kick_hz_played(0x90, 36, 16383, 0), f0, 0.01f), "a bend moved the kick played by its NOTE");
    printf("ok   KEYS CH: C3 %.1f Hz, C4 %.1f, C2 %.1f, bend +2 %.1f\n", f60, f72, f48, fb);
    {
        inc_t *P = sine_kick();
        set(P, "kd_kch", "1");   /* ALL */
        midi(P, 0x90, 50, 100);   /* nobody's NOTE: ALL plays it pitched */
        CHECK(P->kd.active && P->kd.pl.pitched && P->kd.pl.semi == -10.0f, "ALL: %d %d %g", P->kd.active, P->kd.pl.pitched, P->kd.pl.semi);
        midi(P, 0x90, 36, 100);   /* its NOTE first: as tuned */
        CHECK(!P->kd.pl.pitched, "ALL: its NOTE should play it as tuned");
        set(P, "kd_kch", "2");    /* channel 1: the plugin track's notes all play the kick */
        midi(P, 0x90, 38, 100);   /* CL HAT's NOTE */
        CHECK(P->kd.pl.pitched && P->kd.pl.semi == -22.0f && !P->sv[0].active, "KEYS CH 1 takes every note");
        E->destroy(P);
    }
    {
        inc_t *P = E->create("");
        midi(P, 0x93, 64, 100);   /* CL HAT on channel 4 (its KEYS CH to start with) */
        CHECK(P->sv[0].active && P->sv[0].gate && P->sv[0].note == 64 && P->sv[0].pl.semi == 4.0f, "CL HAT pitched");
        midi(P, 0x80, 64, 0);     /* MPC sends the note-off on its own channel: by note alone */
        CHECK(!P->sv[0].gate, "CL HAT's note-off");
        for (int n = 60; n < 63; n++) midi(P, 0x9b, n, 100);   /* FM on channel 12: a voice a note */
        int nv = 0, notes = 0;
        for (int v = 0; v < FM_VOICES; v++)
            if (P->fm[v].active) nv++, notes += P->fm[v].note;
        CHECK(nv == 3 && notes == 36 + 37 + 38, "FM pitched: %d voices, notes %d", nv, notes);
        int16_t *o = run(P, 50);
        CHECK(energy(o, 50 * B) > 0, "FM pitched is silent");
        free(o);
        E->destroy(P);
    }

    /* ---- MIDI MOD lanes: CC 1 / 16-18 on the instrument's KEYS CH set the control the lane names ---- */
    {
        inc_t *P = E->create("");
        int decay = -1, rout = -1;
        for (int c = 0; c < MODN[I_KD]; c++)
            if (!strcmp(MODNAME[I_KD][c], "DECAY")) decay = c;
        for (int c = 0; c < MODN[I_CH]; c++)
            if (!strcmp(MODNAME[I_CH][c], "ROUTING")) rout = c;
        CHECK(decay >= 0 && rout >= 0, "lane targets");
        seti(P, "kd_wheel", decay + 1);
        seti(P, "ch_gp2", rout + 1);
        midi(P, 0xb1, 1, 0);
        CHECK(P->pv[P_KD_DECAY] == 0.0f, "wheel -> KICK DECAY 0: %g", P->pv[P_KD_DECAY]);
        midi(P, 0xb1, 1, 127);
        CHECK(P->pv[P_KD_DECAY] == 127.0f && P->ev[P_KD_DECAY] == 127.0f, "wheel -> KICK DECAY 127");
        midi(P, 0xb2, 1, 0);   /* channel 3: MBRANE's, whose wheel is OFF */
        CHECK(P->pv[P_KD_DECAY] == 127.0f, "a wheel on another channel moved KICK DECAY");
        char k[48];
        CHECK(mpc_engine_changed(P, k, sizeof k) && !strcmp(k, "kd_decay") && !mpc_engine_changed(P, k, sizeof k),
              "the screen hears about KICK DECAY once");
        midi(P, 0xb3, 17, 127);   /* GP 2 on CL HAT's channel: ROUTING to its last option */
        CHECK(P->pv[P_CH_ROUT] == 1.0f, "GP 2 -> CL HAT ROUTING %g", P->pv[P_CH_ROUT]);
        E->get_param(P, "kd_decay", buf, sizeof buf);
        CHECK(atof(buf) == 127.0, "get_param after a lane: %s", buf);
        printf("ok   MIDI MOD lanes (wheel -> KICK DECAY, GP 2 -> CL HAT ROUTING, the screen told)\n");
        E->destroy(P);
    }

    /* ---- the mod matrix ---- */
    {
        inc_t *P = E->create("");
        int pitch = -1, smp = -1, lvl = -1;
        for (int c = 0; c < MODN[I_KD]; c++)
            if (!strcmp(MODNAME[I_KD][c], "PITCH")) pitch = c;
        for (int c = 0; c < MODN[I_CH]; c++)
            if (!strcmp(MODNAME[I_CH][c], "SAMPLE")) smp = c;
        for (int c = 0; c < MODN[NDEST - 1]; c++)
            if (!strcmp(MODNAME[NDEST - 1][c], "REV LEVEL")) lvl = c;
        CHECK(pitch >= 0 && smp >= 0 && lvl >= 0, "matrix targets");
        float base = P->pv[P_KD_PITCH];
        set(P, "l1_rate", "127");   /* fast: both ends within a second */
        seti(P, "m1_src", SRC_LFO1);
        seti(P, "m1_dest", I_KD);
        seti(P, "m1_ctl", pitch);
        set(P, "m1_depth", "100");
        float lo = 1e9f, hi = -1e9f;
        int16_t o[2 * B];
        for (int b = 0; b < 400; b++) {
            E->render(P, o, B);
            lo = fminf(lo, P->ev[P_KD_PITCH]);
            hi = fmaxf(hi, P->ev[P_KD_PITCH]);
        }
        CHECK(lo == 0.0f && hi == 127.0f && P->pv[P_KD_PITCH] == base, "LFO 1 -> KICK PITCH: %g..%g, set %g", lo, hi,
              P->pv[P_KD_PITCH]);
        E->get_param(P, "kd_pitch", buf, sizeof buf);
        CHECK(atof(buf) == base, "the screen shows the set value: %s", buf);
        set(P, "m1_depth", "0");
        E->render(P, o, B);
        CHECK(P->ev[P_KD_PITCH] == base, "DEPTH 0 gives it back");
        /* RANDOM -> CL HAT SAMPLE: a new whole number at each hat, in range; VELOCITY -> FX REV LEVEL */
        seti(P, "m2_src", SRC_RANDOM);
        seti(P, "m2_dest", I_CH);
        seti(P, "m2_ctl", smp);
        set(P, "m2_depth", "50");
        seti(P, "m3_src", SRC_VELOCITY);
        seti(P, "m3_dest", NDEST - 1);
        seti(P, "m3_ctl", lvl);
        set(P, "m3_depth", "-40");
        int diff = 0;
        float last = -1;
        for (int h = 0; h < 20; h++) {
            midi(P, 0x90, 38, 1 + 6 * h);
            float s = P->ev[SBASE[0] + S_SAMPLE];
            CHECK(s >= 0 && s <= 255 && s == floorf(s), "RANDOM sample %g", s);
            diff += s != last;
            last = s;
            float want = clampf(P->pv[P_RV_LEVEL] - 0.4f * (1 + 6 * h) / 127.0f * 127.0f, 0, 127);
            CHECK(fabsf(P->ev[P_RV_LEVEL] - want) < 0.01f, "VELOCITY -> REV LEVEL %g, not %g", P->ev[P_RV_LEVEL], want);
            E->render(P, o, B);
        }
        CHECK(diff > 15, "RANDOM changed the sample only %d times in 20", diff);
        CHECK(MODT[NDEST - 1][MODN[NDEST - 1] - 1] == P_VOLUME, "FX's last control is VOLUME");
        int n = E->get_param(P, "state", buf, sizeof buf);
        CHECK(n > 0 && !strstr(buf, "kd_pitch=0;"), "the state holds the set values");
        printf("ok   mod matrix (LFO 1 -> KICK PITCH over the whole range, RANDOM -> SAMPLE, VELOCITY -> REV LEVEL)\n");
        E->destroy(P);
    }

    /* ---- the saved state: within the wrapper's 8 KB with every value at its longest, and back ---- */
    {
        inc_t *P = E->create(""), *Q;
        for (int i = 0; i < NP; i++) {
            char v[24];
            if (PDEF[i].kind == 0) snprintf(v, sizeof v, "%.2f", PDEF[i].lo + (PDEF[i].hi - PDEF[i].lo) * 0.0337f + 0.33f);
            else if (PDEF[i].kind == 1) snprintf(v, sizeof v, "%d", PDEF[i].nopts - 1);
            else if (PDEF[i].kind == 4) snprintf(v, sizeof v, "%d", (int)PDEF[i].hi);
            else continue;
            E->set_param(P, PDEF[i].key, v);
        }
        int n = E->get_param(P, "state", buf, sizeof buf);
        CHECK(n > 0 && n < 8192, "state %d bytes", n);
        Q = E->create("");
        E->set_param(Q, "state", buf);
        for (int i = 0; i < NP; i++)
            CHECK(fabsf(P->pv[i] - Q->pv[i]) < 0.006f, "%s: %g after the state, not %g", PDEF[i].key, Q->pv[i], P->pv[i]);
        E->destroy(Q);
        E->destroy(P);
        P = E->create("");
        int d = E->get_param(P, "state", buf, sizeof buf);
        printf("ok   state: %d bytes at its longest (8192 max), %d as it starts; restored exactly\n", n, d);
        E->destroy(P);
    }

    /* ---- OUT 1-11: Incipience and [FX] Incipience OUT as two libraries, both orders ---- */
    if (argc > 1) {
        void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        CHECK(h, "dlopen %s: %s", argv[1], dlerror());
        const mpc_engine_t *(*oe)(void) = (const mpc_engine_t *(*)(void))dlsym(h, "mpc_engine");
        const mpc_engine_t *O = oe();
        enum { NB = 300 };
        for (int order = 0; order < 2; order++) {
            /* the reference: the same hits, no OUT played */
            void *A = E->create("");
            int16_t *ref = calloc(NB * B * 2, 2), *mo = calloc(NB * B * 2, 2), *fo = calloc(NB * B * 2, 2);
            for (int b = 0; b < NB; b++) {
                if (b == 40) midi(A, 0x90, 36, 120), midi(A, 0x90, 38, 100);
                if (b == 120) midi(A, 0x90, 36, 90);
                E->render(A, ref + 2 * B * b, B);
            }
            E->destroy(A);
            void *M = E->create(""), *F = O->create("");
            set(F, "out", "0");   /* OUT 1: KICK's, to start with */
            set(F, "thru", "0");
            for (int b = 0; b < NB; b++) {
                if (b == 40) midi(M, 0x90, 36, 120), midi(M, 0x90, 38, 100);
                if (b == 120) midi(M, 0x90, 36, 90);
                if (order) O->render(F, fo + 2 * B * b, B);
                E->render(M, mo + 2 * B * b, B);
                if (!order) O->render(F, fo + 2 * B * b, B);
            }
            O->get_param(F, "status", buf, sizeof buf);
            CHECK(!strcmp(buf, "KICK"), "status: %s", buf);
            /* the main mix lost the kick, the effect has it, and the two add up to the reference: the main one a block
             * late, the effect a block late too, or two if it always runs first (as Percolator OUT: tapbus.h) */
            int lm = -1, lf = -1;
            long worst = 0;
            for (int pair = 0; pair < 9 && lm < 0; pair++) {
                int a = pair / 3, c = pair % 3;
                worst = 0;
                for (int i = 2 * B * 30; i < 2 * B * (NB - 3); i++) {
                    long d = labs((long)mo[i + 2 * B * a] + fo[i + 2 * B * c] - ref[i]);
                    if (d > worst) worst = d;
                }
                if (worst <= 2) lm = a, lf = c;
            }
            CHECK(lm == 1 && lf == 1 + order, "main + OUT = the reference (order %d: main %d, OUT %d blocks late; off by %ld)",
                  order, lm, lf, worst);
            double ek = energy(fo + 2 * B * 40, 200 * B), em = energy(mo + 2 * B * 40, 200 * B);
            CHECK(ek > 0 && em > 0, "both play something");
            O->destroy(F);
            E->destroy(M);
            free(ref), free(mo), free(fo);
        }
        /* no main Incipience: silence, and says so */
        void *F = O->create("");
        int16_t o[2 * B];
        O->render(F, o, B);
        O->get_param(F, "status", buf, sizeof buf);
        CHECK(!strcmp(buf, "NO INCIPIENCE PLAYING") && energy(o, B) == 0, "alone: %s", buf);
        O->destroy(F);
        printf("ok   OUT 1: [FX] Incipience OUT plays the kick, the main mix the rest, a block late (the OUT two if it runs first)\n");
    }

    /* ---- what the matrix costs: every instrument busy, no slots vs all 16 on, LFOs and RANDOM ---- */
    if (argc > 2 && !strcmp(argv[2], "bench")) {
        double t[2];
        for (int on = 0; on < 2; on++) {
            inc_t *P = E->create("");
            set(P, "kit", "1");
            for (int s = 0; on && s < NSLOT; s++) {
                char k[24];
                snprintf(k, sizeof k, "m%d_src", s + 1), seti(P, k, s % 5 + 1);
                snprintf(k, sizeof k, "m%d_dest", s + 1), seti(P, k, s % NDEST);
                snprintf(k, sizeof k, "m%d_ctl", s + 1), seti(P, k, (s * 7) % 20);
                snprintf(k, sizeof k, "m%d_depth", s + 1), set(P, k, "30");
            }
            int16_t o[2 * B];
            clock_t c0 = clock();
            int blocks = 60 * 44100 / B;
            for (int b = 0; b < blocks; b++) {
                if (b % 43 == 0)
                    for (int i = 0; i < NI; i++) midi(P, 0x90, 36 + i, 100);
                E->render(P, o, B);
            }
            t[on] = (double)(clock() - c0) / CLOCKS_PER_SEC;
            E->destroy(P);
        }
        printf("bench: 60 s of all eleven every 1/8 at 120: %.2f s without slots, %.2f s with 16 (x%.3f)\n", t[0], t[1],
               t[1] / t[0]);
    }
    printf("PASSED\n");
    return 0;
}
