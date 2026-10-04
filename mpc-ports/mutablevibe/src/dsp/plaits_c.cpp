// plaits_c.cpp — Plaits polyphonic voice wrapper for MutablePlugin VST
//
// Based on synth_c.cpp (Groovebox), adapted for VST:
//   - Configurable sample rate (pitch correction computed from sr)
//   - Pitch bend, mod wheel, aftertouch
//   - Engine index mapping: UI index 0-10 -> actual Plaits engine index
//
// Amplitude: ADSR envelope applied as VCA on top of Plaits' LPG.
// patch.decay = 1.0 (LPG open) so our ADSR controls the amplitude fully.

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <climits>
#include <new>

#include "plaits/dsp/voice.h"

using namespace plaits;
using namespace stmlib;

// ── Filter C API (from filter_c.c) ────────────────────────────────────────────
extern "C" {
    void* filter_create(float samplerate);
    void  filter_free(void* ptr);
    void  filter_set_cutoff(void* ptr, float hz);
    void  filter_set_resonance(void* ptr, float r);
    void  filter_set_morph(void* ptr, float m);
    void  filter_set_slope(void* ptr, int slope);
    void  filter_process(void* ptr, float* in, float* out, int frames);
    void  filter_flush(void* ptr);
    void  filter_prime(void* ptr, float hz, float res, float morph);
    void  filter_set_fc_smooth(void* ptr, float hz);
}

// ── Constants ──────────────────────────────────────────────────────────────────

static const int kVoices      = 4;
static const int kPoolSize    = 32768;  // 32 KB per Plaits voice
static const int kDeclickLen  = 64;     // ~1.5 ms @ 44.1 kHz — fade-in on voice steal

// Maps UI index 0-15 to Plaits voice.cc engine index
// engine2 (new):  0=VA VCF(0) 1=PhaseDst(1) 2=WaveTrn(5) 3=StrMach(6) 4=Chiptune(7)
// classic:        5=VA(8) 6=Waveshp(9) 7=FM(10) 8=Grain(11) 9=Addit(12)
//                 10=Wavetbl(13) 11=Chord(14) 12=Swarm(16) 13=Noise(17)
//                 14=String(19) 15=Modal(20)
// (SixOp/DX=2-4, Speech=15, Particle=18, Drums=21-23 intentionally omitted)
static const int kEngineMap[16] = { 0, 1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20 };

// ── ADSR ───────────────────────────────────────────────────────────────────────

enum AdsrState : uint8_t { ADSR_IDLE=0, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE };

// ── Per-voice slot ─────────────────────────────────────────────────────────────

struct PlaitsVoice {
    plaits::Voice   voice;
    uint8_t         pool[kPoolSize];
    BufferAllocator alloc;
    Patch           patch;
    Modulations     mods;
    Voice::Frame    frames[kMaxBlockSize];

    bool        active;
    int         midi_note;
    int         age;
    bool        trigger_pending;
    bool        trig_gap;        // erzwinge 1 Render-Block trigger=0 (Flanke für Retrigger)
    bool        last_trig_high;  // stand mods.trigger beim letzten Render-Block auf 1?
    int         stacked;         // note_ons ohne zugehöriges note_off auf midi_note
    float       velocity;

    AdsrState   adsr_state;
    float       adsr_env;
    float       adsr_from;      // env level captured at note-off
    float       steal_env;      // env level at moment of voice steal (for declick crossfade)
    int         declick;        // > 0: crossfade counter after voice steal

    // Per-voice filter
    void*     flt        = nullptr;
    float     fenv2      = 0.f;
    float     fenv2_from = 0.f;
    AdsrState fenv2_state = ADSR_IDLE;
    float     fc_smooth  = 20000.f;
    float     flt_buf[kMaxBlockSize * 2];  // interleaved L/R temp buffer

    void init(const Patch& shared) {
        alloc.Init(pool, kPoolSize);
        voice.Init(&alloc);
        patch               = shared;
        memset(&mods, 0, sizeof(mods));
        mods.trigger_patched = true;
        mods.level_patched   = false;   // let LPG decay per patch.decay
        active              = false;
        midi_note           = -1;
        age                 = 0;
        trigger_pending     = false;
        trig_gap            = false;
        last_trig_high      = false;
        stacked             = 0;
        velocity            = 0.f;
        adsr_state          = ADSR_IDLE;
        adsr_env            = 0.f;
        adsr_from           = 0.f;
        declick             = 0;
    }
};

// ── Engine (one instance per plugin) ──────────────────────────────────────────

struct PlaitsEngine {
    PlaitsVoice slots[kVoices];
    Patch       shared;
    int         age_ctr;
    int         maxVoices = kVoices;

