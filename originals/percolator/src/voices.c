/* Percolator voices: the twelve algorithms, then each voice's filter, drive and level.
 *
 *   voice 1 (low):     FOLD (sine + wavefolder)   WAVE (wavetable)        RES BD (pitched resonator)
 *   voice 2 (mid):     FOLD                        WAVE                    FM (two-operator)
 *   voice 3 (snare):   SNARE (resonators + noise)  SLAP (clap bursts)      TOM (resonator with self-FM)
 *   voice 4 (metal):   NOISE (white/pulse/crush)   METAL (cymbal/bell/PCM) SAMPLE (three slots, slices)
 *
 * Each algorithm writes its signal through the decay envelope; voice_block() then runs the voice's HP/BP/LP filter,
 * its drive and level, and sends to the delay. */
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "percolator.h"

float perc_sintab[SIN_N + 1];
#define WT_N 256
static float wt[3][8][WT_N + 1];
#define PCM_N 2048
static float pcm[PCM_N];

/* the built-in samples of voice 4 / SAMPLE: closed hat, open hat, ride (synthesised here at start-up) */
#define HAT_C 7000
#define HAT_O 36000
#define HAT_R 80000
static float hat_c[HAT_C + 1], hat_o[HAT_O + 1], hat_r[HAT_R + 1];
sample_t perc_builtin[NSLOT];

