/* Incipience: an eleven-instrument drum synth laid out like the AlphaBase Mk II analog drum machine (kick, MBrane, two
 * hats, clap, rim, crash, ride, two sample players and a four-operator FM synth, each with the hardware's controls and
 * 0-127 ranges, a reverb and a delay with a send per instrument). It plays the AlphaBase's own sample set and kit
 * files when they are in its data folder; the DSP is written for this repo, and the hardware's analog circuits are
 * modelled from its manual, not measured. */
#pragma once
#include <stdint.h>
#include "dsp.h"
#include "params_def.h"
#include "tapbus.h"
#include "midiin.h"

#define NI 11             /* instruments */
enum { I_KD, I_MB, I_CH, I_OH, I_CP, I_RM, I_CR, I_RD, I_X1, I_X2, I_FM };
#define SUB 32            /* modulation and coefficient updates every SUB frames */
#define NSMP 501          /* sample numbers: 0 = off, 1-500 */
#define FM_VOICES 6
#define LFO_WAVES 64
#define LFO_LEN 256
#define MAX_KITS 512
#define SMP_RATE 48000.0f /* the AlphaBase's samples */
#define TAP_HOLD 1024     /* the output's delay line while an OUT is played, frames */

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

/* ---- library.c: your own samples, folders of WAV files anywhere on the MPC (the SAMPLES tab) ---- */
#define LIB_PATH 512
#define LIB_FILES 255             /* a folder's first 255 WAV files, in name order: SAMPLE 1-255 */
#ifndef LIB_MEDIA
#define LIB_MEDIA "/media"        /* the MPC's drives */
#endif
#define LIB_FLAT 999              /* a listing of a folder and all its subfolders' files: entries at most (FILE's range) */
enum { LE_FILE, LE_DIR, LE_UP, LE_PLACE, LE_ALPHABASE, LE_HEAD };
typedef struct { int kind; char *name, *path; int grp; } lib_ent_t;   /* path: a place's, a heading's; grp: a file's */
typedef struct { char *path; int first, n; } lib_grp_t;   /* a folder's WAV files in a listing: e[first] on, n of them */
typedef struct {                  /* a folder ("" = the places): "..", then its WAV files and, when they all fit
                                   * (flat), every subfolder's below, each under a heading (LE_HEAD, its path from
                                   * here); else its subfolders, then its WAV files */
    char path[LIB_PATH];
    int n, first_file, nfiles, missing, gen;   /* nfiles: in all; first_file: the first */
    int flat, ng;
    lib_ent_t *e;
    lib_grp_t *g;                 /* g[0]: the folder's own */
} lib_list_t;
typedef struct lib_br lib_br_t;   /* an instance's listings */
void lib_open(const char *dir);
void lib_close(void);
void lib_poke(void);              /* once a block, as pool_poke */
void lib_wait(void);              /* until nothing waits to be read (offline tools) */
int lib_folder(const char *path); /* its number (read on the thread), -1 if there are too many */
int lib_folder_set(const lib_list_t *l, int g);   /* folder g of a listing, as listed: the same number if nothing changed */
int lib_files(int f);             /* its WAV files, -1 while it's being read */
const char *lib_name(int f, int k);
const char *lib_path(int f);
int lib_get(int f, int k, smp_t *s);   /* file k of folder f, held (lib_put) if in memory; otherwise asked for: -1 */
void lib_put(int slot);
lib_br_t *lib_browser(void);
void lib_browser_free(lib_br_t *b);
void lib_browse(lib_br_t *b, const char *path);
lib_list_t *lib_listing(lib_br_t *b);   /* the listing asked for last, once read (yours to free), or NULL */
void lib_list_free(lib_list_t *l);
int lib_is_wav(const char *name);
int lib_decode_wav(const uint8_t *b, long sz, int16_t **out, int *frames, float *rate);

/* ---- the instruments (voices.c) ---- */
/* how a hit was played: its velocity (VOLUME and ACCENT make it a level each block, so they can be modulated), and when
 * it came on the instrument's KEYS CH, its transposition (the note against C3) and channel (whose pitch bend bends it) */
typedef struct { float vel, semi; int ch, pitched; } play_t;

