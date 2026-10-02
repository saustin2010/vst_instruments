/* A plugin's own MIDI output port on MPC OS (steve/tools/midiout, 2026-10-02; from steve/tools/midifx's adapter).
 * MPC OS ignores a VST's MIDI output, so a plugin that plays or modulates other tracks opens an ALSA sequencer port
 * (client = its name, port "MIDI Out"; a second instance "<name> 2"). MPC subscribes to new ports by itself (verified
 * on a Live II 2026-10-01); another track picks it as its MIDI input. libasound is dlopen()ed on the device, so the
 * build needs no ALSA headers; without it (offline tests) nothing is sent, and MIDIOUT_DEBUG=1 prints what would be.
 * Header-only, C or C++. */
#pragma once
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char client, port; } mo_addr_t;
typedef struct {   /* the kernel's stable snd_seq_event_t layout (28 bytes) */
    unsigned char type, flags, tag, queue;
    unsigned int time[2];
    mo_addr_t source, dest;
    unsigned char data[12];
} mo_ev_t;

typedef struct { void *seq, *enc; int port; } mo_port_t;

static struct {
    int ok;
    int (*open)(void **, const char *, int, int);
    int (*set_name)(void *, const char *);
    int (*port)(void *, const char *, unsigned, unsigned);
    int (*nonblock)(void *, int);
    int (*out_direct)(void *, mo_ev_t *);
    int (*close)(void *);
    int (*me_new)(size_t, void **);
    long (*me_encode)(void *, const unsigned char *, long, mo_ev_t *);
    void (*me_reset)(void *);
    void (*me_free)(void *);
} mo_A;

#define MO_SYM(f, n) (*(void **)(&mo_A.f) = dlsym(h, n))
static inline void mo_load(void) {
    if (mo_A.ok) return;
    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { mo_A.ok = -1; return; }
    MO_SYM(open, "snd_seq_open"); MO_SYM(set_name, "snd_seq_set_client_name");
    MO_SYM(port, "snd_seq_create_simple_port"); MO_SYM(nonblock, "snd_seq_nonblock");
    MO_SYM(out_direct, "snd_seq_event_output_direct"); MO_SYM(close, "snd_seq_close");
    MO_SYM(me_new, "snd_midi_event_new"); MO_SYM(me_encode, "snd_midi_event_encode");
    MO_SYM(me_reset, "snd_midi_event_reset_encode"); MO_SYM(me_free, "snd_midi_event_free");
    mo_A.ok = (mo_A.open && mo_A.set_name && mo_A.port && mo_A.out_direct && mo_A.close && mo_A.me_new &&
               mo_A.me_encode && mo_A.me_reset && mo_A.me_free) ? 1 : -1;
}

static int mo_instances;
static inline void mo_open(mo_port_t *p, const char *name) {
    memset(p, 0, sizeof *p);
    mo_load();
    if (mo_A.ok != 1 || mo_A.open(&p->seq, "default", 1 /* SND_SEQ_OPEN_OUTPUT */, 0) < 0) { p->seq = NULL; return; }
    char n[64];
    int k = ++mo_instances;
    if (k > 1) snprintf(n, sizeof n, "%s %d", name, k);
    else snprintf(n, sizeof n, "%s", name);
    mo_A.set_name(p->seq, n);
    p->port = mo_A.port(p->seq, "MIDI Out", (1u << 0) | (1u << 5) /* READ | SUBS_READ */,
                        (1u << 1) | (1u << 20) /* MIDI_GENERIC | APPLICATION */);
    if (mo_A.nonblock) mo_A.nonblock(p->seq, 1);
    if (p->port < 0 || mo_A.me_new(64, &p->enc) < 0) { mo_A.close(p->seq); p->seq = NULL; }
}

static inline void mo_send(mo_port_t *p, const uint8_t *m, int len) {
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("MIDIOUT_DEBUG") != NULL;
    if (dbg) fprintf(stderr, "midi out: %02X %02X %02X\n", m[0], len > 1 ? m[1] : 0, len > 2 ? m[2] : 0);
    if (!p->seq) return;
    mo_ev_t ev;
    memset(&ev, 0, sizeof ev);
    mo_A.me_reset(p->enc);
    if (mo_A.me_encode(p->enc, m, len, &ev) <= 0 || ev.type == 0) return;
    ev.source.port = (unsigned char)p->port;
    ev.dest.client = 254;   /* SND_SEQ_ADDRESS_SUBSCRIBERS */
    ev.dest.port = 253;     /* SND_SEQ_ADDRESS_UNKNOWN */
    ev.queue = 253;         /* SND_SEQ_QUEUE_DIRECT */
    mo_A.out_direct(p->seq, &ev);
}

static inline void mo_close(mo_port_t *p) {
    if (p->enc) mo_A.me_free(p->enc);
    if (p->seq) mo_A.close(p->seq);
    memset(p, 0, sizeof *p);
}
