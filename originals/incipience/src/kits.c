/* Incipience: kits. A few built in (state strings over the defaults), three more that use the AlphaBase's sample set when
 * it's in the data folder, then every named kit in the AlphaBase kit files
 * (ABPR0000.BIN - ABPR0019.BIN, as its SD card has them: 128 kit records of 496 bytes, then its multi sets and
 * settings) found in the data folder. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "incipience.h"

#define ABPR_REC 496
#define ABPR_KITS 128

/* The built-in kits use no samples (the AlphaBase's aren't shipped): the kick, the MBrane, metal noise through the
 * analog filters, self-oscillating filters and the FM synth. */
typedef struct { const char *name, *state; } builtin_t;
static const builtin_t BUILTIN[] = {
    {"Init", ""},
    {"Analog Basics",
     "kd_tune=80;kd_pitch=32;kd_decay=72;kd_attack=80;kd_pulse=90;kd_noise=30;kd_eq=25;kd_gate=40;kd_compr=20;"
     "mb_scene=1;mb_decay=58;"
     "cp_cutoff=92;cp_res=20;cp_mtnz=1;cp_vol=124;cp_vcdec=50;cp_vcrel=50;cp_evamt=20;"
     "rm_vol=96;cr_vol=84;rd_vol=108;"
     "fm_op1=60;fm_op2=79;fm_op3=88;fm_op4=72;fm_fm12=0;fm_fm13=0;fm_fm14=0;fm_fm21=70;fm_fm23=0;fm_fm24=0;"
     "fm_fm31=30;fm_fm34=0;fm_s1amt=110;fm_eg1rl=95;fm_eg2rl=70;fm_eg3rl=50;fm_s1fm=0;fm_chord=0;"},
    {"Low and Slow",
     "kd_tune=40;kd_pitch=26;kd_decay=100;kd_evamt=30;kd_ptdec=70;kd_eq=90;kd_attack=30;kd_pulse=50;kd_noise=0;"
     "kd_compr=0;mb_scene=4;mb_decay=70;"
     "ch_cutoff=95;ch_hpcut=60;ch_mtnz=2;oh_mtnz=2;oh_cutoff=100;"
     "cp_cutoff=90;cp_res=20;cp_mtnz=1;cp_vol=124;cp_vcdec=60;cp_vcrel=60;cp_rsend=80;"
     "rm_rout=3;rm_res=105;rm_cutoff=88;rm_vcdec=30;rm_vcrel=30;rm_vol=96;"
     "cr_rout=1;cr_res=100;cr_cutoff=34;cr_mtnz=0;cr_evamt=24;cr_fidec=50;cr_firel=50;cr_vcdec=78;cr_vcrel=78;cr_vol=110;"
     "rd_rout=1;rd_res=100;rd_cutoff=46;rd_mtnz=0;rd_evamt=24;rd_fidec=45;rd_firel=45;rd_vcdec=72;rd_vcrel=72;rd_vol=110;"
     "fm_op1=28;fm_op2=40;fm_fm21=50;fm_fm13=0;fm_fm14=0;fm_fm12=0;fm_s1amt=127;fm_s1fm=60;fm_eg1rl=70;fm_eg2rl=40;"},
    {"Hard Drive",
     "kd_tune=110;kd_pitch=34;kd_decay=62;kd_attack=110;kd_pulse=110;kd_noise=70;kd_metnz=1;kd_gate=28;kd_eq=8;"
     "kd_harm=40;kd_compr=40;mb_scene=2;mb_decay=52;"
     "ch_hpcut=80;ch_cutoff=120;ch_res=50;ch_mtnz=3;ch_vcdec=40;ch_vcrel=40;"
     "oh_hpcut=76;oh_cutoff=124;oh_mtnz=3;oh_vcdec=78;oh_vcrel=78;"
     "cp_cutoff=95;cp_res=15;cp_mtnz=1;cp_vcdec=56;cp_vcrel=56;cp_rsend=70;cp_dsend=40;cp_vol=124;"
     "cr_mtnz=3;cr_cutoff=110;cr_vol=84;rd_mtnz=3;rd_vol=100;"
     "rv_size=60;rv_level=60;dl_sync=1;dl_time=90;dl_fb=55;"},
    {"Membranes",
     "mb_pitch1=38;mb_pitch2=52;mb_damp1=80;mb_damp2=70;mb_cupl1=50;mb_cupl2=90;mb_noise=40;mb_decay=70;"
     "mb_e1amt=40;mb_p1dec=40;mb_rsend=50;"
     "kd_tune=60;kd_pitch=30;kd_decay=80;"
     "fm_op1=36;fm_op2=55;fm_fm21=80;fm_fm12=0;fm_fm13=0;fm_fm14=0;fm_s1amt=127;fm_s1fm=40;fm_eg1rl=60;fm_eg2rl=35;"},
};

