/* Incipience: your own samples (the SAMPLES tab). Folders of WAV files anywhere on the MPC: a sample channel loaded from
 * one plays its files, SAMPLE stepping through them in name order (1 = the first, up to LIB_FILES). Shared by every
 * instance in the process; a thread of its own does all the reading, and nothing here waits for the audio thread or
 * keeps it waiting (it only looks things up, with atomics):
 *  - folders (lib_folder, lib_folder_set): a path and its WAV files' names, sorted; a folder keeps its number while the
 *    process runs. A folder read again with other files gets a new number (the old one still plays what it had);
 *  - decoded files (lib_get / lib_put): up to LIB_SLOTS of them, LIB_BYTES in all, read when a hit or a choice first
 *    asks; past that the ones longest unplayed go. A voice holds the file it plays (a count), and the thread frees only
 *    a file nothing holds;
 *  - listings (lib_browse / lib_listing): what the SAMPLES tab shows, the places (the drives) or a folder's WAV files
 *    and its subfolders' (or, when they don't all fit, its subfolders), read for an instance and handed to it when
 *    ready. */
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "incipience.h"

#define LIB_FOLDERS 1024                 /* (1.1.0: FILE crosses folders, each one heard is one) */
#define LIB_SLOTS 128
#define LIB_BYTES (64u << 20)            /* decoded files kept, in all */
#define LIB_MAXFRAMES (60 * 48000)       /* a minute of a file at most */
#define LIB_MAXFILE (64L << 20)          /* bigger files aren't read */
#define LIB_MAXDIRS 600                  /* subfolders listed */
#define FLAT_DIRS 256                    /* folders looked in for a flat listing (more: folder by folder) */
#define FLAT_DEPTH 8

enum { FD_WANT, FD_READ };
enum { SL_FREE, SL_CLAIM, SL_WANT, SL_READY, SL_FAILED, SL_EVICT };

typedef struct {
    char *path;                          /* set before the folder count that includes it is published */
    int state;                           /* FD_*, atomic */
    int n;                               /* the WAV files, LIB_FILES at most (once FD_READ) */
    char **names;                        /* sorted (once FD_READ, then never changed) */
} folder_t;

typedef struct {
    int state, key, busy;                /* SL_*; folder << 8 | file; voices playing it. Atomics */
    uint32_t used;                       /* when last asked for (L.clock) */
    int16_t *d;
    int len;
    float rate;
} slot_t;

struct lib_br {
    int refs;                            /* the instance's, and one while a listing is on its way. Atomic */
    pthread_mutex_t mu;                  /* want and ready */
    char want[LIB_PATH];                 /* the folder to list, "" = the places */
    int want_gen;
    lib_list_t *ready;                   /* listed, waiting for the instance */
    int queued;                          /* in L.queue (under L.mu) */
    struct lib_br *next;
};

static struct {
    pthread_t th;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int refs, quit, pending;             /* pending: atomic, something to do since the thread last looked */
    char dir[512];                       /* the plugin's data folder (its samples/ folder is a place) */
    folder_t f[LIB_FOLDERS];
    int nf;                              /* atomic */
    slot_t s[LIB_SLOTS];
    uint32_t clock;
    size_t bytes;                        /* the thread's own */
    lib_br_t *queue;                     /* listings to read (under mu) */
} L = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};

