/* Incipience: the eleven instruments. Each follows its chapter of the AlphaBase manual (its block diagrams and what each
 * control is said to do); the analog circuits are modelled from that, not measured, so a sound is in the hardware's
 * spirit rather than a copy of it.
 *
 * Levels: every instrument plays at VOLUME / 127 x (1/2 + 1/2 x ACCENT / 127 x velocity / 127), the hardware's accent
 * rule (no accent = half the volume, full accent = volume) with a pad's velocity as how much accent; worked out each
 * block, so VOLUME and ACCENT can be modulated. An instrument played on its KEYS CH is transposed by its note against
 * C3 (play_ratio: its oscillators or its sample, and the metal noise) and follows pitch bend on that channel. */
#include <math.h>
#include <string.h>
#include "incipience.h"

#define PV(i) (P->ev[(i)])   /* the controls after the mod matrix */

static float level(float vol, float acc, float vel) {
    float v = vol / 127.0f;
    return v * v * (0.5f + 0.5f * (acc / 127.0f) * (vel / 127.0f)) * 1.25f;
}

float smp_ratio(float pitch, float cents) { return fexp2((pitch - 55.0f + cents / 100.0f) / 12.0f); }
float kick_hz(float pitch) { return 16.0f * fexp2(pitch * 5.3f / 127.0f); }          /* 16 Hz .. 630 Hz */
float mb_hz(float pitch) { return 25.0f * powf(560.0f, pitch / 127.0f); }             /* 25 Hz .. 14 kHz */
float mm_cut_hz(float v) { return 10.0f * powf(2000.0f, v / 127.0f); }                /* 10 Hz .. 20 kHz */
float hh_lp_hz(float v) { return 400.0f * powf(50.0f, v / 127.0f); }                  /* 400 Hz .. 20 kHz */
float hh_hp_hz(float v) { return 250.0f * powf(64.0f, v / 127.0f); }                  /* 250 Hz .. 16 kHz */
float fm_op_hz(float tune, int note) { return mtof(tune + 12.0f + (float)(note - 36)); }   /* tune 0 at C1 = C0 */
static float gate_s(float v) { return 0.0001f * powf(150.0f, v / 127.0f); }          /* 0.1 ms .. 15 ms */
static float lfo_semis(float depth) { float d = depth / 127.0f; return 24.0f * d * d; }

/* ======================================== KICK (manual 6.1) ======================================================
 * An oscillator (a sine, shaped towards pointed peaks by HARMONICS) swept down by the tune envelope (TUNE sets how far,
 * the 909's "tune") to PITCH, through a VCA that falls over DECAY, with COMPRESS pulling the body down after the
 * attack. The attack: a click (PULSE) and a burst of noise (NOISE, white or a metal pattern from METAL NZ), each as
 * long as GATE (0.1-15 ms), mixed by ATTACK; GATE also sets how much the trigger charges the envelope. EQ is a gentle
 * low-pass over the lot. A second pitch envelope (PITCH ATK / DEC / AMT) bends it up and back, for 808-style glides;
 * the LFO (restarted with each hit) moves the pitch. */
void kick_trigger(inc_t *P, const play_t *pl) {
    kick_t *k = &P->kd;
    k->pl = *pl;
    k->active = 1;
    k->n = 0;
    k->ph = 0.0f;
    float g = gate_s(PV(P_KD_GATE));
    k->gate_n = (int)(g * SR) + 1;
    k->amp = 0.6f + 0.4f * (1.0f - expf(-g / 0.0025f));   /* a longer trigger puts more energy into the envelope */
    k->cenv = 1.0f;
    k->tenv = 1.0f;
    k->lfo.ph = 0.0f;
    ad_times(&k->pe, expmap(PV(P_KD_PTATK), 0.0002f, 0.5f), expmap(PV(P_KD_PTDEC), 0.005f, 3.0f));
    k->pe.v = 0.0f;
    ad_start(&k->pe, expmap(PV(P_KD_PTATK), 0.0002f, 0.5f));
    k->click_prev = 0.0f;
}

static float harm_norm(float p) {   /* RMS of |sin|^p, against a sine's */
    float r = expf(0.5f * (lgammaf(p + 0.5f) - lgammaf(p + 1.0f))) / powf(PI_F, 0.25f);
    return 0.70710678f / r;
}

