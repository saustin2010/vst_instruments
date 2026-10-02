/**
 * drift.h — Analog drift / jitter system for Aphex.
 *
 * Multi-source per-instance drift, modeled on the same physical phenomena that
 * give analog synthesizers their "alive" character. Combines insights from:
 *
 *   - MI Braids (pichenettes/eurorack/braids/vco_jitter_source.h, MIT) — the
 *     idea of layering several physically-motivated noise sources (external
 *     temperature, room temperature, mains hum bleed, LFO crosstalk, white
 *     noise) seeded per-instance so each module has its own personality.
 *
 *   - U-He Diva "Trimmers" panel (closed source, public taxonomy only) —
 *     the distinction between *fixed* per-instance offsets (component
 *     tolerance / round-robin variance) vs *time-varying* drift.
 *
 *   - Sequential Prophet-style DCO modulation — broadband noise injected into
 *     the pitch CV path, plus slow sample-and-hold drift.
 *
 * IMPLEMENTATION (original, MIT-licensed):
 *   Five drift sources combined with per-instance random seeding. All math
 *   below is original; no code copied from any external project.
 *
 *   1. PER-INSTANCE FIXED OFFSETS (set once at init, "this unit's personality"):
 *      - VCO1, VCO2, sub detune          — fixed offset in cents per instance
 *      - per-VCO drift amplitude scaling — some units drift more than others
 *      - filter cutoff offset            — slight calibration error
 *      - envelope time scaling           — caps tolerance affecting AR/D/R
 *
 *   2. SLOW DRIFT (thermal / power supply, ~0.1 Hz random walk):
 *      - per-VCO independent random walks driving pitch
 *      - filter cutoff drift
 *      - VCA amplitude drift (subtle)
 *
 *   3. MID-RATE FLUTTER (mains coupling / ground loop, ~6-15 Hz):
 *      - per-VCO sine LFO at slightly different rates per instance
 *      - models 60/120 Hz mains bleed sub-octaved by AC coupling
 *
 *   4. PHASE JITTER (broadband noise on each VCO phase):
 *      - per-sample white noise added to phase increment
 *      - models thermal noise in integrator capacitors
 *
 *   5. STARTUP PHASE OFFSET (per note-on):
 *      - oscillators start at random phase rather than 0
 *      - models the "you'll never get exactly the same waveform twice" effect
 *
 * USAGE
 *   drift_t d;
 *   drift_init(&d, my_seed);              // once per instance
 *
 *   // At note-on:
 *   drift_note_on(&d);                    // re-rolls fast drift state
 *
 *   // Once per audio block (call before per-sample processing):
 *   drift_block_update(&d, drift_amount); // drift_amount ∈ [0,1]
 *
 *   // Per sample, query the modulation values:
 *   float v1_pitch_offset = drift_pitch_v1(&d);   // in cents
 *   float v2_pitch_offset = drift_pitch_v2(&d);   // in cents
 *   float lpf_offset      = drift_lpf(&d);        // in normalized cutoff units
 *   float vca_offset      = drift_vca(&d);        // in linear gain (additive)
 *
 *   // Or get fixed per-instance offsets directly:
 *   d.fixed_v1_cents, d.fixed_v2_cents, etc.
 */

#ifndef APHEX_DRIFT_H
#define APHEX_DRIFT_H

#include <math.h>
#include <stdint.h>

#ifndef APHEX_DRIFT_PI
#define APHEX_DRIFT_PI 3.14159265358979f
#endif

typedef struct {
    /* Per-instance RNG state (LCG, same as aphex.c uses) */
    uint32_t rng;

    /* === Fixed per-instance "personality" offsets (set in drift_init) === */
    float fixed_v1_cents;       /* ±5 cents typical */
    float fixed_v2_cents;
    float fixed_sub_cents;
    float v1_drift_scale;       /* 0.5..1.5 — how much THIS unit drifts */
    float v2_drift_scale;
    float fixed_lpf_offset;     /* ±0.02 of normalized cutoff */
    float fixed_hpf_offset;
    float env_time_scale;       /* 0.95..1.05 — caps tolerance on env stages */

    /* === Slow drift state (random walk, updated once per block) === */
    float slow_v1, slow_v2, slow_sub;       /* in cents, ±~10 cents range */
    float slow_lpf, slow_hpf;               /* in normalized cutoff units */
    float slow_vca;                         /* in linear gain */

    /* === Mid-rate flutter (sine LFOs at instance-specific rates) === */
    float flutter_v1_phase, flutter_v1_inc;
    float flutter_v2_phase, flutter_v2_inc;

    /* === Per-sample interpolation: hold last block's slow values and lerp === */
    float slow_v1_prev, slow_v2_prev, slow_sub_prev;
    float slow_lpf_prev, slow_hpf_prev, slow_vca_prev;
    float interp_t, interp_dt;              /* [0..1) per block */

    /* === Per-note micro-detune (re-rolled every note-on) ===
     * On a real MS-20, the integrator cap charge state was never identical
     * between notes — the exp converter's transistor pair settled to a
     * slightly different operating point each time. Result: ±5 cents of
     * "no two notes the same" character. Re-rolled in drift_note_on(),
     * stable for the duration of the note. */
    float note_v1_cents;
    float note_v2_cents;

    /* === Cached drift_amount for per-sample queries === */
    float amount;
} drift_t;

