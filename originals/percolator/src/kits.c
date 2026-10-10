/* Percolator kits: the built-in ones, and Perkons HD-01 .KIT files from the data folder.
 *
 * Data folder (MODULE_DIR, /sdcard/vst/percolator), read once when the plugin is created:
 *   <bank>/KITS/NN.KIT             a bank of kits, named after its folder
 *   <bank>/SAMPLES/1.wav 2.wav 3.wav   that bank's samples for voice 4 / SAMPLE (cue points = slices)
 *   <bank>/names.txt               optional: "NN Name" per line
 *   <folder>/BANKS/NN/KITS/...     a Perkons SD card, or a kit pack as it unzips (its SAMPLES next to BANKS)
 *   <pack>/NN/KITS/...             a kit pack as it unzips (packs 1 and 4: no BANKS folder; SAMPLES at the top)
 * A pack's banks are named after it: "PERKONS KIT PACK 2" -> "Pack 2" (with the bank's number when it has several).
 *
 * A .KIT file is a length-prefixed protobuf message (worked out from the published kit packs):
 *   1: version
 *   2: { 1: LFO speed (float 0..1), 2: LFO wave (float, k/6), 3: mod level (float), 5: 8 bytes x 4 voices = LFO depth
 *        per knob, 0..8 = off, 10 % .. 80 % }
 *   3: x4 voices { 1: 11 x uint16: TUNE DECAY PARAM1 PARAM2 CUTOFF DRIVE FXSEND LEVEL (0..65535), ALGO, MODE, VCF }
 *   4: kit FX, when stored: { 1: BBD time, 2: feedback, 3: LFO rate, 4: mod depth (floats 0..1), 5: long range }
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "percolator.h"

/* ---- built-in kits: per voice TUNE DECAY P1 P2 CUTOFF DRIVE FX LEVEL, ALGO MODE VCF ---------------------------- */

typedef struct {
    const char *name;
    float v[NV][11];
    int depth[NV][NK];
    float lfo_speed, lfo_level;
    int lfo_wave;
    float fx[4];
    int fx_range;
} bkit_t;

