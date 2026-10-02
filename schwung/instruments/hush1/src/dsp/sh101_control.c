#include "sh101_control.h"

#include <math.h>
#include <string.h>

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float sh101_midi_note_to_hz(int note) {
    return 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
}

static void recalc_glide_alpha(sh101_control_t *ctrl) {
    if (ctrl->glide_ms <= 0.01f) {
        ctrl->glide_alpha = 1.0f;
        return;
    }
    float t = ctrl->glide_ms * 0.001f;
    float tau = t * ctrl->sample_rate;
    if (tau < 1.0f) tau = 1.0f;
    ctrl->glide_alpha = 1.0f - expf(-1.0f / tau);
}

static void remove_from_order(sh101_control_t *ctrl, int note) {
    int j = 0;
    for (int i = 0; i < ctrl->order_len; ++i) {
        if (ctrl->order[i] != note) {
            ctrl->order[j++] = ctrl->order[i];
        }
    }
    ctrl->order_len = j;
}

static void push_order(sh101_control_t *ctrl, int note) {
    remove_from_order(ctrl, note);
    if (ctrl->order_len < (int)(sizeof(ctrl->order) / sizeof(ctrl->order[0]))) {
        ctrl->order[ctrl->order_len++] = note;
        return;
    }
    memmove(ctrl->order, ctrl->order + 1, (size_t)(ctrl->order_len - 1) * sizeof(ctrl->order[0]));
    ctrl->order[ctrl->order_len - 1] = note;
}

static int pick_note(const sh101_control_t *ctrl) {
    if (ctrl->priority == SH101_NOTE_PRIORITY_LOWEST) {
        for (int n = 0; n < 128; ++n) {
            if (ctrl->held[n]) return n;
        }
        return -1;
    }

    for (int i = ctrl->order_len - 1; i >= 0; --i) {
        int n = ctrl->order[i];
        if (n >= 0 && n < 128 && ctrl->held[n]) return n;
    }
    return -1;
}

static void update_target_note(sh101_control_t *ctrl, int new_note) {
    ctrl->current_note = new_note;
    if (new_note < 0) {
        ctrl->gate = ctrl->hold_enabled ? ctrl->gate : 0;
        ctrl->glide_step_hz = 0.0f;
        return;
    }

    int effective_note = clamp_int(new_note + ctrl->transpose, 0, 127);
    ctrl->pitch_target_hz = sh101_midi_note_to_hz(effective_note);
    if ((!ctrl->gate && !ctrl->glide_always) || ctrl->glide_ms <= 0.01f) {
        ctrl->pitch_current_hz = ctrl->pitch_target_hz;
        ctrl->glide_step_hz = 0.0f;
    } else if (ctrl->glide_linear) {
        float tau = ctrl->glide_ms * 0.001f * ctrl->sample_rate;
        if (tau < 1.0f) tau = 1.0f;
        ctrl->glide_step_hz = (ctrl->pitch_target_hz - ctrl->pitch_current_hz) / tau;
    }
    ctrl->gate = 1;
}

void sh101_control_init(sh101_control_t *ctrl, float sample_rate) {
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->sample_rate = sample_rate;
    ctrl->current_note = -1;
    ctrl->pitch_current_hz = sh101_midi_note_to_hz(60);
    ctrl->pitch_target_hz = ctrl->pitch_current_hz;
    ctrl->priority = SH101_NOTE_PRIORITY_LAST;
    ctrl->glide_ms = 0.0f;
    ctrl->glide_always = 0;
    ctrl->glide_linear = 0;
    ctrl->glide_step_hz = 0.0f;
    recalc_glide_alpha(ctrl);
}

void sh101_control_set_priority(sh101_control_t *ctrl, sh101_note_priority_t priority) {
    ctrl->priority = priority;
    update_target_note(ctrl, pick_note(ctrl));
}

void sh101_control_set_hold(sh101_control_t *ctrl, int hold_enabled) {
    ctrl->hold_enabled = hold_enabled ? 1 : 0;
    if (!ctrl->hold_enabled && pick_note(ctrl) < 0) {
        ctrl->gate = 0;
        ctrl->current_note = -1;
    }
}

void sh101_control_set_transpose(sh101_control_t *ctrl, int semitones) {
    ctrl->transpose = clamp_int(semitones, -24, 24);
    if (ctrl->current_note >= 0) {
        update_target_note(ctrl, ctrl->current_note);
    }
}

void sh101_control_note_on(sh101_control_t *ctrl, int note, int velocity) {
    (void)velocity;
    if (note < 0 || note > 127) return;

    ctrl->held[note] = 1;
    push_order(ctrl, note);
    recalc_glide_alpha(ctrl);
    update_target_note(ctrl, pick_note(ctrl));
}

void sh101_control_note_off(sh101_control_t *ctrl, int note) {
    if (note < 0 || note > 127) return;

    ctrl->held[note] = 0;
    remove_from_order(ctrl, note);

    int next_note = pick_note(ctrl);
    if (next_note < 0) {
        if (!ctrl->hold_enabled) {
            ctrl->gate = 0;
            ctrl->current_note = -1;
        }
        return;
    }
    update_target_note(ctrl, next_note);
}

void sh101_control_all_notes_off(sh101_control_t *ctrl) {
    memset(ctrl->held, 0, sizeof(ctrl->held));
    ctrl->order_len = 0;
    if (!ctrl->hold_enabled) {
        ctrl->gate = 0;
        ctrl->current_note = -1;
    }
}

void sh101_control_tick_pitch(sh101_control_t *ctrl) {
    recalc_glide_alpha(ctrl);
    if (ctrl->glide_linear && ctrl->glide_ms > 0.01f) {
        float next = ctrl->pitch_current_hz + ctrl->glide_step_hz;
        if ((ctrl->glide_step_hz >= 0.0f && next >= ctrl->pitch_target_hz) ||
            (ctrl->glide_step_hz < 0.0f && next <= ctrl->pitch_target_hz)) {
            ctrl->pitch_current_hz = ctrl->pitch_target_hz;
        } else {
            ctrl->pitch_current_hz = next;
        }
        return;
    }
    {
        float d = ctrl->pitch_target_hz - ctrl->pitch_current_hz;
        ctrl->pitch_current_hz += d * ctrl->glide_alpha;
    }
}
