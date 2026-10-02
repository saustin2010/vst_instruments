// Schwung Clouds — Mutable Instruments Clouds granular processor as audio FX
// MIT License (Emilie Gillet DSP, community port)
//
// Wraps Clouds GranularProcessor into Schwung audio_fx_api_v2 interface.
// Audio: 44100 Hz, 128 frames/block, stereo interleaved int16.

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"

// ---- Random state (global, shared across instances) ----
namespace stmlib { uint32_t Random::rng_state_ = 0x12345678; }

// ---- Schwung API headers (inline, must match chain_host ABI exactly) ----
extern "C" {

typedef int (*move_mod_emit_value_fn)(void *ctx,
                                      const char *source_id,
                                      const char *target,
                                      const char *param,
                                      float signal,
                                      float depth,
                                      float offset,
                                      int bipolar,
                                      int enabled);
typedef void (*move_mod_clear_source_fn)(void *ctx, const char *source_id);

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
    move_mod_emit_value_fn mod_emit_value;
    move_mod_clear_source_fn mod_clear_source;
    void *mod_host_ctx;
} host_api_v1_t;

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *config_json);
    void  (*destroy_instance)(void *instance);
    void  (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
} audio_fx_api_v2_t;

}  // extern "C"

// ---- Buffer sizes scaled for 44.1k (original was 32k) ----
static const size_t kLargeBufferSize = 163840;  // ~3.7s at 44.1k stereo 16-bit
static const size_t kSmallBufferSize = 90112;

// ---- Instance state ----
struct CloudsInstance {
    clouds::GranularProcessor processor;

    uint8_t large_buffer[kLargeBufferSize];
    uint8_t small_buffer[kSmallBufferSize];

    // Parameters (knob-mapped)
    float position;     // 0-1
    float size;         // 0-1
    int   pitch;        // -24 to +24 semitones
    float density;      // 0-1
    float texture;      // 0-1
    float feedback;     // 0-1
    float reverb;       // 0-1
    float dry_wet;      // 0-1

    // Jog menu params
    int   mode;           // 0-3 (Granular/Stretch/Looper/Spectral)
    bool  freeze;         // toggle
    int   quality;        // 0=16-bit, 1=Lo-Fi
    float stereo_spread;  // 0-1

    // Filter params (one-pole 6dB/oct, Mutable Instruments ONE_POLE style)
    float filter_hp;      // HPF cutoff: 0 Hz to 1000 Hz (0 = off)
    float filter_lp;      // LPF cutoff: 20000 Hz to 1000 Hz (20000 = off)
    float hp_state_l;     // HPF state (left)
    float hp_state_r;     // HPF state (right)
    float lp_state_l;     // LPF state (left)
    float lp_state_r;     // LPF state (right)

    // Low-shelf EQ params (before HPF/LPF)
    float low_boost;      // 0 to 6 dB (default 0 = off)
    float low_freq;       // 30 to 400 Hz (default 100)
    float low_q;          // 0.1 to 4.0 (default 0.7)
    // Biquad state (Direct Form II Transposed)
    float ls_z1_l, ls_z2_l;  // left channel state
    float ls_z1_r, ls_z2_r;  // right channel state
    // Cached biquad coefficients
    float ls_b0, ls_b1, ls_b2, ls_a1, ls_a2;
    float ls_last_boost, ls_last_freq, ls_last_q;  // for recalc detection

    // Limiter params
    bool  limiter_on;     // toggle (default OFF)
    float limiter_pre;    // -6 to +6 dB (default 0)
    float limiter_post;   // -6 to +6 dB (default 0)

    // Smoothed parameter state
    float texture_smooth;  // one-pole smoothed texture (100ms)

    // Page tracking (0=Verglas, 1=Filters)
    int current_page;
};

static const host_api_v1_t *g_host = NULL;

// ---- Knob mapping (1-indexed knobs → parameter keys) ----
static const char *MODE_NAMES[] = {
    "Granular", "Stretch", "Looper", "Spectral"
};
static const char *QUALITY_NAMES[] = { "16-bit", "Lo-Fi" };

struct KnobDef {
    const char *key;
    const char *label;
    float min, max, step;
    bool is_int;
};