    float  sample_rate;
    float  pitch_correction;   // 12 * log2(47872.34 / sr)

    float  pitch_bend;         // semitones, ±2
    float  mod_wheel;          // 0..1 → timbre mod
    float  aftertouch;         // 0..1 → morph mod

    float  adsr_atk_ms;
    float  adsr_dec_ms;
    float  adsr_sus;
    float  adsr_rel_ms;

    // Per-voice filter parameters (set each block by processor)
    float  flt_base      = 20000.f;
    float  flt_env_amt   = 0.f;
    float  flt_res       = 0.f;
    float  flt_morph     = 0.f;
    float  flt_key_track = 0.f;
    int    flt_slope     = 1;   // 0 = 12 dB/oct, 1 = 24 dB/oct
    float  fenv2_atk_ms  = 10.f;
    float  fenv2_dec_ms  = 300.f;
    float  fenv2_sus     = 0.f;
    float  fenv2_rel_ms  = 500.f;

    void init(float sr) {
        age_ctr       = 0;
        pitch_bend    = 0.f;
        mod_wheel     = 0.f;
        aftertouch    = 0.f;
        adsr_atk_ms   = 5.f;
        adsr_dec_ms   = 500.f;
        adsr_sus      = 0.8f;
        adsr_rel_ms   = 200.f;
        flt_base = 20000.f; flt_env_amt = 0.f; flt_res = 0.f;
        flt_morph = 0.f;    flt_key_track = 0.f; flt_slope = 1;
        fenv2_atk_ms = 10.f; fenv2_dec_ms = 300.f;
        fenv2_sus = 0.f;     fenv2_rel_ms = 500.f;

        shared.note                        = 60.f;
        shared.harmonics                   = 0.5f;
        shared.timbre                      = 0.5f;
        shared.morph                       = 0.5f;
        shared.frequency_modulation_amount = 0.f;
        shared.timbre_modulation_amount    = 0.f;
        shared.morph_modulation_amount     = 0.f;
        shared.engine                      = kEngineMap[0];
        shared.decay                       = 1.0f;
        shared.lpg_colour                  = 0.f;

        set_sample_rate(sr);
        for (int i = 0; i < kVoices; ++i) {
            slots[i].init(shared);
            if (slots[i].flt) { filter_free(slots[i].flt); slots[i].flt = nullptr; }
            slots[i].flt = filter_create(sr > 0.f ? sr : 44100.f);
            slots[i].fc_smooth = 20000.f;
        }
    }

    void set_sample_rate(float sr) {
        sample_rate      = sr > 0.f ? sr : 44100.f;
        pitch_correction = 12.f * log2f(47872.34f / sample_rate);
    }

    void sync_patch_to_voices() {
        for (int i = 0; i < kVoices; ++i) {
            float note = slots[i].patch.note;
            slots[i].patch = shared;
            slots[i].patch.note = note;
        }
    }
};

// ── Public C API ───────────────────────────────────────────────────────────────

extern "C" {

void* plaits_create() {
    PlaitsEngine* e = new (std::nothrow) PlaitsEngine();
    if (!e) return nullptr;
    e->init(44100.f);
    return e;
}

void plaits_free(void* h) {
    if (!h) return;
    auto* e = static_cast<PlaitsEngine*>(h);
    for (int i = 0; i < kVoices; ++i)
        if (e->slots[i].flt) filter_free(e->slots[i].flt);
    delete e;
}

void plaits_set_sample_rate(void* h, float sr) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);
    e.set_sample_rate(sr);
    for (int i = 0; i < kVoices; ++i) {
        if (e.slots[i].flt) filter_free(e.slots[i].flt);
        e.slots[i].flt = filter_create(e.sample_rate);
        e.slots[i].fc_smooth = e.flt_base;
    }
}

void plaits_set_filter_params(void* h,
    float base_hz, float env_amt, float res, float morph, float key_track,
    float atk_ms, float dec_ms, float sus, float rel_ms, int slope)
{
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);
    e.flt_base      = base_hz > 20.f ? base_hz : 20.f;
    e.flt_env_amt   = env_amt;
    e.flt_res       = res;
    e.flt_morph     = morph;
    e.flt_key_track = key_track;
    e.flt_slope     = slope;
    e.fenv2_atk_ms  = atk_ms  > 0.1f ? atk_ms  : 0.1f;
    e.fenv2_dec_ms  = dec_ms  > 0.1f ? dec_ms  : 0.1f;
    e.fenv2_sus     = sus < 0.f ? 0.f : (sus > 1.f ? 1.f : sus);
    e.fenv2_rel_ms  = rel_ms  > 0.1f ? rel_ms  : 0.1f;
}

