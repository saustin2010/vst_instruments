/* Schwung MIDI FX module (midi_fx_api_v1) -> mpc_engine_t, for sequencers, arpeggiators and chord generators that
 * play OTHER MPC tracks. (steve/tools/midifx; copied into each sequencer port's mpc/ folder by setup.)
 *
 * - MPC OS ignores a VST's own MIDI output, so everything the module emits goes out an ALSA sequencer port named after
 *   the plugin ("<name> MIDI Out"; a second instance is "<name> 2"). MPC subscribes to new ports by itself (verified
 *   on a stock Live II 2026-10-01); pick that port as another track's MIDI input.
 * - The host transport (engine.h's mpc_engine_transport hook) becomes 24-PPQN MIDI clock plus Start/Stop fed to the
 *   module's process_midi, and answers the module's get_bpm / get_clock_status / get_beat_position.
 * - Notes played on the plugin's own track go to the module (arpeggiators and chord generators use them).
 * - Audio out is silence.
 * libasound is loaded at run time from the device (no ALSA headers needed to build); the event struct below is the
 * kernel's stable snd_seq_event_t layout (28 bytes). */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "params.h"
#include "host/plugin_api_v1.h"
#include "host/midi_fx_api_v1.h"

#ifndef MODULE_DIR
#define MODULE_DIR ""
#endif
#define SR 44100
#define MAX_OUT MIDI_FX_MAX_OUT_MSGS

extern midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host);

/* ---- ALSA sequencer, loaded with dlopen ------------------------------------------------------------------------ */
typedef struct { unsigned char client, port; } seq_addr_t;
typedef struct {
    unsigned char type, flags, tag, queue;
    unsigned int time[2];
    seq_addr_t source, dest;
    unsigned char data[12];
} seq_ev_t;
static struct {
    int ok;
    int (*open)(void **, const char *, int, int);
    int (*set_name)(void *, const char *);
    int (*port)(void *, const char *, unsigned, unsigned);
    int (*nonblock)(void *, int);
    int (*out_direct)(void *, seq_ev_t *);
    int (*close)(void *);
    int (*me_new)(size_t, void **);
    long (*me_encode)(void *, const unsigned char *, long, seq_ev_t *);
    void (*me_reset)(void *);
    void (*me_free)(void *);
} A;

static void alsa_load(void) {
    if (A.ok) return;
    void *h = dlopen("libasound.so.2", RTLD_NOW);
    if (!h) { A.ok = -1; return; }
    A.open = dlsym(h, "snd_seq_open");
    A.set_name = dlsym(h, "snd_seq_set_client_name");
    A.port = dlsym(h, "snd_seq_create_simple_port");
    A.nonblock = dlsym(h, "snd_seq_nonblock");
    A.out_direct = dlsym(h, "snd_seq_event_output_direct");
    A.close = dlsym(h, "snd_seq_close");
    A.me_new = dlsym(h, "snd_midi_event_new");
    A.me_encode = dlsym(h, "snd_midi_event_encode");
    A.me_reset = dlsym(h, "snd_midi_event_reset_encode");
    A.me_free = dlsym(h, "snd_midi_event_free");
    A.ok = (A.open && A.set_name && A.port && A.out_direct && A.close && A.me_new && A.me_encode && A.me_reset && A.me_free) ? 1 : -1;
}

/* ---- instance ---------------------------------------------------------------------------------------------------- */
typedef struct {
    void *mod;
    void *seq, *enc;
    int port;
    double bpm, ppq;        /* tempo; position in quarter notes (advanced per block between host updates) */
    int playing, running;    /* host says playing; we have sent Start */
    long clock;              /* 24-PPQN ticks sent since Start */
} inst_t;

static midi_fx_api_v1_t *api;
static host_api_v1_t host;
static double g_bpm = 120.0, g_ppq = -1;
static int g_running;
static int instances;

