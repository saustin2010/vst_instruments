// Copyright 2021 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// 6-operator FM synth.

#include "plaits/dsp/engine2/six_op_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace fm;
using namespace std;
using namespace stmlib;

void FMVoice::Init(fm::Algorithms<6>* algorithms, float sample_rate) {
  voice_.Init(algorithms, sample_rate);
  lfo_.Init(sample_rate);
  
  parameters_.sustain = false;
  parameters_.gate = false;
  parameters_.note = 48.0f;
  parameters_.velocity = 0.5f;
  parameters_.brightness = 0.5f;
  parameters_.envelope_control = 0.5f;
  parameters_.pitch_mod = 0.0f;
  parameters_.amp_mod = 0.0f;
  
  patch_ = NULL;
}

void FMVoice::Render(float* buffer, size_t size) {
  if (!patch_) {
    return;
  }
  voice_.Render(parameters_, buffer, size);
}

void FMVoice::LoadPatch(const fm::Patch* patch) {
  if (patch == patch_) {
    return;
  }
  patch_ = patch;
  voice_.SetPatch(patch_);
  lfo_.Set(patch_->modulations);
}

const int kNumPatchesPerBank = 32;

void SixOpEngine::Init(BufferAllocator* allocator) {
  patch_index_quantizer_.Init(32, 0.005f, false);

  algorithms_.Init();
  for (int i = 0; i < kNumSixOpVoices; ++i) {
    voice_[i].Init(&algorithms_, kCorrectedSampleRate);
  }
#ifdef MPC_PORT
  // MPC port (2026-10-02): fm::Voice<6>::Render(params, temp, size) uses temp, temp + size and temp + 2 * size, so
  // the scratch needs 3 * kMaxBlockSize; with one block it wrote 48 floats into patches_, allocated right after it,
  // and FM patches 0-1 of the bank played garbage operator data (out-of-range table reads; found with
  // steve/tools/fuzz). Four blocks, to leave a spare.
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize * 4);
#else
  temp_buffer_ = allocator->Allocate<float>(kMaxBlockSize);
#endif
  patches_ = allocator->Allocate<fm::Patch>(kNumPatchesPerBank);
}

void SixOpEngine::Reset() {
  
}

void SixOpEngine::LoadUserData(const uint8_t* user_data) {
  for (int i = 0; i < kNumPatchesPerBank; ++i) {
    patches_[i].Unpack(user_data + i * fm::Patch::SYX_SIZE);
  }
  for (int i = 0; i < kNumSixOpVoices; ++i) {
    voice_[i].UnloadPatch();
  }
}

void SixOpEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  int patch_index = patch_index_quantizer_.Process(
      parameters.harmonics * 1.02f);

  if (parameters.trigger & TRIGGER_UNPATCHED) {
    const float t = parameters.morph;
    voice_[0].mutable_lfo()->Scrub(2.0f * kCorrectedSampleRate * t);
    voice_[0].LoadPatch(&patches_[patch_index]);
    Voice<6>::Parameters* p = voice_[0].mutable_parameters();
    p->sustain = true;
    p->gate = false;
    p->note = parameters.note;
    p->velocity = parameters.accent;
    p->brightness = parameters.timbre;
    p->envelope_control = t;
    voice_[0].set_modulations(voice_[0].lfo());
  } else {
    if (parameters.trigger & TRIGGER_RISING_EDGE) {
      voice_[0].mutable_lfo()->Reset();
    }
    voice_[0].LoadPatch(&patches_[patch_index]);
    Voice<6>::Parameters* p = voice_[0].mutable_parameters();
    p->note = parameters.note;
    p->velocity = parameters.accent;
    p->envelope_control = parameters.morph;
    p->brightness = parameters.timbre;
    p->sustain = false;
    p->gate = (parameters.trigger & TRIGGER_HIGH) != 0;
    voice_[0].mutable_lfo()->Step(float(size));
    voice_[0].set_modulations(voice_[0].lfo());
  }

  // Direct rendering — single voice, no staggering needed.
  fill(&temp_buffer_[0], &temp_buffer_[size], 0.0f);
  voice_[0].Render(temp_buffer_, size);

  for (size_t i = 0; i < size; ++i) {
    aux[i] = out[i] = SoftClip(temp_buffer_[i] * 0.25f);
  }
}

}  // namespace plaits
