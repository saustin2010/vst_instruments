#include "sh101_filter.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float sat(float x) {
    return tanhf(1.5f * x);
}

/* Mild cubic soft-clip mimics CEM3320 OTA stage nonlinearity. */
static float stage_sat(float x) {
    return x - 0.06f * x * x * x;
}

void sh101_filter_init(sh101_filter_t *f, float sample_rate) {
    f->sample_rate = sample_rate;
    f->cutoff_hz = 1200.0f;
    f->resonance = 0.2f;
    f->drive = 1.0f;
    f->g = 0.05f;
    f->y1 = f->y2 = f->y3 = f->y4 = 0.0f;
}

void sh101_filter_set_params(sh101_filter_t *f, float cutoff_hz, float resonance, float drive) {
    f->cutoff_hz = clampf(cutoff_hz, 20.0f, 18000.0f);
    f->resonance = clampf(resonance, 0.0f, 1.2f);
    f->drive = clampf(drive, 0.3f, 4.0f);

    float wc = 2.0f * 3.14159265359f * f->cutoff_hz / f->sample_rate;
    f->g = clampf(wc, 0.0005f, 0.35f);
}

float sh101_filter_process(sh101_filter_t *f, float in) {
    /* Higher resonance naturally reduces perceived low-end/level in vintage behavior. */
    float res = clampf(f->resonance, 0.0f, 1.2f);
    float input_gain = 1.0f - 0.22f * res;
    /* Steepen feedback above res>1.0 to compensate for Euler integration
       energy loss that prevents the digital filter from reaching the
       analog CEM3320's self-oscillation behavior at max Q. */
    float fb_coeff = 1.20f;
    if (res > 1.0f)
        fb_coeff += (res - 1.0f) * 12.0f;
    float fb = (fb_coeff * res) * (f->y4 - 0.15f * f->y3);
    float x = sat((in * input_gain - fb) * f->drive);

    f->y1 += f->g * (x - f->y1);
    f->y1 = stage_sat(f->y1);
    f->y2 += f->g * (f->y1 - f->y2);
    f->y2 = stage_sat(f->y2);
    f->y3 += f->g * (f->y2 - f->y3);
    f->y3 = stage_sat(f->y3);
    f->y4 += f->g * (f->y3 - f->y4);

    return f->y4;
}
