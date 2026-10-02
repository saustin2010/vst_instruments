// Elements (Mutable Instruments modal synthesis voice) as an MPC instrument: mpc_engine_t for the repo's VST2
// wrapper. (steve/schwung-ports/elements, 2026-10-01; MIT.)
//
// One voice, as on the module. MIDI drives its GATE (held while any note is down; last note wins), V/OCT (the note,
// plus OCTAVE and FINE) and STRENGTH (velocity, scaled by VELOCITY). A new note while one is held re-strikes unless
// LEGATO is on (then the pitch moves and the bow/blow carry on). Pitch bend is the FM input. Panel controls map 1:1
// to the module's, smoothed per block with the module's own coefficients; the hidden "Ominous" voice is the fourth
// MODEL. Elements runs at its own 32 kHz in 16-frame blocks; mpc_mi::Resampler brings it to 44.1 kHz.
#include <new>

extern "C" {
#include "engine.h"
}
#include "mi_engine.h"
#include "elements/dsp/part.h"
#include "elements/dsp/dsp.h"

using namespace elements;
using mpc_mi::Param;

namespace {

const char *const kModels[] = {"MODAL", "STRING", "STRINGS", "OMINOUS"};
const char *const kOnOff[] = {"OFF", "ON"};

enum { P_CONTOUR, P_BOW, P_BOW_TIMBRE, P_BLOW, P_FLOW, P_BLOW_TIMBRE, P_STRIKE, P_MALLET, P_STRIKE_TIMBRE,
       P_MODEL, P_GEOMETRY, P_BRIGHTNESS, P_DAMPING, P_POSITION, P_SPACE, P_OCTAVE, P_FINE, P_SIGNATURE, P_LEGATO,
       P_BEND, P_VELOCITY, P_VOLUME, P_COUNT };
const Param kParams[P_COUNT] = {
  {"contour", 0, 1, 1.0f, 0, 0, false},
  {"bow", 0, 1, 0.0f, 0, 0, false},
  {"bow_timbre", 0, 1, 0.5f, 0, 0, false},
  {"blow", 0, 1, 0.0f, 0, 0, false},
  {"flow", 0, 1, 0.5f, 0, 0, false},
  {"blow_timbre", 0, 1, 0.5f, 0, 0, false},
  {"strike", 0, 1, 0.8f, 0, 0, false},
  {"mallet", 0, 1, 0.5f, 0, 0, false},
  {"strike_timbre", 0, 1, 0.5f, 0, 0, false},
  {"model", 0, 3, 0, kModels, 4, true},
  {"geometry", 0, 1, 0.2f, 0, 0, false},
  {"brightness", 0, 1, 0.5f, 0, 0, false},
  {"damping", 0, 1, 0.25f, 0, 0, false},
  {"position", 0, 1, 0.3f, 0, 0, false},
  {"space", 0, 1, 0.5f, 0, 0, false},
  {"octave", -3, 3, 0, 0, 0, true},
  {"fine", -1, 1, 0.0f, 0, 0, false},
  {"signature", 0, 99, 0, 0, 0, true},
  {"legato", 0, 1, 0, kOnOff, 2, true},
  {"bend_range", 0, 12, 2, 0, 0, true},
  {"velocity", 0, 1, 0.7f, 0, 0, false},
  {"volume", 0, 1, 0.7f, 0, 0, false},
};

struct Inst {
  mpc_mi::ParamSet ps;
  Part part;
  uint16_t reverb_buffer[32768];
  mpc_mi::Resampler<kMaxBlockSize> rs;
  uint8_t held[16];   // notes down, oldest first
  int nheld;
  float note, strength, bend;
  bool retrigger;     // drop the gate for one block: a new strike
  int signature;
  // the module's panel smoothing (cv_scaler.cc BIND coefficients)
  float flow, mallet, geometry, position, space;
};

inline void Smooth(float *x, float target, float k) { *x += (target - *x) * k; }

void RenderBlock(Inst *in, float *l, float *r) {   // one Elements block, kMaxBlockSize frames at 32 kHz
  const mpc_mi::ParamSet &ps = in->ps;
  if (ps.index(P_SIGNATURE) != in->signature) {   // the module seeds these from its serial number
    in->signature = ps.index(P_SIGNATURE);
    uint32_t seed = 0x5eed0000u + (uint32_t)in->signature;
    in->part.Seed(&seed, 1);
  }
  int model = ps.index(P_MODEL);
  in->part.set_easter_egg(model == 3);
  if (model < 3) in->part.set_resonator_model((ResonatorModel)model);

  Patch *p = in->part.mutable_patch();
  p->exciter_envelope_shape = ps[P_CONTOUR];
  p->exciter_bow_level = ps[P_BOW];
  p->exciter_bow_timbre = ps[P_BOW_TIMBRE] * 0.9995f;
  p->exciter_blow_level = ps[P_BLOW];
  Smooth(&in->flow, ps[P_FLOW] * 0.9995f, 0.05f);
  p->exciter_blow_meta = in->flow;
  p->exciter_blow_timbre = ps[P_BLOW_TIMBRE] * 0.9995f;
  p->exciter_strike_level = ps[P_STRIKE];
  Smooth(&in->mallet, ps[P_MALLET] * 0.9995f, 0.05f);
  p->exciter_strike_meta = in->mallet;
  p->exciter_strike_timbre = ps[P_STRIKE_TIMBRE] * 0.995f;
  Smooth(&in->geometry, ps[P_GEOMETRY] * 0.9995f, 0.05f);
  p->resonator_geometry = in->geometry;
  p->resonator_brightness = ps[P_BRIGHTNESS] * 0.9995f;
  p->resonator_damping = ps[P_DAMPING] * 0.9995f;
  Smooth(&in->position, ps[P_POSITION] * 0.9995f, 0.01f);
  p->resonator_position = in->position;
  Smooth(&in->space, ps[P_SPACE] * 2.0f, 0.01f);   // the module's SPACE knob reaches 2 (its reverb's far end)
  p->space = in->space;

  PerformanceState s;
  s.gate = in->nheld > 0 && !in->retrigger;
  in->retrigger = false;
  s.note = in->note + 12.0f * ps.index(P_OCTAVE) + ps[P_FINE];
  s.modulation = in->bend;
  float vs = ps[P_VELOCITY];
  s.strength = 1.0f - vs + vs * in->strength;
  float silence[kMaxBlockSize] = {0};
  in->part.Process(s, silence, silence, l, r, kMaxBlockSize);
}

void *Create(const char *) {
  Inst *in = new (std::nothrow) Inst();
  if (!in) return NULL;
  in->ps.Init(kParams, P_COUNT);
  in->part.Init(in->reverb_buffer);
  in->signature = -1;
  in->rs.Init(kSampleRate);
  in->nheld = 0;
  in->note = 60.0f;
  in->strength = 1.0f;
  in->bend = 0.0f;
  in->retrigger = false;
  in->flow = in->mallet = 0.5f;
  in->geometry = 0.2f;
  in->position = 0.3f;
  in->space = 1.0f;
  return in;
}

void Destroy(void *p) { delete (Inst *)p; }

void Midi(void *p, const uint8_t *m, int len) {
  Inst *in = (Inst *)p;
  if (len < 3) return;
  uint8_t st = m[0] & 0xF0;
  bool on = st == 0x90 && m[2] > 0, off = st == 0x80 || (st == 0x90 && m[2] == 0);
  if (on || off) {   // drop the note from the held list, then (note-on) add it as the newest
    int j = 0;
    for (int i = 0; i < in->nheld; ++i)
      if (in->held[i] != m[1]) in->held[j++] = in->held[i];
    in->nheld = j;
  }
  if (on) {
    if (in->nheld == 16) {
      for (int i = 1; i < 16; ++i) in->held[i - 1] = in->held[i];
      in->nheld = 15;
    }
    bool legato = in->nheld > 0 && in->ps.index(P_LEGATO);
    if (in->nheld > 0 && !legato) in->retrigger = true;
    in->held[in->nheld++] = m[1];
    in->note = m[1];
    in->strength = m[2] / 127.0f;
  } else if (off && in->nheld > 0) {
    in->note = in->held[in->nheld - 1];   // back to the newest note still held, no new strike
  } else if (st == 0xE0) {
    float b = (float)(((m[2] << 7) | m[1]) - 8192) / 8192.0f;
    in->bend = b * in->ps.index(P_BEND);
  } else if (st == 0xB0 && (m[1] == 123 || m[1] == 120)) {   // all notes / sound off
    in->nheld = 0;
  }
}

void SetParam(void *p, const char *k, const char *v) { ((Inst *)p)->ps.Set(k, v); }
int GetParam(void *p, const char *k, char *b, int n) { return ((Inst *)p)->ps.Get(k, b, n); }

void Render(void *p, int16_t *out, int frames) {
  Inst *in = (Inst *)p;
  float l[256], r[256];
  if (frames > 256) frames = 256;
  in->rs.Render(l, r, frames, [in](float *bl, float *br) { RenderBlock(in, bl, br); });
  float g = 2.0f * in->ps[P_VOLUME] * in->ps[P_VOLUME];
  for (int i = 0; i < frames; ++i) {
    out[2 * i] = mpc_mi::ToS16(l[i] * g);
    out[2 * i + 1] = mpc_mi::ToS16(r[i] * g);
  }
}

const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render};

}  // namespace

const mpc_engine_t *mpc_engine(void) { return &kEngine; }
