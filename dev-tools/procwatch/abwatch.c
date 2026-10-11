/* abwatch: does a plugin keep working when it has nothing to play? Two instances of one plugin, processed in turn
 * every block (two MPC tracks), so both are timed on the same core at the same moment (a VM's cores change speed):
 *   phase "play":  A plays chords (or noise into an effect), B never plays   -> idle cost as a share of playing cost
 *   (RELEASE s):   A's notes off, both silent, not timed
 *   phase "after": both silent                                               -> released A vs never-played B
 *   abwatch <plugin.so>        env RELEASE=secs (default 6)
 * Prints one line: AB <name> idle/play <x> after/fresh <y> ... and each median as a share of one block's budget
 * (2902 us): meaningful only on the device (device.sh); on a Mac or under emulation only the ratios are. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct AEffect AEffect;
typedef intptr_t (*hostcb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void *process;
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t r1, r2;
    int32_t initialDelay, rOld1, rOld2;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void *processDoubleReplacing;
    char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char midi[4]; char detune, offVel, r1, r2; } MidiEv;
typedef struct { int32_t num; intptr_t reserved; void *ev[64]; } Events;
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStart, cycleEnd;
    int32_t tsNum, tsDen, smpte, smpteRate, toNextClock, flags;
} TimeInfo;
enum { SR = 44100, BLOCK = 128 };

static TimeInfo ti;
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)i; (void)v; (void)p; (void)o;
    return op == 1 ? 2400 : op == 7 ? (intptr_t)&ti : op == 16 ? SR : op == 17 ? BLOCK : 0;
}
static double now_us(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static double median(double *t, int n) { qsort(t, n, sizeof *t, cmpd); return t[n / 2]; }

static AEffect *(*entry)(hostcb);
static float inL[BLOCK], inR[BLOCK], zero[BLOCK], outL[BLOCK], outR[BLOCK];
static float *ins[2] = {inL, inR}, *zins[2] = {zero, zero}, *outs[2] = {outL, outR};
static int synth;
static unsigned rng = 12345;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

static void notes(AEffect *fx, int on) {
    static MidiEv mev[8];
    static Events evs;
    evs.num = 0;
    for (int k = 0; k < 8; k++) {
        MidiEv *m = &mev[k];
        memset(m, 0, sizeof *m);
        m->type = 1; m->byteSize = sizeof *m;
        m->midi[0] = on ? 0x90 : 0x80; m->midi[1] = (unsigned char)(36 + (k * 7) % 36); m->midi[2] = on ? 100 : 0;
        evs.ev[evs.num++] = m;
    }
    fx->dispatcher(fx, 25, 0, 0, &evs, 0);
}
static AEffect *open_instance(void) {
    AEffect *fx = entry(host);
    if (!fx || fx->magic != 0x56737450) return 0;
    fx->dispatcher(fx, 0, 0, 0, 0, 0);
    fx->dispatcher(fx, 10, 0, 0, 0, SR);
    fx->dispatcher(fx, 11, 0, BLOCK, 0, 0);
    fx->dispatcher(fx, 12, 0, 1, 0, 0);
    fx->dispatcher(fx, 71, 0, 0, 0, 0);
    return fx;
}

/* one timed block of fx; returns its thread-CPU us and folds its output peak into *peak */
static double block(AEffect *fx, int noise, float *peak) {
    for (int i = 0; i < BLOCK; i++) { inL[i] = noise ? frand() * 0.5f - 0.25f : 0; inR[i] = noise ? frand() * 0.5f - 0.25f : 0; }
    double t0 = now_us(CLOCK_THREAD_CPUTIME_ID);
    fx->processReplacing(fx, noise ? ins : zins, outs, BLOCK);
    double t = now_us(CLOCK_THREAD_CPUTIME_ID) - t0;
    for (int i = 0; i < BLOCK; i++) { float x = fmaxf(fabsf(outL[i]), fabsf(outR[i])); if (x > *peak) *peak = x; }
    return t;
}

/* `secs` of blocks paced to real time; A plays if a_plays, B never does. Times go to ta/tb when given. */
static void phase(AEffect *a, AEffect *b, double secs, int a_plays, double *ta, double *tb, float *pa, float *pb) {
    int n = (int)(secs * SR / BLOCK), restrike = SR / 2 / BLOCK;
    double t0 = now_us(CLOCK_MONOTONIC);
    float junk;
    for (int k = 0; k < n; k++) {
        double due = t0 + k * 1e6 * BLOCK / SR, nw = now_us(CLOCK_MONOTONIC);
        if (nw < due) { struct timespec ts = {0, (long)((due - nw) * 1000)}; nanosleep(&ts, 0); }
        if (a_plays && synth && k % restrike == 0) { if (k) notes(a, 0); notes(a, 1); }
        int noise = a_plays && !synth;
        double x, y;
        if (k & 1) { x = block(a, noise, pa ? pa : &junk); y = block(b, 0, pb ? pb : &junk); }
        else { y = block(b, 0, pb ? pb : &junk); x = block(a, noise, pa ? pa : &junk); }
        if (ta) ta[k] = x;
        if (tb) tb[k] = y;
        ti.samplePos += BLOCK;
    }
    if (a_plays && synth) notes(a, 0);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    entry = (AEffect * (*)(hostcb)) dlsym(h, "VSTPluginMain");
    if (!entry) return 1;
    double rel = getenv("RELEASE") ? atof(getenv("RELEASE")) : 6.0, secs = 3.0;
    ti.sampleRate = SR; ti.tempo = 120; ti.tsNum = 4; ti.tsDen = 4; ti.flags = 1 << 9 | 1 << 10 | 1 << 13;
    AEffect *a = open_instance(), *b = open_instance();
    if (!a || !b) return 1;
    synth = (a->flags & (1 << 8)) != 0;
    int n = (int)(secs * SR / BLOCK);
    double *tap = malloc(n * sizeof(double)), *tbi = malloc(n * sizeof(double)), *taa = malloc(n * sizeof(double)), *tbf = malloc(n * sizeof(double));
    float pplay = 0, pafter = 0, pfresh = 0;
    phase(a, b, 0.5, 0, 0, 0, 0, 0);                    /* warm */
    phase(a, b, secs, 1, tap, tbi, &pplay, 0);         /* A plays, B idle */
    phase(a, b, rel, 0, 0, 0, 0, 0);                   /* release, untimed */
    phase(a, b, secs, 0, taa, tbf, &pafter, &pfresh);  /* both silent: released A vs fresh B */
    double play = median(tap, n), idle = median(tbi, n), after = median(taa, n), fresh = median(tbf, n);
    const char *name = strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1];
    double budget = 1e6 * BLOCK / SR;
    printf("AB %-18s idle/play %5.2f  after/fresh %5.2f  | play %7.1f idle %7.1f after %7.1f fresh %7.1f us  peak play %.3f after %.5f fresh %.5f"
           "  | %% of block: play %.1f idle %.1f after %.1f\n",
           name, idle / play, after / fresh, play, idle, after, fresh, pplay, pafter, pfresh,
           100 * play / budget, 100 * idle / budget, 100 * after / budget);
    return 0;
}