static const bkit_t BUILTIN[] = {
    {"Init",
     {{38, 22, 18, 34, 60, 18, 0, 85, 0, 0, 2}, {48, 20, 35, 25, 75, 10, 12, 70, 2, 0, 2},
      {45, 18, 55, 35, 80, 15, 10, 75, 0, 0, 2}, {55, 6, 10, 0, 45, 0, 0, 60, 0, 0, 0}},
     {{0}}, 40, 100, 0, {45, 35, 30, 10}, 1},
    {"Analog",
     {{36, 35, 30, 25, 55, 10, 0, 90, 2, 0, 2}, {40, 25, 10, 30, 70, 5, 10, 70, 2, 0, 2},
      {50, 15, 60, 40, 85, 5, 8, 75, 0, 0, 2}, {50, 8, 0, 0, 50, 0, 0, 65, 0, 1, 0}},
     {{0}}, 40, 100, 0, {40, 30, 25, 8}, 1},
    {"Industrial",
     {{30, 30, 70, 50, 50, 70, 0, 80, 0, 2, 2}, {45, 30, 60, 40, 60, 40, 30, 65, 1, 2, 1},
      {50, 30, 40, 30, 70, 30, 20, 70, 1, 1, 2}, {60, 15, 40, 0, 55, 20, 10, 55, 1, 2, 0}},
     {{0}, {0, 0, 4, 0, 0, 0, 0, 0}}, 55, 80, 5, {30, 55, 20, 15}, 0},
    {"Clap Room",
     {{34, 28, 22, 40, 50, 25, 0, 90, 0, 1, 2}, {52, 18, 25, 20, 80, 0, 0, 60, 2, 1, 2},
      {48, 32, 55, 35, 75, 10, 15, 85, 1, 1, 1}, {52, 10, 35, 5, 55, 0, 0, 55, 0, 0, 0}},
     {{0}}, 40, 100, 0, {22, 30, 30, 10}, 0},
    {"Tonal",
     {{40, 40, 15, 20, 65, 5, 0, 80, 1, 0, 2}, {55, 35, 45, 10, 70, 0, 20, 65, 2, 1, 2},
      {42, 40, 30, 20, 70, 5, 15, 70, 2, 1, 2}, {50, 30, 20, 0, 35, 0, 25, 50, 1, 1, 0}},
     {{0}}, 35, 100, 0, {50, 45, 25, 12}, 1},
    {"Lo-Fi",
     {{35, 25, 45, 30, 45, 35, 0, 85, 0, 1, 2}, {46, 18, 30, 30, 55, 20, 0, 65, 1, 0, 2},
      {50, 20, 40, 45, 60, 25, 10, 70, 0, 1, 2}, {50, 30, 65, 0, 40, 10, 0, 65, 2, 0, 0}},
     {{0}}, 40, 100, 0, {35, 40, 20, 5}, 1},
    {"Drone",
     {{30, 100, 30, 0, 45, 30, 20, 70, 1, 1, 2}, {37, 100, 20, 0, 50, 10, 30, 55, 0, 0, 1},
      {40, 30, 50, 60, 60, 10, 30, 55, 0, 2, 2}, {50, 60, 70, 60, 40, 0, 40, 45, 0, 0, 0}},
     {{0, 0, 6, 0, 3, 0, 0, 0}, {0, 0, 4, 0, 0, 0, 0, 0}, {0}, {0, 0, 0, 0, 4, 0, 0, 0}}, 20, 100, 6, {65, 60, 15, 30}, 1},
    {"Thunder",
     {{32, 30, 80, 60, 55, 60, 10, 85, 0, 1, 2}, {30, 45, 70, 50, 50, 45, 25, 70, 2, 2, 2},
      {45, 35, 45, 50, 65, 40, 30, 75, 2, 2, 2}, {45, 45, 50, 10, 50, 20, 35, 55, 1, 0, 1}},
     {{0}}, 30, 100, 1, {55, 50, 35, 20}, 1},
};
#define NBUILTIN ((int)(sizeof BUILTIN / sizeof BUILTIN[0]))

static void builtin_kit(kit_t *k, const bkit_t *b) {
    memset(k, 0, sizeof *k);
    snprintf(k->name, sizeof k->name, "%s", b->name);
    k->bank = -1;
    for (int v = 0; v < NV; v++) {
        for (int j = 0; j < NK; j++) {
            k->knob[v][j] = b->v[v][j];
            k->depth[v][j] = b->depth[v][j];
        }
        k->algo[v] = (int)b->v[v][8];
        k->mode[v] = (int)b->v[v][9];
        k->vcf[v] = (int)b->v[v][10];
    }
    k->has_lfo = 1;
    k->lfo_speed = b->lfo_speed;
    k->lfo_level = b->lfo_level;
    k->lfo_wave = b->lfo_wave;
    k->has_fx = 1;
    k->fx_time = b->fx[0];
    k->fx_feedback = b->fx[1];
    k->fx_rate = b->fx[2];
    k->fx_depth = b->fx[3];
    k->fx_range = b->fx_range;
}

/* ---- protobuf ----------------------------------------------------------------------------------------------- */

typedef struct { const uint8_t *p, *end; } pb_t;

static int pb_varint(pb_t *b, uint64_t *v) {
    uint64_t r = 0;
    for (int s = 0; s < 64 && b->p < b->end; s += 7) {
        uint8_t c = *b->p++;
        r |= (uint64_t)(c & 0x7f) << s;
        if (!(c & 0x80)) { *v = r; return 1; }
    }
    return 0;
}

