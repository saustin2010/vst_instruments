/* Stress test for a port built on wrapper/vst2_wrap.c (steve/tools/fuzz, 2026-10-02): the things a user does on the
 * MPC, fast and at random, under ASan/UBSan, to shake out crashes before a plugin reaches the device.
 *   fuzz [seed] [blocks] [threads]      (fuzz_port.sh builds and runs it; threads=1: MPC's own split)
 * Each round: random parameter values (extremes, random, Q-Link-sized ticks around the current one, triggers,
 * steppers and pop-ups), every display/name read, MIDI (notes, chords, all-notes-off, pitch bend, CCs), state save
 * on one instance and restore on another, and an occasional instance removed and inserted again. With threads=1
 * the audio (MIDI + process) runs on one thread and everything else on another, as MPC calls a plugin: the screen
 * and project load on its message thread while the audio thread renders. Exit 0 if it survives; NaN/Inf output and
 * per-instance peak are reported. A crash or sanitizer report names the engine's file and line. FUZZ_TRACE=1 logs
 * every action to stderr (fuzz_port.sh passes it into the container), so the lines before a crash show its cause. */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"

typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
 void *p; void (*setP)(AEffect *, int32_t, float); float (*getP)(AEffect *, int32_t);
 int32_t np, npar, ni, no, flags; intptr_t r1, r2; int32_t a, b, c; float io; void *obj, *user; int32_t uid, ver;
 void (*pr)(AEffect *, float **, float **, int32_t); void *pdr; char f[56]; };
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char m[4]; char x[4]; } ME;
typedef struct { int32_t n; intptr_t r; void *ev[16]; } EV;
extern AEffect *VSTPluginMain(cb);

static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    static double ti[16];
    (void)e; (void)i; (void)v; (void)p; (void)o;
    if (op == 7) { ti[4] = 120.0; ti[3] = 0; ((int32_t *)&ti[8])[5] = (1 << 10) | (1 << 9) | (1 << 1); return (intptr_t)ti; }
    return 0;
}

static uint64_t rng_state = 88172645463325252ull;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return (uint32_t)rng_state; }
static float frand(void) { return (rnd() & 0xFFFFFF) / (float)0xFFFFFF; }

#define NI 3
static AEffect *inst[NI];
static long nan_blocks, blocks_done;
static float peak[NI];
static volatile int stop;
static int trace;   /* FUZZ_TRACE=1: log every action to stderr, so the last lines before a crash say what led to it */
#define T(...) do { if (trace) fprintf(stderr, __VA_ARGS__); } while (0)
static pthread_mutex_t inst_lock = PTHREAD_MUTEX_INITIALIZER;   /* an instance swap must not race its own use */

static AEffect *open_inst(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, 0, 0);            /* open */
    a->d(a, 10, 0, 0, 0, 44100.f);     /* sample rate */
    a->d(a, 11, 0, 128, 0, 0);         /* block size */
    a->d(a, 12, 0, 1, 0, 0);           /* resume */
    return a;
}

static void midi(AEffect *a) {
    ME ev[16];
    EV list;
    int n = 0, kind = rnd() % 10;
    memset(ev, 0, sizeof ev);
    if (kind < 4) {   /* a note or a chord on, sometimes off */
        int notes = 1 + rnd() % 6, base = 24 + rnd() % 72, on = rnd() % 4 != 0;
        for (int k = 0; k < notes && n < 16; k++, n++) {
            ev[n].type = 1; ev[n].byteSize = sizeof(ME); ev[n].deltaFrames = rnd() % 128;
            ev[n].m[0] = on ? 0x90 : 0x80; ev[n].m[1] = (unsigned char)(base + k * (1 + rnd() % 5)) & 127;
            ev[n].m[2] = on ? (unsigned char)(1 + rnd() % 127) : 0;
        }
    } else if (kind < 6) {   /* pitch bend */
        ev[0].type = 1; ev[0].byteSize = sizeof(ME); ev[0].m[0] = 0xE0;
        int b = rnd() % 3 == 0 ? (rnd() % 2 ? 0 : 16383) : (int)(rnd() % 16384);
        ev[0].m[1] = b & 127; ev[0].m[2] = (b >> 7) & 127; n = 1;
    } else if (kind < 8) {   /* a CC: mod wheel, sustain, or random */
        ev[0].type = 1; ev[0].byteSize = sizeof(ME); ev[0].m[0] = 0xB0;
        int c = rnd() % 3; ev[0].m[1] = c == 0 ? 1 : c == 1 ? 64 : rnd() % 120; ev[0].m[2] = rnd() % 128; n = 1;
    } else if (kind < 9) {   /* all notes off */
        ev[0].type = 1; ev[0].byteSize = sizeof(ME); ev[0].m[0] = 0xB0; ev[0].m[1] = 123; n = 1;
    } else {   /* every note off */
        for (int k = 0; k < 16; k++, n++) { ev[n].type = 1; ev[n].byteSize = sizeof(ME); ev[n].m[0] = 0x80; ev[n].m[1] = 36 + k * 5; }
    }
    list.n = n; list.r = 0;
    T("midi %02X %d %d (+%d)\n", ev[0].m[0], ev[0].m[1], ev[0].m[2], n - 1);
    for (int k = 0; k < n; k++) list.ev[k] = &ev[k];
    a->d(a, 25, 0, 0, &list, 0);
}

