/* Percolator offline renderer (development): plays each kit's four voices alone, then a two-bar groove, and writes
 * a WAV per kit with each voice's peak and length. Built natively, without the wrapper:
 *   cc -O2 -Isrc mpc/render.c src/percolator.c src/voices.c src/kits.c -lm -lpthread -o build/render
 *   build/render <data dir or ""> <out dir> [kit numbers...]          (no kit numbers: every kit) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NOTE0 20   /* voice 1's note, as src/percolator.h */

typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);

static const mpc_engine_t *E;
static void *I;
static int16_t *buf;
static int pos, cap;

static void midi3(int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    E->midi(I, m, 3);
}
static void run(int frames) {
    while (frames > 0) {
        if (pos + 128 > cap) { cap = cap ? cap * 2 : 1 << 20; buf = realloc(buf, cap * 2 * sizeof *buf); }
        E->render(I, buf + 2 * pos, 128);
        pos += 128;
        frames -= 128;
    }
}
static void stats(int from, int to, float *peak_db, float *len_s) {
    int pk = 1, last = from;
    for (int i = from; i < to; i++) if (abs(buf[2 * i]) > pk) pk = abs(buf[2 * i]);
    for (int i = from; i < to; i++) if (abs(buf[2 * i]) > pk / 100) last = i;   /* -40 dB */
    *peak_db = 20.0f * log10f(pk / 32768.0f);
    *len_s = (last - from) / 44100.0f;
}
static void wav(const char *path, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t d = frames * 4, r = 36 + d, sr = 44100, br = sr * 4;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sixteen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&r, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    fwrite(buf, 4, frames, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "render <data dir> <out dir> [kits...]\n"); return 1; }
    E = mpc_engine();
    I = E->create(argv[1][0] ? argv[1] : NULL);
    char s[256];
    E->get_param(I, "kit_count", s, sizeof s);
    int nk = atoi(s), *list = malloc(sizeof(int) * (nk + argc)), nl = 0;
    if (argc > 3) for (int i = 3; i < argc; i++) list[nl++] = atoi(argv[i]);
    else for (int i = 0; i < nk; i++) list[nl++] = i;
    printf("%d kits\n", nk);
    for (int li = 0; li < nl; li++) {
        int k = list[li];
        char v[16], name[64] = "";
        snprintf(v, sizeof v, "%d", k);
        E->set_param(I, "kit", v);
        E->get_param(I, "kit_name", name, sizeof name);
        midi3(0xB0, 120, 0);
        pos = 0;
        run(4410);
        printf("%3d %-26s", k, name);
        for (int vo = 0; vo < 4; vo++) {   /* each voice alone, held 0.5 s (drones stop at the release) */
            int at = pos;
            midi3(0x90, NOTE0 + vo, 120);
            run(22050);
            midi3(0x80, NOTE0 + vo, 0);
            run(44100 + 22050);
            float pk, ln;
            stats(at, pos, &pk, &ln);
            printf("  V%d %5.1f dB %4.2fs", vo + 1, pk, ln);
        }
        int at = pos;
        static const char *const PAT[4] = {"x.......x.x.....", "......x.......x.", "....x.......x...", "x.x.x.x.x.x.x.xx"};
        for (int bar = 0; bar < 2; bar++)
            for (int st = 0; st < 16; st++) {
                for (int vo = 0; vo < 4; vo++) if (PAT[vo][st] == 'x') midi3(0x90, NOTE0 + vo, vo == 3 && (st & 2) ? 80 : 115);
                run(5512);
                for (int vo = 0; vo < 4; vo++) if (PAT[vo][st] == 'x') midi3(0x80, NOTE0 + vo, 0);
            }
        run(44100);
        float pk, ln;
        stats(at, pos, &pk, &ln);
        double rms = 0;
        for (int i = at; i < pos; i++) rms += (double)buf[2 * i] * buf[2 * i];
        rms = sqrt(rms / (pos - at)) / 32768.0;
        printf("  | groove peak %5.1f dB rms %5.1f dB\n", pk, 20 * log10(rms + 1e-9));
        char path[512];
        snprintf(path, sizeof path, "%s/kit_%03d.wav", argv[2], k);
        wav(path, pos);
    }
    E->destroy(I);
    free(buf);
    free(list);
    return 0;
}
