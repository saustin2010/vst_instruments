#ifndef MIDI_FX_API_V1_H
#define MIDI_FX_API_V1_H

/*
 * midi_fx_api_v1.h — Schwung MIDI FX plugin API
 *
 * Source: charlesvestal/schwung src/host/midi_fx_api_v1.h
 * Entry point symbol: "move_midi_fx_init"
 */

#include <stdint.h>

#define MIDI_FX_API_VERSION  1
#define MIDI_FX_MAX_OUT_MSGS 16
#define MIDI_FX_INIT_SYMBOL  "move_midi_fx_init"

struct host_api_v1;

typedef struct midi_fx_api_v1 {
    uint32_t api_version;  /* must be MIDI_FX_API_VERSION */

    void *(*create_instance)(const char *module_dir, const char *config_json);
    void  (*destroy_instance)(void *instance);

    int   (*process_midi)(void *instance,
                          const uint8_t *in_msg, int in_len,
                          uint8_t out_msgs[][3], int out_lens[],
                          int max_out);

    int   (*tick)(void *instance,
                  int frames, int sample_rate,
                  uint8_t out_msgs[][3], int out_lens[],
                  int max_out);

    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);

} midi_fx_api_v1_t;

typedef midi_fx_api_v1_t *(*midi_fx_init_fn)(const struct host_api_v1 *host);

#endif /* MIDI_FX_API_V1_H */
