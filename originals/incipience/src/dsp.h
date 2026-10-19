/* Incipience: small DSP building blocks shared by the instruments and the effects. */
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

static inline float mtof(float note) { return 440.0f * fexp2((note - 69.0f) / 12.0f); }

/* a 0-127 control mapped exponentially from lo to hi */
static inline float expmap(float v, float lo, float hi) { return lo * powf(hi / lo, clampf(v, 0.0f, 127.0f) / 127.0f); }

/* per-sample multiplier that falls 60 dB in t60 seconds */
static inline float t60_coef(float t60) { return t60 <= 0.0f ? 0.0f : expf(-6.9077553f / (t60 * SR)); }

/* one-pole low-pass coefficient from a cutoff */
static inline float op_coef(float fc) { return 1.0f - expf(-TWO_PI_F * clampf(fc, 1.0f, 0.49f * SR) * INV_SR); }

/* sine of a phase in turns (0..1), from a table */
#define SIN_N 4096
extern float inc_sintab[SIN_N + 1];
static inline float sin_t(float ph) {
    ph -= floorf(ph);
    float x = ph * SIN_N;
    int i = (int)x;
    float f = x - i;
    i &= SIN_N - 1;   /* a phase a hair under 0 comes out of the floor as 1: the table's start again */
    return inc_sintab[i] + (inc_sintab[i + 1] - inc_sintab[i]) * f;
}

/* xorshift noise, -1..1 */
static inline float noise(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (int32_t)x * (1.0f / 2147483648.0f);
}

/* Zero-delay-feedback state-variable filter (Zavalishin's TPT form). svf_run is linear; svf_run_nl bounds its two
 * states with a soft clip, the way an analog filter's integrators run out of headroom: what lets a filter with
 * negative damping (k < 0) settle into a steady sine instead of blowing up, and what makes a coupled pair scream.
 * They stay linear up to `lin`, so ordinary ringing keeps its decay. */
typedef struct { float s1, s2; } svf_t;
typedef struct { float k, a1, a2, a3; } svfc_t;
static inline void svf_coef(svfc_t *c, float fc, float k) {
    float g = tanf(PI_F * clampf(fc, 8.0f, 0.45f * SR) * INV_SR);
    c->k = k;
    c->a1 = 1.0f / (1.0f + g * (g + k));
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
/* linear up to lin, then bending smoothly towards head */
static inline float softlim(float x, float lin, float head) {
    float a = fabsf(x);
    if (a <= lin) return x;
    float r = head - lin;
    return copysignf(lin + r * sat((a - lin) / r), x);
}
static inline float svf_run_nl(svf_t *s, const svfc_t *c, float x, float lin, float head, float *bp, float *hp) {
    float y = svf_run(s, c, x, bp, hp);
    s->s1 = softlim(s->s1, lin, head);
    s->s2 = softlim(s->s2, lin, head);
    return y;
}

/* ---- envelopes ------------------------------------------------------------------------------------------------- */

/* ADSR with analog-style curves: the attack charges towards 1.3 (an RC aimed past the top, so it's nearly straight),
 * decay and release fall exponentially. Times in seconds; the coefficients are set per block (adsr_times). */
enum { ENV_IDLE, ENV_ATT, ENV_DEC, ENV_REL };
typedef struct { float v; int stage; float ca, cd, cr, sus; } adsr_t;
static inline void adsr_times(adsr_t *e, float att, float dec, float sus, float rel) {
    e->ca = 1.0f - expf(-1.4663f / (fmaxf(att, 0.0001f) * SR));   /* ln(1.3 / 0.3): reaches 1 in att seconds */
    e->cd = t60_coef(fmaxf(dec, 0.001f));
    e->cr = t60_coef(fmaxf(rel, 0.001f));
    e->sus = sus;
}
static inline void adsr_gate(adsr_t *e, int on) {
    if (on) e->stage = ENV_ATT;
    else if (e->stage != ENV_IDLE) e->stage = ENV_REL;
}
static inline float adsr_run(adsr_t *e) {
    switch (e->stage) {
    case ENV_ATT:
        e->v += (1.3f - e->v) * e->ca;
        if (e->v >= 1.0f) { e->v = 1.0f; e->stage = ENV_DEC; }
        break;
    case ENV_DEC: e->v = e->sus + (e->v - e->sus) * e->cd; break;
    case ENV_REL:
        e->v *= e->cr;
        if (e->v < 1e-5f) { e->v = 0.0f; e->stage = ENV_IDLE; }
        break;
    default: break;
    }
    return e->v;
}

/* attack-decay: rises then falls at once (pitch envelopes, the FM operators' envelopes) */
typedef struct { float v; int stage; float ca, cd; } ad_t;
static inline void ad_times(ad_t *e, float att, float dec) {
    e->ca = 1.0f - expf(-1.4663f / (fmaxf(att, 0.0001f) * SR));
    e->cd = t60_coef(fmaxf(dec, 0.001f));
}
static inline void ad_start(ad_t *e, float att) {
    if (att < 0.0003f) { e->v = 1.0f; e->stage = ENV_DEC; }
    else e->stage = ENV_ATT;
}
static inline float ad_run(ad_t *e) {
    if (e->stage == ENV_ATT) {
        e->v += (1.3f - e->v) * e->ca;
        if (e->v >= 1.0f) { e->v = 1.0f; e->stage = ENV_DEC; }
    } else if (e->stage == ENV_DEC) {
        e->v *= e->cd;
        if (e->v < 1e-5f) { e->v = 0.0f; e->stage = ENV_IDLE; }
    }
    return e->v;
}

/* the AlphaBase's envelope times, 0-127 */
static inline float time_att(float v) { return expmap(v, 0.0002f, 4.0f); }    /* VCA / filter attack: 0.2 ms .. 4 s */
static inline float time_dec(float v) { return expmap(v, 0.004f, 12.0f); }    /* decay, release (to -60 dB) */
