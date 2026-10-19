/* Incipience: the sine table, the 64 LFO waveforms and the metal noise patterns.
 *
 * LFO waves: the AlphaBase keeps 64 single-cycle LFO waveforms of 256 samples in a file of its own (ABWV0300.raw on its
 * SD card). Its manual names the first 16 (saw, triangle, sine, rectified sine, first and second half-wave sine, faded
 * sine, rectangle, each + and -); those are built here the same way, and 16-63 are this plugin's own: pulse widths,
 * steps, multiples, bumps and fixed random patterns. With ABWV0300.raw in the data folder, its 64 are used instead.
 *
 * Metal noise: the AlphaBase's "metallic noise" is a set of digital bit patterns, each value 2-127 its own sound and
 * not a smooth range (its manual: "a kaleidoscope rather than a linear scale"), 0 off and 1 white noise. Its patterns
 * aren't published, so these are made here: clusters of square waves XORed or summed (the bit-pattern cymbals of the
 * classic analog drum machines) and short shift registers clocked at audio rates, which buzz with a pitch, in 126
 * fixed combinations from a seeded hash. */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "incipience.h"

float inc_sintab[SIN_N + 1];
float inc_lfotab[LFO_WAVES][LFO_LEN + 1];   /* +1: the first sample again, for interpolation */
int inc_lfo_from_file;
metal_pat_t inc_metal[128];

static uint32_t mix32(uint32_t x) {   /* a 32-bit integer hash (lowbias32) */
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}
static float frand(uint32_t *s) {   /* 0..1 from a hash chain */
    *s = mix32(*s + 0x9e3779b9u);
    return (*s >> 8) * (1.0f / 16777216.0f);
}

/* Galois LFSR feedback masks giving the longest sequence, by register length */
const uint32_t inc_lfsr_taps[18] = {0, 0, 0x3, 0x6, 0xC, 0x14, 0x30, 0x60, 0xB8, 0x110, 0x240, 0x500, 0x829, 0x100D,
                                    0x2015, 0x6000, 0xD008, 0x12000};

static uint32_t hz_inc(float hz) { return (uint32_t)(hz * (4294967296.0 / SR)); }

static void build_lfo(void) {
    for (int w = 0; w < LFO_WAVES; w++) {
        uint32_t seed = 0x51ED0000u + (uint32_t)w;
        float rnd[32];
        for (int k = 0; k < 32; k++) rnd[k] = 2.0f * frand(&seed) - 1.0f;
        for (int i = 0; i < LFO_LEN; i++) {
            float x = (float)i / LFO_LEN, y = 0.0f, s = sinf(TWO_PI_F * x);
            int base = w & ~1, neg = w & 1;
            if (w < 16) {
                switch (base) {
                case 0: y = 2.0f * x - 1.0f; break;                                     /* saw */
                case 2: y = x < 0.25f ? 4.0f * x : x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f; break;   /* triangle */
                case 4: y = s; break;                                                   /* sine */
                case 6: y = fabsf(s); break;                                            /* rectified sine */
                case 8: y = x < 0.5f ? s : 0.0f; break;                                 /* first half-wave */
                case 10: y = x < 0.5f ? 0.0f : -s; break;                               /* second half-wave */
                case 12: y = s * (1.0f - x) * (1.0f - x); break;                        /* faded sine */
                case 14: y = i == 0 ? 0.0f : x < 0.5f ? 1.0f : -1.0f; break;            /* rectangle */
                }
                if (neg) y = -y;
            } else if (w <= 30) {        /* pulses, 15/32 down to 1/32 high */
                float duty = (31 - w) / 32.0f;
                y = i == 0 ? 0.0f : x < duty ? 1.0f : -1.0f;
            } else if (w <= 45) {        /* inverted pulses, 14/32 down to 0/32 low */
                float duty = (45 - w) / 32.0f;
                y = i == 0 ? 0.0f : x < duty ? -1.0f : 1.0f;
            } else {
                switch (w) {
                case 46: y = floorf(x * 8.0f) / 3.5f - 1.0f; break;                   /* 8 steps up */
                case 47: y = sinf(2.0f * TWO_PI_F * x); break;                        /* sine x2 */
                case 48: y = -sinf(2.0f * TWO_PI_F * x); break;
                case 49: y = sinf(3.0f * TWO_PI_F * x); break;                        /* sine x3 */
                case 50: y = 2.0f * expf(-5.0f * x) - 1.0f; break;                    /* exponential fall */
                case 51: y = 1.0f - 2.0f * expf(-5.0f * x); break;                    /* exponential rise */
                case 52: y = sinf(PI_F * x); break;                                   /* one bump up */
                case 53: y = -sinf(PI_F * x); break;
                case 54: y = clampf(3.0f * sinf(TWO_PI_F * x), -1.0f, 1.0f); break;   /* trapezoid */
                case 55: y = -clampf(3.0f * sinf(TWO_PI_F * x), -1.0f, 1.0f); break;
                case 56: y = rnd[(int)(x * 8.0f)]; break;                             /* random steps, 8 */
                case 57: y = rnd[(int)(x * 16.0f)]; break;                            /* random steps, 16 */
                case 58: {                                                             /* smooth random */
                    float p = x * 8.0f;
                    int k = (int)p;
                    float f = p - k, c = 0.5f - 0.5f * cosf(PI_F * f);
                    y = rnd[k] + (rnd[(k + 1) & 7] - rnd[k]) * c;
                    break;
                }
                case 59: y = fabsf(sinf(PI_F * 4.0f * x * x)) * (1.0f - x) * 2.0f - 1.0f; break;   /* bouncing */
                case 60: y = fmodf(x * 3.0f, 1.0f) < 0.2f ? 1.0f : -1.0f; break;     /* three short pulses */
                case 61: y = x < 0.66f ? 3.0f * x - 1.0f : 3.0f * (x - 0.66f) * 2.9f - 1.0f; break;   /* two saws */
                case 62: y = floorf((x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x) * 6.0f) / 3.0f - 1.0f; break;   /* stair tri */
                default: y = rnd[(int)(x * 32.0f)]; break;                            /* random, 32 */
                }
            }
            inc_lfotab[w][i] = clampf(y, -1.0f, 1.0f);
        }
        inc_lfotab[w][LFO_LEN] = inc_lfotab[w][0];
    }
}