static const KnobDef KNOB_MAP[8] = {
    { "position", "Position", 0,   1,  0.01f, false },
    { "size",     "Size",     0,   1,  0.01f, false },
    { "pitch",    "Pitch",    -24, 24, 1,     true  },
    { "density",  "Density",  0,   1,  0.01f, false },
    { "texture",  "Texture",  0,   1,  0.01f, false },
    { "feedback", "Feedback", 0,   1,  0.01f, false },
    { "reverb",   "Reverb",   0,   1,  0.01f, false },
    { "dry_wet",  "Mix",      0,   1,  0.01f, false },
};

static float* clouds_param_ptr(CloudsInstance *inst, int knob_idx) {
    switch (knob_idx) {
        case 0: return &inst->position;
        case 1: return &inst->size;
        case 3: return &inst->density;
        case 4: return &inst->texture;
        case 5: return &inst->feedback;
        case 6: return &inst->reverb;
        case 7: return &inst->dry_wet;
        default: return NULL;
    }
}

static int* clouds_int_param_ptr(CloudsInstance *inst, int knob_idx) {
    switch (knob_idx) {
        case 2: return &inst->pitch;
        default: return NULL;
    }
}

// ---- Helpers ----
static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline int clampi(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// dB to linear gain: 10^(dB/20)
static inline float db_to_lin(float db) {
    return powf(10.0f, db * 0.05f);
}

// Tape-style soft clip limiter
// Combines Airwindows IronOxide sin(min(|x|,pi/2)) waveshaper with
// Chowdhury DSP cubic soft clip (x - x^3/3) for a smooth tape saturation knee.
// Stateless, no allocation, suitable for render path.
static inline float tape_soft_clip(float x) {
    // Stage 1: Chowdhury-style cubic soft clip — gentle knee, unity slope at origin
    // f(x) = 1.5 * (x - x^3/3) for |x| <= 1, clamped to +/-1 outside
    // (degree-3 polynomial from chowdsp_SoftClipper.h, normalized)
    float ax = fabsf(x);
    if (ax > 1.5f) {
        x = (x > 0.0f) ? 1.0f : -1.0f;
    } else {
        float norm = x * 0.6666667f;  // x * 2/3 (normFactor for degree 3)
        float cn = clampf(norm, -1.0f, 1.0f);
        x = (cn - (cn * cn * cn) * 0.3333333f) * 1.5f;  // * invNormFactor (3/2)
    }

    // Stage 2: Airwindows IronOxide sine waveshaper — tape saturation character
    // sin(min(|x|, pi/2)) preserves sign, soft-clips to exactly +/-1
    float bridge = fabsf(x);
    if (bridge > 1.5707963f) bridge = 1.5707963f;  // pi/2
    bridge = sinf(bridge);
    return (x > 0.0f) ? bridge : -bridge;
}

// Low-shelf biquad coefficient update (Audio EQ Cookbook)
static void update_low_shelf_coeffs(CloudsInstance *inst) {
    if (inst->low_boost == inst->ls_last_boost &&
        inst->low_freq  == inst->ls_last_freq  &&
        inst->low_q     == inst->ls_last_q) return;

    inst->ls_last_boost = inst->low_boost;
    inst->ls_last_freq  = inst->low_freq;
    inst->ls_last_q     = inst->low_q;

    float A = powf(10.0f, inst->low_boost * 0.025f);  // 10^(dB/40)
    float w0 = 6.2831853f * inst->low_freq / 44100.0f;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * inst->low_q);
    float sqrtA2alpha = 2.0f * sqrtf(A) * alpha;

    float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + sqrtA2alpha;
    float inv_a0 = 1.0f / a0;
    inst->ls_b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + sqrtA2alpha) * inv_a0;
    inst->ls_b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)       * inv_a0;
    inst->ls_b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - sqrtA2alpha) * inv_a0;
    inst->ls_a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)           * inv_a0;
    inst->ls_a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - sqrtA2alpha)     * inv_a0;
}