static int ld(const int *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void st(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static int cas(int *p, int from, int to) {
    return __atomic_compare_exchange_n(p, &from, to, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

/* ---- names: WAV files, natural order ("Kick 2" before "Kick 10"), case aside ------------------------------------ */
int lib_is_wav(const char *name) {
    size_t n = strlen(name);
    return n > 4 && name[0] != '.' && !strcasecmp(name + n - 4, ".wav");
}

static int natcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            size_t la = 0, lb = 0;
            while (isdigit((unsigned char)a[la])) la++;
            while (isdigit((unsigned char)b[lb])) lb++;
            if (la != lb) return la < lb ? -1 : 1;
            int c = strncmp(a, b, la);
            if (c) return c;
            a += la, b += lb;
            continue;
        }
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x < y ? -1 : 1;
        a++, b++;
    }
    return *a ? 1 : *b ? -1 : 0;
}
static int natcmp_p(const void *a, const void *b) { return natcmp(*(char *const *)a, *(char *const *)b); }

static int is_dir(const char *path, const struct dirent *e) {
#ifdef DT_DIR
    if (e->d_type == DT_DIR) return 1;
    if (e->d_type != DT_UNKNOWN && e->d_type != DT_LNK) return 0;
#endif
    char p[LIB_PATH + 256];
    struct stat sb;
    snprintf(p, sizeof p, "%s/%s", path, e->d_name);
    return !stat(p, &sb) && S_ISDIR(sb.st_mode);
}

/* a folder's WAV files (sorted, LIB_FILES at most) and, if dirs, its subfolders (sorted); -1 if it can't be read */
static int read_dir(const char *path, char ***files, int *nfiles, char ***dirs, int *ndirs) {
    DIR *d = opendir(path);
    if (!d) return -1;
    int cf = 0, cd = 0, nf = 0, nd = 0;
    char **F = NULL, **D = NULL;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        int isd = dirs && is_dir(path, e);
        char ***arr = isd ? &D : &F;
        int *n = isd ? &nd : &nf, *cap = isd ? &cd : &cf;
        if (!isd && !lib_is_wav(e->d_name)) continue;
        if (*n >= (isd ? LIB_MAXDIRS : 4096)) continue;
        if (*n == *cap) {
            int c = *cap ? *cap * 2 : 64;
            char **g = realloc(*arr, sizeof *g * c);
            if (!g) continue;
            *arr = g;
            *cap = c;
        }
        (*arr)[(*n)++] = strdup(e->d_name);
    }
    closedir(d);
    if (nf > 1) qsort(F, nf, sizeof *F, natcmp_p);
    for (int i = LIB_FILES; i < nf; i++) free(F[i]);   /* SAMPLE reaches the first LIB_FILES */
    *files = F;
    *nfiles = nf < LIB_FILES ? nf : LIB_FILES;
    if (dirs) {
        if (nd > 1) qsort(D, nd, sizeof *D, natcmp_p);
        *dirs = D;
        *ndirs = nd;
    }
    return 0;
}

/* ---- WAV files: 8/16/24/32-bit PCM or 32-bit float, any rate, mixed to mono --------------------------------------- */
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

