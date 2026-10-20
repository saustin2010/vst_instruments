/* Percolator engine: parameters, MIDI, kits, the LFO and the master section (BBD delay, compressor, volume).
 *
 * MIDI: each voice has a NOTE (36-39, C1-D#1, to start with) that plays it as tuned, and a KEYS CH (channels 2-5 to start
 * with; OFF; ALL) on which every note plays it pitched (C3 = as tuned) and pitch bend bends it +-2 semitones (see
 * e_midi), and the MIDI MOD lanes (the mod wheel, Gen Purpose 1-3) move the controls they name. Several voices on one note play together. A voice
 * whose DECAY is at the top drones while its note is held. CC 70-113 set the voices' controls as on the Pērkons
 * (perkons_cc); CC 120 / 123 stop everything. Notes on their own channels come through Percolator MIDI In (midiin.h):
 * MPC hands the plugin's track every note-on on channel 1. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "percolator.h"

/* PERC_TRACE (a device test build only: "cflags" -DPERC_TRACE): every parameter set, state restore, MIDI message,
 * create and destroy, timestamped, into /tmp/percolator-trace.log, to see what MPC sends (2026-10-10: STOP). */
#ifdef PERC_TRACE
#include <stdarg.h>
#include <time.h>
static void trace(const char *fmt, ...) {
    static FILE *f;
    if (!f) f = fopen("/tmp/percolator-trace.log", "a");
    if (!f) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(f, "%ld.%03ld ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
}
#define TRACE(...) trace(__VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

/* the wrapper's engine interface (mpc-vst-plugins wrapper/engine.h) */
typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
} mpc_engine_t;
const mpc_engine_t *mpc_engine(void);
int mpc_engine_changed(void *inst, char *key, int len);   /* optional (engine.h): what a MIDI CC changed */

#define VK(v, k) (P_V1_TUNE + VSTRIDE * (v) + (k))
#define VALGO(v) VK(v, 8)
#define VMODE(v) VK(v, 9)
#define VVCF(v) VK(v, 10)
#define VDEPTH(v, k) (P_V1_M_TUNE + MSTRIDE * (v) + (k))
#define VCOMP(v) (P_V1_COMP + (v))
#define VIO(v, p) ((p) + (P_V2_NOTE - P_V1_NOTE) * (v))   /* a voice's NOTE / KEYS CH / OUT: VIO(v, P_V1_KCH) */
#define VSX(v, p) ((p) + (P_V2_SRC - P_V1_SRC) * (v))     /* its SOURCE / SAMPLE / RANDOMISE: VSX(v, P_V1_SMP) */

static int find_key(const char *key) {
    for (int i = 0; i < NP; i++)
        if (!strcmp(PDEF[i].key, key)) return i;
    return -1;
}

/* ---- flush denormals while rendering (decaying filters and the delay would otherwise crawl) ------------------ */
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

/* ---- the controls after the LFO ----------------------------------------------------------------------------- */

static float lfo_rate(float s) { return 0.05f * powf(600.0f, s); }   /* 0.05 .. 30 Hz */

static void lfo_step(perc_t *P, int n) {
    P->lfo_ph += lfo_rate(P->pv[P_LFO_SPEED] / 100.0f) * n * INV_SR;
    if (P->lfo_ph >= 1.0f) {
        P->lfo_ph -= (int)P->lfo_ph;
        P->lfo_a = P->lfo_b;
        P->lfo_b = noise(&P->lfo_rng);
    }
    float ph = P->lfo_ph, y;
    switch ((int)P->pv[P_LFO_WAVE]) {
    case 0: y = sin_t(ph); break;
    case 1: y = 4.0f * fabsf(ph - 0.5f) - 1.0f; break;
    case 2: y = 2.0f * ph - 1.0f; break;
    case 3: y = 1.0f - 2.0f * ph; break;
    case 4: y = ph < 0.5f ? 1.0f : -1.0f; break;
    case 5: y = P->lfo_a; break;
    default: y = P->lfo_a + (P->lfo_b - P->lfo_a) * (0.5f - 0.5f * sin_t(ph * 0.5f + 0.25f)); break;
    }
    P->lfo_out = y;
}

static void vparams(const perc_t *P, int v, vparam_t *vp) {
    float m = P->lfo_out * P->pv[P_LFO_LEVEL] / 100.0f;
    for (int k = 0; k < NK; k++) {
        float base = P->pv[VK(v, k)] / 100.0f;
        float x = clampf(base + m * 0.1f * P->pv[VDEPTH(v, k)], 0.0f, 1.0f);
        if (k == K_DECAY && base < 0.99f && x > 0.985f) x = 0.985f;   /* the LFO doesn't start a drone */
        vp->k[k] = x;
    }
    vp->algo = (int)P->pv[VALGO(v)];
    vp->mode = (int)P->pv[VMODE(v)];
    vp->vcf = (int)P->pv[VVCF(v)];
    vp->src = (int)P->pv[VSX(v, P_V1_SRC)];
    vp->smp = (int)P->pv[VSX(v, P_V1_SMP)];
}

/* ---- kits --------------------------------------------------------------------------------------------------- */

static void load_kit(perc_t *P, int n) {
    if (n < 0 || n >= P->nkits) return;
    const kit_t *k = &P->kits[n];
    for (int v = 0; v < NV; v++) {
        for (int j = 0; j < NK; j++) {
            P->pv[VK(v, j)] = k->knob[v][j];
            P->pv[VDEPTH(v, j)] = (float)k->depth[v][j];
        }
        P->pv[VALGO(v)] = (float)k->algo[v];
        P->pv[VMODE(v)] = (float)k->mode[v];
        P->pv[VVCF(v)] = (float)k->vcf[v];
        P->pv[VSX(v, P_V1_SRC)] = 0;   /* a kit is the voices' algorithms (each keeps its sample, for SOURCE again) */
        voice_release(P, v);   /* a drone of the old kit stops */
    }
    if (k->has_lfo) {
        P->pv[P_LFO_SPEED] = k->lfo_speed;
        P->pv[P_LFO_WAVE] = (float)(k->lfo_wave > 6 ? 6 : k->lfo_wave);
        P->pv[P_LFO_LEVEL] = k->lfo_level;
    }
    if (k->has_fx) {
        P->pv[P_FX_TIME] = k->fx_time;
        P->pv[P_FX_FEEDBACK] = k->fx_feedback;
        P->pv[P_FX_RATE] = k->fx_rate;
        P->pv[P_FX_DEPTH] = k->fx_depth;
        P->pv[P_FX_RANGE] = (float)k->fx_range;
    }
    P->bank = k->bank;
    P->pv[P_KIT] = (float)n;
}

/* A kit set waits for the next block, MIDI event, other parameter or state save. MPC's plugin host (JUCE) sets
 * parameter 0 (KIT) to the other end of its range and straight back whenever it gets ready to play (on insert and on
 * every STOP: "a dodgy hack to force some plugins to initialise", for plugins without an editor); loading at once made
 * that load the last kit and then this one again over every knob moved since (Live II trace, 2026-10-10). Waiting, the
 * pair ends on the kit already loaded and nothing happens; a real change (PRESET menu, PREV / NEXT, KIT) loads within
 * one block. */
static void apply_kit(perc_t *P) {
    if (P->kit_pending < 0) return;
    int n = P->kit_pending;
    P->kit_pending = -1;
    if (n != (int)P->pv[P_KIT]) load_kit(P, n);
}

/* ---- MIDI --------------------------------------------------------------------------------------------------- */

static void set_value(perc_t *P, int i, const char *val);

/* The Pērkons HD-01's CCs (its MIDI implementation chart, as midi.guide lists it): 11 per voice from CC 70 (voice 1),
 * 81, 92 and 103, in the order of its controls, which is ours too: TUNE (64 = the middle), DECAY, PARAM 1, PARAM 2,
 * CUTOFF, DRIVE, FX SEND, LEVEL, then the ALGO, MODE and VCF switches (thirds of 0-127: 0-42, 43-85, 86-127). On any
 * channel: the number says which voice. MPC's screen follows through mpc_engine_changed below. */
static void perkons_cc(perc_t *P, int cc, int val) {
    int v = (cc - 70) / 11, k = (cc - 70) % 11, i = VK(v, 0) + k;
    char s[16];
    if (PDEF[i].kind == 1) snprintf(s, sizeof s, "%d", val * PDEF[i].nopts / 128);
    else if (k == K_TUNE) snprintf(s, sizeof s, "%.2f", val <= 64 ? val * 50.0f / 64.0f : 50.0f + (val - 64) * 50.0f / 63.0f);
    else snprintf(s, sizeof s, "%.2f", val * 100.0f / 127.0f);
    set_value(P, i, s);
    P->changed[i / 32] |= 1u << (i % 32);
}

