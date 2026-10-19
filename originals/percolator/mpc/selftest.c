/* Percolator self-test (development): your own samples on every voice (WAV kinds and cue points, the SAMPLES tab's
 * browsing, a file loaded, previewed and played on each voice, its pitch, PARAM 1-2's names, SAMPLE through its folder,
 * by name in the saved state, a kit back to the algorithms, past the 64 MB of files while another voice holds one) and
 * RANDOMISE (each voice's own controls in their ranges, the rest as it was, MPC told, every roll heard), and every voice
 * stopping once its hit has died away. Built natively,
 * without the wrapper (from originals/percolator; LIB_MEDIA: a stand-in for the MPC's drives):
 *   cc -O1 -g -DLIB_MEDIA='"build/libtest/media"' -Isrc mpc/selftest.c src/percolator.c src/voices.c src/kits.c \
 *      src/library.c -lm -lpthread -ldl -o build/selftest
 *   build/selftest [rolls [seed]]   (rolls: RANDOMISE's per voice, 2000 to start with; seed: its dice, the same rolls
 *                                    each run unless given)
 * With -fsanitize=address,undefined (or thread) in Docker's gcc:12: Apple clang's ASan hangs on it. Prints a line per
 * check and PASSED at the end (exit 1 on the first failure). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "percolator.h"

typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);
int mpc_engine_changed(void *inst, char *key, int len);

#define B 128
#define VI(v, k) (P_V1_TUNE + VSTRIDE * (v) + (k))   /* voice v's control k: K_TUNE ..., 8 ALGO, 9 MODE, 10 VCF */
static const mpc_engine_t *E;
static char buf[8192], saved[8192];

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
static const char *get(void *I, const char *k) {
    static char b[256];
    b[0] = 0;
    E->get_param(I, k, b, sizeof b);
    return b;
}
static int pi(const char *key) {   /* a parameter's index */
    for (int i = 0; i < NP; i++)
        if (!strcmp(PDEF[i].key, key)) return i;
    CHECK(0, "no parameter %s", key);
    return -1;
}

static int16_t *run(void *I, int blocks) {
    int16_t *o = calloc((size_t)blocks * B * 2, sizeof *o);
    for (int b = 0; b < blocks; b++) E->render(I, o + 2 * B * b, B);
    return o;
}
static int16_t *run_rt(void *I) {   /* a block in real time: the library's thread reads at the MPC's pace */
    struct timespec ts = {0, 2900000};
    nanosleep(&ts, NULL);
    return run(I, 1);
}
static double energy(const int16_t *o, int n) {
    double e = 0;
    for (int i = 0; i < 2 * n; i++) e += (double)o[i] * o[i];
    return e;
}
static int peak(const int16_t *o, int n) {
    int p = 0;
    for (int i = 0; i < 2 * n; i++) p = abs(o[i]) > p ? abs(o[i]) : p;
    return p;
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

/* a 440 Hz sine at amp: fmt 1 PCM (8/16/24/32-bit), 3 float, 0xFFFE extensible (24-bit PCM); cues: cue points */
static void write_wav(const char *path, int frames, int ch, int bits, int fmt, int rate, float amp, const int *cues, int ncue) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int bpf = ch * bits / 8, ext = fmt == 0xFFFE, flen = ext ? 40 : 16;
    uint32_t data = (uint32_t)(frames * bpf), cue = ncue ? 12 + 24 * (uint32_t)ncue : 0;
    uint8_t h[80];
    int n = 0;
#define P4(v) (h[n] = (uint8_t)(v), h[n + 1] = (uint8_t)((v) >> 8), h[n + 2] = (uint8_t)((v) >> 16), h[n + 3] = (uint8_t)((v) >> 24), n += 4)
#define P2(v) (h[n] = (uint8_t)(v), h[n + 1] = (uint8_t)((v) >> 8), n += 2)
    memcpy(h + n, "RIFF", 4), n += 4;
    P4(4 + 8 + flen + 8 + data + cue);
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
    if (ncue) {   /* "cue ": a count, then 24 bytes a point (its sample offset last) */
        n = 0;
        memcpy(h + n, "cue ", 4), n += 4;
        P4(4 + 24 * ncue);
        P4(ncue);
        fwrite(h, 1, (size_t)n, f);
        for (int k = 0; k < ncue; k++) {
            n = 0;
            P4(k + 1), P4(cues[k]);
            memcpy(h + n, "data", 4), n += 4;
            P4(0), P4(0), P4(cues[k]);
            fwrite(h, 1, (size_t)n, f);
        }
    }
#undef P4
#undef P2
    fclose(f);
}

