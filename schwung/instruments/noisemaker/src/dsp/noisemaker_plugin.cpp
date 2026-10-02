/*
 * Noisemaker — Schwung plugin_api_v2 wrapper around the TAL Noisemaker engine.
 *
 * Engine (src/dsp/Engine + src/dsp/Effects) = Patrick Kunz's TAL Noisemaker,
 * GPLv2, vendored byte-verbatim from the DISTRHO-Ports tal-noisemaker source
 * (the current TAL codebase: true 6-voice, Delay, Filter Drive, Vintage Noise,
 * Moog/State-Variable filters). This file is the thin host adapter.
 *
 * PARAM MODEL (differs from the older Nexbit port): the engine's setters take
 * NORMALIZED 0..1 for EVERYTHING — continuous params AND combo/enum params
 * (they convert internally via AudioUtils::calcComboBoxValue). So this wrapper
 * stores normalized values in inst->eng[] and passes them straight through in
 * apply_engine(); the only conversion is at the display boundary (disp<->norm),
 * where enums map a 0-based display index to the combo's normalized value.
 *
 * The Envelope Editor mod source (TAL's spline-based tempo-synced envelope) is
 * ported JUCE-free (see src/dsp/EnvelopeEditor/ + juce_shim.h). The spline SHAPE
 * is fixed per preset (installed from factory_splines.h by load_preset); only
 * Amount / Speed / Destination are user-controllable (env_amt/env_speed/env_dest).
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>

#include "Engine/SynthEngine.h"   // header-only TAL engine (single translation unit)
#include "Engine/Params.h"        // SYNTHPARAMETERS enum + NUMPARAM
#include "factory_bank.h"         // NM_FACTORY_BANK[] (generated; normalized values)
#include "factory_splines.h"      // NM_FACTORY_SPLINES[] (generated; per-preset env shapes)
#include "nm_import.h"            // .noisemakerpreset parser (imported banks)

#include <dirent.h>
#include <sys/stat.h>

/* ---- host / plugin ABI ---- */
#ifndef MOVE_SAMPLE_RATE
#define MOVE_SAMPLE_RATE 44100
#endif
#ifndef MOVE_FRAMES_PER_BLOCK
#define MOVE_FRAMES_PER_BLOCK 128
#endif

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
    int (*get_clock_status)(void);
    void *mod_emit_value;
    void *mod_clear_source;
    void *mod_host_ctx;
    float (*get_bpm)(void);
    int (*midi_inject_to_move)(const uint8_t *msg, int len);
    int (*slot_recv_channel)(void *instance);
} host_api_v1_t;

