/* Background wavetable loader — the machinery behind glitch-free switching.
 *
 * set_param posts a request; this worker thread decodes (WAV or .wtNNNN
 * FLAC), infers the frame size, normalizes, builds the decimated mip
 * pyramid, and hands the finished shared_ptr<Wavetable> to the engine via
 * an atomic swap. The audio thread never sees a partial table and never
 * touches the filesystem. In-flight notes finish on the old table (the
 * engine's render block pins it with a shared_ptr).
 *
 * A small cache means WT1 and WT2 picking the same table share one build.
 */
#pragma once

#include "scanner.h"
#include "../engine.h"
#include "../../ported/wav.h"
#include "../rt.h"

#include <functional>

#include <condition_variable>
#include <map>
#include <mutex>

namespace tb {

/* implemented in loader.cpp (dr_flac lives there) */
bool wtDecodeFlac(const char *path, std::vector<float> &samples,
                  float &sampleRate);

class WtLoader {
public:
    /* ~24 MB per oscillator: frames are subsampled to fit if a pathological
     * table would blow past it (PLAN 5.4). ~12,384 samples/frame decimated. */
    static constexpr size_t kMaxBytesPerTable = 24u * 1024 * 1024;
    static constexpr int    kBytesPerFrame    = 12384 * 4;

    explicit WtLoader(Engine &engine_) : engine(engine_) {}

    /* Start the worker. Called once from create_instance — which is itself
     * on the audio callback, so this is the ONE thread we ever spawn, and it
     * must not be born with the callback's scheduling (see rt.h). */
    void start()
    {
        std::lock_guard<std::mutex> lk(m);
        if (started) return;
        started = rtStartWorker(&worker, &trampoline, this, "tablor-wtload") == 0;
    }

    /* Queue arbitrary off-thread work (file I/O, allocation, scans).
     * Safe to call from set_param / create_instance: it only appends. */
    void post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lk(m);
            jobs.push_back(std::move(job));
        }
        cv.notify_all();
    }

    ~WtLoader() { stop(); }

    /* Quit and join the worker (after the job in hand). MPC port: destroy_instance calls this BEFORE deleting the
     * instance -- the members declared after the loader (sel, presets) are destroyed first, while the worker could
     * still be publishing into them (ASan heap-use-after-free on a quick insert/remove, 2026-10-01). */
    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(m);
            quit = true;
        }
        cv.notify_all();
        if (started)
            pthread_join(worker, nullptr);
        started = false;
    }

    /* Called on the WORKER with each table as it finishes, before the engine
     * takes ownership. The plugin uses it to build the display digest off the
     * real samples -- the one place that data is in hand on a thread allowed
     * to walk it. */
    std::function<void(int, const Wavetable &)> onTable;

    /* UI thread. osc: 0/1. entry: a copy of the scanner row. */
    void requestLoad(int osc, const WtEntry &entry)
    {
        {
            std::lock_guard<std::mutex> lk(m);
            pending[osc & 1] = entry;
            hasPending[osc & 1] = true;
        }
        cv.notify_all();
    }

    bool busy() const
    {
        std::lock_guard<std::mutex> lk(m);
        return hasPending[0] || hasPending[1] || working || !jobs.empty();
    }