static int dbg = -1;   /* MIDIFX_DEBUG=1: print what goes out (offline tests: no ALSA there) */
static void send_out(inst_t *in, const uint8_t *m, int len) {
    if (len < 1 || m[0] >= 0xF8) return;   /* realtime messages are ours to the module, not output */
    if (dbg < 0) dbg = getenv("MIDIFX_DEBUG") != NULL;
    if (dbg) fprintf(stderr, "midi out:%s %02X %02X %02X\n", in->running ? "" : " (stopped)", m[0], len > 1 ? m[1] : 0, len > 2 ? m[2] : 0);
    if (!in->seq) return;
    seq_ev_t ev;
    memset(&ev, 0, sizeof ev);
    A.me_reset(in->enc);
    if (A.me_encode(in->enc, m, len, &ev) <= 0 || ev.type == 0) return;
    ev.source.port = (unsigned char)in->port;
    ev.dest.client = 254;   /* SND_SEQ_ADDRESS_SUBSCRIBERS */
    ev.dest.port = 253;     /* SND_SEQ_ADDRESS_UNKNOWN */
    ev.queue = 253;         /* SND_SEQ_QUEUE_DIRECT */
    A.out_direct(in->seq, &ev);
}

static void emit(inst_t *in, uint8_t out[][3], int lens[], int n) {
    for (int i = 0; i < n; i++) send_out(in, out[i], lens[i]);
}

static void feed(inst_t *in, const uint8_t *m, int len) {
    uint8_t out[MAX_OUT][3];
    int lens[MAX_OUT];
    emit(in, out, lens, api->process_midi(in->mod, m, len, out, lens, MAX_OUT));
}

/* host callbacks (one host, so the transport is global; the current instance's values are mirrored in) */
static void h_log(const char *msg) { (void)msg; }
static inst_t *g_cur;   /* the instance inside a module call, for the send callbacks */
static int h_send(const uint8_t *pkt, int len) {   /* USB-MIDI packet [cable|CIN, status, d1, d2] */
    if (!g_cur || len < 4) return 0;
    int n = (pkt[1] >= 0xC0 && pkt[1] < 0xE0) ? 2 : 3;
    send_out(g_cur, pkt + 1, n);
    return len;
}
static int h_clock_status(void) { return g_running ? MOVE_CLOCK_STATUS_RUNNING : MOVE_CLOCK_STATUS_STOPPED; }
static float h_bpm(void) { return (float)g_bpm; }
static double h_beat(void) { return g_running ? g_ppq : -1.0; }
static int h_recv_channel(void *i) { (void)i; return -1; }