typedef struct plugin_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void (*destroy_instance)(void *instance);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int (*get_error)(void *instance, char *buf, int buf_len);
    void (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

#define MOVE_PLUGIN_API_VERSION_2 2

/* Native poly. The DISTRHO engine renders all MAX_VOICES=6 voices (the old
 * "phantom voice / notes drop at limit" bug is gone). setNumberOfVoices takes a
 * NORMALIZED value; 1.0 -> combo 6. */
#define NM_NUM_VOICES 6

static const host_api_v1_t *g_host = NULL;

/* ======================================================================== *
 *  Parameter surface
 * ======================================================================== */
enum ParamKind {
    K_PCT,      // norm 0..1   <-> display 0..100
    K_BIPOLAR,  // norm 0..1   <-> display 0..100 (50 center); UI shows +/-
    K_TOGGLE,   // norm 0/1    <-> display 0/1
    K_INT,      // combo count <-> display imin..imax (e.g. voices 1..6)
    K_ENUM,     // combo       <-> display index 0..n_opts-1 (calcComboBoxValue)
    K_LFOWAVE,  // LFO waveform: engine (int)(norm*5) -> 0..5; NOT the combo formula
    K_FBGAIN    // delay feedback: display = LOOP GAIN x100 (0..200). See below.
};

/* ---- K_FBGAIN: delay feedback, displayed as loop gain -------------------
 * TAL's delay feedback param is not a gain -- AudioUtils::getDelayFeedback
 * warps the raw 0..1 knob through
 *
 *     g(k) = 1 + (2k - 1)^3
 *
 * which is savagely steep around the middle: k=0.25 is already g=0.875
 * (~52 repeats to -60 dB before the loop EQ, ~17 after), k=0.40 is g=0.992,
 * and the entire top half k>=0.5 is g>=1.0. A linear 0..100 % readout over
 * that curve is a lie: everything musically useful lives in the bottom fifth
 * of the knob, which is exactly the "long tail at low feedback" complaint.
 * TAL's own GUI dodges this by printing getDelayFeedback(value) -- the
 * coefficient -- next to the knob rather than the knob position.
 *
 * Note that g >= 1 is NOT automatically a runaway: TalEq sits inside the
 * feedback loop and costs real level per pass (measured ~-1.8 dB/pass with
 * both cuts wide open), so the tail keeps decaying past g=1.0 and only crosses
 * into true sustain around g=1.03; any high cut pushes that past g=1.4. The
 * top of the range is therefore usable, not merely broken -- it is where you
 * pay back the loop EQ. It is still far too much of the knob.
 *
 * So we invert the warp at the DISPLAY BOUNDARY ONLY and expose the loop gain
 * itself: display d (0..200) = g x 100, engine value
 *
 *     k(g) = (1 + cbrt(g - 1)) / 2
 *
 * which is an exact bijection over the engine's full k=0..1 / g=0..2 range.
 * Consequences worth knowing:
 *   - 0..99 always decays (the delay line is contractive before the EQ even
 *     gets a say). 100+ hands the outcome to the high cut, per the note above.
 *     The self-oscillating region is still reachable -- 5 TAL factory presets
 *     live there, "PD Bionic Pad TAL" at g=1.31 -- but it is no longer half
 *     the travel.
 *   - The DSP is untouched: this is a control-law change, not an engine one.
 *     The engine still sees exactly the k it always did.
 *   - load_preset() raw-applies factory programData and never goes through
 *     disp_to_engine(), so the 256 TAL presets keep their exact TAL feedback.
 *     They simply now READ OUT in gain space like everything else.
 * This is a deliberate departure from TAL's knob taper; the reachable sound
 * range is identical. */
#define NM_FB_DISP_MAX 200

static inline float nm_fb_disp_to_norm(float disp) {
    float g = disp / 100.0f;                       // display is gain x100
    if (g < 0.0f) g = 0.0f;
    if (g > 2.0f) g = 2.0f;
    float k = (1.0f + cbrtf(g - 1.0f)) * 0.5f;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    return k;
}

static inline int nm_fb_norm_to_disp(float k) {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    float t = 2.0f * k - 1.0f;
    float g = 1.0f + t * t * t;                    // TAL's getDelayFeedback
    int d = (int)lroundf(g * 100.0f);
    if (d < 0) d = 0;
    if (d > NM_FB_DISP_MAX) d = NM_FB_DISP_MAX;
    return d;
}

#define MAX_ENUM_OPTS 12

/* ---- Macro params -------------------------------------------------------
 * Three "one knob, many params" controls, modelled on Echidna's Macros bank.
 * They are NOT SYNTHPARAMETERS; they occupy slots past the end of the engine
 * enum in inst->eng[], so find_param / build_state / restore_state / get_param
 * treat them as ordinary params with no special-casing. The preset loop (which
 * runs i < NUMPARAM) therefore cannot touch them -- so load_preset resets them
 * EXPLICITLY via reset_macros(). See that function for why persisting them
 * across a preset change is a bug, not a feature.
 *
 *   NM_M_WAVE    write-through: drives osc1/osc2 waveform + level, PW, FM,
 *                osc2 tune and ringmod along a curated sweep (NM_WAVE_STOPS).
 *                Display 0 is an OFF detent: the macro is inert and the
 *                preset's own oscillator setup stands.
 *   NM_M_TUNE2   osc2 pitch, UPWARD only: 0..24 semitones, semitone-quantized
 *                (Echidna's tune2 convention — 0 = unison, 12 = +1 octave,
 *                24 = +2). Composes additively with whatever offset the Wave
 *                macro is applying; see nm_apply_osc2_tune.
 *   NM_M_FENV_T  non-destructive time-scale over the filter env A/D/R.
 *   NM_M_AENV_T  ditto for the amp env. 50 = x1. The base A/D/R values in
 *                inst->eng[] are never rewritten, so the Filter/Amp Env pages
 *                keep showing the real knob positions and the macro is
 *                fully reversible.
 */
enum {
    NM_M_WAVE = NUMPARAM,
    NM_M_TUNE2,
    NM_M_FENV_T,
    NM_M_AENV_T,
    NM_ENG_SLOTS
};

typedef struct {
    const char *key;
    const char *name;
    int         engine_index;   // SYNTHPARAMETERS
    ParamKind   kind;
    int         imin, imax;     // K_INT range
    int         n_opts;         // K_ENUM combo-item count
    const char *opts[MAX_ENUM_OPTS];
} param_def_t;

/* Enum labels live ONLY in the `opts` field of PARAMS[] below -- that array is
 * what build_chain_params_json() emits, so it is the single source the host
 * menus and the module.json generator read. There used to be a parallel set of
 * standalone FILT_OPTS/LDST1_OPTS/... arrays here that nothing referenced; a
 * correction applied to the dead copy and not to PARAMS[] shipped anyway. Do
 * not reintroduce them.
 *
 * n_opts must match AudioUtils::getNumComboBoxItems for the same param, and
 * the label order must match the engine's destination/type enum starting at
 * its first item (the combo formula emits 1..n, never 0). */
static const param_def_t PARAMS[] = {
  /* ---- Macros (see NM_M_* above; not engine params) ---- */
  { "wave",          "Wave",          NM_M_WAVE,     K_PCT,    0,0, 0,{0} },
  { "tune2",         "Osc2 Pitch",    NM_M_TUNE2,    K_INT,    0,255, 256,{0} }, // native 8-bit -> 0..+24 st
  { "fenv_time",     "Filter Time",   NM_M_FENV_T,   K_BIPOLAR,0,0, 0,{0} },
  { "aenv_time",     "Amp Time",      NM_M_AENV_T,   K_BIPOLAR,0,0, 0,{0} },

  /* ---- Master ---- */
  { "volume",        "Volume",        VOLUME,        K_PCT,    0,0, 0,{0} },
  { "highpass",      "High Pass",     HIGHPASS,      K_PCT,    0,0, 0,{0} },

  /* ---- Oscillators ---- */
  { "osc1_wave",     "Osc1 Wave",     OSC1WAVEFORM,  K_ENUM,   0,0, 3, {"Saw","Pulse","Noise"} },
  { "osc2_wave",     "Osc2 Wave",     OSC2WAVEFORM,  K_ENUM,   0,0, 5, {"Saw","Pulse","Tri","Sine","Noise"} },
  { "osc1_vol",      "Osc1 Level",    OSC1VOLUME,    K_PCT,    0,0, 0,{0} },
  { "osc2_vol",      "Osc2 Level",    OSC2VOLUME,    K_PCT,    0,0, 0,{0} },
  { "osc3_vol",      "Sub Level",     OSC3VOLUME,    K_PCT,    0,0, 0,{0} },
  { "osc_tune",      "Master Tune",   OSCMASTERTUNE, K_BIPOLAR,0,0, 0,{0} },
  { "osc1_tune",     "Osc1 Tune",     OSC1TUNE,      K_BIPOLAR,0,0, 0,{0} },
  { "osc2_tune",     "Osc2 Tune",     OSC2TUNE,      K_BIPOLAR,0,0, 0,{0} },
  { "osc1_fine",     "Osc1 Fine",     OSC1FINETUNE,  K_BIPOLAR,0,0, 0,{0} },
  { "osc2_fine",     "Osc2 Fine",     OSC2FINETUNE,  K_BIPOLAR,0,0, 0,{0} },
  { "osc1_pw",       "Osc1 PW",       OSC1PW,        K_PCT,    0,0, 0,{0} },
  { "osc1_phase",    "Osc1 Phase",    OSC1PHASE,     K_PCT,    0,0, 0,{0} },
  { "osc2_phase",    "Osc2 Phase",    OSC2PHASE,     K_PCT,    0,0, 0,{0} },
  { "osc2_fm",       "Osc2 FM",       OSC2FM,        K_PCT,    0,0, 0,{0} },
  { "osc_sync",      "Osc Sync",      OSCSYNC,       K_TOGGLE, 0,0, 0,{0} },
  { "ringmod",       "Ring Mod",      RINGMODULATION,K_PCT,    0,0, 0,{0} },
  { "detune",        "Detune",        DETUNE,        K_PCT,    0,0, 0,{0} },
  { "bitcrush",      "Bitcrusher",    OSCBITCRUSHER, K_PCT,    0,0, 0,{0} },
  { "vintage",       "Vintage",       VINTAGENOISE,  K_PCT,    0,0, 0,{0} },

  /* ---- Filter ---- */
  { "filter_type",   "Filter Type",   FILTERTYPE,    K_ENUM,   0,0, 12,
        {"LP24","LP18","LP12","LP6","HP24","BP24","Notch","SV-LP","SV-HP","SV-BP","Moog","Moog2"} },
  { "cutoff",        "Cutoff",        CUTOFF,        K_PCT,    0,0, 0,{0} },
  { "resonance",     "Resonance",     RESONANCE,     K_PCT,    0,0, 0,{0} },
  { "keyfollow",     "Key Follow",    KEYFOLLOW,     K_PCT,    0,0, 0,{0} },
  { "filter_env",    "Filter Env",    FILTERCONTOUR, K_PCT,    0,0, 0,{0} },
  { "filter_drive",  "Filter Drive",  FILTERDRIVE,   K_PCT,    0,0, 0,{0} },

  /* ---- Filter envelope ---- */
  { "fenv_a",        "Filter Attack", FILTERATTACK,  K_PCT,    0,0, 0,{0} },
  { "fenv_d",        "Filter Decay",  FILTERDECAY,   K_PCT,    0,0, 0,{0} },
  { "fenv_s",        "Filter Sustain",FILTERSUSTAIN, K_PCT,    0,0, 0,{0} },
  { "fenv_r",        "Filter Release",FILTERRELEASE, K_PCT,    0,0, 0,{0} },

  /* ---- Amp envelope ---- */
  { "aenv_a",        "Amp Attack",    AMPATTACK,     K_PCT,    0,0, 0,{0} },
  { "aenv_d",        "Amp Decay",     AMPDECAY,      K_PCT,    0,0, 0,{0} },
  { "aenv_s",        "Amp Sustain",   AMPSUSTAIN,    K_PCT,    0,0, 0,{0} },
  { "aenv_r",        "Amp Release",   AMPRELEASE,    K_PCT,    0,0, 0,{0} },

  /* ---- LFO 1 ---- */
  { "lfo1_wave",     "LFO1 Wave",     LFO1WAVEFORM,  K_LFOWAVE,0,0, 6,
        {"Sin","Tri","Saw","Sqr","S+H","Rnd"} },
  { "lfo1_rate",     "LFO1 Rate",     LFO1RATE,      K_PCT,    0,0, 0,{0} },
  { "lfo1_amount",   "LFO1 Amount",   LFO1AMOUNT,    K_PCT,    0,0, 0,{0} },
  { "lfo1_dest",     "LFO1 Dest",     LFO1DESTINATION,K_ENUM,  0,0, 8,
        {"None","Filter","Osc1","Osc2","PW","FM","LFO2","Osc1+2"} },
  { "lfo1_sync",     "LFO1 Sync",     LFO1SYNC,      K_TOGGLE, 0,0, 0,{0} },
  { "lfo1_keytrig",  "LFO1 KeyTrig",  LFO1KEYTRIGGER,K_TOGGLE, 0,0, 0,{0} },
  { "lfo1_phase",    "LFO1 Phase",    LFO1PHASE,     K_PCT,    0,0, 0,{0} },

  /* ---- LFO 2 ---- */
  { "lfo2_wave",     "LFO2 Wave",     LFO2WAVEFORM,  K_LFOWAVE,0,0, 6,
        {"Sin","Tri","Saw","Sqr","S+H","Rnd"} },
  { "lfo2_rate",     "LFO2 Rate",     LFO2RATE,      K_PCT,    0,0, 0,{0} },
  { "lfo2_amount",   "LFO2 Amount",   LFO2AMOUNT,    K_PCT,    0,0, 0,{0} },
  /* NOT a copy of LFO1's list: LfoHandler2::Destination puts PAN and VOLUME in
   * slots 5 and 6, where LfoHandler1 has PW and FM. */
  { "lfo2_dest",     "LFO2 Dest",     LFO2DESTINATION,K_ENUM,  0,0, 8,
        {"None","Filter","Osc1","Osc2","Pan","Volume","LFO1","Osc1+2"} },
  { "lfo2_sync",     "LFO2 Sync",     LFO2SYNC,      K_TOGGLE, 0,0, 0,{0} },
  { "lfo2_keytrig",  "LFO2 KeyTrig",  LFO2KEYTRIGGER,K_TOGGLE, 0,0, 0,{0} },
  { "lfo2_phase",    "LFO2 Phase",    LFO2PHASE,     K_PCT,    0,0, 0,{0} },

  /* ---- Free AD envelope ---- */
  { "free_a",        "Env3 Attack",   FREEADATTACK,  K_PCT,    0,0, 0,{0} },
  { "free_d",        "Env3 Decay",    FREEADDECAY,   K_PCT,    0,0, 0,{0} },
  { "free_amt",      "Env3 Amount",   FREEADAMOUNT,  K_BIPOLAR,0,0, 0,{0} },
  { "free_dest",     "Env3 Dest",     FREEADDESTINATION,K_ENUM,0,0, 6,
        {"Off","Filter","Osc1","Osc2","PW","FM"} },

  /* ---- Velocity / wheel ---- */
  { "vel_vol",       "Vel > Vol",     VELOCITYVOLUME, K_PCT,   0,0, 0,{0} },
  { "vel_env",       "Vel > Env",     VELOCITYCONTOUR,K_PCT,   0,0, 0,{0} },
  { "vel_cut",       "Vel > Cutoff",  VELOCITYCUTOFF, K_PCT,   0,0, 0,{0} },
  { "pw_cutoff",     "Wheel > Cutoff",PITCHWHEELCUTOFF,K_PCT,  0,0, 0,{0} },
  { "pw_pitch",      "Bend Range",    PITCHWHEELPITCH,K_PCT,   0,0, 0,{0} },

  /* ---- Voicing ---- */
  { "portamento",    "Portamento",    PORTAMENTO,    K_PCT,    0,0, 0,{0} },
  { "porta_mode",    "Porta Mode",    PORTAMENTOMODE,K_ENUM,   0,0, 3, {"Off","Auto","On"} },
  { "voices",        "Voices",        VOICES,        K_INT,    1,6, 6,{0} },

  /* ---- Chorus / Reverb ---- */
  { "chorus1",       "Chorus I",      CHORUS1ENABLE, K_TOGGLE, 0,0, 0,{0} },
  { "chorus2",       "Chorus II",     CHORUS2ENABLE, K_TOGGLE, 0,0, 0,{0} },
  { "reverb_wet",    "Reverb Wet",    REVERBWET,     K_PCT,    0,0, 0,{0} },
  { "reverb_decay",  "Reverb Decay",  REVERBDECAY,   K_PCT,    0,0, 0,{0} },
  { "reverb_pre",    "Reverb PreDly", REVERBPREDELAY,K_PCT,    0,0, 0,{0} },
  { "reverb_hi",     "Reverb HiCut",  REVERBHIGHCUT, K_PCT,    0,0, 0,{0} },
  { "reverb_lo",     "Reverb LoCut",  REVERBLOWCUT,  K_PCT,    0,0, 0,{0} },

  /* ---- Delay ---- */
  { "delay_wet",     "Delay Wet",     DELAYWET,      K_PCT,    0,0, 0,{0} },
  { "delay_time",    "Delay Time",    DELAYTIME,     K_PCT,    0,0, 0,{0} },
  { "delay_sync",    "Delay Sync",    DELAYSYNC,     K_TOGGLE, 0,0, 0,{0} },
  { "delay_fac_l",   "Delay 2x L",    DELAYFACTORL,  K_TOGGLE, 0,0, 0,{0} },
  { "delay_fac_r",   "Delay 2x R",    DELAYFACTORR,  K_TOGGLE, 0,0, 0,{0} },
  { "delay_fb",      "Delay Feedbk",  DELAYFEEDBACK, K_FBGAIN, 0,0, 0,{0} },
  { "delay_hi",      "Delay HiCut",   DELAYHIGHSHELF,K_PCT,    0,0, 0,{0} },
  { "delay_lo",      "Delay LoCut",   DELAYLOWSHELF, K_PCT,    0,0, 0,{0} },

  /* ---- Envelope Editor (spline mod source; shape fixed per preset) ---- */
  { "env_amt",       "Env Draw Amt",  ENVELOPEEDITORAMOUNT, K_PCT,    0,0, 0,{0} },  // engine squares it -> unipolar
  { "env_speed",     "Env Draw Speed",ENVELOPEEDITORSPEED,  K_ENUM,    0,0, 6,
        {"x1","x2","x4","x8","x16","x32"} },
  { "env_dest",      "Env Draw Dest", ENVELOPEEDITORDEST1,  K_ENUM,    0,0, 8,
        {"Off","Filter","Osc1","Osc2","Osc1+2","FM","RingMod","Volume"} },
};

static const int NM_PARAM_COUNT = (int)(sizeof(PARAMS) / sizeof(PARAMS[0]));

/* Default patch (normalized) — a plain saw/LP init so a fresh instance is
 * audible before preset 0 loads over it. */
typedef struct { int index; float value; } patch_val_t;
static const patch_val_t DEFAULT_PATCH[] = {
  { VOLUME, 0.50f },
  { OSC1VOLUME, 0.80f }, { OSC1WAVEFORM, 0.0f },     // saw (combo 1)
  { OSC2VOLUME, 0.0f },  { OSC2WAVEFORM, 0.0f }, { OSC3VOLUME, 0.0f },
  { OSCMASTERTUNE, 0.5f }, { OSC1TUNE, 0.5f }, { OSC2TUNE, 0.5f },
  { OSC1FINETUNE, 0.5f }, { OSC2FINETUNE, 0.5f }, { DETUNE, 0.20f },
  { OSC1PW, 0.5f },
  { FILTERTYPE, 0.0f },                               // LP24 (combo 1 of 12)
  { CUTOFF, 0.60f }, { RESONANCE, 0.12f }, { KEYFOLLOW, 0.30f },
  { FILTERCONTOUR, 0.35f },
  { FILTERATTACK, 0.0f }, { FILTERDECAY, 0.45f }, { FILTERSUSTAIN, 0.30f }, { FILTERRELEASE, 0.30f },
  { AMPATTACK, 0.0f },    { AMPDECAY, 0.50f },   { AMPSUSTAIN, 0.85f }, { AMPRELEASE, 0.28f },
  { LFO1RATE, 0.30f }, { LFO1AMOUNT, 0.0f },
  { LFO2RATE, 0.30f }, { LFO2AMOUNT, 0.0f },
  { PITCHWHEELPITCH, 0.20f },
  { VOICES, 1.0f },                                   // normalized -> combo 6
};
static const int DEFAULT_PATCH_COUNT = (int)(sizeof(DEFAULT_PATCH) / sizeof(DEFAULT_PATCH[0]));

/* ======================================================================== *
 *  Imported preset banks
 * ========================================================================
 * A user drops folders of loose `.noisemakerpreset` files under
 * <module_dir>/presets; each immediate child folder becomes a selectable bank,
 * gathering presets from its own subfolders recursively (the TAL packs in the
 * wild are one folder per author with BASS/LEAD/PAD/... inside). Loose presets
 * sitting directly in the root become one bank named after the root.
 *
 * <module_dir>/presets is obxd's convention (it scans the same directory for
 * .fxb banks) and matching it is deliberate: one place to look across the
 * fleet. It is NOT the host's presets/<module-id>/ store, which holds
 * module-preset JSON and is walked by the host's own browser.
 *
 * ⚠ A module UNINSTALL deletes this directory along with the module
 * (store_utils.mjs removeModule -> host_remove_dir). An UPDATE does not:
 * installModule extracts the tarball over the directory without removing it
 * first, so imported banks survive updates. Say so in the docs -- an earlier
 * version of this comment claimed updates wiped it, which is false.
 *
 * NOTHING IS CONVERTED OR CACHED. Building the bank list is a readdir of the
 * root; opening a bank is a readdir of that folder; selecting a preset parses
 * exactly ONE ~4 KB file. There is no import step to run, no cache to
 * invalidate, and no way for the module's view to drift from what is on disk.
 * The list is rebuilt on every query, so a folder copied onto the device shows
 * up the next time the selector is opened (obxd does the same -- see the
 * "rescan on each query" comment on its fxb_bank_list).
 *
 * The preset NAME shown is the file name, not the <program programname>
 * inside. That is what the user sees in their folder, it sorts the way their
 * folder sorts, and it means listing a bank costs no file reads at all.
 */
/* The bank root is <module_dir>/presets, resolved per instance at create time
 * (inst->bank_root) -- so a test simply passes its own module_dir to
 * v2_create_instance, with no compile-time override needed. */
#define NM_BANK_SUBDIR      "presets"
#define NM_PRESET_EXT       ".noisemakerpreset"
#define NM_MAX_BANKS        64
#define NM_MAX_BANK_PRESETS 512
#define NM_MAX_RELPATH      160
#define NM_MAX_BANKNAME     64

typedef struct {
    char name[NM_MAX_BANKNAME];   /* folder name; "" for the factory bank */
    int  is_factory;
} nm_bank_t;

/* ======================================================================== *
 *  Instance
 * ======================================================================== */
typedef struct {
    char        module_dir[256];
    SynthEngine *synth;
    float       eng[NM_ENG_SLOTS];  // shadow of NORMALIZED values, by SYNTHPARAMETERS
                                    // (+ the NM_M_* macro slots past NUMPARAM)
    int         octave_transpose;
    float       tempo_bpm;
    int         editor_page;     // canvas overlay state (persists per instance)
    int         cur_preset;      // index into NM_FACTORY_BANK, -1 == Init
    float       wave_tune_semis; // the Wave macro's osc2 pitch contribution
                                 // (FM compensation or an anchor's interval);
                                 // summed with the tune2 macro, not a param

    /* ---- banks ---- */
    char        bank_root[512];  // <module_dir>/presets; "" disables imports
    nm_bank_t   banks[NM_MAX_BANKS];
    int         bank_count;
    int         cur_bank;        // 0 == factory
    /* File list for cur_bank only, relative to that bank's folder. Sorted, so
     * an index is stable for as long as the folder is. ~80 KB; only the
     * selected bank is ever held. */
    char        (*bank_files)[NM_MAX_RELPATH];
    int         bank_file_count;
} nm_instance_t;

static const param_def_t *find_param(const char *key) {
    for (int i = 0; i < NM_PARAM_COUNT; i++)
        if (strcmp(PARAMS[i].key, key) == 0) return &PARAMS[i];
    return NULL;
}

static void apply_engine(nm_instance_t *inst, int idx, float v);
static float combo_idx_to_norm(int idx, int n);

/* ======================================================================== *
 *  Macro: envelope time
 * ========================================================================
 * TAL's Adsr (Engine/Adsr.h) derives a per-sample rate coefficient from the
 * normalized knob v as
 *
 *     u = 1 - v/2        rate(v) = k * (f + c * u^p)
 *
 * with f = 0.0003 and (c,p) = (7,24) attack, (7,23) decay, (2,22) release.
 * The integrators are one-pole approaches, so segment time is proportional to
 * 1/rate; the sample-rate factor k and release's extra x8 both cancel in a
 * ratio, making the transform below sample-rate independent.
 *
 * To stretch a segment by N we want rate(v') = rate(v)/N, i.e.
 *
 *     u' = ( ((f + c*u^p)/N - f) / c )^(1/p)      v' = 2*(1 - u')
 *
 * Note this is a POWER law in u, not an exponential in v — Echidna's trick of
 * adding ln(N)/b to the normalized value does NOT transfer here.
 *
 * D and R use the plain knob SHIFT below and never touch this math. ATTACK
 * does use it (nm_env_time_shift_attack) to hold Echidna's attack floor, and
 * the boundaries of the inversion matter there:
 *   - N < 1: v' goes negative, clamps to 0 = the engine's fastest attack.
 *   - N > 1: no solution exists once (f + c*u^p)/N <= f, since the f term
 *     dominates above v ~ 0.68 and the engine physically cannot run slower.
 *     Clamp to v' = 1 and accept a smaller-than-N stretch.
 */
/* Macro knob (normalized, 0.5 = centre) -> a SHIFT of the envelope knob
 * positions themselves, NOT a multiplier on the resulting times.
 *
 * A multiplier is the mathematically tidy answer and the musically wrong one:
 * x6 of a zero-length segment is still zero, so on the many factory patches
 * whose amp envelope is a pure gate (A=0 D=0 S=1 R=0 -- the corpus median for
 * BS and LD) the knob does visibly and audibly nothing. What a TIME control
 * looks like it does is drag all the envelope sliders together, and that is
 * what it should do: from a gate, turning it up must CREATE a release you can
 * see on the ADSR graphic and hear on the tail.
 *
 * The shift is proportional to the travel REMAINING in that direction, so it
 * is always responsive and never hits a wall:
 *      d > 0:  v' = v + d * (1 - v)      (0 still moves; 1 stays 1)
 *      d < 0:  v' = v + d * v            (1 still moves; 0 stays 0)
 * with d = (macro - 0.5) * 2, i.e. -1 at min, 0 at the detent, +1 at max.
 *
 * Trade worth knowing: this does NOT preserve A:D:R proportions the way a true
 * ratio would, because the underlying time law is non-linear. That is the right
 * trade -- "all the sliders move together" is how the control reads.
 *
 * ⚠ Applies to DECAY and RELEASE only. Attack goes through
 * nm_env_time_shift_attack below; see there for why. */
static float nm_env_time_shift(float v, float m) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (m < 0.0f) m = 0.0f;
    if (m > 1.0f) m = 1.0f;
    const float d = (m - 0.5f) * 2.0f;
    if (d > 0.0f) return v + d * (1.0f - v);
    if (d < 0.0f) return v + d * v;
    return v;                      /* exact pass-through at the detent */
}

