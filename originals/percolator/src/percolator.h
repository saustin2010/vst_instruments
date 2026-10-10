/* Percolator: a four-voice percussion synth laid out like the Erica Synths Perkons HD-01 (three algorithms with
 * three modes per voice, TUNE / DECAY / PARAM 1 / PARAM 2 / CUTOFF / DRIVE / FX SEND / LEVEL, an HP-BP-LP filter, a
 * BBD-style delay, a compressor and one LFO with a depth per knob), and loads the Perkons' .KIT files as kits.
 * The DSP is written for this repo; the Perkons' own firmware is not used or needed. */
#pragma once
#include <stdint.h>
#include "dsp.h"
#include "params_def.h"

#define NV 4            /* voices */
#define NOTE0 20        /* voice 1's note: 20-23 (G#-1-B-1) are pads 1-4 of bank A on an MPC plugin track */
#define NK 8            /* knobs per voice */
#define SUB 32          /* modulation and coefficient updates every SUB frames */
#define MAX_KITS 512
#define MAX_BANKS 48
#define NSLOT 3         /* sample slots (voice 4, algorithm 3) */
#define MAX_CUES 64

enum { K_TUNE, K_DECAY, K_P1, K_P2, K_CUTOFF, K_DRIVE, K_FX, K_LEVEL };

typedef struct {
    float *d;           /* mono, -1..1 */
    int len;
    float rate;         /* the file's sample rate */
    int ncue, cue[MAX_CUES];   /* slice markers (WAV cue points), in frames, ascending */
} sample_t;

typedef struct {
    char name[24];
    sample_t smp[NSLOT];   /* len 0: the built-in sample */
    int has_samples;
} bank_t;

typedef struct {
    char name[32];
    int bank;                      /* -1: built in */
    float knob[NV][NK];            /* 0..100 */
    int algo[NV], mode[NV], vcf[NV];
    int depth[NV][NK];             /* 0..8 = OFF, 10 % .. 80 % */
    int has_lfo;
    float lfo_speed, lfo_level;    /* 0..100 */
    int lfo_wave;
    int has_fx;
    float fx_time, fx_feedback, fx_rate, fx_depth;   /* 0..100 */
    int fx_range;
} kit_t;

/* one voice's controls for a sub-block, after the LFO: 0..1 */
typedef struct {
    float k[NK];
    int algo, mode, vcf;
} vparam_t;

typedef struct {
    float comb[4][512];
    float ap[2][256];
    int ci[4], ai[2];
    float damp[4];
    int tail;              /* frames left to ring after the voice stops */
} room_t;

typedef struct {
    int active, gate, note, drone;
    int ns;                /* frames since the trigger */
    float vel;             /* target amplitude from velocity */
    float amp;             /* smoothed level x velocity */
    float env, atk;        /* decay envelope and attack ramp */
    float penv;            /* pitch envelope, 1 -> 0 */
    float tr;              /* transient envelope */
    float nenv;            /* noise envelope (snare) */
    float bt;              /* clap burst envelope */
    int bursts;            /* clap bursts started */
    float semi;            /* KEYS transposition */
    float ph[8];
    float zr[3], zi[3];    /* complex resonators */
    uint32_t rng;
    svf_t f1, f2, out;
    float lp1, hp1;        /* one-pole states */
    float hold;            /* sample & hold */
    float holdc;
    double spos;
    int sstart, send;
    int slot_bank;         /* bank whose samples the voice plays (-1 built in) */
    float last;            /* last engine sample, for the retrigger declick */
    float dclk;
    float cut_s;           /* smoothed cutoff 0..1 */
    room_t room;
} voice_t;

typedef struct perc {
    float pv[NP];          /* knobs 0..100, options as an index, kit number */
    voice_t v[NV];
    float bend;            /* KEYS pitch bend, semitones */
    /* LFO */
    float lfo_ph, lfo_out, lfo_a, lfo_b;
    uint32_t lfo_rng;
    /* BBD delay */
    float *dl;
    int dl_w;
    float dl_t, dl_ph, dl_lp1, dl_lp2, dl_hp, dl_in1, dl_in2;
    /* compressor */
    float comp_env;
    /* kits */
    kit_t *kits;
    int nkits, nbuiltin;
    bank_t banks[MAX_BANKS];
    int nbanks;
    int bank;              /* bank of the loaded kit (its samples), -1 = built in */
    int kit_pending;       /* a kit set but not loaded yet (-1: none): loaded at the next block (see e_set_param) */
    char dir[512];
} perc_t;

#define DL_N 65536

/* voices.c */
void voice_trigger(perc_t *P, int vi, const vparam_t *vp, float vel, int note, float semi);
void voice_release(perc_t *P, int vi);
void voice_block(perc_t *P, int vi, const vparam_t *vp, float *mix, float *send, int n);
const char *algo_p_name(int vi, int algo, int mode, int which, const perc_t *P);
const char *mode_name(int vi, int algo, int mode);
float voice_freq(int vi, float tune);
float voice_t60(int vi, float decay);
float v4_attack(float p2);
float sample_ratio(float tune);
int sample_slices(const perc_t *P, int slot);
void dsp_tables_init(void);

/* kits.c */
void kits_load(perc_t *P);
void kits_free(perc_t *P);
const sample_t *slot_sample(const perc_t *P, int bank, int slot);
extern sample_t perc_builtin[NSLOT];
