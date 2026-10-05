// rings_c.cpp — Mutable Instruments Rings wrapper for Groovebox
// Handle-based API: one RingsEngine per track.
//
// Pitch: tonic = kPitchCorrection, note = MIDI note → total = note + 1.44
// at 44100 Hz this compensates for Rings' internal 48 kHz sample rate.
//
// Polyphony: managed internally by rings::Part.
// Strum fires on the first DSP block after note_on.
// Amplitude shaped by configurable ADSR envelope.

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <new>

#include "rings/dsp/part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include "rings/dsp/dsp.h"

using namespace rings;

// ── Constants ─────────────────────────────────────────────────────────────────

// Pitch correction and SR are now per-engine (set via rings_set_sample_rate).
// Default fallback values for 44100 Hz:
static const float kDefaultSR            = 44100.0f;
static const float kDefaultPitchCorrect  = 1.4423f;  // 12*log2(48000/44100)
static const size_t kRevBufSz       = 32768;
static const int    kBlk            = (int)kMaxBlockSize;   // 24
static const int    kPendMax        = 16;                   // pending-strum queue depth (fast chords)

// ── ADSR state ─────────────────────────────────────────────────────────────────

enum AdsrState { ADSR_IDLE, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE };

// ── Engine struct ──────────────────────────────────────────────────────────────

struct RingsEngine {
    Part       part;
    uint16_t   reverb_buf[kRevBufSz];
    Patch      patch;

    float  cur_midi_note;    // MIDI note of last strummed note
    float  velocity;         // velocity of last strummed note
    bool   note_active;      // true while any key held
    int    held_notes;       // count of keys currently down (gates the global amp env)
    // Pending-strum FIFO: note_on enqueues, render strums one per sub-block so a fast chord
    // (several note_ons in ONE host block) strums every note, not just the last.
    int    pend_note[kPendMax];
    float  pend_vel[kPendMax];
    int    pend_head, pend_count;
    int    chord;            // PerformanceState.chord 0..10
    float  fm;               // PerformanceState.fm, semitones ±24

    // Expression (MIDI pitch bend, mod wheel, aftertouch)
    float  pitch_bend;       // semitones, ±2
    float  mod_wheel;        // 0..1 → adds to brightness
    float  aftertouch;       // 0..1 → adds to structure

    // Sample-rate-dependent constants (set via rings_set_sample_rate)
    float  sample_rate;
    float  pitch_correction; // 12*log2(48000/sr)

    // ADSR
    AdsrState adsr_state;
    float  adsr_env;
    float  adsr_release_from;
    float  adsr_attack_ms;
    float  adsr_decay_ms;
    float  adsr_sustain;
    float  adsr_release_ms;

    int    tail_samples;

    float  in_buf[kMaxBlockSize];

    void init() {
        part.Init(reverb_buf);
        part.set_polyphony(1);
        part.set_model(RESONATOR_MODEL_MODAL);

        memset(in_buf, 0, sizeof(in_buf));

        patch.structure  = 0.5f;
        patch.brightness = 0.5f;
        patch.damping    = 0.5f;
        patch.position   = 0.5f;

        cur_midi_note    = 60.0f;
        velocity         = 1.0f;
        note_active      = false;
        held_notes       = 0;
        pend_head        = 0;
        pend_count       = 0;
        chord            = 0;
        fm               = 0.0f;

        pitch_bend       = 0.0f;
        mod_wheel        = 0.0f;
        aftertouch       = 0.0f;

        sample_rate      = kDefaultSR;
        pitch_correction = kDefaultPitchCorrect;

        adsr_state       = ADSR_IDLE;
        adsr_env         = 0.0f;
        adsr_release_from = 0.0f;
        adsr_attack_ms   = 10.0f;
        adsr_decay_ms    = 800.0f;
        adsr_sustain     = 0.85f;
        adsr_release_ms  = 1000.0f;
        tail_samples     = 0;
    }
};

// ── Helpers ────────────────────────────────────────────────────────────────────

static inline void _zero(float* p, int n) { memset(p, 0, n * sizeof(float)); }

// ── Public API ─────────────────────────────────────────────────────────────────