/* ---- Attack: the shift, but with Echidna's TIME-INDEPENDENT attack floor ---
 * The raw shift is wrong for attack. Measured on a pluck (aenv_a=0, d=40, s=0,
 * r=20): the attack ran 21 ms at the detent, 1001 ms at aenv_time=75 and
 * 1096 ms at 100 -- so reaching for a longer release turned every pluck into a
 * swell. Echidna refuses to do that on purpose: Adsr::recompute scales only the
 * time ABOVE atkFloorSec_ (rig 2026-07-07, and its filter EG floors at the
 * fastest LUT knot for the same reason -- HW's filter snaps open <8 ms at every
 * filter TIME).
 *
 * There is a second, sharper reason here that Echidna does not have.
 * `Adsr::getValueFasterAttack()` returns 1.0 immediately when `attackReal ==
 * 0.0f` EXACTLY -- an instant-snap path, used by the filter envelope
 * (SynthVoice.h:164) and the free AD (AdsrHandler.h:111). Shifting attack off
 * zero does not merely slow it, it LEAVES that mode. Pinning v=0 is therefore
 * required, not just desirable.
 *
 * So attack takes Echidna's rule, driven by whatever ratio the shift implies
 * for this segment (t is proportional to 1/rate, so k cancels):
 *
 *     N = rate(v) / rate(v_shift)                  what the shift did
 *     t_final = t_floor + (t_base - t_floor) * N   only the excess scales
 *
 * with t_floor = the engine's own fastest attack, rate(0) -- no magic
 * constant needed, and it makes v=0 a fixed point exactly (t_base == t_floor),
 * which is what protects the instant-snap path. */
#define NM_ENV_F     0.0003f       /* the f term, shared by all three stages */
#define NM_ATK_C     7.0f          /* scaleValueAttack: powf(u, 24) * 7 */
#define NM_ATK_P     24.0f

static inline float nm_atk_rate(float v) {          /* k omitted; it cancels */
    const float u = 1.0f - v * 0.5f;
    return NM_ENV_F + NM_ATK_C * powf(u, NM_ATK_P);
}

