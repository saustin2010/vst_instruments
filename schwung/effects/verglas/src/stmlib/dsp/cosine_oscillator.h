// Copyright 2014 Emilie Gillet. MIT License.
#ifndef STMLIB_DSP_COSINE_OSCILLATOR_H_
#define STMLIB_DSP_COSINE_OSCILLATOR_H_

#include "stmlib/stmlib.h"
#include <cmath>

namespace stmlib {

enum CosineOscillatorMode {
  COSINE_OSCILLATOR_APPROXIMATE,
  COSINE_OSCILLATOR_EXACT
};

class CosineOscillator {
 public:
  CosineOscillator() { }
  ~CosineOscillator() { }

  template<CosineOscillatorMode mode>
  inline void Init(float frequency) {
    if (mode == COSINE_OSCILLATOR_APPROXIMATE) {
      InitApproximate(frequency);
    } else {
      iir_coefficient_ = 2.0f * cosf(2.0f * float(M_PI) * frequency);
      initial_amplitude_ = iir_coefficient_ * 0.25f;
    }
    Start();
  }

  inline void InitApproximate(float frequency) {
    float sign = 16.0f;
    frequency -= 0.25f;
    if (frequency < 0.0f) {
      frequency = -frequency;
    } else {
      if (frequency > 0.5f) frequency -= 0.5f;
      else sign = -16.0f;
    }
    iir_coefficient_ = sign * frequency * (1.0f - 2.0f * frequency);
    initial_amplitude_ = iir_coefficient_ * 0.25f;
  }

  inline void Start() { y1_ = initial_amplitude_; y0_ = 0.5f; }
  inline float value() const { return y1_ + 0.5f; }
  inline float Next() {
    float temp = y0_;
    y0_ = iir_coefficient_ * y0_ - y1_;
    y1_ = temp;
    return temp + 0.5f;
  }

 private:
  float y1_, y0_, iir_coefficient_, initial_amplitude_;
  DISALLOW_COPY_AND_ASSIGN(CosineOscillator);
};

}  // namespace stmlib
#endif  // STMLIB_DSP_COSINE_OSCILLATOR_H_
