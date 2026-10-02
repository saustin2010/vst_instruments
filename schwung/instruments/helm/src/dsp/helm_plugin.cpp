/*
 * Helm DSP Plugin for Move Anything
 *
 * GPL-3.0 License
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

/* Plugin API definitions */
extern "C" {
#include <stdint.h>

#define MOVE_PLUGIN_API_VERSION 1
#define MOVE_SAMPLE_RATE 44100
#define MOVE_FRAMES_PER_BLOCK 128

typedef struct host_api_v1 {
  uint32_t api_version;
  int sample_rate;
  int frames_per_block;
  uint8_t *mapped_memory;
  int audio_out_offset;
  int audio_in_offset;
  void (*log)(const char *msg);
  int (*midi_send_internal)(const uint8_t *msg, int len);
  int (*midi_send_external)(const uint8_t *msg, int len);
} host_api_v1_t;

#define MOVE_PLUGIN_API_VERSION_2 2

typedef struct plugin_api_v2 {
  uint32_t api_version;
  void *(*create_instance)(const char *module_dir, const char *json_defaults);
  void (*destroy_instance)(void *instance);
  void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
  void (*set_param)(void *instance, const char *key, const char *val);
  int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
  int (*get_error)(void *instance, char *buf, int buf_len);
  void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

typedef plugin_api_v2_t *(*move_plugin_init_v2_fn)(const host_api_v1_t *host);
#define MOVE_PLUGIN_INIT_V2_SYMBOL "move_plugin_init_v2"
}

/* Helm/JUCE headers */
#include "load_save.h"
#include "midi_manager.h"
#include "synth_base.h"

/* Host API reference */
static const host_api_v1_t *g_host = nullptr;

extern "C" void plugin_log(const char *msg) {
  if (g_host && g_host->log) {
    char buf[512];
    snprintf(buf, sizeof(buf), "[helm] %s", msg);
    g_host->log(buf);
  }
}

/* =====================================================================
 * Headless Helm instance class
 * ===================================================================== */
class HelmMoveInstance : public SynthBase {
public:
  HelmMoveInstance() : SynthBase() {}
  ~HelmMoveInstance() override {}

  using SynthBase::getConnection;

  const juce::CriticalSection &getCriticalSection() override { return lock_; }
  SynthGuiInterface *getGuiInterface() override { return nullptr; }

  void forceProcessModulationChanges() {
    processModulationChanges();
  }

  void prepare(double sample_rate, int buffer_size) {
    engine_.setSampleRate(sample_rate);
    engine_.setBufferSize(buffer_size);
    midi_manager_->setSampleRate(sample_rate);
  }

  void process(juce::AudioSampleBuffer *buffer, juce::MidiBuffer &midi_messages,
               int num_samples) {
    processControlChanges();
    processModulationChanges();
    processMidi(midi_messages, 0, num_samples);
    processAudio(buffer, 2, num_samples, 0);
  }

private:
  juce::CriticalSection lock_;
};

/* =====================================================================
 * Instance structure
 * ===================================================================== */
typedef struct helm_instance_t {
  char module_dir[256];
  char error_msg[256];

  HelmMoveInstance *synth;

  int current_preset;
  int preset_count;
  int octave_transpose;
  float output_gain;
  char preset_name[64];

  /* Preset list */
  juce::Array<juce::File> patches;
  std::map<std::string, juce::String> save_info;

  /* Patch Categories */
  struct category_entry {
    char name[64];
    int first_idx;
  };
  std::vector<category_entry> categories;

  /* Modulation Slots */
  struct virtual_mod_slot {
    bool enabled;
    int source_idx;
    int dest_idx;
    float amount;
    mopo::ModulationConnection *active_conn;
  };
  virtual_mod_slot mod_slots[16];

  /* Thread-safe MIDI queue */
  juce::CriticalSection midi_lock;
  juce::MidiBuffer midi_queue;

  /* Auto BPM Sync */
  bool sync_bpm;
  double last_clock_time;
  int clock_count;
  double clock_time_sum;

  /* Pre-built JSON strings */
  char *ui_hierarchy_json;
  char *chain_params_json;

  helm_instance_t() {
    memset(module_dir, 0, sizeof(module_dir));
    memset(error_msg, 0, sizeof(error_msg));
    synth = nullptr;
    current_preset = 0;
    preset_count = 0;
    octave_transpose = 0;
    output_gain = 0.5f;
    memset(preset_name, 0, sizeof(preset_name));
    sync_bpm = true;
    last_clock_time = 0.0;
    clock_count = 0;
    clock_time_sum = 0.0;
    ui_hierarchy_json = nullptr;
    chain_params_json = nullptr;

    for (int i = 0; i < 16; i++) {
      mod_slots[i].enabled = false;
      mod_slots[i].source_idx = 0;
      mod_slots[i].dest_idx = 0;
      mod_slots[i].amount = 0.0f;
      mod_slots[i].active_conn = nullptr;
    }
  }
} helm_instance_t;

static const char *MOD_SOURCES[] = {
  "none",           "mono_lfo_1",   "mono_lfo_2",   "poly_lfo",
  "step_sequencer", "mod_envelope", "fil_envelope", "amp_envelope",
  "pitch_wheel",    "mod_wheel",    "aftertouch",   "velocity",
  "note",           "random"
};

static const char *MOD_DESTINATIONS[] = {
  "none",
  "cutoff",
  "resonance",
  "volume",
  "bpm",
  "osc_1_volume",
  "osc_2_volume",
  "sub_volume",
  "noise_volume",
  "osc_1_transpose",
  "osc_2_transpose",
  "osc_1_tune",
  "osc_2_tune",
  "osc_1_waveform",
  "osc_2_waveform",
  "sub_waveform",
  "osc_1_unison_voices",
  "osc_2_unison_voices",
  "osc_1_unison_detune",
  "osc_2_unison_detune",
  "cross_modulation",
  "sub_shuffle",
  "osc_feedback_transpose",
  "osc_feedback_amount",
  "osc_feedback_tune",
  "poly_lfo_waveform",
  "poly_lfo_amplitude",
  "poly_lfo_frequency",
  "mono_lfo_1_waveform",
  "mono_lfo_1_amplitude",
  "mono_lfo_1_frequency",
  "mono_lfo_2_waveform",
  "mono_lfo_2_amplitude",
  "mono_lfo_2_frequency",
  "num_steps",
  "step_smoothing",
  "step_frequency",
  "arp_frequency",
  "arp_octaves",
  "arp_pattern",
  "arp_gate",
  "mod_attack",
  "mod_decay",
  "mod_sustain",
  "mod_release",
  "fil_attack",
  "fil_decay",
  "fil_sustain",
  "fil_release",
  "fil_env_depth",
  "amp_attack",
  "amp_decay",
  "amp_sustain",
  "amp_release",
  "keytrack",
  "filter_drive",
  "filter_blend",
  "stutter_frequency",
  "stutter_resample_frequency",
  "stutter_softness",
  "delay_frequency",
  "delay_feedback",
  "delay_dry_wet",
  "reverb_feedback",
  "reverb_damping",
  "reverb_dry_wet",
  "formant_x",
  "formant_y",
  "velocity_track",
  "portamento"
};

static const int NUM_MOD_DESTINATIONS =
    sizeof(MOD_DESTINATIONS) / sizeof(MOD_DESTINATIONS[0]);

static int get_source_idx(const std::string &source) {
  for (int i = 0; i < 14; i++) {
    if (source == MOD_SOURCES[i])
      return i;
  }
  return 0;
}

static int get_dest_idx(const std::string &dest) {
  std::string target = dest;
  if (target == "beats_per_minute")
    target = "bpm";
  for (int i = 0; i < NUM_MOD_DESTINATIONS; i++) {
    if (target == MOD_DESTINATIONS[i])
      return i;
  }
  return 0;
}

static std::string get_destinations_options_json() {
  std::string result = "[";
  for (int i = 0; i < NUM_MOD_DESTINATIONS; i++) {
    if (i > 0)
      result += ",";
    result += "\"";
    result += MOD_DESTINATIONS[i];
    result += "\"";
  }
  result += "]";
  return result;
}

static void sync_mod_slots_from_synth(helm_instance_t *inst) {
  for (int i = 0; i < 16; i++) {
    inst->mod_slots[i].enabled = false;
    inst->mod_slots[i].source_idx = 0;
    inst->mod_slots[i].dest_idx = 0;
    inst->mod_slots[i].amount = 0.0f;
    inst->mod_slots[i].active_conn = nullptr;
  }
  if (!inst || !inst->synth)
    return;

  int slot_idx = 0;
  for (mopo::ModulationConnection *conn :
       inst->synth->getModulationConnections()) {
    if (slot_idx >= 16)
      break;
    if (!conn)
      continue;

    inst->mod_slots[slot_idx].enabled = true;
    inst->mod_slots[slot_idx].source_idx = get_source_idx(conn->source);
    inst->mod_slots[slot_idx].dest_idx = get_dest_idx(conn->destination);
    inst->mod_slots[slot_idx].amount = conn->amount.value();
    inst->mod_slots[slot_idx].active_conn = conn;
    slot_idx++;
  }
}

static void apply_slot_to_synth(helm_instance_t *inst, int i) {
  if (!inst || !inst->synth)
    return;

  auto &slot = inst->mod_slots[i];

  bool should_disconnect = false;
  if (slot.active_conn) {
    if (!slot.enabled || slot.source_idx == 0 || slot.dest_idx == 0 ||
        slot.amount == 0.0f) {
      should_disconnect = true;
    } else {
      std::string current_source = MOD_SOURCES[slot.source_idx];
      std::string current_dest = MOD_DESTINATIONS[slot.dest_idx];
      if (current_dest == "bpm")
        current_dest = "beats_per_minute";
      if (slot.active_conn->source != current_source ||
          slot.active_conn->destination != current_dest) {
        should_disconnect = true;
      }
    }
  }

  if (should_disconnect && slot.active_conn) {
    inst->synth->disconnectModulation(slot.active_conn);
    slot.active_conn = nullptr;
  }

  if (slot.enabled && slot.source_idx > 0 && slot.dest_idx > 0 &&
      slot.amount != 0.0f) {
    std::string new_source = MOD_SOURCES[slot.source_idx];
    std::string new_dest = MOD_DESTINATIONS[slot.dest_idx];
    if (new_dest == "bpm")
      new_dest = "beats_per_minute";

    if (!slot.active_conn) {
      slot.active_conn = inst->synth->getConnection(new_source, new_dest);
      if (!slot.active_conn) {
        slot.active_conn =
            inst->synth->getModulationBank().get(new_source, new_dest);
      }
    }

    if (slot.active_conn) {
      inst->synth->setModulationAmount(slot.active_conn, slot.amount);
    }
  }
}

/* =====================================================================
 * Preset loading
 * ===================================================================== */
static void load_preset_by_index(helm_instance_t *inst, int idx) {
  if (!inst->synth || idx < 0 || idx >= inst->preset_count)
    return;

  LoadSave::loadPatchFile(inst->patches[idx], inst->synth, inst->save_info);
  inst->current_preset = idx;

  juce::String name = inst->patches[idx].getFileNameWithoutExtension();
  strncpy(inst->preset_name, name.toRawUTF8(), sizeof(inst->preset_name) - 1);
  inst->preset_name[sizeof(inst->preset_name) - 1] = '\0';

  char msg[128];
  snprintf(msg, sizeof(msg), "Loaded preset [%d]: %s", idx, inst->preset_name);
  plugin_log(msg);

  inst->synth->forceProcessModulationChanges();
  sync_mod_slots_from_synth(inst);
}

/* =====================================================================
 * JSON builders for ui_hierarchy and chain_params
 * ===================================================================== */
static void build_ui_hierarchy(helm_instance_t *inst) {
  std::string json =
      "{"
        "\"modes\":null,"
        "\"levels\":{"
        "\"root\":{"
        "\"list_param\":\"preset\","
        "\"count_param\":\"preset_count\","
        "\"name_param\":\"preset_name\","
        "\"children\":\"main\","
        "\"knobs\":[\"cutoff\",\"resonance\",\"fil_env_depth\",\"amp_attack\","
        "\"amp_decay\",\"amp_sustain\",\"amp_release\",\"volume\"],"
        "\"params\":["
        "{\"level\":\"category_jump\",\"label\":\"Jump to Category\"}"
        "]"
      "},"
      "\"main\":{"
        "\"children\":null,"
        "\"knobs\":[\"cutoff\",\"resonance\",\"fil_env_depth\",\"amp_attack\","
        "\"amp_decay\",\"amp_sustain\",\"amp_release\",\"volume\"],"
        "\"params\":["
          "{\"level\":\"category_jump\",\"label\":\"Jump to Category\"},"
          "{\"level\":\"osc1\",\"label\":\"Oscillator 1\"},"
          "{\"level\":\"osc2\",\"label\":\"Oscillator 2\"},"
          "{\"level\":\"sub_osc\",\"label\":\"Sub Oscillator\"},"
          "{\"level\":\"noise_feedback\",\"label\":\"Noise / Feedback\"},"
          "{\"level\":\"filter\",\"label\":\"Filter\"},"
          "{\"level\":\"amp_env\",\"label\":\"Amp Envelope\"},"
          "{\"level\":\"filter_env\",\"label\":\"Filter Envelope\"},"
          "{\"level\":\"mod_env\",\"label\":\"Mod Envelope\"},"
          "{\"level\":\"mono_lfo_1\",\"label\":\"Mono LFO 1\"},"
          "{\"level\":\"mono_lfo_2\",\"label\":\"Mono LFO 2\"},"
          "{\"level\":\"poly_lfo\",\"label\":\"Poly LFO\"},"
          "{\"level\":\"step_sequencer\",\"label\":\"Step Sequencer\"},"
          "{\"level\":\"arpeggiator\",\"label\":\"Arpeggiator\"},"
          "{\"level\":\"formant\",\"label\":\"Formant\"},"
          "{\"level\":\"distortion\",\"label\":\"Distortion\"},"
          "{\"level\":\"delay\",\"label\":\"Delay\"},"
          "{\"level\":\"reverb\",\"label\":\"Reverb\"},"
          "{\"level\":\"stutter\",\"label\":\"Stutter\"},"
          "{\"level\":\"modulations\",\"label\":\"Modulations\"},"
          "{\"level\":\"settings\",\"label\":\"Settings\"}"
        "]"
      "},"
      "\"category_jump\":{"
        "\"label\":\"Jump to Category\","
        "\"items_param\":\"category_list\","
        "\"select_param\":\"jump_to_category\","
        "\"navigate_to\":\"root\","
        "\"children\":null,"
        "\"knobs\":[],"
        "\"params\":[]"
      "},"
      "\"osc1\":{"
        "\"children\":null,"
        "\"knobs\":[\"osc_1_waveform\",\"osc_1_volume\",\"osc_1_transpose\","
          "\"osc_1_tune\",\"osc_1_unison_voices\",\"osc_1_unison_detune\",\"unison_1_harmonize\"],"
        "\"params\":[\"osc_1_waveform\",\"osc_1_volume\",\"osc_1_transpose\","
          "\"osc_1_tune\",\"osc_1_unison_voices\",\"osc_1_unison_detune\",\"unison_1_harmonize\"]"
      "},"
      "\"osc2\":{"
        "\"children\":null,"
        "\"knobs\":[\"osc_2_waveform\",\"osc_2_volume\",\"osc_2_transpose\","
          "\"osc_2_tune\",\"osc_2_unison_voices\",\"osc_2_unison_detune\",\"unison_2_harmonize\"],"
        "\"params\":[\"osc_2_waveform\",\"osc_2_volume\",\"osc_2_transpose\","
          "\"osc_2_tune\",\"osc_2_unison_voices\",\"osc_2_unison_detune\",\"unison_2_harmonize\"]"
      "},"
      "\"sub_osc\":{"
        "\"children\":null,"
        "\"knobs\":[\"sub_waveform\",\"sub_volume\",\"sub_octave\",\"sub_shuffle\"],"
        "\"params\":[\"sub_waveform\",\"sub_volume\",\"sub_octave\",\"sub_shuffle\"]"
      "},"
      "\"noise_feedback\":{"
        "\"children\":null,"
        "\"knobs\":[\"noise_volume\",\"osc_feedback_amount\",\"osc_feedback_transpose\",\"osc_feedback_tune\"],"
        "\"params\":[\"noise_volume\",\"osc_feedback_amount\",\"osc_feedback_transpose\",\"osc_feedback_tune\"]"
      "},"
      "\"filter\":{"
        "\"children\":null,"
        "\"knobs\":[\"filter_on\",\"filter_style\",\"filter_blend\",\"filter_shelf\",\"cutoff\",\"resonance\",\"keytrack\",\"filter_drive\"],"
        "\"params\":[\"filter_on\",\"filter_style\",\"filter_blend\",\"filter_shelf\",\"cutoff\",\"resonance\",\"keytrack\",\"filter_drive\"]"
        "},"
      "\"amp_env\":{"
        "\"children\":null,"
        "\"knobs\":[\"amp_attack\",\"amp_decay\",\"amp_sustain\",\"amp_release\"],"
        "\"params\":[\"amp_attack\",\"amp_decay\",\"amp_sustain\",\"amp_release\"]"
      "},"
      "\"filter_env\":{"
        "\"children\":null,"
        "\"knobs\":[\"fil_attack\",\"fil_decay\",\"fil_sustain\",\"fil_release\",\"fil_env_depth\"],"
        "\"params\":[\"fil_attack\",\"fil_decay\",\"fil_sustain\",\"fil_release\",\"fil_env_depth\"]"
      "},"
      "\"mod_env\":{"
        "\"children\":null,"
        "\"knobs\":[\"mod_attack\",\"mod_decay\",\"mod_sustain\",\"mod_release\"],"
        "\"params\":[\"mod_attack\",\"mod_decay\",\"mod_sustain\",\"mod_release\"]"
      "},"
      "\"mono_lfo_1\":{"
        "\"children\":null,"
        "\"knobs\":[\"mono_lfo_1_waveform\",\"mono_lfo_1_amplitude\",\"mono_lfo_1_frequency\",\"mono_lfo_1_sync\",\"mono_lfo_1_tempo\",\"mono_lfo_1_retrigger\"],"
        "\"params\":[\"mono_lfo_1_waveform\",\"mono_lfo_1_amplitude\",\"mono_lfo_1_frequency\",\"mono_lfo_1_sync\",\"mono_lfo_1_tempo\",\"mono_lfo_1_retrigger\"]"
      "},"
      "\"mono_lfo_2\":{"
        "\"children\":null,"
        "\"knobs\":[\"mono_lfo_2_waveform\",\"mono_lfo_2_amplitude\",\"mono_lfo_2_frequency\",\"mono_lfo_2_sync\",\"mono_lfo_2_tempo\",\"mono_lfo_2_retrigger\"],"
        "\"params\":[\"mono_lfo_2_waveform\",\"mono_lfo_2_amplitude\",\"mono_lfo_2_frequency\",\"mono_lfo_2_sync\",\"mono_lfo_2_tempo\",\"mono_lfo_2_retrigger\"]"
      "},"
      "\"poly_lfo\":{"
        "\"children\":null,"
        "\"knobs\":[\"poly_lfo_waveform\",\"poly_lfo_amplitude\",\"poly_lfo_frequency\",\"poly_lfo_sync\",\"poly_lfo_tempo\"],"
        "\"params\":[\"poly_lfo_waveform\",\"poly_lfo_amplitude\",\"poly_lfo_frequency\",\"poly_lfo_sync\",\"poly_lfo_tempo\"]"
      "},"
      "\"step_sequencer\":{"
        "\"children\":null,"
        "\"knobs\":[\"num_steps\",\"step_frequency\",\"step_sequencer_retrigger\",\"step_sequencer_sync\",\"step_sequencer_tempo\",\"step_smoothing\"],"
        "\"params\":[\"num_steps\",\"step_frequency\",\"step_sequencer_retrigger\",\"step_sequencer_sync\",\"step_sequencer_tempo\",\"step_smoothing\","
          // TODO: this doesn't work, floats are not shown/editable
          "\"step_seq_00\",\"step_seq_01\",\"step_seq_02\",\"step_seq_03\","
          "\"step_seq_04\",\"step_seq_05\",\"step_seq_06\",\"step_seq_07\","
          "\"step_seq_08\",\"step_seq_09\",\"step_seq_10\",\"step_seq_11\","
          "\"step_seq_12\",\"step_seq_13\",\"step_seq_14\",\"step_seq_15\","
          "\"step_seq_16\",\"step_seq_17\",\"step_seq_18\",\"step_seq_19\","
          "\"step_seq_20\",\"step_seq_21\",\"step_seq_22\",\"step_seq_23\","
          "\"step_seq_24\",\"step_seq_25\",\"step_seq_26\",\"step_seq_27\","
          "\"step_seq_28\",\"step_seq_29\",\"step_seq_30\",\"step_seq_31\"]"
      "},"
      "\"arpeggiator\":{"
        "\"children\":null,"
        "\"knobs\":[\"arp_on\",\"arp_pattern\",\"arp_sync\",\"arp_tempo\",\"arp_octaves\",\"arp_gate\",\"arp_frequency\"],"
        "\"params\":[\"arp_on\",\"arp_pattern\",\"arp_sync\",\"arp_tempo\",\"arp_octaves\",\"arp_gate\",\"arp_frequency\"]"
      "},"
      "\"formant\":{"
        "\"children\":null,"
        "\"knobs\":[\"formant_on\",\"formant_x\",\"formant_y\"],"
        "\"params\":[\"formant_on\",\"formant_x\",\"formant_y\"]"
      "},"
      "\"distortion\":{"
        "\"children\":null,"
        "\"knobs\":[\"distortion_on\",\"distortion_type\",\"distortion_drive\",\"distortion_mix\"],"
        "\"params\":[\"distortion_on\",\"distortion_type\",\"distortion_drive\",\"distortion_mix\"]"
      "},"
      "\"delay\":{"
        "\"children\":null,"
        "\"knobs\":[\"delay_on\",\"delay_dry_wet\",\"delay_feedback\",\"delay_frequency\",\"delay_sync\",\"delay_tempo\"],"
        "\"params\":[\"delay_on\",\"delay_dry_wet\",\"delay_feedback\",\"delay_frequency\",\"delay_sync\",\"delay_tempo\"]"
      "},"
      "\"reverb\":{"
        "\"children\":null,"
        "\"knobs\":[\"reverb_on\",\"reverb_dry_wet\",\"reverb_feedback\",\"reverb_damping\"],"
        "\"params\":[\"reverb_on\",\"reverb_dry_wet\",\"reverb_feedback\",\"reverb_damping\"]"
      "},"
      "\"stutter\":{"
        "\"children\":null,"
        "\"knobs\":[\"stutter_on\",\"stutter_frequency\",\"stutter_sync\","
        "\"stutter_tempo\",\"stutter_softness\",\"stutter_resample_frequency\","
        "\"stutter_resample_sync\",\"stutter_resample_tempo\"],"
        "\"params\":[\"stutter_on\",\"stutter_frequency\",\"stutter_sync\","
        "\"stutter_tempo\",\"stutter_softness\",\"stutter_resample_frequency\","
        "\"stutter_resample_sync\",\"stutter_resample_tempo\"]"
      "},"
      "\"modulations\":{"
        "\"children\":null,"
        "\"knobs\":[],"
        "\"params\":[";

  for (int i = 0; i < 8; i++) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s{\"level\":\"mod_%d\",\"label\":\"Slot %d\"}",
             (i > 0) ? "," : "", i, i + 1);
    json += buf;
  }

  json += "]"
          "},";

  std::string dest_opts = get_destinations_options_json();
  for (int i = 0; i < 8; i++) {
    char buf[2048];
    snprintf(buf, sizeof(buf),
             "\"mod_%d\":{"
             "\"children\":null,"
             "\"knobs\":[\"mod_%d_enable\",\"mod_%d_source\",\"mod_%d_dest\","
             "\"mod_%d_amount\"],"
             "\"params\":["
              "{\"key\":\"mod_%d_enable\",\"name\":\"Enabled\",\"type\":"
                "\"enum\",\"options\":[\"Off\",\"On\"]},"
              "{\"key\":\"mod_%d_source\",\"name\":\"Source\",\"type\":\"enum\","
                "\"options\":[\"none\",\"mono_lfo_1\",\"mono_lfo_2\",\"poly_lfo\","
                "\"step_sequencer\",\"mod_envelope\",\"fil_envelope\",\"amp_"
                "envelope\",\"pitch_wheel\",\"mod_wheel\",\"aftertouch\","
                "\"velocity\",\"note\",\"random\"]},"
              "{\"key\":\"mod_%d_dest\",\"name\":\"Destination\",\"type\":\"enum\",\"options\":%s},"
              "{\"key\":\"mod_%d_amount\",\"name\":\"Amount\",\"type\":\"float\",\"min\":-1.0,\"max\":1.0}"
             "]"
             "}%s",
             i, i, i, i, i, i, i, i, dest_opts.c_str(), i, (i < 7) ? "," : "");
    json += buf;
  }

  json +=
      ",\"settings\":{"
        "\"children\":null,"
        "\"knobs\":[\"bpm\",\"sync_bpm\",\"volume\",\"polyphony\",\"portamento\",\"portamento_type\",\"legato\",\"pitch_bend_range\",\"velocity_track\"],"
        "\"params\":[\"bpm\",\"sync_bpm\",\"volume\",\"polyphony\",\"portamento\",\"portamento_type\",\"legato\",\"pitch_bend_range\",\"velocity_track\"]"
      "}"
    "}"
  "}";

  inst->ui_hierarchy_json = strdup(json.c_str());
}

