/* Incipience engine: parameters, MIDI, kits, the mod matrix, the mix, OUT 1-11 and the master section.
 *
 * MIDI: each instrument plays on its NOTE (36-46, C1-A#1, to start with: pads 1-11 of bank B on an MPC plugin track),
 * as tuned; the FM synth's note plays its CHORD. Every note on an instrument's KEYS CH (channels 2-12 to start with;
 * OFF; ALL) plays it pitched, C3 as tuned, and pitch bend on that channel bends it +-2 semitones; the FM synth plays
 * them one note each, up to its VOICES. Notes keep their channel only through Incipience MIDI In (midiin.h): MPC hands
 * the plugin's own track every note-on on channel 1. A note-off releases the sample channels' envelopes (their ADSRs
 * hold while the note does). The kick, the MBrane and the FM synth's envelopes run their course. HH CHOKE (the Mk II):
 * the two hats share a VCA, so each cuts the other, and when both land on the same step the open one wins. The MIDI
 * MOD lanes (the mod wheel, Gen Purpose 1-3) on an instrument's KEYS CH set the controls its lanes name; CC 120 / 123
 * stop everything.
 *
 * The mod matrix (mod_apply): 16 slots, each a SOURCE (four LFOs; RANDOM and VELOCITY, taken at each hit of the slot's
 * DEST) moving one control of an instrument or of FX by DEPTH, +-100 % of its range. The sound reads ev[], pv[] plus
 * the slots, worked out every SUB frames and at each hit; the screen shows and the project saves pv[]. A control read
 * when a hit starts (SAMPLE, START, STOP, LOOP, REVERSE, GATE, the FM envelopes) takes the value of that moment. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "incipience.h"
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);
int mpc_engine_changed(void *inst, char *key, int len);   /* optional (engine.h): what a MIDI MOD lane changed */

#define CHOKE_SAME 96   /* frames: a closed and an open hat this close are on the same step */

static int find_key(const char *key) {
    for (int i = 0; i < NP; i++)
        if (!strcmp(PDEF[i].key, key)) return i;
    return -1;
}

/* ---- flush denormals while rendering --------------------------------------------------------------------------- */
typedef unsigned long fpstate_t;
static inline fpstate_t ftz_on(void) {
#if defined(__arm__)
    unsigned old, n;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(old));
    n = old | (1u << 24);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(n));
    return old;
#elif defined(__aarch64__)
    unsigned long old, n;
    __asm__ volatile("mrs %0, fpcr" : "=r"(old));
    n = old | (1ul << 24);
    __asm__ volatile("msr fpcr, %0" : : "r"(n));
    return old;
#elif defined(__x86_64__) || defined(__i386__)
    unsigned old = _mm_getcsr();
    _mm_setcsr(old | 0x8040);
    return old;
#else
    return 0;
#endif
}
static inline void ftz_off(fpstate_t old) {
#if defined(__arm__)
    unsigned o = (unsigned)old;
    __asm__ volatile("vmsr fpscr, %0" : : "r"(o));
#elif defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0" : : "r"(old));
#elif defined(__x86_64__) || defined(__i386__)
    _mm_setcsr((unsigned)old);
#else
    (void)old;
#endif
}

/* ---- parameters ------------------------------------------------------------------------------------------------ */
static void set_value(inc_t *P, int i, const char *val) {
    const pdef_t *d = &PDEF[i];
    float x = (float)atof(val);
    switch (d->kind) {
    case 0: P->pv[i] = clampf(x, d->lo, d->hi); break;
    case 1: {
        int k = atoi(val);
        P->pv[i] = (float)(k < 0 ? 0 : k >= d->nopts ? d->nopts - 1 : k);
        break;
    }
    case 4: P->pv[i] = clampf(roundf(x), d->lo, d->hi); break;
    default: break;
    }
    P->ev[i] = P->pv[i];   /* the mod matrix adds to it again at the next block */
}

static int is_kit_param(int i) { return i > P_KIT_NAME && i < P_KD_NOTE; }   /* the sounds, the mix and the FX */

static void reset_sound(inc_t *P) {
    for (int i = P_KIT_NAME + 1; i < P_KD_NOTE; i++)
        P->pv[i] = P->ev[i] = PDEF[i].def;
}

/* ---- the sample channels' SAMPLE: the AlphaBase set by number, or a file of the channel's own folder ------------- */
static const char *const PFX[NI] = {"kd", "mb", "ch", "oh", "cp", "rm", "cr", "rd", "x1", "x2", "fm"};
#define SMP_P(c) ((c) < 6 ? SBASE[c] + S_SAMPLE : XBASE[(c) - 6] + X_SAMPLE)   /* sample channel c's SAMPLE */

static int sample_chan(int i) {   /* the sample channel whose SAMPLE control i is, or -1 */
    for (int c = 0; c < 8; c++)
        if (i == SMP_P(c)) return c;
    return -1;
}

static float sample_top(const inc_t *P, int c) {   /* SAMPLE's reach: the set's numbers, or the folder's files */
    if (P->sfold[c] < 0) return PDEF[SMP_P(c)].hi;
    int n = lib_files(P->sfold[c]);
    return n > 0 ? (float)n : 0.0f;
}

static void sample_ask(inc_t *P, int c, int n) {   /* sample n of channel c: read now, if it isn't in memory */
    smp_t sm;
    if (n <= 0) return;
    if (P->sfold[c] >= 0) lib_put(lib_get(P->sfold[c], n - 1, &sm));
    else pool_get(n, &sm);
}

static void set_folder(inc_t *P, int c, int f) {   /* -1: back to the AlphaBase set */
    P->sfold[c] = (short)f;
    P->swant[c][0] = 0;
}

/* a path or a name in the state: ';' (which ends a value) and '%' escaped */
static int put_esc(char *o, int len, const char *s) {
    int n = 0;
    for (; *s; s++) {
        const char *e = *s == ';' ? "%3B" : *s == '%' ? "%25" : NULL;
        int w = e ? 3 : 1;
        if (n + w >= len) return -1;
        if (e) memcpy(o + n, e, 3);
        else o[n] = *s;
        n += w;
    }
    o[n] = 0;
    return n;
}
static void unesc(char *s) {
    char *o = s;
    for (; *s; s++, o++) {
        if (s[0] == '%' && s[1] == '3' && (s[2] == 'B' || s[2] == 'b')) *o = ';', s += 2;
        else if (s[0] == '%' && s[1] == '2' && s[2] == '5') *o = '%', s += 2;
        else *o = *s;
    }
    *o = 0;
}

