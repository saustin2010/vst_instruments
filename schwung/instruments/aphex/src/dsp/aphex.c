/**
 * Aphex — Schwung sound generator
 * Author: Filliformes
 * License: MIT
 *
 * MS-10 / MS-20 hybrid mono synth with semi-modular patchbay and ESP.
 * Architecture: Monophonic, last-note priority. Voice state encapsulated
 * in voice_t for clean future polyphonic expansion (just turn `voice_t voice`
 * into `voice_t voices[N]` and add a voice allocator in on_midi).
 *
 * API: plugin_api_v2_t (8 fields, including get_error)
 * Audio: 44100 Hz, 128 frames/block, stereo interleaved int16 output
 *
 * SIGNAL FLOW
 *   VCO1 ─┬───→ MIX → DRIVE → HPF (K35) → LPF (K35) → VCA → tanh → OUT
 *   VCO2 ─┤                                  ^           ^
 *   Sub   ┤                                  │           │
 *   Ring  ┤                                cutoff      ENV2
 *   Noise ┤                                ENV1·amt
 *   ESPAud┘
 *
 * NOTE — K35 filter is a clean-room implementation following Pirkle's full
 * TPT delay-less topology (AN-5 LPF, AN-7v2 HPF) with predictor-corrector
 * NLP placement. See k35.h for the derivation. MIT-licensed.
 *
 * v0.1: 8-destination patchbay, 15 sources (incl. ESP). Mono only.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "host/plugin_api_v1.h"
#include "k35.h"
#include "k35_ota.h"
#include "yin.h"
#include "drift.h"

#define SAMPLE_RATE 44100.0f
#define INV_SR     (1.0f / 44100.0f)
#define PI          3.14159265359f
#define TWO_PI      6.28318530718f
#define VEL_FLOOR   0.25f   /* softest-pad-hit gain floor (at full Vel→Amp depth) */
#define SMOOTH_20MS 0.0011331f  /* per-sample one-pole coef ≈ 20 ms TC at 44.1 kHz */

#define BLOCK       128

/* filter_lpf_dispatch/filter_hpf_dispatch pun k35_t* <-> k35_ota_t* (both are
 * three TPT integrator floats). Guard that assumption at compile time — editing
 * one struct without the other now fails the build instead of corrupting state. */
_Static_assert(sizeof(k35_t) == sizeof(k35_ota_t),
               "k35_t and k35_ota_t must stay layout-compatible (filter dispatch puns between them)");

/* Wave enums — MUST match module.json's options arrays exactly, otherwise
 * parse_enum falls through to atoi(val) which returns 0 for any non-numeric
 * string, silently mapping all "unknown" wave names to the first option.
 * VCO 1 and VCO 2 expose DIFFERENT option sets per the MS-20 spec:
 *   VCO 1: Tri / Saw / PW-Square / Noise
 *   VCO 2: Saw / Square / Pulse / Ring
 * They map to different osc_render cases in the render loop. */
static const char *V1_WAVE_NAMES[4]   = {"Tri","Saw","PW-Square","Noise"};
static const char *V2_WAVE_NAMES[4]   = {"Saw","Square","Pulse","Ring"};
static const char *NOISE_NAMES[2]     = {"White","Pink"};
static const char *SYNC_NAMES[2]      = {"Off","On"};
static const char *FILT_MODE_NAMES[4] = {"HP+LP","LP only","HP only","Notch"};
/* Filter circuit revision: REV.1 = early-MS-20/MS-10 KORG-35 (creamy fuzz),
 * REV.2 = late-MS-20 LM13600 OTA (cleaner howl). */
static const char *FILT_REV_NAMES[2] = {"REV.1 KORG-35","REV.2 OTA"};
/* VCO SCALE switches (organ footage). 8' is the reference (0 semitone offset).
 * Each step = one octave (12 st), NOT one semitone. */
static const char *V1_SCALE_NAMES[4] = {"32'","16'","8'","4'"};  /* offset (idx-2)*12 */
static const char *V2_SCALE_NAMES[4] = {"16'","8'","4'","2'"};   /* offset (idx-1)*12 */
static const char *ESP_PITCH_MODES[3] = {"Off","Track","Quantized"};
static const char *GATE_POL_NAMES[2]  = {"+","-"};
static const char *ON_OFF_NAMES[2]    = {"Off","On"};
/* 41 presets: Init + 40 character patches inspired by Korg Collection MS-20 V2
 * and Arturia MS-20 V factory libraries. Distribution roughly mirrors the
 * Cyborg pack: 8 Bass, 8 Lead, 4 Pad, 3 Keys, 3 Seq, 5 SFX, 4 Drone, 5 ESP. */
#define PRESET_COUNT 41
static const char *PRESET_NAMES[PRESET_COUNT] = {
    "Init",
    /* Bass (8) */
    "Sticky Bass","Acid Squelch","Sub Wobbler","FM Bass",
    "Reso Bass","Pluck Bass","Detuned Doom","Picked Bass",
    /* Lead (8) */
    "Lead Scream","Sync Howl","Whistle Lead","Filter Cry",
    "FM Lead","Hard Sync 5th","Resonant Spike","Saw Stack",
    /* Pad (4) */
    "Analog Pad","Sweep Pad","Ghost Choir","Slow Wash",
    /* Keys (3) */
    "Vintage EP","Glass Bell","Plucked Clav",
    /* Sequence (3) */
    "Acid Seq","Techno Pulse","Wobbly Bleep",
    /* SFX (5) */
    "Laser Zap","Computer Bleep","Helicopter","Stutter Glitch","Random Patch",
    /* Drone (4) */
    "Endless Hold","Slow Evolution","Tape Hiss Drone","Modular Hum",
    /* ESP / External (5) */
    "Vocal Harmonizer","Talk Box","Audio Tracker","Feedback Howl","ESP Mangler"
};

/* ── Patchbay jack-source enum tables (v0.2 source-side model) ────────────────
 * Each MS-20 input jack accepts a small subset of sources. These tables enumerate
 * what's plugged into each jack — the "default" entry (index 0) corresponds to
 * the hard-wired MS-20 routing when nothing is patched. The N counts must match
 * the option lists in module.json exactly. */
static const char *PB_JACK_NAMES_KBD[6]    = {"Normal","ESP CV","Wheel","S&H","Trig CV","None"};
#define PB_JACK_NAMES_KBD_N 6
static const char *PB_JACK_NAMES_TOTAL[9]  = {"MG","Wheel","ESP CV","ESP Env","S&H","Noise","EG1 Rev","EG2 Rev","None"};
#define PB_JACK_NAMES_TOTAL_N 9
static const char *PB_JACK_NAMES_FREQ[7]   = {"EG1","EG2","ESP Env","Wheel","S&H","Noise","None"};
#define PB_JACK_NAMES_FREQ_N 7
static const char *PB_JACK_NAMES_HPF[7]    = {"EG1","EG2","ESP Env","Wheel","S&H","Noise","None"};
#define PB_JACK_NAMES_HPF_N 7
static const char *PB_JACK_NAMES_LPF[7]    = {"EG1","EG2","ESP Env","Wheel","S&H","Noise","None"};
#define PB_JACK_NAMES_LPF_N 7
static const char *PB_JACK_NAMES_VCA[6]    = {"None","Wheel","ESP Env","EG1","S&H","Noise"};
#define PB_JACK_NAMES_VCA_N 6
static const char *PB_JACK_NAMES_PWM[9]    = {"None","MG Tri","MG Sq","EG1","EG2","S&H","Noise","ESP Env","Wheel"};
#define PB_JACK_NAMES_PWM_N 9
/* Ext Sig sources: classic MS-20 (None / ESP Audio / Noise / Mixer Out)
 * + Schwung-only Track 1-4 audio via /move-track-audio SHM (v0.7.8+).
 * Caveat: routing your own track back into Aphex's filter creates a feedback
 * loop — high LPF Reso + Track N self-feedback will run away quickly. */
static const char *PB_JACK_NAMES_EXTSIG[8] = {
    "None","ESP Audio","Noise","Mixer Out",
    "Track 1","Track 2","Track 3","Track 4"
};
#define PB_JACK_NAMES_EXTSIG_N 8
/* MS-20 FS panel jacks Aphex didn't expose before this turn. */
static const char *PB_JACK_NAMES_VCO2CV[6] = {"None","ESP CV","Wheel","S&H","Noise","KBD CV"};
#define PB_JACK_NAMES_VCO2CV_N 6
static const char *PB_JACK_NAMES_EG1TRIG[5] = {"None","S&H","ESP Gate","Wheel","Noise"};
#define PB_JACK_NAMES_EG1TRIG_N 5
static const char *PB_JACK_NAMES_EG12TRIG[5] = {"None","S&H","ESP Gate","Wheel","Noise"};
#define PB_JACK_NAMES_EG12TRIG_N 5
#define PB_NUM_JACKS 11
/* pb_jack[] index assignments (stable for state save/load):
 *   0  pb_kbd_cv     1  pb_total      2  pb_freq        3  pb_hpf_cv
 *   4  pb_lpf_cv     5  pb_vca_in     6  pb_pwm_in      7  pb_ext_sig
 *   8  pb_vco2_cv    9  pb_eg1_trig   10 pb_eg12_trig
 */

/* ── Host API ───────────────────────────────────────────────────────────────
 * Layout comes from host/plugin_api_v1.h — DO NOT redefine inline. The real
 * struct has sample_rate + frames_per_block ahead of mapped_memory; an inline
 * stub with `void *_pad[N]` will misalign the offsets and segfault when the
 * ESP code dereferences audio_in. */

/* ── Voice state (per-voice — poly-ready) ────────────────────────────────── */

typedef struct {
    int    active;
    int    note;
    float  velocity;
    float  aftertouch;

    float  freq;            /* current freq (Hz, after glide) */
    float  freq_target;     /* target freq from MIDI note */

    float  phase1, phase2;  /* oscillator phases 0..1 */
    float  sub_state;       /* +1/-1 sub-osc divide-by-2 */
    int    last_p1_wrap;    /* used for sync */

    int    e1_stage, e2_stage;
    /* Stages: 0=Delay (EG1) / Hold (EG2)  1=Attack  2=Decay  3=Sustain  4=Release  5=Idle.
     * EG1 (MS-20 trapezoid) skips Decay+Sustain — when stage 1 completes it jumps
     * straight to stage 3 with level pinned at 1.0; on note-off, jumps to Release. */
    float  e1, e2;                  /* envelope levels 0..1 */
    float  e1_timer, e2_timer;      /* seconds elapsed in current Delay/Hold stage */
    /* Smoothed VCA gain — one-pole LP on (e2 + jack_vca_init) * velocity.
     * Levels are continuous across envelope stage transitions but the SLOPE
     * jumps (e.g., decay rate → 0 at sustain) — the ear hears slope
     * discontinuities as clicks. ~0.5 ms time constant smears the slope
     * change below audibility without smearing envelope shape. */
    float  sm_vca;
} voice_t;

/* ── Instance state ─────────────────────────────────────────────────────── */

typedef struct {
    const host_api_v1_t *host;

    voice_t voice;          /* mono — promote to voices[N] for poly */

    /* Analog drift system — see drift.h. Multi-source physically-motivated
     * drift seeded per-instance. p_drift ∈ [0,1] is the global intensity. */
    drift_t drift;
    float   p_drift;

    /* Performance */
    float p_drive, p_volume, p_vel_amt;
    float p_mg_rate, p_mg_depth, p_glide;
    float p_master_tune;    /* MS-20 Master Tune, normalized ±1 = ±100 cents */
    int   p_octave;
    int   p_preset;

    /* VCO1 */
    float v1_pitch, v1_fine, v1_pw, v1_sub;
    float v1_pwm_rate, v1_pwm_depth, v1_drift;
    int   v1_wave;
    float v1_pwm_phase;

    /* VCO2 */
    float v2_pitch, v2_fine, v2_pw, v2_xmod, v2_detune, v2_drift;
    int   v2_wave, v2_sync;

    /* Mixer */
    float mix_v1, mix_v2, mix_sub, mix_noise, mix_esp, mix_fb;
    int   noise_color;
    float pink_state[7];    /* Voss-McCartney pink filter */

    /* Filter — Korg-35 LPF + HPF (clean-room TPT delay-less, see k35.h) */
    float lpf_cut, lpf_reso, hpf_cut, hpf_reso, key_track;
    int   filter_mode;
    int   filter_rev;        /* 0 = REV.1 (KORG-35), 1 = REV.2 (OTA). Reserved for k35_ota.h. */
    /* MS-20 panel mod-intensity attenuators (per filter, two each):
     *   MG/T.Ext = bipolar attenuator for MG OR patched T.EXT signal
     *   EG2/Ext  = bipolar attenuator for EG2 OR patched EXT signal
     * Range: -1..+1 (negative inverts modulation polarity, like the original) */
    float hpf_mg_int, hpf_eg_int;
    float lpf_mg_int, lpf_eg_int;

    /* MS-20 VCO MIXER section — FREQ MOD attenuators (panel knobs).
     * These attenuate the patchbay TOTAL and FREQ jack signals on their way
     * to BOTH VCOs' pitch. Independent from the HPF/LPF MG/EG attenuators.
     * Default 0 — knob fully closed = no MG or EG modulation of pitch. */
    float vco_mg_int;     /* MG/T.EXT attenuator (slope source) */
    float vco_eg_int;     /* EG1/EXT attenuator (envelope source) */
    k35_t k35_lpf_state;    /* 3 TPT integrator states for the LPF */
    k35_t k35_hpf_state;    /* 3 TPT integrator states for the HPF */

    /* Envelopes
     * MS-20 EG 1: Delay-Attack-Release (no D, no S — sustain held at max).
     *   Aphex stores 4 values but the canonical MS-20 shape uses only Delay,
     *   Attack, Release (e1_dcy/e1_sus surface on the Modern page as overrides).
     * MS-20 EG 2: Hold-Attack-Decay-Sustain-Release (HADSR). */
    float e1_delay;          /* MS-20 EG1 Delay time (seconds before Attack starts) */
    float e1_atk, e1_rel;
    float e2_hold;           /* MS-20 EG2 Hold time (seconds at peak before Decay) */
    float e2_atk, e2_dcy, e2_sus, e2_rel;

    /* MG / S&H */
    /* MG (LFO) — original MS-20 has TWO simultaneous outputs both available
     * as patch sources, each with continuously variable shape:
     *   mg_shape: slope output. 0=positive saw, 0.5=triangle, 1=negative saw.
     *   mg_pw:    pulse output duty cycle. 0=wide, 0.5=square, 1=narrow.
     * Both outputs run concurrently — `mg_shape` controls the "tri" patch
     * source and `mg_pw` controls the "sq" patch source independently. */
    float mg_shape;
    float mg_pw;
    float mg_delay, mg_phase;
    float sh_rate, sh_smooth;
    float sh_value, sh_target, sh_phase, sh_smoothed;
    float mw_mg, mw_filt;

    /* ESP — MS-20 External Signal Processor.
     * Period-correct controls: Sig Lvl, Low Cut, High Cut, Threshold, CV Adjust.
     * Aphex extras (Pitch Slew, Env Atk/Rel, Aud Mix, Pitch Mode, Gate Pol) live
     * on the Modern page. */
    float esp_in_gain, esp_pitch_slew;
    float esp_env_atk, esp_env_rel;
    float esp_gate_thr, esp_aud_mix;
    float esp_low_cut;       /* MS-20: 50Hz–2.5kHz BPF low cut for pitch tracker */
    float esp_high_cut;      /* MS-20: 100Hz–5kHz BPF high cut for pitch tracker */
    float esp_cv_adjust;     /* MS-20: V/oct trim for the pitch CV out (±100 cents-ish) */
    int   esp_pitch_mode, esp_gate_pol;
    float esp_pitch_raw, esp_pitch_smoothed;   /* V/oct-style CV */
    float esp_env_smoothed;
    int   esp_gate_state;
    float esp_lc_z, esp_hc_z;  /* one-pole states for Lo Cut (HPF) / Hi Cut (LPF) */
    /* YIN pitch tracker buffer */
    float esp_buf[2048];
    int   esp_buf_pos;
    int   esp_pitch_counter;
    /* YIN scratch — cumulative mean normalized difference function (CMNDF).
     * Sized to TAU_MAX, the longest period we search for (512 → ~86 Hz floor). */
    float esp_yin_cmndf[512];
    /* Last raw line-in sample (for ESP routing) */
    float esp_audio_last;

    /* Patchbay (v0.2 source-side jack model — what's plugged into each MS-20 input).
     * Index map: 0=KBD CV, 1=T.EXT, 2=VCO Freq, 3=HPF EG/Ext,
     * 4=LPF EG/Ext, 5=Initial Gain, 6=PWM In, 7=Ext Sig,
     * 8=VCO 2 CV, 9=EG 1 Trig, 10=EG 1+2 Trig.
     * Value 0 = the hard-wired MS-20 default for that jack. */
    int   pb_jack[PB_NUM_JACKS];

    /* MS-10 mode: collapses signal flow to VCO1 + LPF + EG2 only. */
    int   p_ms10_mode;

    /* Edge-detection state for EG TRIG IN jacks. We track previous-tick gate
     * value per jack and retrigger envelopes on rising edges (0→1). */
    float pb_eg1_trig_prev;
    float pb_eg12_trig_prev;

    /* MIDI / shared modulation */
    float pitch_bend;
    float mod_wheel;

    /* Smoothed values (per-sample interp for filter cutoff). sm_drive prevents
     * audible pops when the Drive knob is turned during sustained notes — without
     * smoothing, the tanh saturation level jumps abruptly. ~20ms time constant. */
    float sm_lpf_cut, sm_hpf_cut, sm_lpf_reso, sm_hpf_reso;
    float sm_drive;
    float sm_v1_pitch, sm_v2_pitch;  /* 20ms-smoothed static pitch (knob glide) */
    float sm_volume, sm_mg_depth;    /* 20ms-smoothed master volume / LFO depth */

    /* Output feedback for FB knob */
    float fb_sample;

    /* Output-stage component modeling (vintage character).
     *   out_dc_state — slow LP for DC blocker (~5 Hz HPF residue)
     *   out_xfmr_state — V72-style transformer integrator (~35 Hz);
     *     the HP residue gets tanh'd, models output-transformer iron
     *   out_lpf_state — gentle HF rolloff (~12 kHz one-pole LPF) for
     *     vintage warmth — the MS-20's output bandwidth wasn't 22 kHz
     * All three are very subtle individually; together they pull the
     * sound away from "digital sterile" toward "analog mixing console". */
    float out_dc_state;
    float out_xfmr_state;
    float out_lpf_state;

    /* Random number generator */
    uint32_t rng;

    /* /move-track-audio SHM — read-only access to Tracks 1-4 audio for the
     * "Ext Sig: Track N" patchbay options. Layout: 5 channels × 128 frames ×
     * 2 (stereo int16). Channel i covers offsets [i*256, i*256+256) of int16
     * samples. NULL when the SHM cannot be opened (e.g. running on a host
     * older than v0.7.8) — track sources then silently read as zero. */
    int      track_audio_fd;
    int16_t *track_audio;
    size_t   track_audio_bytes;
} synth_t;

/* /move-track-audio constants — kept here so they can be referenced from
 * jack_ext_sig and the SHM open/close helpers. */
#define TRK_NUM_CHANNELS  5      /* 4 tracks + main (we only use 0..3) */
#define TRK_FRAMES        128
#define TRK_STEREO_INT16_PER_CHAN (TRK_FRAMES * 2)
#define TRK_TOTAL_BYTES   (TRK_NUM_CHANNELS * TRK_STEREO_INT16_PER_CHAN * (int)sizeof(int16_t))

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static inline uint32_t rand32(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s; }
static inline float    rand_f(uint32_t *s) { return (rand32(s) >> 8) * (1.0f / 16777216.0f); }
static inline float    rand_b(uint32_t *s) { return rand_f(s) * 2.0f - 1.0f; }

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int   clampi(int x, int lo, int hi)         { return x < lo ? lo : (x > hi ? hi : x); }

/* Padé tanh approximation — analog warmth */
static inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Fast 2^x for per-sample VCO pitch. Integer part via ldexpf, fractional part
 * via a 6-term minimax polynomial. Max relative error ~1e-5 → under 0.001 cents
 * of detuning (inaudible), but far cheaper than powf(2,x) called twice/sample. */
static inline float fast_exp2(float x) {
    float xi = floorf(x);
    float f  = x - xi;
    float p  = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f
                     + f * (0.0096181f + f * 0.0013333f))));
    return ldexpf(p, (int)xi);
}

