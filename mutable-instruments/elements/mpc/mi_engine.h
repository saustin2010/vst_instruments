// Shared pieces for Mutable Instruments DSP run as MPC plugins (steve/tools/mi, 2026-10-01; MIT).
// Each port's engine (mpc/<port>_engine.cc) implements the repo's mpc_engine_t (wrapper/engine.h) with these:
//  - ParamSet: the engine's parameters from one table: set/get by key, enum options by word or index, and the
//    "state" string the wrapper saves in a project ("key=value;key=value").
//  - Resampler: Mutable's DSP runs at its own rate (Rings 48 kHz, Elements 32 kHz) in its own block size; this pulls
//    blocks as needed and interpolates (4-point cubic) to the MPC's 44.1 kHz.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace mpc_mi {

struct Param {
  const char *key;
  float min, max, def;
  const char *const *options;   // option words for an enum (shown as its labels); never a bare number
  int nopts;
  bool integer;
};

class ParamSet {
 public:
  void Init(const Param *p, int n) {
    p_ = p;
    n_ = n;
    for (int i = 0; i < n; ++i) v_[i] = p[i].def;
  }
  float operator[](int i) const { return v_[i]; }
  int index(int i) const { return (int)lroundf(v_[i]); }
  int Find(const char *key) const {
    for (int i = 0; i < n_; ++i)
      if (!strcmp(p_[i].key, key)) return i;
    return -1;
  }
  bool Set(const char *key, const char *val) {
    if (!strcmp(key, "state")) {
      SetState(val);
      return true;
    }
    int i = Find(key);
    if (i < 0) return false;
    const Param &p = p_[i];
    float v;
    if (p.options) {   // the option's word (a restore, the "state" string) or its index (what the wrapper sends)
      int k = -1;
      for (int o = 0; o < p.nopts && k < 0; ++o)
        if (!strcasecmp(val, p.options[o])) k = o;
      v = k >= 0 ? (float)k : (float)atoi(val);
    } else {
      v = (float)atof(val);
    }
    if (p.integer) v = roundf(v);
    v_[i] = v < p.min ? p.min : (v > p.max ? p.max : v);
    return true;
  }
  int Get(const char *key, char *buf, int len) const {
    if (!strcmp(key, "state")) return GetState(buf, len);
    int i = Find(key);
    if (i < 0) return -1;
    return Format(i, buf, len);
  }

 private:
  int Format(int i, char *buf, int len) const {
    const Param &p = p_[i];
    if (p.options) return snprintf(buf, len, "%s", p.options[index(i)]);
    if (p.integer) return snprintf(buf, len, "%d", index(i));
    return snprintf(buf, len, "%.3f", v_[i]);
  }
  int GetState(char *buf, int len) const {
    int n = 0;
    buf[0] = 0;
    for (int i = 0; i < n_ && n < len - 1; ++i) {
      char v[48];
      Format(i, v, sizeof v);
      n += snprintf(buf + n, len - n, "%s%s=%s", i ? ";" : "", p_[i].key, v);
    }
    return n < len ? n : len - 1;
  }
  void SetState(const char *s) {
    char tmp[2048];
    snprintf(tmp, sizeof tmp, "%s", s);
    for (char *save = NULL, *kv = strtok_r(tmp, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
      char *eq = strchr(kv, '=');
      if (!eq) continue;
      *eq = 0;
      if (strcmp(kv, "state")) Set(kv, eq + 1);
    }
  }
  const Param *p_ = NULL;
  int n_ = 0;
  float v_[64];
};

// Stereo, source rate -> 44.1 kHz. Render(l, r, frames, block) calls block(l, r) for kBlock new source frames
// whenever the interpolator needs them.
template <int kBlock>
class Resampler {
 public:
  void Init(float src_rate, float dst_rate = 44100.0f) {
    step_ = src_rate / dst_rate;
    pos_ = 1.0;
    count_ = 3;   // three frames of silence ahead of the first block: the cubic's left context
    for (int i = 0; i < kCap; ++i) l_[i] = r_[i] = 0.0f;
  }
  template <typename F>
  void Render(float *out_l, float *out_r, int frames, F &&block) {
    for (int n = 0; n < frames; ++n) {
      int i = (int)pos_;
      while (i + 2 >= count_) {
        block(l_ + count_, r_ + count_);
        count_ += kBlock;
      }
      float t = (float)(pos_ - i);
      out_l[n] = Cubic(l_ + i - 1, t);
      out_r[n] = Cubic(r_ + i - 1, t);
      pos_ += step_;
    }
    int keep = (int)pos_ - 1;   // drop what's behind the next frame's left context
    if (keep > 0) {
      memmove(l_, l_ + keep, (count_ - keep) * sizeof(float));
      memmove(r_, r_ + keep, (count_ - keep) * sizeof(float));
      count_ -= keep;
      pos_ -= keep;
    }
  }

 private:
  static float Cubic(const float *x, float t) {   // Catmull-Rom through x[0..3], between x[1] and x[2]
    float c1 = 0.5f * (x[2] - x[0]);
    float c2 = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
    float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
    return ((c3 * t + c2) * t + c1) * t + x[1];
  }
  enum { kCap = 4 * kBlock + 512 };
  float l_[kCap], r_[kCap];
  double pos_, step_;
  int count_;
};

static inline int16_t ToS16(float x) {
  x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
  return (int16_t)lrintf(x * 32767.0f);
}

}  // namespace mpc_mi
