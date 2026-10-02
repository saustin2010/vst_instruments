// A small stand-in for the VCV Rack SDK (v2) API a module's DSP uses, so a Rack module's `process()` can run inside an
// MPC plugin engine (steve/tools/rack, 2026-10-02; GPL-3.0-or-later, like the Rack modules it hosts).
// Covers: rack::engine::Module (config*, params/inputs/outputs/lights, ProcessArgs), Port voltages incl. polyphony
// helpers, rack::simd::float_4 (portable: four floats, masks as all-ones lanes, no SSE/NEON needed), the simd math a
// module calls, dsp::TSchmittTrigger and dsp::PulseGenerator, and the ENUMS macro. Panels, widgets, JSON and the
// plugin/model registry are not here: the MPC engine instantiates the Module and maps params and voltages itself.
// Extend it as more modules need more of the API (the 4ms MetaModule's rack-interface is the same idea, fuller).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define ENUMS(name, count) name, name##_LAST = name + (count) - 1

namespace rack {

inline float clamp(float x, float lo, float hi) { return std::fmin(std::fmax(x, lo), hi); }
inline float crossfade(float a, float b, float p) { return a + (b - a) * p; }
inline float rescale(float x, float x0, float x1, float y0, float y1) { return y0 + (x - x0) / (x1 - x0) * (y1 - y0); }

namespace simd {

struct float_4 {
    union { float s[4]; uint32_t u[4]; };
    float_4() { s[0] = s[1] = s[2] = s[3] = 0.f; }
    float_4(float x) { s[0] = s[1] = s[2] = s[3] = x; }
    float_4(float a, float b, float c, float d) { s[0] = a; s[1] = b; s[2] = c; s[3] = d; }
    static float_4 zero() { return float_4(0.f); }
    static float_4 mask() { float_4 r; r.u[0] = r.u[1] = r.u[2] = r.u[3] = 0xFFFFFFFFu; return r; }
    float &operator[](int i) { return s[i]; }
    const float &operator[](int i) const { return s[i]; }
};

#define RACK_F4_ARITH(op)                                                                                \
    inline float_4 operator op(const float_4 &a, const float_4 &b) {                                     \
        return float_4(a.s[0] op b.s[0], a.s[1] op b.s[1], a.s[2] op b.s[2], a.s[3] op b.s[3]);          \
    }                                                                                                    \
    inline float_4 &operator op##=(float_4 &a, const float_4 &b) { a = a op b; return a; }
RACK_F4_ARITH(+)
RACK_F4_ARITH(-)
RACK_F4_ARITH(*)
RACK_F4_ARITH(/)
#undef RACK_F4_ARITH
inline float_4 operator-(const float_4 &a) { return float_4(-a.s[0], -a.s[1], -a.s[2], -a.s[3]); }

#define RACK_F4_CMP(op)                                                                                  \
    inline float_4 operator op(const float_4 &a, const float_4 &b) {                                     \
        float_4 r;                                                                                       \
        for (int i = 0; i < 4; ++i) r.u[i] = (a.s[i] op b.s[i]) ? 0xFFFFFFFFu : 0u;                      \
        return r;                                                                                        \
    }
RACK_F4_CMP(>)
RACK_F4_CMP(<)
RACK_F4_CMP(>=)
RACK_F4_CMP(<=)
RACK_F4_CMP(==)
RACK_F4_CMP(!=)
#undef RACK_F4_CMP

#define RACK_F4_BIT(op)                                                                                  \
    inline float_4 operator op(const float_4 &a, const float_4 &b) {                                     \
        float_4 r;                                                                                       \
        for (int i = 0; i < 4; ++i) r.u[i] = a.u[i] op b.u[i];                                           \
        return r;                                                                                        \
    }                                                                                                    \
    inline float_4 &operator op##=(float_4 &a, const float_4 &b) { a = a op b; return a; }
RACK_F4_BIT(&)
RACK_F4_BIT(|)
RACK_F4_BIT(^)
#undef RACK_F4_BIT
inline float_4 operator~(const float_4 &a) {
    float_4 r;
    for (int i = 0; i < 4; ++i) r.u[i] = ~a.u[i];
    return r;
}

inline float_4 andnot(const float_4 &a, const float_4 &b) { return ~a & b; }
inline float_4 ifelse(const float_4 &m, const float_4 &a, const float_4 &b) {
    float_4 r;
    for (int i = 0; i < 4; ++i) r.u[i] = (m.u[i] & a.u[i]) | (~m.u[i] & b.u[i]);
    return r;
}
inline int movemask(const float_4 &m) {
    return (m.u[0] >> 31) | ((m.u[1] >> 31) << 1) | ((m.u[2] >> 31) << 2) | ((m.u[3] >> 31) << 3);
}

#define RACK_F4_FN1(name, f)                                                                             \
    inline float_4 name(const float_4 &a) { return float_4(f(a.s[0]), f(a.s[1]), f(a.s[2]), f(a.s[3])); }
RACK_F4_FN1(fabs, std::fabs)
RACK_F4_FN1(exp, std::exp)
RACK_F4_FN1(log, std::log)
RACK_F4_FN1(sqrt, std::sqrt)
RACK_F4_FN1(sin, std::sin)
RACK_F4_FN1(cos, std::cos)
RACK_F4_FN1(tanh, std::tanh)
RACK_F4_FN1(floor, std::floor)
RACK_F4_FN1(round, std::round)
#undef RACK_F4_FN1
inline float_4 fmin(const float_4 &a, const float_4 &b) {
    return float_4(std::fmin(a.s[0], b.s[0]), std::fmin(a.s[1], b.s[1]), std::fmin(a.s[2], b.s[2]), std::fmin(a.s[3], b.s[3]));
}
inline float_4 fmax(const float_4 &a, const float_4 &b) {
    return float_4(std::fmax(a.s[0], b.s[0]), std::fmax(a.s[1], b.s[1]), std::fmax(a.s[2], b.s[2]), std::fmax(a.s[3], b.s[3]));
}
inline float_4 pow(const float_4 &a, const float_4 &b) {
    return float_4(std::pow(a.s[0], b.s[0]), std::pow(a.s[1], b.s[1]), std::pow(a.s[2], b.s[2]), std::pow(a.s[3], b.s[3]));
}
inline float_4 pow(float a, const float_4 &b) { return pow(float_4(a), b); }
inline float_4 sgn(const float_4 &a) {
    float_4 r;
    for (int i = 0; i < 4; ++i) r.s[i] = a.s[i] > 0.f ? 1.f : (a.s[i] < 0.f ? -1.f : 0.f);
    return r;
}
inline float_4 clamp(const float_4 &x, const float_4 &lo, const float_4 &hi) { return fmin(fmax(x, lo), hi); }
inline float_4 crossfade(const float_4 &a, const float_4 &b, const float_4 &p) { return a + (b - a) * p; }

}  // namespace simd

namespace dsp {

// Rack 2's dsp::TSchmittTrigger: rising edges above `high`, re-armed below `low`; starts armed-high so a signal
// that is already high at start doesn't fire.
template <typename T = float>
struct TSchmittTrigger {
    T state;
    TSchmittTrigger() { reset(); }
    void reset() { state = T::mask(); }
    T process(T in, T lowThreshold = 0.f, T highThreshold = 1.f) {
        T on = (in >= highThreshold);
        T off = (in <= lowThreshold);
        T triggered = ~state & on;
        state = on | (state & ~off);
        return triggered;
    }
};
template <>
struct TSchmittTrigger<float> {
    bool state = true;
    void reset() { state = true; }
    bool process(float in, float lowThreshold = 0.f, float highThreshold = 1.f) {
        if (state) {
            if (in <= lowThreshold) state = false;
        } else if (in >= highThreshold) {
            state = true;
            return true;
        }
        return false;
    }
    bool isHigh() const { return state; }
};
typedef TSchmittTrigger<float> SchmittTrigger;

struct PulseGenerator {
    float remaining = 0.f;
    void reset() { remaining = 0.f; }
    bool process(float deltaTime) {
        if (remaining > 0.f) { remaining -= deltaTime; return true; }
        return false;
    }
    void trigger(float duration = 1e-3f) { if (duration > remaining) remaining = duration; }
};

}  // namespace dsp

namespace engine {

static const int PORT_MAX_CHANNELS = 16;

struct ParamInfo { float min = 0.f, max = 1.f, def = 0.f; std::string name; std::vector<std::string> labels; bool button = false; };

struct Param {
    float value = 0.f;
    float getValue() const { return value; }
    void setValue(float v) { value = v; }
};

struct Port {
    float voltages[PORT_MAX_CHANNELS] = {};
    int channels = 0;
    int getChannels() const { return channels; }
    bool isConnected() const { return channels > 0; }
    void setChannels(int n) { channels = n; }
    float getVoltage(int c = 0) const { return voltages[c]; }
    void setVoltage(float v, int c = 0) { voltages[c] = v; }
    float getPolyVoltage(int c) const { return channels == 1 ? voltages[0] : voltages[c]; }
    float getNormalVoltage(float normal, int c = 0) const { return isConnected() ? getVoltage(c) : normal; }
    template <class T> T getPolyVoltageSimd(int c) const {
        T r;
        for (int i = 0; i < 4 && c + i < PORT_MAX_CHANNELS; ++i) r.s[i] = getPolyVoltage(c + i);
        return r;
    }
    template <class T> T getVoltageSimd(int c) const {
        T r;
        for (int i = 0; i < 4 && c + i < PORT_MAX_CHANNELS; ++i) r.s[i] = voltages[c + i];
        return r;
    }
    template <class T> void setVoltageSimd(T v, int c) {
        for (int i = 0; i < 4 && c + i < PORT_MAX_CHANNELS; ++i) voltages[c + i] = v.s[i];
    }
};
struct Input : Port {};
struct Output : Port {};

struct Light {
    float value = 0.f;
    float getBrightness() const { return value; }
    void setBrightness(float b) { value = b; }
    void setSmoothBrightness(float b, float deltaTime) { value += (b - value) * std::fmin(1.f, deltaTime * 60.f); }
};

struct Module {
    std::vector<Param> params;
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    std::vector<Light> lights;
    std::vector<ParamInfo> paramInfo;
    struct ProcessArgs { float sampleRate = 44100.f; float sampleTime = 1.f / 44100.f; int64_t frame = 0; };
    virtual ~Module() {}
    void config(int numParams, int numInputs, int numOutputs, int numLights = 0) {
        params.assign(numParams, Param());
        inputs.assign(numInputs, Input());
        outputs.assign(numOutputs, Output());
        lights.assign(numLights, Light());
        paramInfo.assign(numParams, ParamInfo());
    }
    void *configParam(int id, float min, float max, float def, std::string name = "", std::string = "", float = 0.f,
                      float = 1.f, float = 0.f) {
        ParamInfo &i = paramInfo[id];
        i.min = min; i.max = max; i.def = def; i.name = name;
        params[id].value = def;
        return nullptr;
    }
    void *configSwitch(int id, float min, float max, float def, std::string name = "",
                       std::vector<std::string> labels = {}) {
        configParam(id, min, max, def, name);
        paramInfo[id].labels = labels;
        return nullptr;
    }
    void *configButton(int id, std::string name = "") {
        configParam(id, 0.f, 1.f, 0.f, name);
        paramInfo[id].button = true;
        return nullptr;
    }
    void *configInput(int, std::string = "") { return nullptr; }
    void *configOutput(int, std::string = "") { return nullptr; }
    void *configLight(int, std::string = "") { return nullptr; }
    void *configBypass(int, int) { return nullptr; }
    virtual void process(const ProcessArgs &) {}
    virtual void onReset() {}
    virtual void onSampleRateChange() {}
};

}  // namespace engine

using namespace engine;
using simd::float_4;

}  // namespace rack