/* MIDI note → frequency */
static float note_to_freq(int note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

/* Map normalized 0..1 cutoff knob to Hz, exponential */
static float cutoff_to_hz(float c) {
    /* 20 Hz to 18 kHz, exponential sweep */
    return 20.0f * powf(900.0f, clampf(c, 0.0f, 1.0f));
}

/* === Filter dispatch — switches between REV.1 (KORG-35) and REV.2 (OTA) ===
 *
 * Both filter types share the same 3-integrator state layout (k35_t and
 * k35_ota_t are structurally identical), so we keep ONE state field per
 * filter (k35_t) and just pick which tick function to call. The cast is
 * safe because the structs are layout-compatible by design — see k35_ota.h
 * comments. We use compound literals to avoid any pointer aliasing concerns.
 */
static inline float filter_lpf_dispatch(int rev, k35_t *state,
                                        float in, float fc_hz, float reso, float sr) {
    if (rev == 0) {
        return k35_lpf_tick(state, in, fc_hz, reso, sr);
    } else {
        return k35_ota_lpf_tick((k35_ota_t *)state, in, fc_hz, reso, sr);
    }
}
static inline float filter_hpf_dispatch(int rev, k35_t *state,
                                        float in, float fc_hz, float reso, float sr) {
    if (rev == 0) {
        return k35_hpf_tick(state, in, fc_hz, reso, sr);
    } else {
        return k35_ota_hpf_tick((k35_ota_t *)state, in, fc_hz, reso, sr);
    }
}

/* Pink noise (Voss-McCartney) */
static float pink_noise(synth_t *inst) {
    float white = rand_b(&inst->rng);
    inst->pink_state[0] = 0.99886f * inst->pink_state[0] + white * 0.0555179f;
    inst->pink_state[1] = 0.99332f * inst->pink_state[1] + white * 0.0750759f;
    inst->pink_state[2] = 0.96900f * inst->pink_state[2] + white * 0.1538520f;
    inst->pink_state[3] = 0.86650f * inst->pink_state[3] + white * 0.3104856f;
    inst->pink_state[4] = 0.55000f * inst->pink_state[4] + white * 0.5329522f;
    inst->pink_state[5] = -0.7616f * inst->pink_state[5] - white * 0.0168980f;
    float pink = inst->pink_state[0] + inst->pink_state[1] + inst->pink_state[2]
               + inst->pink_state[3] + inst->pink_state[4] + inst->pink_state[5]
               + inst->pink_state[6] + white * 0.5362f;
    inst->pink_state[6] = white * 0.115926f;
    return pink * 0.11f;
}

/* Polyblep anti-aliasing residual */
static inline float polyblep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t*t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t*t + t + t + 1.0f;
    }
    return 0.0f;
}

/* PolyBLAMP residual — anti-aliases slope discontinuities (triangle corners),
 * the BLEP analogue for waveforms continuous in value but not derivative. */
static inline float polyblamp(float t, float dt) {
    if (t < dt) {
        t = t / dt - 1.0f;
        return -(1.0f / 3.0f) * t * t * t;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt + 1.0f;
        return (1.0f / 3.0f) * t * t * t;
    }
    return 0.0f;
}

/* ── Oscillators ─────────────────────────────────────────────────────────── */

static float osc_render(int wave, float phase, float dt, float pw) {
    /* phase in [0,1), dt = freq/SR */
    float s = 0.0f;
    switch (wave) {
        case 0: { /* Triangle — polyBLAMP-corrected corners (anti-aliased) */
            s = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
            /* Two slope discontinuities per cycle: min at phase 0, max at 0.5.
             * Slope magnitude is 4; residual scaled by slope*dt rounds them. */
            s += 4.0f * dt * polyblamp(phase, dt);
            float pmax = phase + 0.5f; if (pmax >= 1.0f) pmax -= 1.0f;
            s -= 4.0f * dt * polyblamp(pmax, dt);
            break;
        }
        case 1: { /* Saw with polyblep */
            s = 2.0f * phase - 1.0f;
            s -= polyblep(phase, dt);
            break;
        }
        case 2: { /* Pulse with PWM */
            float p = clampf(pw, 0.05f, 0.95f);
            s = (phase < p) ? 1.0f : -1.0f;
            s += polyblep(phase, dt);
            float pp = phase + 1.0f - p;
            if (pp >= 1.0f) pp -= 1.0f;
            s -= polyblep(pp, dt);
            break;
        }
        case 3: { /* Reverse Saw */
            s = 1.0f - 2.0f * phase;
            s += polyblep(phase, dt);
            break;
        }
    }
    return s;
}

/* ── Filter — Korg-35 LPF + HPF ─────────────────────────────────────────────
 *
 * Implementation lives in k35.h (clean-room TPT delay-less, predictor-corrector
 * NLP placement). Each filter is a 12-byte struct of three TPT integrator
 * states. Resonance ∈ [0,1] mapped internally to K ∈ [0,1.99]; self-oscillates
 * cleanly near K=2.0. License: MIT, original derivation from Pirkle AN-5/AN-7v2.
 */


/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/* File-scope host pointer — captured in move_plugin_init_v2 and attached
 * to every instance in create_instance (Phasma/Deforme pattern). */
static const host_api_v1_t *g_host = NULL;

static void apply_init_preset(synth_t *inst);

static void *create_instance(const char *module_dir, const char *json_defaults) {
    (void)module_dir; (void)json_defaults;
    synth_t *inst = calloc(1, sizeof(synth_t));
    if (!inst) return NULL;

    inst->host = g_host;     /* attach line-in access */
    inst->rng = 0xC0FFEE12u;
    apply_init_preset(inst);

    /* Set a default pitch so the Trigger button can preview presets BEFORE
     * any MIDI note arrives. Without this, voice.freq stays at 0 (calloc'd)
     * and the Trigger button plays silent DC — making "preview the random
     * patch I just generated" impossible. A3 = 220 Hz, a natural bass-mid
     * register that sits well for most preset categories. */
    inst->voice.note        = 57;     /* MIDI A3 */
    inst->voice.velocity    = 0.7f;
    inst->voice.freq        = 220.0f;
    inst->voice.freq_target = 220.0f;

    /* Initialize per-instance analog drift system. Seed mixes pointer address
     * + a fixed magic so each instance has its own personality (per-VCO detune,
     * drift scaling, env time tolerances) while still being deterministic
     * across reloads of the same instance. See drift.h for details. */
    drift_init(&inst->drift, (uint32_t)((uintptr_t)inst >> 4) ^ 0xA9F03Bu);

    /* Try to attach to /move-track-audio (Schwung v0.7.8+). We open the SHM
     * via its /dev/shm path with plain open() rather than shm_open() —
     * shm_open's symbol moved from librt to libc in glibc 2.34, which is
     * newer than the Move's runtime. open() has been in libc forever and
     * keeps the GLIBC requirement at 2.27. Failure is silent — track sources
     * will read as zero on hosts that don't expose this SHM. */
    inst->track_audio_fd    = -1;
    inst->track_audio       = NULL;
    inst->track_audio_bytes = 0;
    int fd = open("/dev/shm/move-track-audio", O_RDONLY);
    if (fd >= 0) {
        void *p = mmap(NULL, TRK_TOTAL_BYTES, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            close(fd);
        } else {
            inst->track_audio_fd    = fd;
            inst->track_audio       = (int16_t *)p;
            inst->track_audio_bytes = TRK_TOTAL_BYTES;
        }
    }

    return inst;
}

static void destroy_instance(void *instance) {
    synth_t *inst = (synth_t *)instance;
    if (!inst) return;
    if (inst->track_audio && inst->track_audio_bytes > 0)
        munmap(inst->track_audio, inst->track_audio_bytes);
    if (inst->track_audio_fd >= 0)
        close(inst->track_audio_fd);
    free(inst);
}

static void apply_init_preset(synth_t *inst) {
    inst->p_drive = 0.25f; inst->p_volume = 0.7f;     /* a touch of grit baseline */
    inst->p_vel_amt = 0.8f;                            /* velocity→amp depth (floor+curve protect soft hits) */
    inst->p_mg_rate = 0.3f; inst->p_mg_depth = 0.4f; inst->p_glide = 0.0f;
    inst->p_master_tune = 0.0f;
    inst->p_octave = 0; inst->p_preset = 0;
    /* Drift defaults: enough to be audibly "alive" out of the box.
     * Total wobble at these defaults is ~6-8 cents on each VCO with
     * uncorrelated slow walks creating slowly-evolving beat frequencies
     * between V1 and V2 — the hallmark vintage MS-20 character. */
    inst->p_drift = 0.55f;

    inst->v1_pitch = 0; inst->v1_fine = 0; inst->v1_wave = 1; inst->v1_pw = 0.5f;
    inst->v1_sub = 0; inst->v1_pwm_rate = 0.2f; inst->v1_pwm_depth = 0; inst->v1_drift = 0.35f;

    inst->v2_pitch = 0; inst->v2_fine = 0; inst->v2_wave = 0; inst->v2_pw = 0.5f;
    inst->v2_sync = 0; inst->v2_xmod = 0; inst->v2_detune = 0.08f; inst->v2_drift = 0.4f;

    inst->mix_v1 = 0.7f; inst->mix_v2 = 0.5f; inst->mix_sub = 0;
    inst->mix_noise = 0; inst->noise_color = 0; inst->mix_esp = 0; inst->mix_fb = 0;

    inst->lpf_cut = 0.6f; inst->lpf_reso = 0.2f; inst->hpf_cut = 0.0f; inst->hpf_reso = 0.0f;
    inst->key_track = 0.5f; inst->filter_mode = 0;   /* HP+LP cascade (MS-20 default) */
    inst->filter_rev = 0;                             /* REV.1 = KORG-35 by default */
    inst->hpf_mg_int = 0.0f; inst->hpf_eg_int = 0.0f;
    inst->lpf_mg_int = 0.12f; inst->lpf_eg_int = 0.6f; /* subtle MG wobble + EG1 auto-wah */
    inst->vco_mg_int = 0.0f; inst->vco_eg_int = 0.0f; /* VCO pitch mod attenuators closed */

    /* Vintage envelope timings — MS-20 EGs were rarely as snappy as digital.
     * The slightly-longer attack/release give the analog "breath" without
     * sounding sluggish on plucked notes. */
    inst->e1_delay = 0.0f;
    inst->e1_atk = 0.008f; inst->e1_rel = 0.55f;
    inst->e2_hold = 0.0f;
    inst->e2_atk = 0.008f; inst->e2_dcy = 0.35f; inst->e2_sus = 0.7f; inst->e2_rel = 0.45f;

    inst->mg_shape = 0.5f;     /* triangle (MS-20 "normal setting") */
    inst->mg_pw    = 0.5f;     /* square */
    inst->mg_delay = 0; inst->sh_rate = 0.3f; inst->sh_smooth = 0;
    inst->mw_mg = 0; inst->mw_filt = 0;

    inst->esp_in_gain = 1.0f; inst->esp_pitch_slew = 0.1f;
    inst->esp_env_atk = 0.005f; inst->esp_env_rel = 0.1f;
    inst->esp_gate_thr = 0.1f; inst->esp_aud_mix = 1.0f;
    inst->esp_low_cut = 0.0f; inst->esp_high_cut = 1.0f; inst->esp_cv_adjust = 0.5f;
    inst->esp_pitch_mode = 1; inst->esp_gate_pol = 0;

    /* Patchbay defaults — all jacks unpatched (each jack uses its hard-wired
     * MS-20 default source: TOTAL=MG, FREQ=EG1, HPF=EG2, LPF=EG2, etc.). */
    for (int i = 0; i < PB_NUM_JACKS; i++) inst->pb_jack[i] = 0;

    /* Reset smoothed shadows */
    inst->sm_lpf_cut = inst->lpf_cut;
    inst->sm_hpf_cut = inst->hpf_cut;
    inst->sm_lpf_reso = inst->lpf_reso;
    inst->sm_hpf_reso = inst->hpf_reso;
    inst->sm_drive    = inst->p_drive;
    inst->sm_v1_pitch = inst->v1_pitch + inst->v1_fine + inst->p_master_tune;
    inst->sm_v2_pitch = inst->v2_pitch + inst->v2_fine + inst->v2_detune + inst->p_master_tune;
    inst->sm_volume   = inst->p_volume;
    inst->sm_mg_depth = inst->p_mg_depth;
}


/* ── Factory presets (40 patches + Init) ──────────────────────────────────
 *
 * Inspired by the Korg Collection MS-20 V2 and Arturia MS-20 V factory
 * libraries. Distribution: 8 Bass, 8 Lead, 4 Pad, 3 Keys, 3 Sequence,
 * 5 SFX, 4 Drone, 5 ESP/External = 40 character presets.
 *
 * Each preset calls apply_init_preset() first then overrides only what
 * defines its character. Convention: REV.1 (KORG-35) for aggressive bass
 * and acid; REV.2 (OTA) for cleaner sync/lead/pad/ESP work.
 *
 * Patchbay jacks index map (set inst->pb_jack[i] = ...):
 *   0 KBD CV   1 T.EXT    2 VCO Freq  3 HPF Ext
 *   4 LPF Ext  5 Init Gn  6 PWM In    7 Ext Sig
 *   8 VCO 2 CV 9 EG 1 Trig 10 EG 1+2 Trig
 */

/* ════════════════ BASS (8) ════════════════════════════════════════════════ */

/* 1. Sticky Bass — the iconic MS-20 low-end. REV.1 + cranked LPF reso.
 * Vintage tuning: more drift + slight feedback for that "alive" low-end
 * the original MS-20 has on stage. */
static void apply_preset_sticky_bass(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 1;             /* Saw -1 oct */
    inst->v2_pitch = -12; inst->v2_wave = 0; inst->v2_detune = 0.06f;
    inst->mix_v1 = 0.85f; inst->mix_v2 = 0.55f; inst->mix_sub = 0.35f;
    inst->mix_fb  = 0.05f;                                /* a whisker of feedback */
    inst->lpf_cut = 0.32f; inst->lpf_reso = 0.55f; inst->lpf_eg_int = 0.85f;
    inst->p_drive = 0.45f;
    inst->p_drift = 0.55f;                                /* extra wobble */
    inst->filter_rev = 0; inst->filter_mode = 1;          /* REV.1 LP-only */
    inst->e1_atk = 0.006f; inst->e1_rel = 0.18f;
    inst->e2_atk = 0.006f; inst->e2_dcy = 0.35f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.22f;          /* punchy */
}

/* 2. Acid Squelch — Roland-style acid via MS-20 character.
 * Vintage tuning: a touch longer attack (3ms vs 1ms) for natural transient,
 * and slightly higher MG depth on the filter for that breathing squelch. */
static void apply_preset_acid_squelch(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 1;
    inst->mix_v1 = 0.85f; inst->mix_v2 = 0;
    inst->lpf_cut = 0.4f; inst->lpf_reso = 0.78f; inst->lpf_eg_int = 0.95f;
    inst->lpf_mg_int = 0.08f;                             /* subtle squelch breath */
    inst->p_drive = 0.55f;
    inst->p_glide = 0.06f;
    inst->p_drift = 0.5f;
    inst->p_mg_rate = 0.4f;                               /* 4-5 Hz wobble */
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e1_atk = 0.003f; inst->e1_rel = 0.08f;
    inst->e2_atk = 0.003f; inst->e2_dcy = 0.18f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.15f;
}

/* 3. Sub Wobbler — pure sub with slow MG modulation on cutoff. */
static void apply_preset_sub_wobbler(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -24; inst->v1_wave = 0;              /* Tri 2 oct down */
    inst->mix_v1 = 0.7f; inst->mix_v2 = 0; inst->mix_sub = 0.6f;
    inst->lpf_cut = 0.45f; inst->lpf_reso = 0.4f; inst->lpf_eg_int = 0.3f;
    inst->lpf_mg_int = 0.5f;                              /* MG → LPF */
    inst->p_drive = 0.3f;
    inst->p_mg_rate = 0.15f; inst->p_mg_depth = 0.7f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.4f;
    inst->e2_sus = 0.6f;   inst->e2_rel = 0.3f;
}

/* 4. FM Bass — VCO 1×VCO 2 cross-mod, gnarly low-mid character. */
static void apply_preset_fm_bass(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 0;              /* Tri carrier */
    inst->v2_pitch = -12; inst->v2_fine = 7.0f; inst->v2_wave = 0;
    inst->v2_xmod = 1.0f;                                  /* FM ON */
    inst->mix_v1 = 0.7f; inst->mix_v2 = 0.4f;
    inst->lpf_cut = 0.5f; inst->lpf_reso = 0.5f; inst->lpf_eg_int = 0.7f;
    inst->p_drive = 0.4f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.4f;
    inst->e2_sus = 0.2f;   inst->e2_rel = 0.25f;
}

/* 5. Reso Bass — self-oscillating LPF with KEY TRACK following. */
static void apply_preset_reso_bass(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 2;              /* PW square */
    inst->v1_pw = 0.3f;
    inst->mix_v1 = 0.6f; inst->mix_sub = 0.3f;
    inst->lpf_cut = 0.3f; inst->lpf_reso = 0.92f;         /* near-self-osc */
    inst->lpf_eg_int = 0.8f;
    inst->key_track = 0.85f;
    inst->p_drive = 0.4f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.25f;
    inst->e2_sus = 0.1f;   inst->e2_rel = 0.2f;
}

/* 6. Pluck Bass — short envelope, fingerpicked-style. */
static void apply_preset_pluck_bass(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 1;
    inst->v2_pitch = -12; inst->v2_wave = 0; inst->v2_detune = 0.06f;
    inst->mix_v1 = 0.7f; inst->mix_v2 = 0.5f; inst->mix_sub = 0.25f;
    inst->lpf_cut = 0.55f; inst->lpf_reso = 0.4f; inst->lpf_eg_int = 0.7f;
    inst->p_drive = 0.25f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.15f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.12f;
}

/* 7. Detuned Doom — heavy detune for thick low-end. */
static void apply_preset_detuned_doom(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 1;
    inst->v2_pitch = -12; inst->v2_fine = -7.0f;
    inst->v2_wave = 0; inst->v2_detune = 0.18f;
    inst->mix_v1 = 0.7f; inst->mix_v2 = 0.7f; inst->mix_sub = 0.45f;
    inst->lpf_cut = 0.35f; inst->lpf_reso = 0.45f; inst->lpf_eg_int = 0.55f;
    inst->p_drive = 0.55f;
    inst->p_drift = 0.55f;                                /* extra wander */
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.5f;
    inst->e2_sus = 0.5f;   inst->e2_rel = 0.4f;
}

/* 8. Picked Bass — bright pluck with HPF removing low-end mud. */
static void apply_preset_picked_bass(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 1;
    inst->mix_v1 = 0.8f; inst->mix_sub = 0.2f;
    inst->hpf_cut = 0.18f;
    inst->lpf_cut = 0.6f; inst->lpf_reso = 0.3f; inst->lpf_eg_int = 0.6f;
    inst->p_drive = 0.3f;
    inst->filter_rev = 0; inst->filter_mode = 0;          /* HP+LP */
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.2f;
    inst->e2_sus = 0.15f;  inst->e2_rel = 0.18f;
}

/* ════════════════ LEAD (8) ════════════════════════════════════════════════ */

/* 9. Lead Scream — max LPF reso, the screaming MS-20 lead signature.
 * Vintage tuning: drop glide from 0.15s (synth-pop style) to 0.05s for
 * tighter monosynth feel, faster filter envelope for that "snap" the
 * KORG-35 makes when self-osc kicks in. */
static void apply_preset_lead_scream(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 0; inst->v2_detune = 0.12f;
    inst->mix_v1 = 0.7f; inst->mix_v2 = 0.7f;
    inst->lpf_cut = 0.7f; inst->lpf_reso = 0.85f; inst->lpf_eg_int = 0.55f;
    inst->p_drive = 0.6f;
    inst->p_glide = 0.05f;                                /* tight mono glide */
    inst->p_drift = 0.55f;                                /* alive feel */
    inst->filter_rev = 0; inst->filter_mode = 1;          /* REV.1 fuzz break-up */
    inst->e1_atk = 0.005f; inst->e1_rel = 0.55f;
    inst->e2_atk = 0.006f; inst->e2_dcy = 1.5f;
    inst->e2_sus = 0.6f;   inst->e2_rel = 0.45f;
}

/* 10. Sync Howl — V2 hard-synced + EG1 sweeping V2 freq. */
static void apply_preset_sync_howl(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1;
    inst->v2_wave = 0; inst->v2_fine = 7.0f; inst->v2_sync = 1;
    inst->mix_v1 = 0.3f; inst->mix_v2 = 0.85f;
    inst->lpf_cut = 0.65f; inst->lpf_reso = 0.55f; inst->lpf_eg_int = 0.65f;
    inst->vco_eg_int = 0.4f;                              /* EG1 → V2 freq */
    inst->p_drive = 0.35f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e1_atk = 0.5f;  inst->e1_rel = 0.8f;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.4f;
    inst->e2_sus = 0.7f;   inst->e2_rel = 0.3f;
}

/* 11. Whistle Lead — high cutoff + reso for flute/whistle texture. */
static void apply_preset_whistle_lead(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 0; inst->v2_wave = 0; inst->v2_detune = 0.04f;
    inst->mix_v1 = 0.6f; inst->mix_v2 = 0.4f;
    inst->lpf_cut = 0.85f; inst->lpf_reso = 0.92f;
    inst->lpf_eg_int = 0.2f;
    inst->p_drive = 0.15f;
    inst->p_mg_rate = 0.55f; inst->p_mg_depth = 0.15f;
    inst->vco_mg_int = 0.15f;                             /* slight vibrato */
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.05f; inst->e2_dcy = 0.4f;
    inst->e2_sus = 0.85f; inst->e2_rel = 0.3f;
}

/* 12. Filter Cry — slow LPF sweep, expressive with mod wheel. */
static void apply_preset_filter_cry(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 0; inst->v2_detune = 0.08f;
    inst->mix_v1 = 0.65f; inst->mix_v2 = 0.55f;
    inst->lpf_cut = 0.5f; inst->lpf_reso = 0.7f; inst->lpf_eg_int = 0.6f;
    inst->mw_filt = 0.7f;                                 /* wheel sweeps filter */
    inst->p_drive = 0.4f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.3f; inst->e2_dcy = 0.8f;
    inst->e2_sus = 0.7f; inst->e2_rel = 0.6f;
}

/* 13. FM Lead — V1 FMs V2 for metallic, bell-edged lead. */
static void apply_preset_fm_lead(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 0;                                    /* Tri carrier */
    inst->v2_wave = 0; inst->v2_fine = 12.0f;
    inst->v2_xmod = 1.0f;                                 /* FM ON */
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.6f;
    inst->lpf_cut = 0.7f; inst->lpf_reso = 0.55f; inst->lpf_eg_int = 0.4f;
    inst->p_drive = 0.35f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.6f;
    inst->e2_sus = 0.55f;  inst->e2_rel = 0.4f;
}