static float pcm(const uint8_t *p, int bits, int fl) {
    if (fl) {
        uint32_t u = u32(p);
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    switch (bits) {
    case 8: return (p[0] - 128) / 128.0f;
    case 16: return (int16_t)u16(p) / 32768.0f;
    case 24: return (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f;
    default: return (int32_t)u32(p) / 2147483648.0f;
    }
}

int lib_decode_wav(const uint8_t *b, long sz, int16_t **out, int *frames, float *rate) {
    if (sz < 44 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return -1;
    int ch = 0, bits = 0, fmt = 0;
    long p = 12;
    while (p + 8 <= sz) {
        uint32_t cl = u32(b + p + 4);
        if (!memcmp(b + p, "fmt ", 4) && cl >= 16 && p + 8 + 16 <= sz) {
            fmt = u16(b + p + 8);
            ch = u16(b + p + 10);
            *rate = (float)u32(b + p + 12);
            bits = u16(b + p + 22);
            if (fmt == 0xFFFE && cl >= 26 && p + 8 + 26 <= sz) fmt = u16(b + p + 8 + 24);   /* the subformat's own */
        } else if (!memcmp(b + p, "data", 4) && ch >= 1 && ch <= 8) {
            int fl = fmt == 3 && bits == 32;
            if (!(fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !fl) return -1;
            long avail = sz - (p + 8);
            if ((long)cl > avail) cl = (uint32_t)avail;
            int bpf = ch * bits / 8, n = (int)(cl / (uint32_t)bpf);
            if (n > LIB_MAXFRAMES) n = LIB_MAXFRAMES;
            if (n < 2 || *rate < 1000.0f) return -1;
            int16_t *o = malloc(sizeof *o * n);
            if (!o) return -1;
            const uint8_t *q = b + p + 8;
            for (int i = 0; i < n; i++, q += bpf) {
                float acc = 0.0f;
                for (int c = 0; c < ch; c++) acc += pcm(q + c * bits / 8, bits, fl);
                acc /= ch;
                acc = acc > 1.0f ? 1.0f : acc < -1.0f ? -1.0f : acc;
                o[i] = (int16_t)lrintf(acc * 32767.0f);
            }
            *out = o;
            *frames = n;
            return 0;
        }
        p += 8 + (long)cl + (cl & 1);
    }
    return -1;
}

static int load_file(const char *path, int16_t **out, int *frames, float *rate) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 44 || sz > LIB_MAXFILE) { fclose(f); return -1; }
    uint8_t *b = malloc((size_t)sz);
    size_t got = b ? fread(b, 1, (size_t)sz, f) : 0;
    fclose(f);
    int r = got == (size_t)sz ? lib_decode_wav(b, sz, out, frames, rate) : -1;
    free(b);
    return r;
}

/* ---- listings ------------------------------------------------------------------------------------------------------ */
void lib_list_free(lib_list_t *l) {
    if (!l) return;
    for (int i = 0; i < l->n; i++) {
        free(l->e[i].name);
        free(l->e[i].path);
    }
    for (int i = 0; i < l->ng; i++) free(l->g[i].path);
    free(l->e);
    free(l->g);
    free(l);
}

static int add(lib_list_t *l, int kind, const char *name, const char *path) {
    lib_ent_t *e = realloc(l->e, sizeof *e * (l->n + 1));
    if (!e) return -1;
    l->e = e;
    e[l->n].kind = kind;
    e[l->n].name = strdup(name);
    e[l->n].path = path ? strdup(path) : NULL;
    e[l->n].grp = -1;
    return l->n++;
}

static int add_grp(lib_list_t *l, const char *path) {   /* a folder whose files come next */
    lib_grp_t *g = realloc(l->g, sizeof *g * (l->ng + 1));
    if (!g) return -1;
    l->g = g;
    g[l->ng].path = strdup(path);
    g[l->ng].first = l->n;
    g[l->ng].n = 0;
    return l->ng++;
}

static void add_files(lib_list_t *l, int g, char **F, int nf) {
    for (int i = 0; g >= 0 && i < nf; i++) {
        int k = add(l, LE_FILE, F[i], NULL);
        if (k < 0) return;
        l->e[k].grp = g;
        l->g[g].n++;
        l->nfiles++;
    }
}

static void free_names(char **N, int n) {
    for (int i = 0; i < n; i++) free(N[i]);
    free(N);
}

/* the places: the AlphaBase set, the plugin's own samples folder, the MPC's drives (/media/<drive>: the internal one,
 * SD cards, SSDs, USB drives) */
static lib_list_t *list_places(void) {
    lib_list_t *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    add(l, LE_ALPHABASE, "SAMPLE SET BY NUMBER", NULL);   /* no maker's name on the screen */
    char p[LIB_PATH + 300];
    struct stat sb;
    snprintf(p, sizeof p, "%s/samples", L.dir);
    if (!stat(p, &sb) && S_ISDIR(sb.st_mode)) add(l, LE_PLACE, "INCIPIENCE SAMPLES", p);
    char **F = NULL, **D = NULL;
    int nf = 0, nd = 0;
    if (!read_dir(LIB_MEDIA, &F, &nf, &D, &nd)) {
        for (int i = 0; i < nd; i++) {
            /* az01-internal-sd is /sdcard (the internal drive again), acvs-* MPC's own read-only content */
            if (strcmp(D[i], "az01-internal-sd") && strncmp(D[i], "acvs", 4)) {
                snprintf(p, sizeof p, "%s/%s", LIB_MEDIA, D[i]);
                add(l, LE_PLACE, !strcmp(D[i], "az01-internal") ? "INTERNAL" : D[i], p);
            }
            free(D[i]);
        }
        for (int i = 0; i < nf; i++) free(F[i]);
    }
    free(F);
    free(D);
    l->first_file = l->n;
    return l;
}

/* 1.1.0 (owner, 2026-10-20: "can we just use the wheel to scroll through folders and sounds?"): the WAV files of path's
 * subfolders D, depth first in name order, each folder's under a heading (its path from the listing's folder, rel);
 * folders without any are left out. -1 once they don't all fit: LIB_FLAT entries, FLAT_DIRS folders looked in */
static int flat_sub(lib_list_t *l, const char *path, char **D, int nd, const char *rel, int depth, int *looked) {
    for (int i = 0; i < nd; i++) {
        char p[LIB_PATH], r[LIB_PATH];
        if (snprintf(p, sizeof p, "%s/%s", path, D[i]) >= (int)sizeof p) continue;
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", D[i]);
        if (++*looked > FLAT_DIRS) return -1;
        char **F = NULL, **S = NULL;
        int nf = 0, ns = 0, bad = 0;
        if (read_dir(p, &F, &nf, depth < FLAT_DEPTH ? &S : NULL, &ns)) continue;
        if (nf && l->n + 1 + nf > LIB_FLAT) bad = 1;
        else if (nf) {
            int h = add(l, LE_HEAD, r, p), g = add_grp(l, p);
            if (h >= 0) l->e[h].grp = g;
            add_files(l, g, F, nf);
        }
        if (!bad && ns) bad = flat_sub(l, p, S, ns, r, depth + 1, looked);
        free_names(F, nf);
        free_names(S, ns);
        if (bad) return -1;
    }
    return 0;
}

static lib_list_t *new_list(const char *path) {
    lib_list_t *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    snprintf(l->path, sizeof l->path, "%s", path);
    add(l, LE_UP, "..", NULL);
    return l;
}

static lib_list_t *list_folder(const char *path) {
    lib_list_t *l = new_list(path);
    if (!l) return NULL;
    char **F = NULL, **D = NULL;
    int nf = 0, nd = 0, looked = 0;
    if (read_dir(path, &F, &nf, &D, &nd)) {
        l->missing = 1;
        return l;
    }
    l->first_file = l->n;   /* flat: its own files, then its subfolders' */
    add_files(l, add_grp(l, path), F, nf);
    l->flat = 1;
    if (nd && flat_sub(l, path, D, nd, "", 1, &looked)) {   /* too many: its subfolders, then its own files */
        lib_list_free(l);
        if ((l = new_list(path))) {
            for (int i = 0; i < nd; i++) add(l, LE_DIR, D[i], NULL);
            l->first_file = l->n;
            add_files(l, add_grp(l, path), F, nf);
        }
    }
    free_names(F, nf);
    free_names(D, nd);
    return l;
}

/* ---- the thread ---------------------------------------------------------------------------------------------------- */
static void br_unref(lib_br_t *b) {
    if (__atomic_sub_fetch(&b->refs, 1, __ATOMIC_ACQ_REL)) return;
    lib_list_free(b->ready);
    pthread_mutex_destroy(&b->mu);
    free(b);
}

static uint32_t used(int i) { return __atomic_load_n(&L.s[i].used, __ATOMIC_RELAXED); }

/* past LIB_BYTES, or few slots free: the files longest unplayed that nothing holds go (a file that couldn't be read
 * first: it holds no memory, only its slot, and is tried again if asked for) */
static void make_room(void) {
    for (;;) {
        int nfree = 0, old = -1, bad = -1;
        for (int i = 0; i < LIB_SLOTS; i++) {
            int s = ld(&L.s[i].state);
            if (s == SL_FREE) nfree++;
            else if (s == SL_FAILED && (bad < 0 || (int32_t)(used(i) - used(bad)) < 0)) bad = i;
            else if (s == SL_READY && !ld(&L.s[i].busy) && (old < 0 || (int32_t)(used(i) - used(old)) < 0)) old = i;
        }
        if (nfree < 8 && bad >= 0) {
            st(&L.s[bad].state, SL_FREE);
            continue;
        }
        if ((L.bytes <= LIB_BYTES && nfree >= 8) || old < 0) return;
        slot_t *q = &L.s[old];
        if (!cas(&q->state, SL_READY, SL_EVICT)) continue;
        if (ld(&q->busy)) {   /* a voice took it meanwhile */
            st(&q->state, SL_READY);
            __atomic_store_n(&q->used, __atomic_load_n(&L.clock, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
            continue;
        }
        L.bytes -= sizeof *q->d * (size_t)q->len;
        free(q->d);
        q->d = NULL;
        q->len = 0;
        st(&q->state, SL_FREE);
    }
}

static void *thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&L.mu);
    for (;;) {
        while (!ld(&L.pending) && !L.quit) pthread_cond_wait(&L.cv, &L.mu);
        if (L.quit) break;
        st(&L.pending, 0);
        lib_br_t *q[32];
        int nq = 0;
        while (L.queue && nq < 32) {   /* (more: next time round) */
            q[nq] = L.queue;
            L.queue = q[nq]->next;
            q[nq++]->queued = 0;
        }
        if (L.queue) st(&L.pending, 1);
        int nf = ld(&L.nf);
        pthread_mutex_unlock(&L.mu);
        /* folders */
        for (int i = 0; i < nf; i++) {
            folder_t *f = &L.f[i];
            if (ld(&f->state) != FD_WANT) continue;
            char **F = NULL;
            int n = 0;
            if (read_dir(f->path, &F, &n, NULL, NULL)) n = 0;
            f->names = F;
            f->n = n;
            st(&f->state, FD_READ);
        }
        /* listings */
        for (int j = 0; j < nq; j++) {
            lib_br_t *b = q[j];
            char want[LIB_PATH];
            pthread_mutex_lock(&b->mu);
            snprintf(want, sizeof want, "%s", b->want);
            int gen = b->want_gen;
            pthread_mutex_unlock(&b->mu);
            lib_list_t *l = want[0] ? list_folder(want) : list_places();
            pthread_mutex_lock(&b->mu);
            if (l && gen == b->want_gen) {
                l->gen = gen;
                lib_list_free(b->ready);
                b->ready = l;
                l = NULL;
            }
            pthread_mutex_unlock(&b->mu);
            lib_list_free(l);
            br_unref(b);
        }
        /* files */
        for (int i = 0; i < LIB_SLOTS; i++) {
            slot_t *s = &L.s[i];
            if (ld(&s->state) != SL_WANT) continue;
            int key = ld(&s->key), fi = key >> 8, k = key & 255;
            folder_t *f = fi < ld(&L.nf) ? &L.f[fi] : NULL;
            if (!f) {
                st(&s->state, SL_FAILED);
                continue;
            }
            if (ld(&f->state) != FD_READ) continue;   /* its folder is read first (next time round) */
            char path[LIB_PATH + 256];
            int16_t *d = NULL;
            int len = 0;
            float rate = 44100.0f;
            if (k >= f->n) {
                st(&s->state, SL_FAILED);
                continue;
            }
            snprintf(path, sizeof path, "%s/%s", f->path, f->names[k]);
            if (load_file(path, &d, &len, &rate)) {
                st(&s->state, SL_FAILED);
                continue;
            }
            s->d = d;
            s->len = len;
            s->rate = rate;
            L.bytes += sizeof *d * (size_t)len;
            st(&s->state, SL_READY);
        }
        make_room();
        pthread_mutex_lock(&L.mu);
        for (int i = 0; i < LIB_SLOTS; i++)   /* a file waiting for its folder: round again */
            if (ld(&L.s[i].state) == SL_WANT) st(&L.pending, 1);
    }
    pthread_mutex_unlock(&L.mu);
    return NULL;
}

static void wake(void) {
    st(&L.pending, 1);
    pthread_cond_signal(&L.cv);
}

void lib_open(const char *dir) {
    pthread_mutex_lock(&L.mu);
    if (L.refs++ == 0) {
        snprintf(L.dir, sizeof L.dir, "%s", dir ? dir : ".");
        L.quit = 0;
        if (pthread_create(&L.th, NULL, thread, NULL)) L.refs = -1000;   /* no thread: no folders */
    }
    pthread_mutex_unlock(&L.mu);
}

void lib_close(void) {
    pthread_mutex_lock(&L.mu);
    int last = --L.refs == 0;
    if (last) {
        L.quit = 1;
        pthread_cond_signal(&L.cv);
    }
    pthread_mutex_unlock(&L.mu);
    if (!last) return;
    pthread_join(L.th, NULL);
    while (L.queue) {
        lib_br_t *b = L.queue;
        L.queue = b->next;
        br_unref(b);
    }
    for (int i = 0; i < ld(&L.nf); i++) {
        for (int k = 0; k < L.f[i].n; k++) free(L.f[i].names[k]);
        free(L.f[i].names);
        free(L.f[i].path);
        memset(&L.f[i], 0, sizeof L.f[i]);
    }
    st(&L.nf, 0);
    for (int i = 0; i < LIB_SLOTS; i++) {
        free(L.s[i].d);
        memset(&L.s[i], 0, sizeof L.s[i]);
    }
    L.bytes = 0;
}

/* wakes the thread if a request is waiting, without blocking: the audio thread's way (as pool_poke) */
void lib_poke(void) {
    if (!ld(&L.pending) || pthread_mutex_trylock(&L.mu)) return;
    pthread_cond_signal(&L.cv);
    pthread_mutex_unlock(&L.mu);
}

/* ---- folders ------------------------------------------------------------------------------------------------------- */
static int find_folder(const char *path) {   /* the newest with that path; L.mu held */
    for (int i = ld(&L.nf) - 1; i >= 0; i--)
        if (!strcmp(L.f[i].path, path)) return i;
    return -1;
}

static int new_folder(const char *path, char **names, int n) {   /* L.mu held; names: already read, or NULL */
    int i = ld(&L.nf);
    if (i >= LIB_FOLDERS) return -1;
    L.f[i].path = strdup(path);
    L.f[i].names = names;
    L.f[i].n = n;
    L.f[i].state = names ? FD_READ : FD_WANT;
    st(&L.nf, i + 1);
    return i;
}

int lib_folder(const char *path) {
    pthread_mutex_lock(&L.mu);
    int i = find_folder(path);
    if (i < 0) {
        i = new_folder(path, NULL, 0);
        if (i >= 0) wake();
    }
    pthread_mutex_unlock(&L.mu);
    return i;
}

int lib_folder_set(const lib_list_t *l, int g) {
    if (g < 0 || g >= l->ng) return -1;
    const lib_grp_t *G = &l->g[g];
    pthread_mutex_lock(&L.mu);
    int i = find_folder(G->path);
    if (i >= 0 && ld(&L.f[i].state) == FD_READ && L.f[i].n == G->n) {
        int same = 1;
        for (int k = 0; k < G->n && same; k++) same = !strcmp(L.f[i].names[k], l->e[G->first + k].name);
        if (same) {
            pthread_mutex_unlock(&L.mu);
            return i;
        }
    }
    char **names = calloc(G->n ? G->n : 1, sizeof *names);
    for (int k = 0; names && k < G->n; k++) names[k] = strdup(l->e[G->first + k].name);
    i = names ? new_folder(G->path, names, G->n) : -1;
    if (i < 0 && names) free_names(names, G->n);
    pthread_mutex_unlock(&L.mu);
    return i;
}

int lib_files(int f) { return f >= 0 && f < ld(&L.nf) && ld(&L.f[f].state) == FD_READ ? L.f[f].n : -1; }
const char *lib_name(int f, int k) { return lib_files(f) > k && k >= 0 ? L.f[f].names[k] : NULL; }
const char *lib_path(int f) { return f >= 0 && f < ld(&L.nf) ? L.f[f].path : NULL; }

/* ---- decoded files (the audio thread) ------------------------------------------------------------------------------ */
int lib_get(int f, int k, smp_t *out) {
    if (f < 0 || f >= LIB_FOLDERS || k < 0 || k >= LIB_FILES) return -1;
    int key = f << 8 | k, free_i = -1;
    uint32_t now = __atomic_add_fetch(&L.clock, 1, __ATOMIC_RELAXED);
    for (int i = 0; i < LIB_SLOTS; i++) {
        slot_t *s = &L.s[i];
        int state = ld(&s->state);
        if (state == SL_FREE) {
            if (free_i < 0) free_i = i;
            continue;
        }
        if (state < SL_WANT || state == SL_EVICT || ld(&s->key) != key) continue;
        if (state != SL_READY) return -1;   /* on its way, or a file that can't be read */
        __atomic_add_fetch(&s->busy, 1, __ATOMIC_SEQ_CST);
        if (ld(&s->state) != SL_READY || ld(&s->key) != key) {   /* being freed meanwhile */
            __atomic_sub_fetch(&s->busy, 1, __ATOMIC_SEQ_CST);
            return -1;
        }
        __atomic_store_n(&s->used, now, __ATOMIC_RELAXED);
        out->d = s->d;
        out->len = s->len;
        out->rate = s->rate;
        return i;
    }
    for (int i = free_i < 0 ? LIB_SLOTS : free_i; i < LIB_SLOTS; i++)
        if (cas(&L.s[i].state, SL_FREE, SL_CLAIM)) {
            st(&L.s[i].key, key);
            __atomic_store_n(&L.s[i].used, now, __ATOMIC_RELAXED);
            st(&L.s[i].state, SL_WANT);
            break;
        }
    st(&L.pending, 1);   /* (no slot free: the thread makes room, and the next hit asks again) */
    lib_poke();
    return -1;
}

void lib_put(int slot) {
    if (slot >= 0 && slot < LIB_SLOTS) __atomic_sub_fetch(&L.s[slot].busy, 1, __ATOMIC_SEQ_CST);
}

void lib_wait(void) {   /* offline tools: until nothing waits to be read */
    for (int tries = 0; tries < 2000; tries++) {
        int busy = 0;
        for (int i = 0; i < LIB_SLOTS; i++) busy |= ld(&L.s[i].state) == SL_WANT || ld(&L.s[i].state) == SL_CLAIM;
        for (int i = 0; i < ld(&L.nf); i++) busy |= ld(&L.f[i].state) == FD_WANT;
        pthread_mutex_lock(&L.mu);
        busy |= L.queue != NULL;
        if (busy) wake();
        pthread_mutex_unlock(&L.mu);
        if (!busy) return;
        struct timespec ts = {0, 2000000};
        nanosleep(&ts, NULL);
    }
}

/* ---- listings for an instance ---------------------------------------------------------------------------------------- */
lib_br_t *lib_browser(void) {
    lib_br_t *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->refs = 1;
    pthread_mutex_init(&b->mu, NULL);
    return b;
}

void lib_browser_free(lib_br_t *b) {
    if (b) br_unref(b);
}

void lib_browse(lib_br_t *b, const char *path) {
    pthread_mutex_lock(&b->mu);
    snprintf(b->want, sizeof b->want, "%s", path ? path : "");
    b->want_gen++;
    pthread_mutex_unlock(&b->mu);
    pthread_mutex_lock(&L.mu);
    if (!b->queued && L.refs > 0) {
        b->queued = 1;
        __atomic_add_fetch(&b->refs, 1, __ATOMIC_ACQ_REL);
        b->next = L.queue;
        L.queue = b;
    }
    wake();
    pthread_mutex_unlock(&L.mu);
}

lib_list_t *lib_listing(lib_br_t *b) {
    if (!b) return NULL;
    pthread_mutex_lock(&b->mu);
    lib_list_t *l = b->ready && b->ready->gen == b->want_gen ? b->ready : NULL;
    if (l) b->ready = NULL;
    pthread_mutex_unlock(&b->mu);
    return l;
}