static void audio_block(int i) {
    float L[128], R[128], *o[2] = {L, R};
    AEffect *a = inst[i];
    if (rnd() % 3 == 0) midi(a);
    T("render inst %d\n", i);
    a->pr(a, 0, o, 128);
    int bad = 0;
    for (int k = 0; k < 128; k++) {
        if (!isfinite(L[k]) || !isfinite(R[k])) bad = 1;
        else { float m = fabsf(L[k]) > fabsf(R[k]) ? fabsf(L[k]) : fabsf(R[k]); if (m > peak[i]) peak[i] = m; }
    }
    nan_blocks += bad;
}

static int focus = -1;   /* a parameter half the control steps go to (argv[4], its key) */
static void control_step(int i) {
    AEffect *a = inst[i];
    char buf[512];
    int p = NPARAMS ? (int)(rnd() % NPARAMS) : 0, what = rnd() % 16;
    if (focus >= 0 && rnd() % 2) { p = focus; what = rnd() % 9; }
    if (!NPARAMS) return;
    if (what < 6) {   /* a value: an end, the middle, or anywhere */
        float v = what == 0 ? 0.f : what == 1 ? 1.f : what == 2 ? 0.5f : frand();
        T("inst %d set %s %.4f\n", i, PARAMS[p].key, v);
        a->setP(a, p, v);
    } else if (what < 9) {   /* a slow Q-Link turn: small ticks re-reading in between */
        float v = a->getP(a, p);
        T("inst %d turn %s from %.4f\n", i, PARAMS[p].key, v);
        for (int k = 0; k < 8; k++) { v += (rnd() % 2 ? 1 : -1) * 0.003f; a->setP(a, p, v < 0 ? 0 : v > 1 ? 1 : v); v = a->getP(a, p); }
    } else if (what < 12) {   /* the screen reading names and values */
        a->d(a, 7, p, 0, buf, 0);   /* display */
        a->d(a, 8, p, 0, buf, 0);   /* name */
        a->d(a, 6, p, 0, buf, 0);   /* label */
        (void)a->getP(a, p);
    } else if (what < 14) {   /* a trigger / stepper / preset: fire it, as a touch does */
        for (int k = 0; k < NPARAMS; k++) {
            int q = (p + k) % NPARAMS;
            if (PARAMS[q].momentary || PARAMS[q].step_target >= 0) { T("inst %d fire %s\n", i, PARAMS[q].key); a->setP(a, q, 1.f); a->setP(a, q, 0.f); break; }
        }
    } else {   /* state save here, restore on another instance (project save / load) */
        void *chunk = 0;
        intptr_t n = a->d(a, 23, 0, 0, &chunk, 0);
        if (n > 0 && chunk) {
            char *copy = malloc((size_t)n);
            memcpy(copy, chunk, (size_t)n);
            AEffect *b = inst[(i + 1) % NI];
            T("inst %d state -> inst %d (%ld bytes)\n", i, (i + 1) % NI, (long)n);
            b->d(b, 24, 0, n, copy, 0);
            free(copy);
        }
    }
}

static int threaded, total_blocks;
static void *audio_thread(void *arg) {
    (void)arg;
    for (int b = 0; b < total_blocks && !stop; b++) {
        pthread_mutex_lock(&inst_lock);
        for (int i = 0; i < NI; i++) audio_block(i);
        pthread_mutex_unlock(&inst_lock);
        blocks_done++;
    }
    stop = 1;
    return 0;
}

static void swap_instance(void) {   /* the plugin removed from a track and inserted again */
    int i = rnd() % NI;
    T("swap inst %d\n", i);
    pthread_mutex_lock(&inst_lock);
    inst[i]->d(inst[i], 1, 0, 0, 0, 0);   /* close (frees it) */
    inst[i] = open_inst();
    pthread_mutex_unlock(&inst_lock);
}

int main(int argc, char **argv) {
    uint64_t seed = argc > 1 ? strtoull(argv[1], 0, 10) : 1;
    total_blocks = argc > 2 ? atoi(argv[2]) : 3000;
    threaded = argc > 3 ? atoi(argv[3]) : 0;
    trace = getenv("FUZZ_TRACE") != NULL;
    rng_state ^= seed * 0x9E3779B97F4A7C15ull;
    for (int k = 0; argc > 4 && k < NPARAMS; k++) if (!strcmp(PARAMS[k].key, argv[4])) focus = k;
    for (int i = 0; i < NI; i++) inst[i] = open_inst();
    printf("fuzz %s: seed %llu, %d blocks x %d instances, %s\n", PLUG_NAME, (unsigned long long)seed, total_blocks, NI,
           threaded ? "audio and control on two threads (as MPC)" : "one thread");
    fflush(stdout);
    if (threaded) {
        pthread_t t;
        pthread_create(&t, 0, audio_thread, 0);
        long steps = 0;
        while (!stop) {   /* the message thread: no lock, like MPC (the instance lock only guards swaps) */
            control_step(rnd() % NI);
            if (++steps % 4000 == 0) swap_instance();
        }
        pthread_join(t, 0);
        printf("control steps %ld\n", steps);
    } else {
        for (int b = 0; b < total_blocks; b++) {
            for (int i = 0; i < NI; i++) {
                for (int k = rnd() % 4; k > 0; k--) control_step(i);
                audio_block(i);
            }
            if (b % 700 == 699) swap_instance();
            blocks_done++;
        }
    }
    for (int i = 0; i < NI; i++) inst[i]->d(inst[i], 1, 0, 0, 0, 0);
    printf("blocks %ld, blocks with NaN/Inf %ld, peak %.2f %.2f %.2f\n", blocks_done, nan_blocks, peak[0], peak[1], peak[2]);
    printf("SURVIVED\n");
    return 0;
}
