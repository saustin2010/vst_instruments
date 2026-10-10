/* Percolator engine: parameters, MIDI, kits, the LFO and the master section (BBD delay, compressor, volume).
 *
 * MIDI: notes 20-23 (G#-1-B-1, MPC pads 1-4 of bank A) play voices 1-4. With KEYS on a voice, every other note plays that voice
 * pitched (C3 = as tuned) and pitch bend bends it +-2 semitones. A voice whose DECAY is at the top drones while its
 * note is held. CC 120 / 123 stop everything. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define VK(v, k) (P_V1_TUNE + VSTRIDE * (v) + (k))
#define VALGO(v) VK(v, 8)
#define VMODE(v) VK(v, 9)
#define VVCF(v) VK(v, 10)
#define VDEPTH(v, k) (P_V1_M_TUNE + MSTRIDE * (v) + (k))

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

static void e_midi(void *inst, const uint8_t *m, int len) {
    perc_t *P = inst;
    apply_kit(P);
    if (len > 0 && m[0] < 0xF0) TRACE("midi %02x %02x %02x (%d)", m[0], len > 1 ? m[1] : 0, len > 2 ? m[2] : 0, len);
    if (len < 2) return;
    int st = m[0] & 0xF0, d1 = m[1] & 0x7F, d2 = len > 2 ? m[2] & 0x7F : 0;
    if (st == 0x90 && d2 > 0) {
        int vi = -1;
        float semi = 0;
        if (d1 >= NOTE0 && d1 < NOTE0 + NV) vi = d1 - NOTE0;
        else if (P->pv[P_KEYS] >= 1) {
            vi = (int)P->pv[P_KEYS] - 1;
            semi = (float)(d1 - 60);
        }
        if (vi < 0) return;
        vparam_t vp;
        vparams(P, vi, &vp);
        voice_trigger(P, vi, &vp, (float)d2, d1, semi);
    } else if (st == 0x80 || st == 0x90) {
        for (int v = 0; v < NV; v++)
            if (P->v[v].note == d1) voice_release(P, v);
    } else if (st == 0xE0) {
        P->bend = (float)((d2 << 7 | d1) - 8192) / 8192.0f * 2.0f;
    } else if (st == 0xB0 && (d1 == 120 || d1 == 123)) {
        for (int v = 0; v < NV; v++) {
            voice_release(P, v);
            if (d1 == 120) {
                P->v[v].active = 0;
                P->v[v].room.tail = 0;
            }
        }
    }
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

static void e_render(void *inst, int16_t *out, int frames) {
    perc_t *P = inst;
    apply_kit(P);
    fpstate_t fp = ftz_on();
    float target = bbd_seconds(P) * SR, rate = bbd_rate(P) * INV_SR;
    float dep = P->pv[P_FX_DEPTH] / 100.0f;
    float depth = dep * dep * fminf(0.012f, 0.5f * bbd_seconds(P)) * SR;
    float fb = 1.08f * P->pv[P_FX_FEEDBACK] / 100.0f;
    float ca = op_coef(P->pv[P_FX_COLOUR] >= 1 ? 7000.0f : 2200.0f), ch = op_coef(40.0f);
    float sm = 1.0f - expf(-1.0f / (0.12f * SR));
    float amt = P->pv[P_COMP_AMT] / 100.0f, thr_db = -36.0f * P->pv[P_COMP_THRESH] / 100.0f;
    float ratio = 1.0f + 9.0f * amt, thr = powf(10.0f, thr_db / 20.0f);
    float makeup = powf(10.0f, -thr_db * (1.0f - 1.0f / ratio) * 0.5f / 20.0f);
    float catk = op_coef(80.0f), crel = op_coef(1.3f);
    float vol = P->pv[P_VOLUME] / 100.0f;
    vol = 2.0f * vol * vol;
    for (int off = 0; off < frames; off += SUB) {
        int n = frames - off < SUB ? frames - off : SUB;
        float mix[SUB] = {0}, send[SUB] = {0};
        lfo_step(P, n);
        for (int v = 0; v < NV; v++) {
            vparam_t vp;
            vparams(P, v, &vp);
            voice_block(P, v, &vp, mix, send, n);
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
            float x = mix[i] * 0.55f + wet * 0.55f;
            /* compressor: peak follower, ratio up to 10:1, half the reduction made back */
            if (amt > 0.001f) {
                float a = fabsf(x);
                P->comp_env += (a > P->comp_env ? catk : crel) * (a - P->comp_env);
                float g = 1.0f;
                if (P->comp_env > thr) g = powf(P->comp_env / thr, 1.0f / ratio - 1.0f);
                x *= g * makeup;
            }
            x *= vol;
            /* a soft ceiling instead of a hard clip */
            float ax = fabsf(x);
            if (ax > 0.9f) x = copysignf(0.9f + 0.1f * sat((ax - 0.9f) * 10.0f), x);
            int s = (int)lrintf(x * 32767.0f);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            out[2 * (off + i)] = out[2 * (off + i) + 1] = (int16_t)s;
        }
    }
    ftz_off(fp);
}