extern "C" {

void* rings_create() {
    RingsEngine* e = new (std::nothrow) RingsEngine();
    if (!e) return nullptr;
    e->init();
    return static_cast<void*>(e);
}

void rings_free(void* h) {
    if (h) delete static_cast<RingsEngine*>(h);
}

void rings_note_on(void* h, int midi_note, float velocity) {
    if (!h) return;
    RingsEngine& e    = *static_cast<RingsEngine*>(h);
    // Held-Note-Zähler: eine Taste mehr unten. Die neueste Note treibt den nächsten
    // Strum; der Part (set_polyphony) verteilt die Stimmen intern und stiehlt bei
    // Überschreiten der Polyphonie die ÄLTESTE — der Wrapper würgt nichts ab.
    e.held_notes++;
    // Enqueue this note's strum; render pops one per sub-block. Drop oldest if the queue is full.
    if (e.pend_count >= kPendMax) { e.pend_head = (e.pend_head + 1) % kPendMax; e.pend_count--; }
    {
        int t = (e.pend_head + e.pend_count) % kPendMax;
        e.pend_note[t] = midi_note;
        e.pend_vel[t]  = (velocity > 0.0f) ? velocity : 0.0f;
        e.pend_count++;
    }
    e.cur_midi_note   = (float)midi_note;
    e.note_active     = true;
    // Kein adsr_env-Reset — Attack rampt vom aktuellen Level hoch (kein Click)
    e.adsr_state      = ADSR_ATTACK;
}

void rings_note_off(void* h, int midi_note) {
    if (!h) return;
    RingsEngine& e = *static_cast<RingsEngine*>(h);
    // Die globale Amp-Hülle ist EIN VCA über den ganzen (polyphonen) Rings-Ausgang.
    // Darum erst releasen, wenn die LETZTE Taste los ist — sonst würgt das Off einer
    // einzelnen Note alle noch klingenden Stimmen ab. Kein Tonhöhen-Match nötig
    // (Zähler ist robust gegen Off-vor-On-Legato und Oktav-Transpose).
    (void)midi_note;
    if (e.held_notes > 0) e.held_notes--;
    if (e.held_notes <= 0) {
        e.held_notes        = 0;
        e.note_active       = false;
        e.adsr_release_from = e.adsr_env;
        e.adsr_state        = ADSR_RELEASE;
    }
}

void rings_set_adsr(void* h, float attack_ms, float decay_ms,
                    float sustain, float release_ms) {
    if (!h) return;
    RingsEngine& e    = *static_cast<RingsEngine*>(h);
    e.adsr_attack_ms  = attack_ms  > 0.1f ? attack_ms  : 0.1f;
    e.adsr_decay_ms   = decay_ms   > 0.1f ? decay_ms   : 0.1f;
    e.adsr_sustain    = sustain < 0.0f ? 0.0f : (sustain > 1.0f ? 1.0f : sustain);
    e.adsr_release_ms = release_ms > 0.1f ? release_ms : 0.1f;
}

// param_idx:
//   0  Structure   0..1
//   1  Brightness  0..1
//   2  Damping     0..1
//   3  Position    0..1
//   4  Model       stored as float 0..5 (integer steps)
//   5  Polyphony   stored as float 1..4 (integer steps)
//   6  Chord       stored as float 0..10 (integer steps)
//   7  FM          0..1 → detune ‑24..+24 semitones (centre 0.5 = 0)
void rings_set_param(void* h, int idx, float v) {
    if (!h) return;
    RingsEngine& e = *static_cast<RingsEngine*>(h);
    switch (idx) {
        case 0: e.patch.structure  = v; break;
        case 1: e.patch.brightness = v; break;
        case 2: e.patch.damping    = v; break;
        case 3: e.patch.position   = v; break;
        case 4: {
            int m = (int)(v + 0.5f);
            if (m < 0) m = 0;
            if (m > 5) m = 5;
            e.part.set_model(static_cast<ResonatorModel>(m));
            break;
        }
        case 5: {
            int p = (int)(v + 0.5f);
            if (p < 1) p = 1;
            if (p > 8) p = 8;
            if (p != e.part.polyphony())  // set_polyphony always sets dirty_ — only call when changed
                e.part.set_polyphony(p);
            break;
        }
        case 6: {
            int c = (int)(v + 0.5f);
            if (c < 0)  c = 0;
            if (c > 10) c = 10;
            e.chord = c;
            break;
        }
        case 7:
            e.fm = (v - 0.5f) * 48.0f;   // 0..1 → -24..+24
            break;
        default: break;
    }
}

void rings_set_sample_rate(void* h, float sr) {
    if (!h) return;
    RingsEngine& e    = *static_cast<RingsEngine*>(h);
    e.sample_rate     = sr > 0.0f ? sr : kDefaultSR;
    e.pitch_correction = 12.0f * log2f(48000.0f / e.sample_rate);
}

void rings_set_pitch_bend(void* h, int bend_14bit) {
    // bend_14bit: 0..16383, centre=8192 → ±2 semitones
    if (!h) return;
    static_cast<RingsEngine*>(h)->pitch_bend =
        ((float)(bend_14bit - 8192) / 8192.0f) * 2.0f;
}

void rings_set_mod_wheel(void* h, float v) {
    if (!h) return;
    static_cast<RingsEngine*>(h)->mod_wheel = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void rings_set_aftertouch(void* h, float v) {
    if (!h) return;
    static_cast<RingsEngine*>(h)->aftertouch = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void rings_all_notes_off(void* h) {
    if (!h) return;
    RingsEngine& e   = *static_cast<RingsEngine*>(h);
    e.note_active    = false;
    e.held_notes     = 0;
    e.pend_head      = 0;
    e.pend_count     = 0;
    e.adsr_env       = 0.0f;
    e.adsr_state     = ADSR_IDLE;
}

float rings_get_env(void* h) {
    if (!h) return 0.0f;
    return static_cast<RingsEngine*>(h)->adsr_env;
}

// Renders n_frames into out_L / out_R (additive — caller should zero first).
// Rings outputs main (out) → out_L and aux → out_R for natural stereo spread.
void rings_render(void* h, float* out_L, float* out_R, int n_frames) {
    if (!h) { _zero(out_L, n_frames); _zero(out_R, n_frames); return; }
    RingsEngine& e = *static_cast<RingsEngine*>(h);

    // Skip DSP when completely silent
    if (!e.note_active && e.pend_count == 0 && e.adsr_state == ADSR_IDLE) {
        return;
    }

    float blk_out[kMaxBlockSize];
    float blk_aux[kMaxBlockSize];

    int pos = 0;
    // Poly headroom: the global amp VCA sits over the WHOLE polyphonic Part output, so raising polyphony
    // raises the summed level and used to clip. Scale by 1/sqrt(voices) (RMS-correct headroom) so a full
    // chord stays inside the output soft-clip knee instead of tearing. Trade-off: a single note played on a
    // high poly setting is quieter; predictable (no pumping) and matches the Plaits port's fixed headroom.
    float poly_hr = 1.0f / sqrtf((float)std::max(1, e.part.polyphony()));
    while (pos < n_frames) {
        int sz = std::min(kBlk, n_frames - pos);

        // Build a patch copy with mod-wheel and aftertouch applied
        Patch p = e.patch;
        p.brightness = std::min(1.0f, p.brightness + e.mod_wheel  * 0.5f);
        p.structure  = std::min(1.0f, p.structure  + e.aftertouch * 0.5f);

        // Strum one queued note per sub-block, so a fast chord fans out across voices.
        bool strum = false;
        if (e.pend_count > 0) {
            e.cur_midi_note = (float)e.pend_note[e.pend_head];
            e.velocity      = e.pend_vel[e.pend_head];
            e.pend_head     = (e.pend_head + 1) % kPendMax;
            e.pend_count--;
            strum = true;
        }

        PerformanceState ps;
        ps.strum            = strum;
        ps.internal_exciter = true;
        ps.internal_strum   = false;
        ps.internal_note    = false;
        ps.tonic            = e.pitch_correction;
        ps.note             = e.cur_midi_note;
        ps.fm               = e.fm + e.pitch_bend;  // pitch bend on top of FM param
        ps.chord            = e.chord;

        e.part.Process(ps, p, e.in_buf, blk_out, blk_aux, (size_t)sz);

        // Full ADSR state machine
        float fsz = (float)sz;
        switch (e.adsr_state) {
            case ADSR_ATTACK: {
                float attack_samples = e.adsr_attack_ms * e.sample_rate / 1000.0f;
                e.adsr_env += fsz / attack_samples;
                if (e.adsr_env >= 1.0f) { e.adsr_env = 1.0f; e.adsr_state = ADSR_DECAY; }
                break;
            }
            case ADSR_DECAY: {
                float decay_samples = e.adsr_decay_ms * e.sample_rate / 1000.0f;
                e.adsr_env -= fsz * (1.0f - e.adsr_sustain) / decay_samples;
                if (e.adsr_env <= e.adsr_sustain) { e.adsr_env = e.adsr_sustain; e.adsr_state = ADSR_SUSTAIN; }
                break;
            }
            case ADSR_SUSTAIN:
                e.adsr_env = e.adsr_sustain;
                break;
            case ADSR_RELEASE: {
                float release_samples = e.adsr_release_ms * e.sample_rate / 1000.0f;
                e.adsr_env -= fsz * e.adsr_release_from / release_samples;
                if (e.adsr_env <= 0.0f) { e.adsr_env = 0.0f; e.adsr_state = ADSR_IDLE; }
                break;
            }
            default: break;
        }

        float gain = e.velocity * 0.65f * e.adsr_env * poly_hr;
        for (int j = 0; j < sz; ++j) {
            float l = blk_out[j] * 0.65f + blk_aux[j] * 0.35f;
            float r = blk_out[j] * 0.35f + blk_aux[j] * 0.65f;
            out_L[pos + j] += l * gain;
            out_R[pos + j] += r * gain;
        }

        pos += sz;
    }
}

} // extern "C"
