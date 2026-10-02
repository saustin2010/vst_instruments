#ifndef SH101_OSC_H
#define SH101_OSC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sample_rate;
    float phase;
    float sub_phase;
    float sub2_phase;
    float noise_lp;
    uint32_t noise_state;
} sh101_osc_t;

void sh101_osc_init(sh101_osc_t *osc, float sample_rate, uint32_t seed);
float sh101_white_noise(sh101_osc_t *osc);
float sh101_osc_render(sh101_osc_t *osc,
                       float freq_hz,
                       float pwm,
                       float saw_mix,
                       float pulse_mix,
                       float sub_mix,
                       float noise_mix,
                       int sub_mode,
                       float noise_color);

#ifdef __cplusplus
}
#endif

#endif