static void apply_state(inc_t *P, const char *s, int kit_only) {
    char kv[LIB_PATH + 64];
    while (*s) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                int i = find_key(kv), c = -1;
                for (int k = I_CH; k <= I_X2 && c < 0 && !kit_only; k++)
                    if (!strncmp(kv, PFX[k], 2) && (!strcmp(kv + 2, "_dir") || !strcmp(kv + 2, "_file"))) c = k - I_CH;
                if (c >= 0) {   /* a channel's own folder and file (get_state) */
                    unesc(eq);
                    if (kv[3] == 'd') set_folder(P, c, lib_folder(eq));
                    else snprintf(P->swant[c], sizeof P->swant[c], "%s", eq);
                } else if (!strcmp(kv, "br_path") && !kit_only) {
                    unesc(eq);
                    snprintf(P->bpath, sizeof P->bpath, "%s", eq);
                } else if (i == P_KIT && !kit_only) P->pv[P_KIT] = (float)atoi(eq);
                else if (i >= 0 && (!kit_only || is_kit_param(i))) set_value(P, i, eq);
            }
        }
        s += n;
        if (*s) s++;
    }
}

static void load_kit(inc_t *P, int n) {
    if (n < 0 || n >= P->nkits) return;
    reset_sound(P);
    apply_state(P, P->kits[n].state, 1);
    P->pv[P_KIT] = (float)n;
    /* a kit plays the AlphaBase set; its samples asked for now, so they're in by the time it plays */
    for (int c = 0; c < 8; c++) {
        set_folder(P, c, -1);
        sample_ask(P, c, (int)P->pv[SMP_P(c)]);
    }
}

/* KIT waits for the next block (MPC sets parameter 0 to the far end of its range and straight back on insert and on
 * every STOP; see Percolator's notes): the pair ends on the kit already loaded and nothing happens. */
static void apply_kit(inc_t *P) {
    if (P->kit_pending < 0) return;
    int n = P->kit_pending;
    P->kit_pending = -1;
    if (n != (int)P->pv[P_KIT]) load_kit(P, n);
}

/* The project's copy: the kit, then each control not at its default (2026-10-18: a state read starts from the defaults,
 * so the rest are as they were; the older states with every control load as before), then each sample channel's
 * folder and file, and the folder the SAMPLES tab shows. The wrapper's 8 KB: every control moved takes 6.5 KB; a
 * folder that doesn't fit after that isn't kept (its channel opens on the AlphaBase set). */
static int get_state(const inc_t *P, char *buf, int len) {
    int o = snprintf(buf, len, "kit=%d;", (int)P->pv[P_KIT]);
    for (int i = 0; i < NP && o < len; i++) {
        int k = PDEF[i].kind;
        float v = P->pv[i];
        if ((k == 0 || k == 1 || k == 4) && v == PDEF[i].def) continue;
        if (k == 0 && fabsf(v - roundf(v)) >= 0.005f) o += snprintf(buf + o, len - o, "%s=%.2f;", PDEF[i].key, v);
        else if (k == 0) o += snprintf(buf + o, len - o, "%s=%d;", PDEF[i].key, (int)roundf(v));
        else if (k == 1 || k == 4) o += snprintf(buf + o, len - o, "%s=%d;", PDEF[i].key, (int)P->pv[i]);
    }
    if (o >= len) return -1;
    char d[LIB_PATH * 3 + 16], f[512];
    for (int c = 0; c < 8; c++) {
        const char *path = lib_path(P->sfold[c]), *name = lib_name(P->sfold[c], (int)P->pv[SMP_P(c)] - 1);
        if (!path) continue;
        if (!name) name = P->swant[c];   /* not read yet: the file it was asked for */
        int a = put_esc(d, sizeof d, path), b = put_esc(f, sizeof f, name);
        if (a < 0 || b < 0 || o + a + b + 16 >= len) continue;
        o += snprintf(buf + o, len - o, "%s_dir=%s;%s_file=%s;", PFX[I_CH + c], d, PFX[I_CH + c], f);
    }
    if (P->bpath[0] && put_esc(d, sizeof d, P->bpath) > 0 && o + (int)strlen(d) + 10 < len)
        o += snprintf(buf + o, len - o, "br_path=%s;", d);
    return o < len ? o : -1;
}

/* a state read: everything at its default first (get_state leaves those out) */
static void reset_all(inc_t *P) {
    for (int i = 0; i < NP; i++)
        if (PDEF[i].kind == 0 || PDEF[i].kind == 1 || PDEF[i].kind == 4) P->pv[i] = P->ev[i] = PDEF[i].def;
    for (int c = 0; c < 8; c++) set_folder(P, c, -1);
    P->bpath[0] = 0;
}

/* ---- the mod matrix -------------------------------------------------------------------------------------------- */
#define SLOT(s, p) (P->pv[(p) + SLOTSTRIDE * (s)])   /* SLOT(s, P_M1_DEPTH): slot s's DEPTH */
#define LFOP(l, p) (P->pv[(p) + LFOSTRIDE * (l)])    /* LFOP(l, P_L1_RATE): LFO l's RATE */

static float mod_source(const inc_t *P, int s, int src, int dest) {
    if (src >= SRC_LFO1 && src < SRC_LFO1 + NLFO) return P->mlfo[src - SRC_LFO1].out;   /* -1..1 */
    if (src == SRC_RANDOM) return P->mrand[s];                                       /* -1..1 */
    return src == SRC_VELOCITY ? P->mvel[dest] : 0.0f;                               /* 0..1 */
}

/* ev[] = pv[] plus every slot on (a SOURCE and a DEPTH), clamped to each control's range, options and steppers on
 * whole steps. Only the controls the slots name differ from pv[], and they go back to it first. */
static void mod_apply(inc_t *P) {
    for (int k = 0; k < P->ntouched; k++) P->ev[P->touched[k]] = P->pv[P->touched[k]];
    P->ntouched = 0;
    for (int s = 0; s < NSLOT; s++) {
        int src = (int)SLOT(s, P_M1_SRC), dest = (int)SLOT(s, P_M1_DEST), c = (int)SLOT(s, P_M1_CTL);
        float depth = SLOT(s, P_M1_DEPTH) / 100.0f;
        if (!src || depth == 0.0f) continue;
        int t = MODT[dest][c < MODN[dest] ? c : MODN[dest] - 1], k = 0, ch = sample_chan(t);
        while (k < P->ntouched && P->touched[k] != t) k++;
        if (k == P->ntouched) P->touched[P->ntouched++] = (short)t;
        /* SAMPLE on a folder of your own: +-100 % is its files */
        float span = ch >= 0 && P->sfold[ch] >= 0 ? sample_top(P, ch) : PDEF[t].hi - PDEF[t].lo;
        P->ev[t] += depth * mod_source(P, s, src, dest) * span;
    }
    for (int k = 0; k < P->ntouched; k++) {
        int t = P->touched[k], ch = sample_chan(t);
        float v = clampf(P->ev[t], PDEF[t].lo, ch >= 0 ? sample_top(P, ch) : PDEF[t].hi);
        P->ev[t] = PDEF[t].kind == 0 ? v : roundf(v);
        if (ch >= 0) sample_ask(P, ch, (int)P->ev[t]);
    }
}

