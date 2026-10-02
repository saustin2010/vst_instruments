#ifndef PLUGIN_API_V1_H
#define PLUGIN_API_V1_H

/*
 * plugin_api_v1.h — Schwung host API (shared by all module types)
 *
 * Source: charlesvestal/schwung src/host/plugin_api_v1.h
 */

#include <stdint.h>

#define MOVE_CLOCK_STATUS_UNAVAILABLE 0
#define MOVE_CLOCK_STATUS_STOPPED     1
#define MOVE_CLOCK_STATUS_RUNNING     2

typedef int  (*move_mod_emit_value_fn)(void *ctx,
                                        const char *source_id,
                                        const char *target,
                                        const char *param,
                                        float signal,
                                        float depth,
                                        float offset,
                                        int bipolar,
                                        int enabled);
typedef void (*move_mod_clear_source_fn)(void *ctx, const char *source_id);

typedef struct host_api_v1 {
    uint32_t api_version;

    int sample_rate;
    int frames_per_block;

    uint8_t *mapped_memory;
    int      audio_out_offset;
    int      audio_in_offset;

    void (*log)(const char *msg);

    /* 4-byte USB-MIDI packets: [cable|CIN, status, data1, data2] */
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);

    int (*get_clock_status)(void);  /* returns MOVE_CLOCK_STATUS_* */

    move_mod_emit_value_fn   mod_emit_value;   /* may be NULL */
    move_mod_clear_source_fn mod_clear_source; /* may be NULL */
    void                    *mod_host_ctx;

    float (*get_bpm)(void);  /* may be NULL; returns current BPM */

} host_api_v1_t;

#endif /* PLUGIN_API_V1_H */
