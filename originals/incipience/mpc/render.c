/* Incipience offline renderer (development): plays each instrument alone, then a groove, for a kit; or a list of
 * single sounds from a script. Writes WAVs and prints each sound's peak, RMS and length. Built natively, without the
 * wrapper:
 *   cc -O2 -Isrc mpc/render.c src/incipience.c src/voices.c src/fx.c src/tables.c src/samples.c src/kits.c src/library.c -lm -lpthread -o build/render
 *   build/render <data dir or ""> <out dir> kit <n...>          each instrument of kit n alone, then a groove
 *   build/render <data dir or ""> <out dir> script <file>       one WAV per line: name;key=value;...;hits=<ins>@<s>[:vel]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);
void pool_wait(void);

static const char *const INS[11] = {"kd", "mb", "ch", "oh", "cp", "rm", "cr", "rd", "x1", "x2", "fm"};
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
        if (pos + 128 > cap) { cap = cap ? cap * 2 : 1 << 20; buf = realloc(buf, (size_t)cap * 2 * sizeof *buf); }
        E->render(I, buf + 2 * pos, 128);
        pos += 128;
        frames -= 128;
    }
}
static void stats(int from, int to, float *peak_db, float *rms_db, float *len_s) {
    int pk = 1, last = from;
    double ss = 0;
    for (int i = from; i < to; i++) {
        int a = abs(buf[2 * i]), b = abs(buf[2 * i + 1]);
        if (a > pk) pk = a;
        if (b > pk) pk = b;
        ss += (double)buf[2 * i] * buf[2 * i] + (double)buf[2 * i + 1] * buf[2 * i + 1];
    }
    for (int i = from; i < to; i++) if (abs(buf[2 * i]) > pk / 100 || abs(buf[2 * i + 1]) > pk / 100) last = i;
    *peak_db = 20.0f * log10f(pk / 32768.0f);
    *len_s = (last - from) / 44100.0f;
    /* RMS over the sound itself (to -40 dB), not the silence after */
    int n = last - from + 1;
    double s2 = 0;
    for (int i = from; i <= last; i++) s2 += (double)buf[2 * i] * buf[2 * i] + (double)buf[2 * i + 1] * buf[2 * i + 1];
    *rms_db = 10.0f * log10f((float)(s2 / (2.0 * n)) / (32768.0f * 32768.0f) + 1e-12f);
    (void)ss;
}
static void wav(const char *path, int from, int frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t d = frames * 4, r = 36 + d, sr = 44100, br = sr * 4;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sixteen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&r, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    fwrite(buf + 2 * from, 4, frames, f);
    fclose(f);
}
static int ins_index(const char *s) {
    for (int i = 0; i < 11; i++) if (!strncmp(s, INS[i], 2)) return i;
    return -1;
}
static int note_of(int ins) {
    char k[16], v[32];
    snprintf(k, sizeof k, "%s_note", INS[ins]);
    E->get_param(I, k, v, sizeof v);
    return atoi(v);
}
static void hit(int ins, int vel, int len_frames) {
    int n = note_of(ins);
    midi3(0x90, n, vel);
    run(len_frames);
    midi3(0x80, n, 0);
}
static void quiet(void) {   /* everything stops, and the FX empty */
    midi3(0xB0, 120, 0);
    run(44100 * 2);
}

