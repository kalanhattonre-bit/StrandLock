#pragma once

// The sound loader (PLUGIN-PLAN 1.3, 7.4 decisions 3 and 4): a worker thread that makes starter sounds and
// decodes sound files, and hands each finished sound to the audio thread through
// rt::VersionExchange<engine::SampleData>. The message thread never makes a starter or decodes a file.
//
// - Starters: sawopen is the default sound (D9). The worker keeps the last 4 starters it made, by (id, seed), so
//   a project that uses the current or a recent starter does not make it again (decision 3).
// - Files (decision 4): decoded with AudioFormatManager::registerBasicFormats () on the worker. A file longer than
//   10 minutes loads its first 10 minutes (question 9.3). The SMPL file record is filled (path, size, SHA-256,
//   rate, channels, frames, the loaded part, the display name; the analysis cache empty). A project's file is
//   found again by its path when its size and SHA-256 still match (plan 3); otherwise it is missing, the
//   default starter plays, the report names the file, and the record is kept as it was so a save keeps it.
// - Who asked decides who publishes. A load the user asked for (the menu, Browse, a drop) changes parameters:
//   the root and cents (60 and 0 for a starter, and for a file until M7's Detect) and the Region (the whole
//   sound; for a song, longer than 20 s as sl-ui.js's LONG_FILE, 4 s from 35 % in), as host-visible edits on the
//   message thread (plan 2.4). So the worker hands it over (takePending), and the owner applies the patch and
//   then commits it, so the audio thread never plays the new sound with the old Region. A project load, Undo and
//   a new instance's default change no parameter, so the worker commits those itself, and a state load can
//   wait for it on its own thread (decision 3: the worker never needs the message thread).
// - Newest wins. Each request has a number; the worker takes only the newest one waiting, and a sound whose
//   request is older than the newest request made is never committed.