/* Local LCG (don't touch aphex.c's rng). */
static inline uint32_t drift_rand32(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}
static inline float drift_rand_f(uint32_t *s) {
    return (drift_rand32(s) >> 8) * (1.0f / 16777216.0f);    /* [0, 1) */
}
static inline float drift_rand_b(uint32_t *s) {
    return drift_rand_f(s) * 2.0f - 1.0f;                    /* (-1, 1) */
}
/* Approximate Gaussian via central-limit (sum of 4 uniforms). Roughly N(0, 1/3).
 * Used to make outliers rare — most detune offsets are small, occasional big ones. */
static inline float drift_rand_g(uint32_t *s) {
    return (drift_rand_b(s) + drift_rand_b(s) + drift_rand_b(s) + drift_rand_b(s)) * 0.5f;
}

/* Initialize a drift instance with a seed. Call once at instance creation.
 * The seed should be unique per module instance — using something like
 * time(NULL) ^ (uintptr_t)inst_pointer works well. */
static inline void drift_init(drift_t *d, uint32_t seed) {
    /* Shuffle seed bits before deriving anything (Marsaglia mix step) */
    d->rng = seed * 1664525u + 1013904223u;
    d->rng ^= d->rng >> 16;

    /* Per-instance fixed offsets. Use Gaussian-ish so most units are close to
     * spec; rare outliers get noticeably detuned (just like real production). */
    d->fixed_v1_cents   = drift_rand_g(&d->rng) * 5.0f;       /* ±~5 cents */
    d->fixed_v2_cents   = drift_rand_g(&d->rng) * 5.0f;
    d->fixed_sub_cents  = drift_rand_g(&d->rng) * 2.0f;       /* sub usually tighter */
    d->v1_drift_scale   = 0.7f + drift_rand_f(&d->rng) * 0.6f;   /* 0.7..1.3 */
    d->v2_drift_scale   = 0.7f + drift_rand_f(&d->rng) * 0.6f;
    d->fixed_lpf_offset = drift_rand_g(&d->rng) * 0.02f;      /* ±2% of cutoff range */
    d->fixed_hpf_offset = drift_rand_g(&d->rng) * 0.02f;
    d->env_time_scale   = 1.0f + drift_rand_g(&d->rng) * 0.025f;  /* ±~2.5% */

    /* Slow drift starts at zero */
    d->slow_v1 = d->slow_v2 = d->slow_sub = 0.0f;
    d->slow_lpf = d->slow_hpf = d->slow_vca = 0.0f;
    d->slow_v1_prev = d->slow_v2_prev = d->slow_sub_prev = 0.0f;
    d->slow_lpf_prev = d->slow_hpf_prev = d->slow_vca_prev = 0.0f;

    /* Flutter rates around 6-15 Hz, slightly different per VCO so they
     * don't beat in lockstep. */
    float fv1 = 6.0f + drift_rand_f(&d->rng) * 9.0f;
    float fv2 = 6.0f + drift_rand_f(&d->rng) * 9.0f;
    /* phase increment per BLOCK at SR=44100, BLOCK=128 → block_dt = 128/44100 s */
    d->flutter_v1_inc   = 2.0f * APHEX_DRIFT_PI * fv1 * (128.0f / 44100.0f);
    d->flutter_v2_inc   = 2.0f * APHEX_DRIFT_PI * fv2 * (128.0f / 44100.0f);
    d->flutter_v1_phase = drift_rand_f(&d->rng) * 2.0f * APHEX_DRIFT_PI;
    d->flutter_v2_phase = drift_rand_f(&d->rng) * 2.0f * APHEX_DRIFT_PI;

    /* Per-sample interpolation across block: dt = 1/BLOCK = 1/128 */
    d->interp_t  = 0.0f;
    d->interp_dt = 1.0f / 128.0f;

    d->note_v1_cents = 0.0f;
    d->note_v2_cents = 0.0f;

    d->amount = 0.0f;
}