// ---- API callbacks ----
static void* clouds_create(const char *module_dir, const char *config_json) {
    CloudsInstance *inst = (CloudsInstance*)calloc(1, sizeof(CloudsInstance));
    if (!inst) return NULL;

    // Init DSP
    inst->processor.Init(
        inst->large_buffer, kLargeBufferSize,
        inst->small_buffer, kSmallBufferSize);

    // Defaults
    inst->position = 0.5f;
    inst->size = 0.5f;
    inst->pitch = 0;
    inst->density = 1.0f;
    inst->texture = 0.5f;
    inst->texture_smooth = 0.5f;
    inst->feedback = 0.0f;
    inst->reverb = 0.0f;
    inst->dry_wet = 0.5f;

    inst->mode = 0;
    inst->freeze = false;
    inst->quality = 0;
    inst->stereo_spread = 1.0f;

    inst->filter_hp = 0.0f;       // HPF off (0 Hz)
    inst->filter_lp = 20000.0f;   // LPF off (20 kHz)
    inst->hp_state_l = inst->hp_state_r = 0.0f;
    inst->lp_state_l = inst->lp_state_r = 0.0f;

    inst->low_boost = 0.0f;       // Low-shelf off (0 dB)
    inst->low_freq = 100.0f;      // 100 Hz
    inst->low_q = 0.7f;           // Butterworth-ish
    inst->ls_z1_l = inst->ls_z2_l = 0.0f;
    inst->ls_z1_r = inst->ls_z2_r = 0.0f;
    inst->ls_last_boost = -999.0f; // force coefficient calc on first block

    inst->limiter_on = false;
    inst->limiter_pre = 0.0f;
    inst->limiter_post = 0.0f;

    inst->processor.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
    inst->processor.set_quality(inst->quality * 2);  // 0=stereo 16-bit, 2=stereo lo-fi

    if (g_host && g_host->log) g_host->log("[verglas] instance created");
    return inst;
}

static void clouds_destroy(void *instance) {
    CloudsInstance *inst = (CloudsInstance*)instance;
    if (inst) {
        free(inst);
    }
}

