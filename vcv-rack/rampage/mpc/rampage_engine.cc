// Befaco Rampage (VCV Rack module, GPL-3.0) as an MPC plugin: a dual slope generator that modulates OTHER tracks.
// (steve/schwung-ports/rampage, 2026-10-02; GPL-3.0-or-later.)
//
// Rampage's own DSP (src/Rampage_dsp.hpp, unchanged from VCVRack/Befaco) runs per sample through
// steve/tools/rack/rack_shim.hpp. Around it:
//  - MIDI in (the plugin's own track): per channel, MODE TRIGGER fires the rise/fall cycle on each note-on (the
//    TRIGG input), GATE holds its IN at 10 V while any note is down (rise, hold, fall: an ASR), OFF ignores notes.
//    KEY TRACK feeds the note to the EXP CV input (faster at higher notes), for audio-rate cycling.
//  - MIDI out (steve/tools/midiout, the plugin's own ALSA port, as the sequencers): OUT A, OUT B, MIN and MAX as
//    MIDI CCs (0-10 V -> 0-127, CC 0 = off) on CC CHANNEL, sent when they change; END OF CYCLE A/B as short notes.
//    Another track takes that port as its MIDI input and MIDI-learns the CCs onto whatever it should move.
//  - Audio (AUDIO ON): OUT A left, OUT B right, DC-blocked: Rampage cycling fast is an oscillator.
#include <new>

extern "C" {
#include "engine.h"
#include "params.h"
}
#include "mi_engine.h"
#include "alsa_midi_out.h"
#include "Rampage_dsp.hpp"