/* ---- parameters ---------------------------------------------------------------------------------------------- */

static void set_value(perc_t *P, int i, const char *val) {
    const pdef_t *d = &PDEF[i];
    if (d->kind == 0) P->pv[i] = clampf((float)atof(val), 0.0f, 100.0f);
    else if (d->kind == 1) {
        int x = atoi(val);
        P->pv[i] = (float)(x < 0 ? 0 : x >= d->nopts ? d->nopts - 1 : x);
    }
}

static void set_state(perc_t *P, const char *s) {
    char kv[256];
    while (*s) {
        size_t n = strcspn(s, ";");
        if (n && n < sizeof kv) {
            memcpy(kv, s, n);
            kv[n] = 0;
            char *eq = strchr(kv, '=');
            if (eq) {
                *eq++ = 0;
                int i = find_key(kv);
                if (!strcmp(kv, "bank")) {
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
        else if (PDEF[i].kind == 1) o += snprintf(buf + o, len - o, "%s=%d;", PDEF[i].key, (int)P->pv[i]);
    return o < len ? o : -1;
}

static void e_set_param(void *inst, const char *key, const char *val) {
    perc_t *P = inst;
    TRACE("set %s = %.48s%s (%d chars)", key, val, strlen(val) > 48 ? "..." : "", (int)strlen(val));
    if (!strcmp(key, "state")) {
        P->kit_pending = -1;   /* a restore says which kit it is, and every knob */
        set_state(P, val);
        return;
    }
    int i = find_key(key);
    if (i < 0) return;
    if (i == P_KIT) {   /* loaded at the next block (apply_kit); setting the kit already loaded changes nothing */
        int n = atoi(val);
        P->kit_pending = n < 0 ? 0 : n >= P->nkits ? P->nkits - 1 : n;
    } else {
        apply_kit(P);   /* a kit first, then this on top of it, in the order they came */
        set_value(P, i, val);
    }
}

static int fmt_hz(char *buf, int len, float hz) {
    return hz < 1000.0f ? snprintf(buf, len, "%.0f Hz", hz) : snprintf(buf, len, "%.2f kHz", hz / 1000.0f);
}
static int fmt_s(char *buf, int len, float s) {
    return s < 1.0f ? snprintf(buf, len, "%.0f ms", s * 1000.0f) : snprintf(buf, len, "%.2f s", s);
}

/* what PARAM 1 / PARAM 2 read as, in their own units where they have one */
static int p_display(const perc_t *P, int v, int which, char *buf, int len) {
    int algo = (int)P->pv[VALGO(v)], mode = (int)P->pv[VMODE(v)];
    float x = P->pv[VK(v, which == 1 ? K_P1 : K_P2)] / 100.0f;
    const char *n = algo_p_name(v, algo, mode, which, P);
    if (!strcmp(n, "PITCH ENV")) return snprintf(buf, len, "%.1f oct", (v == 0 && algo != 2 ? 6.0f : v == 0 ? 5.0f : 4.0f) * powf(x, 1.3f));
    if (!strcmp(n, "ENV TIME")) return fmt_s(buf, len, 0.003f * powf(100.0f, x));
    if (!strcmp(n, "ATTACK")) return fmt_s(buf, len, v4_attack(x));
    if (!strcmp(n, "NOISE DECAY")) return fmt_s(buf, len, 0.04f * powf(30.0f, x));
    if (!strcmp(n, "NOISE TONE")) return fmt_hz(buf, len, 1200.0f * powf(12.0f, x));
    if (!strcmp(n, "SLICE")) {
        int ns = sample_slices(P, mode % 3), q = (int)(x * ns);
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
        if (PDEF[i].kind == 0) return snprintf(buf, len, "%.1f", P->pv[i]);
        if (PDEF[i].kind == 1 || PDEF[i].kind == 2) return snprintf(buf, len, "%d", (int)P->pv[i]);
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
    if (i >= P_V1_TUNE && i < P_V1_TUNE + NV * VSTRIDE) {
        int v = (i - P_V1_TUNE) / VSTRIDE, k = (i - P_V1_TUNE) % VSTRIDE;
        int algo = (int)P->pv[VALGO(v)], mode = (int)P->pv[VMODE(v)];
        float x = P->pv[i] / 100.0f;
        if (name) return (k == K_P1 || k == K_P2) ? snprintf(buf, len, "V%d %s", v + 1, algo_p_name(v, algo, mode, k == K_P1 ? 1 : 2, P)) : 0;
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
    kits_free(P);
    free(P->dl);
    free(P);
}

static const mpc_engine_t ENGINE = {e_create, e_destroy, e_midi, e_set_param, e_get_param, e_render};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