/* ABWV0300.raw (the AlphaBase's own LFO waves) in a folder: 1 if it was there */
static int lfo_file_in(const char *dir) {
    static const char *const names[] = {"ABWV0300.raw", "ABWV0300.RAW", "abwv0300.raw"};
    for (int k = 0; k < 3; k++) {
        char path[800];
        snprintf(path, sizeof path, "%s/%s", dir, names[k]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        int16_t b[LFO_WAVES * LFO_LEN];
        size_t n = fread(b, 2, LFO_WAVES * LFO_LEN, f);
        fclose(f);
        if (n != LFO_WAVES * LFO_LEN) return 0;
        for (int w = 0; w < LFO_WAVES; w++) {
            for (int i = 0; i < LFO_LEN; i++) inc_lfotab[w][i] = b[w * LFO_LEN + i] / 32768.0f;
            inc_lfotab[w][LFO_LEN] = inc_lfotab[w][0];
        }
        inc_lfo_from_file = 1;
        return 1;
    }
    return 0;
}

/* in the data folder, or a folder one level down (as the samples, samples.c) */
static void load_lfo_file(const char *dir) {
    if (!dir || lfo_file_in(dir)) return;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[600];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (lfo_file_in(p)) break;
    }
    if (d) closedir(d);
}

static void build_metal(void) {
    /* 2-7 are set by hand; the rest from a hash */
    static const float classic[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};   /* the 808's six squares */
    memset(inc_metal, 0, sizeof inc_metal);
    for (int n = 2; n < 128; n++) {
        metal_pat_t *p = &inc_metal[n];
        uint32_t s = 0xA1FA0000u + (uint32_t)n * 7919u;
        if (n == 2 || n == 3) {   /* the six squares, summed (2) or XORed (3) */
            p->mode = n == 2 ? MT_SUM : MT_XOR;
            p->nsq = 6;
            for (int i = 0; i < 6; i++) p->inc[i] = hz_inc(classic[i] * (n == 2 ? 1.0f : 1.57f));
            continue;
        }
        if (n == 4 || n == 5) {   /* a long register at the sample rate (bright digital noise), then a short buzz */
            p->mode = MT_LFSR;
            p->len = n == 4 ? 17 : 7;
            p->cinc = (uint64_t)((n == 4 ? 44100.0 : 9000.0) / SR * 4294967296.0);
            continue;
        }
        float r = frand(&s);
        p->mode = r < 0.35f ? MT_XOR : r < 0.55f ? MT_SUM : r < 0.8f ? MT_LFSR_XOR : MT_LFSR;
        p->nsq = 2 + (int)(frand(&s) * 5.0f);
        if (p->nsq > 6) p->nsq = 6;
        float base = 140.0f * powf(18.0f, frand(&s));            /* 140 Hz .. 2.5 kHz */
        for (int i = 0; i < p->nsq; i++) {
            float f = base * powf(9.0f, frand(&s));               /* up to 9x: inharmonic clusters */
            p->inc[i] = hz_inc(fminf(f, 15000.0f));
        }
        p->len = 5 + (int)(frand(&s) * 12.0f);                   /* 5 .. 16 bits */
        if (p->len > 17) p->len = 17;
        p->cinc = (uint64_t)(800.0 * pow(90.0, frand(&s)) / SR * 4294967296.0);   /* 800 Hz .. 72 kHz */
    }
}