/* 14. Hard Sync 5th — V2 a 5th up, hard-synced, fast envelope. */
static void apply_preset_hard_sync_5th(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1;
    inst->v2_wave = 0; inst->v2_fine = 7.0f; inst->v2_sync = 1;
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.7f;
    inst->lpf_cut = 0.75f; inst->lpf_reso = 0.4f;
    inst->lpf_eg_int = 0.55f;
    inst->p_drive = 0.4f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.3f;
    inst->e2_sus = 0.5f;   inst->e2_rel = 0.25f;
}

/* 15. Resonant Spike — single-cycle pluck through self-oscillating filter. */
static void apply_preset_resonant_spike(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 2;                                    /* PW pulse */
    inst->v1_pw = 0.2f;
    inst->mix_v1 = 0.6f;
    inst->lpf_cut = 0.45f; inst->lpf_reso = 0.95f;        /* self-osc */
    inst->lpf_eg_int = 0.9f;
    inst->key_track = 0.7f;
    inst->p_drive = 0.3f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.12f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.1f;
}

/* 16. Saw Stack — three-saw sound through detune + slow LFO on cutoff. */
static void apply_preset_saw_stack(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1;
    inst->v2_wave = 0; inst->v2_detune = 0.15f;
    inst->mix_v1 = 0.7f; inst->mix_v2 = 0.7f; inst->mix_sub = 0.2f;
    inst->lpf_cut = 0.7f; inst->lpf_reso = 0.4f; inst->lpf_eg_int = 0.35f;
    inst->p_drive = 0.45f;
    inst->p_mg_rate = 0.2f; inst->p_mg_depth = 0.2f;
    inst->lpf_mg_int = 0.25f;
    inst->p_glide = 0.05f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.05f; inst->e2_dcy = 0.5f;
    inst->e2_sus = 0.7f;  inst->e2_rel = 0.4f;
}

/* ════════════════ PAD (4) ═════════════════════════════════════════════════ */

/* 17. Analog Pad — slow attack, slight detune, REV.2 smoothness. */
static void apply_preset_analog_pad(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 0; inst->v2_detune = 0.1f;
    inst->mix_v1 = 0.55f; inst->mix_v2 = 0.55f; inst->mix_sub = 0.15f;
    inst->lpf_cut = 0.55f; inst->lpf_reso = 0.2f; inst->lpf_eg_int = 0.3f;
    inst->p_drive = 0.15f;
    inst->p_drift = 0.45f;
    inst->filter_rev = 1; inst->filter_mode = 0;          /* HP+LP body */
    inst->hpf_cut = 0.1f;
    inst->e2_atk = 1.2f; inst->e2_dcy = 0.8f;
    inst->e2_sus = 0.85f; inst->e2_rel = 1.5f;
}

/* 18. Sweep Pad — MG modulating LPF for slow filter sweeps. */
static void apply_preset_sweep_pad(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 0; inst->v2_detune = 0.08f;
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.5f;
    inst->lpf_cut = 0.5f; inst->lpf_reso = 0.45f; inst->lpf_eg_int = 0.2f;
    inst->lpf_mg_int = 0.6f;                              /* big MG sweep */
    inst->p_drive = 0.2f;
    inst->p_mg_rate = 0.08f; inst->p_mg_depth = 0.7f;
    inst->mg_shape = 0.5f;                                /* triangle */
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->e2_atk = 1.5f; inst->e2_dcy = 1.0f;
    inst->e2_sus = 0.8f; inst->e2_rel = 2.0f;
}

/* 19. Ghost Choir — formant-like with HPF + slow chorus from drift. */
static void apply_preset_ghost_choir(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 0;
    inst->v2_fine = 7.0f; inst->v2_detune = 0.08f;        /* 5th + chorus */
    inst->mix_v1 = 0.55f; inst->mix_v2 = 0.55f;
    inst->lpf_cut = 0.65f; inst->lpf_reso = 0.55f; inst->lpf_eg_int = 0.15f;
    inst->hpf_cut = 0.25f;
    inst->p_drive = 0.15f;
    inst->p_drift = 0.55f;
    inst->v1_drift = 0.7f; inst->v2_drift = 0.7f;
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->e2_atk = 2.0f; inst->e2_dcy = 1.5f;
    inst->e2_sus = 0.9f; inst->e2_rel = 2.5f;
}

/* 20. Slow Wash — very slow attack, deep release, mod wheel adds shimmer. */
static void apply_preset_slow_wash(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 0; inst->v2_wave = 0; inst->v2_detune = 0.13f;
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.5f; inst->mix_sub = 0.1f;
    inst->lpf_cut = 0.45f; inst->lpf_reso = 0.3f; inst->lpf_eg_int = 0.15f;
    inst->p_drive = 0.1f;
    inst->mw_filt = 0.5f; inst->mw_mg = 0.4f;
    inst->p_mg_rate = 0.06f; inst->p_mg_depth = 0.25f;
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->e2_atk = 3.5f; inst->e2_dcy = 2.0f;
    inst->e2_sus = 1.0f; inst->e2_rel = 4.0f;
}

/* ════════════════ KEYS (3) ════════════════════════════════════════════════ */

/* 21. Vintage EP — Wurli/Rhodes-ish using FM + short envelope. */
static void apply_preset_vintage_ep(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 0;                                    /* Tri */
    inst->v2_wave = 0; inst->v2_fine = 12.0f;
    inst->v2_xmod = 1.0f;
    inst->mix_v1 = 0.7f; inst->mix_v2 = 0.4f;
    inst->lpf_cut = 0.65f; inst->lpf_reso = 0.25f; inst->lpf_eg_int = 0.6f;
    inst->p_drive = 0.25f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 1.2f;
    inst->e2_sus = 0.15f;  inst->e2_rel = 0.4f;
}

/* 22. Glass Bell — high cutoff, very short, ring-mod-flavored. */
static void apply_preset_glass_bell(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 0; inst->v2_wave = 3;                 /* V2 = Ring */
    inst->v2_fine = 19.0f;                                /* harmonic gap */
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.55f;
    inst->lpf_cut = 0.85f; inst->lpf_reso = 0.4f; inst->lpf_eg_int = 0.4f;
    inst->p_drive = 0.2f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 1.5f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 1.0f;
}

/* 23. Plucked Clav — bright fast pluck, HPF cuts sub mud. */
static void apply_preset_plucked_clav(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 2; inst->v1_pw = 0.25f;               /* narrow pulse */
    inst->mix_v1 = 0.75f;
    inst->hpf_cut = 0.3f;
    inst->lpf_cut = 0.7f; inst->lpf_reso = 0.5f; inst->lpf_eg_int = 0.55f;
    inst->p_drive = 0.3f;
    inst->filter_rev = 0; inst->filter_mode = 0;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.18f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.15f;
}

/* ════════════════ SEQUENCE (3) ════════════════════════════════════════════ */

/* 24. Acid Seq — short envelope + S&H modulating cutoff. */
static void apply_preset_acid_seq(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 1;
    inst->mix_v1 = 0.85f; inst->mix_sub = 0.2f;
    inst->lpf_cut = 0.45f; inst->lpf_reso = 0.75f; inst->lpf_eg_int = 0.85f;
    inst->p_drive = 0.5f;
    inst->p_glide = 0.04f;
    inst->pb_jack[4] = 4;                                 /* LPF Ext = S&H */
    inst->sh_rate = 0.55f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.15f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.1f;
}

/* 25. Techno Pulse — driving square pulse, MG → LPF. */
static void apply_preset_techno_pulse(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_pitch = -12; inst->v1_wave = 2; inst->v1_pw = 0.5f;
    inst->mix_v1 = 0.8f; inst->mix_sub = 0.25f;
    inst->lpf_cut = 0.5f; inst->lpf_reso = 0.5f; inst->lpf_eg_int = 0.4f;
    inst->lpf_mg_int = 0.45f;
    inst->p_drive = 0.4f;
    inst->p_mg_rate = 0.45f; inst->p_mg_depth = 0.55f;
    inst->mg_shape = 0.0f;                                /* positive saw */
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.12f;
    inst->e2_sus = 0.1f;   inst->e2_rel = 0.08f;
}

/* 26. Wobbly Bleep — bouncy short notes with PWM. */
static void apply_preset_wobbly_bleep(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 2; inst->v1_pw = 0.3f;
    inst->v1_pwm_depth = 0.7f;                            /* MG → V1 PW */
    inst->mix_v1 = 0.75f;
    inst->lpf_cut = 0.7f; inst->lpf_reso = 0.4f; inst->lpf_eg_int = 0.5f;
    inst->p_drive = 0.3f;
    inst->p_mg_rate = 0.4f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.15f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.1f;
}

/* ════════════════ SFX (5) ═════════════════════════════════════════════════ */

/* 27. Laser Zap — fast pitch sweep via EG1 → VCO Master EG1/EXT. */
static void apply_preset_laser_zap(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1;
    inst->mix_v1 = 0.7f;
    inst->lpf_cut = 0.7f; inst->lpf_reso = 0.6f; inst->lpf_eg_int = 0.4f;
    inst->vco_eg_int = 0.95f;                             /* huge pitch sweep */
    inst->p_drive = 0.4f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e1_atk = 0.001f; inst->e1_rel = 0.15f;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.2f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.1f;
}

/* 28. Computer Bleep — sync + ring + short envelope. */
static void apply_preset_computer_bleep(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 2; inst->v1_pw = 0.5f;
    inst->v2_wave = 3; inst->v2_fine = 14.0f;             /* Ring + 14 semis */
    inst->v2_sync = 1;
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.65f;
    inst->lpf_cut = 0.85f; inst->lpf_reso = 0.4f;
    inst->p_drive = 0.2f;
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.05f;
    inst->e2_sus = 0.0f;   inst->e2_rel = 0.04f;
}

/* 29. Helicopter — noise + LFO chopping VCA via patchbay. */
static void apply_preset_helicopter(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 3;                                    /* Noise */
    inst->mix_v1 = 0.7f; inst->mix_noise = 0.4f;
    inst->lpf_cut = 0.4f; inst->lpf_reso = 0.5f;
    inst->p_drive = 0.4f;
    inst->p_mg_rate = 0.55f;                              /* fast chop */
    inst->mg_pw = 0.8f;                                   /* narrow pulse */
    inst->pb_jack[5] = 1;                                 /* Init Gain = Wheel */
    inst->lpf_mg_int = 0.5f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.05f; inst->e2_dcy = 1.0f;
    inst->e2_sus = 0.8f; inst->e2_rel = 0.5f;
}

/* 30. Stutter Glitch — S&H driving envelopes for retrig artifacts. */
static void apply_preset_stutter_glitch(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 2; inst->v2_detune = 0.05f;
    inst->mix_v1 = 0.6f; inst->mix_v2 = 0.5f;
    inst->lpf_cut = 0.55f; inst->lpf_reso = 0.7f; inst->lpf_eg_int = 0.6f;
    inst->p_drive = 0.5f;
    inst->p_mg_rate = 0.6f;
    inst->sh_rate = 0.7f;
    inst->pb_jack[10] = 1;                                /* EG 1+2 Trig = S&H */
    inst->pb_jack[2] = 4;                                 /* VCO Freq = S&H */
    inst->vco_eg_int = 0.4f;
    inst->filter_rev = 0; inst->filter_mode = 1;
    inst->e2_atk = 0.001f; inst->e2_dcy = 0.2f;
    inst->e2_sus = 0.4f;   inst->e2_rel = 0.15f;
}

/* 31. Random Patch — wide-open MG, multiple jacks routed weird. */
static void apply_preset_random_patch(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 2; inst->v2_detune = 0.2f;
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.5f; inst->mix_noise = 0.15f;
    inst->lpf_cut = 0.55f; inst->lpf_reso = 0.65f; inst->lpf_eg_int = 0.5f;
    inst->p_drive = 0.45f;
    inst->p_mg_rate = 0.3f; inst->p_mg_depth = 0.6f;
    inst->mg_shape = 1.0f;                                /* reverse saw */
    inst->vco_mg_int = 0.45f;                             /* MG → VCO pitch */
    inst->lpf_mg_int = 0.5f;
    inst->pb_jack[3] = 5;                                 /* HPF Ext = Noise */
    inst->pb_jack[6] = 5;                                 /* PWM In = S&H */
    inst->p_drift = 0.5f;
    inst->filter_rev = 0; inst->filter_mode = 0;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.6f;
    inst->e2_sus = 0.5f;   inst->e2_rel = 0.4f;
}

/* ════════════════ DRONE (4) ═══════════════════════════════════════════════ */

/* 32. Endless Hold — EG2 hold @ 20s for self-sustaining drone. */
static void apply_preset_endless_hold(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 0; inst->v1_fine = 0.05f;
    inst->v2_wave = 0; inst->v2_detune = -0.13f;
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.5f; inst->mix_noise = 0.05f;
    inst->lpf_cut = 0.5f; inst->lpf_reso = 0.3f;
    inst->key_track = 0.0f;
    inst->p_drive = 0.2f;
    inst->p_drift = 0.55f;
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->hpf_cut = 0.12f;
    inst->e2_hold = 20.0f;                                /* MS-20 hold magic */
    inst->e2_atk = 1.0f;  inst->e2_dcy = 0.5f;
    inst->e2_sus = 1.0f;  inst->e2_rel = 5.0f;
}

/* 33. Slow Evolution — slow MG modulating multiple destinations. */
static void apply_preset_slow_evolution(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 0; inst->v2_detune = 0.16f;
    inst->mix_v1 = 0.5f; inst->mix_v2 = 0.5f;
    inst->lpf_cut = 0.4f; inst->lpf_reso = 0.4f;
    inst->lpf_mg_int = 0.5f;
    inst->vco_mg_int = 0.15f;
    inst->p_mg_rate = 0.04f; inst->p_mg_depth = 0.55f;    /* very slow */
    inst->mg_shape = 0.5f;                                /* triangle */
    inst->p_drift = 0.6f;
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->e2_hold = 8.0f;
    inst->e2_atk = 3.0f; inst->e2_dcy = 2.0f;
    inst->e2_sus = 0.85f; inst->e2_rel = 4.0f;
}

/* 34. Tape Hiss Drone — noise + filtered for textural underlayer. */
static void apply_preset_tape_hiss_drone(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 3;                                    /* V1 = Noise */
    inst->mix_v1 = 0.4f; inst->mix_noise = 0.6f;
    inst->noise_color = 1;                                /* Pink */
    inst->lpf_cut = 0.5f; inst->lpf_reso = 0.55f;
    inst->lpf_mg_int = 0.4f;
    inst->hpf_cut = 0.2f;
    inst->p_drive = 0.25f;
    inst->p_mg_rate = 0.06f; inst->p_mg_depth = 0.5f;
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->e2_hold = 15.0f;
    inst->e2_atk = 2.5f; inst->e2_dcy = 1.0f;
    inst->e2_sus = 0.8f; inst->e2_rel = 5.0f;
}

/* 35. Modular Hum — saw + LFO drift, no key tracking, slow. */
static void apply_preset_modular_hum(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v1_fine = 0.0f;
    inst->v2_wave = 0; inst->v2_fine = -0.07f; inst->v2_detune = -0.21f;
    inst->mix_v1 = 0.55f; inst->mix_v2 = 0.55f;
    inst->lpf_cut = 0.45f; inst->lpf_reso = 0.45f;
    inst->key_track = 0.0f;
    inst->p_drive = 0.3f;
    inst->p_drift = 0.7f;
    inst->v1_drift = 0.85f; inst->v2_drift = 0.85f;
    inst->p_mg_rate = 0.07f; inst->p_mg_depth = 0.3f;
    inst->lpf_mg_int = 0.3f;
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->hpf_cut = 0.1f;
    inst->e2_hold = 18.0f;
    inst->e2_atk = 1.5f; inst->e2_dcy = 0.8f;
    inst->e2_sus = 0.95f; inst->e2_rel = 4.0f;
}

/* ════════════════ ESP / EXTERNAL (5) ══════════════════════════════════════ */

/* 36. Vocal Harmonizer — KBD CV ← ESP CV, V2 5th up for harmony. */
static void apply_preset_vocal_harmonizer(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1;
    inst->v2_wave = 0; inst->v2_fine = 7.0f;              /* 5th harmony */
    inst->mix_v1 = 0.6f; inst->mix_v2 = 0.6f; inst->mix_esp = 0.4f;
    inst->lpf_cut = 0.7f; inst->lpf_reso = 0.2f; inst->lpf_eg_int = 0.4f;
    inst->hpf_cut = 0.2f;
    inst->p_drive = 0.15f;
    inst->esp_in_gain = 1.2f; inst->esp_pitch_mode = 1;
    inst->esp_pitch_slew = 0.2f;
    inst->pb_jack[0] = 1;                                 /* KBD CV = ESP CV */
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->e2_atk = 0.02f; inst->e2_dcy = 0.25f;
    inst->e2_sus = 0.85f; inst->e2_rel = 0.3f;
}

/* 37. Talk Box — ESP audio through filter for vocal-formant texture. */
static void apply_preset_talk_box(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1; inst->v2_wave = 0; inst->v2_detune = 0.05f;
    inst->mix_v1 = 0.55f; inst->mix_v2 = 0.55f;
    inst->lpf_cut = 0.45f; inst->lpf_reso = 0.7f; inst->lpf_eg_int = 0.4f;
    inst->p_drive = 0.35f;
    inst->pb_jack[7] = 1;                                 /* Ext Sig = ESP Audio */
    inst->pb_jack[4] = 2;                                 /* LPF Ext = ESP Env */
    inst->mix_esp = 0.0f;                                 /* not direct in mixer */
    inst->esp_in_gain = 1.5f;
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->hpf_cut = 0.25f;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.5f;
    inst->e2_sus = 0.8f;   inst->e2_rel = 0.4f;
}

/* 38. Audio Tracker — ESP CV → V2 pitch for pitch-following synth. */
static void apply_preset_audio_tracker(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 0;
    inst->v2_wave = 0;
    inst->mix_v1 = 0.45f; inst->mix_v2 = 0.7f;
    inst->lpf_cut = 0.65f; inst->lpf_reso = 0.45f; inst->lpf_eg_int = 0.35f;
    inst->p_drive = 0.25f;
    inst->esp_in_gain = 1.3f; inst->esp_pitch_mode = 1;
    inst->esp_pitch_slew = 0.15f;
    inst->pb_jack[8] = 1;                                 /* VCO 2 CV = ESP CV */
    inst->pb_jack[5] = 2;                                 /* Init Gain = ESP Env */
    inst->filter_rev = 1; inst->filter_mode = 1;
    inst->e2_atk = 0.05f; inst->e2_dcy = 0.6f;
    inst->e2_sus = 0.8f;  inst->e2_rel = 0.4f;
}

/* 39. Feedback Howl — output → input feedback at high reso. */
static void apply_preset_feedback_howl(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 1;
    inst->mix_v1 = 0.45f; inst->mix_fb = 0.55f;           /* feedback enabled */
    inst->lpf_cut = 0.5f; inst->lpf_reso = 0.85f;
    inst->p_drive = 0.55f;
    inst->pb_jack[7] = 3;                                 /* Ext Sig = Mixer Out (extra fb) */
    inst->lpf_eg_int = 0.45f;
    inst->filter_rev = 0; inst->filter_mode = 0;
    inst->hpf_cut = 0.35f;
    inst->e2_atk = 0.02f; inst->e2_dcy = 0.6f;
    inst->e2_sus = 0.6f;  inst->e2_rel = 0.5f;
}

/* 40. ESP Mangler — ESP audio in mixer + MG modulating LPF + REV.2. */
static void apply_preset_esp_mangler(synth_t *inst) {
    apply_init_preset(inst);
    inst->v1_wave = 2; inst->v1_pw = 0.3f;                /* PW square */
    inst->v2_wave = 2; inst->v2_pw = 0.3f; inst->v2_detune = 0.04f;
    inst->mix_v1 = 0.4f; inst->mix_v2 = 0.4f; inst->mix_esp = 0.7f;
    inst->lpf_cut = 0.55f; inst->lpf_reso = 0.5f; inst->lpf_eg_int = 0.3f;
    inst->lpf_mg_int = 0.55f;
    inst->p_drive = 0.5f;
    inst->p_mg_rate = 0.45f; inst->p_mg_depth = 0.6f;
    inst->mg_shape = 0.0f;                                /* positive saw */
    inst->esp_in_gain = 1.5f; inst->esp_pitch_mode = 1;
    inst->pb_jack[7] = 1;                                 /* Ext Sig = ESP Audio */
    inst->filter_rev = 1; inst->filter_mode = 0;
    inst->e2_atk = 0.005f; inst->e2_dcy = 0.5f;
    inst->e2_sus = 0.6f;   inst->e2_rel = 0.4f;
}

/* ── Preset dispatch ──────────────────────────────────────────────────────── */

