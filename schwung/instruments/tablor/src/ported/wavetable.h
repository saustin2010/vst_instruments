/* Band-limited wavetable with DECIMATED mip levels.
 *
 * Ported from gin (BSD-3, (c) Roland Rabien) — gin_bandlimitedlookuptable /
 * loadWavetables — with one deliberate change: gin stores every mip level at
 * full frame size (21 x 2049 floats per frame = 43 MB for a 256-frame Serum
 * table); we store each level at the smallest power of two that holds its
 * kept harmonics (min 32). Same audible content, ~12.7 MB, ~3.5x less FFT
 * work on load. See PLAN.md 5.4.
 *
 * Level indexing matches gin exactly (tableIndexForNote), so the oscillator
 * port behaves identically.
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tb {

inline float midiNoteToHz(float note)
{
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

struct MipLevel {
    std::vector<float> data;   /* size + 1 samples: wraps for lerp w/o mod */
    int size = 0;
};

/* One wavetable frame (one cycle), band-limited into mip levels — gin's
 * BandLimitedLookupTable, decimated. */
class FrameTable {
public:
    std::vector<MipLevel> levels;
    int notesPerTable = 6;

    inline int levelForNote(float note) const
    {
        int i = (int) ((note - 0.5f) / (float) notesPerTable);
        if (i < 0) i = 0;
        int mx = (int) levels.size() - 1;
        return i > mx ? mx : i;
    }

    inline float processLinear(float note, float phase) const
    {
        const MipLevel &t = levels[(size_t) levelForNote(note)];
        float fidx = phase * (float) t.size;
        int   i    = (int) fidx;
        float f    = fidx - (float) i;
        const float *d = t.data.data();
        return d[i] + f * (d[i + 1] - d[i]);
    }

    size_t bytes() const
    {
        size_t b = 0;
        for (auto &l : levels) b += l.data.size() * sizeof(float);
        return b;
    }
};

/* A full wavetable: N frames, morphed by the POS parameter — gin's Wavetable. */
class Wavetable {
public:
    std::vector<FrameTable> frames;
    int frameSize = 0;

    int size() const { return (int) frames.size(); }
    const FrameTable *frame(int i) const
    {
        if (i < 0 || i >= (int) frames.size()) return nullptr;
        return &frames[(size_t) i];
    }
    size_t bytes() const
    {
        size_t b = 0;
        for (auto &f : frames) b += f.bytes();
        return b;
    }
    void clear() { frames.clear(); frameSize = 0; }
};

struct WtBuildStats {
    int    frames = 0;
    int    levelsPerFrame = 0;
    size_t bytes = 0;
    double buildMs = 0.0;
};

/* Build a Wavetable from a mono frame stack (nFrames x frameSize samples).
 * frameSize must be a power of two. fileRate is the WAV's sample rate;
 * playbackRate the engine's (44100 on Move). notesPerTable=6 matches gin. */
bool wtBuild(Wavetable &out, const float *samples, int nFrames, int frameSize,
             float playbackRate, float fileRate, int notesPerTable = 6,
             WtBuildStats *stats = nullptr);

} // namespace tb