static std::string get_param_options_json(const std::string &key) {
  if (key == "osc_1_waveform" || key == "osc_2_waveform" ||
      key == "sub_waveform") {
    return "\"type\":\"enum\",\"options\":[\"Sine\",\"Triangle\",\"Square\","
           "\"Saw Down\",\"Saw Up\",\"3 Step\",\"4 Step\",\"8 Step\",\"3 "
           "Pyramid\",\"5 Pyramid\",\"9 Pyramid\"]";
  }
  if (key == "mono_lfo_1_waveform" || key == "mono_lfo_2_waveform" ||
      key == "poly_lfo_waveform") {
    return "\"type\":\"enum\",\"options\":[\"Sine\",\"Triangle\",\"Square\","
           "\"Saw Up\",\"Saw Down\",\"3 Step\",\"4 Step\",\"8 Step\",\"3 "
           "Pyramid\",\"5 Pyramid\",\"9 Pyramid\",\"Sample & Hold\",\"Sample & "
           "Glide\"]";
  }
  if (key == "mono_lfo_1_sync" || key == "mono_lfo_2_sync" ||
      key == "poly_lfo_sync" || key == "step_sequencer_sync" ||
      key == "delay_sync" || key == "stutter_sync" ||
      key == "stutter_resample_sync" || key == "arp_sync") {
    return "\"type\":\"enum\",\"options\":[\"Seconds\",\"Tempo\",\"Tempo "
           "Dotted\",\"Tempo Triplets\"]";
  }
  if (key == "mono_lfo_1_retrigger" || key == "mono_lfo_2_retrigger" ||
      key == "step_sequencer_retrigger") {
    return "\"type\":\"enum\",\"options\":[\"Free\",\"Retrigger\",\"Sync to "
           "Playhead\"]";
  }
  if (key == "mono_lfo_1_tempo" || key == "mono_lfo_2_tempo" ||
      key == "poly_lfo_tempo" || key == "step_sequencer_tempo" ||
      key == "delay_tempo" || key == "stutter_tempo" ||
      key == "stutter_resample_tempo" || key == "arp_tempo") {
    return "\"type\":\"enum\",\"options\":[\"32/1\",\"16/1\",\"8/1\",\"4/"
           "1\",\"2/1\",\"1/1\",\"1/2\",\"1/4\",\"1/8\",\"1/16\",\"1/32\",\"1/"
           "64\"]";
  }
  if (key == "portamento_type") {
    return "\"type\":\"enum\",\"options\":[\"Off\",\"Auto\",\"On\"]";
  }
  if (key == "legato" || key == "distortion_on" || key == "delay_on" ||
      key == "reverb_on" || key == "stutter_on" || key == "filter_on" ||
      key == "formant_on" || key == "arp_on" || key == "unison_1_harmonize" ||
      key == "unison_2_harmonize") {
    return "\"type\":\"enum\",\"options\":[\"Off\",\"On\"]";
  }
  if (key == "filter_style") {
    return "\"type\":\"enum\",\"options\":[\"12dB\",\"24dB\",\"Shelf\"]";
  }
  if (key == "filter_shelf") {
    return "\"type\":\"enum\",\"options\":[\"Low Shelf\",\"Band Shelf\",\"High "
           "Shelf\"]";
  }
  if (key == "distortion_type") {
    return "\"type\":\"enum\",\"options\":[\"Soft Clip\",\"Hard "
           "Clip\",\"Linear Fold\",\"Sine Fold\"]";
  }
  if (key == "arp_pattern") {
    return "\"type\":\"enum\",\"options\":[\"Up\",\"Down\",\"Up-Down\",\"As "
           "Played\",\"Random\"]";
  }
  return "";
}