// ui_idx: 0-10, mapped to actual Plaits engine via kEngineMap
void plaits_set_model(void* h, int ui_idx) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);
    ui_idx = std::max(0, std::min(15, ui_idx));
    e.shared.engine = kEngineMap[ui_idx];
    e.sync_patch_to_voices();
}

// idx: 0=harmonics, 1=timbre, 2=morph, 3=lpg_colour, 4=lpg_decay
void plaits_set_param(void* h, int idx, float v) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);
    switch (idx) {
        case 0: e.shared.harmonics  = v; break;
        case 1: e.shared.timbre     = v; break;
        case 2: e.shared.morph      = v; break;
        case 3: e.shared.lpg_colour = v; break;
        case 4: e.shared.decay      = v; break;
        default: return;
    }
    e.sync_patch_to_voices();
}

void plaits_set_adsr(void* h, float atk_ms, float dec_ms, float sus, float rel_ms) {
    if (!h) return;
    auto& e        = *static_cast<PlaitsEngine*>(h);
    e.adsr_atk_ms  = atk_ms  > 0.1f ? atk_ms  : 0.1f;
    e.adsr_dec_ms  = dec_ms  > 0.1f ? dec_ms  : 0.1f;
    e.adsr_sus     = sus     < 0.f  ? 0.f  : (sus > 1.f ? 1.f : sus);
    e.adsr_rel_ms  = rel_ms  > 0.1f ? rel_ms  : 0.1f;
}

void plaits_note_on(void* h, int midi_note, float velocity) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);

    // Bevorzuge: 1. idle, 2. leoseste RELEASE-Stimme, 3. älteste aktive
    int nv = e.maxVoices;
    int target = -1, min_age = INT_MAX, quietest_rel = -1;
    float min_rel_env = 1.f;
    for (int i = 0; i < nv; ++i) {
        if (e.slots[i].adsr_state == ADSR_IDLE) { target = i; break; }
        if (e.slots[i].adsr_state == ADSR_RELEASE && e.slots[i].adsr_env < min_rel_env) {
            min_rel_env = e.slots[i].adsr_env;
            quietest_rel = i;
        }
        if (e.slots[i].age < min_age) { min_age = e.slots[i].age; target = i; }
    }
    if (target < 0 || (quietest_rel >= 0 && min_rel_env < 0.3f))
        target = quietest_rel >= 0 ? quietest_rel : target;

    PlaitsVoice& s   = e.slots[target];
    bool was_active  = (s.adsr_state != ADSR_IDLE);
    // Stack-Zähler: wird derselbe Slot legato mit gleicher Tonhöhe wiederverwendet
    // (Note-Off der alten Note kommt erst nach diesem Note-On), darf das Off die
    // neue Note nicht releasen — erst wenn gleich viele Offs wie Ons da waren.
    if (s.active && s.midi_note == midi_note)
        s.stacked++;
    else
        s.stacked = 1;
    // War der Trigger beim letzten Render noch high, braucht die Voice erst einen
    // Block trigger=0, sonst sieht Plaits keine steigende Flanke → kein Retrigger
    // des internen LPG/Decay (Rolling-Bass-Aussterben).
    s.trig_gap       = s.last_trig_high;
    s.steal_env      = was_active ? s.adsr_env : 0.f;  // env-Stand für declick-Crossfade
    s.patch          = e.shared;
    s.patch.note     = (float)midi_note + e.pitch_correction + e.pitch_bend;
    s.midi_note      = midi_note;
    s.active         = true;
    s.trigger_pending = true;
    s.velocity       = velocity;
    s.age            = ++e.age_ctr;
    s.mods.trigger   = 0.f;
    s.mods.level     = velocity;
    s.adsr_state     = ADSR_ATTACK;
    s.declick        = was_active ? kDeclickLen : 0;
    // Reset per-voice filter — prime with key-tracked cutoff to avoid note-on zap
    {
        float note_hz = 440.f * powf(2.f, (midi_note - 69) / 12.f);
        float fc_init = e.flt_base;
        if (e.flt_key_track > 0.001f)
            fc_init *= powf(note_hz / 261.63f, e.flt_key_track);
        if (fc_init < 20.f) fc_init = 20.f; else if (fc_init > 20000.f) fc_init = 20000.f;
        s.fc_smooth = fc_init;
        if (s.flt) filter_set_fc_smooth(s.flt, fc_init); /* fc snap ohne State-Flush → kein Click */
    }
    s.fenv2       = 0.f;
    s.fenv2_state = ADSR_ATTACK;
}

