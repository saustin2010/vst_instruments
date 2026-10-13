/* Incipience engine: parameters, MIDI, kits, the mix and the master section.
 *
 * MIDI: each instrument plays on its NOTE (36-46, C1-A#1, to start with: pads 1-11 of bank B on an MPC plugin track),
 * as tuned; the FM synth's note plays its CHORD. A note-off releases the sample channels' envelopes (their ADSRs hold
 * while the note does). The kick, the MBrane and the FM synth's envelopes run their course. HH CHOKE (the Mk II): the
 * two hats share a VCA, so each cuts the other, and when both land on the same step the open one wins. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
}

static int is_kit_param(int i) { return i > P_KIT_NAME && i < P_KD_NOTE; }   /* the sounds, the mix and the FX */

static void reset_sound(inc_t *P) {
    for (int i = P_KIT_NAME + 1; i < P_KD_NOTE; i++)
        P->pv[i] = PDEF[i].kind == 1 ? PDEF[i].def : PDEF[i].def;
}

static void apply_state(inc_t *P, const char *s, int kit_only) {
    char kv[96];
    while (*s) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                int i = find_key(kv);
                if (i == P_KIT && !kit_only) P->pv[P_KIT] = (float)atoi(eq);
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
    /* ask for the kit's samples now, so they're in by the time it plays */
    smp_t s;
    for (int c = 0; c < 6; c++) pool_get((int)P->pv[SBASE[c] + S_SAMPLE], &s);
    for (int c = 0; c < 2; c++) pool_get((int)P->pv[XBASE[c] + X_SAMPLE], &s);
}

/* KIT waits for the next block (MPC sets parameter 0 to the far end of its range and straight back on insert and on
 * every STOP; see Percolator's notes): the pair ends on the kit already loaded and nothing happens. */
static void apply_kit(inc_t *P) {
    if (P->kit_pending < 0) return;
    int n = P->kit_pending;
    P->kit_pending = -1;
    if (n != (int)P->pv[P_KIT]) load_kit(P, n);
}

static int get_state(const inc_t *P, char *buf, int len) {
    int o = snprintf(buf, len, "kit=%d;", (int)P->pv[P_KIT]);
    for (int i = 0; i < NP && o < len; i++) {
        int k = PDEF[i].kind;
        if (k == 0) o += snprintf(buf + o, len - o, "%s=%.2f;", PDEF[i].key, P->pv[i]);
        else if (k == 1 || k == 4) o += snprintf(buf + o, len - o, "%s=%d;", PDEF[i].key, (int)P->pv[i]);
    }
    return o < len ? o : -1;
}

/* ---- MIDI ------------------------------------------------------------------------------------------------------ */
static const int MUTE0 = P_KD_MUTE;

static void trigger(inc_t *P, int ins, float vel, int note) {
    if (P->pv[MUTE0 + ins] >= 0.5f) return;
    switch (ins) {
    case I_KD: kick_trigger(P, vel, 0.0f); break;
    case I_MB: mbrane_trigger(P, vel, 0.0f); break;
    case I_FM: fm_trigger(P, vel, 36, 1); break;
    default: {
        int s = ins - I_CH;
        if (P->pv[P_HH_CHOKE] >= 0.5f && (ins == I_CH || ins == I_OH)) {
            if (ins == I_CH && P->choke_n < CHOKE_SAME) return;   /* the open hat already played this step */
            smpv_t *o = &P->sv[ins == I_CH ? 1 : 0];
            if (o->active) o->choke = 0.999f;   /* fades in a few ms */
            if (ins == I_OH) P->choke_n = 0;
        }
        smp_trigger(P, s, vel, 0.0f);
    }
    }
    (void)note;
}

static void e_midi(void *inst, const uint8_t *m, int len) {
    inc_t *P = inst;
    apply_kit(P);
    if (len < 2) return;
    int st = m[0] & 0xF0, d1 = m[1] & 0x7F, d2 = len > 2 ? m[2] & 0x7F : 0;
    if (st == 0x90 && d2 > 0) {
        for (int i = 0; i < NI; i++)
            if ((int)P->pv[P_KD_NOTE + i] == d1) trigger(P, i, (float)d2, d1);
    } else if (st == 0x80 || st == 0x90) {
        for (int i = I_CH; i <= I_X2; i++)
            if ((int)P->pv[P_KD_NOTE + i] == d1) smp_release(P, i - I_CH);
    } else if (st == 0xB0 && (d1 == 120 || d1 == 123)) {
        for (int s = 0; s < 8; s++) {
            smp_release(P, s);
            if (d1 == 120) P->sv[s].active = 0;
        }
        if (d1 == 120) {
            P->kd.active = P->mb.active = 0;
            for (int v = 0; v < FM_VOICES; v++) P->fm[v].active = 0;
        }
    }
}