/* With the AlphaBase's sample set in the data folder (Jomox's free download, not shipped here): kits that use it. The
 * numbers are its factory samples (hats 1-24, cymbals 27-59 and 168-217, snares and claps 84-96 and 132-161, kicks
 * and toms 97-120, synth notes 60-83); the kick and the MBrane stay analog. */
static const builtin_t WITH_SAMPLES[] = {
    {"Sampled House",
     "kd_tune=85;kd_pitch=30;kd_decay=76;kd_attack=80;kd_pulse=90;kd_noise=20;kd_eq=30;mb_scene=1;mb_decay=60;"
     "ch_sample=10;ch_rout=1;ch_mtnz=0;ch_cutoff=118;ch_hpcut=40;ch_res=20;ch_vcdec=60;ch_vcrel=60;"
     "oh_sample=43;oh_rout=1;oh_mtnz=0;oh_cutoff=122;oh_hpcut=40;oh_res=20;oh_vcdec=96;oh_vcrel=96;"
     "cp_sample=84;cp_rout=0;cp_vcdec=80;cp_vcrel=80;cp_rsend=50;"
     "rm_sample=142;rm_rout=0;rm_vcdec=60;rm_vcrel=60;"
     "cr_sample=27;cr_rout=0;cr_vcdec=118;cr_vcrel=118;cr_vol=90;"
     "rd_sample=168;rd_rout=0;rd_vcdec=110;rd_vcrel=110;rd_vol=95;"
     "x1_sample=86;x2_sample=97;"},
    {"Sampled Techno",
     "kd_tune=110;kd_pitch=34;kd_decay=62;kd_attack=110;kd_pulse=110;kd_noise=70;kd_metnz=1;kd_gate=28;kd_eq=8;"
     "kd_harm=40;kd_compr=40;kd_vol=100;mb_scene=2;mb_decay=52;"
     "ch_sample=2;ch_rout=1;ch_mtnz=3;ch_nzvol=50;ch_cutoff=120;ch_hpcut=70;ch_res=40;ch_vcdec=50;ch_vcrel=50;"
     "oh_sample=46;oh_rout=1;oh_mtnz=0;oh_cutoff=124;oh_hpcut=60;oh_vcdec=100;oh_vcrel=100;"
     "cp_sample=132;cp_rout=1;cp_cutoff=110;cp_res=40;cp_mtnz=0;cp_vcdec=85;cp_vcrel=85;cp_rsend=60;cp_dsend=40;"
     "rm_sample=154;rm_rout=0;rm_vcdec=55;rm_vcrel=55;"
     "cr_sample=210;cr_rout=0;cr_vcdec=116;cr_vcrel=116;cr_vol=88;"
     "rd_sample=172;rd_rout=0;rd_vcdec=110;rd_vcrel=110;rd_vol=92;"
     "x1_sample=133;x2_sample=99;rv_size=60;rv_level=60;dl_sync=1;dl_time=90;dl_fb=55;"},
    {"Synth Filters",
     "kd_tune=40;kd_pitch=26;kd_decay=100;kd_evamt=30;kd_ptdec=70;kd_eq=90;kd_attack=30;kd_pulse=50;kd_noise=0;"
     "mb_scene=6;mb_decay=80;"
     "cp_sample=60;cp_rout=1;cp_cutoff=60;cp_res=80;cp_evamt=60;cp_fidec=55;cp_firel=55;cp_mtnz=0;cp_vcdec=80;cp_vcrel=80;"
     "rm_sample=62;rm_rout=1;rm_cutoff=66;rm_res=90;rm_evamt=55;rm_fidec=45;rm_firel=45;rm_mtnz=0;rm_vcdec=70;rm_vcrel=70;"
     "cr_sample=64;cr_rout=1;cr_cutoff=56;cr_res=95;cr_evamt=70;cr_fidec=60;cr_firel=60;cr_mtnz=0;cr_vcdec=86;cr_vcrel=86;"
     "rd_sample=66;rd_rout=1;rd_cutoff=70;rd_res=85;rd_evamt=50;rd_fidec=50;rd_firel=50;rd_mtnz=0;rd_vcdec=76;rd_vcrel=76;"
     "x2_sample=61;x2_loopon=1;cp_rsend=30;cr_dsend=40;"},
};

