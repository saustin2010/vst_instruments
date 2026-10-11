/* procwatch: what a VST2 .so does behind the scenes, hosted the way MPC hosts it (44100 Hz, 128-frame blocks).
 * Built from the framework's tools/bench.c host. Runs the shipped 32-bit ARM .so under emulation (emulated.sh), a
 * native build (native.sh), or on the MPC itself (device.sh). dev-tools/procwatch/README.md has the how-to.
 *
 *   procwatch <plugin.so> [stage seconds scale, default 1.0]
 *   env: RELEASE=secs (wait before "after", default 4), CYCLES=n (n more inserts + removes, heap after each),
 *        FTZ=1 (flush denormals to zero on the audio thread)
 *
 * Stages: load -> warm -> idle (transport stopped, no notes) -> idle-playing (transport running) -> notes (chords,
 * or noise into an effect) -> release (notes off, 4 s) -> after (silence again: compare with idle) -> a second
 * instance opened and closed -> the first closed. For each: the plugin's threads (new/gone, CPU ticks, wakeups),
 * open fds, malloc'd heap, host callbacks by opcode and from which thread, audio-thread CPU per block, output peak.
 * Also: whether the plugin changed the process environment (setenv: on MPC that is all of MPC's).
 * Last line: one JSON object. */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

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
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midi[4];
    char detune, offVel, r1, r2;
} MidiEv;
typedef struct {
    int32_t num;
    intptr_t reserved;
    void *ev[64];
} Events;
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStart, cycleEnd;
    int32_t tsNum, tsDen, smpte, smpteRate, toNextClock, flags;
} TimeInfo;

enum { SR = 44100, BLOCK = 128 };
enum { effOpen = 0, effClose = 1, effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
       effProcessEvents = 25, effStartProcess = 71, effStopProcess = 72 };
enum { FL_PLAYING = 2, FL_PPQ = 1 << 9, FL_TEMPO = 1 << 10, FL_TIMESIG = 1 << 13 };

/* ---- host callbacks, counted ---- */
static TimeInfo ti;
static int main_tid;
static const void *g_base;   /* the Snap before dlopen: its threads are the emulator's */
static long ncalls[64], ncalls_bg, ncalls_bg_ops[64];
static int gettid_(void) { return (int)syscall(SYS_gettid); }
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)i; (void)v; (void)p; (void)o;
    int k = op >= 0 && op < 63 ? op : 63;
    __atomic_add_fetch(&ncalls[k], 1, __ATOMIC_RELAXED);
    if (gettid_() != main_tid) { __atomic_add_fetch(&ncalls_bg, 1, __ATOMIC_RELAXED); __atomic_add_fetch(&ncalls_bg_ops[k], 1, __ATOMIC_RELAXED); }
    switch (op) {
    case 1: return 2400;               /* audioMasterVersion */
    case 7: return (intptr_t)&ti;      /* audioMasterGetTime */
    case 16: return SR;                /* audioMasterGetSampleRate */
    case 17: return BLOCK;             /* audioMasterGetBlockSize */
    default: return 0;
    }
}

/* ---- process snapshots ---- */
typedef struct { int tid; char comm[32]; long ticks, vcs, nvcs; } Th;
typedef struct { int n; Th t[256]; int fds; char fdname[256][96]; size_t heap; } Snap;