/* ---- render ---------------------------------------------------------------------------------------------------- */
static inline int16_t out_sample(float x) {   /* a soft ceiling instead of a hard clip */
    float ax = fabsf(x);
    if (ax > 0.9f) x = copysignf(0.9f + 0.1f * sat((ax - 0.9f) * 10.0f), x);
    int s = (int)lrintf(x * 32767.0f);
    return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
}

static void e_render(void *inst, int16_t *out, int frames) {
    inc_t *P = inst;
    apply_kit(P);
    pool_poke();
    fpstate_t fp = ftz_on();
    float vol = P->pv[P_VOLUME] / 100.0f;   /* 100 (to start with) = 0 dB, 127 = +4 dB */
    vol = vol * vol;
    float gl[NI], gr[NI], rs[NI], ds[NI];
    for (int i = 0; i < NI; i++) {   /* constant-power pan */
        float p = (P->pv[P_KD_PAN + MIXSTRIDE * i] - 64.0f) / 63.0f;
        float a = (clampf(p, -1.0f, 1.0f) + 1.0f) * 0.25f * PI_F;
        gl[i] = cosf(a) * 1.41421356f;
        gr[i] = sinf(a) * 1.41421356f;
        float r = P->pv[P_KD_RSEND + MIXSTRIDE * i] / 127.0f, d = P->pv[P_KD_DSEND + MIXSTRIDE * i] / 127.0f;
        rs[i] = r * r;
        ds[i] = d * d;
        if (P->pv[MUTE0 + i] >= 0.5f) gl[i] = gr[i] = rs[i] = ds[i] = 0.0f;
    }
    for (int off = 0; off < frames; off += SUB) {
        int n = frames - off < SUB ? frames - off : SUB;
        float L[SUB] = {0}, R[SUB] = {0}, rin[SUB] = {0}, din[SUB] = {0};
        for (int ins = 0; ins < NI; ins++) {
            float y[SUB] = {0}, side[SUB] = {0};
            switch (ins) {
            case I_KD: if (!P->kd.active) continue; kick_block(P, y, n); break;
            case I_MB: if (!P->mb.active) continue; mbrane_block(P, y, n); break;
            case I_FM: fm_block(P, y, side, n); break;
            default: if (!P->sv[ins - I_CH].active) continue; smp_block(P, ins - I_CH, y, n); break;
            }
            for (int i = 0; i < n; i++) {
                L[i] += y[i] * gl[ins] + side[i];
                R[i] += y[i] * gr[ins] - side[i];
                rin[i] += y[i] * rs[ins];
                din[i] += y[i] * ds[ins];
            }
        }
        fx_block(P, rin, din, L, R, n);
        for (int i = 0; i < n; i++) {
            out[2 * (off + i)] = out_sample(L[i] * vol);
            out[2 * (off + i) + 1] = out_sample(R[i] * vol);
        }
        if (P->choke_n < 1 << 30) P->choke_n += n;
    }
    ftz_off(fp);
}

/* ---- create / destroy / parameters ------------------------------------------------------------------------------ */
static void *e_create(const char *dir) {
    inc_t *P = calloc(1, sizeof *P);
    if (!P) return NULL;
    snprintf(P->dir, sizeof P->dir, "%s", dir ? dir : ".");
    inc_tables_init(P->dir);
    pool_open(P->dir);
    for (int i = 0; i < NP; i++) P->pv[i] = PDEF[i].def;
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
    fx_free(&P->fx);
    kits_free(P);
    pool_close();
    free(P);
}

static void e_set_param(void *inst, const char *key, const char *val) {
    inc_t *P = inst;
    if (!strcmp(key, "state")) {
        P->kit_pending = -1;
        apply_state(P, val, 0);
        smp_t s;
        for (int c = 0; c < 6; c++) pool_get((int)P->pv[SBASE[c] + S_SAMPLE], &s);
        for (int c = 0; c < 2; c++) pool_get((int)P->pv[XBASE[c] + X_SAMPLE], &s);
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
    set_value(P, i, val);
    if (PDEF[i].kind == 4 && (strstr(key, "_sample"))) {   /* a new sample number: start reading it */
        smp_t s;
        pool_get((int)P->pv[i], &s);
    }
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
    if (ENDS("_sample")) {
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