static void e_midi(void *inst, const uint8_t *m, int len) {
    perc_t *P = inst;
    apply_kit(P);
    if (len > 0 && m[0] < 0xF0) TRACE("midi %02x %02x %02x (%d)", m[0], len > 1 ? m[1] : 0, len > 2 ? m[2] : 0, len);
    if (len < 2 || P->pv[P_OUTPUT] >= 1) return;   /* a tap plays the main instance's OUT, not its own voices */
    int st = m[0] & 0xF0, ch = m[0] & 0x0F, d1 = m[1] & 0x7F, d2 = len > 2 ? m[2] & 0x7F : 0;
    if (st == 0x90 && d2 > 0) {
        /* who plays it: a voice whose KEYS CH this is takes every note on it, pitched (the Perkons' MULTI); otherwise
         * each voice whose NOTE it is, as it's tuned; otherwise voices with KEYS CH = ALL, pitched (the old KEYS) */
        int hit = 0;
        for (int pass = 0; pass < 3 && !hit; pass++)
            for (int v = 0; v < NV; v++) {
                int kch = (int)P->pv[VIO(v, P_V1_KCH)];   /* 0 OFF, 1 ALL, 2-17 = channels 1-16 */
                if (pass == 0 ? kch != ch + 2 : pass == 1 ? (int)P->pv[VIO(v, P_V1_NOTE)] != d1 : kch != 1) continue;
                int pitched = pass != 1;
                vparam_t vp;
                vparams(P, v, &vp);
                voice_trigger(P, v, &vp, (float)d2, d1, pitched ? (float)(d1 - 60) : 0.0f);
                P->v[v].ch = ch;
                P->v[v].pitched = pitched;
                hit = 1;
            }
    } else if (st == 0x80 || st == 0x90) {
        /* by note alone: MPC hands a plugin every note-on on channel 1 but its note-off on the channel it came on
         * (Live II trace, 2026-10-12: a MIDI track on channel 3 sent 90 41 7f ... 82 41 40) */
        for (int v = 0; v < NV; v++)
            if (P->v[v].note == d1) voice_release(P, v);
    } else if (st == 0xE0) {
        P->bend[ch] = (float)((d2 << 7 | d1) - 8192) / 8192.0f * 2.0f;
    } else if (st == 0xB0 && d1 >= 70 && d1 <= 113) {
        perkons_cc(P, d1, d2);
    } else if (st == 0xB0 && (d1 == 1 || (d1 >= 16 && d1 <= 18))) {
        /* the MIDI MOD lanes: the mod wheel and Gen Purpose 1-3 (CC 16-18), as MPC names them in a MIDI track's lane
         * menu. On a voice's KEYS CH (any channel for ALL) each sets the control that voice's lane names, as that
         * control's Pērkons CC would, so a voice's MIDI track moves its controls by name, several at once */
        for (int v = 0; v < NV; v++) {
            int kch = (int)P->pv[VIO(v, P_V1_KCH)];
            int t = (int)P->pv[d1 == 1 ? P_V1_WHEEL + v : P_V1_GP1 + 3 * v + d1 - 16];
            if (t > 0 && (kch == ch + 2 || kch == 1)) perkons_cc(P, 70 + VSTRIDE * v + t - 1, d2);
        }
    } else if (st == 0xB0 && (d1 == 120 || d1 == 123)) {
        for (int v = 0; v < NV; v++) {
            voice_release(P, v);
            if (d1 == 120) voice_stop(P, v);
        }
    }
}

/* ---- your own samples (library.c; the SAMPLES tab) ------------------------------------------------------------ */

static void mark(perc_t *P, int i) { P->changed[i / 32] |= 1u << (i % 32); }

static float sample_top(const perc_t *P, int v) {   /* SAMPLE's reach: its folder's files (none without one) */
    int n = lib_files(P->sfold[v]);
    return n > 0 ? (float)n : 0.0f;
}

static void sample_ask(perc_t *P, int v) {   /* its file read now, if it isn't in memory, so the next hit has it */
    const sample_t *s;
    int n = (int)P->pv[VSX(v, P_V1_SMP)];
    if (n > 0 && P->sfold[v] >= 0) lib_put(lib_get(P->sfold[v], n - 1, &s));
}

/* the slices of the file a voice's SAMPLE names (0: none, or it isn't in memory yet) */
static int own_slices(const perc_t *P, int v) {
    const sample_t *s = NULL;
    int n = (int)P->pv[VSX(v, P_V1_SMP)], sl = n > 0 && P->sfold[v] >= 0 ? lib_get(P->sfold[v], n - 1, &s) : -1;
    int k = sl >= 0 && s->ncue >= 2 ? s->ncue - 1 : 0;
    lib_put(sl);
    return k;
}