static void mod_step(inc_t *P, int n) {   /* every SUB frames: the LFOs a slot uses on, then the slots (none: nothing) */
    int on = 0, lfos = 0;
    for (int s = 0; s < NSLOT; s++) {
        int src = (int)SLOT(s, P_M1_SRC);
        if (!src || SLOT(s, P_M1_DEPTH) == 0.0f) continue;
        on = 1;
        if (src >= SRC_LFO1 && src < SRC_LFO1 + NLFO) lfos |= 1 << (src - SRC_LFO1);
    }
    for (int l = 0; l < NLFO; l++)
        if (lfos >> l & 1)
            lfo_run(&P->mlfo[l], (int)LFOP(l, P_L1_WAVE), lfo_hz(LFOP(l, P_L1_RATE), LFOP(l, P_L1_CLOCK) >= 0.5f, P->bpm), n);
    if (on || P->ntouched) mod_apply(P);
}

/* a hit of instrument ins: its slots' RANDOM and VELOCITY (FX's at any hit), the LFOs it restarts; then the slots, so
 * what the hit reads as it starts has them */
static void mod_hit(inc_t *P, int ins, float vel) {
    P->mvel[ins] = P->mvel[NDEST - 1] = vel / 127.0f;
    for (int s = 0; s < NSLOT; s++) {
        int dest = (int)SLOT(s, P_M1_DEST);
        if (dest == ins || dest == NDEST - 1) P->mrand[s] = noise(&P->mrng);
    }
    for (int l = 0; l < NLFO; l++)
        if ((int)LFOP(l, P_L1_TRIG) == ins + 1) {
            P->mlfo[l].ph = 0.0f;
            lfo_run(&P->mlfo[l], (int)LFOP(l, P_L1_WAVE), 0.0f, 0);
        }
    mod_apply(P);
}

/* ---- MIDI ------------------------------------------------------------------------------------------------------ */
static const int MUTE0 = P_KD_MUTE;
#define KCH(i) ((int)P->pv[P_KD_KCH + IOSTRIDE * (i)])        /* 0 OFF, 1 ALL, 2-17 = channels 1-16 */
#define IOUT(i) ((int)P->pv[P_KD_OUT + IOSTRIDE * (i)] - 1)   /* -1 MAIN, 0-10 OUT 1-11 */

/* note: the MIDI note; pitched: on the instrument's KEYS CH (C3 = as tuned), on MIDI channel ch */
static void trigger(inc_t *P, int ins, int note, float vel, int pitched, int ch) {
    if (P->pv[MUTE0 + ins] >= 0.5f) return;
    int hat = P->pv[P_HH_CHOKE] >= 0.5f && (ins == I_CH || ins == I_OH);
    if (hat && ins == I_CH && P->choke_n < CHOKE_SAME) return;   /* the open hat already played this step */
    mod_hit(P, ins, vel);
    play_t pl = {vel, pitched ? (float)(note - 60) : 0.0f, ch, pitched};
    switch (ins) {
    case I_KD: kick_trigger(P, &pl); break;
    case I_MB: mbrane_trigger(P, &pl); break;
    case I_FM:
        if (!pitched) fm_trigger(P, &pl, 36, 1);
        else {   /* a note a voice, the note its tuning (36 = as tuned): the bend is all play_ratio adds */
            pl.semi = 0.0f;
            fm_trigger(P, &pl, note - 24, -1);
        }
        break;
    default: {
        int s = ins - I_CH;
        if (hat) {
            smpv_t *o = &P->sv[ins == I_CH ? 1 : 0];
            if (o->active) o->choke = 0.999f;   /* fades in a few ms */
            if (ins == I_OH) P->choke_n = 0;
        }
        smp_trigger(P, s, &pl);
        P->sv[s].note = note;
    }
    }
}

/* a MIDI MOD lane: CC value v (0-127) sets control i across its range, as a Q-Link would; MPC's screen follows */
static void lane_set(inc_t *P, int i, int v) {
    const pdef_t *d = &PDEF[i];
    int opt = v * d->nopts / 128, c = sample_chan(i);   /* an option list in equal parts */
    float hi = c >= 0 ? sample_top(P, c) : d->hi;       /* SAMPLE on a folder of your own: its files */
    float x = d->kind == 1 ? (float)opt : d->lo + (hi - d->lo) * (float)v / 127.0f;
    P->pv[i] = P->ev[i] = d->kind == 4 ? roundf(x) : x;
    if (c >= 0) sample_ask(P, c, (int)P->pv[i]);
    P->changed[i / 32] |= 1u << (i % 32);
}

/* ---- RANDOMISE ------------------------------------------------------------------------------------------------- */
static int rnd_in(inc_t *P, int lo, int hi) {
    uint32_t x = P->rrng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    P->rrng = x;
    return lo + (int)(x % (uint32_t)(hi - lo + 1));
}

/* a SAMPLE for instrument ins from the sample set: among the numbers of its kind (the set's rough classes), one there;
 * a hat or a cymbal has none a third of the time (the analog part alone). -1: none found (no set), leave it */
static int rand_sample(inc_t *P, int ins) {
    static const short HATS[] = {1, 24, 0}, CLAPS[] = {84, 96, 132, 161, 0}, RIMS[] = {60, 83, 84, 96, 132, 161, 0},
                       CYMS[] = {27, 59, 168, 217, 0}, ANY[] = {1, 255, 0};   /* SAMPLE's reach */
    const short *r = ins == I_CH || ins == I_OH ? HATS : ins == I_CP ? CLAPS : ins == I_RM ? RIMS
                   : ins == I_CR || ins == I_RD ? CYMS : ANY;
    if (ins < I_X1 && rnd_in(P, 0, 99) < 33) return 0;
    int f = P->sfold[ins - I_CH], n = 0;
    if (f >= 0) {   /* a folder of your own: any of its files */
        int nf = lib_files(f);
        return nf > 0 ? rnd_in(P, 1, nf) : -1;
    }
    for (int k = 0; r[k]; k += 2) n += r[k + 1] - r[k] + 1;
    for (int tries = 0; tries < 64; tries++) {
        int j = rnd_in(P, 0, n - 1), k = 0;
        while (j > r[k + 1] - r[k]) j -= r[k + 1] - r[k] + 1, k += 2;
        if (pool_exists(r[k] + j)) return r[k] + j;
    }
    return -1;
}

static void rand_set(inc_t *P, int i, float v) {
    P->pv[i] = P->ev[i] = v;
    P->changed[i / 32] |= 1u << (i % 32);
}

/* a new sound for instrument ins (gen.py's RAND: each control's range), then whatever makes it heard: a hat or a
 * cymbal with no sample keeps a noise source and its filter. Its VOLUME, ACCENT and MIX stay; MPC's screen follows */
