/* Mix Strip and Mix Bus: what every instance of the two shares (2026-10-25).
 *
 * Mix Strip goes last in a track's inserts and gives that track a mixer strip a MIDI controller reaches: LEVEL, MUTE,
 * CUE (a pre-fader listen), FILTER and two sends. Mix Bus, on an otherwise empty audio track, plays one of the buses
 * the strips feed: CUE, FX A or FX B. They are separate libraries and every instance sits on its own track, so they
 * share no statics; they meet in one region of shared memory, /dev/shm/mixstrip-v<version>-<MPC's pid> (/tmp when
 * there is no /dev/shm), which each instance maps when it's created, as Percolator and Percolator OUT do (tapbus.h).
 * The file is removed by the next MPC that starts after this one has gone.
 *
 * What's in it:
 * - The controller table. One instance at a time, the listener, reads the controllers plugged into the MPC (mixin.h)
 *   and writes every Control Change it gets here: per channel and CC, how many have come, the last value and the
 *   running sum of (value - 64), which is what a relative (binary offset) encoder sends. Each strip picks out its own
 *   six CCs (MIX_CC_*) on its channel. The listener is whichever instance runs a block first; if it stops running
 *   blocks (removed, or its track bypassed), another takes over after MIX_STALE of its own.
 * - One slot per TRACK 1-16, held by one strip (a second strip set to the same TRACK still follows the controller but
 *   feeds no bus). The strip writes its three taps there, CUE, SEND A and SEND B, each a stereo ring, publishes how far
 *   it has written, and leaves its level and switches for Mix Bus's overview.
 * - What the strips share: the CUE bus's CUE MODE (SOLO: a new cue turns the others off) and the last MIDI CH and KNOBS
 *   set on any strip, which a new strip starts with.
 * - Mix Desk's settings (2026-11-07, one desk at a time): which TRACK each of the 16 columns plays, page 1's MIDI CH,
 *   whether LEVEL and MUTE are MPC's own (MPC MIXER: the desk sends them to MPC's fader and Track Mute from its own
 *   MIDI port, learned once; the strips then leave them alone) and whether BUTTON 2 is CUE or SEND KILL. With no desk
 *   each strip's column is its TRACK, as before. The desk also leaves each TRACK's fader as it last sent it, so a
 *   strip's sends can follow MPC's fader (which comes after the strip).
 *
 * The blocks run on MPC's worker threads at the same time, in no fixed order. A bus reads each slot the way Percolator
 * OUT reads an OUT (mix_read): the block before the last one written, which it finds whichever of the two ran first
 * in a period. So the buses play a block or two (2.9 to 5.8 ms) behind the strips' own outputs, which are never
 * delayed.
 *
 * This header is in both plugins (originals/mixstrip/src and originals/mixstrip/mixbus/src): keep the two the same.
 * MIX_VERSION is part of the file name and the header, so two different layouts never read each other. */
#pragma once
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIX_VERSION 2              /* 2: Mix Desk (2026-11-07) */
#define MIX_TRACKS 16
#define MIX_COLS 16                /* the controller's columns: page 1 (1-8) and page 2 (9-16) */
#define MIX_TAPS 3                /* the buses: CUE, FX A, FX B */
#define MIX_N 1024                /* frames per tap ring (8 blocks of 128) */
#define MIX_ALIVE (16u * 128u)    /* frames a bus's last look counts for */
#define MIX_STALE 32              /* blocks (93 ms) the listener may skip before another instance takes over */

/* The controller chart: column c (0-7) of a page sends these CCs plus c, page 1 on MIDI CH and page 2 on MIDI CH + 1.
 * Knobs are relative (binary offset) or absolute (KNOBS); the faders absolute; the buttons on at 64 and up. */
#define MIX_CC_SEND_A 13
#define MIX_CC_SEND_B 29
#define MIX_CC_FILTER 49
#define MIX_CC_LEVEL 77
#define MIX_CC_MUTE 104
#define MIX_CC_CUE 112             /* BUTTON 2: CUE, or SEND KILL when Mix Desk says so */