static float get_param_step(const std::string &key) {
  if (key.rfind("attack") != std::string::npos ||
      key.rfind("decay") != std::string::npos ||
      key.rfind("release") != std::string::npos) {
    return 0.02f;
  }
  if (key.rfind("sustain") != std::string::npos) {
    return 0.02f;
  }
  if (key.rfind("volume") != std::string::npos ||
      key.rfind("mix") != std::string::npos ||
      key.rfind("dry_wet") != std::string::npos ||
      key.rfind("amplitude") != std::string::npos ||
      key.rfind("depth") != std::string::npos ||
      key.rfind("amount") != std::string::npos ||
      key == "volume" || key == "noise_volume" ||
      key == "osc_feedback_amount" || key == "filter_drive" ||
      key == "distortion_drive" || key == "distortion_mix") {
    return 0.01f;
  }
  if (key == "resonance" || key == "reverb_feedback" || key == "delay_feedback") {
    return 0.01f;
  }
  if (key == "cutoff") {
    return 0.5f;
  }
  if (key.rfind("frequency") != std::string::npos ||
      key.rfind("rate") != std::string::npos) {
    return 0.05f;
  }
  if (key.rfind("detune") != std::string::npos ||
      key.rfind("tune") != std::string::npos) {
    return 0.02f;
  }
  return 0.0f;
}

