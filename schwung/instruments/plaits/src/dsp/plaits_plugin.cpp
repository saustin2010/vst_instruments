// Plaits macro oscillator plugin for Move Anything
// Based on Mutable Instruments Plaits by Emilie Gillet
// Port by charlesvestal

extern "C" {
#include "host/plugin_api_v2.h"
}

#include "plaits/dsp/voice.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <new>
#include <algorithm>

using std::min;
using std::max;

// ──────────────────────────────────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────────────────────────────────

static const int   BLOCK_SIZE   = 128;
static const int   BUFFER_SIZE  = 32768;  // 32KB for voice engine buffers

static const int   LEGATO_OFF = 0;
static const int   LEGATO_ON  = 1;

static constexpr float kOutputVolume = 0.85f;  // internal output scalar, not user-exposed

static const int kNumFmPresets = 32;

// Preset names extracted from syx bank data (bytes 118-127 of each 128-byte patch).
// Indexed as kFmPresetNames[bank][preset], where bank 0/1/2 = engine 2/3/4.
static const char* kFmPresetNames[3][32] = {
    // Bank 0: 6-Op I — Basses & Synths
    {
        "SOLID BASS", "Mooger Low", "LeaderTape", "MORHOL TB1",
        "BASS    3",  "BILL BASS",  "BASS    1",  "ELEC BASS",
        "S.BAS 27.7", "RESONANCES", "SYN-BASS 2", "PRC SYNTH1",
        "CROMA 2",    "ANALOG  4",  "ANALOG A",   "ANALOG  6",
        "CS 80",      "INSERT 1",   "SPIRAL",     "DX-TROTT",
        "GASHAUS",    "RING DING",  "PAPAGAYO",   "WINEGLASS",
        "AMYTAL",     "FAIRLIGHT",  "*PPG*Vol.1", "*PPG*Vol.2",
        "*Fairl. 3",  "*Vocoder 2", "*Sequence",  "Bounce 4",
    },
    // Bank 1: 6-Op II — Keys & Plucked
    {
        "E.PIANO 1",  "FENDER 1",   "WINTRHODES", "RS-EP C",
        "*Mark III",  "CLAV-E.PNO", "SYN-CLAV",   "CLAVINET",
        "PIANO   5",  "GRD PIANO1", "STEINWAY",   "GUIT ACOUS",
        "SITAR",      "KOTO",       "HARPSICH 1", "CLAV    3",
        "XYLOPHONE",  "MARIMBA",    "VIBE    1",  "GLOKENSPL",
        "BELL C",     "BELLS",      "TUB BELLS",  "GONG    2",
        "KETTLE 6",   "MID DRM 3",  "ORI DRUM 1", "WOOD 6",
        "LATN DRM 4", "CIMBAL",     "SYNDM 25.8", "B.DRM-SNAR",
    },
    // Bank 2: 6-Op III — Organs, Pads & Brass
    {
        "CLICK 124",  "*Hammond 1", "E.ORGAN 3",  "60-S ORGAN",
        "OPTIC 28",   "PIPES   1",  "PIPES   3",  "PIPES   2",
        "JX-33-P",    "SOUNDTRACK", "ICE PAD  2", "M1 PADS",
        "CARLOS   2", "SOFT TOUCH", "*Planets",   "CIRRUS",
        "ENTRIX",     "MAL POLY",   "Textures 6", "Etherial5a",
        "'Airy'",     "BORON A",    "VANGELIS 1", "STRINGS C",
        "STRINGS 3",  "STRINGS 2",  "STRINGS 7",  "FULL STRIN",
        "SYN-ORCH",   "BRASS   1",  "BRASS 6 BC", "BR TRUMPET",
    },
};

// ──────────────────────────────────────────────────────────────────────────
// Per-engine default parameter values.
// Applied ONCE when an engine is first visited in a session.
// Subsequent visits restore last-used values — not re-applied.
// Only covers: harmonics, timbre, morph, decay, lpg_colour.
// fm_amount, timbre_mod, morph_mod, and aux_mix are NOT reset on engine switch.
// ──────────────────────────────────────────────────────────────────────────

namespace {
struct EngineDefaults {
    float harmonics, timbre, morph, decay, lpg_colour, velocity_sensitivity;
};

// Indexed by engine registration order from plaits/dsp/voice.cc Voice::Init().
constexpr EngineDefaults kEngineDefaults[24] = {
    //   harm   tim    mor    dec    lpg    vel_s
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  //  0  VA VCF
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  //  1  Phase Dist
    { 0.5f, 0.5f, 0.5f, 0.7f, 0.5f, 0.0f },  //  2  6-Op I      (vel_sens=0: full level)
    { 0.5f, 0.5f, 0.5f, 0.7f, 0.5f, 0.0f },  //  3  6-Op II     (vel_sens=0: full level)
    { 0.5f, 0.5f, 0.5f, 0.7f, 0.5f, 0.0f },  //  4  6-Op III    (vel_sens=0: full level)
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  //  5  Wave Terr
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  //  6  Str Mach
    { 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f },  //  7  Chiptune    (morph=0 prevents self-osc)
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  //  8  V. Analog
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  //  9  Waveshape
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 10  FM
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 11  Grain
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 12  Additive
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 13  Wavetable
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 14  Chord
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 15  Speech
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 16  Swarm
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 17  Noise
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 18  Particle
    { 0.5f, 0.5f, 0.3f, 0.6f, 0.5f, 0.5f },  // 19  String      (lower morph, longer decay)
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },  // 20  Modal
    { 0.5f, 0.5f, 0.5f, 0.4f, 0.5f, 0.5f },  // 21  Bass Drum   (slightly shorter decay)
    { 0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.5f },  // 22  Snare Drum  (shorter decay)
    { 0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.5f },  // 23  Hi-Hat      (shorter decay)
};
} // namespace