void inc_tables_init(const char *dir) {
    static int done;
    if (!done) {
        for (int i = 0; i <= SIN_N; i++) inc_sintab[i] = sinf(TWO_PI_F * i / SIN_N);
        build_lfo();
        build_metal();
        done = 1;
    }
    if (!inc_lfo_from_file) load_lfo_file(dir);
}

/* a generator's squares and register start at their own places, as if it had been running (the hardware's patterns
 * run all the time): started in step, the squares XOR to a constant for their first cycles and the first hit is quiet */
void metal_init(metal_t *m, uint32_t seed) {
    memset(m, 0, sizeof *m);
    uint32_t s = seed;
    for (int i = 0; i < 6; i++) m->ph[i] = mix32(s += 0x9e3779b9u);
    m->lfsr = 1u + (mix32(s += 0x9e3779b9u) & 0xFFFFu);
    m->cph = mix32(s += 0x9e3779b9u);
    m->rng = mix32(s + 0x9e3779b9u) | 1u;
    m->rate = 1.0f;
}

/* one sample of metal noise pattern n (0 off, 1 white, 2-127); len_override > 0 sets the shift register's length
 * (the MBrane's NOISE SHAPE on its metal noise 1); m->rate tunes the squares and the register's clock (the sample
 * channels' PITCH; 1 = as made) */
float metal_run(metal_t *m, int n, int len_override) {
    if (n <= 0) return 0.0f;
    if (n == 1) return noise(&m->rng);
    const metal_pat_t *p = &inc_metal[n > 127 ? 127 : n];
    int mode = p->mode, len = len_override > 0 ? len_override : p->len;
    if (len_override > 0 && (mode == MT_XOR || mode == MT_SUM)) mode = MT_LFSR_XOR;   /* the shape needs a register */
    uint32_t bits = 0;
    float sum = 0.0f;
    if (mode != MT_LFSR) {
        for (int i = 0; i < p->nsq; i++) {
            m->ph[i] += m->rate == 1.0f ? p->inc[i] : (uint32_t)(p->inc[i] * m->rate);
            uint32_t b = m->ph[i] >> 31;
            bits ^= b;
            sum += b ? 1.0f : -1.0f;
        }
    }
    if (mode == MT_SUM) return sum * (1.0f / 2.45f);   /* about unit RMS for six */
    if (mode != MT_XOR) {
        if (len < 2) len = 2;
        if (len > 17) len = 17;
        if (!m->lfsr) m->lfsr = 1;
        m->cph += m->rate == 1.0f ? p->cinc : (uint64_t)(p->cinc * m->rate);
        for (uint32_t k = (uint32_t)(m->cph >> 32); k; k--) {
            uint32_t lsb = m->lfsr & 1u;
            m->lfsr >>= 1;
            if (lsb) m->lfsr ^= inc_lfsr_taps[len];
            m->lfsr &= (1u << len) - 1u;
            if (!m->lfsr) m->lfsr = 1;
            m->lbit = lsb;
        }
        m->cph &= 0xFFFFFFFFull;
        bits ^= m->lbit;
    }
    return bits ? 1.0f : -1.0f;
}

/* the LFO: 0-127 RATE free (0.04 Hz .. 60 Hz) or synced (the Mk II: the value is cycles per two bars, 32 = one per
 * 16th, 0 = still); out -1..1 */
float lfo_hz(float rate, int synced, float bpm) {
    if (synced) return rate < 0.5f ? 0.0f : rate * (bpm > 0.0f ? bpm : 120.0f) / 480.0f;
    return expmap(rate, 0.04f, 60.0f);
}
float lfo_run(lfo_t *l, int wave, float hz, int n) {
    l->ph += hz * n * INV_SR;
    l->ph -= floorf(l->ph);
    const float *t = inc_lfotab[wave < 0 ? 0 : wave > 63 ? 63 : wave];
    float x = l->ph * LFO_LEN;
    int i = (int)x;
    float f = x - i;
    l->out = t[i] + (t[i + 1] - t[i]) * f;
    return l->out;
}