static void render_kit(const char *out, int kit) {
    char v[16], name[64];
    snprintf(v, sizeof v, "%d", kit);
    E->set_param(I, "kit", v);
    run(128);
    pool_wait();
    E->get_param(I, "kit_name", name, sizeof name);
    for (char *c = name; *c; c++) if (*c == ' ' || *c == '/') *c = '_';
    printf("kit %d %s\n", kit, name);
    quiet();
    int start = pos;
    for (int ins = 0; ins < 11; ins++) {
        int a = pos;
        hit(ins, 127, 2205);
        run(44100 * 2 - 2205);
        float pk, rms, len;
        stats(a, pos, &pk, &rms, &len);
        printf("  %s  peak %6.1f dB  rms %6.1f dB  %5.2f s\n", INS[ins], pk, rms, len);
    }
    /* a two-bar groove at 120: kick 1 5 9 13, MBrane 5 13, hats on 8ths, open hat on the and of 4, clap 13 */
    for (int bar = 0; bar < 2; bar++)
        for (int s = 0; s < 16; s++) {
            int f = 5512;   /* a 16th at 120 */
            int hits[11] = {0};
            if (s % 4 == 0) hits[0] = 120;
            if (s == 4 || s == 12) hits[1] = 120;
            if (s % 2 == 0 && s != 14) hits[2] = s % 4 ? 90 : 120;
            if (s == 14) hits[3] = 110;
            if (s == 12) hits[4] = 100;
            if (bar == 1 && s == 15) hits[5] = 100;
            for (int i = 0; i < 11; i++) if (hits[i]) midi3(0x90, note_of(i), hits[i]);
            run(1024);
            for (int i = 0; i < 11; i++) if (hits[i]) midi3(0x80, note_of(i), 0);
            run(f - 1024);
        }
    run(44100);
    char path[512];
    snprintf(path, sizeof path, "%s/kit%03d_%s.wav", out, kit, name);
    wav(path, start, pos - start);
}

/* name;key=value;...;hits=kd@0:127,kd@0.5;len=2 */
static void render_line(const char *out, char *line, const char *dir) {
    char *name = strtok(line, ";"), *tok;
    if (!name || name[0] == '#' || !name[0]) return;
    E->destroy(I);   /* a fresh instance per line: every control at its default */
    I = E->create(dir);
    run(128);
    double len = 2.0;
    char hits[512] = "";
    while ((tok = strtok(NULL, ";"))) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq++ = 0;
        if (!strcmp(tok, "hits")) snprintf(hits, sizeof hits, "%s", eq);
        else if (!strcmp(tok, "len")) len = atof(eq);
        else E->set_param(I, tok, eq);
    }
    pool_wait();
    quiet();
    int start = pos;
    /* the hits, in time order as written */
    double t = 0.0;
    char *h = hits[0] ? hits : (char *)"kd@0";
    char copy[512];
    snprintf(copy, sizeof copy, "%s", h);
    for (char *p = strtok(copy, ","); p; p = strtok(NULL, ",")) {
        int ins = ins_index(p), vel = 127;
        char *at = strchr(p, '@'), *colon = strchr(p, ':');
        double when = at ? atof(at + 1) : 0.0;
        if (colon) vel = atoi(colon + 1);
        if (ins < 0) continue;
        int wait = (int)((when - t) * 44100.0);
        if (wait > 0) run(wait);
        t = when > t ? when : t;
        midi3(0x90, note_of(ins), vel);
        run(1024);
        midi3(0x80, note_of(ins), 0);
        t += 1024 / 44100.0;
    }
    int rest = (int)((len - t) * 44100.0);
    if (rest > 0) run(rest);
    float pk, rms, l;
    stats(start, pos, &pk, &rms, &l);
    printf("%-24s peak %6.1f dB  rms %6.1f dB  %5.2f s\n", name, pk, rms, l);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", out, name);
    wav(path, start, pos - start);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: render <data dir> <out dir> kit <n...> | script <file>\n");
        return 1;
    }
    E = mpc_engine();
    const char *dir = argv[1][0] ? argv[1] : NULL;
    void *holder = E->create(dir);   /* keeps the sample pool loaded between instances */
    I = E->create(dir);
    if (!strcmp(argv[3], "kit")) {
        char v[16];
        E->get_param(I, "kit_count", v, sizeof v);
        int nk = atoi(v);
        if (argc == 4) for (int k = 0; k < nk; k++) render_kit(argv[2], k);
        for (int a = 4; a < argc; a++) render_kit(argv[2], atoi(argv[a]));
    } else if (!strcmp(argv[3], "script") && argc > 4) {
        FILE *f = fopen(argv[4], "r");
        char line[4096];
        while (f && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            render_line(argv[2], line, dir);
        }
        if (f) fclose(f);
    }
    E->destroy(I);
    E->destroy(holder);
    free(buf);
    return 0;
}
