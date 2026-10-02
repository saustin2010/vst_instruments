#include "sh101_env.h"

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void sh101_env_init(sh101_env_t *env, float sample_rate) {
    env->sample_rate = sample_rate;
    env->attack_s = 0.01f;
    env->decay_s = 0.12f;
    env->sustain = 0.7f;
    env->release_s = 0.2f;
    env->value = 0.0f;
    env->attack_start = 0.0f;
    env->release_start = 0.0f;
    env->velocity = 1.0f;
    env->stage = ENV_IDLE;
}

void sh101_env_set_adsr(sh101_env_t *env, float attack_s, float decay_s, float sustain, float release_s) {
    env->attack_s = clampf(attack_s, 0.0005f, 10.0f);
    env->decay_s = clampf(decay_s, 0.0005f, 10.0f);
    env->sustain = clampf(sustain, 0.0f, 1.0f);
    env->release_s = clampf(release_s, 0.0005f, 10.0f);
}

void sh101_env_gate_on(sh101_env_t *env, float velocity) {
    env->velocity = clampf(velocity, 0.0f, 1.0f);
    env->attack_start = env->value;
    env->stage = ENV_ATTACK;
}

void sh101_env_gate_off(sh101_env_t *env) {
    env->release_start = env->value;
    env->stage = ENV_RELEASE;
}

float sh101_env_process(sh101_env_t *env) {
    float target = 0.0f;
    float coeff = 1.0f;

    switch (env->stage) {
        case ENV_ATTACK:
            target = env->velocity;
            coeff = 1.0f / (env->attack_s * env->sample_rate);
            env->value += (target - env->value) * coeff * 1.8f;
            if (env->value >= target - 0.001f) {
                env->value = target;
                env->stage = ENV_DECAY;
            }
            break;

        case ENV_DECAY:
            target = env->sustain * env->velocity;
            coeff = 1.0f / (env->decay_s * env->sample_rate);
            env->value += (target - env->value) * coeff * 2.0f;
            if (env->value <= target + 0.001f) {
                env->value = target;
                env->stage = ENV_SUSTAIN;
            }
            break;

        case ENV_SUSTAIN:
            env->value = env->sustain * env->velocity;
            break;

        case ENV_RELEASE:
            target = 0.0f;
            coeff = 1.0f / (env->release_s * env->sample_rate);
            env->value += (target - env->value) * coeff * 2.0f;
            if (env->value <= 0.0001f) {
                env->value = 0.0f;
                env->stage = ENV_IDLE;
            }
            break;

        case ENV_IDLE:
        default:
            env->value = 0.0f;
            break;
    }

    env->value = clampf(env->value, 0.0f, 1.0f);
    return env->value;
}