static void randomise(inc_t *P, int ins) {
    for (int pass = 0; pass < 2; pass++)   /* "the same as" controls after the ones they follow */
        for (int c = 0; c < MODN[ins]; c++) {
            int i = MODT[ins][c];
            const rnd_t *r = &RND[i];
            if (r->how == 2 && pass == 1) rand_set(P, i, P->pv[r->eq]);
            if (pass == 1 || (r->how != 1 && r->how != 3)) continue;
            if (r->how == 3) {
                int n = rand_sample(P, ins);
                if (n >= 0) rand_set(P, i, (float)n);
            } else {
                rand_set(P, i, (float)(r->p && rnd_in(P, 0, 99) < r->p ? r->v : rnd_in(P, r->lo, r->hi)));
            }
        }
    if (ins >= I_CH && ins < I_X1) {
        int b = SBASE[ins - I_CH], smp = P->pv[b + S_SAMPLE] >= 0.5f;
        if (!smp && P->pv[b + S_ROUT] < 0.5f) rand_set(P, b + S_ROUT, 1.0f);   /* OFF passes the sample alone */
        int rout = (int)P->pv[b + S_ROUT];
        /* the multimode filter: a low-pass not so low (CUTOFF 55 is 300 Hz), a band-pass not below 200 Hz, a high-pass
         * not so high, nor its envelope so deep, that little is left */
        if (ins >= I_CP && rout == 1 && P->pv[b + S_CUTOFF] < 55.0f) rand_set(P, b + S_CUTOFF, (float)rnd_in(P, 55, 127));
        if (ins >= I_CP && rout == 3 && P->pv[b + S_CUTOFF] < 50.0f) rand_set(P, b + S_CUTOFF, (float)rnd_in(P, 50, 120));
        if (ins >= I_CP && rout == 2) {
            if (P->pv[b + S_CUTOFF] > 100.0f) rand_set(P, b + S_CUTOFF, (float)rnd_in(P, 30, 100));
            if (P->pv[b + S_EVAMT] > 40.0f) rand_set(P, b + S_EVAMT, (float)rnd_in(P, 0, 40));
        }
        /* the hats' band: the low-pass an octave or more above the high-pass */
        if (ins < I_CP && hh_lp_hz(P->pv[b + S_CUTOFF]) < 2.0f * hh_hp_hz(P->pv[b + S_HPCUT])) {
            float hp = P->pv[b + S_HPCUT];
            while (hp > 0.0f && hh_lp_hz(P->pv[b + S_CUTOFF]) < 2.0f * hh_hp_hz(hp)) hp -= 5.0f;
            rand_set(P, b + S_HPCUT, hp < 0.0f ? 0.0f : hp);
        }
        if (!smp) {   /* no sample: a noise source, unless the filter whistles */
            int whistle = ins >= I_CP && P->pv[b + S_RES] > 85.0f;
            if (!whistle && P->pv[b + S_MTNZ] < 0.5f) rand_set(P, b + S_MTNZ, (float)rnd_in(P, 1, 127));
            if (!whistle && P->pv[b + S_NZVOL] < 60.0f) rand_set(P, b + S_NZVOL, (float)rnd_in(P, 60, 127));
        }
    }
    if (ins >= I_CH && ins <= I_X2) sample_ask(P, ins - I_CH, (int)P->pv[SMP_P(ins - I_CH)]);
}

/* ---- the SAMPLES tab: the drives, their folders and WAV files (library.c reads them, off the audio thread) -------- */
static void mark(inc_t *P, int i) { P->changed[i / 32] |= 1u << (i % 32); }

static void browse(inc_t *P, const char *path) {   /* show this folder ("" = the places) */
    if (!P->br && !(P->br = lib_browser())) return;
    if (path != P->bpath) snprintf(P->bpath, sizeof P->bpath, "%s", path);
    lib_browse(P->br, P->bpath);
    P->bwait = 1;
}

static void scroll_to(inc_t *P, int r) {
    int n = P->bl ? P->bl->n : 0, top = n > BROWSE_ROWS ? n - BROWSE_ROWS : 0;
    r = r < 0 ? 0 : r > top ? top : r;
    if (P->pv[P_BR_SCROLL] != (float)r) mark(P, P_BR_SCROLL);
    P->pv[P_BR_SCROLL] = P->ev[P_BR_SCROLL] = (float)r;
}

/* the listing asked for, once it's read: shown from its top, or around the folder just left (UP) */
static void browse_adopt(inc_t *P) {
    if (!P->br) browse(P, P->bpath);   /* the tab's first look: where it was */
    lib_list_t *l = lib_listing(P->br);
    if (!l) return;
    int at = 0;
    if (P->bl && l->path[0] && !strncmp(P->bl->path, l->path, strlen(l->path)) && P->bl->path[strlen(l->path)] == '/') {
        const char *left = P->bl->path + strlen(l->path) + 1;
        for (int k = 0; k < l->n; k++)
            if (l->e[k].kind == LE_DIR && !strcmp(l->e[k].name, left)) at = k - BROWSE_ROWS / 2 + 1;
    }
    lib_list_free(P->bl);
    P->bl = l;
    P->bwait = 0;
    scroll_to(P, at);
    for (int r = 0; r < BROWSE_ROWS; r++) mark(P, P_BR_ROW_1 + r);   /* their text: MPC asks again */
}

static int place_top(const inc_t *P, const char *path) {   /* a drive's own folder, or the plugin's samples folder */
    char s[LIB_PATH + 16];
    snprintf(s, sizeof s, "%s/samples", P->dir);
    const char *sl = strrchr(path, '/');
    return !strcmp(path, s) || !sl || sl == path ||
           ((size_t)(sl - path) == strlen(LIB_MEDIA) && !strncmp(path, LIB_MEDIA, strlen(LIB_MEDIA)));
}

static void browse_up(inc_t *P) {
    if (!P->bpath[0]) return;
    if (place_top(P, P->bpath)) {
        browse(P, "");
        return;
    }
    char p[LIB_PATH];
    snprintf(p, sizeof p, "%s", P->bpath);
    *strrchr(p, '/') = 0;
    browse(P, p);
}

static void audition(inc_t *P, int c) {   /* PREVIEW: the channel plays its new sample once it's in */
    if (P->pv[P_BR_PREVIEW] < 0.5f) return;
    P->aud = c + 1;
    P->aud_n = 0;
}

static void browse_tap(inc_t *P, int row) {
    browse_adopt(P);
    lib_list_t *l = P->bl;
    int k = (int)P->pv[P_BR_SCROLL] + row, c = (int)P->pv[P_BR_TARGET];
    if (!l || P->bwait || k >= l->n) return;
    const lib_ent_t *e = &l->e[k];
    char p[LIB_PATH + 256];
    switch (e->kind) {
    case LE_UP: browse_up(P); break;
    case LE_PLACE: browse(P, e->path); break;
    case LE_DIR:
        snprintf(p, sizeof p, "%s/%s", l->path, e->name);
        if (strlen(p) < LIB_PATH) browse(P, p);
        break;
    case LE_ALPHABASE:   /* back to the set by number */
        set_folder(P, c, -1);
        sample_ask(P, c, (int)P->pv[SMP_P(c)]);
        mark(P, SMP_P(c));
        audition(P, c);
        break;
    default: {
        int f = lib_folder_set(l->path, l);
        if (f < 0) return;   /* (too many folders in one session: LIB_FOLDERS) */
        set_folder(P, c, f);
        int n = k - l->first_file + 1;
        P->pv[SMP_P(c)] = P->ev[SMP_P(c)] = (float)n;
        mark(P, SMP_P(c));
        sample_ask(P, c, n);
        audition(P, c);
    }
    }
    for (int r = 0; r < BROWSE_ROWS; r++) mark(P, P_BR_ROW_1 + r);   /* the loaded file's mark moved */
}

