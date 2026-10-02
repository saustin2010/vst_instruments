/*
 * Chordism DSP Plugin for Schwung (Move Anything)
 *
 * MIT License. See LICENSE.
 *
 * Milestone 3a: 4-voice sine chord at fixed intervals
 * (root, M3, P5, octave). Mono last-note priority with
 * clean voice-steal release.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

extern "C" {

#define MOVE_PLUGIN_API_VERSION_2 2

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

}

static const host_api_v1_t *g_host = nullptr;

/* Runtime sample rate. Defaults to 44100 (the Move host rate). The host's
 * actual rate, if provided via host_api_v1.sample_rate, is applied in
 * v2_create_instance before any rate-derived state is computed — so AUv3 /
 * standalone hosts running at 48k get native-rate tuning with no resampling.
 * All rate-derived values (LFO/vibrato increments, envelope times, filter
 * coefficients) are computed at function scope from this, so updating it
 * before instance creation is sufficient; there are no precomputed SR tables. */
static float SAMPLE_RATE = 44100.0f;
static const float TWO_PI = 6.28318530717958647692f;

static const int   CHORD_SIZE = 4;
static const int   NUM_VOICES = 16;  /* 4 banks of CHORD_SIZE — release tails finish before steal */
static const int   HELD_STACK_MAX = 16;

/* Scale quantizer LUTs — semitone offsets from scale root, ascending.
 * Each row's last value is the row count (so we can pack variable-length
 * scales in a fixed-width array). */
#define MAX_SCALE_NOTES 12
static const int SCALE_TABLE[25][MAX_SCALE_NOTES + 1] = {
    /* Chromatic */         { 0,1,2,3,4,5,6,7,8,9,10,11, 12 },
    /* Major (Ionian) */    { 0,2,4,5,7,9,11,0,0,0,0,0, 7 },
    /* Natural minor */     { 0,2,3,5,7,8,10,0,0,0,0,0, 7 },
    /* Harmonic minor */    { 0,2,3,5,7,8,11,0,0,0,0,0, 7 },
    /* Pentatonic major */  { 0,2,4,7,9,0,0,0,0,0,0,0, 5 },
    /* Pentatonic minor */  { 0,3,5,7,10,0,0,0,0,0,0,0, 5 },
    /* Diminished (W-H) */  { 0,2,3,5,6,8,9,11,0,0,0,0, 8 },
    /* Dorian */            { 0,2,3,5,7,9,10,0,0,0,0,0, 7 },
    /* Phrygian */          { 0,1,3,5,7,8,10,0,0,0,0,0, 7 },
    /* Lydian */            { 0,2,4,6,7,9,11,0,0,0,0,0, 7 },
    /* Mixolydian */        { 0,2,4,5,7,9,10,0,0,0,0,0, 7 },
    /* Locrian */           { 0,1,3,5,6,8,10,0,0,0,0,0, 7 },
    /* Blues major */       { 0,2,3,4,7,9,0,0,0,0,0,0, 6 },
    /* Blues minor */       { 0,3,5,6,7,10,0,0,0,0,0,0, 6 },
    /* Arabic */            { 0,2,4,5,6,8,10,0,0,0,0,0, 7 },
    /* Arabic (h.mix) */    { 0,1,4,5,7,8,10,0,0,0,0,0, 7 },
    /* Arabic (Hijaz) */    { 0,1,4,5,7,8,11,0,0,0,0,0, 7 },
    /* Iwato (Japanese) */  { 0,1,5,6,10,0,0,0,0,0,0,0, 5 },
    /* Pelog (Gamelan) */   { 0,1,3,7,8,0,0,0,0,0,0,0, 5 },
    /* Slendro (Gamelan) */ { 0,2,5,7,10,0,0,0,0,0,0,0, 5 },
    /* Folk */              { 0,2,3,7,8,10,0,0,0,0,0,0, 6 },
    /* Japanese */          { 0,1,5,7,8,0,0,0,0,0,0,0, 5 },
    /* Gypsy */             { 0,2,3,6,7,8,11,0,0,0,0,0, 7 },
    /* Flamenco */          { 0,1,3,4,5,7,8,11,0,0,0,0, 8 },
    /* Whole tone */        { 0,2,4,6,8,10,0,0,0,0,0,0, 6 },
};
static const int NUM_SCALES = 25;
/* Quantize a MIDI note to the nearest scale degree on the same octave or
 * above. Snaps UP to ensure chord intervals stay in voicing. */
static int scale_quantize(int note, int scale_idx, int scale_root) {
    if (scale_idx < 0) scale_idx = 0;
    if (scale_idx >= NUM_SCALES) scale_idx = NUM_SCALES - 1;
    const int *scale = SCALE_TABLE[scale_idx];
    int count = scale[MAX_SCALE_NOTES];
    if (count <= 0) return note;
    int rel = note - scale_root;
    int octave = (rel >= 0) ? (rel / 12) : ((rel - 11) / 12);
    int pc = rel - octave * 12;          /* pitch class within octave 0..11 */
    /* Find nearest scale degree (snap up if between). */
    int best_pc = scale[0];
    int best_dist = 99;
    for (int i = 0; i < count; ++i) {
        int d = pc - scale[i];
        if (d < 0) d = -d;
        if (d < best_dist) {
            best_dist = d;
            best_pc = scale[i];
        }
    }
    return scale_root + octave * 12 + best_pc;
}

/* Chord LUT — chord types. Each row is CHORD_SIZE semitone offsets
 * from the root note (in MIDI semitones, so 12 = octave). */
enum ChordType {
    CHORD_UNISON_OCTAVES = 0,
    CHORD_FIFTH,
    CHORD_MINOR,
    CHORD_MINOR_7,
    CHORD_MINOR_9,
    CHORD_MINOR_11,
    CHORD_MAJOR,
    CHORD_MAJOR_7,
    CHORD_MAJOR_9,
    CHORD_SUS_4,
    CHORD_SIX_NINE,
    CHORD_MINOR_6,
    CHORD_TENTH,
    CHORD_DOMINANT_7,
    CHORD_DOMINANT_7_B9,
    CHORD_HALF_DIMINISHED,
    NUM_CHORDS
};

static const int CHORD_TABLE[NUM_CHORDS][CHORD_SIZE] = {
    { 0, 12, 24, 36 },   /* Unison/Octaves */
    { 0,  7, 12, 19 },   /* Fifth */
    { 0,  3,  7, 12 },   /* Minor */
    { 0,  3,  7, 10 },   /* Minor 7 */
    { 0,  3,  7, 14 },   /* Minor 9 */
    { 0,  3,  7, 17 },   /* Minor 11 */
    { 0,  4,  7, 12 },   /* Major */
    { 0,  4,  7, 11 },   /* Major 7 */
    { 0,  4,  7, 14 },   /* Major 9 */
    { 0,  5,  7, 12 },   /* Suspended 4 */
    { 0,  4,  9, 14 },   /* 6/9 */
    { 0,  3,  7,  9 },   /* Minor 6 */
    { 0,  4,  7, 16 },   /* 10th */
    { 0,  4,  7, 10 },   /* Dominant 7 */
    { 0,  4,  7, 13 },   /* Dominant 7 / b9 */
    { 0,  3,  6, 10 },   /* Half Diminished */
};

/* Attack range: 1 ms .. 4 s, linear ramp.
 * Release range: 5 ms .. 4 s, exponential decay (asymptotic). */
static const float ATTACK_MIN_S = 0.001f;
static const float ATTACK_MAX_S = 16.0f;
static const float RELEASE_MIN_S = 0.001f;
static const float RELEASE_MAX_S = 16.0f;

/* Exponential (perceptual) time mapping for envelope stages: a knob from 0..1
 * maps geometrically across [min,max], so most of the travel sits in the short
 * end where the ear is sensitive and the very top reaches long pad times. This
 * replaces the old linear lerp, which made every mid setting feel the same. */
static inline float env_time_exp(float v01, float min_s, float max_s) {
    if (v01 <= 0.0f) return min_s;
    if (v01 >= 1.0f) return max_s;
    return min_s * powf(max_s / min_s, v01);
}

/* Silence threshold below which the envelope clamps to 0 and the voice idles. */
static const float ENV_SILENCE = 1e-4f;

/* Detune max — applied as (chord_step_index * detune * MAX_DETUNE_CENTS).
 * 50 cents per step means voice 3 can be up to 150 cents (1.5 semitones)
 * sharp of voice 0 at full detune. */
static const float MAX_DETUNE_CENTS = 50.0f;

/* Pan morph LUT — 16 hand-authored 4-voice pan rows. Each value is -1
 * (hard left) to +1 (hard right). Added on top of width-based spread. */
static const float PAN_MORPH_LUT[16][CHORD_SIZE] = {
    {  0.0f,  0.0f,  0.0f,  0.0f },  /* 0  all center */
    { -1.0f, -0.3f,  0.3f,  1.0f },  /* 1  L→R ramp */
    {  1.0f,  0.3f, -0.3f, -1.0f },  /* 2  R→L ramp */
    { -1.0f,  1.0f, -1.0f,  1.0f },  /* 3  alt LRLR */
    {  1.0f, -1.0f,  1.0f, -1.0f },  /* 4  alt RLRL */
    { -1.0f, -1.0f,  1.0f,  1.0f },  /* 5  LL/RR split */
    { -1.0f,  0.0f,  0.0f,  1.0f },  /* 6  outer extremes */
    {  0.0f, -1.0f,  1.0f,  0.0f },  /* 7  inner extremes */
    { -0.5f,  0.5f, -0.5f,  0.5f },  /* 8  zigzag */
    {  0.5f, -0.5f,  0.5f, -0.5f },  /* 9  inv zigzag */
    { -1.0f, -0.5f,  0.5f,  1.0f },  /* 10 wide ramp */
    {  0.0f, -1.0f,  0.0f,  1.0f },  /* 11 L kick / R kick */
    {  0.0f,  1.0f,  0.0f, -1.0f },  /* 12 R kick / L kick */
    { -1.0f,  1.0f,  0.0f,  0.0f },  /* 13 first two outer */
    {  0.0f,  0.0f,  1.0f, -1.0f },  /* 14 last two outer */
    {  0.7f, -0.7f, -0.7f,  0.7f },  /* 15 outside-in */
};

/* Level morph LUT — 16 hand-authored 4-voice gain rows, musically useful
 * variations of the chord mix. morph_index linearly interpolates between
 * adjacent rows; morph_intensity blends the row toward "all 1.0" (flat). */
static const int NUM_LEVEL_MORPHS = 16;
static const float LEVEL_MORPH_LUT[NUM_LEVEL_MORPHS][CHORD_SIZE] = {
    {1.0f, 1.0f, 1.0f, 1.0f},  /* 0  all equal */
    {0.2f, 0.5f, 0.8f, 1.0f},  /* 1  ramp up */
    {1.0f, 0.8f, 0.5f, 0.2f},  /* 2  ramp down */
    {1.0f, 0.0f, 0.0f, 0.0f},  /* 3  root only */
    {0.0f, 0.0f, 0.0f, 1.0f},  /* 4  top only */
    {1.0f, 0.0f, 0.0f, 1.0f},  /* 5  root + top */
    {0.0f, 1.0f, 1.0f, 0.0f},  /* 6  inner pair */
    {1.0f, 0.0f, 1.0f, 0.0f},  /* 7  odd */
    {0.0f, 1.0f, 0.0f, 1.0f},  /* 8  even */
    {0.1f, 0.3f, 0.7f, 1.0f},  /* 9  log up */
    {0.3f, 1.0f, 1.0f, 0.3f},  /* 10 middle-peak triangle */
    {1.0f, 0.3f, 0.3f, 1.0f},  /* 11 inv-triangle */
    {0.0f, 0.0f, 0.5f, 1.0f},  /* 12 top-heavy */
    {1.0f, 0.7f, 0.3f, 0.0f},  /* 13 bottom-heavy */
    {1.0f, 1.0f, 0.5f, 0.5f},  /* 14 low pair */
    {0.5f, 0.5f, 1.0f, 1.0f},  /* 15 high pair */
};

/* Delay — 65536-sample ring buffer (~1.486 s @ 44.1k). Power-of-two for cheap
 * mask-based wrap. Per-channel (true stereo delay). */
static const int DELAY_BUFFER_SIZE = 65536;
static const int DELAY_BUFFER_MASK = DELAY_BUFFER_SIZE - 1;
static const float DELAY_TIME_MIN_S = 0.005f;
static const float DELAY_TIME_MAX_S = 1.4f;
static const float DELAY_FEEDBACK_MAX = 0.95f;

enum DelayMode {
    DELAY_STEREO = 0,
    DELAY_PINGPONG = 1,
    DELAY_FLIPFLOP = 2,
    DELAY_LONG = 3,         /* mono ≤ buffer length */
    DELAY_ZENITH = 4,       /* each repeat shifted +1 octave */
    DELAY_INTERVAL = 5      /* each repeat pitch-shifted by delay_mod_depth±12 semis */
};
static const int NUM_DELAY_MODES = 6;

enum EnvMode { ENVMODE_AD = 0, ENVMODE_ASR = 1, ENVMODE_LOOPING = 2 };
static const int NUM_ENV_MODES = 3;

/* Vibrato range. Speed exp-mapped 0.1..12 Hz, depth 0..100 cents, delay
 * linear 0..2 sec rise time (ramp from 0 to full depth after note-on). */
static const float VIB_SPEED_MIN_HZ = 0.1f;
static const float VIB_SPEED_MAX_HZ = 12.0f;
static const float VIB_DEPTH_MAX_CENTS = 100.0f;
static const float VIB_DELAY_MAX_S = 2.0f;
static const float ONE_OVER_1200 = 1.0f / 1200.0f;
static const float ONE_OVER_12 = 1.0f / 12.0f;

/* Pitch sweep: ±12 semitones max offset at amount=±1. Rate sets exp decay
 * time toward 0. */
static const float SWEEP_MAX_SEMITONES = 12.0f;
static const float SWEEP_TIME_MIN_S = 0.01f;
static const float SWEEP_TIME_MAX_S = 4.0f;

/* Glide: portamento time at rate=1.0 (rate=0 → snap). */
static const float GLIDE_TIME_MAX_S = 2.0f;

enum ArpDirection { ARP_UP = 0, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM };
static const int NUM_ARP_DIRECTIONS = 4;
static const float ARP_BPM_MIN = 30.0f;
static const float ARP_BPM_MAX = 240.0f;

/* Preset — snapshot of meaningful parameters for instant recall. */
struct Preset {
    const char *name;
    int chord_type;
    float chord_spread;
    float chord_rotation;
    float detune;
    float width;
    int waveforms[CHORD_SIZE];
    float shapes[CHORD_SIZE];
    float morph_index;
    float morph_intensity;
    float pan_morph_index;
    float pan_morph_intensity;
    int fm_modulator_idx;
    float fm_amount;
    float lfo_rate;
    float lfo_depth;
    int lfo_shape;
    float vib_depth;
    float vib_speed;
    float vib_delay;
    float sweep_amount;
    float sweep_rate;
    float glide_rate;
    float filter_cutoff;
    float filter_resonance;
    int filter_mode;
    int filter_slope;
    float filter_env_attack;
    float filter_env_decay;
    float filter_env_depth;
    float filter_lfo_rate;
    float filter_lfo_depth;
    float filter_lfo_spread;
    int filter_lfo_shape;
    float drive;
    float attack;
    float release;
    float volume;
    float reverb_mix;
    float reverb_decay;
    float reverb_damp;
    float delay_mix;
    float delay_time;
    float delay_feedback;
    float delay_tone;
    float grind;
    float bit_shift;
    float decimator;
    int arp_enabled;
    float arp_tempo;
    int arp_direction;
    int delay_mode;
    int vib_stray;
    int glide_legato;
    /* v0.3.3 fields */
    float lm_lfo_rate;
    float lm_lfo_depth;
    int   lm_lfo_shape;
    float pm_lfo_rate;
    float pm_lfo_depth;
    int   pm_lfo_shape;
    int   scale_index;
    int   scale_root;
    /* v0.3.6 fields */
    int   vca_mode;
    int   vca_drone;
    int   fenv_mode;
    int   ctrl_source;
    float ctrl_to_cutoff;
    float ctrl_to_morph;
    float ctrl_to_vib;
    float ctrl_to_shape;
    float ctrl_to_fm;
    /* v0.3.10 fields */
    float amp_lfo_rate;
    float amp_lfo_depth;
    int   amp_lfo_shape;
    /* v0.3.11 fields — Chord Multi captured per preset */
    int   tuning_mode;     /* 0 Chord, 1 Interval, 2 Chord Multi */
    int   chord_pc[12];    /* per-pitch-class chord type (used when tuning_mode==2) */
    /* v0.3.11 — plate-reverb character, now captured per preset */
    float reverb_shimmer;    /* chorus/shimmer depth */
    float reverb_lowcut;   /* wet high-pass amount */
    float reverb_size;    /* tail size */
    float reverb_mod_rate; /* chorus rate */
    float reverb_mod_depth;/* extra chorus depth */
};

