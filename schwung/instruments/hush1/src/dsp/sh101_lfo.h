#ifndef SH101_LFO_H
#define SH101_LFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sample_rate;
    float rate_hz;
    float phase;
} sh101_lfo_t;

void sh101_lfo_init(sh101_lfo_t *lfo, float sample_rate);
void sh101_lfo_set_rate_hz(sh101_lfo_t *lfo, float rate_hz);
float sh101_lfo_process(sh101_lfo_t *lfo);

#ifdef __cplusplus
}
#endif

#endif
