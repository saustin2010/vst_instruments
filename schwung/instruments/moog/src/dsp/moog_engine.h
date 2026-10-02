/*
 * moog_engine.h - RaffoSynth synthesizer engine
 *
 * Based on RaffoSynth by Nicolas Roulet and Julian Palladino
 * https://github.com/nicoroulet/RaffoSynth
 * MIT License
 *
 * Ported from LV2 to standalone C for Move Anything.
 * Stripped LV2 dependencies, adapted for 44100Hz int16 output.
 */

#ifndef MOOG_ENGINE_H
#define MOOG_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOOG_MAX_KEYS 16
#define MOOG_SAMPLE_RATE 44100
#define MOOG_MAX_RENDER 256

/* Envelope states */
typedef enum {
    ENV_OFF = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} moog_env_state_t;

/* Key list node for note priority */
typedef struct moog_key_node {
    int note;
    float velocity;
    struct moog_key_node *next;
} moog_key_node_t;

/* Oscillator waveform types */
typedef enum {
    WAVE_TRIANGLE = 0,
    WAVE_SAWTOOTH,
    WAVE_SQUARE,
    WAVE_PULSE,
    WAVE_COUNT
} moog_wave_t;

/* RaffoSynth engine state */
typedef struct {
    /* Sample rate */
    float sample_rate;

    /* Oscillator parameters (4 oscillators) */
    moog_wave_t osc_wave[4];      /* Waveform type per oscillator */
    float osc_volume[4];          /* Volume per oscillator (0.0 - 1.0) */
    int   osc_range[4];           /* Octave range offset (-2 to +2) */
    float osc2_detune;            /* Oscillator 2 fine detune (0.0 - 1.0) */
    float osc3_detune;            /* Oscillator 3 fine detune (0.0 - 1.0) */
    float osc4_detune;            /* Oscillator 4 fine detune (0.0 - 1.0) */

    /* Filter parameters */
    float filter_cutoff;          /* Cutoff frequency (0.0 - 1.0) */
    float filter_resonance;       /* Resonance/emphasis (0.0 - 1.0) */
    float filter_contour;         /* Envelope amount to filter (0.0 - 1.0) */
    float filter_key_follow;      /* Key tracking amount (0.0 - 1.0) */

    /* Amplitude envelope (ADSR) */
    float amp_attack;             /* Attack time (0.0 - 1.0) */
    float amp_decay;              /* Decay time (0.0 - 1.0) */
    float amp_sustain;            /* Sustain level (0.0 - 1.0) */
    float amp_release;            /* Release time (0.0 - 1.0) */

    /* Filter envelope (ADSR) */
    float filt_attack;            /* Filter envelope attack (0.0 - 1.0) */
    float filt_decay;             /* Filter envelope decay (0.0 - 1.0) */
    float filt_sustain;           /* Filter envelope sustain (0.0 - 1.0) */
    float filt_release;           /* Filter envelope release (0.0 - 1.0) */

    /* Glide */
    float glide;                  /* Glide time (0.0 - 1.0) */

    /* Master */
    float master_volume;          /* Master output volume (0.0 - 1.0) */
    float noise_volume;           /* Noise mix level (0.0 - 1.0) */

    /* Mod wheel */
    float mod_wheel;              /* Mod wheel amount (0.0 - 1.0) */
    float mod_to_filter;          /* Mod wheel to filter cutoff (0.0 - 1.0) */
    float mod_to_pitch;           /* Mod wheel to pitch (0.0 - 1.0) */

    /* Pitch bend */
    float pitch_bend;             /* Current pitch bend (-1.0 to 1.0) */
    float bend_range;             /* Bend range in semitones (0.0 - 1.0, maps to 0-12) */

    /* Internal state - oscillators */
    double counter;               /* Master sample counter */
    double period;                /* Current note period in samples */
    double glide_period;          /* Glide target period */
    float last_val[5];            /* Last sample values (4 oscillators + noise) */

    /* Internal state - envelopes */
    moog_env_state_t amp_env_state;
    float amp_env_level;
    float amp_env_attack_level;   /* Level captured at attack start (for smooth retrigger) */
    float amp_env_release_level;  /* Level captured at release start */
    double amp_env_counter;

    moog_env_state_t filt_env_state;
    float filt_env_level;
    float filt_env_attack_level;  /* Level captured at attack start (for smooth retrigger) */
    float filt_env_release_level; /* Level captured at release start */
    double filt_env_counter;

    /* Internal state - filter */
    float filter_prev[6];         /* Filter state variables */

    /* Internal state - noise */
    uint32_t noise_seed;          /* LFSR noise state */

    /* Internal state - key tracking */
    int current_note;             /* Currently playing MIDI note */
    int gate_on;                  /* Gate state */

    /* Key list (simple array for note priority) */
    int key_stack[MOOG_MAX_KEYS];
    int key_stack_count;

    /* Octave transpose (plugin level) */
    int octave_transpose;

    /* LFO */
    float lfo_rate;               /* LFO rate (0.0 - 1.0) */
    float lfo_phase;              /* Current LFO phase */
    float lfo_depth_pitch;        /* LFO depth to pitch */
    float lfo_depth_filter;       /* LFO depth to filter */

    /* Velocity */
    float velocity;               /* Current note velocity */
    float velocity_sensitivity;   /* Velocity sensitivity (0.0 - 1.0) */

} moog_engine_t;

/* Initialize engine with defaults */
void moog_engine_init(moog_engine_t *engine);

/* Reset engine state (all notes off) */
void moog_engine_reset(moog_engine_t *engine);

/* Process MIDI note on */
void moog_engine_note_on(moog_engine_t *engine, int note, float velocity);

/* Process MIDI note off */
void moog_engine_note_off(moog_engine_t *engine, int note);

/* Process pitch bend */
void moog_engine_pitch_bend(moog_engine_t *engine, float bend);

/* Process mod wheel */
void moog_engine_mod_wheel(moog_engine_t *engine, float amount);

/* Render audio block (mono output, caller duplicates to stereo) */
void moog_engine_render(moog_engine_t *engine, float *output, int frames);

/* All notes off */
void moog_engine_all_notes_off(moog_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* MOOG_ENGINE_H */