static void clouds_process(void *instance, int16_t *audio_inout, int frames) {
    CloudsInstance *inst = (CloudsInstance*)instance;
    if (!inst || frames <= 0) return;
    if (frames > 128) frames = 128;

    // Update processor settings
    inst->processor.set_playback_mode(
        static_cast<clouds::PlaybackMode>(clampi(inst->mode, 0, 3)));
    inst->processor.set_quality(inst->quality * 2);  // 0=stereo 16-bit, 2=stereo lo-fi
    inst->processor.set_freeze(inst->freeze);

    // Fill parameters
    clouds::Parameters *p = inst->processor.mutable_parameters();
    p->position = clampf(inst->position, 0.0f, 1.0f);
    p->size = clampf(inst->size, 0.0f, 1.0f);
    p->pitch = (float)inst->pitch;
    p->density = clampf(inst->density, 0.0f, 1.0f);
    // Smooth texture (one-pole, 100ms time constant at block rate)
    // coeff = 1 - exp(-1 / (tau * block_rate)) where tau=0.1s, block_rate=44100/128
    const float tex_coeff = 0.0286f;
    inst->texture_smooth += tex_coeff * (clampf(inst->texture, 0.0f, 1.0f) - inst->texture_smooth);
    p->texture = inst->texture_smooth;
    p->feedback = clampf(inst->feedback, 0.0f, 1.0f);
    p->reverb = clampf(inst->reverb, 0.0f, 1.0f);
    // Bypass Clouds internal dry/wet — its equal-power LUT attenuates dry by
    // -3dB at mix=0 (lut_xfade_out[0]=0.7071). We do our own mix after Process.
    p->dry_wet = 0.99999f;   // MPC port: 1.0 makes Interpolate(lut_xfade_in, 1.0, 16) read lut[17], one past the table (ASan)
    p->stereo_spread = clampf(inst->stereo_spread, 0.0f, 1.0f);
    p->freeze = inst->freeze;
    p->trigger = false;
    p->gate = false;

    // Prepare (handles mode changes, buffer allocation, vocoder buffering)
    inst->processor.Prepare();

    // Save dry input before Process (which reads from input at the end)
    int16_t dry_buf[256];  // 128 stereo frames = 256 samples
    memcpy(dry_buf, audio_inout, frames * 2 * sizeof(int16_t));

    // Process — ShortFrame has same layout as interleaved int16 stereo pairs
    clouds::ShortFrame* input = reinterpret_cast<clouds::ShortFrame*>(audio_inout);
    clouds::ShortFrame output[128];
    inst->processor.Process(input, output, (size_t)frames);

    // Our own dry/wet crossfade (linear — unity at both extremes)
    // Compensate for Clouds' volume loss at non-zero pitch:
    // grain shrinkage (up), spectral bin spreading (down), crossfade imbalance
    float abs_pitch = fabsf((float)inst->pitch);
    float pitch_comp = 1.0f + abs_pitch * 0.07f;  // ~7% per semitone
    float mix = clampf(inst->dry_wet, 0.0f, 1.0f);
    float dry_gain = 1.0f - mix;
    float wet_gain = mix * pitch_comp;
    for (int i = 0; i < frames * 2; ++i) {
        float d = (float)dry_buf[i];
        float w = (float)((int16_t*)output)[i];
        int32_t s = (int32_t)(d * dry_gain + w * wet_gain);
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        audio_inout[i] = (int16_t)s;
    }

    // Low-shelf EQ (biquad, before HPF/LPF)
    bool do_ls = (inst->low_boost > 0.05f);
    if (do_ls) {
        update_low_shelf_coeffs(inst);
        for (int i = 0; i < frames; ++i) {
            // Left channel — Direct Form II Transposed
            float xl = (float)audio_inout[i * 2] / 32768.0f;
            float yl = inst->ls_b0 * xl + inst->ls_z1_l;
            inst->ls_z1_l = inst->ls_b1 * xl - inst->ls_a1 * yl + inst->ls_z2_l;
            inst->ls_z2_l = inst->ls_b2 * xl - inst->ls_a2 * yl;
            // Right channel
            float xr = (float)audio_inout[i * 2 + 1] / 32768.0f;
            float yr = inst->ls_b0 * xr + inst->ls_z1_r;
            inst->ls_z1_r = inst->ls_b1 * xr - inst->ls_a1 * yr + inst->ls_z2_r;
            inst->ls_z2_r = inst->ls_b2 * xr - inst->ls_a2 * yr;
            // Write back
            int32_t ol = (int32_t)(yl * 32767.0f);
            int32_t or_ = (int32_t)(yr * 32767.0f);
            if (ol > 32767) ol = 32767; if (ol < -32768) ol = -32768;
            if (or_ > 32767) or_ = 32767; if (or_ < -32768) or_ = -32768;
            audio_inout[i * 2]     = (int16_t)ol;
            audio_inout[i * 2 + 1] = (int16_t)or_;
        }
    }

    // One-pole filters + tape limiter (post-Clouds, in float domain)
    // Mutable Instruments ONE_POLE: state += coeff * (input - state)
    // Coefficient: 1 - exp(-2*pi*f/sr)
    bool do_hp = (inst->filter_hp > 1.0f);
    bool do_lp = (inst->filter_lp < 19999.0f);
    bool do_lim = inst->limiter_on;

    if (do_hp || do_lp || do_lim) {
        const float sr = 44100.0f;
        float hp_coeff = do_hp ? (1.0f - expf(-6.2831853f * inst->filter_hp / sr)) : 0.0f;
        float lp_coeff = do_lp ? (1.0f - expf(-6.2831853f * inst->filter_lp / sr)) : 1.0f;
        float pre = do_lim ? db_to_lin(inst->limiter_pre) : 1.0f;
        float post = do_lim ? db_to_lin(inst->limiter_post) : 1.0f;

        for (int i = 0; i < frames; ++i) {
            float l = (float)audio_inout[i * 2]     / 32768.0f;
            float r = (float)audio_inout[i * 2 + 1] / 32768.0f;

            // HPF: state tracks lowpass, output = input - state
            if (do_hp) {
                inst->hp_state_l += hp_coeff * (l - inst->hp_state_l);
                l -= inst->hp_state_l;
                inst->hp_state_r += hp_coeff * (r - inst->hp_state_r);
                r -= inst->hp_state_r;
            }

            // LPF: state tracks lowpass, output = state
            if (do_lp) {
                inst->lp_state_l += lp_coeff * (l - inst->lp_state_l);
                l = inst->lp_state_l;
                inst->lp_state_r += lp_coeff * (r - inst->lp_state_r);
                r = inst->lp_state_r;
            }

            // Tape limiter
            if (do_lim) {
                l *= pre; r *= pre;
                l = tape_soft_clip(l);
                r = tape_soft_clip(r);
                l *= post; r *= post;
            }

            // Convert back to int16 with saturation
            int32_t ol = (int32_t)(l * 32767.0f);
            int32_t or_ = (int32_t)(r * 32767.0f);
            if (ol > 32767) ol = 32767; if (ol < -32768) ol = -32768;
            if (or_ > 32767) or_ = 32767; if (or_ < -32768) or_ = -32768;
            audio_inout[i * 2]     = (int16_t)ol;
            audio_inout[i * 2 + 1] = (int16_t)or_;
        }
    }
}