static void build_chain_params(helm_instance_t *inst) {
  std::string json = "[";
  json += "{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,"
          "\"max\":9999}";
  json += ",{\"key\":\"octave_transpose\",\"name\":\"Octave\",\"type\":\"int\","
          "\"min\":-3,\"max\":3}";
  json += ",{\"key\":\"bpm\",\"name\":\"BPM\",\"type\":\"int\",\"min\":20,"
          "\"max\":300}";
  json += ",{\"key\":\"sync_bpm\",\"name\":\"Auto BPM\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]}";

  std::map<std::string, mopo::ValueDetails> all_details =
      mopo::Parameters::lookup_.getAllDetails();
  for (const auto &item : all_details) {
    const mopo::ValueDetails &details = item.second;

    if (details.name == "beats_per_minute") {
      continue;
    }

    std::string opt_json = get_param_options_json(details.name);
    if (!opt_json.empty()) {
      char buf[512];
      snprintf(buf, sizeof(buf), ",{\"key\":\"%s\",\"name\":\"%s\",%s}",
               details.name.c_str(), details.display_name.c_str(),
               opt_json.c_str());
      json += buf;
    } else {
      const char *type_str = (details.steps > 0) ? "int" : "float";
      char buf[512];
      if (strncmp(details.name.c_str(), "step_seq_", 9) == 0) {
        snprintf(buf, sizeof(buf),
                 ",{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%f,"
                 "\"max\":%f,\"step\":0.05}",
                 details.name.c_str(), details.display_name.c_str(), type_str,
                 details.min, details.max);
      } else {
        float step = get_param_step(details.name);
        if (step > 0.0f) {
          snprintf(buf, sizeof(buf),
                   ",{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%f,"
                   "\"max\":%f,\"step\":%f}",
                   details.name.c_str(), details.display_name.c_str(), type_str,
                   details.min, details.max, step);
        } else {
          snprintf(buf, sizeof(buf),
                   ",{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"min\":%f,"
                   "\"max\":%f}",
                   details.name.c_str(), details.display_name.c_str(), type_str,
                   details.min, details.max);
        }
      }
      json += buf;
    }
  }

  for (int i = 0; i < 16; i++) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             ",{\"key\":\"mod_%d_amount\",\"name\":\"Mod %d "
             "Amt\",\"type\":\"float\",\"min\":-1.0,\"max\":1.0,\"step\":0.02}",
             i, i + 1);
    json += buf;
  }

  json += "]";
  inst->chain_params_json = strdup(json.c_str());
}