static inline float nm_atk_rate_to_v(float r) {
    const float x = (r - NM_ENV_F) / NM_ATK_C;
    if (x >= 1.0f) return 0.0f;    /* at or past the fastest the engine has */
    if (x <= 0.0f) return 1.0f;    /* slower than the f term allows; saturate */
    const float u = powf(x, 1.0f / NM_ATK_P);
    float v = 2.0f * (1.0f - u);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

static float nm_env_time_shift_attack(float v, float m) {
    const float vs = nm_env_time_shift(v, m);
    if (vs == v) return v;                       /* detent: exact pass-through */

    const float rBase  = nm_atk_rate(v);
    const float rShift = nm_atk_rate(vs);
    const float rFloor = nm_atk_rate(0.0f);      /* fastest attack the engine has */

    const float N      = rBase / rShift;         /* stretch the shift implies */
    const float tFloor = 1.0f / rFloor;
    const float tBase  = 1.0f / rBase;

    float tFinal = tFloor + (tBase - tFloor) * N;
    if (tFinal < tFloor) tFinal = tFloor;        /* never faster than the floor */
    return nm_atk_rate_to_v(1.0f / tFinal);
}

/* ======================================================================== *
 *  Macro: Wave
 * ========================================================================
 * TAL hard-switches oscillator shapes (Osc::process is a plain switch, no
 * blend coefficient anywhere), so the sweep below is built from ANCHOR
 * configurations with a level CROSSFADE between neighbours: the continuous
 * fields (levels, pulse width, FM, ringmod, detune) lerp, while waveform,
 * coarse tune and sync SNAP. Anchors sit on exact display values so each named
 * sound is precisely dialable, and the travel between two of them is a usable
 * blend rather than a jump.
 *
 * FM note: osc2 is the CARRIER and osc1 the MODULATOR (Vco.h does
 * osc2->setFm(osc2Fm) + osc2->setFmFrequency(osc1->getCurrentFrequency()),
 * despite the OSC2FM / setOsc1Fm naming), and only osc1's FREQUENCY is used --
 * the modulator is an internal sine -- so osc1's waveform is irrelevant there
 * and it stays muted. Osc::process updates currentFrequency BEFORE the
 * oscVolume>0 gate, so a silent osc1 still modulates. */
enum { NM_O1_SAW = 0, NM_O1_PULSE = 1, NM_O1_NOISE = 2 };
enum { NM_O2_SAW = 0, NM_O2_PULSE = 1, NM_O2_TRI = 2, NM_O2_SINE = 3, NM_O2_NOISE = 4 };

typedef struct {
    int   disp;          /* knob display value 1..100 this anchor sits on */
    const char *name;
    int   o1wave, o2wave;
    float o1vol, o2vol, subvol;
    float pw;            /* osc1 pulse width */
    int   tune2;         /* osc2 coarse tune, semitones */
    float detune;
    int   sync;
    float fm;            /* osc2 FM amount (osc1 is the modulator) */
    float ring;
} nm_wave_stop_t;

static const nm_wave_stop_t NM_WAVE_STOPS[] = {
  /* disp  name          o1wave       o2wave       o1vol o2vol  sub    pw    t2  detune sync  fm     ring */
  {   1, "Sine",        NM_O1_SAW,   NM_O2_SINE,  0.00f,0.90f, 0.00f, 0.50f,  0, 0.05f, 0, 0.000f, 0.00f },
  {  12, "FM",          NM_O1_SAW,   NM_O2_SINE,  0.00f,0.85f, 0.00f, 0.50f,  0, 0.00f, 0, 0.075f, 0.00f },
  {  23, "Triangle",    NM_O1_SAW,   NM_O2_TRI,   0.00f,0.85f, 0.00f, 0.50f,  0, 0.05f, 0, 0.000f, 0.00f },
  {  34, "Saw",         NM_O1_SAW,   NM_O2_SAW,   0.80f,0.00f, 0.00f, 0.50f,  0, 0.10f, 0, 0.000f, 0.00f },
  {  45, "Dual Saw",    NM_O1_SAW,   NM_O2_SAW,   0.65f,0.65f, 0.00f, 0.50f,  0, 0.35f, 0, 0.000f, 0.00f },
  {  56, "Square",      NM_O1_PULSE, NM_O2_SAW,   0.80f,0.00f, 0.00f, 0.50f,  0, 0.10f, 0, 0.000f, 0.00f },
  {  67, "Thin Pls",    NM_O1_PULSE, NM_O2_SAW,   0.85f,0.00f, 0.00f, 0.88f,  0, 0.10f, 0, 0.000f, 0.00f },
  {  78, "Pulse+Saw",   NM_O1_PULSE, NM_O2_SAW,   0.60f,0.60f, 0.00f, 0.88f,  0, 0.25f, 0, 0.000f, 0.00f },
  {  89, "Ring",        NM_O1_PULSE, NM_O2_SAW,   0.45f,0.45f, 0.00f, 0.50f,  7, 0.10f, 0, 0.000f, 0.25f },
  { 100, "Sub Bass",    NM_O1_SAW,   NM_O2_SAW,   0.70f,0.00f, 0.65f, 0.50f,  0, 0.10f, 0, 0.000f, 0.00f },
};
static const int NM_WAVE_STOP_COUNT = (int)(sizeof(NM_WAVE_STOPS) / sizeof(NM_WAVE_STOPS[0]));

/* TAL's FM modulator is UNIPOLAR -- the oscillators do
 *     freq += fm * 10 * fmFreq * (1 + sin(phase))
 * and that "1 +" is a DC term, so deepening FM also RAISES pitch. With osc1 at
 * frequency F and osc2 detuned by s semitones, osc2's average frequency is
 * F*(2^(s/12) + 10*fm); holding that at F needs
 *     s = 12 * log2(1 - 10*fm)
 * which is why the sweep caps fm at 0.075: that is exactly s = -24, the bottom
 * of OSC2TUNE's range (AudioUtils::getOscTuneValue -> value*48 - 24). The
 * result is an FM zone that gets progressively more metallic at CONSTANT
 * pitch, which is fiddly to dial by hand and is the point of the macro. */
static float nm_fm_tune_semis(float fm) {
    float x = 1.0f - 10.0f * fm;
    if (x < 0.0625f) x = 0.0625f;          /* -48 st floor; the cap keeps us above this */
    float s = 12.0f * log2f(x);
    if (s < -24.0f) s = -24.0f;
    if (s > 24.0f) s = 24.0f;
    return s;
}

/* Normalized value that survives getOscTuneValue's truncate-toward-zero:
 * (int)(v*48 - 24) must land on `semis`, so aim half a step away from the
 * boundary in whichever direction truncation rounds. */
static float nm_tune_norm(int semis) {
    if (semis < -24) semis = -24;
    if (semis > 24) semis = 24;
    float x = (float)semis + (semis < 0 ? -0.5f : 0.5f);
    return (x + 24.0f) / 48.0f;
}

/* tune2 knob -> semitones, Echidna's law (its canvas.js tune2Semis).
 *
 * The knob is a native 8-bit 0..255 spanning 0..+24 semitones. Everywhere it
 * SNAPS to whole semitones, EXCEPT within +/-8 native steps of the three
 * anchors (0 / 128 / 255 = unison / +1 oct / +2 oct), where it becomes a fine
 * DETUNE of up to +/-0.2 semitone (+/-20 cents at the window edge). That gives
 * clean intervals over most of the travel and usable beating right where you
 * want it. The 8-bit wire exists for this: 0..24 alone has no room for it. */
#define NM_TUNE2_DSTEPS 8.0f
#define NM_TUNE2_DMAX   0.2f
static float nm_tune2_semis(float knob01) {
    const float raw = floorf(knob01 * 255.0f + 0.5f);
    static const float kCenter[3] = { 0.0f, 128.0f, 255.0f };
    static const float kSemis[3]  = { 0.0f,  12.0f,  24.0f };
    for (int i = 0; i < 3; i++) {
        const float steps = raw - kCenter[i];
        if (fabsf(steps) <= NM_TUNE2_DSTEPS)
            return kSemis[i] + (steps / NM_TUNE2_DSTEPS) * NM_TUNE2_DMAX;
    }
    return floorf(raw * 24.0f / 255.0f + 0.5f);      /* nearest semitone */
}

/* Osc2 pitch is written by TWO macros, so it goes through one place.
 *
 *   tune2  -- the user's upward-only 0..+24 semitone knob (with the fine
 *             detune windows above)
 *   wave   -- an additive offset: the FM zone's pitch compensation, or an
 *             anchor's interval (Ring's +7)
 *
 * They sum. The coarse part goes to OSC2TUNE (which the engine truncates to
 * whole semitones) and any fraction to OSC2FINETUNE (+/-1 semitone), which is
 * what carries both the fine detune and the FM compensation's sub-semitone
 * part instead of letting them be quantised away. */
static void nm_apply_osc2_tune(nm_instance_t *inst) {
    float s = nm_tune2_semis(inst->eng[NM_M_TUNE2]) + inst->wave_tune_semis;
    if (s < -24.0f) s = -24.0f;
    if (s >  24.0f) s =  24.0f;

    int   coarse = (int)floorf(s + 0.5f);
    float frac   = s - (float)coarse;
    if (frac < -1.0f) frac = -1.0f;
    if (frac >  1.0f) frac =  1.0f;
    apply_engine(inst, OSC2TUNE,     nm_tune_norm(coarse));
    apply_engine(inst, OSC2FINETUNE, frac * 0.5f + 0.5f);
}

/* Discrete-field rule for a crossfade segment: an oscillator that is silent
 * at one end takes the OTHER end's value for the whole segment (so it fades
 * in already wearing its destination shape, or fades out still wearing its
 * source shape); one that is audible at both ends switches at the midpoint. */
static int nm_pick_disc(int va, int vb, float lvla, float lvlb, float f) {
    if (lvla <= 0.0001f) return vb;
    if (lvlb <= 0.0001f) return va;
    return (f < 0.5f) ? va : vb;
}

/* Drive the oscillator section from the Wave macro. v is the macro's stored
 * normalized value; v == 0 is the OFF detent and never reaches here. */
static void nm_apply_wave_macro(nm_instance_t *inst, float v) {
    float d = v * 100.0f;
    if (d < 1.0f) d = 1.0f;
    if (d > 100.0f) d = 100.0f;

    int i = 0;
    while (i < NM_WAVE_STOP_COUNT - 2 && d > (float)NM_WAVE_STOPS[i + 1].disp) i++;
    const nm_wave_stop_t *a = &NM_WAVE_STOPS[i];
    const nm_wave_stop_t *b = &NM_WAVE_STOPS[i + 1];
    float span = (float)(b->disp - a->disp);
    float f = (span > 0.5f) ? (d - (float)a->disp) / span : 0.0f;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    #define NM_LERP(field) (a->field + (b->field - a->field) * f)

    /* Continuous: the crossfade itself. */
    const float o1vol  = NM_LERP(o1vol);
    const float o2vol  = NM_LERP(o2vol);
    const float subvol = NM_LERP(subvol);
    const float pw     = NM_LERP(pw);
    const float fm     = NM_LERP(fm);
    const float ring   = NM_LERP(ring);
    const float detune = NM_LERP(detune);
    #undef NM_LERP

    /* Discrete: snap. */
    const int o1wave = nm_pick_disc(a->o1wave, b->o1wave, a->o1vol, b->o1vol, f);
    const int o2wave = nm_pick_disc(a->o2wave, b->o2wave, a->o2vol, b->o2vol, f);
    const int tune2  = nm_pick_disc(a->tune2,  b->tune2,  a->o2vol, b->o2vol, f);
    const int sync   = nm_pick_disc(a->sync,   b->sync,   1.0f,     1.0f,     f);

    apply_engine(inst, OSC1WAVEFORM,   combo_idx_to_norm(o1wave, 3));
    apply_engine(inst, OSC2WAVEFORM,   combo_idx_to_norm(o2wave, 5));
    apply_engine(inst, OSC1VOLUME,     o1vol);
    apply_engine(inst, OSC2VOLUME,     o2vol);
    apply_engine(inst, OSC3VOLUME,     subvol);
    apply_engine(inst, OSC1PW,         pw);
    apply_engine(inst, OSC2FM,         fm);
    apply_engine(inst, RINGMODULATION, ring);
    apply_engine(inst, DETUNE,         detune);
    apply_engine(inst, OSCSYNC,        sync ? 1.0f : 0.0f);

    /* FM raises pitch (see nm_fm_tune_semis), so inside the FM zone osc2's
     * tune is spent compensating and the anchor's own interval is ignored.
     * Either way this is only the Wave macro's CONTRIBUTION -- the tune2 knob
     * adds to it in nm_apply_osc2_tune. */
    inst->wave_tune_semis = (fm > 0.0f) ? nm_fm_tune_semis(fm) : (float)tune2;
    nm_apply_osc2_tune(inst);
}

/* Push one NORMALIZED value into the engine. Most params pass straight to their
 * setter (which does its own log/combo scaling); LFO rate/sync need tempo, and
 * the delay params route through the delay engine. Mirrors TalCore::setParameter. */
static void apply_engine(nm_instance_t *inst, int idx, float v) {
    SynthEngine *e = inst->synth;
    inst->eng[idx] = v;
    float bpm = inst->tempo_bpm;
    switch (idx) {
        case VOLUME:         e->setVolume(v); break;
        case CUTOFF:         e->setCutoff(v); break;
        case RESONANCE:      e->setResonance(v); break;
        case FILTERCONTOUR:  e->setFilterContour(v); break;
        case KEYFOLLOW:      e->setKeyfollow(v); break;
        case FILTERDRIVE:    e->setFilterDrive(v); break;
        /* A/D/R go through the env-time macro on the way to the engine; the
         * unscaled value stays in inst->eng[] (set above), so the macro never
         * consumes the base knob and load_preset gets the scaling for free.
         * Sustain is a LEVEL, not a time — deliberately not scaled. */
        case FILTERATTACK:   e->setFilterAttack (nm_env_time_shift_attack(v, inst->eng[NM_M_FENV_T])); break;
        case FILTERDECAY:    e->setFilterDecay  (nm_env_time_shift(v, inst->eng[NM_M_FENV_T])); break;
        case FILTERSUSTAIN:  e->setFilterSustain(v); break;
        case FILTERRELEASE:  e->setFilterRelease(nm_env_time_shift(v, inst->eng[NM_M_FENV_T])); break;
        case AMPATTACK:      e->setAmpAttack    (nm_env_time_shift_attack(v, inst->eng[NM_M_AENV_T])); break;
        case AMPDECAY:       e->setAmpDecay     (nm_env_time_shift(v, inst->eng[NM_M_AENV_T])); break;
        case AMPSUSTAIN:     e->setAmpSustain(v); break;
        case AMPRELEASE:     e->setAmpRelease   (nm_env_time_shift(v, inst->eng[NM_M_AENV_T])); break;
        case OSC1VOLUME:     e->setOsc1Volume(v); break;
        case OSC2VOLUME:     e->setOsc2Volume(v); break;
        case OSC3VOLUME:     e->setOsc3Volume(v); break;
        case OSC1WAVEFORM:   e->setOsc1Waveform(v); break;
        case OSC2WAVEFORM:   e->setOsc2Waveform(v); break;
        case OSC1TUNE:       e->setOsc1Tune(v); break;
        case OSC2TUNE:       e->setOsc2Tune(v); break;
        case OSC1FINETUNE:   e->setOsc1FineTune(v); break;
        case OSC2FINETUNE:   e->setOsc2FineTune(v); break;
        case OSCSYNC:        e->setOscSync(v); break;
        case OSCMASTERTUNE:  e->setMastertune(v); break;
        case TRANSPOSE:      e->setTranspose(v); break;
        case DETUNE:         e->setDetune(v); break;
        case VINTAGENOISE:   e->setVintageNoise(v); break;
        case OSC1PW:         e->setOsc1Pw(v); break;
        case OSC1PHASE:      e->setOsc1Phase(v); break;
        case OSC2FM:         e->setOsc1Fm(v); break;   // TAL maps OSC2FM -> setOsc1Fm
        case OSC2PHASE:      e->setOsc2Phase(v); break;
        case RINGMODULATION: e->setRingmodulation(v); break;
        case OSCBITCRUSHER:  e->setOscBitcrusher(v); break;
        case HIGHPASS:       e->setHighPass(v); break;
        case FILTERTYPE:     e->setFiltertype(v); break;
        case PORTAMENTO:     e->setPortamento(v); break;
        case PORTAMENTOMODE: e->setPortamentoMode(v); break;
        case LFO1RATE:       e->setLfo1Rate(v, bpm); break;
        case LFO2RATE:       e->setLfo2Rate(v, bpm); break;
        case LFO1AMOUNT:     e->setLfo1Amount(v); break;
        case LFO2AMOUNT:     e->setLfo2Amount(v); break;
        case LFO1WAVEFORM:   e->setLfo1Waveform(v); break;
        case LFO2WAVEFORM:   e->setLfo2Waveform(v); break;
        case LFO1DESTINATION:e->setLfo1Destination(v); break;
        case LFO2DESTINATION:e->setLfo2Destination(v); break;
        case LFO1PHASE:      e->setLfo1Phase(v); break;
        case LFO2PHASE:      e->setLfo2Phase(v); break;
        case LFO1SYNC:       e->setLfo1Sync(v, inst->eng[LFO1RATE], bpm); break;
        case LFO2SYNC:       e->setLfo2Sync(v, inst->eng[LFO2RATE], bpm); break;
        case LFO1KEYTRIGGER: e->setLfo1KeyTrigger(v); break;
        case LFO2KEYTRIGGER: e->setLfo2KeyTrigger(v); break;
        case FREEADATTACK:   e->setFreeAdAttack(v); break;
        case FREEADDECAY:    e->setFreeAdDecay(v); break;
        case FREEADAMOUNT:   e->setFreeAdAmount(v); break;
        case FREEADDESTINATION: e->setFreeAdDestination(v); break;
        case VELOCITYVOLUME:  e->setVelocityVolume(v); break;
        case VELOCITYCONTOUR: e->setVelocityContour(v); break;
        case VELOCITYCUTOFF:  e->setVelocityCutoff(v); break;
        case PITCHWHEELCUTOFF:e->setPitchwheelCutoff(v); break;
        case PITCHWHEELPITCH: e->setPitchwheelPitch(v); break;
        case CHORUS1ENABLE:   e->setChorus(v > 0.5f, inst->eng[CHORUS2ENABLE] > 0.5f); break;
        case CHORUS2ENABLE:   e->setChorus(inst->eng[CHORUS1ENABLE] > 0.5f, v > 0.5f); break;
        case REVERBWET:       e->setReverbWet(v); break;
        case REVERBDECAY:     e->setReverbDecay(v); break;
        case REVERBPREDELAY:  e->setReverbPreDelay(v); break;
        case REVERBHIGHCUT:   e->setReverbHighCut(v); break;
        case REVERBLOWCUT:    e->setReverbLowCut(v); break;
        case DELAYWET:        e->getDelayEngine()->setWet(v); break;
        case DELAYTIME:       e->getDelayEngine()->setDelay(v); break;
        case DELAYSYNC:       e->getDelayEngine()->setSync(v > 0.5f); break;
        case DELAYFACTORL:    e->getDelayEngine()->setFactor2xL(v > 0.5f); break;
        case DELAYFACTORR:    e->getDelayEngine()->setFactor2xR(v > 0.5f); break;
        case DELAYHIGHSHELF:  e->getDelayEngine()->setHighCut(v); break;
        case DELAYLOWSHELF:   e->getDelayEngine()->setLowCut(v); break;
        case DELAYFEEDBACK:   e->getDelayEngine()->setFeedback(v); break;
        case VOICES:          e->setNumberOfVoices(v); break;
        /* EnvelopeEditor mod source (shape installed per-preset by load_preset). */
        case ENVELOPEEDITORDEST1:   e->setEnvelopeEditorDest1(v); break;
        case ENVELOPEEDITORSPEED:   e->setEnvelopeEditorSpeed(v); break;
        case ENVELOPEEDITORAMOUNT:  e->setEnvelopeEditorAmount(v); break;
        case ENVELOPEONESHOT:       e->setEnvelopeEditorOneShot(v > 0.5f); break;
        case ENVELOPEFIXTEMPO:      e->setEnvelopeEditorFixTempo(v > 0.5f); break;

        /* ---- Macros ---- */
        case NM_M_WAVE:  if (v > 0.0f) nm_apply_wave_macro(inst, v); break;  /* 0 = OFF detent */
        case NM_M_TUNE2: nm_apply_osc2_tune(inst); break;
        /* Re-push the six base A/D/R values so the new ratio takes effect; the
         * cases above re-scale them from inst->eng[] on the way through. */
        case NM_M_FENV_T:
            apply_engine(inst, FILTERATTACK,  inst->eng[FILTERATTACK]);
            apply_engine(inst, FILTERDECAY,   inst->eng[FILTERDECAY]);
            apply_engine(inst, FILTERRELEASE, inst->eng[FILTERRELEASE]);
            break;
        case NM_M_AENV_T:
            apply_engine(inst, AMPATTACK,  inst->eng[AMPATTACK]);
            apply_engine(inst, AMPDECAY,   inst->eng[AMPDECAY]);
            apply_engine(inst, AMPRELEASE, inst->eng[AMPRELEASE]);
            break;
        default: break;
    }
}

/* ---- display <-> normalized conversion ---- */
static int combo_norm_to_idx(float norm, int n) {   // matches calcComboBoxValue-1
    if (n < 2) return 0;
    int k = (int)floorf(norm * (n - 1) + 1.5f) - 1;
    if (k < 0) k = 0;
    if (k > n - 1) k = n - 1;
    return k;
}
static float combo_idx_to_norm(int idx, int n) {
    if (n < 2) return 0.0f;
    if (idx < 0) idx = 0;
    if (idx > n - 1) idx = n - 1;
    return (float)idx / (float)(n - 1);
}

static float disp_to_engine(const param_def_t *p, const char *val) {
    switch (p->kind) {
        case K_PCT:
        case K_BIPOLAR: return (float)atof(val) / 100.0f;              // 0..100 -> 0..1
        case K_TOGGLE:  return (atoi(val) != 0) ? 1.0f : 0.0f;
        case K_INT: {
            int iv = atoi(val);
            if (iv < p->imin) iv = p->imin;
            if (iv > p->imax) iv = p->imax;
            return combo_idx_to_norm(iv - p->imin, p->n_opts);         // 1..6 -> norm
        }
        case K_ENUM:    return combo_idx_to_norm(atoi(val), p->n_opts);
        case K_LFOWAVE: {                                             // idx 0..5 -> norm
            int iv = atoi(val);
            if (iv < 0) iv = 0;
            if (iv > p->n_opts - 1) iv = p->n_opts - 1;
            return (float)iv / (float)(p->n_opts - 1);
        }
        case K_FBGAIN:  return nm_fb_disp_to_norm((float)atof(val));   // gain x100 -> norm
    }
    return 0.0f;
}

static void engine_to_disp(const param_def_t *p, float norm, char *buf, int len) {
    switch (p->kind) {
        case K_PCT:
        case K_BIPOLAR: snprintf(buf, len, "%d", (int)lroundf(norm * 100.0f)); break;
        case K_TOGGLE:  snprintf(buf, len, "%d", norm > 0.5f ? 1 : 0); break;
        case K_INT:     snprintf(buf, len, "%d", p->imin + combo_norm_to_idx(norm, p->n_opts)); break;
        case K_ENUM:    snprintf(buf, len, "%d", combo_norm_to_idx(norm, p->n_opts)); break;
        case K_LFOWAVE: {                                            // norm -> idx 0..5 (engine (int)(norm*5))
            int idx = (int)(norm * 5.000001f);
            if (idx < 0) idx = 0;
            if (idx > p->n_opts - 1) idx = p->n_opts - 1;
            snprintf(buf, len, "%d", idx);
            break;
        }
        case K_FBGAIN:  snprintf(buf, len, "%d", nm_fb_norm_to_disp(norm)); break;
        default:        snprintf(buf, len, "0"); break;
    }
}

/* Install a preset's Envelope Editor spline shape. Builds a fresh
 * Array<SplinePoint*> from NM_FACTORY_SPLINES[idx] and hands it to the editor
 * (which takes ownership of the new set); the previously-installed set is freed
 * afterwards. Presets with no custom shape get the flat 0.5 line = no
 * modulation (matches TAL's default and the empty-<splinePoints/> presets). */
static void install_spline_points(nm_instance_t *inst,
                                  const nm_spline_point_t *src, int count) {
    EnvelopeEditor *ed = inst->synth->getEnvelopeEditor();
    Array<SplinePoint*> old = ed->getPoints();   // current (default or prev preset)

    Array<SplinePoint*> pts;
    if (src && count >= 2) {
        for (int i = 0; i < count; i++) {
            const nm_spline_point_t *sp = &src[i];
            SplinePoint *p = new SplinePoint(juce::Point<float>(sp->cx, sp->cy));
            p->setStartPoint(sp->isStart != 0);
            p->setEndPoint(sp->isEnd != 0);
            p->setControlPointLeftPosition(juce::Point<float>(sp->clx, sp->cly));
            p->setControlPointRightPosition(juce::Point<float>(sp->crx, sp->cry));
            pts.add(p);
        }
    } else {
        SplinePoint *start = new SplinePoint(juce::Point<float>(0.0f, 0.5f));
        SplinePoint *end   = new SplinePoint(juce::Point<float>(1.0f, 0.5f));
        start->setStartPoint(true);
        end->setEndPoint(true);
        pts.add(start);
        pts.add(end);
    }

    ed->setPoints(pts);                                  // editor copies the pointer array
    for (int i = 0; i < old.size(); i++) delete old[i];  // free the replaced set
}

static void install_preset_spline(nm_instance_t *inst, int idx) {
    const int nSets = (int)(sizeof(NM_FACTORY_SPLINES) / sizeof(NM_FACTORY_SPLINES[0]));
    if (idx < 0 || idx >= nSets) return;
    const nm_spline_set_t *s = &NM_FACTORY_SPLINES[idx];
    install_spline_points(inst, s->points, s->count);
}

/* Load one factory program (256-entry bank, normalized values). Release first
 * so a held note across a switch isn't stranded (setNumberOfVoices clears the
 * playing list without note-offing). */
/* Macros back to neutral. Called by load_preset BEFORE the preset's params are
 * applied, so the incoming A/D/R land at x1 rather than inheriting whatever
 * scaling the last patch's macro happened to be sitting at.
 *
 * These slots live past NUMPARAM precisely so the preset loop cannot touch
 * them -- but that must NOT mean they survive a preset change. Leaving FEG at
 * max silently gave every subsequently loaded preset 6x envelope times (and
 * left the knob with nowhere to travel, which reads as "the macro does
 * nothing"). The write-through macros are worse: wave/tune2 do not re-apply on
 * preset load, so a stale position would keep displaying while the preset's
 * own oscillator setup was actually in force -- the knob lying about the sound.
 *
 * State restore still round-trips: restore_state calls load_preset first and
 * then overlays every PARAMS key, macros included. */
static void reset_macros(nm_instance_t *inst) {
    inst->eng[NM_M_WAVE]   = 0.0f;   /* OFF */
    inst->eng[NM_M_TUNE2]  = 0.0f;   /* unison */
    inst->eng[NM_M_FENV_T] = 0.5f;   /* x1 */
    inst->eng[NM_M_AENV_T] = 0.5f;   /* x1 */
    inst->wave_tune_semis  = 0.0f;
}

/* ======================================================================== *
 *  Bank discovery + loading
 * ======================================================================== */

static int nm_has_ext(const char *name, const char *ext) {
    size_t nl = strlen(name), el = strlen(ext);
    if (nl <= el) return 0;
    return strcasecmp(name + nl - el, ext) == 0;
}

static int nm_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int nm_str_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

/* Absolute path of a bank's folder. The factory bank has none. */
static void nm_bank_dir(const nm_instance_t *inst, int bank, char *out, int len) {
    out[0] = '\0';
    if (bank <= 0 || bank >= inst->bank_count) return;
    const nm_bank_t *b = &inst->banks[bank];
    if (!inst->bank_root[0]) return;
    if (b->name[0] == '\0') snprintf(out, len, "%s", inst->bank_root);
    else                    snprintf(out, len, "%s/%s", inst->bank_root, b->name);
}

/* Rebuild banks[] from the filesystem. Cheap (one or two readdirs, no file
 * reads), so it runs on every bank-list query rather than needing a Rescan. */
static void nm_scan_banks(nm_instance_t *inst) {
    char prev[NM_MAX_BANKNAME];
    snprintf(prev, sizeof(prev), "%s",
             (inst->cur_bank >= 0 && inst->cur_bank < inst->bank_count)
                 ? inst->banks[inst->cur_bank].name : "");
    int prev_factory = (inst->cur_bank == 0);

    inst->bank_count = 0;
    nm_bank_t *f = &inst->banks[inst->bank_count++];
    snprintf(f->name, sizeof(f->name), "Factory");
    f->is_factory = 1;

    DIR *d = inst->bank_root[0] ? opendir(inst->bank_root) : NULL;
    if (d) {
        int loose = 0;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && inst->bank_count < NM_MAX_BANKS) {
            if (e->d_name[0] == '.') continue;      /* . .. and dotfiles */
            char p[512];
            snprintf(p, sizeof(p), "%s/%s", inst->bank_root, e->d_name);
            if (nm_is_dir(p)) {
                nm_bank_t *b = &inst->banks[inst->bank_count++];
                snprintf(b->name, sizeof(b->name), "%s", e->d_name);
                b->is_factory = 0;
            } else if (nm_has_ext(e->d_name, NM_PRESET_EXT)) {
                loose = 1;
            }
        }
        closedir(d);

        /* Presets sitting directly in the root are a bank too, so pointing the
         * feature at a flat folder of presets works without nesting. */
        if (loose && inst->bank_count < NM_MAX_BANKS) {
            nm_bank_t *b = &inst->banks[inst->bank_count++];
            b->name[0] = '\0';                      /* "" == the root itself */
            b->is_factory = 0;
        }

        /* Sort the imported banks (leave Factory pinned at 0). */
        if (inst->bank_count > 2)
            qsort(inst->banks + 1, (size_t)(inst->bank_count - 1),
                  sizeof(nm_bank_t), nm_str_cmp);
    }

    /* Re-find the selected bank BY NAME. Indices move when a folder is added
     * or removed, so restoring an index would silently select a different
     * bank. */
    inst->cur_bank = 0;
    if (!prev_factory) {
        for (int i = 1; i < inst->bank_count; i++) {
            if (strcmp(inst->banks[i].name, prev) == 0) { inst->cur_bank = i; break; }
        }
    }
}

