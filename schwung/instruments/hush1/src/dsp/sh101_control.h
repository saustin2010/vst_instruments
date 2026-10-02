#ifndef SH101_CONTROL_H
#define SH101_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SH101_NOTE_PRIORITY_LAST = 0,
    SH101_NOTE_PRIORITY_LOWEST = 1
} sh101_note_priority_t;

typedef struct {
    int held[128];
    int order[16];
    int order_len;
    int current_note;
    int gate;
    int hold_enabled;
    int transpose;

    float sample_rate;
    float glide_ms;
    int glide_always;
    int glide_linear;
    float glide_alpha;
    float glide_step_hz;
    float pitch_current_hz;
    float pitch_target_hz;

    sh101_note_priority_t priority;
} sh101_control_t;

void sh101_control_init(sh101_control_t *ctrl, float sample_rate);
void sh101_control_set_priority(sh101_control_t *ctrl, sh101_note_priority_t priority);
void sh101_control_set_hold(sh101_control_t *ctrl, int hold_enabled);
void sh101_control_set_transpose(sh101_control_t *ctrl, int semitones);
void sh101_control_note_on(sh101_control_t *ctrl, int note, int velocity);
void sh101_control_note_off(sh101_control_t *ctrl, int note);
void sh101_control_all_notes_off(sh101_control_t *ctrl);
void sh101_control_tick_pitch(sh101_control_t *ctrl);

float sh101_midi_note_to_hz(int note);

#ifdef __cplusplus
}
#endif

#endif
