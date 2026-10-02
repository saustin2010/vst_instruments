/* Minimal WAV reader — build path only, never on the audio thread.
 *
 * Reads PCM 16/24/32-bit and float32, first channel only (wavetables are
 * mono; for stereo files channel 0 is the table). Walks RIFF chunks and
 * captures the Serum "clm " chunk ("<!>2048 ...") for the frame size —
 * the same detection gin's getWavetableSize does.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>
#include <vector>

namespace tb {

struct WavData {
    std::vector<float> samples;   /* channel 0 */
    float sampleRate = 0.0f;
    int   clmFrameSize = 0;       /* 0 = no clm chunk */
};

inline bool wavLoad(const char *path, WavData &out)
{
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;

    auto rd32 = [&](uint32_t &v) { return std::fread(&v, 4, 1, f) == 1; };

    uint32_t riff = 0, size = 0, wave = 0;
    if (!rd32(riff) || !rd32(size) || !rd32(wave) ||
        riff != 0x46464952u /*RIFF*/ || wave != 0x45564157u /*WAVE*/) {
        std::fclose(f);
        return false;
    }

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    bool haveFmt = false;

    out = WavData();

    uint32_t id = 0, len = 0;
    while (rd32(id) && rd32(len)) {
        long next = std::ftell(f) + (long) len + (len & 1); /* chunks pad to even */

        if (id == 0x20746d66u) {                            /* "fmt " */
            uint8_t buf[40];   /* 16-byte core + the EXTENSIBLE extension */
            /* Read the EXTENSION too, not just the 16-byte core: a file
             * written as WAVE_FORMAT_EXTENSIBLE (0xFFFE) carries its real
             * format in the SubFormat GUID at offset 24, and every tool that
             * writes multichannel or >16-bit through the Windows API produces
             * one -- Audacity's default export among them. Judging those by
             * the tag alone rejected the whole file. */
            const uint32_t want = len >= 40 ? 40u : 16u;
            if (len >= 16 && std::fread(buf, want, 1, f) == 1) {
                std::memcpy(&fmt, buf + 0, 2);
                std::memcpy(&channels, buf + 2, 2);
                std::memcpy(&rate, buf + 4, 4);
                std::memcpy(&bits, buf + 14, 2);
                if (fmt == 0xFFFEu && want == 40u)
                    std::memcpy(&fmt, buf + 24, 2);     /* SubFormat GUID */
                haveFmt = channels >= 1;
            }
        } else if (id == 0x206d6c63u) {                     /* "clm " (Serum) */
            std::string s((size_t) len, 0);
            if (len && std::fread(&s[0], len, 1, f) == 1 && s.rfind("<!>", 0) == 0)
                out.clmFrameSize = std::atoi(s.c_str() + 3);
        } else if (id == 0x61746164u && haveFmt) {          /* "data" */
            const int bytesPer = bits / 8;
            if ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
                (fmt == 3 && (bits == 32 || bits == 64))) {
                const uint32_t frames = len / (uint32_t) (bytesPer * channels);
                out.samples.resize(frames);
                std::vector<uint8_t> raw((size_t) bytesPer * channels);
                for (uint32_t i = 0; i < frames; i++) {
                    if (std::fread(raw.data(), raw.size(), 1, f) != 1) break;
                    const uint8_t *p = raw.data();          /* channel 0 */
                    float v = 0.0f;
                    if (fmt == 3 && bits == 64) {
                        double d; std::memcpy(&d, raw.data(), 8);
                        v = (float) d;
                    } else if (fmt == 3) {
                        std::memcpy(&v, p, 4);
                    } else if (bits == 8) {
                        /* 8-bit PCM is UNSIGNED, centred on 128 */
                        v = ((float) raw[0] - 128.0f) / 128.0f;
                    } else if (bits == 16) {
                        int16_t s16; std::memcpy(&s16, p, 2);
                        v = (float) s16 / 32768.0f;
                    } else if (bits == 24) {
                        int32_t s32 = (int32_t) ((uint32_t) p[0] << 8 |
                                                 (uint32_t) p[1] << 16 |
                                                 (uint32_t) p[2] << 24) >> 8;
                        v = (float) s32 / 8388608.0f;
                    } else {                                /* PCM 32 */
                        int32_t s32; std::memcpy(&s32, p, 4);
                        v = (float) s32 / 2147483648.0f;
                    }
                    out.samples[i] = v;
                }
            }
        }
        if (std::fseek(f, next, SEEK_SET) != 0) break;
    }
    std::fclose(f);

    out.sampleRate = (float) rate;
    return haveFmt && !out.samples.empty();
}

inline bool wavIsPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

