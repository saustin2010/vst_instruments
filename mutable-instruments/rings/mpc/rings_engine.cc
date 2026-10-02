// Rings (Mutable Instruments resonator) as an MPC instrument: mpc_engine_t for the repo's VST2 wrapper.
// (steve/schwung-ports/rings, 2026-10-01; MIT.)
//
// Played the way the module is with nothing patched into IN: the internal exciter strums the resonator. Each MIDI
// note-on is one strum at that note (with the port's velocity patch, src/rings/dsp/part.cc); voices rotate as on the
// module (1, 2 or 4). Note-offs do nothing: the resonance decays by DAMPING, as on the hardware. Pitch bend is the
// FM input. Model 7 is the module's hidden "Disastrous Peace" string synth with its six effects.
// Rings runs at its own 48 kHz in 24-frame blocks; mpc_mi::Resampler brings it to 44.1 kHz. OUT/AUX (odd/even
// voices, or the two pickups with one voice) become left/right, narrowed by WIDTH.
#include <new>

extern "C" {
#include "engine.h"
}
#include "mi_engine.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/dsp.h"

using namespace rings;
using mpc_mi::Param;

namespace {

const char *const kModels[] = {"MODAL", "SYMPATHETIC", "STRING", "FM VOICE", "SYMP CHORDS", "STRING+VERB",
                               "STRING SYNTH"};
const char *const kPoly[] = {"1 VOICE", "2 VOICES", "4 VOICES"};
const char *const kFx[] = {"FORMANT", "CHORUS", "REVERB", "FORMANT 2", "ENSEMBLE", "REVERB 2"};

enum { P_MODEL, P_POLY, P_STRUCTURE, P_BRIGHTNESS, P_DAMPING, P_POSITION, P_FX, P_OCTAVE, P_BEND, P_VELOCITY,
       P_WIDTH, P_VOLUME, P_COUNT };
const Param kParams[P_COUNT] = {
  {"model", 0, 6, 0, kModels, 7, true},
  {"polyphony", 0, 2, 2, kPoly, 3, true},
  {"structure", 0, 1, 0.4f, 0, 0, false},
  {"brightness", 0, 1, 0.5f, 0, 0, false},
  {"damping", 0, 1, 0.6f, 0, 0, false},
  {"position", 0, 1, 0.3f, 0, 0, false},
  {"synth_fx", 0, 5, 0, kFx, 6, true},
  {"octave", -2, 2, 0, 0, 0, true},
  {"bend_range", 0, 12, 2, 0, 0, true},
  {"velocity", 0, 1, 0.7f, 0, 0, false},
  {"width", 0, 1, 1.0f, 0, 0, false},
  {"volume", 0, 1, 0.7f, 0, 0, false},
};

struct Strum { float note, velocity; };

struct Inst {
  mpc_mi::ParamSet ps;
  Part part;
  StringSynthPart synth;
  uint16_t reverb_buffer[32768];
  mpc_mi::Resampler<kMaxBlockSize> rs;
  Strum queue[32];
  int q_head, q_tail;
  float note, velocity, bend;
  int model, poly, fx;
};

void Configure(Inst *in) {
  int model = in->ps.index(P_MODEL), poly = 1 << in->ps.index(P_POLY), fx = in->ps.index(P_FX);
  if (model != in->model && model < RESONATOR_MODEL_LAST) in->part.set_model((ResonatorModel)model);
  if (poly != in->poly) {
    in->part.set_polyphony(poly);
    in->synth.set_polyphony(poly);
  }
  if (fx != in->fx) in->synth.set_fx((FxType)fx);
  in->model = model;
  in->poly = poly;
  in->fx = fx;
}

void RenderBlock(Inst *in, float *l, float *r) {   // one Rings block, kMaxBlockSize frames at 48 kHz
  Configure(in);
  PerformanceState s;
  s.strum = false;
  if (in->q_head != in->q_tail) {   // one strum per block: a chord lands within a few blocks (0.5 ms each)
    in->note = in->queue[in->q_head].note;
    in->velocity = in->queue[in->q_head].velocity;
    in->q_head = (in->q_head + 1) % 32;
    s.strum = true;
  }
  s.internal_exciter = true;
  s.internal_strum = false;
  s.internal_note = false;
  s.tonic = 12.0f + 12.0f * in->ps.index(P_OCTAVE);
  s.note = in->note - 12.0f;
  s.fm = in->bend;
  s.chord = (int32_t)lroundf(in->ps[P_STRUCTURE] * (kNumChords - 1));
  float vs = in->ps[P_VELOCITY];
  s.velocity = 1.0f - vs + vs * in->velocity;
  Patch patch;
  patch.structure = in->ps[P_STRUCTURE];
  patch.brightness = in->ps[P_BRIGHTNESS];
  patch.damping = in->ps[P_DAMPING];
  patch.position = in->ps[P_POSITION];
  float input[kMaxBlockSize] = {0};
  if (in->model == 6) in->synth.Process(s, patch, input, l, r, kMaxBlockSize);
  else in->part.Process(s, patch, input, l, r, kMaxBlockSize);
}

void *Create(const char *) {
  Inst *in = new (std::nothrow) Inst();
  if (!in) return NULL;
  in->ps.Init(kParams, P_COUNT);
  in->part.Init(in->reverb_buffer);
  in->synth.Init(in->reverb_buffer);
  in->rs.Init(kSampleRate);
  in->q_head = in->q_tail = 0;
  in->note = 60.0f;
  in->velocity = 1.0f;
  in->bend = 0.0f;
  in->model = in->poly = in->fx = -1;
  return in;
}

void Destroy(void *p) { delete (Inst *)p; }

void Midi(void *p, const uint8_t *m, int len) {
  Inst *in = (Inst *)p;
  if (len < 3) return;
  uint8_t st = m[0] & 0xF0;
  if (st == 0x90 && m[2] > 0) {
    int next = (in->q_tail + 1) % 32;
    if (next != in->q_head) {
      in->queue[in->q_tail].note = (float)m[1];
      in->queue[in->q_tail].velocity = m[2] / 127.0f;
      in->q_tail = next;
    }
  } else if (st == 0xE0) {
    float b = (float)(((m[2] << 7) | m[1]) - 8192) / 8192.0f;
    in->bend = b * in->ps.index(P_BEND);
  }
}

void SetParam(void *p, const char *k, const char *v) { ((Inst *)p)->ps.Set(k, v); }
int GetParam(void *p, const char *k, char *b, int n) { return ((Inst *)p)->ps.Get(k, b, n); }

void Render(void *p, int16_t *out, int frames) {
  Inst *in = (Inst *)p;
  float l[256], r[256];
  if (frames > 256) frames = 256;
  in->rs.Render(l, r, frames, [in](float *bl, float *br) { RenderBlock(in, bl, br); });
  float g = 3.0f * in->ps[P_VOLUME] * in->ps[P_VOLUME], w = in->ps[P_WIDTH];
  for (int i = 0; i < frames; ++i) {
    float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * w;
    out[2 * i] = mpc_mi::ToS16((mid + side) * g);
    out[2 * i + 1] = mpc_mi::ToS16((mid - side) * g);
  }
}

const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render};

}  // namespace

const mpc_engine_t *mpc_engine(void) { return &kEngine; }