/* Re-roll fast state on note-on. Doesn't touch fixed offsets (those are
 * "this unit's personality" and don't change). Slow drift carries over —
 * thermal state doesn't reset just because you pressed a key.
 *
 * Also rolls a fresh per-note micro-detune for both VCOs (±5 cents
 * Gaussian-shaped). Models the integrator-cap charge settling slightly
 * differently each time the exp converter (2SC1685S pair on the real
 * MS-20) is gated — gives every note its own micro-personality. */
static inline void drift_note_on(drift_t *d) {
    d->flutter_v1_phase = drift_rand_f(&d->rng) * 2.0f * APHEX_DRIFT_PI;
    d->flutter_v2_phase = drift_rand_f(&d->rng) * 2.0f * APHEX_DRIFT_PI;
    d->note_v1_cents    = drift_rand_g(&d->rng) * 5.0f;
    d->note_v2_cents    = drift_rand_g(&d->rng) * 5.0f;
}

/* Update slow drift state and advance flutter LFOs. Call once per block.
 *
 * drift_amount ∈ [0, 1]:
 *   0.0  → drift effectively off (just the per-instance fixed offsets remain)
 *   0.5  → noticeable analog character (calibrated MS-20)
 *   1.0  → "needs calibration" — heavy drift
 *
 * The slow random walk uses leaky integration (low-pass filtered noise) with
 * gentle pulling toward zero so values don't wander forever. Cutoff ≈ 0.3 Hz
 * for the slow path — very gradual.
 */
static inline void drift_block_update(drift_t *d, float drift_amount) {
    if (drift_amount < 0.0f) drift_amount = 0.0f;
    if (drift_amount > 1.0f) drift_amount = 1.0f;
    d->amount = drift_amount;

    /* Hold previous values for per-sample interpolation */
    d->slow_v1_prev  = d->slow_v1;
    d->slow_v2_prev  = d->slow_v2;
    d->slow_sub_prev = d->slow_sub;
    d->slow_lpf_prev = d->slow_lpf;
    d->slow_hpf_prev = d->slow_hpf;
    d->slow_vca_prev = d->slow_vca;

    /* Random walk with leak: y[n] = α·y[n-1] + (1-α)·noise·scale.
     * α = exp(-2π·fc·block_dt). For fc ≈ 0.3 Hz, block_dt = 128/44100 ≈ 2.9 ms,
     * α ≈ 0.9945 — very smooth, low-frequency wander. */
    const float alpha = 0.9945f;
    const float one_m_a = 1.0f - alpha;
    /* Pitch drift target amplitude (cents) — scales with drift_amount */
    float pitch_amp = 12.0f * drift_amount;     /* up to ±12 cents */
    float lpf_amp   = 0.03f * drift_amount;     /* up to ±3% of cutoff range */
    float vca_amp   = 0.04f * drift_amount;     /* up to ±4% gain */

    d->slow_v1  = alpha * d->slow_v1  + one_m_a * drift_rand_g(&d->rng) * pitch_amp * d->v1_drift_scale;
    d->slow_v2  = alpha * d->slow_v2  + one_m_a * drift_rand_g(&d->rng) * pitch_amp * d->v2_drift_scale;
    d->slow_sub = alpha * d->slow_sub + one_m_a * drift_rand_g(&d->rng) * pitch_amp * 0.3f;
    d->slow_lpf = alpha * d->slow_lpf + one_m_a * drift_rand_g(&d->rng) * lpf_amp;
    d->slow_hpf = alpha * d->slow_hpf + one_m_a * drift_rand_g(&d->rng) * lpf_amp;
    d->slow_vca = alpha * d->slow_vca + one_m_a * drift_rand_g(&d->rng) * vca_amp;

    /* Advance flutter LFOs once per block (per-sample interpolation done
     * via small linear ramp inside drift_pitch_v*). */
    d->flutter_v1_phase += d->flutter_v1_inc;
    d->flutter_v2_phase += d->flutter_v2_inc;
    if (d->flutter_v1_phase > 2.0f * APHEX_DRIFT_PI) d->flutter_v1_phase -= 2.0f * APHEX_DRIFT_PI;
    if (d->flutter_v2_phase > 2.0f * APHEX_DRIFT_PI) d->flutter_v2_phase -= 2.0f * APHEX_DRIFT_PI;

    d->interp_t = 0.0f;
}

