// SPDX-License-Identifier: ISC
// Modified for Move Anything: JUCE dependencies removed

#pragma once
#include <initializer_list>
#include <cassert>

template <class FaustDSP>
static void compute(
    FaustDSP &dsp,
    std::initializer_list<const float *> inputs,
    std::initializer_list<float *> outputs,
    int numSamples)
{
    assert(inputs.size() == (size_t)FaustDSP::getNumInputs());
    assert(outputs.size() == (size_t)FaustDSP::getNumOutputs());
    dsp.compute(numSamples, (float **)inputs.begin(), (float **)outputs.begin());
}

template <class FaustDSP>
static void flushSmoothValues(FaustDSP &dsp)
{
    float in[FaustDSP::getNumInputs()] = {};
    float out[FaustDSP::getNumOutputs()];

    float *inPtr[FaustDSP::getNumInputs()];
    float *outPtr[FaustDSP::getNumOutputs()];
    for (int i = 0; i < FaustDSP::getNumInputs(); ++i)
        inPtr[i] = &in[i];
    for (int i = 0; i < FaustDSP::getNumOutputs(); ++i)
        outPtr[i] = &out[i];

    dsp.setSmoothDisabled(1);
    dsp.compute(1, inPtr, outPtr);
    dsp.setSmoothDisabled(0);
}
