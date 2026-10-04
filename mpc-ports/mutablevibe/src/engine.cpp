/* Mutable Vibe MPC -- the Mac "Mutable Vibe" synth ported to an MPC OS native VST2 engine.
 *
 * Wraps two Mutable engines behind one `model` param: Rings (models 0-5) and a curated set of the
 * melodic Plaits engines (models 6-19; Elements, Noise and Grain are intentionally left out). Both DSP
 * cores are instantiated up front; `model` selects which one plays and renders. It is the thin adapter
 * the mpc-vst-plugins wrapper (wrapper/engine.h) drives, reusing the host-independent C API already
 * exported by the Mac plugin's rings_c.cpp / plaits_c.cpp (create/note_on/set_param/set_adsr/render) --
 * no DSP is touched here. Those wrappers + the eurorack Rings/Plaits/stmlib sources are vendored under
 * src/dsp/ and referenced in vst.json (build.sources). Keep the MPC port and the Mac plugin separate.
 *
 * Modulation (ported from the Mac PluginProcessor): 4 free envelopes (ENV3-6) and 4 LFOs, each with a
 * target destination + bipolar amount, summed into a per-target offset applied over the base params each
 * render block (applyModulation()). Tempo-sync and the 16-step LFO wave are dropped (the engine.h ABI has
 * no playhead); LFOs are free-running in Hz with waves Sine/Saw-Tri/Fold/Square/S&H. All amounts default
 * to 0 so an untouched patch sounds exactly like the pre-modulation port.
 *
 * Contract (wrapper/engine.h): 44100 Hz, interleaved int16 stereo, 128-frame blocks. Parameters are
 * string key/value pairs whose keys/ranges come from params.json. The special key "state" serialises
 * the whole instance for the host's project chunk (effGetChunk/effSetChunk). */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
extern "C" {
#include "engine.h"
}

extern "C" {
void  *rings_create();
void   rings_free(void *h);
void   rings_note_on(void *h, int midi_note, float velocity);
void   rings_note_off(void *h, int midi_note);
void   rings_set_adsr(void *h, float attack_ms, float decay_ms, float sustain, float release_ms);
void   rings_set_param(void *h, int idx, float v);
void   rings_set_sample_rate(void *h, float sr);
void   rings_set_pitch_bend(void *h, int bend_14bit);
void   rings_set_mod_wheel(void *h, float v);
void   rings_set_aftertouch(void *h, float v);
void   rings_all_notes_off(void *h);
void   rings_render(void *h, float *out_L, float *out_R, int n_frames);

void  *plaits_create();
void   plaits_free(void *h);
void   plaits_note_on(void *h, int midi_note, float velocity);
void   plaits_note_off(void *h, int midi_note);
void   plaits_set_adsr(void *h, float attack_ms, float decay_ms, float sustain, float release_ms);
void   plaits_set_param(void *h, int idx, float v);
void   plaits_set_model(void *h, int ui_idx);
void   plaits_set_sample_rate(void *h, float sr);
void   plaits_set_pitch_bend(void *h, int bend_14bit);
void   plaits_set_mod_wheel(void *h, float v);
void   plaits_set_aftertouch(void *h, float v);
void   plaits_all_notes_off(void *h);
void   plaits_render(void *h, float *out_L, float *out_R, int n_frames);
void   plaits_set_filter_params(void *h, float base_hz, float env_amt, float res, float morph,
                                float key_track, float atk_ms, float dec_ms, float sus,
                                float rel_ms, int slope);

void  *filter_create(float samplerate);
void   filter_free(void *ptr);
void   filter_set_cutoff(void *ptr, float hz);
void   filter_set_resonance(void *ptr, float r);
void   filter_set_morph(void *ptr, float m);       /* 0=LP  0.5=BP  1=HP */
void   filter_set_slope(void *ptr, int slope);     /* 0=12 dB/oct  1=24 dB/oct */
void   filter_set_fc_smooth(void *ptr, float hz);  /* snap the cutoff smoother (no state flush) */
void   filter_process(void *ptr, float *in, float *out, int frames);

void  *reverb_create(int sr);
void   reverb_free(void *r);
void   reverb_set_decay(void *r, float v);
void   reverb_set_damping(void *r, float v);
void   reverb_set_modulate(void *r, float v);
void   reverb_set_predelay(void *r, float ms);
void   reverb_set_wet(void *r, float v);
void   reverb_set_dry(void *r, float v);
void   reverb_set_hipass(void *r, float v);
void   reverb_process(void *r, float *in, float *out, int n_frames);

void  *delay_create(int samplerate);
void   delay_free(void *d);
void   delay_flush(void *d);
void   delay_set_time_ms(void *d, float v);
void   delay_set_feedback(void *d, float v);
void   delay_set_tone(void *d, float v);
void   delay_set_flutter(void *d, float v);
void   delay_set_head_bump(void *d, float v);
void   delay_set_hipass(void *d, float v);
void   delay_process(void *d, const float *in, float *out, int n);

void  *chorus_create(float samplerate);
void   chorus_free(void *ptr);
void   chorus_set_rate(void *ptr, float hz);
void   chorus_set_depth(void *ptr, float d);
void   chorus_set_wet(void *ptr, float w);
void   chorus_process(void *ptr, float *in, float *out, int frames);

void  *sat_create(float samplerate);
void   sat_free(void *ptr);
void   sat_set_amount(void *ptr, float v);
void   sat_process(void *ptr, float *buf, int frames);
}

/* Minimal linear ADSR, a drop-in for juce::ADSR (linear attack/decay/release). Used for ENV2 (filter env
 * on the Rings path) and ENV3-6 (free modulation envelopes). Times are ms. */
enum { ADSR_IDLE = 0, ADSR_ATK, ADSR_DEC, ADSR_SUS, ADSR_REL };
typedef struct {
    float sr;
    float atk_ms, dec_ms, rel_ms, sustain;
    float atk_inc, dec_inc, rel_inc;   /* per-sample level deltas */
    int   state;
    float level;
} adsr_t;

static void adsr_init(adsr_t *a, float sr) { a->sr = sr; a->state = ADSR_IDLE; a->level = 0.f;
    a->atk_ms = 10.f; a->dec_ms = 300.f; a->rel_ms = 500.f; a->sustain = 0.f; }
static void adsr_set(adsr_t *a, float atk_ms, float dec_ms, float sus, float rel_ms) {
    a->atk_ms = atk_ms > 0.1f ? atk_ms : 0.1f;
    a->dec_ms = dec_ms > 0.1f ? dec_ms : 0.1f;
    a->rel_ms = rel_ms > 0.1f ? rel_ms : 0.1f;
    a->sustain = sus < 0.f ? 0.f : (sus > 1.f ? 1.f : sus);
    a->atk_inc = 1000.f / (a->atk_ms * a->sr);
    a->dec_inc = (1.f - a->sustain) * 1000.f / (a->dec_ms * a->sr);
}
static void adsr_note_on(adsr_t *a)  { a->state = ADSR_ATK; }         /* retrigger from current level */
static void adsr_note_off(adsr_t *a) { if (a->state != ADSR_IDLE) {
    a->rel_inc = a->level * 1000.f / (a->rel_ms * a->sr); a->state = ADSR_REL; } }
