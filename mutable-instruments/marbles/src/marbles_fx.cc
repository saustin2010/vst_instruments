// Marbles (Mutable Instruments random sampler) as a Schwung-style MIDI FX module, for the MPC through
// steve/tools/midifx (notes leave by the plugin's own MIDI port, clocked by MPC's transport).
// (steve/schwung-ports/marbles, 2026-10-01; MIT, like Mutable's code it drives.)
//
// Mutable's T and X/Y generators run unchanged at 44.1 kHz. Their external clock is MPC's: a gate at RATE BASE
// (default sixteenths) made from the 24-PPQN MIDI clock the adapter feeds in, so CLOCK DIV and T RANGE multiply or
// divide that, as the module's RATE knob does with a patched clock. Three voices, as the module's outputs pair up:
// T1 plays X1, T2 (the steady clock) plays X2, T3 plays X3. A gate's rise is a note-on at its X voltage (sampled two
// samples later, the module's own gate delay), 1 V = 1 octave above BASE NOTE; its fall is the note-off. Each voice
// has its own MIDI channel (1/2/3), or all go out on channel 1. Start resets both sections (the module's RESET).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <new>

extern "C" {
#include "plugin_api_v1.h"
#include "midi_fx_api_v1.h"
}
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "marbles_scales.h"

using namespace marbles;
using stmlib::GateFlags;