// ──────────────────────────────────────────────────────────────────────────
// Instance struct
// ──────────────────────────────────────────────────────────────────────────

struct plaits_instance_t {
    // Plaits voice engine
    plaits::Voice voice;
    plaits::Patch patch;
    plaits::Modulations mods;
    stmlib::BufferAllocator allocator;
    char shared_buffer[BUFFER_SIZE];

    // Note state
    int  current_note;    // MIDI note 0-127
    bool note_active;     // Is a note currently held?
    bool trigger_pending; // True for one render cycle after Note On
    float velocity;       // Last note velocity, 0.0-1.0
    int  legato_mode;     // LEGATO_OFF or LEGATO_ON

    // Note stack for mono voice stealing with return-to-held
    static const int NOTE_STACK_SIZE = 16;
    uint8_t note_stack[NOTE_STACK_SIZE];  // MIDI notes, oldest at [0]
    float   vel_stack[NOTE_STACK_SIZE];   // velocity for each stacked note
    int     note_stack_len;               // number of notes currently held

    // Parameters
    int   engine;
    float harmonics;
    float timbre;
    float morph;
    float decay;
    float lpg_colour;
    float fm_amount;
    float timbre_mod;
    float morph_mod;
    float aux_mix;
    float velocity_sensitivity;
    int   octave_transpose;
    int   fm_preset;          // 0-31, selects preset within 6-Op FM banks (engines 2-4)
    float attack;             // 0.0 = instant, 1.0 = ~2s fade-in
#ifdef MPC_PORT
    float volume;             // MPC port: output level (gain 2 * v^2; 0.7071 = unity, the Move port's level)
#endif

    // Attack envelope state
    float attack_env;         // current attack envelope level, 0.0-1.0

    // Render frame buffer (plaits::Voice::Frame has interleaved short out/aux)
    plaits::Voice::Frame frame_buf[BLOCK_SIZE];

    // Engine switch tracking
    int  previous_engine;  // detects change in set_param to trigger voice reset
    bool engine_visited[24]; // true after first visit; prevents re-applying defaults

    // Chiptune arpeggiator self-clock and gate envelope
    float chiptune_clock_phase;  // 0-1 accumulator for arp self-clock
    float chiptune_gate_env;     // amplitude envelope for note-off release
};

// ──────────────────────────────────────────────────────────────────────────
// create_instance
// ──────────────────────────────────────────────────────────────────────────

static void* create_instance(const char* module_dir, const char* json_defaults) {
    (void)module_dir;
    (void)json_defaults;

    // Must use new (not calloc) — Engine subclasses have virtual functions;
    // calloc zeros vtable pointers causing a crash on the first virtual call.
    plaits_instance_t* inst = new (std::nothrow) plaits_instance_t();
    if (!inst) return nullptr;

    // Initialize Plaits voice
    inst->allocator.Init(inst->shared_buffer, BUFFER_SIZE);
    inst->voice.Init(&inst->allocator);

    // Default parameter values
    inst->engine             = 0;
    inst->previous_engine    = -1;  // sentinel: forces reset on first set_param("engine", ...)
    // Mark engine 0 as visited — defaults applied below from kEngineDefaults[0]
    memset(inst->engine_visited, 0, sizeof(inst->engine_visited));
    inst->engine_visited[0] = true;
    inst->harmonics          = kEngineDefaults[0].harmonics;
    inst->timbre             = kEngineDefaults[0].timbre;
    inst->morph              = kEngineDefaults[0].morph;
    inst->decay              = kEngineDefaults[0].decay;
    inst->lpg_colour         = kEngineDefaults[0].lpg_colour;
    inst->fm_amount          = 0.0f;
    inst->timbre_mod         = 0.0f;
    inst->morph_mod          = 0.0f;
    inst->aux_mix            = 0.0f;
    inst->velocity_sensitivity = 0.5f;
    inst->octave_transpose   = 0;
    inst->fm_preset          = 0;
    inst->attack             = 0.0f;
#ifdef MPC_PORT
    inst->volume             = 0.70711f;
#endif
    inst->attack_env         = 1.0f;  // start fully open (no attack)
    inst->legato_mode        = LEGATO_OFF;

    // Note state
    inst->current_note    = 60;
    inst->note_active     = false;
    inst->trigger_pending = false;
    inst->velocity        = 1.0f;
    inst->note_stack_len  = 0;

    inst->chiptune_clock_phase = 0.0f;
    inst->chiptune_gate_env    = 0.0f;

    return inst;
}

// ──────────────────────────────────────────────────────────────────────────
// destroy_instance
// ──────────────────────────────────────────────────────────────────────────

static void destroy_instance(void* instance) {
    delete (plaits_instance_t*)instance;
}

// ──────────────────────────────────────────────────────────────────────────
// on_midi
// ──────────────────────────────────────────────────────────────────────────

