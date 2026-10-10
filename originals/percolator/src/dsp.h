/* Percolator: small DSP building blocks shared by the voices and the master section. */
#pragma once
#include <math.h>
#include <stdint.h>

#define SR 44100.0f
#define INV_SR (1.0f / 44100.0f)
#define PI_F 3.14159265358979f
#define TWO_PI_F 6.28318530717959f

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* tanh, close enough for saturation (exact at 0, +-1 past +-3) */
static inline float sat(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* 2^x for |x| < ~30: exponent bits plus a cubic for the fraction (error < 0.01 %) */
static inline float fexp2(float x) {
    if (x < -30.0f) return 0.0f;
    if (x > 30.0f) x = 30.0f;
    float fl = floorf(x), f = x - fl;
    union { float f; int32_t i; } u;
    u.i = (int32_t)(fl + 127.0f) << 23;
    return u.f * (1.0f + f * (0.6951786f + f * (0.2261587f + f * 0.0782630f)));
}

/* per-sample multiplier that falls 60 dB in t60 seconds */
static inline float t60_coef(float t60) { return t60 <= 0.0f ? 0.0f : expf(-6.9077553f / (t60 * SR)); }

/* sine of a phase in turns (0..1), from a table */
#define SIN_N 4096
extern float perc_sintab[SIN_N + 1];
void perc_tables_init(void);
static inline float sin_t(float ph) {
    ph -= floorf(ph);
    float x = ph * SIN_N;
    int i = (int)x;
    float f = x - i;
    return perc_sintab[i] + (perc_sintab[i + 1] - perc_sintab[i]) * f;
}

/* xorshift noise, -1..1 */
static inline float noise(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (int32_t)x * (1.0f / 2147483648.0f);
}

/* Zero-delay-feedback state-variable filter (Zavalishin's TPT form): stable under fast cutoff changes. */
typedef struct { float s1, s2; } svf_t;
typedef struct { float k, a1, a2, a3; } svfc_t;
static inline void svf_coef(svfc_t *c, float fc, float q) {
    float g = tanf(PI_F * clampf(fc, 8.0f, 0.46f * SR) * INV_SR);
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}
/* returns low-pass; band and high through the pointers when wanted */
static inline float svf_run(svf_t *s, const svfc_t *c, float x, float *bp, float *hp) {
    float v3 = x - s->s2;
    float v1 = c->a1 * s->s1 + c->a2 * v3;
    float v2 = s->s2 + c->a2 * s->s1 + c->a3 * v3;
    s->s1 = 2.0f * v1 - s->s1;
    s->s2 = 2.0f * v2 - s->s2;
    if (bp) *bp = v1;
    if (hp) *hp = x - c->k * v1 - v2;
    return v2;
}

/* one-pole low-pass, coefficient from a cutoff */
static inline float op_coef(float fc) { return 1.0f - expf(-TWO_PI_F * fc * INV_SR); }

/* triangle wavefolder: identity inside -1..1, reflected beyond */
static inline float fold(float x) {
    float u = (x + 1.0f) * 0.25f;
    u -= floorf(u);
    return 1.0f - 4.0f * fabsf(u - 0.5f);
}
