// Rings (Mutable Instruments resonator) as an MPC audio effect: the track's audio excites the resonator, as with a
// signal patched into the module's IN and nothing in V/OCT or STRUM. (steve/schwung-ports/ringsfx, 2026-10-01; MIT.)
//
// Onsets in the input strum it (the module's own onset detector), at NOTE + FINE; MIDI note-ons, if the host sends any
// to an effect, set the note and strum too. Models 1-6 as on the module (no string synth: it ignores the input).
// It runs at the MPC's 44.1 kHz with its tuning corrected (its DSP assumes 48 kHz); its decays run ~9% long.
#include <new>

extern "C" {
#include "engine.h"
}
#include "mi_engine.h"
#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"
#include "rings/dsp/dsp.h"

using namespace rings;
using mpc_mi::Param;

namespace {

const char *const kModels[] = {"MODAL", "SYMPATHETIC", "STRING", "FM VOICE", "SYMP CHORDS", "STRING+VERB"};
const char *const kPoly[] = {"1 VOICE", "2 VOICES", "4 VOICES"};

enum { P_MODEL, P_POLY, P_STRUCTURE, P_BRIGHTNESS, P_DAMPING, P_POSITION, P_NOTE, P_FINE, P_INPUT, P_MIX, P_WIDTH,
       P_VOLUME, P_COUNT };
const Param kParams[P_COUNT] = {
  {"model", 0, 5, 0, kModels, 6, true},
  {"polyphony", 0, 2, 1, kPoly, 3, true},
  {"structure", 0, 1, 0.4f, 0, 0, false},
  {"brightness", 0, 1, 0.5f, 0, 0, false},
  {"damping", 0, 1, 0.6f, 0, 0, false},
  {"position", 0, 1, 0.3f, 0, 0, false},
  {"note", 24, 96, 48, 0, 0, true},
  {"fine", -1, 1, 0.0f, 0, 0, false},
  {"input_gain", 0, 1, 0.5f, 0, 0, false},
  {"mix", 0, 1, 0.7f, 0, 0, false},
  {"width", 0, 1, 1.0f, 0, 0, false},
  {"volume", 0, 1, 0.7f, 0, 0, false},
};

const float kTuning = -12.0f * 0.12553088f;   // 12 * log2(44100 / 48000): Rings' a3 assumes 48 kHz

struct Inst {
  mpc_mi::ParamSet ps;
  Part part;
  Strummer strummer;
  uint16_t reverb_buffer[32768];
  int16_t in[2 * 256];
  int frames;
  float midi_note;   // < 0: none yet
  bool midi_strum;
  int model, poly;
};

void *Create(const char *) {
  Inst *in = new (std::nothrow) Inst();
  if (!in) return NULL;
  in->ps.Init(kParams, P_COUNT);
  in->part.Init(in->reverb_buffer);
  in->strummer.Init(0.01f, 44100.0f / kMaxBlockSize);
  in->frames = 0;
  in->midi_note = -1.0f;
  in->midi_strum = false;
  in->model = in->poly = -1;
  return in;
}

void Destroy(void *p) { delete (Inst *)p; }

void Midi(void *p, const uint8_t *m, int len) {
  Inst *in = (Inst *)p;
  if (len >= 3 && (m[0] & 0xF0) == 0x90 && m[2] > 0) {
    in->midi_note = m[1];
    in->midi_strum = true;
  }
}

void SetParam(void *p, const char *k, const char *v) { ((Inst *)p)->ps.Set(k, v); }
int GetParam(void *p, const char *k, char *b, int n) { return ((Inst *)p)->ps.Get(k, b, n); }

void Render(void *p, int16_t *out, int frames) {
  Inst *in = (Inst *)p;
  const mpc_mi::ParamSet &ps = in->ps;
  if (frames > 256) frames = 256;
  if (in->frames != frames) memset(in->in, 0, sizeof in->in);
  in->frames = 0;
  int model = ps.index(P_MODEL), poly = 1 << ps.index(P_POLY);
  if (model != in->model) in->part.set_model((ResonatorModel)model);
  if (poly != in->poly) in->part.set_polyphony(poly);
  in->model = model;
  in->poly = poly;

  Patch patch;
  patch.structure = ps[P_STRUCTURE];
  patch.brightness = ps[P_BRIGHTNESS];
  patch.damping = ps[P_DAMPING];
  patch.position = ps[P_POSITION];
  float gain = 2.0f * ps[P_INPUT] * ps[P_INPUT];
  float g = 2.0f * ps[P_VOLUME] * ps[P_VOLUME], mix = ps[P_MIX], w = ps[P_WIDTH];
  for (int done = 0; done < frames; done += (int)kMaxBlockSize) {
    int n = frames - done < (int)kMaxBlockSize ? frames - done : (int)kMaxBlockSize;
    float input[kMaxBlockSize] = {0}, l[kMaxBlockSize], r[kMaxBlockSize];
    for (int i = 0; i < n; ++i)
      input[i] = gain * (in->in[2 * (done + i)] + in->in[2 * (done + i) + 1]) / 65536.0f;
    PerformanceState s;
    s.internal_exciter = false;   // the input is the exciter
    s.internal_note = true;       // no V/OCT: the note is the tonic
    s.internal_strum = !in->midi_strum;
    s.strum = in->midi_strum;
    in->midi_strum = false;
    s.note = 0.0f;
    s.tonic = (in->midi_note >= 0 ? in->midi_note : (float)ps.index(P_NOTE)) + ps[P_FINE] + kTuning;
    s.fm = 0.0f;
    s.chord = (int32_t)lroundf(patch.structure * (kNumChords - 1));
    s.velocity = 1.0f;
    in->strummer.Process(input, n, &s);
    in->part.Process(s, patch, input, l, r, n);
    for (int i = 0; i < n; ++i) {
      float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * w;
      float dl = in->in[2 * (done + i)] / 32768.0f, dr = in->in[2 * (done + i) + 1] / 32768.0f;
      out[2 * (done + i)] = mpc_mi::ToS16((dl + mix * (mid + side - dl)) * g);
      out[2 * (done + i) + 1] = mpc_mi::ToS16((dr + mix * (mid - side - dr)) * g);
    }
  }
}

const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render};

}  // namespace

const mpc_engine_t *mpc_engine(void) { return &kEngine; }

extern "C" void mpc_engine_input(void *p, const int16_t *in_lr, int frames) {
  Inst *in = (Inst *)p;
  if (frames > 256) frames = 256;
  memcpy(in->in, in_lr, (size_t)frames * 2 * sizeof(int16_t));
  in->frames = frames;
}