static int row_is(perc_t *P, int r, const char *want) {
    char k[32];
    snprintf(k, sizeof k, "br_row_%d_display", r);
    return !strcmp(get(P, k), want);
}
static const char *rows(perc_t *P) {
    static char all[600];
    all[0] = 0;
    for (int r = 1; r <= BROWSE_ROWS; r++) {
        char k[32];
        snprintf(k, sizeof k, "br_row_%d_display", r);
        snprintf(all + strlen(all), sizeof all - strlen(all), "[%s]", get(P, k));
    }
    return all;
}
static void browse_wait(perc_t *P) {   /* until the listing asked for is read and shown */
    for (int t = 0; t < 4000; t++) {
        get(P, "br_row_1_display");
        if (!P->bwait) return;
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
    CHECK(0, "no listing came");
}
static void play_wait(perc_t *P, int v) {   /* PREVIEW: until the voice plays its new file */
    for (int b = 0; b < 600 && !P->v[v].active; b++) free(run_rt(P));
    CHECK(P->v[v].active && P->v[v].src && P->v[v].smp && P->v[v].lslot >= 0, "V%d didn't play its new sample", v + 1);
}
static void tap(perc_t *P, const char *key) {   /* a touch: 1, then the release */
    set(P, key, "1");
    set(P, key, "0");
}

/* ---- RANDOMISE: rolls a voice and checks what moved (its own sound controls, in range) and that the roll is heard;
 * returns the hit's peak ---- */
static int roll(perc_t *P, int v, int *told_all) {
    char k[48];
    while (mpc_engine_changed(P, k, sizeof k)) {}
    float before[NP];
    memcpy(before, P->pv, sizeof before);
    snprintf(k, sizeof k, "v%d_rand", v + 1);
    tap(P, k);
    static const char *const OWN[] = {"tune", "decay", "p1", "p2", "cutoff", "drive", "algo", "mode", "vcf", "smp", NULL};
    int moved = 0;
    for (int i = 0; i < NP; i++) {
        if (P->pv[i] == before[i]) continue;
        moved++;
        const char *key = PDEF[i].key;
        int ok = key[0] == 'v' && key[1] == '1' + v && key[2] == '_';
        for (int j = 0; ok && OWN[j]; j++)
            if (!strcmp(key + 3, OWN[j])) ok = 2;
        CHECK(ok == 2, "V%d RANDOMISE moved %s", v + 1, key);
        CHECK(P->pv[i] == floorf(P->pv[i]) && P->pv[i] >= 0, "%s = %g", key, P->pv[i]);
        CHECK(PDEF[i].kind == 0 ? P->pv[i] <= 100 : P->pv[i] < PDEF[i].nopts, "%s = %g", key, P->pv[i]);
    }
    CHECK(P->pv[VI(v, K_DECAY)] < 99.0f, "V%d RANDOMISE made a drone", v + 1);
    int told = 0;
    while (mpc_engine_changed(P, k, sizeof k)) told++;
    CHECK(told >= moved, "V%d: %d moved, MPC told of %d", v + 1, moved, told);
    *told_all += told;
    if (P->v[v].active) midi(P, 0xB0, 120, 0);
    midi(P, 0x90, 36 + v, 100);
    int16_t *o = run(P, 172);   /* half a second */
    midi(P, 0x80, 36 + v, 0);
    int p = peak(o, 172 * B);
    free(o);
    midi(P, 0xB0, 120, 0);   /* all sound off: the next roll starts from silence */
    free(run(P, 2));
    return p;
}

int main(int argc, char **argv) {
    E = mpc_engine();
    int rolls = argc > 1 ? atoi(argv[1]) : 2000;
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 0x2f6b9e31u;
    int fail = system("rm -rf build/libtest && mkdir -p build/libtest/data/kicks/Sub build/libtest/big "
                      "build/libtest/media/az01-internal build/libtest/media/az01-internal-sd "
                      "build/libtest/media/acvs-content build/libtest/media/MyUSB");
    CHECK(!fail, "mkdir build/libtest");

    /* ---- WAV files of each kind read as the kit packs' are (kits.c wav_load), cue points as slices ---- */
    const struct { const char *name; int ch, bits, fmt, rate; } W[] = {
        {"kick 1.WAV", 1, 16, 1, 44100}, {"Kick 2.wav", 2, 24, 1, 48000}, {"Kick 10.wav", 1, 32, 3, 96000},
        {"snare.wav", 1, 8, 1, 22050}, {"Sub/inner.wav", 2, 24, 0xFFFE, 44100}, {"wide.wav", 6, 16, 1, 44100}};
    char path[512];
    for (size_t w = 0; w < sizeof W / sizeof W[0]; w++) {
        snprintf(path, sizeof path, "build/libtest/data/kicks/%s", W[w].name);
        write_wav(path, 4410, W[w].ch, W[w].bits, W[w].fmt, W[w].rate, 0.5f, NULL, 0);
        sample_t s;
        CHECK(wav_load(path, &s, 64L << 20, 60 * 48000), "%s doesn't load", W[w].name);
        float want = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * 100.0f / W[w].rate);
        CHECK(s.len == 4410 && s.rate == (float)W[w].rate && fabsf(s.d[100] - want) < 0.01f && s.d[s.len] == 0.0f,
              "%s: %d frames at %g, %g not %g", W[w].name, s.len, s.rate, s.d[100], want);
        free(s.d);
    }
    const int CUES[3] = {11025, 22050, 33075};   /* four slices of a second */
    write_wav("build/libtest/data/kicks/sliced.wav", 44100, 1, 16, 1, 44100, 0.5f, CUES, 3);
    {
        sample_t s;
        CHECK(wav_load("build/libtest/data/kicks/sliced.wav", &s, 64L << 20, 60 * 48000) && s.ncue == 5 &&
              s.cue[0] == 0 && s.cue[1] == 11025 && s.cue[3] == 33075 && s.cue[4] == 44100, "cue points: %d", s.ncue);
        free(s.d);
        CHECK(wav_load("build/libtest/data/kicks/sliced.wav", &s, 64L << 20, 1000) && s.len == 1000, "a minute at most");
        free(s.d);
    }
    FILE *f = fopen("build/libtest/data/kicks/notes.txt", "w");
    if (f) fclose(f);
    write_wav("build/libtest/data/kicks/.hidden.wav", 100, 1, 16, 1, 44100, 0.5f, NULL, 0);
    printf("ok   WAV: 8/16/24/32-bit, float, extensible, 1-6 channels; cue points as slices\n");

    /* ---- the SAMPLES tab: the places, a folder in name order, a file loaded into LOAD TO's voice and previewed ---- */
    perc_t *P = E->create("build/libtest/data");
    CHECK(P->nkits == P->nbuiltin, "a folder of WAV files read as kits");
    browse_wait(P);   /* the tab's first look: the places */
    CHECK(row_is(P, 1, "PERCOLATOR FOLDER") && row_is(P, 2, "INTERNAL") && row_is(P, 3, "MyUSB") && row_is(P, 4, " "),
          "places: %s", rows(P));
    CHECK(!strcmp(get(P, "br_path"), "DRIVES") && !strcmp(get(P, "br_status"), "PICK A DRIVE"), "path %s", get(P, "br_path"));
    tap(P, "br_row_1");
    browse_wait(P);
    CHECK(row_is(P, 1, ".. UP") && row_is(P, 2, "kicks /") && row_is(P, 3, " "), "data folder: %s", rows(P));
    tap(P, "br_row_2");
    browse_wait(P);
    CHECK(row_is(P, 1, ".. UP") && row_is(P, 2, "Sub /") && row_is(P, 3, "kick 1") && row_is(P, 4, "Kick 2") &&
          row_is(P, 5, "Kick 10") && row_is(P, 6, "sliced") && row_is(P, 7, "snare") && row_is(P, 8, "wide"),
          "folder: %s", rows(P));
    CHECK(!strcmp(get(P, "br_status"), "6 WAVS") && !strcmp(get(P, "br_path"), "PERCOLATOR/kicks"), "status %s, path %s",
          get(P, "br_status"), get(P, "br_path"));
    CHECK(P->pv[pi("v1_src")] == 0 && !strcmp(get(P, "v1_smp_display"), "NO SAMPLE"), "V1 starts on its algorithm");
    char k[48];
    while (mpc_engine_changed(P, k, sizeof k)) {}
    tap(P, "br_row_4");   /* Kick 2, into V1 (LOAD TO to start with), PREVIEW on */
    CHECK(P->sfold[0] >= 0 && P->pv[pi("v1_smp")] == 2 && P->pv[pi("v1_src")] == 1, "V1: folder %d, SAMPLE %g, SOURCE %g",
          P->sfold[0], P->pv[pi("v1_smp")], P->pv[pi("v1_src")]);
    int told_smp = 0, told_src = 0;
    while (mpc_engine_changed(P, k, sizeof k)) told_smp |= !strcmp(k, "v1_smp"), told_src |= !strcmp(k, "v1_src");
    CHECK(told_smp && told_src, "MPC not told of V1's SAMPLE and SOURCE");
    play_wait(P, 0);
    int16_t *o = run(P, 20);
    CHECK(energy(o, 20 * B) > 1e6, "PREVIEW is silent");
    free(o);
    CHECK(row_is(P, 4, "> Kick 2"), "the loaded file's mark: %s", rows(P));
    {
        const char *want[][2] = {{"v1_smp_display", "Kick 2"}, {"v1_p1_name", "V1 CRUSH"}, {"v1_p2_name", "V1 START"},
                                 {"v1_mode_name", "ALGO OFF: SAMPLE"}, {"v1_tune_display", "-5.8 st"}};
        for (size_t j = 0; j < sizeof want / sizeof want[0]; j++)
            CHECK(!strcmp(get(P, want[j][0]), want[j][1]), "%s reads %s, not %s", want[j][0], get(P, want[j][0]), want[j][1]);
    }
    /* SAMPLE steps through the folder, no further than its files */
    seti(P, "v1_smp", 3);
    CHECK(!strcmp(get(P, "v1_smp_display"), "Kick 10"), "SAMPLE 3 shows %s", get(P, "v1_smp_display"));
    seti(P, "v1_smp", 9);
    CHECK(P->pv[pi("v1_smp")] == 6, "SAMPLE past the folder's files: %g", P->pv[pi("v1_smp")]);
    seti(P, "v2_smp", 4);
    CHECK(P->pv[pi("v2_smp")] == 0, "SAMPLE with no folder: %g", P->pv[pi("v2_smp")]);
    printf("ok   SAMPLES tab: places, a folder in name order, a file loaded into V1 (SOURCE = SAMPLE), previewed, named\n");

    /* ---- every voice plays its own file: at its pitch (TUNE 50 = as recorded), pitched on its KEYS CH, sliced ---- */
    for (int v = 0; v < NV; v++) {
        char key[32];
        seti(P, "br_target", v);
        tap(P, "br_row_3");   /* kick 1: 440 Hz */
        CHECK(P->pv[pi("v1_src") + 5 * v] == 1 && P->pv[pi("v1_smp") + 5 * v] == 1, "V%d didn't load", v + 1);
        play_wait(P, v);
        free(run(P, 100));
        const char *kv[][2] = {{"tune", "50"}, {"decay", "100"}, {"cutoff", "100"}, {"drive", "0"}, {"vcf", "2"},
                               {"p1", "0"}, {"p2", "0"}, {"level", "85"}};
        for (size_t j = 0; j < sizeof kv / sizeof kv[0]; j++) {
            snprintf(key, sizeof key, "v%d_%s", v + 1, kv[j][0]);
            set(P, key, kv[j][1]);
        }
        midi(P, 0x90, 36 + v, 100);
        o = run(P, 30);
        float f0 = freq(o, 400, 30 * B);
        free(o);
        midi(P, 0xB0, 120, 0);
        CHECK(fabsf(f0 - 440.0f) < 3.0f, "V%d plays its file at %.1f Hz, not 440", v + 1, f0);
        midi(P, 0x91 + v, 72, 100);   /* its KEYS CH (2-5 to start with): C4 = an octave up (the file ends at 2205) */
        o = run(P, 30);
        float f1 = freq(o, 400, 2000);
        free(o);
        midi(P, 0xB0, 120, 0);
        CHECK(fabsf(f1 - 880.0f) < 6.0f, "V%d pitched an octave up: %.1f Hz", v + 1, f1);
        snprintf(key, sizeof key, "v%d_tune", v + 1);
        set(P, key, "75");   /* +12 semitones */
        midi(P, 0x90, 36 + v, 100);
        o = run(P, 30);
        f1 = freq(o, 400, 2000);
        free(o);
        midi(P, 0xB0, 120, 0);
        CHECK(fabsf(f1 - 880.0f) < 6.0f, "V%d TUNE 75: %.1f Hz", v + 1, f1);
        snprintf(key, sizeof key, "v%d_tune_display", v + 1);
        CHECK(!strcmp(get(P, key), "+12.0 st"), "TUNE reads %s", get(P, key));
    }
    /* the sliced file on V4: PARAM 2 picks the slice */
    seti(P, "br_target", 3);
    tap(P, "br_row_6");
    play_wait(P, 3);
    free(run(P, 100));
    CHECK(!strcmp(get(P, "v4_p2_name"), "V4 SLICE"), "V4 PARAM 2: %s", get(P, "v4_p2_name"));
    set(P, "v4_p2", "60");
    CHECK(!strcmp(get(P, "v4_p2_display"), "3 / 4"), "slice %s", get(P, "v4_p2_display"));
    midi(P, 0x90, 39, 100);
    CHECK(P->v[3].sstart == 22050 && P->v[3].send == 33075, "slice 3: %d..%d", P->v[3].sstart, P->v[3].send);
    midi(P, 0xB0, 120, 0);
    CHECK(P->v[3].lslot < 0 && !P->v[3].active, "all sound off: V4 still holds its file");
    /* back to SYNTH: the algorithm again */
    set(P, "v1_src", "0");
    midi(P, 0x90, 36, 100);
    CHECK(P->v[0].active && !P->v[0].src && P->v[0].lslot < 0, "V1 on SYNTH");
    midi(P, 0xB0, 120, 0);
    set(P, "v1_src", "1");
    printf("ok   every voice plays its own file: as recorded at TUNE 50, an octave on its KEYS CH and at TUNE 75; slices\n");

    /* ---- the saved state: each voice's folder and file by name; another instance finds them (and, in a session of
     * its own below, with a file added before it); a state from 2.0.1 ---- */
    {
        seti(P, "v1_smp", 5);   /* snare */
        set(P, "v2_decay", "33");
        int n = E->get_param(P, "state", saved, sizeof saved);
        CHECK(n > 0 && n < 8192 && strstr(saved, "v1_dir=build/libtest/data/kicks;") && strstr(saved, "v1_file=snare.wav;") &&
              strstr(saved, "v4_file=sliced.wav;") && strstr(saved, "br_path=build/libtest/data/kicks;"), "state: %s", saved);
        perc_t *Q = E->create("build/libtest/data");
        E->set_param(Q, "state", saved);
        lib_wait();
        free(run(Q, 2));
        CHECK(Q->sfold[0] >= 0 && Q->pv[pi("v1_smp")] == 5 && Q->pv[pi("v1_src")] == 1 && Q->pv[pi("v2_decay")] == 33 &&
              !strcmp(get(Q, "v1_smp_display"), "snare") && !strcmp(get(Q, "v4_smp_display"), "sliced"),
              "restored: %g", Q->pv[pi("v1_smp")]);
        /* a state as 2.0.1 saved it (no SOURCE, SAMPLE, folders): the algorithms, and nothing of the last state */
        snprintf(buf, sizeof buf, "%s", saved);
        char *cut = strstr(buf, "v1_src=");
        CHECK(cut, "v1_src in the state");
        *cut = 0;
        E->set_param(Q, "state", buf);
        free(run(Q, 2));
        for (int v = 0; v < NV; v++)
            CHECK(Q->pv[pi("v1_src") + 5 * v] == 0 && Q->pv[pi("v1_smp") + 5 * v] == 0 && Q->sfold[v] < 0,
                  "a 2.0.1 state left V%d on its sample", v + 1);
        CHECK(Q->pv[pi("v2_decay")] == 33 && !Q->bpath[0], "2.0.1's values");
        E->destroy(Q);
        printf("ok   state: %d bytes, each voice's folder and file by name; a 2.0.1 state plays the algorithms\n", n);
    }

    /* ---- a kit: every voice back to its algorithm, each keeping its file for SOURCE = SAMPLE again ---- */
    set(P, "kit", "1");
    free(run(P, 1));
    for (int v = 0; v < NV; v++) CHECK(P->pv[pi("v1_src") + 5 * v] == 0 && P->sfold[v] >= 0, "a kit: V%d", v + 1);
    set(P, "v2_src", "1");
    CHECK(!strcmp(get(P, "v2_smp_display"), "kick 1"), "V2's file after a kit: %s", get(P, "v2_smp_display"));
    printf("ok   a kit turns every voice back to SYNTH; each keeps its file\n");

    /* ---- RANDOMISE ---- */
    {
        int told = 0, soft[NV][4], quiet = 0, soft30 = 0;   /* under -36 dBFS; under -30 */
        for (int v = 0; v < NV; v++)
            for (int a = 0; a < 4; a++) soft[v][a] = 1 << 30;
        perc_t *Q = E->create("build/libtest/data");
        Q->rrng = seed ? seed : 1;
        for (int v = 0; v < NV; v++) {
            float keep[NP];
            memcpy(keep, Q->pv, sizeof keep);
            for (int r = 0; r < rolls; r++) {
                int p = roll(Q, v, &told), a = (int)Q->pv[VI(v, 8)];
                if (p < soft[v][a]) soft[v][a] = p;
                soft30 += p < 1036;
                if (p < 517) {
                    quiet++;
                    if (quiet < 20)
                        printf("     quiet: V%d algo %d mode %g vcf %g tune %g decay %g p1 %g p2 %g cut %g drive %g: peak %d\n",
                               v + 1, a, Q->pv[VI(v, 9)], Q->pv[VI(v, 10)], Q->pv[VI(v, K_TUNE)], Q->pv[VI(v, K_DECAY)],
                               Q->pv[VI(v, K_P1)], Q->pv[VI(v, K_P2)], Q->pv[VI(v, K_CUTOFF)], Q->pv[VI(v, K_DRIVE)], p);
                }
            }
            /* the voice's mix, MIDI and LFO depths as they were */
            const char *same[] = {"level", "fx", "comp", "note", "kch", "out", "wheel", "gp1", "m_tune", "m_p1", "src", NULL};
            for (int j = 0; same[j]; j++) {
                char key[32];
                snprintf(key, sizeof key, "v%d_%s", v + 1, same[j]);
                CHECK(Q->pv[pi(key)] == keep[pi(key)], "RANDOMISE moved %s", key);
            }
        }
        /* on SOURCE = SAMPLE: a file of its folder (its six, all read first: a roll's file not in memory yet is
         * read meanwhile, and that one hit has none), heard */
        set(Q, "state", "kit=0;v3_src=1;v3_smp=1;v3_dir=build/libtest/data/kicks;v3_file=kick 1.WAV;");
        lib_wait();
        free(run(Q, 2));
        for (int n = 1; n <= 7; n++) {
            seti(Q, "v3_smp", n);
            lib_wait();
        }
        CHECK(Q->pv[pi("v3_smp")] == 6, "V3's folder: %g files", Q->pv[pi("v3_smp")]);
        int sq = 0, files = 0;
        for (int r = 0; r < 200; r++) {
            int p = roll(Q, 2, &told), n = (int)Q->pv[pi("v3_smp")];
            files |= 1 << n;
            CHECK(n >= 1 && n <= 6 && Q->pv[pi("v3_src")] == 1, "V3 SAMPLE %d", n);
            sq += p < 1036;
        }
        CHECK(sq == 0 && files == 0x7e, "on SAMPLE: %d of 200 under -30 dBFS, files %x", sq, files);
        E->destroy(Q);
        printf("     softest peak (of 32767) by voice and algorithm:");
        for (int v = 0; v < NV; v++) printf(" V%d %d/%d/%d", v + 1, soft[v][0], soft[v][1], soft[v][2]);
        printf("\n");
        /* heard: every roll over -36 dBFS, and hardly any under -30 (the built-in kits' softest voice peaks at -27) */
        CHECK(quiet == 0 && soft30 * 1000 <= NV * rolls, "%d of %d rolls under -36 dBFS, %d under -30", quiet, NV * rolls,
              soft30);
        printf("ok   RANDOMISE: %d rolls a voice (seed %#x), only its sound controls moved, MPC told; every one heard (over "
               "-36 dBFS; %d of %d under -30); on SAMPLE, every file of its folder\n", rolls, seed, soft30, NV * rolls);
    }
    E->destroy(P);

    /* ---- a session of its own (every instance closed: the library's folders go too): the saved file found by name,
     * though the folder has another file before it now ---- */
    write_wav("build/libtest/data/kicks/Kick 1b.wav", 4410, 1, 16, 1, 44100, 0.5f, NULL, 0);
    P = E->create("build/libtest/data");
    E->set_param(P, "state", saved);
    lib_wait();
    free(run(P, 2));
    CHECK(P->pv[pi("v1_smp")] == 6 && !strcmp(get(P, "v1_smp_display"), "snare"), "by name: %g", P->pv[pi("v1_smp")]);
    E->destroy(P);
    printf("ok   a new session: V1's file found by name with another file added before it\n");

    /* ---- past LIB_BYTES (64 MB): 40 files of 3.5 MB (20 s, as float), each loaded into V1 and played, while V2
     * holds the first one, droning, its pad held the whole time (with ASan: no file freed under it) ---- */
    for (int n = 0; n < 40; n++) {
        snprintf(path, sizeof path, "build/libtest/big/b%02d.wav", n);
        write_wav(path, 20 * 44100, 1, 16, 1, 44100, 0.3f, NULL, 0);
    }
    P = E->create("build/libtest/data");
    E->set_param(P, "state", "kit=0;br_path=build/libtest/big;");
    browse_wait(P);
    set(P, "br_target", "1");   /* V2 */
    set(P, "v2_decay", "100");
    tap(P, "br_row_2");         /* b00 */
    play_wait(P, 1);
    free(run(P, 100));          /* PREVIEW's quarter-second tap over */
    midi(P, 0x90, 37, 100);     /* its pad, held from here on */
    set(P, "br_target", "0");   /* V1 */
    int played = 0;
    for (int n = 1; n < 40; n++) {
        seti(P, "br_scroll", n - 1);
        int row = n + 2 - (int)P->pv[P_BR_SCROLL];   /* row 1 is ".." at the top */
        snprintf(k, sizeof k, "br_row_%d", row);
        tap(P, k);
        CHECK(P->pv[pi("v1_smp")] == n + 1, "b%02d: SAMPLE %g (row %d)", n, P->pv[pi("v1_smp")], row);
        for (int b = 0; b < 600 && P->aud; b++) free(run_rt(P));
        played += P->v[0].active && P->v[0].smp && P->v[0].smp->len == 20 * 44100;
        free(run(P, 8));
        CHECK(P->v[1].active && P->v[1].smp && P->v[1].lslot >= 0, "V2 stopped at b%02d", n);
    }
    o = run(P, 50);
    CHECK(energy(o, 50 * B) > 0 && played >= 34, "played %d of 39", played);
    free(o);
    E->destroy(P);
    fail = system("rm -rf build/libtest");
    printf("ok   past 64 MB: 39 files of 3.5 MB played on V1 while V2 held and played another\n");

    /* ---- every voice stops once its hit has died away (up to 2.1.0 only SNARE did): each algorithm and MODE at short,
     * middle and long DECAY and as a drone, DRIVE off and full; under 1 LSB for the 15 blocks before (the voice
     * counts 2048 frames in its own 32), silent after ---- */
    {
        perc_t *Q = E->create(NULL);
        seti(Q, "lfo_level", 0);
        static const int DEC[4] = {0, 50, 95, 100};
        int cases = 0;
        float most = 0;
        char key[16], what[64] = "";
        for (int v = 0; v < NV; v++)
            for (int a = 0; a < 3; a++)
                for (int m = 0; m < 3; m++)
                    for (int d = 0; d < 4; d++)
                        for (int dr = 0; dr <= 100; dr += 100) {
#define SETV(name, x) (snprintf(key, sizeof key, "v%d_" name, v + 1), seti(Q, key, x))
                            SETV("algo", a), SETV("mode", m), SETV("decay", DEC[d]), SETV("drive", dr);
                            SETV("level", 100), SETV("fx", 0), SETV("comp", 0);
                            free(run(Q, 4));
                            midi(Q, 0x90, 36 + v, 127);
                            int held = d == 3 ? 3 * 44100 / B : 2;   /* a drone plays on while its note is held */
                            free(run(Q, held));
                            CHECK(Q->v[v].active, "V%d ALGO %d MODE %d DECAY %d DRIVE %d stopped while held", v + 1, a, m,
                                  DEC[d], dr);
                            midi(Q, 0x80, 36 + v, 0);
                            float t60 = d == 3 ? 0.35f : voice_t60(v, DEC[d] / 100.0f);
                            int limit = (int)((3.0f * t60 + 4.0f) * 44100 / B), b = 0, pk[15] = {0}, stop = -1;
                            for (; b < limit && (Q->v[v].active || Q->v[v].room.tail > 0); b++) {
                                int16_t *o = run(Q, 1);
                                if (Q->v[v].active) pk[b % 15] = peak(o, B);
                                else if (stop < 0) stop = b;
                                free(o);
                            }
                            CHECK(stop >= 0 && b < limit, "V%d ALGO %d MODE %d DECAY %d DRIVE %d never stops", v + 1, a, m,
                                  DEC[d], dr);
                            int last = 0;
                            for (int j = 0; j < 15; j++) last = pk[j] > last ? pk[j] : last;
                            CHECK(last <= 1, "V%d ALGO %d MODE %d DECAY %d DRIVE %d stopped at %d LSB", v + 1, a, m, DEC[d], dr,
                                  last);
                            int16_t *o = run(Q, 200);
                            CHECK(peak(o, 200 * B) == 0, "V%d ALGO %d MODE %d DECAY %d DRIVE %d: not silent after it stopped",
                                  v + 1, a, m, DEC[d], dr);
                            free(o);
                            float x = stop * B / 44100.0f;
                            if (x > most) {
                                most = x;
                                snprintf(what, sizeof what, "V%d ALGO %d MODE %d DECAY %d DRIVE %d", v + 1, a, m, DEC[d], dr);
                            }
                            cases++;
                        }
        E->destroy(Q);
        printf("ok   every voice stops: %d hits (each ALGO and MODE, DECAY 0/50/95/drone, DRIVE 0/100), under 1 LSB before, "
               "silent after; the longest %.1f s after its note off (%s)\n", cases, most, what);
    }
    printf("PASSED\n");
    return fail;
}
