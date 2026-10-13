/* Incipience: an eleven-instrument drum synth laid out like the AlphaBase Mk II analog drum machine (kick, MBrane, two
 * hats, clap, rim, crash, ride, two sample players and a four-operator FM synth, each with the hardware's controls and
 * 0-127 ranges, a reverb and a delay with a send per instrument). It plays the AlphaBase's own sample set and kit
 * files when they are in its data folder; the DSP is written for this repo, and the hardware's analog circuits are
 * modelled from its manual, not measured. */
#pragma once
#include <stdint.h>
#include "dsp.h"
#include "params_def.h"

#define NI 11             /* instruments */
enum { I_KD, I_MB, I_CH, I_OH, I_CP, I_RM, I_CR, I_RD, I_X1, I_X2, I_FM };
#define SUB 32            /* modulation and coefficient updates every SUB frames */
#define NSMP 501          /* sample numbers: 0 = off, 1-500 */
#define FM_VOICES 6
#define LFO_WAVES 64
#define LFO_LEN 256
#define MAX_KITS 512
#define SMP_RATE 48000.0f /* the AlphaBase's samples */

/* ---- tables.c ---- */
enum { MT_XOR, MT_SUM, MT_LFSR, MT_LFSR_XOR };
typedef struct {
    int mode, nsq, len;
    uint32_t inc[6];
    uint64_t cinc;        /* the register's clock, 32.32 per sample */
} metal_pat_t;
typedef struct {
    uint32_t ph[6], lfsr, lbit, rng;
    uint64_t cph;
    float rate;           /* 1 = as made; the sample channels' PITCH */
} metal_t;
typedef struct { float ph, out; } lfo_t;
extern float inc_lfotab[LFO_WAVES][LFO_LEN + 1];
extern int inc_lfo_from_file;
void inc_tables_init(const char *dir);
void metal_init(metal_t *m, uint32_t seed);
float metal_run(metal_t *m, int n, int len_override);
float lfo_hz(float rate, int synced, float bpm);
float lfo_run(lfo_t *l, int wave, float hz, int n);

/* ---- samples.c: the sample pool, shared by every instance, loaded on a thread of its own ---- */
typedef struct { const int16_t *d; int len; float rate; } smp_t;
void pool_open(const char *dir);
void pool_close(void);
int pool_get(int n, smp_t *s);    /* 1 if sample n is in memory; otherwise asks for it and returns 0 */
int pool_exists(int n);           /* there is a file for it */
void pool_wait(void);             /* until nothing is waiting to load (offline tools) */
void pool_poke(void);             /* once a block: wakes the loader if a request is waiting */

/* ---- the instruments (voices.c) ---- */
typedef struct {
    int active, n, gate_n;
    float lvl;                    /* level from VOLUME, ACCENT and velocity */
    float ph, amp, cenv, tenv, tc, ac, cc;
    ad_t pe;
    lfo_t lfo;
    float click_hp, click_prev, eq;
    metal_t nz;
    float out_prev;
} kick_t;

typedef struct {
    int active, n, gate_n;
    float lvl;
    svf_t f1, f2;
    float bp1, bp2;
    ad_t pe1, pe2;
    lfo_t l1, l2;
    float nenv, venv;
    float nlp, nlp2;
    metal_t m1, m2;
    uint32_t rng;
} mbrane_t;

typedef struct {
    int active, gate, done, held;
    float lvl;
    double pos;
    int start, stop, loop, dir;   /* in source frames */
    smp_t s;
    int smp;                      /* the sample number playing */
    adsr_t vca, fenv;
    lfo_t lfo;
    svf_t f;                      /* the multimode filter */
    float hp, lp, fb;             /* the hi-hat filter */
    metal_t nz;
    uint32_t rng;
    float fade;                   /* X: release fade */
    float choke;                  /* 1, falling to 0 when choked */
} smpv_t;

typedef struct {
    int active, note, age;
    float lvl, semi;
    float ph[4], out[4];
    ad_t eg[4];
} fmv_t;

/* ---- fx.c ---- */
#define RV_LINES 8
#define RV_MAX 4096
#define DL_MAX 66150              /* 1.5 s */
typedef struct {
    float *buf;                   /* RV_LINES x RV_MAX */
    int len[RV_LINES], w;
    svf_t lp[RV_LINES], hp[RV_LINES];
    float ap[4][1024];
    int apl[4], apw[4];
    float *dl[2];
    int dw;
    float dt;                     /* delay time in frames, smoothed */
    float dlp[2];
} fx_t;
void fx_init(fx_t *f);
void fx_free(fx_t *f);
struct inc;
void fx_block(struct inc *P, const float *rin, const float *din, float *L, float *R, int n);
float delay_seconds(const struct inc *P);

/* ---- the plugin ---- */
typedef struct {
    char name[24];
    char *state;                  /* key=value;... over the defaults */
} kit_t;

typedef struct inc {
    float pv[NP];
    kick_t kd;
    mbrane_t mb;
    smpv_t sv[8];                 /* CH OH CP RM CR RD X1 X2 */
    fmv_t fm[FM_VOICES];
    int fm_age, fm_last;
    lfo_t fm_lfo;
    fx_t fx;
    float bpm;
    kit_t *kits;
    int nkits, nbuiltin;
    int kit_pending;
    char dir[512];
    int choke_n;                  /* frames since the open hat played (HH CHOKE: a closed hat on the same step is dropped) */
} inc_t;

void kick_trigger(inc_t *P, float vel, float semi);
void kick_block(inc_t *P, float *y, int n);
void mbrane_trigger(inc_t *P, float vel, float semi);
void mbrane_block(inc_t *P, float *y, int n);
void smp_trigger(inc_t *P, int s, float vel, float semi);
void smp_release(inc_t *P, int s);
void smp_block(inc_t *P, int s, float *y, int n);
void fm_trigger(inc_t *P, float vel, int note, int chord);
void fm_release(inc_t *P, int note);
void fm_block(inc_t *P, float *y, float *side, int n);
void voices_init(inc_t *P);
float kick_hz(float pitch);
float mb_hz(float pitch);
float mm_cut_hz(float v);
float hh_lp_hz(float v);
float hh_hp_hz(float v);
float fm_op_hz(float tune, int note);
float smp_ratio(float pitch, float cents);

/* ---- kits.c ---- */
void kits_load(inc_t *P);
void kits_free(inc_t *P);
int abpr_decode(const uint8_t *rec, char *out, int len, char *name);   /* one AlphaBase kit record -> state */