/* Frame-size inference: clm chunk first, then divisibility, largest first.
 *
 * The list used to stop at 256, which quietly excluded every short table.
 * A single cycle of 128 samples divides by nothing in {2048..256}, so it
 * inferred nothing and the caller's 2048 fallback made nFrames 0 -- the load
 * failed and the table was silent. A file whose real cycle is short but whose
 * LENGTH happens to divide by 2048 was worse: it loaded, with each "frame"
 * spanning many real cycles, which is the buzz.
 *
 * Returns 0 when no power-of-two cycle fits at all -- notably Adventure Kid's
 * own AKWF format, which is 600 samples per cycle. That is not a failure and
 * the caller must not treat it as one: see the resampling path in the loader. */
inline int wavInferFrameSizeForLength(int n)
{
    for (int fs : { 2048, 1024, 512, 256, 128, 64, 32 })
        if (n >= fs && n % fs == 0)
            return fs;
    return 0;
}

/* A frame size named in the FILE NAME, or 0.
 *
 * The convention every pack follows: "ADD Low FM 001 2048.wav",
 * "Growl 512.wav", Adventure Kid's ".wt2048". When the author says what the
 * frame is, that beats any guess made from the length -- and it is the only
 * fully reliable way to declare a short frame, since a run of short frames
 * and one long frame holding a high harmonic are the same samples. */
inline int wavFrameSizeFromName(const char *path)
{
    if (!path) return 0;
    const char *base = path;
    for (const char *c = path; *c; c++) if (*c == '/') base = c + 1;
    const char *dot = nullptr;
    for (const char *c = base; *c; c++) if (*c == '.') dot = c;
    const char *end = dot ? dot : base + std::strlen(base);
    /* walk back over the trailing digits */
    const char *d = end;
    while (d > base && d[-1] >= '0' && d[-1] <= '9') d--;
    if (d == end) return 0;
    /* It must be a STANDALONE token. Not the tail of a word: "float32.wav"
     * ends in a legal frame size and means nothing of the kind, and neither
     * does "AKWP0042". (The .wtNNNN extension declares its size through the
     * scanner, not through this.) */
    if (d > base && d[-1] != ' ' && d[-1] != '_' && d[-1] != '-')
        return 0;
    int v = 0;
    for (const char *c = d; c < end && v <= 8192; c++) v = v * 10 + (*c - '0');
    if (v < 32 || v > 4096 || !wavIsPow2(v)) return 0;
    return v;
}

/* HARMONIC ANALYSIS of the frame size: is this frame secretly two cycles?
 *
 * wavInferFrameSize takes the largest power of two that divides the file,
 * which is right for the 2048-frame tables every tool exports. But 16 frames
 * of 256 is 4096 samples, and 2048 divides that too -- so it read as 2 frames
 * of 2048, each holding 8 cycles, and played EIGHT TIMES too high. That is
 * what "shorter wavetables sound wrong" is.
 *
 * The test is exact, not a guess. Split a frame into halves a and b:
 *
 *     (a - b) / 2  is precisely the ODD-harmonic part of the waveform
 *     (a + b) / 2  is precisely the EVEN-harmonic part
 *
 * because an odd harmonic inverts across half a period and an even one
 * repeats. So a frame whose real period is half its length has ZERO odd
 * harmonics -- not "similar halves", zero -- and the ratio of odd energy to
 * total answers the question directly.
 *
 * EVERY frame must pass, and that is what protects real tables. An additive
 * wavetable that morphs from a full harmonic series to even harmonics only
 * (DigiAdd07 does exactly this) ends with frames that are two identical
 * half-cycles, legitimately. Its early frames still carry odd harmonics, so
 * the table as a whole is not halved -- while a table that really is short
 * frames has no odd energy in ANY frame.
 *
 * Halving repeats: a 2048 frame holding 8 cycles passes three times down to
 * 256.
 */