/* Collect preset files under `dir`, recursively, as paths relative to the
 * bank root. Depth-limited: a symlink loop under a user-writable directory
 * would otherwise hang the synth. */
static void nm_collect(nm_instance_t *inst, const char *dir, const char *rel,
                       int depth, int max_depth) {
    if (depth > max_depth || inst->bank_file_count >= NM_MAX_BANK_PRESETS) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && inst->bank_file_count < NM_MAX_BANK_PRESETS) {
        if (e->d_name[0] == '.') continue;
        char sub[512], subrel[NM_MAX_RELPATH];
        snprintf(sub, sizeof(sub), "%s/%s", dir, e->d_name);
        if (rel[0]) snprintf(subrel, sizeof(subrel), "%s/%s", rel, e->d_name);
        else        snprintf(subrel, sizeof(subrel), "%s", e->d_name);
        if (nm_is_dir(sub)) {
            nm_collect(inst, sub, subrel, depth + 1, max_depth);
        } else if (nm_has_ext(e->d_name, NM_PRESET_EXT)) {
            snprintf(inst->bank_files[inst->bank_file_count++], NM_MAX_RELPATH, "%s", subrel);
        }
    }
    closedir(d);
}

/* Populate bank_files[] for the current bank. Sorted so indices are stable. */
static void nm_load_bank_files(nm_instance_t *inst) {
    inst->bank_file_count = 0;
    if (inst->cur_bank <= 0) return;              /* factory is baked in */
    if (!inst->bank_files) {
        inst->bank_files = (char (*)[NM_MAX_RELPATH])
            calloc(NM_MAX_BANK_PRESETS, NM_MAX_RELPATH);
        if (!inst->bank_files) return;
    }
    char dir[512];
    nm_bank_dir(inst, inst->cur_bank, dir, sizeof(dir));
    /* ⚠ The "(loose)" bank IS the root, whose subfolders are the other banks.
     * Recursing there would gather every preset on the device into it -- the
     * whole library, duplicated, on top of the banks that already hold it.
     * Loose means loose: depth 0 only. */
    int max_depth = inst->banks[inst->cur_bank].name[0] ? 4 : 0;
    if (dir[0]) nm_collect(inst, dir, "", 0, max_depth);
    if (inst->bank_file_count > 1)
        qsort(inst->bank_files, (size_t)inst->bank_file_count,
              NM_MAX_RELPATH, nm_str_cmp);
}