/* Mix Desk to MPC (MPC MIXER), learned once with MPC's MIDI Learn: MPC track t's volume is CC MIX_OUT_CC + t - 1 on
 * MIDI channel MIX_OUT_VOL_CH, its Track Mute the same CC on MIX_OUT_MUTE_CH (127 muted, 0 not, or a press: 127 0) */
#define MIX_OUT_CC 102             /* tracks 1-16: CC 102-117 */
#define MIX_OUT_VOL_CH 1
#define MIX_OUT_MUTE_CH 2
#define MIX_UNKNOWN 255            /* a fader or mute the desk hasn't sent yet */

/* a slot's flags, for the overview */
#define MIX_F_MUTE 1u
#define MIX_F_CUE 2u
#define MIX_F_SEND_A 4u
#define MIX_F_SEND_B 8u
#define MIX_F_KILL 16u   /* its sends are killed (SEND KILL) */

typedef struct {
    uint32_t owner;               /* the strip holding this TRACK (its id), 0 = none */
    uint32_t gen;                 /* changes whenever the owner does: buses sync again */
    uint32_t wpos;                /* frames written to the rings */
    uint32_t flags;               /* MIX_F_* */
    float level_db;               /* the strip's LEVEL in dB (-200 = off) */
    float send_a_db, send_b_db;   /* its sends, for the desk */
    uint32_t col;                 /* the column it follows, 1-16 (0 = none) */
    float ring[MIX_TAPS][MIX_N][2];
} mixtrack_t;

typedef struct {
    uint32_t magic, version, size;
    uint32_t next_id;                       /* instance ids, from 1 */
    uint32_t listener, listener_beat;       /* the instance reading the controllers, and its block count */
    uint32_t controllers;                   /* controller ports it's connected to */
    uint32_t cc_seq[16][128];               /* per channel and CC: how many have come */
    int32_t cc_rel[16][128];                /* the sum of (value - 64) over them */
    uint8_t cc_val[16][128];                /* the last value */
    uint32_t prefs, midi_ch, knobs;           /* prefs != 0: the last MIDI CH and KNOBS set on a strip */
    uint32_t cue_mode;                      /* 0 ADD, 1 SOLO (the CUE bus's) */
    uint32_t solo_seq, solo_track;          /* SOLO: the last strip whose CUE went on (its TRACK) */
    uint32_t bus_beat[MIX_TAPS];            /* a block count per bus played (strips say when nothing plays theirs) */
    uint32_t desk, desk_gen;                /* the Mix Desk's id (0 = none); changes with each of its settings */
    uint32_t desk_mpc, desk_kill, desk_ch;  /* LEVEL + MUTE on MPC's mixer; BUTTON 2 = SEND KILL; page 1's MIDI CH */
    uint8_t alloc[MIX_COLS];                /* column -> TRACK 1-16, 0 = none */
    uint8_t fader[MIX_TRACKS];              /* per TRACK: the fader value the desk last sent MPC (MIX_UNKNOWN) */
    uint8_t mpc_mute[MIX_TRACKS];           /* and its Track Mute */
    mixtrack_t track[MIX_TRACKS];
} mixshm_t;

typedef struct {
    int sync;
    uint32_t gen, t, lag, w;   /* the owner it follows, its own frame count, the lag it reads at, wpos last time */
    uint32_t from;             /* the first frame it reads (written after it synced to this owner) */
} mixread_t;

typedef struct { uint32_t beat, stale; } mixlisten_t;

typedef struct { int on, mpc, kill, ch; uint8_t alloc[MIX_COLS]; } mixdesk_t;   /* a strip's copy of the desk's settings */

#define MIX_MAGIC 0x3158494du   /* "MIX1" */
#define MIX_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define MIX_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static void mix_clean(const char *dir) {   /* the files of MPCs that have gone */
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char pre[32], path[300];
    snprintf(pre, sizeof pre, "mixstrip-v%d-", MIX_VERSION);
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, pre, strlen(pre))) continue;
        int pid = atoi(e->d_name + strlen(pre));
        if (pid > 0 && pid != (int)getpid() && kill(pid, 0) < 0 && errno == ESRCH) {
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            unlink(path);
        }
    }
    closedir(d);
}