static int read_file(const char *path, char *buf, int max) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = (int)fread(buf, 1, max - 1, f);
    fclose(f);
    buf[n < 0 ? 0 : n] = 0;
    return n;
}
static void snap(Snap *s) {
    memset(s, 0, sizeof *s);
    DIR *d = opendir("/proc/self/task");
    struct dirent *de;
    while (d && (de = readdir(d)) && s->n < 256) {
        if (de->d_name[0] == '.') continue;
        Th *t = &s->t[s->n];
        t->tid = atoi(de->d_name);
        char path[64], buf[2048];
        snprintf(path, sizeof path, "/proc/self/task/%d/comm", t->tid);
        if (read_file(path, t->comm, sizeof t->comm) > 0) t->comm[strcspn(t->comm, "\n")] = 0;
        snprintf(path, sizeof path, "/proc/self/task/%d/stat", t->tid);
        if (read_file(path, buf, sizeof buf) > 0) {
            char *p = strrchr(buf, ')');
            long f[20] = {0};
            int k = 0;
            for (char *tok = p ? strtok(p + 2, " ") : 0; tok && k < 20; tok = strtok(0, " ")) f[k++] = atol(tok);
            t->ticks = f[11] + f[12];   /* utime + stime: fields 14, 15 */
        }
        snprintf(path, sizeof path, "/proc/self/task/%d/status", t->tid);
        if (read_file(path, buf, sizeof buf) > 0) {
            char *v = strstr(buf, "voluntary_ctxt_switches:");
            if (v) t->vcs = atol(v + 24);
            v = strstr(buf, "nonvoluntary_ctxt_switches:");
            if (v) t->nvcs = atol(v + 27);
        }
        s->n++;
    }
    if (d) closedir(d);
    d = opendir("/proc/self/fd");
    while (d && (de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        char path[64];
        snprintf(path, sizeof path, "/proc/self/fd/%s", de->d_name);
        if (s->fds < 256) {
            ssize_t n = readlink(path, s->fdname[s->fds], 95);
            s->fdname[s->fds][n > 0 ? n : 0] = 0;
        }
        s->fds++;
    }
    if (d) closedir(d);
    s->heap = mallinfo2().uordblks;
}
static const Th *find(const Snap *s, int tid) {
    for (int i = 0; i < s->n; i++) if (s->t[i].tid == tid) return &s->t[i];
    return 0;
}

/* ---- the plugin ---- */
static AEffect *(*entry)(hostcb);
static float inL[BLOCK], inR[BLOCK], outL[BLOCK], outR[BLOCK];
static float *ins[2] = {inL, inR}, *outs[2] = {outL, outR};
static MidiEv mev[64];
static Events evs;
static int synth;
static unsigned rng = 12345;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }
static double now_us(clockid_t c) { struct timespec t; clock_gettime(c, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static void send_notes(AEffect *fx, int on, int n, int base) {
    evs.num = 0;
    for (int k = 0; k < n && k < 64; k++) {
        MidiEv *m = &mev[k];
        memset(m, 0, sizeof *m);
        m->type = 1; m->byteSize = sizeof *m;
        m->midi[0] = on ? 0x90 : 0x80;
        m->midi[1] = (unsigned char)(base + (k * 7) % 36);
        m->midi[2] = on ? 100 : 0;
        evs.ev[evs.num++] = m;
    }
    fx->dispatcher(fx, effProcessEvents, 0, 0, &evs, 0);
}

static AEffect *open_instance(void) {
    AEffect *fx = entry(host);
    if (!fx || fx->magic != 0x56737450) return 0;
    fx->dispatcher(fx, effOpen, 0, 0, 0, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, 0, SR);
    fx->dispatcher(fx, effSetBlockSize, 0, BLOCK, 0, 0);
    fx->dispatcher(fx, effMainsChanged, 0, 1, 0, 0);
    fx->dispatcher(fx, effStartProcess, 0, 0, 0, 0);
    return fx;
}
static void close_instance(AEffect *fx) {
    fx->dispatcher(fx, effStopProcess, 0, 0, 0, 0);
    fx->dispatcher(fx, effMainsChanged, 0, 0, 0, 0);
    fx->dispatcher(fx, effClose, 0, 0, 0, 0);
}

/* JSON accumulates here */
static char js[1 << 16];
static int jlen;
#define J(...) (jlen += snprintf(js + jlen, sizeof js - jlen, __VA_ARGS__))
static void J_str(const char *s) { J("\""); for (; *s; s++) if (*s != '"' && *s != '\\' && (unsigned char)*s >= 32) J("%c", *s); J("\""); }

/* ---- the environment: a plugin's setenv changes it for the whole host process ---- */
extern char **environ;
static char *env0[512];
static int nenv0;
static void env_save(void) { for (int i = 0; environ[i] && nenv0 < 512; i++) env0[nenv0++] = strdup(environ[i]); }
static int env_had(const char *kv) { for (int i = 0; i < nenv0; i++) if (!strcmp(env0[i], kv)) return 1; return 0; }
static void env_report(void) {   /* JSON "env_changed": entries added or changed, then names removed */
    int first = 1;
    J("\"env_changed\":[");
    for (int i = 0; environ[i]; i++)
        if (!env_had(environ[i])) { printf("environment changed by the plugin: %s\n", environ[i]); if (!first) J(","); J_str(environ[i]); first = 0; }
    for (int i = 0; i < nenv0; i++) {
        char name[128]; size_t k = strcspn(env0[i], "=");
        snprintf(name, sizeof name, "%.*s", (int)(k < sizeof name ? k : sizeof name - 1), env0[i]);
        if (!getenv(name)) { printf("environment variable removed by the plugin: %s\n", name); if (!first) J(","); J_str(name); first = 0; }
    }
    J("],");
}

static void report_threads(const char *stage, const Snap *a, const Snap *b, double secs) {
    J("\"threads\":[");
    int first = 1;
    for (int i = 0; i < b->n; i++) {
        const Th *t = &b->t[i], *p = find(a, t->tid);
        if (t->tid == main_tid || find(g_base, t->tid)) continue;
        double cpu = (t->ticks - (p ? p->ticks : 0)) * 10.0 / 1000.0 / secs * 100.0;   /* % of a core (10 ms ticks) */
        double wake = (t->vcs - (p ? p->vcs : 0)) / secs;
        printf("  %-14s thread %-16s tid %-6d %s cpu %5.1f%%  wakeups %6.1f/s\n", stage, t->comm, t->tid,
               p ? "    " : "NEW ", cpu, wake);
        J("%s{\"comm\":\"%s\",\"new\":%d,\"cpu_pct\":%.2f,\"wakeups_per_s\":%.1f}", first ? "" : ",", t->comm, !p, cpu, wake);
        first = 0;
    }
    for (int i = 0; i < a->n; i++)
        if (!find(b, a->t[i].tid) && a->t[i].tid != main_tid && !find(g_base, a->t[i].tid)) printf("  %-14s thread %-16s tid %-6d GONE\n", stage, a->t[i].comm, a->t[i].tid);
    J("],");
}

static void report_calls(const char *stage, long *before, double secs) {
    J("\"host_calls_per_s\":{");
    int first = 1;
    for (int k = 0; k < 64; k++) {
        long d = ncalls[k] - before[k];
        if (!d) continue;
        printf("  %-14s host opcode %2d: %8.1f/s\n", stage, k, d / secs);
        J("%s\"%d\":%.1f", first ? "" : ",", k, d / secs);
        first = 0;
    }
    J("},");
}

/* mode: 0 silence, 1 notes (or noise into an effect), 2 release (notes off at the start, silence after) */
static void stage(AEffect *fx, const char *name, double secs, int playing, int mode) {
    int nblocks = (int)(secs * SR / BLOCK), restrike = SR / 2 / BLOCK, held = 0;
    double *t = malloc(sizeof(double) * nblocks);
    long before[64], bg0 = ncalls_bg;
    memcpy(before, ncalls, sizeof before);
    static Snap a, b;
    snap(&a);
    float peak = 0, peak_last = 0;
    ti.flags = FL_PPQ | FL_TEMPO | FL_TIMESIG | (playing ? FL_PLAYING : 0);
    double wall0 = now_us(CLOCK_MONOTONIC);
    for (int bl = 0; bl < nblocks; bl++) {
        double due = wall0 + bl * 1e6 * BLOCK / SR, nw = now_us(CLOCK_MONOTONIC);
        if (nw < due) { struct timespec ts = {0, (long)((due - nw) * 1000)}; nanosleep(&ts, 0); }
        for (int i = 0; i < BLOCK; i++) {
            float n = mode == 1 && !synth ? 1 : 0;
            inL[i] = n * (frand() * 0.5f - 0.25f); inR[i] = n * (frand() * 0.5f - 0.25f);
        }
        double t0 = now_us(CLOCK_THREAD_CPUTIME_ID);
        if (synth && mode == 1 && bl % restrike == 0) {
            if (held) send_notes(fx, 0, held, 36);
            held = 8;
            send_notes(fx, 1, held, 36);
        }
        if (synth && mode == 2 && bl == 0) send_notes(fx, 0, 16, 36);
        fx->processReplacing(fx, ins, outs, BLOCK);
        t[bl] = now_us(CLOCK_THREAD_CPUTIME_ID) - t0;
        for (int i = 0; i < BLOCK; i++) {
            float x = fabsf(outL[i]) > fabsf(outR[i]) ? fabsf(outL[i]) : fabsf(outR[i]);
            if (x > peak) peak = x;
            if (bl >= nblocks * 3 / 4 && x > peak_last) peak_last = x;
        }
        if (playing) { ti.samplePos += BLOCK; ti.ppqPos += BLOCK * ti.tempo / 60.0 / SR; }
    }
    if (synth && held) send_notes(fx, 0, held, 36);
    double wall = (now_us(CLOCK_MONOTONIC) - wall0) / 1e6;
    snap(&b);
    qsort(t, nblocks, sizeof(double), cmpd);
    double med = t[nblocks / 2], p99 = t[(int)(nblocks * 0.99)];
    printf("%-14s audio thread median %7.1f us/block p99 %7.1f, peak %.4f (last quarter %.5f), heap %+ld B, fds %+d, wall %.1fs\n",
           name, med, p99, peak, peak_last, (long)b.heap - (long)a.heap, b.fds - a.fds, wall);
    J("\"%s\":{\"audio_median_us\":%.1f,\"audio_p99_us\":%.1f,\"peak\":%.5f,\"peak_last_quarter\":%.5f,\"heap_delta\":%ld,"
      "\"fd_delta\":%d,\"host_calls_from_other_threads\":%ld,",
      name, med, p99, peak, peak_last, (long)b.heap - (long)a.heap, b.fds - a.fds, ncalls_bg - bg0);
    report_calls(name, before, secs);
    report_threads(name, &a, &b, wall);   /* thread CPU against the wall time this stage took */
    jlen--;  /* drop trailing comma */
    J("},");
    free(t);
}

static void fd_diff(const char *what, const Snap *a, const Snap *b) {
    for (int i = 0; i < b->fds && i < 256; i++) {
        int seen = 0;
        for (int j = 0; j < a->fds && j < 256; j++) if (!strcmp(a->fdname[j], b->fdname[i])) { seen = 1; break; }
        if (!seen && !strstr(b->fdname[i], "/proc/self")) printf("  %s: fd -> %s\n", what, b->fdname[i]);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: procwatch <plugin.so> [seconds scale]\n"); return 2; }
    const char *path = argv[1];
    double sc = argc > 2 ? atof(argv[2]) : 1.0;
    main_tid = gettid_();
    if (getenv("FTZ")) {   /* flush denormals to zero on this (the audio) thread: FPSCR/FPCR bit 24 */
#if defined(__arm__)
        unsigned r; __asm__ volatile("vmrs %0, fpscr" : "=r"(r)); r |= 1u << 24; __asm__ volatile("vmsr fpscr, %0" ::"r"(r));
#elif defined(__aarch64__)
        uint64_t r; __asm__ volatile("mrs %0, fpcr" : "=r"(r)); r |= 1u << 24; __asm__ volatile("msr fpcr, %0" ::"r"(r));
#endif
        printf("flush-to-zero on\n");
    }
    setvbuf(stdout, 0, _IOLBF, 0);
    static Snap s_base, s_dl, s_open, s_inst2, s_inst2_closed, s_closed, s_pre2;
    snap(&s_base);
    g_base = &s_base;
    env_save();
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    entry = (AEffect * (*)(hostcb)) dlsym(h, "VSTPluginMain");
    if (!entry) entry = (AEffect * (*)(hostcb)) dlsym(h, "main");
    if (!entry) { fprintf(stderr, "no VSTPluginMain\n"); return 1; }
    snap(&s_dl);
    ti.sampleRate = SR; ti.tempo = 120; ti.tsNum = 4; ti.tsDen = 4;
    double o0 = now_us(CLOCK_MONOTONIC);
    AEffect *fx = open_instance();
    if (!fx) { fprintf(stderr, "bad AEffect\n"); return 1; }
    double open_ms = (now_us(CLOCK_MONOTONIC) - o0) / 1e3;
    synth = (fx->flags & (1 << 8)) != 0;
    snap(&s_open);
    J("{\"plugin\":\"%s\",\"instrument\":%d,\"params\":%d,\"open_ms\":%.0f,", path, synth, fx->numParams, open_ms);
    printf("plugin %s: %s, %d params, open %.0f ms\n", path, synth ? "instrument" : "effect", fx->numParams, open_ms);
    J("\"threads_at_dlopen\":%d,\"threads_after_open\":%d,\"fds_after_open\":%d,", s_dl.n - s_base.n, s_open.n - s_base.n, s_open.fds - s_base.fds);
    printf("threads: %d before load, +%d at dlopen, +%d after open; fds +%d after open\n", s_base.n, s_dl.n - s_base.n,
           s_open.n - s_base.n, s_open.fds - s_base.fds);
    fd_diff("open", &s_base, &s_open);
    J("\"thread_names_after_open\":[");
    for (int i = 0, f = 1; i < s_open.n; i++)
        if (!find(&s_base, s_open.t[i].tid)) { J("%s\"%s\"", f ? "" : ",", s_open.t[i].comm); f = 0; }
    J("],");

    stage(fx, "warm", 0.5 * sc, 0, 0);
    stage(fx, "idle", 2.0 * sc, 0, 0);
    stage(fx, "idle_playing", 2.0 * sc, 1, 0);
    stage(fx, "notes", 2.0 * sc, 0, 1);
    stage(fx, "release", getenv("RELEASE") ? atof(getenv("RELEASE")) : 4.0 * sc, 0, 2);   /* RELEASE=secs: a longer wait before "after" */
    stage(fx, "after", 2.0 * sc, 0, 0);

    /* a second instance: per-instance threads, and what one insert + remove leaves behind */
    snap(&s_pre2);
    AEffect *fx2 = open_instance();
    snap(&s_inst2);
    if (fx2) {
        for (int b = 0; b < SR / 2 / BLOCK; b++) fx2->processReplacing(fx2, ins, outs, BLOCK);
        close_instance(fx2);
    }
    usleep(200000);
    snap(&s_inst2_closed);
    printf("second instance: threads %+d while open, %+d after its close; heap %+ld B, fds %+d after its close\n",
           s_inst2.n - s_pre2.n, s_inst2_closed.n - s_pre2.n, (long)s_inst2_closed.heap - (long)s_pre2.heap,
           s_inst2_closed.fds - s_pre2.fds);
    fd_diff("after 2nd close", &s_pre2, &s_inst2_closed);
    J("\"inst2\":{\"threads_open\":%d,\"threads_after_close\":%d,\"heap_after_close\":%ld,\"fds_after_close\":%d},",
      s_inst2.n - s_pre2.n, s_inst2_closed.n - s_pre2.n, (long)s_inst2_closed.heap - (long)s_pre2.heap,
      s_inst2_closed.fds - s_pre2.fds);

    /* CYCLES=n: n more inserts + removes, heap after each (a leak grows, a one-off cache doesn't) */
    int cycles = getenv("CYCLES") ? atoi(getenv("CYCLES")) : 0;
    if (cycles > 0) {
        static Snap c;
        J("\"cycle_heap\":[");
        for (int k = 0; k < cycles; k++) {
            AEffect *f = open_instance();
            if (f) { for (int b = 0; b < 8; b++) f->processReplacing(f, ins, outs, BLOCK); close_instance(f); }
            snap(&c);
            printf("cycle %d: heap %+ld B vs before the second instance, threads %+d\n", k + 1, (long)c.heap - (long)s_pre2.heap, c.n - s_pre2.n);
            J("%s%ld", k ? "," : "", (long)c.heap - (long)s_pre2.heap);
        }
        J("],");
    }

    close_instance(fx);
    usleep(200000);
    snap(&s_closed);
    printf("all closed: threads %+d vs before load, fds %+d, heap %+ld B vs after dlopen\n", s_closed.n - s_base.n,
           s_closed.fds - s_base.fds, (long)s_closed.heap - (long)s_dl.heap);
    for (int i = 0; i < s_closed.n; i++)
        if (!find(&s_base, s_closed.t[i].tid)) printf("  still running after close: %s (tid %d)\n", s_closed.t[i].comm, s_closed.t[i].tid);
    fd_diff("after all closed", &s_base, &s_closed);
    env_report();
    J("\"closed\":{\"threads\":%d,\"fds\":%d,\"heap\":%ld,\"left_threads\":[", s_closed.n - s_base.n, s_closed.fds - s_base.fds,
      (long)s_closed.heap - (long)s_dl.heap);
    for (int i = 0, f = 1; i < s_closed.n; i++)
        if (!find(&s_base, s_closed.t[i].tid)) { J("%s\"%s\"", f ? "" : ",", s_closed.t[i].comm); f = 0; }
    J("]}}");
    printf("JSON %s\n", js);
    return 0;
}