static void adsr_reset(adsr_t *a)    { a->state = ADSR_IDLE; a->level = 0.f; }
static float adsr_process(adsr_t *a, int n) {
    for (int i = 0; i < n; i++) {
        switch (a->state) {
        case ADSR_ATK: a->level += a->atk_inc; if (a->level >= 1.f) { a->level = 1.f; a->state = ADSR_DEC; } break;
        case ADSR_DEC: a->level -= a->dec_inc; if (a->level <= a->sustain) { a->level = a->sustain; a->state = ADSR_SUS; } break;
        case ADSR_SUS: a->level = a->sustain; break;
        case ADSR_REL: a->level -= a->rel_inc; if (a->level <= 0.f) { a->level = 0.f; a->state = ADSR_IDLE; } break;
        default:       a->level = 0.f; break;
        }
    }
    return a->level;
}

/* ── Modulation target IDs (must match params.json <src>Target option order) ── */
enum {
    MOD_NONE = 0,
    MOD_STRUCTURE, MOD_BRIGHTNESS, MOD_DAMPING, MOD_POSITION, MOD_LPGDECAY,
    MOD_CUTOFF, MOD_RESONANCE, MOD_MORPH,
    MOD_REVDECAY, MOD_REVWET,
    MOD_DLYTIME, MOD_DLYFBK,
    MOD_COUNT
};

/* LFO waveform generator, ported from the Mac computeLFOValue (waves 0-4; 16-step dropped). */
static float lfo_wave_value(float phase, int waveform, float shape, float shValue) {
    const float TWO_PI = 6.28318530718f, HALF_PI = 1.57079632679f;
    switch (waveform) {
    case 0: { /* Sine + wavefold */
        float s = sinf(phase * TWO_PI);
        if (shape > 0.01f) s = sinf(s * (1.f + shape * 4.f) * HALF_PI);
        return s;
    }
    case 1: { /* Saw <-> Triangle <-> Reverse Saw */
        float peak = 1.f - shape; if (peak < 0.0001f) peak = 0.0001f; if (peak > 0.9999f) peak = 0.9999f;
        float s = phase < peak ? phase / peak : (1.f - phase) / (1.f - peak);
        return 2.f * s - 1.f;
    }
    case 2: { /* Triangle + linear wavefold on a skewed ramp */
        float peak = 0.5f - shape * 0.15f; if (peak < 0.35f) peak = 0.35f; if (peak > 0.5f) peak = 0.5f;
        float t = phase < peak ? phase / peak : (1.f - phase) / (1.f - peak);
        float s = 2.f * t - 1.f;
        if (shape > 0.01f) {
            float v = s * (1.f + shape * 6.f);
            float band = floorf((v + 1.f) * 0.25f);
            v = v - 4.f * band;
            if (v > 1.f) v = 2.f - v;
            int bandIdx = (int)fabsf(band);
            if (bandIdx == 2 || bandIdx == 4) {
                float v2 = v * 5.f;
                float band2 = floorf((v2 + 1.f) * 0.25f);
                v2 = v2 - 4.f * band2;
                if (v2 > 1.f) v2 = 2.f - v2;
                v = v2;
            }
            float decay = powf(0.75f, fabsf(band));
            s = -1.f + (v + 1.f) * decay;
        }
        return s;
    }
    case 3: /* Square + pulse width (5%-95%) */
        return phase < (0.05f + shape * 0.9f) ? 1.f : -1.f;
    case 4: /* S&H -- value set externally on phase wrap */
        return shValue;
    default: return 0.f;
    }
}

#define RINGS_MODELS 6
static const int kMelodicPlaits[14] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 14, 15 };
#define PLAITS_MODELS ((int)(sizeof(kMelodicPlaits) / sizeof(kMelodicPlaits[0])))
#define NUM_MODELS    (RINGS_MODELS + PLAITS_MODELS)   /* 20 */

typedef struct {
    float atk, dec, sus, rel;   /* ms / 0..1 / ms */
    int   target;               /* MOD_* */
    float amount;               /* -1..1 */
    adsr_t env;
} modenv_t;

typedef struct {
    float rate;                 /* Hz (free-run) */
    int   wave;                 /* 0..4 */
    float shape;                /* 0..1 */
    int   target;               /* MOD_* */
    float amount;               /* -1..1 */
    int   bipolar;              /* 0 unipolar, 1 bipolar */
    int   sync;                 /* 0 free, 1 tempo-synced */
    int   div;                  /* division index into kDivBeats */
    float phase, shValue, shSmoothed, level;
} lfo_t;

/* Tempo-sync divisions (beats per cycle), matching params.json div options
 * ["1/16","1/8","1/4","1/2","1","2","4"]: a 1/4 note = 1 beat. */
/* LFO tempo-sync divisions: straight, up to long cycles (4 bars) -- long LFO cycles are musically useful. */
static const float kDivBeats[7] = { 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f };
static const char *kDivLabels[7] = { "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1" };
/* Delay tempo-sync divisions: includes two dotted values (1/8. , 1/4.) for classic dotted echoes; tops out
 * at 1/1 (very long delays are rarely wanted). Separate from the LFO set (see kDivBeats). */
static const float kDelayDivBeats[7] = { 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f, 4.f };
static const char *kDelayDivLabels[7] = { "1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1/1" };

typedef struct {
    void *rings;
    void *plaits;
    void *filter;                             /* shared filter for the Rings path */
    void *reverb, *delay, *chorus, *sat;      /* FX chain */
    float structure, brightness, damping, position;
    int   model;                              /* 0..NUM_MODELS-1 */
    float attack, decay, sustain, release;    /* attack/decay/release in ms, sustain 0..1 */
    float filterCutoff, filterResonance, filterMorph;   /* Hz, 0..1, 0..1 */
    int   filterSlope;                        /* 0=12 dB/oct  1=24 dB/oct */
    float reverbDecay, reverbDamping, reverbWet, reverbHipass;
    float delayTime, delayFeedback, delayTone, delayWet;   /* delayTime in ms */
    float chorusRate, chorusDepth, chorusWet;              /* chorusRate in Hz */
    float satDrive;
    /* ENV2 -> filter env */
    float filterEnvAmount;
    float env2Attack, env2Decay, env2Sustain, env2Release; /* ms / 0..1 / ms */
    adsr_t env2;                     /* port-side ENV2 for the Rings path (Plaits does its own per-voice) */
    float cutoffSmooth_;             /* one-pole smoother state for the Rings-path cutoff */
    int   noteCount_;                /* held-note count, gates the envelopes */
    /* Modulation */
    modenv_t menv[6];                /* [0,1]=ENV3/4 envs, [2,3]=Velocity slots, [4]=Mod Wheel, [5]=Aftertouch */
    lfo_t    lfo[4];                 /* LFO1-4 */
    int      modActive[MOD_COUNT];   /* per-target: a nonzero offset was pushed last block (so we restore base) */
    int      filterModWas;           /* filter cutoff/res/morph modulation was active last block */
    /* Global / performance */
    float    bpm;                    /* host tempo from the wrapper's "lfo_bpm" param (HAS_LFO_BPM) */
    int      octaveSemis;            /* octave transpose in semitones (octave * 12) */
    int      poly;                   /* Rings polyphony 1..4 (Plaits is fixed 4) */
    float    lpgDecay;               /* Plaits LPG decay 0..1 */
    int      delaySync;              /* 0 free, 1 tempo-synced */
    int      delayDiv;               /* division index into kDivBeats */
    float    delayTimeCur;           /* last delay time pushed (ms), so we only re-set on change */
    float    noteVel;                /* last note-on velocity 0..1, latched -- source for the velocity mod slots */
    float    modWheel;               /* CC1 0..1, latched -- source for the Mod Wheel mod slot (menv[4]) */
    float    aftertouch;             /* channel pressure 0..1, latched -- source for the Aftertouch slot (menv[5]) */
} state_t;