static const Preset PRESETS[] = {
/*  name           chord  spread rot detune width  waveforms          shapes              morphI morphIn panI panIn fmM fmA  lfoR lfoD lfoS vibD vibS vibDly swpA swpR gld  cutoff reso mode slope fEnvA fEnvD fEnvAmt fLfoR fLfoD fLfoS fLfoSh drive  A     R     vol   rvMix rvDec rvDmp dlMix dlT  dlFb dlTn grnd bShf dec   arp tempo dir dlyMd vStray glLeg
*/
{"Init", 6, 0.5f, 0.0f, 0.0f, 1.0f, {1,1,1,1}, {0,0,0,0}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0, 0, 0.0000f, 0.7327f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5478f, 0.7327f, 0.8f, 0.0f, 0.5f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Lush Pad", 8, 0.5f, 0.0f, 0.30f, 1.0f, {3,3,3,3}, {0.4f,0.4f,0.4f,0.4f}, 0.4f, 0.5f, 0.5f, 0.6f, 0, 0.0f, 0.15f, 0.4f, 0, 0.3f, 0.3f, 0.3f, 0.0f, 0.5f, 0.4f, 0.5f, 0.4f, 0, 0, 0.6192f, 0.8041f, 0.3f, 0.05f, 0.3f, 0.3f, 0, 0.05f, 0.7137f, 0.8041f, 0.7f, 0.55f, 0.7f, 0.4f, 0.1f, 0.35f, 0.3f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"FM Bell", 10, 0.5f, 0.0f, 0.05f, 0.8f, {1,1,1,1}, {0,0,0,0}, 0.2f, 0.3f, 0.0f, 0.0f, 0, 0.40f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.7f, 0.0f, 0, 0, 0.0000f, 0.7853f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.7951f, 0.7f, 0.35f, 0.6f, 0.2f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Plucky Lead", 13, 0.5f, 0.0f, 0.0f, 0.6f, {3,3,3,3}, {0.2f,0.2f,0.2f,0.2f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.45f, 0.0f, 0, 0, 0.0000f, 0.7140f, 0.85f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.7327f, 0.7f, 0.2f, 0.5f, 0.4f, 0.15f, 0.4f, 0.45f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Filtered Sweep", 9, 0.5f, 0.0f, 0.0f, 0.8f, {3,3,3,3}, {0,0,0,0}, 0, 0, 0.4f, 0.4f, 0, 0.0f, 0.45f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.45f, 0.30f, 0, 0, 0.0000f, 0.7853f, 0.0f, 0.4f, 0.35f, 0.15f, 0, 0.10f, 0.5478f, 0.7623f, 0.6f, 0.4f, 0.7f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Ambient Drone", 0, 0.5f, 0.0f, 0.10f, 0.5f, {1,2,2,1}, {0.6f,0.6f,0.3f,0.3f}, 0.3f, 0.25f, 0.0f, 0.0f, 0, 0.0f, 0.10f, 0.3f, 0, 0.2f, 0.45f, 0.6f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0, 0, 0.0000f, 0.7853f, 0.0f, 0.10f, 0.4f, 0.5f, 0, 0.0f, 0.7852f, 0.8338f, 0.7f, 0.7f, 0.85f, 0.3f, 0.3f, 0.6f, 0.55f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Arp Bliss", 10, 0.5f, 0.0f, 0.0f, 0.9f, {3,3,3,3}, {0,0,0,0}, 0, 0, 0.3f, 0.4f, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0, 0, 0.0000f, 0.7853f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.7327f, 0.7f, 0.4f, 0.6f, 0.4f, 0.3f, 0.4f, 0.55f, 0.6f, 0.0f, 0.0f, 0.0f, 1, 0.75f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Lo-Fi Stab", 3, 0.5f, 0.0f, 0.0f, 0.7f, {3,3,3,3}, {0,0,0,0}, 0, 0, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.0f, 0, 0, 0.0000f, 0.7486f, 0.4f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.7140f, 0.7f, 0.2f, 0.5f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.5f, 0.3f, 0.5f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
/* === New in 0.2.2 === */
{"Glacial", 8, 0.5f, 0.0f, 0.20f, 0.6f, {3,3,3,3}, {0.3f,0.3f,0.3f,0.3f}, 0.3f, 0.4f, 0.2f, 0.3f, 0, 0.0f, 0.15f, 0.3f, 0, 0.1f, 0.3f, 0.6f, 0.0f, 0.5f, 0.0f, 0.45f, 0.2f, 0, 0, 0.7852f, 0.8200f, 0.10f, 0.12f, 0.25f, 0.7f, 0, 0.05f, 0.8200f, 0.8400f, 0.6f, 0.75f, 0.9f, 0.4f, 0.2f, 0.55f, 0.5f, 0.55f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Acid Lead", 1, 0.4f, 0.0f, 0.0f, 0.4f, {3,3,0,0}, {0.2f,0.2f,0,0}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.25f, 0.25f, 0.75f, 0, 1, 0.0000f, 0.7327f, 0.55f, 0.0f, 0.0f, 0.0f, 0, 0.35f, 0.0000f, 0.6910f, 0.6f, 0.20f, 0.5f, 0.3f, 0.20f, 0.4f, 0.55f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Sub Bass", 0, 0.4f, 0.0f, 0.0f, 0.15f, {1,0,0,0}, {0,0,0,0}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.10f, 0.65f, 0.0f, 0, 0, 0.0000f, 0.7327f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.7327f, 0.85f, 0.05f, 0.4f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"EP", 7, 0.5f, 0.0f, 0.05f, 0.5f, {1,1,1,1}, {0.45f,0.40f,0.40f,0.45f}, 0.2f, 0.3f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.50f, 0.12f, 0, 0, 0.0f, 0.50f, 0.55f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.004f, 0.74f, 0.7f, 0.40f, 0.6f, 0.4f, 0.1f, 0.4f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.66f, 0.28f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Synth Brass", 9, 0.5f, 0.0f, 0.10f, 0.7f, {3,3,3,3}, {0.1f,0.1f,0.1f,0.1f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.15f, 0, 0, 0.5478f, 0.7853f, 0.50f, 0.0f, 0.0f, 0.0f, 0, 0.15f, 0.5478f, 0.7486f, 0.7f, 0.35f, 0.55f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Underwater", 8, 0.6f, 0.0f, 0.15f, 0.7f, {1,2,2,1}, {0.5f,0.5f,0.5f,0.5f}, 0.5f, 0.5f, 0.4f, 0.7f, 0, 0.0f, 0.20f, 0.4f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.10f, 0.50f, 0.4f, 0, 0, 0.7325f, 0.8200f, 0.20f, 0.15f, 0.4f, 0.9f, 0, 0.0f, 0.7852f, 0.8200f, 0.6f, 0.55f, 0.7f, 0.4f, 0.35f, 0.5f, 0.6f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Trance Arp", 4, 0.5f, 0.0f, 0.05f, 0.8f, {3,3,3,3}, {0.2f,0.2f,0.2f,0.2f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.45f, 0.5f, 0, 1, 0.0000f, 0.7140f, 0.55f, 0.0f, 0.0f, 0.0f, 0, 0.20f, 0.0000f, 0.6615f, 0.6f, 0.30f, 0.5f, 0.4f, 0.35f, 0.3f, 0.55f, 0.7f, 0.0f, 0.0f, 0.0f, 1, 0.85f, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},

/* === New in v0.3.3 — showcasing v0.3.x features === */
{"Phrygian Pad", 3, 0.5f, 0.0f, 0.10f, 0.8f, {3,3,3,3}, {0.3f,0.3f,0.3f,0.3f}, 0.4f, 0.5f, 0.3f, 0.4f, 0, 0.0f, 0.10f, 0.3f, 0, 0.0f, 0.5f, 0.4f, 0.0f, 0.5f, 0.0f, 0.45f, 0.30f, 0, 0, 0.6192f, 0.8041f, 0.20f, 0.10f, 0.30f, 0.6f, 0, 0.05f, 0.7622f, 0.8124f, 0.65f, 0.55f, 0.7f, 0.4f, 0.10f, 0.4f, 0.4f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0.2f, 0.30f, 0, 0.15f, 0.20f, 0, 8, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Pentatonic Pluck", 5, 0.5f, 0.0f, 0.0f, 0.6f, {3,3,3,3}, {0.2f,0.2f,0.2f,0.2f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.20f, 0, 0, 0.0000f, 0.7140f, 0.70f, 0.0f, 0.0f, 0.0f, 0, 0.10f, 0.0000f, 0.7327f, 0.7f, 0.20f, 0.5f, 0.4f, 0.15f, 0.4f, 0.5f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Wavetable Drone", 1, 0.5f, 0.0f, 0.05f, 0.7f, {6,6,6,6}, {0.4f,0.5f,0.6f,0.7f}, 0.4f, 0.5f, 0.2f, 0.3f, 0, 0.0f, 0.05f, 0.2f, 0, 0.05f, 0.4f, 0.5f, 0.0f, 0.5f, 0.0f, 0.50f, 0.20f, 0, 0, 0.7325f, 0.8041f, 0.15f, 0.10f, 0.25f, 0.6f, 0, 0.0f, 0.6906f, 0.1663f, 0.55f, 0.65f, 0.85f, 0.3f, 0.25f, 0.5f, 0.5f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0.10f, 0.3f, 0, 0.10f, 0.3f, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Pulse Stab", 13, 0.5f, 0.0f, 0.0f, 0.5f, {5,5,5,5}, {0.3f,0.3f,0.3f,0.3f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.30f, 0.15f, 0, 1, 0.0000f, 0.6910f, 0.65f, 0.0f, 0.0f, 0.0f, 0, 0.25f, 0.0000f, 0.6201f, 0.65f, 0.15f, 0.4f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Aftertouch Wow", 8, 0.5f, 0.0f, 0.10f, 0.7f, {3,3,3,3}, {0.2f,0.2f,0.2f,0.2f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.20f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.40f, 0, 1, 0.0000f, 0.7853f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.10f, 0.5478f, 0.7623f, 0.65f, 0.35f, 0.6f, 0.4f, 0.10f, 0.4f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.40f, 0.0f, 0.30f, 0.20f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Tape Echo", 9, 0.5f, 0.0f, 0.05f, 0.7f, {3,3,3,3}, {0.2f,0.2f,0.2f,0.2f}, 0.2f, 0.3f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.55f, 0.30f, 0, 0, 0.0000f, 0.7853f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.15f, 0.5478f, 0.7486f, 0.65f, 0.40f, 0.6f, 0.4f, 0.40f, 0.5f, 0.6f, 0.4f, 0.10f, 0.05f, 0.2f, 0, 0.4f, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Cosmic Sweep", 10, 0.6f, 0.0f, 0.15f, 0.9f, {3,3,3,3}, {0.3f,0.3f,0.3f,0.3f}, 0.4f, 0.6f, 0.5f, 0.6f, 0, 0.0f, 0.10f, 0.3f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.40f, 0, 0, 0.7622f, 0.8338f, 0.30f, 0.20f, 0.5f, 0.85f, 0, 0.05f, 0.7325f, 0.8200f, 0.65f, 0.60f, 0.8f, 0.4f, 0.20f, 0.5f, 0.6f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 2, 0, 0, 0.20f, 0.4f, 0, 0.15f, 0.4f, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Dub Bass", 0, 0.4f, 0.0f, 0.0f, 0.3f, {3,3,0,0}, {0.2f,0.2f,0,0}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.22f, 0.30f, 0.50f, 0, 1, 0.0000f, 0.6910f, 0.40f, 0.0f, 0.0f, 0.0f, 0, 0.30f, 0.0000f, 0.7623f, 0.7f, 0.20f, 0.55f, 0.4f, 0.55f, 0.55f, 0.65f, 0.4f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Random Bleeps", 14, 0.4f, 0.0f, 0.0f, 0.6f, {1,5,4,5}, {0.4f,0.3f,0.5f,0.3f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.40f, 0, 1, 0.0000f, 0.6910f, 0.50f, 0.0f, 0.0f, 0.0f, 0, 0.15f, 0.0000f, 0.6910f, 0.6f, 0.25f, 0.55f, 0.4f, 0.30f, 0.4f, 0.6f, 0.6f, 0.0f, 0.0f, 0.0f, 1, 0.80f, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0.40f, 0.0f, 0.0f, 0.50f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Lo-Fi Pad", 3, 0.5f, 0.0f, 0.05f, 0.6f, {5,5,5,5}, {0.2f,0.2f,0.2f,0.2f}, 0.3f, 0.4f, 0.2f, 0.3f, 0, 0.0f, 0.10f, 0.3f, 0, 0.0f, 0.5f, 0.4f, 0.0f, 0.5f, 0.0f, 0.50f, 0.20f, 0, 0, 0.7325f, 0.8200f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.7622f, 0.8271f, 0.55f, 0.65f, 0.85f, 0.4f, 0.10f, 0.5f, 0.5f, 0.5f, 0.40f, 0.15f, 0.35f, 0, 0.4f, 0, 0, 0, 0, 0.10f, 0.3f, 0, 0.10f, 0.3f, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Gypsy Lead", 13, 0.5f, 0.0f, 0.0f, 0.6f, {3,3,3,3}, {0.2f,0.2f,0.2f,0.2f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.28f, 0.50f, 0.30f, 0, 1, 0.0000f, 0.7327f, 0.70f, 0.0f, 0.0f, 0.0f, 0, 0.15f, 0.0000f, 0.7623f, 0.7f, 0.30f, 0.6f, 0.4f, 0.20f, 0.4f, 0.5f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 22, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Coin Toss Lead", 2, 0.5f, 0.0f, 0.05f, 0.6f, {3,3,3,3}, {0.3f,0.3f,0.3f,0.3f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.45f, 0.30f, 0, 1, 0.0000f, 0.7327f, 0.55f, 0.0f, 0.0f, 0.0f, 0, 0.20f, 0.0000f, 0.7140f, 0.65f, 0.20f, 0.55f, 0.4f, 0.25f, 0.4f, 0.5f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0.0f, 0.0f, 0.0f, 0.70f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Flamenco Stab", 13, 0.5f, 0.0f, 0.05f, 0.7f, {3,3,3,3}, {0.15f,0.15f,0.15f,0.15f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.35f, 0.40f, 0, 1, 0.0000f, 0.6910f, 0.70f, 0.0f, 0.0f, 0.0f, 0, 0.30f, 0.0000f, 0.7140f, 0.65f, 0.30f, 0.55f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 23, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Looping Pulse", 1, 0.4f, 0.0f, 0.0f, 0.4f, {5,5,5,5}, {0.4f,0.4f,0.4f,0.4f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.30f, 0, 0, 0.5478f, 0.7327f, 0.40f, 0.0f, 0.0f, 0.0f, 0, 0.10f, 0.5478f, 0.6910f, 0.7f, 0.30f, 0.55f, 0.4f, 0.20f, 0.4f, 0.5f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Wavetable Glow", 12, 0.5f, 0.0f, 0.10f, 0.7f, {6,6,6,6}, {0.5f,0.5f,0.5f,0.5f}, 0.2f, 0.3f, 0.0f, 0.0f, 0, 0.0f, 0.05f, 0.2f, 0, 0.05f, 0.4f, 0.4f, 0.0f, 0.5f, 0.0f, 0.45f, 0.20f, 0, 0, 0.5478f, 0.7623f, 0.40f, 0.0f, 0.0f, 0.0f, 0, 0.15f, 0.5478f, 0.7486f, 0.7f, 0.35f, 0.6f, 0.3f, 0.15f, 0.4f, 0.45f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},

/* === New: rhythmic / percussive / expressive / generative / formant === */
{"Mallet", 10, 0.5f, 0.0f, 0.0f, 0.5000f, {3,3,1,1}, {0.15f,0.15f,0.00f,0.00f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.6200f, 0.1800f, 0, 0, 0.0000f, 0.5364f, 0.4500f, 0.0f, 0.0f, 0.0f, 0, 0.1200f, 0.0000f, 0.5473f, 0.5500f, 0.2200f, 0.4500f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Music Box", 7, 0.5f, 0.0f, 0.0200f, 1.0f, {1,1,1,1}, {0,0,0,0}, 0, 0, 0, 0, 0, 0.3200f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.8500f, 0.0500f, 0, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.6420f, 0.5000f, 0.4000f, 0.6000f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Tine Pluck", 3, 0.5f, 0.0f, 0.0f, 1.0f, {5,5,1,1}, {0.30f,0.30f,0.00f,0.00f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.5500f, 0.0f, 0, 0, 0.0000f, 0.4946f, 0.5500f, 0.0f, 0.0f, 0.0f, 0, 0.2000f, 0.0000f, 0.5892f, 0.5000f, 0.0f, 0.5f, 0.3f, 0.1800f, 0.3500f, 0.3500f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Pulse Gate", 15, 0.5f, 0.0f, 0.0f, 0.7000f, {3,3,3,3}, {0.20f,0.20f,0.20f,0.20f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.6000f, 0.2500f, 0, 0, 0.0000f, 0.4041f, 0.4000f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.6189f, 0.5500f, 0.2000f, 0.5f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Filter Run", 2, 0.5f, 0.0f, 0.0f, 1.0f, {3,3,3,3}, {0,0,0,0}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.1000f, 0.5000f, 0, 0, 0.0000f, 0.3811f, 0.7000f, 0.0f, 0.0f, 0.0f, 0, 0.1500f, 0.3095f, 0.6189f, 0.5000f, 0.2500f, 0.5f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Tremolo Choir", 9, 0.5f, 0.0f, 0.0f, 0.9000f, {1,2,2,1}, {0.50f,0.50f,0.50f,0.50f}, 0.4000f, 0.5000f, 0, 0, 0, 0.0f, 0.5500f, 0.0000f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.7000f, 0.0f, 0, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.35f, 0.50f, 0.5000f, 0.4500f, 0.7000f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.62f, 0.55f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Bloom", 10, 0.5f, 0.0f, 0.1500f, 0.9000f, {3,3,3,3}, {0.30f,0.30f,0.30f,0.30f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.1200f, 0.3000f, 0, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.6189f, 0.7555f, 0.6000f, 0.5000f, 0.7500f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.9000f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Press Lead", 1, 0.5f, 0.0f, 0.0f, 1.0f, {5,5,3,3}, {0.25f,0.25f,0.20f,0.20f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.3000f, 0.2000f, 0.3500f, 0, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.2500f, 0.2379f, 0.5892f, 0.5500f, 0.0f, 0.5f, 0.3f, 0.2000f, 0.3000f, 0.4000f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.8500f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Wander", 11, 0.5f, 0.0f, 0.0f, 0.9000f, {6,6,6,6}, {0.40f,0.50f,0.60f,0.70f}, 0.4000f, 0.5000f, 0.3000f, 0.4000f, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.6000f, 0.0f, 0, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5892f, 0.7555f, 0.5000f, 0.5000f, 0.8000f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0.2000f, 0.3000f, 0, 0.1500f, 0.3000f, 0, 0, 0, 1, 0, 0, 1, 0.0f, 0.0f, 0.0f, 0.6000f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Glass Bell", 8, 0.5f, 0.0f, 0.0f, 1.0f, {1,1,5,5}, {0,0,0,0}, 0.3000f, 0.4000f, 0, 0, 0, 0.2500f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.7000f, 0.0f, 0, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.6189f, 0.5000f, 0.3500f, 0.5f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 1, 0.7000f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Vowel", 9, 0.5f, 0.0f, 0.0f, 0.6000f, {3,3,3,3}, {0.20f,0.20f,0.20f,0.20f}, 0, 0, 0, 0, 0, 0.0f, 0.4500f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.5500f, 0.6000f, 1, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.4041f, 0.6189f, 0.6000f, 0.3000f, 0.5f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},
{"Telephone", 2, 0.5f, 0.0f, 0.0f, 1.0f, {5,5,5,5}, {0.30f,0.30f,0.30f,0.30f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.6000f, 0.5000f, 1, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.5892f, 0.6000f, 0.0f, 0.5f, 0.3f, 0.2500f, 0.3000f, 0.4000f, 0.7f, 0.4000f, 0.2000f, 0.3000f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6}, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f},


/* === Chord Multi showcase (v0.3.11) — one-finger harmony maps, distinct timbres === */
{"Jazz Comp C", 7, 0.5f, 0.0f, 0.05f, 0.5f, {1,1,1,1}, {0.45f,0.40f,0.40f,0.45f}, 0.2f, 0.3f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.50f, 0.12f, 0, 0, 0.0f, 0.50f, 0.55f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.004f, 0.74f, 0.7f, 0.40f, 0.6f, 0.4f, 0.1f, 0.4f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.66f, 0.28f, 0, 2, {7, 13, 3, 13, 3, 7, 15, 13, 13, 3, 13, 15}, 0.35f, 0.15f, 0.4f, 0.2f, 0.1f},
{"Pop Triads C", 10, 0.5f, 0.0f, 0.0f, 0.5000f, {3,3,1,1}, {0.15f,0.15f,0.00f,0.00f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.6200f, 0.1800f, 0, 0, 0.0000f, 0.5364f, 0.4500f, 0.0f, 0.0f, 0.0f, 0, 0.1200f, 0.0000f, 0.5473f, 0.5500f, 0.2200f, 0.4500f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {10, 6, 2, 6, 2, 6, 2, 13, 6, 2, 6, 15}, 0.3f, 0.1f, 0.35f, 0.25f, 0.1f},
{"A Minor Keys", 3, 0.5f, 0.0f, 0.0f, 1.0f, {5,5,1,1}, {0.30f,0.30f,0.00f,0.00f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.5500f, 0.0f, 0, 0, 0.0000f, 0.4946f, 0.5500f, 0.0f, 0.0f, 0.0f, 0, 0.2000f, 0.0000f, 0.5892f, 0.5000f, 0.0f, 0.5f, 0.3f, 0.1800f, 0.3500f, 0.3500f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {7, 13, 3, 7, 3, 7, 15, 13, 14, 3, 7, 15}, 0.4f, 0.15f, 0.45f, 0.25f, 0.15f},
{"Neo-Soul 9ths", 3, 0.5f, 0.0f, 0.05f, 0.6f, {5,5,5,5}, {0.2f,0.2f,0.2f,0.2f}, 0.3f, 0.4f, 0.2f, 0.3f, 0, 0.0f, 0.10f, 0.3f, 0, 0.0f, 0.5f, 0.4f, 0.0f, 0.5f, 0.0f, 0.50f, 0.20f, 0, 0, 0.7325f, 0.8200f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.15f, 0.7622f, 0.8271f, 0.55f, 0.65f, 0.85f, 0.4f, 0.10f, 0.5f, 0.5f, 0.5f, 0.40f, 0.15f, 0.35f, 0, 0.4f, 0, 0, 0, 0, 0.10f, 0.3f, 0, 0.10f, 0.3f, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {8, 14, 4, 10, 4, 8, 5, 13, 8, 4, 10, 15}, 0.55f, 0.2f, 0.55f, 0.3f, 0.3f},
{"House Stab Em", 13, 0.5f, 0.0f, 0.0f, 0.5f, {5,5,5,5}, {0.3f,0.3f,0.3f,0.3f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.30f, 0.15f, 0, 1, 0.0000f, 0.6910f, 0.65f, 0.0f, 0.0f, 0.0f, 0, 0.25f, 0.0000f, 0.6201f, 0.65f, 0.15f, 0.4f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {8, 13, 13, 10, 4, 7, 15, 8, 3, 4, 8, 4}, 0.25f, 0.25f, 0.3f, 0.2f, 0.05f},
{"Suspended Dream", 8, 0.5f, 0.0f, 0.30f, 1.0f, {3,3,3,3}, {0.4f,0.4f,0.4f,0.4f}, 0.4f, 0.5f, 0.5f, 0.6f, 0, 0.0f, 0.15f, 0.4f, 0, 0.3f, 0.3f, 0.3f, 0.0f, 0.5f, 0.4f, 0.5f, 0.4f, 0, 0, 0.6192f, 0.8041f, 0.3f, 0.05f, 0.3f, 0.3f, 0, 0.05f, 0.7137f, 0.8041f, 0.7f, 0.60f, 0.7f, 0.4f, 0.1f, 0.35f, 0.3f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {9, 9, 10, 9, 5, 9, 9, 9, 10, 5, 9, 9}, 0.7f, 0.1f, 0.8f, 0.35f, 0.45f},
{"Quartal Modern", 9, 0.5f, 0.0f, 0.0f, 0.6000f, {3,3,3,3}, {0.20f,0.20f,0.20f,0.20f}, 0, 0, 0, 0, 0, 0.0f, 0.4500f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.5500f, 0.6000f, 1, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.4041f, 0.6189f, 0.6000f, 0.3000f, 0.5f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {9, 1, 9, 1, 9, 9, 1, 9, 1, 9, 1, 9}, 0.5f, 0.15f, 0.55f, 0.3f, 0.3f},
{"Power Fifths", 9, 0.5f, 0.0f, 0.10f, 0.7f, {3,3,3,3}, {0.1f,0.1f,0.1f,0.1f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.15f, 0, 0, 0.5478f, 0.7853f, 0.50f, 0.0f, 0.0f, 0.0f, 0, 0.30f, 0.5478f, 0.7486f, 0.7f, 0.35f, 0.55f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 0.3f, 0.2f, 0.35f, 0.2f, 0.1f},
{"Octave Lead", 13, 0.5f, 0.0f, 0.0f, 0.6f, {3,3,3,3}, {0.2f,0.2f,0.2f,0.2f}, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.45f, 0.0f, 0, 0, 0.0000f, 0.7140f, 0.85f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.7327f, 0.7f, 0.2f, 0.5f, 0.4f, 0.15f, 0.4f, 0.45f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0.25f, 0.2f, 0.3f, 0.2f, 0.05f},
{"Blues Dominants", 12, 0.5f, 0.0f, 0.10f, 0.7f, {6,6,6,6}, {0.5f,0.5f,0.5f,0.5f}, 0.2f, 0.3f, 0.0f, 0.0f, 0, 0.0f, 0.05f, 0.2f, 0, 0.05f, 0.4f, 0.4f, 0.0f, 0.5f, 0.0f, 0.45f, 0.20f, 0, 0, 0.5478f, 0.7623f, 0.40f, 0.0f, 0.0f, 0.0f, 0, 0.20f, 0.5478f, 0.7486f, 0.7f, 0.35f, 0.6f, 0.3f, 0.15f, 0.4f, 0.45f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {13, 14, 13, 14, 13, 13, 14, 13, 14, 13, 13, 14}, 0.35f, 0.2f, 0.4f, 0.25f, 0.15f},
{"Gospel Keys", 10, 0.6f, 0.0f, 0.15f, 0.9f, {3,3,3,3}, {0.3f,0.3f,0.3f,0.3f}, 0.4f, 0.6f, 0.5f, 0.6f, 0, 0.0f, 0.10f, 0.3f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.40f, 0.40f, 0, 0, 0.7622f, 0.8338f, 0.30f, 0.20f, 0.5f, 0.85f, 0, 0.05f, 0.7325f, 0.8200f, 0.65f, 0.50f, 0.8f, 0.4f, 0.20f, 0.5f, 0.6f, 0.6f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 2, 0, 0, 0.20f, 0.4f, 0, 0.15f, 0.4f, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {8, 14, 3, 13, 3, 8, 14, 13, 14, 3, 13, 15}, 0.6f, 0.15f, 0.6f, 0.3f, 0.35f},
{"Bossa Nova", 7, 0.5f, 0.0f, 0.0200f, 1.0f, {1,1,1,1}, {0,0,0,0}, 0, 0, 0, 0, 0, 0.3200f, 0.0f, 0.0f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.8500f, 0.0500f, 0, 0, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0000f, 0.6420f, 0.5000f, 0.4000f, 0.6000f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {7, 15, 3, 13, 3, 7, 15, 7, 14, 3, 13, 13}, 0.4f, 0.15f, 0.45f, 0.3f, 0.2f},
{"D Dorian", 8, 0.5f, 0.0f, 0.20f, 0.6f, {3,3,3,3}, {0.3f,0.3f,0.3f,0.3f}, 0.3f, 0.4f, 0.2f, 0.3f, 0, 0.0f, 0.15f, 0.3f, 0, 0.1f, 0.3f, 0.6f, 0.0f, 0.5f, 0.0f, 0.45f, 0.2f, 0, 0, 0.7852f, 0.8200f, 0.10f, 0.12f, 0.25f, 0.7f, 0, 0.05f, 0.8200f, 0.8400f, 0.6f, 0.75f, 0.9f, 0.4f, 0.2f, 0.55f, 0.5f, 0.55f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {7, 3, 3, 7, 3, 7, 15, 13, 3, 3, 7, 15}, 0.65f, 0.1f, 0.7f, 0.35f, 0.4f},
{"Cinematic Minor", 8, 0.6f, 0.0f, 0.15f, 0.7f, {1,2,2,1}, {0.5f,0.5f,0.5f,0.5f}, 0.5f, 0.5f, 0.4f, 0.7f, 0, 0.0f, 0.20f, 0.4f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.10f, 0.50f, 0.4f, 0, 0, 0.7325f, 0.8200f, 0.20f, 0.15f, 0.4f, 0.9f, 0, 0.0f, 0.7852f, 0.8200f, 0.6f, 0.55f, 0.7f, 0.4f, 0.35f, 0.5f, 0.6f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {7, 14, 3, 15, 14, 7, 15, 11, 14, 3, 7, 15}, 0.6f, 0.15f, 0.7f, 0.3f, 0.35f},
{"6/9 Shimmer", 9, 0.5f, 0.0f, 0.0f, 0.8f, {3,3,3,3}, {0,0,0,0}, 0, 0, 0.4f, 0.4f, 0, 0.0f, 0.45f, 0.4f, 0, 0.0f, 0.5f, 0.2f, 0.0f, 0.5f, 0.0f, 0.45f, 0.4f, 0, 0, 0.0000f, 0.7853f, 0.0f, 0.4f, 0.35f, 0.3f, 0, 0.10f, 0.5478f, 0.7623f, 0.6f, 0.55f, 0.7f, 0.3f, 0.0f, 0.3f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 2, {10, 8, 10, 8, 10, 10, 8, 10, 8, 10, 8, 10}, 0.7f, 0.1f, 0.65f, 0.4f, 0.45f},
};
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

/* Filter cutoff range, exp-mapped 0..1 → 20 Hz .. 20 kHz. TPT SVF is stable
 * up to Nyquist but tan() blows up exactly AT Nyquist — keep cutoff under
 * 0.49 * SR. */
static const float FILTER_CUTOFF_MIN_HZ = 20.0f;
static const float FILTER_CUTOFF_MAX_HZ = 20000.0f;
/* Resonance maps to Q (peak height). 0 = broad (Q=0.5, Butterworth-ish),
 * 1 = very resonant (Q=20). */
static const float FILTER_Q_MIN = 0.5f;
static const float FILTER_Q_MAX = 12.0f;   /* was 20; lower max keeps peaks musical */

/* TPT (Topology-Preserving Transform) SVF state — Vadim Zavalishin /
 * Andy Simper formulation. Stable for any g and k. */
struct SVF {
    float ic1eq;
    float ic2eq;
};

/* Warm ZDF ladder state (4 integrators) — see ladder_lp_process below. */
typedef struct { float s[4]; } LadderLP;

/* Dattorro plate reverb state (J. Dattorro, AES 1997 — public structure/taps).
 * Input diffusion (4 allpasses) -> cross-coupled figure-8 tank (modulated
 * allpass + delay + damping + allpass + delay per half). Lush and long. */
typedef struct {
    float d142[142], d107[107], d379[379], d277[277];      /* input diffusers */
    float apa[692], dla[4453], apb[928], dlb[4217];        /* tank stage 1 */
    float ap2a[1800], dl2a[3720], ap2b[2656], dl2b[3163];  /* tank stage 2 */
    int   i142, i107, i379, i277;
    int   iapa, idla, iapb, idlb, iap2a, idl2a, iap2b, idl2b;
    float bw, damp_a, damp_b, lphase;
} DattorroReverb;

enum FilterMode { FILT_LP = 0, FILT_HP = 1, FILT_BP = 2 };
static const int NUM_FILTER_MODES = 3;

enum EnvStage { ENV_IDLE, ENV_ATTACK, ENV_HOLD, ENV_RELEASE };

enum Waveform {
    WAVE_OFF = 0,
    WAVE_SINE = 1,
    WAVE_TRIANGLE = 2,
    WAVE_SAW = 3,
    WAVE_SQUARE = 4,
    WAVE_PULSE_TRAIN = 5,
    WAVE_WAVETABLE = 6
};
static const int NUM_WAVEFORMS = 7;

enum LfoMode { LFOMODE_FREE = 0, LFOMODE_NOTE_RESET = 1, LFOMODE_ONE_SHOT = 2 };
static const int NUM_LFO_MODES = 3;

enum ControlSource {
    CTRL_AFTERTOUCH = 0,
    CTRL_RANDOM = 1,
    CTRL_COIN_TOSS = 2,
    CTRL_CC = 3,
    CTRL_VELOCITY = 4
};
static const int NUM_CONTROL_SOURCES = 5;

enum LFOShape { LFO_TRIANGLE = 0, LFO_RAMP_UP = 1, LFO_RAMP_DOWN = 2, LFO_SQUARE = 3 };
static const int NUM_LFO_SHAPES = 4;

/* LFO rate range, exp-mapped: 0.02 Hz (very slow swell) up to 50 Hz (fast,
 * near-audio-rate shimmer). Wider than the old 0.01..10 for more expressive reach. */
static const float LFO_RATE_MIN_HZ = 0.02f;
static const float LFO_RATE_MAX_HZ = 50.0f;

struct LFO {
    float phase;      /* 0..1 */
    float phase_inc;  /* per audio sample */
    int   shape;      /* 0..NUM_LFO_SHAPES-1 */
};

struct AREnv {
    EnvStage stage;
    float value;
    float attack_inc;
    float release_coef;
};

/* AD (Attack-Decay) envelope — note-triggered, ignores gate. Shares attack
 * timing with AR's attack_inc and decay timing with release_coef. */
struct ADEnv {
    EnvStage stage;
    float value;
    float attack_inc;
    float decay_coef;
};

struct Voice {
    bool active;
    bool gate;           /* true while pad is held; chord_off sets to false */
    int root_note;       /* MIDI root that spawned this voice (for note-off match) */
    int chord_step;      /* 0..CHORD_SIZE-1 — position in the chord, for morph lookups */
    int waveform;        /* per-voice waveform — set at voice_start */
    float phase;
    float phase_inc;
    float target_phase_inc;  /* glide target — phase_inc ramps toward this */
    float glide_step;        /* per-sample addition; 0 = no glide */
    float velocity;
    float pan_offset;    /* (chord_step_norm - 0.5), -0.5..+0.5. Pan scales by inst->width per sample. */
    AREnv env;
};

struct chordism_instance_t {
    char module_dir[512];

    /* Params */
    float attack;        /* 0..1 */
    float release;       /* 0..1 */
    float volume;        /* 0..1 */
    int   waveforms[CHORD_SIZE];  /* per-osc waveform (chord step → wave) */
    float shapes[CHORD_SIZE];     /* per-osc shape */
    float morph_index;   /* 0..1 — sweeps level-morph LUT */
    float morph_intensity; /* 0..1 — blend flat→LUT row */
    float morph_gains[CHORD_SIZE];  /* cached effective gain per chord step */

    float pan_morph_index;     /* 0..1 — sweeps pan-morph LUT */
    float pan_morph_intensity; /* 0..1 — blend center→LUT row */
    float pan_morph_pans[CHORD_SIZE]; /* cached effective pan per chord step (-1..+1) */

    /* Chord voicing: spread scales intervals, rotation shifts which osc plays
     * which chord degree. */
    float chord_spread;     /* 0..1 → interval multiplier 0..2 */
    float chord_rotation;   /* 0..1 → integer rotation 0..CHORD_SIZE-1 */

    /* FM: phase modulation. fm_modulator_idx picks which chord_step is the
     * modulator; carriers (other steps) get phase += last_modulator * fm_amounts[step]. */
    int   fm_modulator_idx;   /* 0..CHORD_SIZE-1 */
    float fm_amount;          /* 0..1 — global amount, scales fm_amounts */
    float fm_amounts[CHORD_SIZE]; /* per-carrier amount (modulator's slot is unused) */
    float last_modulator_sample;

    /* Per-osc mixer trims (0..1, multiplies on top of morph_gain). */
    float mixer_trims[CHORD_SIZE];

    /* Per-osc enable bitmasks for vibrato and pitch sweep. 1 bit per chord step. */
    int   vib_osc_enable;     /* low 4 bits, 1=enabled per step */
    int   sweep_osc_enable;

    /* LFO mode for each LFO (free / note-reset / one-shot). */
    int   shape_lfo_mode;
    int   filter_lfo_mode;
    int   lm_lfo_mode;
    int   pm_lfo_mode;
    bool  shape_lfo_done;
    bool  filter_lfo_done;
    bool  lm_lfo_done;
    bool  pm_lfo_done;

    /* FM position: 0 = pre-level-morph (FM amount constant), 1 = post-level-morph
     * (FM amount scales with carrier level, varies with morph). */
    int   fm_position;

    /* Scale quantizer. */
    int   scale_index;       /* 0..NUM_SCALES-1; 0 = chromatic (no quantize) */
    int   scale_root;        /* MIDI pitch class 0..11 — root of the scale */

    /* Tuning mode: 0 = Chord Single, 1 = Interval Single, 2 = Chord Multi
     * (each pitch class gets its own chord_type). */
    int   tuning_mode;
    int   interval_1;        /* -24..+24 semitones */
    int   interval_2;
    int   interval_3;
    int   chord_pc[12];      /* per-pitch-class chord type for Chord Multi */

    /* Per-osc shape LFO phase offsets — phase offset per oscillator,
     * applied on top of the shared shape LFO phase. */
    float shape_lfo_phase_offsets[CHORD_SIZE];

    /* Modulation source matrix. Single source globally, routed to multiple
     * targets via per-target depth knobs (-1..+1). */
    int   ctrl_source;       /* ControlSource enum */
    int   ctrl_cc;           /* MIDI CC number for CTRL_CC (0..127) */
    float ctrl_value;        /* smoothed source value used by the DSP (slews to target) */
    float ctrl_value_target; /* instantaneous source value set by MIDI (aftertouch/CC/etc.) */
    float vel_baseline;      /* velocity-seeded floor for the aftertouch source (vel*0.8) so
                              * notes open on velocity; aftertouch pushes the remaining range. */
    float at_smooth;         /* smoothed raw aftertouch pressure (no velocity baseline) — drives
                              * the default "press = brighter" cutoff so it eases from zero. */
    float ctrl_to_cutoff;
    float ctrl_to_morph;
    float ctrl_to_vib;
    float ctrl_to_shape;
    float ctrl_to_fm;

    /* Arp: Hold, Euclidean, variation transpose. */
    int   arp_hold;
    int   arp_euclid_steps;     /* 1..16 */
    int   arp_euclid_beats;     /* 0..steps (0 = euclidean off) */
    int   arp_variation_interval; /* -12..+12 semis */
    int   arp_variations;       /* 1..8 (1 = no variation) */
    bool  arp_pattern[16];      /* precomputed from euclid steps/beats */
    int   arp_pattern_pos;      /* current position within euclid pattern */
    int   arp_variation_idx;    /* 0..arp_variations-1 */

    /* Level morph LFO — animates morph_index continuously. */
    float lm_lfo_rate;
    float lm_lfo_depth;
    int   lm_lfo_shape;
    float lm_lfo_phase;
    float lm_lfo_phase_inc;

    /* Pan morph LFO — animates pan_morph_index continuously. */
    float pm_lfo_rate;
    float pm_lfo_depth;
    int   pm_lfo_shape;
    float pm_lfo_phase;
    float pm_lfo_phase_inc;

    /* Amplitude LFO — tremolo; wobbles output gain. */
    float amp_lfo_rate;
    float amp_lfo_depth;
    int   amp_lfo_shape;
    float amp_lfo_phase;
    float amp_lfo_phase_inc;

    /* VCA envelope mode + drone. */
    int   vca_mode;       /* 0=AR (ASR-like), 1=AD, 2=Looping */
    int   vca_hard_reset;
    int   vca_drone;      /* if 1, bypass VCA envelope (always open) */

    /* Filter envelope mode + hard reset. */
    int   fenv_mode;      /* 0=AD, 1=ASR, 2=Looping */
    int   fenv_hard_reset;

    /* Quality control position pre/post-filter. */
    int   quality_position;  /* 0=post-filter (current), 1=pre-filter */

    /* Reverb (Dattorro plate) character. */
    float reverb_shimmer;     /* 0..1 — chorus excursion (primary) */
    float reverb_lowcut;      /* 0..1 — HP on the wet output */
    float reverb_size;        /* 0..1 — adds tail length */
    float reverb_mod_rate;    /* 0..1 → chorus LFO speed */
    float reverb_mod_depth;   /* 0..1 → extra chorus excursion */
    float reverb_hp_l;        /* one-pole HP state for low cut */
    float reverb_hp_r;

    /* Delay extras: Long mode buffer (single long mono), Tone Hi/Lo split. */
    float delay_tone_hi;       /* 0..1 — LP cutoff in feedback (current 'tone') */
    float delay_tone_lo;       /* 0..1 — HP cutoff in feedback */
    float delay_mod_rate;      /* 0..1 → 0.1..5 Hz delay-time modulation */
    float delay_mod_depth;     /* 0..1 → chorus/flange depth in delay time */
    float delay_mod_phase;
    float delay_mod_phase_inc;
    float delay_hp_l;
    float delay_hp_r;

    /* Pitch-shifted delay state (Zenith / Interval modes — overlap-add granular). */
    int   delay_grain_pos_l;
    int   delay_grain_pos_r;
    float delay_grain_phase;   /* 0..1, read-head fractional */
    float lfo_rate;      /* 0..1 — exp-mapped to Hz */
    float lfo_depth;     /* 0..1 — modulation amount on shape */
    int   lfo_shape;     /* 0..NUM_LFO_SHAPES-1 */
    int   chord_type;    /* 0..NUM_CHORDS-1 — row in CHORD_TABLE */
    float detune;        /* 0..1 → 0..MAX_DETUNE_CENTS per chord step */
    float width;         /* 0..1 — 0=mono, 1=full LR spread across chord */
    float vib_speed;     /* 0..1 → exp 0.1..12 Hz */
    float vib_depth;     /* 0..1 → 0..VIB_DEPTH_MAX_CENTS */
    float vib_delay;     /* 0..1 → 0..VIB_DELAY_MAX_S rise time */
    float sweep_amount;  /* -1..+1 → ±12 semitones offset at note-on */
    float sweep_rate;    /* 0..1 → exp time SWEEP_TIME_MIN_S..MAX_S */

    /* Vibrato runtime state — single global LFO + ramp shared by all voices. */
    float vibrato_phase;
    float vibrato_phase_inc;
    float vib_ramp_value;   /* 0..1, climbs after chord_on */
    float vib_ramp_inc;     /* per-sample ramp step (precomputed from vib_delay) */

    /* Pitch sweep runtime state — single offset, decays toward 0. */
    float sweep_value;        /* current semitones */
    float sweep_coef;         /* exp decay coef per sample (precomputed) */
    float filter_cutoff;     /* 0..1 → exp 20..20kHz */
    float filter_resonance;  /* 0..1 → Q from FILTER_Q_MIN to FILTER_Q_MAX */
    int   filter_mode;       /* 0..NUM_FILTER_MODES-1 */
    float filter_env_attack; /* 0..1 → linear A time */
    float filter_env_decay;  /* 0..1 → exp D time */
    float filter_env_depth;  /* -1..+1 — bipolar modulation of cutoff */
    float drive;             /* 0..1 → 1..10x pre-tanh gain */

    SVF   filter_l;
    SVF   filter_r;
    /* Second SVF stage per channel — used when filter_slope = 24 dB (cascade).
     * For 12 dB mode the b-stages are inert. */
    SVF   filter_l_b;
    SVF   filter_r_b;
    LadderLP ladder_l;   /* warm ZDF ladder, used for the LP filter mode */
    LadderLP ladder_r;
    /* Static (no-modulation) TPT SVF coefficients. Per-sample recompute kicks
     * in when filter_env_depth != 0 OR filter_lfo_depth != 0. */
    float filter_a1;
    float filter_a2;
    float filter_a3;
    float filter_k;
    int   filter_slope;  /* 0 = 12 dB, 1 = 24 dB */

    /* Filter LFO — dedicated, separate from shape LFO. Stereo spread offsets
     * the R-channel LFO phase from L for auto-pan / stereo movement. */
    float filter_lfo_rate;
    float filter_lfo_depth;
    float filter_lfo_spread;     /* 0..1 → 0..0.5 phase offset between L and R */
    int   filter_lfo_shape;
    float filter_lfo_phase;
    float filter_lfo_phase_inc;

    ADEnv filter_env;

    /* Lo-Fi (Quality Control) */
    float grind;          /* 0..1 → bit reduction */
    float bit_shift;      /* 0..1 → DC offset bias pre-crush */
    float decimator;      /* 0..1 → sample-rate hold (0=off, 1=hold 32 samples) */
    int   decim_counter;
    float decim_hold_l;
    float decim_hold_r;

    /* Reverb — Dattorro plate */
    float reverb_mix;    /* 0..1 dry/wet */
    float reverb_decay;  /* 0..1 → base RT60 */
    float reverb_damp;   /* 0..1 → HF damping in the tank */
    DattorroReverb dattorro;   /* lush plate reverb (active reverb path) */

    /* Delay */
    float delay_mix;
    float delay_time;       /* 0..1 → DELAY_TIME_MIN_S..MAX_S */
    float delay_feedback;   /* 0..1 → 0..DELAY_FEEDBACK_MAX */
    float delay_tone;       /* 0..1 → bright LP coef on feedback */
    int   delay_mode;       /* 0=stereo, 1=ping-pong, 2=flip-flop */
    float delay_buf_l[DELAY_BUFFER_SIZE];
    float delay_buf_r[DELAY_BUFFER_SIZE];
    int   delay_write_idx;
    float delay_lp_l;
    float delay_lp_r;
    int   delay_flip_counter;  /* increments per delay-period; even=L→R, odd=R→L */

    LFO   shape_lfo;
    Voice voices[NUM_VOICES];
    int next_chord_base;   /* 0, 4, 8, 12 — round-robin between voice banks */

    /* Held-note stack — mono priority with note-off return-to-previously-held. */
    int held_notes[HELD_STACK_MAX];
    int held_count;

    /* Aftertouch (channel pressure). Routed to vibrato depth boost. */
    float aftertouch;

    int   preset_index;     /* 0..NUM_PRESETS-1 */

    /* Glide */
    float glide_rate;                       /* 0..1 → 0..GLIDE_TIME_MAX_S */
    int   glide_legato;                     /* 0=always, 1=only when overlapping notes */
    bool  has_prev_chord;                   /* false until first chord_on completes */
    bool  prev_chord_still_held;            /* true if previous note hasn't been released yet (legato) */
    float prev_phase_inc[CHORD_SIZE];       /* last chord's per-step base phase_inc */

    /* Vibrato extras */
    int   vib_stray;                        /* 0=periodic LFO, 1=smoothed-random */
    float vib_random_value;
    float vib_random_target;
    int   vib_random_counter;

    /* Arpeggiator */
    int   arp_enabled;                      /* 0/1 */
    float arp_tempo;                        /* 0..1 → 30..240 BPM (used when clock_sync = 0) */
    int   arp_direction;                    /* 0=up, 1=down, 2=updown, 3=random */
    int   arp_step_idx;
    int   arp_step_dir;                     /* +1 or -1 (for updown) */
    int   arp_sample_counter;
    int   arp_step_period;                  /* samples per arp step */
    int   arp_clock_sync;                   /* 0 = internal, 1 = MIDI clock */
    int   arp_clock_division;               /* 0=1/4, 1=1/4T, 2=1/8, 3=1/8T, 4=1/16, 5=1/32 */
    int   arp_clock_count;                  /* received clock ticks since last arp step */
    bool  arp_clock_running;                /* set by MIDI Start (0xFA) / Continue (0xFB) */
};

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static float midi_to_hz(int note) {
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static float param_from_string(const char *val, float fallback) {
    if (!val) return fallback;
    char *end = nullptr;
    float v = strtof(val, &end);
    if (end == val) return fallback;
    return clampf(v, 0.0f, 1.0f);
}

static float lerp01(float t, float lo, float hi) {
    return lo + (hi - lo) * t;
}

/* PolyBLEP — Polynomial Bandlimited stEP correction. Subtracts the
 * high-frequency content introduced by a discontinuous waveform transition
 * at sample t (phase in [0,1)). dt is phase_inc per sample. Returns 0
 * outside the small neighborhood around the discontinuity. */
static float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static float lfo_sample(int shape, float phase) {
    switch (shape) {
        case LFO_TRIANGLE:  return 4.0f * fabsf(phase - 0.5f) - 1.0f;
        case LFO_RAMP_UP:   return 2.0f * phase - 1.0f;
        case LFO_RAMP_DOWN: return 1.0f - 2.0f * phase;
        case LFO_SQUARE:    return (phase < 0.5f) ? 1.0f : -1.0f;
        default:            return 0.0f;
    }
}

/* Exp-map [0,1] to [LFO_RATE_MIN_HZ, LFO_RATE_MAX_HZ]. */
static float lfo_rate_to_hz(float rate01) {
    float ratio = LFO_RATE_MAX_HZ / LFO_RATE_MIN_HZ;
    return LFO_RATE_MIN_HZ * powf(ratio, rate01);
}

static float vib_speed_to_hz(float speed01) {
    float ratio = VIB_SPEED_MAX_HZ / VIB_SPEED_MIN_HZ;
    return VIB_SPEED_MIN_HZ * powf(ratio, speed01);
}

static void pan_morph_recompute_at(chordism_instance_t *inst, float index01) {
    const int N = 16;
    if (index01 < 0.0f) index01 = 0.0f;
    if (index01 > 1.0f) index01 = 1.0f;
    float idx = index01 * (float)(N - 1);
    int lo = (int)idx;
    int hi = lo + 1;
    if (lo < 0) lo = 0;
    if (lo >= N) lo = N - 1;
    if (hi >= N) hi = N - 1;
    float frac = idx - (float)lo;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    for (int i = 0; i < CHORD_SIZE; ++i) {
        float lut = PAN_MORPH_LUT[lo][i] * (1.0f - frac)
                  + PAN_MORPH_LUT[hi][i] * frac;
        inst->pan_morph_pans[i] = lut * inst->pan_morph_intensity;
    }
}

static void pan_morph_recompute(chordism_instance_t *inst) {
    pan_morph_recompute_at(inst, inst->pan_morph_index);
}

static void morph_recompute_at(chordism_instance_t *inst, float index01) {
    if (index01 < 0.0f) index01 = 0.0f;
    if (index01 > 1.0f) index01 = 1.0f;
    float idx = index01 * (float)(NUM_LEVEL_MORPHS - 1);
    int lo = (int)idx;
    int hi = lo + 1;
    if (lo < 0) lo = 0;
    if (lo >= NUM_LEVEL_MORPHS) lo = NUM_LEVEL_MORPHS - 1;
    if (hi >= NUM_LEVEL_MORPHS) hi = NUM_LEVEL_MORPHS - 1;
    float frac = idx - (float)lo;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    for (int i = 0; i < CHORD_SIZE; ++i) {
        float lut = LEVEL_MORPH_LUT[lo][i] * (1.0f - frac)
                  + LEVEL_MORPH_LUT[hi][i] * frac;
        /* Blend flat (1.0) → lut row by intensity. */
        inst->morph_gains[i] = 1.0f + (lut - 1.0f) * inst->morph_intensity;
    }
}

static void morph_recompute(chordism_instance_t *inst) {
    morph_recompute_at(inst, inst->morph_index);
}

/* Apply lo-fi (grind / shift / decimator) in-place to a stereo sample pair.
 * Used in either pre- or post-filter position based on inst->quality_position. */
static inline void apply_lofi(chordism_instance_t *inst, float *l, float *r) {
    if (inst->grind > 0.0f) {
        float bits = 16.0f - inst->grind * 14.0f;
        float steps = powf(2.0f, bits) * 0.5f;
        float shift = inst->bit_shift * 0.5f;
        *l = (floorf((*l + shift) * steps + 0.5f) / steps) - shift;
        *r = (floorf((*r + shift) * steps + 0.5f) / steps) - shift;
    }
    if (inst->decimator > 0.0f) {
        int hold = (int)(inst->decimator * 32.0f);
        if (hold < 1) hold = 1;
        if (inst->decim_counter <= 0) {
            inst->decim_hold_l = *l;
            inst->decim_hold_r = *r;
            inst->decim_counter = hold;
        }
        inst->decim_counter--;
        *l = inst->decim_hold_l;
        *r = inst->decim_hold_r;
    }
}

static void reverb_init(chordism_instance_t *inst) {
    memset(&inst->dattorro, 0, sizeof(inst->dattorro));  /* clear plate tail */
    inst->reverb_hp_l = 0.0f;
    inst->reverb_hp_r = 0.0f;
}

static void vibrato_recompute(chordism_instance_t *inst) {
    inst->vibrato_phase_inc = vib_speed_to_hz(inst->vib_speed) / SAMPLE_RATE;

    /* delay=0 → instant full ramp (single sample). */
    float delay_s = inst->vib_delay * VIB_DELAY_MAX_S;
    float delay_samples = delay_s * SAMPLE_RATE;
    if (delay_samples < 1.0f) delay_samples = 1.0f;
    inst->vib_ramp_inc = 1.0f / delay_samples;
}

/* Forward declarations needed because preset_apply calls these recompute
 * helpers, which are defined further down. */
static void filter_recompute(chordism_instance_t *inst);
static void sweep_recompute(chordism_instance_t *inst);
static void arp_recompute(chordism_instance_t *inst);
static void env_recompute_rates(AREnv *env, float attack01, float release01);
static void aenv_recompute_rates(ADEnv *env, float attack01, float decay01);

static void preset_apply(chordism_instance_t *inst, int idx) {
    if (idx < 0) idx = 0;
    if (idx >= NUM_PRESETS) idx = NUM_PRESETS - 1;
    const Preset *p = &PRESETS[idx];
    inst->preset_index = idx;

    inst->chord_type = p->chord_type;
    inst->chord_spread = p->chord_spread;
    inst->chord_rotation = p->chord_rotation;
    inst->detune = p->detune;
    inst->width = p->width;
    for (int i = 0; i < CHORD_SIZE; ++i) {
        inst->waveforms[i] = p->waveforms[i];
        inst->shapes[i] = p->shapes[i];
    }
    inst->morph_index = p->morph_index;
    inst->morph_intensity = p->morph_intensity;
    inst->pan_morph_index = p->pan_morph_index;
    inst->pan_morph_intensity = p->pan_morph_intensity;
    inst->fm_modulator_idx = p->fm_modulator_idx;
    inst->fm_amount = p->fm_amount;
    inst->lfo_rate = p->lfo_rate;
    inst->lfo_depth = p->lfo_depth;
    inst->lfo_shape = p->lfo_shape;
    if (inst->lfo_shape < 0) inst->lfo_shape = 0;
    if (inst->lfo_shape >= NUM_LFO_SHAPES) inst->lfo_shape = NUM_LFO_SHAPES - 1;
    inst->vib_depth = p->vib_depth;
    inst->vib_speed = p->vib_speed;
    inst->vib_delay = p->vib_delay;
    inst->sweep_amount = p->sweep_amount;
    inst->sweep_rate = p->sweep_rate;
    inst->glide_rate = p->glide_rate;
    inst->filter_cutoff = p->filter_cutoff;
    inst->filter_resonance = p->filter_resonance;
    inst->filter_mode = p->filter_mode;
    inst->filter_slope = p->filter_slope;
    inst->filter_env_attack = p->filter_env_attack;
    inst->filter_env_decay = p->filter_env_decay;
    inst->filter_env_depth = p->filter_env_depth;
    inst->filter_lfo_rate = p->filter_lfo_rate;
    inst->filter_lfo_depth = p->filter_lfo_depth;
    inst->filter_lfo_spread = p->filter_lfo_spread;
    inst->filter_lfo_shape = p->filter_lfo_shape;
    inst->drive = p->drive;
    inst->attack = p->attack;
    inst->release = p->release;
    inst->volume = p->volume;
    inst->reverb_mix = p->reverb_mix;
    inst->reverb_decay = p->reverb_decay;
    inst->reverb_damp = p->reverb_damp;
    inst->delay_mix = p->delay_mix;
    inst->delay_time = p->delay_time;
    inst->delay_feedback = p->delay_feedback;
    inst->delay_tone = p->delay_tone;
    inst->grind = p->grind;
    inst->bit_shift = p->bit_shift;
    inst->decimator = p->decimator;
    inst->arp_enabled = p->arp_enabled;
    inst->arp_tempo = p->arp_tempo;
    inst->arp_direction = p->arp_direction;
    inst->delay_mode = p->delay_mode;
    inst->vib_stray = p->vib_stray;
    inst->glide_legato = p->glide_legato;
    inst->lm_lfo_rate = p->lm_lfo_rate;
    inst->lm_lfo_depth = p->lm_lfo_depth;
    inst->lm_lfo_shape = p->lm_lfo_shape;
    inst->lm_lfo_phase_inc = lfo_rate_to_hz(inst->lm_lfo_rate) / SAMPLE_RATE;
    inst->pm_lfo_rate = p->pm_lfo_rate;
    inst->pm_lfo_depth = p->pm_lfo_depth;
    inst->pm_lfo_shape = p->pm_lfo_shape;
    inst->pm_lfo_phase_inc = lfo_rate_to_hz(inst->pm_lfo_rate) / SAMPLE_RATE;
    inst->scale_index = p->scale_index;
    inst->scale_root = p->scale_root;
    inst->vca_mode = p->vca_mode;
    inst->vca_drone = p->vca_drone;
    inst->fenv_mode = p->fenv_mode;
    inst->ctrl_source = p->ctrl_source;
    inst->ctrl_value = 0.0f;
    inst->ctrl_value_target = 0.0f;
    inst->vel_baseline = 0.0f;
    inst->at_smooth = 0.0f;
    inst->ctrl_to_cutoff = p->ctrl_to_cutoff;
    inst->ctrl_to_morph = p->ctrl_to_morph;
    inst->ctrl_to_vib = p->ctrl_to_vib;
    inst->ctrl_to_shape = p->ctrl_to_shape;
    inst->ctrl_to_fm = p->ctrl_to_fm;
    inst->amp_lfo_rate = p->amp_lfo_rate;
    inst->amp_lfo_depth = p->amp_lfo_depth;
    inst->amp_lfo_shape = p->amp_lfo_shape;
    inst->amp_lfo_phase_inc = lfo_rate_to_hz(inst->amp_lfo_rate) / SAMPLE_RATE;
    inst->tuning_mode = p->tuning_mode;
    for (int i = 0; i < 12; ++i) inst->chord_pc[i] = p->chord_pc[i];
    inst->reverb_shimmer = p->reverb_shimmer;
    inst->reverb_lowcut = p->reverb_lowcut;
    inst->reverb_size = p->reverb_size;
    inst->reverb_mod_rate = p->reverb_mod_rate;
    inst->reverb_mod_depth = p->reverb_mod_depth;

    /* Recompute derived values. */
    morph_recompute(inst);
    pan_morph_recompute(inst);
    filter_recompute(inst);
    vibrato_recompute(inst);
    sweep_recompute(inst);
    arp_recompute(inst);
    inst->shape_lfo.phase_inc = lfo_rate_to_hz(inst->lfo_rate) / SAMPLE_RATE;
    inst->shape_lfo.shape = inst->lfo_shape;
    inst->filter_lfo_phase_inc = lfo_rate_to_hz(inst->filter_lfo_rate) / SAMPLE_RATE;
    for (int i = 0; i < NUM_VOICES; ++i) {
        env_recompute_rates(&inst->voices[i].env, inst->attack, inst->release);
    }
    aenv_recompute_rates(&inst->filter_env,
                         inst->filter_env_attack, inst->filter_env_decay);
}

/* Forward declaration of chord_on (defined later — needed because
 * arp_step_and_fire calls it). */
static void chord_on(chordism_instance_t *inst, int root_note, int velocity);

/* Advance the arpeggiator one step + fire chord_on if the step is on the
 * pattern. Shared between sample-counter and MIDI-clock-driven ticks. */
static void arp_step_and_fire(chordism_instance_t *inst) {
    if (inst->held_count <= 0) return;
    int hc = inst->held_count;
    switch (inst->arp_direction) {
        case ARP_UP:
            inst->arp_step_idx = (inst->arp_step_idx + 1) % hc;
            break;
        case ARP_DOWN:
            inst->arp_step_idx--;
            if (inst->arp_step_idx < 0) inst->arp_step_idx = hc - 1;
            break;
        case ARP_UPDOWN:
            if (hc <= 1) {
                inst->arp_step_idx = 0;
            } else {
                inst->arp_step_idx += inst->arp_step_dir;
                if (inst->arp_step_idx >= hc) {
                    inst->arp_step_idx = hc - 2;
                    inst->arp_step_dir = -1;
                } else if (inst->arp_step_idx < 0) {
                    inst->arp_step_idx = 1;
                    inst->arp_step_dir = 1;
                }
            }
            break;
        case ARP_RANDOM:
            inst->arp_step_idx = rand() % hc;
            break;
    }
    if (inst->arp_step_idx < 0) inst->arp_step_idx = 0;
    if (inst->arp_step_idx >= hc) inst->arp_step_idx = hc - 1;

    bool fire = inst->arp_pattern[inst->arp_pattern_pos];
    inst->arp_pattern_pos = (inst->arp_pattern_pos + 1) % inst->arp_euclid_steps;
    if (inst->arp_pattern_pos == 0 && inst->arp_variations > 1) {
        inst->arp_variation_idx = (inst->arp_variation_idx + 1) % inst->arp_variations;
    }
    if (fire) {
        int note = inst->held_notes[inst->arp_step_idx]
                 + inst->arp_variation_idx * inst->arp_variation_interval;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        chord_on(inst, note, 100);
    }
}

static void arp_recompute(chordism_instance_t *inst) {
    float bpm = ARP_BPM_MIN + (ARP_BPM_MAX - ARP_BPM_MIN) * inst->arp_tempo;
    /* Quarter note period in samples = 60/BPM * SR. */
    float period = (60.0f / bpm) * SAMPLE_RATE;
    int p = (int)period;
    if (p < 64) p = 64;
    inst->arp_step_period = p;
}

/* Euclidean rhythm — distribute beats evenly across steps using
 * (i*beats)/steps modular comparison. */
static void arp_euclid_recompute(chordism_instance_t *inst) {
    int s = inst->arp_euclid_steps;
    int b = inst->arp_euclid_beats;
    if (s < 1) s = 1;
    if (s > 16) s = 16;
    if (b < 0) b = 0;
    if (b > s) b = s;
    for (int i = 0; i < 16; ++i) {
        if (i >= s) { inst->arp_pattern[i] = false; continue; }
        if (b == 0) {
            /* 0 beats = all-on pattern (treat 0 as "no euclidean filtering"). */
            inst->arp_pattern[i] = true;
            continue;
        }
        inst->arp_pattern[i] = (((i * b) % s) < b);
    }
}

static void sweep_recompute(chordism_instance_t *inst) {
    /* Time exp-mapped: rate=0 → slow (4s), rate=1 → fast (10ms). */
    float ratio = SWEEP_TIME_MAX_S / SWEEP_TIME_MIN_S;
    float time_s = SWEEP_TIME_MAX_S / powf(ratio, inst->sweep_rate);
    float samples = time_s * SAMPLE_RATE;
    if (samples < 1.0f) samples = 1.0f;
    inst->sweep_coef = expf(-5.0f / samples);
}

/* Exp-map filter cutoff. */
static float filter_cutoff_to_hz(float cutoff01) {
    float ratio = FILTER_CUTOFF_MAX_HZ / FILTER_CUTOFF_MIN_HZ;
    return FILTER_CUTOFF_MIN_HZ * powf(ratio, cutoff01);
}

/* Recompute TPT SVF coefficients. Stable for any g and k. */
static void filter_recompute(chordism_instance_t *inst) {
    float hz = filter_cutoff_to_hz(inst->filter_cutoff);
    float max_hz = SAMPLE_RATE * 0.49f;
    if (hz > max_hz) hz = max_hz;
    if (hz < 1.0f) hz = 1.0f;

    float g = tanf(3.14159265358979f * hz / SAMPLE_RATE);

    /* Q ramps from broad (FILTER_Q_MIN) to resonant (FILTER_Q_MAX). */
    float Q = FILTER_Q_MIN + (FILTER_Q_MAX - FILTER_Q_MIN) * inst->filter_resonance;
    if (Q < 0.05f) Q = 0.05f;
    float k = 1.0f / Q;

    inst->filter_k  = k;
    inst->filter_a1 = 1.0f / (1.0f + g * (g + k));
    inst->filter_a2 = g * inst->filter_a1;
    inst->filter_a3 = g * inst->filter_a2;
}

/* TPT SVF: stable for any g (tan-mapped cutoff) and k (1/Q).
 * Andy Simper / Vadim Zavalishin formulation.
 * mode: FILT_LP / FILT_HP / FILT_BP. */
static inline float svf_process(SVF *s, float input, int mode,
                                float a1, float a2, float a3, float k) {
    float v3 = input - s->ic2eq;
    float v1 = a1 * s->ic1eq + a2 * v3;
    float v2 = s->ic2eq + a2 * s->ic1eq + a3 * v3;
    s->ic1eq = 2.0f * v1 - s->ic1eq;
    s->ic2eq = 2.0f * v2 - s->ic2eq;

    switch (mode) {
        case FILT_HP: return input - k * v1 - v2;
        case FILT_BP: return v1;
        case FILT_LP:
        default:      return v2;
    }
}

/* ---------------------------------------------------------------------------
 * Warm 4-pole lowpass ladder — zero-delay (ZDF/TPT) algebraic feedback solve,
 * tanh input drive + soft output for analog character. Stable and resonance is
 * cutoff-independent (the algebraic solve removes the unit-delay that makes
 * naive ladders ring/blow up at high cutoff). Standard published technique
 * (Zavalishin); coefficients are our own. Used for the LP filter mode; BP/HP
 * stay on the clean SVF. Self-oscillates musically at max resonance, bounded
 * by the output saturator so it never runs away. */
static inline float ladder_lp_process(LadderLP *L, float in, float hz,
                                      float res, float drive) {
    float g = tanf(3.14159265358979f * hz / SAMPLE_RATE);
    if (g > 10.0f) g = 10.0f;
    float G = g / (1.0f + g);
    float G2 = G * G, G4 = G2 * G2;
    float k = res * 3.8f;                         /* -> self-oscillation near res=1 */
    float x = (drive > 0.0f) ? tanhf(in * (1.0f + drive * 3.0f)) : in;
    /* zero-delay feedback: u = (x - k*S) / (1 + k*G^4) */
    float S = G2 * G * (1.0f - G) * L->s[0] + G2 * (1.0f - G) * L->s[1]
            + G * (1.0f - G) * L->s[2] + (1.0f - G) * L->s[3];
    float u = (x - k * S) / (1.0f + k * G4);
    float y0 = G * (u  - L->s[0]) + L->s[0]; L->s[0] = 2.0f * y0 - L->s[0];
    float y1 = G * (y0 - L->s[1]) + L->s[1]; L->s[1] = 2.0f * y1 - L->s[1];
    float y2 = G * (y1 - L->s[2]) + L->s[2]; L->s[2] = 2.0f * y2 - L->s[2];
    float y3 = G * (y2 - L->s[3]) + L->s[3]; L->s[3] = 2.0f * y3 - L->s[3];
    float out = tanhf(y3 * (1.6f + res * 1.2f)); /* soft output: warmth + spike tame */
    if (!isfinite(out)) { L->s[0]=L->s[1]=L->s[2]=L->s[3]=0.0f; out = 0.0f; }
    return out;
}

/* --- Dattorro plate reverb helpers --- */
static inline float drev_ap(float *buf, int len, int *idx, float in, float c) {
    float d = buf[*idx];
    float v = in - c * d;
    float out = d + c * v;
    buf[*idx] = v; *idx = (*idx + 1) % len;
    return out;
}
static inline float drev_dl_read(float *buf, int len, int idx, int back) {
    int i = idx - back; while (i < 0) i += len; return buf[i % len];
}
/* Modulated allpass (buffer sized baselen+20 for the excursion). */
static inline float drev_modap(float *buf, int baselen, int *idx, float in, float c, float exc) {
    int span = baselen + 20;
    float pos = exc; int bk = baselen + (int)pos;
    int i = *idx - bk; while (i < 0) i += span;
    float frac = pos - (float)(int)pos;
    float a = buf[i % span], b = buf[(i + 1) % span];
    float d = a + (b - a) * frac;
    float v = in - c * d;
    float out = d + c * v;
    buf[*idx] = v; *idx = (*idx + 1) % span;
    return out;
}

/* Process one stereo sample of the plate. decay 0..~0.9 (RT60), damp 0..1
 * (HF damping), modd = chorus excursion (samples), modrate = chorus LFO Hz.
 * Returns the wet signal. */
static void dattorro_process(DattorroReverb *R, float in, float decay,
                             float damp, float modd, float modrate,
                             float *wetL, float *wetR) {
    R->bw += 0.5f * (in - R->bw);
    float x = R->bw;
    x = drev_ap(R->d142, 142, &R->i142, x, 0.75f);
    x = drev_ap(R->d107, 107, &R->i107, x, 0.75f);
    x = drev_ap(R->d379, 379, &R->i379, x, 0.625f);
    x = drev_ap(R->d277, 277, &R->i277, x, 0.625f);

    /* Plate chorus modulation. Rate is caller-controlled (~0.4..3 Hz); kept
     * slow so long tails shimmer rather than flutter. */
    R->lphase += modrate / SAMPLE_RATE; if (R->lphase >= 1.0f) R->lphase -= 1.0f;
    float exc = (0.5f + 0.5f * sinf(6.2831853f * R->lphase)) * modd;
    float lastA = drev_dl_read(R->dl2a, 3720, R->idl2a, 0);
    float lastB = drev_dl_read(R->dl2b, 3163, R->idl2b, 0);

    /* left half */
    float la = x + decay * lastB;
    la = drev_modap(R->apa, 672, &R->iapa, la, 0.7f, exc);
    R->dla[R->idla] = la; float ta = R->dla[R->idla]; R->idla = (R->idla + 1) % 4453;
    R->damp_a += (1.0f - damp) * (ta - R->damp_a); ta = R->damp_a;
    ta = drev_ap(R->ap2a, 1800, &R->iap2a, ta, 0.5f);
    R->dl2a[R->idl2a] = ta * decay; R->idl2a = (R->idl2a + 1) % 3720;

    /* right half */
    float rb = x + decay * lastA;
    rb = drev_modap(R->apb, 908, &R->iapb, rb, 0.7f, exc);
    R->dlb[R->idlb] = rb; float tb = R->dlb[R->idlb]; R->idlb = (R->idlb + 1) % 4217;
    R->damp_b += (1.0f - damp) * (tb - R->damp_b); tb = R->damp_b;
    tb = drev_ap(R->ap2b, 2656, &R->iap2b, tb, 0.5f);
    R->dl2b[R->idl2b] = tb * decay; R->idl2b = (R->idl2b + 1) % 3163;

    /* output taps from the opposite half (Dattorro-style stereo spread) */
    float l = drev_dl_read(R->dlb, 4217, R->idlb, 266) + drev_dl_read(R->dlb, 4217, R->idlb, 2974)
            - drev_dl_read(R->ap2b, 2656, R->iap2b, 1913) + drev_dl_read(R->dl2b, 3163, R->idl2b, 1996);
    float r = drev_dl_read(R->dla, 4453, R->idla, 353) + drev_dl_read(R->dla, 4453, R->idla, 3627)
            - drev_dl_read(R->ap2a, 1800, R->iap2a, 1228) + drev_dl_read(R->dl2a, 3720, R->idl2a, 2673);
    *wetL = 0.6f * l;
    *wetR = 0.6f * r;
}

/* Reflective wavefolder — folds input back across [-1, +1] (Buchla style). */
static float wavefold(float x) {
    /* Iterative reflection; bounded loop count for safety. */
    for (int i = 0; i < 8; ++i) {
        if (x > 1.0f)       x = 2.0f - x;
        else if (x < -1.0f) x = -2.0f - x;
        else break;
    }
    return x;
}

static float osc_sample(int waveform, float phase, float phase_inc, float shape) {
    switch (waveform) {
        case WAVE_OFF:
            return 0.0f;

        case WAVE_SINE: {
            /* shape drives a wavefolder. gain ramps from 1 (no fold) to 6
             * (heavy fold, complex harmonics). */
            float gain = 1.0f + shape * 5.0f;
            return wavefold(gain * sinf(TWO_PI * phase));
        }

        case WAVE_TRIANGLE: {
            /* shape tilts the triangle toward sawtooth. peak position
             * moves from 0.5 (centered tri) to 1e-4 (falling saw). */
            float peak = 0.5f * (1.0f - shape);
            if (peak < 1e-4f) peak = 1e-4f;
            if (phase < peak) {
                return (phase / peak) * 2.0f - 1.0f;
            }
            return 1.0f - ((phase - peak) / (1.0f - peak)) * 2.0f;
        }

        case WAVE_SAW: {
            /* Fundamental saw plus an octave-up saw, crossfaded by shape.
             * shape=0: pure fundamental. shape=1: pure octave. */
            float s = 2.0f * phase - 1.0f;
            s -= poly_blep(phase, phase_inc);

            if (shape > 0.0f) {
                float p2 = phase * 2.0f;
                if (p2 >= 1.0f) p2 -= 1.0f;
                float s2 = 2.0f * p2 - 1.0f;
                s2 -= poly_blep(p2, phase_inc * 2.0f);
                s = s * (1.0f - shape) + s2 * shape;
            }
            return s;
        }

        case WAVE_SQUARE: {
            /* shape controls pulse width: 0 → 5%, 0.5 → 50%, 1 → 95%. */
            float pw = 0.05f + shape * 0.9f;
            float t2 = phase + (1.0f - pw);
            if (t2 >= 1.0f) t2 -= 1.0f;

            float s = (phase < pw) ? 1.0f : -1.0f;
            s += poly_blep(phase, phase_inc);
            s -= poly_blep(t2, phase_inc);
            return s;
        }

        case WAVE_PULSE_TRAIN: {
            /* Narrow pulse train: PWM constrained to 2%..30% — thinner and
             * buzzier than the regular Square's full PWM range. */
            float pw = 0.02f + shape * 0.28f;
            float t2 = phase + (1.0f - pw);
            if (t2 >= 1.0f) t2 -= 1.0f;
            float s = (phase < pw) ? 1.0f : -1.0f;
            s += poly_blep(phase, phase_inc);
            s -= poly_blep(t2, phase_inc);
            return s;
        }

        case WAVE_WAVETABLE: {
            /* Sum-of-harmonics with shape-controlled bandwidth. Up to 8
             * partials. Cheaper than embedding a real wavetable, and lets
             * shape act as a brightness control. */
            float s = sinf(TWO_PI * phase);
            int N = 1 + (int)(shape * 7.0f);   /* 1..8 partials */
            for (int h = 2; h <= N; ++h) {
                s += sinf(TWO_PI * phase * (float)h) / (float)h;
            }
            return s * 0.5f;   /* approximate normalization */
        }

        default:
            return 0.0f;
    }
}

static void aenv_recompute_rates(ADEnv *env, float attack01, float decay01) {
    float attack_s = env_time_exp(attack01, ATTACK_MIN_S, ATTACK_MAX_S);
    float decay_s = env_time_exp(decay01, RELEASE_MIN_S, RELEASE_MAX_S);

    float attack_samples = attack_s * SAMPLE_RATE;
    if (attack_samples < 1.0f) attack_samples = 1.0f;
    env->attack_inc = 1.0f / attack_samples;

    float decay_samples = decay_s * SAMPLE_RATE;
    if (decay_samples < 1.0f) decay_samples = 1.0f;
    env->decay_coef = expf(-5.0f / decay_samples);
}

/* Mode-aware tick: AD auto-decays; ASR sustains at peak while gate; Looping
 * cycles attack-decay while gate. Reuses ENV_RELEASE as decay stage. */
static inline float aenv_tick(chordism_instance_t *inst, ADEnv *env) {
    int mode = inst->fenv_mode;
    bool gate = inst->held_count > 0;
    switch (env->stage) {
        case ENV_ATTACK:
            env->value += env->attack_inc;
            if (env->value >= 1.0f) {
                env->value = 1.0f;
                env->stage = (mode == ENVMODE_ASR) ? ENV_HOLD : ENV_RELEASE;
            }
            break;
        case ENV_HOLD:
            if (!gate) env->stage = ENV_RELEASE;
            break;
        case ENV_RELEASE:
            env->value *= env->decay_coef;
            if (env->value < ENV_SILENCE) {
                if (gate && mode == ENVMODE_LOOPING) {
                    env->value = 0.0f;
                    env->stage = ENV_ATTACK;
                } else {
                    env->value = 0.0f;
                    env->stage = ENV_IDLE;
                }
            }
            break;
        case ENV_IDLE:
        default:
            break;
    }
    return env->value;
}

static void env_recompute_rates(AREnv *env, float attack01, float release01) {
    float attack_s = env_time_exp(attack01, ATTACK_MIN_S, ATTACK_MAX_S);
    float release_s = env_time_exp(release01, RELEASE_MIN_S, RELEASE_MAX_S);

    float attack_samples = attack_s * SAMPLE_RATE;
    if (attack_samples < 1.0f) attack_samples = 1.0f;
    env->attack_inc = 1.0f / attack_samples;

    float release_samples = release_s * SAMPLE_RATE;
    if (release_samples < 1.0f) release_samples = 1.0f;
    env->release_coef = expf(-5.0f / release_samples);
}

/* Release every active voice. Used on note steal and on All Notes Off.
 * In ASR mode, the per-sample env tick transitions HOLD→RELEASE when gate
 * goes false; in AD mode this is a no-op (already auto-decaying); in
 * Looping mode the tick stops looping and goes to RELEASE. */
static void release_all_voices(chordism_instance_t *inst) {
    for (int i = 0; i < NUM_VOICES; ++i) {
        Voice *v = &inst->voices[i];
        if (v->active) {
            v->gate = false;
            if (v->env.stage == ENV_HOLD) v->env.stage = ENV_RELEASE;
        }
    }
}

static void voice_start(chordism_instance_t *inst, Voice *v,
                        int root_note, int interval_semis,
                        float detune_cents, int velocity,
                        int chord_step) {
    v->active = true;
    v->gate = true;
    v->root_note = root_note;
    v->chord_step = chord_step;
    v->waveform = inst->waveforms[chord_step];
    v->phase = 0.0f;
    float hz = midi_to_hz(root_note + interval_semis);
    if (detune_cents != 0.0f) {
        hz *= powf(2.0f, detune_cents / 1200.0f);
    }
    float target_inc = hz / SAMPLE_RATE;
    v->target_phase_inc = target_inc;

    bool glide_active = inst->has_prev_chord && inst->glide_rate > 0.0f;
    if (glide_active && inst->glide_legato && !inst->prev_chord_still_held) {
        glide_active = false;
    }
    if (glide_active) {
        float start = inst->prev_phase_inc[chord_step];
        float glide_s = inst->glide_rate * GLIDE_TIME_MAX_S;
        float glide_samples = glide_s * SAMPLE_RATE;
        if (glide_samples < 1.0f) glide_samples = 1.0f;
        v->phase_inc = start;
        v->glide_step = (target_inc - start) / glide_samples;
    } else {
        v->phase_inc = target_inc;
        v->glide_step = 0.0f;
    }

    v->velocity = (float)velocity / 127.0f;

    /* Pan offset is fixed per voice (chord position). The render loop applies
     * the live `width` knob value each sample, so pan responds in realtime. */
    float norm = (CHORD_SIZE > 1)
        ? ((float)chord_step / (float)(CHORD_SIZE - 1))   /* 0..1 across chord */
        : 0.5f;
    v->pan_offset = norm - 0.5f;

    env_recompute_rates(&v->env, inst->attack, inst->release);
    /* Hard reset = ON: zero env value so attack ramps from silence (click,
     * but exact retrigger). Hard reset = OFF (default): keep current env
     * value for smoother retrigger (the legacy behavior). */
    if (inst->vca_hard_reset) {
        v->env.value = 0.0f;
    }
    /* With dual-bank steal, claimed voice was usually idle (env.value = 0).
     * If stealing a still-active slot, soft-retrigger means continuing from
     * current value rather than dropping a cliff. */
    v->env.stage = ENV_ATTACK;
}

static void chord_on(chordism_instance_t *inst, int root_note, int velocity) {
    /* Release the previously-active chord. Its voices keep ringing through
     * their decay while the new chord starts in the *other* voice bank —
     * masks retrigger click and lets release tails breathe. */
    release_all_voices(inst);

    int base = inst->next_chord_base;
    /* Pick which chord_type to use based on tuning mode.
     *  0 = Chord Single  → inst->chord_type
     *  1 = Interval      → user intervals (chord_type ignored)
     *  2 = Chord Multi   → per-pitch-class chord_pc[root_note % 12] */
    int chord = inst->chord_type;
    if (inst->tuning_mode == 2) {
        int pc = root_note % 12;
        if (pc < 0) pc += 12;
        chord = inst->chord_pc[pc];
    }
    if (chord < 0) chord = 0;
    if (chord >= NUM_CHORDS) chord = NUM_CHORDS - 1;

    int interval_buf[CHORD_SIZE];
    const int *intervals;
    if (inst->tuning_mode == 1) {
        interval_buf[0] = 0;
        interval_buf[1] = inst->interval_1;
        interval_buf[2] = inst->interval_2;
        interval_buf[3] = inst->interval_3;
        intervals = interval_buf;
    } else {
        intervals = CHORD_TABLE[chord];
    }

    /* Spread scales intervals (0 → unison, 0.5 → original, 1 → 2× wide).
     * Rotation shifts which osc plays which chord degree. */
    float spread_mult = inst->chord_spread * 2.0f;
    int rotation = (int)(inst->chord_rotation * 3.999f);
    if (rotation < 0) rotation = 0;
    if (rotation >= CHORD_SIZE) rotation = CHORD_SIZE - 1;

    for (int i = 0; i < CHORD_SIZE; ++i) {
        Voice *target = &inst->voices[base + i];
        int src = (i + rotation) % CHORD_SIZE;
        int scaled_interval = (int)((float)intervals[src] * spread_mult);
        /* Scale quantizer: snap each chord note to the selected scale.
         * scale_index=0 (Chromatic) leaves notes unchanged. */
        if (inst->scale_index > 0) {
            int target_note = root_note + scaled_interval;
            int quantized = scale_quantize(target_note, inst->scale_index, inst->scale_root);
            scaled_interval = quantized - root_note;
        }
        /* Detune linear: voice 0 = 0¢, voice 1 = +detune*MAX¢, ... */
        float cents = (float)i * inst->detune * MAX_DETUNE_CENTS;
        voice_start(inst, target, root_note, scaled_interval, cents, velocity, i);
    }
    inst->next_chord_base = (base + CHORD_SIZE) % NUM_VOICES;

    /* Save this chord's per-step target as the glide source for the next chord. */
    for (int i = 0; i < CHORD_SIZE; ++i) {
        inst->prev_phase_inc[i] = inst->voices[base + i].target_phase_inc;
    }
    inst->has_prev_chord = true;

    /* Re-trigger filter envelope. Hard reset = ON forces value to 0;
     * otherwise the env starts attacking from wherever it was. */
    aenv_recompute_rates(&inst->filter_env,
                         inst->filter_env_attack, inst->filter_env_decay);
    if (inst->fenv_hard_reset) {
        inst->filter_env.value = 0.0f;
    }
    inst->filter_env.stage = ENV_ATTACK;

    /* Re-trigger vibrato delay ramp (phase stays continuous). */
    inst->vib_ramp_value = 0.0f;

    /* Re-trigger pitch sweep: start at full offset. */
    inst->sweep_value = inst->sweep_amount * SWEEP_MAX_SEMITONES;

    /* LFO mode: note-reset and one-shot both reset phase and clear the
     * "done" latch (one-shot will re-arm on next note). */
    if (inst->shape_lfo_mode != LFOMODE_FREE) {
        inst->shape_lfo.phase = 0.0f;
        inst->shape_lfo_done = false;
    }
    if (inst->filter_lfo_mode != LFOMODE_FREE) {
        inst->filter_lfo_phase = 0.0f;
        inst->filter_lfo_done = false;
    }
    if (inst->lm_lfo_mode != LFOMODE_FREE) {
        inst->lm_lfo_phase = 0.0f;
        inst->lm_lfo_done = false;
    }
    if (inst->pm_lfo_mode != LFOMODE_FREE) {
        inst->pm_lfo_phase = 0.0f;
        inst->pm_lfo_done = false;
    }
}

static void chord_off(chordism_instance_t *inst, int root_note) {
    /* Mark matching voices as gate-off. The env tick handles HOLD→RELEASE
     * and stops Looping mode in the next sample. */
    for (int i = 0; i < NUM_VOICES; ++i) {
        Voice *v = &inst->voices[i];
        if (v->active && v->root_note == root_note) {
            v->gate = false;
            if (v->env.stage == ENV_HOLD) v->env.stage = ENV_RELEASE;
        }
    }
}

/* Held-note stack helpers. */
static void held_push(chordism_instance_t *inst, int note) {
    /* Remove any previous occurrence so duplicates don't pile up. */
    for (int i = 0; i < inst->held_count; ++i) {
        if (inst->held_notes[i] == note) {
            for (int j = i; j < inst->held_count - 1; ++j) {
                inst->held_notes[j] = inst->held_notes[j + 1];
            }
            inst->held_count--;
            break;
        }
    }
    if (inst->held_count < HELD_STACK_MAX) {
        inst->held_notes[inst->held_count++] = note;
    }
}

/* Returns true if note was on top of stack (currently playing); false if it
 * was elsewhere or absent. */
static bool held_pop(chordism_instance_t *inst, int note) {
    if (inst->held_count == 0) return false;
    int top = inst->held_notes[inst->held_count - 1];
    for (int i = 0; i < inst->held_count; ++i) {
        if (inst->held_notes[i] == note) {
            for (int j = i; j < inst->held_count - 1; ++j) {
                inst->held_notes[j] = inst->held_notes[j + 1];
            }
            inst->held_count--;
            return (note == top);
        }
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* v2 API callbacks                                                          */
/* ------------------------------------------------------------------------- */

static void* v2_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    auto *inst = (chordism_instance_t*)calloc(1, sizeof(chordism_instance_t));
    if (!inst) return nullptr;
    if (module_dir) {
        strncpy(inst->module_dir, module_dir, sizeof(inst->module_dir) - 1);
    }

    /* Adopt the host's sample rate before any rate-derived state is computed
     * below. Bounds-checked so a missing/garbage value falls back to 44100. */
    if (g_host && g_host->sample_rate >= 8000 && g_host->sample_rate <= 384000) {
        SAMPLE_RATE = (float)g_host->sample_rate;
    }

    inst->attack = 0.05f;
    inst->release = 0.30f;
    inst->volume = 0.80f;
    for (int i = 0; i < CHORD_SIZE; ++i) {
        inst->waveforms[i] = WAVE_SINE;
        inst->shapes[i] = 0.0f;
    }
    inst->morph_index = 0.0f;
    inst->morph_intensity = 0.0f;
    morph_recompute(inst);
    inst->pan_morph_index = 0.0f;
    inst->pan_morph_intensity = 0.0f;
    pan_morph_recompute(inst);
    inst->chord_spread = 0.5f;       /* 1.0× multiplier */
    inst->chord_rotation = 0.0f;
    inst->fm_modulator_idx = 0;
    inst->fm_amount = 0.0f;
    for (int i = 0; i < CHORD_SIZE; ++i) inst->fm_amounts[i] = 0.5f;  /* default mid */
    inst->last_modulator_sample = 0.0f;
    for (int i = 0; i < CHORD_SIZE; ++i) inst->mixer_trims[i] = 1.0f;
    inst->vib_osc_enable = 0xF;      /* all 4 on by default */
    inst->sweep_osc_enable = 0xF;
    inst->lm_lfo_rate = 0.0f;
    inst->lm_lfo_depth = 0.0f;
    inst->lm_lfo_shape = LFO_TRIANGLE;
    inst->lm_lfo_phase = 0.0f;
    inst->lm_lfo_phase_inc = lfo_rate_to_hz(0.0f) / SAMPLE_RATE;
    inst->pm_lfo_rate = 0.0f;
    inst->pm_lfo_depth = 0.0f;
    inst->pm_lfo_shape = LFO_TRIANGLE;
    inst->pm_lfo_phase = 0.0f;
    inst->pm_lfo_phase_inc = lfo_rate_to_hz(0.0f) / SAMPLE_RATE;
    inst->amp_lfo_rate = 0.0f;
    inst->amp_lfo_depth = 0.0f;
    inst->amp_lfo_shape = LFO_TRIANGLE;
    inst->amp_lfo_phase = 0.0f;
    inst->amp_lfo_phase_inc = lfo_rate_to_hz(0.0f) / SAMPLE_RATE;
    inst->vca_mode = ENVMODE_ASR;     /* matches legacy behavior (HOLD = sustain) */
    inst->vca_hard_reset = 0;
    inst->vca_drone = 0;
    inst->fenv_mode = ENVMODE_AD;     /* filter env auto-decays */
    inst->fenv_hard_reset = 0;
    inst->quality_position = 0;
    inst->reverb_shimmer = 0.5f;
    inst->reverb_lowcut = 0.0f;
    inst->reverb_size = 0.5f;
    inst->reverb_mod_rate = 0.0f;
    inst->reverb_mod_depth = 0.0f;
    inst->reverb_hp_l = 0.0f;
    inst->reverb_hp_r = 0.0f;
    inst->delay_tone_hi = 0.7f;
    inst->delay_tone_lo = 0.0f;
    inst->delay_mod_rate = 0.0f;
    inst->delay_mod_depth = 0.0f;
    inst->delay_mod_phase = 0.0f;
    inst->delay_mod_phase_inc = 0.5f / SAMPLE_RATE;
    inst->delay_hp_l = 0.0f;
    inst->delay_hp_r = 0.0f;
    inst->delay_grain_pos_l = 0;
    inst->delay_grain_pos_r = 0;
    inst->delay_grain_phase = 0.0f;
    inst->lfo_rate = 0.0f;
    inst->lfo_depth = 0.0f;
    inst->lfo_shape = LFO_TRIANGLE;
    inst->chord_type = CHORD_MAJOR;
    inst->detune = 0.0f;
    inst->width = 1.0f;               /* full stereo spread by default */
    inst->vib_speed = 0.5f;           /* ~1 Hz */
    inst->vib_depth = 0.0f;           /* disabled by default */
    inst->vib_delay = 0.2f;
    inst->vibrato_phase = 0.0f;
    inst->vib_ramp_value = 0.0f;
    vibrato_recompute(inst);
    inst->sweep_amount = 0.0f;
    inst->sweep_rate = 0.5f;
    inst->sweep_value = 0.0f;
    sweep_recompute(inst);
    inst->held_count = 0;
    inst->aftertouch = 0.0f;
    inst->glide_rate = 0.0f;
    inst->glide_legato = 0;
    inst->has_prev_chord = false;
    inst->prev_chord_still_held = false;
    for (int i = 0; i < CHORD_SIZE; ++i) inst->prev_phase_inc[i] = 0.0f;
    inst->vib_stray = 0;
    inst->vib_random_value = 0.0f;
    inst->vib_random_target = 0.0f;
    inst->vib_random_counter = 0;
    inst->preset_index = 0;
    preset_apply(inst, 0);  /* Init preset */
    inst->arp_enabled = 0;
    inst->arp_tempo = 0.4f;            /* ~120 BPM */
    inst->arp_direction = ARP_UP;
    inst->arp_step_idx = 0;
    inst->arp_step_dir = 1;
    inst->arp_sample_counter = 0;
    inst->arp_hold = 0;
    inst->arp_euclid_steps = 16;
    inst->arp_euclid_beats = 0;   /* 0 = all-on (no euclidean filtering) */
    inst->arp_variation_interval = 0;
    inst->arp_variations = 1;
    inst->arp_pattern_pos = 0;
    inst->arp_variation_idx = 0;
    inst->arp_clock_sync = 0;
    inst->arp_clock_division = 2;     /* 1/8 default */
    inst->arp_clock_count = 0;
    inst->arp_clock_running = false;
    inst->shape_lfo_mode = LFOMODE_FREE;
    inst->filter_lfo_mode = LFOMODE_FREE;
    inst->lm_lfo_mode = LFOMODE_FREE;
    inst->pm_lfo_mode = LFOMODE_FREE;
    inst->shape_lfo_done = false;
    inst->filter_lfo_done = false;
    inst->lm_lfo_done = false;
    inst->pm_lfo_done = false;
    inst->fm_position = 0;
    inst->scale_index = 0;          /* chromatic — no quantization */
    inst->scale_root = 0;           /* C */
    inst->tuning_mode = 0;          /* Chord (Single) */
    inst->interval_1 = 4;           /* major third */
    inst->interval_2 = 7;           /* perfect fifth */
    inst->interval_3 = 12;          /* octave */
    for (int i = 0; i < 12; ++i) inst->chord_pc[i] = CHORD_MAJOR;
    inst->shape_lfo_phase_offsets[0] = 0.0f;
    inst->shape_lfo_phase_offsets[1] = 0.25f;
    inst->shape_lfo_phase_offsets[2] = 0.5f;
    inst->shape_lfo_phase_offsets[3] = 0.75f;
    inst->ctrl_source = CTRL_AFTERTOUCH;
    inst->ctrl_cc = 1;              /* MIDI CC 1 = modulation wheel */
    inst->ctrl_value = 0.0f;
    inst->ctrl_value_target = 0.0f;
    inst->vel_baseline = 0.0f;
    inst->at_smooth = 0.0f;
    inst->ctrl_to_cutoff = 0.0f;
    inst->ctrl_to_morph = 0.0f;
    inst->ctrl_to_vib = 0.0f;
    inst->ctrl_to_shape = 0.0f;
    inst->ctrl_to_fm = 0.0f;
    arp_recompute(inst);
    arp_euclid_recompute(inst);
    inst->reverb_mix = 0.0f;          /* dry by default */
    inst->reverb_decay = 0.5f;
    inst->reverb_damp = 0.3f;
    reverb_init(inst);
    inst->grind = 0.0f;
    inst->bit_shift = 0.0f;
    inst->decimator = 0.0f;
    inst->decim_counter = 0;
    inst->decim_hold_l = 0.0f;
    inst->decim_hold_r = 0.0f;
    inst->delay_mix = 0.0f;
    inst->delay_time = 0.3f;
    inst->delay_feedback = 0.4f;
    inst->delay_tone = 0.7f;
    inst->delay_mode = DELAY_STEREO;
    inst->delay_write_idx = 0;
    inst->delay_lp_l = 0.0f;
    inst->delay_lp_r = 0.0f;
    inst->delay_flip_counter = 0;
    memset(inst->delay_buf_l, 0, sizeof(inst->delay_buf_l));
    memset(inst->delay_buf_r, 0, sizeof(inst->delay_buf_r));
    inst->filter_cutoff = 1.0f;       /* wide open by default */
    inst->filter_resonance = 0.0f;    /* no resonance */
    inst->filter_mode = FILT_LP;
    inst->filter_slope = 0;           /* 12 dB by default */
    inst->filter_env_attack = 0.0f;
    inst->filter_env_decay = 0.30f;
    inst->filter_env_depth = 0.0f;    /* disabled by default */
    inst->filter_env.stage = ENV_IDLE;
    inst->filter_env.value = 0.0f;
    aenv_recompute_rates(&inst->filter_env,
                         inst->filter_env_attack, inst->filter_env_decay);
    inst->filter_l_b.ic1eq = 0.0f;
    inst->filter_l_b.ic2eq = 0.0f;
    inst->filter_r_b.ic1eq = 0.0f;
    inst->filter_r_b.ic2eq = 0.0f;
    inst->filter_lfo_rate = 0.0f;
    inst->filter_lfo_depth = 0.0f;
    inst->filter_lfo_spread = 0.0f;
    inst->filter_lfo_shape = LFO_TRIANGLE;
    inst->filter_lfo_phase = 0.0f;
    inst->filter_lfo_phase_inc = lfo_rate_to_hz(inst->filter_lfo_rate) / SAMPLE_RATE;
    inst->drive = 0.0f;               /* clean by default */
    inst->filter_l.ic1eq = 0.0f;
    inst->filter_l.ic2eq = 0.0f;
    inst->filter_r.ic1eq = 0.0f;
    inst->filter_r.ic2eq = 0.0f;
    filter_recompute(inst);
    inst->shape_lfo.phase = 0.0f;
    inst->shape_lfo.phase_inc = lfo_rate_to_hz(inst->lfo_rate) / SAMPLE_RATE;
    inst->shape_lfo.shape = inst->lfo_shape;

    for (int i = 0; i < NUM_VOICES; ++i) {
        inst->voices[i].env.stage = ENV_IDLE;
        env_recompute_rates(&inst->voices[i].env, inst->attack, inst->release);
    }

    if (g_host && g_host->log) {
        g_host->log("[chordism] instance created (4-voice)");
    }
    return inst;
}

static void v2_destroy_instance(void *instance) {
    free(instance);
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    if (!instance || !msg) return;
    auto *inst = (chordism_instance_t*)instance;

    uint8_t raw = msg[0];

    /* System real-time messages (single byte, no channel). */
    if (raw == 0xF8) {
        /* MIDI clock tick (24 per quarter note). */
        if (inst->arp_enabled && inst->arp_clock_sync && inst->arp_clock_running
                && inst->held_count > 0) {
            inst->arp_clock_count++;
            /* Division → clock ticks per arp step. */
            static const int clocks_per_step[6] = { 24, 16, 12, 8, 6, 3 };
            int target = clocks_per_step[
                inst->arp_clock_division < 6 ? inst->arp_clock_division : 5];
            if (inst->arp_clock_count >= target) {
                inst->arp_clock_count = 0;
                arp_step_and_fire(inst);
            }
        }
        return;
    }
    if (raw == 0xFA) {  /* Start */
        inst->arp_clock_running = true;
        inst->arp_clock_count = 0;
        return;
    }
    if (raw == 0xFB) {  /* Continue */
        inst->arp_clock_running = true;
        return;
    }
    if (raw == 0xFC) {  /* Stop */
        inst->arp_clock_running = false;
        return;
    }

    if (len < 3) return;

    uint8_t status = raw & 0xF0;
    uint8_t d1 = msg[1] & 0x7F;
    uint8_t d2 = msg[2] & 0x7F;

    if (status == 0x90 && d2 > 0) {
        /* Legato flag: a previous chord is still ringing if any pad is held.
         * Capture this BEFORE pushing the new note to the stack. */
        inst->prev_chord_still_held = (inst->held_count > 0);
        held_push(inst, d1);

        /* Update modulation source value for note-triggered sources. */
        switch (inst->ctrl_source) {
            case CTRL_RANDOM:
                inst->ctrl_value_target = ((float)(rand() & 0xFFFF) / 32768.0f) - 1.0f;
                break;
            case CTRL_COIN_TOSS:
                inst->ctrl_value_target = (rand() & 1) ? 1.0f : -1.0f;
                break;
            case CTRL_VELOCITY:
                inst->ctrl_value_target = (float)d2 / 127.0f;
                break;
            case CTRL_AFTERTOUCH:
                /* Seed a velocity baseline so presets that route aftertouch→cutoff
                 * (e.g. Press Lead, Bloom) open immediately on how hard you strike,
                 * instead of starting closed/silent. Scaled to 0.8 so full velocity
                 * still leaves headroom for aftertouch to push to the top. Snap
                 * ctrl_value too so the response is immediate on the new note. */
                inst->vel_baseline = ((float)d2 / 127.0f) * 0.8f;
                inst->ctrl_value_target = inst->vel_baseline
                    + (1.0f - inst->vel_baseline) * inst->aftertouch;
                inst->ctrl_value = inst->ctrl_value_target;
                break;
            default:
                break;
        }
        if (inst->arp_enabled) {
            /* Fire this note immediately, then schedule next arp tick. */
            chord_on(inst, d1, d2);
            inst->arp_step_idx = inst->held_count - 1;
            inst->arp_step_dir = 1;
            inst->arp_sample_counter = inst->arp_step_period;
        } else {
            chord_on(inst, d1, d2);
        }
    } else if (status == 0x80 || (status == 0x90 && d2 == 0)) {
        /* Arp Hold: latch held notes; ignore note-off. */
        if (inst->arp_enabled && inst->arp_hold) {
            return;
        }
        bool was_top = held_pop(inst, d1);
        if (inst->arp_enabled) {
            /* Arp keeps cycling — don't retrigger here. If stack empties,
             * arp will simply stop firing until next note-on. */
            if (inst->held_count == 0) {
                chord_off(inst, d1);
            }
        } else if (was_top) {
            /* Mono-priority return to previously-held. */
            if (inst->held_count > 0) {
                int prev = inst->held_notes[inst->held_count - 1];
                chord_on(inst, prev, 100);
            } else {
                chord_off(inst, d1);
            }
        }
    } else if (status == 0xA0) {
        /* Polyphonic aftertouch (Move's pads) — d1 = note, d2 = pressure.
         * Chordism is mono (last-note priority), so only read pressure from the
         * note that is actually sounding (top of the held stack). Otherwise a
         * second held pad's pressure stream fights the first and garbles the
         * modulation. */
        if (inst->held_count > 0 &&
            d1 == inst->held_notes[inst->held_count - 1]) {
            inst->aftertouch = (float)d2 / 127.0f;
            if (inst->ctrl_source == CTRL_AFTERTOUCH) {
                inst->ctrl_value_target = inst->vel_baseline
                    + (1.0f - inst->vel_baseline) * inst->aftertouch;
            }
        }
    } else if (status == 0xD0) {
        /* Channel aftertouch — single-byte pressure in d1. */
        inst->aftertouch = (float)d1 / 127.0f;
        if (inst->ctrl_source == CTRL_AFTERTOUCH) {
            inst->ctrl_value_target = inst->vel_baseline
                + (1.0f - inst->vel_baseline) * inst->aftertouch;
        }
    } else if (status == 0xB0) {
        if (d1 == 123) {
            /* All notes off */
            inst->held_count = 0;
            release_all_voices(inst);
        } else if (inst->ctrl_source == CTRL_CC && d1 == inst->ctrl_cc) {
            /* Selected MIDI CC → modulation source. */
            inst->ctrl_value_target = (float)d2 / 127.0f;
        }
    }
}

/* ------------------------------------------------------------------ *
 * State serialization
 *
 * The host persists a chain slot by calling get_param("state") and
 * restores it with set_param("state", <json>).  Without these the slot
 * reloads with constructor defaults — i.e. the init patch (2026-08).
 *
 * Both directions route through the existing per-key set_param /
 * get_param dispatch so the two can't drift apart: the table below is
 * the single list of what persists.  Every key here is handled by both
 * functions; the "waveform"/"shape" compat aliases are deliberately
 * excluded (they fan out to the per-osc keys, which we save instead).
 *
 * Order matters on restore: "preset" comes first so per-param values
 * override the preset it applies, and arp_euclid_steps precedes
 * arp_euclid_beats because the beats setter clamps against steps.
 * ------------------------------------------------------------------ */
static const char *STATE_KEYS[] = {
    "preset",
    /* Oscillators / chord */
    "wave_1", "wave_2", "wave_3", "wave_4",
    "shape_1", "shape_2", "shape_3", "shape_4",
    "mix_1", "mix_2", "mix_3", "mix_4",
    "chord_type", "chord_spread", "chord_rotation", "detune", "width",
    "fm_modulator", "fm_amount", "fm_position",
    "fm_amount_1", "fm_amount_2", "fm_amount_3", "fm_amount_4",
    "vib_osc_enable", "sweep_osc_enable",
    /* Filter */
    "filter_cutoff", "filter_resonance", "filter_mode", "filter_slope", "drive",
    "filter_env_attack", "filter_env_decay", "filter_env_depth",
    "fenv_mode", "fenv_hard_reset",
    "filter_lfo_rate", "filter_lfo_depth", "filter_lfo_spread",
    "filter_lfo_shape", "filter_lfo_mode",
    /* Amp */
    "attack", "release", "volume", "vca_mode", "vca_hard_reset", "vca_drone",
    "amp_lfo_rate", "amp_lfo_depth", "amp_lfo_shape",
    /* Modulation */
    "lfo_rate", "lfo_depth", "lfo_shape", "shape_lfo_mode",
    "lfo_phase_1", "lfo_phase_2", "lfo_phase_3", "lfo_phase_4",
    "vib_speed", "vib_depth", "vib_delay", "vib_stray",
    "sweep_amount", "sweep_rate",
    "glide_rate", "glide_legato",
    "morph_index", "morph_intensity",
    "lm_lfo_rate", "lm_lfo_depth", "lm_lfo_shape", "lm_lfo_mode",
    "pan_morph_index", "pan_morph_intensity",
    "pm_lfo_rate", "pm_lfo_depth", "pm_lfo_shape", "pm_lfo_mode",
    /* Reverb / degrade */
    "reverb_mix", "reverb_decay", "reverb_damp", "reverb_shimmer",
    "reverb_lowcut", "reverb_size", "reverb_mod_rate", "reverb_mod_depth",
    "grind", "bit_shift", "decimator", "quality_position",
    /* Delay */
    "delay_mix", "delay_time", "delay_feedback", "delay_tone",
    "delay_tone_hi", "delay_tone_lo", "delay_mode",
    "delay_mod_rate", "delay_mod_depth",
    /* Arp */
    "arp_enabled", "arp_tempo", "arp_direction", "arp_hold",
    "arp_euclid_steps", "arp_euclid_beats",
    "arp_variation_interval", "arp_variations",
    "arp_clock_sync", "arp_clock_division",
    /* Scale / tuning */
    "scale_index", "scale_root", "tuning_mode",
    "interval_1", "interval_2", "interval_3",
    /* Chord map */
    "chord_pc_0", "chord_pc_1", "chord_pc_2", "chord_pc_3",
    "chord_pc_4", "chord_pc_5", "chord_pc_6", "chord_pc_7",
    "chord_pc_8", "chord_pc_9", "chord_pc_10", "chord_pc_11",
    /* Control routing */
    "ctrl_source", "ctrl_cc", "ctrl_to_cutoff", "ctrl_to_morph",
    "ctrl_to_vib", "ctrl_to_shape", "ctrl_to_fm",
};
static const int NUM_STATE_KEYS = (int)(sizeof(STATE_KEYS) / sizeof(STATE_KEYS[0]));

/* Extract a scalar value for `key` from a flat JSON object into `out`.
 * Matching includes both quotes, so "lfo_rate" does not match
 * "filter_lfo_rate". Returns 1 on success, 0 if the key is absent. */
static int state_json_get(const char *json, const char *key, char *out, int out_len) {
    if (!json || !key || !out || out_len <= 0) return 0;

    char needle[64];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || n >= (int)sizeof(needle)) return 0;

    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += n;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    int quoted = (*p == '"');
    if (quoted) p++;

    int i = 0;
    while (*p && i < out_len - 1) {
        if (quoted) {
            if (*p == '"') break;
        } else if (*p == ',' || *p == '}' || *p == ' ' ||
                   *p == '\n' || *p == '\r' || *p == '\t') {
            break;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static void v2_set_param(void *instance, const char *key, const char *val);
static int v2_get_param(void *instance, const char *key, char *buf, int buf_len);

static void state_restore(void *instance, const char *json) {
    if (!instance || !json) return;
    char val[64];
    for (int i = 0; i < NUM_STATE_KEYS; ++i) {
        if (state_json_get(json, STATE_KEYS[i], val, sizeof(val))) {
            v2_set_param(instance, STATE_KEYS[i], val);
        }
    }
}

/* Emit every persistable param as a flat JSON object. Values come back
 * from get_param already formatted as JSON-legal numbers. */
static int state_serialize(void *instance, char *buf, int buf_len) {
    if (!instance || !buf || buf_len < 3) return 0;

    int offset = 0;
    buf[offset++] = '{';

    char val[64];
    for (int i = 0; i < NUM_STATE_KEYS; ++i) {
        int len = v2_get_param(instance, STATE_KEYS[i], val, sizeof(val));
        if (len <= 0) continue;  /* Unknown key — skip rather than emit junk */

        /* "key":value plus a leading comma and the closing brace. */
        int need = (int)strlen(STATE_KEYS[i]) + (int)strlen(val) + 5;
        if (offset + need >= buf_len) break;

        if (offset > 1) buf[offset++] = ',';
        offset += snprintf(buf + offset, buf_len - offset,
                           "\"%s\":%s", STATE_KEYS[i], val);
    }

    if (offset + 2 > buf_len) return 0;
    buf[offset++] = '}';
    buf[offset] = '\0';
    return offset;
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    if (!instance || !key) return;
    auto *inst = (chordism_instance_t*)instance;

    /* State restore from slot autosave / patch load. */
    if (strcmp(key, "state") == 0) {
        state_restore(instance, val);
        return;
    }

    if (strcmp(key, "preset") == 0) {
        if (val) {
            int p = atoi(val);
            if (p < 0) p = 0;
            if (p >= NUM_PRESETS) p = NUM_PRESETS - 1;
            preset_apply(inst, p);
        }
        return;
    }

    if (strcmp(key, "attack") == 0) {
        inst->attack = param_from_string(val, inst->attack);
        for (int i = 0; i < NUM_VOICES; ++i) {
            env_recompute_rates(&inst->voices[i].env, inst->attack, inst->release);
        }
    } else if (strcmp(key, "release") == 0) {
        inst->release = param_from_string(val, inst->release);
        for (int i = 0; i < NUM_VOICES; ++i) {
            env_recompute_rates(&inst->voices[i].env, inst->attack, inst->release);
        }
    } else if (strcmp(key, "volume") == 0) {
        inst->volume = param_from_string(val, inst->volume);
    } else if (strcmp(key, "waveform") == 0) {
        /* Compatibility: "waveform" sets ALL chord steps to the same value. */
        if (val) {
            int w = atoi(val);
            if (w < 0) w = 0;
            if (w >= NUM_WAVEFORMS) w = NUM_WAVEFORMS - 1;
            for (int i = 0; i < CHORD_SIZE; ++i) inst->waveforms[i] = w;
        }
    } else if (strncmp(key, "wave_", 5) == 0 && key[5] >= '1' && key[5] <= '0' + CHORD_SIZE) {
        int idx = key[5] - '1';
        if (val) {
            int w = atoi(val);
            if (w < 0) w = 0;
            if (w >= NUM_WAVEFORMS) w = NUM_WAVEFORMS - 1;
            inst->waveforms[idx] = w;
        }
    } else if (strcmp(key, "shape") == 0) {
        /* Compatibility: sets ALL per-osc shapes. */
        float v = param_from_string(val, inst->shapes[0]);
        for (int i = 0; i < CHORD_SIZE; ++i) inst->shapes[i] = v;
    } else if (strncmp(key, "shape_", 6) == 0 && key[6] >= '1' && key[6] <= '0' + CHORD_SIZE && key[7] == '\0') {
        int idx = key[6] - '1';
        inst->shapes[idx] = param_from_string(val, inst->shapes[idx]);
    } else if (strcmp(key, "pan_morph_index") == 0) {
        inst->pan_morph_index = param_from_string(val, inst->pan_morph_index);
        pan_morph_recompute(inst);
    } else if (strcmp(key, "pan_morph_intensity") == 0) {
        inst->pan_morph_intensity = param_from_string(val, inst->pan_morph_intensity);
        pan_morph_recompute(inst);
    } else if (strcmp(key, "chord_spread") == 0) {
        inst->chord_spread = param_from_string(val, inst->chord_spread);
    } else if (strcmp(key, "chord_rotation") == 0) {
        inst->chord_rotation = param_from_string(val, inst->chord_rotation);
    } else if (strcmp(key, "fm_modulator") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= CHORD_SIZE) m = CHORD_SIZE - 1;
            inst->fm_modulator_idx = m;
        }
    } else if (strcmp(key, "fm_amount") == 0) {
        inst->fm_amount = param_from_string(val, inst->fm_amount);
    } else if (strncmp(key, "fm_amount_", 10) == 0 && key[10] >= '1' && key[10] <= '0' + CHORD_SIZE && key[11] == '\0') {
        int idx = key[10] - '1';
        inst->fm_amounts[idx] = param_from_string(val, inst->fm_amounts[idx]);
    } else if (strncmp(key, "mix_", 4) == 0 && key[4] >= '1' && key[4] <= '0' + CHORD_SIZE && key[5] == '\0') {
        int idx = key[4] - '1';
        inst->mixer_trims[idx] = param_from_string(val, inst->mixer_trims[idx]);
    } else if (strcmp(key, "vib_osc_enable") == 0) {
        if (val) inst->vib_osc_enable = atoi(val) & 0xF;
    } else if (strcmp(key, "sweep_osc_enable") == 0) {
        if (val) inst->sweep_osc_enable = atoi(val) & 0xF;
    } else if (strcmp(key, "lm_lfo_rate") == 0) {
        inst->lm_lfo_rate = param_from_string(val, inst->lm_lfo_rate);
        inst->lm_lfo_phase_inc = lfo_rate_to_hz(inst->lm_lfo_rate) / SAMPLE_RATE;
    } else if (strcmp(key, "lm_lfo_depth") == 0) {
        inst->lm_lfo_depth = param_from_string(val, inst->lm_lfo_depth);
    } else if (strcmp(key, "lm_lfo_shape") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NUM_LFO_SHAPES) s = NUM_LFO_SHAPES - 1;
            inst->lm_lfo_shape = s;
        }
    } else if (strcmp(key, "pm_lfo_rate") == 0) {
        inst->pm_lfo_rate = param_from_string(val, inst->pm_lfo_rate);
        inst->pm_lfo_phase_inc = lfo_rate_to_hz(inst->pm_lfo_rate) / SAMPLE_RATE;
    } else if (strcmp(key, "pm_lfo_depth") == 0) {
        inst->pm_lfo_depth = param_from_string(val, inst->pm_lfo_depth);
    } else if (strcmp(key, "pm_lfo_shape") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NUM_LFO_SHAPES) s = NUM_LFO_SHAPES - 1;
            inst->pm_lfo_shape = s;
        }
    } else if (strcmp(key, "amp_lfo_rate") == 0) {
        inst->amp_lfo_rate = param_from_string(val, inst->amp_lfo_rate);
        inst->amp_lfo_phase_inc = lfo_rate_to_hz(inst->amp_lfo_rate) / SAMPLE_RATE;
    } else if (strcmp(key, "amp_lfo_depth") == 0) {
        inst->amp_lfo_depth = param_from_string(val, inst->amp_lfo_depth);
    } else if (strcmp(key, "amp_lfo_shape") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NUM_LFO_SHAPES) s = NUM_LFO_SHAPES - 1;
            inst->amp_lfo_shape = s;
        }
    } else if (strcmp(key, "vca_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_ENV_MODES) m = NUM_ENV_MODES - 1;
            inst->vca_mode = m;
        }
    } else if (strcmp(key, "vca_hard_reset") == 0) {
        if (val) inst->vca_hard_reset = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "vca_drone") == 0) {
        if (val) inst->vca_drone = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "fenv_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_ENV_MODES) m = NUM_ENV_MODES - 1;
            inst->fenv_mode = m;
        }
    } else if (strcmp(key, "fenv_hard_reset") == 0) {
        if (val) inst->fenv_hard_reset = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "quality_position") == 0) {
        if (val) inst->quality_position = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "reverb_shimmer") == 0) {
        inst->reverb_shimmer = param_from_string(val, inst->reverb_shimmer);
    } else if (strcmp(key, "reverb_lowcut") == 0) {
        inst->reverb_lowcut = param_from_string(val, inst->reverb_lowcut);
    } else if (strcmp(key, "reverb_size") == 0) {
        inst->reverb_size = param_from_string(val, inst->reverb_size);
    } else if (strcmp(key, "reverb_mod_rate") == 0) {
        inst->reverb_mod_rate = param_from_string(val, inst->reverb_mod_rate);
    } else if (strcmp(key, "reverb_mod_depth") == 0) {
        inst->reverb_mod_depth = param_from_string(val, inst->reverb_mod_depth);
    } else if (strcmp(key, "delay_tone_hi") == 0) {
        inst->delay_tone_hi = param_from_string(val, inst->delay_tone_hi);
    } else if (strcmp(key, "delay_tone_lo") == 0) {
        inst->delay_tone_lo = param_from_string(val, inst->delay_tone_lo);
    } else if (strcmp(key, "delay_mod_rate") == 0) {
        inst->delay_mod_rate = param_from_string(val, inst->delay_mod_rate);
        inst->delay_mod_phase_inc = (0.1f + inst->delay_mod_rate * 4.9f) / SAMPLE_RATE;
    } else if (strcmp(key, "delay_mod_depth") == 0) {
        inst->delay_mod_depth = param_from_string(val, inst->delay_mod_depth);
    } else if (strcmp(key, "lfo_rate") == 0) {
        inst->lfo_rate = param_from_string(val, inst->lfo_rate);
        inst->shape_lfo.phase_inc = lfo_rate_to_hz(inst->lfo_rate) / SAMPLE_RATE;
    } else if (strcmp(key, "lfo_depth") == 0) {
        inst->lfo_depth = param_from_string(val, inst->lfo_depth);
    } else if (strcmp(key, "lfo_shape") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NUM_LFO_SHAPES) s = NUM_LFO_SHAPES - 1;
            inst->lfo_shape = s;
            inst->shape_lfo.shape = s;
        }
    } else if (strcmp(key, "chord_type") == 0) {
        if (val) {
            int c = atoi(val);
            if (c < 0) c = 0;
            if (c >= NUM_CHORDS) c = NUM_CHORDS - 1;
            inst->chord_type = c;
        }
    } else if (strcmp(key, "detune") == 0) {
        inst->detune = param_from_string(val, inst->detune);
    } else if (strcmp(key, "width") == 0) {
        inst->width = param_from_string(val, inst->width);
    } else if (strcmp(key, "vib_speed") == 0) {
        inst->vib_speed = param_from_string(val, inst->vib_speed);
        vibrato_recompute(inst);
    } else if (strcmp(key, "vib_depth") == 0) {
        inst->vib_depth = param_from_string(val, inst->vib_depth);
    } else if (strcmp(key, "vib_delay") == 0) {
        inst->vib_delay = param_from_string(val, inst->vib_delay);
        vibrato_recompute(inst);
    } else if (strcmp(key, "sweep_amount") == 0) {
        if (val) {
            char *end = nullptr;
            float v = strtof(val, &end);
            if (end != val) {
                if (v < -1.0f) v = -1.0f;
                if (v > 1.0f) v = 1.0f;
                inst->sweep_amount = v;
            }
        }
    } else if (strcmp(key, "sweep_rate") == 0) {
        inst->sweep_rate = param_from_string(val, inst->sweep_rate);
        sweep_recompute(inst);
    } else if (strcmp(key, "filter_cutoff") == 0) {
        inst->filter_cutoff = param_from_string(val, inst->filter_cutoff);
        filter_recompute(inst);
    } else if (strcmp(key, "filter_resonance") == 0) {
        inst->filter_resonance = param_from_string(val, inst->filter_resonance);
        filter_recompute(inst);
    } else if (strcmp(key, "drive") == 0) {
        inst->drive = param_from_string(val, inst->drive);
    } else if (strcmp(key, "filter_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_FILTER_MODES) m = NUM_FILTER_MODES - 1;
            inst->filter_mode = m;
        }
    } else if (strcmp(key, "filter_slope") == 0) {
        if (val) {
            int s = atoi(val);
            inst->filter_slope = s ? 1 : 0;
        }
    } else if (strcmp(key, "filter_lfo_rate") == 0) {
        inst->filter_lfo_rate = param_from_string(val, inst->filter_lfo_rate);
        inst->filter_lfo_phase_inc = lfo_rate_to_hz(inst->filter_lfo_rate) / SAMPLE_RATE;
    } else if (strcmp(key, "filter_lfo_depth") == 0) {
        inst->filter_lfo_depth = param_from_string(val, inst->filter_lfo_depth);
    } else if (strcmp(key, "filter_lfo_spread") == 0) {
        inst->filter_lfo_spread = param_from_string(val, inst->filter_lfo_spread);
    } else if (strcmp(key, "filter_lfo_shape") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NUM_LFO_SHAPES) s = NUM_LFO_SHAPES - 1;
            inst->filter_lfo_shape = s;
        }
    } else if (strcmp(key, "filter_env_attack") == 0) {
        inst->filter_env_attack = param_from_string(val, inst->filter_env_attack);
        aenv_recompute_rates(&inst->filter_env,
                             inst->filter_env_attack, inst->filter_env_decay);
    } else if (strcmp(key, "filter_env_decay") == 0) {
        inst->filter_env_decay = param_from_string(val, inst->filter_env_decay);
        aenv_recompute_rates(&inst->filter_env,
                             inst->filter_env_attack, inst->filter_env_decay);
    } else if (strcmp(key, "filter_env_depth") == 0) {
        if (val) {
            char *end = nullptr;
            float v = strtof(val, &end);
            if (end != val) {
                if (v < -1.0f) v = -1.0f;
                if (v > 1.0f) v = 1.0f;
                inst->filter_env_depth = v;
            }
        }
    } else if (strcmp(key, "reverb_mix") == 0) {
        inst->reverb_mix = param_from_string(val, inst->reverb_mix);
    } else if (strcmp(key, "reverb_decay") == 0) {
        inst->reverb_decay = param_from_string(val, inst->reverb_decay);
    } else if (strcmp(key, "reverb_damp") == 0) {
        inst->reverb_damp = param_from_string(val, inst->reverb_damp);
    } else if (strcmp(key, "grind") == 0) {
        inst->grind = param_from_string(val, inst->grind);
    } else if (strcmp(key, "bit_shift") == 0) {
        inst->bit_shift = param_from_string(val, inst->bit_shift);
    } else if (strcmp(key, "decimator") == 0) {
        inst->decimator = param_from_string(val, inst->decimator);
    } else if (strcmp(key, "morph_index") == 0) {
        inst->morph_index = param_from_string(val, inst->morph_index);
        morph_recompute(inst);
    } else if (strcmp(key, "morph_intensity") == 0) {
        inst->morph_intensity = param_from_string(val, inst->morph_intensity);
        morph_recompute(inst);
    } else if (strcmp(key, "delay_mix") == 0) {
        inst->delay_mix = param_from_string(val, inst->delay_mix);
    } else if (strcmp(key, "delay_time") == 0) {
        inst->delay_time = param_from_string(val, inst->delay_time);
    } else if (strcmp(key, "delay_feedback") == 0) {
        inst->delay_feedback = param_from_string(val, inst->delay_feedback);
    } else if (strcmp(key, "delay_tone") == 0) {
        inst->delay_tone = param_from_string(val, inst->delay_tone);
    } else if (strcmp(key, "delay_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_DELAY_MODES) m = NUM_DELAY_MODES - 1;
            inst->delay_mode = m;
        }
    } else if (strcmp(key, "glide_rate") == 0) {
        inst->glide_rate = param_from_string(val, inst->glide_rate);
    } else if (strcmp(key, "glide_legato") == 0) {
        if (val) inst->glide_legato = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "vib_stray") == 0) {
        if (val) inst->vib_stray = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "arp_enabled") == 0) {
        if (val) {
            int e = atoi(val);
            inst->arp_enabled = e ? 1 : 0;
            if (!inst->arp_enabled) {
                inst->arp_sample_counter = 0;
            }
        }
    } else if (strcmp(key, "arp_tempo") == 0) {
        inst->arp_tempo = param_from_string(val, inst->arp_tempo);
        arp_recompute(inst);
    } else if (strcmp(key, "arp_direction") == 0) {
        if (val) {
            int d = atoi(val);
            if (d < 0) d = 0;
            if (d >= NUM_ARP_DIRECTIONS) d = NUM_ARP_DIRECTIONS - 1;
            inst->arp_direction = d;
            inst->arp_step_dir = 1;
        }
    } else if (strcmp(key, "arp_hold") == 0) {
        if (val) inst->arp_hold = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "arp_euclid_steps") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 1) s = 1;
            if (s > 16) s = 16;
            inst->arp_euclid_steps = s;
            arp_euclid_recompute(inst);
        }
    } else if (strcmp(key, "arp_euclid_beats") == 0) {
        if (val) {
            int b = atoi(val);
            if (b < 0) b = 0;
            if (b > inst->arp_euclid_steps) b = inst->arp_euclid_steps;
            inst->arp_euclid_beats = b;
            arp_euclid_recompute(inst);
        }
    } else if (strcmp(key, "arp_variation_interval") == 0) {
        if (val) {
            int v = atoi(val);
            if (v < -12) v = -12;
            if (v > 12) v = 12;
            inst->arp_variation_interval = v;
        }
    } else if (strcmp(key, "arp_variations") == 0) {
        if (val) {
            int v = atoi(val);
            if (v < 1) v = 1;
            if (v > 8) v = 8;
            inst->arp_variations = v;
        }
    } else if (strcmp(key, "arp_clock_sync") == 0) {
        if (val) inst->arp_clock_sync = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "arp_clock_division") == 0) {
        if (val) {
            int d = atoi(val);
            if (d < 0) d = 0;
            if (d > 5) d = 5;
            inst->arp_clock_division = d;
        }
    } else if (strcmp(key, "shape_lfo_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_LFO_MODES) m = NUM_LFO_MODES - 1;
            inst->shape_lfo_mode = m;
        }
    } else if (strcmp(key, "filter_lfo_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_LFO_MODES) m = NUM_LFO_MODES - 1;
            inst->filter_lfo_mode = m;
        }
    } else if (strcmp(key, "lm_lfo_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_LFO_MODES) m = NUM_LFO_MODES - 1;
            inst->lm_lfo_mode = m;
        }
    } else if (strcmp(key, "pm_lfo_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m >= NUM_LFO_MODES) m = NUM_LFO_MODES - 1;
            inst->pm_lfo_mode = m;
        }
    } else if (strcmp(key, "fm_position") == 0) {
        if (val) inst->fm_position = atoi(val) ? 1 : 0;
    } else if (strcmp(key, "scale_index") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NUM_SCALES) s = NUM_SCALES - 1;
            inst->scale_index = s;
        }
    } else if (strcmp(key, "tuning_mode") == 0) {
        if (val) {
            int m = atoi(val);
            if (m < 0) m = 0;
            if (m > 2) m = 2;
            inst->tuning_mode = m;
        }
    } else if (strncmp(key, "chord_pc_", 9) == 0) {
        /* chord_pc_0..chord_pc_11 (or 0..9 single digit + 10/11 two digit). */
        int pc = atoi(key + 9);
        if (val && pc >= 0 && pc < 12) {
            int c = atoi(val);
            if (c < 0) c = 0;
            if (c >= NUM_CHORDS) c = NUM_CHORDS - 1;
            inst->chord_pc[pc] = c;
        }
    } else if (strcmp(key, "interval_1") == 0) {
        if (val) {
            int v = atoi(val);
            if (v < -24) v = -24;
            if (v > 24) v = 24;
            inst->interval_1 = v;
        }
    } else if (strcmp(key, "interval_2") == 0) {
        if (val) {
            int v = atoi(val);
            if (v < -24) v = -24;
            if (v > 24) v = 24;
            inst->interval_2 = v;
        }
    } else if (strcmp(key, "interval_3") == 0) {
        if (val) {
            int v = atoi(val);
            if (v < -24) v = -24;
            if (v > 24) v = 24;
            inst->interval_3 = v;
        }
    } else if (strncmp(key, "lfo_phase_", 10) == 0 && key[10] >= '1' && key[10] <= '0' + CHORD_SIZE && key[11] == '\0') {
        int idx = key[10] - '1';
        inst->shape_lfo_phase_offsets[idx] = param_from_string(val, inst->shape_lfo_phase_offsets[idx]);
    } else if (strcmp(key, "scale_root") == 0) {
        if (val) {
            int r = atoi(val);
            while (r < 0) r += 12;
            while (r >= 12) r -= 12;
            inst->scale_root = r;
        }
    } else if (strcmp(key, "ctrl_source") == 0) {
        if (val) {
            int s = atoi(val);
            if (s < 0) s = 0;
            if (s >= NUM_CONTROL_SOURCES) s = NUM_CONTROL_SOURCES - 1;
            inst->ctrl_source = s;
            /* Reset value when source switches. */
            inst->ctrl_value = 0.0f;
    inst->ctrl_value_target = 0.0f;
        }
    } else if (strcmp(key, "ctrl_cc") == 0) {
        if (val) {
            int c = atoi(val);
            if (c < 0) c = 0;
            if (c > 127) c = 127;
            inst->ctrl_cc = c;
        }
    } else if (strcmp(key, "ctrl_to_cutoff") == 0) {
        inst->ctrl_to_cutoff = param_from_string(val, inst->ctrl_to_cutoff);
    } else if (strcmp(key, "ctrl_to_morph") == 0) {
        inst->ctrl_to_morph = param_from_string(val, inst->ctrl_to_morph);
    } else if (strcmp(key, "ctrl_to_vib") == 0) {
        inst->ctrl_to_vib = param_from_string(val, inst->ctrl_to_vib);
    } else if (strcmp(key, "ctrl_to_shape") == 0) {
        inst->ctrl_to_shape = param_from_string(val, inst->ctrl_to_shape);
    } else if (strcmp(key, "ctrl_to_fm") == 0) {
        inst->ctrl_to_fm = param_from_string(val, inst->ctrl_to_fm);
    }
}

