/* How loud each of a plugin's presets plays, through the real plugin (wrapper + engine, x86): levels.sh links it with
 * the objects the offline test leaves in <plugin>/build (host_*.o, minus host_test's own), like dump_state.
 * For every program (MPC's PRESET menu) a fresh instance loads it and plays a phrase: note LOW held 1.5 s, then the
 * triad on HIGH held 1.5 s (a mono preset plays its top note), each released and ringing out. Prints one line per
 * preset: the loudest 400 ms (RMS, about what the ear calls loud), the peak, and the whole phrase's RMS, in dBFS.
 * A plugin with no presets is measured once, as it sounds when inserted. LEVELS_MAX=<n> measures only every k-th
 * preset, n in all (a quick survey of a plugin with hundreds).
 *   levels <low note> <high note> [<program> ...] */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
typedef struct { int32_t type,byteSize,deltaFrames,flags,noteLength,noteOffset; unsigned char m[4]; char x[4]; } ME;
typedef struct { int32_t n; intptr_t r; void* ev[4]; } EV;
extern AEffect* VSTPluginMain(cb);
enum { effClose = 1, effSetProgram = 2, effProcessEvents = 25, effGetProgramNameIndexed = 29 };

static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    static double ti[16];
    if (op == 7) { ti[4] = 120.0; ((int32_t*)&ti[8])[5] = 1 << 10; return (intptr_t)ti; }   /* time info: 120 BPM */
    return 0;
}

#define BLOCK 128
#define WIN (44100 * 4 / 10 / BLOCK)   /* 400 ms in blocks */
static double blk[44100 * 12 / BLOCK];   /* sum of squares per block */
static int nblk;
static double peak;

static void notes(AEffect *a, int on, const int *n, int count) {
    ME m[4]; EV ev = {count, 0, {0}};
    for (int k = 0; k < count; k++) {
        m[k] = (ME){1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)(on ? 0x90 : 0x80), (unsigned char)n[k], on ? 100 : 0, 0}};
        ev.ev[k] = &m[k];
    }
    a->d(a, effProcessEvents, 0, 0, &ev, 0);
}
static void run(AEffect *a, double secs) {
    float L[BLOCK], R[BLOCK], *o[2] = {L, R};
    for (int k = 0; k < (int)(secs * 44100 / BLOCK); k++) {
        a->pr(a, 0, o, BLOCK);
        double s = 0;
        for (int i = 0; i < BLOCK; i++) {
            s += L[i] * L[i] + R[i] * R[i];
            if (fabs(L[i]) > peak) peak = fabs(L[i]);
            if (fabs(R[i]) > peak) peak = fabs(R[i]);
        }
        if (nblk < (int)(sizeof blk / sizeof blk[0])) blk[nblk++] = s;
    }
}
static double db(double x) { return x > 1e-9 ? 20 * log10(x) : -180; }

int main(int argc, char **argv) {
    int lo = argc > 1 ? atoi(argv[1]) : 48, hi = argc > 2 ? atoi(argv[2]) : 60;
    AEffect *first = VSTPluginMain(host);
    if (!first) return 1;
    int n = first->np, every = 1, none = n == 0;
    first->d(first, effClose, 0, 0, 0, 0);
    if (none) n = 1;   /* no presets: the inserted state, once */
    if (getenv("LEVELS_MAX") && atoi(getenv("LEVELS_MAX")) > 0 && n > atoi(getenv("LEVELS_MAX")))
        every = (n + atoi(getenv("LEVELS_MAX")) - 1) / atoi(getenv("LEVELS_MAX"));
    for (int p = 0; p < n; p += every) {
        if (argc > 3) {   /* only the programs asked for */
            int want = 0;
            for (int k = 3; k < argc; k++) want |= atoi(argv[k]) == p;
            if (!want) continue;
        }
        AEffect *a = VSTPluginMain(host);
        char name[64] = "(as inserted)";
        if (!none) {
            a->d(a, effSetProgram, 0, p, 0, 0);
            a->d(a, effGetProgramNameIndexed, p, 0, name, 0);
        }
        nblk = 0; peak = 0;
        {   /* settle: some engines load a preset's files on a worker thread (Tablor's wavetables) */
            float L[BLOCK], R[BLOCK], *o[2] = {L, R};
            for (int k = 0; k < 40; k++) a->pr(a, 0, o, BLOCK);
            usleep(500000);
            for (int k = 0; k < 40; k++) a->pr(a, 0, o, BLOCK);
        }
        int low[1] = {lo}, tri[3] = {hi, hi + 4, hi + 7};
        notes(a, 1, low, 1); run(a, 1.5); notes(a, 0, low, 1); run(a, 1.0);
        notes(a, 1, tri, 3); run(a, 1.5); notes(a, 0, tri, 3); run(a, 2.0);
        double all = 0, best = 0;
        for (int k = 0; k < nblk; k++) all += blk[k];
        for (int k = 0; k + WIN <= nblk; k++) {
            double s = 0;
            for (int j = 0; j < WIN; j++) s += blk[k + j];
            if (s > best) best = s;
        }
        printf("%3d  %-24s loud %6.1f  peak %6.1f  rms %6.1f\n", p, name, db(sqrt(best / (WIN * BLOCK * 2))), db(peak),
               db(sqrt(all / (nblk * BLOCK * 2.0))));
        fflush(stdout);
        a->d(a, effClose, 0, 0, 0, 0);
    }
    return 0;
}
