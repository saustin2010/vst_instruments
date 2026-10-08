/* Schwung audio FX module (audio_fx_api_v2) -> mpc_engine_t, for effect ports (vst.json "effect": true).
 * (steve/tools/audiofx, 2026-10-01; copied into each effect port's mpc/ folder by sync.sh.)
 *
 * The wrapper hands each 128-frame block of the track's audio to mpc_engine_input() (wrapper/engine.h) just before
 * asking for that block's output; render() runs the module's process_block() on it in place. Schwung's own
 * contract (44.1 kHz, 128-frame interleaved int16 stereo) is the engine interface's, so nothing is converted.
 * The module's host is a stub: logging off, no MIDI out, no modulation, clock "stopped". */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "params.h"
#include "host/plugin_api_v1.h"

#ifndef MODULE_DIR
#define MODULE_DIR ""
#endif

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *config_json);
    void (*destroy_instance)(void *instance);
    void (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
} audio_fx_api_v2_t;
extern audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static audio_fx_api_v2_t *api;
static host_api_v1_t host;

typedef struct {
    void *mod;
    int16_t in[2 * 256];
    int frames;
} inst_t;

static void h_log(const char *msg) { (void)msg; }
static int h_clock(void) { return 0; }

static void *create(const char *dir) {
    inst_t *in = calloc(1, sizeof *in);
    if (!in) return NULL;
    in->mod = api->create_instance(dir && dir[0] ? dir : MODULE_DIR, NULL);
    if (!in->mod) { free(in); return NULL; }
    return in;
}

static void destroy(void *p) {
    inst_t *in = p;
    if (!in) return;
    api->destroy_instance(in->mod);
    free(in);
}

static void midi(void *p, const uint8_t *m, int n) {
    inst_t *in = p;
    if (api->api_version >= 2 && api->on_midi) api->on_midi(in->mod, m, n, 2 /* external */);
}

static void set_param(void *p, const char *k, const char *v) { inst_t *in = p; api->set_param(in->mod, k, v); }
static int get_param(void *p, const char *k, char *b, int n) { inst_t *in = p; return api->get_param(in->mod, k, b, n); }

void mpc_engine_input(void *p, const int16_t *in_lr, int frames) {
    inst_t *in = p;
    if (frames > 256) frames = 256;
    memcpy(in->in, in_lr, (size_t)frames * 2 * sizeof(int16_t));
    in->frames = frames;
}

static void render(void *p, int16_t *out, int frames) {
    inst_t *in = p;
    if (frames > 256) frames = 256;
    if (in->frames == frames) memcpy(out, in->in, (size_t)frames * 2 * sizeof(int16_t));
    else memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
    in->frames = 0;
    api->process_block(in->mod, out, frames);
}

/* The newer wrapper's effect call (engine.h process()): this block's input, then render() on it. */
static void process(void *p, const int16_t *in_lr, int16_t *out_lr, int frames) {
    mpc_engine_input(p, in_lr, frames);
    render(p, out_lr, frames);
}

static const mpc_engine_t engine = { create, destroy, midi, set_param, get_param, render, process };

const mpc_engine_t *mpc_engine(void) {
    if (!api) {
        memset(&host, 0, sizeof host);
        host.api_version = 1;
        host.sample_rate = 44100;
        host.frames_per_block = 128;
        host.log = h_log;
        host.get_clock_status = h_clock;
        api = move_audio_fx_init_v2(&host);
    }
    return api ? &engine : NULL;
}