/* Shadow UI menu structure — fetched by host via get_param("ui_hierarchy").
 * String entries in `params` are key references; objects are level nav. */
static const char *ui_hierarchy_json =
"{"
  "\"modes\":null,"
  "\"levels\":{"
    "\"root\":{"
      "\"name\":\"Chordism\","
      "\"list_param\":\"preset\","
      "\"count_param\":\"preset_count\","
      "\"name_param\":\"preset_name\","
      "\"children\":null,"
      "\"knobs\":[\"chord_type\",\"width\",\"filter_cutoff\",\"filter_resonance\",\"drive\",\"shape\",\"reverb_mix\",\"volume\"],"
      "\"params\":["
        "\"chord_type\",\"width\",\"filter_cutoff\",\"filter_resonance\","
        "\"drive\",\"shape\",\"reverb_mix\",\"volume\","
        "{\"level\":\"osc\",\"label\":\"Oscillators\"},"
        "{\"level\":\"filter\",\"label\":\"Filter\"},"
        "{\"level\":\"mod\",\"label\":\"Modulation\"},"
        "{\"level\":\"env\",\"label\":\"Envelope\"},"
        "{\"level\":\"fx\",\"label\":\"FX\"},"
        "{\"level\":\"delay\",\"label\":\"Delay\"},"
        "{\"level\":\"arp\",\"label\":\"Arp\"},"
        "{\"level\":\"morph\",\"label\":\"Morph\"},"
        "{\"level\":\"mixer\",\"label\":\"Mixer\"},"
        "{\"level\":\"scale\",\"label\":\"Scale\"},"
        "{\"level\":\"ctrl\",\"label\":\"Ctrl Src\"},"
        "{\"level\":\"chordmulti\",\"label\":\"ChordMulti\"}"
      "]"
    "},"
    "\"osc\":{"
      "\"name\":\"Oscillators\","
      "\"children\":null,"
      "\"knobs\":[\"wave_1\",\"wave_2\",\"wave_3\",\"wave_4\",\"shape_1\",\"shape_2\",\"shape_3\",\"shape_4\"],"
      "\"params\":[\"wave_1\",\"wave_2\",\"wave_3\",\"wave_4\",\"shape_1\",\"shape_2\",\"shape_3\",\"shape_4\",\"chord_type\",\"chord_spread\",\"chord_rotation\",\"detune\",\"width\","
                  "\"morph_index\",\"morph_intensity\",\"pan_morph_index\",\"pan_morph_intensity\",\"fm_modulator\",\"fm_amount\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"filter\":{"
      "\"name\":\"Filter\","
      "\"children\":null,"
      "\"knobs\":[\"filter_cutoff\",\"filter_resonance\",\"filter_mode\",\"filter_slope\",\"filter_env_attack\",\"filter_env_decay\",\"filter_env_depth\",\"fenv_mode\"],"
      "\"params\":[\"filter_cutoff\",\"filter_resonance\",\"filter_mode\",\"filter_slope\",\"filter_env_attack\",\"filter_env_decay\",\"filter_env_depth\",\"fenv_mode\",\"fenv_hard_reset\",\"filter_lfo_rate\",\"filter_lfo_depth\",\"filter_lfo_spread\",\"filter_lfo_shape\",\"drive\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"mod\":{"
      "\"name\":\"Modulation\","
      "\"children\":null,"
      "\"knobs\":[\"lfo_shape\",\"lfo_rate\",\"lfo_depth\",\"vib_depth\",\"vib_speed\",\"vib_stray\",\"sweep_amount\",\"glide_rate\"],"
      "\"params\":[\"lfo_shape\",\"lfo_rate\",\"lfo_depth\",\"vib_depth\",\"vib_speed\",\"vib_delay\",\"vib_stray\",\"sweep_amount\",\"sweep_rate\",\"glide_rate\",\"glide_legato\",\"detune\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"env\":{"
      "\"name\":\"Envelope\","
      "\"children\":null,"
      "\"knobs\":[\"attack\",\"release\",\"volume\",\"vca_mode\",\"amp_lfo_rate\",\"amp_lfo_depth\",\"vca_hard_reset\",\"vca_drone\"],"
      "\"params\":[\"attack\",\"release\",\"volume\",\"vca_mode\",\"amp_lfo_rate\",\"amp_lfo_depth\",\"amp_lfo_shape\",\"vca_hard_reset\",\"vca_drone\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"fx\":{"
      "\"name\":\"FX\","
      "\"children\":null,"
      "\"knobs\":[\"reverb_mix\",\"reverb_decay\",\"reverb_damp\",\"reverb_shimmer\",\"reverb_lowcut\",\"reverb_size\",\"grind\",\"decimator\"],"
      "\"params\":[\"reverb_mix\",\"reverb_decay\",\"reverb_damp\",\"reverb_shimmer\",\"reverb_lowcut\",\"reverb_size\",\"reverb_mod_rate\",\"reverb_mod_depth\",\"grind\",\"bit_shift\",\"decimator\",\"quality_position\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"delay\":{"
      "\"name\":\"Delay\","
      "\"children\":null,"
      "\"knobs\":[\"delay_mix\",\"delay_time\",\"delay_feedback\",\"delay_tone_hi\",\"delay_tone_lo\",\"delay_mode\",\"delay_mod_rate\",\"delay_mod_depth\"],"
      "\"params\":[\"delay_mix\",\"delay_time\",\"delay_feedback\",\"delay_tone\",\"delay_tone_hi\",\"delay_tone_lo\",\"delay_mode\",\"delay_mod_rate\",\"delay_mod_depth\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"arp\":{"
      "\"name\":\"Arp\","
      "\"children\":null,"
      "\"knobs\":[\"arp_enabled\",\"arp_tempo\",\"arp_direction\",\"arp_hold\",\"arp_euclid_steps\",\"arp_euclid_beats\",\"arp_clock_sync\",\"arp_clock_division\"],"
      "\"params\":[\"arp_enabled\",\"arp_tempo\",\"arp_direction\",\"arp_hold\",\"arp_euclid_steps\",\"arp_euclid_beats\",\"arp_variation_interval\",\"arp_variations\",\"arp_clock_sync\",\"arp_clock_division\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"morph\":{"
      "\"name\":\"Morph\","
      "\"children\":null,"
      "\"knobs\":[\"morph_index\",\"morph_intensity\",\"lm_lfo_rate\",\"lm_lfo_depth\",\"pan_morph_index\",\"pan_morph_intensity\",\"pm_lfo_rate\",\"pm_lfo_depth\"],"
      "\"params\":[\"morph_index\",\"morph_intensity\",\"lm_lfo_rate\",\"lm_lfo_depth\",\"lm_lfo_shape\",\"pan_morph_index\",\"pan_morph_intensity\",\"pm_lfo_rate\",\"pm_lfo_depth\",\"pm_lfo_shape\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"mixer\":{"
      "\"name\":\"Mixer\","
      "\"children\":null,"
      "\"knobs\":[\"mix_1\",\"mix_2\",\"mix_3\",\"mix_4\",\"fm_amount_1\",\"fm_amount_2\",\"fm_amount_3\",\"fm_amount_4\"],"
      "\"params\":[\"mix_1\",\"mix_2\",\"mix_3\",\"mix_4\",\"fm_amount\",\"fm_amount_1\",\"fm_amount_2\",\"fm_amount_3\",\"fm_amount_4\",\"fm_modulator\",\"fm_position\",\"vib_osc_enable\",\"sweep_osc_enable\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"scale\":{"
      "\"name\":\"Scale/Tune\","
      "\"children\":null,"
      "\"knobs\":[\"scale_index\",\"scale_root\",\"tuning_mode\",\"interval_1\",\"interval_2\",\"interval_3\"],"
      "\"params\":[\"scale_index\",\"scale_root\",\"tuning_mode\",\"interval_1\",\"interval_2\",\"interval_3\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"chordmulti\":{"
      "\"name\":\"Chord Multi\","
      "\"children\":null,"
      "\"knobs\":[\"chord_pc_0\",\"chord_pc_1\",\"chord_pc_2\",\"chord_pc_3\",\"chord_pc_4\",\"chord_pc_5\",\"chord_pc_6\",\"chord_pc_7\"],"
      "\"params\":[\"chord_pc_0\",\"chord_pc_1\",\"chord_pc_2\",\"chord_pc_3\",\"chord_pc_4\",\"chord_pc_5\",\"chord_pc_6\",\"chord_pc_7\",\"chord_pc_8\",\"chord_pc_9\",\"chord_pc_10\",\"chord_pc_11\"],"
      "\"navigate_to\":\"root\""
    "},"
    "\"ctrl\":{"
      "\"name\":\"Ctrl Src\","
      "\"children\":null,"
      "\"knobs\":[\"ctrl_source\",\"ctrl_cc\",\"ctrl_to_cutoff\",\"ctrl_to_morph\",\"ctrl_to_vib\",\"ctrl_to_shape\",\"ctrl_to_fm\"],"
      "\"params\":[\"ctrl_source\",\"ctrl_cc\",\"ctrl_to_cutoff\",\"ctrl_to_morph\",\"ctrl_to_vib\",\"ctrl_to_shape\",\"ctrl_to_fm\"],"
      "\"navigate_to\":\"root\""
    "}"
  "}"
