/* Incipience self-test (development): KEYS CH and pitch bend, the MIDI MOD lanes, the mod matrix, the saved state's
 * size, OUT 1-11 through [FX] Incipience OUT loaded as a library of its own (as MPC loads it), and the matrix's cost.
 * Built natively, without the wrapper (from originals/incipience):
 *   cc -O1 -g -fsanitize=address,undefined -Isrc mpc/selftest.c src/incipience.c src/voices.c src/fx.c src/tables.c \
 *      src/samples.c src/kits.c src/library.c -lm -lpthread -ldl -o build/selftest
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

/* a 440 Hz sine at amp: fmt 1 PCM (8/16/24/32-bit), 3 float, 0xFFFE extensible (24-bit PCM) */
static void write_wav(const char *path, int frames, int ch, int bits, int fmt, int rate, float amp) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int bpf = ch * bits / 8, ext = fmt == 0xFFFE, flen = ext ? 40 : 16;
    uint32_t data = (uint32_t)(frames * bpf);
    uint8_t h[80];
    int n = 0;
#define P4(v) (h[n] = (uint8_t)(v), h[n + 1] = (uint8_t)((v) >> 8), h[n + 2] = (uint8_t)((v) >> 16), h[n + 3] = (uint8_t)((v) >> 24), n += 4)
#define P2(v) (h[n] = (uint8_t)(v), h[n + 1] = (uint8_t)((v) >> 8), n += 2)
    memcpy(h + n, "RIFF", 4), n += 4;
    P4(4 + 8 + flen + 8 + data);
    memcpy(h + n, "WAVEfmt ", 8), n += 8;
    P4(flen);
    P2(fmt);
    P2(ch);
    P4(rate);
    P4(rate * bpf);
    P2(bpf);
    P2(bits);
    if (ext) {
        P2(22);
        P2(bits);
        P4(0);
        P2(1);   /* the subformat GUID's first two bytes: PCM */
        memcpy(h + n, "\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71", 14), n += 14;
    }
    memcpy(h + n, "data", 4), n += 4;
    P4(data);
    fwrite(h, 1, (size_t)n, f);
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < ch; c++) {
            float x = amp * sinf(2.0f * 3.14159265f * 440.0f * (float)i / (float)rate);
            uint8_t s[4];
            int32_t v = (int32_t)lrintf(x * 2147483647.0f);
            if (fmt == 3) memcpy(s, &x, 4);
            else for (int k = 0; k < 4; k++) s[k] = (uint8_t)((uint32_t)v >> (8 * k));
            if (bits == 8) s[0] = (uint8_t)(((uint32_t)v >> 24) ^ 0x80);
            fwrite(fmt == 3 || bits == 32 || bits == 8 ? s : s + 4 - bits / 8, 1, (size_t)(bits / 8), f);
        }
#undef P4
#undef P2
    fclose(f);
}

static uint8_t *slurp(const char *path, long *sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*sz);
    if (b && fread(b, 1, (size_t)*sz, f) != (size_t)*sz) b = (free(b), NULL);
    fclose(f);
    return b;
}

static int16_t *run_rt(void *I) {   /* a block in real time: the library's thread reads at the MPC's pace */
    struct timespec ts = {0, 2900000};
    nanosleep(&ts, NULL);
    return run(I, 1);
}