void plaits_note_off(void* h, int midi_note) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);
    // Nur die ÄLTESTE aktive Voice mit dieser Tonhöhe releasen — bei direkt
    // aneinandergrenzenden Noten (Note-On der neuen Note vor Note-Off der alten,
    // gleiche Tonhöhe) darf das Off die frisch getriggerte Voice nicht töten.
    int target = -1, oldest_age = INT_MAX;
    for (int i = 0; i < kVoices; ++i) {
        PlaitsVoice& s = e.slots[i];
        if (s.active && s.midi_note == midi_note && s.age < oldest_age) {
            oldest_age = s.age;
            target = i;
        }
    }
    if (target >= 0) {
        PlaitsVoice& s  = e.slots[target];
        if (--s.stacked <= 0) {
            s.stacked       = 0;
            s.active        = false;
            s.adsr_from     = s.adsr_env;
            s.adsr_state    = ADSR_RELEASE;
            s.mods.trigger  = 0.f;
            s.fenv2_from    = s.fenv2;
            s.fenv2_state   = ADSR_RELEASE;
        }
    }
}

void plaits_set_polyphony(void* h, int n) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);
    e.maxVoices = n < 1 ? 1 : (n > kVoices ? kVoices : n);
}

void plaits_set_pitch_bend(void* h, int bend_14bit) {
    if (!h) return;
    // ±2 semitones range
    static_cast<PlaitsEngine*>(h)->pitch_bend =
        (bend_14bit - 8192) / 8192.f * 2.f;
}

void plaits_set_mod_wheel(void* h, float v) {
    if (!h) return;
    static_cast<PlaitsEngine*>(h)->mod_wheel = v;
}

void plaits_set_aftertouch(void* h, float v) {
    if (!h) return;
    static_cast<PlaitsEngine*>(h)->aftertouch = v;
}

void plaits_all_notes_off(void* h) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);
    for (int i = 0; i < kVoices; ++i) {
        PlaitsVoice& s    = e.slots[i];
        s.active          = false;
        s.velocity        = 0.f;
        s.trigger_pending = false;
        s.trig_gap        = false;
        s.last_trig_high  = false;
        s.stacked         = 0;
        s.adsr_state      = ADSR_IDLE;
        s.adsr_env        = 0.f;
        s.mods.trigger    = 0.f;
        s.mods.level      = 0.f;
        s.fenv2           = 0.f;
        s.fenv2_state     = ADSR_IDLE;
        if (s.flt) filter_flush(s.flt);
    }
}

// Returns max envelope level across all active voices (for filter env modulation)
float plaits_get_env(void* h) {
    if (!h) return 0.f;
    auto& e = *static_cast<PlaitsEngine*>(h);
    float mx = 0.f;
    for (int i = 0; i < kVoices; ++i)
        if (e.slots[i].adsr_state != ADSR_IDLE && e.slots[i].adsr_env > mx)
            mx = e.slots[i].adsr_env;
    return mx;
}

