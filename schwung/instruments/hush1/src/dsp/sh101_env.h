#ifndef SH101_ENV_H
#define SH101_ENV_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} sh101_env_stage_t;

typedef struct {
    float sample_rate;
    float attack_s;
    float decay_s;
    float sustain;
    float release_s;

    float value;
    float attack_start;
    float release_start;
    float velocity;

    sh101_env_stage_t stage;
} sh101_env_t;

void sh101_env_init(sh101_env_t *env, float sample_rate);
void sh101_env_set_adsr(sh101_env_t *env, float attack_s, float decay_s, float sustain, float release_s);
void sh101_env_gate_on(sh101_env_t *env, float velocity);
void sh101_env_gate_off(sh101_env_t *env);
float sh101_env_process(sh101_env_t *env);

#ifdef __cplusplus
}
#endif

#endif
