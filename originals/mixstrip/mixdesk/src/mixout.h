/* Mix Desk: its own MIDI port, for MPC's MIDI Learn (2026-11-07).
 *
 * An ALSA sequencer client "Mix Desk" with one output port, "MIDI Out", that anyone can subscribe to (READ |
 * SUBS_READ), so MPC lists it as a MIDI input ("Mix Desk MIDI Out"). In MPC's MIDI preferences it wants Control on
 * (MIDI Learn listens there) and Track off (no track should play it). Each column's fader and MUTE go out as fixed CCs
 * per MPC track (mixshm.h MIX_OUT_*), learned once in a template project, so a column moved to another track here
 * needs no new learning.
 *
 * Sent without blocking (a full queue drops the message) at the start of a block, on the audio thread, a way of sending
 * already proven on the MPC. libasound is dlopen()ed: no ALSA headers to build with; without it (offline tests)
 * nothing is sent, and every message is logged for the tests. */
#pragma once
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned char client, port; } mixout_addr_t;
typedef struct {   /* snd_seq_event_t, as libasound has it on 32-bit ARM */
    unsigned char type, flags, tag, queue;
    unsigned int time[2];
    mixout_addr_t source, dest;
    unsigned char data[12];
} mixout_ev_t;

typedef struct {
    void *seq, *enc;
    int port, tried;
    unsigned sent;      /* messages sent (or, offline, logged) */
    char log[512];      /* offline: "B0 66 7F;" per message, for the tests */
    int loglen;
} mixout_t;

static struct {
    int ok;
    int (*open)(void **, const char *, int, int);
    int (*set_name)(void *, const char *);
    int (*port)(void *, const char *, unsigned, unsigned);
    int (*nonblock)(void *, int);
    int (*out_direct)(void *, mixout_ev_t *);
    int (*close)(void *);
    int (*me_new)(size_t, void **);
    long (*me_encode)(void *, const unsigned char *, long, mixout_ev_t *);
    void (*me_reset)(void *);
    void (*me_free)(void *);
} mixout_A;

#define MIXOUT_SYM(f, n) (*(void **)(&mixout_A.f) = dlsym(h, n))
static inline void mixout_load(void) {
    if (mixout_A.ok) return;
    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { mixout_A.ok = -1; return; }
    MIXOUT_SYM(open, "snd_seq_open"); MIXOUT_SYM(set_name, "snd_seq_set_client_name");
    MIXOUT_SYM(port, "snd_seq_create_simple_port"); MIXOUT_SYM(nonblock, "snd_seq_nonblock");
    MIXOUT_SYM(out_direct, "snd_seq_event_output_direct"); MIXOUT_SYM(close, "snd_seq_close");
    MIXOUT_SYM(me_new, "snd_midi_event_new"); MIXOUT_SYM(me_encode, "snd_midi_event_encode");
    MIXOUT_SYM(me_reset, "snd_midi_event_reset_encode"); MIXOUT_SYM(me_free, "snd_midi_event_free");
    mixout_A.ok = (mixout_A.open && mixout_A.set_name && mixout_A.port && mixout_A.nonblock && mixout_A.out_direct &&
                   mixout_A.close && mixout_A.me_new && mixout_A.me_encode && mixout_A.me_reset && mixout_A.me_free) ? 1 : -1;
}

/* Opens the client and its port (once: a second call does nothing); p->seq NULL if ALSA isn't there. */
static __attribute__((unused)) void mixout_open(mixout_t *p, const char *name) {
    if (p->tried) return;
    p->tried = 1;
    mixout_load();
    if (mixout_A.ok != 1 || mixout_A.open(&p->seq, "default", 1 /* SND_SEQ_OPEN_OUTPUT */, 0) < 0) {
        p->seq = NULL;
        return;
    }
    mixout_A.set_name(p->seq, name);
    p->port = mixout_A.port(p->seq, "MIDI Out", (1u << 0) | (1u << 5) /* READ | SUBS_READ */,
                            (1u << 1) | (1u << 20) /* MIDI_GENERIC | APPLICATION */);
    if (p->port < 0 || mixout_A.nonblock(p->seq, 1) < 0 || mixout_A.me_new(64, &p->enc) < 0) {
        if (p->enc) mixout_A.me_free(p->enc);
        mixout_A.close(p->seq);
        p->seq = p->enc = NULL;
    }
}

/* A Control Change: ch 0-15 */
static __attribute__((unused)) void mixout_cc(mixout_t *p, int ch, int cc, int v) {
    unsigned char m[3] = {(unsigned char)(0xB0 | (ch & 15)), (unsigned char)(cc & 127), (unsigned char)(v < 0 ? 0 : v > 127 ? 127 : v)};
    p->sent++;
    if (!p->seq) {
        if (p->loglen < (int)sizeof p->log - 10)
            p->loglen += snprintf(p->log + p->loglen, sizeof p->log - p->loglen, "%02X %02X %02X;", m[0], m[1], m[2]);
        return;
    }
    mixout_ev_t ev;
    memset(&ev, 0, sizeof ev);
    mixout_A.me_reset(p->enc);
    if (mixout_A.me_encode(p->enc, m, 3, &ev) <= 0 || ev.type == 0) return;
    ev.source.port = (unsigned char)p->port;
    ev.dest.client = 254;   /* SND_SEQ_ADDRESS_SUBSCRIBERS */
    ev.dest.port = 253;
    ev.queue = 253;         /* SND_SEQ_QUEUE_DIRECT */
    mixout_A.out_direct(p->seq, &ev);
}

static __attribute__((unused)) void mixout_close(mixout_t *p) {
    if (p->enc) mixout_A.me_free(p->enc);
    if (p->seq) mixout_A.close(p->seq);
    memset(p, 0, sizeof *p);
}
