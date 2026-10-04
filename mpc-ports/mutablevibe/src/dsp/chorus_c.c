/*
 * chorus_c.c — Juno-style stereo BBD chorus
 *
 * L channel: center_delay + depth * tri_lfo(phase)
 * R channel: center_delay - depth * tri_lfo(phase)  (inverted LFO → stereo spread)
 * Triangle LFO, center delay 3.5 ms, max depth ±1.5 ms
 */

#include <stdlib.h>
#include <string.h>

#define CHORUS_BUF_SIZE 8192   /* power of 2, ~170 ms @ 48 kHz */
#define CHORUS_BUF_MASK (CHORUS_BUF_SIZE - 1)

typedef struct {
    float samplerate;
    float rate;     /* Hz  0.1 .. 3.0 */
    float depth;    /* 0 .. 1         */
    float wet;      /* 0 .. 1         */
    float phase;    /* 0 .. 1, triangle LFO */
    float bufL[CHORUS_BUF_SIZE];
    float bufR[CHORUS_BUF_SIZE];
    int   writeIdx;
} Chorus;

void *chorus_create(float samplerate) {
    Chorus *c = calloc(1, sizeof(Chorus));
    if (!c) return NULL;
    c->samplerate = samplerate;
    c->rate  = 0.51f;
    c->depth = 0.5f;
    c->wet   = 0.0f;
    return c;
}

void chorus_free (void *ptr) { free(ptr); }

void chorus_set_rate (void *ptr, float hz) { ((Chorus*)ptr)->rate  = hz < 0.01f ? 0.01f : hz; }
void chorus_set_depth(void *ptr, float d)  { ((Chorus*)ptr)->depth = d < 0.f ? 0.f : (d > 1.f ? 1.f : d); }
void chorus_set_wet  (void *ptr, float w)  { ((Chorus*)ptr)->wet   = w < 0.f ? 0.f : (w > 1.f ? 1.f : w); }

static float read_delay(const float *buf, int writeIdx, float delaySamples)
{
    float readPos = (float)writeIdx - delaySamples;
    if (readPos < 0.f) readPos += (float)CHORUS_BUF_SIZE;
    int   i0   = (int)readPos & CHORUS_BUF_MASK;
    int   i1   = (i0 + 1)    & CHORUS_BUF_MASK;
    float frac = readPos - (float)(int)readPos;
    return buf[i0] + frac * (buf[i1] - buf[i0]);
}

void chorus_process(void *ptr, float *in, float *out, int frames)
{
    Chorus *c = (Chorus*)ptr;
    const float wet  = c->wet;
    const float dry  = 1.0f - wet;
    const float ctr  = 0.0035f * c->samplerate;          /* center: 3.5 ms */
    const float dep  = 0.0015f * c->samplerate * c->depth; /* max depth: 1.5 ms */
    const float inc  = c->rate / c->samplerate;

    for (int i = 0; i < frames; i++) {
        float xL = in[i * 2];
        float xR = in[i * 2 + 1];

        c->bufL[c->writeIdx] = xL;
        c->bufR[c->writeIdx] = xR;

        /* Triangle LFO: ramps 0 → +1 → 0 → -1 → 0 */
        float p = c->phase;
        float lfo;
        if      (p < 0.25f) lfo =  4.f * p;
        else if (p < 0.75f) lfo =  2.f - 4.f * p;
        else                lfo =  4.f * p - 4.f;

        float delL = ctr + dep * lfo;
        float delR = ctr - dep * lfo;

        float choL = read_delay(c->bufL, c->writeIdx, delL);
        float choR = read_delay(c->bufR, c->writeIdx, delR);

        out[i * 2]     = dry * xL + wet * choL;
        out[i * 2 + 1] = dry * xR + wet * choR;

        c->writeIdx = (c->writeIdx + 1) & CHORUS_BUF_MASK;

        c->phase += inc;
        if (c->phase >= 1.f) c->phase -= 1.f;
    }
}

void chorus_flush(void *ptr)
{
    Chorus *c = (Chorus*)ptr;
    memset(c->bufL, 0, sizeof(c->bufL));
    memset(c->bufR, 0, sizeof(c->bufR));
    c->writeIdx = 0;
    c->phase    = 0.f;
}