private:
    static void *trampoline(void *self)
    {
        static_cast<WtLoader *>(self)->run();
        return nullptr;
    }

    void run()
    {
        for (;;) {
            WtEntry entry;
            int osc = -1;
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] {
                    return quit || hasPending[0] || hasPending[1] || !jobs.empty();
                });
                if (quit) return;
                if (!jobs.empty()) {            /* plain jobs first (init) */
                    job = std::move(jobs.front());
                    jobs.erase(jobs.begin());
                }
                /* No settle delay: a table change loads the instant the pot
                 * moves. Deferring it to coalesce a fast spin was tried and
                 * rejected -- the lag is the whole feel of the control. */
                for (int i = 0; i < 2 && !job; i++) {
                    if (hasPending[i]) {
                        entry = pending[i];
                        hasPending[i] = false;
                        osc = i;
                        break;
                    }
                }
                working = true;
            }

            if (job) {
                job();
                std::lock_guard<std::mutex> lk(m);
                working = false;
                continue;
            }
            if (osc < 0) {
                std::lock_guard<std::mutex> lk(m);
                working = false;
                continue;
            }

            std::shared_ptr<Wavetable> table = load(entry);
            if (table) {
                if (onTable) onTable(osc, *table);
                engine.setTable(osc, std::move(table));
            }

            {
                std::lock_guard<std::mutex> lk(m);
                working = false;
            }
        }
    }

    std::shared_ptr<Wavetable> load(const WtEntry &entry)
    {
        if (entry.path.empty())
            return makeInitTable();

        /* cache hit? (weak: a table both oscs use is built once) */
        {
            std::lock_guard<std::mutex> lk(cacheM);
            auto it = cache.find(entry.path);
            if (it != cache.end())
                if (auto sp = it->second.lock())
                    return sp;
        }

        std::vector<float> samples;
        float rate = 44100.0f;
        int frameSize = 0;

        if (entry.flac || entry.flacFrameSize > 0) {
            if (!wtDecodeFlac(entry.path.c_str(), samples, rate))
                return nullptr;
            frameSize = entry.flacFrameSize;       /* 0 = infer it below */
            if (!frameSize) {
                frameSize = wavInferFrameSizeForLength((int) samples.size());
                if (!frameSize) frameSize = (int) samples.size();  /* one cycle */
            }
        } else {
            WavData w;
            if (!wavLoad(entry.path.c_str(), w))
                return nullptr;
            rate = w.sampleRate > 0 ? w.sampleRate : 44100.0f;

            /* DECIDE THE FRAME SIZE BEFORE MOVING THE SAMPLES.
             *
             * This read `wavInferFrameSize(w)` AFTER `samples = move(w.samples)`,
             * so it asked a moved-from vector for its length, got 0, and fell
             * through to "the whole file is one cycle" -- every .wav without a
             * clm chunk collapsed into a single frame. A Serum or Vital export
             * survived, because clm is a separate field the move does not
             * touch; everything else lost its frames entirely. That is why
             * "only 2048 wavetables work": the ones that worked were the ones
             * carrying clm.
             */
            frameSize = wavFrameSizeFromName(entry.path.c_str());
            if (!frameSize) frameSize = wavInferFrameSize(w);
            /* A short file that repeats is MULTI-FRAME, not one long cycle.
             * Taking the whole thing as a single cycle made its content loop
             * inside every oscillator period. Only for short files -- see
             * wavDetectCycle. */
            if (w.clmFrameSize <= 0 && (int) w.samples.size() <= 2048) {
                int cyc = wavDetectCycle(w.samples, (int) w.samples.size());
                if (cyc > 0) frameSize = cyc;
            }
            /* ...and a frame that is really a run of shorter frames.
             *
             * CONTENT OUTRANKS THE NAME. A name is a claim, and a wrong one
             * is easy to make -- call a 128 table "pad 2048.wav" and the
             * claim is simply false. So the name only sets the starting
             * guess; if the samples PROVE a shorter period, the samples win.
             * Only a clm chunk is exempt, because a tool wrote it. */
            if (w.clmFrameSize <= 0)
                frameSize = wavRefineFrameSize(w.samples, frameSize);

            samples = std::move(w.samples);
            if (!frameSize) {
                /* No power-of-two cycle divides the file. That is the normal
                 * shape of a single-cycle table -- Adventure Kid's own AKWF is
                 * 600 samples -- so treat the whole file as ONE cycle rather
                 * than forcing a 2048 default that would make nFrames 0 and
                 * lose the file silently. Resampled to a power of two below. */
                frameSize = (int) samples.size();
            }
        }

        int nFrames = (int) samples.size() / frameSize;
        if (nFrames < 1) return nullptr;

        /* wtBuild needs a power-of-two frame of at least 32. Anything else --
         * a 600-sample AKWF cycle, a 3-sample oddity -- is stretched to 2048
         * per cycle so it plays instead of being rejected. */
        if (!wavIsPow2(frameSize) || frameSize < 32) {
            const int target = 2048;
            samples = wavResampleFrames(samples, nFrames, frameSize, target);
            frameSize = target;
        }

        /* memory ceiling: subsample frames evenly to fit */
        int maxFrames = (int) (kMaxBytesPerTable / (size_t) kBytesPerFrame);
        if (nFrames > maxFrames) {
            std::vector<float> picked((size_t) maxFrames * (size_t) frameSize);
            for (int i = 0; i < maxFrames; i++) {
                int src = (int) ((long) i * (nFrames - 1) / (maxFrames - 1));
                std::memcpy(&picked[(size_t) i * frameSize],
                            &samples[(size_t) src * frameSize],
                            (size_t) frameSize * sizeof(float));
            }
            samples = std::move(picked);
            nFrames = maxFrames;
        } else {
            samples.resize((size_t) nFrames * (size_t) frameSize);
        }

        /* normalize to 0.9 peak for consistent loudness across libraries */
        float peak = 0.0f;
        for (float v : samples) peak = std::max(peak, std::fabs(v));
        if (peak > 0.0f) {
            float g = 0.9f / peak;
            for (float &v : samples) v *= g;
        }

        auto wt = std::make_shared<Wavetable>();
        if (!wtBuild(*wt, samples.data(), nFrames, frameSize, 44100.0f, rate))
            return nullptr;

        {
            std::lock_guard<std::mutex> lk(cacheM);
            cache[entry.path] = wt;
            /* drop dead cache rows now and then */
            if (cache.size() > 16)
                for (auto it = cache.begin(); it != cache.end();)
                    it = it->second.expired() ? cache.erase(it) : std::next(it);
        }
        return wt;
    }

    Engine &engine;

    mutable std::mutex m;
    std::condition_variable cv;
    pthread_t worker {};
    bool started = false;
    WtEntry pending[2];
    std::vector<std::function<void()>> jobs;
    bool hasPending[2] = { false, false };
    bool working = false;
    bool quit = false;

    std::mutex cacheM;
    std::map<std::string, std::weak_ptr<Wavetable>> cache;
};

} // namespace tb