void kick_block(inc_t *P, float *y, int n) {
    kick_t *k = &P->kd;
    if (!k->active) return;
    float tune = PV(P_KD_TUNE) / 127.0f;
    float K = 9.0f * powf(tune, 1.7f);
    float tc = expf(-1.0f / (0.006f * SR));
    float ac = t60_coef(expmap(PV(P_KD_DECAY), 0.05f, 5.0f));
    float c = PV(P_KD_COMPR) / 127.0f, cc = expf(-1.0f / (0.012f * SR));
    float hp = 1.0f + 2.5f * PV(P_KD_HARM) / 127.0f, hn = harm_norm(hp);
    hn = sqrtf(hn);   /* half the loudness made back: the pointed wave should sound harder, not just quieter */
    float att = PV(P_KD_ATTACK) / 127.0f;
    float pulse = att * PV(P_KD_PULSE) / 127.0f, nz = att * PV(P_KD_NOISE) / 127.0f;
    int metal = (int)PV(P_KD_METNZ);
    float eqc = op_coef(18000.0f * fexp2(-7.0f * PV(P_KD_EQ) / 127.0f));
    float hpc = op_coef(80.0f);
    float f0 = kick_hz(PV(P_KD_PITCH)) * INV_SR * play_ratio(P, &k->pl);
    k->lvl = level(PV(P_KD_VOL), PV(P_KD_ACC), k->pl.vel);
    float pamt = 24.0f * powf(PV(P_KD_EVAMT) / 127.0f, 1.5f);
    float lsemi = lfo_run(&k->lfo, (int)PV(P_KD_LFWAV), lfo_hz(PV(P_KD_LFRAT), 0, P->bpm), n) * lfo_semis(PV(P_KD_LFINT));
    ad_times(&k->pe, expmap(PV(P_KD_PTATK), 0.0002f, 0.5f), expmap(PV(P_KD_PTDEC), 0.005f, 3.0f));
    for (int i = 0; i < n; i++) {
        k->tenv *= tc;
        float pe = ad_run(&k->pe);
        float f = f0 * (1.0f + K * k->tenv) * fexp2((pamt * pe + lsemi) / 12.0f);
        k->ph += f;
        if (k->ph >= 1.0f) k->ph -= 1.0f;
        float s = sin_t(k->ph);
        if (hp > 1.001f) s = copysignf(powf(fabsf(s), hp), s) * hn;
        k->amp *= ac;
        k->cenv *= cc;
        float body = s * k->amp * ((1.0f - c) + c * k->cenv);
        /* the attack: a rectangular click as long as GATE (AC-coupled) and noise for as long */
        float in_gate = k->n < k->gate_n ? 1.0f : 0.0f;
        k->click_hp += hpc * (in_gate - k->click_hp);
        float click = (in_gate - k->click_hp) * pulse * 0.9f;
        if (in_gate > 0.0f && nz > 0.0f) click += metal_run(&k->nz, metal, 0) * nz * 0.6f;
        float x = body * 0.95f + click;
        k->eq += eqc * (x - k->eq);
        y[i] += k->eq * k->lvl;
        k->n++;
    }
    if (k->amp < 1e-4f && k->n > k->gate_n + 64) k->active = 0;
}

/* ======================================== MBRANE (manual 6.2) ====================================================
 * Two filter oscillators, M1 and M2 (resonant band-pass filters, 25 Hz - 14 kHz): DAMPEN sets how long each rings, and
 * at the top it rings on as an oscillator (negative damping, held by the filter's headroom). The trigger pulse (GATE
 * long) and a noise burst strike both; each feeds part of its band-pass output to the other (CPL 1>2; CPL 2>1 either
 * way round, 64 = none), which also bends the other's pitch a little: with both the same sign and the two near unison,
 * a fifth or an octave they can scream, opposite signs damp each other. The noise (white or metal 1 + metal 2, NOISE
 * SHAPE setting metal 1's register length) goes through a 6 dB low-pass (NOISE FILT, 0 = off) and its own envelope,
 * always shorter than the final VCA's (DECAY sets both); a little of it goes straight to the VCA. Each oscillator has a
 * pitch envelope and an LFO, restarted with each hit.
 *
 * SCENE 1-9 (the Mk II): ready-made sounds with a limited tuning range: 1-4 snares, 5-7 toms, 8-9 kicks. M1 / M2 PITCH
 * then tune the scene up or down (64 = as made); DECAY, the LFOs, VOLUME and ACCENT stay yours. */
enum { SC_P1, SC_D1, SC_P2, SC_D2, SC_C1, SC_C2, SC_NZ, SC_E1, SC_P1D, SC_E2, SC_P2D, SC_NF, SC_M1, SC_SH, SC_M2,
       SC_GATE, SC_N };