/* Maps this process's region, making it if it isn't there; NULL if that can't be done (the plugin then passes its
 * audio through as a plain strip). On the message thread (create), never while playing. */
static __attribute__((unused)) mixshm_t *mix_open(void) {
    static const char *const DIRS[2] = {"/dev/shm", "/tmp"};
    for (int i = 0; i < 2; i++) {
        char path[96];
        snprintf(path, sizeof path, "%s/mixstrip-v%d-%d", DIRS[i], MIX_VERSION, (int)getpid());
        int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) continue;
        mix_clean(DIRS[i]);
        struct stat st;
        void *p = MAP_FAILED;
        if (!fstat(fd, &st) && (st.st_size >= (off_t)sizeof(mixshm_t) || !ftruncate(fd, sizeof(mixshm_t))))
            p = mmap(NULL, sizeof(mixshm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED) continue;
        mixshm_t *m = (mixshm_t *)p;
        uint32_t none = 0;
        if (__atomic_compare_exchange_n(&m->magic, &none, MIX_MAGIC, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            m->version = MIX_VERSION;
            memset(m->fader, MIX_UNKNOWN, sizeof m->fader);
            memset(m->mpc_mute, MIX_UNKNOWN, sizeof m->mpc_mute);
            MIX_STORE(m->size, (uint32_t)sizeof(mixshm_t));
        }
        for (int k = 0; k < 1000 && !MIX_LOAD(m->size); k++) usleep(100);   /* another instance is filling it in */
        if (MIX_LOAD(m->magic) == MIX_MAGIC && m->version == MIX_VERSION && MIX_LOAD(m->size) == sizeof(mixshm_t))
            return m;
        munmap(p, sizeof(mixshm_t));
    }
    return NULL;
}

static __attribute__((unused)) void mix_close(mixshm_t *m) {
    if (m) munmap(m, sizeof(mixshm_t));
}

static __attribute__((unused)) uint32_t mix_new_id(mixshm_t *m) {
    uint32_t id;
    do id = __atomic_add_fetch(&m->next_id, 1, __ATOMIC_ACQ_REL);
    while (!id);
    return id;
}

/* ---- the listener ---- */

/* Each block: 1 if this instance is (now) the one reading the controllers. */
static __attribute__((unused)) int mix_listen(mixshm_t *m, uint32_t id, mixlisten_t *L) {
    uint32_t l = MIX_LOAD(m->listener);
    if (l == id) {
        __atomic_add_fetch(&m->listener_beat, 1, __ATOMIC_ACQ_REL);
        return 1;
    }
    uint32_t b = MIX_LOAD(m->listener_beat);
    if (l && b != L->beat) {   /* it's running */
        L->beat = b;
        L->stale = 0;
        return 0;
    }
    if (l && ++L->stale < MIX_STALE) return 0;
    if (__atomic_compare_exchange_n(&m->listener, &l, id, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        L->stale = 0;
        return 1;
    }
    return 0;
}

static __attribute__((unused)) void mix_unlisten(mixshm_t *m, uint32_t id) {
    uint32_t me = id;
    __atomic_compare_exchange_n(&m->listener, &me, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* A Control Change from a controller (the listener only, or an offline test) */
static __attribute__((unused)) void mix_cc_in(mixshm_t *m, int ch, int cc, int v) {
    if (ch < 0 || ch > 15 || cc < 0 || cc > 127) return;
    m->cc_val[ch][cc] = (uint8_t)(v & 127);
    __atomic_add_fetch(&m->cc_rel[ch][cc], (v & 127) - 64, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&m->cc_seq[ch][cc], 1, __ATOMIC_ACQ_REL);
}

/* ---- the TRACK slots ---- */

/* 1 if this strip holds slot s (taking it if it's free) */
static __attribute__((unused)) int mix_claim(mixshm_t *m, int s, uint32_t id) {
    mixtrack_t *T = &m->track[s];
    uint32_t none = 0;
    if (MIX_LOAD(T->owner) == id) return 1;
    if (!__atomic_compare_exchange_n(&T->owner, &none, id, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 0;
    __atomic_add_fetch(&T->gen, 1, __ATOMIC_ACQ_REL);
    return 1;
}

static __attribute__((unused)) void mix_release(mixshm_t *m, int s, uint32_t id) {
    mixtrack_t *T = &m->track[s];
    uint32_t me = id;
    if (__atomic_compare_exchange_n(&T->owner, &me, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        MIX_STORE(T->flags, 0u);
        __atomic_add_fetch(&T->gen, 1, __ATOMIC_ACQ_REL);
    }
}

/* The slot's owner: its three taps for the next `frames` (interleaved stereo each; NULL = silence), then publishes
 * them. quiet[k] counts the silence written to tap k since it went quiet: once the whole ring is silent, a quiet tap
 * isn't written at all. */
static __attribute__((unused)) void mix_write(mixtrack_t *T, const float *taps[MIX_TAPS], int quiet[MIX_TAPS], int frames) {
    uint32_t w = T->wpos;   /* only the owner writes it */
    for (int k = 0; k < MIX_TAPS; k++) {
        if (!taps[k] && quiet[k] >= MIX_N) continue;
        for (int i = 0; i < frames; i++) {
            float *d = T->ring[k][(w + (uint32_t)i) & (MIX_N - 1)];
            d[0] = taps[k] ? taps[k][2 * i] : 0.0f;
            d[1] = taps[k] ? taps[k][2 * i + 1] : 0.0f;
        }
        quiet[k] = taps[k] ? 0 : quiet[k] + frames;
    }
    MIX_STORE(T->wpos, w + (uint32_t)frames);
}

/* A bus: tap `tap` of slot T for `frames` (<= MIX_N / 4), added into dst (interleaved stereo). Returns 1 if anything
 * was added, 0 for silence (no strip, or it has stopped: never its last block again). Percolator OUT's tapbus_read. */
static __attribute__((unused)) int mix_read(mixtrack_t *T, mixread_t *r, int tap, float *dst, int frames) {
    uint32_t n = (uint32_t)frames;
    if (frames <= 0 || frames > MIX_N / 4 || tap < 0 || tap >= MIX_TAPS || !MIX_LOAD(T->owner)) {
        r->sync = 0;
        return 0;
    }
    uint32_t g = MIX_LOAD(T->gen), w = MIX_LOAD(T->wpos);
    uint32_t lag = w - r->t;   /* how far the strip is ahead of this bus's own count */
    int moved = w != r->w;     /* the strip has written since this bus's last block */
    uint32_t away = w - r->w;  /* frames since this bus last looked */
    r->w = w;
    if (!r->sync || g != r->gen || away > MIX_ALIVE / 2) {
        if (!r->sync || g != r->gen) r->lag = lag;
        r->sync = 1;
        r->gen = g;
        r->from = w;
    } else if ((int32_t)(lag - r->lag) > 0) r->lag = lag;   /* it ran first this time: a block less late */
    uint32_t start = r->t + r->lag - 2 * n;
    if ((int32_t)(w - (start + n)) < 0 || w - start > MIX_N - 2 * n) {   /* not written yet, or gone */
        if (!moved) {   /* the strip has stopped (removed, bypassed) */
            r->t += n;
            return 0;
        }
        r->lag = lag;   /* it started again behind where it was: follow it from here */
        start = w - 2 * n;
    }
    r->t += n;
    int any = 0;
    for (int i = 0; i < frames; i++) {
        if ((int32_t)(start + (uint32_t)i - r->from) < 0) continue;
        const float *s = T->ring[tap][(start + (uint32_t)i) & (MIX_N - 1)];
        dst[2 * i] += s[0];
        dst[2 * i + 1] += s[1];
        any = 1;
    }
    return any;
}

/* ---- Mix Desk ---- */

/* The desk: 1 if this one holds the columns (taking them if no desk does) */
static __attribute__((unused)) int mix_desk_claim(mixshm_t *m, uint32_t id) {
    uint32_t none = 0;
    return MIX_LOAD(m->desk) == id || __atomic_compare_exchange_n(&m->desk, &none, id, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* The desk holding the columns: its settings, then the change the strips look for */
static __attribute__((unused)) void mix_desk_publish(mixshm_t *m, int mpc, int kill, int ch, const int alloc[MIX_COLS]) {
    for (int c = 0; c < MIX_COLS; c++) __atomic_store_n(&m->alloc[c], (uint8_t)alloc[c], __ATOMIC_RELEASE);
    MIX_STORE(m->desk_mpc, (uint32_t)mpc);
    MIX_STORE(m->desk_kill, (uint32_t)kill);
    MIX_STORE(m->desk_ch, (uint32_t)ch);
    if (!mpc)
        for (int t = 0; t < MIX_TRACKS; t++) {
            __atomic_store_n(&m->fader[t], (uint8_t)MIX_UNKNOWN, __ATOMIC_RELEASE);
            __atomic_store_n(&m->mpc_mute[t], (uint8_t)MIX_UNKNOWN, __ATOMIC_RELEASE);
        }
    __atomic_add_fetch(&m->desk_gen, 1, __ATOMIC_ACQ_REL);
}

static __attribute__((unused)) void mix_desk_release(mixshm_t *m, uint32_t id) {
    uint32_t me = id;
    if (!__atomic_compare_exchange_n(&m->desk, &me, 0, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    for (int t = 0; t < MIX_TRACKS; t++) {
        __atomic_store_n(&m->fader[t], (uint8_t)MIX_UNKNOWN, __ATOMIC_RELEASE);
        __atomic_store_n(&m->mpc_mute[t], (uint8_t)MIX_UNKNOWN, __ATOMIC_RELEASE);
    }
    __atomic_add_fetch(&m->desk_gen, 1, __ATOMIC_ACQ_REL);
}

/* A strip: the desk's settings into d if they changed since *gen; 1 if they did. (A setting changing while this
 * reads changes desk_gen again, so the next block reads it again.) */
static __attribute__((unused)) int mix_desk_read(mixshm_t *m, uint32_t *gen, mixdesk_t *d) {
    uint32_t g = MIX_LOAD(m->desk_gen);
    if (g == *gen) return 0;
    *gen = g;
    d->on = MIX_LOAD(m->desk) != 0;
    d->mpc = d->on && MIX_LOAD(m->desk_mpc);
    d->kill = d->on && MIX_LOAD(m->desk_kill);
    uint32_t ch = MIX_LOAD(m->desk_ch);
    d->ch = ch >= 1 && ch <= 15 ? (int)ch : 1;
    for (int c = 0; c < MIX_COLS; c++) {
        uint8_t t = __atomic_load_n(&m->alloc[c], __ATOMIC_ACQUIRE);
        d->alloc[c] = d->on && t <= MIX_TRACKS ? t : 0;
    }
    return 1;
}

/* ---- the fader and send laws (both plugins show the same dB) ---- */

/* LEVEL 0-100: off at 0, 0 dB at 75 (a small detent around it), +6 dB at 100 */
static __attribute__((unused)) float mix_level_db(float p) {
    if (p <= 0.0f) return -200.0f;
    if (p > 74.6f && p < 75.4f) return 0.0f;
    if (p >= 75.0f) return 0.24f * (p - 75.0f);
    float db = 66.4f * log10f(p / 75.0f);   /* -20 dB halfway to 0 dB, -40 at a quarter */
    return db < -199.0f ? -199.0f : db;
}

/* SEND 0-100: off at 0, 0 dB at 100, -20 dB halfway */
static __attribute__((unused)) float mix_send_db(float p) {
    if (p <= 0.0f) return -200.0f;
    float db = 66.4f * log10f(p / 100.0f);
    return db < -199.0f ? -199.0f : db;
}

static __attribute__((unused)) float mix_gain(float db) { return db <= -199.5f ? 0.0f : powf(10.0f, db / 20.0f); }

static __attribute__((unused)) int mix_db_text(float db, char *buf, int len) {
    if (db <= -199.5f) return snprintf(buf, len, "OFF");
    if (db > -0.05f && db < 0.05f) return snprintf(buf, len, "0.0 dB");
    return snprintf(buf, len, "%+.1f dB", db);
}