/* =====================================================================
 * Plugin API v2 Implementation
 * ===================================================================== */
static void *v2_create_instance(const char *module_dir,
                                const char *json_defaults) {
  (void)json_defaults;
  plugin_log("create_instance called");

  helm_instance_t *inst = new helm_instance_t();
  if (!inst)
    return nullptr;

  strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);
  inst->output_gain = 0.5f;
  snprintf(inst->preset_name, sizeof(inst->preset_name), "Init");
  inst->error_msg[0] = '\0';
  inst->octave_transpose = 0;

  /* Redirect JUCE HOME and config paths to writable directory on Move */
  char helm_home_path[512];
  snprintf(helm_home_path, sizeof(helm_home_path),
           "/data/UserData/schwung/helm-config");
  setenv("HOME", helm_home_path, 1);
  setenv("XDG_DATA_HOME", helm_home_path, 1);

  try {
    inst->synth = new HelmMoveInstance();
    plugin_log("HelmMoveInstance created OK");
  } catch (const std::exception &e) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Exception creating Helm instance: %s",
             e.what());
    plugin_log(msg);
    delete inst;
    return nullptr;
  } catch (...) {
    plugin_log("Unknown exception creating Helm instance");
    delete inst;
    return nullptr;
  }

  /* Configure engine for Move specs */
  inst->synth->prepare((double)MOVE_SAMPLE_RATE, MOVE_FRAMES_PER_BLOCK);

  /* Scan and load presets */
  char patches_path[512];
  snprintf(patches_path, sizeof(patches_path), "%s/helm-data/patches",
           module_dir);
  LoadSave::setCustomBankDirectory(juce::File(patches_path));

  inst->patches = LoadSave::getAllPatches();
  inst->preset_count = inst->patches.size();

  inst->categories.clear();
  std::string last_cat_name = "";
  for (int i = 0; i < inst->preset_count; i++) {
    juce::String category = inst->patches[i].getParentDirectory().getFileName();
    std::string cat_name = category.toStdString();
    if (cat_name != last_cat_name) {
      helm_instance_t::category_entry entry;
      strncpy(entry.name, cat_name.c_str(), sizeof(entry.name) - 1);
      entry.name[sizeof(entry.name) - 1] = '\0';
      entry.first_idx = i;
      inst->categories.push_back(entry);
      last_cat_name = cat_name;
    }
  }

  char msg[128];
  snprintf(msg, sizeof(msg), "Scanned %d presets and %d categories in %s",
           inst->preset_count, (int)inst->categories.size(), patches_path);
  plugin_log(msg);

  int default_idx = 0;
  for (int i = 0; i < inst->preset_count; i++) {
    juce::String name = inst->patches[i].getFileNameWithoutExtension();
    if (name == "Move Organ") {
      default_idx = i;
      break;
    }
  }
  if (inst->preset_count > 0) {
    load_preset_by_index(inst, default_idx);
  }

  build_ui_hierarchy(inst);
  build_chain_params(inst);

  return inst;
}