// ---- State persistence ----
// Schwung saves a module by calling get_param("state") and expects a JSON
// snapshot; it restores AFTER create_instance() via set_param("state", json).
// On a successful JSON.parse the host RE-EMITS the state PRETTY-PRINTED
// ("key": "value" with spaces/newlines after each colon) on the next save, so
// the parser must be key-based and whitespace/newline-tolerant. Every user-
// settable param must be listed here or it silently resets to its default on
// reload. Momentary/navigation keys (_level, knob_*_adjust) are excluded.
static const char *STATE_KEYS[] = {
    "position", "size", "pitch", "density", "texture", "feedback",
    "reverb", "dry_wet", "mode", "freeze", "quality", "stereo_spread",
    "filter_hp", "filter_lp", "limiter_on", "limiter_pre", "limiter_post",
    "low_boost", "low_freq", "low_q"
};
#define STATE_KEY_COUNT ((int)(sizeof(STATE_KEYS)/sizeof(STATE_KEYS[0])))

// Extract the quoted string value for "key". Searches for the fully-quoted key
// ("size" never matches inside "stereo_spread") and tolerates whitespace or
// newlines after the colon, so a pretty-printed round-trip still parses.
// Returns 0 on success (value copied into out), -1 if the key is absent.
static int clouds_json_get_str(const char *json, const char *key,
                               char *out, int out_len) {
    char search[48];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ':' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    int len = (int)(end - p);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, (size_t)len);
    out[len] = '\0';
    return 0;
}

static void clouds_set_param(void *instance, const char *key, const char *val) {
    CloudsInstance *inst = (CloudsInstance*)instance;
    if (!inst || !key || !val) return;

    /* Page tracking — Shadow UI sends _level when user navigates */
    if (strcmp(key, "_level") == 0) {
        if (strcmp(val, "Filters") == 0) inst->current_page = 1;
        else inst->current_page = 0;  /* "Verglas" or root */
        return;
    }

    if (strcmp(key, "position") == 0) {
        inst->position = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "size") == 0) {
        inst->size = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "pitch") == 0) {
        inst->pitch = clampi(atoi(val), -24, 24);
    } else if (strcmp(key, "density") == 0) {
        inst->density = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "texture") == 0) {
        inst->texture = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "feedback") == 0) {
        inst->feedback = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "reverb") == 0) {
        inst->reverb = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "dry_wet") == 0) {
        inst->dry_wet = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "mode") == 0) {
        int found = 0;
        for (int i = 0; i < 4; i++) {
            if (strcmp(val, MODE_NAMES[i]) == 0) { inst->mode = i; found = 1; break; }
        }
        if (!found) inst->mode = clampi(atoi(val), 0, 3);
    } else if (strcmp(key, "freeze") == 0) {
        inst->freeze = (strcmp(val, "On") == 0) ? true : (strcmp(val, "Off") == 0) ? false : (atoi(val) != 0);
    } else if (strcmp(key, "quality") == 0) {
        if (strcmp(val, "Lo-Fi") == 0) inst->quality = 1;
        else if (strcmp(val, "16-bit") == 0) inst->quality = 0;
        else inst->quality = clampi(atoi(val), 0, 1);
    } else if (strcmp(key, "stereo_spread") == 0) {
        inst->stereo_spread = clampf(atof(val), 0.0f, 1.0f);
    } else if (strcmp(key, "low_boost") == 0) {
        inst->low_boost = clampf(atof(val), 0.0f, 6.0f);
    } else if (strcmp(key, "low_freq") == 0) {
        inst->low_freq = clampf(atof(val), 30.0f, 400.0f);
    } else if (strcmp(key, "low_q") == 0) {
        inst->low_q = clampf(atof(val), 0.1f, 4.0f);
    } else if (strcmp(key, "filter_hp") == 0) {
        inst->filter_hp = clampf(atof(val), 0.0f, 1000.0f);
    } else if (strcmp(key, "filter_lp") == 0) {
        inst->filter_lp = clampf(atof(val), 1000.0f, 20000.0f);
    } else if (strcmp(key, "limiter_on") == 0) {
        inst->limiter_on = (strcmp(val, "On") == 0) ? true : (strcmp(val, "Off") == 0) ? false : (atoi(val) != 0);
    } else if (strcmp(key, "limiter_pre") == 0) {
        inst->limiter_pre = clampf(atof(val), -6.0f, 6.0f);
    } else if (strcmp(key, "limiter_post") == 0) {
        inst->limiter_post = clampf(atof(val), -6.0f, 6.0f);
    } else if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_adjust")) {
        int knob_num = atoi(key + 5);  // 1-indexed
        int delta = atoi(val);
        if (inst->current_page == 1) {
            /* Filters page */
            switch (knob_num) {
                case 1: inst->filter_hp = clampf(inst->filter_hp + delta * 10.0f, 0.0f, 1000.0f); break;
                case 2: inst->filter_lp = clampf(inst->filter_lp + delta * 10.0f, 1000.0f, 20000.0f); break;
                case 3: inst->limiter_on = (delta != 0) ? !inst->limiter_on : inst->limiter_on; break;
                case 4: inst->limiter_pre = clampf(inst->limiter_pre + delta * 0.5f, -6.0f, 6.0f); break;
                case 5: inst->limiter_post = clampf(inst->limiter_post + delta * 0.5f, -6.0f, 6.0f); break;
                case 6: inst->low_boost = clampf(inst->low_boost + delta * 0.5f, 0.0f, 6.0f); break;
                case 7: inst->low_freq = clampf(inst->low_freq + delta * 10.0f, 30.0f, 400.0f); break;
                case 8: inst->low_q = clampf(inst->low_q + delta * 0.1f, 0.1f, 4.0f); break;
            }
        } else {
            /* Verglas page */
            int idx = knob_num - 1;
            if (idx >= 0 && idx < 8) {
                const KnobDef &k = KNOB_MAP[idx];
                if (k.is_int) {
                    int *p = clouds_int_param_ptr(inst, idx);
                    if (p) *p = clampi(*p + delta * (int)k.step, (int)k.min, (int)k.max);
                } else {
                    float *p = clouds_param_ptr(inst, idx);
                    if (p) *p = clampf(*p + delta * k.step, k.min, k.max);
                }
            }
        }
    } else if (strcmp(key, "state") == 0) {
        // Key-based JSON restore: hand each key to the ordinary per-key handler
        // above so clamping + enum/toggle matching are reused. Tolerates
        // whitespace, newlines, reordering, and missing keys — a key that is
        // absent simply leaves that param at its current value (never aborts
        // the whole parse, unlike the old positional sscanf).
        char vb[32];
        for (int i = 0; i < STATE_KEY_COUNT; i++) {
            if (clouds_json_get_str(val, STATE_KEYS[i], vb, sizeof(vb)) == 0)
                clouds_set_param(instance, STATE_KEYS[i], vb);
        }
    }
}