"}";

/* Param metadata for chain_params query — flat array, one entry per param. */
static const char *chain_params_json =
"["
  "{\"key\":\"chord_type\",\"name\":\"Chord\",\"type\":\"enum\",\"options\":[\"Octaves\",\"Fifth\",\"Minor\",\"Min 7\",\"Min 9\",\"Min 11\",\"Major\",\"Maj 7\",\"Maj 9\",\"Sus 4\",\"6/9\",\"Min 6\",\"10th\",\"Dom 7\",\"Dom 7 b9\",\"Half Dim\"],\"default\":6},"
  "{\"key\":\"detune\",\"name\":\"Detune\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"width\",\"name\":\"Width\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
  "{\"key\":\"filter_cutoff\",\"name\":\"Cutoff\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
  "{\"key\":\"filter_resonance\",\"name\":\"Reso\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"filter_mode\",\"name\":\"Mode\",\"type\":\"enum\",\"options\":[\"LP\",\"HP\",\"BP\"],\"default\":0},"
  "{\"key\":\"filter_slope\",\"name\":\"Slope\",\"type\":\"enum\",\"options\":[\"12 dB\",\"24 dB\"],\"default\":0},"
  "{\"key\":\"filter_lfo_rate\",\"short_name\":\"Rate\",\"name\":\"Flt LFO Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"filter_lfo_depth\",\"short_name\":\"Depth\",\"name\":\"Flt LFO Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"filter_lfo_spread\",\"short_name\":\"Spread\",\"name\":\"Flt LFO Spread\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"filter_lfo_shape\",\"short_name\":\"Wave\",\"name\":\"Flt LFO Wave\",\"type\":\"enum\",\"options\":[\"Triangle\",\"Ramp Up\",\"Ramp Down\",\"Square\"],\"default\":0},"
  "{\"key\":\"filter_env_attack\",\"name\":\"Env A\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"filter_env_decay\",\"name\":\"Env D\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.3},"
  "{\"key\":\"filter_env_depth\",\"short_name\":\"Amt\",\"name\":\"Env Amt\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,\"default\":0},"
  "{\"key\":\"drive\",\"name\":\"Drive\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"volume\",\"name\":\"Volume\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.02,\"default\":0.8},"
  "{\"key\":\"wave_1\",\"name\":\"Wave 1\",\"type\":\"enum\",\"options\":[\"Off\",\"Sine\",\"Triangle\",\"Saw\",\"Square\",\"Pulse Tr\",\"Wavetable\"],\"default\":1},"
  "{\"key\":\"wave_2\",\"name\":\"Wave 2\",\"type\":\"enum\",\"options\":[\"Off\",\"Sine\",\"Triangle\",\"Saw\",\"Square\",\"Pulse Tr\",\"Wavetable\"],\"default\":1},"
  "{\"key\":\"wave_3\",\"name\":\"Wave 3\",\"type\":\"enum\",\"options\":[\"Off\",\"Sine\",\"Triangle\",\"Saw\",\"Square\",\"Pulse Tr\",\"Wavetable\"],\"default\":1},"
  "{\"key\":\"wave_4\",\"name\":\"Wave 4\",\"type\":\"enum\",\"options\":[\"Off\",\"Sine\",\"Triangle\",\"Saw\",\"Square\",\"Pulse Tr\",\"Wavetable\"],\"default\":1},"
  "{\"key\":\"shape\",\"name\":\"Shape\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"shape_1\",\"name\":\"Shape 1\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"shape_2\",\"name\":\"Shape 2\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"shape_3\",\"name\":\"Shape 3\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"shape_4\",\"name\":\"Shape 4\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"morph_index\",\"name\":\"Morph\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"morph_intensity\",\"short_name\":\"Int\",\"name\":\"Morph Int\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"pan_morph_index\",\"short_name\":\"Pan\",\"name\":\"Pan Morph\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"pan_morph_intensity\",\"name\":\"Pan Int\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"chord_spread\",\"name\":\"Spread\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"chord_rotation\",\"name\":\"Rotation\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"fm_modulator\",\"short_name\":\"Modu\",\"name\":\"FM Modulator\",\"type\":\"int\",\"min\":0,\"max\":3,\"step\":1,\"default\":0},"
  "{\"key\":\"fm_amount\",\"name\":\"FM Amt\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lfo_shape\",\"short_name\":\"Wave\",\"name\":\"LFO Wave\",\"type\":\"enum\",\"options\":[\"Triangle\",\"Ramp Up\",\"Ramp Down\",\"Square\"],\"default\":0},"
  "{\"key\":\"lfo_rate\",\"short_name\":\"Rate\",\"name\":\"LFO Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lfo_depth\",\"short_name\":\"Dpt\",\"name\":\"LFO Dpt\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"vib_depth\",\"name\":\"Vib Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"vib_speed\",\"short_name\":\"Speed\",\"name\":\"Vib Speed\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"vib_delay\",\"name\":\"Vib Delay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.2},"
  "{\"key\":\"sweep_amount\",\"name\":\"Sweep\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,\"default\":0},"
  "{\"key\":\"sweep_rate\",\"short_name\":\"Sweep\",\"name\":\"Sweep Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"attack\",\"name\":\"Attack\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.05},"
  "{\"key\":\"release\",\"name\":\"Release\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.3},"
  "{\"key\":\"reverb_mix\",\"short_name\":\"Mix\",\"name\":\"Reverb Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"reverb_decay\",\"short_name\":\"Decay\",\"name\":\"Reverb Decay\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"reverb_damp\",\"short_name\":\"Damp\",\"name\":\"Reverb Damp\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.3},"
  "{\"key\":\"grind\",\"name\":\"Grind\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"bit_shift\",\"name\":\"Shift\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"decimator\",\"name\":\"Decim\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"delay_mix\",\"short_name\":\"Mix\",\"name\":\"Delay Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"delay_time\",\"short_name\":\"Time\",\"name\":\"Delay Time\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.3},"
  "{\"key\":\"delay_feedback\",\"short_name\":\"Feedback\",\"name\":\"Delay Feedback\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.4},"
  "{\"key\":\"delay_tone\",\"short_name\":\"Tone\",\"name\":\"Delay Tone\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.7},"
  "{\"key\":\"glide_rate\",\"name\":\"Glide\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"glide_legato\",\"short_name\":\"Legato\",\"name\":\"Glide Legato\",\"type\":\"enum\",\"options\":[\"Always\",\"Legato\"],\"default\":0},"
  "{\"key\":\"vib_stray\",\"short_name\":\"Stray\",\"name\":\"Vibrato Stray\",\"type\":\"enum\",\"options\":[\"LFO\",\"Random\"],\"default\":0},"
  "{\"key\":\"delay_mode\",\"short_name\":\"Mode\",\"name\":\"Delay Mode\",\"type\":\"enum\",\"options\":[\"Stereo\",\"Ping-Pong\",\"Flip-Flop\",\"Long\",\"Zenith\",\"Interval\"],\"default\":0},"
  "{\"key\":\"delay_tone_hi\",\"short_name\":\"Tone Hi\",\"name\":\"Delay Tone Hi\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.7},"
  "{\"key\":\"delay_tone_lo\",\"short_name\":\"Tone Lo\",\"name\":\"Delay Tone Lo\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"delay_mod_rate\",\"short_name\":\"Mod Rate\",\"name\":\"Delay Mod Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"delay_mod_depth\",\"short_name\":\"Mod Depth\",\"name\":\"Delay Mod Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"reverb_shimmer\",\"short_name\":\"Shim\",\"name\":\"Reverb Shimmer\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"reverb_lowcut\",\"short_name\":\"Low Cut\",\"name\":\"Reverb Low Cut\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"reverb_size\",\"short_name\":\"Size\",\"name\":\"Reverb Size\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"reverb_mod_rate\",\"short_name\":\"Rate\",\"name\":\"Reverb Mod Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"reverb_mod_depth\",\"short_name\":\"Depth\",\"name\":\"Reverb Mod Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lm_lfo_rate\",\"short_name\":\"Lvl Rate\",\"name\":\"Lvl Morph LFO Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lm_lfo_depth\",\"short_name\":\"Lvl Depth\",\"name\":\"Lvl Morph LFO Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lm_lfo_shape\",\"short_name\":\"Lvl LFO\",\"name\":\"Lvl Morph LFO Wave\",\"type\":\"enum\",\"options\":[\"Triangle\",\"Ramp Up\",\"Ramp Down\",\"Square\"],\"default\":0},"
  "{\"key\":\"pm_lfo_rate\",\"short_name\":\"Pan LFO Rate\",\"name\":\"Pan Morph LFO Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"pm_lfo_depth\",\"short_name\":\"Pan LFO Depth\",\"name\":\"Pan Morph LFO Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"pm_lfo_shape\",\"short_name\":\"Pan LFO\",\"name\":\"Pan Morph LFO Wave\",\"type\":\"enum\",\"options\":[\"Triangle\",\"Ramp Up\",\"Ramp Down\",\"Square\"],\"default\":0},"
  "{\"key\":\"amp_lfo_rate\",\"short_name\":\"Rate\",\"name\":\"Tremolo Rate\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"amp_lfo_depth\",\"short_name\":\"Depth\",\"name\":\"Tremolo Depth\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"amp_lfo_shape\",\"short_name\":\"Wave\",\"name\":\"Tremolo Wave\",\"type\":\"enum\",\"options\":[\"Triangle\",\"Ramp Up\",\"Ramp Down\",\"Square\"],\"default\":0},"
  "{\"key\":\"vca_mode\",\"short_name\":\"Mode\",\"name\":\"VCA Mode\",\"type\":\"enum\",\"options\":[\"AD\",\"ASR\",\"Looping\"],\"default\":1},"
  "{\"key\":\"vca_hard_reset\",\"short_name\":\"Hard Reset\",\"name\":\"VCA Hard Reset\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":0},"
  "{\"key\":\"vca_drone\",\"name\":\"Drone\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":0},"
  "{\"key\":\"fenv_mode\",\"short_name\":\"Flt\",\"name\":\"Flt Env Mode\",\"type\":\"enum\",\"options\":[\"AD\",\"ASR\",\"Looping\"],\"default\":0},"
  "{\"key\":\"fenv_hard_reset\",\"short_name\":\"Env Hard Reset\",\"name\":\"Flt Env Hard Reset\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":0},"
  "{\"key\":\"quality_position\",\"short_name\":\"Position\",\"name\":\"LoFi Position\",\"type\":\"enum\",\"options\":[\"Post-Flt\",\"Pre-Flt\"],\"default\":0},"
  "{\"key\":\"fm_amount_1\",\"short_name\":\"FM1\",\"name\":\"FM Amount 1\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"fm_amount_2\",\"short_name\":\"FM2\",\"name\":\"FM Amount 2\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"fm_amount_3\",\"short_name\":\"FM3\",\"name\":\"FM Amount 3\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"fm_amount_4\",\"short_name\":\"FM4\",\"name\":\"FM Amount 4\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"mix_1\",\"short_name\":\"Mix 1\",\"name\":\"Mix 1\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
  "{\"key\":\"mix_2\",\"short_name\":\"Mix 2\",\"name\":\"Mix 2\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
  "{\"key\":\"mix_3\",\"short_name\":\"Mix 3\",\"name\":\"Mix 3\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
  "{\"key\":\"mix_4\",\"short_name\":\"Mix 4\",\"name\":\"Mix 4\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
  "{\"key\":\"vib_osc_enable\",\"short_name\":\"Vib\",\"name\":\"Vib Osc Enable\",\"type\":\"int\",\"min\":0,\"max\":15,\"step\":1,\"default\":15},"
  "{\"key\":\"sweep_osc_enable\",\"short_name\":\"Sweep\",\"name\":\"Sweep Osc Enable\",\"type\":\"int\",\"min\":0,\"max\":15,\"step\":1,\"default\":15},"
  "{\"key\":\"shape_lfo_mode\",\"name\":\"LFO Mode\",\"type\":\"enum\",\"options\":[\"Free\",\"Note Reset\",\"One Shot\"],\"default\":0},"
  "{\"key\":\"filter_lfo_mode\",\"name\":\"Flt LFO Mode\",\"type\":\"enum\",\"options\":[\"Free\",\"Note Reset\",\"One Shot\"],\"default\":0},"
  "{\"key\":\"lm_lfo_mode\",\"name\":\"Lvl Morph LFO Mode\",\"type\":\"enum\",\"options\":[\"Free\",\"Note Reset\",\"One Shot\"],\"default\":0},"
  "{\"key\":\"pm_lfo_mode\",\"name\":\"Pan Morph LFO Mode\",\"type\":\"enum\",\"options\":[\"Free\",\"Note Reset\",\"One Shot\"],\"default\":0},"
  "{\"key\":\"fm_position\",\"name\":\"FM Position\",\"type\":\"enum\",\"options\":[\"Pre-Morph\",\"Post-Morph\"],\"default\":0},"
  "{\"key\":\"arp_hold\",\"short_name\":\"Hold\",\"name\":\"Arp Hold\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":0},"
  "{\"key\":\"arp_euclid_steps\",\"short_name\":\"Steps\",\"name\":\"Euclid Steps\",\"type\":\"int\",\"min\":1,\"max\":16,\"step\":1,\"default\":16},"
  "{\"key\":\"arp_euclid_beats\",\"short_name\":\"Beats\",\"name\":\"Euclid Beats\",\"type\":\"int\",\"min\":0,\"max\":16,\"step\":1,\"default\":0},"
  "{\"key\":\"arp_variation_interval\",\"short_name\":\"Inte\",\"name\":\"Var Interval\",\"type\":\"int\",\"min\":-12,\"max\":12,\"step\":1,\"default\":0},"
  "{\"key\":\"arp_variations\",\"short_name\":\"Count\",\"name\":\"Var Count\",\"type\":\"int\",\"min\":1,\"max\":8,\"step\":1,\"default\":1},"
  "{\"key\":\"arp_clock_sync\",\"short_name\":\"Sync\",\"name\":\"Clock Sync\",\"type\":\"enum\",\"options\":[\"Internal\",\"MIDI Clk\"],\"default\":0},"
  "{\"key\":\"arp_clock_division\",\"short_name\":\"Division\",\"name\":\"Clock Division\",\"type\":\"enum\",\"options\":[\"1/4\",\"1/4T\",\"1/8\",\"1/8T\",\"1/16\",\"1/32\"],\"default\":2},"
  "{\"key\":\"scale_index\",\"name\":\"Scale\",\"type\":\"enum\",\"options\":[\"Chromatic\",\"Major\",\"Minor\",\"Harm Min\",\"Pent Maj\",\"Pent Min\",\"Diminished\",\"Dorian\",\"Phrygian\",\"Lydian\",\"Mixolyd\",\"Locrian\",\"Blues Maj\",\"Blues Min\",\"Arabic\",\"Arabic2\",\"Hijaz\",\"Iwato\",\"Pelog\",\"Slendro\",\"Folk\",\"Japanese\",\"Gypsy\",\"Flamenco\",\"Whole Tone\"],\"default\":0},"
  "{\"key\":\"scale_root\",\"short_name\":\"Root\",\"name\":\"Scale Root\",\"type\":\"enum\",\"options\":[\"C\",\"C#\",\"D\",\"D#\",\"E\",\"F\",\"F#\",\"G\",\"G#\",\"A\",\"A#\",\"B\"],\"default\":0},"
  "{\"key\":\"tuning_mode\",\"name\":\"Tuning\",\"type\":\"enum\",\"options\":[\"Chord\",\"Interval\",\"Chord Multi\"],\"default\":0},"
  "{\"key\":\"chord_pc_0\",\"name\":\"C\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_1\",\"name\":\"C#\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_2\",\"name\":\"D\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_3\",\"name\":\"D#\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_4\",\"name\":\"E\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_5\",\"name\":\"F\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_6\",\"name\":\"F#\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_7\",\"name\":\"G\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_8\",\"name\":\"G#\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_9\",\"name\":\"A\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_10\",\"name\":\"A#\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"chord_pc_11\",\"name\":\"B\",\"type\":\"enum\",\"options\":[\"Oct\",\"5th\",\"Min\",\"Min7\",\"Min9\",\"Min11\",\"Maj\",\"Maj7\",\"Maj9\",\"Sus4\",\"6/9\",\"Min6\",\"10\",\"Dom7\",\"Dom7b9\",\"HalfDim\"],\"default\":6},"
  "{\"key\":\"interval_1\",\"short_name\":\"Int 1\",\"name\":\"Interval 1\",\"type\":\"int\",\"min\":-24,\"max\":24,\"step\":1,\"default\":4},"
  "{\"key\":\"interval_2\",\"short_name\":\"Int 2\",\"name\":\"Interval 2\",\"type\":\"int\",\"min\":-24,\"max\":24,\"step\":1,\"default\":7},"
  "{\"key\":\"interval_3\",\"short_name\":\"Int 3\",\"name\":\"Interval 3\",\"type\":\"int\",\"min\":-24,\"max\":24,\"step\":1,\"default\":12},"
  "{\"key\":\"lfo_phase_1\",\"name\":\"Shape LFO Phase 1\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
  "{\"key\":\"lfo_phase_2\",\"name\":\"Shape LFO Phase 2\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.25},"
  "{\"key\":\"lfo_phase_3\",\"name\":\"Shape LFO Phase 3\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
  "{\"key\":\"lfo_phase_4\",\"name\":\"Shape LFO Phase 4\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.75},"
  "{\"key\":\"ctrl_source\",\"short_name\":\"Src\",\"name\":\"Ctrl Src\",\"type\":\"enum\",\"options\":[\"Aftertouch\",\"Random\",\"Coin Toss\",\"MIDI CC\",\"Velocity\"],\"default\":0},"
  "{\"key\":\"ctrl_cc\",\"short_name\":\"CC\",\"name\":\"Ctrl CC\",\"type\":\"int\",\"min\":0,\"max\":127,\"step\":1,\"default\":1},"
  "{\"key\":\"ctrl_to_cutoff\",\"short_name\":\"to Cutoff\",\"name\":\"Ctrl to Cutoff\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,\"default\":0},"
  "{\"key\":\"ctrl_to_morph\",\"short_name\":\"to Morph\",\"name\":\"Ctrl to Morph\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,\"default\":0},"
  "{\"key\":\"ctrl_to_vib\",\"short_name\":\"to Vibrato\",\"name\":\"Ctrl to Vibrato\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,\"default\":0},"
  "{\"key\":\"ctrl_to_shape\",\"short_name\":\"to Shape\",\"name\":\"Ctrl to Shape\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,\"default\":0},"
  "{\"key\":\"ctrl_to_fm\",\"short_name\":\"to FM\",\"name\":\"Ctrl to FM\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.02,\"default\":0},"
  "{\"key\":\"arp_enabled\",\"name\":\"Arp\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"],\"default\":0},"
  "{\"key\":\"arp_tempo\",\"short_name\":\"Tempo\",\"name\":\"Arp Tempo\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.4},"
  "{\"key\":\"arp_direction\",\"short_name\":\"Dire\",\"name\":\"Arp Direction\",\"type\":\"enum\",\"options\":[\"Up\",\"Down\",\"Up/Down\",\"Random\"],\"default\":0}"
