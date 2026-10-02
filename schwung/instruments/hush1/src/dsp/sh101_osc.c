#include "sh101_osc.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float soft_sat(float x) {
    return tanhf(1.4f * x);
}

void sh101_osc_init(sh101_osc_t *osc, float sample_rate, uint32_t seed) {
    osc->sample_rate = sample_rate;
    osc->phase = 0.0f;
    osc->sub_phase = 0.0f;
    osc->sub2_phase = 0.0f;
    osc->noise_lp = 0.0f;
    osc->noise_state = seed ? seed : 0x12345678u;
}

float sh101_white_noise(sh101_osc_t *osc) {
    osc->noise_state = osc->noise_state * 1664525u + 1013904223u;
    return ((float)((int32_t)osc->noise_state) / 2147483648.0f);
}

float sh101_osc_render(sh101_osc_t *osc,
                       float freq_hz,
                       float pwm,
                       float saw_mix,
                       float pulse_mix,
                       float sub_mix,
                       float noise_mix,
                       int sub_mode,
                       float noise_color) {
    float inc = freq_hz / osc->sample_rate;
    if (inc < 0.0f) inc = 0.0f;
    if (inc > 0.45f) inc = 0.45f;

    osc->phase += inc;
    if (osc->phase >= 1.0f) osc->phase -= 1.0f;

    osc->sub_phase += inc * 0.5f;
    if (osc->sub_phase >= 1.0f) osc->sub_phase -= 1.0f;
    osc->sub2_phase += inc * 0.25f;
    if (osc->sub2_phase >= 1.0f) osc->sub2_phase -= 1.0f;

    pwm = clampf(pwm, 0.05f, 0.95f);
    noise_color = clampf(noise_color, 0.0f, 1.0f);

    float saw = 2.0f * osc->phase - 1.0f;

    /* Small asymmetries keep pulse/sub from sounding sterile. */
    float pulse = (osc->phase < pwm) ? 1.0f : -0.95f;
    float sub1_square = (osc->sub_phase < 0.5f) ? 0.94f : -1.0f;
    float sub2_square = (osc->sub2_phase < 0.5f) ? 0.94f : -1.0f;
    float sub2_pulse = (osc->sub2_phase < 0.25f) ? 1.0f : -1.0f;
    float sub = sub1_square;
    if (sub_mode == 1) sub = sub2_square;
    else if (sub_mode == 2) sub = sub2_pulse;

    /* Slightly colored noise sits better for SH-style transients than pure white noise. */
    float white = sh101_white_noise(osc);
    osc->noise_lp += 0.085f * (white - osc->noise_lp);
    {
        float colored = 0.72f * osc->noise_lp + 0.28f * white;
        float noise = colored + (white - colored) * noise_color;
        float mix = saw_mix * saw + pulse_mix * pulse + sub_mix * sub + noise_mix * noise;

        /* Mixer headroom + soft clipping helps preserve the "pushed mixer" character. */
        return soft_sat(mix * 0.42f);
    }
}
