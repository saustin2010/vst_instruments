#ifndef SH101_FILTER_H
#define SH101_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sample_rate;
    float cutoff_hz;
    float resonance;
    float drive;

    float g;
    float y1;
    float y2;
    float y3;
    float y4;
} sh101_filter_t;

void sh101_filter_init(sh101_filter_t *f, float sample_rate);
void sh101_filter_set_params(sh101_filter_t *f, float cutoff_hz, float resonance, float drive);
float sh101_filter_process(sh101_filter_t *f, float in);

#ifdef __cplusplus
}
#endif

#endif