void plaits_render(void* h, float* out_L, float* out_R, int n_frames) {
    if (!h) return;
    auto& e = *static_cast<PlaitsEngine*>(h);

    const float scale     = 1.f / 32767.f;
    const float atk_rate  = 1000.f / (e.adsr_atk_ms  * e.sample_rate);
    const float dec_rate  = (1.f - e.adsr_sus) * 1000.f / (e.adsr_dec_ms  * e.sample_rate);
    const float rel_rate  = 1000.f / (e.adsr_rel_ms  * e.sample_rate);
    const float fatk_rate = 1000.f / (e.fenv2_atk_ms * e.sample_rate);
    const float fdec_rate = (1.f - e.fenv2_sus) * 1000.f / (e.fenv2_dec_ms * e.sample_rate);
    const float frel_rate = 1000.f / (e.fenv2_rel_ms * e.sample_rate);

    for (int v = 0; v < kVoices; ++v) {
        PlaitsVoice& s = e.slots[v];
        if (s.adsr_state == ADSR_IDLE && s.fenv2_state == ADSR_IDLE && !s.trigger_pending) continue;

        s.patch.note   = (float)s.midi_note + e.pitch_correction + e.pitch_bend;
        s.mods.timbre  = e.mod_wheel  * 0.5f;
        s.mods.morph   = e.aftertouch * 0.5f;

        int remaining = n_frames, pos = 0;
        while (remaining > 0) {
            int block = std::min(remaining, (int)kMaxBlockSize);

            if (s.trig_gap) {
                s.mods.trigger    = 0.f;   // ein Block low → nächster Block hat Flanke
                s.trig_gap        = false;
            } else if (s.trigger_pending) {
                s.mods.trigger    = 1.f;
                s.trigger_pending = false;
            } else if (s.active) {
                s.mods.trigger    = 1.f;
            } else {
                s.mods.trigger    = 0.f;
            }
            s.last_trig_high = (s.mods.trigger > 0.5f);

            s.voice.Render(s.patch, s.mods, s.frames, (size_t)block);

            for (int j = 0; j < block; ++j) {
                // Advance amplitude ADSR
                switch (s.adsr_state) {
                    case ADSR_ATTACK:
                        s.adsr_env += atk_rate;
                        if (s.adsr_env >= 1.f) { s.adsr_env = 1.f; s.adsr_state = ADSR_DECAY; }
                        break;
                    case ADSR_DECAY:
                        s.adsr_env -= dec_rate;
                        if (s.adsr_env <= e.adsr_sus) { s.adsr_env = e.adsr_sus; s.adsr_state = ADSR_SUSTAIN; }
                        break;
                    case ADSR_SUSTAIN: s.adsr_env = e.adsr_sus; break;
                    case ADSR_RELEASE:
                        s.adsr_env -= s.adsr_from * rel_rate;
                        if (s.adsr_env <= 0.f) {
                            s.adsr_env = 0.f; s.adsr_state = ADSR_IDLE;
                            s.fenv2 = 0.f;    s.fenv2_state = ADSR_IDLE;
                        }
                        break;
                    case ADSR_IDLE: s.adsr_env = 0.f; break;
                }

                // Advance filter ENV2
                switch (s.fenv2_state) {
                    case ADSR_ATTACK:
                        s.fenv2 += fatk_rate;
                        if (s.fenv2 >= 1.f) { s.fenv2 = 1.f; s.fenv2_state = ADSR_DECAY; }
                        break;
                    case ADSR_DECAY:
                        s.fenv2 -= fdec_rate;
                        if (s.fenv2 <= e.fenv2_sus) { s.fenv2 = e.fenv2_sus; s.fenv2_state = ADSR_SUSTAIN; }
                        break;
                    case ADSR_SUSTAIN: s.fenv2 = e.fenv2_sus; break;
                    case ADSR_RELEASE:
                        s.fenv2 -= s.fenv2_from * frel_rate;
                        if (s.fenv2 <= 0.f) { s.fenv2 = 0.f; s.fenv2_state = ADSR_IDLE; }
                        break;
                    case ADSR_IDLE: s.fenv2 = 0.f; break;
                }

                // Declick crossfade
                float dc = 1.f;
                if (s.declick > 0) {
                    float t = 1.f - (float)s.declick / (float)kDeclickLen;
                    dc = s.steal_env + (1.f - s.steal_env) * t;
                    --s.declick;
                }

                // Write to per-voice filter buffer (interleaved)
                float samp = s.frames[j].out * scale * s.adsr_env * s.velocity * 0.7f;
                samp = tanhf(samp * 1.25f) * 0.8f * dc;
                s.flt_buf[j * 2]     = samp;
                s.flt_buf[j * 2 + 1] = samp;
            }

            // ── Per-voice filter for this sub-block ───────────────────────────
            if (s.flt) {
                float note_hz = 440.f * powf(2.f, (s.midi_note - 69) / 12.f);
                float fc = e.flt_base;
                if (e.flt_key_track > 0.001f)
                    fc *= powf(note_hz / 261.63f, e.flt_key_track);
                if (e.flt_env_amt > 0.001f && s.fenv2 > 0.001f)
                    fc *= powf(20000.f / (fc < 1.f ? 1.f : fc), e.flt_env_amt * s.fenv2);
                if (fc < 20.f) fc = 20.f; else if (fc > 20000.f) fc = 20000.f;

                float tau = (fc >= s.fc_smooth) ? 0.003f : 0.030f;
                float k   = expf(-(float)block / (tau * e.sample_rate));
                s.fc_smooth = k * s.fc_smooth + (1.f - k) * fc;

                filter_set_cutoff   (s.flt, s.fc_smooth);
                filter_set_resonance(s.flt, e.flt_res);
                filter_set_morph    (s.flt, e.flt_morph);
                filter_set_slope    (s.flt, e.flt_slope);
                filter_process(s.flt, s.flt_buf, s.flt_buf, block);
            }

            for (int j = 0; j < block; ++j) {
                out_L[pos + j] += s.flt_buf[j * 2];
                out_R[pos + j] += s.flt_buf[j * 2 + 1];
            }

            pos       += block;
            remaining -= block;
        }
    }
}

} // extern "C"
