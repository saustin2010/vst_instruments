/* Percolator's OUT 1-4: how the main Percolator hands voices to other MPC tracks.
 *
 * MPC plays only a plugin's first stereo pair (sd88me's multi-out test on a Force, 2026-09-30), so a voice gets a track
 * of its own another way: the main Percolator (TRACK PLAYS = MAIN KIT) sends each voice to MAIN or OUT 1-4 (V<n> OUT),
 * and whatever plays an OUT on another track gets those voices: [FX] Percolator OUT as an insert (an audio track
 * will do), or a second Percolator with TRACK PLAYS = OUT <n>. While an OUT is played, the main one leaves its voices
 * out of its own mix and compressor (their FX SEND still feeds its delay).
 *
 * The OUTs live in shared memory, /dev/shm/percolator-out-<MPC's pid> (/tmp when there is no /dev/shm), which every
 * instance of either plugin maps when it's created: they are separate libraries, so they share no statics, and this
 * way neither has to look for the other or wait for it. The file is 64 KB and is removed by the next MPC that starts
 * after this one has gone.
 *
 * The plugins' blocks run on MPC's worker threads at the same time, in no fixed order. The main one writes each OUT
 * into a ring and publishes how far it has written; a reader takes the block before that, which it can always find
 * written whichever of the two ran first in a period (two blocks back if it only ever saw the main one run second),
 * and the main one holds its own output back a block while an OUT is played, so the voices stay together. The main
 * one writes an OUT only while something reads it (a reader plays silence until its first block is written), and a
 * reader that stops (removed, switched off) hands its voices back after 16 blocks.
 *
 * This header is in both plugins (originals/percolator/src and originals/percolator-out/src): keep the two the same.
 * TAPBUS_VERSION is part of the file name and the header, so two different layouts never read each other. */
#pragma once
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAPBUS_VERSION 1
#define TAP_OUTS 4
#define TAP_N 4096   /* frames per OUT ring (32 blocks of 128) */
#define TAP_ALIVE (16u * 128u)   /* frames a reader's last look counts for */
#define TAP_GAIN (0.55f * 2.0f * 0.71f * 0.71f)   /* a voice into a reader's output: Percolator's mix gain x VOLUME at 71 */

typedef struct {
    uint32_t magic, version, size;
    uint32_t owner;                     /* the main Percolator writing the rings (its id), 0 = none */
    uint32_t gen;                       /* changes whenever the owner does: readers sync again */
    uint32_t wpos;                      /* frames written to the rings */
    uint32_t seen[TAP_OUTS], seen_gen[TAP_OUTS];   /* each OUT's reader: the last wpos it saw, under which gen */
    uint32_t voices[TAP_OUTS];          /* which voices the main one sends to each OUT (bit 0 = voice 1) */
    float ring[TAP_OUTS][TAP_N];
} tapbus_t;

typedef struct {
    int sync;
    uint32_t gen, t, lag, w;   /* the owner it follows, its own frame count, the lag it reads at, wpos last time */
    uint32_t from;             /* the first frame written for it (the main one writes an OUT only while it's read) */
} tapread_t;

#define TAPBUS_MAGIC 0x31435250u   /* "PRC1" */
#define TB_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define TB_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static void tapbus_clean(const char *dir) {   /* the files of MPCs that have gone */
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char pre[32], path[300];
    snprintf(pre, sizeof pre, "percolator-out-v%d-", TAPBUS_VERSION);
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

/* Maps this process's bus, making it if it isn't there; NULL if that can't be done (OUT 1-4 then stay quiet). On the
 * message thread (create), never while playing. */
static __attribute__((unused)) tapbus_t *tapbus_open(void) {
    static const char *const DIRS[2] = {"/dev/shm", "/tmp"};
    for (int i = 0; i < 2; i++) {
        char path[96];
        snprintf(path, sizeof path, "%s/percolator-out-v%d-%d", DIRS[i], TAPBUS_VERSION, (int)getpid());
        int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) continue;
        tapbus_clean(DIRS[i]);
        struct stat st;
        void *p = MAP_FAILED;
        if (!fstat(fd, &st) && (st.st_size >= (off_t)sizeof(tapbus_t) || !ftruncate(fd, sizeof(tapbus_t))))
            p = mmap(NULL, sizeof(tapbus_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED) continue;
        tapbus_t *b = (tapbus_t *)p;
        uint32_t none = 0;
        if (__atomic_compare_exchange_n(&b->magic, &none, TAPBUS_MAGIC, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            b->version = TAPBUS_VERSION;
            TB_STORE(b->size, (uint32_t)sizeof(tapbus_t));
        }
        for (int k = 0; k < 1000 && !TB_LOAD(b->size); k++) usleep(100);   /* another instance is filling it in */
        if (TB_LOAD(b->magic) == TAPBUS_MAGIC && b->version == TAPBUS_VERSION && TB_LOAD(b->size) == sizeof(tapbus_t))
            return b;
        munmap(p, sizeof(tapbus_t));
    }
    return NULL;
}

static __attribute__((unused)) void tapbus_close(tapbus_t *b) {
    if (b) munmap(b, sizeof(tapbus_t));
}

/* A reader: OUT `out` (0-3) of the main Percolator into dst (mono, `frames` <= TAP_N / 4), raw voice level. Returns
 * 1 with the block, 0 for silence (no main Percolator, or it has stopped: never its last block again). */
static __attribute__((unused)) int tapbus_read(tapbus_t *b, tapread_t *r, int out, float *dst, int frames) {
    uint32_t n = (uint32_t)frames;
    if (!b || out < 0 || out >= TAP_OUTS || frames <= 0 || frames > TAP_N / 4 || !TB_LOAD(b->owner)) {
        r->sync = 0;
        return 0;
    }
    uint32_t g = TB_LOAD(b->gen), w = TB_LOAD(b->wpos);
    TB_STORE(b->seen[out], w);
    TB_STORE(b->seen_gen[out], g);
    uint32_t lag = w - r->t;   /* how far the main one is ahead of this reader's own count */
    int moved = w != r->w;     /* the main one has written since this reader's last block */
    uint32_t away = w - r->w;  /* frames since this reader last looked: long enough, and the main one stopped writing */
    r->w = w;
    if (!r->sync || g != r->gen || away > TAP_ALIVE / 2) {
        if (!r->sync || g != r->gen) r->lag = lag;
        r->sync = 1;
        r->gen = g;
        r->from = w;   /* the main one writes for it from here (it sees this look at its next block) */
    } else if ((int32_t)(lag - r->lag) > 0) r->lag = lag;   /* it ran first this time: a block less late */
    uint32_t start = r->t + r->lag - 2 * n;
    if ((int32_t)(w - (start + n)) < 0 || w - start > TAP_N - 2 * n) {   /* not written yet, or gone */
        if (!moved) {   /* the main one has stopped (removed, not playing) */
            r->t += n;
            return 0;
        }
        r->lag = lag;   /* it started again behind where it was: follow it from here */
        start = w - 2 * n;
    }
    r->t += n;
    int any = 0;
    for (int i = 0; i < frames; i++) {
        int ok = (int32_t)(start + i - r->from) >= 0;
        dst[i] = ok ? b->ring[out][(start + i) & (TAP_N - 1)] : 0.0f;
        any |= ok;
    }
    return any;
}