static int clouds_get_param(void *instance, const char *key, char *buf, int buf_len) {
    CloudsInstance *inst = (CloudsInstance*)instance;
    if (!inst || !key || !buf || buf_len < 1) return 0;

    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Verglas");
    } else if (strcmp(key, "ui_hierarchy") == 0) {
        static const char *hier =
        "{\"modes\":null,"
        "\"levels\":{"
          "\"root\":{\"name\":\"Verglas\","
            "\"knobs\":[\"position\",\"size\",\"pitch\",\"density\",\"texture\",\"feedback\",\"reverb\",\"dry_wet\"],"
            "\"params\":["
              "{\"level\":\"Verglas\",\"label\":\"Verglas\"},"
              "{\"level\":\"Filters\",\"label\":\"Filters\"}"
            "]},"
          "\"Verglas\":{\"label\":\"Verglas\","
            "\"knobs\":[\"position\",\"size\",\"pitch\",\"density\",\"texture\",\"feedback\",\"reverb\",\"dry_wet\"],"
            "\"params\":[\"position\",\"size\",\"pitch\",\"density\",\"texture\",\"feedback\",\"reverb\",\"dry_wet\","
              "\"mode\",\"freeze\",\"quality\",\"stereo_spread\"]},"
          "\"Filters\":{\"label\":\"Filters\","
            "\"knobs\":[\"filter_hp\",\"filter_lp\",\"limiter_on\",\"limiter_pre\",\"limiter_post\",\"low_boost\",\"low_freq\",\"low_q\"],"
            "\"params\":[\"filter_hp\",\"filter_lp\",\"limiter_on\",\"limiter_pre\",\"limiter_post\",\"low_boost\",\"low_freq\",\"low_q\"]}"
        "}}";
        int len = (int)strlen(hier);
        if (len >= buf_len) return -1;
        memcpy(buf, hier, len + 1);
        return len;
    } else if (strcmp(key, "position") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->position);
    } else if (strcmp(key, "size") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->size);
    } else if (strcmp(key, "pitch") == 0) {
        return snprintf(buf, buf_len, "%d", inst->pitch);
    } else if (strcmp(key, "density") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->density);
    } else if (strcmp(key, "texture") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->texture);
    } else if (strcmp(key, "feedback") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->feedback);
    } else if (strcmp(key, "reverb") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->reverb);
    } else if (strcmp(key, "dry_wet") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->dry_wet);
    } else if (strcmp(key, "mode") == 0) {
        return snprintf(buf, buf_len, "%s", MODE_NAMES[clampi(inst->mode, 0, 3)]);
    } else if (strcmp(key, "freeze") == 0) {
        return snprintf(buf, buf_len, "%s", inst->freeze ? "On" : "Off");
    } else if (strcmp(key, "quality") == 0) {
        return snprintf(buf, buf_len, "%s", QUALITY_NAMES[clampi(inst->quality, 0, 1)]);
    } else if (strcmp(key, "stereo_spread") == 0) {
        return snprintf(buf, buf_len, "%.4f", inst->stereo_spread);
    } else if (strcmp(key, "low_boost") == 0) {
        return snprintf(buf, buf_len, "%.1f", inst->low_boost);
    } else if (strcmp(key, "low_freq") == 0) {
        return snprintf(buf, buf_len, "%d", (int)inst->low_freq);
    } else if (strcmp(key, "low_q") == 0) {
        return snprintf(buf, buf_len, "%.1f", inst->low_q);
    } else if (strcmp(key, "filter_hp") == 0) {
        return snprintf(buf, buf_len, "%d", (int)inst->filter_hp);
    } else if (strcmp(key, "filter_lp") == 0) {
        return snprintf(buf, buf_len, "%d", (int)inst->filter_lp);
    } else if (strcmp(key, "limiter_on") == 0) {
        return snprintf(buf, buf_len, "%s", inst->limiter_on ? "On" : "Off");
    } else if (strcmp(key, "limiter_pre") == 0) {
        return snprintf(buf, buf_len, "%.4f", (double)inst->limiter_pre);
    } else if (strcmp(key, "limiter_post") == 0) {
        return snprintf(buf, buf_len, "%.4f", (double)inst->limiter_post);
    } else if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_name")) {
        int knob_num = atoi(key + 5);
        if (inst->current_page == 1) {
            switch (knob_num) {
                case 1: return snprintf(buf, buf_len, "HPF");
                case 2: return snprintf(buf, buf_len, "LPF");
                case 3: return snprintf(buf, buf_len, "Limiter");
                case 4: return snprintf(buf, buf_len, "Pre Gain");
                case 5: return snprintf(buf, buf_len, "Post Gain");
                case 6: return snprintf(buf, buf_len, "Low Boost");
                case 7: return snprintf(buf, buf_len, "Low Freq");
                case 8: return snprintf(buf, buf_len, "Low Q");
            }
        } else {
            int idx = knob_num - 1;
            if (idx >= 0 && idx < 8)
                return snprintf(buf, buf_len, "%s", KNOB_MAP[idx].label);
        }
        return 0;
    } else if (strncmp(key, "knob_", 5) == 0 && strstr(key, "_value")) {
        int knob_num = atoi(key + 5);
        if (inst->current_page == 1) {
            switch (knob_num) {
                case 1: return snprintf(buf, buf_len, "%.0f Hz", inst->filter_hp);
                case 2: return snprintf(buf, buf_len, "%.0f Hz", inst->filter_lp);
                case 3: return snprintf(buf, buf_len, "%s", inst->limiter_on ? "On" : "Off");
                case 4: return snprintf(buf, buf_len, "%+.1f dB", inst->limiter_pre);
                case 5: return snprintf(buf, buf_len, "%+.1f dB", inst->limiter_post);
                case 6: return snprintf(buf, buf_len, "+%.1f dB", inst->low_boost);
                case 7: return snprintf(buf, buf_len, "%d Hz", (int)inst->low_freq);
                case 8: return snprintf(buf, buf_len, "%.1f", inst->low_q);
            }
        } else {
            int idx = knob_num - 1;
            if (idx < 0 || idx >= 8) return 0;
            switch (idx) {
                case 0: return snprintf(buf, buf_len, "%d%%", (int)(inst->position * 100.0f));
                case 1: return snprintf(buf, buf_len, "%d%%", (int)(inst->size * 100.0f));
                case 2: return snprintf(buf, buf_len, "%+d st", inst->pitch);
                case 3: return snprintf(buf, buf_len, "%d%%", (int)(inst->density * 100.0f));
                case 4: return snprintf(buf, buf_len, "%d%%", (int)(inst->texture * 100.0f));
                case 5: return snprintf(buf, buf_len, "%d%%", (int)(inst->feedback * 100.0f));
                case 6: return snprintf(buf, buf_len, "%d%%", (int)(inst->reverb * 100.0f));
                case 7: return snprintf(buf, buf_len, "%d%%", (int)(inst->dry_wet * 100.0f));
            }
        }
        return 0;
    } else if (strcmp(key, "chain_params") == 0) {
        static const char *cp =
        "["
        "{\"key\":\"position\",\"name\":\"Position\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"size\",\"name\":\"Size\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"pitch\",\"name\":\"Pitch\",\"type\":\"int\",\"min\":-24,\"max\":24,\"step\":1},"
        "{\"key\":\"density\",\"name\":\"Density\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"texture\",\"name\":\"Texture\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"feedback\",\"name\":\"Feedback\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"reverb\",\"name\":\"Reverb\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"dry_wet\",\"name\":\"Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"mode\",\"name\":\"Mode\",\"type\":\"enum\",\"options\":[\"Granular\",\"Stretch\",\"Looper\",\"Spectral\"]},"
        "{\"key\":\"freeze\",\"name\":\"Freeze\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
        "{\"key\":\"quality\",\"name\":\"Quality\",\"type\":\"enum\",\"options\":[\"16-bit\",\"Lo-Fi\"]},"
        "{\"key\":\"stereo_spread\",\"name\":\"Spread\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
        "{\"key\":\"filter_hp\",\"name\":\"HPF\",\"type\":\"int\",\"min\":0,\"max\":1000,\"step\":10},"
        "{\"key\":\"filter_lp\",\"name\":\"LPF\",\"type\":\"int\",\"min\":1000,\"max\":20000,\"step\":10},"
        "{\"key\":\"limiter_on\",\"name\":\"Limiter\",\"type\":\"enum\",\"options\":[\"Off\",\"On\"]},"
        "{\"key\":\"limiter_pre\",\"name\":\"Pre Gain\",\"type\":\"float\",\"min\":-6,\"max\":6,\"step\":0.5},"
        "{\"key\":\"limiter_post\",\"name\":\"Post Gain\",\"type\":\"float\",\"min\":-6,\"max\":6,\"step\":0.5},"
        "{\"key\":\"low_boost\",\"name\":\"Low Boost\",\"type\":\"float\",\"min\":0,\"max\":6,\"step\":0.5},"
        "{\"key\":\"low_freq\",\"name\":\"Low Freq\",\"type\":\"int\",\"min\":30,\"max\":400,\"step\":10},"
        "{\"key\":\"low_q\",\"name\":\"Low Q\",\"type\":\"float\",\"min\":0.1,\"max\":4.0,\"step\":0.1}"
        "]";
        int len = (int)strlen(cp);
        if (len >= buf_len) return -1;
        memcpy(buf, cp, len + 1);
        return len;
    } else if (strcmp(key, "state") == 0) {
        // Full snapshot for Schwung autosave / set reload. Every value is
        // emitted as a QUOTED string (raw float text, integer, or the enum
        // option name) using the same per-key getters that a live read uses,
        // so it survives any pretty-printed re-serialization unchanged and
        // feeds straight back into set_param via the state branch above.
        if (buf_len < 2) return -1;
        int n = 0;
        buf[n++] = '{';
        for (int i = 0; i < STATE_KEY_COUNT; i++) {
            char vb[32];
            if (clouds_get_param(instance, STATE_KEYS[i], vb, sizeof(vb)) < 0)
                continue;
            int w = snprintf(buf + n, (size_t)(buf_len - n), "%s\"%s\":\"%s\"",
                             (n > 1) ? "," : "", STATE_KEYS[i], vb);
            if (w < 0 || w >= buf_len - n) return -1;   // would overflow
            n += w;
        }
        if (n + 1 >= buf_len) return -1;
        buf[n++] = '}';
        buf[n] = '\0';
        return n;
    }
    return -1;
}

// ---- Static API struct ----
static audio_fx_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = clouds_create,
    .destroy_instance = clouds_destroy,
    .process_block = clouds_process,
    .set_param = clouds_set_param,
    .get_param = clouds_get_param,
    .on_midi = NULL,
};

// ---- Entry point (dlsym) ----
extern "C" {

__attribute__((visibility("default")))
audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    if (host && host->log) host->log("[verglas] Verglas (Clouds) v1.2.3 loaded");
    return &g_api;
}

}  // extern "C"