static int row_is(inc_t *P, int r, const char *want) {
    char k[32], b[64];
    snprintf(k, sizeof k, "br_row_%d_display", r);
    E->get_param(P, k, b, sizeof b);
    return !strcmp(b, want);
}
static int row_val(inc_t *P, int r) {   /* 1: lit (the cursor's row) */
    char k[32], b[64];
    snprintf(k, sizeof k, "br_row_%d", r);
    E->get_param(P, k, b, sizeof b);
    return atoi(b);
}
static const char *rows(inc_t *P) {
    static char all[600];
    all[0] = 0;
    for (int r = 1; r <= BROWSE_ROWS; r++) {
        char k[32], b[64];
        snprintf(k, sizeof k, "br_row_%d_display", r);
        E->get_param(P, k, b, sizeof b);
        snprintf(all + strlen(all), sizeof all - strlen(all), "[%s]", b);
    }
    return all;
}
static void browse_wait(inc_t *P) {   /* until the listing asked for is read and shown */
    char b[64];
    for (int t = 0; t < 4000; t++) {
        E->get_param(P, "br_row_1_display", b, sizeof b);
        if (!P->bwait) return;
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
    CHECK(0, "no listing came");
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

    /* ---- RANDOMISE: each instrument's own sound controls in their ranges, its VOLUME, ACCENT and MIX as they were,
     * MPC told of each change, and every roll heard (no sample set here: X1 and X2, which are samples alone, aside) ---- */
    {
        inc_t *P = E->create("");
        char k[32];
        int rolls = 0, quiet = 0;
        double softest = 1e30;
        for (int ins = 0; ins < NI; ins++) {
            const char *key = PDEF[P_KD_RAND + ins].key;
            int pl = (int)(strchr(key, '_') - key);
            for (int roll = 0; roll < 40; roll++) {
                while (mpc_engine_changed(P, k, sizeof k)) {}
                float before[NP];
                memcpy(before, P->pv, sizeof before);
                set(P, key, "1");
                set(P, key, "0");   /* the button's release: nothing more */
                int moved = 0;
                for (int i = 0; i < NP; i++) {
                    if (P->pv[i] == before[i]) continue;
                    moved++;
                    CHECK(!strncmp(PDEF[i].key, key, pl + 1), "%s moved %s", key, PDEF[i].key);
                    const char *suf = PDEF[i].key + pl + 1;
                    CHECK(strcmp(suf, "vol") && strcmp(suf, "acc") && strcmp(suf, "pan") && strcmp(suf, "rsend") &&
                          strcmp(suf, "dsend"), "%s moved %s", key, PDEF[i].key);
                    const rnd_t *r = &RND[i];
                    float v = P->pv[i];
                    CHECK(v == floorf(v) && v >= PDEF[i].lo && v <= (PDEF[i].kind == 1 ? PDEF[i].nopts - 1 : PDEF[i].hi),
                          "%s = %g", PDEF[i].key, v);
                    CHECK(r->how != 1 || (v >= r->lo && v <= r->hi) || v == r->v ||
                          (strstr(PDEF[i].key, "_rout") || strstr(PDEF[i].key, "_mtnz") || strstr(PDEF[i].key, "_nzvol")),
                          "%s = %g, not %d..%d", PDEF[i].key, v, r->lo, r->hi);   /* the last three: heard (see randomise) */
                    CHECK(r->how != 2 || v == P->pv[r->eq], "%s = %g, not %s's", PDEF[i].key, v, PDEF[r->eq].key);
                    CHECK(P->ev[i] == v, "%s: the sound has %g, the screen %g", PDEF[i].key, P->ev[i], v);
                }
                int told = 0;
                while (mpc_engine_changed(P, k, sizeof k)) told++;
                CHECK(moved > 0 && told >= moved, "%s: %d moved, MPC told of %d", key, moved, told);
                if (ins == I_X1 || ins == I_X2) continue;
                midi(P, 0x90, 36 + ins, 100);
                int16_t *o = run(P, 8);
                midi(P, 0x80, 36 + ins, 0);
                int16_t *o2 = run(P, 160);
                double e = energy(o, 8 * B) + energy(o2, 160 * B);
                free(o), free(o2);
                rolls++;
                if (e < softest) softest = e;
                if (e < 1e6) {
                    quiet++;
                    printf("     quiet: %s roll %d (energy %.3g)\n", key, roll, e);
                }
                midi(P, 0xB0, 120, 0);   /* all sound off: the next roll starts from silence */
                free(run(P, 4));
            }
        }
        CHECK(quiet == 0, "%d of %d rolls silent", quiet, rolls);
        printf("ok   RANDOMISE: 40 rolls an instrument, each in its ranges, the mix kept, MPC told; %d heard (softest %.3g)\n",
               rolls, softest);
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

    /* ---- your own samples (library.c, the SAMPLES tab): WAV files of each kind read; a folder's WAV files in name
     * order; the tab's browsing; a file tapped loads into LOAD TO's channel and plays; SAMPLE steps through its folder;
     * by name in the state; RANDOMISE and the matrix within the folder; a kit back to the AlphaBase set; and past the
     * memory limit, files go while a voice holds and plays another (built with ASan: no file freed under it) ---- */
    {
        int fail = system("rm -rf build/libtest && mkdir -p build/libtest/samples/Sub build/libtest/big");
        CHECK(!fail, "mkdir build/libtest");
        /* the kinds of WAV file: 16-bit mono, 24-bit stereo, 32-bit float, 8-bit, 24-bit WAVE_FORMAT_EXTENSIBLE */
        const struct { const char *name; int ch, bits, fmt, rate; } W[] = {
            {"kick 1.WAV", 1, 16, 1, 44100}, {"Kick 2.wav", 2, 24, 1, 48000}, {"Kick 10.wav", 1, 32, 3, 96000},
            {"snare.wav", 1, 8, 1, 22050}, {"Sub/inner.wav", 2, 24, 0xFFFE, 44100}};
        char path[512];
        for (size_t w = 0; w < sizeof W / sizeof W[0]; w++) {
            snprintf(path, sizeof path, "build/libtest/samples/%s", W[w].name);
            write_wav(path, 4410, W[w].ch, W[w].bits, W[w].fmt, W[w].rate, 0.5f);
            long sz;
            uint8_t *b = slurp(path, &sz);
            int16_t *d = NULL;
            int fr = 0;
            float rate = 0;
            CHECK(b && !lib_decode_wav(b, sz, &d, &fr, &rate), "%s doesn't decode", W[w].name);
            float want = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * 100.0f / W[w].rate) * 32767.0f;
            CHECK(fr == 4410 && rate == (float)W[w].rate && fabsf(d[100] - want) < 300.0f, "%s: %d frames at %g, %d not %.0f",
                  W[w].name, fr, rate, d[100], want);
            free(b), free(d);
        }
        FILE *f = fopen("build/libtest/samples/notes.txt", "w");
        if (f) fclose(f);
        write_wav("build/libtest/samples/.hidden.wav", 100, 1, 16, 1, 44100, 0.5f);

        inc_t *P = E->create("build/libtest");
        browse_wait(P);   /* the tab's first look: the places */
        CHECK(row_is(P, 1, "> SAMPLE SET BY NUMBER") && row_is(P, 2, "INCIPIENCE SAMPLES"), "places: %s", rows(P));
        set(P, "br_row_2", "1");
        browse_wait(P);
        CHECK(row_is(P, 1, ".. UP") && row_is(P, 2, "Sub /") && row_is(P, 3, "kick 1") && row_is(P, 4, "Kick 2") &&
              row_is(P, 5, "Kick 10") && row_is(P, 6, "snare") && row_is(P, 7, " "), "folder: %s", rows(P));
        E->get_param(P, "br_status", buf, sizeof buf);
        CHECK(!strcmp(buf, "4 WAVS"), "status %s", buf);
        E->get_param(P, "br_path", buf, sizeof buf);
        CHECK(!strcmp(buf, "INCIPIENCE"), "path %s", buf);
        /* 1.1.0: the cursor starts at the top (on Sub, after ..); a tap on a file puts it there, lights its row and plays
         * the file straight from the folder (PREVIEW on); X1 is unchanged */
        CHECK(P->pv[P_BR_CURSOR] == 1.0f && row_val(P, 2) == 1 && row_val(P, 1) == 0, "the cursor at the top: %g",
              P->pv[P_BR_CURSOR]);
        set(P, "br_row_4", "1");
        E->get_param(P, "br_cursor_display", buf, sizeof buf);
        CHECK(P->pv[P_BR_CURSOR] == 3.0f && !strcmp(buf, "Kick 2") && row_val(P, 4) == 1 && row_val(P, 2) == 0 &&
              P->sfold[6] < 0, "a tap: cursor %g (%s), X1 folder %d", P->pv[P_BR_CURSOR], buf, P->sfold[6]);
        for (int b = 0; b < 400 && P->prv_slot < 0; b++) free(run_rt(P));
        CHECK(P->prv_slot >= 0 && P->prv_s.len == 4410 && !P->sv[6].active, "PREVIEW didn't play the file under the cursor");
        int16_t *o = run(P, 20);
        CHECK(energy(o, 20 * B) > 1e6, "the cursor's PREVIEW is silent");
        free(o);
        /* MPC turning the lit row off straight after (an echo) isn't a tap; a while later it is: X1 (LOAD TO, to start
         * with) loads it and plays it, PREVIEW on */
        set(P, "br_row_4", "0");
        CHECK(P->sfold[6] < 0, "an echo loaded the file");
        free(run(P, 40));
        set(P, "br_row_4", "0");
        CHECK(P->sfold[6] >= 0 && P->pv[P_X1_SAMPLE] == 2.0f && P->prv_slot < 0, "X1: folder %d, SAMPLE %g",
              P->sfold[6], P->pv[P_X1_SAMPLE]);
        for (int b = 0; b < 400 && !P->sv[6].active; b++) free(run_rt(P));
        CHECK(P->sv[6].active && P->sv[6].s.len == 4410 && P->sv[6].lslot >= 0, "PREVIEW didn't play X1's new sample");
        o = run(P, 20);
        CHECK(energy(o, 20 * B) > 1e6, "PREVIEW is silent");
        free(o);
        CHECK(row_is(P, 4, "> Kick 2"), "the loaded file's mark: %s", rows(P));
        /* FILE (the data wheel, Q-Link 1): one entry at a time, heard; LOAD loads it; past the end it stays */
        seti(P, "br_cursor", 4);
        for (int b = 0; b < 400 && P->prv_slot < 0; b++) free(run_rt(P));
        CHECK(P->prv_slot >= 0 && P->pv[P_X1_SAMPLE] == 2.0f, "FILE 4: PREVIEW %d, X1 %g", P->prv_slot, P->pv[P_X1_SAMPLE]);
        set(P, "br_load", "1");
        CHECK(P->pv[P_X1_SAMPLE] == 3.0f, "LOAD: X1 %g", P->pv[P_X1_SAMPLE]);
        seti(P, "br_cursor", 99);
        CHECK(P->pv[P_BR_CURSOR] == 5.0f, "FILE past the end: %g", P->pv[P_BR_CURSOR]);
        seti(P, "br_cursor", 3);
        set(P, "br_row_4", "1");   /* a tap on the cursor's row (MPC saw it off): loads */
        CHECK(P->pv[P_X1_SAMPLE] == 2.0f, "a tap on the cursor's row: X1 %g", P->pv[P_X1_SAMPLE]);
        /* AUDITION: turning SAMPLE plays it (off: it doesn't) */
        for (int b = 0; b < 400 && P->aud; b++) free(run_rt(P));
        free(run(P, 100));
        seti(P, "x1_sample", 3);
        CHECK(!P->aud, "AUDITION off, SAMPLE played");
        set(P, "br_audition", "1");
        seti(P, "x1_sample", 2);
        CHECK(P->aud == 7, "AUDITION on: SAMPLE didn't play");
        for (int b = 0; b < 400 && P->aud; b++) free(run_rt(P));
        CHECK(P->sv[6].active, "AUDITION: X1 silent");
        set(P, "br_audition", "0");
        free(run(P, 100));
        E->get_param(P, "x1_sample_display", buf, sizeof buf);
        CHECK(!strcmp(buf, "Kick 2"), "X1 SAMPLE shows %s", buf);
        seti(P, "x1_sample", 3);
        E->get_param(P, "x1_sample_display", buf, sizeof buf);
        CHECK(!strcmp(buf, "Kick 10"), "SAMPLE 3 shows %s", buf);
        seti(P, "x1_sample", 9);
        CHECK(P->pv[P_X1_SAMPLE] == 4.0f, "SAMPLE past the folder's files: %g", P->pv[P_X1_SAMPLE]);
        /* the state names the folder and the file; another instance finds it by name */
        int n = E->get_param(P, "state", buf, sizeof buf);
        CHECK(n > 0 && strstr(buf, "x1_dir=build/libtest/samples;") && strstr(buf, "x1_file=snare.wav;"), "state: %s", buf);
        inc_t *Q = E->create("build/libtest");
        E->set_param(Q, "state", buf);
        lib_wait();
        free(run(Q, 2));
        E->get_param(Q, "x1_sample_display", buf, sizeof buf);
        CHECK(Q->sfold[6] >= 0 && Q->pv[P_X1_SAMPLE] == 4.0f && !strcmp(buf, "snare"), "restored: %g %s", Q->pv[P_X1_SAMPLE], buf);
        E->destroy(Q);
        /* a file added: the folder listed again is a new folder (the old one still plays what it had); by name again */
        write_wav("build/libtest/samples/Kick 1b.wav", 4410, 1, 16, 1, 44100, 0.5f);
        set(P, "br_up", "1");
        browse_wait(P);
        set(P, "br_row_2", "1");
        browse_wait(P);
        /* X1 plays snare from the folder as it was: marked by name (1.0.0 marked by number, the wrong file once one was added) */
        CHECK(row_is(P, 4, "Kick 1b") && row_is(P, 7, "> snare") && P->pv[P_BR_CURSOR] == 6.0f, "re-listed: %s", rows(P));
        int old = P->sfold[6];
        set(P, "br_row_7", "1");
        set(P, "br_load", "1");
        CHECK(P->sfold[6] != old && P->pv[P_X1_SAMPLE] == 5.0f, "the new folder: %d (was %d), SAMPLE %g", P->sfold[6], old,
              P->pv[P_X1_SAMPLE]);
        /* RANDOMISE: a file of the folder */
        for (int r = 0; r < 30; r++) {
            set(P, "x1_rand", "1");
            CHECK(P->pv[P_X1_SAMPLE] >= 1.0f && P->pv[P_X1_SAMPLE] <= 5.0f, "RANDOMISE: SAMPLE %g", P->pv[P_X1_SAMPLE]);
        }
        /* the matrix: RANDOM over the folder's files */
        set(P, "m1_src", "5"), seti(P, "m1_dest", I_X1), set(P, "m1_depth", "100");
        for (int c = 0; c < MODN[I_X1]; c++)
            if (MODT[I_X1][c] == P_X1_SAMPLE) seti(P, "m1_ctl", c);
        for (int h = 0; h < 40; h++) {
            midi(P, 0x90, 44, 100);
            float s = P->ev[P_X1_SAMPLE];
            CHECK(s >= 0.0f && s <= 5.0f && s == floorf(s), "RANDOM -> SAMPLE on the folder: %g", s);
            free(run(P, 2));
        }
        set(P, "m1_src", "0");
        /* SAMPLE SET BY NUMBER; and a kit: back to the set */
        set(P, "br_up", "1");
        browse_wait(P);
        set(P, "br_row_1", "1");
        CHECK(P->sfold[6] < 0 && row_is(P, 1, "> SAMPLE SET BY NUMBER"), "SAMPLE SET BY NUMBER: folder %d", P->sfold[6]);
        set(P, "br_row_2", "1");
        browse_wait(P);
        set(P, "br_row_3", "1");
        set(P, "br_load", "1");
        CHECK(P->sfold[6] >= 0, "loaded again");
        /* LOAD TO: X2 (on the set by number) leaves the list where it is; Sub's file into X2, then X1: the list goes
         * to X1's folder, the cursor on its file */
        set(P, "br_target", "7");
        CHECK(row_is(P, 3, "> kick 1") == 0 && row_is(P, 2, "Sub /"), "X2 picked: %s", rows(P));
        set(P, "br_row_2", "1");   /* Sub */
        browse_wait(P);
        set(P, "br_load", "1");    /* inner, the cursor's to start with */
        CHECK(P->sfold[7] >= 0 && P->pv[P_X2_SAMPLE] == 1.0f, "X2: folder %d, SAMPLE %g", P->sfold[7], P->pv[P_X2_SAMPLE]);
        set(P, "br_target", "6");
        browse_wait(P);
        E->get_param(P, "br_cursor_display", buf, sizeof buf);
        CHECK(!strcmp(buf, "kick 1") && row_is(P, 3, "> kick 1"), "X1 picked: the cursor on %s, %s", buf, rows(P));
        /* a long name: its start and end, 24 characters */
        write_wav("build/libtest/samples/A long kick drum name take 04.wav", 441, 1, 16, 1, 44100, 0.5f);
        set(P, "br_up", "1");
        browse_wait(P);
        set(P, "br_row_2", "1");
        browse_wait(P);
        CHECK(row_is(P, 3, "A long kick..ame take 04") && row_is(P, 4, "> kick 1"), "a long name: %s", rows(P));
        set(P, "kit", "1");
        free(run(P, 1));
        CHECK(P->sfold[6] < 0, "a kit keeps X1's folder");
        E->destroy(P);

        /* past LIB_BYTES (64 MB): 48 files of 1.8 MB, each loaded into X1 and played, while X2 holds the first one,
         * looping, its pad held the whole time */
        for (int k = 0; k < 48; k++) {
            snprintf(path, sizeof path, "build/libtest/big/b%02d.wav", k);
            write_wav(path, 20 * 44100, 1, 16, 1, 44100, 0.3f);
        }
        P = E->create("build/libtest");
        browse_wait(P);
        set(P, "br_row_2", "1");
        browse_wait(P);
        set(P, "br_row_2", "1");   /* Sub */
        browse_wait(P);
        set(P, "br_up", "1");      /* back, around Sub */
        browse_wait(P);
        CHECK(row_is(P, 2, "Sub /"), "back up: %s", rows(P));
        set(P, "br_up", "1");
        browse_wait(P);
        /* (big isn't a place: browsed straight to) */
        E->set_param(P, "state", "kit=0;br_path=build/libtest/big;");
        browse_wait(P);
        set(P, "br_target", "7");   /* X2 */
        set(P, "x2_loopon", "1");
        set(P, "br_row_2", "1");    /* b00 */
        set(P, "br_load", "1");
        for (int b = 0; b < 400 && !P->sv[7].active; b++) free(run_rt(P));
        CHECK(P->sv[7].active, "X2 plays b00");
        free(run(P, 100));          /* PREVIEW's quarter-second tap over */
        midi(P, 0x90, 45, 100);     /* its pad, held from here on */
        set(P, "br_target", "6");   /* X1 */
        int played = 0;
        for (int k = 1; k < 48; k++) {
            seti(P, "br_scroll", k - 1 > 0 ? k - 1 : 0);
            int row = k + 2 - (int)P->pv[P_BR_SCROLL];   /* row 1 is ".." at the top */
            char key[32];
            snprintf(key, sizeof key, "br_row_%d", row);
            set(P, key, "1");
            set(P, "br_load", "1");
            CHECK(P->pv[P_X1_SAMPLE] == k + 1, "b%02d: SAMPLE %g (row %d)", k, P->pv[P_X1_SAMPLE], row);
            for (int b = 0; b < 400 && P->aud; b++) free(run_rt(P));
            played += P->sv[6].active && P->sv[6].s.len == 20 * 44100;
            free(run(P, 8));
            CHECK(P->sv[7].active && P->sv[7].s.d, "X2 stopped at b%02d", k);
        }
        o = run(P, 50);
        CHECK(energy(o, 50 * B) > 0 && played >= 40, "played %d of 47", played);
        free(o);
        E->destroy(P);
        fail = system("rm -rf build/libtest");
        printf("ok   your own samples: 5 kinds of WAV, a folder in name order, browsed (the cursor: taps, FILE, LOAD, lit rows, "
               "PREVIEW from the folder, LOAD TO's folder, long names), loaded, played, AUDITION, by name in the state; "
               "past 64 MB, 47 files played while X2 held one\n");
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