"]";

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    if (!instance || !buf || buf_len <= 0) return 0;
    auto *inst = (chordism_instance_t*)instance;

    /* Preset browser queries. */
    if (key && strcmp(key, "preset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->preset_index);
    }
    if (key && strcmp(key, "preset_count") == 0) {
        return snprintf(buf, buf_len, "%d", NUM_PRESETS);
    }
    if (key && strcmp(key, "preset_name") == 0) {
        int idx = inst->preset_index;
        if (idx < 0) idx = 0;
        if (idx >= NUM_PRESETS) idx = NUM_PRESETS - 1;
        return snprintf(buf, buf_len, "%s", PRESETS[idx].name);
    }

    /* Full state for slot autosave / patch save. */
    if (key && strcmp(key, "state") == 0) {
        return state_serialize(instance, buf, buf_len);
    }

    /* Metadata queries from the Shadow UI. */
    if (key && strcmp(key, "ui_hierarchy") == 0) {
        int len = (int)strlen(ui_hierarchy_json);
        if (len >= buf_len) return -1;
        memcpy(buf, ui_hierarchy_json, len + 1);
        return len;
    }
    if (key && strcmp(key, "chain_params") == 0) {
        int len = (int)strlen(chain_params_json);
        if (len >= buf_len) return -1;
        memcpy(buf, chain_params_json, len + 1);
        return len;
    }

    if (key && strcmp(key, "attack") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->attack);
    } else if (key && strcmp(key, "release") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->release);
    } else if (key && strcmp(key, "volume") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->volume);
    } else if (key && strcmp(key, "waveform") == 0) {
        /* Report voice-0 waveform for backward compatibility. */
        return snprintf(buf, buf_len, "%d", inst->waveforms[0]);
    } else if (key && strncmp(key, "wave_", 5) == 0 && key[5] >= '1' && key[5] <= '0' + CHORD_SIZE && key[6] == '\0') {
        int idx = key[5] - '1';
        return snprintf(buf, buf_len, "%d", inst->waveforms[idx]);
    } else if (key && strcmp(key, "shape") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->shapes[0]);
    } else if (key && strncmp(key, "shape_", 6) == 0 && key[6] >= '1' && key[6] <= '0' + CHORD_SIZE && key[7] == '\0') {
        int idx = key[6] - '1';
        return snprintf(buf, buf_len, "%.4f", inst->shapes[idx]);
    } else if (key && strcmp(key, "pan_morph_index") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->pan_morph_index);
    } else if (key && strcmp(key, "pan_morph_intensity") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->pan_morph_intensity);
    } else if (key && strcmp(key, "chord_spread") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->chord_spread);
    } else if (key && strcmp(key, "chord_rotation") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->chord_rotation);
    } else if (key && strcmp(key, "fm_modulator") == 0) {
        return snprintf(buf, buf_len, "%d", inst->fm_modulator_idx);
    } else if (key && strcmp(key, "fm_amount") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->fm_amount);
    } else if (key && strncmp(key, "fm_amount_", 10) == 0 && key[10] >= '1' && key[10] <= '0' + CHORD_SIZE && key[11] == '\0') {
        return snprintf(buf, buf_len, "%.4f", inst->fm_amounts[key[10] - '1']);
    } else if (key && strncmp(key, "mix_", 4) == 0 && key[4] >= '1' && key[4] <= '0' + CHORD_SIZE && key[5] == '\0') {
        return snprintf(buf, buf_len, "%.4f", inst->mixer_trims[key[4] - '1']);
    } else if (key && strcmp(key, "vib_osc_enable") == 0) {
        return snprintf(buf, buf_len, "%d", inst->vib_osc_enable);
    } else if (key && strcmp(key, "sweep_osc_enable") == 0) {
        return snprintf(buf, buf_len, "%d", inst->sweep_osc_enable);
    } else if (key && strcmp(key, "lm_lfo_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->lm_lfo_rate);
    } else if (key && strcmp(key, "lm_lfo_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->lm_lfo_depth);
    } else if (key && strcmp(key, "lm_lfo_shape") == 0) {
        return snprintf(buf, buf_len, "%d", inst->lm_lfo_shape);
    } else if (key && strcmp(key, "pm_lfo_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->pm_lfo_rate);
    } else if (key && strcmp(key, "pm_lfo_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->pm_lfo_depth);
    } else if (key && strcmp(key, "pm_lfo_shape") == 0) {
        return snprintf(buf, buf_len, "%d", inst->pm_lfo_shape);
    } else if (key && strcmp(key, "amp_lfo_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->amp_lfo_rate);
    } else if (key && strcmp(key, "amp_lfo_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->amp_lfo_depth);
    } else if (key && strcmp(key, "amp_lfo_shape") == 0) {
        return snprintf(buf, buf_len, "%d", inst->amp_lfo_shape);
    } else if (key && strcmp(key, "vca_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->vca_mode);
    } else if (key && strcmp(key, "vca_hard_reset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->vca_hard_reset);
    } else if (key && strcmp(key, "vca_drone") == 0) {
        return snprintf(buf, buf_len, "%d", inst->vca_drone);
    } else if (key && strcmp(key, "fenv_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->fenv_mode);
    } else if (key && strcmp(key, "fenv_hard_reset") == 0) {
        return snprintf(buf, buf_len, "%d", inst->fenv_hard_reset);
    } else if (key && strcmp(key, "quality_position") == 0) {
        return snprintf(buf, buf_len, "%d", inst->quality_position);
    } else if (key && strcmp(key, "reverb_shimmer") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_shimmer);
    } else if (key && strcmp(key, "reverb_lowcut") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_lowcut);
    } else if (key && strcmp(key, "reverb_size") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_size);
    } else if (key && strcmp(key, "reverb_mod_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_mod_rate);
    } else if (key && strcmp(key, "reverb_mod_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_mod_depth);
    } else if (key && strcmp(key, "delay_tone_hi") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_tone_hi);
    } else if (key && strcmp(key, "delay_tone_lo") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_tone_lo);
    } else if (key && strcmp(key, "delay_mod_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_mod_rate);
    } else if (key && strcmp(key, "delay_mod_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_mod_depth);
    } else if (key && strcmp(key, "lfo_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->lfo_rate);
    } else if (key && strcmp(key, "lfo_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->lfo_depth);
    } else if (key && strcmp(key, "lfo_shape") == 0) {
        return snprintf(buf, buf_len, "%d", inst->lfo_shape);
    } else if (key && strcmp(key, "chord_type") == 0) {
        return snprintf(buf, buf_len, "%d", inst->chord_type);
    } else if (key && strcmp(key, "detune") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->detune);
    } else if (key && strcmp(key, "width") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->width);
    } else if (key && strcmp(key, "vib_speed") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->vib_speed);
    } else if (key && strcmp(key, "vib_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->vib_depth);
    } else if (key && strcmp(key, "vib_delay") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->vib_delay);
    } else if (key && strcmp(key, "sweep_amount") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->sweep_amount);
    } else if (key && strcmp(key, "sweep_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->sweep_rate);
    } else if (key && strcmp(key, "filter_cutoff") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_cutoff);
    } else if (key && strcmp(key, "filter_resonance") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_resonance);
    } else if (key && strcmp(key, "drive") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->drive);
    } else if (key && strcmp(key, "filter_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->filter_mode);
    } else if (key && strcmp(key, "filter_slope") == 0) {
        return snprintf(buf, buf_len, "%d", inst->filter_slope);
    } else if (key && strcmp(key, "filter_lfo_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_lfo_rate);
    } else if (key && strcmp(key, "filter_lfo_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_lfo_depth);
    } else if (key && strcmp(key, "filter_lfo_spread") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_lfo_spread);
    } else if (key && strcmp(key, "filter_lfo_shape") == 0) {
        return snprintf(buf, buf_len, "%d", inst->filter_lfo_shape);
    } else if (key && strcmp(key, "filter_env_attack") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_env_attack);
    } else if (key && strcmp(key, "filter_env_decay") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_env_decay);
    } else if (key && strcmp(key, "filter_env_depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->filter_env_depth);
    } else if (key && strcmp(key, "reverb_mix") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_mix);
    } else if (key && strcmp(key, "reverb_decay") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_decay);
    } else if (key && strcmp(key, "reverb_damp") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb_damp);
    } else if (key && strcmp(key, "grind") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->grind);
    } else if (key && strcmp(key, "bit_shift") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->bit_shift);
    } else if (key && strcmp(key, "decimator") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->decimator);
    } else if (key && strcmp(key, "morph_index") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->morph_index);
    } else if (key && strcmp(key, "morph_intensity") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->morph_intensity);
    } else if (key && strcmp(key, "delay_mix") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_mix);
    } else if (key && strcmp(key, "delay_time") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_time);
    } else if (key && strcmp(key, "delay_feedback") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_feedback);
    } else if (key && strcmp(key, "delay_tone") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->delay_tone);
    } else if (key && strcmp(key, "delay_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->delay_mode);
    } else if (key && strcmp(key, "glide_legato") == 0) {
        return snprintf(buf, buf_len, "%d", inst->glide_legato);
    } else if (key && strcmp(key, "vib_stray") == 0) {
        return snprintf(buf, buf_len, "%d", inst->vib_stray);
    } else if (key && strcmp(key, "glide_rate") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->glide_rate);
    } else if (key && strcmp(key, "arp_enabled") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_enabled);
    } else if (key && strcmp(key, "arp_tempo") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->arp_tempo);
    } else if (key && strcmp(key, "arp_direction") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_direction);
    } else if (key && strcmp(key, "arp_hold") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_hold);
    } else if (key && strcmp(key, "arp_euclid_steps") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_euclid_steps);
    } else if (key && strcmp(key, "arp_euclid_beats") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_euclid_beats);
    } else if (key && strcmp(key, "arp_variation_interval") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_variation_interval);
    } else if (key && strcmp(key, "arp_variations") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_variations);
    } else if (key && strcmp(key, "arp_clock_sync") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_clock_sync);
    } else if (key && strcmp(key, "arp_clock_division") == 0) {
        return snprintf(buf, buf_len, "%d", inst->arp_clock_division);
    } else if (key && strcmp(key, "shape_lfo_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->shape_lfo_mode);
    } else if (key && strcmp(key, "filter_lfo_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->filter_lfo_mode);
    } else if (key && strcmp(key, "lm_lfo_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->lm_lfo_mode);
    } else if (key && strcmp(key, "pm_lfo_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->pm_lfo_mode);
    } else if (key && strcmp(key, "fm_position") == 0) {
        return snprintf(buf, buf_len, "%d", inst->fm_position);
    } else if (key && strcmp(key, "scale_index") == 0) {
        return snprintf(buf, buf_len, "%d", inst->scale_index);
    } else if (key && strcmp(key, "scale_root") == 0) {
        return snprintf(buf, buf_len, "%d", inst->scale_root);
    } else if (key && strcmp(key, "tuning_mode") == 0) {
        return snprintf(buf, buf_len, "%d", inst->tuning_mode);
    } else if (key && strncmp(key, "chord_pc_", 9) == 0) {
        int pc = atoi(key + 9);
        if (pc >= 0 && pc < 12) {
            return snprintf(buf, buf_len, "%d", inst->chord_pc[pc]);
        }
        return 0;
    } else if (key && strcmp(key, "interval_1") == 0) {
        return snprintf(buf, buf_len, "%d", inst->interval_1);
    } else if (key && strcmp(key, "interval_2") == 0) {
        return snprintf(buf, buf_len, "%d", inst->interval_2);
    } else if (key && strcmp(key, "interval_3") == 0) {
        return snprintf(buf, buf_len, "%d", inst->interval_3);
    } else if (key && strncmp(key, "lfo_phase_", 10) == 0 && key[10] >= '1' && key[10] <= '0' + CHORD_SIZE && key[11] == '\0') {
        return snprintf(buf, buf_len, "%.4f", inst->shape_lfo_phase_offsets[key[10] - '1']);
    } else if (key && strcmp(key, "ctrl_source") == 0) {
        return snprintf(buf, buf_len, "%d", inst->ctrl_source);
    } else if (key && strcmp(key, "ctrl_cc") == 0) {
        return snprintf(buf, buf_len, "%d", inst->ctrl_cc);
    } else if (key && strcmp(key, "ctrl_to_cutoff") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->ctrl_to_cutoff);
    } else if (key && strcmp(key, "ctrl_to_morph") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->ctrl_to_morph);
    } else if (key && strcmp(key, "ctrl_to_vib") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->ctrl_to_vib);
    } else if (key && strcmp(key, "ctrl_to_shape") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->ctrl_to_shape);
    } else if (key && strcmp(key, "ctrl_to_fm") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->ctrl_to_fm);
    } else if (key && strcmp(key, "version") == 0) {
        return snprintf(buf, buf_len, "0.3.12");
    }
    buf[0] = '\0';
    return 0;
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    if (buf && buf_len > 0) buf[0] = '\0';
    return 0;
}