static int nm_preset_count(const nm_instance_t *inst) {
    return inst->cur_bank <= 0 ? NM_FACTORY_COUNT : inst->bank_file_count;
}

/* Display name for a preset: the file's stem. */
static void nm_preset_name(const nm_instance_t *inst, int idx, char *out, int len) {
    out[0] = '\0';
    if (inst->cur_bank <= 0) {
        if (idx >= 0 && idx < NM_FACTORY_COUNT)
            snprintf(out, len, "%s", NM_FACTORY_BANK[idx].name);
        return;
    }
    if (idx < 0 || idx >= inst->bank_file_count) return;
    const char *rel = inst->bank_files[idx];
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    snprintf(out, len, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot && strcasecmp(dot, NM_PRESET_EXT) == 0) *dot = '\0';
}

/* Install an arbitrary spline shape (imported presets); the factory path goes
 * through install_preset_spline, which indexes the baked table. */
static void install_spline_points(nm_instance_t *inst,
                                  const nm_spline_point_t *pts, int count);

/* Read + apply one imported preset. Returns 1 on success. */
static int nm_load_imported(nm_instance_t *inst, int idx) {
    if (idx < 0 || idx >= inst->bank_file_count) return 0;
    char dir[512], path[700];
    nm_bank_dir(inst, inst->cur_bank, dir, sizeof(dir));
    if (!dir[0]) return 0;
    snprintf(path, sizeof(path), "%s/%s", dir, inst->bank_files[idx]);

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* A preset is a few KB; anything far larger is not one, and reading it
     * would be a denial of service against a synth voice. */
    if (n <= 0 || n > 512 * 1024) { fclose(f); return 0; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';

    nm_import_preset_t p;
    int ok = nm_import_parse(buf, (int)got, &p, NULL);
    free(buf);
    if (!ok) return 0;

    inst->synth->setPanic();
    reset_macros(inst);
    for (int i = 1; i < NUMPARAM; i++) {          // skip UNUSED1(0)
        if (i == PANIC) continue;
        apply_engine(inst, i, p.data[i]);
    }
    install_spline_points(inst, p.spline, p.spline_count);
    return 1;
}

static void load_preset(nm_instance_t *inst, int idx) {
    if (inst->cur_bank > 0) {
        if (nm_load_imported(inst, idx)) inst->cur_preset = idx;
        return;
    }
    if (idx < 0 || idx >= NM_FACTORY_COUNT) return;
    inst->cur_preset = idx;
    inst->synth->setPanic();
    reset_macros(inst);
    const nm_factory_preset_t *p = &NM_FACTORY_BANK[idx];
    for (int i = 1; i < NUMPARAM; i++) {          // skip UNUSED1(0)
        if (i == PANIC) continue;
        apply_engine(inst, i, p->programData[i]);
    }
    install_preset_spline(inst, idx);   // per-preset envelope-editor shape
}

/* Switch banks. The preset index does not carry across -- bank 1 slot 40 has
 * nothing to do with bank 2 slot 40 -- so land on the first preset. */
static void nm_switch_bank(nm_instance_t *inst, int bank) {
    if (bank < 0 || bank >= inst->bank_count) return;
    inst->cur_bank = bank;
    nm_load_bank_files(inst);
    inst->cur_preset = -1;
    if (nm_preset_count(inst) > 0) load_preset(inst, 0);
}

/* ======================================================================== *
 *  ui_hierarchy + chain_params JSON
 * ======================================================================== */
static const char *kUiHierarchy =
"{\"modes\":null,\"levels\":{"
 "\"root\":{\"label\":\"Noisemaker\","
   "\"list_param\":\"preset\",\"count_param\":\"preset_count\",\"name_param\":\"preset_name\","
   "\"knobs\":[\"wave\",\"tune2\",\"cutoff\",\"resonance\",\"filter_env\",\"fenv_time\",\"aenv_time\",\"volume\"],"
   "\"params\":["
     "{\"key\":\"editor\",\"label\":\"Editor\"},"
     "{\"level\":\"banks\",\"label\":\"Preset Bank\"},"
     "{\"level\":\"macros\",\"label\":\"Macros\"},"
     "{\"level\":\"osc\",\"label\":\"Oscillators\"},"
     "{\"level\":\"filter\",\"label\":\"Filter\"},"
     "{\"level\":\"fenv\",\"label\":\"Filter Env\"},"
     "{\"level\":\"aenv\",\"label\":\"Amp Env\"},"
     "{\"level\":\"lfo1\",\"label\":\"LFO 1\"},"
     "{\"level\":\"lfo2\",\"label\":\"LFO 2\"},"
     "{\"level\":\"env3\",\"label\":\"Env 3 (Free)\"},"
     "{\"level\":\"envd\",\"label\":\"Env Draw\"},"
     "{\"level\":\"voice\",\"label\":\"Voicing / Vel\"},"
     "{\"level\":\"fx\",\"label\":\"Chorus / Reverb\"},"
     "{\"level\":\"delay\",\"label\":\"Delay\"}"
   "]},"
 /* Selection level: items_param is re-read (and rescanned) on entry, so banks
  * copied onto the device appear without any explicit refresh. */
 "\"banks\":{\"label\":\"Preset Bank\",\"items_param\":\"bank_list\",\"select_param\":\"bank_index\"},"
 "\"macros\":{\"knobs\":[\"wave\",\"tune2\",\"cutoff\",\"resonance\",\"filter_env\",\"fenv_time\",\"aenv_time\",\"volume\"],"
   "\"params\":[\"wave\",\"tune2\",\"fenv_time\",\"aenv_time\",\"cutoff\",\"resonance\",\"filter_env\",\"volume\"]},"
 "\"osc\":{\"knobs\":[\"osc1_wave\",\"osc2_wave\",\"osc1_vol\",\"osc2_vol\",\"osc3_vol\",\"detune\",\"osc1_pw\",\"ringmod\"],"
   "\"params\":[\"osc1_wave\",\"osc1_vol\",\"osc1_tune\",\"osc1_fine\",\"osc1_pw\",\"osc1_phase\","
     "\"osc2_wave\",\"osc2_vol\",\"osc2_tune\",\"osc2_fine\",\"osc2_phase\",\"osc2_fm\","
     "\"osc3_vol\",\"osc_sync\",\"ringmod\",\"detune\",\"osc_tune\",\"bitcrush\",\"vintage\"]},"
 "\"filter\":{\"knobs\":[\"filter_type\",\"cutoff\",\"resonance\",\"keyfollow\",\"filter_env\",\"filter_drive\",\"highpass\",\"vel_cut\"],"
   "\"params\":[\"filter_type\",\"cutoff\",\"resonance\",\"keyfollow\",\"filter_env\",\"filter_drive\",\"highpass\"]},"
 "\"fenv\":{\"knobs\":[\"fenv_a\",\"fenv_d\",\"fenv_s\",\"fenv_r\",\"fenv_time\",\"filter_env\",\"cutoff\",\"resonance\"],"
   "\"params\":[\"fenv_a\",\"fenv_d\",\"fenv_s\",\"fenv_r\",\"fenv_time\"]},"
 "\"aenv\":{\"knobs\":[\"aenv_a\",\"aenv_d\",\"aenv_s\",\"aenv_r\",\"aenv_time\",\"vel_vol\",\"volume\",\"cutoff\"],"
   "\"params\":[\"aenv_a\",\"aenv_d\",\"aenv_s\",\"aenv_r\",\"aenv_time\"]},"
 "\"lfo1\":{\"knobs\":[\"lfo1_wave\",\"lfo1_rate\",\"lfo1_amount\",\"lfo1_dest\",\"lfo1_sync\",\"lfo1_keytrig\",\"lfo1_phase\",\"volume\"],"
   "\"params\":[\"lfo1_wave\",\"lfo1_rate\",\"lfo1_amount\",\"lfo1_dest\",\"lfo1_sync\",\"lfo1_keytrig\",\"lfo1_phase\"]},"
 "\"lfo2\":{\"knobs\":[\"lfo2_wave\",\"lfo2_rate\",\"lfo2_amount\",\"lfo2_dest\",\"lfo2_sync\",\"lfo2_keytrig\",\"lfo2_phase\",\"volume\"],"
   "\"params\":[\"lfo2_wave\",\"lfo2_rate\",\"lfo2_amount\",\"lfo2_dest\",\"lfo2_sync\",\"lfo2_keytrig\",\"lfo2_phase\"]},"
 "\"env3\":{\"knobs\":[\"free_a\",\"free_d\",\"free_amt\",\"free_dest\",\"cutoff\",\"resonance\",\"filter_env\",\"volume\"],"
   "\"params\":[\"free_a\",\"free_d\",\"free_amt\",\"free_dest\"]},"
 "\"envd\":{\"knobs\":[\"env_dest\",\"env_amt\",\"env_speed\",\"cutoff\",\"resonance\",\"filter_env\",\"aenv_a\",\"volume\"],"
   "\"params\":[\"env_dest\",\"env_amt\",\"env_speed\"]},"
 "\"voice\":{\"knobs\":[\"voices\",\"portamento\",\"porta_mode\",\"vel_vol\",\"vel_env\",\"vel_cut\",\"pw_pitch\",\"pw_cutoff\"],"
   "\"params\":[\"voices\",\"portamento\",\"porta_mode\",\"vel_vol\",\"vel_env\",\"vel_cut\",\"pw_pitch\",\"pw_cutoff\"]},"
 "\"fx\":{\"knobs\":[\"chorus1\",\"chorus2\",\"reverb_wet\",\"reverb_decay\",\"reverb_pre\",\"reverb_hi\",\"reverb_lo\",\"volume\"],"
   "\"params\":[\"chorus1\",\"chorus2\",\"reverb_wet\",\"reverb_decay\",\"reverb_pre\",\"reverb_hi\",\"reverb_lo\"]},"
 "\"delay\":{\"knobs\":[\"delay_wet\",\"delay_time\",\"delay_fb\",\"delay_sync\",\"delay_fac_l\",\"delay_fac_r\",\"delay_hi\",\"delay_lo\"],"
   "\"params\":[\"delay_wet\",\"delay_time\",\"delay_fb\",\"delay_sync\",\"delay_fac_l\",\"delay_fac_r\",\"delay_hi\",\"delay_lo\"]}"
"}}";

static int build_chain_params(char *buf, int len) {
    int n = 0;
    n += snprintf(buf + n, len - n, "[");
    n += snprintf(buf + n, len - n,
        "{\"key\":\"editor\",\"name\":\"Bank Editor\",\"type\":\"canvas\","
        "\"canvas_script\":\"canvas.js#bank_editor\",\"show_footer\":false,\"show_value\":false}");
    for (int i = 0; i < NM_PARAM_COUNT && n < len - 256; i++) {
        const param_def_t *p = &PARAMS[i];
        n += snprintf(buf + n, len - n, ",");
        switch (p->kind) {
            case K_PCT:
                n += snprintf(buf + n, len - n,
                    "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"int\",\"min\":0,\"max\":100,\"step\":1,\"unit\":\"%%\"}",
                    p->key, p->name);
                break;
            case K_BIPOLAR:
                n += snprintf(buf + n, len - n,
                    "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"int\",\"min\":0,\"max\":100,\"step\":1}",
                    p->key, p->name);
                break;
            case K_FBGAIN:   /* loop gain x100; 100 = unity = never decays */
                n += snprintf(buf + n, len - n,
                    "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"int\",\"min\":0,\"max\":%d,\"step\":1,\"unit\":\"%%\"}",
                    p->key, p->name, NM_FB_DISP_MAX);
                break;
            case K_TOGGLE:
                n += snprintf(buf + n, len - n,
                    "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]}",
                    p->key, p->name);
                break;
            case K_INT:
                n += snprintf(buf + n, len - n,
                    "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"int\",\"min\":%d,\"max\":%d}",
                    p->key, p->name, p->imin, p->imax);
                break;
            case K_ENUM:
            case K_LFOWAVE: {
                n += snprintf(buf + n, len - n,
                    "{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"enum\",\"options\":[", p->key, p->name);
                for (int o = 0; o < p->n_opts; o++)
                    n += snprintf(buf + n, len - n, "%s\"%s\"", o ? "," : "", p->opts[o]);
                n += snprintf(buf + n, len - n, "]}");
                break;
            }
        }
    }
    n += snprintf(buf + n, len - n, "]");
    return n;
}

/* ======================================================================== *
 *  v2 entry points
 * ======================================================================== */
static void *v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    nm_instance_t *inst = (nm_instance_t *)calloc(1, sizeof(nm_instance_t));
    if (!inst) return NULL;
    if (module_dir) { strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1); }
    inst->tempo_bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
    if (inst->tempo_bpm <= 0.0f) inst->tempo_bpm = 120.0f;
    inst->octave_transpose = 0;
    inst->editor_page = 0;
    inst->cur_preset = -1;
    inst->cur_bank = 0;
    inst->bank_files = NULL;
    inst->bank_file_count = 0;
    /* Imported banks live beside the module, matching obxd. With no
     * module_dir there is nowhere to look and imports stay disabled. */
    if (inst->module_dir[0])
        snprintf(inst->bank_root, sizeof(inst->bank_root), "%s/%s",
                 inst->module_dir, NM_BANK_SUBDIR);
    nm_scan_banks(inst);

    inst->synth = new SynthEngine((float)MOVE_SAMPLE_RATE);
    inst->synth->setNumberOfVoices(1.0f);   // normalized -> combo 6

    for (int i = 0; i < NM_ENG_SLOTS; i++) inst->eng[i] = 0.0f;
    /* Macro neutrals must be in place before any envelope value is applied. */
    reset_macros(inst);
    for (int i = 0; i < DEFAULT_PATCH_COUNT; i++) {
        int idx = DEFAULT_PATCH[i].index;
        if (idx >= 0 && idx < NUMPARAM) apply_engine(inst, idx, DEFAULT_PATCH[i].value);
    }
    load_preset(inst, 0);   /* factory startup patch; host preset recall overrides */
    return inst;
}

