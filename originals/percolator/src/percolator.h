/* Percolator: a four-voice percussion synth laid out like the Erica Synths Perkons HD-01 (three algorithms with
 * three modes per voice, TUNE / DECAY / PARAM 1 / PARAM 2 / CUTOFF / DRIVE / FX SEND / LEVEL, an HP-BP-LP filter, a
 * BBD-style delay, a compressor and one LFO with a depth per knob), and loads the Perkons' .KIT files as kits.
 * The DSP is written for this repo; the Perkons' own firmware is not used or needed. */
#pragma once
#include <stdint.h>
#include "dsp.h"
#include "params_def.h"
#include "tapbus.h"
#include "midiin.h"

#define NV 4            /* voices */
#define NK 8            /* knobs per voice */
#define SUB 32          /* modulation and coefficient updates every SUB frames */
#define MAX_KITS 512
#define MAX_BANKS 48
#define NSLOT 3         /* sample slots (voice 4, algorithm 3) */
#define MAX_CUES 64
#define TAP_HOLD 1024   /* the main instance's output delay line, frames */

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
    int src;            /* SOURCE: 0 its algorithm, 1 its own sample (the SAMPLES tab) */
    int smp;            /* its SAMPLE: file smp of its folder, 0 = none */
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
    int ch, pitched;       /* the trigger's MIDI channel (0-15); played pitched (KEYS, MULTI): pitch bend reaches it */
    int ns;                /* frames since the trigger */
    float vel;             /* target amplitude from velocity */
    float amp;             /* smoothed level x velocity */
    float env, atk;        /* decay envelope and attack ramp */
    float pk;              /* their product's peak so far: the hit is over 80 dB under it */
    int quiet;             /* frames its output has been under -100 dB */
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
    int src;               /* this hit plays the voice's own sample (SOURCE = SAMPLE when it came) */
    int p2start;           /* PARAM 2 is where the sample starts (or its slice), not voice 4's attack */
    const sample_t *smp;   /* what a sample hit plays: voice 4's slot (a kit pack's, or built in), or its own file */
    int lslot;             /* the library file it holds while it plays it (library.c), or -1 */
    float last;            /* last engine sample, for the retrigger declick */
    float dclk;
    float cut_s;           /* smoothed cutoff 0..1 */
    room_t room;
} voice_t;

typedef struct perc {
    float pv[NP];          /* knobs 0..100, options as an index, kit number */
    voice_t v[NV];
    float bend[16];        /* pitch bend per MIDI channel, semitones (voices played pitched) */
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
    /* OUT 1-4 (tapbus.h) */
    uint32_t id;           /* this instance, process-wide, from 1 */
    tapbus_t *bus;         /* NULL: OUT 1-4 unavailable */
    tapread_t tr;          /* playing an OUT (TRACK PLAYS = OUT n) */
    float hold[TAP_HOLD];  /* the main instance's output, a block late while a voice is tapped */
    uint32_t hold_w;
    mi_port_t midi_in;     /* "Percolator MIDI In": notes on their own channels, for KEYS CH (midiin.h) */
    uint32_t changed[6];   /* parameters a MIDI CC, RANDOMISE or the SAMPLES tab set, for MPC's screen
                            * (mpc_engine_changed), a bit each */
    uint32_t rrng;         /* RANDOMISE's own */
    /* your own samples (library.c): each voice's folder, its SAMPLE then a file of it */
    short sfold[NV];       /* -1: none */
    char swant[NV][256];   /* a file to find once its folder is read (a saved project's), or "" */
    struct lib_br *br;     /* the SAMPLES tab's listings, from its first use */
    struct lib_list *bl;   /* the listing shown */
    char bpath[512];       /* the folder asked for ("" = the places) */
    int bwait;             /* a listing is on its way */
    int aud, aud_n, aud_off, aud_v;   /* PREVIEW: voice + 1 waiting to play, frames waited; frames to its release */
} perc_t;

#define DL_N 65536

/* voices.c */
void voice_trigger(perc_t *P, int vi, const vparam_t *vp, float vel, int note, float semi);
void voice_release(perc_t *P, int vi);
void voice_stop(perc_t *P, int vi);   /* silent at once, its file let go */
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
int wav_load(const char *path, sample_t *s, long max_bytes, int max_frames);   /* 1 if read */
extern sample_t perc_builtin[NSLOT];

/* library.c: your own samples, folders of WAV files anywhere on the MPC (the SAMPLES tab) */
#define LIB_PATH 512
#define LIB_FILES 255             /* a folder's first 255 WAV files, in name order: SAMPLE 1-255 */
#ifndef LIB_MEDIA
#define LIB_MEDIA "/media"        /* the MPC's drives */
#endif
enum { LE_FILE, LE_DIR, LE_UP, LE_PLACE };
typedef struct { int kind; char *name, *path; } lib_ent_t;   /* path: a place's own */
typedef struct lib_list {         /* a folder ("" = the places): "..", its subfolders, then its WAV files */
    char path[LIB_PATH];
    int n, first_file, nfiles, missing, gen;
    lib_ent_t *e;
} lib_list_t;
typedef struct lib_br lib_br_t;   /* an instance's listings */
void lib_open(const char *dir);
void lib_close(void);
void lib_poke(void);              /* once a block: wakes the thread if a request is waiting */
void lib_wait(void);              /* until nothing waits to be read (offline tools) */
int lib_folder(const char *path); /* its number (read on the thread), -1 if there are too many */
int lib_folder_set(const char *path, const lib_list_t *l);   /* as listed: the same number if nothing changed */
int lib_files(int f);             /* its WAV files, -1 while it's being read */
const char *lib_name(int f, int k);
const char *lib_path(int f);
int lib_get(int f, int k, const sample_t **s);   /* file k of folder f, held (lib_put) if in memory; else asked for: -1 */
void lib_put(int slot);
lib_br_t *lib_browser(void);
void lib_browser_free(lib_br_t *b);
void lib_browse(lib_br_t *b, const char *path);
lib_list_t *lib_listing(lib_br_t *b);   /* the listing asked for last, once read (yours to free), or NULL */
lib_list_t *lib_places(void);     /* the places' listing, read at once (yours to free) */
void lib_list_free(lib_list_t *l);
int lib_is_wav(const char *name);