static void v2_destroy_instance(void *instance) {
  helm_instance_t *inst = (helm_instance_t *)instance;
  if (!inst)
    return;

  free(inst->ui_hierarchy_json);
  free(inst->chain_params_json);
  delete inst->synth;
  delete inst;
  plugin_log("Instance destroyed");
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len,
                       int source) {
  helm_instance_t *inst = (helm_instance_t *)instance;
  if (!inst || !inst->synth || len < 1)
    return;
  (void)source;

  uint8_t status = msg[0] & 0xF0;
  uint8_t channel = msg[0] & 0x0F;
  uint8_t data1 = (len > 1) ? msg[1] : 0;
  uint8_t data2 = (len > 2) ? msg[2] : 0;

  /* Auto BPM Sync calculation */
  if (msg[0] == 0xF8) {
    double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (inst->last_clock_time > 0.0) {
      double delta = now - inst->last_clock_time;
      if (delta > 0.005 && delta < 0.2) {
        inst->clock_time_sum += delta;
        inst->clock_count++;
        if (inst->clock_count >= 24) {
          double avg_delta = inst->clock_time_sum / inst->clock_count;
          double bpm = 60.0 / (avg_delta * 24.0);
          if (bpm >= 20.0 && bpm <= 300.0) {
            if (inst->sync_bpm && inst->synth) {
              mopo::control_map &controls = inst->synth->getControls();
              if (controls.count("beats_per_minute")) {
                controls["beats_per_minute"]->set(bpm / 60.0f);
              }
            }
          }
          inst->clock_time_sum = 0.0;
          inst->clock_count = 0;
        }
      } else {
        inst->clock_time_sum = 0.0;
        inst->clock_count = 0;
      }
    }
    inst->last_clock_time = now;
  }

  /* Octave transpose for Note On / Note Off */
  if (status == 0x90 || status == 0x80) {
    int note = data1 + inst->octave_transpose * 12;
    if (note < 0)
      note = 0;
    if (note > 127)
      note = 127;

    /* Reconstruct modified MIDI message bytes */
    uint8_t mod_msg[3] = {msg[0], (uint8_t)note, data2};
    juce::ScopedLock l(inst->midi_lock);
    inst->midi_queue.addEvent(juce::MidiMessage(mod_msg, len), 0);
  } else {
    juce::ScopedLock l(inst->midi_lock);
    inst->midi_queue.addEvent(juce::MidiMessage(msg, len), 0);
  }
}

static int json_get_number(const char *json, const char *key, float *out) {
  if (!json || !key)
    return -1;
  char search[64];
  snprintf(search, sizeof(search), "\"%s\":", key);
  const char *pos = strstr(json, search);
  if (!pos)
    return -1;
  pos += strlen(search);
  while (*pos == ' ' || *pos == '\t')
    pos++;
  *out = (float)atof(pos);
  return 0;
}