static void v2_render_block(void *instance, int16_t *out_interleaved_lr, int frames) {
    if (!instance || !out_interleaved_lr || frames <= 0) {
        if (out_interleaved_lr && frames > 0) {
            memset(out_interleaved_lr, 0, sizeof(int16_t) * 2 * frames);
        }
        return;
    }
    auto *inst = (chordism_instance_t*)instance;

    /* Sum across voices. A chord is CHORD_SIZE voices at unit amplitude → peak
     * ±CHORD_SIZE. With voice-steal banks, an overlapping release tail can add
     * up to CHORD_SIZE more — so worst-case peak is NUM_VOICES units. Divide
     * by CHORD_SIZE so the steady-state chord hits unit amplitude, then keep
     * 6 dB headroom by scaling to 16000 instead of 32767 (clipping path
     * catches the rare overlap-peak spikes). */
    const float voice_gain = 1.0f / (float)CHORD_SIZE;
    const float master_gain = inst->volume * 16000.0f;

    LFO *lfo = &inst->shape_lfo;

    /* Animated morphs: per-block, advance morph LFOs and recompute morph LUT
     * gains using effective index = base + LFO * depth. Block-rate update is
     * inaudible for slow LFOs and dramatically cheaper than per-sample. */
    /* Effective morph index = base + LFO + control source routing. */
    float morph_idx_base = inst->morph_index + inst->ctrl_value * inst->ctrl_to_morph * 0.5f;

    if (inst->lm_lfo_depth > 0.0f) {
        if (!inst->lm_lfo_done) {
            inst->lm_lfo_phase += inst->lm_lfo_phase_inc * (float)frames;
            if (inst->lm_lfo_phase >= 1.0f) {
                if (inst->lm_lfo_mode == LFOMODE_ONE_SHOT) {
                    inst->lm_lfo_phase = 1.0f;
                    inst->lm_lfo_done = true;
                } else {
                    while (inst->lm_lfo_phase >= 1.0f) inst->lm_lfo_phase -= 1.0f;
                }
            }
        }
        float lm_val = lfo_sample(inst->lm_lfo_shape, inst->lm_lfo_phase) * inst->lm_lfo_depth;
        morph_recompute_at(inst, morph_idx_base + lm_val * 0.5f);
    } else if (inst->ctrl_to_morph != 0.0f) {
        morph_recompute_at(inst, morph_idx_base);
    }
    if (inst->pm_lfo_depth > 0.0f) {
        if (!inst->pm_lfo_done) {
            inst->pm_lfo_phase += inst->pm_lfo_phase_inc * (float)frames;
            if (inst->pm_lfo_phase >= 1.0f) {
                if (inst->pm_lfo_mode == LFOMODE_ONE_SHOT) {
                    inst->pm_lfo_phase = 1.0f;
                    inst->pm_lfo_done = true;
                } else {
                    while (inst->pm_lfo_phase >= 1.0f) inst->pm_lfo_phase -= 1.0f;
                }
            }
        }
        float pm_val = lfo_sample(inst->pm_lfo_shape, inst->pm_lfo_phase) * inst->pm_lfo_depth;
        pan_morph_recompute_at(inst, inst->pan_morph_index + pm_val * 0.5f);
    }

    for (int i = 0; i < frames; ++i) {
        /* Slew the control-source value toward its target so discrete MIDI
         * aftertouch/CC steps don't zipper the cutoff (or any ctrl destination).
         * ~5 ms one-pole: immediate to play, smooth enough to remove the steps. */
        inst->ctrl_value += (inst->ctrl_value_target - inst->ctrl_value) * 0.005f;
        /* Smoothed raw pressure for the default press-brighter path — eases from
         * zero so brightness ramps in gradually instead of jumping on first touch. */
        inst->at_smooth += (inst->aftertouch - inst->at_smooth) * 0.005f;

        /* Arp tick (internal clock). MIDI-clock-synced ticks are fired from
         * on_midi when 0xF8 ticks arrive. */
        if (inst->arp_enabled && !inst->arp_clock_sync && inst->held_count > 0) {
            inst->arp_sample_counter--;
            if (inst->arp_sample_counter <= 0) {
                inst->arp_sample_counter = inst->arp_step_period;
                arp_step_and_fire(inst);
            }
        }

        /* Advance shape LFO, compute effective shape offset for this sample. */
        if (!inst->shape_lfo_done) {
            lfo->phase += lfo->phase_inc;
            if (lfo->phase >= 1.0f) {
                if (inst->shape_lfo_mode == LFOMODE_ONE_SHOT) {
                    lfo->phase = 1.0f;
                    inst->shape_lfo_done = true;
                } else {
                    lfo->phase -= 1.0f;
                }
            }
        }
        /* Per-voice shape LFO offsets are computed inside the voice loop below
         * (each voice uses lfo->phase + its own shape_lfo_phase_offsets[step]). */

        /* Advance vibrato LFO + delay ramp, compute pitch-shift ratio. */
        inst->vibrato_phase += inst->vibrato_phase_inc;
        if (inst->vibrato_phase >= 1.0f) inst->vibrato_phase -= 1.0f;
        if (inst->vib_ramp_value < 1.0f) {
            inst->vib_ramp_value += inst->vib_ramp_inc;
            if (inst->vib_ramp_value > 1.0f) inst->vib_ramp_value = 1.0f;
        }
        /* Vibrato depth: base + control-source matrix. (Aftertouch no longer
         * goes here; it's hardcoded to filter cutoff instead — see below.) */
        float vib_depth_eff = inst->vib_depth
                            + inst->ctrl_value * inst->ctrl_to_vib * 0.5f;
        if (vib_depth_eff > 1.0f) vib_depth_eff = 1.0f;
        if (vib_depth_eff < 0.0f) vib_depth_eff = 0.0f;

        float vib_mod_source;
        if (inst->vib_stray) {
            /* Smoothed random: pick a new random target every period_samples
             * and slew toward it. Period derived from vib_speed so it tracks
             * the user knob. */
            if (inst->vib_random_counter <= 0) {
                /* New target in [-1, +1]. */
                float r = ((float)(rand() & 0xFFFF) / 32768.0f) - 1.0f;
                inst->vib_random_target = r;
                int period = (int)(SAMPLE_RATE / (vib_speed_to_hz(inst->vib_speed) * 2.0f));
                if (period < 16) period = 16;
                inst->vib_random_counter = period;
            }
            inst->vib_random_counter--;
            /* Slew current toward target. */
            float slew = 0.002f;
            inst->vib_random_value += (inst->vib_random_target - inst->vib_random_value) * slew;
            vib_mod_source = inst->vib_random_value;
        } else {
            vib_mod_source = sinf(TWO_PI * inst->vibrato_phase);
        }

        float vib_cents = vib_mod_source
                          * inst->vib_ramp_value
                          * vib_depth_eff * VIB_DEPTH_MAX_CENTS;

        /* Pitch sweep: exp decay toward 0. */
        if (inst->sweep_value != 0.0f) {
            inst->sweep_value *= inst->sweep_coef;
            if (inst->sweep_value < 1e-4f && inst->sweep_value > -1e-4f) {
                inst->sweep_value = 0.0f;
            }
        }

        float pitch_semis = inst->sweep_value;
        /* per-voice vib_ratio is computed below in the voice loop (osc-enable bitmasks). */

        float l_mix = 0.0f;
        float r_mix = 0.0f;

        for (int vi = 0; vi < NUM_VOICES; ++vi) {
            Voice *v = &inst->voices[vi];
            if (!v->active) continue;

            switch (v->env.stage) {
                case ENV_ATTACK:
                    v->env.value += v->env.attack_inc;
                    if (v->env.value >= 1.0f) {
                        v->env.value = 1.0f;
                        /* AD and Looping auto-decay from peak; ASR holds. */
                        if (inst->vca_mode == ENVMODE_ASR) {
                            v->env.stage = ENV_HOLD;
                        } else {
                            v->env.stage = ENV_RELEASE;
                        }
                    }
                    break;
                case ENV_HOLD:
                    /* If gate dropped between samples and we're still in HOLD,
                     * transition out. (Normally chord_off handles this.) */
                    if (!v->gate) v->env.stage = ENV_RELEASE;
                    break;
                case ENV_RELEASE:
                    v->env.value *= v->env.release_coef;
                    if (v->env.value < ENV_SILENCE) {
                        /* Looping: while gate still held, restart attack. */
                        if (v->gate && inst->vca_mode == ENVMODE_LOOPING) {
                            v->env.value = 0.0f;
                            v->env.stage = ENV_ATTACK;
                        } else {
                            v->env.value = 0.0f;
                            v->env.stage = ENV_IDLE;
                            v->active = false;
                        }
                    }
                    break;
                case ENV_IDLE:
                default:
                    v->active = false;
                    break;
            }

            if (!v->active) continue;

            /* Glide: ramp phase_inc toward target. */
            if (v->glide_step != 0.0f) {
                v->phase_inc += v->glide_step;
                if ((v->glide_step > 0.0f && v->phase_inc >= v->target_phase_inc) ||
                    (v->glide_step < 0.0f && v->phase_inc <= v->target_phase_inc)) {
                    v->phase_inc = v->target_phase_inc;
                    v->glide_step = 0.0f;
                }
            }

            /* Per-osc vibrato + pitch-sweep enable bitmasks. If this voice's
             * bit is 0, that modulation source doesn't apply to it. */
            float voice_vib_cents = ((inst->vib_osc_enable >> v->chord_step) & 1)
                ? vib_cents : 0.0f;
            float voice_sweep_semis = ((inst->sweep_osc_enable >> v->chord_step) & 1)
                ? pitch_semis : 0.0f;
            float voice_vib_ratio = (voice_vib_cents == 0.0f && voice_sweep_semis == 0.0f)
                ? 1.0f
                : exp2f(voice_vib_cents * ONE_OVER_1200 + voice_sweep_semis * ONE_OVER_12);

            float inc = v->phase_inc * voice_vib_ratio;

            /* Per-osc shape: each voice's shape LFO uses a phase OFFSET from
             * the shared LFO phase (independent per-osc phases). */
            float voice_lfo_phase = lfo->phase + inst->shape_lfo_phase_offsets[v->chord_step];
            voice_lfo_phase -= floorf(voice_lfo_phase);
            float voice_shape_lfo = lfo_sample(lfo->shape, voice_lfo_phase) * inst->lfo_depth;

            float eff_shape = inst->shapes[v->chord_step]
                            + voice_shape_lfo
                            + inst->ctrl_value * inst->ctrl_to_shape * 0.5f;
            if (eff_shape < 0.0f) eff_shape = 0.0f;
            if (eff_shape > 1.0f) eff_shape = 1.0f;

            /* FM: carriers' phase lookup offset by last modulator sample,
             * per-carrier amount × global fm_amount × (ctrl modulation). */
            float fm_amt_eff = inst->fm_amount + inst->ctrl_value * inst->ctrl_to_fm * 0.5f;
            if (fm_amt_eff < 0.0f) fm_amt_eff = 0.0f;
            if (fm_amt_eff > 1.0f) fm_amt_eff = 1.0f;
            float phase_used = v->phase;
            if (v->chord_step != inst->fm_modulator_idx && fm_amt_eff > 0.0f) {
                float per_carrier = inst->fm_amounts[v->chord_step];
                phase_used += inst->last_modulator_sample * fm_amt_eff * per_carrier;
                phase_used -= floorf(phase_used);
            }

            float s = osc_sample(v->waveform, phase_used, inc, eff_shape);
            v->phase += inc;
            if (v->phase >= 1.0f) v->phase -= 1.0f;
            else if (v->phase < 0.0f) v->phase += 1.0f;

            if (v->chord_step == inst->fm_modulator_idx) {
                /* FM position 0 (pre-morph): full modulator output is captured
                 * regardless of how the morph scales this voice's level.
                 * FM position 1 (post-morph): captured AFTER morph gain so
                 * the morph's voicing affects modulation depth — when this
                 * voice is morphed quiet, FM falls off too. */
                if (inst->fm_position == 1) {
                    inst->last_modulator_sample = s * inst->morph_gains[v->chord_step]
                                                    * inst->mixer_trims[v->chord_step];
                } else {
                    inst->last_modulator_sample = s;
                }
            }

            /* VCA drone: bypass envelope, hold open at 1.0. */
            float env_val = inst->vca_drone ? 1.0f : v->env.value;

            float amp = s * env_val * v->velocity * voice_gain
                          * inst->morph_gains[v->chord_step]
                          * inst->mixer_trims[v->chord_step];

            /* Pan = width-based static pan + pan-morph offset, summed and
             * clamped. Equal-power L/R via sqrt. */
            float pan = 0.5f
                      + v->pan_offset * inst->width
                      + inst->pan_morph_pans[v->chord_step] * 0.5f;
            if (pan < 0.0f) pan = 0.0f;
            if (pan > 1.0f) pan = 1.0f;
            float pan_l = sqrtf(1.0f - pan);
            float pan_r = sqrtf(pan);

            l_mix += amp * pan_l;
            r_mix += amp * pan_r;
        }

        /* Lo-Fi pre-filter (if user toggled position=1). */
        if (inst->quality_position == 1) {
            apply_lofi(inst, &l_mix, &r_mix);
        }

        /* Effective (modulated) cutoff per channel: base + filter env + filter
         * LFO (with L/R spread) + control source + aftertouch. */
        float eff_l = inst->filter_cutoff, eff_r = inst->filter_cutoff;
        bool env_on = inst->filter_env_depth != 0.0f;
        bool lfo_on = inst->filter_lfo_depth != 0.0f;
        bool ctrl_on = inst->ctrl_to_cutoff != 0.0f;
        /* Keep recomputing while pressure is held/decaying OR the smoothed control
         * value is still settling toward its target (so the slew completes cleanly). */
        bool at_on = inst->aftertouch > 0.0f || inst->at_smooth > 1e-4f ||
                     fabsf(inst->ctrl_value - inst->ctrl_value_target) > 1e-4f;

        if (env_on || lfo_on || ctrl_on || at_on) {
            float env_val = env_on ? aenv_tick(inst, &inst->filter_env) : 0.0f;
            float env_mod = env_val * inst->filter_env_depth;

            float lfo_l_mod = 0.0f, lfo_r_mod = 0.0f;
            if (lfo_on) {
                if (!inst->filter_lfo_done) {
                    inst->filter_lfo_phase += inst->filter_lfo_phase_inc;
                    if (inst->filter_lfo_phase >= 1.0f) {
                        if (inst->filter_lfo_mode == LFOMODE_ONE_SHOT) {
                            inst->filter_lfo_phase = 1.0f;
                            inst->filter_lfo_done = true;
                        } else {
                            inst->filter_lfo_phase -= 1.0f;
                        }
                    }
                }
                float l_phase = inst->filter_lfo_phase;
                float r_phase = inst->filter_lfo_phase + inst->filter_lfo_spread * 0.5f;
                if (r_phase >= 1.0f) r_phase -= 1.0f;
                lfo_l_mod = lfo_sample(inst->filter_lfo_shape, l_phase) * inst->filter_lfo_depth;
                lfo_r_mod = lfo_sample(inst->filter_lfo_shape, r_phase) * inst->filter_lfo_depth;
            }

            float ctrl_mod = inst->ctrl_value * inst->ctrl_to_cutoff * 0.5f;
            /* Default "press harder = brighter": only when the source is
             * aftertouch and the preset doesn't already route it to cutoff
             * (avoids double-opening). Uses the smoothed RAW pressure (at_smooth,
             * no velocity baseline) so it eases in from zero rather than jumping
             * to the velocity-seeded level on first touch. */
            float at_mod = (inst->ctrl_source == CTRL_AFTERTOUCH &&
                            inst->ctrl_to_cutoff == 0.0f)
                         ? inst->at_smooth * 0.4f : 0.0f;
            eff_l = inst->filter_cutoff + env_mod + lfo_l_mod + ctrl_mod + at_mod;
            eff_r = inst->filter_cutoff + env_mod + lfo_r_mod + ctrl_mod + at_mod;
            if (eff_l < 0.0f) eff_l = 0.0f; if (eff_l > 1.0f) eff_l = 1.0f;
            if (eff_r < 0.0f) eff_r = 0.0f; if (eff_r > 1.0f) eff_r = 1.0f;
        }

        float max_hz = SAMPLE_RATE * 0.49f;
        float hz_l = filter_cutoff_to_hz(eff_l);
        if (hz_l > max_hz) hz_l = max_hz; if (hz_l < 1.0f) hz_l = 1.0f;
        float hz_r = filter_cutoff_to_hz(eff_r);
        if (hz_r > max_hz) hz_r = max_hz; if (hz_r < 1.0f) hz_r = 1.0f;

        float fl, fr;
        bool lp_mode = (inst->filter_mode == FILT_LP);
        if (lp_mode) {
            /* Warm ZDF ladder for lowpass — drive is folded into the ladder. */
            fl = ladder_lp_process(&inst->ladder_l, l_mix, hz_l,
                                   inst->filter_resonance, inst->drive);
            fr = ladder_lp_process(&inst->ladder_r, r_mix, hz_r,
                                   inst->filter_resonance, inst->drive);
        } else {
            /* Clean SVF for band-pass / high-pass. */
            float g_l = tanf(3.14159265358979f * hz_l / SAMPLE_RATE);
            float al1 = 1.0f / (1.0f + g_l * (g_l + inst->filter_k));
            float al2 = g_l * al1, al3 = g_l * al2;
            float g_r = tanf(3.14159265358979f * hz_r / SAMPLE_RATE);
            float ar1 = 1.0f / (1.0f + g_r * (g_r + inst->filter_k));
            float ar2 = g_r * ar1, ar3 = g_r * ar2;
            fl = svf_process(&inst->filter_l, l_mix, inst->filter_mode, al1, al2, al3, inst->filter_k);
            fr = svf_process(&inst->filter_r, r_mix, inst->filter_mode, ar1, ar2, ar3, inst->filter_k);
            if (inst->filter_slope == 1) {
                fl = svf_process(&inst->filter_l_b, fl, inst->filter_mode, al1, al2, al3, inst->filter_k);
                fr = svf_process(&inst->filter_r_b, fr, inst->filter_mode, ar1, ar2, ar3, inst->filter_k);
            }
            float reso_factor = (inst->filter_slope == 1) ? 7.0f : 4.0f;
            float reso_comp = 1.0f / (1.0f + inst->filter_resonance * reso_factor);
            fl *= reso_comp;
            fr *= reso_comp;
        }

        /* Drive: the ladder (LP) already saturates internally; apply the post
         * soft-clip drive only for the SVF (BP/HP) path. */
        float dl, dr;
        if (!lp_mode && inst->drive > 0.0f) {
            float drive_gain = 1.0f + inst->drive * 9.0f;
            dl = tanhf(drive_gain * fl);
            dr = tanhf(drive_gain * fr);
        } else {
            dl = fl;
            dr = fr;
        }

        /* Lo-Fi post-filter (current position). For pre-filter, see the
         * apply_lofi() call inserted before the SVF block. */
        if (inst->quality_position == 0) {
            apply_lofi(inst, &dl, &dr);
        }

        /* Delay: runs after lo-fi, before reverb. Three modes:
         *  0 (stereo)    – independent L/R lines, feedback per channel
         *  1 (ping-pong) – cross-feedback (L←R, R←L), single tap input mono'd
         *  2 (flip-flop) – ping-pong + swap which channel is read on each
         *                  delay period for ear-pinging stereo motion. */
        if (inst->delay_mix > 0.0f || inst->delay_feedback > 0.0f) {
            float delay_s = DELAY_TIME_MIN_S +
                            inst->delay_time * (DELAY_TIME_MAX_S - DELAY_TIME_MIN_S);
            int delay_samples = (int)(delay_s * SAMPLE_RATE);
            if (delay_samples < 1) delay_samples = 1;
            if (delay_samples >= DELAY_BUFFER_SIZE) delay_samples = DELAY_BUFFER_SIZE - 1;

            int read_idx = (inst->delay_write_idx - delay_samples) & DELAY_BUFFER_MASK;
            float wet_l = inst->delay_buf_l[read_idx];
            float wet_r = inst->delay_buf_r[read_idx];

            float lp = 0.05f + inst->delay_tone * 0.95f;
            inst->delay_lp_l = lp * wet_l + (1.0f - lp) * inst->delay_lp_l;
            inst->delay_lp_r = lp * wet_r + (1.0f - lp) * inst->delay_lp_r;

            float fb = inst->delay_feedback * DELAY_FEEDBACK_MAX;
            float in_l = dl, in_r = dr;
            float out_l = wet_l, out_r = wet_r;

            switch (inst->delay_mode) {
                case DELAY_STEREO:
                default:
                    inst->delay_buf_l[inst->delay_write_idx] = in_l + inst->delay_lp_l * fb;
                    inst->delay_buf_r[inst->delay_write_idx] = in_r + inst->delay_lp_r * fb;
                    break;

                case DELAY_PINGPONG:
                case DELAY_FLIPFLOP: {
                    float in_mono = (in_l + in_r) * 0.5f;
                    inst->delay_buf_l[inst->delay_write_idx] = in_mono + inst->delay_lp_r * fb;
                    inst->delay_buf_r[inst->delay_write_idx] = inst->delay_lp_l * fb;
                    if (inst->delay_mode == DELAY_FLIPFLOP) {
                        if ((inst->delay_write_idx % delay_samples) == 0) {
                            inst->delay_flip_counter++;
                        }
                        if (inst->delay_flip_counter & 1) {
                            float tmp = out_l; out_l = out_r; out_r = tmp;
                        }
                    }
                    break;
                }

                case DELAY_LONG: {
                    /* Single mono line, both channels read the same tap. */
                    float in_mono = (in_l + in_r) * 0.5f;
                    float wet_mono = (inst->delay_lp_l + inst->delay_lp_r) * 0.5f;
                    inst->delay_buf_l[inst->delay_write_idx] = in_mono + wet_mono * fb;
                    inst->delay_buf_r[inst->delay_write_idx] = inst->delay_buf_l[inst->delay_write_idx];
                    out_l = (wet_l + wet_r) * 0.5f;
                    out_r = out_l;
                    break;
                }

                case DELAY_ZENITH:
                case DELAY_INTERVAL: {
                    /* Pitch-shifted repeats — simplified granular: write the
                     * dry input plus pitch-shifted feedback from a roving
                     * read head. Zenith fixes +12 semitones; Interval is
                     * controlled by delay_mod_depth (-12..+12). */
                    float semis = (inst->delay_mode == DELAY_ZENITH)
                        ? 12.0f
                        : (inst->delay_mod_depth * 24.0f - 12.0f);
                    float read_speed = exp2f(semis / 12.0f);
                    inst->delay_grain_phase += read_speed - 1.0f;
                    /* Use shifted feedback as input + dry */
                    float shifted_l = inst->delay_buf_l[
                        (inst->delay_write_idx - delay_samples
                          + (int)(inst->delay_grain_phase * (float)delay_samples * 0.5f))
                          & DELAY_BUFFER_MASK];
                    float shifted_r = inst->delay_buf_r[
                        (inst->delay_write_idx - delay_samples
                          + (int)(inst->delay_grain_phase * (float)delay_samples * 0.5f))
                          & DELAY_BUFFER_MASK];
                    inst->delay_buf_l[inst->delay_write_idx] = in_l + shifted_l * fb;
                    inst->delay_buf_r[inst->delay_write_idx] = in_r + shifted_r * fb;
                    /* Reset grain phase periodically. */
                    if (inst->delay_grain_phase > 4.0f || inst->delay_grain_phase < -4.0f) {
                        inst->delay_grain_phase = 0.0f;
                    }
                    break;
                }
            }
            inst->delay_write_idx = (inst->delay_write_idx + 1) & DELAY_BUFFER_MASK;

            float mix = inst->delay_mix;
            dl = dl * (1.0f - mix) + out_l * mix;
            dr = dr * (1.0f - mix) + out_r * mix;
        }

        /* Reverb: lush Dattorro plate.
         *   decay   -> base RT60
         *   size    -> adds tail length (larger plate)
         *   damp    -> HF darkening
         *   shimmer -> chorus excursion (primary)
         *   mod_depth -> extra chorus excursion
         *   mod_rate  -> chorus LFO speed
         *   low cut -> HP on the wet output. */
        float wl = dl, wr = dr;
        if (inst->reverb_mix > 0.0f) {
            float decay = 0.45f + inst->reverb_decay * 0.34f
                                + inst->reverb_size * 0.11f;
            if (decay > 0.92f) decay = 0.92f;
            float damp    = inst->reverb_damp;
            float modd    = 3.0f + inst->reverb_shimmer * 9.0f
                                 + inst->reverb_mod_depth * 6.0f;
            float modrate = 0.4f + inst->reverb_mod_rate * 2.5f;
            float rl, rr;
            dattorro_process(&inst->dattorro, (dl + dr) * 0.5f,
                             decay, damp, modd, modrate, &rl, &rr);

            /* Low cut: one-pole HP on reverb output. coef 1 = no cut,
             * coef → 0 = heavier cut. */
            if (inst->reverb_lowcut > 0.0f) {
                float hp_coef = 1.0f - inst->reverb_lowcut * 0.5f;
                float pl = inst->reverb_hp_l;
                float pr = inst->reverb_hp_r;
                inst->reverb_hp_l = hp_coef * (pl + rl - inst->reverb_hp_l);
                inst->reverb_hp_r = hp_coef * (pr + rr - inst->reverb_hp_r);
                rl = inst->reverb_hp_l;
                rr = inst->reverb_hp_r;
            }

            wl = dl * (1.0f - inst->reverb_mix) + rl * inst->reverb_mix;
            wr = dr * (1.0f - inst->reverb_mix) + rr * inst->reverb_mix;
        }

        /* Amplitude LFO (tremolo): downward gain wobble. depth=0 -> no effect,
         * depth=1 -> full dip to silence on the LFO trough. Advanced per-sample
         * so fast rates stay smooth. */
        float amp_gain = 1.0f;
        if (inst->amp_lfo_depth > 0.0f) {
            inst->amp_lfo_phase += inst->amp_lfo_phase_inc;
            while (inst->amp_lfo_phase >= 1.0f) inst->amp_lfo_phase -= 1.0f;
            float a = lfo_sample(inst->amp_lfo_shape, inst->amp_lfo_phase); /* -1..1 */
            amp_gain = 1.0f - inst->amp_lfo_depth * 0.5f * (1.0f - a);
        }

        float sl = wl * master_gain * amp_gain;
        float sr = wr * master_gain * amp_gain;
        if (sl > 32767.0f) sl = 32767.0f;
        if (sl < -32768.0f) sl = -32768.0f;
        if (sr > 32767.0f) sr = 32767.0f;
        if (sr < -32768.0f) sr = -32768.0f;

        out_interleaved_lr[2 * i + 0] = (int16_t)sl;
        out_interleaved_lr[2 * i + 1] = (int16_t)sr;
    }
}

/* ------------------------------------------------------------------------- */
/* v2 entry point                                                            */
/* ------------------------------------------------------------------------- */

static plugin_api_v2_t g_plugin_api_v2;

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
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
