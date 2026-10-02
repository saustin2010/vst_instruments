/* Tablor engine — voice allocation, poly/mono, glide, MIDI, master out.
 * Owns the wavetables, the analog tables and the 8 voices.
 */
#pragma once

#include "voice.h"

#include <atomic>
#include <memory>
#include <vector>

namespace tb {

class Engine {
public:
    Engine();

    /* Pots are written by set_param (UI thread) and read by the audio
     * thread; aligned float stores are atomic on aarch64. */
    float pots[TB_PARAM_COUNT] = {};

    /* Refresh mod slots from the pot array (call after any m*_ change). */

    void setHostBpm(float bpm) { hostBpm = bpm; }

    void onMidi(const uint8_t *msg, int len);
    void renderBlock(int16_t *outInterleavedLr, int frames);

    /* Wavetable slots — phase 4 swaps these from the loader thread. */
    void setTable(int osc, std::shared_ptr<Wavetable> t)
    {
        if (osc == 0) std::atomic_store(&table1, std::move(t));
        else          std::atomic_store(&table2, std::move(t));
    }

    void allNotesOff(bool allowTail);

private:
    void noteOn(int note, float vel);
    void noteOff(int note);
    Voice *findVoiceToSteal();
    VoiceContext makeContext(const std::shared_ptr<Wavetable> &t1,
                             const std::shared_ptr<Wavetable> &t2);

    AnalogTables analog { 44100.0 };
    std::vector<Voice> voices;
    std::shared_ptr<Wavetable> table1, table2;

    /* mono mode state: held-note stack, last-note priority */
    static constexpr int kStackMax = 32;
    int  monoStack[kStackMax];
    int  monoStackLen = 0;

    float lastPlayedNote = -1.0f;
    uint32_t serialCounter = 0;

    /* controller state */
    float modWheel = 0.0f, aftertouch = 0.0f;
    float pitchBendNorm = 0.0f;
    float hostBpm = 120.0f;
    bool  sustainDown = false;
    bool  sustained[128] = {};
};

/* Built-in "Init" wavetable: 64-frame PWM morph, generated in memory so the
 * synth makes sound with no files installed. Band-limiting happens in
 * wtBuild, so the naive source waveform is fine. */
std::shared_ptr<Wavetable> makeInitTable();

} // namespace tb