static void v2_set_param(void *instance, const char *key, const char *val) {
  helm_instance_t *inst = (helm_instance_t *)instance;
  if (!inst || !inst->synth)
    return;

  if (strcmp(key, "state") == 0) {
    float fval;

    /* Restore preset first (sets all engine params to preset values) */
    if (json_get_number(val, "preset", &fval) == 0) {
      int idx = (int)fval;
      if (idx >= 0 && idx < inst->preset_count) {
        load_preset_by_index(inst, idx);
      }
    }

    if (json_get_number(val, "octave_transpose", &fval) == 0) {
      inst->octave_transpose = (int)fval;
      if (inst->octave_transpose < -3)
        inst->octave_transpose = -3;
      if (inst->octave_transpose > 3)
        inst->octave_transpose = 3;
    }

    if (json_get_number(val, "sync_bpm", &fval) == 0) {
      inst->sync_bpm = (fval != 0.0f);
    }

    if (json_get_number(val, "bpm", &fval) == 0) {
      mopo::control_map &controls = inst->synth->getControls();
      if (controls.count("beats_per_minute")) {
        controls["beats_per_minute"]->set(fval / 60.0f);
      }
    }

    mopo::control_map &controls = inst->synth->getControls();
    std::map<std::string, mopo::ValueDetails> all_details =
        mopo::Parameters::lookup_.getAllDetails();
    for (const auto &item : all_details) {
      const std::string &pname = item.first;
      if (pname == "beats_per_minute")
        continue;
      if (controls.count(pname)) {
        if (json_get_number(val, pname.c_str(), &fval) == 0) {
          controls[pname]->set(fval);
        }
      }
    }

    for (int i = 0; i < 16; i++) {
      char key_enable[32], key_source[32], key_dest[32], key_amount[32];
      snprintf(key_enable, sizeof(key_enable), "mod_%d_enable", i);
      snprintf(key_source, sizeof(key_source), "mod_%d_source", i);
      snprintf(key_dest, sizeof(key_dest), "mod_%d_dest", i);
      snprintf(key_amount, sizeof(key_amount), "mod_%d_amount", i);

      bool changed = false;
      if (json_get_number(val, key_enable, &fval) == 0) {
        inst->mod_slots[i].enabled = ((int)fval != 0);
        changed = true;
      }
      if (json_get_number(val, key_source, &fval) == 0) {
        inst->mod_slots[i].source_idx = (int)fval;
        changed = true;
      }
      if (json_get_number(val, key_dest, &fval) == 0) {
        inst->mod_slots[i].dest_idx = (int)fval;
        changed = true;
      }
      if (json_get_number(val, key_amount, &fval) == 0) {
        inst->mod_slots[i].amount = fval;
        changed = true;
      }
      if (changed) {
        apply_slot_to_synth(inst, i);
      }
    }

    inst->synth->forceProcessModulationChanges();
    return;
  }

  if (strcmp(key, "preset") == 0) {
    int idx = atoi(val);
    if (idx >= 0 && idx < inst->preset_count && idx != inst->current_preset) {
      load_preset_by_index(inst, idx);
    }
    return;
  }

  if (strcmp(key, "bpm") == 0) {
    float bpm = (float)atof(val);
    mopo::control_map &controls = inst->synth->getControls();
    if (controls.count("beats_per_minute")) {
      controls["beats_per_minute"]->set(bpm / 60.0f);
    }
    return;
  }

  if (strcmp(key, "sync_bpm") == 0) {
    inst->sync_bpm = (atoi(val) != 0);
    return;
  }

  if (strcmp(key, "octave_transpose") == 0) {
    inst->octave_transpose = atoi(val);
    if (inst->octave_transpose < -3)
      inst->octave_transpose = -3;
    if (inst->octave_transpose > 3)
      inst->octave_transpose = 3;
    return;
  }

  if (strcmp(key, "all_notes_off") == 0) {
    juce::ScopedLock l(inst->midi_lock);
    inst->midi_queue.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    return;
  }

  if (strcmp(key, "jump_to_category") == 0) {
    int idx = atoi(val);
    if (idx >= 0 && idx < (int)inst->categories.size()) {
      load_preset_by_index(inst, inst->categories[idx].first_idx);
    }
    return;
  }

  if (strncmp(key, "mod_", 4) == 0) {
    int slot_idx = -1;
    char param_name[32] = {0};
    if (sscanf(key, "mod_%d_%s", &slot_idx, param_name) == 2) {
      if (slot_idx >= 0 && slot_idx < 16) {
        if (strcmp(param_name, "enable") == 0) {
          inst->mod_slots[slot_idx].enabled = (atoi(val) != 0);
          apply_slot_to_synth(inst, slot_idx);
          return;
        }
        if (strcmp(param_name, "source") == 0) {
          inst->mod_slots[slot_idx].source_idx = atoi(val);
          apply_slot_to_synth(inst, slot_idx);
          return;
        }
        if (strcmp(param_name, "dest") == 0) {
          inst->mod_slots[slot_idx].dest_idx = atoi(val);
          apply_slot_to_synth(inst, slot_idx);
          return;
        }
        if (strcmp(param_name, "amount") == 0) {
          inst->mod_slots[slot_idx].amount = (float)atof(val);
          apply_slot_to_synth(inst, slot_idx);
          return;
        }
      }
    }
  }

  /* Generic Helm parameter setter */
  mopo::control_map &controls = inst->synth->getControls();
  if (controls.count(key)) {
    float v = (float)atof(val);
    controls[key]->set(v);
    return;
  }
}