/* next field: its number and wire type; the value in *v (varint, fixed) or *sub (length-delimited) */
static int pb_next(pb_t *b, int *field, int *type, uint64_t *v, pb_t *sub) {
    uint64_t key;
    if (b->p >= b->end || !pb_varint(b, &key)) return 0;
    *field = (int)(key >> 3);
    *type = (int)(key & 7);
    switch (*type) {
    case 0: return pb_varint(b, v);
    case 1: if (b->end - b->p < 8) return 0; memcpy(v, b->p, 8); b->p += 8; return 1;
    case 5: { uint32_t x; if (b->end - b->p < 4) return 0; memcpy(&x, b->p, 4); *v = x; b->p += 4; return 1; }
    case 2: {
        uint64_t n;
        if (!pb_varint(b, &n) || n > (uint64_t)(b->end - b->p)) return 0;
        sub->p = b->p;
        sub->end = b->p + n;
        b->p += n;
        return 1;
    }
    default: return 0;
    }
}

static float pb_float(uint64_t v) {
    uint32_t x = (uint32_t)v;
    float f;
    memcpy(&f, &x, 4);
    return f != f ? 0.0f : clampf(f, 0.0f, 1.0f);
}

static int parse_kit(const uint8_t *data, int len, kit_t *k) {
    pb_t top = {data, data + len};
    uint64_t n;
    /* the files start with the message length; take it when it fits */
    pb_t t = top;
    if (pb_varint(&t, &n) && n <= (uint64_t)(t.end - t.p)) top = (pb_t){t.p, t.p + n};
    int field, type, nv = 0, nd = 0;
    uint64_t v;
    pb_t sub;
    while (pb_next(&top, &field, &type, &v, &sub)) {
        if (field == 2 && type == 2) {
            pb_t g = sub, s2;
            k->has_lfo = 1;
            k->lfo_speed = k->lfo_level = 0;
            k->lfo_wave = 0;
            while (pb_next(&g, &field, &type, &v, &s2)) {
                if (type == 5 && field == 1) k->lfo_speed = pb_float(v) * 100.0f;
                else if (type == 5 && field == 2) k->lfo_wave = (int)(pb_float(v) * 6.0f + 0.5f);
                else if (type == 5 && field == 3) k->lfo_level = pb_float(v) * 100.0f;
                else if (type == 2 && field == 5 && nd < NV) {
                    for (int j = 0; j < NK && s2.p + j < s2.end; j++) k->depth[nd][j] = s2.p[j] > 8 ? 8 : s2.p[j];
                    nd++;
                }
            }
        } else if (field == 3 && type == 2 && nv < NV) {
            pb_t vb = sub, s2;
            while (pb_next(&vb, &field, &type, &v, &s2)) {
                if (field != 1 || type != 2 || s2.end - s2.p < 22) continue;
                for (int j = 0; j < 11; j++) {
                    int x = s2.p[2 * j] | s2.p[2 * j + 1] << 8;
                    if (j < NK) k->knob[nv][j] = x >= 65520 ? 100.0f : x * (100.0f / 65535.0f);
                    else if (j == 8) k->algo[nv] = x > 2 ? 2 : x;
                    else if (j == 9) k->mode[nv] = x > 2 ? 2 : x;
                    else k->vcf[nv] = x > 2 ? 2 : x;
                }
            }
            nv++;
        } else if (field == 4 && type == 2 && sub.end > sub.p) {
            pb_t fb = sub, s2;
            k->has_fx = 1;
            k->fx_time = k->fx_feedback = k->fx_rate = k->fx_depth = 0;
            k->fx_range = 0;
            while (pb_next(&fb, &field, &type, &v, &s2)) {
                if (type == 5 && field >= 1 && field <= 4) {
                    float *dst[4] = {&k->fx_time, &k->fx_feedback, &k->fx_rate, &k->fx_depth};
                    *dst[field - 1] = pb_float(v) * 100.0f;
                } else if (type == 0 && field == 5) k->fx_range = v ? 1 : 0;
            }
        }
    }
    return nv == NV;
}

