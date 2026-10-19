/* Incipience: the sample pool. Sample n (1-500) is the file ABWV<nnnn>.raw (the AlphaBase's own: 16-bit, 48 kHz, mono,
 * no header) or ABWV<nnnn>.wav (16-bit PCM, any rate; stereo is mixed down) in the data folder, as the AlphaBase names
 * them on its SD card; upper or lower case. One pool serves every instance in the process, and a sample is read only
 * when a kit or a control asks for it, by a thread of the pool's own, which sleeps while there is nothing to read (the
 * audio thread asks without waiting: a trylock and a signal). */
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "incipience.h"

enum { SL_NONE, SL_WANTED, SL_READY, SL_MISSING };

static struct {
    int16_t *d[NSMP];
    int len[NSMP];
    float rate[NSMP];
    int state[NSMP];              /* SL_*, read with atomics */
    char *file[NSMP];             /* the file's name, from the folder scan */
    char dir[512];
    pthread_t th;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int refs, quit, pending;      /* pending: something was asked for since the loader last looked */
} G = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};

static int ld(const int *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void st(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }

static int is_wav(const char *f) { size_t n = strlen(f); return n > 4 && !strcasecmp(f + n - 4, ".wav"); }

/* the data folder, and its folders one level down (an unzip often leaves the files in AlphaBaseWaves/ or the like) */
static void scan_dir(const char *sub);

static void scan(void) {
    scan_dir("");
    DIR *d = opendir(G.dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[600];
        snprintf(p, sizeof p, "%s/%s", G.dir, e->d_name);
        DIR *s = opendir(p);
        if (!s) continue;
        closedir(s);
        scan_dir(e->d_name);
    }
    closedir(d);
}

static void scan_dir(const char *sub) {
    char path[600];
    snprintf(path, sizeof path, "%s%s%s", G.dir, *sub ? "/" : "", sub);
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *s = e->d_name;
        if (strlen(s) != 12 || strncasecmp(s, "ABWV", 4) || s[8] != '.') continue;
        if (strcasecmp(s + 9, "raw") && strcasecmp(s + 9, "wav")) continue;
        int ok = 1, n = 0;
        for (int i = 4; i < 8; i++) {
            if (!isdigit((unsigned char)s[i])) ok = 0;
            n = n * 10 + (s[i] - '0');
        }
        if (!ok || n < 1 || n >= NSMP) continue;
        /* the top level before a folder, then (at one level) a .wav before a .raw of the same number */
        if (G.file[n] && ((*sub && !strchr(G.file[n], '/')) || is_wav(G.file[n]))) continue;
        char rel[300];
        snprintf(rel, sizeof rel, "%s%s%s", sub, *sub ? "/" : "", s);
        free(G.file[n]);
        G.file[n] = strdup(rel);
    }
    closedir(d);
}


static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

/* the whole file into memory, then the samples out of it */
static void load(int n) {
    char path[600];
    snprintf(path, sizeof path, "%s/%s", G.dir, G.file[n]);
    FILE *f = fopen(path, "rb");
    if (!f) { st(&G.state[n], SL_MISSING); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 4 || sz > 64L << 20) { fclose(f); st(&G.state[n], SL_MISSING); return; }
    uint8_t *b = malloc((size_t)sz);
    size_t got = b ? fread(b, 1, (size_t)sz, f) : 0;
    fclose(f);
    int16_t *out = NULL;
    int frames = 0;
    float rate = SMP_RATE;
    if (got == (size_t)sz && is_wav(G.file[n]) && sz > 44 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4)) {
        int ch = 0, bits = 0, fmt = 0;
        long p = 12;
        while (p + 8 <= sz) {
            uint32_t cl = u32(b + p + 4);
            if (!memcmp(b + p, "fmt ", 4) && cl >= 16) {
                fmt = u16(b + p + 8);
                ch = u16(b + p + 10);
                rate = (float)u32(b + p + 12);
                bits = u16(b + p + 22);
            } else if (!memcmp(b + p, "data", 4) && fmt == 1 && bits == 16 && ch >= 1) {
                long avail = sz - (p + 8);
                if ((long)cl > avail) cl = (uint32_t)avail;
                frames = (int)(cl / (2 * ch));
                out = malloc(sizeof(int16_t) * (frames ? frames : 1));
                for (int i = 0; out && i < frames; i++) {
                    int acc = 0;
                    for (int c = 0; c < ch; c++) acc += (int16_t)u16(b + p + 8 + 2 * (i * ch + c));
                    out[i] = (int16_t)(acc / ch);
                }
                break;
            }
            p += 8 + cl + (cl & 1);
        }
    } else if (got == (size_t)sz) {
        frames = (int)(sz / 2);
        out = malloc(sizeof(int16_t) * frames);
        for (int i = 0; out && i < frames; i++) out[i] = (int16_t)u16(b + 2 * i);
    }
    free(b);
    if (!out || frames < 2 || rate < 1000.0f) { free(out); st(&G.state[n], SL_MISSING); return; }
    G.d[n] = out;
    G.len[n] = frames;
    G.rate[n] = rate;
    st(&G.state[n], SL_READY);
}

