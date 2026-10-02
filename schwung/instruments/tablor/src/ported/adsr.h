/* Analog-modeled ADSR — de-JUCE'd port of gin::AnalogADSR (BSD-3, Roland
 * Rabien; after Will Pirkle's "Designing Software Synthesizer Plug-Ins").
 * Exponential curves via one-pole coefficient + offset per stage.
 * Behaviour preserved exactly, including the attack==0 fast path and the
 * noteOn-from-idle output reset.
 */
#pragma once

#include <cmath>

namespace tb {

class AnalogADSR {
public:
    enum class State { idle, attack, decay, sustain, release };

    AnalogADSR()
    {
        setAttack(0.2f);
        setDecay(0.2f);
        setRelease(0.2f);
        setSustainLevel(0.8f);
        reset();
    }

    void setSampleRate(double sr)
    {
        sampleRate = sr;
        calculateAttack(); calculateDecay(); calculateRelease();
    }

    float getOutput() const { return output; }
    State getState()  const { return state;  }

    void noteOn()
    {
        calculateRelease();
        State orig = state;
        if (attack == 0.0f) {
            state = State::decay;
            output = 1.0f;
        } else {
            state = State::attack;
            if (orig == State::idle)
                output = 0.0f;
        }
    }

    void noteOff()
    {
        state = (output > 0.0f) ? State::release : State::idle;
    }

    void setAttack(float a)
    {
        if (attack != a) { attack = a; calculateAttack(); }
    }
    void setDecay(float d)
    {
        if (decay != d) { decay = d; calculateDecay(); }
    }
    void setRelease(float r)
    {
        if (release != r) { release = r; calculateRelease(); }
    }
    void setSustainLevel(float s)
    {
        if (sustain != s) {
            sustain = s;
            calculateDecay();
            if (state != State::release)
                calculateRelease();
        }
    }

    inline float process()
    {
        switch (state) {
        case State::idle: break;
        case State::attack:
            output = attackOffset + output * attackCoeff;
            if (output >= 1.0f || attack == 0.0f) { output = 1.0f; state = State::decay; }
            break;
        case State::decay:
            output = decayOffset + output * decayCoeff;
            if (output <= sustain) state = State::sustain;
            break;
        case State::sustain: break;
        case State::release:
            output = releaseOffset + output * releaseCoeff;
            if (output <= 0.0f || release == 0.0f) { output = 0.0f; state = State::idle; }
            break;
        }
        return output;
    }

    float process(int num)
    {
        for (int i = num; --i >= 0;) process();
        return output;
    }

    /* VCA use: multiply stereo audio in place. */
    void processMultiplying(float *l, float *r, int n)
    {
        for (int i = 0; i < n; i++) {
            process();
            l[i] *= output;
            r[i] *= output;
        }
    }

    void reset()
    {
        state = State::idle;
        output = 0.0f;
    }

private:
    void calculateAttack()
    {
        float samples = (float) (sampleRate * attack);
        float tco = std::exp(-0.5f);
        attackCoeff = std::exp(-std::log((1.0f + tco) / tco) / samples);
        attackOffset = (1.0f + tco) * (1.0f - attackCoeff);
    }
    void calculateDecay()
    {
        float samples = (float) (sampleRate * decay);
        float tco = std::exp(-5.0f);
        decayCoeff = std::exp(-std::log((1.0f + tco) / tco) / samples);
        decayOffset = (sustain - tco) * (1.0f - decayCoeff);
    }
    void calculateRelease()
    {
        float samples = (float) (sampleRate * release);
        float tco = std::exp(-5.0f);
        releaseCoeff = std::exp(-std::log((1.0f + tco) / tco) / samples);
        releaseOffset = -tco * (1.0f - releaseCoeff);
    }

    State state = State::idle;
    double sampleRate = 44100.0;
    float attack = 0.0f, decay = 0.0f, sustain = 0.0f, release = 0.0f;
    float attackCoeff = 0.0f, decayCoeff = 0.0f, releaseCoeff = 0.0f;
    float attackOffset = 0.0f, decayOffset = 0.0f, releaseOffset = 0.0f;
    float output = 0.0f;
};

/* One-pole value smoother — port of gin::ValueSmoother (glide/noteSmoother). */
template <class T>
class ValueSmoother {
public:
    void setSampleRate(double sr) { sampleRate = sr; reset(); }
    void setTime(double seconds)
    {
        time = seconds;
        reset();
    }
    void reset()
    {
        /* per-sample step for a linear ramp across `time` seconds */
        delta = (time > 0.0) ? (T) (1.0 / (sampleRate * time)) : (T) 1;
    }
    void snapToValue() { current = target; }

    T getCurrentValue() const { return current; }
    T getTargetValue()  const { return target;  }

    void setValue(T v) { target = v; }
    void setValueUnsmoothed(T v) { target = current = v; }

    inline void process(int n)
    {
        if (target != current)
            for (int i = 0; i < n; i++) updateValue();
    }
    inline void updateValue()
    {
        if (current < target)
            current = (T) std::fmin((double) target, (double) (current + delta));
        else if (current > target)
            current = (T) std::fmax((double) target, (double) (current - delta));
    }

private:
    double sampleRate = 44100.0, time = 0.1;
    T delta = (T) 0;
    T current = (T) 0, target = (T) 0;
};

} // namespace tb