static const float F808[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const float FCYM[6] = {1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f};

/* ---- tables ------------------------------------------------------------------------------------------------- */

static void make_wave(float *w, const double *amp, const double *phs, int nh) {
    double peak = 1e-9;
    for (int i = 0; i < WT_N; i++) {
        double s = 0, t = (double)i / WT_N;
        for (int h = 1; h <= nh; h++)
            if (amp[h] != 0.0) s += amp[h] * sin(2.0 * M_PI * (h * t + phs[h]));
        w[i] = (float)s;
        if (fabs(s) > peak) peak = fabs(s);
    }
    for (int i = 0; i < WT_N; i++) w[i] = (float)(w[i] / peak);
    w[WT_N] = w[0];
}

static void make_tables(void) {
    enum { H = 24 };
    double amp[H + 1], phs[H + 1];
    /* table 1, "analog": sine, warm, triangle, tri/saw, saw, square, pulse 25 %, pulse 10 % */
    for (int k = 0; k < 8; k++) {
        memset(amp, 0, sizeof amp);
        memset(phs, 0, sizeof phs);
        for (int h = 1; h <= H; h++) {
            double sig = h == 1 ? 1.0 : sin(M_PI * h / (H + 1)) / (M_PI * h / (H + 1));   /* Lanczos */
            double tri = (h & 1) ? ((((h - 1) / 2) & 1) ? -1.0 : 1.0) / (h * (double)h) : 0.0;
            double saw = 1.0 / h;
            double sq = (h & 1) ? 1.0 / h : 0.0;
            double a = 0;
            switch (k) {
            case 0: a = h == 1; break;
            case 1: a = h == 1 ? 1.0 : h == 2 ? 0.25 : h == 3 ? 0.08 : 0.0; break;
            case 2: a = tri; break;
            case 3: a = 0.5 * tri + 0.5 * saw * (h > 1 ? 0.7 : 1.0); break;
            case 4: a = saw; break;
            case 5: a = sq; break;
            case 6: a = 2.0 / (M_PI * h) * sin(M_PI * h * 0.25); phs[h] = 0.25; break;
            case 7: a = 2.0 / (M_PI * h) * sin(M_PI * h * 0.10); phs[h] = 0.25; break;
            }
            amp[h] = a * sig;
        }
        make_wave(wt[0][k], amp, phs, H);
    }
    /* table 2, "formant": a resonance that climbs the harmonics */
    for (int k = 0; k < 8; k++) {
        double hc = 1.5 + k * 2.2;
        memset(phs, 0, sizeof phs);
        for (int h = 1; h <= H; h++) amp[h] = exp(-(h - hc) * (h - hc) / (2.0 * 1.6 * 1.6)) + (h == 1 ? 0.6 : 0.0);
        make_wave(wt[1][k], amp, phs, H);
    }
    /* table 3, "digital": sparse harmonic sets with scattered phases */
    for (int k = 0; k < 8; k++) {
        memset(amp, 0, sizeof amp);
        memset(phs, 0, sizeof phs);
        int hs[4] = {1, 3 + k, 6 + 2 * k, 11 + k};
        double as[4] = {1.0, 0.8, 0.6, 0.45};
        for (int j = 0; j < 4; j++) {
            amp[hs[j]] += as[j];
            phs[hs[j]] = 0.13 * j * (k + 1);
        }
        make_wave(wt[2][k], amp, phs, H);
    }
    uint32_t r = 0x9e3779b9u;
    for (int i = 0; i < PCM_N; i++) pcm[i] = noise(&r);
}

/* a metallic hat or ride, for the built-in sample slots */
static void render_hat(float *out, int len, float t60, float fbp, float noise_mix, int ride) {
    float ph[6] = {0.1f, 0.37f, 0.61f, 0.83f, 0.29f, 0.53f};
    svf_t bp = {0, 0}, hp = {0, 0};
    svfc_t cb, ch;
    svf_coef(&cb, fbp, ride ? 0.8f : 1.2f);
    svf_coef(&ch, ride ? 2500.0f : 6000.0f, 0.7f);
    uint32_t r = ride ? 0x1234567u : 0x7654321u;
    float env = 1.0f, mul = t60_coef(t60), peak = 1e-9f;
    for (int i = 0; i < len; i++) {
        float s = 0;
        for (int k = 0; k < 6; k++) {
            float f = ride ? 310.0f * FCYM[k] : F808[k] * 1.7f;
            ph[k] += f * INV_SR;
            ph[k] -= (int)ph[k];
            s += ph[k] < 0.5f ? 1.0f : -1.0f;
        }
        if (ride) s = s * 0.15f + 0.6f * sin_t(ph[0] * 2.0f + ph[3]);   /* the ping of a ride's bell */
        s = s * (1.0f - noise_mix) / 3.0f + noise(&r) * noise_mix;
        float b, h;
        svf_run(&bp, &cb, s, &b, NULL);
        svf_run(&hp, &ch, b, NULL, &h);
        float a = i < 20 ? i / 20.0f : 1.0f;
        out[i] = h * env * a;
        env *= mul;
        if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
    }
    for (int i = 0; i < len; i++) out[i] *= 0.9f / peak;
    out[len] = 0;
}

static pthread_once_t tables_once = PTHREAD_ONCE_INIT;
static void tables_init(void) {
    for (int i = 0; i <= SIN_N; i++) perc_sintab[i] = (float)sin(2.0 * M_PI * i / SIN_N);
    make_tables();
    render_hat(hat_c, HAT_C, 0.09f, 9000.0f, 0.35f, 0);
    render_hat(hat_o, HAT_O, 0.65f, 8500.0f, 0.35f, 0);
    render_hat(hat_r, HAT_R, 1.6f, 5200.0f, 0.2f, 1);
    float *d[NSLOT] = {hat_c, hat_o, hat_r};
    int n[NSLOT] = {HAT_C, HAT_O, HAT_R};
    for (int s = 0; s < NSLOT; s++) {
        perc_builtin[s].d = d[s];
        perc_builtin[s].len = n[s];
        perc_builtin[s].rate = SR;
        perc_builtin[s].ncue = 0;
    }
}
void dsp_tables_init(void) { pthread_once(&tables_once, tables_init); }

/* ---- what the controls mean --------------------------------------------------------------------------------- */

float voice_freq(int vi, float t) {   /* voices 1-3: Hz */
    switch (vi) {
    case 0: return 12.0f * fexp2(6.0f * t);    /* 12 Hz .. 768 Hz */
    case 1: return 25.0f * fexp2(6.5f * t);    /* 25 Hz .. 2.3 kHz */
    default: return 50.0f * fexp2(5.0f * t);   /* 50 Hz .. 1.6 kHz */
    }
}
float voice_t60(int vi, float d) {   /* seconds to fall 60 dB; the top of the knob is DRONE */
    static const float lo[NV] = {0.12f, 0.08f, 0.07f, 0.03f}, span[NV] = {100.0f, 120.0f, 120.0f, 200.0f};
    return lo[vi] * powf(span[vi], d);
}
float v4_attack(float p2) { return 0.0005f * powf(600.0f, p2); }   /* 0.5 ms .. 300 ms */
float sample_ratio(float t) { return fexp2((t - 0.5f) * 4.0f); }    /* +-2 octaves, the middle = as recorded */
static float metal_ratio(float t) { return fexp2((t - 0.5f) * 3.0f); }

static const char *const MODES[NV][3][3] = {
    {{"CLEAN", "NOISE HIT", "PULSE HIT"}, {"ANALOG", "FORMANT", "DIGITAL"}, {"SOFT", "PUNCH", "HARD"}},
    {{"CLEAN", "NOISE HIT", "PULSE HIT"}, {"ANALOG", "FORMANT", "DIGITAL"}, {"SINE MOD", "TRI MOD", "SQUARE MOD"}},
    {{"CLASSIC", "TIGHT", "METALLIC"}, {"3 CLAPS", "4 CLAPS", "5 CLAPS"}, {"FLAT", "DROP", "DEEP DROP"}},
    {{"WHITE", "PULSE STACK", "CRUSH"}, {"CYMBAL", "BELL", "PCM NOISE"}, {"SAMPLE 1", "SAMPLE 2", "SAMPLE 3"}},
};
const char *mode_name(int vi, int algo, int mode) { return MODES[vi & 3][algo % 3][mode % 3]; }

static int bank_samples(const perc_t *P, int bank, int slot) {
    return bank >= 0 && bank < P->nbanks && P->banks[bank].smp[slot].len > 0;
}
int sample_slices(const perc_t *P, int slot) {
    if (!bank_samples(P, P->bank, slot)) return 0;
    const sample_t *s = &P->banks[P->bank].smp[slot];
    return s->ncue >= 2 ? s->ncue - 1 : 0;
}

const char *algo_p_name(int vi, int algo, int mode, int which, const perc_t *P) {
    algo %= 3;
    if (vi < 2) {
        if (which == 2) return "PITCH ENV";
        if (algo == 0) return "FOLD";
        if (algo == 1) return "WAVE POS";
        return vi == 0 ? "ENV TIME" : "FM";
    }
    if (vi == 2) {
        static const char *const n[3][2] = {{"NOISE TONE", "NOISE DECAY"}, {"REVERB", "FILTER"}, {"FM", "TONE"}};
        return n[algo][which - 1];
    }
    if (which == 1) {
        if (algo == 2) return "CRUSH";
        if (algo == 1 && mode == 2) return "SIZE";
        return "REVERB";
    }
    if (algo == 2 && bank_samples(P, P->bank, mode % 3)) return sample_slices(P, mode % 3) ? "SLICE" : "START";
    return "ATTACK";
}

/* ---- the algorithms ----------------------------------------------------------------------------------------- */

typedef struct {
    perc_t *P;
    voice_t *v;
    const vparam_t *vp;
    int vi;
    float f0;          /* Hz (voices 1-3) */
    float rt;          /* frequency ratio (voice 4) */
    float ring;        /* resonator decay per sample */
    float semi;        /* KEYS transposition and bend, semitones */
    const float *e;    /* envelope */
    float *o;
    int n;
} ctx_t;

static inline void rotate(float *zr, float *zi, float turns, float r) {
    float c = sin_t(turns + 0.25f), s = sin_t(turns);
    float a = *zr, b = *zi;
    *zr = r * (c * a - s * b);
    *zi = r * (s * a + c * b);
}

static float pmul(float tau) { return expf(-1.0f / (tau * SR)); }

static void a_fold(ctx_t *c) {
    voice_t *v = c->v;
    const float *k = c->vp->k;
    float oct = (c->vi == 0 ? 6.0f : 4.0f) * powf(k[K_P2], 1.3f);
    float pm = pmul(c->vi == 0 ? 0.028f : 0.018f), g = 1.0f + 7.0f * powf(k[K_P1], 1.5f);
    float trn = pmul(0.003f), trp = pmul(0.001f);
    for (int i = 0; i < c->n; i++) {
        float f = c->f0 * fexp2(oct * v->penv);
        v->penv *= pm;
        v->ph[0] += f * INV_SR;
        v->ph[0] -= (int)v->ph[0];
        float y = fold(sin_t(v->ph[0]) * g * (0.35f + 0.65f * c->e[i])) * c->e[i];
        if (v->tr > 1e-4f) {
            if (c->vp->mode == 1) {
                float nz = noise(&v->rng);
                v->hp1 += 0.25f * (nz - v->hp1);
                y += (nz - v->hp1) * v->tr * 0.7f;
                v->tr *= trn;
            } else if (c->vp->mode == 2) {
                int t = v->ns + i;
                if (t < 40) y += (t < 20 ? 0.9f : -0.9f) * v->tr;
                v->tr *= trp;
            } else v->tr = 0;
        }
        c->o[i] = y * 0.8f;
    }
}

static void a_wave(ctx_t *c) {
    voice_t *v = c->v;
    const float *k = c->vp->k;
    float oct = (c->vi == 0 ? 6.0f : 4.0f) * powf(k[K_P2], 1.3f);
    float pm = pmul(c->vi == 0 ? 0.028f : 0.018f);
    float pos = k[K_P1] * 6.999f;
    int w0 = (int)pos;
    float wf = pos - w0;
    const float *A = wt[c->vp->mode % 3][w0], *B = wt[c->vp->mode % 3][w0 + 1];
    for (int i = 0; i < c->n; i++) {
        float f = c->f0 * fexp2(oct * v->penv);
        v->penv *= pm;
        v->ph[0] += f * INV_SR;
        v->ph[0] -= (int)v->ph[0];
        float x = v->ph[0] * WT_N;
        int j = (int)x;
        float fr = x - j;
        float a = A[j] + (A[j + 1] - A[j]) * fr, b = B[j] + (B[j + 1] - B[j]) * fr;
        c->o[i] = (a + (b - a) * wf) * c->e[i] * 0.75f;
    }
}

static void a_resbd(ctx_t *c) {
    voice_t *v = c->v;
    const float *k = c->vp->k;
    float oct = 5.0f * powf(k[K_P2], 1.3f), pm = pmul(0.003f * powf(100.0f, k[K_P1]));
    int mode = c->vp->mode;
    for (int i = 0; i < c->n; i++) {
        float f = c->f0 * fexp2(oct * v->penv);
        v->penv *= pm;
        rotate(&v->zr[0], &v->zi[0], f * INV_SR, c->ring);
        int t = v->ns + i;
        if (mode == 1 && t < 180) v->zr[0] += noise(&v->rng) * 0.05f * (1.0f - t / 180.0f);
        float y = v->zi[0];
        if (mode == 2) y = sat(y * 2.5f) * 0.7f;
        c->o[i] = y * c->e[i] * 0.85f;
    }
}

static void a_fm(ctx_t *c) {
    voice_t *v = c->v;
    const float *k = c->vp->k;
    float oct = 4.0f * powf(k[K_P2], 1.3f), pm = pmul(0.018f);
    float ratio = 0.5f + 3.5f * k[K_P1], index = 5.0f * powf(k[K_P1], 1.2f) / TWO_PI_F;
    int mode = c->vp->mode;
    for (int i = 0; i < c->n; i++) {
        float f = c->f0 * fexp2(oct * v->penv);
        v->penv *= pm;
        v->ph[0] += f * INV_SR;
        v->ph[0] -= (int)v->ph[0];
        v->ph[1] += f * ratio * INV_SR;
        v->ph[1] -= (int)v->ph[1];
        float m = mode == 0 ? sin_t(v->ph[1]) : mode == 1 ? 1.0f - 4.0f * fabsf(v->ph[1] - 0.5f) : sat(4.0f * sin_t(v->ph[1]));
        c->o[i] = sin_t(v->ph[0] + index * (0.25f + 0.75f * c->e[i]) * m) * c->e[i] * 0.8f;
    }
}

static void a_snare(ctx_t *c) {
    voice_t *v = c->v;
    const float *k = c->vp->k;
    int mode = c->vp->mode;
    svfc_t nc;
    svf_coef(&nc, 1200.0f * powf(12.0f, k[K_P1]), 0.7f);
    float nm = t60_coef(0.04f * powf(30.0f, k[K_P2])), pm = pmul(0.012f), hpc = op_coef(300.0f);
    float r2 = mode == 2 ? 2.31f : 1.58f;
    for (int i = 0; i < c->n; i++) {
        float f = c->f0 * fexp2(0.35f * v->penv);
        v->penv *= pm;
        rotate(&v->zr[0], &v->zi[0], f * INV_SR, c->ring);
        float body;
        if (mode == 1) body = v->zi[0] * 0.6f;
        else {
            rotate(&v->zr[1], &v->zi[1], f * r2 * INV_SR, c->ring);
            body = mode == 0 ? 0.45f * v->zi[0] + 0.3f * v->zi[1] : 0.25f * v->zi[0] + 1.1f * v->zi[0] * v->zi[1];
        }
        float nz = svf_run(&v->f1, &nc, noise(&v->rng), NULL, NULL);
        v->hp1 += hpc * (nz - v->hp1);
        nz -= v->hp1;
        c->o[i] = body * c->e[i] + nz * v->nenv * (mode == 1 ? 1.0f : 0.65f);
        v->nenv *= nm;
    }
}

static const int CLAPS[3] = {3, 4, 5};
static const int CLAP_GAP[3] = {397, 529, 706};   /* 9, 12, 16 ms */

static void a_slap(ctx_t *c) {
    voice_t *v = c->v;
    const float *k = c->vp->k;
    int mode = c->vp->mode % 3, nb = CLAPS[mode], gap = CLAP_GAP[mode];
    svfc_t bc;
    svf_coef(&bc, 400.0f * fexp2(4.0f * k[K_TUNE] + c->semi / 12.0f), 0.6f + 5.0f * k[K_P2]);
    float bm = pmul(0.0026f);
    for (int i = 0; i < c->n; i++) {
        int t = v->ns + i;
        if (v->bursts < nb && t >= v->bursts * gap + (v->bursts ? (int)(v->rng & 63) : 0)) {
            v->bt = 1.0f;
            v->bursts++;
        }
        float a = v->bursts < nb ? v->bt : c->e[i];
        v->bt *= bm;
        float b;
        svf_run(&v->f1, &bc, noise(&v->rng), &b, NULL);
        c->o[i] = b * a * 1.6f;
    }
}

static void a_tom(ctx_t *c) {
    voice_t *v = c->v;
    const float *k = c->vp->k;
    static const float OCT[3] = {0.25f, 0.8f, 2.0f};
    float oct = OCT[c->vp->mode % 3], pm = pmul(0.045f), fm = 0.6f * k[K_P1], trm = pmul(0.005f);
    float hpc = op_coef(800.0f);
    for (int i = 0; i < c->n; i++) {
        float f = c->f0 * fexp2(oct * v->penv) * (1.0f + fm * v->zi[0]);
        v->penv *= pm;
        rotate(&v->zr[0], &v->zi[0], f * INV_SR, c->ring);
        float y = v->zi[0] * c->e[i];
        if (v->tr > 1e-4f) {
            float nz = noise(&v->rng);
            v->hp1 += hpc * (nz - v->hp1);
            y += (nz - v->hp1) * v->tr * 0.4f;
            v->tr *= trm;
        }
        c->o[i] = y * 0.85f;
    }
}

static void a_noise(ctx_t *c) {
    voice_t *v = c->v;
    int mode = c->vp->mode;
    svfc_t a, b;
    if (mode == 0) {
        svf_coef(&a, 4000.0f * c->rt, 0.7f);
        for (int i = 0; i < c->n; i++) {
            float h;
            svf_run(&v->f1, &a, noise(&v->rng), NULL, &h);
            c->o[i] = h * c->e[i] * 0.75f;
        }
    } else if (mode == 1) {
        svf_coef(&a, 7500.0f * sqrtf(c->rt), 1.2f);
        svf_coef(&b, 6000.0f * sqrtf(c->rt), 0.7f);
        float inc[6];
        for (int j = 0; j < 6; j++) inc[j] = F808[j] * 1.7f * c->rt * INV_SR;
        for (int i = 0; i < c->n; i++) {
            float s = 0;
            for (int j = 0; j < 6; j++) {
                v->ph[j] += inc[j];
                v->ph[j] -= (int)v->ph[j];
                s += v->ph[j] < 0.5f ? 1.0f : -1.0f;
            }
            float bp, h;
            svf_run(&v->f1, &a, s * 0.3f, &bp, NULL);
            svf_run(&v->f2, &b, bp, NULL, &h);
            c->o[i] = h * c->e[i] * 2.2f;
        }
    } else {
        float rate = 1500.0f * powf(10.0f, c->vp->k[K_TUNE]) * fexp2(c->semi / 12.0f) * INV_SR;
        svf_coef(&a, 1500.0f, 0.7f);
        for (int i = 0; i < c->n; i++) {
            v->holdc += rate;
            if (v->holdc >= 1.0f) {
                v->holdc -= (int)v->holdc;
                v->hold = roundf(noise(&v->rng) * 16.0f) / 16.0f;
            }
            float h;
            svf_run(&v->f1, &a, v->hold, NULL, &h);
            c->o[i] = h * c->e[i] * 0.65f;
        }
    }
}

static void a_metal(ctx_t *c) {
    voice_t *v = c->v;
    int mode = c->vp->mode;
    svfc_t a, b;
    if (mode == 0) {
        svf_coef(&a, 5000.0f * sqrtf(c->rt), 0.9f);
        svf_coef(&b, 3000.0f * sqrtf(c->rt), 0.7f);
        float inc[6];
        for (int j = 0; j < 6; j++) inc[j] = 320.0f * FCYM[j] * c->rt * INV_SR;
        for (int i = 0; i < c->n; i++) {
            float s[6];
            for (int j = 0; j < 6; j++) {
                v->ph[j] += inc[j];
                v->ph[j] -= (int)v->ph[j];
                s[j] = v->ph[j] < 0.5f ? 1.0f : -1.0f;
            }
            float x = (s[0] * s[1] + s[2] * s[3] + s[4] * s[5]) * 0.33f, bp, h;
            svf_run(&v->f1, &a, x, &bp, NULL);
            svf_run(&v->f2, &b, bp, NULL, &h);
            c->o[i] = h * c->e[i] * 1.6f;
        }
    } else if (mode == 1) {
        svf_coef(&a, 400.0f, 0.7f);
        float base = 380.0f * c->rt * INV_SR;
        for (int i = 0; i < c->n; i++) {
            v->ph[0] += base;
            v->ph[1] += base * 1.4f;
            v->ph[2] += base * 3.17f;
            for (int j = 0; j < 3; j++) v->ph[j] -= (int)v->ph[j];
            float m2 = sin_t(v->ph[2]);
            float m1 = sin_t(v->ph[1] + 0.19f * m2);
            float y = sin_t(v->ph[0] + (0.1f + 0.3f * c->e[i]) * m1), h;
            svf_run(&v->f1, &a, y, NULL, &h);
            c->o[i] = h * c->e[i] * 0.7f;
        }
    } else {
        int len = 16 + (int)(c->vp->k[K_P1] * c->vp->k[K_P1] * (PCM_N - 16));
        svf_coef(&a, 300.0f, 0.7f);
        float step = 2.0f * c->rt;
        for (int i = 0; i < c->n; i++) {
            v->ph[0] += step;
            if (v->ph[0] >= len) v->ph[0] -= len * (int)(v->ph[0] / len);
            float h;
            svf_run(&v->f1, &a, pcm[(int)v->ph[0]], NULL, &h);
            c->o[i] = h * c->e[i] * 0.55f;
        }
    }
}

static void a_sample(ctx_t *c) {
    voice_t *v = c->v;
    const perc_t *P = c->P;
    int slot = c->vp->mode % 3;
    const sample_t *s = slot_sample(P, v->slot_bank, slot);
    float p1 = c->vp->k[K_P1];
    float step = s->rate * INV_SR * sample_ratio(c->vp->k[K_TUNE]) * fexp2(c->semi / 12.0f);
    float holdn = 1.0f + 31.0f * p1 * p1;
    float levels = p1 > 0.5f ? fexp2(11.0f - (p1 - 0.5f) * 16.0f) : 0.0f;   /* 2048 .. 16 steps */
    int end = v->send < s->len ? v->send : s->len;
    for (int i = 0; i < c->n; i++) {
        float x = 0;
        if (v->spos < end) {
            int j = (int)v->spos;
            float fr = (float)(v->spos - j);
            x = s->d[j] + (s->d[j + 1] - s->d[j]) * fr;
            v->spos += step;
        } else v->active = 0;
        v->holdc += 1.0f;
        if (v->holdc >= holdn) {
            v->holdc -= holdn;
            v->hold = levels > 0 ? roundf(x * levels) / levels : x;
        }
        c->o[i] = (holdn > 1.01f || levels > 0 ? v->hold : x) * c->e[i];
    }
}

/* ---- the room (voice 3 SLAP, voice 4 NOISE / METAL: PARAM 1 = reverb) -------------------------------------- */

static const int COMB_L[4] = {347, 389, 431, 467}, AP_L[2] = {131, 211};

static void room_run(room_t *r, float *o, float amount, int n) {
    for (int i = 0; i < n; i++) {
        float in = o[i] * amount * 0.3f, acc = 0;
        for (int j = 0; j < 4; j++) {
            float y = r->comb[j][r->ci[j]];
            r->damp[j] += 0.35f * (y - r->damp[j]);
            r->comb[j][r->ci[j]] = in + r->damp[j] * 0.86f;
            if (++r->ci[j] >= COMB_L[j]) r->ci[j] = 0;
            acc += y;
        }
        for (int j = 0; j < 2; j++) {
            float wd = r->ap[j][r->ai[j]], w = acc + 0.6f * wd;
            acc = wd - 0.6f * w;
            r->ap[j][r->ai[j]] = w;
            if (++r->ai[j] >= AP_L[j]) r->ai[j] = 0;
        }
        o[i] += acc * amount;
    }
}

static int uses_room(int vi, int algo, int mode) {
    return (vi == 2 && algo == 1) || (vi == 3 && (algo == 0 || (algo == 1 && mode != 2)));
}

/* ---- trigger, release, render ------------------------------------------------------------------------------- */

void voice_trigger(perc_t *P, int vi, const vparam_t *vp, float vel, int note, float semi) {
    voice_t *v = &P->v[vi];
    v->dclk = clampf(v->dclk + v->last, -1.5f, 1.5f);
    v->last = 0;
    v->active = 1;
    v->gate = 1;
    v->note = note;
    v->semi = semi;
    v->vel = powf(clampf(vel, 1.0f, 127.0f) / 127.0f, 1.4f);
    v->env = 1.0f;
    v->atk = 0;
    v->penv = 1.0f;
    v->tr = 1.0f;
    v->nenv = 1.0f;
    v->bt = 0;
    v->bursts = 0;
    v->ns = 0;
    v->hold = 0;
    v->holdc = 0;
    v->slot_bank = P->bank;
    const float *k = vp->k;
    v->amp = k[K_LEVEL] * k[K_LEVEL] * v->vel;   /* the hit at its level at once (the smoothing is for knob moves) */
    int algo = vp->algo, mode = vp->mode;
    if (vi < 2 || (vi == 2 && algo != 1)) {
        v->ph[0] = v->ph[1] = 0;
        v->zr[0] = v->zi[0] = v->zr[1] = v->zi[1] = 0;
    }
    if (vi == 0 && algo == 2) {
        static const float ER[3] = {0.9f, 0.35f, 0.2f}, EI[3] = {0.0f, 0.85f, 0.95f};
        v->zr[0] = ER[mode % 3];
        v->zi[0] = EI[mode % 3];
    } else if (vi == 2 && algo == 0) {
        v->zr[0] = 0.3f; v->zi[0] = 0.7f;
        v->zr[1] = 0.25f; v->zi[1] = 0.5f;
    } else if (vi == 2 && algo == 2) {
        v->zr[0] = 0.85f * (1.0f - k[K_P2]);
        v->zi[0] = 0.25f + 0.75f * k[K_P2];
        v->tr = k[K_P2];
    } else if (vi == 3 && algo == 2) {
        int slot = mode % 3;
        const sample_t *s = slot_sample(P, P->bank, slot);
        v->sstart = 0;
        v->send = s->len;
        if (bank_samples(P, P->bank, slot)) {
            int nsl = s->ncue >= 2 ? s->ncue - 1 : 0;
            if (nsl) {
                int q = (int)(k[K_P2] * nsl);
                if (q > nsl - 1) q = nsl - 1;
                v->sstart = s->cue[q];
                v->send = s->cue[q + 1];
            } else v->sstart = (int)(k[K_P2] * 0.98f * s->len);
        }
        v->spos = v->sstart;
    }
}

void voice_release(perc_t *P, int vi) { P->v[vi].gate = 0; }

void voice_block(perc_t *P, int vi, const vparam_t *vp, float *mix, float *send, int n) {
    voice_t *v = &P->v[vi];
    const float *k = vp->k;
    int algo = vp->algo % 3, mode = vp->mode % 3;
    int room = uses_room(vi, algo, mode);
    if (!v->active && v->room.tail <= 0) {
        v->last = 0;
        v->dclk = 0;
        return;
    }
    float e[SUB], o[SUB];
    /* the envelope: an attack ramp, then a fall of 60 dB in DECAY's time; at the top of DECAY the voice drones
     * until its note is released */
    int drone = k[K_DECAY] >= 0.99f;
    v->drone = drone;
    float mul = drone ? (v->gate ? 1.0f : t60_coef(0.35f)) : t60_coef(voice_t60(vi, k[K_DECAY]));
    float at = 0.0001f;
    if (vi == 3 && (algo != 2 || !bank_samples(P, v->slot_bank, mode))) at = v4_attack(k[K_P2]);
    float ainc = 1.0f / (at * SR);
    int hold = (vi == 2 && algo == 1) ? (CLAPS[mode] - 1) * CLAP_GAP[mode] : 0;
    for (int i = 0; i < n; i++) {
        v->atk = v->atk + ainc < 1.0f ? v->atk + ainc : 1.0f;
        if (v->ns + i >= hold) v->env *= mul;
        e[i] = v->env * v->atk;
    }
    float semi = v->semi + (v->note < 36 || v->note > 39 ? P->bend : 0);
    ctx_t c = {P, v, vp, vi, 0, 1, 1, semi, e, o, n};
    if (vi < 3) c.f0 = voice_freq(vi, k[K_TUNE]) * fexp2(semi / 12.0f);
    else c.rt = metal_ratio(k[K_TUNE]) * fexp2(semi / 12.0f);
    c.ring = drone ? 1.0f : t60_coef(voice_t60(vi, k[K_DECAY]) * (vi == 0 ? 2.0f : 1.5f));
    if (v->active) {
        switch (vi * 3 + algo) {
        case 0: case 3: a_fold(&c); break;
        case 1: case 4: a_wave(&c); break;
        case 2: a_resbd(&c); break;
        case 5: a_fm(&c); break;
        case 6: a_snare(&c); break;
        case 7: a_slap(&c); break;
        case 8: a_tom(&c); break;
        case 9: a_noise(&c); break;
        case 10: a_metal(&c); break;
        default: a_sample(&c); break;
        }
        v->ns += n;
        float lvl = v->env * v->atk;
        if (!drone && lvl < 1e-4f && v->nenv < 1e-4f && (vi != 2 || algo != 1 || v->bursts >= CLAPS[mode])) v->active = 0;
        if (room) v->room.tail = (int)(1.5f * SR);
    } else {
        memset(o, 0, sizeof o);
        v->room.tail -= n;
    }
    if (room && (k[K_P1] > 0.005f || v->room.tail > 0)) room_run(&v->room, o, k[K_P1], n);
    else if (!v->active) v->room.tail = 0;
    v->last = v->active ? o[n - 1] : 0;

    /* the "analog" end: HP / BP / LP (no resonance control), overdrive, level */
    v->cut_s += (k[K_CUTOFF] - v->cut_s) * 0.3f;
    svfc_t fc;
    svf_coef(&fc, 20.0f * fexp2(v->cut_s * 9.9658f), 0.9f);
    float d = k[K_DRIVE], g = 1.0f + 24.0f * d * d, bias = 0.12f * d, sb = sat(bias), mk = 1.0f / (1.0f + 0.6f * d);
    float tgt = k[K_LEVEL] * k[K_LEVEL] * v->vel, fxs = k[K_FX] * k[K_FX];
    int vcf = vp->vcf;
    for (int i = 0; i < n; i++) {
        float y = o[i] + v->dclk, bp, hp;
        v->dclk *= 0.985f;
        float lp = svf_run(&v->out, &fc, y, &bp, &hp);
        y = vcf == 2 ? lp : vcf == 1 ? bp * 1.4f : hp;
        y = (sat(g * y + bias) - sb) * mk;
        v->amp += (tgt - v->amp) * 0.02f;
        y *= v->amp;
        mix[i] += y;
        send[i] += y * fxs;
    }
}