static void v2_destroy_instance(void *instance) {
    nm_instance_t *inst = (nm_instance_t *)instance;
    if (!inst) return;
    delete inst->synth;
    free(inst->bank_files);
    free(inst);
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    nm_instance_t *inst = (nm_instance_t *)instance;
    if (!inst || !inst->synth || len < 2) return;
    (void)source;
    uint8_t status = msg[0] & 0xF0;
    uint8_t d1 = msg[1];
    uint8_t d2 = (len > 2) ? msg[2] : 0;

    switch (status) {
        case 0x90: {
            int note = d1 + inst->octave_transpose * 12;
            if (note < 0) note = 0; if (note > 127) note = 127;
            if (d2 > 0) inst->synth->setNoteOn(note, d2 / 127.0f);
            else        inst->synth->setNoteOff(note);
            break;
        }
        case 0x80: {
            int note = d1 + inst->octave_transpose * 12;
            if (note < 0) note = 0; if (note > 127) note = 127;
            inst->synth->setNoteOff(note);
            break;
        }
        case 0xE0: {
            if (len < 3) break;
            int bend = ((d2 << 7) | d1) - 8192;
            inst->synth->setPitchwheelAmount(bend / 8192.0f);
            break;
        }
        case 0xB0: {
            if (d1 == 120 || d1 == 123) inst->synth->setPanic();
            break;
        }
        default: break;
    }
}