inline int wavRefineFrameSize(const std::vector<float> &s, int frameSize)
{
    const int n = (int) s.size();
    if (frameSize < 64 || n < frameSize) return frameSize;

    int f = frameSize;
    while (f >= 64 && (f & 1) == 0 && n / f >= 1) {
        const int h = f / 2;
        const int frames = n / f;
        double worst = 0.0;
        int judged = 0;
        for (int k = 0; k < frames; k++) {
            const float *a = &s[(size_t) k * f];
            const float *b = a + h;
            double odd = 0.0, even = 0.0;
            for (int i = 0; i < h; i++) {
                const double d = ((double) a[i] - b[i]) * 0.5;
                const double m = ((double) a[i] + b[i]) * 0.5;
                odd += d * d;
                even += m * m;
            }
            if (odd + even < 1e-12) continue;       /* a silent frame says nothing */
            const double ratio = odd / (odd + even);
            if (ratio > worst) worst = ratio;
            judged++;
        }
        /*
         * The threshold sits in a wide, measured gap.
         *
         * Tables whose frames really are shorter than they look score 0.011
         * to 0.056 at every step down to the true size (a 128-frame table
         * measured 0.056 / 0.043 / 0.026 / 0.056 across four halvings). Real
         * tables score 0.35 to 1.00 at their own frame size -- 1.00 exactly
         * when the frame is genuinely one cycle, and 0.48 for DigiAdd07,
         * whose later frames legitimately hold only even harmonics.
         *
         * 0.15 is roughly 3x above the highest false frame and 3x below the
         * lowest real one. It is not zero because sub-cycles MORPH: eight
         * neighbouring cycles inside one apparent frame differ slightly, and
         * that difference is odd-harmonic energy. Demanding zero only caught
         * tables whose sub-cycles were identical.
         *
         * The chain stops itself: halving continues while the answer stays
         * small and stops at the first frame size that is genuinely one
         * cycle, which is the size wanted.
         */
        if (!judged || worst > 0.15) break;
        f = h;
    }
    return f;
}

inline int wavInferFrameSize(const WavData &w)
{
    if (w.clmFrameSize >= 32 && w.clmFrameSize <= 4096 && wavIsPow2(w.clmFrameSize))
        return w.clmFrameSize;
    return wavInferFrameSizeForLength((int) w.samples.size());
}

/* Find the shortest repeating unit in a SHORT file.
 *
 * Divisibility cannot tell one cycle from eight: a 256-sample file is equally
 * "one frame of 256" and "eight frames of 32", and taking the whole file as a
 * single cycle makes those eight cycles repeat inside every oscillator period
 * -- audible as the table looping during playback. Correlating the candidate
 * segments settles it from the signal itself.
 *
 * Deliberately only consulted for short files. A 2048-sample cycle whose
 * CONTENT happens to be eight identical sub-cycles is a legitimate single
 * cycle, and the long-file convention (2048) already reads it correctly;
 * running detection there would drop it an octave and change existing sounds.
 *
 * Returns 0 when nothing repeats convincingly, which is the answer for a
 * genuine single cycle. */
inline int wavDetectCycle(const std::vector<float> &x, int n)
{
    if (n < 64) return 0;
    for (int p : { 32, 64, 128, 256, 512, 1024 }) {
        if (p * 2 > n || n % p != 0) continue;
        /* Correlate ADJACENT segments and take the worst pair.
         *
         * Comparing every segment against the FIRST was tried and is wrong for
         * the tables this exists to catch: a morphing wavetable drifts in
         * amplitude across its frames, and lumping all the pairs into one sum
         * let that drift drag the coefficient below the threshold, so an
         * eight-frame table was read as two. A per-pair coefficient is
         * scale-invariant -- a frame and the same frame at 95% correlate 1.0 --
         * so only a real change of SHAPE counts against it. */
        const int segs = n / p;
        double worst = 1.0;
        for (int s = 1; s < segs; s++) {
            double num = 0.0, e0 = 0.0, e1 = 0.0;
            for (int i = 0; i < p; i++) {
                double a = x[(size_t) ((s - 1) * p + i)], b = x[(size_t) (s * p + i)];
                num += a * b; e0 += a * a; e1 += b * b;
            }
            if (e0 <= 1e-12 || e1 <= 1e-12) { worst = 0.0; break; }
            worst = std::min(worst, num / std::sqrt(e0 * e1));
        }
        if (worst > 0.95) return p;       /* smallest period wins */
    }
    return 0;
}

/* Resample every frame to `target` samples, linearly.
 *
 * wtBuild needs a power-of-two frame of at least 32; a 600-sample AKWF cycle
 * is neither. Stretching each cycle to a power of two is what makes those
 * playable at all, and costs nothing audible -- wtBuild band-limits the result
 * afterwards regardless. */
inline std::vector<float> wavResampleFrames(const std::vector<float> &in,
                                            int nFrames, int frameSize,
                                            int target)
{
    std::vector<float> out((size_t) nFrames * (size_t) target);
    for (int f = 0; f < nFrames; f++) {
        const float *src = in.data() + (size_t) f * (size_t) frameSize;
        float *dst = out.data() + (size_t) f * (size_t) target;
        for (int i = 0; i < target; i++) {
            float pos = (float) i * (float) frameSize / (float) target;
            int   i0  = (int) pos;
            float t   = pos - (float) i0;
            int   i1  = (i0 + 1) % frameSize;      /* wrap: it is a cycle */
            if (i0 >= frameSize) i0 = frameSize - 1;
            dst[i] = src[i0] + t * (src[i1] - src[i0]);
        }
    }
    return out;
}

} // namespace tb