static int is_mine(const inc_t *P, const lib_list_t *l, int k) {   /* the file LOAD TO's channel plays */
    int c = (int)P->pv[P_BR_TARGET];
    const char *fp = lib_path(P->sfold[c]);
    return fp && !strcmp(fp, l->path) && (int)P->pv[SMP_P(c)] == k - l->first_file + 1;
}

static int browse_row(inc_t *P, int row, char *buf, int len) {
    browse_adopt(P);
    const lib_list_t *l = P->bl;
    int k = (int)P->pv[P_BR_SCROLL] + row;
    if (!l || k >= l->n) return snprintf(buf, len, "%s", !l && P->bwait && row == 0 ? "READING ..." : " ");
    const lib_ent_t *e = &l->e[k];
    switch (e->kind) {
    case LE_UP: return snprintf(buf, len, ".. UP");
    case LE_DIR: return snprintf(buf, len, "%.21s /", e->name);
    case LE_PLACE: return snprintf(buf, len, "%s", e->name);
    case LE_ALPHABASE: return snprintf(buf, len, "%s%s", P->sfold[(int)P->pv[P_BR_TARGET]] < 0 ? "> " : "", e->name);
    default: return snprintf(buf, len, "%s%.*s", is_mine(P, l, k) ? "> " : "", (int)strlen(e->name) - 4, e->name);
    }
}

static int browse_path(inc_t *P, char *buf, int len) {   /* where it is, its end if long (MPC shows 24 characters) */
    browse_adopt(P);
    const char *p = P->bpath;
    char s[LIB_PATH + 16];
    snprintf(s, sizeof s, "%s/samples", P->dir);
    if (!p[0]) return snprintf(buf, len, "DRIVES");
    char t[LIB_PATH + 32];
    if (!strncmp(p, s, strlen(s))) snprintf(t, sizeof t, "INCIPIENCE%s", p + strlen(s));
    else if (!strncmp(p, LIB_MEDIA "/", strlen(LIB_MEDIA) + 1)) snprintf(t, sizeof t, "%s", p + strlen(LIB_MEDIA) + 1);
    else snprintf(t, sizeof t, "%s", p);
    if (!strncmp(t, "az01-internal", 13)) {
        memmove(t + 8, t + 13, strlen(t + 13) + 1);
        memcpy(t, "INTERNAL", 8);
    }
    size_t n = strlen(t);
    return n > 23 ? snprintf(buf, len, "..%s", t + n - 21) : snprintf(buf, len, "%s", t);
}

static int browse_status(inc_t *P, char *buf, int len) {
    browse_adopt(P);
    const lib_list_t *l = P->bl;
    if (P->bwait || !l) return snprintf(buf, len, "%s", P->bwait ? "READING ..." : " ");
    if (l->missing) return snprintf(buf, len, "CAN'T OPEN IT");
    if (!l->path[0]) return snprintf(buf, len, "PICK A DRIVE");
    int dirs = l->first_file - 1;
    if (l->nfiles) return snprintf(buf, len, "%d WAV%s%s", l->nfiles, l->nfiles == 1 ? "" : "S", l->nfiles == LIB_FILES ? " (MAX)" : "");
    return dirs ? snprintf(buf, len, "%d FOLDER%s", dirs, dirs == 1 ? "" : "S") : snprintf(buf, len, "NO WAV FILES");
}

/* a saved project's file, once its folder is read: SAMPLE on it (if it's gone, SAMPLE stays at the number saved) */
static void find_wanted(inc_t *P, int c) {
    int n = lib_files(P->sfold[c]);
    for (int k = 0; k < n; k++)
        if (!strcmp(lib_name(P->sfold[c], k), P->swant[c])) {
            P->pv[SMP_P(c)] = P->ev[SMP_P(c)] = (float)(k + 1);
            mark(P, SMP_P(c));
            sample_ask(P, c, k + 1);
            break;
        }
    P->swant[c][0] = 0;
}

static void e_midi(void *inst, const uint8_t *m, int len) {
    inc_t *P = inst;
    apply_kit(P);
    if (len < 2) return;
    int st = m[0] & 0xF0, ch = m[0] & 0x0F, d1 = m[1] & 0x7F, d2 = len > 2 ? m[2] & 0x7F : 0;
    if (st == 0x90 && d2 > 0) {
        /* who plays it: an instrument whose KEYS CH this is takes every note on it, pitched; otherwise each instrument
         * whose NOTE it is, as it's tuned; otherwise those with KEYS CH = ALL, pitched (as Percolator) */
        int hit = 0;
        for (int pass = 0; pass < 3 && !hit; pass++)
            for (int i = 0; i < NI; i++) {
                int kch = KCH(i);
                if (pass == 0 ? kch != ch + 2 : pass == 1 ? (int)P->pv[P_KD_NOTE + NOTESTRIDE * i] != d1 : kch != 1)
                    continue;
                trigger(P, i, d1, (float)d2, pass != 1, ch);
                hit = 1;
            }
    } else if (st == 0x80 || st == 0x90) {
        /* by note alone: MPC hands a plugin every note-on on channel 1 but its note-off on the channel it came on
         * (Percolator's Live II trace, 2026-10-12) */
        for (int s = 0; s < 8; s++)
            if (P->sv[s].active && P->sv[s].note == d1) smp_release(P, s);
    } else if (st == 0xE0) {
        P->bend[ch] = (float)((d2 << 7 | d1) - 8192) / 8192.0f * 2.0f;
    } else if (st == 0xB0 && (d1 == 1 || (d1 >= 16 && d1 <= 18))) {
        /* the MIDI MOD lanes: the mod wheel and Gen Purpose 1-3 (CC 16-18), as MPC names them in a MIDI track's lane
         * menu. On an instrument's KEYS CH (any channel for ALL) each sets the control that instrument's lane names */
        int lane = d1 == 1 ? 0 : d1 - 15;
        for (int i = 0; i < NI; i++) {
            int kch = KCH(i), t = (int)P->pv[P_KD_WHEEL + LANESTRIDE * i + LANESTEP * lane];
            if (t > 0 && t <= MODN[i] && (kch == ch + 2 || kch == 1)) lane_set(P, MODT[i][t - 1], d2);
        }
    } else if (st == 0xB0 && (d1 == 120 || d1 == 123)) {
        for (int s = 0; s < 8; s++) {
            smp_release(P, s);
            if (d1 == 120) smp_drop(P, s);
        }
        if (d1 == 120) {
            P->kd.active = P->mb.active = 0;
            for (int v = 0; v < FM_VOICES; v++) P->fm[v].active = 0;
        }
    }
}

/* ---- OUT 1-11 (tapbus.h: how they work) ------------------------------------------------------------------------ */
_Static_assert(NOUT == TAP_OUTS && NI <= 32, "tapbus.h: an OUT per instrument, a bit each");
static uint32_t next_id = 1;