static void apply_preset(synth_t *inst, int idx) {
    switch (idx) {
        /* Bass */
        case  1: apply_preset_sticky_bass(inst);      break;
        case  2: apply_preset_acid_squelch(inst);     break;
        case  3: apply_preset_sub_wobbler(inst);      break;
        case  4: apply_preset_fm_bass(inst);          break;
        case  5: apply_preset_reso_bass(inst);        break;
        case  6: apply_preset_pluck_bass(inst);       break;
        case  7: apply_preset_detuned_doom(inst);     break;
        case  8: apply_preset_picked_bass(inst);      break;
        /* Lead */
        case  9: apply_preset_lead_scream(inst);      break;
        case 10: apply_preset_sync_howl(inst);        break;
        case 11: apply_preset_whistle_lead(inst);     break;
        case 12: apply_preset_filter_cry(inst);       break;
        case 13: apply_preset_fm_lead(inst);          break;
        case 14: apply_preset_hard_sync_5th(inst);    break;
        case 15: apply_preset_resonant_spike(inst);   break;
        case 16: apply_preset_saw_stack(inst);        break;
        /* Pad */
        case 17: apply_preset_analog_pad(inst);       break;
        case 18: apply_preset_sweep_pad(inst);        break;
        case 19: apply_preset_ghost_choir(inst);      break;
        case 20: apply_preset_slow_wash(inst);        break;
        /* Keys */
        case 21: apply_preset_vintage_ep(inst);       break;
        case 22: apply_preset_glass_bell(inst);       break;
        case 23: apply_preset_plucked_clav(inst);     break;
        /* Sequence */
        case 24: apply_preset_acid_seq(inst);         break;
        case 25: apply_preset_techno_pulse(inst);     break;
        case 26: apply_preset_wobbly_bleep(inst);     break;
        /* SFX */
        case 27: apply_preset_laser_zap(inst);        break;
        case 28: apply_preset_computer_bleep(inst);   break;
        case 29: apply_preset_helicopter(inst);       break;
        case 30: apply_preset_stutter_glitch(inst);   break;
        case 31: apply_preset_random_patch(inst);     break;
        /* Drone */
        case 32: apply_preset_endless_hold(inst);     break;
        case 33: apply_preset_slow_evolution(inst);   break;
        case 34: apply_preset_tape_hiss_drone(inst);  break;
        case 35: apply_preset_modular_hum(inst);      break;
        /* ESP / External */
        case 36: apply_preset_vocal_harmonizer(inst); break;
        case 37: apply_preset_talk_box(inst);         break;
        case 38: apply_preset_audio_tracker(inst);    break;
        case 39: apply_preset_feedback_howl(inst);    break;
        case 40: apply_preset_esp_mangler(inst);      break;
        default: apply_init_preset(inst);             break;
    }
}
/* ── MIDI handler ───────────────────────────────────────────────────────── */

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    synth_t *inst = (synth_t *)instance;
    if (len < 2) return;
    uint8_t status = msg[0] & 0xF0;
    uint8_t d1 = msg[1];
    uint8_t d2 = (len >= 3) ? msg[2] : 0;

    if (status == 0x90 && d2 > 0) {
        /* Note On */
        voice_t *v = &inst->voice;
        int was_active_audible = v->active && v->e2 > 0.001f;
        v->note     = d1;
        v->velocity = d2 / 127.0f;
        v->freq_target = note_to_freq(d1 + inst->p_octave * 12);
        if (inst->p_glide < 0.001f || !v->active) v->freq = v->freq_target;
        v->active   = 1;
        if (was_active_audible) {
            /* Legato retrigger: previous note still ringing audibly. Skip
             * Delay/Hold (those are for the initial attack shape, not
             * mid-phrase). Restart Attack from the CURRENT envelope level —
             * the exponential approach to 1.0 ramps smoothly from wherever
             * we are. Zeroing e1/e2 here was the cause of the audible click
             * on every retrigger. */
            v->e1_stage = 1;
            v->e2_stage = 1;
        } else {
            /* Fresh trigger: full envelope from zero, including Delay/Hold. */
            v->e1_stage = 0;
            v->e2_stage = 0;
            v->e1       = 0.0f;
            v->e2       = 0.0f;
        }
        v->e1_timer = 0.0f;
        v->e2_timer = 0.0f;
        /* Re-roll fast drift state — gives each note a slightly different
         * starting phase on the flutter LFOs (analog "no two notes alike"). */
        drift_note_on(&inst->drift);
    } else if (status == 0x80 || (status == 0x90 && d2 == 0)) {
        /* Note Off — only release if it's the held note */
        voice_t *v = &inst->voice;
        if (v->note == d1) {
            v->e1_stage = 4;        /* Release stage in MS-faithful state machine */
            v->e2_stage = 4;
        }
    } else if (status == 0xE0) {
        int bend = (d2 << 7) | d1;
        /* MS-20's spec was ±1 octave for the wheel — match it. The previous
         * ±2 semitones felt cramped for the kind of vocal/synth slides this
         * synth is built for. Users who want a tighter bend can use the
         * controller's own range setting. */
        inst->pitch_bend = (bend - 8192) / 8192.0f * 12.0f;
    } else if (status == 0xB0) {
        if (d1 == 1) inst->mod_wheel = d2 / 127.0f;
    } else if (status == 0xD0) {
        /* Channel pressure */
        inst->voice.aftertouch = d1 / 127.0f;
    } else if (status == 0xA0) {
        /* Poly aftertouch */
        if (d2 > 0 && inst->voice.note == d1)
            inst->voice.aftertouch = d2 / 127.0f;
    }
}

/* ── Randomization ──────────────────────────────────────────────────────── */

/* rnd_mod — randomize ONLY the patchbay (sources + depths).
 * Used by both the standalone "Random Mod" trigger AND as the patchbay portion
 * of the full "Random Patch" trigger. Weighted distribution favors subtle
 * routings (Denis pattern): 35% no patch, then a roll for magnitude with most
 * mass below 0.25. */
/* ── Performance triggers: random patch + mutate ──────────────────────────── */

/* rnd_full — generate a fresh random patch. Called by the Random Patch trigger.
 *
 * MUST always be audible. Silent-failure modes we explicitly guard against:
 *   1. e2_hold up to 2s → user hears 2 seconds of silence on note-on.
 *      Cap to 0.05s (a trace, no audible wait).
 *   2. KBD CV jack randomized to None/ESP/Wheel/S&H → VCOs lose keyboard
 *      pitch CV → silent or stuck at last freq. Always force jack 0 to Normal.
 *   3. lpf_eg_int bipolar negative + high envelope → cutoff pulled below 0
 *      → LPF kills everything. Bias the random to mostly-positive so the
 *      filter OPENS rather than closes on note-on.
 *   4. filter_mode left in HP-only or Notch from a previous patch → kills
 *      bass content. Always force a passing mode (HP+LP or LP only).
 *   5. e1_delay up to 500ms → filter envelope starts too late, sound is dull
 *      until the EG opens. Cap to 50ms.
 *   6. VCA Initial Gain jack randomized to a dead source while EG2 hasn't
 *      retriggered → silence. Force this jack to None (uses EG2 normally).
 *   7. Ext Sig jack to Tracks 1-4 on a firmware that doesn't expose them →
 *      effectively silent if user set mix_v1=0. Stay conservative on this jack.
 *
 * Within those constraints we keep oscillators within ±1 octave, capped drive,
 * patchbay can re-route most jacks. */
static void rnd_full(synth_t *inst) {
    /* Oscillators */
    inst->v1_pitch = ((int)(rand_f(&inst->rng) * 4.0f) - 2) * 12;  /* 32'/16'/8'/4' */
    inst->v2_pitch = ((int)(rand_f(&inst->rng) * 4.0f) - 1) * 12;  /* 16'/8'/4'/2' */
    inst->v1_wave  = rand32(&inst->rng) & 3;
    inst->v2_wave  = rand32(&inst->rng) & 3;
    /* Prevent VCO1=Noise + VCO2=Ring + low mix → all silent (Ring needs an
     * oscillator partner with content). Force at least one to a real osc. */
    if (inst->v1_wave == 3 && inst->v2_wave == 3) {
        inst->v1_wave = 1;  /* Saw — guaranteed content for the ring partner */
    }
    inst->v1_pw    = 0.3f + rand_f(&inst->rng) * 0.4f;
    inst->v2_pw    = 0.3f + rand_f(&inst->rng) * 0.4f;
    inst->v2_detune = rand_b(&inst->rng) * 0.3f;
    inst->v2_sync   = (rand_f(&inst->rng) < 0.2f) ? 1 : 0;
    inst->v2_xmod   = (rand_f(&inst->rng) < 0.15f) ? 1 : 0;

    /* Mixer — always at least 0.5 VCO1 so there's a guaranteed audio source.
     * Noise wave on VCO1 is fine here; "noise but audible" is a valid patch. */
    inst->mix_v1   = 0.5f + rand_f(&inst->rng) * 0.3f;
    inst->mix_v2   = 0.2f + rand_f(&inst->rng) * 0.5f;
    inst->mix_sub  = rand_f(&inst->rng) * 0.3f;
    inst->mix_noise = rand_f(&inst->rng) * 0.2f;
    inst->mix_fb   = 0.0f;                                /* no random feedback */

    /* VCO MIXER FREQ MOD attenuators (panel knobs) — bipolar, mostly closed */
    inst->vco_mg_int = rand_b(&inst->rng) * 0.4f;
    inst->vco_eg_int = rand_b(&inst->rng) * 0.5f;

    /* Filter — REV.1 vs REV.2 for character variety. Force a mode that passes
     * sound (HP+LP or LP only). LP only is the most common MS-20 sound. */
    inst->filter_mode = (rand_f(&inst->rng) < 0.6f) ? 1 : 0;  /* 60% LP, 40% HP+LP */
    inst->filter_rev  = (rand_f(&inst->rng) < 0.5f) ? 0 : 1;
    inst->lpf_cut    = 0.45f + rand_f(&inst->rng) * 0.4f;  /* 0.45-0.85: always audible */
    inst->lpf_reso   = rand_f(&inst->rng) * 0.65f;          /* cap below self-osc */
    inst->hpf_cut    = rand_f(&inst->rng) * 0.25f;          /* gentle HPF only */
    inst->hpf_reso   = rand_f(&inst->rng) * 0.4f;
    /* Bias EG mod POSITIVE so envelope OPENS the filter (audible). Allow a
     * small fraction of negative bias for "filter closes on attack" effect. */
    inst->lpf_eg_int = -0.1f + rand_f(&inst->rng) * 0.85f;  /* mostly positive */
    inst->lpf_mg_int = rand_b(&inst->rng) * 0.4f;
    inst->hpf_eg_int = -0.05f + rand_f(&inst->rng) * 0.35f; /* mostly positive */
    inst->hpf_mg_int = rand_b(&inst->rng) * 0.3f;
    inst->key_track  = 0.3f + rand_f(&inst->rng) * 0.5f;    /* always some tracking */
    inst->p_drive    = rand_f(&inst->rng) * 0.6f;

    /* Envelopes — capped to always start audibly within ~100ms. */
    inst->e1_delay = rand_f(&inst->rng) * 0.05f;            /* was 0.5s, now 50ms */
    inst->e1_atk   = 0.002f + rand_f(&inst->rng) * 0.2f;
    inst->e1_rel   = 0.05f  + rand_f(&inst->rng) * 1.5f;
    inst->e2_hold  = rand_f(&inst->rng) * 0.05f;            /* was 2s, now 50ms */
    inst->e2_atk   = 0.002f + rand_f(&inst->rng) * 0.2f;
    inst->e2_dcy   = 0.05f  + rand_f(&inst->rng) * 0.8f;
    inst->e2_sus   = 0.4f   + rand_f(&inst->rng) * 0.6f;    /* never below 0.4 */
    inst->e2_rel   = 0.1f   + rand_f(&inst->rng) * 1.2f;

    /* MG */
    inst->p_mg_rate  = rand_f(&inst->rng) * 0.7f;
    inst->p_mg_depth = rand_f(&inst->rng) * 0.6f;
    inst->mg_shape   = rand_f(&inst->rng);
    inst->mg_pw      = rand_f(&inst->rng);

    /* Patchbay jacks — most jacks can be re-routed freely; a couple are
     * locked to safe values to prevent silent patches.
     *
     * Locked: pb_kbd_cv (jack 0) — anything other than Normal kills the
     *         keyboard → VCO pitch path → silence on the first note.
     *         pb_vca_in (jack 5) — Initial Gain at None lets EG2 fully
     *         control the VCA; randomizing here can sum EG offsets that
     *         overdrive or float.
     *         pb_ext_sig (jack 7) — Tracks 1-4 are silent on firmware
     *         <2.0; Mixer Out feedback could blow up at high reso. */
    int jack_max[PB_NUM_JACKS] = {
        PB_JACK_NAMES_KBD_N, PB_JACK_NAMES_TOTAL_N, PB_JACK_NAMES_FREQ_N,
        PB_JACK_NAMES_HPF_N, PB_JACK_NAMES_LPF_N, PB_JACK_NAMES_VCA_N,
        PB_JACK_NAMES_PWM_N, PB_JACK_NAMES_EXTSIG_N,
        PB_JACK_NAMES_VCO2CV_N, PB_JACK_NAMES_EG1TRIG_N, PB_JACK_NAMES_EG12TRIG_N
    };
    static const int LOCKED_TO_DEFAULT[PB_NUM_JACKS] = {
        1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0
    };
    for (int i = 0; i < PB_NUM_JACKS; i++) {
        if (LOCKED_TO_DEFAULT[i] || rand_f(&inst->rng) < 0.6f) {
            inst->pb_jack[i] = 0;
        } else {
            inst->pb_jack[i] = rand32(&inst->rng) % jack_max[i];
        }
    }
}

/* apply_mutate — small variation around the current patch. Picks ~6 random
 * float parameters and applies a ±20% multiplicative offset. Keeps the
 * current sound's character but tweaks it. Live-friendly: each press of the
 * trigger button gives a slightly different version of the current sound. */
static void apply_mutate(synth_t *inst) {
    /* Pool of float pointers we're allowed to mutate. Limited to "musical"
     * params — no enums (would jump too radically), no envelope times below
     * 1ms (would click), no extreme depths. */
    float *pool[] = {
        &inst->lpf_cut, &inst->lpf_reso, &inst->hpf_cut, &inst->hpf_reso,
        &inst->p_drive, &inst->p_mg_rate, &inst->p_mg_depth,
        &inst->mg_shape, &inst->mg_pw,
        &inst->v1_pw,    &inst->v2_pw,    &inst->v2_detune,
        &inst->mix_v1,   &inst->mix_v2,   &inst->mix_sub, &inst->mix_noise,
        &inst->lpf_eg_int, &inst->lpf_mg_int, &inst->hpf_eg_int, &inst->hpf_mg_int,
        &inst->vco_mg_int, &inst->vco_eg_int,
        &inst->e1_atk,   &inst->e1_rel,
        &inst->e2_atk,   &inst->e2_dcy,   &inst->e2_sus,  &inst->e2_rel
    };
    int n_params = (int)(sizeof(pool) / sizeof(pool[0]));
    /* Tweak 6 of them */
    for (int n = 0; n < 6; n++) {
        int idx = rand32(&inst->rng) % n_params;
        float *p = pool[idx];
        float offset = rand_b(&inst->rng) * 0.2f;   /* ±20% */
        float v = *p * (1.0f + offset);
        /* Clamp to common-sense range — params here are all 0..1 or -1..1.
         * We don't know which, but clamping to [-1, 1] is safe for both. */
        *p = clampf(v, -1.0f, 1.0f);
    }
}

/* ── Parameters ─────────────────────────────────────────────────────────── */

#define EQ(s,k)  (strcmp((s),(k)) == 0)
/* Trigger params (Weird Dreams pattern, confirmed working on Schwung v0.9.7):
 *   chain_params: "type":"int","min":0,"max":1,"step":1
 *   set_param: action fires on ANY non-zero (atof(val) != 0.0)
 *   get_param: always returns "0"
 * Encoder cycle: starts at 0 → user turns once → value increments to 1 →
 * DSP fires action → DSP get_param returns "0" → next turn does 0→1 again.
 *
 * History: we tried "enum [Off,On]" (doesn't work — Shadow UI doesn't
 * cycle 2-option enums on encoder turns) and "int 0..127" (sometimes
 * worked but needed multiple turns to register). Range 0..1 is what
 * Weird Dreams uses and it's the only fully-reliable form on v0.9.7. */
#define TRIG_FIRED(v) (atof(v) != 0.0)

static int parse_enum(const char *val, const char **names, int n) {
    /* Accept either string name or numeric index */
    for (int i = 0; i < n; i++) if (strcmp(val, names[i]) == 0) return i;
    return atoi(val);
}