static const float SCENE[9][SC_N] = {
    /*          p1  d1  p2  d2  c12 c21 noise e1  p1d e2  p2d nzf mt1 shp mt2 gate */
    /* snare */ {40, 50, 52, 45, 15, 64, 85, 10, 20, 0, 20, 110, 1, 6, 0, 25},
    /* snare */ {42, 42, 56, 40, 25, 50, 95, 14, 16, 8, 16, 120, 1, 6, 0, 15},
    /* snare */ {46, 55, 61, 50, 30, 80, 80, 8, 18, 0, 20, 127, 37, 9, 0, 22},
    /* snare */ {35, 64, 48, 55, 10, 64, 70, 16, 26, 0, 20, 90, 1, 6, 0, 40},
    /* tom   */ {25, 78, 34, 60, 15, 64, 15, 30, 40, 20, 40, 60, 1, 6, 0, 45},
    /* tom   */ {34, 76, 45, 58, 15, 64, 15, 28, 38, 18, 38, 64, 1, 6, 0, 40},
    /* tom   */ {47, 72, 59, 50, 40, 40, 10, 20, 30, 10, 30, 70, 1, 6, 0, 30},
    /* kick  */ {14, 90, 28, 70, 20, 64, 10, 50, 25, 20, 25, 40, 1, 6, 0, 80},
    /* kick  */ {17, 84, 40, 40, 10, 64, 20, 60, 18, 30, 18, 70, 1, 6, 0, 60},
};

static float damp_k(float d) {   /* DAMPEN -> the filter's damping (1/Q): constant Q, like the circuit */
    if (d <= 118.0f) return 1.0f / (0.6f * powf(2500.0f / 0.6f, d / 118.0f));
    return -0.0006f * (d - 118.0f);
}

void mbrane_trigger(inc_t *P, const play_t *pl) {
    mbrane_t *m = &P->mb;
    m->pl = *pl;
    int sc = (int)PV(P_MB_SCENE);
    float gate = sc ? SCENE[sc - 1][SC_GATE] : PV(P_MB_GATE);
    m->active = 1;
    m->n = 0;
    m->gate_n = (int)(gate_s(gate) * SR) + 1;
    m->nenv = 1.0f;
    m->venv = 1.0f;
    m->l1.ph = m->l2.ph = 0.0f;
    float p1a = expmap(PV(P_MB_P1ATK), 0.0002f, 0.5f), p2a = expmap(PV(P_MB_P2ATK), 0.0002f, 0.5f);
    m->pe1.v = m->pe2.v = 0.0f;
    ad_start(&m->pe1, p1a);
    ad_start(&m->pe2, p2a);
}

