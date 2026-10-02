#include "wavetable.h"
#include "fft.h"

#include <ctime>

namespace tb {

static double nowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1e3 + (double) ts.tv_nsec / 1e6;
}

static int nextPow2(int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

static bool isPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

bool wtBuild(Wavetable &out, const float *samples, int nFrames, int frameSize,
             float playbackRate, float fileRate, int notesPerTable,
             WtBuildStats *stats)
{
    if (!samples || nFrames <= 0 || !isPow2(frameSize) || frameSize < 32)
        return false;

    const double t0 = nowMs();

    out.clear();
    out.frameSize = frameSize;
    out.frames.resize((size_t) nFrames);

    /* One cycle per frame: its fundamental at the file's rate. */
    const float baseFreq = fileRate / (float) frameSize;

    std::vector<Cpx> spec((size_t) frameSize);   /* forward spectrum of a frame */
    std::vector<Cpx> small((size_t) frameSize);  /* scratch for decimated IFFT  */

    for (int fi = 0; fi < nFrames; fi++) {
        const float *frame = samples + (size_t) fi * (size_t) frameSize;
        FrameTable &ft = out.frames[(size_t) fi];
        ft.notesPerTable = notesPerTable;

        /* Forward FFT once per frame. */
        for (int i = 0; i < frameSize; i++)
            spec[(size_t) i] = { frame[i], 0.0f };
        fftRadix2(spec.data(), frameSize, false);

        /* Same level walk as gin: one table every notesPerTable semitones. */
        for (float note = (float) notesPerTable + 0.5f; note < 127.0f;
             note += (float) notesPerTable) {

            const float noteFreq = midiNoteToHz(note);
            MipLevel lvl;

            /* Highest harmonic that stays below playback Nyquist when this
             * frame is transposed up to `note` (gin's zeroing condition). */
            int kmax;
            if (noteFreq < baseFreq) {
                kmax = frameSize / 2 - 1;        /* nothing to remove */
            } else {
                const float ratio = noteFreq / baseFreq;
                kmax = (int) ((playbackRate * 0.5f) /
                              (ratio * fileRate / (float) frameSize));
                if (kmax > frameSize / 2 - 1) kmax = frameSize / 2 - 1;
                if (kmax < 1) kmax = 1;          /* always keep the fundamental */
            }

            if (kmax >= frameSize / 2 - 1) {
                /* Full-bandwidth level: copy the raw frame. */
                lvl.size = frameSize;
                lvl.data.assign(frame, frame + frameSize);
                lvl.data.push_back(frame[0]);    /* wrap sample */
            } else {
                /* DECIMATED level — the change vs gin. The kept spectrum fits
                 * a smaller table; a small inverse FFT reconstructs it exactly
                 * (all discarded bins are zero by construction). */
                int S = nextPow2(2 * (kmax + 1));
                if (S < 32) S = 32;
                if (S > frameSize) S = frameSize;

                const float scale = 1.0f / (float) frameSize;
                for (int i = 0; i < S; i++) small[(size_t) i] = { 0.0f, 0.0f };
                small[0] = { spec[0].re * scale, spec[0].im * scale };
                for (int k = 1; k <= kmax; k++) {
                    small[(size_t) k] = { spec[(size_t) k].re * scale,
                                          spec[(size_t) k].im * scale };
                    /* conjugate partner, keeps the IFFT exactly real */
                    small[(size_t) (S - k)] = { spec[(size_t) (frameSize - k)].re * scale,
                                                spec[(size_t) (frameSize - k)].im * scale };
                }
                fftRadix2(small.data(), S, true);

                lvl.size = S;
                lvl.data.resize((size_t) S + 1);
                for (int i = 0; i < S; i++)
                    lvl.data[(size_t) i] = small[(size_t) i].re;
                lvl.data[(size_t) S] = lvl.data[0];
            }

            ft.levels.push_back(std::move(lvl));
        }
    }

    if (stats) {
        stats->frames = nFrames;
        stats->levelsPerFrame = (int) out.frames[0].levels.size();
        stats->bytes = out.bytes();
        stats->buildMs = nowMs() - t0;
    }
    return true;
}

} // namespace tb