/* Per-sample tick: advance the inter-block linear ramp by 1/BLOCK each sample.
 * Call this exactly once per audio sample inside the render loop. */
static inline void drift_sample_tick(drift_t *d) {
    d->interp_t += d->interp_dt;
    if (d->interp_t > 1.0f) d->interp_t = 1.0f;
}

/* Linear interp between block boundaries — keeps slow drift glitch-free at SR */
static inline float drift_lerp_(float prev, float curr, float t) {
    return prev + (curr - prev) * t;
}

/* === Per-sample query functions ===
 * Call after drift_sample_tick(). Return values are MODULATION OFFSETS to be
 * added to the corresponding base value (cents for pitch, normalized for cutoff,
 * additive linear gain for VCA).
 */

static inline float drift_pitch_v1(const drift_t *d) {
    float slow = drift_lerp_(d->slow_v1_prev, d->slow_v1, d->interp_t);
    /* Mid-rate flutter (sin) modulated by drift_amount, ~1.5 cents at full */
    float flutter = sinf(d->flutter_v1_phase) * 1.5f * d->amount * d->v1_drift_scale;
    /* Per-note micro-detune scales with drift_amount — at amount=0 the
     * unit is "perfectly calibrated", at amount=1 every note is audibly
     * mistuned. Fixed offset always present (it's hardware tolerance). */
    float note = d->note_v1_cents * d->amount * d->v1_drift_scale;
    return d->fixed_v1_cents + slow + flutter + note;
}

static inline float drift_pitch_v2(const drift_t *d) {
    float slow = drift_lerp_(d->slow_v2_prev, d->slow_v2, d->interp_t);
    float flutter = sinf(d->flutter_v2_phase) * 1.5f * d->amount * d->v2_drift_scale;
    float note   = d->note_v2_cents * d->amount * d->v2_drift_scale;
    return d->fixed_v2_cents + slow + flutter + note;
}

static inline float drift_pitch_sub(const drift_t *d) {
    float slow = drift_lerp_(d->slow_sub_prev, d->slow_sub, d->interp_t);
    return d->fixed_sub_cents + slow;
}

static inline float drift_lpf(const drift_t *d) {
    float slow = drift_lerp_(d->slow_lpf_prev, d->slow_lpf, d->interp_t);
    return d->fixed_lpf_offset + slow;
}

static inline float drift_hpf(const drift_t *d) {
    float slow = drift_lerp_(d->slow_hpf_prev, d->slow_hpf, d->interp_t);
    return d->fixed_hpf_offset + slow;
}

static inline float drift_vca(const drift_t *d) {
    return drift_lerp_(d->slow_vca_prev, d->slow_vca, d->interp_t);
}

/* === Phase jitter — per-sample broadband noise on VCO phase ===
 * Returns a small random phase delta in normalized phase units (one cycle = 1.0).
 * Models thermal noise in the integrator caps of the 2SC1685S exp converter
 * pair on the real MS-20. At normal drift levels it's an almost-subliminal
 * "dust" on the waveform; at high drift + high filter resonance it becomes
 * the gritty, alive character that distinguishes analog from digital.
 *
 * Intensity 8e-5: at drift_amount=1 with VCO at 440 Hz (dt ≈ 0.01), the
 * jitter is ±0.8% of dt — well below pitch perception but enough to
 * dither integrator state and prevent the "frozen-in-time" digital feel. */
static inline float drift_phase_jitter(drift_t *d) {
    /* Mutable: this advances the local RNG. Called per-sample, so cost matters —
     * one LCG step plus one float convert is ~2 ns on the Move's CPU. */
    return drift_rand_b(&d->rng) * 8e-5f * d->amount;
}

/* === Envelope time scale (per-instance personality, fixed at init) ===
 * Capacitor tolerance in the EG timing circuit gave each MS-20 slightly
 * different attack/decay/release times — typically ±2.5% across production.
 * Apply by MULTIPLYING user-set envelope times: `scaled = user * env_time_scale`.
 * Two units side-by-side with identical preset will sound subtly different. */
static inline float drift_env_time_scale(const drift_t *d) {
    return d->env_time_scale;
}

#endif /* APHEX_DRIFT_H */