/* ---- minimal JSON getters for the slot "state" blob (per obxd) ---- */
static int nm_json_get_number(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    pos += strlen(search);
    while (*pos == ' ') pos++;
    *out = (float)atof(pos);
    return 0;
}

/* Serialize the full instance state (preset + octave + every param as its
 * display value) so the host can autosave the slot and the module-preset
 * feature can capture it. Restored by set_param("state", ...). */
static int build_state(nm_instance_t *inst, char *buf, int buf_len) {
    int n = 0;
    /* The bank is stored by NAME. An index would silently point at a different
     * bank as soon as the user adds or removes a folder. */
    n += snprintf(buf + n, buf_len - n,
                  "{\"preset\":%d,\"octave_transpose\":%d,\"bank_name\":\"%s\"",
                  inst->cur_preset, inst->octave_transpose,
                  (inst->cur_bank > 0 && inst->cur_bank < inst->bank_count)
                      ? inst->banks[inst->cur_bank].name : "");
    char v[32];
    for (int i = 0; i < NM_PARAM_COUNT && n < buf_len - 64; i++) {
        engine_to_disp(&PARAMS[i], inst->eng[PARAMS[i].engine_index], v, sizeof(v));
        n += snprintf(buf + n, buf_len - n, ",\"%s\":%s", PARAMS[i].key, v);
    }
    n += snprintf(buf + n, buf_len - n, "}");
    return n;
}

/* Restore from a state blob: apply the preset first (installs its envelope
 * shape + base param values), then overlay every stored param value. */
static void restore_state(nm_instance_t *inst, const char *json) {
    float f;

    /* Bank first: it decides what the preset index means. Resolved by name
     * against a fresh scan, so a state blob still finds its bank after other
     * folders have come and gone. A bank that is no longer on the device falls
     * back to Factory rather than to whatever now sits at that index. */
    nm_scan_banks(inst);
    char want[NM_MAX_BANKNAME] = "";
    const char *bp = strstr(json, "\"bank_name\":\"");
    if (bp) {
        bp += 13;
        int j = 0;
        while (*bp && *bp != '"' && j < (int)sizeof(want) - 1) want[j++] = *bp++;
        want[j] = '\0';
    }
    int bank = 0;
    if (want[0]) {
        for (int i = 1; i < inst->bank_count; i++)
            if (strcmp(inst->banks[i].name, want) == 0) { bank = i; break; }
    }
    inst->cur_bank = bank;
    nm_load_bank_files(inst);

    if (nm_json_get_number(json, "preset", &f) == 0) {
        int idx = (int)f;
        if (idx >= 0 && idx < nm_preset_count(inst)) load_preset(inst, idx);
    }
    if (nm_json_get_number(json, "octave_transpose", &f) == 0)
        inst->octave_transpose = (int)f;
    for (int i = 0; i < NM_PARAM_COUNT; i++) {
        char search[64];
        snprintf(search, sizeof(search), "\"%s\":", PARAMS[i].key);
        const char *pos = strstr(json, search);
        if (!pos) continue;
        pos += strlen(search);
        while (*pos == ' ') pos++;
        char dv[32]; int j = 0;
        while (*pos && *pos != ',' && *pos != '}' && j < (int)sizeof(dv) - 1) dv[j++] = *pos++;
        dv[j] = '\0';
        apply_engine(inst, PARAMS[i].engine_index, disp_to_engine(&PARAMS[i], dv));
    }
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    nm_instance_t *inst = (nm_instance_t *)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "all_notes_off") == 0) { inst->synth->setPanic(); return; }
    if (strcmp(key, "octave_transpose") == 0) { inst->octave_transpose = atoi(val); return; }
    if (strcmp(key, "editor") == 0) { inst->editor_page = atoi(val); return; }
    if (strcmp(key, "state") == 0) { restore_state(inst, val); return; }
    if (strcmp(key, "preset") == 0) {
        int idx = atoi(val);
        if (idx >= 0 && idx < nm_preset_count(inst) && idx != inst->cur_preset)
            load_preset(inst, idx);
        return;
    }
    if (strcmp(key, "bank_index") == 0) {
        int idx = atoi(val);
        /* The list the UI is selecting from came from a scan; re-scan so a
         * folder added since then cannot make the index point elsewhere. */
        nm_scan_banks(inst);
        if (idx >= 0 && idx < inst->bank_count && idx != inst->cur_bank)
            nm_switch_bank(inst, idx);
        return;
    }

    const param_def_t *p = find_param(key);
    if (!p) return;
    apply_engine(inst, p->engine_index, disp_to_engine(p, val));
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    nm_instance_t *inst = (nm_instance_t *)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "ui_hierarchy") == 0)
        return snprintf(buf, buf_len, "%s", kUiHierarchy);
    if (strcmp(key, "chain_params") == 0)
        return build_chain_params(buf, buf_len);
    if (strcmp(key, "name") == 0)
        return snprintf(buf, buf_len, "Noisemaker");
    if (strcmp(key, "state") == 0)
        return build_state(inst, buf, buf_len);
    if (strcmp(key, "preset_name") == 0) {
        char nm[NM_MAX_RELPATH];
        nm_preset_name(inst, inst->cur_preset, nm, sizeof(nm));
        return snprintf(buf, buf_len, "%s", nm[0] ? nm : "Init");
    }
    if (strcmp(key, "preset_count") == 0)
        return snprintf(buf, buf_len, "%d", nm_preset_count(inst));
    if (strcmp(key, "preset") == 0)
        return snprintf(buf, buf_len, "%d", inst->cur_preset < 0 ? 0 : inst->cur_preset);

    /* ---- banks ----
     * bank_list rescans on every read, so a folder copied onto the device is
     * simply there the next time the selector opens -- no Rescan action, and
     * no way for the list to be stale. */
    if (strcmp(key, "bank_list") == 0) {
        nm_scan_banks(inst);
        /* ⚠ The host's contract for an items_param level is an array of
         * OBJECTS, not of strings. shadow_ui.js does
         *     label: item.label || item.name || `Item ${item.index}`
         * and, on click, writes String(item.index) to select_param. An array
         * of plain strings therefore renders every row as "Item undefined"
         * AND selects index "undefined" -> 0. Both fields are required. */
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < inst->bank_count && n < buf_len - 96; i++) {
            const char *nm = inst->banks[i].name[0] ? inst->banks[i].name : "(loose)";
            n += snprintf(buf + n, buf_len - n, "%s{\"index\":%d,\"name\":\"", i ? "," : "", i);
            /* Folder names are user data: escape what would break the JSON. */
            for (const char *c = nm; *c && n < buf_len - 8; c++) {
                if (*c == '"' || *c == '\\') n += snprintf(buf + n, buf_len - n, "\\%c", *c);
                else if ((unsigned char)*c < 0x20) n += snprintf(buf + n, buf_len - n, " ");
                else n += snprintf(buf + n, buf_len - n, "%c", *c);
            }
            n += snprintf(buf + n, buf_len - n, "\"}");
        }
        n += snprintf(buf + n, buf_len - n, "]");
        return n;
    }
    if (strcmp(key, "bank_index") == 0)
        return snprintf(buf, buf_len, "%d", inst->cur_bank);
    if (strcmp(key, "bank_count") == 0)
        return snprintf(buf, buf_len, "%d", inst->bank_count);
    if (strcmp(key, "bank_name") == 0)
        return snprintf(buf, buf_len, "%s",
            (inst->cur_bank >= 0 && inst->cur_bank < inst->bank_count &&
             inst->banks[inst->cur_bank].name[0])
                ? inst->banks[inst->cur_bank].name : "Factory");
    if (strcmp(key, "octave_transpose") == 0)
        return snprintf(buf, buf_len, "%d", inst->octave_transpose);
    if (strcmp(key, "editor") == 0)
        return snprintf(buf, buf_len, "%d", inst->editor_page);

    const param_def_t *p = find_param(key);
    if (!p) return -1;
    engine_to_disp(p, inst->eng[p->engine_index], buf, buf_len);
    return (int)strlen(buf);
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    if (buf && buf_len > 0) buf[0] = '\0';
    return 0;
}

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    nm_instance_t *inst = (nm_instance_t *)instance;
    if (!inst || !inst->synth) {
        if (out_interleaved_lr) memset(out_interleaved_lr, 0, frames * 4);
        return;
    }
    /* Refresh tempo for host-synced LFOs + delay; re-apply only when BPM moves. */
    if (g_host && g_host->get_bpm) {
        float bpm = g_host->get_bpm();
        if (bpm > 0.0f && fabsf(bpm - inst->tempo_bpm) > 0.01f) {
            inst->tempo_bpm = bpm;
            inst->synth->setLfo1Rate(inst->eng[LFO1RATE], bpm);
            inst->synth->setLfo2Rate(inst->eng[LFO2RATE], bpm);
            inst->synth->setDelayBpm(bpm);
        }
    }
    for (int i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;
        inst->synth->process(&l, &r);
        int32_t li = (int32_t)(l * 32767.0f);
        int32_t ri = (int32_t)(r * 32767.0f);
        if (li >  32767) li =  32767; if (li < -32768) li = -32768;
        if (ri >  32767) ri =  32767; if (ri < -32768) ri = -32768;
        out_interleaved_lr[i * 2]     = (int16_t)li;
        out_interleaved_lr[i * 2 + 1] = (int16_t)ri;
    }
}

static plugin_api_v2_t g_plugin_api_v2 = {
    MOVE_PLUGIN_API_VERSION_2,
    v2_create_instance,
    v2_destroy_instance,
    v2_on_midi,
    v2_set_param,
    v2_get_param,
    v2_get_error,
    v2_render_block,
};

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &g_plugin_api_v2;
}