static void fosc_coef(svfc_t *c, float g, float k) {
    c->k = k;
    c->a1 = 1.0f / (1.0f + g * (g + k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

void mbrane_block(inc_t *P, float *y, int n) {
    mbrane_t *m = &P->mb;
    if (!m->active) return;
    int sc = (int)PV(P_MB_SCENE);
    const float *S = sc ? SCENE[sc - 1] : NULL;
    float p1 = S ? S[SC_P1] + (PV(P_MB_PITCH1) - 64.0f) * 0.19f : PV(P_MB_PITCH1);   /* a scene: +-12 steps */
    float p2 = S ? S[SC_P2] + (PV(P_MB_PITCH2) - 64.0f) * 0.19f : PV(P_MB_PITCH2);
    float d1 = S ? S[SC_D1] : PV(P_MB_DAMP1), d2 = S ? S[SC_D2] : PV(P_MB_DAMP2);
    float cp1 = S ? S[SC_C1] : PV(P_MB_CUPL1), cp2 = S ? S[SC_C2] : PV(P_MB_CUPL2);
    float nzl = S ? S[SC_NZ] : PV(P_MB_NOISE), nzf = S ? S[SC_NF] : PV(P_MB_NZFIL);
    int mt1 = (int)(S ? S[SC_M1] : PV(P_MB_MTNZ1)), mt2 = (int)(S ? S[SC_M2] : PV(P_MB_MTNZ2));
    int shp = (int)(S ? S[SC_SH] : PV(P_MB_NZSHP));
    float e1 = S ? S[SC_E1] : PV(P_MB_E1AMT), e2 = S ? S[SC_E2] : PV(P_MB_E2AMT);
    float p1d = S ? S[SC_P1D] : PV(P_MB_P1DEC), p2d = S ? S[SC_P2D] : PV(P_MB_P2DEC);
    ad_times(&m->pe1, expmap(PV(P_MB_P1ATK), 0.0002f, 0.5f), expmap(p1d, 0.004f, 2.0f));
    ad_times(&m->pe2, expmap(PV(P_MB_P2ATK), 0.0002f, 0.5f), expmap(p2d, 0.004f, 2.0f));
    float l1 = lfo_run(&m->l1, (int)PV(P_MB_L1WAV), lfo_hz(PV(P_MB_L1RAT), 0, P->bpm), n) * lfo_semis(PV(P_MB_L1INT));
    float l2 = lfo_run(&m->l2, (int)PV(P_MB_L2WAV), lfo_hz(PV(P_MB_L2RAT), 0, P->bpm), n) * lfo_semis(PV(P_MB_L2INT));
    float a1 = 36.0f * powf(e1 / 127.0f, 1.5f), a2 = 36.0f * powf(e2 / 127.0f, 1.5f);
    /* coupling: each feeds the other its band-pass output normalised to unity gain at resonance (x its damping), so
     * the loop gain at resonance is CPL 1>2 x CPL 2>1: past 1 with the same sign (both tuned close) the pair feeds
     * itself into a scream, held by the filters' headroom; with opposite signs the loop takes energy out of both and
     * they ring shorter (the manual: "at opposing signs, they attenuate each other") */
    float c12 = 1.3f * powf(cp1 / 127.0f, 1.3f);
    float c21v = (cp2 - 64.0f) / 63.0f, c21 = 1.3f * copysignf(powf(fabsf(clampf(c21v, -1.0f, 1.0f)), 1.3f), c21v);
    float k1 = damp_k(d1), k2 = damp_k(d2);
    float kn1 = fmaxf(k1, 0.002f), kn2 = fmaxf(k2, 0.002f);
    float nl = 1.2f * powf(nzl / 127.0f, 1.5f);
    float nlc = nzf < 0.5f ? 0.0f : op_coef(150.0f * powf(16000.0f / 150.0f, (nzf - 1.0f) / 126.0f));
    /* the noise into a resonator: scaled by its damping, so a high-Q one is struck, not driven into a whistle */
    float ns1 = sqrtf(fmaxf(k1, 0.002f)), ns2 = sqrtf(fmaxf(k2, 0.002f));
    float tv = expmap(PV(P_MB_DECAY), 0.02f, 6.0f);
    float vc = t60_coef(tv), ncf = t60_coef(0.3f * tv);
    float tr = play_ratio(P, &m->pl), base1 = mb_hz(p1) * tr, base2 = mb_hz(p2) * tr;
    m->lvl = level(PV(P_MB_VOL), PV(P_MB_ACC), m->pl.vel);
    for (int i = 0; i < n; i++) {
        float pe1 = ad_run(&m->pe1), pe2 = ad_run(&m->pe2);
        float f1 = fminf(base1 * fexp2((a1 * pe1 + l1) / 12.0f), 0.45f * SR);
        float f2 = fminf(base2 * fexp2((a2 * pe2 + l2) / 12.0f), 0.45f * SR);
        /* coupling also frequency-modulates the partner a little */
        float w1 = PI_F * f1 * INV_SR, w2 = PI_F * f2 * INV_SR;
        float g1 = tanf(w1), g2 = tanf(w2);
        float dm1 = 0.15f * c21 * kn2 * m->bp2, dm2 = 0.15f * c12 * kn1 * m->bp1;
        g1 = fmaxf(g1 + dm1 * w1 * (1.0f + g1 * g1), 1e-5f);
        g2 = fmaxf(g2 + dm2 * w2 * (1.0f + g2 * g2), 1e-5f);
        svfc_t c1, c2;
        fosc_coef(&c1, g1, k1);
        fosc_coef(&c2, g2, k2);
        /* excitation: the trigger pulse and the noise */
        float pulse = m->n < m->gate_n ? 1.0f : 0.0f;
        float nz = 0.0f;
        if (nl > 0.0f && nlc > 0.0f) {
            float raw = metal_run(&m->m1, mt1, mt1 >= 2 ? 3 + shp : 0) + metal_run(&m->m2, mt2, 0);
            m->nlp += nlc * (raw - m->nlp);
            nz = m->nlp * nl * m->nenv;
        }
        m->nenv *= ncf;
        float in1 = sat(pulse + nz * 0.35f * ns1 * 4.0f + c21 * kn2 * m->bp2);
        float in2 = sat(pulse + nz * 0.35f * ns2 * 4.0f + c12 * kn1 * m->bp1);
        float b1, b2;
        svf_run_nl(&m->f1, &c1, in1, 2.0f, 4.0f, &b1, NULL);
        svf_run_nl(&m->f2, &c2, in2, 2.0f, 4.0f, &b2, NULL);
        m->bp1 = b1;
        m->bp2 = b2;
        m->venv *= vc;
        float out = (b1 + b2) * 0.45f + nz * 0.25f;
        y[i] += sat(out * m->venv) * m->lvl;
        m->n++;
    }
    if (m->venv < 1e-4f) {
        m->active = 0;
        memset(&m->f1, 0, sizeof m->f1);
        memset(&m->f2, 0, sizeof m->f2);
        m->bp1 = m->bp2 = 0.0f;
    }
}

/* =========================== the sample channels (manual 7.1-7.3): CH OH CP RM CR RD, then X1 X2 =====================
 * A sample (number 1-500, the AlphaBase's ABWV<n>.raw in the data folder; 0 = none) played from START to STOP (in the
 * hardware's units: 512 frames of 48 kHz a step, 127 = the end), looping back to LOOP if LOOP ON, or backwards. On
 * the six hybrid channels, ROUTING 0 sends it straight to the VCA (no filter, no noise); otherwise it is mixed with
 * the channel's metal noise (NOISE VOL, the Mk II) and goes through the filter: the hats' HH FILTER (a 6 dB high-pass
 * and a 6 dB low-pass forming a peaked band-pass that doesn't self-oscillate, never below about 250 Hz) or the others'
 * 12 dB multimode (LP / HP / BP), which whistles from RESONANCE 85 up: with no sample and no noise it is the sound
 * (toms, claves, sticks under the VCA envelope). Both envelopes are ADSRs held while the note is. The LFO moves the
 * pitch (LFO TUNE), the sample's level (LFO VCA, before the VCA envelope) and the cutoff (LFO CUTOFF). X1 and X2 are
 * the sample engine alone: no envelopes, filter or noise. */
static int is_x(int s) { return s >= 6; }
static int base_of(int s) { return is_x(s) ? XBASE[s - 6] : SBASE[s]; }

static int zero_x(const smp_t *sm, int at, int lo, int hi) {   /* the nearest zero crossing within +-256 frames */
    if (!sm->d || sm->len < 2) return at;
    for (int d = 0; d <= 256; d++) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int j = at + sgn * d;
            if (j < lo + 1 || j >= hi) continue;
            if ((sm->d[j - 1] <= 0 && sm->d[j] >= 0) || (sm->d[j - 1] >= 0 && sm->d[j] <= 0)) return j;
        }
    }
    return at;
}

void smp_trigger(inc_t *P, int s, const play_t *pl) {
    smpv_t *v = &P->sv[s];
    v->pl = *pl;
    int b = base_of(s), x = is_x(s);
    int start = x ? X_START : S_START, stop = x ? X_STOP : S_STOP, loop = x ? X_LOOP : S_LOOP;
    int smp = (int)PV(b + (x ? X_SAMPLE : S_SAMPLE)), rev = (int)PV(b + (x ? X_REV : S_REV));
    v->smp = smp;
    v->s.d = NULL;
    v->s.len = 0;
    if (smp > 0) pool_get(smp, &v->s);
    int len = v->s.len;
    int st = (int)(PV(b + start) * 512.0f), sp = PV(b + stop) >= 126.5f ? len : (int)(PV(b + stop) * 512.0f);
    if (st > len) st = len;
    if (sp > len) sp = len;
    if (sp < st) sp = st;
    st = zero_x(&v->s, st, 0, len);
    if (sp < len) sp = zero_x(&v->s, sp, st, len);
    int lp = (int)(PV(b + loop) * 512.0f);
    lp = lp < st ? st : lp >= sp ? st : zero_x(&v->s, lp, st, sp);
    v->start = st;
    v->stop = sp;
    v->loop = lp;
    v->dir = rev ? -1 : 1;
    v->pos = rev ? sp - 1 : st;
    v->done = sp <= st;
    v->active = 1;
    v->gate = 1;
    v->fade = 1.0f;
    v->choke = 1.0f;
    if (!x) {
        adsr_gate(&v->vca, 1);
        adsr_gate(&v->fenv, 1);
        if (v->vca.stage == ENV_IDLE) v->vca.v = 0.0f;
    }
    if (PV(b + (x ? X_LFSYN : S_LFSYN)) >= 0.5f) v->lfo.ph = 0.0f;
    /* a multimode filter past RESONANCE 85 oscillates all the time on the hardware and the VCA lets it through: here it
     * rests while the channel is silent, so it starts at full swing, as if it had been running */
    if (!x && s >= 2 && PV(b + S_ROUT) >= 0.5f && PV(b + S_RES) > 85.0f && fabsf(v->f.s1) + fabsf(v->f.s2) < 0.1f) {
        v->f.s1 = 1.1f;
        v->f.s2 = 0.0f;
    }
}

void smp_release(inc_t *P, int s) {
    smpv_t *v = &P->sv[s];
    if (!v->active) return;
    v->gate = 0;
    if (!is_x(s)) {
        adsr_gate(&v->vca, 0);
        adsr_gate(&v->fenv, 0);
    }
}

void smp_block(inc_t *P, int s, float *y, int n) {
    smpv_t *v = &P->sv[s];
    if (!v->active) return;
    int b = base_of(s), x = is_x(s);
    int hh = s < 2;
    float pitch = PV(b + (x ? X_PITCH : S_PITCH)), cents = PV(b + (x ? X_CENTS : S_CENTS));
    int loopon = PV(b + (x ? X_LOOPON : S_LOOPON)) >= 0.5f;
    float hz = lfo_hz(PV(b + (x ? X_LFRAT : S_LFRAT)), PV(b + (x ? X_LFCLK : S_LFCLK)) >= 0.5f, P->bpm);
    float lo = lfo_run(&v->lfo, (int)PV(b + (x ? X_LFWAV : S_LFWAV)), hz, n);
    float lt = lfo_semis(PV(b + (x ? X_LFTUN : S_LFTUN)));
    float lvca = PV(b + (x ? X_LFVCA : S_LFVCA)) / 127.0f;
    float sg = 1.0f - lvca * (1.0f - lo) * 0.5f;
    float tr = play_ratio(P, &v->pl);
    v->lvl = level(PV(b + (x ? X_VOL : S_VOL)), PV(b + (x ? X_ACC : S_ACC)), v->pl.vel);
    double step = 0.0;
    if (v->s.d) step = smp_ratio(pitch, cents) * tr * fexp2(lo * lt / 12.0f) * (v->s.rate / SR);
    if (step > 32.0) step = 32.0;

    /* the analog part */
    int rout = x ? 0 : (int)PV(b + S_ROUT);
    float nzv = 0.0f, fb = 0.0f, hpc = 0.0f, lpc = 0.0f, ogain = 1.0f, bpn = 1.0f;
    int mt = 0;
    svfc_t fc;
    if (!x) {
        adsr_times(&v->vca, time_att(PV(b + S_VCATT)), time_dec(PV(b + S_VCDEC)), PV(b + S_VCSUS) / 127.0f,
                   time_dec(PV(b + S_VCREL)));
        adsr_times(&v->fenv, time_att(PV(b + S_FIATT)), time_dec(PV(b + S_FIDEC)), PV(b + S_FISUS) / 127.0f,
                   time_dec(PV(b + S_FIREL)));
    }
    if (rout) {
        mt = (int)PV(b + S_MTNZ);
        nzv = mt ? powf(PV(b + S_NZVOL) / 127.0f, 1.5f) : 0.0f;
        /* PITCH and CENTS tune the channel's metal noise too (not on the AlphaBase, where they move the sample alone):
         * with no sample a hat or a cymbal is still tunable */
        v->nz.rate = clampf(smp_ratio(pitch, cents) * tr, 0.125f, 8.0f);
        float env = v->fenv.v;   /* the filter envelope moves the cutoff once a block */
        float oct = env * 7.0f * PV(b + S_EVAMT) / 127.0f + lo * 4.0f * powf(PV(b + S_LFCUT) / 127.0f, 1.5f);
        float r = PV(b + S_RES);
        if (hh) {
            lpc = op_coef(fminf(hh_lp_hz(PV(b + S_CUTOFF)) * fexp2(oct), 0.45f * SR));
            hpc = op_coef(hh_hp_hz(PV(b + S_HPCUT)));
            fb = 0.92f * r / 127.0f;
        } else {
            float k = r <= 85.0f ? 1.0f / (0.5f * powf(400.0f, r / 85.0f)) : -0.012f * (r - 85.0f) / 42.0f;
            svf_coef(&fc, mm_cut_hz(PV(b + S_CUTOFF)) * fexp2(oct), k);
            ogain = r > 70.0f ? 1.0f - 0.45f * fminf(1.0f, (r - 70.0f) / 25.0f) : 1.0f;   /* a whistle is louder */
            /* band-pass: its peak grows with sqrt(Q), not Q; once it whistles its headroom holds it, so no less */
            float t = clampf((r - 75.0f) / 15.0f, 0.0f, 1.0f);
            bpn = lerpf(sqrtf(fmaxf(k, 0.02f)) * 1.2f, 1.0f, t);
        }
    }
    for (int i = 0; i < n; i++) {
        float smp = 0.0f;
        if (!v->done && v->s.d) {
            int j = (int)v->pos;
            float f = (float)(v->pos - j);
            int j1 = j + 1 < v->s.len ? j + 1 : j;
            smp = (v->s.d[j] + (v->s.d[j1] - v->s.d[j]) * f) * (0.6f / 32768.0f);   /* samples are cut hot */
            v->pos += step * v->dir;
            if (v->dir > 0 && v->pos >= v->stop) {
                if (loopon && v->stop > v->loop + 1) v->pos = v->loop + (v->pos - v->stop);
                else v->done = 1;
            } else if (v->dir < 0 && v->pos < v->start) {
                if (loopon && v->stop > v->loop + 1) v->pos = v->stop - 1 - (v->start - v->pos);
                else v->done = 1;
            }
        }
        float out;
        if (x) {
            if (!v->gate && loopon) v->fade *= 0.9985f;   /* a looping X fades out when its note ends */
            out = smp * sg * v->fade;
        } else {
            float in = smp * sg;
            if (rout) {
                in += nzv ? metal_run(&v->nz, mt, 0) * nzv * 0.4f : 0.0f;
                in += noise(&v->rng) * 1e-4f;   /* the circuit's own hiss: what a self-oscillating filter starts from */
                if (hh) {
                    float u = in + fb * 2.0f * v->fb;
                    v->hp += hpc * (u - v->hp);
                    float h = u - v->hp;
                    v->lp += lpc * (h - v->lp);
                    v->fb = sat(v->lp);
                    in = v->lp;
                } else {
                    float bp, hpo, lpo = svf_run_nl(&v->f, &fc, in, 1.0f, 1.6f, &bp, &hpo);
                    in = (rout == 1 ? lpo : rout == 2 ? hpo : bp * bpn) * ogain;
                }
            }
            adsr_run(&v->fenv);
            out = in * adsr_run(&v->vca);
        }
        if (v->choke < 1.0f) v->choke *= 0.985f;
        y[i] += out * v->lvl * v->choke;
    }
    int silent = x ? (v->done || v->fade < 1e-4f || !v->s.d) : v->vca.stage == ENV_IDLE;
    if (silent || v->choke < 1e-4f) {
        v->active = 0;
        v->f.s1 = v->f.s2 = 0.0f;
        v->hp = v->lp = v->fb = 0.0f;
    }
}

/* ======================================== FM SYNTH (manual 7.4) ==================================================
 * Four sine operators, each modulated by the other three by the 4x4 matrix (FM i>j: i modulates j; the ones below the
 * diagonal are the feedback ones), each with an attack-decay envelope that shapes its output, sets how loud it is in
 * the mix (OP LEVEL: 0 = only a modulator) and bends its pitch (OP PITCH ENV, for toms). OP TUNE is the operator's
 * pitch in semitones (0 = C0 when played at C1). Up to six voices; its own note plays CHORD. SPATIAL spreads the four
 * operators across the stereo field. */
static const int CHORD_NOTES[NCHORD][6] = {
    {0, -1}, {0, 12, -1}, {0, 7, -1}, {0, 7, 12, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 5, 7, -1}, {0, 4, 7, 11, -1},
    {0, 3, 7, 10, -1}, {0, 4, 7, 10, -1}, {0, 3, 7, 10, 14, -1}, {0, 1, 2, 3, 4, 5},
};

static void fm_start(inc_t *P, int note, const play_t *pl) {
    int nv = (int)PV(P_FM_VOICES), pick = -1, oldest = 1 << 30;
    if (nv < 1) nv = 1;
    if (nv > FM_VOICES) nv = FM_VOICES;
    for (int i = 0; i < nv && pick < 0; i++)
        if (!P->fm[i].active) pick = i;
    if (pick < 0)
        for (int i = 0; i < nv; i++)
            if (P->fm[i].age < oldest) { oldest = P->fm[i].age; pick = i; }
    fmv_t *v = &P->fm[pick];
    P->fm_last = pick;
    v->active = 1;
    v->note = note;
    v->age = ++P->fm_age;
    v->pl = *pl;
    v->cscale = 1.0f;
    for (int o = 0; o < 4; o++) {
        int b = P_FM_EG1AT + 4 * o;
        float a = expmap(PV(b), 0.0002f, 2.0f);
        ad_times(&v->eg[o], a, expmap(PV(b + 1), 0.005f, 10.0f));
        v->eg[o].v = 0.0f;
        ad_start(&v->eg[o], a);
        v->ph[o] = 0.0f;
        v->out[o] = 0.0f;
    }
}

void fm_trigger(inc_t *P, const play_t *pl, int note, int chord) {
    if (chord < 0) {
        fm_start(P, note, pl);
        return;
    }
    int c = (int)PV(P_FM_CHORD), nv = (int)PV(P_FM_VOICES), n = 0;
    while (n < 6 && n < nv && CHORD_NOTES[c][n] >= 0 && (!n || CHORD_NOTES[c][n])) n++;
    for (int k = 0; k < n; k++) {
        fm_start(P, note + CHORD_NOTES[c][k], pl);
        P->fm[P->fm_last].cscale = 1.0f / sqrtf((float)n);   /* a chord as loud as one note, about */
    }
}

void fm_release(inc_t *P, int note) { (void)P; (void)note; }   /* the envelopes are attack-decay: they run out */

void fm_block(inc_t *P, float *y, float *side, int n) {
    int any = 0;
    for (int i = 0; i < FM_VOICES; i++) any |= P->fm[i].active;
    if (!any) return;
    float M[4][4], lvl[4], pe[4], tune[4], pan[4];
    static const float SPREAD[4] = {-1.0f, -0.33f, 0.33f, 1.0f};
    float sp = PV(P_FM_SPATIAL) / 127.0f;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i == j) continue;
            /* the matrix's keys run fm<i><j>: P_FM_FM12 ... laid out row by row with the tunings on the diagonal */
            float a = PV(P_FM_OP1 + 4 * i + j) / 127.0f;
            M[i][j] = 1.6f * a * a;
        }
        M[i][i] = 0.0f;
        tune[i] = PV(P_FM_OP1 + 5 * i);
        lvl[i] = PV(P_FM_S1AMT + 4 * i) / 127.0f * 0.55f;
        pe[i] = 3.0f * powf(PV(P_FM_S1FM + 4 * i) / 127.0f, 1.5f);
        pan[i] = SPREAD[i] * sp * 0.5f;
    }
    for (int k = 0; k < FM_VOICES; k++) {
        fmv_t *v = &P->fm[k];
        if (!v->active) continue;
        float f0[4], tr = play_ratio(P, &v->pl);   /* the note is the transposition: this is the bend */
        v->lvl = level(PV(P_FM_VOL), PV(P_FM_ACC), v->pl.vel) * v->cscale;
        for (int o = 0; o < 4; o++) f0[o] = fminf(fm_op_hz(tune[o], v->note) * tr, 0.45f * SR) * INV_SR;
        float peak = 0.0f;
        for (int i = 0; i < n; i++) {
            float o4[4], mono = 0.0f, sd = 0.0f;
            for (int o = 0; o < 4; o++) {
                float pm = M[0][o] * v->out[0] + M[1][o] * v->out[1] + M[2][o] * v->out[2] + M[3][o] * v->out[3];
                float e = ad_run(&v->eg[o]);
                v->ph[o] += pe[o] > 0.0f ? f0[o] * fexp2(pe[o] * e) : f0[o];
                if (v->ph[o] >= 1.0f) v->ph[o] -= floorf(v->ph[o]);
                o4[o] = sin_t(v->ph[o] + pm) * e;
                mono += o4[o] * lvl[o];
                sd += o4[o] * lvl[o] * pan[o];
            }
            for (int o = 0; o < 4; o++) v->out[o] = o4[o];
            y[i] += mono * v->lvl;
            side[i] += sd * v->lvl;
        }
        for (int o = 0; o < 4; o++) peak = fmaxf(peak, v->eg[o].v * (lvl[o] > 0.0f ? 1.0f : 0.0f));
        if (peak < 1e-4f) v->active = 0;
    }
}

void voices_init(inc_t *P) {
    metal_init(&P->kd.nz, 0x1234567u);
    P->mb.rng = 0x2345671u;
    metal_init(&P->mb.m1, 0x3456712u);
    metal_init(&P->mb.m2, 0x4567123u);
    for (int s = 0; s < 8; s++) {
        P->sv[s].rng = 0x5671234u + 77u * (uint32_t)s;
        metal_init(&P->sv[s].nz, 0x6712345u + 131u * (uint32_t)s);
    }
}