static void *loader(void *arg) {
    (void)arg;
    pthread_mutex_lock(&G.mu);
    scan();
    for (;;) {
        while (!ld(&G.pending) && !G.quit) pthread_cond_wait(&G.cv, &G.mu);
        if (G.quit) break;
        st(&G.pending, 0);
        pthread_mutex_unlock(&G.mu);
        for (int n = 1; n < NSMP; n++)
            if (ld(&G.state[n]) == SL_WANTED) {
                if (G.file[n]) load(n);
                else st(&G.state[n], SL_MISSING);
            }
        pthread_mutex_lock(&G.mu);
    }
    pthread_mutex_unlock(&G.mu);
    return NULL;
}

void pool_open(const char *dir) {
    pthread_mutex_lock(&G.mu);
    if (G.refs++ == 0) {
        snprintf(G.dir, sizeof G.dir, "%s", dir ? dir : ".");
        G.quit = 0;
        if (pthread_create(&G.th, NULL, loader, NULL)) G.refs = -1000;   /* no thread: no samples */
    }
    pthread_mutex_unlock(&G.mu);
}

void pool_close(void) {
    pthread_mutex_lock(&G.mu);
    int last = --G.refs == 0;
    if (last) {
        G.quit = 1;
        pthread_cond_signal(&G.cv);
    }
    pthread_mutex_unlock(&G.mu);
    if (!last) return;
    pthread_join(G.th, NULL);
    for (int n = 0; n < NSMP; n++) {
        free(G.d[n]);
        free(G.file[n]);
        G.d[n] = NULL;
        G.file[n] = NULL;
        G.len[n] = 0;
        G.state[n] = SL_NONE;
    }
}

int pool_get(int n, smp_t *s) {
    if (n < 1 || n >= NSMP) return 0;
    int state = ld(&G.state[n]);
    if (state == SL_READY) {
        s->d = G.d[n];
        s->len = G.len[n];
        s->rate = G.rate[n];
        return 1;
    }
    if (state == SL_NONE) {
        st(&G.state[n], SL_WANTED);
        st(&G.pending, 1);
        pool_poke();
    }
    return 0;
}

/* wakes the loader if something is waiting, without ever blocking: called with each request and once a block (a
 * request made while the loader held the lock, about to sleep, is picked up by the next block's poke) */
void pool_poke(void) {
    if (!ld(&G.pending) || pthread_mutex_trylock(&G.mu)) return;
    pthread_cond_signal(&G.cv);
    pthread_mutex_unlock(&G.mu);
}

int pool_exists(int n) {
    return n >= 1 && n < NSMP && ld(&G.state[n]) != SL_MISSING && G.file[n] != NULL;
}

void pool_wait(void) {
    for (int tries = 0; tries < 2000; tries++) {
        int busy = 0;
        for (int n = 1; n < NSMP; n++) busy |= ld(&G.state[n]) == SL_WANTED;
        if (!busy) return;
        pthread_mutex_lock(&G.mu);
        st(&G.pending, 1);
        pthread_cond_signal(&G.cv);
        pthread_mutex_unlock(&G.mu);
        usleep(2000);
    }
}