/* ---- files -------------------------------------------------------------------------------------------------- */

static int cmp_str(const void *a, const void *b) {   /* by name, ignoring case: "Perkons Kit Pack 01" before "PERKONS KIT PACK 2" */
    int c = strcasecmp(*(char *const *)a, *(char *const *)b);
    return c ? c : strcmp(*(char *const *)a, *(char *const *)b);
}

/* sorted names in a folder (files or folders), each malloc'd; returns the count */
static int list_dir(const char *path, char ***out, int want_dirs) {
    DIR *d = opendir(path);
    *out = NULL;
    if (!d) return 0;
    int n = 0, cap = 0;
    char **v = NULL;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[4400];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        if (stat(full, &st) || (want_dirs ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode))) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **nv = realloc(v, cap * sizeof *v);
            if (!nv) break;
            v = nv;
        }
        v[n++] = strdup(e->d_name);
    }
    closedir(d);
    if (n) qsort(v, n, sizeof *v, cmp_str);
    *out = v;
    return n;
}
static void free_list(char **v, int n) {
    for (int i = 0; i < n; i++) free(v[i]);
    free(v);
}

static uint8_t *read_file(const char *path, long max, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = NULL;
    if (n > 0 && n <= max && (b = malloc(n)) && fread(b, 1, n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = n;
    return b;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* WAV (PCM 8/16/24/32-bit or float, mono or stereo) -> mono float, with its cue points as slice markers */
static int load_wav(const char *path, sample_t *s) {
    long len;
    uint8_t *b = read_file(path, 16L << 20, &len);
    memset(s, 0, sizeof *s);
    if (!b) return 0;
    int ok = 0, fmt = 0, ch = 0, bits = 0, rate = 0, ncue = 0;
    const uint8_t *data = NULL;
    uint32_t dlen = 0;
    int cues[MAX_CUES];
    if (len >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4)) {
        long i = 12;
        while (i + 8 <= len) {
            uint32_t n = rd32(b + i + 4);
            const uint8_t *c = b + i + 8;
            if (n > (uint32_t)(len - i - 8)) n = (uint32_t)(len - i - 8);
            if (!memcmp(b + i, "fmt ", 4) && n >= 16) {
                fmt = c[0] | c[1] << 8;
                ch = c[2] | c[3] << 8;
                rate = (int)rd32(c + 4);
                bits = c[14] | c[15] << 8;
                if (fmt == 0xFFFE && n >= 26) fmt = c[24] | c[25] << 8;   /* WAVE_FORMAT_EXTENSIBLE */
            } else if (!memcmp(b + i, "data", 4)) {
                data = c;
                dlen = n;
            } else if (!memcmp(b + i, "cue ", 4) && n >= 4) {
                uint32_t cnt = rd32(c);
                for (uint32_t k = 0; k < cnt && 4 + 24 * (k + 1) <= n && ncue < MAX_CUES - 2; k++)
                    cues[ncue++] = (int)rd32(c + 4 + 24 * k + 20);
            }
            i += 8 + n + (n & 1);
        }
    }
    int bps = bits / 8;
    if (data && ch >= 1 && ch <= 2 && rate >= 8000 && rate <= 192000 &&
        ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) {
        int frames = (int)(dlen / (uint32_t)(bps * ch));
        if (frames > 0 && (s->d = malloc((frames + 1) * sizeof(float)))) {
            for (int f = 0; f < frames; f++) {
                float acc = 0;
                for (int c = 0; c < ch; c++) {
                    const uint8_t *p = data + (size_t)(f * ch + c) * bps;
                    float x;
                    if (fmt == 3) { float y; memcpy(&y, p, 4); x = y; }
                    else if (bits == 8) x = (p[0] - 128) / 128.0f;
                    else if (bits == 16) x = (int16_t)(p[0] | p[1] << 8) / 32768.0f;
                    else if (bits == 24) x = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f;
                    else x = (int32_t)rd32(p) / 2147483648.0f;
                    acc += x;
                }
                s->d[f] = clampf(acc / ch, -1.0f, 1.0f);
            }
            s->d[frames] = 0;
            s->len = frames;
            s->rate = (float)rate;
            /* slices: the cue points in order, from the start to the end */
            int m = 0;
            s->cue[m++] = 0;
            qsort(cues, ncue, sizeof(int), cmp_int);
            for (int k = 0; k < ncue; k++)
                if (cues[k] > s->cue[m - 1] + 32 && cues[k] < frames - 32) s->cue[m++] = cues[k];
            s->cue[m++] = frames;
            s->ncue = ncue ? m : 0;
            ok = 1;
        }
    }
    free(b);
    return ok;
}

static void read_names(const char *dir, char names[][24], int max) {
    char path[4400], line[256];
    snprintf(path, sizeof path, "%s/names.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *end;
        long n = strtol(line, &end, 10);
        if (end == line || n < 0 || n >= max) continue;
        while (*end == ' ' || *end == '\t') end++;
        end[strcspn(end, "\r\n")] = 0;
        snprintf(names[n], 24, "%s", end);
    }
    fclose(f);
}

/* one bank: <dir>/KITS/NN.KIT, samples from <dir>/SAMPLES or <fallback>/SAMPLES */
static void scan_bank(perc_t *P, const char *dir, const char *name, const char *fallback) {
    char kd[4200], path[4400];
    snprintf(kd, sizeof kd, "%s/KITS", dir);
    char **files;
    int nf = list_dir(kd, &files, 0);
    if (!nf || P->nbanks >= MAX_BANKS) { free_list(files, nf); return; }
    int bi = P->nbanks;
    bank_t *b = &P->banks[bi];
    memset(b, 0, sizeof *b);
    snprintf(b->name, sizeof b->name, "%s", name);
    static const char *const SMP[NSLOT] = {"1.wav", "2.wav", "3.wav"};
    for (int s = 0; s < NSLOT; s++) {
        snprintf(path, sizeof path, "%s/SAMPLES/%s", dir, SMP[s]);
        if (!load_wav(path, &b->smp[s]) && fallback) {
            snprintf(path, sizeof path, "%s/SAMPLES/%s", fallback, SMP[s]);
            load_wav(path, &b->smp[s]);
        }
        if (b->smp[s].len) b->has_samples = 1;
    }
    char (*names)[24] = calloc(100, 24);
    if (names) read_names(dir, names, 100);
    int added = 0;
    for (int i = 0; i < nf && P->nkits < MAX_KITS; i++) {
        size_t l = strlen(files[i]);
        if (l < 5 || files[i][0] == '.' || strcasecmp(files[i] + l - 4, ".KIT")) continue;   /* not macOS's ._ files */
        long len;
        if (l > 255 || snprintf(path, sizeof path, "%s/%.255s", kd, files[i]) >= (int)sizeof path) continue;
        uint8_t *data = read_file(path, 4096, &len);
        if (!data) continue;
        kit_t *k = &P->kits[P->nkits];
        builtin_kit(k, &BUILTIN[0]);
        k->has_lfo = k->has_fx = 0;
        memset(k->depth, 0, sizeof k->depth);
        if (parse_kit(data, (int)len, k)) {
            int num = atoi(files[i]);
            k->bank = bi;
            if (names && num >= 0 && num < 100 && names[num][0]) snprintf(k->name, sizeof k->name, "%s %s", name, names[num]);
            else snprintf(k->name, sizeof k->name, "%s %.*s", name, (int)(l - 4), files[i]);
            P->nkits++;
            added++;
        }
        free(data);
    }
    free(names);
    free_list(files, nf);
    if (added) P->nbanks++;
    else for (int s = 0; s < NSLOT; s++) free(b->smp[s].d);
}

/* a short name for a pack's folder: "PERKONS KIT PACK 2", "Perkons Kit Pack 01" -> "Pack 2", "Pack 1"; else its
 * first 12 characters */
static void pack_name(const char *d, char *out, size_t n) {
    for (const char *p = d; *p; p++)
        if (!strncasecmp(p, "pack", 4)) {
            const char *q = p + 4;
            while (*q == ' ' || *q == '_' || *q == '-' || *q == '0') q++;
            if (*q >= '1' && *q <= '9') {
                snprintf(out, n, "Pack %d", atoi(q));
                return;
            }
        }
    snprintf(out, n, "%.12s", d);
}

static int has_kits(const char *dir) {
    char kd[4200];
    snprintf(kd, sizeof kd, "%s/KITS", dir);
    DIR *d = opendir(kd);
    if (d) closedir(d);
    return d != NULL;
}

void kits_load(perc_t *P) {
    P->kits = calloc(MAX_KITS, sizeof(kit_t));
    if (!P->kits) return;
    for (int i = 0; i < NBUILTIN; i++) builtin_kit(&P->kits[i], &BUILTIN[i]);
    P->nkits = P->nbuiltin = NBUILTIN;
    if (!P->dir[0]) return;
    char path[2048], sub[3072];
    /* a Perkons card copied straight into the folder */
    snprintf(path, sizeof path, "%s/BANKS", P->dir);
    char **bn;
    int nb = list_dir(path, &bn, 1);
    for (int j = 0; j < nb; j++) {
        snprintf(sub, sizeof sub, "%s/%s", path, bn[j]);
        scan_bank(P, sub, bn[j], P->dir);
    }
    free_list(bn, nb);
    char **dirs;
    int nd = list_dir(P->dir, &dirs, 1);
    for (int i = 0; i < nd; i++) {
        if (!strcmp(dirs[i], "BANKS") || !strcmp(dirs[i], "SAMPLES")) continue;
        snprintf(path, sizeof path, "%s/%s", P->dir, dirs[i]);
        scan_bank(P, path, dirs[i], NULL);
        /* a pack as it unzips: <pack>/BANKS/NN/KITS, or <pack>/NN/KITS; its samples in <pack>/SAMPLES */
        char pn[16];
        pack_name(dirs[i], pn, sizeof pn);
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0) snprintf(sub, sizeof sub, "%s/BANKS", path);
            else snprintf(sub, sizeof sub, "%s", path);
            nb = list_dir(sub, &bn, 1);
            int nk = 0;
            char bp[4096];
            for (int j = 0; j < nb; j++) {
                snprintf(bp, sizeof bp, "%s/%s", sub, bn[j]);
                nk += has_kits(bp);
            }
            for (int j = 0; j < nb; j++) {
                if (pass == 1 && (!strcmp(bn[j], "BANKS") || !strcmp(bn[j], "SAMPLES") || !strcmp(bn[j], "KITS"))) continue;
                char name[48];
                snprintf(bp, sizeof bp, "%s/%s", sub, bn[j]);
                if (nk > 1) snprintf(name, sizeof name, "%s %.8s", pn, bn[j]);
                else snprintf(name, sizeof name, "%s", pn);
                scan_bank(P, bp, name, path);
            }
            free_list(bn, nb);
        }
    }
    free_list(dirs, nd);
}

void kits_free(perc_t *P) {
    for (int b = 0; b < P->nbanks; b++)
        for (int s = 0; s < NSLOT; s++) free(P->banks[b].smp[s].d);
    free(P->kits);
    P->kits = NULL;
}

const sample_t *slot_sample(const perc_t *P, int bank, int slot) {
    slot %= NSLOT;
    if (bank >= 0 && bank < P->nbanks && P->banks[bank].smp[slot].len > 0) return &P->banks[bank].smp[slot];
    return &perc_builtin[slot];
}
