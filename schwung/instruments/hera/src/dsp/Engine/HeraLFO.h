// SPDX-License-Identifier: GPL-3.0-or-later
// Modified for Move Anything: JUCE dependencies removed

#pragma once
#include "SmoothValue.h"
#include <cstdint>

// Simple xorshift PRNG to replace juce::Random
class SimplePRNG {
public:
    SimplePRNG(uint32_t seed = 12345) : state(seed) {}
    float nextFloat() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float)(state & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    }
private:
    uint32_t state;
};

class HeraLFO {
public:
    enum Type { Triangle, Sine, Square, Random, Noise, None = -1 };

    HeraLFO();
    void setSampleRate(double newRate);
    void processBlock(float *output, int numFrames);
    void setFrequency(float freq) { smoothFrequency_.setTargetValue(freq); }
    void setType(int type) { type_ = type; reset(); }
    void reset() { currentPhase_ = 0; currentValue_ = 0; }

private:
    int type_ = Triangle;
    float sampleRate_ = 44100;
    OnePoleSmoothValue smoothFrequency_;
    float currentPhase_ = 0;
    float currentValue_ = 0;
    SimplePRNG prng_;
};
