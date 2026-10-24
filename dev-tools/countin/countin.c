/* MPC's count-in replayed through a sequencer (wrapper + mpc/schwung_midi_fx.c + module, x86): run.sh links it with
 * the objects the offline test leaves in <plugin>/build, like dev-tools/presets/levels.c. The transport is what MPC
 * reported on a Live II (docs/NOTES.md, 2026-10-24): stopped (ppq 0); for a count-in, playing with the recording flag
 * while the position runs on from 1.43 through one bar, then a jump back to 0 and normal playback. Each case is a fresh
 * instance with its COUNT-IN set; markers go to stderr between the adapter's "midi out" lines (MIDIFX_DEBUG=1), and
 * run.sh's awk says whether MIDI went out before the real start.
 *   countin <count_in param index> */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
 void *p; void (*setP)(AEffect *, int32_t, float); float (*getP)(AEffect *, int32_t);
 int32_t np, npar, ni, no, flags; intptr_t r1, r2; int32_t a, b, c; float io; void *obj, *user; int32_t uid, ver;
 void (*pr)(AEffect *, float **, float **, int32_t); void *pdr; char f[56]; };
typedef struct { double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
                 int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags; } TimeInfo;
extern AEffect *VSTPluginMain(cb);
enum { effOpen = 0, effClose = 1, effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12 };
enum { STOPPED = 0x7fc4, PLAYING = 0x7fc6, RECORDING = 0x7fce };   /* the flags MPC sent */

#define BLOCK 128
#define BEAT (BLOCK * 126.0 / 60.0 / 44100.0)   /* quarter notes per block at 126 BPM */
static TimeInfo ti = {0, 44100, 0, 0, 126, 0, 0, 0, 4, 4, 0, 0, 0, STOPPED};
static double since = -1;   /* beats since the real start (a marker every beat), -1 before it */
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)i; (void)v; (void)p; (void)o;
    if (op == 1) return 2400;
    if (op == 7) return (intptr_t)&ti;
    return 0;
}
static void blocks(AEffect *a, int n) {
    static float L[BLOCK], R[BLOCK];
    float *out[2] = {L, R};
    for (int k = 0; k < n; k++) {
        a->pr(a, 0, out, BLOCK);
        if (ti.flags != STOPPED) { ti.ppqPos += BEAT; ti.samplePos += BLOCK; }
        if (since >= 0) {
            double was = since;
            since += BEAT;
            if ((long)since != (long)was) { fprintf(stderr, "== beat %ld\n", (long)since); fflush(stderr); }
        }
    }
}
static void mark(const char *m) {
    if (!strcmp(m, "real start")) since = 0;
    if (!strcmp(m, "stopping")) since = -1;
    fprintf(stderr, "== %s\n", m); fflush(stderr);
}
static void stop(AEffect *a) { ti.flags = STOPPED; ti.ppqPos = 0; ti.samplePos = 0; blocks(a, 40); }
/* one bar of count-in from 1.43 (as measured), then the jump back to 0 */
static void count_in(AEffect *a, int flags) {
    ti.flags = flags; ti.ppqPos = 1.43; ti.samplePos = 30035;
    blocks(a, (int)(4.0 / BEAT));
    ti.ppqPos = 0; ti.samplePos = 0;
}

int main(int argc, char **argv) {
    int idx = argc > 1 ? atoi(argv[1]) : -1;
    static const struct { const char *name; float count_in; int kind; } cases[] = {
        {"REC+PLAY, count-in with Rec + Play", 1.0f, 0},
        {"RECORD, count-in with Rec + Play", 0.5f, 0},
        {"REC+PLAY, count-in with plain Play", 1.0f, 1},
        {"RECORD, plain Play, no count-in", 0.5f, 2},
        {"OFF, count-in with Rec + Play", 0.0f, 0},
        {"REC+PLAY, plain Play, no count-in (no jump: the nine-beat limit)", 1.0f, 2},
        {"REC+PLAY, Stop during the count-in, then a count-in", 1.0f, 3},
    };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        AEffect *a = VSTPluginMain(host);
        if (!a) return 1;
        a->d(a, effOpen, 0, 0, 0, 0); a->d(a, effSetSampleRate, 0, 0, 0, 44100); a->d(a, effSetBlockSize, 0, BLOCK, 0, 0);
        a->d(a, effMainsChanged, 0, 1, 0, 0);
        if (idx < 0) idx = a->npar - 1;
        a->setP(a, idx, cases[c].count_in);
        fprintf(stderr, "== case %u: %s\n", c + 1, cases[c].name);
        stop(a);
        mark("play pressed");
        switch (cases[c].kind) {
        case 0: count_in(a, RECORDING); mark("real start"); blocks(a, (int)(8.0 / BEAT)); break;
        case 1: count_in(a, PLAYING); mark("real start"); blocks(a, (int)(8.0 / BEAT)); break;
        case 2: mark("real start"); ti.flags = PLAYING; blocks(a, (int)(12.0 / BEAT)); break;
        case 3: ti.flags = RECORDING; ti.ppqPos = 1.43; blocks(a, (int)(2.0 / BEAT)); stop(a);
                count_in(a, RECORDING); mark("real start"); blocks(a, (int)(8.0 / BEAT)); break;
        }
        mark("stopping");
        stop(a);
        a->d(a, effMainsChanged, 0, 0, 0, 0);
        a->d(a, effClose, 0, 0, 0, 0);
    }
    mark("end");
    return 0;
}
