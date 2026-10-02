// Warps (Mutable Instruments meta-modulator) as an MPC audio effect: mpc_engine_t for the repo's VST2 wrapper with
// vst.json "effect": true. (steve/schwung-ports/warps, 2026-10-01; MIT.)
//
// The module crosses a CARRIER with a MODULATOR. With CARRIER on SINE / TRIANGLE / SAW (its internal oscillator, at
// NOTE + FINE) the track's audio is the modulator: ring mod, folding, XOR, comparator, spectral morph or the vocoder
// along ALGORITHM, shaped by TIMBRE. EXTERNAL uses the input's left channel as the carrier and its right as the
// modulator (a stereo source with different sides). MODE's second entry is the module's hidden frequency shifter
// (SHIFT: 0.5 = none). Warps runs at the MPC's 44.1 kHz (its Init takes the rate); MIX blends the dry input back.
#include <new>

extern "C" {
#include "engine.h"
}
#include "mi_engine.h"
#include "warps/dsp/modulator.h"

using namespace warps;
using mpc_mi::Param;

namespace {

const char *const kCarrier[] = {"EXTERNAL", "SINE", "TRIANGLE", "SAW"};
const char *const kMode[] = {"META MOD", "FREQ SHIFTER"};
const char *const kOutput[] = {"OUT", "OUT + AUX"};

enum { P_MODE, P_ALGORITHM, P_TIMBRE, P_CARRIER, P_NOTE, P_FINE, P_LEVEL1, P_LEVEL2, P_SHIFT, P_OUTPUT, P_MIX,
       P_VOLUME, P_COUNT };
const Param kParams[P_COUNT] = {
  {"mode", 0, 1, 0, kMode, 2, true},
  {"algorithm", 0, 1, 0.25f, 0, 0, false},
  {"timbre", 0, 1, 0.5f, 0, 0, false},
  {"carrier", 0, 3, 1, kCarrier, 4, true},
  {"note", 12, 96, 48, 0, 0, true},
  {"fine", -1, 1, 0.0f, 0, 0, false},
  {"level_1", 0, 1, 0.8f, 0, 0, false},
  {"level_2", 0, 1, 0.8f, 0, 0, false},
  {"shift", 0, 1, 0.5f, 0, 0, false},
  {"output", 0, 1, 0, kOutput, 2, true},
  {"mix", 0, 1, 1.0f, 0, 0, false},
  {"volume", 0, 1, 0.7f, 0, 0, false},
};

struct Inst {
  mpc_mi::ParamSet ps;
  Modulator modulator;
  int16_t in[2 * 256];
  int frames;
};

void *Create(const char *) {
  Inst *in = new (std::nothrow) Inst();
  if (!in) return NULL;
  in->ps.Init(kParams, P_COUNT);
  in->modulator.Init(44100.0f);
  in->frames = 0;
  return in;
}

void Destroy(void *p) { delete (Inst *)p; }
void Midi(void *, const uint8_t *, int) {}
void SetParam(void *p, const char *k, const char *v) { ((Inst *)p)->ps.Set(k, v); }
int GetParam(void *p, const char *k, char *b, int n) { return ((Inst *)p)->ps.Get(k, b, n); }

void Render(void *p, int16_t *out, int frames) {
  Inst *in = (Inst *)p;
  const mpc_mi::ParamSet &ps = in->ps;
  if (frames > 256) frames = 256;
  if (in->frames != frames) memset(in->in, 0, sizeof in->in);
  in->frames = 0;

  bool shifter = ps.index(P_MODE) == 1;
  int carrier = shifter ? (ps.index(P_CARRIER) ? ps.index(P_CARRIER) : 1) : ps.index(P_CARRIER);
  in->modulator.set_easter_egg(shifter);
  Parameters *pr = in->modulator.mutable_parameters();
  pr->channel_drive[0] = ps[P_LEVEL1] * ps[P_LEVEL1];
  pr->channel_drive[1] = ps[P_LEVEL2] * ps[P_LEVEL2];
  pr->modulation_algorithm = ps[P_ALGORITHM];
  pr->modulation_parameter = ps[P_TIMBRE];
  pr->frequency_shift_pot = ps[P_SHIFT];
  pr->frequency_shift_cv = 0.0f;
  pr->phase_shift = ps[P_ALGORITHM];
  pr->note = (float)ps.index(P_NOTE) + ps[P_FINE];
  pr->carrier_shape = carrier;

  ShortFrame src[256], dst[256];
  for (int i = 0; i < frames; ++i) {
    int l = in->in[2 * i], r = in->in[2 * i + 1];
    if (carrier) {   // internal carrier: the track (both sides) modulates it; nothing on the phase-mod input
      src[i].l = 0;
      src[i].r = (short)((l + r) / 2);
    } else {
      src[i].l = (short)l;
      src[i].r = (short)r;
    }
  }
  for (int done = 0; done < frames;) {   // the module's blocks are at most kMaxBlockSize (96)
    int n = frames - done < 64 ? frames - done : 64;
    in->modulator.Process(src + done, dst + done, n);
    done += n;
  }
  float mix = ps[P_MIX], g = 2.0f * ps[P_VOLUME] * ps[P_VOLUME];
  bool stereo = ps.index(P_OUTPUT) == 1;
  for (int i = 0; i < frames; ++i) {
    float wl = dst[i].l / 32768.0f, wr = (stereo ? dst[i].r : dst[i].l) / 32768.0f;
    float dl = in->in[2 * i] / 32768.0f, dr = in->in[2 * i + 1] / 32768.0f;
    out[2 * i] = mpc_mi::ToS16((dl + mix * (wl - dl)) * g);
    out[2 * i + 1] = mpc_mi::ToS16((dr + mix * (wr - dr)) * g);
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
