// Portable PRNG replacement for stmlib Random (which uses STM32 RNG).
#ifndef STMLIB_UTILS_RANDOM_H_
#define STMLIB_UTILS_RANDOM_H_

#include <stdint.h>

namespace stmlib {

class Random {
 public:
  static inline uint32_t state() { return rng_state_; }

  static inline void Seed(uint32_t seed) { rng_state_ = seed; }

  static inline uint32_t GetWord() {
    rng_state_ = rng_state_ * 1664525L + 1013904223L;
    return rng_state_;
  }

  static inline float GetFloat() {
    return static_cast<float>(GetWord()) / 4294967296.0f;
  }

  static inline int16_t GetSample() {
    return static_cast<int16_t>(GetWord() >> 16);
  }

 private:
  static uint32_t rng_state_;
};

}  // namespace stmlib

#endif  // STMLIB_UTILS_RANDOM_H_
