// Copyright 2012 Emilie Gillet. MIT License.
// Adapted for Schwung: portable Clip16/Sqrt (no ARM asm).

#ifndef STMLIB_UTILS_DSP_DSP_H_
#define STMLIB_UTILS_DSP_DSP_H_

#include "stmlib/stmlib.h"
#include <cmath>
#include <math.h>

namespace stmlib {

#define MAKE_INTEGRAL_FRACTIONAL(x) \
  int32_t x ## _integral = static_cast<int32_t>(x); \
  float x ## _fractional = x - static_cast<float>(x ## _integral);

inline float Interpolate(const float* table, float index, float size) {
  index *= size;
#ifdef MPC_PORT
  // MPC port (2026-10-02): the hardware's ADCs stop just short of full scale, but a host parameter at exactly 1.0, or a
  // modulation sum past it, read one past the end of the table (size + 1 entries; found with steve/tools/fuzz).
  if (!(index > 0.0f)) index = 0.0f;  // also NaN
  if (index > size) index = size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  if (index_integral >= static_cast<int32_t>(size)) {
    index_integral = static_cast<int32_t>(size) - 1;
    index_fractional = index - static_cast<float>(index_integral);
  }
#else
  MAKE_INTEGRAL_FRACTIONAL(index)
#endif
  float a = table[index_integral];
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}

inline float InterpolateHermite(const float* table, float index, float size) {
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  const float xm1 = table[index_integral - 1];
  const float x0 = table[index_integral + 0];
  const float x1 = table[index_integral + 1];
  const float x2 = table[index_integral + 2];
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float b_neg = w + a;
  const float f = index_fractional;
  return (((a * f) - b_neg) * f + c) * f + x0;
}

inline float InterpolateWrap(const float* table, float index, float size) {
  index -= static_cast<float>(static_cast<int32_t>(index));
  index *= size;
  MAKE_INTEGRAL_FRACTIONAL(index)
  float a = table[index_integral];
  float b = table[index_integral + 1];
  return a + (b - a) * index_fractional;
}

inline float SmoothStep(float value) {
  return value * value * (3.0f - 2.0f * value);
}

#define ONE_POLE(out, in, coefficient) out += (coefficient) * ((in) - out);
#define SLOPE(out, in, positive, negative) { \
  float error = (in) - out; \
  out += (error > 0 ? positive : negative) * error; \
}
#define SLEW(out, in, delta) { \
  float error = (in) - out; \
  float d = (delta); \
  if (error > d) { \
    error = d; \
  } else if (error < -d) { \
    error = -d; \
  } \
  out += error; \
}

inline float Crossfade(float a, float b, float fade) {
  return a + (b - a) * fade;
}

inline float SoftLimit(float x) {
  return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float SoftClip(float x) {
  if (x < -3.0f) return -1.0f;
  else if (x > 3.0f) return 1.0f;
  else return SoftLimit(x);
}

// Portable versions (no ARM asm)
inline int32_t Clip16(int32_t x) {
  if (x < -32768) return -32768;
  else if (x > 32767) return 32767;
  else return x;
}

inline uint16_t ClipU16(int32_t x) {
  if (x < 0) return 0;
  else if (x > 65535) return 65535;
  else return (uint16_t)x;
}

inline float Sqrt(float x) {
  return sqrtf(x);
}

inline int16_t SoftConvert(float x) {
  return Clip16(static_cast<int32_t>(SoftLimit(x * 0.5f) * 32768.0f));
}

}  // namespace stmlib

#endif  // STMLIB_UTILS_DSP_DSP_H_