static void set_folder(perc_t *P, int v, int f) {   /* -1: none */
    P->sfold[v] = (short)f;
    P->swant[v][0] = 0;
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

/* ---- the SAMPLES tab: Incipience 1.1.0's browser (owner, 2026-10-20: "I want the same browser in percolator"). A
 * folder's WAV files and every subfolder's in one list, a cursor (FILE: the data wheel on its box, BROWSE on Q-Link 1)
 * whose file plays as it lands (PREVIEW), a second tap or LOAD to load it into LOAD TO's voice (AUTO LOAD: as it
 * lands). How it works: the repo's docs/sample-browser.md */
static void mark_rows(perc_t *P) {   /* their text and which is lit: MPC asks again */
    for (int r = 0; r < BROWSE_ROWS; r++) mark(P, P_BR_ROW_1 + r);
}

static void scroll_to(perc_t *P, int r) {
    int n = P->bl ? P->bl->n : 0, top = n > BROWSE_ROWS ? n - BROWSE_ROWS : 0;
    r = r < 0 ? 0 : r > top ? top : r;
    if (P->pv[P_BR_SCROLL] != (float)r) {
        mark(P, P_BR_SCROLL);
        mark_rows(P);
    }
    P->pv[P_BR_SCROLL] = (float)r;
}

/* PREVIEW: the cursor's file straight from its folder (no voice), until it ends or the cursor moves */
static void preview_stop(perc_t *P) {
    if (P->prv_slot >= 0) lib_put(P->prv_slot);
    P->prv_slot = -1;
    P->prv_want = 0;
}

static void preview(perc_t *P, int k) {   /* file entry k of the listing shown */
    preview_stop(P);
    const lib_list_t *l = P->bl;
    if (P->pv[P_BR_PREVIEW] < 0.5f || !l || k < 0 || k >= l->n || l->e[k].kind != LE_FILE) return;
    int g = l->e[k].grp, f = lib_folder_set(l, g);
    if (f < 0) return;   /* (too many folders in one session: LIB_FOLDERS) */
    const sample_t *s;
    P->prv_f = f;
    P->prv_k = k - l->g[g].first;
    P->prv_want = 1;
    P->prv_n = 0;
    lib_put(lib_get(f, P->prv_k, &s));   /* asked for now: read on the library's thread */
}

static void preview_block(perc_t *P, float *y, int n) {   /* -6 dB (about where a voice plays it), under VOLUME */
    const sample_t *s = P->prv_s;
    double inc = s->rate * INV_SR;
    for (int i = 0; i < n; i++) {
        int j = (int)P->prv_pos;
        if (j + 1 >= s->len) {
            preview_stop(P);
            return;
        }
        float fr = (float)(P->prv_pos - j);
        y[i] += (s->d[j] + (s->d[j + 1] - s->d[j]) * fr) * 0.5f;
        P->prv_pos += inc;
    }
}

/* the cursor (FILE), an entry of the listing: its row is lit (its value reads 1) and the view follows it */
static int cursor(const perc_t *P) { return (int)P->pv[P_BR_CURSOR]; }

static void cursor_set(perc_t *P, int k) {   /* FILE, and FILE's Q-Link (BROWSE, its own parameter) reading the same */
    P->pv[P_BR_CURSOR] = P->pv[P_BR_CURSOR_Q] = (float)k;
    mark(P, P_BR_CURSOR);
    mark(P, P_BR_CURSOR_Q);
}

/* MPC sends a Q-Link's small turn as several events (a data wheel click as one, the same size), so BROWSE moves a row,
 * and SCROLL a page, per QL_TICKS events; turning back starts over (Incipience on the Live II, 2026-10-20: three) */
#define QL_TICKS 3
static int ql_tick(perc_t *P, int q, int dir) {   /* 1: this event makes a step */
    if (!dir) return 0;
    if (dir * P->qacc[q] < 0) P->qacc[q] = 0;
    P->qacc[q] += dir;
    if (abs(P->qacc[q]) < QL_TICKS) return 0;
    P->qacc[q] = 0;
    return 1;
}

/* A small move of a long list (under a tenth of its range: a wheel click, a Q-Link event, an arrow) is one step, as the
 * release tools' wrapper makes it with nudge_pct; this repo's wrapper hands on a wheel click as 1/100 of the range (ten
 * files of FILE, two or three of SAMPLE) */
static int nudge(int cur, float x, int range) {
    int to = (int)lrintf(x), d = to - cur;
    return d && abs(d) * 10 < range ? cur + (d > 0 ? 1 : -1) : to;
}

/* a flat listing's cursor is on a file: past a heading (or ..) to the next file the way it was going (else back) */
static int on_file(const lib_list_t *l, int k, int d) {
    if (!l || !l->flat || !l->nfiles) return k;
    for (int j = k; j >= 0 && j < l->n; j += d)
        if (l->e[j].kind == LE_FILE) return j;
    for (int j = k; j >= 0 && j < l->n; j -= d)
        if (l->e[j].kind == LE_FILE) return j;
    return k;
}

static void browse_act(perc_t *P, int k);

/* hear: a move of the cursor's own: the file it lands on plays (PREVIEW), or with AUTO LOAD, loads into LOAD TO's voice
 * (and plays through it, PREVIEW on) */
static void cursor_to(perc_t *P, int k, int hear) {
    const lib_list_t *l = P->bl;
    int n = l ? l->n : 0, was = cursor(P);
    k = k >= n ? n - 1 : k;
    k = k < 0 ? 0 : k;
    k = on_file(l, k, k < was ? -1 : 1);
    cursor_set(P, k);
    int s = (int)P->pv[P_BR_SCROLL];
    if (k < s) scroll_to(P, k);
    else if (k >= s + BROWSE_ROWS) scroll_to(P, k - BROWSE_ROWS + 1);
    mark_rows(P);
    if (hear && k != was) {
        if (l && k < n && l->e[k].kind == LE_FILE && P->pv[P_BR_AUTOLOAD] >= 0.5f) browse_act(P, k);
        else if (l && k < n && l->e[k].kind == LE_FILE) preview(P, k);
        else preview_stop(P);
    }
}

static int find_in(const lib_list_t *l, int g, const char *name) {   /* a file of folder g of the listing, by name */
    for (int k = l->g[g].first; name && k < l->g[g].first + l->g[g].n; k++)
        if (!strcmp(l->e[k].name, name)) return k;
    return -1;
}

static int group_of(const lib_list_t *l, const char *path) {   /* the listing's folder with that path, or -1 */
    for (int g = 0; path && g < l->ng; g++)
        if (!strcmp(l->g[g].path, path)) return g;
    return -1;
}

static int mine_at(const perc_t *P, const lib_list_t *l) {   /* the file LOAD TO's voice plays, if it's listed */
    int v = (int)P->pv[P_BR_TARGET], g = group_of(l, lib_path(P->sfold[v]));
    return g < 0 ? -1 : find_in(l, g, lib_name(P->sfold[v], (int)P->pv[VSX(v, P_V1_SMP)] - 1));
}

static int under(const char *path, const char *dir) {   /* path is dir or in it */
    size_t n = strlen(dir);
    return !strncmp(path, dir, n) && (path[n] == 0 || path[n] == '/');
}

/* a listing shown: the cursor on LOAD TO's file (bfind, or the one it plays if it's in this folder), on the folder just
 * left (UP), else on the first file; the view around it */
static void adopt(perc_t *P, lib_list_t *l) {
    int at = P->bfind[0] && l->ng ? find_in(l, 0, P->bfind) : -1;   /* LOAD TO picked: its file first */
    if (at < 0 && P->bl && l->path[0] && !strncmp(P->bl->path, l->path, strlen(l->path)) &&
        P->bl->path[strlen(l->path)] == '/') {   /* UP: the folder left, its row (or a flat listing's first file of it) */
        const char *left = P->bl->path + strlen(l->path) + 1;
        for (int k = 0; at < 0 && k < l->n; k++)
            if ((l->e[k].kind == LE_DIR && !strcmp(l->e[k].name, left)) ||
                (l->e[k].kind == LE_HEAD && under(l->e[k].path, P->bl->path)))
                at = k;
    }
    if (at < 0) at = mine_at(P, l);
    if (at < 0) at = l->n > 1 && l->e[0].kind == LE_UP ? 1 : 0;
    at = on_file(l, at, 1);
    P->bfind[0] = 0;
    preview_stop(P);
    lib_list_free(P->bl);
    P->bl = l;
    P->bwait = 0;
    cursor_set(P, at);
    scroll_to(P, at - BROWSE_ROWS / 2 + 1);
    mark_rows(P);
}

/* show this folder ("" = the places: read here and shown at once, a look at the data folder and /media; a folder is
 * read on the library's thread and shown when it's in, browse_adopt) */
static void browse(perc_t *P, const char *path) {
    if (!P->br && !(P->br = lib_browser())) return;
    if (path != P->bpath) snprintf(P->bpath, sizeof P->bpath, "%s", path);
    lib_browse(P->br, P->bpath);   /* (the places too: what was asked for last is what's shown) */
    P->bwait = 1;
    lib_list_t *l = P->bpath[0] ? NULL : lib_places();
    if (l) adopt(P, l);
}

static void browse_adopt(perc_t *P) {   /* called from every display and set, so a listing lands at once */
    if (!P->br) browse(P, P->bpath);   /* the tab's first look: where it was */
    lib_list_t *l = lib_listing(P->br);
    if (l) adopt(P, l);
}

static int place_top(const perc_t *P, const char *path) {   /* a drive's own folder, or the data folder */
    const char *sl = strrchr(path, '/');
    return !strcmp(path, P->dir) || !sl || sl == path ||
           ((size_t)(sl - path) == strlen(LIB_MEDIA) && !strncmp(path, LIB_MEDIA, strlen(LIB_MEDIA)));
}

static void browse_up(perc_t *P) {
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

static void audition(perc_t *P, int v) {   /* the voice plays its new file once it's in (PREVIEW on a load, AUDITION) */
    P->aud = v + 1;
    P->aud_n = 0;
}

/* entry k: open a folder or a place, go up, or load a file into LOAD TO's voice */
static void browse_act(perc_t *P, int k) {
    const lib_list_t *l = P->bl;
    int v = (int)P->pv[P_BR_TARGET];
    const lib_ent_t *e = &l->e[k];
    char p[LIB_PATH + 256];
    switch (e->kind) {
    case LE_UP: browse_up(P); break;
    case LE_PLACE: browse(P, e->path); break;
    case LE_DIR:
        snprintf(p, sizeof p, "%s/%s", l->path, e->name);
        if (strlen(p) < LIB_PATH) browse(P, p);
        break;
    case LE_HEAD: browse(P, e->path); break;   /* a flat listing's folder: that folder alone */
    default: {   /* a WAV file: LOAD TO's voice plays it (SOURCE = SAMPLE), its SAMPLE then steps through its folder */
        int f = lib_folder_set(l, e->grp);
        if (f < 0) return;   /* (too many folders in one session: LIB_FOLDERS) */
        set_folder(P, v, f);
        P->pv[VSX(v, P_V1_SMP)] = (float)(k - l->g[e->grp].first + 1);
        P->pv[VSX(v, P_V1_SRC)] = 1;
        mark(P, VSX(v, P_V1_SMP));
        mark(P, VSX(v, P_V1_SRC));
        for (int j = K_P1; j <= K_P2; j++) mark(P, VK(v, j));   /* their names: CRUSH, START */
        mark(P, P_V1_MODE_NAME + v);
        sample_ask(P, v);
        preview_stop(P);   /* now heard through the voice */
        if (P->pv[P_BR_PREVIEW] >= 0.5f) audition(P, v);
    }
    }
    mark_rows(P);   /* the loaded file's mark moved */
}

/* a tap on a row: a folder (or a place, UP) opens; a file gets the cursor and is heard, and a second tap loads it. A
 * row's value reads 1 while the cursor is on it, so MPC turns that row off on a tap: that's a tap too, unless it comes
 * straight after the tap that lit it (a host echoing it back) */
#define ECHO_FRAMES ((uint32_t)(0.1f * SR))
static void browse_tap(perc_t *P, int row, int on) {
    browse_adopt(P);
    const lib_list_t *l = P->bl;
    int k = (int)P->pv[P_BR_SCROLL] + row;
    if (!l || P->bwait || k >= l->n) return;
    if (!on && (k != cursor(P) || P->clock - P->btap_at < ECHO_FRAMES)) return;
    if (l->e[k].kind == LE_FILE && k != cursor(P)) {
        cursor_to(P, k, 1);
        P->btap_at = P->clock;
        return;
    }
    browse_act(P, k);
}

static void browse_load(perc_t *P) {   /* LOAD: the cursor's entry, as a second tap on its row */
    browse_adopt(P);
    if (P->bl && !P->bwait && cursor(P) < P->bl->n) browse_act(P, cursor(P));
}

/* LOAD TO picked: the cursor on the file that voice plays, the list going to its folder if it isn't listed (a voice
 * with no file: the list stays where it is) */
static void target_jump(perc_t *P) {
    int v = (int)P->pv[P_BR_TARGET];
    mark_rows(P);
    const char *fp = lib_path(P->sfold[v]), *nm = lib_name(P->sfold[v], (int)P->pv[VSX(v, P_V1_SMP)] - 1);
    if (!fp || !nm) return;
    if (P->bl && !P->bwait && group_of(P->bl, fp) >= 0) {
        int k = mine_at(P, P->bl);
        if (k >= 0) cursor_to(P, k, 0);
        return;
    }
    snprintf(P->bfind, sizeof P->bfind, "%s", nm);
    browse(P, fp);
}

static int is_mine(const perc_t *P, const lib_list_t *l, int k) {   /* the file LOAD TO's voice plays */
    int v = (int)P->pv[P_BR_TARGET];
    const char *fp = lib_path(P->sfold[v]), *nm = lib_name(P->sfold[v], (int)P->pv[VSX(v, P_V1_SMP)] - 1);
    const lib_ent_t *e = &l->e[k];   /* by name: the folder may have changed */
    return fp && nm && e->grp >= 0 && !strcmp(fp, l->g[e->grp].path) && !strcmp(nm, e->name);
}

/* a name in n characters (MPC shows 24): without ".wav", and when longer its start and end, ".." between (takes of one
 * sound often differ only at the end: "Kick Hard Room 04") */
static int fit(char *buf, int len, const char *s, int n, int strip_wav) {
    int l = (int)strlen(s);
    if (strip_wav && l > 4 && !strcasecmp(s + l - 4, ".wav")) l -= 4;
    if (l <= n) return snprintf(buf, len, "%.*s", l, s);
    int tail = (n - 2) / 2, head = n - 2 - tail;
    return snprintf(buf, len, "%.*s..%.*s", head, s, tail, s + l - tail);
}

static int entry_text(const perc_t *P, const lib_list_t *l, int k, char *buf, int len, int marks) {
    const lib_ent_t *e = &l->e[k];
    char t[32];
    switch (e->kind) {
    case LE_UP: return snprintf(buf, len, ".. UP");
    case LE_DIR:
    case LE_HEAD: fit(t, sizeof t, e->name, 22, 0); return snprintf(buf, len, "%s /", t);
    case LE_PLACE: fit(t, sizeof t, e->name, 24, 0); return snprintf(buf, len, "%s", t);
    default: {
        int mine = marks && is_mine(P, l, k);
        fit(t, sizeof t, e->name, mine ? 22 : 24, 1);
        return snprintf(buf, len, "%s%s", mine ? "> " : "", t);
    }
    }
}

static int browse_row(perc_t *P, int row, char *buf, int len) {
    browse_adopt(P);
    const lib_list_t *l = P->bl;
    int k = (int)P->pv[P_BR_SCROLL] + row;
    if (!l || k >= l->n) return snprintf(buf, len, "%s", !l && P->bwait && row == 0 ? "READING ..." : " ");
    return entry_text(P, l, k, buf, len, 1);
}

static int cursor_text(perc_t *P, char *buf, int len) {   /* FILE: the cursor's entry */
    browse_adopt(P);
    const lib_list_t *l = P->bl;
    if (!l || P->bwait) return snprintf(buf, len, "%s", P->bwait ? "READING ..." : "-");
    return cursor(P) < l->n ? entry_text(P, l, cursor(P), buf, len, 0) : snprintf(buf, len, "-");
}

/* where it is, its end if long (MPC shows 24 characters); in a flat listing, the folder of the cursor's file */
static int browse_path(perc_t *P, char *buf, int len) {
    browse_adopt(P);
    const char *p = P->bpath;
    const lib_list_t *l = P->bl;
    if (l && !P->bwait && l->flat && cursor(P) < l->n && l->e[cursor(P)].grp > 0) p = l->g[l->e[cursor(P)].grp].path;
    if (!p[0]) return snprintf(buf, len, "DRIVES");
    char t[LIB_PATH + 32];
    size_t dl = strlen(P->dir);
    if (dl && !strncmp(p, P->dir, dl) && (!p[dl] || p[dl] == '/')) snprintf(t, sizeof t, "PERCOLATOR%s", p + dl);
    else if (!strncmp(p, LIB_MEDIA "/", strlen(LIB_MEDIA) + 1)) snprintf(t, sizeof t, "%s", p + strlen(LIB_MEDIA) + 1);
    else snprintf(t, sizeof t, "%s", p);
    if (!strncmp(t, "az01-internal", 13)) {
        memmove(t + 8, t + 13, strlen(t + 13) + 1);
        memcpy(t, "INTERNAL", 8);
    }
    size_t n = strlen(t);
    return n > 23 ? snprintf(buf, len, "..%s", t + n - 21) : snprintf(buf, len, "%s", t);
}

static int browse_status(perc_t *P, char *buf, int len) {
    browse_adopt(P);
    const lib_list_t *l = P->bl;
    if (P->bwait || !l) return snprintf(buf, len, "%s", P->bwait ? "READING ..." : " ");
    if (l->missing) return snprintf(buf, len, "CAN'T OPEN IT");
    if (!l->path[0]) return snprintf(buf, len, "PICK A DRIVE");
    int dirs = l->flat ? 0 : l->first_file - 1, nf = l->nfiles;
    if (l->flat && l->ng > 1) {   /* the folders below too */
        for (int g = 0; g < l->ng; g++) dirs += l->g[g].n > 0;
        return snprintf(buf, len, "%d WAV%s IN %d FOLDER%s", nf, nf == 1 ? "" : "S", dirs, dirs == 1 ? "" : "S");
    }
    if (dirs) /* too many for one list: folder by folder */
        return nf ? snprintf(buf, len, "%d FOLDER%s + %d WAV%s", dirs, dirs == 1 ? "" : "S", nf, nf == 1 ? "" : "S")
                  : snprintf(buf, len, "%d FOLDER%s", dirs, dirs == 1 ? "" : "S");
    if (nf) return snprintf(buf, len, "%d WAV%s%s", nf, nf == 1 ? "" : "S", nf == LIB_FILES ? " (MAX)" : "");
    return snprintf(buf, len, "NO WAV FILES");
}

/* a voice's SAMPLE, as text: its file's name (no .wav; a long one, its start and end) */
static int sample_display(const perc_t *P, int v, char *buf, int len) {
    int iv = (int)P->pv[VSX(v, P_V1_SMP)], f = P->sfold[v];
    if (f >= 0 && lib_files(f) < 0) return snprintf(buf, len, "READING");
    if (f < 0 || iv <= 0) return snprintf(buf, len, "NO SAMPLE");
    const char *nm = lib_name(f, iv - 1);
    return nm ? fit(buf, len, nm, 24, 1) : snprintf(buf, len, "%d (NONE)", iv);
}

/* a saved project's file, once its folder is read: SAMPLE on it (if it's gone, SAMPLE stays at the number saved) */
static void find_wanted(perc_t *P, int v) {
    int n = lib_files(P->sfold[v]);
    for (int k = 0; k < n; k++)
        if (!strcmp(lib_name(P->sfold[v], k), P->swant[v])) {
            P->pv[VSX(v, P_V1_SMP)] = (float)(k + 1);
            mark(P, VSX(v, P_V1_SMP));
            sample_ask(P, v);
            break;
        }
    P->swant[v][0] = 0;
}

/* each block: a voice's new file once it's in (a second at most), as a quarter-second tap of its pad (PREVIEW on a
 * load, AUDITION); the cursor's file, once it's in (PREVIEW) */
static void preview_poll(perc_t *P, int frames) {
    if (P->aud) {
        int v = P->aud - 1, n = (int)P->pv[VSX(v, P_V1_SMP)], ready = 0;
        const sample_t *s;
        if (n > 0 && P->sfold[v] >= 0) {
            int sl = lib_get(P->sfold[v], n - 1, &s);
            ready = sl >= 0;
            lib_put(sl);
        }
        if (ready) {
            vparam_t vp;
            vparams(P, v, &vp);
            voice_trigger(P, v, &vp, 100.0f, (int)P->pv[VIO(v, P_V1_NOTE)], 0.0f);
            P->v[v].ch = 0;
            P->v[v].pitched = 0;
            P->aud_v = v;
            P->aud_off = (int)SR / 4;
            P->aud = 0;
        } else if ((P->aud_n += frames) > (int)SR || n <= 0) {
            P->aud = 0;
        }
    }
    if (P->aud_off > 0 && (P->aud_off -= frames) <= 0) voice_release(P, P->aud_v);
    if (P->prv_want) {
        int sl = lib_get(P->prv_f, P->prv_k, &P->prv_s);
        if (sl >= 0) {
            P->prv_slot = sl;
            P->prv_pos = 0.0;
            P->prv_want = 0;
        } else if ((P->prv_n += frames) > (int)SR) {
            P->prv_want = 0;
        }
    }
}

/* ---- RANDOMISE ------------------------------------------------------------------------------------------------- */
/* Owner, 2026-10-19: "a random button to randomise each parameter per voice like we did with incipience". A voice's
 * sound, each control in a range that keeps it a drum of its kind: ALGO, MODE, TUNE, DECAY (never a drone), PARAM 1-2
 * (by the algorithm), VCF and CUTOFF (by where the sound is, from TUNE, the algorithm and its mode: a low-pass an
 * octave or more above it, a band-pass on it (not on voice 4), a high-pass below it; a sample, whose sound isn't known,
 * by fixed ranges and mostly low-passed), DRIVE; on SOURCE = SAMPLE a file of its folder, its pitch, start and crush
 * instead of the algorithm's. Left as they are: LEVEL, FX SEND, COMP SEND, NOTE, KEYS CH, OUT, the lanes, the LFO
 * depths, SOURCE. Every roll is heard (mpc/selftest.c, 20,000 a voice: each peaks over -36 dBFS, all but about 1 in
 * 10,000 over -30; the built-in kits' softest voice peaks at -27). */
typedef struct { signed char lo, hi; } rr_t;
static const rr_t R_TUNE[NV + 1] = {{20, 62}, {15, 80}, {15, 85}, {20, 80}, {30, 70}};   /* the last: a sample */
static const rr_t R_TUNE_V4SMP = {25, 65};   /* voice 4's SAMPLE: +-2 octaves at most (a hat far up is a tick) */
static const rr_t R_DECAY[NV + 1] = {{10, 60}, {5, 55}, {5, 55}, {12, 50}, {35, 90}};
static const rr_t R_P[NV][3][2] = {   /* PARAM 1, PARAM 2 by voice and algorithm */
    {{{0, 80}, {0, 70}}, {{0, 100}, {0, 70}}, {{0, 80}, {0, 80}}},
    {{{0, 80}, {0, 70}}, {{0, 100}, {0, 70}}, {{0, 80}, {0, 70}}},
    {{{0, 100}, {0, 80}}, {{0, 70}, {0, 100}}, {{0, 70}, {0, 100}}},
    {{{0, 60}, {0, 25}}, {{0, 60}, {0, 25}}, {{0, 60}, {0, 25}}},
};
static const rr_t R_CUT_SMP[3] = {{0, 25}, {35, 60}, {55, 100}};   /* a sample's CUTOFF by VCF: HP, BP, LP */
/* % HP, % BP (the rest LP); voice 4's sounds are high-passed noise and partials, which a band-pass leaves thin */
static const unsigned char R_VCF[NV + 1][2] = {{10, 15}, {15, 25}, {25, 25}, {55, 0}, {15, 10}};

static int rnd_in(perc_t *P, int lo, int hi) {
    uint32_t x = P->rrng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    P->rrng = x;
    return lo + (int)(x % (uint32_t)(hi - lo + 1));
}
static int rnd_r(perc_t *P, rr_t r) { return rnd_in(P, r.lo, r.hi); }

static void rand_set(perc_t *P, int i, int x) {   /* MPC is told (mpc_engine_changed) */
    if (P->pv[i] == (float)x) return;
    P->pv[i] = (float)x;
    mark(P, i);
}

static int cut_knob(float hz) {   /* CUTOFF's value for a frequency (20 Hz .. 20 kHz) */
    int c = (int)lrintf(100.0f * log2f(hz / 20.0f) / 9.9658f);
    return c < 0 ? 0 : c > 100 ? 100 : c;
}

/* where a voice's sound is, in Hz (voices.c): its pitch, and on voices 1-2 half the sweep of PARAM 2's pitch envelope
 * (a short hit is mostly sweep); WAVE's FORMANT table, the harmonic PARAM 1 lifts (up to the 17th); SLAP, its noise
 * band; voice 4, its noise's or partials' band, the built-in hats' at their pitch */
static float rand_centre(const perc_t *P, int v, int algo, int mode) {
    float t = P->pv[VK(v, K_TUNE)] / 100.0f;
    if (v == 3) {   /* (BELL high-passes itself at 400 Hz: its upper partials) */
        static const float F4[3][3] = {{4000.0f, 7000.0f, 4000.0f}, {4000.0f, 1000.0f, 4000.0f},
                                       {8000.0f, 8000.0f, 4000.0f}};
        float rt = fexp2((t - 0.5f) * 3.0f);
        if (algo == 2) return F4[2][mode] * sample_ratio(t);
        return F4[algo][mode] * (algo == 1 && mode == 1 ? rt : sqrtf(rt));
    }
    if (v == 2 && algo == 1) return 400.0f * fexp2(4.0f * t);
    float f = voice_freq(v, t);
    float oct = v == 0 && algo != 2 ? 6.0f : v == 0 ? 5.0f : 4.0f;   /* PARAM 2's sweep at its top (voices.c) */
    if (v < 2) f *= fexp2(0.5f * oct * powf(P->pv[VK(v, K_P2)] / 100.0f, 1.3f));
    return v < 2 && algo == 1 && mode == 1 ? f * (1.5f + 15.4f * P->pv[VK(v, K_P1)] / 100.0f) : f;
}

static int rand_cutoff(perc_t *P, int v, int src, int vcf) {
    int algo = (int)P->pv[VALGO(v)], mode = (int)P->pv[VMODE(v)] % 3;
    int pack = v == 3 && algo == 2 && slot_sample(P, P->bank, mode) != &perc_builtin[mode];   /* a kit pack's sample */
    if (src || pack) return rnd_r(P, R_CUT_SMP[vcf]);
    float f = rand_centre(P, v, algo, mode);
    return vcf == 2 ? rnd_in(P, cut_knob(2.0f * f), 100) : vcf == 1 ? rnd_in(P, cut_knob(0.7f * f), cut_knob(4.0f * f))
                    : rnd_in(P, 0, cut_knob(0.7f * f));
}

static void randomise(perc_t *P, int v) {
    int src = P->pv[VSX(v, P_V1_SRC)] >= 0.5f, row = src ? NV : v;
    if (!src) {
        rand_set(P, VALGO(v), rnd_in(P, 0, 2));
        rand_set(P, VMODE(v), rnd_in(P, 0, 2));
    }
    int algo = (int)P->pv[VALGO(v)], r = rnd_in(P, 0, 99);
    rand_set(P, VK(v, K_TUNE), rnd_r(P, v == 3 && !src && algo == 2 ? R_TUNE_V4SMP : R_TUNE[row]));
    rand_set(P, VK(v, K_DECAY), rnd_r(P, R_DECAY[row]));
    if (src) {   /* a file of its folder; a little crush now and then; the start near the top, or any slice */
        int nf = lib_files(P->sfold[v]);
        if (nf > 0) rand_set(P, VSX(v, P_V1_SMP), rnd_in(P, 1, nf));
        sample_ask(P, v);
        rand_set(P, VK(v, K_P1), rnd_in(P, 0, 99) < 50 ? 0 : rnd_in(P, 0, 60));
        rand_set(P, VK(v, K_P2), own_slices(P, v) ? rnd_in(P, 0, 100) : rnd_in(P, 0, 99) < 60 ? 0 : rnd_in(P, 0, 30));
    } else {
        rand_set(P, VK(v, K_P1), rnd_r(P, R_P[v][algo][0]));
        /* voice 4's SAMPLE with a kit pack's sliced sample: any slice */
        int sliced = v == 3 && algo == 2 && sample_slices(P, (int)P->pv[VMODE(v)]);
        rand_set(P, VK(v, K_P2), sliced ? rnd_in(P, 0, 100) : rnd_r(P, R_P[v][algo][1]));
    }
    int vcf = r < R_VCF[row][0] ? 0 : r < R_VCF[row][0] + R_VCF[row][1] ? 1 : 2;
    rand_set(P, VVCF(v), vcf);
    rand_set(P, VK(v, K_CUTOFF), rand_cutoff(P, v, src, vcf));
    rand_set(P, VK(v, K_DRIVE), rnd_in(P, 0, 99) < 30 ? 0 : rnd_in(P, 0, 60));
    for (int j = K_P1; j <= K_P2; j++) mark(P, VK(v, j));   /* their names follow the algorithm */
    mark(P, P_V1_MODE_NAME + v);
}

/* ---- the master section ------------------------------------------------------------------------------------- */

static float bbd_seconds(const perc_t *P) {
    float t = P->pv[P_FX_TIME] / 100.0f;
    return P->pv[P_FX_RANGE] >= 1 ? 0.06f * powf(20.0f, t) : 0.008f * powf(25.0f, t);   /* 60 ms-1.2 s / 8-200 ms */
}
static float bbd_rate(const perc_t *P) { return 0.05f * powf(200.0f, P->pv[P_FX_RATE] / 100.0f); }   /* .05-10 Hz */

static inline float hermite(const float *b, float pos) {
    int i = (int)pos;
    float f = pos - i;
    float xm = b[(i - 1) & (DL_N - 1)], x0 = b[i & (DL_N - 1)], x1 = b[(i + 1) & (DL_N - 1)], x2 = b[(i + 2) & (DL_N - 1)];
    float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

/* ---- OUT 1-4 (tapbus.h: how they work) ------------------------------------------------------------------------ */
static uint32_t next_id = 1;

static int tap_claim(perc_t *P) {   /* this instance writes OUT 1-4: it already does, or nobody did */
    tapbus_t *b = P->bus;
    if (!b) return 0;
    uint32_t o = TB_LOAD(b->owner), none = 0;
    if (o == P->id) return 1;
    if (o || !__atomic_compare_exchange_n(&b->owner, &none, P->id, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    __atomic_add_fetch(&b->gen, 1, __ATOMIC_ACQ_REL);
    return 1;
}

static void tap_release(perc_t *P) {
    tapbus_t *b = P->bus;
    uint32_t me = P->id;
    if (b && TB_LOAD(b->owner) == me && __atomic_compare_exchange_n(&b->owner, &me, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        __atomic_add_fetch(&b->gen, 1, __ATOMIC_ACQ_REL);
}

static inline int16_t out_sample(float x) {   /* a soft ceiling instead of a hard clip */
    float ax = fabsf(x);
    if (ax > 0.9f) x = copysignf(0.9f + 0.1f * sat((ax - 0.9f) * 10.0f), x);
    int s = (int)lrintf(x * 32767.0f);
    return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
}

/* The compressor works on the COMP SEND bus: each voice sends part of itself there (V<n> COMP SEND, wet/dry) and the
 * rest goes past it, as do the delay's echoes. Threshold 0 to -36 dB (THRESH), ratio 1:1 to 10:1 (AMOUNT), and its
 * reduction made back against a -3 dB peak: turned up, it only ever makes the bus louder (measured on the built-in kits:
 * +0.3 to +11 dB) and a peak stays under -3 dB. The detector catches a peak at once (a drum's first sample would
 * otherwise get the whole make-up gain and clip) and lets go over 120 ms. COMP LEVEL sets what comes out of it (71 =
 * as it is). AMOUNT at 0 is off: the bus passes untouched, COMP LEVEL too, the same as no compressor. */
typedef struct {
    int on;
    float thr, expo, makeup, rel;   /* makeup includes COMP LEVEL */
} comp_t;

static float level_gain(float knob) {   /* VOLUME and COMP LEVEL: 0..100 -> silent .. +6 dB, 71 = 0 dB */
    float x = knob / 100.0f;
    return 2.0f * x * x;
}

static void comp_setup(const perc_t *P, comp_t *c) {
    float amt = P->pv[P_COMP_AMT] / 100.0f, thr_db = -36.0f * P->pv[P_COMP_THRESH] / 100.0f, ratio = 1.0f + 9.0f * amt;
    c->on = amt > 0.0001f;
    c->thr = powf(10.0f, thr_db / 20.0f);
    c->expo = 1.0f / ratio - 1.0f;
    c->makeup = powf(10.0f, fmaxf(-3.0f - thr_db, 0.0f) * (1.0f - 1.0f / ratio) / 20.0f) * level_gain(P->pv[P_COMP_LEVEL]);
    c->rel = op_coef(1.3f);
}

static inline float comp_run(float *env, const comp_t *c, float x) {
    float a = fabsf(x);
    *env = a > *env ? a : *env + c->rel * (a - *env);
    return *env > c->thr ? x * powf(*env / c->thr, c->expo) * c->makeup : x * c->makeup;
}

/* TRACK PLAYS = OUT n: the main Percolator's OUT n, through this instance's own compressor and VOLUME */
static void tap_render(perc_t *P, int o, int16_t *out, int frames) {
    tap_release(P);
    for (int v = 0; v < NV; v++) voice_stop(P, v);   /* its own voices stay quiet */
    float y[TAP_N / 4];
    if (!tapbus_read(P->bus, &P->tr, o, y, frames)) {
        memset(out, 0, sizeof(int16_t) * 2 * frames);
        return;
    }
    comp_t cmp;
    comp_setup(P, &cmp);
    float vol = level_gain(P->pv[P_VOLUME]);
    for (int i = 0; i < frames; i++) {
        float x = y[i] * 0.55f;
        if (cmp.on) x = comp_run(&P->comp_env, &cmp, x);
        out[2 * i] = out[2 * i + 1] = out_sample(x * vol);
    }
}

static void e_render(void *inst, int16_t *out, int frames) {
    perc_t *P = inst;
    P->clock += (uint32_t)frames;
    apply_kit(P);
    uint8_t m[3];   /* what MIDI tracks sent to Percolator MIDI In since the last block, channels and all */
    for (int k = 0, n; k < 256 && (n = mi_read(&P->midi_in, m)) > 0; k++) e_midi(P, m, n);
    lib_poke();
    for (int v = 0; v < NV; v++)
        if (P->swant[v][0] && lib_files(P->sfold[v]) >= 0) find_wanted(P, v);
    int tap = (int)P->pv[P_OUTPUT];
    if (!tap) preview_poll(P, frames);
    if (tap) {
        tap_render(P, tap - 1, out, frames);
        return;
    }
    fpstate_t fp = ftz_on();
    /* OUT 1-4: this one writes them if it is the main one, and leaves out the voices something else plays */
    tapbus_t *bus = P->bus;
    int own = tap_claim(P), tapped[TAP_OUTS] = {0}, vout[NV], hold = 0;
    uint32_t w0 = own ? TB_LOAD(bus->wpos) : 0, g = own ? TB_LOAD(bus->gen) : 0, mask[TAP_OUTS] = {0};
    for (int o = 0; o < TAP_OUTS && own; o++) {
        tapped[o] = TB_LOAD(bus->seen_gen[o]) == g && w0 - TB_LOAD(bus->seen[o]) < TAP_ALIVE;
        hold |= tapped[o];
    }
    for (int v = 0; v < NV; v++) {
        vout[v] = (int)P->pv[VIO(v, P_V1_OUT)] - 1;   /* -1: MAIN */
        if (vout[v] >= 0) mask[vout[v]] |= 1u << v;
    }
    for (int o = 0; o < TAP_OUTS && own; o++) TB_STORE(bus->voices[o], mask[o]);
    float target = bbd_seconds(P) * SR, rate = bbd_rate(P) * INV_SR;
    float dep = P->pv[P_FX_DEPTH] / 100.0f;
    float depth = dep * dep * fminf(0.012f, 0.5f * bbd_seconds(P)) * SR;
    float fb = 1.08f * P->pv[P_FX_FEEDBACK] / 100.0f;
    float ca = op_coef(P->pv[P_FX_COLOUR] >= 1 ? 7000.0f : 2200.0f), ch = op_coef(40.0f);
    float sm = 1.0f - expf(-1.0f / (0.12f * SR));
    comp_t cmp;
    comp_setup(P, &cmp);
    float vol = level_gain(P->pv[P_VOLUME]);
    for (int off = 0; off < frames; off += SUB) {
        int n = frames - off < SUB ? frames - off : SUB;
        float mix[SUB] = {0}, cbus[SUB] = {0}, send[SUB] = {0}, prv[SUB] = {0};
        lfo_step(P, n);
        if (P->prv_slot >= 0) preview_block(P, prv, n);   /* the cursor's file (PREVIEW) */
        for (int o = 0; o < TAP_OUTS; o++)   /* an OUT is written only while something reads it */
            if (tapped[o])
                for (int i = 0; i < n; i++) bus->ring[o][(w0 + off + i) & (TAP_N - 1)] = 0;
        for (int v = 0; v < NV; v++) {
            vparam_t vp;
            float y[SUB] = {0}, cs = P->pv[VCOMP(v)] / 100.0f;
            int live = P->v[v].active || P->v[v].room.tail > 0;   /* silent: nothing to mix */
            vparams(P, v, &vp);
            voice_block(P, v, &vp, y, send, n);
            if (!live) continue;
            if (vout[v] >= 0 && tapped[vout[v]])
                for (int i = 0; i < n; i++) bus->ring[vout[v]][(w0 + off + i) & (TAP_N - 1)] += y[i];
            if (vout[v] < 0 || !tapped[vout[v]])
                for (int i = 0; i < n; i++) {
                    mix[i] += y[i] * (1.0f - cs);
                    cbus[i] += y[i] * cs;
                }
        }
        for (int i = 0; i < n; i++) {
            /* BBD-style delay: band-limited in and in the loop, soft-clipped feedback, LFO on the time, and a
             * time knob that glides (the pitch bends as the clock would) */
            P->dl_t += (target - P->dl_t) * sm;
            P->dl_ph += rate;
            if (P->dl_ph >= 1.0f) P->dl_ph -= 1.0f;
            float d = clampf(P->dl_t + depth * sin_t(P->dl_ph), 2.0f, DL_N - 4.0f);
            float y = hermite(P->dl, (float)P->dl_w - d + DL_N);
            P->dl_lp1 += ca * (y - P->dl_lp1);
            P->dl_lp2 += ca * (P->dl_lp1 - P->dl_lp2);
            P->dl_hp += ch * (P->dl_lp2 - P->dl_hp);
            float wet = P->dl_lp2 - P->dl_hp;
            P->dl_in1 += ca * (send[i] - P->dl_in1);
            P->dl_in2 += ca * (P->dl_in1 - P->dl_in2);
            P->dl[P->dl_w] = sat(P->dl_in2 + fb * wet);
            P->dl_w = (P->dl_w + 1) & (DL_N - 1);
            float c = cbus[i] * 0.55f;
            if (cmp.on) c = comp_run(&P->comp_env, &cmp, c);
            float x = (mix[i] * 0.55f + wet * 0.55f + c + prv[i]) * vol;
            P->hold[P->hold_w & (TAP_HOLD - 1)] = x;
            if (hold) x = P->hold[(P->hold_w - (uint32_t)frames) & (TAP_HOLD - 1)];   /* a block late, with the taps */
            P->hold_w++;
            out[2 * (off + i)] = out[2 * (off + i) + 1] = out_sample(x);
        }
    }
    if (own) TB_STORE(bus->wpos, w0 + (uint32_t)frames);
    ftz_off(fp);
}

/* ---- parameters ---------------------------------------------------------------------------------------------- */

static void set_value(perc_t *P, int i, const char *val) {
    const pdef_t *d = &PDEF[i];
    if (i == P_KEYS) {   /* the one KEYS switch up to 1.2.0 (a project saved then): that voice's KEYS CH = ALL */
        int k = atoi(val);
        if (k >= 1 && k <= NV) P->pv[VIO(k - 1, P_V1_KCH)] = 1;
        P->pv[P_KEYS] = 0;
        return;
    }
    if (d->kind == 0) P->pv[i] = clampf((float)atof(val), 0.0f, 100.0f);
    else if (d->kind == 1 || d->kind == 4) {
        int x = d->kind == 4 ? (int)lrintf((float)atof(val)) : atoi(val);
        P->pv[i] = (float)(x < 0 ? 0 : x >= d->nopts ? d->nopts - 1 : x);
    }
}

/* A project's copy (get_state below): every control, then each voice's folder and file by name (since 2.1.0; a
 * state from before has neither, and its voices play their algorithms), and the folder the SAMPLES tab shows. */
static void set_state(perc_t *P, const char *s) {
    char kv[LIB_PATH * 3 + 64];
    for (int i = P_V1_SRC; i < NP; i++) P->pv[i] = PDEF[i].def;   /* what a state from before 2.1.0 doesn't name */
    for (int v = 0; v < NV; v++) set_folder(P, v, -1);
    P->bpath[0] = 0;
    while (*s) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                int i = find_key(kv), v = -1;
                if (kv[0] == 'v' && kv[1] >= '1' && kv[1] <= '4' && (!strcmp(kv + 2, "_dir") ||
                                                                       !strcmp(kv + 2, "_file")))
                    v = kv[1] - '1';
                if (v >= 0) {   /* a voice's own folder and file */
                    unesc(eq);
                    if (kv[3] == 'd') set_folder(P, v, lib_folder(eq));
                    else snprintf(P->swant[v], sizeof P->swant[v], "%s", eq);
                } else if (!strcmp(kv, "br_path")) {
                    unesc(eq);
                    snprintf(P->bpath, sizeof P->bpath, "%s", eq);
                } else if (!strcmp(kv, "bank")) {
                    P->bank = -1;
                    for (int b = 0; b < P->nbanks; b++)
                        if (!strcmp(P->banks[b].name, eq)) P->bank = b;
                } else if (i == P_KIT) P->pv[P_KIT] = (float)atoi(eq);
                else if (i >= 0) set_value(P, i, eq);
            }
        }
        s += n;
        if (*s) s++;
    }
}

static int get_state(const perc_t *P, char *buf, int len) {
    int o = snprintf(buf, len, "kit=%d;bank=%s;", (int)P->pv[P_KIT], P->bank >= 0 ? P->banks[P->bank].name : "");
    for (int i = 0; i < NP && o < len; i++)
        if (PDEF[i].kind == 0) o += snprintf(buf + o, len - o, "%s=%.2f;", PDEF[i].key, P->pv[i]);
        else if (PDEF[i].kind == 1 || PDEF[i].kind == 4)
            o += snprintf(buf + o, len - o, "%s=%d;", PDEF[i].key, (int)P->pv[i]);
    if (o >= len) return -1;
    /* the wrapper's 8 KB: every control takes about 2.6 KB, a folder that doesn't fit after that isn't kept */
    char d[LIB_PATH * 3 + 16], f[256 * 3 + 16];
    for (int v = 0; v < NV; v++) {
        const char *path = lib_path(P->sfold[v]), *name = lib_name(P->sfold[v], (int)P->pv[VSX(v, P_V1_SMP)] - 1);
        if (!path) continue;
        if (!name) name = P->swant[v];   /* not read yet: the file it was asked for */
        int a = put_esc(d, sizeof d, path), b = put_esc(f, sizeof f, name);
        if (a < 0 || b < 0 || o + a + b + 20 >= len) continue;
        o += snprintf(buf + o, len - o, "v%d_dir=%s;v%d_file=%s;", v + 1, d, v + 1, f);
    }
    if (P->bpath[0] && put_esc(d, sizeof d, P->bpath) > 0 && o + (int)strlen(d) + 10 < len)
        o += snprintf(buf + o, len - o, "br_path=%s;", d);
    return o < len ? o : -1;
}

static void e_set_param(void *inst, const char *key, const char *val) {
    perc_t *P = inst;
    TRACE("set %s = %.48s%s (%d chars)", key, val, strlen(val) > 48 ? "..." : "", (int)strlen(val));
    if (!strcmp(key, "state")) {
        P->kit_pending = -1;   /* a restore says which kit it is, and every knob */
        preview_stop(P);
        set_state(P, val);
        for (int v = 0; v < NV; v++) sample_ask(P, v);
        if (P->br) browse(P, P->bpath);
        return;
    }
    int i = find_key(key);
    if (i < 0) return;
    if (i == P_KIT) {   /* loaded at the next block (apply_kit); setting the kit already loaded changes nothing */
        int n = atoi(val);
        P->kit_pending = n < 0 ? 0 : n >= P->nkits ? P->nkits - 1 : n;
        return;
    }
    apply_kit(P);   /* a kit first, then this on top of it, in the order they came */
    if (i >= P_BR_ROW_1 && i < P_BR_ROW_1 + BROWSE_ROWS) {   /* a row's tap: 1, or 0 on the lit row (browse_tap) */
        browse_tap(P, i - P_BR_ROW_1, atof(val) >= 0.5);
        return;
    }
    if (PDEF[i].kind == 3) {   /* taps: RANDOMISE and the SAMPLES tab's (each sends 1, its release 0) */
        if (atof(val) < 0.5) return;
        if (i == P_BR_UP) browse_up(P);
        else if (i == P_BR_PLACES) browse(P, "");
        else if (i == P_BR_LOAD) browse_load(P);
        for (int v = 0; v < NV; v++)
            if (i == VSX(v, P_V1_RAND)) randomise(P, v);
        return;
    }
    if (i == P_BR_CURSOR) {   /* FILE: the data wheel on its box, or its arrows, an entry a click */
        browse_adopt(P);
        cursor_to(P, nudge(cursor(P), (float)atof(val), PDEF[i].nopts - 1), 1);
        mark(P, i);   /* MPC reads back where it is */
        return;
    }
    if (i == P_BR_CURSOR_Q) {   /* FILE's Q-Link (BROWSE): a row per QL_TICKS events */
        browse_adopt(P);
        int k = (int)lrintf((float)atof(val)), c = cursor(P);
        if (ql_tick(P, 0, k > c ? 1 : k < c ? -1 : 0)) cursor_to(P, c + (k > c ? 1 : -1), 1);
        else cursor_set(P, c);   /* MPC reads back where it is */
        return;
    }
    if (i == P_BR_SCROLL) {
        /* a page at a time: an arrow's (a whole number, a page away or at an end) at once, a Q-Link's or the wheel's
         * (between whole numbers) per QL_TICKS events; the cursor stays in view (silently) */
        browse_adopt(P);
        float x = (float)atof(val);
        int top = (int)P->pv[P_BR_SCROLL], d = x > top ? 1 : x < top ? -1 : 0;
        mark(P, P_BR_SCROLL);   /* MPC reads back where it is */
        if (fabsf(x - roundf(x)) < 1e-3f) scroll_to(P, (int)lrintf(x));
        else if (ql_tick(P, 1, d)) scroll_to(P, top + d * BROWSE_ROWS);
        int s0 = (int)P->pv[P_BR_SCROLL], k = cursor(P), last = s0 + BROWSE_ROWS - 1;
        if (k < s0 || k > last) cursor_to(P, k < s0 ? s0 : last, 0);
        return;
    }
    int sv = -1;
    for (int v = 0; v < NV; v++)
        if (i == VSX(v, P_V1_SMP)) sv = v;
    if (sv >= 0) {   /* SAMPLE: a file a click (nudge), no further than its folder's (none without one) */
        int was = (int)P->pv[i], n = nudge(was, (float)atof(val), PDEF[i].nopts - 1), top = (int)sample_top(P, sv);
        if (n > top && (P->sfold[sv] < 0 || lib_files(P->sfold[sv]) >= 0)) n = top;
        P->pv[i] = (float)(n < 0 ? 0 : n >= PDEF[i].nopts ? PDEF[i].nopts - 1 : n);
        mark(P, i);   /* MPC reads back where it is */
        /* AUDITION: turning SAMPLE plays it (on SOURCE = SAMPLE: on SYNTH the file isn't heard) */
        if (n != was && P->pv[P_BR_AUDITION] >= 0.5f && P->pv[VSX(sv, P_V1_SRC)] >= 0.5f) audition(P, sv);
    } else {
        set_value(P, i, val);
    }
    for (int v = 0; v < NV; v++)
        if (i == VSX(v, P_V1_SMP) || i == VSX(v, P_V1_SRC)) {   /* read now; PARAM 1-2's names follow */
            sample_ask(P, v);
            for (int j = K_P1; j <= K_P2; j++) mark(P, VK(v, j));
            mark(P, P_V1_MODE_NAME + v);
        }
    if (i == P_BR_TARGET) target_jump(P);
}

static int fmt_hz(char *buf, int len, float hz) {
    return hz < 1000.0f ? snprintf(buf, len, "%.0f Hz", hz) : snprintf(buf, len, "%.2f kHz", hz / 1000.0f);
}
static int fmt_s(char *buf, int len, float s) {
    return s < 1.0f ? snprintf(buf, len, "%.0f ms", s * 1000.0f) : snprintf(buf, len, "%.2f s", s);
}

static int own_sample(const perc_t *P, int v) { return P->pv[VSX(v, P_V1_SRC)] >= 0.5f; }

/* PARAM 1 / PARAM 2's names: the algorithm's, or on SOURCE = SAMPLE, as voice 4's SAMPLE */
static const char *p_name(const perc_t *P, int v, int which) {
    if (own_sample(P, v)) return which == 1 ? "CRUSH" : own_slices(P, v) ? "SLICE" : "START";
    return algo_p_name(v, (int)P->pv[VALGO(v)], (int)P->pv[VMODE(v)], which, P);
}

/* what PARAM 1 / PARAM 2 read as, in their own units where they have one */
static int p_display(const perc_t *P, int v, int which, char *buf, int len) {
    int algo = (int)P->pv[VALGO(v)], mode = (int)P->pv[VMODE(v)];
    float x = P->pv[VK(v, which == 1 ? K_P1 : K_P2)] / 100.0f;
    const char *n = p_name(P, v, which);
    if (!strcmp(n, "PITCH ENV")) return snprintf(buf, len, "%.1f oct", (v == 0 && algo != 2 ? 6.0f : v == 0 ? 5.0f : 4.0f) * powf(x, 1.3f));
    if (!strcmp(n, "ENV TIME")) return fmt_s(buf, len, 0.003f * powf(100.0f, x));
    if (!strcmp(n, "ATTACK")) return fmt_s(buf, len, v4_attack(x));
    if (!strcmp(n, "NOISE DECAY")) return fmt_s(buf, len, 0.04f * powf(30.0f, x));
    if (!strcmp(n, "NOISE TONE")) return fmt_hz(buf, len, 1200.0f * powf(12.0f, x));
    if (!strcmp(n, "SLICE")) {
        int ns = own_sample(P, v) ? own_slices(P, v) : sample_slices(P, mode % 3), q = (int)(x * ns);
        return snprintf(buf, len, "%d / %d", (q >= ns ? ns - 1 : q) + 1, ns);
    }
    return snprintf(buf, len, "%.0f", x * 100.0f);
}

static int e_get_param(void *inst, const char *key, char *buf, int len) {
    perc_t *P = inst;
    if (!strcmp(key, "state")) {
        apply_kit(P);
        return get_state(P, buf, len);
    }
    if (!strcmp(key, "kit_count")) return snprintf(buf, len, "%d", P->nkits);
    if (!strcmp(key, "br_path")) return browse_path(P, buf, len);
    if (!strcmp(key, "br_status")) return browse_status(P, buf, len);
    int cur_kit = P->kit_pending >= 0 ? P->kit_pending : (int)P->pv[P_KIT];   /* as set, loaded or not */
    if (!strcmp(key, "kit")) return snprintf(buf, len, "%d", cur_kit);
    if (!strcmp(key, "kit_name") || !strncmp(key, "kit_name_at:", 12)) {
        int n = key[8] ? atoi(key + 12) : cur_kit;
        return n >= 0 && n < P->nkits ? snprintf(buf, len, "%s", P->kits[n].name) : 0;
    }
    if (key[0] == 'v' && key[1] >= '1' && key[1] <= '4' && !strcmp(key + 2, "_mode_name")) {   /* the readout under MODE */
        char k2[24];
        snprintf(k2, sizeof k2, "v%c_mode_display", key[1]);
        return e_get_param(inst, k2, buf, len);
    }
    int i = find_key(key);
    if (i >= 0) {
        if (i >= P_BR_ROW_1 && i < P_BR_ROW_1 + BROWSE_ROWS)   /* lit: the cursor's row */
            return snprintf(buf, len, "%d", P->bl && !P->bwait && (int)P->pv[P_BR_SCROLL] + i - P_BR_ROW_1 == cursor(P));
        if (PDEF[i].kind == 0) return snprintf(buf, len, "%.1f", P->pv[i]);
        if (PDEF[i].kind == 1 || PDEF[i].kind == 2 || PDEF[i].kind == 4) return snprintf(buf, len, "%d", (int)P->pv[i]);
        return snprintf(buf, len, "0");
    }
    /* "<key>_name" and "<key>_display": names and values that follow the algorithm */
    char base[48];
    size_t kl = strlen(key);
    int disp = kl > 8 && !strcmp(key + kl - 8, "_display"), name = kl > 5 && !strcmp(key + kl - 5, "_name");
    if (!disp && !name) return 0;
    snprintf(base, sizeof base, "%.*s", (int)(kl - (disp ? 8 : 5)), key);
    i = find_key(base);
    if (i < 0) return 0;
    if (disp && i >= P_BR_ROW_1 && i < P_BR_ROW_1 + BROWSE_ROWS) return browse_row(P, i - P_BR_ROW_1, buf, len);
    /* the rows shown (the box beside FOLDER says how many there are: this one mustn't change as a listing comes in) */
    if (disp && i == P_BR_SCROLL) return snprintf(buf, len, "ROWS %d-%d", (int)P->pv[i] + 1, (int)P->pv[i] + BROWSE_ROWS);
    if (disp && (i == P_BR_CURSOR || i == P_BR_CURSOR_Q)) return cursor_text(P, buf, len);
    for (int v = 0; v < NV && disp; v++)
        if (i == VSX(v, P_V1_SMP)) return sample_display(P, v, buf, len);
    if (i >= P_V1_TUNE && i < P_V1_TUNE + NV * VSTRIDE) {
        int v = (i - P_V1_TUNE) / VSTRIDE, k = (i - P_V1_TUNE) % VSTRIDE;
        int algo = (int)P->pv[VALGO(v)], mode = (int)P->pv[VMODE(v)];
        float x = P->pv[i] / 100.0f;
        if (name) return (k == K_P1 || k == K_P2) ? snprintf(buf, len, "V%d %s", v + 1, p_name(P, v, k == K_P1 ? 1 : 2)) : 0;
        if (own_sample(P, v) && k == K_TUNE) return snprintf(buf, len, "%+.1f st", (x - 0.5f) * 48.0f);
        if (own_sample(P, v) && k == 9) return snprintf(buf, len, "ALGO OFF: SAMPLE");
        switch (k) {
        case K_TUNE:
            if (v == 2 && algo == 1) return fmt_hz(buf, len, 400.0f * fexp2(4.0f * x));
            if (v < 3) return fmt_hz(buf, len, voice_freq(v, x));
            return snprintf(buf, len, "%+.1f st", (x - 0.5f) * (algo == 2 ? 48.0f : 36.0f));
        case K_DECAY: return x >= 0.99f ? snprintf(buf, len, "DRONE") : fmt_s(buf, len, voice_t60(v, x));
        case K_P1: return p_display(P, v, 1, buf, len);
        case K_P2: return p_display(P, v, 2, buf, len);
        case K_CUTOFF: return fmt_hz(buf, len, 20.0f * fexp2(x * 9.9658f));
        case 9:
            if (v == 3 && algo == 2 && !(P->bank >= 0 && P->banks[P->bank].smp[mode % 3].len)) {
                static const char *const HAT[3] = {"CLOSED HAT", "OPEN HAT", "RIDE"};
                return snprintf(buf, len, "%s", HAT[mode % 3]);
            }
            return snprintf(buf, len, "%s", mode_name(v, algo, mode));
        }
        return 0;
    }
    if (!disp) return 0;
    float x = P->pv[i] / 100.0f;
    switch (i) {
    case P_FX_TIME: return fmt_s(buf, len, bbd_seconds(P));
    case P_FX_RATE: return snprintf(buf, len, "%.2f Hz", bbd_rate(P));
    case P_COMP_THRESH: return x > 0.005f ? snprintf(buf, len, "%.0f dB", -36.0f * x) : snprintf(buf, len, "0 dB");
    case P_COMP_AMT: return x > 0.000001f ? snprintf(buf, len, "%.1f:1", 1.0f + 9.0f * x) : snprintf(buf, len, "OFF");
    case P_COMP_LEVEL: return x > 0.001f ? snprintf(buf, len, "%+.1f dB", 20.0f * log10f(level_gain(P->pv[i]))) : snprintf(buf, len, "-inf dB");
    case P_LFO_SPEED: return snprintf(buf, len, "%.2f Hz", lfo_rate(x));
    }
    return 0;
}

/* ---- life ---------------------------------------------------------------------------------------------------- */

static void *e_create(const char *dir) {
    dsp_tables_init();
    perc_t *P = calloc(1, sizeof *P);
    if (!P) return NULL;
    P->dl = calloc(DL_N, sizeof(float));
    if (!P->dl) { free(P); return NULL; }
    snprintf(P->dir, sizeof P->dir, "%s", dir ? dir : "");
    for (int i = 0; i < NP; i++) P->pv[i] = PDEF[i].def;
    P->bank = -1;
    P->kit_pending = -1;
    P->lfo_rng = 0x2545f491u;
    P->id = __atomic_fetch_add(&next_id, 1, __ATOMIC_RELAXED);
    P->rrng = (uint32_t)time(NULL) * 2654435761u ^ P->id * 0x9e3779b9u;
    if (!P->rrng) P->rrng = 1;
    lib_open(P->dir);
    P->prv_slot = -1;
    for (int v = 0; v < NV; v++) {
        P->sfold[v] = -1;
        P->v[v].lslot = -1;
    }
    P->bus = tapbus_open();
    mi_open(&P->midi_in, "Percolator");
    for (int v = 0; v < NV; v++) {
        P->v[v].rng = 0x9e3779b9u + 7919u * (unsigned)v;
        P->v[v].note = -1;
        P->v[v].cut_s = P->pv[VK(v, K_CUTOFF)] / 100.0f;
    }
    kits_load(P);
    P->dl_t = bbd_seconds(P) * SR;
    TRACE("create %p (%d kits, dir %s)", (void *)P, P->nkits, P->dir);
    return P;
}

static void e_destroy(void *inst) {
    perc_t *P = inst;
    TRACE("destroy %p", inst);
    if (!P) return;
    tap_release(P);
    tapbus_close(P->bus);
    mi_close(&P->midi_in);
    for (int v = 0; v < NV; v++) voice_stop(P, v);
    preview_stop(P);
    lib_browser_free(P->br);
    lib_list_free(P->bl);
    lib_close();
    kits_free(P);
    free(P->dl);
    free(P);
}

static const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

/* After each block the wrapper asks which parameters changed here (the Pērkons CCs, perkons_cc), one at a time, and
 * tells MPC, so the screen and the Q-Links follow. A wrapper without the hook never asks: the values still change. */
_Static_assert(NP <= 32 * (int)(sizeof ((perc_t *)0)->changed / sizeof(uint32_t)), "perc_t.changed: a bit per parameter");
int mpc_engine_changed(void *inst, char *key, int len) {
    perc_t *P = inst;
    for (int w = 0; w < (int)(sizeof P->changed / sizeof P->changed[0]); w++)
        if (P->changed[w]) {
            int b = __builtin_ctz(P->changed[w]);
            P->changed[w] &= P->changed[w] - 1;
            return snprintf(key, len, "%s", PDEF[32 * w + b].key) > 0;
        }
    return 0;
}