typedef struct {
    int active, n, gate_n;
    play_t pl;
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
    play_t pl;
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
    int note;                     /* the MIDI note that played it: its note-off releases it */
    play_t pl;
    float lvl;
    double pos;
    int start, stop, loop, dir;   /* in source frames */
    smp_t s;
    int smp;                      /* the sample number playing */
    int lslot;                    /* the library file it holds (one of your own samples, library.c), or -1 */
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
    play_t pl;
    float lvl, cscale;            /* cscale: a chord's notes share one note's level */
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
    int rv_sleep, dl_sleep;       /* nothing in, nothing left: skipped until something is sent */
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
    float pv[NP];                 /* the controls as set (shown, saved) */
    float ev[NP];                 /* as the sound uses them: pv plus the mod matrix (mod_apply) */
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
    float bend[16];               /* pitch bend per MIDI channel, semitones (instruments played on their KEYS CH) */
    /* the mod matrix */
    lfo_t mlfo[NLFO];
    float mrand[NSLOT];           /* each slot's RANDOM, drawn at each hit of its DEST */
    float mvel[NDEST];            /* each DEST's last velocity, 0..1 (FX: any instrument's) */
    uint32_t mrng;
    short touched[NSLOT];         /* the controls the matrix moved in ev[] last time */
    int ntouched;
    /* OUT 1-11 (tapbus.h) */
    uint32_t id;                  /* this instance, process-wide, from 1 */
    tapbus_t *bus;                /* NULL: OUT 1-11 unavailable */
    float hold[2][TAP_HOLD];      /* the output, a block late while an OUT is played */
    uint32_t hold_w;
    float pan_at[NI], pan_l[NI], pan_r[NI];   /* each instrument's pan gains, and the PAN they're for */
    mi_port_t midi_in;            /* "Incipience MIDI In": notes on their own channels, for KEYS CH (midiin.h) */
    uint32_t changed[(NP + 31) / 32];   /* controls a MIDI MOD lane or RANDOMISE set, for MPC's screen (mpc_engine_changed) */
    uint32_t rrng;                /* RANDOMISE's own (the matrix's RANDOM stays repeatable) */
    /* your own samples (library.c): each sample channel's folder, its SAMPLE then a file of it */
    short sfold[8];               /* -1: the AlphaBase set, by number */
    char swant[8][128];           /* a file to find once its folder is read (a saved project's), or "" */
    lib_br_t *br;                 /* the SAMPLES tab's listings, from its first use */
    lib_list_t *bl;               /* the listing shown */
    char bpath[LIB_PATH];         /* the folder asked for ("" = the places) */
    int bwait;                    /* a listing is on its way */
    int aud, aud_n, aud_off, aud_ch;   /* PREVIEW: channel + 1 waiting to play, frames waited; frames to its release */
    /* 1.1.0: the file under the cursor (FILE), played as it lands, straight from its folder (PREVIEW) */
    int prv_slot;                 /* the library file playing (held), or -1 */
    int prv_f, prv_k, prv_want, prv_n;   /* the one asked for: folder, file; waiting for it, frames waited */
    smp_t prv_s;
    double prv_pos;
    uint32_t clock;               /* frames played: a tap told from MPC turning off the row it lit */
    uint32_t btap_at;             /* when a tap last lit a row */
    char bfind[128];              /* a file to put the cursor on once its folder is listed (LOAD TO's own) */
} inc_t;

void kick_trigger(inc_t *P, const play_t *pl);
void kick_block(inc_t *P, float *y, int n);
void mbrane_trigger(inc_t *P, const play_t *pl);
void mbrane_block(inc_t *P, float *y, int n);
void smp_trigger(inc_t *P, int s, const play_t *pl);
void smp_release(inc_t *P, int s);
void smp_drop(inc_t *P, int s);
void smp_block(inc_t *P, int s, float *y, int n);
void fm_trigger(inc_t *P, const play_t *pl, int note, int chord);   /* chord < 0: the note alone */
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
static inline float play_ratio(const inc_t *P, const play_t *p) {   /* KEYS CH: transposition and pitch bend */
    return p->pitched ? fexp2((p->semi + P->bend[p->ch & 15]) / 12.0f) : 1.0f;
}

/* ---- kits.c ---- */
void kits_load(inc_t *P);
void kits_free(inc_t *P);
int abpr_decode(const uint8_t *rec, char *out, int len, char *name);   /* one AlphaBase kit record -> state */
