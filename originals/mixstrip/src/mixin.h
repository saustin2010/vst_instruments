/* Mix Strip and Mix Bus: reading the controllers plugged into the MPC (2026-10-25).
 *
 * The instance that is the listener (mixshm.h) opens an ALSA sequencer client, "Mix Strip" (or "Mix Bus"), with one
 * port that takes MIDI in, and connects every controller's MIDI output to it: each hardware MIDI device's readable
 * ports, from /proc/asound/seq/clients, looked at again every 3 s so a controller plugged in later is found. Left out:
 * the MPC's own ports (its pads, controls and MIDI DIN ports), ports named "DAW" (a controller's DAW-mode port) and
 * other programs' clients. So no MPC MIDI track is needed. MPC still gets the controller as before: an ALSA port can
 * have several readers.
 *
 * The port can't be subscribed to by anyone else (no SUBS_WRITE): MPC, which connects to new ports by itself, can't
 * send it its own MIDI, and shouldn't list it as an output.
 *
 * Read without blocking at the start of a block, on the audio thread, as Percolator's own MIDI port is (midiin.h);
 * the client is opened there too, when an instance becomes the listener (a few system calls, once). libasound is
 * dlopen()ed, so the build needs no ALSA headers; without it (offline tests) there are no controllers, and the
 * engines' midi() feeds the table instead.
 *
 * This header is in both plugins (originals/mixstrip/src and originals/mixstrip/mixbus/src): keep the two the same. */
#pragma once
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MIXIN_SCAN_FRAMES (44100 * 3)

typedef struct { void *seq, *dec; int port, ports, scan; } mixin_t;

static struct {
    int ok;
    int (*open)(void **, const char *, int, int);
    int (*set_name)(void *, const char *);
    int (*port)(void *, const char *, unsigned, unsigned);
    int (*nonblock)(void *, int);
    int (*input)(void *, void **);
    int (*connect_from)(void *, int, int, int);
    int (*close)(void *);
    int (*me_new)(size_t, void **);
    long (*me_decode)(void *, unsigned char *, long, const void *);
    void (*me_no_status)(void *, int);
    void (*me_free)(void *);
} mixin_A;

#define MIXIN_SYM(f, n) (*(void **)(&mixin_A.f) = dlsym(h, n))
static inline void mixin_load(void) {
    if (mixin_A.ok) return;
    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { mixin_A.ok = -1; return; }
    MIXIN_SYM(open, "snd_seq_open"); MIXIN_SYM(set_name, "snd_seq_set_client_name");
    MIXIN_SYM(port, "snd_seq_create_simple_port"); MIXIN_SYM(nonblock, "snd_seq_nonblock");
    MIXIN_SYM(input, "snd_seq_event_input"); MIXIN_SYM(connect_from, "snd_seq_connect_from");
    MIXIN_SYM(close, "snd_seq_close");
    MIXIN_SYM(me_new, "snd_midi_event_new"); MIXIN_SYM(me_decode, "snd_midi_event_decode");
    MIXIN_SYM(me_no_status, "snd_midi_event_no_status"); MIXIN_SYM(me_free, "snd_midi_event_free");
    mixin_A.ok = (mixin_A.open && mixin_A.set_name && mixin_A.port && mixin_A.nonblock && mixin_A.input &&
                  mixin_A.connect_from && mixin_A.close && mixin_A.me_new && mixin_A.me_decode &&
                  mixin_A.me_no_status && mixin_A.me_free) ? 1 : -1;
}

/* 1 where controllers can be read (libasound is there) */
static __attribute__((unused)) int mixin_available(void) {
    mixin_load();
    return mixin_A.ok == 1;
}

/* Connects every controller's readable ports to ours; how many there are. */
static int mixin_scan(mixin_t *p) {
    FILE *f = fopen("/proc/asound/seq/clients", "r");
    if (!f) return p->ports;
    char line[256], name[128];
    int client = -1, want = 0, n = 0;
    while (fgets(line, sizeof line, f)) {
        int c, port;
        char pname[128], caps[16];
        if (sscanf(line, "Client %d : \"%127[^\"]\"", &c, name) == 2) {
            client = c;
            want = strstr(line, "[Kernel") && strncmp(name, "MPC", 3) && strncmp(name, "Force", 5) &&
                   strncmp(name, "Akai Pro Force", 14) && strcmp(name, "System") && strncmp(name, "Midi Through", 12);
        } else if (want && sscanf(line, " Port %d : \"%127[^\"]\" (%15[^)])", &port, pname, caps) == 3 &&
                   caps[0] == 'R' && !strstr(pname, "DAW")) {
            mixin_A.connect_from(p->seq, p->port, client, port);   /* already connected: an error, harmless */
            n++;
        }
    }
    fclose(f);
    return p->ports = n;
}

/* Opens the client and connects the controllers; p->seq NULL if ALSA isn't there. */
static __attribute__((unused)) void mixin_open(mixin_t *p, const char *name) {
    memset(p, 0, sizeof *p);
    if (!mixin_available() || mixin_A.open(&p->seq, "default", 2 /* SND_SEQ_OPEN_INPUT */, 0) < 0) {
        p->seq = NULL;
        return;
    }
    mixin_A.set_name(p->seq, name);
    p->port = mixin_A.port(p->seq, "Controllers In", 1u << 1 /* WRITE, and no SUBS_WRITE */,
                           (1u << 1) | (1u << 20) /* MIDI_GENERIC | APPLICATION */);
    if (p->port < 0 || mixin_A.nonblock(p->seq, 1) < 0 || mixin_A.me_new(64, &p->dec) < 0) {
        if (p->dec) mixin_A.me_free(p->dec);
        mixin_A.close(p->seq);
        memset(p, 0, sizeof *p);
        return;
    }
    mixin_A.me_no_status(p->dec, 1);   /* every message with its status byte */
    mixin_scan(p);
}

/* Each block, while listening: now and then a look for new controllers. */
static __attribute__((unused)) void mixin_tick(mixin_t *p, int frames) {
    if (p->seq && (p->scan += frames) >= MIXIN_SCAN_FRAMES) {
        p->scan = 0;
        mixin_scan(p);
    }
}

/* The next channel message waiting into m (up to 3 bytes); its length, or 0 when none is waiting. */
static __attribute__((unused)) int mixin_read(mixin_t *p, uint8_t *m) {
    if (!p->seq) return 0;
    for (int tries = 0; tries < 64; tries++) {
        void *ev = NULL;
        int r = mixin_A.input(p->seq, &ev);
        if (r == -28 /* -ENOSPC: the queue overran and was emptied */) continue;
        if (r < 0 || !ev) return 0;   /* -EAGAIN: nothing waiting */
        unsigned char b[16];
        long n = mixin_A.me_decode(p->dec, b, sizeof b, ev);
        if (n >= 2 && n <= 3 && b[0] >= 0x80 && b[0] < 0xF0) {
            memcpy(m, b, (size_t)n);
            return (int)n;
        }
    }
    return 0;
}

static __attribute__((unused)) void mixin_close(mixin_t *p) {
    if (p->dec) mixin_A.me_free(p->dec);
    if (p->seq) mixin_A.close(p->seq);
    memset(p, 0, sizeof *p);
}