static void set_param(void *instance, const char *key, const char *val) {
    synth_t *inst = (synth_t *)instance;
    if (!key || !val) return;

    /* === Aliases for v0.2 module.json (MS-faithful + Modern layout) ===
     * These map the new canonical keys to the existing internal state fields
     * so module.json can use the period-correct names without breaking the
     * older internal naming. */
    if (EQ(key, "portamento")) { inst->p_glide = atof(val); return; }
    if (EQ(key, "mg_freq"))    { inst->p_mg_rate = atof(val); return; }
    if (EQ(key, "hpf_mg"))     { inst->hpf_mg_int = clampf(atof(val), -1.0f, 1.0f); return; }
    if (EQ(key, "hpf_eg"))     { inst->hpf_eg_int = clampf(atof(val), -1.0f, 1.0f); return; }
    if (EQ(key, "lpf_mg"))     { inst->lpf_mg_int = clampf(atof(val), -1.0f, 1.0f); return; }
    if (EQ(key, "lpf_eg"))     { inst->lpf_eg_int = clampf(atof(val), -1.0f, 1.0f); return; }
    if (EQ(key, "esp_lo_cut"))    { inst->esp_low_cut = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "esp_hi_cut"))    { inst->esp_high_cut = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "esp_threshold")) { inst->esp_gate_thr = atof(val); return; }
    if (EQ(key, "esp_cv_adj"))    { inst->esp_cv_adjust = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "x_mod_amt"))     { inst->v2_xmod = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "ms10_mode"))     { inst->p_ms10_mode = parse_enum(val, ON_OFF_NAMES, 2); return; }
    /* Patchbay jack-source enums (v0.2 source-side model — no destination depths).
     * These map to a flat array pb_jack[8] indexed in jack-panel order:
     *   0=KBD CV  1=T.EXT  2=VCO Freq  3=HPF EG/Ext  4=LPF EG/Ext
     *   5=Initial Gain  6=PWM In  7=Ext Sig
     * The jack source value is parsed against PB_JACK_SRC_NAMES which are
     * defined per-jack — for v0.2 we just store the index from a generic
     * fallback list and route in render_block. */
    if (EQ(key, "pb_kbd_cv"))  { inst->pb_jack[0] = parse_enum(val, PB_JACK_NAMES_KBD,    PB_JACK_NAMES_KBD_N); return; }
    if (EQ(key, "pb_total"))   { inst->pb_jack[1] = parse_enum(val, PB_JACK_NAMES_TOTAL,  PB_JACK_NAMES_TOTAL_N); return; }
    if (EQ(key, "pb_freq"))    { inst->pb_jack[2] = parse_enum(val, PB_JACK_NAMES_FREQ,   PB_JACK_NAMES_FREQ_N); return; }
    if (EQ(key, "pb_hpf_cv"))  { inst->pb_jack[3] = parse_enum(val, PB_JACK_NAMES_HPF,    PB_JACK_NAMES_HPF_N); return; }
    if (EQ(key, "pb_lpf_cv"))  { inst->pb_jack[4] = parse_enum(val, PB_JACK_NAMES_LPF,    PB_JACK_NAMES_LPF_N); return; }
    if (EQ(key, "pb_vca_in"))  { inst->pb_jack[5] = parse_enum(val, PB_JACK_NAMES_VCA,    PB_JACK_NAMES_VCA_N); return; }
    if (EQ(key, "pb_pwm_in"))  { inst->pb_jack[6] = parse_enum(val, PB_JACK_NAMES_PWM,    PB_JACK_NAMES_PWM_N); return; }
    if (EQ(key, "pb_ext_sig")) { inst->pb_jack[7] = parse_enum(val, PB_JACK_NAMES_EXTSIG, PB_JACK_NAMES_EXTSIG_N); return; }
    /* New MS-20 FS-faithful patchbay jacks (added in v0.2 cleanup) */
    if (EQ(key, "pb_vco2_cv"))   { inst->pb_jack[8]  = parse_enum(val, PB_JACK_NAMES_VCO2CV,   PB_JACK_NAMES_VCO2CV_N);   return; }
    if (EQ(key, "pb_eg1_trig"))  { inst->pb_jack[9]  = parse_enum(val, PB_JACK_NAMES_EG1TRIG,  PB_JACK_NAMES_EG1TRIG_N);  return; }
    if (EQ(key, "pb_eg12_trig")) { inst->pb_jack[10] = parse_enum(val, PB_JACK_NAMES_EG12TRIG, PB_JACK_NAMES_EG12TRIG_N); return; }
    /* VCO MIXER FREQ MOD attenuators (panel knobs on Mixer page slots 3-4) */
    if (EQ(key, "vco_mg")) { inst->vco_mg_int = clampf(atof(val), -1.0f, 1.0f); return; }
    if (EQ(key, "vco_eg")) { inst->vco_eg_int = clampf(atof(val), -1.0f, 1.0f); return; }

    /* Performance triggers — Signal v0.9.7 pattern: type:int 0..127, action
     * fires on any non-zero, get_param always returns "0" so the encoder
     * keeps rolling from 0→1→fires→snaps-to-0→repeats on next turn. */
    if (EQ(key, "rnd_patch"))    { if (TRIG_FIRED(val)) rnd_full(inst); return; }
    if (EQ(key, "mutate"))       { if (TRIG_FIRED(val)) apply_mutate(inst); return; }
    if (EQ(key, "rnd_mod"))      {
        if (!TRIG_FIRED(val)) return;
        /* Randomize ONLY the patchbay jacks. Each jack: 50% stays default,
         * 50% picks a random source from that jack's option list. Keeps
         * synth params intact — same sound, scrambled routing.
         * Lock the same set of jacks as rnd_full (KBD CV / Initial Gain /
         * Ext Sig) to prevent silent rerouting on jacks that need careful
         * matching. */
        int jack_max[PB_NUM_JACKS] = {
            PB_JACK_NAMES_KBD_N, PB_JACK_NAMES_TOTAL_N, PB_JACK_NAMES_FREQ_N,
            PB_JACK_NAMES_HPF_N, PB_JACK_NAMES_LPF_N, PB_JACK_NAMES_VCA_N,
            PB_JACK_NAMES_PWM_N, PB_JACK_NAMES_EXTSIG_N,
            PB_JACK_NAMES_VCO2CV_N, PB_JACK_NAMES_EG1TRIG_N, PB_JACK_NAMES_EG12TRIG_N
        };
        static const int LOCKED_TO_DEFAULT[PB_NUM_JACKS] = {
            1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0
        };
        for (int i = 0; i < PB_NUM_JACKS; i++) {
            if (LOCKED_TO_DEFAULT[i] || rand_f(&inst->rng) < 0.5f) {
                inst->pb_jack[i] = 0;
            } else {
                inst->pb_jack[i] = (int)(rand32(&inst->rng) % jack_max[i]);
            }
        }
        return;
    }
    if (EQ(key, "reset_patch"))  { if (TRIG_FIRED(val)) apply_init_preset(inst); return; }

    /* Floats — root */
    if (EQ(key, "drive"))     { inst->p_drive = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "vel_amt"))   { inst->p_vel_amt = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "mg_rate"))   { inst->p_mg_rate = atof(val); return; }
    if (EQ(key, "mg_depth"))  { inst->p_mg_depth = atof(val); return; }
    if (EQ(key, "volume"))    { inst->p_volume = atof(val); return; }
    if (EQ(key, "drift"))     { inst->p_drift = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "octave"))    { inst->p_octave = atoi(val); return; }
    if (EQ(key, "master_tune")) { inst->p_master_tune = clampf(atof(val), -1.0f, 1.0f); return; }
    /* Momentary trigger button — int 0-127 momentary. Each encoder click fires.
     * On firing, retrigger envelopes from current state (same as a keyboard
     * note-on but without changing the pitch). */
    if (EQ(key, "trigger")) {
        if (TRIG_FIRED(val)) {
            voice_t *v = &inst->voice;
            int was_audible = v->active && v->e2 > 0.001f;
            v->active = 1;
            if (was_audible) {
                v->e1_stage = 1; v->e2_stage = 1;
            } else {
                v->e1_stage = 0; v->e2_stage = 0;
                v->e1 = 0.0f;    v->e2 = 0.0f;
            }
            v->e1_timer = 0.0f; v->e2_timer = 0.0f;
            drift_note_on(&inst->drift);
        }
        return;
    }
    if (EQ(key, "preset"))    {
        int idx = parse_enum(val, PRESET_NAMES, PRESET_COUNT);
        apply_preset(inst, idx);
        inst->p_preset = idx;
        return;
    }

    /* VCO1 */
    /* Scale is an octave SWITCH (enum), not a free semitone value: each step = 1
     * octave. Host sends the enum index ("0".."3"); map to semitone offset with
     * 8' as the 0 reference. (Old code used atof → 1 semitone/step bug.) */
    if (EQ(key, "v1_pitch"))     { inst->v1_pitch = (parse_enum(val, V1_SCALE_NAMES, 4) - 2) * 12; return; }
    if (EQ(key, "v1_fine"))      { inst->v1_fine = atof(val); return; }
    if (EQ(key, "v1_wave"))      { inst->v1_wave = parse_enum(val, V1_WAVE_NAMES, 4); return; }
    if (EQ(key, "v1_pw"))        { inst->v1_pw = atof(val); return; }
    if (EQ(key, "v1_sub"))       { inst->v1_sub = atof(val); return; }
    if (EQ(key, "v1_pwm_rate"))  { inst->v1_pwm_rate = atof(val); return; }
    if (EQ(key, "v1_pwm_depth")) { inst->v1_pwm_depth = atof(val); return; }
    if (EQ(key, "v1_drift"))     { inst->v1_drift = atof(val); return; }

    /* VCO2 */
    if (EQ(key, "v2_pitch"))   { inst->v2_pitch = (parse_enum(val, V2_SCALE_NAMES, 4) - 1) * 12; return; }
    if (EQ(key, "v2_fine"))    { inst->v2_fine = atof(val); return; }
    if (EQ(key, "v2_wave"))    { inst->v2_wave = parse_enum(val, V2_WAVE_NAMES, 4); return; }
    if (EQ(key, "v2_pw"))      { inst->v2_pw = atof(val); return; }
    if (EQ(key, "v2_sync"))    { inst->v2_sync = parse_enum(val, SYNC_NAMES, 2); return; }
    if (EQ(key, "v2_xmod"))    { inst->v2_xmod = (float)parse_enum(val, ON_OFF_NAMES, 2); return; }
    if (EQ(key, "v2_detune"))  { inst->v2_detune = atof(val); return; }
    if (EQ(key, "v2_drift"))   { inst->v2_drift = atof(val); return; }

    /* Mixer */
    if (EQ(key, "mix_v1"))      { inst->mix_v1 = atof(val); return; }
    if (EQ(key, "mix_v2"))      { inst->mix_v2 = atof(val); return; }
    if (EQ(key, "mix_sub"))     { inst->mix_sub = atof(val); return; }
    if (EQ(key, "mix_noise"))   { inst->mix_noise = atof(val); return; }
    if (EQ(key, "noise_color")) { inst->noise_color = parse_enum(val, NOISE_NAMES, 2); return; }
    if (EQ(key, "mix_esp"))     { inst->mix_esp = atof(val); return; }
    if (EQ(key, "mix_fb"))      { inst->mix_fb = atof(val); return; }

    /* Filter */
    if (EQ(key, "lpf_cut"))     { inst->lpf_cut = atof(val); return; }
    if (EQ(key, "lpf_reso"))    { inst->lpf_reso = atof(val); return; }
    if (EQ(key, "hpf_cut"))     { inst->hpf_cut = atof(val); return; }
    if (EQ(key, "hpf_reso"))    { inst->hpf_reso = atof(val); return; }
    if (EQ(key, "key_track"))   { inst->key_track = atof(val); return; }
    if (EQ(key, "filter_mode")) { inst->filter_mode = parse_enum(val, FILT_MODE_NAMES, 4); return; }
    if (EQ(key, "filter_rev"))  { inst->filter_rev = parse_enum(val, FILT_REV_NAMES, 2); return; }
    /* Envelopes — EG1 is DAR (no D, no S); EG2 is HADSR. */
    if (EQ(key, "e1_delay")) { inst->e1_delay = atof(val); return; }
    if (EQ(key, "e1_atk"))   { inst->e1_atk = atof(val); return; }
    if (EQ(key, "e1_rel"))   { inst->e1_rel = atof(val); return; }
    if (EQ(key, "e2_hold"))  { inst->e2_hold = atof(val); return; }
    if (EQ(key, "e2_atk"))   { inst->e2_atk = atof(val); return; }
    if (EQ(key, "e2_dcy"))   { inst->e2_dcy = atof(val); return; }
    if (EQ(key, "e2_sus"))   { inst->e2_sus = atof(val); return; }
    if (EQ(key, "e2_rel"))   { inst->e2_rel = atof(val); return; }

    /* MG / S&H */
    if (EQ(key, "mg_shape")) { inst->mg_shape = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "mg_pw"))    { inst->mg_pw    = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "mg_delay")) { inst->mg_delay = atof(val); return; }
    if (EQ(key, "sh_rate"))  { inst->sh_rate = atof(val); return; }
    if (EQ(key, "sh_smooth")){ inst->sh_smooth = atof(val); return; }
    if (EQ(key, "mw_mg"))    { inst->mw_mg = atof(val); return; }
    if (EQ(key, "mw_filt"))  { inst->mw_filt = atof(val); return; }

    /* ESP */
    if (EQ(key, "esp_in_gain"))     { inst->esp_in_gain = atof(val); return; }
    if (EQ(key, "esp_pitch_slew"))  { inst->esp_pitch_slew = atof(val); return; }
    if (EQ(key, "esp_env_atk"))     { inst->esp_env_atk = atof(val); return; }
    if (EQ(key, "esp_env_rel"))     { inst->esp_env_rel = atof(val); return; }
    if (EQ(key, "esp_gate_thr"))    { inst->esp_gate_thr = atof(val); return; }
    if (EQ(key, "esp_aud_mix"))     { inst->esp_aud_mix = atof(val); return; }
    if (EQ(key, "esp_low_cut"))     { inst->esp_low_cut = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "esp_high_cut"))    { inst->esp_high_cut = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "esp_cv_adjust"))   { inst->esp_cv_adjust = clampf(atof(val), 0.0f, 1.0f); return; }
    if (EQ(key, "esp_pitch_mode"))  { inst->esp_pitch_mode = parse_enum(val, ESP_PITCH_MODES, 3); return; }
    if (EQ(key, "esp_gate_pol"))    { inst->esp_gate_pol = parse_enum(val, GATE_POL_NAMES, 2); return; }

    /* State serialization (deserialize) — newline-separated "key=value" lines.
     * Robust against future schema changes: unknown keys are skipped silently,
     * missing keys leave their values at the apply_init_preset default. */
    if (EQ(key, "state")) {
        const char *p = val;
        while (*p) {
            const char *eol = strchr(p, '\n');
            if (!eol) eol = p + strlen(p);
            const char *eq = (const char *)memchr(p, '=', eol - p);
            if (eq && eq > p) {
                /* Copy "key" and "value" to scratch buffers, then recurse. */
                char k[64], v[256];
                int klen = (int)(eq - p);
                int vlen = (int)(eol - eq - 1);
                if (klen > 0 && klen < (int)sizeof(k) && vlen >= 0 && vlen < (int)sizeof(v)) {
                    memcpy(k, p, klen); k[klen] = 0;
                    memcpy(v, eq + 1, vlen); v[vlen] = 0;
                    if (strcmp(k, "state") != 0) set_param(inst, k, v);
                }
            }
            if (!*eol) break;
            p = eol + 1;
        }
        return;
    }
}

/* Knob name labels per page */
/* Knob name labels per page. Match the merged-page layout in module.json:
 *   Root  = Cutoff/Reso/Drive/Env→Filt/MG Rate/MG Depth/M.Tune/Volume
 *   VCO   = V1 Pitch/V1 Wave/V1 PW/V2 Pitch/V2 Wave/V2 PW/V2 Detune/V2 Sync
 *   Mixer = unchanged
 *   Filter (8 = MS-20 layout) = HPF Cut/Peak/MG/EG · LPF Cut/Peak/MG/EG
 *   Env   (8 = MS-20 DAR + HADSR) = E1 Dly/Atk/Rel · E2 Hold/Atk/Dcy/Sus/Rel
 *   MG/ESP merged: MG Freq/MG Wave/MG Depth · ESP Lvl/Lo/Hi/Thr/CV Adj
 *   Patch: 8 source selectors (no depth)
 *   Modern: Drift/Drive/KeyTrack/Filter Rev/Sub Lvl/X-Mod/MW→MG/MW→Filt */
/* Per-page knob overlay names. Only ROOT is consumed by the C-side knob_N_name
 * handler below; sub-page knob labels come from module.json's "name" fields and
 * are rendered by the Schwung UI engine directly. */
/* Root-knob popup labels for the legacy knob_N_name/value path (Master FX
 * dispatch fallback). MUST stay in the same order as the root.knobs[] array
 * in ui_hierarchy. After the menu restructure (Signal-style root with
 * lpf/hpf/mg×2 + drive + volume) slot 6 changed from M.Tune to Drive. */
