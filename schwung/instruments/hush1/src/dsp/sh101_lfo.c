#include "sh101_lfo.h"

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void sh101_lfo_init(sh101_lfo_t *lfo, float sample_rate) {
    lfo->sample_rate = sample_rate;
    lfo->rate_hz = 5.0f;
    lfo->phase = 0.0f;
}

void sh101_lfo_set_rate_hz(sh101_lfo_t *lfo, float rate_hz) {
    lfo->rate_hz = clampf(rate_hz, 0.02f, 40.0f);
}

float sh101_lfo_process(sh101_lfo_t *lfo) {
    float inc = lfo->rate_hz / lfo->sample_rate;
    lfo->phase += inc;
    if (lfo->phase >= 1.0f) lfo->phase -= 1.0f;

    /* Triangle wave in [-1, 1] */
    float x = lfo->phase;
    float tri = (x < 0.5f) ? (x * 4.0f - 1.0f) : (3.0f - x * 4.0f);
    return tri;
}