static void *create(const char *dir) {
    if (!api) return NULL;
    inst_t *in = calloc(1, sizeof *in);
    if (!in) return NULL;
    in->bpm = 120;
    in->ppq = -1;
    g_cur = in;
    in->mod = api->create_instance(dir && dir[0] ? dir : MODULE_DIR, NULL);
    g_cur = NULL;
    if (!in->mod) { free(in); return NULL; }
#ifdef MIDIFX_INIT   /* per-port start-up settings for the MPC, "key=val;key=val" (vst.json "defines"), e.g. sync=clock */
    {
        char init[256], *save = NULL;
        snprintf(init, sizeof init, "%s", MIDIFX_INIT);
        for (char *kv = strtok_r(init, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
            char *eq = strchr(kv, '=');
            if (!eq) continue;
            *eq = 0;
            api->set_param(in->mod, kv, eq + 1);
        }
    }
#endif
    alsa_load();
    if (A.ok == 1 && A.open(&in->seq, "default", 1 /* SND_SEQ_OPEN_OUTPUT */, 0) >= 0) {
        char name[64];
        int n = ++instances;
        if (n > 1) snprintf(name, sizeof name, "%s %d", PLUG_NAME, n);
        else snprintf(name, sizeof name, "%s", PLUG_NAME);
        A.set_name(in->seq, name);
        in->port = A.port(in->seq, "MIDI Out", (1u << 0) | (1u << 5) /* READ | SUBS_READ */,
                          (1u << 1) | (1u << 20) /* MIDI_GENERIC | APPLICATION */);
        if (A.nonblock) A.nonblock(in->seq, 1);
        if (in->port < 0 || A.me_new(64, &in->enc) < 0) { A.close(in->seq); in->seq = NULL; }
    }
    return in;
}

static void destroy(void *p) {
    inst_t *in = p;
    if (!in) return;
    if (in->running) { uint8_t stop = 0xFC; g_cur = in; feed(in, &stop, 1); g_cur = NULL; }
    api->destroy_instance(in->mod);
    if (in->enc) A.me_free(in->enc);
    if (in->seq) A.close(in->seq);
    free(in);
}

static void midi(void *p, const uint8_t *m, int len) {
    inst_t *in = p;
    g_cur = in;
    feed(in, m, len);
    g_cur = NULL;
}

static void set_param(void *p, const char *k, const char *v) { inst_t *in = p; g_cur = in; api->set_param(in->mod, k, v); g_cur = NULL; }
static int get_param(void *p, const char *k, char *b, int n) { inst_t *in = p; return api->get_param ? api->get_param(in->mod, k, b, n) : -1; }

/* Transport from the wrapper (once per host buffer): start/stop, and resync the position after a jump. */
void mpc_engine_transport(void *p, double bpm, double ppq, int playing) {
    inst_t *in = p;
    if (!in) return;
    if (bpm > 0) in->bpm = bpm;
    g_cur = in;
    if (playing && !in->running) {
        in->ppq = ppq >= 0 ? ppq : 0;
        in->clock = 0;
        in->running = 1;
        uint8_t start = 0xFA;
        feed(in, &start, 1);
        uint8_t tick = 0xF8;   /* the first clock coincides with Start */
        feed(in, &tick, 1);
        in->clock = 1;
    } else if (!playing && in->running) {
        in->running = 0;
        uint8_t stop = 0xFC;
        feed(in, &stop, 1);
    } else if (playing && ppq >= 0 && fabs(ppq - in->ppq) > 1.0 / 24) {
        in->ppq = ppq;   /* loop or locate: follow the host; the module keeps counting clocks */
    }
    in->playing = playing;
    g_cur = NULL;
}

static void render(void *p, int16_t *out, int frames) {
    inst_t *in = p;
    memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
    g_cur = in;
    g_bpm = in->bpm;
    g_running = in->running;
    g_ppq = in->ppq;
    if (in->running) {   /* clock ticks that fall inside this block */
        double start = in->ppq, end = start + frames * in->bpm / (60.0 * SR);
        long first = (long)floor(start * 24) + 1, last = (long)floor(end * 24);
        uint8_t tick = 0xF8;
        for (long t = first; t <= last; t++) feed(in, &tick, 1);
        in->ppq = end;
        g_ppq = end;
    }
    uint8_t outm[MAX_OUT][3];
    int lens[MAX_OUT];
    emit(in, outm, lens, api->tick(in->mod, frames, SR, outm, lens, MAX_OUT));
    g_cur = NULL;
}

static const mpc_engine_t engine = { create, destroy, midi, set_param, get_param, render };

const mpc_engine_t *mpc_engine(void) {
    if (!api) {
        memset(&host, 0, sizeof host);
        host.api_version = 1;
        host.sample_rate = SR;
        host.frames_per_block = 128;
        host.log = h_log;
        host.midi_send_internal = h_send;
        host.midi_send_external = h_send;
        host.midi_inject_to_move = h_send;
        host.get_clock_status = h_clock_status;
        host.get_bpm = h_bpm;
        host.slot_recv_channel = h_recv_channel;
        host.get_beat_position = h_beat;
        api = move_midi_fx_init(&host);
    }
    return api ? &engine : NULL;
}