static int model_is_plaits(int model) { return model >= RINGS_MODELS; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void push_adsr(state_t *s) {
    rings_set_adsr(s->rings, s->attack, s->decay, s->sustain, s->release);
    plaits_set_adsr(s->plaits, s->attack, s->decay, s->sustain, s->release);
}

/* push the filter settings to both paths (base values, no modulation) */
static void push_filter(state_t *s) {
    filter_set_cutoff(s->filter, s->filterCutoff);
    filter_set_resonance(s->filter, s->filterResonance);
    filter_set_morph(s->filter, 0.0f);   /* morph killed: filter is a clean resonant LP (see NOTES) */
    filter_set_slope(s->filter, s->filterSlope);
    plaits_set_filter_params(s->plaits, s->filterCutoff, s->filterEnvAmount, s->filterResonance,
                             0.0f, 0.0f,
                             s->env2Attack, s->env2Decay, s->env2Sustain, s->env2Release, s->filterSlope);
}

/* retrigger / release / reset the note-gated envelopes together */
static void envs_note_on(state_t *s)  { adsr_note_on(&s->env2);
    for (int i = 0; i < 4; i++) adsr_note_on(&s->menv[i].env); }
static void envs_note_off(state_t *s) { adsr_note_off(&s->env2);
    for (int i = 0; i < 4; i++) adsr_note_off(&s->menv[i].env); }
static void envs_reset(state_t *s)    { adsr_reset(&s->env2);
    for (int i = 0; i < 4; i++) adsr_reset(&s->menv[i].env); }

/* ── Delay safety / tone shaping (ported from the Vibe FX insert) ──────────────────────────────
 * The tape soft-clip in the feedback path (x*(1.5-0.5x^2)) has ~1.5x small-signal gain, so the real
 * loop gain is feedback*1.5. Cap the coefficient at 0.66 -> loop gain ~0.99 < 1: the delay still ALWAYS
 * decays when the input stops (very long near-self-oscillating tails at the top, but it can never feed
 * itself louder / run away). Paired with head_bump = 0 at create() (removes the 60 Hz feedback boost). */
static inline float delay_fb_curve(float f) {
    float c = f * 0.66f;
    return c < 0.f ? 0.f : (c > 0.66f ? 0.66f : c);
}
/* The one-pole feedback HP in the core is coef = 1 - v*0.98 (cutoff ~ linear in v), which bunches the
 * musical range into the first ~15% of a linear knob. Map u (0..1) to a LOG frequency (25 Hz .. 2500 Hz)
 * so each increment is a constant interval, then invert back to the core's v. */
static inline float hp_warp(float u) {
    if (u <= 0.001f) return 0.f;
    float fc = 25.f * powf(100.f, u);
    float v  = fc * 6.2831853f / (0.98f * 44100.f);
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}
/* delayTone is one bipolar knob: 0.5 = neutral, left half a low-pass (dark), right half a feedback
 * high-pass (thins the runaway low end). Centre does nothing. */
static void apply_delay_tone(state_t *s) {
    float t = s->delayTone;
    if (t < 0.5f) { delay_set_tone(s->delay, t * 2.f); delay_set_hipass(s->delay, 0.f); }
    else          { delay_set_tone(s->delay, 1.f);     delay_set_hipass(s->delay, hp_warp((t - 0.5f) * 2.f)); }
}

/* apply one actual (un-normalised) value; the wrapper has already mapped 0..1 -> range for us */
static void apply(state_t *s, const char *key, double v) {
    if      (!strcmp(key, "structure"))  { s->structure  = (float)v; rings_set_param(s->rings, 0, s->structure);  plaits_set_param(s->plaits, 0, s->structure);  }
    else if (!strcmp(key, "brightness")) { s->brightness = (float)v; rings_set_param(s->rings, 1, s->brightness); plaits_set_param(s->plaits, 1, s->brightness); }
    else if (!strcmp(key, "damping"))    { s->damping    = (float)v; rings_set_param(s->rings, 2, s->damping);    plaits_set_param(s->plaits, 2, s->damping);    }
    else if (!strcmp(key, "position"))   { s->position   = (float)v; rings_set_param(s->rings, 3, s->position);   plaits_set_param(s->plaits, 3, s->position);   }
    else if (!strcmp(key, "model"))      {
        int m = (int)(v + 0.5); if (m < 0) m = 0; if (m > NUM_MODELS - 1) m = NUM_MODELS - 1;
        if (m != s->model) {                  /* switching model: silence both cores to avoid stuck notes */
            rings_all_notes_off(s->rings);
            plaits_all_notes_off(s->plaits);
            s->noteCount_ = 0;
            envs_reset(s);
        }
        s->model = m;
        if (model_is_plaits(m)) plaits_set_model(s->plaits, kMelodicPlaits[m - RINGS_MODELS]);
        else                    rings_set_param(s->rings, 4, (float)m);
    }
    else if (!strcmp(key, "attack"))     { s->attack  = (float)v; push_adsr(s); }
    else if (!strcmp(key, "decay"))      { s->decay   = (float)v; push_adsr(s); }
    else if (!strcmp(key, "sustain"))    { s->sustain = (float)v; push_adsr(s); }
    else if (!strcmp(key, "release"))    { s->release = (float)v; push_adsr(s); }
    else if (!strcmp(key, "filterCutoff"))    { s->filterCutoff    = (float)v; push_filter(s); }
    else if (!strcmp(key, "filterResonance")) { s->filterResonance = (float)v; push_filter(s); }
    else if (!strcmp(key, "filterMorph"))     { s->filterMorph     = (float)v; push_filter(s); }
    else if (!strcmp(key, "filterSlope"))     { int sl = (int)(v + 0.5); s->filterSlope = sl <= 0 ? 0 : 1; push_filter(s); }
    else if (!strcmp(key, "reverbDecay"))     { s->reverbDecay    = (float)v; reverb_set_decay(s->reverb, s->reverbDecay);     }
    else if (!strcmp(key, "reverbDamping"))   { s->reverbDamping  = (float)v; reverb_set_damping(s->reverb, s->reverbDamping); }
    else if (!strcmp(key, "reverbWet"))       { s->reverbWet      = (float)v; reverb_set_wet(s->reverb, s->reverbWet);         }
    else if (!strcmp(key, "reverbHipass"))    { s->reverbHipass   = (float)v; reverb_set_hipass(s->reverb, hp_warp(s->reverbHipass)); }
    else if (!strcmp(key, "delayTime"))       { s->delayTime      = (float)v; delay_set_time_ms(s->delay, s->delayTime);       }
    else if (!strcmp(key, "delayFeedback"))   { s->delayFeedback  = (float)v; delay_set_feedback(s->delay, delay_fb_curve(s->delayFeedback)); }
    else if (!strcmp(key, "delayTone"))       { s->delayTone      = (float)v; apply_delay_tone(s);                            }
    else if (!strcmp(key, "delayWet"))        { s->delayWet       = (float)v; /* send level applied in render */               }
    else if (!strcmp(key, "chorusRate"))      { s->chorusRate     = (float)v; chorus_set_rate(s->chorus, s->chorusRate);       }
    else if (!strcmp(key, "chorusDepth"))     { s->chorusDepth    = (float)v; chorus_set_depth(s->chorus, s->chorusDepth);     }
    else if (!strcmp(key, "chorusWet"))       { s->chorusWet      = (float)v; chorus_set_wet(s->chorus, s->chorusWet);         }
    else if (!strcmp(key, "satDrive"))        { s->satDrive       = (float)v; sat_set_amount(s->sat, s->satDrive);             }
    else if (!strcmp(key, "filterEnvAmount")) { s->filterEnvAmount = (float)v; push_filter(s); }
    else if (!strcmp(key, "env2Attack"))      { s->env2Attack  = (float)v; adsr_set(&s->env2, s->env2Attack, s->env2Decay, s->env2Sustain, s->env2Release); push_filter(s); }
    else if (!strcmp(key, "env2Decay"))       { s->env2Decay   = (float)v; adsr_set(&s->env2, s->env2Attack, s->env2Decay, s->env2Sustain, s->env2Release); push_filter(s); }
    else if (!strcmp(key, "env2Sustain"))     { s->env2Sustain = (float)v; adsr_set(&s->env2, s->env2Attack, s->env2Decay, s->env2Sustain, s->env2Release); push_filter(s); }
    else if (!strcmp(key, "env2Release"))     { s->env2Release = (float)v; adsr_set(&s->env2, s->env2Attack, s->env2Decay, s->env2Sustain, s->env2Release); push_filter(s); }
    else if (!strncmp(key, "env", 3) && key[3] >= '3' && key[3] <= '6') {
        int i = key[3] - '3';                 /* menv[0..3] = ENV3..ENV6 */
        modenv_t *m = &s->menv[i];
        const char *f = key + 4;
        if      (!strcmp(f, "Attack"))  { m->atk = (float)v; adsr_set(&m->env, m->atk, m->dec, m->sus, m->rel); }
        else if (!strcmp(f, "Decay"))   { m->dec = (float)v; adsr_set(&m->env, m->atk, m->dec, m->sus, m->rel); }
        else if (!strcmp(f, "Sustain")) { m->sus = (float)v; adsr_set(&m->env, m->atk, m->dec, m->sus, m->rel); }
        else if (!strcmp(f, "Release")) { m->rel = (float)v; adsr_set(&m->env, m->atk, m->dec, m->sus, m->rel); }
        else if (!strcmp(f, "Target"))  { int t = (int)(v + 0.5); m->target = (t < 0 ? 0 : (t >= MOD_COUNT ? MOD_COUNT - 1 : t)); }
        else if (!strcmp(f, "Amount"))  { m->amount = clampf((float)v, -1.f, 1.f); }
    }
    else if (!strncmp(key, "modwheel", 8) || !strncmp(key, "aftertouch", 10)) {
        modenv_t *m = &s->menv[key[0] == 'm' ? 4 : 5];   /* Mod Wheel -> menv[4], Aftertouch -> menv[5] */
        const char *f = key + (key[0] == 'm' ? 8 : 10);
        if      (!strcmp(f, "Target")) { int t = (int)(v + 0.5); m->target = (t < 0 ? 0 : (t >= MOD_COUNT ? MOD_COUNT - 1 : t)); }
        else if (!strcmp(f, "Amount")) { m->amount = clampf((float)v, -1.f, 1.f); }
    }
    else if (!strncmp(key, "lfo", 3) && key[3] >= '1' && key[3] <= '4') {
        int i = key[3] - '1';                 /* lfo[0..3] = LFO1..LFO4 */
        lfo_t *l = &s->lfo[i];
        const char *f = key + 4;
        if      (!strcmp(f, "Rate"))    { l->rate  = (float)v; }
        else if (!strcmp(f, "Wave"))    { int w = (int)(v + 0.5); l->wave = (w < 0 ? 0 : (w > 4 ? 4 : w)); }
        else if (!strcmp(f, "Shape"))   { l->shape = clampf((float)v, 0.f, 1.f); }
        else if (!strcmp(f, "Target"))  { int t = (int)(v + 0.5); l->target = (t < 0 ? 0 : (t >= MOD_COUNT ? MOD_COUNT - 1 : t)); }
        else if (!strcmp(f, "Amount"))  { l->amount = clampf((float)v, -1.f, 1.f); }
        else if (!strcmp(f, "Bipolar")) { l->bipolar = (v >= 0.5) ? 1 : 0; }
        else if (!strcmp(f, "Sync"))    { l->sync = (v >= 0.5) ? 1 : 0; }
        else if (!strcmp(f, "Div"))     { int d = (int)(v + 0.5); l->div = (d < 0 ? 0 : (d > 6 ? 6 : d)); }
    }
    else if (!strcmp(key, "lfo_bpm"))    { if (v > 1.0) s->bpm = (float)v; }   /* host tempo from the wrapper */
    else if (!strcmp(key, "lpgDecay"))   { s->lpgDecay = clampf((float)v, 0.f, 1.f); plaits_set_param(s->plaits, 4, s->lpgDecay); }
    else if (!strcmp(key, "polyphony"))  { int p = (int)(v + 0.5) + 1; s->poly = (p < 1 ? 1 : (p > 4 ? 4 : p)); rings_set_param(s->rings, 5, (float)s->poly); }   /* enum index 0..3 -> 1..4 voices */
    else if (!strcmp(key, "octave"))     {
        int oct = (int)lrint(v); if (oct < -2) oct = -2; if (oct > 2) oct = 2;
        int semis = oct * 12;
        if (semis != s->octaveSemis) {          /* transpose changed: silence to avoid stuck (mismatched) notes */
            rings_all_notes_off(s->rings); plaits_all_notes_off(s->plaits);
            s->noteCount_ = 0; envs_reset(s);
            s->octaveSemis = semis;
        }
    }
    else if (!strcmp(key, "delaySync"))  { s->delaySync = (v >= 0.5) ? 1 : 0; }
    else if (!strcmp(key, "delayDiv"))   { int d = (int)(v + 0.5); s->delayDiv = (d < 0 ? 0 : (d > 6 ? 6 : d)); }
}

static void *create(const char *data_dir) {
    (void)data_dir;
    state_t *s = (state_t *)calloc(1, sizeof(state_t));
    if (!s) return NULL;
    s->rings  = rings_create();
    s->plaits = plaits_create();
    s->filter = filter_create(44100.0f);
    s->reverb = reverb_create(44100);
    s->delay  = delay_create(44100);
    delay_set_head_bump(s->delay, 0.f);   /* safety: remove the 60 Hz feedback boost (bass runaway), see delay_fb_curve */
    s->chorus = chorus_create(44100.0f);
    s->sat    = sat_create(44100.0f);
    if (!s->rings || !s->plaits || !s->filter || !s->reverb || !s->delay || !s->chorus || !s->sat) {
        if (s->rings)  rings_free(s->rings);
        if (s->plaits) plaits_free(s->plaits);
        if (s->filter) filter_free(s->filter);
        if (s->reverb) reverb_free(s->reverb);
        if (s->delay)  delay_free(s->delay);
        if (s->chorus) chorus_free(s->chorus);
        if (s->sat)    sat_free(s->sat);
        free(s);
        return NULL;
    }
    rings_set_sample_rate(s->rings, 44100.0f);
    plaits_set_sample_rate(s->plaits, 44100.0f);
    adsr_init(&s->env2, 44100.0f);
    for (int i = 0; i < 6; i++) adsr_init(&s->menv[i].env, 44100.0f);
    for (int i = 0; i < 4; i++) {
        s->lfo[i].phase = 0.f; s->lfo[i].shValue = 0.f; s->lfo[i].shSmoothed = 0.f; s->lfo[i].level = 0.f;
    }
    s->noteCount_ = 0;
    /* reverb runs as a send: dry=0, no predelay/modulation for now (fixed neutral) */
    reverb_set_dry(s->reverb, 0.0f);
    reverb_set_modulate(s->reverb, 0.0f);
    reverb_set_predelay(s->reverb, 0.0f);
    /* defaults must match params.json so the first getParameter() matches each param's declared default */
    apply(s, "structure", 0.5);  apply(s, "brightness", 0.5);
    apply(s, "damping",   0.5);  apply(s, "position",   0.3);
    apply(s, "model", 1);   /* default model = R: SYMPATHETIC (plays on open) */
    apply(s, "attack", 2.0);  apply(s, "decay", 400.0);
    apply(s, "sustain", 0.8); apply(s, "release", 600.0);
    apply(s, "filterCutoff", 20000.0);  apply(s, "filterResonance", 0.0);
    apply(s, "filterMorph", 0.0);       apply(s, "filterSlope", 1);
    apply(s, "reverbDecay", 0.7);   apply(s, "reverbDamping", 0.0);  apply(s, "reverbWet", 0.0);
    apply(s, "reverbHipass", 0.0);
    apply(s, "delayTime", 300.0);   apply(s, "delayFeedback", 0.3);
    apply(s, "delayTone", 0.5);     apply(s, "delayWet", 0.0);
    apply(s, "chorusRate", 0.5);    apply(s, "chorusDepth", 0.3);    apply(s, "chorusWet", 0.0);
    apply(s, "satDrive", 0.0);
    apply(s, "filterEnvAmount", 0.0);
    apply(s, "env2Attack", 10.0);  apply(s, "env2Decay", 300.0);
    apply(s, "env2Sustain", 0.0);  apply(s, "env2Release", 500.0);
    /* ENV3-6 + LFO1-4: amounts default 0 (no modulation), so the raw synth is unchanged */
    for (int n = 3; n <= 6; n++) {
        char k[16];
        snprintf(k, sizeof k, "env%dAttack",  n); apply(s, k, 10.0);
        snprintf(k, sizeof k, "env%dDecay",   n); apply(s, k, 300.0);
        snprintf(k, sizeof k, "env%dSustain", n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "env%dRelease", n); apply(s, k, 500.0);
        snprintf(k, sizeof k, "env%dTarget",  n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "env%dAmount",  n); apply(s, k, 0.0);
    }
    for (int n = 1; n <= 4; n++) {
        char k[16];
        snprintf(k, sizeof k, "lfo%dRate",    n); apply(s, k, 1.0);
        snprintf(k, sizeof k, "lfo%dWave",    n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "lfo%dShape",   n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "lfo%dTarget",  n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "lfo%dAmount",  n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "lfo%dBipolar", n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "lfo%dSync",    n); apply(s, k, 0.0);
        snprintf(k, sizeof k, "lfo%dDiv",     n); apply(s, k, 2.0);   /* 1/4 note */
    }
    /* Global / performance params */
    s->bpm = 120.f;
    apply(s, "lpgDecay", 1.0);   apply(s, "polyphony", 3.0);  apply(s, "octave", 0.0);   /* poly index 3 = 4 voices */
    apply(s, "delaySync", 1.0);  apply(s, "delayDiv", 2.0);   /* default synced */
    s->cutoffSmooth_ = s->filterCutoff;
    s->delayTimeCur  = s->delayTime;
    return s;
}

static void destroy(void *inst) {
    state_t *s = (state_t *)inst;
    if (!s) return;
    rings_free(s->rings);
    plaits_free(s->plaits);
    filter_free(s->filter);
    reverb_free(s->reverb);
    delay_free(s->delay);
    chorus_free(s->chorus);
    sat_free(s->sat);
    free(s);
}

/* route note messages to the active core, control messages to both so they stay in sync on a switch */
static void midi(void *inst, const uint8_t *msg, int len) {
    state_t *s = (state_t *)inst;
    if (len < 2) return;
    int plaits = model_is_plaits(s->model);
    uint8_t st = msg[0] & 0xF0;
    int note = (int)msg[1] + s->octaveSemis;     /* octave transpose */
    if (note < 0) note = 0; else if (note > 127) note = 127;
    switch (st) {
    case 0x90:
        if (len >= 3 && msg[2] > 0) {
            s->noteVel = msg[2] / 127.0f;        /* latch velocity for the velocity mod slots (ENV5/6) */
            if (plaits) plaits_note_on(s->plaits, note, msg[2] / 127.0f);
            else        rings_note_on(s->rings, note, msg[2] / 127.0f);
            s->noteCount_++;
            envs_note_on(s);                     /* ENV2-6 retrigger on every note-on */
            s->cutoffSmooth_ = s->filterCutoff;  /* snap the Rings-path cutoff so the env sweep is clickless */
            filter_set_fc_smooth(s->filter, s->filterCutoff);
            break;
        }
        /* note-on with velocity 0 is a note-off */
        if (plaits) plaits_note_off(s->plaits, note); else rings_note_off(s->rings, note);
        if (--s->noteCount_ <= 0) { s->noteCount_ = 0; envs_note_off(s); }
        break;
    case 0x80:
        if (plaits) plaits_note_off(s->plaits, note); else rings_note_off(s->rings, note);
        if (--s->noteCount_ <= 0) { s->noteCount_ = 0; envs_note_off(s); }
        break;
    case 0xE0:
        if (len >= 3) {
            int bend = msg[1] | (msg[2] << 7);
            rings_set_pitch_bend(s->rings, bend);
            plaits_set_pitch_bend(s->plaits, bend);
        }
        break;
    case 0xB0:
        if (len >= 3) {
            if (msg[1] == 1)   { s->modWheel = msg[2] / 127.0f;                    /* latch for the Mod Wheel mod slot */
                                 rings_set_mod_wheel(s->rings, msg[2] / 127.0f); plaits_set_mod_wheel(s->plaits, msg[2] / 127.0f); }
            if (msg[1] >= 120) { rings_all_notes_off(s->rings); plaits_all_notes_off(s->plaits);
                                 s->noteCount_ = 0; envs_reset(s); }
        }
        break;
    case 0xD0:
        s->aftertouch = msg[1] / 127.0f;                     /* latch for the Aftertouch mod slot */
        rings_set_aftertouch(s->rings, msg[1] / 127.0f);
        plaits_set_aftertouch(s->plaits, msg[1] / 127.0f);
        break;
    default: break;
    }
}

static int serialise(state_t *s, char *buf, int len) {
    int n = snprintf(buf, len,
        "structure=%.6g;brightness=%.6g;damping=%.6g;position=%.6g;model=%d;"
        "attack=%.6g;decay=%.6g;sustain=%.6g;release=%.6g;"
        "filterCutoff=%.6g;filterResonance=%.6g;filterMorph=%.6g;filterSlope=%d;"
        "reverbDecay=%.6g;reverbDamping=%.6g;reverbWet=%.6g;"
        "delayTime=%.6g;delayFeedback=%.6g;delayTone=%.6g;delayWet=%.6g;"
        "chorusRate=%.6g;chorusDepth=%.6g;chorusWet=%.6g;satDrive=%.6g;"
        "filterEnvAmount=%.6g;env2Attack=%.6g;env2Decay=%.6g;env2Sustain=%.6g;env2Release=%.6g",
        s->structure, s->brightness, s->damping, s->position, s->model,
        s->attack, s->decay, s->sustain, s->release,
        s->filterCutoff, s->filterResonance, s->filterMorph, s->filterSlope,
        s->reverbDecay, s->reverbDamping, s->reverbWet,
        s->delayTime, s->delayFeedback, s->delayTone, s->delayWet,
        s->chorusRate, s->chorusDepth, s->chorusWet, s->satDrive,
        s->filterEnvAmount, s->env2Attack, s->env2Decay, s->env2Sustain, s->env2Release);
    if (n < 0 || n >= len) return n;
    for (int i = 0; i < 4 && n < len; i++) {
        modenv_t *m = &s->menv[i];
        n += snprintf(buf + n, len - n,
            ";env%dAttack=%.6g;env%dDecay=%.6g;env%dSustain=%.6g;env%dRelease=%.6g;env%dTarget=%d;env%dAmount=%.6g",
            i + 3, m->atk, i + 3, m->dec, i + 3, m->sus, i + 3, m->rel, i + 3, m->target, i + 3, m->amount);
    }
    for (int i = 0; i < 4 && n < len; i++) {
        lfo_t *l = &s->lfo[i];
        n += snprintf(buf + n, len - n,
            ";lfo%dRate=%.6g;lfo%dWave=%d;lfo%dShape=%.6g;lfo%dTarget=%d;lfo%dAmount=%.6g;lfo%dBipolar=%d;lfo%dSync=%d;lfo%dDiv=%d",
            i + 1, l->rate, i + 1, l->wave, i + 1, l->shape, i + 1, l->target, i + 1, l->amount, i + 1, l->bipolar,
            i + 1, l->sync, i + 1, l->div);
    }
    if (n < len)
        n += snprintf(buf + n, len - n,
            ";lpgDecay=%.6g;polyphony=%d;octave=%d;delaySync=%d;delayDiv=%d;reverbHipass=%.6g"
            ";modwheelTarget=%d;modwheelAmount=%.6g;aftertouchTarget=%d;aftertouchAmount=%.6g",
            s->lpgDecay, s->poly - 1, s->octaveSemis / 12, s->delaySync, s->delayDiv, s->reverbHipass,
            s->menv[4].target, s->menv[4].amount, s->menv[5].target, s->menv[5].amount);
    return n;
}

static void deserialise(state_t *s, const char *str) {
    char tmp[8192];
    strncpy(tmp, str, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    for (char *tok = strtok(tmp, ";"); tok; tok = strtok(NULL, ";")) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        apply(s, tok, atof(eq + 1));
    }
}

static void set_param(void *inst, const char *key, const char *val) {
    state_t *s = (state_t *)inst;
    if (!strcmp(key, "state")) { deserialise(s, val); return; }
    apply(s, key, atof(val));
}

static int get_param(void *inst, const char *key, char *buf, int buf_len) {
    state_t *s = (state_t *)inst;
    if (!strcmp(key, "state"))      return serialise(s, buf, buf_len);
    if (!strcmp(key, "structure"))  return snprintf(buf, buf_len, "%.6g", s->structure);
    if (!strcmp(key, "brightness")) return snprintf(buf, buf_len, "%.6g", s->brightness);
    if (!strcmp(key, "damping"))    return snprintf(buf, buf_len, "%.6g", s->damping);
    if (!strcmp(key, "position"))   return snprintf(buf, buf_len, "%.6g", s->position);
    if (!strcmp(key, "model"))      return snprintf(buf, buf_len, "%d",   s->model);
    if (!strcmp(key, "attack"))     return snprintf(buf, buf_len, "%.6g", s->attack);
    if (!strcmp(key, "decay"))      return snprintf(buf, buf_len, "%.6g", s->decay);
    if (!strcmp(key, "sustain"))    return snprintf(buf, buf_len, "%.6g", s->sustain);
    if (!strcmp(key, "release"))    return snprintf(buf, buf_len, "%.6g", s->release);
    if (!strcmp(key, "filterCutoff"))    return snprintf(buf, buf_len, "%.6g", s->filterCutoff);
    if (!strcmp(key, "filterResonance")) return snprintf(buf, buf_len, "%.6g", s->filterResonance);
    if (!strcmp(key, "filterMorph"))     return snprintf(buf, buf_len, "%.6g", s->filterMorph);   /* unused (morph killed) */
    if (!strcmp(key, "filterSlope"))     return snprintf(buf, buf_len, "%d",   s->filterSlope);
    if (!strcmp(key, "reverbDecay"))     return snprintf(buf, buf_len, "%.6g", s->reverbDecay);
    if (!strcmp(key, "reverbDamping"))   return snprintf(buf, buf_len, "%.6g", s->reverbDamping);
    if (!strcmp(key, "reverbWet"))       return snprintf(buf, buf_len, "%.6g", s->reverbWet);
    if (!strcmp(key, "reverbHipass"))    return snprintf(buf, buf_len, "%.6g", s->reverbHipass);
    if (!strcmp(key, "delayTime")) {     /* string display: synced -> division + resulting ms, free -> ms */
        if (s->delaySync) {
            float ms = kDelayDivBeats[s->delayDiv] * 60000.f / (s->bpm > 1.f ? s->bpm : 120.f);
            return snprintf(buf, buf_len, "%.0f ms (%s)", ms, kDelayDivLabels[s->delayDiv]);   /* ms first: knob norm stays sane */
        }
        return snprintf(buf, buf_len, "%.0f ms", s->delayTime);
    }
    if (!strcmp(key, "delayFeedback"))   return snprintf(buf, buf_len, "%.6g", s->delayFeedback);
    if (!strcmp(key, "delayTone"))       return snprintf(buf, buf_len, "%.6g", s->delayTone);
    if (!strcmp(key, "delayWet"))        return snprintf(buf, buf_len, "%.6g", s->delayWet);
    if (!strcmp(key, "chorusRate"))      return snprintf(buf, buf_len, "%.6g", s->chorusRate);
    if (!strcmp(key, "chorusDepth"))     return snprintf(buf, buf_len, "%.6g", s->chorusDepth);
    if (!strcmp(key, "chorusWet"))       return snprintf(buf, buf_len, "%.6g", s->chorusWet);
    if (!strcmp(key, "satDrive"))        return snprintf(buf, buf_len, "%.6g", s->satDrive);
    if (!strcmp(key, "filterEnvAmount")) return snprintf(buf, buf_len, "%.6g", s->filterEnvAmount);
    if (!strcmp(key, "env2Attack"))      return snprintf(buf, buf_len, "%.6g", s->env2Attack);
    if (!strcmp(key, "env2Decay"))       return snprintf(buf, buf_len, "%.6g", s->env2Decay);
    if (!strcmp(key, "env2Sustain"))     return snprintf(buf, buf_len, "%.6g", s->env2Sustain);
    if (!strcmp(key, "env2Release"))     return snprintf(buf, buf_len, "%.6g", s->env2Release);
    if (!strncmp(key, "env", 3) && key[3] >= '3' && key[3] <= '6') {
        modenv_t *m = &s->menv[key[3] - '3'];
        const char *f = key + 4;
        if (!strcmp(f, "Attack"))  return snprintf(buf, buf_len, "%.6g", m->atk);
        if (!strcmp(f, "Decay"))   return snprintf(buf, buf_len, "%.6g", m->dec);
        if (!strcmp(f, "Sustain")) return snprintf(buf, buf_len, "%.6g", m->sus);
        if (!strcmp(f, "Release")) return snprintf(buf, buf_len, "%.6g", m->rel);
        if (!strcmp(f, "Target"))  return snprintf(buf, buf_len, "%d",   m->target);
        if (!strcmp(f, "Amount"))  return snprintf(buf, buf_len, "%.6g", m->amount);
    }
    if (!strncmp(key, "modwheel", 8) || !strncmp(key, "aftertouch", 10)) {
        modenv_t *m = &s->menv[key[0] == 'm' ? 4 : 5];
        const char *f = key + (key[0] == 'm' ? 8 : 10);
        if (!strcmp(f, "Target")) return snprintf(buf, buf_len, "%d",   m->target);
        if (!strcmp(f, "Amount")) return snprintf(buf, buf_len, "%.6g", m->amount);
    }
    if (!strncmp(key, "lfo", 3) && key[3] >= '1' && key[3] <= '4') {
        lfo_t *l = &s->lfo[key[3] - '1'];
        const char *f = key + 4;
        if (!strcmp(f, "Rate"))    return snprintf(buf, buf_len, "%.6g", l->rate);
        if (!strcmp(f, "Wave"))    return snprintf(buf, buf_len, "%d",   l->wave);
        if (!strcmp(f, "Shape"))   return snprintf(buf, buf_len, "%.6g", l->shape);
        if (!strcmp(f, "Target"))  return snprintf(buf, buf_len, "%d",   l->target);
        if (!strcmp(f, "Amount"))  return snprintf(buf, buf_len, "%.6g", l->amount);
        if (!strcmp(f, "Bipolar")) return snprintf(buf, buf_len, "%d",   l->bipolar);
        if (!strcmp(f, "Sync"))    return snprintf(buf, buf_len, "%d",   l->sync);
        if (!strcmp(f, "Div"))     return snprintf(buf, buf_len, "%d",   l->div);
    }
    if (!strcmp(key, "lpgDecay"))  return snprintf(buf, buf_len, "%.6g", s->lpgDecay);
    if (!strcmp(key, "polyphony")) return snprintf(buf, buf_len, "%d",   s->poly - 1);   /* -> enum index 0..3 */
    if (!strcmp(key, "octave"))    return snprintf(buf, buf_len, "%d",   s->octaveSemis / 12);
    if (!strcmp(key, "delaySync")) return snprintf(buf, buf_len, "%d",   s->delaySync);
    if (!strcmp(key, "delayDiv"))  return snprintf(buf, buf_len, "%d",   s->delayDiv);
    /* dynamic_name (wrapper asks "<key>_name"): the four macro knobs and LPG decay read differently on the
     * Plaits engines (model >= 6) than on Rings. The static params.json name is the Rings label. */
    {
        size_t klen = strlen(key);
        if (klen > 5 && !strcmp(key + klen - 5, "_name")) {
            int plaits = s->model >= 6;
            if (!strcmp(key, "structure_name"))  return snprintf(buf, buf_len, "%s", plaits ? "HARMONICS" : "STRUCTURE");
            if (!strcmp(key, "brightness_name")) return snprintf(buf, buf_len, "%s", plaits ? "TIMBRE"    : "BRIGHTNESS");
            if (!strcmp(key, "damping_name"))    return snprintf(buf, buf_len, "%s", plaits ? "MORPH"     : "DAMPING");
            if (!strcmp(key, "position_name"))   return snprintf(buf, buf_len, "%s", plaits ? "LPG COLOR" : "POSITION");
            if (!strcmp(key, "lpgDecay_name"))   return snprintf(buf, buf_len, "%s", plaits ? "LPG DECAY" : " ");
        }
    }
    /* dynamic_display (wrapper asks "<key>_display"): delayTime shows the tempo-sync division (composed by the
     * plain "delayTime" case above); every other dynamic_display param is a 0..1 knob shown as a 0..100 percent. */
    {
        size_t klen = strlen(key);
        if (klen > 8 && !strcmp(key + klen - 8, "_display")) {
            char base[64];
            int bl = (int)(klen - 8);
            if (bl >= (int)sizeof base) bl = (int)sizeof base - 1;
            memcpy(base, key, bl);
            base[bl] = 0;
            if (!strcmp(base, "delayTime")) return get_param(inst, "delayTime", buf, buf_len);
            char vb[32];
            if (get_param(inst, base, vb, sizeof vb) > 0)
                return snprintf(buf, buf_len, "%.0f", atof(vb) * 100.0);
            return 0;
        }
    }
    return 0;
}

/* Advance ENV3-6 + LFO1-4 by `frames` samples and sum their contributions per modulation target.
 * Returns via modVals[MOD_COUNT] (0 for untouched targets). */
static void compute_mod(state_t *s, int frames, float *modVals) {
    for (int i = 0; i < MOD_COUNT; i++) modVals[i] = 0.f;
    for (int i = 0; i < 6; i++) {
        modenv_t *m = &s->menv[i];
        /* menv[0..1] = ENV3/ENV4 free mod-envelopes (source = envelope level, gated by its own ADSR);
         * menv[2..3] = VELOCITY slots (latched note velocity, held on note-off);
         * menv[4] = MOD WHEEL, menv[5] = AFTERTOUCH (latched CC1 / channel pressure).
         * Same target/amount fields + path, so all stack with the LFO mods. */
        float src;
        if (i < 2) {
            src = adsr_process(&m->env, frames);             /* 0..1 envelope level */
            if (src <= 0.001f) continue;                     /* idle env: leave target at base */
        } else if (i < 4) {
            src = s->noteVel;                                /* latched velocity -- no snap-back */
            if (src <= 0.001f) continue;                     /* nothing played yet: leave target at base */
        } else {
            src = (i == 4) ? s->modWheel : s->aftertouch;    /* latched controller, 0 by default = no offset */
        }
        if (m->target > 0 && m->target < MOD_COUNT && fabsf(m->amount) > 0.001f)
            modVals[m->target] += m->amount * src;
    }
    for (int i = 0; i < 4; i++) {
        lfo_t *l = &s->lfo[i];
        float rate = l->sync ? (s->bpm / (60.f * kDivBeats[l->div])) : l->rate;   /* tempo-sync or free Hz */
        float inc = rate * (float)frames / 44100.0f;
        l->phase += inc;
        if (l->phase >= 1.f) {
            l->phase -= floorf(l->phase);
            if (l->wave == 4) l->shValue = (float)rand() / (float)RAND_MAX * 2.f - 1.f;  /* S&H */
        }
        float raw = lfo_wave_value(l->phase, l->wave, l->shape, l->shValue);
        if (l->wave == 4 && l->shape > 0.01f) {                                          /* S&H glide */
            float smoothHz = rate * (0.01f > (1.f - l->shape * 0.98f) ? 0.01f : (1.f - l->shape * 0.98f));
            float coeff = 1.f - expf(-6.28318530718f * smoothHz * (float)frames / 44100.0f);
            l->shSmoothed += (raw - l->shSmoothed) * clampf(coeff, 0.f, 1.f);
            raw = l->shSmoothed;
        }
        l->level = l->bipolar ? raw : (raw + 1.f) * 0.5f;
        if (l->target > 0 && l->target < MOD_COUNT && fabsf(l->amount) > 0.001f)
            modVals[l->target] += l->amount * l->level * (l->bipolar ? 0.5f : 1.0f);
    }
}

/* Clean below the knee (-2.5 dBFS), then a tanh knee up to full scale. A global tanh would colour even a
 * single voice; the knee keeps quiet material bit-exact and only rounds the peaks that polyphony stacks up.
 * Replaces the old hard clip so summed voices / FX overshoot saturate softly instead of tearing. */
static inline float soft_clip(float x) {
    const float knee = 0.75f;
    float a = fabsf(x);
    if (a <= knee) return x;
    return copysignf(knee + (1.0f - knee) * tanhf((a - knee) / (1.0f - knee)), x);
}

static void render(void *inst, int16_t *out_lr, int frames) {
    state_t *s = (state_t *)inst;
    int plaits = model_is_plaits(s->model);

    /* ── Modulation: advance sources once per block, apply offsets over the base params ── */
    float modVals[MOD_COUNT];
    compute_mod(s, frames, modVals);

    /* engine knobs (structure/brightness/damping/position): push base+mod, restore base when mod stops */
    const int engTgt[4] = { MOD_STRUCTURE, MOD_BRIGHTNESS, MOD_DAMPING, MOD_POSITION };
    const float engBase[4] = { s->structure, s->brightness, s->damping, s->position };
    for (int k = 0; k < 4; k++) {
        int t = engTgt[k];
        if (fabsf(modVals[t]) > 0.001f) {
            float val = clampf(engBase[k] + modVals[t], 0.f, 1.f);
            if (plaits) plaits_set_param(s->plaits, k, val); else rings_set_param(s->rings, k, val);
            s->modActive[t] = 1;
        } else if (s->modActive[t]) {
            if (plaits) plaits_set_param(s->plaits, k, engBase[k]); else rings_set_param(s->rings, k, engBase[k]);
            s->modActive[t] = 0;
        }
    }

    /* filter resonance/morph/cutoff modulation (cutoff handled per-chunk below for the Rings path) */
    float fres  = clampf(s->filterResonance + modVals[MOD_RESONANCE], 0.f, 1.f);
    float fmorph = 0.f;   /* morph killed: filter stays a clean resonant LP (MOD_MORPH is a dead target) */
    float cutoffMod = modVals[MOD_CUTOFF];
    int filterModNow = (fabsf(modVals[MOD_RESONANCE]) > 0.001f ||
                        fabsf(modVals[MOD_MORPH])     > 0.001f ||
                        fabsf(cutoffMod)              > 0.001f);
    if (filterModNow) {
        if (plaits) {
            float fc = s->filterCutoff;
            if (cutoffMod > 0.f) fc *= powf(20000.f / (fc > 1.f ? fc : 1.f),  cutoffMod);
            else if (cutoffMod < 0.f) fc *= powf(20.f / (fc > 1.f ? fc : 1.f), -cutoffMod);
            plaits_set_filter_params(s->plaits, clampf(fc, 20.f, 20000.f), s->filterEnvAmount, fres, fmorph,
                                     0.f, s->env2Attack, s->env2Decay, s->env2Sustain, s->env2Release, s->filterSlope);
        } else {
            filter_set_resonance(s->filter, fres);
            filter_set_morph(s->filter, fmorph);
        }
        s->filterModWas = 1;
    } else if (s->filterModWas) {
        push_filter(s);                 /* restore un-modulated filter params */
        s->filterModWas = 0;
    }

    /* reverb / delay modulation (base+mod when active, restore base when it stops) */
    if (fabsf(modVals[MOD_REVDECAY]) > 0.001f) { reverb_set_decay(s->reverb, clampf(s->reverbDecay + modVals[MOD_REVDECAY], 0.f, 1.f)); s->modActive[MOD_REVDECAY] = 1; }
    else if (s->modActive[MOD_REVDECAY]) { reverb_set_decay(s->reverb, s->reverbDecay); s->modActive[MOD_REVDECAY] = 0; }
    if (fabsf(modVals[MOD_REVWET]) > 0.001f) { reverb_set_wet(s->reverb, clampf(s->reverbWet + modVals[MOD_REVWET], 0.f, 1.f)); s->modActive[MOD_REVWET] = 1; }
    else if (s->modActive[MOD_REVWET]) { reverb_set_wet(s->reverb, s->reverbWet); s->modActive[MOD_REVWET] = 0; }
    /* Delay time: tempo-sync overrides modulation overrides the base time; only re-set on change. */
    float dlyTarget;
    if (s->delaySync) dlyTarget = clampf(kDelayDivBeats[s->delayDiv] * 60000.f / (s->bpm > 1.f ? s->bpm : 120.f), 10.f, 2000.f);
    else if (fabsf(modVals[MOD_DLYTIME]) > 0.001f) dlyTarget = clampf(s->delayTime + modVals[MOD_DLYTIME] * 2000.f, 10.f, 2000.f);
    else dlyTarget = s->delayTime;
    if (fabsf(dlyTarget - s->delayTimeCur) > 0.01f) { delay_set_time_ms(s->delay, dlyTarget); s->delayTimeCur = dlyTarget; }
    if (fabsf(modVals[MOD_DLYFBK]) > 0.001f) { delay_set_feedback(s->delay, delay_fb_curve(clampf(s->delayFeedback + modVals[MOD_DLYFBK], 0.f, 1.f))); s->modActive[MOD_DLYFBK] = 1; }
    else if (s->modActive[MOD_DLYFBK]) { delay_set_feedback(s->delay, delay_fb_curve(s->delayFeedback)); s->modActive[MOD_DLYFBK] = 0; }

    /* ── Render the active engine in <=128-frame chunks, run the FX chain ── */
    float L[128], R[128];
    float mix[256], rvb[256], snd[256], dly[256];   /* interleaved stereo, c <= 128 -> 256 floats */
    int done = 0;
    while (done < frames) {
        int c = frames - done;
        if (c > 128) c = 128;
        int n2 = c * 2;
        /* both rings_render() and plaits_render() accumulate (out += ...) and early-return on silence,
         * so start from zero each block */
        memset(L, 0, c * sizeof(float));
        memset(R, 0, c * sizeof(float));
        /* advance ENV2 for this chunk; drives the Rings-path filter env (Plaits runs its own per-voice) */
        float e2 = adsr_process(&s->env2, c);
        if (plaits) plaits_render(s->plaits, L, R, c);   /* Plaits filters per-voice internally */
        else        rings_render(s->rings, L, R, c);
        for (int i = 0; i < c; i++) { mix[i * 2] = L[i]; mix[i * 2 + 1] = R[i]; }

        /* Rings has no internal filter -> apply ENV2 + cutoff mod to the shared cutoff, then filter. */
        if (!plaits) {
            float fc = s->filterCutoff;
            if (s->filterEnvAmount > 0.001f && e2 > 0.001f) {
                float top = 20000.0f / (fc > 1.0f ? fc : 1.0f);
                fc *= powf(top, s->filterEnvAmount * e2);
            }
            if (cutoffMod > 0.f)      fc *= powf(20000.f / (fc > 1.f ? fc : 1.f),  cutoffMod);
            else if (cutoffMod < 0.f) fc *= powf(20.f    / (fc > 1.f ? fc : 1.f), -cutoffMod);
            if (fc < 20.0f) fc = 20.0f; else if (fc > 20000.0f) fc = 20000.0f;
            float tau = (fc >= s->cutoffSmooth_) ? 0.003f : 0.030f;
            float kS  = expf(-(float)c / (tau * 44100.0f));
            s->cutoffSmooth_ = kS * s->cutoffSmooth_ + (1.0f - kS) * fc;
            filter_set_cutoff(s->filter, s->cutoffSmooth_);
            filter_process(s->filter, mix, mix, c);
        }

        /* Reverb send: dry=0 so reverb_process yields wet only, added back onto the dry mix */
        for (int i = 0; i < n2; i++) rvb[i] = mix[i];
        reverb_process(s->reverb, rvb, rvb, c);
        for (int i = 0; i < n2; i++) mix[i] += rvb[i];

        /* Delay send: input scaled by wet, delay output added back */
        for (int i = 0; i < n2; i++) snd[i] = mix[i] * s->delayWet;
        delay_process(s->delay, snd, dly, c);
        for (int i = 0; i < n2; i++) mix[i] += dly[i];

        /* Chorus (internal wet) then Saturation, both in place */
        chorus_process(s->chorus, mix, mix, c);
        sat_process(s->sat, mix, c);

        for (int i = 0; i < c; i++) {
            float l = soft_clip(mix[i * 2]);
            float r = soft_clip(mix[i * 2 + 1]);
            out_lr[(done + i) * 2]     = (int16_t)lrintf(l * 32767.0f);
            out_lr[(done + i) * 2 + 1] = (int16_t)lrintf(r * 32767.0f);
        }
        done += c;
    }
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render };
extern "C" const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