namespace {

const char *const kRange[] = {"MEDIUM", "FAST", "SLOW"};
const char *const kOnOff[] = {"OFF", "ON"};
const char *const kMode[] = {"OFF", "TRIGGER", "GATE"};

enum { P_RANGE_A, P_SHAPE_A, P_RISE_A, P_FALL_A, P_CYCLE_A, P_TRIG_A,
       P_RANGE_B, P_SHAPE_B, P_RISE_B, P_FALL_B, P_CYCLE_B, P_TRIG_B, P_BALANCE,
       P_MODE_A, P_MODE_B, P_KEY_A, P_KEY_B, P_CC_CH, P_CC_A, P_CC_B, P_CC_MIN, P_CC_MAX,
       P_EOC, P_EOC_A, P_EOC_B, P_AUDIO, P_VOLUME, P_COUNT };
const mpc_mi::Param kParams[P_COUNT] = {
  {"range_a", 0, 2, 0, kRange, 3, true},
  {"shape_a", -1, 1, 0.0f, 0, 0, false},
  {"rise_a", 0, 1, 0.3f, 0, 0, false},
  {"fall_a", 0, 1, 0.5f, 0, 0, false},
  {"cycle_a", 0, 1, 0, kOnOff, 2, true},
  {"trig_a", 0, 1, 0, kOnOff, 2, true},
  {"range_b", 0, 2, 2, kRange, 3, true},
  {"shape_b", -1, 1, 0.0f, 0, 0, false},
  {"rise_b", 0, 1, 0.5f, 0, 0, false},
  {"fall_b", 0, 1, 0.5f, 0, 0, false},
  {"cycle_b", 0, 1, 1, kOnOff, 2, true},
  {"trig_b", 0, 1, 0, kOnOff, 2, true},
  {"balance", 0, 1, 0.5f, 0, 0, false},
  {"mode_a", 0, 2, 1, kMode, 3, true},
  {"mode_b", 0, 2, 0, kMode, 3, true},
  {"keytrack_a", 0, 1, 0, kOnOff, 2, true},
  {"keytrack_b", 0, 1, 0, kOnOff, 2, true},
  {"cc_channel", 1, 16, 1, 0, 0, true},
  {"cc_a", 0, 119, 20, 0, 0, true},
  {"cc_b", 0, 119, 21, 0, 0, true},
  {"cc_min", 0, 119, 0, 0, 0, true},
  {"cc_max", 0, 119, 0, 0, 0, true},
  {"eoc_notes", 0, 1, 0, kOnOff, 2, true},
  {"eoc_note_a", 0, 127, 36, 0, 0, true},
  {"eoc_note_b", 0, 127, 38, 0, 0, true},
  {"audio", 0, 1, 0, kOnOff, 2, true},
  {"volume", 0, 1, 0.5f, 0, 0, false},
};

struct Inst {
  mpc_mi::ParamSet ps;
  Rampage mod;
  rack::Module::ProcessArgs args;
  mo_port_t port;
  uint8_t held[16];
  int nheld, note;
  bool fire[2];              // a note-on to send to TRIGG A/B on the next sample
  bool kick[2];              // CYCLE just turned on: one trigger starts the loop (it only re-fires at a cycle's end)
  int cycle_was[2];
  int last_cc[4];            // last value sent per CC output (-1: none)
  int eoc_note[2];           // an end-of-cycle note playing (-1: none)
  float eoc_prev[2];
  float dc[2];               // DC blocker state per side
};

void Send(Inst *in, uint8_t a, uint8_t b, uint8_t c) {
  uint8_t m[3] = {a, b, c};
  mo_send(&in->port, m, 3);
}

void Apply(Inst *in) {   // the panel params, as Rampage's own
  const mpc_mi::ParamSet &ps = in->ps;
  static const int map[13][2] = {
    {P_RANGE_A, Rampage::RANGE_A_PARAM}, {P_SHAPE_A, Rampage::SHAPE_A_PARAM}, {P_RISE_A, Rampage::RISE_A_PARAM},
    {P_FALL_A, Rampage::FALL_A_PARAM}, {P_CYCLE_A, Rampage::CYCLE_A_PARAM}, {P_TRIG_A, Rampage::TRIGG_A_PARAM},
    {P_RANGE_B, Rampage::RANGE_B_PARAM}, {P_SHAPE_B, Rampage::SHAPE_B_PARAM}, {P_RISE_B, Rampage::RISE_B_PARAM},
    {P_FALL_B, Rampage::FALL_B_PARAM}, {P_CYCLE_B, Rampage::CYCLE_B_PARAM}, {P_TRIG_B, Rampage::TRIGG_B_PARAM},
    {P_BALANCE, Rampage::BALANCE_PARAM}};
  for (auto &m : map) in->mod.params[m[1]].setValue(ps[m[0]]);
}

void *Create(const char *) {
  Inst *in = new (std::nothrow) Inst();
  if (!in) return NULL;
  in->ps.Init(kParams, P_COUNT);
  in->args.sampleRate = 44100.f;
  in->args.sampleTime = 1.f / 44100.f;
  mo_open(&in->port, PLUG_NAME);
  for (int i = 0; i < 4; ++i) in->last_cc[i] = -1;
  in->eoc_note[0] = in->eoc_note[1] = -1;
  in->note = 60;
  in->cycle_was[0] = in->cycle_was[1] = 0;
  return in;
}

void Destroy(void *p) {
  Inst *in = (Inst *)p;
  for (int k = 0; k < 2; ++k)
    if (in->eoc_note[k] >= 0) Send(in, (uint8_t)(0x80 | (in->ps.index(P_CC_CH) - 1)), (uint8_t)in->eoc_note[k], 0);
  mo_close(&in->port);
  delete in;
}

void Midi(void *p, const uint8_t *m, int len) {
  Inst *in = (Inst *)p;
  if (len < 3) return;
  uint8_t st = m[0] & 0xF0;
  bool on = st == 0x90 && m[2] > 0, off = st == 0x80 || (st == 0x90 && m[2] == 0);
  if (on || off) {
    int j = 0;
    for (int i = 0; i < in->nheld; ++i)
      if (in->held[i] != m[1]) in->held[j++] = in->held[i];
    in->nheld = j;
  }
  if (on) {
    if (in->nheld < 16) in->held[in->nheld++] = m[1];
    in->note = m[1];
    in->fire[0] = in->fire[1] = true;
  } else if (off && in->nheld) {
    in->note = in->held[in->nheld - 1];
  }
}

void SetParam(void *p, const char *k, const char *v) { ((Inst *)p)->ps.Set(k, v); }
int GetParam(void *p, const char *k, char *b, int n) { return ((Inst *)p)->ps.Get(k, b, n); }

void Render(void *p, int16_t *out, int frames) {
  Inst *in = (Inst *)p;
  const mpc_mi::ParamSet &ps = in->ps;
  Apply(in);
  Rampage &r = in->mod;
  int mode[2] = {ps.index(P_MODE_A), ps.index(P_MODE_B)};
  for (int k = 0; k < 2; ++k) {
    int cyc = ps.index(k ? P_CYCLE_B : P_CYCLE_A);
    if (cyc && !in->cycle_was[k]) in->kick[k] = true;
    in->cycle_was[k] = cyc;
  }
  for (int k = 0; k < 2; ++k) {   // what MIDI is patched into each channel
    r.inputs[Rampage::IN_A_INPUT + k].setChannels(mode[k] == 2 ? 1 : 0);
    r.inputs[Rampage::IN_A_INPUT + k].setVoltage(mode[k] == 2 && in->nheld ? 10.f : 0.f);
    r.inputs[Rampage::TRIGG_A_INPUT + k].setChannels(mode[k] == 1 || in->kick[k] ? 1 : 0);
    bool key = ps.index(k ? P_KEY_B : P_KEY_A);
    r.inputs[Rampage::EXP_CV_A_INPUT + k].setChannels(key ? 1 : 0);
    r.inputs[Rampage::EXP_CV_A_INPUT + k].setVoltage((in->note - 60) / 12.f);
  }
  bool audio = ps.index(P_AUDIO);
  float g = 2.f * ps[P_VOLUME] * ps[P_VOLUME];
  int eoc_rise[2] = {0, 0};
  for (int i = 0; i < frames; ++i) {
    for (int k = 0; k < 2; ++k) {   // a note-on is one 10 V sample on TRIGG (Rampage's Schmitt trigger re-arms below 0.2 V)
      // (a kick waits a sample: Rack's Schmitt trigger starts high, so it must see the input low first)
      bool f = (mode[k] == 1 && in->fire[k]) || (in->kick[k] && i > 0);
      r.inputs[Rampage::TRIGG_A_INPUT + k].setVoltage(f ? 10.f : 0.f);
      if (f) in->fire[k] = in->kick[k] = false;
    }
    r.process(in->args);
    in->args.frame++;
    for (int k = 0; k < 2; ++k) {
      float eoc = r.outputs[Rampage::EOC_A_OUTPUT + k].getVoltage();
      if (eoc > 5.f && in->eoc_prev[k] <= 5.f) eoc_rise[k] = 1;
      in->eoc_prev[k] = eoc;
    }
    float s[2];
    for (int k = 0; k < 2; ++k) {
      float v = r.outputs[Rampage::OUT_A_OUTPUT + k].getVoltage() / 10.f;
      in->dc[k] += (v - in->dc[k]) * 0.0015f;   // ~10 Hz DC blocker
      s[k] = audio ? (v - in->dc[k]) * g : 0.f;
    }
    out[2 * i] = mpc_mi::ToS16(s[0]);
    out[2 * i + 1] = mpc_mi::ToS16(s[1]);
  }
  for (int k = 0; k < 2; ++k) in->fire[k] = false;   // OFF / GATE channels drop their note-on

  // MIDI out, once per block: CCs that changed, and end-of-cycle notes
  uint8_t ch = (uint8_t)(ps.index(P_CC_CH) - 1);
  const int outs[4] = {Rampage::OUT_A_OUTPUT, Rampage::OUT_B_OUTPUT, Rampage::MIN_OUTPUT, Rampage::MAX_OUTPUT};
  const int ccs[4] = {ps.index(P_CC_A), ps.index(P_CC_B), ps.index(P_CC_MIN), ps.index(P_CC_MAX)};
  for (int j = 0; j < 4; ++j) {
    if (!ccs[j]) continue;
    int v = (int)lroundf(rack::clamp(r.outputs[outs[j]].getVoltage(), 0.f, 10.f) * 12.7f);
    if (v != in->last_cc[j]) {
      Send(in, (uint8_t)(0xB0 | ch), (uint8_t)ccs[j], (uint8_t)v);
      in->last_cc[j] = v;
    }
  }
  for (int k = 0; k < 2; ++k) {
    if (in->eoc_note[k] >= 0) {   // last block's pulse ends
      Send(in, (uint8_t)(0x80 | ch), (uint8_t)in->eoc_note[k], 0);
      in->eoc_note[k] = -1;
    }
    if (eoc_rise[k] && ps.index(P_EOC)) {
      int n = ps.index(k ? P_EOC_B : P_EOC_A);
      Send(in, (uint8_t)(0x90 | ch), (uint8_t)n, 100);
      in->eoc_note[k] = n;
    }
  }
}

const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render};

}  // namespace

const mpc_engine_t *mpc_engine(void) { return &kEngine; }
