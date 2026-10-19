/* Incipience's own MIDI input port, "Incipience MIDI In" in MPC's lists (2026-10-17; Percolator's, src/midiin.h there).
 *
 * MPC hands a plugin on its track every note-on on channel 1, whatever channel it was sent on (traced on a Live II),
 * so KEYS CH, a MIDI channel per instrument, can't work through the track. Instead a MIDI track sends to this port
 * (its MIDI OUTPUT PORT), on the channel it plays, as it would to a hardware drum machine: an ALSA sequencer client
 * named after the plugin ("Incipience", a second instance "Incipience 2") with one writable port, "MIDI In". MPC sees
 * new ports by itself and lists each under its client's name ("Incipience MIDI In"), as it does Percolator's.
 *
 * Read without blocking at the start of each block, on the audio thread (one read() when nothing is waiting); no
 * thread of its own. libasound is dlopen()ed on the device, so the build needs no ALSA headers; without it (offline
 * tests) there is no port. Shaped like the framework's alsa_midi_out.h (dev-tools/midiout). */
#pragma once
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { void *seq, *dec; int port; } mi_port_t;

static struct {
    int ok;
    int (*open)(void **, const char *, int, int);
    int (*set_name)(void *, const char *);
    int (*port)(void *, const char *, unsigned, unsigned);
    int (*nonblock)(void *, int);
    int (*input)(void *, void **);
    int (*close)(void *);
    int (*me_new)(size_t, void **);
    long (*me_decode)(void *, unsigned char *, long, const void *);
    void (*me_no_status)(void *, int);
    void (*me_free)(void *);
} mi_A;

#define MI_SYM(f, n) (*(void **)(&mi_A.f) = dlsym(h, n))
static inline void mi_load(void) {
    if (mi_A.ok) return;
    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { mi_A.ok = -1; return; }
    MI_SYM(open, "snd_seq_open"); MI_SYM(set_name, "snd_seq_set_client_name");
    MI_SYM(port, "snd_seq_create_simple_port"); MI_SYM(nonblock, "snd_seq_nonblock");
    MI_SYM(input, "snd_seq_event_input"); MI_SYM(close, "snd_seq_close");
    MI_SYM(me_new, "snd_midi_event_new"); MI_SYM(me_decode, "snd_midi_event_decode");
    MI_SYM(me_no_status, "snd_midi_event_no_status"); MI_SYM(me_free, "snd_midi_event_free");
    mi_A.ok = (mi_A.open && mi_A.set_name && mi_A.port && mi_A.nonblock && mi_A.input && mi_A.close && mi_A.me_new &&
               mi_A.me_decode && mi_A.me_no_status && mi_A.me_free) ? 1 : -1;
}

/* On the message thread (create). No port (p->seq NULL) if ALSA isn't there. */
static int mi_instances;
static __attribute__((unused)) void mi_open(mi_port_t *p, const char *name) {
    memset(p, 0, sizeof *p);
    mi_load();
    if (mi_A.ok != 1 || mi_A.open(&p->seq, "default", 2 /* SND_SEQ_OPEN_INPUT */, 0) < 0) { p->seq = NULL; return; }
    char n[64];
    int k = __atomic_add_fetch(&mi_instances, 1, __ATOMIC_RELAXED);
    if (k > 1) snprintf(n, sizeof n, "%s %d", name, k);
    else snprintf(n, sizeof n, "%s", name);
    mi_A.set_name(p->seq, n);
    p->port = mi_A.port(p->seq, "MIDI In", (1u << 1) | (1u << 6) /* WRITE | SUBS_WRITE */,
                        (1u << 1) | (1u << 20) /* MIDI_GENERIC | APPLICATION */);
    if (p->port < 0 || mi_A.nonblock(p->seq, 1) < 0 || mi_A.me_new(64, &p->dec) < 0) {
        mi_A.close(p->seq);
        p->seq = NULL;
        return;
    }
    mi_A.me_no_status(p->dec, 1);   /* every message with its status byte (no running status) */
}

/* The next channel message waiting at the port into m (up to 3 bytes); its length, or 0 when none is waiting. Other
 * events (subscriptions, clock, SysEx) are skipped. */
static __attribute__((unused)) int mi_read(mi_port_t *p, uint8_t *m) {
    if (!p->seq) return 0;
    for (int tries = 0; tries < 64; tries++) {
        void *ev = NULL;
        int r = mi_A.input(p->seq, &ev);
        if (r == -28 /* -ENOSPC: the queue overran and was emptied */) continue;
        if (r < 0 || !ev) return 0;   /* -EAGAIN: nothing waiting */
        unsigned char b[16];
        long n = mi_A.me_decode(p->dec, b, sizeof b, ev);
        if (n >= 2 && n <= 3 && b[0] >= 0x80 && b[0] < 0xF0) {
            memcpy(m, b, (size_t)n);
            return (int)n;
        }
    }
    return 0;
}

static __attribute__((unused)) void mi_close(mi_port_t *p) {
    if (p->dec) mi_A.me_free(p->dec);
    if (p->seq) mi_A.close(p->seq);
    memset(p, 0, sizeof *p);
}
