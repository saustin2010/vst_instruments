/* Incipience: the reverb and the delay (manual 7.5), send effects fed by each instrument's REV SEND and DLY SEND.
 *
 * Reverb: eight delay lines mixed through a Householder matrix (a feedback delay network) after four diffusing
 * all-passes. ROOM is the feedback: up to 100 a long but finite tail, above it almost endless and finally ringing on
 * its own, held by a soft clip in the loop (the manual's "resonance catastrophe": careful above 100). LOW PASS and HIGH
 * PASS are 2-pole filters on the reflections (LOW PASS 127 and HIGH PASS 0 = open); set close together they make the
 * tail a resonant band.
 *
 * Delay: a ping-pong pair; SPATIAL 0 keeps the echoes in the middle, 127 throws them left and right in turn. TIME is
 * free (5 ms - 1.4 s) or, with CLOCK = SYNC (the Mk II), a note value from the host tempo. */
#include <stdlib.h>
#include <string.h>
#include "incipience.h"

static const int RV_LEN[RV_LINES] = {1031, 1327, 1523, 1801, 2053, 2381, 2713, 3089};
static const int AP_LEN[4] = {142, 107, 379, 277};
/* SYNC: TIME picks one of these, in beats: 1/32, 1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4, 1/4. */
static const float DIV[10] = {0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f, 1.5f};

void fx_init(fx_t *f) {
    memset(f, 0, sizeof *f);
    f->buf = calloc(RV_LINES * RV_MAX, sizeof(float));
    f->dl[0] = calloc(DL_MAX, sizeof(float));
    f->dl[1] = calloc(DL_MAX, sizeof(float));
    for (int i = 0; i < RV_LINES; i++) f->len[i] = RV_LEN[i];
    for (int i = 0; i < 4; i++) f->apl[i] = AP_LEN[i];
    f->dt = 0.3f * SR;
}

void fx_free(fx_t *f) {
    free(f->buf);
    free(f->dl[0]);
    free(f->dl[1]);
    f->buf = f->dl[0] = f->dl[1] = NULL;
}

float delay_seconds(const inc_t *P) {
    float t = P->pv[P_DL_TIME];
    if (P->pv[P_DL_SYNC] >= 0.5f) {
        int i = (int)(t * 10.0f / 128.0f);
        float bpm = P->bpm > 0.0f ? P->bpm : 120.0f;
        return fminf(DIV[i < 0 ? 0 : i > 9 ? 9 : i] * 60.0f / bpm, (DL_MAX - 4) / SR);
    }
    return expmap(t, 0.005f, 1.4f);
}

/* an effect sleeps once nothing goes in and what comes out has died away (below -120 dB): a silent track then costs
 * next to nothing (TESTS.md); the first sound sent in wakes it, its buffers as they were */
static int silent(const float *x, int n) {
    for (int i = 0; i < n; i++)
        if (fabsf(x[i]) > 1e-6f) return 0;
    return 1;
}

void fx_block(inc_t *P, const float *rin, const float *din, float *L, float *R, int n) {
    fx_t *f = &P->fx;
    if (!silent(rin, n)) f->rv_sleep = 0;
    if (!silent(din, n)) f->dl_sleep = 0;
    float pk = 0.0f;
    /* ---- reverb ---- */
    if (P->pv[P_RV_ON] >= 0.5f && !f->rv_sleep) {
        float rs = P->pv[P_RV_SIZE];
        float g = rs <= 100.0f ? 0.25f + 0.73f * powf(rs / 100.0f, 0.7f) : 0.98f + 0.035f * (rs - 100.0f) / 27.0f;
        svfc_t lp, hp;
        svf_coef(&lp, 300.0f * powf(20000.0f / 300.0f, P->pv[P_RV_LO] / 127.0f), 1.414f);
        svf_coef(&hp, 20.0f * powf(4000.0f / 20.0f, P->pv[P_RV_HI] / 127.0f), 1.414f);
        float lvl = P->pv[P_RV_LEVEL] / 127.0f;
        lvl *= lvl * 0.7f;
        for (int i = 0; i < n; i++) {
            float x = rin[i];
            for (int a = 0; a < 4; a++) {   /* diffusion */
                float *b = f->ap[a];
                int w = f->apw[a];
                float d = b[w];
                float v = x + 0.6f * d;
                b[w] = v;
                x = d - 0.6f * v;
                f->apw[a] = (w + 1) % f->apl[a];
            }
            float o[RV_LINES], sum = 0.0f;
            for (int l = 0; l < RV_LINES; l++) {
                int rp = f->w - f->len[l];
                if (rp < 0) rp += RV_MAX;
                float v = f->buf[l * RV_MAX + rp];
                float h;
                v = svf_run(&f->lp[l], &lp, v, NULL, NULL);
                svf_run(&f->hp[l], &hp, v, NULL, &h);
                o[l] = h;
                sum += h;
            }
            sum *= 2.0f / RV_LINES;
            float outl = 0.0f, outr = 0.0f;
            for (int l = 0; l < RV_LINES; l++) {
                float fb = (o[l] - sum) * g;
                float in = (l & 1 ? -x : x) * 0.35f;
                f->buf[l * RV_MAX + f->w] = 1.5f * sat((fb + in) * (1.0f / 1.5f));
                if (l & 1) outr += o[l]; else outl += o[l];
            }
            f->w = (f->w + 1) % RV_MAX;
            L[i] += outl * lvl;
            R[i] += outr * lvl;
            pk = fmaxf(pk, fabsf(outl) + fabsf(outr));
        }
        if (pk < 1e-6f && silent(rin, n)) f->rv_sleep = 1;
    }
    /* ---- delay ---- */
    if (P->pv[P_DL_ON] >= 0.5f && !f->dl_sleep) {
        pk = 0.0f;
        float target = delay_seconds(P) * SR;
        float fb = 1.02f * P->pv[P_DL_FB] / 127.0f;
        float s = P->pv[P_DL_SPATIAL] / 127.0f;
        float lvl = P->pv[P_DL_LEVEL] / 127.0f;
        lvl *= lvl * 0.8f;
        float dc = op_coef(7000.0f), glide = 1.0f - expf(-1.0f / (0.08f * SR));
        for (int i = 0; i < n; i++) {
            f->dt += (target - f->dt) * glide;
            float rp = (float)f->dw - f->dt;
            if (rp < 0.0f) rp += DL_MAX;
            int j = (int)rp;
            float fr = rp - j;
            int j1 = j + 1 < DL_MAX ? j + 1 : 0;
            float a = f->dl[0][j] + (f->dl[0][j1] - f->dl[0][j]) * fr;
            float b = f->dl[1][j] + (f->dl[1][j1] - f->dl[1][j]) * fr;
            f->dlp[0] += dc * (a - f->dlp[0]);
            f->dlp[1] += dc * (b - f->dlp[1]);
            f->dl[0][f->dw] = sat(din[i] + fb * f->dlp[1]);   /* left takes the input and the right's echo */
            f->dl[1][f->dw] = sat(fb * f->dlp[0]);
            f->dw = (f->dw + 1) % DL_MAX;
            float m = 0.5f * (a + b);
            L[i] += lerpf(m, a, s) * lvl;
            R[i] += lerpf(m, b, s) * lvl;
            pk = fmaxf(pk, fabsf(a) + fabsf(b));
        }
        /* asleep only with the whole line quiet: an echo still on its way would otherwise be lost */
        if (pk < 1e-6f && silent(din, n) && silent(f->dl[0], DL_MAX) && silent(f->dl[1], DL_MAX)) f->dl_sleep = 1;
    }
}