static int count_abwv(const char *dir, int depth) {   /* ABWV files here and (depth 1) one folder down */
    int n = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)) && n < 20) {
        if (!strncasecmp(e->d_name, "ABWV", 4)) n++;
        else if (depth && e->d_name[0] != '.') {
            char p[600];
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            n += count_abwv(p, 0);
        }
    }
    if (d) closedir(d);
    return n;
}
static int has_sample_set(const char *dir) { return count_abwv(dir, 1) >= 20; }   /* a few of its numbers: it's there */

static void add_kit(inc_t *P, const char *name, const char *state) {
    if (P->nkits >= MAX_KITS) return;
    kit_t *k = &P->kits[P->nkits++];
    snprintf(k->name, sizeof k->name, "%s", name);
    k->state = strdup(state);
}

static int cmp_str(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

static void load_abpr(inc_t *P, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t *b = malloc(ABPR_REC * ABPR_KITS);
    size_t got = b ? fread(b, 1, ABPR_REC * ABPR_KITS, f) : 0;
    fclose(f);
    if (got == ABPR_REC * ABPR_KITS) {
        char *st = malloc(16384), name[24];
        for (int k = 0; st && k < ABPR_KITS; k++)
            if (abpr_decode(b + k * ABPR_REC, st, 16384, name) > 0) add_kit(P, name, st);
        free(st);
    }
    free(b);
}

void kits_load(inc_t *P) {
    P->kits = calloc(MAX_KITS, sizeof(kit_t));
    P->nkits = 0;
    if (!P->kits) return;
    for (size_t i = 0; i < sizeof BUILTIN / sizeof BUILTIN[0]; i++) add_kit(P, BUILTIN[i].name, BUILTIN[i].state);
    if (has_sample_set(P->dir))
        for (size_t i = 0; i < sizeof WITH_SAMPLES / sizeof WITH_SAMPLES[0]; i++)
            add_kit(P, WITH_SAMPLES[i].name, WITH_SAMPLES[i].state);
    P->nbuiltin = P->nkits;
    DIR *d = opendir(P->dir);
    if (!d) return;
    char *names[32];
    int nn = 0;
    struct dirent *e;
    while ((e = readdir(d)) && nn < 32) {
        const char *s = e->d_name;
        if (strlen(s) == 12 && !strncasecmp(s, "ABPR", 4) && !strcasecmp(s + 8, ".BIN")) names[nn++] = strdup(s);
    }
    closedir(d);
    qsort(names, nn, sizeof names[0], cmp_str);
    for (int i = 0; i < nn; i++) {
        char path[600];
        snprintf(path, sizeof path, "%s/%s", P->dir, names[i]);
        load_abpr(P, path);
        free(names[i]);
    }
}

void kits_free(inc_t *P) {
    for (int i = 0; i < P->nkits; i++) free(P->kits[i].state);
    free(P->kits);
    P->kits = NULL;
    P->nkits = 0;
}

/* one AlphaBase kit record -> a state string; 0 for an empty slot ("Init Kit") */
int abpr_decode(const uint8_t *rec, char *out, int len, char *name) {
    char nm[9];
    memcpy(nm, rec + 0x1DE, 8);
    nm[8] = 0;
    for (int i = 7; i >= 0 && (nm[i] == ' ' || nm[i] == 0); i--) nm[i] = 0;
    if (!nm[0] || !strcmp(nm, "Init Kit")) return 0;
    snprintf(name, 24, "%s", nm);
    out[0] = 0;
    (void)len;
    return 0;   /* the field layout isn't decoded yet */
}
