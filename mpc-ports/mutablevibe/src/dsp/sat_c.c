/*
 * sat_c.c — Tape-style saturation, dry/wet
 *
 * Einzel-Knob (0..1):
 *   wet   = param                  (linearer Blend)
 *   drive = 1 + param² × 15       (exponentiell, bei 0.75 → ~9.4×, nie Rechteck)
 *
 * Waveshaper:
 *   tanh(drive·x + 0.08·x·|x|) / norm
 *   → x·|x| statt x² für symmetrische Asymmetrie (gerade Harmonische, kein DC-Offset)
 *   → klingt röhrenartig, nicht quadratisch
 *
 * Post-LP @ 8 kHz auf den Wet-Anteil: nimmt Schärfe der hinzugefügten Obertöne weg
 */

#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float samplerate;
    float amount;
    float lpL, lpR;
    float lpC;
} Saturator;

void* sat_create(float samplerate)
{
    Saturator* s = (Saturator*)calloc(1, sizeof(Saturator));
    if (!s) return NULL;
    s->samplerate = samplerate;
    s->amount     = 0.0f;
    float fc = 8000.0f;
    s->lpC = 1.0f - expf(-2.0f * (float)M_PI * fc / samplerate);
    return s;
}

void sat_free(void* ptr) { free(ptr); }

void sat_set_amount(void* ptr, float v)
{
    if (!ptr) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    ((Saturator*)ptr)->amount = v;
}

void sat_process(void* ptr, float* buf, int frames)
{
    if (!ptr) return;
    Saturator* s = (Saturator*)ptr;

    float param = s->amount;
    if (param < 0.001f) return;   /* fully dry — skip */

    float wet   = param;
    float dry   = 1.0f - wet;
    /* Kubische Kurve: unterer Bereich subtil, oberer Bereich kräftig.
     * Referenzpegel 0.3 (~-10 dBFS): Lautstärke bleibt gleich bei jedem Drive. */
    float drive = 1.0f + param * param * param * 7.0f;   /* 1× → 8× kubisch */
    const float kRef = 0.3f;
    float norm = kRef / tanhf(drive * kRef);

    float lpC   = s->lpC;
    float lpL   = s->lpL;
    float lpR   = s->lpR;

    for (int i = 0; i < frames; ++i) {
        float xL = buf[i * 2];
        float xR = buf[i * 2 + 1];

        /* Waveshaper — x·|x| gibt gerade Harmonische ohne DC-Offset */
        float sL = tanhf(drive * xL + 0.08f * xL * fabsf(xL)) * norm;
        float sR = tanhf(drive * xR + 0.08f * xR * fabsf(xR)) * norm;

        /* Post-LP: erwärmt den gesättigten Anteil */
        lpL += lpC * (sL - lpL);
        lpR += lpC * (sR - lpR);

        /* Dry/wet */
        buf[i * 2]     = dry * xL + wet * lpL;
        buf[i * 2 + 1] = dry * xR + wet * lpR;
    }

    s->lpL = lpL;
    s->lpR = lpR;
}