namespace {

const host_api_v1_t *g_host;
const float kSr = 44100.0f;
const int kBlock = 8;
const int kGateDelay = 2;

const char *const kTModels[] = {"COMPLEMENTARY", "CLUSTERS", "DRUMS", "INDEPENDENT", "DIVIDER", "THREE STATES",
                                "MARKOV"};
const char *const kTRange[] = {"0.25X", "1X", "4X"};
const char *const kDiv[] = {"1/4", "1/3", "1/2", "2/3", "1:1", "3/2", "2X", "3X", "4X"};
const char *const kBase[] = {"QUARTERS", "EIGHTHS", "SIXTEENTHS"};
const char *const kDejaVu[] = {"OFF", "ON", "LOCKED"};
const char *const kXRange[] = {"2 OCTAVES", "5 OCTAVES", "10 OCTAVES"};
const char *const kScales[] = {"MAJOR", "MINOR", "PENTATONIC", "PELOG", "BHAIRAV", "SHRI"};
const char *const kXMode[] = {"IDENTICAL", "BUMP", "TILT"};
const char *const kChannels[] = {"SPLIT 1-2-3", "ALL ON 1"};
const char *const kOnOff[] = {"OFF", "ON"};

enum { DEJA_VU_OFF, DEJA_VU_ON, DEJA_VU_LOCKED };   // marbles/settings.h

struct Param { const char *key; float min, max, def; const char *const *opts; bool integer; };
enum { P_T_MODEL, P_T_RANGE, P_DIV, P_BASE, P_T_BIAS, P_JITTER, P_PW, P_PW_RAND, P_T_DEJA_VU,
       P_SPREAD, P_X_BIAS, P_STEPS, P_X_RANGE, P_SCALE, P_X_MODE, P_X_DEJA_VU, P_DEJA_VU, P_LENGTH,
       P_NOTE, P_VELOCITY, P_CHANNELS, P_T1, P_T2, P_T3, P_COUNT };
const Param kParams[P_COUNT] = {
  {"t_model", 0, 6, 0, kTModels, true},
  {"t_range", 0, 2, 1, kTRange, true},
  {"clock_div", 0, 8, 4, kDiv, true},
  {"rate_base", 0, 2, 2, kBase, true},
  {"t_bias", 0, 1, 0.5f, 0, false},
  {"jitter", 0, 1, 0.0f, 0, false},
  {"gate_len", 0, 1, 0.5f, 0, false},
  {"gate_rand", 0, 1, 0.0f, 0, false},
  {"t_deja_vu", 0, 2, 0, kDejaVu, true},
  {"spread", 0, 1, 0.5f, 0, false},
  {"x_bias", 0, 1, 0.5f, 0, false},
  {"steps", 0, 1, 0.7f, 0, false},
  {"x_range", 0, 2, 0, kXRange, true},
  {"scale", 0, 5, 0, kScales, true},
  {"x_mode", 0, 2, 0, kXMode, true},
  {"x_deja_vu", 0, 2, 0, kDejaVu, true},
  {"deja_vu", 0, 1, 0.5f, 0, false},
  {"length", 1, 16, 8, 0, true},
  {"base_note", 0, 96, 48, 0, true},
  {"velocity", 1, 127, 100, 0, true},
  {"channels", 0, 1, 0, kChannels, true},
  {"t1_out", 0, 1, 1, kOnOff, true},
  {"t2_out", 0, 1, 1, kOnOff, true},
  {"t3_out", 0, 1, 1, kOnOff, true},
};

struct Instance {
  float v[P_COUNT];
  RandomGenerator random_generator;
  RandomStream random_stream;
  TGenerator t;
  XYGenerator xy;
  float ramp[kBlock * 4];
  GateFlags clock_state;
  bool running, clock_high, reset;
  long clock_ticks;
  bool gate[3];
  int pending[3];         // samples until a risen gate reads its X voltage (-1: none)
  int playing[3];         // the note each voice holds (-1: none)
  uint8_t queue[48][3];   // note events waiting for room in the adapter's output
  int queued;
};

int Index(const Instance *m, int i) { return (int)lroundf(m->v[i]); }

void Queue(Instance *m, uint8_t status, int note, int vel) {
  if (m->queued < 48) {
    m->queue[m->queued][0] = status;
    m->queue[m->queued][1] = (uint8_t)note;
    m->queue[m->queued][2] = (uint8_t)vel;
    ++m->queued;
  }
}

int Drain(Instance *m, uint8_t out[][3], int lens[], int max) {
  int n = m->queued < max ? m->queued : max;
  for (int i = 0; i < n; ++i) {
    memcpy(out[i], m->queue[i], 3);
    lens[i] = 3;
  }
  memmove(m->queue, m->queue + n, (m->queued - n) * 3);
  m->queued -= n;
  return n;
}

int Channel(const Instance *m, int voice) { return Index(m, P_CHANNELS) == 0 ? voice : 0; }

void NoteOff(Instance *m, int k) {
  if (m->playing[k] >= 0) Queue(m, (uint8_t)(0x80 | Channel(m, k)), m->playing[k], 0);
  m->playing[k] = -1;
}

float DejaVu(float d) {   // the module's deadband around 12 o'clock (marbles.cc)
  if (d < 0.47f) return d * 1.06382978723f;
  if (d > 0.53f) return 0.5f + (d - 0.53f) * 1.06382978723f;
  return 0.5f;
}

void Configure(Instance *m, GroupSettings *x, GroupSettings *y) {
  float dv = DejaVu(m->v[P_DEJA_VU]);
  int td = Index(m, P_T_DEJA_VU), xd = Index(m, P_X_DEJA_VU);
  m->t.set_model((TGeneratorModel)Index(m, P_T_MODEL));
  m->t.set_range((TGeneratorRange)Index(m, P_T_RANGE));
  float r = (Index(m, P_DIV) + 0.5f) / 9.0f;   // the RATE knob position that picks this clock ratio
  m->t.set_rate((r - 0.5f) * 96.0f / 1.05f);
  m->t.set_bias(m->v[P_T_BIAS]);
  m->t.set_jitter(m->v[P_JITTER]);
  m->t.set_deja_vu(td == DEJA_VU_LOCKED ? 0.5f : (td == DEJA_VU_ON ? dv : 0.0f));
  m->t.set_length(Index(m, P_LENGTH));
  m->t.set_pulse_width_mean(m->v[P_PW]);
  m->t.set_pulse_width_std(m->v[P_PW_RAND]);

  x->control_mode = (ControlMode)Index(m, P_X_MODE);
  x->voltage_range = (VoltageRange)Index(m, P_X_RANGE);
  x->register_mode = false;
  x->register_value = 0.0f;
  x->spread = m->v[P_SPREAD];
  x->bias = m->v[P_X_BIAS];
  x->steps = m->v[P_STEPS];
  x->deja_vu = xd == DEJA_VU_LOCKED ? 0.5f : (xd == DEJA_VU_ON ? dv : 0.0f);
  x->length = Index(m, P_LENGTH);
  x->ratio.p = 1;
  x->ratio.q = 1;
  x->scale_index = Index(m, P_SCALE);
  y->control_mode = CONTROL_MODE_IDENTICAL;   // the module's default Y settings
  y->voltage_range = VOLTAGE_RANGE_FULL;
  y->register_mode = false;
  y->register_value = 0.0f;
  y->spread = 0.5f;
  y->bias = 0.5f;
  y->steps = 0.0f;
  y->deja_vu = 0.0f;
  y->length = 1;
  y->ratio.p = 1;
  y->ratio.q = 8;
  y->scale_index = x->scale_index;
}

// ---- midi_fx_api_v1 -------------------------------------------------------------------------------------------
void *Create(const char *, const char *) {
  Instance *m = new (std::nothrow) Instance();
  if (!m) return NULL;
  for (int i = 0; i < P_COUNT; ++i) m->v[i] = kParams[i].def;
  m->random_generator.Init(0x6d61726bu ^ (uint32_t)(uintptr_t)m);
  m->random_stream.Init(&m->random_generator);
  m->t.Init(&m->random_stream, kSr);
  m->xy.Init(&m->random_stream, kSr);
  for (int i = 0; i < 6; ++i) m->xy.LoadScale(i, preset_scales[i]);
  m->clock_state = stmlib::GATE_FLAG_LOW;
  for (int k = 0; k < 3; ++k) {
    m->pending[k] = -1;
    m->playing[k] = -1;
  }
  return m;
}

void Destroy(void *p) { delete (Instance *)p; }

int ProcessMidi(void *p, const uint8_t *msg, int len, uint8_t out[][3], int lens[], int max) {
  Instance *m = (Instance *)p;
  if (len < 1) return 0;
  if (msg[0] == 0xFA) {
    m->running = true;
    m->clock_ticks = 0;
    m->reset = true;
  } else if (msg[0] == 0xFC) {
    m->running = false;
    m->clock_high = false;
    for (int k = 0; k < 3; ++k) {
      m->pending[k] = -1;
      NoteOff(m, k);
    }
    return Drain(m, out, lens, max);
  } else if (msg[0] == 0xF8 && m->running) {
    static const int kTicks[] = {24, 12, 6};   // MIDI clocks per base pulse: quarters, eighths, sixteenths
    int period = kTicks[Index(m, P_BASE)];
    m->clock_high = (m->clock_ticks % period) < period / 2;
    ++m->clock_ticks;
  }
  return 0;   // notes played into the track are not passed on
}

int Tick(void *p, int frames, int, uint8_t out[][3], int lens[], int max) {
  Instance *m = (Instance *)p;
  GroupSettings x, y;
  Configure(m, &x, &y);
  for (int f = 0; f < frames; f += kBlock) {
    int size = frames - f < kBlock ? frames - f : kBlock;
    GateFlags clock[kBlock];
    for (int i = 0; i < size; ++i)
      clock[i] = m->clock_state = stmlib::ExtractGateFlags(m->clock_state, m->running && m->clock_high);
    Ramps ramps;
    ramps.external = &m->ramp[0];
    ramps.master = &m->ramp[kBlock];
    ramps.slave[0] = &m->ramp[kBlock * 2];
    ramps.slave[1] = &m->ramp[kBlock * 3];
    bool gates[kBlock * 2];
    float volts[kBlock * 4];
    bool t_reset = m->reset, xy_reset = m->reset;
    m->reset = false;
    m->t.Process(true, &t_reset, clock, ramps, gates, size);
    m->xy.Process(CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, &xy_reset, clock, ramps, volts, size);
    for (int i = 0; i < size; ++i) {
      bool g[3] = {gates[2 * i], ramps.master[i] < 0.5f, gates[2 * i + 1]};
      for (int k = 0; k < 3; ++k) {
        if (m->pending[k] >= 0 && m->pending[k]-- == 0) {   // the voltage has settled: play it
          int note = Index(m, P_NOTE) + (int)lroundf(volts[4 * i + k] * 12.0f);
          note = note < 0 ? 0 : (note > 127 ? 127 : note);
          NoteOff(m, k);
          Queue(m, (uint8_t)(0x90 | Channel(m, k)), note, Index(m, P_VELOCITY));
          m->playing[k] = note;
        }
        bool enabled = Index(m, P_T1 + k) && m->running;
        if (g[k] && !m->gate[k] && enabled) m->pending[k] = kGateDelay;
        if (!g[k] && m->gate[k]) {
          if (m->pending[k] >= 0) m->pending[k] = -1;   // a pulse shorter than the gate delay: skip it
          NoteOff(m, k);
        }
        m->gate[k] = g[k];
      }
    }
  }
  return Drain(m, out, lens, max);
}

void SetParam(void *p, const char *key, const char *val) {
  Instance *m = (Instance *)p;
  if (!strcmp(key, "state")) {
    char tmp[1024], *save = NULL;
    snprintf(tmp, sizeof tmp, "%s", val);
    for (char *kv = strtok_r(tmp, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
      char *eq = strchr(kv, '=');
      if (eq && strncmp(kv, "state", 5)) { *eq = 0; SetParam(p, kv, eq + 1); }
    }
    return;
  }
  for (int i = 0; i < P_COUNT; ++i) {
    if (strcmp(key, kParams[i].key)) continue;
    float v = -1;
    if (kParams[i].opts)
      for (int o = 0; o <= (int)kParams[i].max && v < 0; ++o)
        if (!strcasecmp(val, kParams[i].opts[o])) v = (float)o;
    if (v < 0) v = (float)atof(val);
    if (kParams[i].integer) v = roundf(v);
    m->v[i] = v < kParams[i].min ? kParams[i].min : (v > kParams[i].max ? kParams[i].max : v);
    if (i >= P_T1 && i <= P_T3 && !Index(m, i)) NoteOff(m, i - P_T1);
    return;
  }
}

int GetParam(void *p, const char *key, char *buf, int len) {
  Instance *m = (Instance *)p;
  for (int i = 0; i < P_COUNT; ++i) {
    if (strcmp(key, kParams[i].key)) continue;
    if (kParams[i].opts) return snprintf(buf, len, "%s", kParams[i].opts[Index(m, i)]);
    if (kParams[i].integer) return snprintf(buf, len, "%d", Index(m, i));
    return snprintf(buf, len, "%.3f", m->v[i]);
  }
  if (!strcmp(key, "state")) {
    int n = 0;
    for (int i = 0; i < P_COUNT && n < len; ++i) {
      char v[32];
      GetParam(p, kParams[i].key, v, sizeof v);
      n += snprintf(buf + n, len - n, "%s%s=%s", i ? ";" : "", kParams[i].key, v);
    }
    return n;
  }
  return -1;
}

midi_fx_api_v1_t g_api = {MIDI_FX_API_VERSION, Create, Destroy, ProcessMidi, Tick, SetParam, GetParam};

}  // namespace

extern "C" midi_fx_api_v1_t *move_midi_fx_init(const host_api_v1_t *host) {
  g_host = host;
  return &g_api;
}
