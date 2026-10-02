// Copyright 2014 Emilie Gillet. MIT License.
#ifndef STMLIB_DSP_UNITS_H_
#define STMLIB_DSP_UNITS_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"

namespace stmlib {

extern const float lut_pitch_ratio_high[257];
extern const float lut_pitch_ratio_low[257];

inline float SemitonesToRatio(float semitones) {
  float pitch = semitones + 128.0f;
  MAKE_INTEGRAL_FRACTIONAL(pitch)
  return lut_pitch_ratio_high[pitch_integral] *
      lut_pitch_ratio_low[static_cast<int32_t>(pitch_fractional * 256.0f)];
}

inline float SemitonesToRatioSafe(float semitones) {
  float scale = 1.0f;
  while (semitones > 120.0f) { semitones -= 120.0f; scale *= 1024.0f; }
  while (semitones < -120.0f) { semitones += 120.0f; scale *= 1.0f / 1024.0f; }
  return scale * SemitonesToRatio(semitones);
}

}  // namespace stmlib
#endif  // STMLIB_DSP_UNITS_H_