static const char *KNOB_NAMES_ROOT[8] = {"LPF Cut","LPF Peak","HPF Cut","HPF Peak","MG Freq","MG Depth","Drive","Volume"};

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    synth_t *inst = (synth_t *)instance;

    /* knob_N_name / knob_N_value popup for Master FX & root knob overlay */
    if (strncmp(key, "knob_", 5) == 0 && (strstr(key, "_name") || strstr(key, "_value"))) {
        int idx = atoi(key + 5) - 1;
        int is_value = (strstr(key, "_value") != NULL);
        if (idx < 0 || idx >= 8) return 0;
        if (is_value) {
            /* Same order as root.knobs[] in ui_hierarchy and KNOB_NAMES_ROOT[]
             * above. All 8 values are 0..1 normalized → display as integer
             * percent. (After the menu restructure, M.Tune was moved to the
             * Patch sub-page and Drive took its slot.) */
            const float *vals[8] = {
                &inst->lpf_cut, &inst->lpf_reso, &inst->hpf_cut, &inst->hpf_reso,
                &inst->p_mg_rate, &inst->p_mg_depth, &inst->p_drive, &inst->p_volume
            };
            return snprintf(buf, buf_len, "%d%%", (int)(*vals[idx] * 100));
        } else {
            return snprintf(buf, buf_len, "%s", KNOB_NAMES_ROOT[idx]);
        }
    }

    /* Root identification */
    if (EQ(key, "name")) return snprintf(buf, buf_len, "Aphex");

    /* Schwung v0.9.0+ requires sound generators to return ui_hierarchy from
     * get_param — module.json alone is no longer sufficient. Without this
     * handler the chain host's getComponentHierarchy() returns nothing and
     * the slot opens to a "No presets" preset browser instead of the menu.
     *
     * Layout: root is navigation-only (Signal-style), 8 perf knobs map to
     * the 8 encoders. "Patch" is a sub-page containing preset selection,
     * triggers, octave/tuning. Matches the menu pattern of other Schwung
     * synths (Signal, Denis, Essaim). */
    if (EQ(key, "ui_hierarchy")) {
        return snprintf(buf, buf_len,
            "{\"modes\":null,\"levels\":{"
              "\"root\":{"
                "\"name\":\"Aphex\","
                "\"knobs\":[\"lpf_cut\",\"lpf_reso\",\"hpf_cut\",\"hpf_reso\",\"mg_freq\",\"mg_depth\",\"drive\",\"volume\"],"
                "\"params\":["
                  "{\"level\":\"patch\",\"label\":\"Patch\"},"
                  "{\"level\":\"vcos\",\"label\":\"VCO 1+2\"},"
                  "{\"level\":\"mixer\",\"label\":\"Mixer\"},"
                  "{\"level\":\"filter\",\"label\":\"Filter\"},"
                  "{\"level\":\"env\",\"label\":\"Envelopes\"},"
                  "{\"level\":\"mg_esp\",\"label\":\"MG / ESP\"},"
                  "{\"level\":\"patchbay\",\"label\":\"Patchbay\"},"
                  "{\"level\":\"modern\",\"label\":\"Modern\"}"
                "]"
              "},"
              "\"patch\":{"
                "\"name\":\"Patch\","
                "\"knobs\":[\"preset\",\"rnd_patch\",\"mutate\",\"rnd_mod\",\"reset_patch\",\"octave\",\"master_tune\",\"portamento\"],"
                "\"params\":[\"preset\",\"rnd_patch\",\"mutate\",\"rnd_mod\",\"reset_patch\",\"octave\",\"master_tune\",\"portamento\",\"trigger\"]"
              "},"
              "\"vcos\":{"
                "\"name\":\"VCO 1+2\","
                "\"knobs\":[\"v1_wave\",\"v2_wave\",\"v1_pw\",\"v2_fine\",\"v1_pitch\",\"v2_pitch\",\"portamento\",\"master_tune\"],"
                "\"params\":[\"v1_wave\",\"v2_wave\",\"v1_pw\",\"v2_fine\",\"v1_pitch\",\"v2_pitch\",\"portamento\",\"master_tune\",\"v2_sync\",\"v2_xmod\"]"
              "},"
              "\"mixer\":{"
                "\"name\":\"Mixer\","
                "\"knobs\":[\"mix_v1\",\"mix_v2\",\"vco_mg\",\"vco_eg\",\"mix_esp\",\"mix_sub\",\"mix_noise\",\"mix_fb\"],"
                "\"params\":[\"mix_v1\",\"mix_v2\",\"vco_mg\",\"vco_eg\",\"mix_esp\",\"mix_sub\",\"mix_noise\",\"noise_color\",\"mix_fb\"]"
              "},"
              "\"filter\":{"
                "\"name\":\"Filter\","
                "\"knobs\":[\"hpf_cut\",\"hpf_reso\",\"hpf_mg\",\"hpf_eg\",\"lpf_cut\",\"lpf_reso\",\"lpf_mg\",\"lpf_eg\"],"
                "\"params\":[\"hpf_cut\",\"hpf_reso\",\"hpf_mg\",\"hpf_eg\",\"lpf_cut\",\"lpf_reso\",\"lpf_mg\",\"lpf_eg\",\"filter_rev\"]"
              "},"
              "\"env\":{"
                "\"name\":\"Envelopes\","
                "\"knobs\":[\"e1_delay\",\"e1_atk\",\"e1_rel\",\"e2_hold\",\"e2_atk\",\"e2_dcy\",\"e2_sus\",\"e2_rel\"],"
                "\"params\":[\"e1_delay\",\"e1_atk\",\"e1_rel\",\"e2_hold\",\"e2_atk\",\"e2_dcy\",\"e2_sus\",\"e2_rel\"]"
              "},"
              "\"mg_esp\":{"
                "\"name\":\"MG / ESP\","
                "\"knobs\":[\"mg_freq\",\"mg_shape\",\"esp_in_gain\",\"esp_lo_cut\",\"esp_hi_cut\",\"esp_cv_adj\",\"esp_threshold\",\"mg_pw\"],"
                "\"params\":[\"mg_freq\",\"mg_shape\",\"esp_in_gain\",\"esp_lo_cut\",\"esp_hi_cut\",\"esp_cv_adj\",\"esp_threshold\",\"mg_depth\",\"mg_pw\",\"esp_pitch_slew\",\"esp_env_atk\",\"esp_env_rel\",\"esp_aud_mix\",\"esp_pitch_mode\",\"esp_gate_pol\"]"
              "},"
              "\"patchbay\":{"
                "\"name\":\"Patchbay\","
                "\"knobs\":[\"pb_kbd_cv\",\"pb_total\",\"pb_freq\",\"pb_hpf_cv\",\"pb_lpf_cv\",\"pb_vca_in\",\"pb_vco2_cv\",\"pb_ext_sig\"],"
                "\"params\":[\"pb_kbd_cv\",\"pb_total\",\"pb_freq\",\"pb_hpf_cv\",\"pb_lpf_cv\",\"pb_vca_in\",\"pb_vco2_cv\",\"pb_ext_sig\",\"pb_pwm_in\",\"pb_eg1_trig\",\"pb_eg12_trig\"]"
              "},"
              "\"modern\":{"
                "\"name\":\"Modern\","
                "\"knobs\":[\"drift\",\"drive\",\"key_track\",\"ms10_mode\",\"v1_drift\",\"v2_drift\",\"mw_mg\",\"mw_filt\"],"
                "\"params\":[\"drift\",\"drive\",\"key_track\",\"ms10_mode\",\"v1_drift\",\"v2_drift\",\"mw_mg\",\"mw_filt\",\"filter_mode\",\"x_mod_amt\",\"v2_pw\",\"v2_detune\",\"vel_amt\"]"
              "}"
            "}}");
    }

    /* Floats — root */
    if (EQ(key, "drive"))     return snprintf(buf, buf_len, "%.4f", inst->p_drive);
    if (EQ(key, "vel_amt"))   return snprintf(buf, buf_len, "%.4f", inst->p_vel_amt);
    if (EQ(key, "mg_rate"))   return snprintf(buf, buf_len, "%.4f", inst->p_mg_rate);
    if (EQ(key, "mg_depth"))  return snprintf(buf, buf_len, "%.4f", inst->p_mg_depth);
    if (EQ(key, "volume"))    return snprintf(buf, buf_len, "%.4f", inst->p_volume);
    if (EQ(key, "drift"))     return snprintf(buf, buf_len, "%.4f", inst->p_drift);
    if (EQ(key, "octave"))    return snprintf(buf, buf_len, "%d", inst->p_octave);
    if (EQ(key, "master_tune")) return snprintf(buf, buf_len, "%.2f", inst->p_master_tune);
    /* Trigger params (Signal v0.9.7 pattern) — always read "0" so the
     * encoder keeps incrementing 0→1→fires→snap-to-0 on each turn. */
    if (EQ(key, "trigger"))     return snprintf(buf, buf_len, "0");
    if (EQ(key, "rnd_patch"))   return snprintf(buf, buf_len, "0");
    if (EQ(key, "mutate"))      return snprintf(buf, buf_len, "0");
    if (EQ(key, "rnd_mod"))     return snprintf(buf, buf_len, "0");
    if (EQ(key, "reset_patch")) return snprintf(buf, buf_len, "0");
    if (EQ(key, "preset"))    return snprintf(buf, buf_len, "%s", PRESET_NAMES[clampi(inst->p_preset, 0, PRESET_COUNT-1)]);

    /* === v0.2 module.json key aliases (read-side) === */
    if (EQ(key, "portamento"))    return snprintf(buf, buf_len, "%.4f", inst->p_glide);
    if (EQ(key, "mg_freq"))       return snprintf(buf, buf_len, "%.4f", inst->p_mg_rate);
    if (EQ(key, "hpf_mg"))        return snprintf(buf, buf_len, "%.4f", inst->hpf_mg_int);
    if (EQ(key, "hpf_eg"))        return snprintf(buf, buf_len, "%.4f", inst->hpf_eg_int);
    if (EQ(key, "lpf_mg"))        return snprintf(buf, buf_len, "%.4f", inst->lpf_mg_int);
    if (EQ(key, "lpf_eg"))        return snprintf(buf, buf_len, "%.4f", inst->lpf_eg_int);
    if (EQ(key, "esp_lo_cut"))    return snprintf(buf, buf_len, "%.4f", inst->esp_low_cut);
    if (EQ(key, "esp_hi_cut"))    return snprintf(buf, buf_len, "%.4f", inst->esp_high_cut);
    if (EQ(key, "esp_threshold")) return snprintf(buf, buf_len, "%.4f", inst->esp_gate_thr);
    if (EQ(key, "esp_cv_adj"))    return snprintf(buf, buf_len, "%.4f", inst->esp_cv_adjust);
    if (EQ(key, "x_mod_amt"))     return snprintf(buf, buf_len, "%.4f", inst->v2_xmod);
    if (EQ(key, "ms10_mode"))     return snprintf(buf, buf_len, "%s", ON_OFF_NAMES[clampi(inst->p_ms10_mode, 0, 1)]);
    if (EQ(key, "pb_kbd_cv"))  return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_KBD   [clampi(inst->pb_jack[0], 0, PB_JACK_NAMES_KBD_N-1)]);
    if (EQ(key, "pb_total"))   return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_TOTAL [clampi(inst->pb_jack[1], 0, PB_JACK_NAMES_TOTAL_N-1)]);
    if (EQ(key, "pb_freq"))    return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_FREQ  [clampi(inst->pb_jack[2], 0, PB_JACK_NAMES_FREQ_N-1)]);
    if (EQ(key, "pb_hpf_cv"))  return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_HPF   [clampi(inst->pb_jack[3], 0, PB_JACK_NAMES_HPF_N-1)]);
    if (EQ(key, "pb_lpf_cv"))  return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_LPF   [clampi(inst->pb_jack[4], 0, PB_JACK_NAMES_LPF_N-1)]);
    if (EQ(key, "pb_vca_in"))  return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_VCA   [clampi(inst->pb_jack[5], 0, PB_JACK_NAMES_VCA_N-1)]);
    if (EQ(key, "pb_pwm_in"))  return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_PWM   [clampi(inst->pb_jack[6], 0, PB_JACK_NAMES_PWM_N-1)]);
    if (EQ(key, "pb_ext_sig")) return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_EXTSIG[clampi(inst->pb_jack[7], 0, PB_JACK_NAMES_EXTSIG_N-1)]);
    if (EQ(key, "pb_vco2_cv"))   return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_VCO2CV  [clampi(inst->pb_jack[8],  0, PB_JACK_NAMES_VCO2CV_N-1)]);
    if (EQ(key, "pb_eg1_trig"))  return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_EG1TRIG [clampi(inst->pb_jack[9],  0, PB_JACK_NAMES_EG1TRIG_N-1)]);
    if (EQ(key, "pb_eg12_trig")) return snprintf(buf, buf_len, "%s", PB_JACK_NAMES_EG12TRIG[clampi(inst->pb_jack[10], 0, PB_JACK_NAMES_EG12TRIG_N-1)]);
    if (EQ(key, "vco_mg")) return snprintf(buf, buf_len, "%.4f", inst->vco_mg_int);
    if (EQ(key, "vco_eg")) return snprintf(buf, buf_len, "%.4f", inst->vco_eg_int);

    /* VCO1 */
    if (EQ(key, "v1_pitch"))     return snprintf(buf, buf_len, "%s", V1_SCALE_NAMES[clampi((int)roundf(inst->v1_pitch / 12.0f) + 2, 0, 3)]);
    if (EQ(key, "v1_fine"))      return snprintf(buf, buf_len, "%.4f", inst->v1_fine);
    if (EQ(key, "v1_wave"))      return snprintf(buf, buf_len, "%s", V1_WAVE_NAMES[clampi(inst->v1_wave, 0, 3)]);
    if (EQ(key, "v1_pw"))        return snprintf(buf, buf_len, "%.4f", inst->v1_pw);
    if (EQ(key, "v1_sub"))       return snprintf(buf, buf_len, "%.4f", inst->v1_sub);
    if (EQ(key, "v1_pwm_rate"))  return snprintf(buf, buf_len, "%.4f", inst->v1_pwm_rate);
    if (EQ(key, "v1_pwm_depth")) return snprintf(buf, buf_len, "%.4f", inst->v1_pwm_depth);
    if (EQ(key, "v1_drift"))     return snprintf(buf, buf_len, "%.4f", inst->v1_drift);

    /* VCO2 */
    if (EQ(key, "v2_pitch"))    return snprintf(buf, buf_len, "%s", V2_SCALE_NAMES[clampi((int)roundf(inst->v2_pitch / 12.0f) + 1, 0, 3)]);
    if (EQ(key, "v2_fine"))     return snprintf(buf, buf_len, "%.4f", inst->v2_fine);
    if (EQ(key, "v2_wave"))     return snprintf(buf, buf_len, "%s", V2_WAVE_NAMES[clampi(inst->v2_wave, 0, 3)]);
    if (EQ(key, "v2_pw"))       return snprintf(buf, buf_len, "%.4f", inst->v2_pw);
    if (EQ(key, "v2_sync"))     return snprintf(buf, buf_len, "%s", SYNC_NAMES[clampi(inst->v2_sync, 0, 1)]);
    if (EQ(key, "v2_xmod"))     return snprintf(buf, buf_len, "%s", ON_OFF_NAMES[inst->v2_xmod > 0.5f ? 1 : 0]);
    if (EQ(key, "v2_detune"))   return snprintf(buf, buf_len, "%.4f", inst->v2_detune);
    if (EQ(key, "v2_drift"))    return snprintf(buf, buf_len, "%.4f", inst->v2_drift);

    /* Mixer */
    if (EQ(key, "mix_v1"))      return snprintf(buf, buf_len, "%.4f", inst->mix_v1);
    if (EQ(key, "mix_v2"))      return snprintf(buf, buf_len, "%.4f", inst->mix_v2);
    if (EQ(key, "mix_sub"))     return snprintf(buf, buf_len, "%.4f", inst->mix_sub);
    if (EQ(key, "mix_noise"))   return snprintf(buf, buf_len, "%.4f", inst->mix_noise);
    if (EQ(key, "noise_color")) return snprintf(buf, buf_len, "%s", NOISE_NAMES[clampi(inst->noise_color, 0, 1)]);
    if (EQ(key, "mix_esp"))     return snprintf(buf, buf_len, "%.4f", inst->mix_esp);
    if (EQ(key, "mix_fb"))      return snprintf(buf, buf_len, "%.4f", inst->mix_fb);

    /* Filter */
    if (EQ(key, "lpf_cut"))     return snprintf(buf, buf_len, "%.4f", inst->lpf_cut);
    if (EQ(key, "lpf_reso"))    return snprintf(buf, buf_len, "%.4f", inst->lpf_reso);
    if (EQ(key, "hpf_cut"))     return snprintf(buf, buf_len, "%.4f", inst->hpf_cut);
    if (EQ(key, "hpf_reso"))    return snprintf(buf, buf_len, "%.4f", inst->hpf_reso);
    if (EQ(key, "key_track"))   return snprintf(buf, buf_len, "%.4f", inst->key_track);
    if (EQ(key, "filter_mode")) return snprintf(buf, buf_len, "%s", FILT_MODE_NAMES[clampi(inst->filter_mode, 0, 3)]);
    if (EQ(key, "filter_rev"))  return snprintf(buf, buf_len, "%s", FILT_REV_NAMES[clampi(inst->filter_rev, 0, 1)]);
    /* Envelopes — EG1 is DAR, EG2 is HADSR */
    if (EQ(key, "e1_delay")) return snprintf(buf, buf_len, "%.4f", inst->e1_delay);
    if (EQ(key, "e1_atk"))   return snprintf(buf, buf_len, "%.4f", inst->e1_atk);
    if (EQ(key, "e1_rel"))   return snprintf(buf, buf_len, "%.4f", inst->e1_rel);
    if (EQ(key, "e2_hold"))  return snprintf(buf, buf_len, "%.4f", inst->e2_hold);
    if (EQ(key, "e2_atk"))   return snprintf(buf, buf_len, "%.4f", inst->e2_atk);
    if (EQ(key, "e2_dcy")) return snprintf(buf, buf_len, "%.4f", inst->e2_dcy);
    if (EQ(key, "e2_sus")) return snprintf(buf, buf_len, "%.4f", inst->e2_sus);
    if (EQ(key, "e2_rel")) return snprintf(buf, buf_len, "%.4f", inst->e2_rel);

    /* MG / S&H */
    if (EQ(key, "mg_shape")) return snprintf(buf, buf_len, "%.4f", inst->mg_shape);
    if (EQ(key, "mg_pw"))    return snprintf(buf, buf_len, "%.4f", inst->mg_pw);
    /* Back-compat: old mg_wave readback returns the closest enum based on mg_shape */
    if (EQ(key, "mg_wave"))   {
        const char *n;
        if (inst->mg_shape < 0.25f)      n = "Saw";
        else if (inst->mg_shape < 0.75f) n = "Tri";
        else                             n = "Rev-Saw";
        return snprintf(buf, buf_len, "%s", n);
    }
    if (EQ(key, "mg_delay"))  return snprintf(buf, buf_len, "%.4f", inst->mg_delay);
    if (EQ(key, "sh_rate"))   return snprintf(buf, buf_len, "%.4f", inst->sh_rate);
    if (EQ(key, "sh_smooth")) return snprintf(buf, buf_len, "%.4f", inst->sh_smooth);
    if (EQ(key, "mw_mg"))     return snprintf(buf, buf_len, "%.4f", inst->mw_mg);
    if (EQ(key, "mw_filt"))   return snprintf(buf, buf_len, "%.4f", inst->mw_filt);

    /* ESP */
    if (EQ(key, "esp_in_gain"))     return snprintf(buf, buf_len, "%.4f", inst->esp_in_gain);
    if (EQ(key, "esp_pitch_slew"))  return snprintf(buf, buf_len, "%.4f", inst->esp_pitch_slew);
    if (EQ(key, "esp_env_atk"))     return snprintf(buf, buf_len, "%.4f", inst->esp_env_atk);
    if (EQ(key, "esp_env_rel"))     return snprintf(buf, buf_len, "%.4f", inst->esp_env_rel);
    if (EQ(key, "esp_gate_thr"))    return snprintf(buf, buf_len, "%.4f", inst->esp_gate_thr);
    if (EQ(key, "esp_aud_mix"))     return snprintf(buf, buf_len, "%.4f", inst->esp_aud_mix);
    if (EQ(key, "esp_low_cut"))     return snprintf(buf, buf_len, "%.4f", inst->esp_low_cut);
    if (EQ(key, "esp_high_cut"))    return snprintf(buf, buf_len, "%.4f", inst->esp_high_cut);
    if (EQ(key, "esp_cv_adjust"))   return snprintf(buf, buf_len, "%.4f", inst->esp_cv_adjust);
    if (EQ(key, "esp_pitch_mode"))  return snprintf(buf, buf_len, "%s", ESP_PITCH_MODES[clampi(inst->esp_pitch_mode, 0, 2)]);
    if (EQ(key, "esp_gate_pol"))    return snprintf(buf, buf_len, "%s", GATE_POL_NAMES[clampi(inst->esp_gate_pol, 0, 1)]);

    /* State serialization (serialize) — newline-separated key=value lines. */
    if (EQ(key, "state")) {
        /* Newline-separated "key=value" — robust to schema changes. The
         * deserializer (set_param "state") splits on \n and recurses through
         * set_param for each line, so any field with a regular setter is
         * automatically saveable. We list every persistent state field once.
         * Patchbay jacks save by their canonical names (pb_kbd_cv etc.).
         * Trigger-style buttons (rnd_patch, mutate, reset_patch) and ephemeral
         * runtime state (smoothed shadows, voice phase) are omitted. */
        int n = 0;
        #define ST_F(k, v) n += snprintf(buf + n, buf_len - n, k "=%.6f\n", (double)(v))
        #define ST_I(k, v) n += snprintf(buf + n, buf_len - n, k "=%d\n",   (int)(v))
        #define ST_S(k, v) n += snprintf(buf + n, buf_len - n, k "=%s\n",   (v))

        /* Preset FIRST: applying it runs apply_preset()->apply_init_preset()
         * which RESETS every p_* / VCO / mix / filter field to defaults. If any
         * other key were restored before this line, the preset would clobber it.
         * Emitting preset as line 1 guarantees the individually-restored values
         * below win. (Serialized as the enum index; parse_enum strcmp-first then
         * atoi, so the bare integer round-trips to the same preset.) */
        ST_I("preset",       inst->p_preset);

        /* Performance */
        ST_F("drive",        inst->p_drive);
        ST_F("vel_amt",      inst->p_vel_amt);
        ST_F("mg_rate",      inst->p_mg_rate);
        ST_F("mg_depth",     inst->p_mg_depth);
        ST_F("portamento",   inst->p_glide);
        ST_F("volume",       inst->p_volume);
        ST_I("octave",       inst->p_octave);
        ST_F("master_tune",  inst->p_master_tune);
        ST_F("drift",        inst->p_drift);

        /* VCO 1 */
        /* v1_pitch is stored as a SEMITONE offset, but its setter parses a SCALE
         * enum NAME ("32'".."4'") and re-applies (idx-2)*12. Emitting the raw
         * semitone float here made the setter re-interpret e.g. "-12" as an enum
         * index -> (-12-2)*12 = -168 st (near-silent), corrupting every non-8'
         * patch on reload. Emit the matching SCALE NAME (same mapping the direct
         * get_param("v1_pitch") uses) so the setter's enum path restores the
         * correct octave. 8' is the 0-semitone reference => name idx = st/12 + 2. */
        ST_S("v1_pitch",     V1_SCALE_NAMES[clampi((int)roundf(inst->v1_pitch / 12.0f) + 2, 0, 3)]);
        ST_F("v1_fine",      inst->v1_fine);
        ST_I("v1_wave",      inst->v1_wave);
        ST_F("v1_pw",        inst->v1_pw);
        ST_F("v1_sub",       inst->v1_sub);
        ST_F("v1_pwm_rate",  inst->v1_pwm_rate);
        ST_F("v1_pwm_depth", inst->v1_pwm_depth);
        ST_F("v1_drift",     inst->v1_drift);

        /* VCO 2 */
        /* Same fix as v1_pitch: v2 SCALE names are ["16'","8'","4'","2'"] with
         * setter (idx-1)*12, so 8' is the 0-semitone reference => name idx =
         * st/12 + 1. Emit the SCALE NAME, not the raw semitone float. */
        ST_S("v2_pitch",     V2_SCALE_NAMES[clampi((int)roundf(inst->v2_pitch / 12.0f) + 1, 0, 3)]);
        ST_F("v2_fine",      inst->v2_fine);
        ST_I("v2_wave",      inst->v2_wave);
        ST_F("v2_pw",        inst->v2_pw);
        ST_I("v2_sync",      inst->v2_sync);
        /* X-Mod depth lives in inst->v2_xmod as a CONTINUOUS 0..1 value (the
         * Modern x_mod_amt control writes it raw; the binary v2_xmod FM switch is
         * just a quantized view of the same field). Saving it under the on/off
         * "v2_xmod" key was lossy: that setter is parse_enum(ON_OFF) -> atoi, so
         * a fractional depth like 0.35 restored as 0. Serialize under x_mod_amt,
         * whose setter is a raw clampf(atof), preserving the exact depth. */
        ST_F("x_mod_amt",    inst->v2_xmod);
        ST_F("v2_detune",    inst->v2_detune);
        ST_F("v2_drift",     inst->v2_drift);

        /* Mixer */
        ST_F("mix_v1",       inst->mix_v1);
        ST_F("mix_v2",       inst->mix_v2);
        ST_F("mix_sub",      inst->mix_sub);
        ST_F("mix_noise",    inst->mix_noise);
        ST_I("noise_color",  inst->noise_color);
        ST_F("mix_esp",      inst->mix_esp);
        ST_F("mix_fb",       inst->mix_fb);

        /* Filter */
        ST_F("lpf_cut",      inst->lpf_cut);
        ST_F("lpf_reso",     inst->lpf_reso);
        ST_F("hpf_cut",      inst->hpf_cut);
        ST_F("hpf_reso",     inst->hpf_reso);
        ST_F("key_track",    inst->key_track);
        ST_I("filter_mode",  inst->filter_mode);
        ST_I("filter_rev",   inst->filter_rev);
        /* Serialize under the SHORT (canonical) names that match module.json
         * chain_params keys — these are also the ones set_param actually
         * handles after the _int alias cleanup. */
        ST_F("hpf_mg",       inst->hpf_mg_int);
        ST_F("hpf_eg",       inst->hpf_eg_int);
        ST_F("lpf_mg",       inst->lpf_mg_int);
        ST_F("lpf_eg",       inst->lpf_eg_int);
        ST_F("vco_mg",       inst->vco_mg_int);
        ST_F("vco_eg",       inst->vco_eg_int);

        /* Envelopes */
        ST_F("e1_delay",     inst->e1_delay);
        ST_F("e1_atk",       inst->e1_atk);
        ST_F("e1_rel",       inst->e1_rel);
        ST_F("e2_hold",      inst->e2_hold);
        ST_F("e2_atk",       inst->e2_atk);
        ST_F("e2_dcy",       inst->e2_dcy);
        ST_F("e2_sus",       inst->e2_sus);
        ST_F("e2_rel",       inst->e2_rel);

        /* MG / S&H */
        ST_F("mg_shape",     inst->mg_shape);
        ST_F("mg_pw",        inst->mg_pw);
        ST_F("mg_delay",     inst->mg_delay);
        ST_F("sh_rate",      inst->sh_rate);
        ST_F("sh_smooth",    inst->sh_smooth);
        ST_F("mw_mg",        inst->mw_mg);
        ST_F("mw_filt",      inst->mw_filt);

        /* ESP */
        ST_F("esp_in_gain",     inst->esp_in_gain);
        ST_F("esp_pitch_slew",  inst->esp_pitch_slew);
        ST_F("esp_env_atk",     inst->esp_env_atk);
        ST_F("esp_env_rel",     inst->esp_env_rel);
        ST_F("esp_threshold",   inst->esp_gate_thr);
        ST_F("esp_aud_mix",     inst->esp_aud_mix);
        ST_F("esp_lo_cut",      inst->esp_low_cut);
        ST_F("esp_hi_cut",      inst->esp_high_cut);
        ST_F("esp_cv_adj",      inst->esp_cv_adjust);
        ST_I("esp_pitch_mode",  inst->esp_pitch_mode);
        ST_I("esp_gate_pol",    inst->esp_gate_pol);

        /* Patchbay jacks (by canonical name) */
        ST_S("pb_kbd_cv",    PB_JACK_NAMES_KBD    [clampi(inst->pb_jack[0], 0, PB_JACK_NAMES_KBD_N-1)]);
        ST_S("pb_total",     PB_JACK_NAMES_TOTAL  [clampi(inst->pb_jack[1], 0, PB_JACK_NAMES_TOTAL_N-1)]);
        ST_S("pb_freq",      PB_JACK_NAMES_FREQ   [clampi(inst->pb_jack[2], 0, PB_JACK_NAMES_FREQ_N-1)]);
        ST_S("pb_hpf_cv",    PB_JACK_NAMES_HPF    [clampi(inst->pb_jack[3], 0, PB_JACK_NAMES_HPF_N-1)]);
        ST_S("pb_lpf_cv",    PB_JACK_NAMES_LPF    [clampi(inst->pb_jack[4], 0, PB_JACK_NAMES_LPF_N-1)]);
        ST_S("pb_vca_in",    PB_JACK_NAMES_VCA    [clampi(inst->pb_jack[5], 0, PB_JACK_NAMES_VCA_N-1)]);
        ST_S("pb_pwm_in",    PB_JACK_NAMES_PWM    [clampi(inst->pb_jack[6], 0, PB_JACK_NAMES_PWM_N-1)]);
        ST_S("pb_ext_sig",   PB_JACK_NAMES_EXTSIG [clampi(inst->pb_jack[7], 0, PB_JACK_NAMES_EXTSIG_N-1)]);
        ST_S("pb_vco2_cv",   PB_JACK_NAMES_VCO2CV [clampi(inst->pb_jack[8], 0, PB_JACK_NAMES_VCO2CV_N-1)]);
        ST_S("pb_eg1_trig",  PB_JACK_NAMES_EG1TRIG [clampi(inst->pb_jack[9], 0, PB_JACK_NAMES_EG1TRIG_N-1)]);
        ST_S("pb_eg12_trig", PB_JACK_NAMES_EG12TRIG[clampi(inst->pb_jack[10], 0, PB_JACK_NAMES_EG12TRIG_N-1)]);

        /* Modern */
        ST_I("ms10_mode",    inst->p_ms10_mode);

        #undef ST_F
        #undef ST_I
        #undef ST_S
        return n;
    }

    /* CRITICAL: -1 for unknown keys, NOT 0. Returning 0 breaks Master FX menu editing. */
    return -1;
}

/* ── Audio rendering ────────────────────────────────────────────────────── */

/* === MS-20 envelopes (per-sample state machines) ============================
 *
 * EG 1 — TRAPEZOID (Delay → Attack → Sustain@max → Release).
 *   Three knobs only: Delay, Attack, Release.
 *   Sustain is FORCED to 1.0 internally — no decay, no sustain knob.
 *   On note-on: stage=0 (delay timer counting up). When timer ≥ delay,
 *     advance to stage 1 (attack). When level reaches 1.0, jump to stage 3
 *     (sustain at peak — never decays). On note-off: stage=4 (release).
 *
 * EG 2 — HADSR (Hold → Attack → Decay → Sustain → Release).
 *   Five knobs: Hold (max 20s per MS-20 spec), Attack, Decay, Sustain, Release.
 *   On note-on: stage=0 (hold timer counting at level=0).
 *     ⚠ MS-20 quirk: the original EG2's Hold is BEFORE attack and stays at zero.
 *     Some sources place Hold AFTER attack at peak; we use the MS-20 mini
 *     manual's interpretation (pre-attack hold).
 *   When timer ≥ hold, advance to stage 1 (attack). Then standard ADSR.
 *
 * Linear release is intentional (per Denis lessons: exponential release at 1.5s
 * feels like 10s on the ear — linear matches MS-20's CV decay better).
 */