static void on_midi(void* instance, const uint8_t* msg, int len, int source) {
    (void)source;
    if (len < 3) return;

    plaits_instance_t* inst = (plaits_instance_t*)instance;
    uint8_t status = msg[0] & 0xF0;
    uint8_t note   = msg[1];
    uint8_t vel    = msg[2];

    if (status == 0x90 && vel > 0) {
        // Note On — remove if already in stack (re-press), then push
        for (int i = 0; i < inst->note_stack_len; i++) {
            if (inst->note_stack[i] == note) {
                // Shift down to remove duplicate
                for (int j = i; j < inst->note_stack_len - 1; j++) {
                    inst->note_stack[j] = inst->note_stack[j + 1];
                    inst->vel_stack[j]  = inst->vel_stack[j + 1];
                }
                inst->note_stack_len--;
                break;
            }
        }
        // Push onto top of stack
        if (inst->note_stack_len < plaits_instance_t::NOTE_STACK_SIZE) {
            inst->note_stack[inst->note_stack_len] = note;
            inst->vel_stack[inst->note_stack_len]  = vel / 127.0f;
            inst->note_stack_len++;
        }

        // Retrigger logic
        bool retrigger = true;
        if (inst->legato_mode == LEGATO_ON && inst->note_active) {
            retrigger = false;
        }
        inst->current_note    = (int)note;
        inst->velocity        = vel / 127.0f;
        inst->note_active     = true;
        inst->trigger_pending = retrigger;
        // Reset attack envelope on retrigger (instant if attack == 0)
        if (retrigger && inst->attack > 0.0f) {
            inst->attack_env = 0.0f;
        }

    } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
        // Note Off — remove from stack
        for (int i = 0; i < inst->note_stack_len; i++) {
            if (inst->note_stack[i] == note) {
                for (int j = i; j < inst->note_stack_len - 1; j++) {
                    inst->note_stack[j] = inst->note_stack[j + 1];
                    inst->vel_stack[j]  = inst->vel_stack[j + 1];
                }
                inst->note_stack_len--;
                break;
            }
        }

        if (inst->note_stack_len > 0) {
            // Return to the most recent still-held note
            int top = inst->note_stack_len - 1;
            inst->current_note    = (int)inst->note_stack[top];
            inst->velocity        = inst->vel_stack[top];
            inst->note_active     = true;
            inst->trigger_pending = true;  // retrigger so LPG re-opens
        } else {
            // No notes held — release
            inst->note_active     = false;
            inst->trigger_pending = false;
        }

    } else if (status == 0xB0) {
        // Control Change
        uint8_t cc = msg[1];
        if (cc == 123 || cc == 120) {
            // All Notes Off / All Sound Off
            inst->note_active     = false;
            inst->trigger_pending = false;
            inst->note_stack_len  = 0;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Engine names (registration order from plaits::Voice::Init)
// ──────────────────────────────────────────────────────────────────────────

static const char* kEngineNames[] = {
    "VA VCF",     // 0  Virtual Analog VCF  [engine2]
    "Phase Dist", // 1  Phase Distortion    [engine2]
    "6-Op I",     // 2  Six Op FM (1)       [engine2]
    "6-Op II",    // 3  Six Op FM (2)       [engine2]
    "6-Op III",   // 4  Six Op FM (3)       [engine2]
    "Wave Terr",  // 5  Wave Terrain        [engine2]
    "Str Mach",   // 6  String Machine      [engine2]
    "Chiptune",   // 7  Chiptune            [engine2]
    "V. Analog",  // 8  Virtual Analog      [engine]
    "Waveshape",  // 9  Waveshaping         [engine]
    "FM",         // 10 FM                  [engine]
    "Grain",      // 11 Granular            [engine]
    "Additive",   // 12 Additive            [engine]
    "Wavetable",  // 13 Wavetable           [engine]
    "Chord",      // 14 Chord               [engine]
    "Speech",     // 15 Speech              [engine]
    "Swarm",      // 16 Swarm               [engine]
    "Noise",      // 17 Noise               [engine]
    "Particle",   // 18 Particle            [engine]
    "String",     // 19 Karplus-Strong      [engine]
    "Modal",      // 20 Modal resonator     [engine]
    "Bass Drum",  // 21                     [engine]
    "Snare Drum", // 22                     [engine]
    "Hi-Hat",     // 23                     [engine]
};
static const int kNumEngines = 24;

// Per-engine labels for Harmonics (0), Timbre (1), Morph (2).
// Strings are substituted directly into JSON in get_param("chain_params") —
// must not contain JSON special characters (", \, control chars).
static const char* kEngineLabels[24][3] = {
    {"Detune",     "Cutoff",      "Resonance" },  //  0 VA VCF
    {"Ratio",      "Phase",       "Distortion"},  //  1 Phase Dist
    {"Preset",     "Mod Level",   "Env Stretch"},  //  2 6-Op I
    {"Preset",     "Mod Level",   "Env Stretch"},  //  3 6-Op II
    {"Preset",     "Mod Level",   "Env Stretch"},  //  4 6-Op III
    {"Terrain",    "X",           "Y"         },  //  5 Wave Terrain
    {"Detune",     "Tone",        "Envelope"  },  //  6 Str Machine
    {"Arpeggio",   "Duty",        "Filter"    },  //  7 Chiptune
    {"Detune",     "Cutoff",      "Shape"     },  //  8 V. Analog
    {"Overtone",   "Waveshape",   "Fold"      },  //  9 Waveshape
    {"Ratio",      "Feedback",    "Topology"  },  // 10 FM
    {"Position",   "Size",        "Density"   },  // 11 Grain
    {"Bumps",      "Slope",       "Shift"     },  // 12 Additive
    {"Bank",       "Pointer",     "Mirror"    },  // 13 Wavetable
    {"Chord",      "Timbre",      "Waveform"  },  // 14 Chord
    {"Prosody",    "Phoneme",     "Formant"   },  // 15 Speech
    {"Detune",     "Spread",      "Chaos"     },  // 16 Swarm
    {"Frequency",  "Slope",       "Ruggedness"},  // 17 Noise
    {"Spread",     "Extinction",  "Scatter"   },  // 18 Particle
    {"Harmonic",   "Timbre",      "Morph"     },  // 19 String
    {"Harmonic",   "Timbre",      "Decay"     },  // 20 Modal
    {"Frequency",  "Punch",       "Decay"     },  // 21 Bass Drum
    {"Frequency",  "Tone",        "Decay"     },  // 22 Snare Drum
    {"Frequency",  "Tone",        "Decay"     },  // 23 Hi-Hat
};

// Per-engine gain compensation table.
// Indexed by engine registration order from plaits::Voice::Init() in voice.cc.
//
// Applied AFTER Plaits' own ChannelPostProcessor, which already bakes each
// engine's RegisterInstance gain into the Frame shorts. Do NOT attempt to
// derive these values from RegisterInstance parameters — that gain is already
// included in the short values written to frame_buf. These values are
// empirically tuned against what the hardware I2S bus would produce, to
// equalise perceived loudness across engine families.
static constexpr float kGainTable[24] = {
    1.0f,  //  0 VA VCF       (VirtualAnalogVCFEngine)
    1.0f,  //  1 Phase Dist   (PhaseDistortionEngine)
    8.0f,  //  2 6-Op I       (SixOpEngine — 0.25f internal scaling + LPG)
    8.0f,  //  3 6-Op II      (SixOpEngine — 0.25f internal scaling + LPG)
    8.0f,  //  4 6-Op III     (SixOpEngine — 0.25f internal scaling + LPG)
    1.8f,  //  5 Wave Terr    (WaveTerrainEngine)
    1.5f,  //  6 Str Mach     (StringMachineEngine)
    1.2f,  //  7 Chiptune     (ChiptuneEngine)
    1.0f,  //  8 V. Analog    (VirtualAnalogEngine)
    1.0f,  //  9 Waveshape    (WaveshapingEngine)
    2.5f,  // 10 FM           (FMEngine, two-op)
    1.1f,  // 11 Grain        (GrainEngine)
    1.0f,  // 12 Additive     (AdditiveEngine)
    1.2f,  // 13 Wavetable    (WavetableEngine)
    0.8f,  // 14 Chord        (ChordEngine)
    1.3f,  // 15 Speech       (SpeechEngine)
    0.9f,   // 16  Swarm      (SwarmEngine,       limiter path)
    1.2f,   // 17  Noise      (NoiseEngine,        limiter path)
    1.2f,   // 18  Particle   (ParticleEngine,     limiter path)
    1.0f,   // 19  String     (StringEngine,       limiter path)
    1.0f,   // 20  Modal      (ModalEngine,        limiter path)
    1.0f,  // 21 Bass Drum    (BassDrumEngine)
    1.0f,  // 22 Snare Drum   (SnareDrumEngine)
    1.0f,  // 23 Hi-Hat       (HiHatEngine)
};


// ──────────────────────────────────────────────────────────────────────────
// reset_voice  —  reinitialize Plaits voice to clear all internal state.
// Called on engine switch. Does NOT change any plugin parameters.
// Voice::Init re-registers all engines and resets post-processors, envelopes,
// and internal state. Allocator must be re-initialized first (reuses same buffer).
//
// COST: Voice::Init calls engine->Init() on all 24 engines (virtual dispatch +
// allocator re-walk). This is intentional — the only way to clear
// aux_post_processor_, lpg_envelope_, and decay_envelope_ without modifying
// vendor code. Engine switches are infrequent (user-initiated) so the cost
// is acceptable. Do not call from a hot path.
//
// Also resets previous_engine_index_ to -1 inside voice.cc, ensuring the
// next voice.Render() call sees a fresh engine selection and calls e->Reset()
// and out_post_processor_.Reset() on the new engine.
//
// THREADING: Assumes set_param is not called concurrently with render_block.
// The plugin_api_v2 contract does not document a threading model. If concurrent
// delivery is ever required, move the reset_voice call into render_block behind
// a reset_pending flag owned by the audio thread.
// ──────────────────────────────────────────────────────────────────────────

static void reset_voice(plaits_instance_t* inst) {
    inst->allocator.Init(inst->shared_buffer, BUFFER_SIZE);
    inst->voice.Init(&inst->allocator);
}

// ──────────────────────────────────────────────────────────────────────────
// JSON helper for state restore (minimal — finds "key":number in flat JSON)
// ──────────────────────────────────────────────────────────────────────────

static int json_find_number(const char* json, const char* key, float* out) {
    if (!json || !key) return -1;
    char needle[64];
    int nlen = snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = json;
    while ((p = strstr(p, needle)) != NULL) {
        // Verify exact key match: char after closing quote must be ':' or whitespace
        char after = p[nlen];
        if (after == ':' || after == ' ') {
            p += nlen;
            while (*p == ' ' || *p == ':') p++;
            *out = (float)atof(p);
            return 0;
        }
        p += nlen;
    }
    return -1;
}

static int json_find_string(const char* json, const char* key, char* out, int out_len) {
    if (!json || !key) return -1;
    char needle[64];
    int nlen = snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = json;
    while ((p = strstr(p, needle)) != NULL) {
        char after = p[nlen];
        if (after == ':' || after == ' ') {
            p += nlen;
            while (*p == ' ' || *p == ':') p++;
            if (*p != '"') return -1;
            p++;
            int i = 0;
            while (*p && *p != '"' && i < out_len - 1) {
                out[i++] = *p++;
            }
            out[i] = '\0';
            return 0;
        }
        p += nlen;
    }
    return -1;
}

// ──────────────────────────────────────────────────────────────────────────
// set_param
// ──────────────────────────────────────────────────────────────────────────

static void set_param(void* instance, const char* key, const char* val) {
    plaits_instance_t* inst = (plaits_instance_t*)instance;
#ifdef MPC_PORT
    /* MPC port: the FM patch as a plain index (the names differ per 6-Op bank, so a browser steps the number and
     * shows "fm_preset"'s name) */
    if (strcmp(key, "fm_preset_index") == 0) key = "fm_preset";
    /* MPC port: VOLUME, so presets can be levelled (the engines differ by ~30 dB) */
    if (strcmp(key, "volume") == 0) {
        float v = (float)atof(val);
        inst->volume = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
        return;
    }
#endif

    // State restore from patch save/load
    if (strcmp(key, "state") == 0) {
        if (!val) return;
        float fval;

        // Restore engine first (affects other param interpretation)
        char engine_str[32];
        if (json_find_string(val, "engine", engine_str, sizeof(engine_str)) == 0) {
            // Reuse set_param to handle engine switching logic
            set_param(instance, "engine", engine_str);
        } else if (json_find_number(val, "engine", &fval) == 0) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "%d", (int)fval);
            set_param(instance, "engine", tmp);
        }

        // Restore numeric params
        if (json_find_number(val, "harmonics", &fval) == 0)
            inst->harmonics = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "timbre", &fval) == 0)
            inst->timbre = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "morph", &fval) == 0)
            inst->morph = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "decay", &fval) == 0)
            inst->decay = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "lpg_colour", &fval) == 0)
            inst->lpg_colour = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "fm_amount", &fval) == 0)
            inst->fm_amount = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "timbre_mod", &fval) == 0)
            inst->timbre_mod = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "morph_mod", &fval) == 0)
            inst->morph_mod = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "aux_mix", &fval) == 0)
            inst->aux_mix = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "attack", &fval) == 0)
            inst->attack = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "velocity_sensitivity", &fval) == 0)
            inst->velocity_sensitivity = fval < 0.0f ? 0.0f : fval > 1.0f ? 1.0f : fval;
        if (json_find_number(val, "octave_transpose", &fval) == 0) {
            int v = (int)fval;
            inst->octave_transpose = v < -3 ? -3 : v > 3 ? 3 : v;
        }
        if (json_find_number(val, "fm_preset", &fval) == 0) {
            int v = (int)fval;
            inst->fm_preset = v < 0 ? 0 : v >= kNumFmPresets ? kNumFmPresets - 1 : v;
        }

        // Restore legato
        char legato_str[8];
        if (json_find_string(val, "legato", legato_str, sizeof(legato_str)) == 0) {
            inst->legato_mode = (strcmp(legato_str, "on") == 0) ? LEGATO_ON : LEGATO_OFF;
        }
        return;
    }

    if (strcmp(key, "engine") == 0) {
        // Accept engine name string (enum) or numeric index (legacy)
        int v = -1;
        for (int i = 0; i < kNumEngines; i++) {
            if (strcmp(val, kEngineNames[i]) == 0) { v = i; break; }
        }
        if (v < 0) v = atoi(val);  // fallback: numeric string
        int new_engine = (v < 0) ? 0 : (v >= kNumEngines) ? kNumEngines - 1 : v;
        if (new_engine != inst->previous_engine) {
            int old_engine = inst->previous_engine;
            inst->engine = new_engine;
            inst->previous_engine = new_engine;

            // The three 6-Op FM engines (indices 2, 3, 4) are the SAME
            // SixOpEngine instance registered three times in voice.cc.
            // Differentiation comes from LoadUserData(fm_patches_table[idx])
            // called by Voice::Render when it detects an engine_index change.
            //
            // Skip reset_voice for 6-Op <-> 6-Op switches:
            //  - reset_voice calls Voice::Init which re-inits SixOpEngine 3x
            //    on the same object, potentially corrupting shared allocator
            //    state that the engine depends on for patch bank storage.
            //  - LPG envelope state carrying over between 6-Op variants is
            //    fine — all three share the same post-processing settings.
            //  - Matches original Plaits hardware behavior: Voice::Init is
            //    called once at boot; engine switches are handled entirely
            //    by Voice::Render via previous_engine_index_ detection.
            bool old_is_6op = (old_engine >= 2 && old_engine <= 4);
            bool new_is_6op = (new_engine >= 2 && new_engine <= 4);
            if (!(old_is_6op && new_is_6op)) {
                reset_voice(inst);
            }

            // Apply per-engine defaults on first visit only.
            // Subsequent visits restore last-used values.
            if (!inst->engine_visited[new_engine]) {
                inst->engine_visited[new_engine] = true;
                const EngineDefaults& d = kEngineDefaults[new_engine];
                inst->harmonics  = d.harmonics;
                inst->timbre     = d.timbre;
                inst->morph      = d.morph;
                inst->decay      = d.decay;
                inst->lpg_colour = d.lpg_colour;
                inst->velocity_sensitivity = d.velocity_sensitivity;
            }
        }
    } else if (strcmp(key, "harmonics") == 0) {
        float v = (float)atof(val);
        inst->harmonics = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "timbre") == 0) {
        float v = (float)atof(val);
        inst->timbre = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "morph") == 0) {
        float v = (float)atof(val);
        inst->morph = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "decay") == 0) {
        float v = (float)atof(val);
        inst->decay = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "lpg_colour") == 0) {
        float v = (float)atof(val);
        inst->lpg_colour = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "fm_amount") == 0) {
        float v = (float)atof(val);
        inst->fm_amount = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "timbre_mod") == 0) {
        float v = (float)atof(val);
        inst->timbre_mod = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "morph_mod") == 0) {
        float v = (float)atof(val);
        inst->morph_mod = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "aux_mix") == 0) {
        float v = (float)atof(val);
        inst->aux_mix = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "attack") == 0) {
        float v = (float)atof(val);
        inst->attack = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "legato") == 0) {
        inst->legato_mode = (strcmp(val, "on") == 0) ? LEGATO_ON : LEGATO_OFF;
    } else if (strcmp(key, "velocity_sensitivity") == 0) {
        float v = (float)atof(val);
        inst->velocity_sensitivity = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    } else if (strcmp(key, "octave_transpose") == 0) {
        int v = atoi(val);
        inst->octave_transpose = v < -3 ? -3 : v > 3 ? 3 : v;
    } else if (strcmp(key, "fm_preset") == 0) {
        // Menu-driven preset selection for 6-Op engines.
        // Accept preset name string or numeric index.
        int e = inst->engine;
        if (e >= 2 && e <= 4) {
            int bank = e - 2;
            int v = -1;
            for (int i = 0; i < kNumFmPresets; i++) {
                if (strcmp(val, kFmPresetNames[bank][i]) == 0) { v = i; break; }
            }
            if (v < 0) v = atoi(val);
            v = v < 0 ? 0 : v >= kNumFmPresets ? kNumFmPresets - 1 : v;
            inst->fm_preset = v;
            // Sync harmonics so knob 2 position reflects menu selection.
            // Map preset index to center of its quantizer bin.
            inst->harmonics = ((float)v + 0.5f) / 32.0f;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// get_param
// ──────────────────────────────────────────────────────────────────────────

static int get_single_param(const plaits_instance_t* inst, const char* key,
                             char* buf, int buf_len) {
    if (strcmp(key, "engine") == 0)
        return snprintf(buf, buf_len, "%s", kEngineNames[inst->engine]);
#ifdef MPC_PORT
    if (strcmp(key, "fm_preset_index") == 0)
        return snprintf(buf, buf_len, "%d", inst->fm_preset);
    if (strcmp(key, "volume") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->volume);
#endif
    if (strcmp(key, "harmonics") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->harmonics);
    if (strcmp(key, "timbre") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->timbre);
    if (strcmp(key, "morph") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->morph);
    if (strcmp(key, "decay") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->decay);
    if (strcmp(key, "lpg_colour") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->lpg_colour);
    if (strcmp(key, "fm_amount") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->fm_amount);
    if (strcmp(key, "timbre_mod") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->timbre_mod);
    if (strcmp(key, "morph_mod") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->morph_mod);
    if (strcmp(key, "aux_mix") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->aux_mix);
    if (strcmp(key, "attack") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->attack);
    if (strcmp(key, "legato") == 0)
        return snprintf(buf, buf_len, "%s",
                        inst->legato_mode == LEGATO_ON ? "on" : "off");
    if (strcmp(key, "velocity_sensitivity") == 0)
        return snprintf(buf, buf_len, "%.3f", inst->velocity_sensitivity);
    if (strcmp(key, "octave_transpose") == 0)
        return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    if (strcmp(key, "fm_preset") == 0) {
        int e = inst->engine;
        if (e >= 2 && e <= 4) {
            return snprintf(buf, buf_len, "%s", kFmPresetNames[e - 2][inst->fm_preset]);
        }
        return snprintf(buf, buf_len, "%d", inst->fm_preset);
    }
    return -1;
}

static int get_param(void* instance, const char* key, char* buf, int buf_len) {
    const plaits_instance_t* inst = (const plaits_instance_t*)instance;

    // Individual param read
    int r = get_single_param(inst, key, buf, buf_len);
    if (r >= 0) return r;

    // ui_hierarchy — Shadow UI navigation structure (static; host caches at load)
    if (strcmp(key, "ui_hierarchy") == 0) {
        const char* json =
            "{"
              "\"label\":\"Plaits\","
              "\"levels\":{"
                "\"root\":{"
                  "\"label\":\"Plaits\","
                  "\"knobs\":[\"engine\",\"harmonics\",\"timbre\",\"morph\","
                              "\"decay\",\"lpg_colour\",\"fm_amount\",\"aux_mix\"],"
                  "\"params\":["
                    "{\"key\":\"engine\",\"label\":\"Engine\",\"type\":\"enum\"},"
                    "{\"key\":\"fm_preset\",\"label\":\"FM Preset\",\"type\":\"enum\"},"
                    "{\"key\":\"harmonics\",\"label\":\"Harmonics\"},"
                    "{\"key\":\"timbre\",\"label\":\"Timbre\"},"
                    "{\"key\":\"morph\",\"label\":\"Morph\"},"
                    "{\"key\":\"decay\",\"label\":\"Decay\"},"
                    "{\"key\":\"lpg_colour\",\"label\":\"LPG Color\"},"
                    "{\"key\":\"fm_amount\",\"label\":\"FM\"},"
                    "{\"key\":\"attack\",\"label\":\"Attack\"},"
                    "{\"key\":\"timbre_mod\",\"label\":\"Timbre Mod\"},"
                    "{\"key\":\"morph_mod\",\"label\":\"Morph Mod\"},"
                    "{\"key\":\"aux_mix\",\"label\":\"Mix\"},"
                    "{\"key\":\"legato\",\"label\":\"Legato\"},"
                    "{\"key\":\"velocity_sensitivity\",\"label\":\"Vel Sens\"},"
                    "{\"key\":\"octave_transpose\",\"label\":\"Octave\"}"
                  "]"
                "}"
              "}"
            "}";
        int len = (int)strlen(json);
        if (len >= buf_len) return -1;
        memcpy(buf, json, len + 1);
        return len;
    }

    // State serialization for patch save/load
    if (strcmp(key, "state") == 0) {
        int offset = 0;
        offset += snprintf(buf + offset, buf_len - offset,
            "{\"engine\":\"%s\"", kEngineNames[inst->engine]);
        offset += snprintf(buf + offset, buf_len - offset,
            ",\"harmonics\":%.4f,\"timbre\":%.4f,\"morph\":%.4f",
            inst->harmonics, inst->timbre, inst->morph);
        offset += snprintf(buf + offset, buf_len - offset,
            ",\"decay\":%.4f,\"lpg_colour\":%.4f,\"fm_amount\":%.4f",
            inst->decay, inst->lpg_colour, inst->fm_amount);
        offset += snprintf(buf + offset, buf_len - offset,
            ",\"timbre_mod\":%.4f,\"morph_mod\":%.4f,\"aux_mix\":%.4f",
            inst->timbre_mod, inst->morph_mod, inst->aux_mix);
        offset += snprintf(buf + offset, buf_len - offset,
            ",\"attack\":%.4f", inst->attack);
        offset += snprintf(buf + offset, buf_len - offset,
            ",\"legato\":\"%s\",\"velocity_sensitivity\":%.4f",
            inst->legato_mode == LEGATO_ON ? "on" : "off",
            inst->velocity_sensitivity);
        offset += snprintf(buf + offset, buf_len - offset,
            ",\"octave_transpose\":%d,\"fm_preset\":%d}",
            inst->octave_transpose, inst->fm_preset);
        if (offset < 0 || offset >= buf_len) return -1;
        return offset;
    }

    // chain_params — parameter metadata for Shadow UI knob editing
    // Returns engine-specific names for harmonics, timbre, morph.
    // For 6-Op engines (2-4), includes fm_preset enum with bank-specific preset names.
    if (strcmp(key, "chain_params") == 0) {
        const char* h = kEngineLabels[inst->engine][0];
        const char* t = kEngineLabels[inst->engine][1];
        const char* m = kEngineLabels[inst->engine][2];

        bool is_6op = (inst->engine >= 2 && inst->engine <= 4);

        // fm_preset menu entry removed from chain_params to avoid buffer
        // overflow (~550 bytes of preset names pushed 6-Op JSON past host's
        // buf_len, causing labels to not update). Preset selection is on
        // knob 2 (harmonics). fm_preset remains accessible via ui_hierarchy.

        // fm_amount: inactive for 6-Op (zeroed in render_block to prevent
        // decay envelope pitch wobble). For other engines, controls
        // frequency modulation depth.
        const char* fm_name = is_6op ? "---" : "FM";

        int len = snprintf(buf, buf_len,
            "["
              "{\"key\":\"engine\",\"name\":\"Engine\",\"type\":\"enum\","
               "\"options\":[\"VA VCF\",\"Phase Dist\",\"6-Op I\",\"6-Op II\",\"6-Op III\","
               "\"Wave Terr\",\"Str Mach\",\"Chiptune\",\"V. Analog\",\"Waveshape\","
               "\"FM\",\"Grain\",\"Additive\",\"Wavetable\",\"Chord\",\"Speech\","
               "\"Swarm\",\"Noise\",\"Particle\",\"String\",\"Modal\","
               "\"Bass Drum\",\"Snare Drum\",\"Hi-Hat\"],\"default\":\"VA VCF\","
               "\"refreshes_labels\":true},"
              "{\"key\":\"harmonics\",\"name\":\"%s\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
              "{\"key\":\"timbre\",\"name\":\"%s\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
              "{\"key\":\"morph\",\"name\":\"%s\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
              "{\"key\":\"decay\",\"name\":\"Decay\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
              "{\"key\":\"lpg_colour\",\"name\":\"LPG Color\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
              "{\"key\":\"fm_amount\",\"name\":\"%s\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.0},"
              "{\"key\":\"aux_mix\",\"name\":\"Mix\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.0},"
              "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.0},"
              "{\"key\":\"timbre_mod\",\"name\":\"Timbre Mod\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.0},"
              "{\"key\":\"morph_mod\",\"name\":\"Morph Mod\",\"type\":\"float\","
               "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.0},"
              "{\"key\":\"legato\",\"name\":\"Legato\",\"type\":\"enum\","
               "\"options\":[\"off\",\"on\"],\"default\":\"off\"},"
              "{\"key\":\"velocity_sensitivity\",\"name\":\"Vel Sens\","
               "\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
              "{\"key\":\"octave_transpose\",\"name\":\"Octave\",\"type\":\"int\","
               "\"min\":-3,\"max\":3,\"default\":0}"
            "]",
            h, t, m, fm_name);
        if (len < 0 || len >= buf_len) return -1;
        return len;
    }

    return -1;
}

// ──────────────────────────────────────────────────────────────────────────
// get_error  (stub)
// ──────────────────────────────────────────────────────────────────────────

static int get_error(void* instance, char* buf, int buf_len) {
    (void)instance; (void)buf; (void)buf_len;
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// render_block
// ──────────────────────────────────────────────────────────────────────────

// Pitch compensation: Plaits was designed for 48kHz hardware.
// At 44100Hz the pitch is slightly flat (~1.47 semitones).
// Compensate by adding semitones to the note value.
// Plaits uses kCorrectedSampleRate = 47872.34f (actual STM32 I2S clock).
// At 44100Hz: log2(47872.34 / 44100) * 12 ≈ 1.421 semitones compensation.
static const float kPitchOffset = 12.0f * log2f(47872.34f / 44100.0f);

static void render_block(void* instance, int16_t* out_lr, int frames) {
    plaits_instance_t* inst = (plaits_instance_t*)instance;

    // ── Build Patch ──────────────────────────────────────────────────────
    inst->patch.engine   = inst->engine;
    inst->patch.note     = (float)(inst->current_note + inst->octave_transpose * 12)
                           + kPitchOffset;
    inst->patch.harmonics = inst->harmonics;
    inst->patch.timbre    = inst->timbre;
    inst->patch.morph     = inst->morph;
    inst->patch.decay     = inst->decay;
    inst->patch.lpg_colour = inst->lpg_colour;
    inst->patch.frequency_modulation_amount = inst->fm_amount;
    inst->patch.timbre_modulation_amount    = inst->timbre_mod;
    inst->patch.morph_modulation_amount     = inst->morph_mod;

    // For 6-Op FM engines (2-4), harmonics knob (knob 2) directly selects
    // presets — SixOpEngine::Render quantizes (harmonics * 1.02) into 0-31
    // via HysteresisQuantizer2(32). No override needed; inst->harmonics
    // flows through naturally. Zero frequency_modulation_amount to prevent
    // decay envelope from modulating pitch (fm_amount knob is "---" for 6-Op).
    if (inst->engine >= 2 && inst->engine <= 4) {
        inst->patch.frequency_modulation_amount = 0.0f;
    }

    // ── Build Modulations ────────────────────────────────────────────────
    memset(&inst->mods, 0, sizeof(inst->mods));
    inst->mods.trigger_patched = true;
    inst->mods.level_patched   = true;

    // Trigger: pulse for one render block after Note On
    bool had_note_on = inst->trigger_pending;
    inst->mods.trigger    = had_note_on ? 1.0f : 0.0f;
    inst->trigger_pending = false;

    // Chiptune (engine 7) self-clock: the arpeggiator only advances on
    // TRIGGER_RISING_EDGE events.  On hardware these come from an external
    // clock patch; here we self-clock so the arpeggio cycles continuously
    // while a note is held.  Decay knob controls arp rate.
    if (inst->engine == 7) {
        if (had_note_on) {
            // Note-on: reset clock phase, open gate envelope
            inst->chiptune_clock_phase = 0.0f;
            inst->chiptune_gate_env = 1.0f;
        } else if (inst->note_active) {
            // Arp rate from Decay: 0 → fast (16 Hz), 1 → slow (2 Hz)
            float arp_rate = 2.0f * powf(8.0f, 1.0f - inst->decay);
            float block_time = (float)frames / 44100.0f;
            inst->chiptune_clock_phase += arp_rate * block_time;
            if (inst->chiptune_clock_phase >= 1.0f) {
                inst->chiptune_clock_phase -= floorf(inst->chiptune_clock_phase);
                inst->mods.trigger = 1.0f;
            }
        }
    }

    // Level: controls LPG amplitude (gate + velocity)
    if (inst->note_active) {
        // Scale level by velocity sensitivity:
        // At vel_sens=0: always full level regardless of velocity
        // At vel_sens=1: level is exactly the velocity value
        float vs = inst->velocity_sensitivity;
        inst->mods.level = inst->velocity * vs + (1.0f - vs);
    } else {
        inst->mods.level = 0.0f;  // Note off: LPG starts release
    }

    // ── Render ───────────────────────────────────────────────────────────
    {
        // Plaits internal buffers are kMaxBlockSize=24 frames; must render in chunks
        static const int kChunkSize = 24;
        int remaining = frames;
        int offset = 0;
        while (remaining > 0) {
            int chunk = remaining < kChunkSize ? remaining : kChunkSize;
            inst->voice.Render(inst->patch, inst->mods,
                               inst->frame_buf + offset, (size_t)chunk);
            // Only trigger on the first chunk
            inst->mods.trigger = 0.0f;
            remaining -= chunk;
            offset    += chunk;
        }
    }

    // ── Convert Frame output to int16 stereo with output routing ────────
#ifdef MPC_PORT
    const float gain = kOutputVolume * 2.0f * inst->volume * inst->volume;   /* MPC port: VOLUME */
#else
    const float gain = kOutputVolume;
#endif
    const float eg = kGainTable[inst->engine];

    // Chiptune gate envelope: chiptune uses already_enveloped=true (bypasses
    // LPG), so we apply our own amplitude envelope for note-off release.
    // Decay knob controls release time: 0 → ~10ms, 1 → ~2s.
    float ct_decay_rate = 1.0f;
    if (inst->engine == 7 && !inst->note_active) {
        float release_s = 0.01f + inst->decay * inst->decay * 2.0f;
        ct_decay_rate = 1.0f - 1.0f / (release_s * 44100.0f);
    }

    // Attack envelope: exponential rise from 0→1 on note-on.
    // attack=0 → instant (no ramp), attack=1 → ~2s rise.
    // Rate computed per-sample: coefficient approaches 1.0 smoothly.
    float attack_rate = 1.0f;  // 1.0 means "already there" (no attack)
    if (inst->attack > 0.0f && inst->attack_env < 1.0f) {
        float attack_s = 0.005f + inst->attack * inst->attack * 2.0f;
        attack_rate = 1.0f - expf(-1.0f / (attack_s * 44100.0f));
    }

    for (int i = 0; i < frames; i++) {
        // Frame values are already int16 range (short)
        float out_f = -inst->frame_buf[i].out / 32767.0f * eg;
        float aux_f = -inst->frame_buf[i].aux / 32767.0f * eg;

        // Move is mono hardware: blend OUT and AUX, write same sample to both channels.
        // Applies to all 24 engines including percussion (21-23).
        // Percussion AUX carries a separate drum voice (e.g. open vs closed hi-hat).
        // Blending is intentional — allows mixing the two drum voices via the Mix knob.
        const float blended = out_f * (1.0f - inst->aux_mix) + aux_f * inst->aux_mix;
        float sample = blended * gain;

        // Attack envelope: smooth exponential ramp up
        if (inst->attack_env < 1.0f) {
            inst->attack_env += attack_rate * (1.0f - inst->attack_env);
            if (inst->attack_env > 0.999f) inst->attack_env = 1.0f;
            sample *= inst->attack_env;
        }

        // Chiptune gate envelope: hold while note active, decay on release
        if (inst->engine == 7) {
            if (inst->note_active) {
                inst->chiptune_gate_env = 1.0f;
            } else {
                inst->chiptune_gate_env *= ct_decay_rate;
            }
            sample *= inst->chiptune_gate_env;
        }

        // Clamp and write
        const int16_t s = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, sample * 32767.0f));  // hard clip intentional
        out_lr[i * 2]     = s;
        out_lr[i * 2 + 1] = s;
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Plugin API export
// ──────────────────────────────────────────────────────────────────────────

static plugin_api_v2_t api = {
    /* api_version      = */ 2,
    /* create_instance  = */ create_instance,
    /* destroy_instance = */ destroy_instance,
    /* on_midi          = */ on_midi,
    /* set_param        = */ set_param,
    /* get_param        = */ get_param,
    /* get_error        = */ get_error,
    /* render_block     = */ render_block,
};

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t* host) {
    (void)host;
    return &api;
}