static int tap_claim(inc_t *P) {   /* this instance writes OUT 1-11: it already does, or nobody did */
    tapbus_t *b = P->bus;
    if (!b) return 0;
    uint32_t o = TB_LOAD(b->owner), none = 0;
    if (o == P->id) return 1;
    if (o || !__atomic_compare_exchange_n(&b->owner, &none, P->id, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    __atomic_add_fetch(&b->gen, 1, __ATOMIC_ACQ_REL);
    return 1;
}

static void tap_release(inc_t *P) {
    tapbus_t *b = P->bus;
    uint32_t me = P->id;
    if (b && TB_LOAD(b->owner) == me && __atomic_compare_exchange_n(&b->owner, &me, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        __atomic_add_fetch(&b->gen, 1, __ATOMIC_ACQ_REL);
}

/* ---- render ---------------------------------------------------------------------------------------------------- */
static inline int16_t out_sample(float x) {   /* a soft ceiling instead of a hard clip */
    float ax = fabsf(x);
    if (ax > 0.9f) x = copysignf(0.9f + 0.1f * sat((ax - 0.9f) * 10.0f), x);
    int s = (int)lrintf(x * 32767.0f);
    return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
}

/* constant-power pan, worked out again only when the (modulated) PAN moves */
static void pan_gains(inc_t *P, int i) {
    float pan = P->ev[P_KD_PAN + MIXSTRIDE * i];
    if (pan == P->pan_at[i]) return;
    float a = (clampf((pan - 64.0f) / 63.0f, -1.0f, 1.0f) + 1.0f) * 0.25f * PI_F;
    P->pan_at[i] = pan;
    P->pan_l[i] = cosf(a) * 1.41421356f;
    P->pan_r[i] = sinf(a) * 1.41421356f;
}

static void e_render(void *inst, int16_t *out, int frames) {
    inc_t *P = inst;
    apply_kit(P);
    uint8_t m[3];   /* what MIDI tracks sent to Incipience MIDI In since the last block, channels and all */
    for (int k = 0, n; k < 256 && (n = mi_read(&P->midi_in, m)) > 0; k++) e_midi(P, m, n);
    pool_poke();
    lib_poke();
    for (int c = 0; c < 8; c++)
        if (P->swant[c][0] && lib_files(P->sfold[c]) >= 0) find_wanted(P, c);
    if (P->aud) {   /* PREVIEW: the channel's new sample, once it's in (a second at most), as a 1/4 s tap of its pad */
        int c = P->aud - 1, n = (int)P->pv[SMP_P(c)], ready = n <= 0;
        smp_t sm;
        if (!ready && P->sfold[c] >= 0) {
            int sl = lib_get(P->sfold[c], n - 1, &sm);
            ready = sl >= 0;
            lib_put(sl);
        } else if (!ready) {
            ready = pool_get(n, &sm);
        }
        if (ready) {
            trigger(P, I_CH + c, (int)P->pv[P_KD_NOTE + NOTESTRIDE * (I_CH + c)], 100.0f, 0, 0);
            P->aud_ch = c;
            P->aud_off = 44100 / 4;
            P->aud = 0;
        } else if ((P->aud_n += frames) > 44100) {
            P->aud = 0;
        }
    }
    if (P->aud_off > 0 && (P->aud_off -= frames) <= 0) smp_release(P, P->aud_ch);
    fpstate_t fp = ftz_on();
    /* OUT 1-11: this one writes them if it is the main one; an instrument sent to an OUT something plays leaves this
     * mix (its reverb and delay sends stay), and this mix waits a block for it */
    tapbus_t *bus = P->bus;
    int own = tap_claim(P), to[NI], tapped[NOUT] = {0}, hold = 0;
    uint32_t w0 = own ? TB_LOAD(bus->wpos) : 0, g = own ? TB_LOAD(bus->gen) : 0, mask[NOUT] = {0};
    for (int o = 0; o < NOUT && own; o++) {
        tapped[o] = TB_LOAD(bus->seen_gen[o]) == g && w0 - TB_LOAD(bus->seen[o]) < TAP_ALIVE;
        hold |= tapped[o];
    }
    for (int i = 0; i < NI; i++) {
        int o = IOUT(i);
        if (o >= 0) mask[o] |= 1u << i;
        to[i] = o >= 0 && tapped[o] ? o : -1;
    }
    for (int o = 0; o < NOUT && own; o++) TB_STORE(bus->inst[o], mask[o]);
    if (frames > TAP_HOLD) hold = 0;
    for (int off = 0; off < frames; off += SUB) {
        int n = frames - off < SUB ? frames - off : SUB;
        mod_step(P, n);
        float vol = P->ev[P_VOLUME] / 100.0f;   /* 100 (to start with) = 0 dB, 127 = +4 dB */
        vol = vol * vol;
        float L[SUB] = {0}, R[SUB] = {0}, rin[SUB] = {0}, din[SUB] = {0};
        for (int o = 0; o < NOUT; o++)   /* an OUT is written only while something reads it */
            if (tapped[o])
                for (int i = 0; i < n; i++) bus->ring[o][0][(w0 + off + i) & (TAP_N - 1)] = bus->ring[o][1][(w0 + off + i) & (TAP_N - 1)] = 0.0f;
        for (int ins = 0; ins < NI; ins++) {
            float y[SUB] = {0}, side[SUB] = {0};
            switch (ins) {
            case I_KD: if (!P->kd.active) continue; kick_block(P, y, n); break;
            case I_MB: if (!P->mb.active) continue; mbrane_block(P, y, n); break;
            case I_FM: fm_block(P, y, side, n); break;
            default: if (!P->sv[ins - I_CH].active) continue; smp_block(P, ins - I_CH, y, n); break;
            }
            if (P->pv[MUTE0 + ins] >= 0.5f) continue;
            pan_gains(P, ins);
            float gl = P->pan_l[ins], gr = P->pan_r[ins];
            float r = P->ev[P_KD_RSEND + MIXSTRIDE * ins] / 127.0f, d = P->ev[P_KD_DSEND + MIXSTRIDE * ins] / 127.0f;
            float rs = r * r, ds = d * d;
            float *l = L, *rr = R, g2 = 1.0f;
            uint32_t at = 0, wrap = SUB;   /* into this mix, or into OUT to[ins] at the main VOLUME */
            if (to[ins] >= 0) {
                l = bus->ring[to[ins]][0];
                rr = bus->ring[to[ins]][1];
                at = w0 + (uint32_t)off;
                wrap = TAP_N;
                g2 = vol;
            }
            for (int i = 0; i < n; i++) {
                uint32_t j = (at + (uint32_t)i) & (wrap - 1);
                l[j] += (y[i] * gl + side[i]) * g2;
                rr[j] += (y[i] * gr - side[i]) * g2;
                rin[i] += y[i] * rs;
                din[i] += y[i] * ds;
            }
        }
        fx_block(P, rin, din, L, R, n);
        for (int i = 0; i < n; i++) {
            float l = L[i] * vol, r = R[i] * vol;
            uint32_t w = P->hold_w++ & (TAP_HOLD - 1);
            P->hold[0][w] = l;
            P->hold[1][w] = r;
            if (hold) {   /* a block late, with the OUTs */
                uint32_t h = (w - (uint32_t)frames) & (TAP_HOLD - 1);
                l = P->hold[0][h];
                r = P->hold[1][h];
            }
            out[2 * (off + i)] = out_sample(l);
            out[2 * (off + i) + 1] = out_sample(r);
        }
        if (P->choke_n < 1 << 30) P->choke_n += n;
    }
    if (own) TB_STORE(bus->wpos, w0 + (uint32_t)frames);
    ftz_off(fp);
}

/* ---- create / destroy / parameters ------------------------------------------------------------------------------ */
static void *e_create(const char *dir) {
    inc_t *P = calloc(1, sizeof *P);
    if (!P) return NULL;
    snprintf(P->dir, sizeof P->dir, "%s", dir ? dir : ".");
    inc_tables_init(P->dir);
    pool_open(P->dir);
    lib_open(P->dir);
    for (int i = 0; i < NP; i++) P->pv[i] = P->ev[i] = PDEF[i].def;
    for (int c = 0; c < 8; c++) P->sfold[c] = -1;
    for (int i = 0; i < NI; i++) P->pan_at[i] = -1.0f;
    P->mrng = 0x6c8e9cf5u;
    P->id = __atomic_fetch_add(&next_id, 1, __ATOMIC_RELAXED);
    P->rrng = ((uint32_t)time(NULL) * 2654435761u ^ P->id * 0x9E3779B9u) | 1u;
    P->bus = tapbus_open();
    mi_open(&P->midi_in, "Incipience");
    voices_init(P);
    fx_init(&P->fx);
    P->bpm = 120.0f;
    P->kit_pending = -1;
    P->choke_n = 1 << 20;
    kits_load(P);
    load_kit(P, 0);
    return P;
}

static void e_destroy(void *inst) {
    inc_t *P = inst;
    tap_release(P);
    tapbus_close(P->bus);
    mi_close(&P->midi_in);
    fx_free(&P->fx);
    kits_free(P);
    for (int s = 0; s < 8; s++) smp_drop(P, s);
    lib_browser_free(P->br);
    lib_list_free(P->bl);
    lib_close();
    pool_close();
    free(P);
}

static void e_set_param(void *inst, const char *key, const char *val) {
    inc_t *P = inst;
    if (!strcmp(key, "state")) {
        P->kit_pending = -1;
        reset_all(P);
        apply_state(P, val, 0);
        for (int c = 0; c < 8; c++) sample_ask(P, c, (int)P->pv[SMP_P(c)]);
        if (P->br) browse(P, P->bpath);
        return;
    }
    if (!strcmp(key, "lfo_bpm")) {
        float b = (float)atof(val);
        if (b > 20.0f && b < 400.0f) P->bpm = b;
        return;
    }
    int i = find_key(key);
    if (i < 0) return;
    if (i == P_KIT) {
        int n = atoi(val);
        P->kit_pending = n < 0 ? 0 : n >= P->nkits ? P->nkits - 1 : n;
        return;
    }
    apply_kit(P);
    if (i >= P_KD_RAND && i < P_KD_RAND + NI) {   /* RANDOMISE: on the press (the release sends 0) */
        if (atof(val) >= 0.5) randomise(P, i - P_KD_RAND);
        return;
    }
    if (PDEF[i].kind == 3 && i >= P_BR_ROW_1) {   /* the SAMPLES tab's taps (each sends 1, its release 0) */
        if (atof(val) < 0.5) return;
        if (i < P_BR_ROW_1 + BROWSE_ROWS) browse_tap(P, i - P_BR_ROW_1);
        else if (i == P_BR_UP) browse_up(P);
        else if (i == P_BR_PLACES) browse(P, "");
        return;
    }
    set_value(P, i, val);
    int c = sample_chan(i);
    if (c >= 0) {   /* a new sample: start reading it; on a folder of your own, no further than its files */
        if (P->pv[i] > sample_top(P, c) && lib_files(P->sfold[c]) >= 0) P->pv[i] = P->ev[i] = sample_top(P, c);
        sample_ask(P, c, (int)P->pv[i]);
    }
    if (i == P_BR_SCROLL) scroll_to(P, (int)P->pv[i]);
    if (i == P_BR_TARGET)
        for (int r = 0; r < BROWSE_ROWS; r++) mark(P, P_BR_ROW_1 + r);
}

void mpc_engine_transport(void *inst, double bpm, double ppq, int playing) {
    inc_t *P = inst;
    if (bpm > 20.0 && bpm < 400.0) P->bpm = (float)bpm;
    (void)ppq;
    (void)playing;
}

/* ---- what the screen shows ------------------------------------------------------------------------------------- */
static int fmt_hz(char *buf, int len, float hz) {
    return hz < 1000.0f ? snprintf(buf, len, "%.0f Hz", hz) : snprintf(buf, len, "%.1f kHz", hz / 1000.0f);
}
static int fmt_s(char *buf, int len, float s) {
    return s < 1.0f ? snprintf(buf, len, "%.0f ms", s * 1000.0f) : snprintf(buf, len, "%.2f s", s);
}

static int display(inc_t *P, int i, char *buf, int len) {
    const char *k = PDEF[i].key;
    float v = P->pv[i];
    int iv = (int)lrintf(v);
    size_t kl = strlen(k);
#define ENDS(s) (kl >= strlen(s) && !strcmp(k + kl - strlen(s), s))
    if (ENDS("_metnz") || ENDS("_mtnz") || ENDS("_mtnz1") || ENDS("_mtnz2"))
        return iv == 0 ? snprintf(buf, len, "OFF") : iv == 1 ? snprintf(buf, len, "NOISE") : snprintf(buf, len, "%03d", iv);
    if (i >= P_BR_ROW_1 && i < P_BR_ROW_1 + BROWSE_ROWS) return browse_row(P, i - P_BR_ROW_1, buf, len);
    /* the rows shown (the box beside FOLDER says how many there are: this one mustn't change as a listing comes in) */
    if (i == P_BR_SCROLL) return snprintf(buf, len, "ROWS %d-%d", iv + 1, iv + BROWSE_ROWS);
    if (ENDS("_sample")) {
        int c = sample_chan(i);
        if (c >= 0 && P->sfold[c] >= 0) {   /* a file of the channel's own folder, by name */
            const char *nm = lib_name(P->sfold[c], iv - 1);
            if (lib_files(P->sfold[c]) < 0) return snprintf(buf, len, "READING");
            if (iv == 0) return snprintf(buf, len, "OFF");
            return nm ? snprintf(buf, len, "%.*s", (int)strlen(nm) - 4, nm) : snprintf(buf, len, "%03d (none)", iv);
        }
        if (iv == 0) return snprintf(buf, len, "OFF");
        return snprintf(buf, len, pool_exists(iv) ? "%03d" : "%03d (none)", iv);
    }
    if (ENDS("_lfrat") || ENDS("_l1rat") || ENDS("_l2rat")) {
        int synced = 0;
        if (i >= SBASE[0] && i < XBASE[0]) {
            int c = 0;
            while (c < 5 && i >= SBASE[c + 1]) c++;
            synced = P->pv[SBASE[c] + S_LFCLK] >= 0.5f;
        } else if (i >= XBASE[0] && i < P_FM_OP1) {
            synced = P->pv[(i < XBASE[1] ? XBASE[0] : XBASE[1]) + X_LFCLK] >= 0.5f;
        }
        if (synced) return iv == 0 ? snprintf(buf, len, "S000 still") : snprintf(buf, len, "S%03d %.3g/bar", iv, iv / 2.0f);
        return snprintf(buf, len, "%.2f Hz", lfo_hz(v, 0, P->bpm));
    }
    if (ENDS("_pitch") && i < P_KD_PITCH + 1) return fmt_hz(buf, len, kick_hz(v));
    if (i == P_MB_PITCH1 || i == P_MB_PITCH2) {
        if (P->pv[P_MB_SCENE] >= 1.0f) return snprintf(buf, len, "%+d", iv - 64);
        return fmt_hz(buf, len, mb_hz(v));
    }
    if (i == P_MB_SCENE) {
        static const char *const SC[10] = {"OFF", "SNARE 1", "SNARE 2", "SNARE 3", "SNARE 4", "TOM 1", "TOM 2", "TOM 3",
                                           "KICK 1", "KICK 2"};
        return snprintf(buf, len, "%s", SC[iv < 0 ? 0 : iv > 9 ? 9 : iv]);
    }
    if (i == P_MB_CUPL2) return snprintf(buf, len, "%+d", iv - 64);
    if (ENDS("_pan")) return iv == 64 ? snprintf(buf, len, "C") : snprintf(buf, len, "%c%d", iv < 64 ? 'L' : 'R', abs(iv - 64));
    if (i >= SBASE[0] && i < XBASE[0] + 32) {   /* the sample channels' and X's own units */
        if (ENDS("_pitch")) return snprintf(buf, len, "%03d (%+d st)", iv, iv - 55);
        if (ENDS("_cutoff")) {
            int c = 0;
            while (c < 5 && i >= SBASE[c + 1]) c++;
            return fmt_hz(buf, len, c < 2 ? hh_lp_hz(v) : mm_cut_hz(v));
        }
        if (ENDS("_hpcut")) return fmt_hz(buf, len, hh_hp_hz(v));
        if (ENDS("att")) return fmt_s(buf, len, time_att(v));
        if (ENDS("dec") || ENDS("rel")) return fmt_s(buf, len, time_dec(v));
    }
    if (i == P_DL_TIME) {
        if (P->pv[P_DL_SYNC] >= 0.5f) {
            static const char *const D[10] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/4."};
            int d = iv * 10 / 128;
            return snprintf(buf, len, "%s", D[d < 0 ? 0 : d > 9 ? 9 : d]);
        }
        return fmt_s(buf, len, delay_seconds(P));
    }
    if (i == P_KD_GATE || i == P_MB_GATE) return snprintf(buf, len, "%.1f ms", 0.1f * powf(150.0f, v / 127.0f));
    if (i == P_KD_DECAY) return fmt_s(buf, len, expmap(v, 0.05f, 5.0f));
    if (i == P_MB_DECAY) return fmt_s(buf, len, expmap(v, 0.02f, 6.0f));
    if (i >= P_FM_OP1 && i < P_FM_OP1 + 16 && (i - P_FM_OP1) % 5 == 0) {
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return snprintf(buf, len, "%03d %s%d", iv, NN[iv % 12], iv / 12);
    }
    if (i >= P_M1_SRC) {   /* a mod slot's CONTROL (by name, on its DEST) and DEPTH */
        int s = (i - P_M1_SRC) / SLOTSTRIDE, o = i - SLOTSTRIDE * s;
        if (o == P_M1_CTL) {
            int dest = (int)SLOT(s, P_M1_DEST);
            return snprintf(buf, len, "%s", MODNAME[dest][iv < MODN[dest] ? iv : MODN[dest] - 1]);
        }
        if (o == P_M1_DEPTH) return snprintf(buf, len, "%+d%%", iv);
    }
    if (i >= P_L1_WAVE && i < P_M1_SRC && (i - P_L1_WAVE) % LFOSTRIDE == P_L1_RATE - P_L1_WAVE) {   /* a mod LFO */
        int l = (i - P_L1_WAVE) / LFOSTRIDE;
        if (LFOP(l, P_L1_CLOCK) >= 0.5f)
            return iv == 0 ? snprintf(buf, len, "S000 still") : snprintf(buf, len, "S%03d %.3g/bar", iv, iv / 2.0f);
        return snprintf(buf, len, "%.2f Hz", lfo_hz(v, 0, P->bpm));
    }
    if (PDEF[i].kind == 4) return snprintf(buf, len, "%d", iv);
    return snprintf(buf, len, "%03d", iv);
#undef ENDS
}

static int e_get_param(void *inst, const char *key, char *buf, int len) {
    inc_t *P = inst;
    if (!strcmp(key, "state")) {
        apply_kit(P);
        return get_state(P, buf, len);
    }
    if (!strcmp(key, "kit_count")) return snprintf(buf, len, "%d", P->nkits);
    if (!strcmp(key, "br_path")) return browse_path(P, buf, len);
    if (!strcmp(key, "br_status")) return browse_status(P, buf, len);
    int cur = P->kit_pending >= 0 ? P->kit_pending : (int)P->pv[P_KIT];
    if (!strcmp(key, "kit")) return snprintf(buf, len, "%d", cur);
    if (!strcmp(key, "kit_name") || !strncmp(key, "kit_name_at:", 12)) {
        int n = key[8] ? atoi(key + 12) : cur;
        return n >= 0 && n < P->nkits ? snprintf(buf, len, "%s", P->kits[n].name) : 0;
    }
    int i = find_key(key);
    if (i >= 0) {
        if (PDEF[i].kind == 0) return snprintf(buf, len, "%.2f", P->pv[i]);
        if (PDEF[i].kind == 1 || PDEF[i].kind == 4) return snprintf(buf, len, "%d", (int)P->pv[i]);
        return snprintf(buf, len, "0");
    }
    size_t kl = strlen(key);
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        char base[48];
        snprintf(base, sizeof base, "%.*s", (int)(kl - 8), key);
        i = find_key(base);
        if (i >= 0) return display(P, i, buf, len);
    }
    return 0;
}

static const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

/* After each block the wrapper asks which parameters changed here (the MIDI MOD lanes, RANDOMISE), one at a time, and
 * tells MPC, so the screen and the Q-Links follow. A wrapper without the hook never asks: the values still change. */
int mpc_engine_changed(void *inst, char *key, int len) {
    inc_t *P = inst;
    for (int w = 0; w < (int)(sizeof P->changed / sizeof P->changed[0]); w++)
        if (P->changed[w]) {
            int b = __builtin_ctz(P->changed[w]);
            P->changed[w] &= P->changed[w] - 1;
            return snprintf(key, len, "%s", PDEF[32 * w + b].key) > 0;
        }
    return 0;
}