static inline void env1_step_dar(voice_t *v, float delay, float atk, float rel) {
    switch (v->e1_stage) {
        case 0: { /* Delay — level stays at 0 until timer expires */
            v->e1_timer += INV_SR;
            if (v->e1_timer >= delay) { v->e1_stage = 1; }
            break;
        }
        case 1: { /* Attack — exponential approach */
            float coef = 1.0f - expf(-1.0f / (atk * SAMPLE_RATE + 1.0f));
            v->e1 += coef * (1.05f - v->e1);
            if (v->e1 >= 1.0f) { v->e1 = 1.0f; v->e1_stage = 3; }
            break;
        }
        /* No stage 2 (Decay) for EG1 — trapezoid jumps straight to Sustain@max */
        case 3: break;  /* Sustain at peak — held until note-off */
        case 4: { /* Release — linear */
            float rate = 1.0f / (rel * SAMPLE_RATE);
            v->e1 -= rate;
            if (v->e1 <= 0.0f) { v->e1 = 0.0f; v->e1_stage = 5; }
            break;
        }
        default: break;
    }
}

static inline void env2_step_hadsr(voice_t *v, float hold, float atk, float dcy,
                                   float sus, float rel) {
    switch (v->e2_stage) {
        case 0: { /* Hold — level at 0 until timer expires */
            v->e2_timer += INV_SR;
            if (v->e2_timer >= hold) { v->e2_stage = 1; }
            break;
        }
        case 1: { /* Attack — exponential */
            float coef = 1.0f - expf(-1.0f / (atk * SAMPLE_RATE + 1.0f));
            v->e2 += coef * (1.05f - v->e2);
            if (v->e2 >= 1.0f) { v->e2 = 1.0f; v->e2_stage = 2; }
            break;
        }
        case 2: { /* Decay — linear */
            float rate = (1.0f - sus) / (dcy * SAMPLE_RATE);
            v->e2 -= rate;
            if (v->e2 <= sus) { v->e2 = sus; v->e2_stage = 3; }
            break;
        }
        case 3: break;  /* Sustain */
        case 4: { /* Release — linear */
            float rate = 1.0f / (rel * SAMPLE_RATE);
            v->e2 -= rate;
            if (v->e2 <= 0.0f) { v->e2 = 0.0f; v->e2_stage = 5; }
            break;
        }
        default: break;
    }
}

/* Resolve patchbay source value at audio rate.
 * Returns bipolar -1..+1 for most sources, scaled by the host of the destination. */
/* === MS-20 input-jack resolvers (v0.2 source-side patchbay) ============
 *
 * Each function returns the SIGNAL flowing through one specific MS-20 input
 * jack, given the jack's current source-selector value. Default (index 0)
 * returns the hard-wired MS-20 default — what the synth does when nothing is
 * patched into that jack.
 *
 * These signals then feed the existing panel attenuators (HPF MG, LPF EG,
 * etc.), preserving the original MS-20 semantic where the panel knob is the
 * intensity control and the jack just substitutes the source.
 *
 * Bipolar convention: pitch/CV signals are ±1 (±2 octaves at full scale for
 * pitch); audio-rate signals (mg_tri, mg_sq, esp_aud) are passed through as-is.
 */

/* T.EXT — replaces MG in panel MG/T.EXT attenuators (VCO Master + HPF + LPF).
 * Default 0 = MG itself (route the panel "MG" attenuator to MG triangle out). */
static inline float jack_total(synth_t *inst, int sel, float mg_tri, float sh,
                               float noise_smoothed) {
    voice_t *v = &inst->voice;
    switch (sel) {
        case 0:  return mg_tri;                           /* Default: MG */
        case 1:  return inst->mod_wheel * 2.0f - 1.0f;    /* Wheel */
        case 2:  return inst->esp_pitch_smoothed;         /* ESP CV */
        case 3:  return inst->esp_env_smoothed * 2.0f - 1.0f;  /* ESP Env */
        case 4:  return sh;                               /* S&H */
        case 5:  return noise_smoothed;                   /* Noise */
        case 6:  return -(v->e1 * 2.0f - 1.0f);           /* EG1 Reverse */
        case 7:  return -(v->e2 * 2.0f - 1.0f);           /* EG2 Reverse */
        case 8:  return 0.0f;                             /* None */
        default: return mg_tri;
    }
}

/* VCO Freq — replaces EG1 in VCO Master EG1/EXT attenuator.
 * Default 0 = EG1 (the hard-wired routing for this attenuator). */
static inline float jack_vco_freq(synth_t *inst, int sel, float sh, float noise_smoothed) {
    voice_t *v = &inst->voice;
    switch (sel) {
        case 0:  return v->e1 * 2.0f - 1.0f;              /* Default: EG1 */
        case 1:  return v->e2 * 2.0f - 1.0f;              /* EG2 */
        case 2:  return inst->esp_env_smoothed * 2.0f - 1.0f;
        case 3:  return inst->mod_wheel * 2.0f - 1.0f;
        case 4:  return sh;
        case 5:  return noise_smoothed;
        case 6:  return 0.0f;                             /* None */
        default: return v->e1 * 2.0f - 1.0f;
    }
}

/* HPF EG/Ext — feeds the HPF EG/EXT attenuator. Envelope sources are UNIPOLAR
 * (0..1) so they only OPEN the filter; LFO/S&H/Wheel/Noise stay bipolar. */
static inline float jack_hpf_cv(synth_t *inst, int sel, float sh, float noise_smoothed) {
    voice_t *v = &inst->voice;
    switch (sel) {
        case 0:  return v->e1;                            /* Default: EG1 (filter env, unipolar) */
        case 1:  return v->e2;                            /* EG2 (unipolar) */
        case 2:  return inst->esp_env_smoothed * 2.0f - 1.0f;
        case 3:  return inst->mod_wheel * 2.0f - 1.0f;
        case 4:  return sh;
        case 5:  return noise_smoothed;
        case 6:  return 0.0f;
        default: return v->e1;
    }
}

/* LPF EG/Ext — feeds the LPF EG/EXT attenuator. EG1 is the default filter
 * envelope (unipolar: opens the filter only — never pulls cutoff below base,
 * so lowering sustain/decay can no longer silence a held note). */
static inline float jack_lpf_cv(synth_t *inst, int sel, float sh, float noise_smoothed) {
    voice_t *v = &inst->voice;
    switch (sel) {
        case 0:  return v->e1;                            /* Default: EG1 (filter env, unipolar) */
        case 1:  return v->e2;                            /* EG2 (unipolar) */
        case 2:  return inst->esp_env_smoothed * 2.0f - 1.0f;
        case 3:  return inst->mod_wheel * 2.0f - 1.0f;
        case 4:  return sh;
        case 5:  return noise_smoothed;
        case 6:  return 0.0f;
        default: return v->e1;
    }
}

/* INITIAL GAIN — adds to EG2's VCA control (MS-20 spec: no attenuator at jack;
 * the signal is passed at full level and SUMMED with EG2's contribution).
 * Returns 0..1 (positive only — initial gain raises VCA's floor). */
static inline float jack_vca_initial(synth_t *inst, int sel, float sh, float noise_smoothed) {
    voice_t *v = &inst->voice;
    switch (sel) {
        case 0:  return 0.0f;                             /* None (default) */
        case 1:  return inst->mod_wheel;                  /* Wheel: 0..1 */
        case 2:  return inst->esp_env_smoothed;           /* ESP Env: 0..1 */
        case 3:  return v->e1;                            /* EG1: 0..1 */
        case 4:  return (sh + 1.0f) * 0.5f;               /* S&H: shift to 0..1 */
        case 5:  return (noise_smoothed + 1.0f) * 0.5f;
        default: return 0.0f;
    }
}

/* PWM In — modulates VCO1 PW-Square (MS-20 mini Kit feature).
 * Returns ±1 (added to v1_pw with depth). */
static inline float jack_pwm(synth_t *inst, int sel, float mg_tri, float mg_sq,
                             float sh, float noise_smoothed) {
    voice_t *v = &inst->voice;
    switch (sel) {
        case 0:  return 0.0f;                             /* None (default) */
        case 1:  return mg_tri;
        case 2:  return mg_sq;
        case 3:  return v->e1 * 2.0f - 1.0f;
        case 4:  return v->e2 * 2.0f - 1.0f;
        case 5:  return sh;
        case 6:  return noise_smoothed;
        case 7:  return inst->esp_env_smoothed * 2.0f - 1.0f;
        case 8:  return inst->mod_wheel * 2.0f - 1.0f;
        default: return 0.0f;
    }
}

/* External Signal — what feeds the VCF external signal input.
 * Used by the audio-rate filter input (3Vp-p max per MS-20 spec).
 * Cases 4-7 read Track 1-4 from /move-track-audio (Schwung v0.7.8+). The
 * `frame_idx` argument is the current sample within the 128-frame block.
 * Track audio is mono-summed (L+R)/2 and normalized to ±1.0. If the SHM
 * isn't available (older host), tracks return silence. */
static inline float jack_ext_sig(synth_t *inst, int sel,
                                 float esp_audio, float mixer_out, float noise_audio,
                                 int frame_idx) {
    switch (sel) {
        case 0:  return 0.0f;                             /* None (default) */
        case 1:  return esp_audio;                        /* ESP Audio */
        case 2:  return noise_audio;                      /* Noise (audio rate, not smoothed) */
        case 3:  return mixer_out;                        /* Mixer Out (feedback) */
        case 4: case 5: case 6: case 7: {
            /* Track N (1-4) — read stereo int16 from SHM, mono-sum to ±1. */
            if (!inst->track_audio) return 0.0f;
            int trk = sel - 4;            /* 0..3 */
            int base = trk * TRK_STEREO_INT16_PER_CHAN + frame_idx * 2;
            int16_t l = inst->track_audio[base];
            int16_t r = inst->track_audio[base + 1];
            return ((float)l + (float)r) * (0.5f / 32768.0f);
        }
        default: return 0.0f;
    }
}

/* VCO 2 CV IN — separate Hz/V into VCO 2 only.
 * Returns ±1 (semitones × 12 = ±1 octave at full scale). */
static inline float jack_vco2_cv(synth_t *inst, int sel, float sh, float noise_smoothed) {
    voice_t *v = &inst->voice;
    switch (sel) {
        case 0:  return 0.0f;                             /* None (default) */
        case 1:  return inst->esp_pitch_smoothed;         /* ESP CV */
        case 2:  return inst->mod_wheel * 2.0f - 1.0f;    /* Wheel */
        case 3:  return sh;
        case 4:  return noise_smoothed;
        case 5:  return (v->note - 60) / 24.0f;           /* KBD CV (echoes V1's pitch) */
        default: return 0.0f;
    }
}

/* EG TRIG jacks — return a 0..1 gate. Edge detection happens in render_block.
 * Sources are envelope/gate-shaped, not oscillator outputs. */
static inline float jack_eg_trig(synth_t *inst, int sel, float sh, float noise_smoothed) {
    switch (sel) {
        case 0:  return 0.0f;                                /* None — never triggers */
        case 1:  return (sh > 0.0f) ? 1.0f : 0.0f;           /* S&H gate */
        case 2:  return inst->esp_gate_state ? 1.0f : 0.0f;  /* ESP Gate */
        case 3:  return inst->mod_wheel;                     /* Wheel: 0..1 already */
        case 4:  return (noise_smoothed > 0.0f) ? 1.0f : 0.0f; /* Noise gate */
        default: return 0.0f;
    }
}

/* Check ESP audio at line-in for pitch/env/gate every block */
static void update_esp(synth_t *inst, const int16_t *audio_in, int frames) {
    if (!audio_in) {
        /* No line-in — decay state cleanly */
        inst->esp_env_smoothed *= 0.9f;
        inst->esp_audio_last = 0.0f;
        return;
    }

    /* Average input level + buffer for pitch */
    float env_atk_coef = 1.0f - expf(-1.0f / (inst->esp_env_atk * SAMPLE_RATE + 1.0f));
    float env_rel_coef = 1.0f - expf(-1.0f / (inst->esp_env_rel * SAMPLE_RATE + 1.0f));

    /* ESP band-limit coefficients (block-invariant). Lo Cut = one-pole high-pass
     * (50 Hz..2.5 kHz), Hi Cut = one-pole low-pass (100 Hz..5 kHz). These shape
     * the signal the env follower / gate / pitch tracker and the routed ESP Audio
     * all see — previously stored but never applied, so the knobs did nothing. */
    float lc_hz = 50.0f  * powf(50.0f, inst->esp_low_cut);
    float hc_hz = 100.0f * powf(50.0f, inst->esp_high_cut);
    float lc_a  = 1.0f - expf(-TWO_PI * lc_hz * INV_SR);
    float hc_a  = 1.0f - expf(-TWO_PI * hc_hz * INV_SR);

    /* Soft noise gate: below ~−60 dBFS we stop pulling the envelope UP toward
     * ambient hiss but still allow it to decay. Prevents the env follower
     * from latching onto the floor when no signal is present (the Move's
     * line-in has a non-trivial preamp noise floor). */
    const float NOISE_FLOOR = 0.001f;
    float last = inst->esp_audio_last;
    for (int i = 0; i < frames; i++) {
        float l = audio_in[i*2] / 32768.0f;
        float r = audio_in[i*2+1] / 32768.0f;
        float in = (l + r) * 0.5f * inst->esp_in_gain;
        /* Lo Cut (high-pass residue) then Hi Cut (low-pass) */
        inst->esp_lc_z += lc_a * (in - inst->esp_lc_z);
        in -= inst->esp_lc_z;
        inst->esp_hc_z += hc_a * (in - inst->esp_hc_z);
        in = inst->esp_hc_z;
        last = in;
        float abs_in = fabsf(in);
        if (abs_in < NOISE_FLOOR) abs_in = 0.0f;
        float c = (abs_in > inst->esp_env_smoothed) ? env_atk_coef : env_rel_coef;
        inst->esp_env_smoothed += c * (abs_in - inst->esp_env_smoothed);

        /* Buffer for pitch tracker */
        inst->esp_buf[inst->esp_buf_pos] = in;
        inst->esp_buf_pos = (inst->esp_buf_pos + 1) & 2047;
    }
    inst->esp_audio_last = last;

    /* Gate (Schmitt) */
    float thr_hi = inst->esp_gate_thr;
    float thr_lo = thr_hi * 0.7f;
    if (!inst->esp_gate_state && inst->esp_env_smoothed > thr_hi) inst->esp_gate_state = 1;
    if (inst->esp_gate_state  && inst->esp_env_smoothed < thr_lo) inst->esp_gate_state = 0;
    if (inst->esp_gate_pol) inst->esp_gate_state = !inst->esp_gate_state;

    /* Pitch tracker — YIN (de Cheveigné & Kawahara 2002), see yin.h.
     * Range: τ ∈ [30, 512] = ~86 Hz to ~1.47 kHz at 44.1 kHz. */
    if (inst->esp_pitch_mode > 0) {
        inst->esp_pitch_counter++;
        if (inst->esp_pitch_counter >= 8) {  /* update every 8 blocks ≈ 23ms */
            inst->esp_pitch_counter = 0;
            float tau = 0.0f;
            int found = yin_analyze(inst->esp_buf, inst->esp_buf_pos, 2047,
                                    1024, 30, 512, 0.15f,
                                    inst->esp_yin_cmndf, &tau);
            if (found && tau > 0.0f) {
                float hz = SAMPLE_RATE / tau;
                /* Convert to V/oct-style CV: log2(hz / C4), scaled ±2 oct → ±0.5 */
                inst->esp_pitch_raw = log2f(hz / 261.63f) * 0.25f;
            }
        }
        /* Smooth pitch CV (slew = 0.001 .. 0.011 per sample) */
        float slew = 0.001f + inst->esp_pitch_slew * 0.01f;
        inst->esp_pitch_smoothed += slew * (inst->esp_pitch_raw - inst->esp_pitch_smoothed);
    }
}