#include "engine/SampleData.h"
#include "rt/VersionExchange.h"
#include "sounds/StarterSounds.h"
#include "state/StateChunks.h"
#include "core/Inst.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sl::doc
{
    /** Who asked for a sound (see above). */
    enum class LoadOrigin : std::uint8_t { user, project, undo, startup };

    /** The sound as the editor sees it (the waveform strip draws its peaks from left and right): it shares its
        samples with the SampleData the engine plays. Never changed once made. */
    struct SoundView
    {
        std::shared_ptr<const void> storage;   // keeps left and right alive
        const float* left = nullptr;
        const float* right = nullptr;           // the same pointer as left when mono
        std::int64_t frames = 0;
        double sampleRate = 44100.0;
        bool stereo = false;
        std::string name;                       // "Saw that opens up", or the file's name
        bool builtIn = true;
        sounds::StarterId starter = sounds::kDefaultStarter;   // when builtIn
        std::int32_t seed = 1;
        state::SampleRef ref;                   // what the project names (SMPL): a missing file's record stays here
        bool missing = false;                   // the project names a file that was not found; the default starter plays
        bool cut = false;                       // a file longer than 10 minutes: only its first 10 minutes are loaded
        std::uint64_t request = 0;              // the load that made it

        double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (frames) / sampleRate : 0.0; }
    };

    /** One finished load, for the toast. */
    struct LoadReport
    {
        std::uint64_t request = 0;
        LoadOrigin origin = LoadOrigin::user;
        bool ok = false;        // the sound asked for is the one that plays now
        std::string message;    // what the toast says ("" when nothing needs saying)
        std::string name;       // the sound asked for: a starter's name, a file's name (the toast's "Loaded ...")
    };

    /** A load the user asked for, ready: the sound, and the parameters it sets (parameters.json touchedBy
        "soundLoad": rootNote, rootCents, regionStart, regionEnd) with the exact Region edges (plan 2.2). */
    struct PendingSound
    {
        std::uint64_t request = 0;
        std::unique_ptr<engine::SampleData> data;
        std::shared_ptr<const SoundView> view;
        core::InstPatch patch;
        double regionStartExact = 0.0, regionEndExact = 1.0;
        LoadReport report;
    };

    class SampleManager final : private juce::Thread
    {
    public:
        static constexpr int kStarterCacheSize = 4;
        static constexpr double kMaxFileSeconds = 600.0;      // decision 4: a longer file loads its first 10 minutes
        static constexpr double kLongFileSeconds = 20.0;      // sl-ui.js LONG_FILE: a longer file is a song
        static constexpr double kDefaultPartStart = 0.35;     // a song's Region: 4 s from 35 % in (plan 4 M2)
        static constexpr double kDefaultPartSeconds = 4.0;
        static constexpr int kMinFileFrames = 64;             // sl-ui.js loadFile: a shorter file is "empty"
        static constexpr std::size_t kMaxReports = 32;         // reports kept until someone takes them

        /** Starts the worker. */
        explicit SampleManager (rt::VersionExchange<engine::SampleData>& exchange);

        /** Stops the worker (a starter being made finishes first; a file stops between chunks). */
        ~SampleManager() override;

        //==========================================================================================
        // Requests: any thread but the audio thread. Each returns its number (1, 2, ...).

        std::uint64_t loadStarter (sounds::StarterId id, std::int32_t seed, LoadOrigin origin);
        std::uint64_t loadFile (const juce::File& file, LoadOrigin origin);

        /** The sound a project (or an undo state) names: a starter, or a file found by its path. */
        std::uint64_t loadRef (const state::SampleRef& ref, LoadOrigin origin);

        /** Blocks until that request is finished (committed, waiting in takePending, failed, or replaced by a
            newer one), at most timeoutMs. True when it is. */
        bool waitFor (std::uint64_t request, int timeoutMs);

        /** Until every request made so far is finished, at most timeoutMs. */
        bool waitIdle (int timeoutMs);

        /** Until every request the worker publishes by itself (a project, Undo, a new instance's default) is
            finished, at most timeoutMs. A user load ends in takePending and only its owner publishes it, on the
            message thread, so a wait there gains nothing (prepareToPlay, fix pass RT2-5). */
        bool waitForSelfPublished (int timeoutMs);

        /** True while a request waits or is being made. */
        bool busy() const;

        /** True when no request newer than this one has been made: a pending user load that is not the newest
            would be refused by commit (). */
        bool isNewest (std::uint64_t request) const;

        /** Every request up to this number is finished. Lock-free: the audio thread may read it (a block played
            while a state load's sound is not there yet is counted by the diagnostics, decision 3). */
        std::uint64_t finishedRequest() const noexcept { return finishedAtomic.load (std::memory_order_acquire); }

        //==========================================================================================
        // The owner (message thread).

        /** The user loads that are ready, oldest first. */
        std::vector<PendingSound> takePending();

        /** Publishes a pending sound once its patch is applied, unless a newer request was made since (then
            nothing happens). True when it was published. */
        bool commit (PendingSound sound);

        /** Called on the worker thread when takePending () has something new. */
        std::function<void()> onPendingReady;

        /** Called right after a sound is published, on the thread that published it, with the manager's lock held:
            keep it short (the processor stores the sound's length for the Region readouts). */
        std::function<void (const SoundView&)> onCommitted;

        /** The sound published last: what plays (the waveform strip's). Null before the first. */
        std::shared_ptr<const SoundView> current() const;

        /** The reports since the last call, oldest first (the toasts). */
        std::vector<LoadReport> takeReports();

        //==========================================================================================
        // For the tests and the diagnostics.

        int cachedStarters() const;
        std::uint64_t startersMade() const;   // starters this worker made (not found in its cache)
        double lastStarterMs() const;         // how long the last one it made took on the worker
        double lastFileMs() const;            // how long the last file took (hash and decode)
        std::uint64_t lastRequest() const;

        /** For the tests: while held, the worker takes no new request (one it is making finishes first), as if
            a sound took a long time to make. */
        void setHeld (bool held);

    private:
        enum class Kind : std::uint8_t { starter, file, ref };

        struct Request
        {
            std::uint64_t id = 0;
            Kind kind = Kind::starter;
            sounds::StarterId starter = sounds::kDefaultStarter;
            std::int32_t seed = 1;
            juce::File file;
            state::SampleRef ref;
            LoadOrigin origin = LoadOrigin::user;
        };

        struct Made
        {
            std::unique_ptr<engine::SampleData> data;
            std::shared_ptr<SoundView> view;
        };

        struct DecodedFile
        {
            std::vector<float> left, right;
            double sampleRate = 0.0;
            state::SampleFile record;
            bool cut = false;
            std::string error;   // empty when it worked
            bool missing = false, changed = false;
        };

        std::uint64_t enqueue (Request r);
        void run() override;
        void process (const Request& r);
        Made starterSound (sounds::StarterId id, std::int32_t seed);
        DecodedFile decodeFile (const juce::File& file, const state::SampleFile* expected);
        static Made fileSound (DecodedFile decoded);

        void keepReport (LoadReport report);   // under lock

        /** Publishes (under lock) unless a newer request exists. */
        bool commitLocked (std::uint64_t request, std::unique_ptr<engine::SampleData> data, std::shared_ptr<const SoundView> view,
                           LoadReport report);
        void finish (std::uint64_t request, LoadReport report, std::unique_ptr<engine::SampleData> data,
                     std::shared_ptr<const SoundView> view, std::unique_ptr<PendingSound> pending);

        rt::VersionExchange<engine::SampleData>& exchange;

        mutable std::mutex lock;
        std::condition_variable finishedChanged;
        std::deque<Request> queue;
        std::uint64_t requested = 0;   // the newest request number
        std::uint64_t newestSelfPublished = 0;   // the newest request that is not a user load
        std::uint64_t finished = 0;    // every request up to this one is finished
        bool held = false;             // the tests' setHeld
        std::atomic<std::uint64_t> finishedAtomic { 0 };
        bool working = false;
        std::vector<PendingSound> pending;
        std::vector<LoadReport> reports;
        std::shared_ptr<const SoundView> currentView;
        std::uint64_t made = 0;
        double starterMs = 0.0, fileMs = 0.0;

        // The worker's own.
        struct CachedStarter
        {
            sounds::StarterId id;
            std::int32_t seed;
            std::shared_ptr<const sounds::StarterSound> sound;
        };
        std::deque<CachedStarter> starters;   // most recently used last
        std::atomic<int> starterCount { 0 };
        std::unique_ptr<juce::AudioFormatManager> formats;
    };
}