static int v2_get_param(void *instance, const char *key, char *buf,
                        int buf_len) {
  helm_instance_t *inst = (helm_instance_t *)instance;
  if (!inst)
    return -1;

  if (strcmp(key, "state") == 0) {
    int offset = 0;
    offset += snprintf(buf + offset, buf_len - offset,
                       "{\"preset\":%d,\"octave_transpose\":%d,\"sync_bpm\":%d",
                       inst->current_preset, inst->octave_transpose, inst->sync_bpm ? 1 : 0);

    mopo::control_map &controls = inst->synth->getControls();
    if (controls.count("beats_per_minute")) {
      offset += snprintf(buf + offset, buf_len - offset,
                         ",\"bpm\":%f", controls["beats_per_minute"]->value() * 60.0f);
    }

    std::map<std::string, mopo::ValueDetails> all_details =
        mopo::Parameters::lookup_.getAllDetails();
    for (const auto &item : all_details) {
      const std::string &pname = item.first;
      if (pname == "beats_per_minute")
        continue;
      if (controls.count(pname) && offset < buf_len - 128) {
        float val = controls[pname]->value();
        offset += snprintf(buf + offset, buf_len - offset,
                           ",\"%s\":%f", pname.c_str(), val);
      }
    }

    for (int i = 0; i < 16 && offset < buf_len - 256; i++) {
      offset += snprintf(buf + offset, buf_len - offset,
                         ",\"mod_%d_enable\":%d"
                         ",\"mod_%d_source\":%d"
                         ",\"mod_%d_dest\":%d"
                         ",\"mod_%d_amount\":%f",
                         i, inst->mod_slots[i].enabled ? 1 : 0,
                         i, inst->mod_slots[i].source_idx,
                         i, inst->mod_slots[i].dest_idx,
                         i, inst->mod_slots[i].amount);
    }

    if (offset < buf_len - 2) {
      offset += snprintf(buf + offset, buf_len - offset, "}");
    }
    return offset;
  }

  if (strcmp(key, "preset") == 0)
    return snprintf(buf, buf_len, "%d", inst->current_preset);
  if (strcmp(key, "bpm") == 0) {
    mopo::control_map &controls = inst->synth->getControls();
    if (controls.count("beats_per_minute")) {
      return snprintf(buf, buf_len, "%f",
                      controls["beats_per_minute"]->value() * 60.0f);
    }
    return -1;
  }

  if (strcmp(key, "sync_bpm") == 0) {
    return snprintf(buf, buf_len, "%d", inst->sync_bpm ? 1 : 0);
  }
  if (strcmp(key, "preset_count") == 0)
    return snprintf(buf, buf_len, "%d", inst->preset_count);
  if (strcmp(key, "preset_name") == 0)
    return snprintf(buf, buf_len, "%s", inst->preset_name);
  if (strcmp(key, "name") == 0)
    return snprintf(buf, buf_len, "Helm");
  if (strcmp(key, "octave_transpose") == 0)
    return snprintf(buf, buf_len, "%d", inst->octave_transpose);

  if (strcmp(key, "bank_name") == 0) {
    if (inst->current_preset >= 0 &&
        inst->current_preset < inst->preset_count) {
      juce::String category = inst->patches[inst->current_preset]
                                  .getParentDirectory()
                                  .getFileName();
      return snprintf(buf, buf_len, "%s", category.toRawUTF8());
    }
    return snprintf(buf, buf_len, "Factory Presets");
  }

  if (strcmp(key, "category_list") == 0) {
    std::string json = "[";
    for (size_t i = 0; i < inst->categories.size(); i++) {
      if (i > 0)
        json += ",";
      json += "{\"index\":" + std::to_string(i) + ",\"label\":\"";
      for (const char *p = inst->categories[i].name; *p; p++) {
        if (*p == '"' || *p == '\\') {
          json += '\\';
        }
        json += *p;
      }
      json += "\"}";
    }
    json += "]";
    int len = (int)json.size();
    if (len < buf_len) {
      strcpy(buf, json.c_str());
      return len;
    }
    return -1;
  }

  if (strcmp(key, "ui_hierarchy") == 0 && inst->ui_hierarchy_json) {
    int len = strlen(inst->ui_hierarchy_json);
    if (len < buf_len) {
      strcpy(buf, inst->ui_hierarchy_json);
      return len;
    }
    return -1;
  }
  if (strcmp(key, "chain_params") == 0 && inst->chain_params_json) {
    int len = strlen(inst->chain_params_json);
    if (len < buf_len) {
      strcpy(buf, inst->chain_params_json);
      return len;
    }
    return -1;
  }

  if (strncmp(key, "mod_", 4) == 0) {
    int slot_idx = -1;
    char param_name[32] = {0};
    if (sscanf(key, "mod_%d_%s", &slot_idx, param_name) == 2) {
      if (slot_idx >= 0 && slot_idx < 16) {
        if (strcmp(param_name, "enable") == 0) {
          return snprintf(buf, buf_len, "%d",
                          inst->mod_slots[slot_idx].enabled ? 1 : 0);
        }
        if (strcmp(param_name, "source") == 0) {
          return snprintf(buf, buf_len, "%d",
                          inst->mod_slots[slot_idx].source_idx);
        }
        if (strcmp(param_name, "dest") == 0) {
          return snprintf(buf, buf_len, "%d",
                          inst->mod_slots[slot_idx].dest_idx);
        }
        if (strcmp(param_name, "amount") == 0) {
          return snprintf(buf, buf_len, "%f", inst->mod_slots[slot_idx].amount);
        }
      }
    }
  }

  /* Generic Helm parameter getter */
  mopo::control_map &controls = inst->synth->getControls();
  if (controls.count(key)) {
    std::string opt_json = get_param_options_json(key);
    if (!opt_json.empty()) {
      return snprintf(buf, buf_len, "%d",
                      (int)std::round(controls[key]->value()));
    }
    return snprintf(buf, buf_len, "%f", controls[key]->value());
  }

  return -1;
}

static void v2_render_block(void *instance, int16_t *out_interleaved_lr,
                            int frames) {
  helm_instance_t *inst = (helm_instance_t *)instance;
  if (!inst || !inst->synth) {
    memset(out_interleaved_lr, 0, frames * 4);
    return;
  }

  /* Thread-safe retrieval of MIDI events */
  juce::MidiBuffer local_midi;
  {
    juce::ScopedLock l(inst->midi_lock);
    local_midi.addEvents(inst->midi_queue, 0, frames, 0);
    inst->midi_queue.clear();
  }

  /* Render float audio from Helm engine */
  juce::AudioSampleBuffer float_buffer(2, frames);
  float_buffer.clear();
  inst->synth->process(&float_buffer, local_midi, frames);

  /* Convert and interleave output */
  const float *left_chan = float_buffer.getReadPointer(0);
  const float *right_chan = float_buffer.getReadPointer(1);

  for (int i = 0; i < frames; i++) {
    float left = left_chan[i] * inst->output_gain;
    float right = right_chan[i] * inst->output_gain;

    int32_t l = (int32_t)(left * 32767.0f);
    int32_t r = (int32_t)(right * 32767.0f);
    if (l > 32767)
      l = 32767;
    if (l < -32768)
      l = -32768;
    if (r > 32767)
      r = 32767;
    if (r < -32768)
      r = -32768;

    out_interleaved_lr[i * 2] = (int16_t)l;
    out_interleaved_lr[i * 2 + 1] = (int16_t)r;
  }
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
  helm_instance_t *inst = (helm_instance_t *)instance;
  if (!inst || inst->error_msg[0] == '\0')
    return 0;
  return snprintf(buf, buf_len, "%s", inst->error_msg);
}

/* =====================================================================
 * Plugin API v2 export
 * ===================================================================== */
static plugin_api_v2_t g_plugin_api_v2;

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
  g_host = host;

  memset(&g_plugin_api_v2, 0, sizeof(g_plugin_api_v2));
  g_plugin_api_v2.api_version = MOVE_PLUGIN_API_VERSION_2;
  g_plugin_api_v2.create_instance = v2_create_instance;
  g_plugin_api_v2.destroy_instance = v2_destroy_instance;
  g_plugin_api_v2.on_midi = v2_on_midi;
  g_plugin_api_v2.set_param = v2_set_param;
  g_plugin_api_v2.get_param = v2_get_param;
  g_plugin_api_v2.get_error = v2_get_error;
  g_plugin_api_v2.render_block = v2_render_block;

  return &g_plugin_api_v2;
}

/* Stub for missing juce::Colour constructor to avoid compiling full
 * juce_graphics */
namespace juce {
Colour::Colour(uint32 argb) noexcept { (void)argb; }
} // namespace juce