static void render_block(void *instance, int16_t *out_lr, int frames) {
    synth_t *inst = (synth_t *)instance;
    voice_t *v = &inst->voice;

    /* Read line-in for ESP */
    int16_t *audio_in = NULL;
    if (inst->host && inst->host->mapped_memory) {
        audio_in = (int16_t *)((uint8_t *)inst->host->mapped_memory + inst->host->audio_in_offset);
    }
    update_esp(inst, audio_in, frames);

    /* Per-block: pre-compute static modulation values */
    float mg_rate_hz = 0.05f * powf(800.0f, inst->p_mg_rate);  /* 0.05 to 40 Hz */
    float sh_rate_hz = 0.05f * powf(400.0f, inst->sh_rate);    /* 0.05 to 20 Hz */

    /* Smoothed cutoff coefficient (20ms — per Denis lessons) */
    const float SMOOTH = 0.135f;

    /* Block-rate drive smoothing: 20ms is ~7 blocks, so per-block stepping is
     * smooth enough at the ear and avoids per-sample cost. Pop-free when the
     * user turns the Drive knob during sustained notes. */
    inst->sm_drive += SMOOTH * (inst->p_drive - inst->sm_drive);

    /* === Analog drift — multi-source random walk + flutter LFOs.
     * Updated once per block; per-sample interpolation done inside drift_*. */
    drift_block_update(&inst->drift, inst->p_drift);

    /* Pre-compute key tracking offset */
    float kt = (v->note - 60) / 12.0f * inst->key_track;  /* in octaves */

    /* Block-invariant smoothing coefficients — hoisted out of the per-sample
     * loop (they depend only on block-constant params, so the expf() ran 44100×
     * per second for nothing). */
    int   glide_on  = inst->p_glide > 0.001f;
    float glide_amt = glide_on
        ? (1.0f - expf(-1.0f / (inst->p_glide * 2.0f * SAMPLE_RATE + 1.0f)))
        : 1.0f;
    float sh_coef   = (inst->sh_smooth > 0.001f)
        ? (1.0f - expf(-1.0f / (inst->sh_smooth * 0.05f * SAMPLE_RATE + 1.0f)))
        : 1.0f;

    for (int i = 0; i < frames; i++) {
        /* Per-sample drift advance — interpolates the slow random walk and
         * holds flutter phases stable across the block. Cheap (~5 instructions). */
        drift_sample_tick(&inst->drift);

        /* === Glide === */
        if (glide_on) v->freq += (v->freq_target - v->freq) * glide_amt;
        else          v->freq = v->freq_target;

        /* === Modulation generators per sample === */

        /* MG / LFO — MS-20 has two simultaneous outputs with continuous shape
         * controls. The slope output morphs through positive saw → triangle →
         * negative saw via mg_shape; the pulse output's duty cycle is set by
         * mg_pw independently. Both signals are always available as patch
         * sources (MG Tri at mg_tri, MG Sq at mg_sq). */
        inst->mg_phase += mg_rate_hz * INV_SR;
        if (inst->mg_phase >= 1.0f) inst->mg_phase -= 1.0f;

        /* Slope output: peak position = (1 - mg_shape).
         *   shape=0   → peak at phase=1.0  → positive saw (0..1 ramp)
         *   shape=0.5 → peak at phase=0.5  → triangle
         *   shape=1   → peak at phase=0.0  → negative saw (1..0 ramp)
         * Two-segment piecewise linear: rise from -1 to +1 across the first
         * segment, fall from +1 to -1 across the second. Avoids div-by-zero at
         * the endpoints by clamping the peak position into (0, 1). */
        float peak = 1.0f - clampf(inst->mg_shape, 0.001f, 0.999f);
        float mg_tri;
        if (inst->mg_phase < peak) {
            mg_tri = -1.0f + 2.0f * (inst->mg_phase / peak);
        } else {
            mg_tri =  1.0f - 2.0f * ((inst->mg_phase - peak) / (1.0f - peak));
        }

        /* Pulse output: duty cycle from mg_pw.
         *   pw=0 → wide (almost-always-high)
         *   pw=0.5 → square (50% duty)
         *   pw=1 → narrow (almost-always-low)
         * We use 1-pw so the user's mental model (pw=0=wide) maps to a
         * threshold at high phase value. */
        float pw_thresh = clampf(1.0f - inst->mg_pw, 0.05f, 0.95f);
        float mg_sq = (inst->mg_phase < pw_thresh) ? 1.0f : -1.0f;

        /* Global MG Depth (root-page knob) + mod-wheel→MG. Scales BOTH MG
         * outputs before they reach any attenuator or patchbay destination.
         * (Previously p_mg_depth was stored but never read — MG Depth was dead.) */
        float mg_depth_tgt = clampf(inst->p_mg_depth + inst->mw_mg * inst->mod_wheel, 0.0f, 1.0f);
        inst->sm_mg_depth += SMOOTH_20MS * (mg_depth_tgt - inst->sm_mg_depth);
        mg_tri *= inst->sm_mg_depth;
        mg_sq  *= inst->sm_mg_depth;

        /* S&H */
        inst->sh_phase += sh_rate_hz * INV_SR;
        if (inst->sh_phase >= 1.0f) {
            inst->sh_phase -= 1.0f;
            inst->sh_target = rand_b(&inst->rng);
        }
        inst->sh_value += sh_coef * (inst->sh_target - inst->sh_value);
        float sh = inst->sh_value;

        /* Slow filtered noise (modulation source — separate from audio noise) */
        float noise_mod = rand_b(&inst->rng);
        inst->sh_smoothed += 0.001f * (noise_mod - inst->sh_smoothed);
        float noise_smoothed = inst->sh_smoothed * 3.0f;

        /* === EG TRIG IN jacks — rising-edge retrigger (MS-20 FS panel feature) ===
         * EG 1 TRIG IN retriggers EG 1 only; EG 1+2 TRIG IN retriggers both.
         * Source values are 0..1 gates; we track previous-sample value and fire
         * on 0→1 transitions. The keyboard still triggers normally — these are
         * ADDITIONAL trigger sources, matching MS-20 semi-modular semantics. */
        float eg1_trig  = jack_eg_trig(inst, inst->pb_jack[9],  sh, noise_smoothed);
        float eg12_trig = jack_eg_trig(inst, inst->pb_jack[10], sh, noise_smoothed);
        if (eg1_trig > 0.5f && inst->pb_eg1_trig_prev <= 0.5f) {
            /* Click-free retrigger: if envelope is still audible, restart
             * Attack from its current level (skipping Delay). Same legato
             * pattern as keyboard note-on retrigger. */
            if (v->active && v->e1 > 0.001f) {
                v->e1_stage = 1;
            } else {
                v->e1 = 0.0f; v->e1_stage = 0;
            }
            v->e1_timer = 0.0f;
            v->active = 1;
        }
        if (eg12_trig > 0.5f && inst->pb_eg12_trig_prev <= 0.5f) {
            if (v->active && v->e1 > 0.001f) { v->e1_stage = 1; }
            else                              { v->e1 = 0.0f; v->e1_stage = 0; }
            if (v->active && v->e2 > 0.001f) { v->e2_stage = 1; }
            else                              { v->e2 = 0.0f; v->e2_stage = 0; }
            v->e1_timer = 0.0f;
            v->e2_timer = 0.0f;
            v->active = 1;
        }
        inst->pb_eg1_trig_prev  = eg1_trig;
        inst->pb_eg12_trig_prev = eg12_trig;

        /* === Envelopes ===
         * Run while voice is active. Clear `active` ONLY when EG2 reaches
         * stage 5 (idle, level=0). The previous gate had two bugs:
         *   1. `v->active || v->e2_stage != 4` was true on every sample of
         *      release (active=1 + stage=4), so envelopes stepped once...
         *   2. ...then the inner `if (stage==4) active=0` cleared active
         *      mid-release. Next sample the gate failed (active=0 AND
         *      stage==4), envelopes never advanced again, and the voice
         *      rang at ~0.7 forever. Test for stage==5 (idle) instead.
         *
         * env_time_scale (per-instance, ±2.5%) models EG cap tolerance —
         * two units with identical preset have subtly different envelopes,
         * matching real MS-20 production spread. */
        if (v->active) {
            float ets = drift_env_time_scale(&inst->drift);
            env1_step_dar(v, inst->e1_delay * ets, inst->e1_atk * ets, inst->e1_rel * ets);
            env2_step_hadsr(v, inst->e2_hold * ets, inst->e2_atk * ets,
                            inst->e2_dcy * ets, inst->e2_sus, inst->e2_rel * ets);
            if (v->e2_stage == 5) v->active = 0;
        }

        /* === Patchbay resolution (v0.2 source-side jack model) === */
        float esp_aud = inst->esp_audio_last;
        /* Each jack returns the signal flowing through one specific MS-20 input.
         * These feed the panel attenuators (HPF MG, LPF EG, etc.) — the panel
         * knobs control intensity, the jack just substitutes the source.
         * Default jack=0 returns the hard-wired MS-20 default for that jack. */
        float jack_total_sig    = jack_total(inst, inst->pb_jack[1], mg_tri, sh, noise_smoothed);
        float jack_vco_freq_sig = jack_vco_freq(inst, inst->pb_jack[2], sh, noise_smoothed);
        float jack_hpf_cv_sig   = jack_hpf_cv(inst, inst->pb_jack[3], sh, noise_smoothed);
        float jack_lpf_cv_sig   = jack_lpf_cv(inst, inst->pb_jack[4], sh, noise_smoothed);
        float jack_vca_init     = jack_vca_initial(inst, inst->pb_jack[5], sh, noise_smoothed);
        float jack_pwm_sig      = jack_pwm(inst, inst->pb_jack[6], mg_tri, mg_sq, sh, noise_smoothed);
        /* mixer_out for ext_sig feedback resolved AFTER the mix is computed below */

        /* === Oscillators === */
        /* Compute frequencies with bend, fine, drift, patchbay.
         * Drift output is in cents (divided by 100 → semitones). The per-VCO
         * v1_drift/v2_drift trim knobs scale the drift contribution so users
         * can dial different amounts on each oscillator (Diva-style). */
        float drift_v1_semi = drift_pitch_v1(&inst->drift) * inst->v1_drift / 100.0f;
        float drift_v2_semi = drift_pitch_v2(&inst->drift) * inst->v2_drift / 100.0f;
        /* Master Tune (±100 cents, MS-20 panel knob) — applied to both VCOs equally. */
        float master_semi = inst->p_master_tune;  /* ±1 knob = ±1 semitone = ±100 cents */

        /* MS-20 VCO MIXER FREQ MOD attenuators — these are the panel knobs at
         * the bottom of the VCO MIXER column on the original FS panel. They
         * scale the patchbay TOTAL and FREQ jack signals on their way to
         * BOTH VCOs' pitch input. Independent from the HPF/LPF mod knobs.
         * In MS-10 mode the EG1 path is zeroed (MS-10 has only the HADSR). */
        float vco_mod_mgtext = inst->vco_mg_int * jack_total_sig;
        float vco_mod_eg1ext = inst->p_ms10_mode ? 0.0f
                                                 : inst->vco_eg_int * jack_vco_freq_sig;

        /* VCO 2 CV IN jack — separate Hz/V contribution to VCO 2 only (in
         * addition to whatever feeds VCO 1+2 via KBD CV). Signal is bipolar
         * ±1 → ±1 octave when fully present. */
        float jack_vco2_cv_sig = jack_vco2_cv(inst, inst->pb_jack[8], sh, noise_smoothed);

        /* Smooth the static (knob) pitch at ~20 ms so turning Tune/Fine/Detune
         * or flipping the Scale octave glides like an analog VCO settling, while
         * pitch bend, drift and jack modulation stay immediate. */
        float v1_static = inst->v1_pitch + inst->v1_fine + master_semi;
        float v2_static = inst->v2_pitch + inst->v2_fine + inst->v2_detune + master_semi;
        inst->sm_v1_pitch += SMOOTH_20MS * (v1_static - inst->sm_v1_pitch);
        inst->sm_v2_pitch += SMOOTH_20MS * (v2_static - inst->sm_v2_pitch);
        float v1_semi = inst->sm_v1_pitch + inst->pitch_bend + drift_v1_semi;
        float v2_semi = inst->sm_v2_pitch + inst->pitch_bend + drift_v2_semi;
        /* Add jack-routed pitch modulation. The MG/T.EXT and EG1/EXT VCO
         * MIXER attenuators (vco_mg_int, vco_eg_int) already scale the
         * signals; bipolar ±1 × 12 = ±1 octave at attenuator=1. */
        v1_semi += (vco_mod_mgtext + vco_mod_eg1ext) * 12.0f;
        v2_semi += (vco_mod_mgtext + vco_mod_eg1ext) * 12.0f
                 + jack_vco2_cv_sig * 12.0f;

        float v1_freq = v->freq * fast_exp2(v1_semi * (1.0f / 12.0f));
        float v2_freq = v->freq * fast_exp2(v2_semi * (1.0f / 12.0f));
        if (inst->v2_xmod > 0.001f) {
            /* Cross-mod: VCO1 modulates VCO2 frequency */
            v2_freq *= 1.0f + inst->v2_xmod * 0.5f * sinf(inst->v1_pwm_phase * TWO_PI);
        }

        /* MS-10 mode: silence VCO 2 entirely (one-VCO synth) */
        if (inst->p_ms10_mode) {
            v2_freq = 0.0f;
        }

        float dt1 = v1_freq * INV_SR;
        float dt2 = v2_freq * INV_SR;

        /* Advance phases.
         * Per-sample phase jitter models thermal noise in the 2SC1685S exp
         * converter's integrator caps (see drift.h) — adds the "dust" that
         * distinguishes analog VCO from a perfect sample-clock digital one.
         * The jitter amount is below pitch-perception threshold individually
         * but dithers integrator state so the K35 filter never sees a perfectly
         * periodic input — it's why real analog stuff doesn't feel "frozen". */
        float jit1 = drift_phase_jitter(&inst->drift);
        float jit2 = drift_phase_jitter(&inst->drift);
        v->phase1 += dt1 + jit1;
        if (v->phase1 >= 1.0f) { v->phase1 -= 1.0f; v->last_p1_wrap = 1; } else { v->last_p1_wrap = 0; }
        if (v->phase1 < 0.0f)  { v->phase1 += 1.0f; }  /* guard against negative jitter at low freq */

        v->phase2 += dt2 + jit2;
        if (v->phase2 >= 1.0f) v->phase2 -= 1.0f;
        if (v->phase2 < 0.0f)  v->phase2 += 1.0f;

        /* Hard sync: reset phase2 when VCO1 wraps and the v2_sync switch is on */
        if (inst->v2_sync && v->last_p1_wrap) {
            v->phase2 = 0.0f;
        }

        /* Sub-osc: divide-by-2 on VCO1 */
        if (v->last_p1_wrap) inst->v1_pwm_phase = 0.0f;
        inst->v1_pwm_phase += dt1 * 0.5f;
        if (inst->v1_pwm_phase >= 1.0f) inst->v1_pwm_phase -= 1.0f;
        float sub = (inst->v1_pwm_phase < 0.5f) ? 1.0f : -1.0f;

        /* PW with PWM mod — jack_pwm_sig is the MS-20 PWM IN jack signal. */
        float pw1 = inst->v1_pw + inst->v1_pwm_depth * mg_tri * 0.4f + jack_pwm_sig * 0.3f;
        float pw2 = inst->v2_pw + inst->v1_pwm_depth * mg_tri * 0.4f + jack_pwm_sig * 0.3f;

        /* === VCO 1 — wave map matches V1_WAVE_NAMES order ===
         *   0 Tri        → osc_render(0)
         *   1 Saw        → osc_render(1)
         *   2 PW-Square  → osc_render(2) with variable PW
         *   3 Noise      → noise generator (bypasses oscillator) */
        float osc1;
        if (inst->v1_wave == 3) {
            osc1 = (inst->noise_color == 0) ? rand_b(&inst->rng) : pink_noise(inst);
        } else {
            osc1 = osc_render(inst->v1_wave, v->phase1, dt1, pw1);
        }

        /* === VCO 2 — wave map matches V2_WAVE_NAMES order (different from V1) ===
         *   0 Saw    → osc_render(1)
         *   1 Square → osc_render(2) with fixed 50% PW
         *   2 Pulse  → osc_render(2) with variable PW (the MS-20 mini Kit "PW" knob)
         *   3 Ring   → osc_render(1) under-saw, then the V1*V2 ring product replaces
         *              osc2 in the mix below. Ring needs a non-zero osc2 to multiply. */
        float osc2;
        if (inst->p_ms10_mode) {
            osc2 = 0.0f;
        } else {
            switch (inst->v2_wave) {
                case 0:  osc2 = osc_render(1, v->phase2, dt2, pw2);    break; /* Saw */
                case 1:  osc2 = osc_render(2, v->phase2, dt2, 0.5f);   break; /* Square */
                case 2:  osc2 = osc_render(2, v->phase2, dt2, pw2);    break; /* Pulse */
                case 3:  osc2 = osc_render(1, v->phase2, dt2, pw2);    break; /* Ring (under-saw) */
                default: osc2 = osc_render(1, v->phase2, dt2, pw2);    break;
            }
        }

        /* === Mixer === */
        float ring  = osc1 * osc2;
        float noise = (inst->noise_color == 0) ? rand_b(&inst->rng) : pink_noise(inst);
        float esp_aud_in = (audio_in) ? esp_aud * inst->esp_aud_mix : 0.0f;

        /* When v2_wave==Ring, the ring product REPLACES osc2's direct
         * contribution to the mix (previously the code added ring on top of
         * the Rev-Saw osc2 output, which double-counted and sounded muddy). */
        float osc2_mix = (inst->v2_wave == 3) ? ring : osc2;

        float mix = osc1 * inst->mix_v1
                  + osc2_mix * inst->mix_v2
                  + sub  * inst->mix_sub
                  + noise * inst->mix_noise
                  + esp_aud_in * inst->mix_esp
                  + inst->fb_sample * inst->mix_fb * 0.5f;

        /* === Drive (pre-filter) — MS-20 magic lives here ===
         * Use sm_drive (block-rate smoothed) instead of p_drive so the
         * tanh saturation level doesn't pop when the knob is turned. */
        /* Pre-gain into the saturator grows harmonic content; the makeup term
         * keeps loudness roughly constant so Drive colors rather than just gets
         * louder. The old /fast_tanh(drive_amt) normalization cancelled the
         * effect at the low input levels typical of a single oscillator. */
        float drive_amt = 1.0f + inst->sm_drive * 9.0f;
        float driven = fast_tanh(mix * drive_amt) * (0.6f + 0.4f / drive_amt);

        /* === Filter === */
        /* Per-sample smoothed cutoff (with modulation + analog drift offset).
         * The MS-20 has separate panel attenuators for each filter:
         *   HPF MG (×T.EXT signal) + HPF EG (×EG2/EXT signal)
         *   LPF MG (×T.EXT signal) + LPF EG (×EG2/EXT signal)
         * Jack signals substitute the source feeding each attenuator. */
        float drift_lpf_off = drift_lpf(&inst->drift);
        float drift_hpf_off = drift_hpf(&inst->drift);

        /* Per-filter MG/EG modulation using v0.2 source-side jacks */
        float lpf_mod = inst->lpf_mg_int * jack_total_sig
                      + inst->lpf_eg_int * jack_lpf_cv_sig;
        float hpf_mod = inst->hpf_mg_int * jack_total_sig
                      + inst->hpf_eg_int * jack_hpf_cv_sig;

        /* Smooth the base cutoff/reso KNOBS at ~20 ms for an analog feel, THEN
         * add the envelope/LFO/drift modulation unsmoothed so filter sweeps and
         * the MG stay snappy. (The old code smoothed the fully-summed target at
         * ~0.16 ms — no real analog glide on the knob, and it would have dulled
         * the envelope if simply slowed down.) */
        inst->sm_lpf_cut  += SMOOTH_20MS * (inst->lpf_cut  - inst->sm_lpf_cut);
        inst->sm_hpf_cut  += SMOOTH_20MS * (inst->hpf_cut  - inst->sm_hpf_cut);
        inst->sm_lpf_reso += SMOOTH_20MS * (inst->lpf_reso - inst->sm_lpf_reso);
        inst->sm_hpf_reso += SMOOTH_20MS * (inst->hpf_reso - inst->sm_hpf_reso);

        float lpf_cut_target = clampf(inst->sm_lpf_cut + kt * 0.5f
                                      + lpf_mod
                                      + inst->mod_wheel * inst->mw_filt * 0.5f
                                      + drift_lpf_off, 0.0f, 1.0f);
        float hpf_cut_target = clampf(inst->sm_hpf_cut + hpf_mod + drift_hpf_off, 0.0f, 1.0f);

        float lpf_hz = clampf(cutoff_to_hz(lpf_cut_target), 20.0f, 18000.0f);
        float hpf_hz = clampf(cutoff_to_hz(hpf_cut_target), 20.0f, 18000.0f);

        /* Ext Sig jack — when patched, audio from the selected source (ESP/Noise/
         * mixer feedback) is summed into the filter input. Default 0 = no extra.
         * jack_ext_sig needs mixer_out, so we use the current driven signal. */
        float ext_sig = jack_ext_sig(inst, inst->pb_jack[7], esp_aud_in, driven, noise, i);
        float filt_in = driven + ext_sig * 0.5f;

        /* MS-10 mode: force LP-only filter routing (bypass HPF — MS-10 has no HPF).
         * The original MS-10 also used the KORG-35 only — leave filter_rev choice
         * to the user but document this in CLAUDE.md. */
        int effective_filter_mode = inst->p_ms10_mode ? 1 : inst->filter_mode;

        float filt_out = filt_in;
        switch (effective_filter_mode) {
            case 0: { /* HP+LP cascade — classic MS-20 routing */
                filt_out = filter_hpf_dispatch(inst->filter_rev, &inst->k35_hpf_state, filt_out, hpf_hz, inst->sm_hpf_reso, SAMPLE_RATE);
                filt_out = filter_lpf_dispatch(inst->filter_rev, &inst->k35_lpf_state, filt_out, lpf_hz, inst->sm_lpf_reso, SAMPLE_RATE);
                break;
            }
            case 1: filt_out = filter_lpf_dispatch(inst->filter_rev, &inst->k35_lpf_state, filt_out, lpf_hz, inst->sm_lpf_reso, SAMPLE_RATE); break;
            case 2: filt_out = filter_hpf_dispatch(inst->filter_rev, &inst->k35_hpf_state, filt_out, hpf_hz, inst->sm_hpf_reso, SAMPLE_RATE); break;
            case 3: { /* Notch — parallel HPF + LPF summed (approximation; sounds like a "smile") */
                float lp = filter_lpf_dispatch(inst->filter_rev, &inst->k35_lpf_state, filt_in, lpf_hz, inst->sm_lpf_reso, SAMPLE_RATE);
                float hp = filter_hpf_dispatch(inst->filter_rev, &inst->k35_hpf_state, filt_in, hpf_hz, inst->sm_hpf_reso, SAMPLE_RATE);
                filt_out = lp + hp;
                break;
            }
        }

        /* === VCA ===
         * MS-20 INITIAL GAIN jack: positive offset summed with EG2's contribution.
         * Per spec the jack has no attenuator, but in practice values >1 push
         * the chain into hard clipping (final tanh can't soak it all). Clamp
         * the sum so we stay inside the soft-saturation sweet spot. */
        /* Velocity → amplitude. A sqrt curve perceptually flattens the response
         * so soft hits don't collapse; VEL_FLOOR keeps the quietest hit audible;
         * p_vel_amt blends between "velocity ignored" (0 → every note full
         * level) and full dynamic range (1). At depth 1 the range is
         * VEL_FLOOR..1 (~-12 dB..0); the old code was a raw linear ×velocity. */
        float vshaped  = VEL_FLOOR + (1.0f - VEL_FLOOR) * sqrtf(v->velocity);
        float vel_gain = 1.0f - inst->p_vel_amt * (1.0f - vshaped);
        float vca_target = clampf(v->e2 + jack_vca_init, 0.0f, 1.0f) * vel_gain;
        /* VCA drift: subtle ±4% gain wander (additive on the multiplicative gain).
         * Models slight changes in VCA gain due to power supply / thermal drift. */
        vca_target *= 1.0f + drift_vca(&inst->drift);
        /* Click-free transitions: one-pole LP on the VCA gain. Coefficient 0.10
         * per sample = ~22 samples (~0.5 ms) time constant at 44.1 kHz. This
         * smears the slope discontinuities at envelope stage boundaries
         * (Hold→Attack, Attack→Decay, Decay→Sustain, Sustain→Release) below
         * audibility, while preserving the envelope's musical timing. */
        v->sm_vca += 0.10f * (vca_target - v->sm_vca);
        inst->sm_volume += SMOOTH_20MS * (inst->p_volume - inst->sm_volume);
        float final = filt_out * v->sm_vca * inst->sm_volume;

        /* === Output stage — component modeling for vintage character ===
         * Three subtle stages applied in series before the safety limiter:
         *   1. DC blocker (~5 Hz HPF): removes any DC offset from the
         *      saturation chain so it doesn't bias the transformer stage.
         *   2. V72 transformer: HP-coupled tanh on the >35 Hz residue
         *      adds "iron" warmth on transients while leaving steady-state
         *      bass linear. Adapted from KrautDrums Attitude chain.
         *   3. Gentle HF LPF (~12 kHz): the MS-20's output stage rolls off
         *      well before Nyquist; without this Aphex sounds too sterile. */
        /* 1. DC blocker — LP residue method, coef = 1−exp(−2π·5/44100) */
        inst->out_dc_state += 0.000712f * (final - inst->out_dc_state);
        final -= inst->out_dc_state;
        /* 2. V72 transformer — HP-couple, saturate HF residue, recombine */
        float xfmr_hp = final - inst->out_xfmr_state;
        inst->out_xfmr_state += 0.005f * xfmr_hp;
        final = fast_tanh(xfmr_hp * 1.2f) * 0.6f + inst->out_xfmr_state;
        /* 3. HF rolloff — coef = 1−exp(−2π·12000/44100) ≈ 0.821 */
        inst->out_lpf_state += 0.821f * (final - inst->out_lpf_state);
        final = inst->out_lpf_state;

        /* Final safety saturation (the user's "post-amp limiter") */
        final = fast_tanh(final * 1.1f) * 0.9f;

        /* Capture for FB next sample (post-everything → analog character
         * is part of the feedback loop, the MS-20 way) */
        inst->fb_sample = final;

        int16_t s = (int16_t)(clampf(final, -1.0f, 1.0f) * 32767.0f);
        out_lr[i*2]     = s;
        out_lr[i*2+1]   = s;
    }
}

/* ── API v2 export ───────────────────────────────────────────────────────
 * plugin_api_v2_t is defined in host/plugin_api_v1.h — do NOT redefine it
 * locally (the layout MUST match what the host expects). */

__attribute__((visibility("default")))
plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    static plugin_api_v2_t api;
    api.api_version      = 2;
    api.create_instance  = create_instance;
    api.destroy_instance = destroy_instance;
    api.on_midi          = on_midi;
    api.set_param        = set_param;
    api.get_param        = get_param;
    api.get_error        = NULL;
    api.render_block     = render_block;
    return &api;
}
