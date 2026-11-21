#pragma once

// The sound loader (PLUGIN-PLAN 1.3, 7.4 decisions 3 and 4, 7.9 decision 1): a worker thread that makes starter
// sounds, decodes sound files and analyses them, and hands each finished sound to the audio thread through
// rt::VersionExchange<engine::SampleData>. The message thread never makes a starter, decodes a file or analyses one.
//
// - Starters: sawopen is the default sound (D9). The worker keeps the last 4 starters it made, by (id, seed), so
//   a project that uses the current or a recent starter does not make it again (decision 3).
// - Files (decision 4): decoded with AudioFormatManager::registerBasicFormats () on the worker. A file longer than
//   10 minutes loads a part of 10 minutes (question 9.3, M7): its first 10 minutes unless the user picked another
//   part (loadFile's partStart) or the project names one (SMPL's loaded range). A part longer than 3 minutes is kept
//   as 16-bit samples (plan 8.2 #14), converted chunk by chunk, so a 10-minute stereo 48 kHz song takes its
//   115.2 MB and never a float copy. The SMPL file record is filled (path, size, SHA-256, rate, channels, frames,
//   the loaded part, the display name, the analysis cache). A project's file is found again by its path when its
//   size and SHA-256 still match (plan 3); otherwise it is missing, the default starter plays, the report names the
//   file, and the record is kept as it was so a save keeps it.
// - Analysis (M7, decision 1): after every decode, before the sound is handed on, the worker suggests the sound's
//   parts and, for a file, detects its key and (for a file of 20 s or less) its pitch, all through sl::analysis.
//   A project's file whose SMPL cache is readable is not analysed again. "Detect pitch & key", the root suggestion
//   for a new Region and a long file's overview are jobs the worker runs between sounds. A newer sound request
//   stops any of them (and a decode) at its next question, and its result is dropped.
// - Who asked decides who publishes. A load the user asked for (the menu, Browse, a drop, a part) changes
//   parameters: the root and cents (60 and 0) and the Region (the whole sound; for a song, longer than 20 s as
//   sl-ui.js's LONG_FILE, the part pickPart chooses, U2), as host-visible edits on the message thread (plan 2.4). So
//   the worker hands it over (takePending), and the owner applies the patch and then commits it, so the audio thread
//   never plays the new sound with the old Region. A project load, Undo and a new instance's default change no
//   parameter, so the worker commits those itself, and a state load can wait for it on its own thread (decision 3:
//   the worker never needs the message thread).
// - Newest wins. Each request has a number; the worker takes only the newest one waiting, and a sound whose
//   request is older than the newest request made is never committed.

#include "analysis/Analysis.h"
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
#include <optional>
#include <string>
#include <vector>

namespace sl::doc
{
    /** Who asked for a sound (see above). */
    enum class LoadOrigin : std::uint8_t { user, project, undo, startup };

    /** What listening to a sound found (M7): the load's analysis, or the SMPL cache it came back from. */
    struct SoundAnalysis
    {
        bool analysed = false;                     // the analysis ran (or its cache was read)
        std::optional<analysis::Pitch> pitch;      // the pitch of what was listened to (a file of 20 s or less, or Detect)
        std::optional<analysis::Key> key;          // the key (none for noise: detectKey reads a key in anything)
        std::vector<analysis::Part> parts;         // the suggested parts, fractions of the loaded sound

        /** The prototype's strip words (sl-ui.js S.detected): "key A minor", "" (a pitch and no key) or
            "no clear pitch"; "" before the analysis. */
        std::string detectedText() const;
    };

    /** SMPL's analysis cache from an analysis, and back (plan 3). fromCache gives nothing when the cache was not
        written (analysed 0) or holds what this version cannot read (a part kind, a scale, a key or a number out of
        range): such a sound is analysed again. */
    state::AnalysisCache toCache (const SoundAnalysis& a);
    std::optional<SoundAnalysis> fromCache (const state::AnalysisCache& c);

    /** sl-ui.js keyName: "A minor", "C major" (the note, then the scale's name in lower case). */
    std::string keyName (int key, analysis::Scale scale);

    /** The sound as the editor sees it (the waveform strip draws its peaks from its samples): it shares its samples
        with the SampleData the engine plays. Never changed once made. */
    struct SoundView
    {
        std::shared_ptr<const void> storage;   // keeps the samples alive
        engine::SampleFormat format = engine::SampleFormat::f32;
        const float* left = nullptr;            // f32
        const float* right = nullptr;           // f32: the same pointer as left when mono
        const std::int16_t* left16 = nullptr;   // i16 (a part longer than 3 minutes)
        const std::int16_t* right16 = nullptr;  // i16: the same pointer as left16 when mono
        std::int64_t frames = 0;
        double sampleRate = 44100.0;
        bool stereo = false;
        int channels = 1;                       // the channels decoded (1 or 2; a starter has 2): what the analysis reads
        std::string name;                       // "Saw that opens up", or the file's name
        bool builtIn = true;
        sounds::StarterId starter = sounds::kDefaultStarter;   // when builtIn
        std::int32_t seed = 1;
        state::SampleRef ref;                   // what the project names (SMPL): a missing file's record stays here
        bool missing = false;                   // the project names a file that was not found; the default starter plays
        bool cut = false;                       // a part of a file longer than 10 minutes (ref.file's loaded range)
        std::uint64_t request = 0;              // the load that made it
        SoundAnalysis analysis;                 // what the worker found (or the SMPL cache said) before it was handed on

        double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (frames) / sampleRate : 0.0; }

        /** True when there are samples to draw (either format). */
        bool hasSamples() const noexcept { return format == engine::SampleFormat::i16 ? left16 != nullptr : left != nullptr; }

        /** Frame i (0 <= i < frames) of channel 0 (left) or 1 (right), as a double (a 16-bit sample is s / 32768). */
        double sample (int channel, std::int64_t i) const noexcept
        {
            if (format == engine::SampleFormat::i16)
            {
                const std::int16_t* p = channel == 0 || right16 == nullptr ? left16 : right16;
                return static_cast<double> (p[i]) * engine::kInt16Scale;
            }
            const float* p = channel == 0 || right == nullptr ? left : right;
            return static_cast<double> (p[i]);
        }

        /** The mid ((L + R) x 0.5, in double) of frame i, as the strip's peaks read it. */
        double mid (std::int64_t i) const noexcept { return (sample (0, i) + sample (1, i)) * 0.5; }

        /** The whole file's length in seconds (a part-loaded file's, not the part's); the sound's own otherwise. */
        double fileSeconds() const noexcept
        {
            if (ref.kind == state::kSampleFile && ! missing && ref.file.fileFrames > 0 && ref.file.fileSampleRate > 0.0)
                return static_cast<double> (ref.file.fileFrames) / ref.file.fileSampleRate;
            return seconds();
        }
    };

    /** One finished load, for the toast. */
    struct LoadReport
    {
        std::uint64_t request = 0;
        LoadOrigin origin = LoadOrigin::user;
        bool ok = false;        // the sound asked for is the one that plays now
        std::string message;    // what the toast says ("" when nothing needs saying)
        std::string name;       // the sound asked for: a starter's name, a file's name (the toast's "Loaded ...")
        int toastMs = 0;        // how long the toast shows (0: 2.6 s when ok, else 5 s)
    };

    /** A root heard in a stretch of the sound (sl-ui.js pitchToRoot of detectPitch), with the stretch it was heard in
        (the Region it was measured on, fractions of the sound). */
    struct RootHeard
    {
        int root = 60;
        int cents = 0;
        double regionStart = 0.0, regionEnd = 1.0;
    };

    /** sl-ui.js pitchToRoot: the nearest note (0 .. 127) and the cents from it, rounded and kept to -50 .. 50. */
    RootHeard pitchToRoot (const analysis::Pitch& m);

    /** A load the user asked for, ready: the sound, and the parameters it sets (parameters.json touchedBy
        "soundLoad": rootNote, rootCents, regionStart, regionEnd) with the exact Region edges (plan 2.2), and what
        listening to it found (the owner shows suggestions; none of them applies itself, decision 2). */
    struct PendingSound
    {
        std::uint64_t request = 0;
        std::unique_ptr<engine::SampleData> data;
        std::shared_ptr<const SoundView> view;
        core::InstPatch patch;
        double regionStartExact = 0.0, regionEndExact = 1.0;
        LoadReport report;
        std::optional<RootHeard> rootHeard;    // the root heard in the sound (a short file) or in its first Region (U2)
        std::optional<analysis::Part> part;    // the suggested part the Region starts on (U2), if any
    };

    /** The whole of a file longer than 10 minutes, coarsely (the part picker draws it): the lowest and highest mid
        value in each of kOverviewBins equal stretches of the file. */
    struct FileOverview
    {
        std::string path;
        std::uint64_t fileFrames = 0;
        double sampleRate = 0.0;
        std::vector<float> lo, hi;
        bool ok = false;
    };

    /** The jobs the worker runs between sounds (M7). parts is a starter's part suggestions, made right after the
        starter is handed on (so a project waiting for its starter never waits for them, decision 3). */
    enum class JobKind : std::uint8_t { detect = 0, rootSuggestion = 1, overview = 2, parts = 3 };

    /** What a job found, for the owner (message thread). */
    struct JobResult
    {
        JobKind kind = JobKind::detect;
        std::uint64_t job = 0;                 // the job's number
        std::uint64_t soundRequest = 0;        // the sound it listened to (its load's request)
        double regionStart = 0.0, regionEnd = 1.0;
        bool whole = true;                     // the Region was the whole sound (within 0.0005 of each edge)
        bool longFile = false;                 // the sound is longer than 20 s
        bool listened = false;                 // detectPitch ran on the Region
        std::optional<analysis::Pitch> pitch;
        std::optional<analysis::Key> key;      // detect: the sound's key (none for noise)
        bool noisy = false;                    // detect: no pitch and a noise-like spectrum
        std::vector<analysis::Part> parts;     // parts: the starter's suggested parts
    };

    class SampleManager final : private juce::Thread
    {
    public:
        static constexpr int kStarterCacheSize = 4;
        static constexpr double kMaxFileSeconds = 600.0;      // a longer file loads a part of 10 minutes (question 9.3)
        static constexpr double kInt16Seconds = 180.0;        // a longer part is kept as 16-bit samples (plan 8.2 #14)
        static constexpr double kLongFileSeconds = 20.0;      // sl-ui.js LONG_FILE: a longer file is a song
        static constexpr double kDefaultPartStart = 0.35;     // a song with no suggested part: 4 s from 35 % in (plan 4 M2)
        static constexpr double kDefaultPartSeconds = 4.0;
        static constexpr int kMinFileFrames = 64;             // sl-ui.js loadFile: a shorter file is "empty"
        static constexpr std::size_t kMaxReports = 32;         // reports kept until someone takes them
        static constexpr int kOverviewBins = 2048;            // the part picker's overview

        /** Starts the worker. */
        explicit SampleManager (rt::VersionExchange<engine::SampleData>& exchange);

        /** Stops the worker (a starter being made finishes first; a file stops between chunks). */
        ~SampleManager() override;

        //==========================================================================================
        // Requests: any thread but the audio thread. Each returns its number (1, 2, ...).

        std::uint64_t loadStarter (sounds::StarterId id, std::int32_t seed, LoadOrigin origin);

        /** A file. partStart (frames of the file) loads the 10 minutes from there when the file is longer than
            10 minutes (the part picker); without it such a file loads its first 10 minutes. */
        std::uint64_t loadFile (const juce::File& file, LoadOrigin origin, std::optional<std::uint64_t> partStart = std::nullopt);

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
        // Jobs (M7): any thread but the audio thread. Each returns its number, or 0 when there is nothing to do.
        // A newer job of the same kind replaces one still waiting (and stops one running), and a newer sound request
        // drops them all.

        /** "Detect pitch & key" (sl-ui.js runDetect 'button') on the sound playing now: the pitch of the Region
            (regionStart .. regionEnd, fractions of the sound) and the sound's key. 0 for no sound or a built-in one. */
        std::uint64_t detect (double regionStart, double regionEnd);

        /** A root suggestion for a new Region (sl-ui.js suggestRoot): the pitch of the Region when it is a part
            (not the whole) of a file and 0.05 .. 30 s long. 0 (no job) otherwise. */
        std::uint64_t suggestRoot (double regionStart, double regionEnd);

        /** The overview of the file playing now, when a part of it is loaded (the part picker). 0 otherwise. */
        std::uint64_t requestOverview();

        /** Blocks until every job asked for so far is done (or dropped), at most timeoutMs. */
        bool waitJobs (int timeoutMs);

        /** The detect and root-suggestion results since the last call, oldest first (message thread). */
        std::vector<JobResult> takeResults();

        /** The newest overview made (null before the first). */
        std::shared_ptr<const FileOverview> overview() const;

        /** Called on the worker thread when takeResults () or overview () has something new. */
        std::function<void()> onJobDone;

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
        double lastFileMs() const;            // how long the last file took (hash, decode and analysis)
        double lastAnalysisMs() const;        // how long the last load's analysis took
        std::uint64_t analysesRun() const;    // load analyses the worker finished (a readable cache runs none)
        std::uint64_t jobsRun() const;        // detect and root-suggestion jobs that ran to the end
        std::uint64_t stopsMade() const;      // decodes, analyses and jobs a newer request stopped
        std::uint64_t lastRequest() const;

        /** For the tests: while held, the worker takes no new request (one it is making finishes first), as if
            a sound took a long time to make. */
        void setHeld (bool held);

        /** For the tests: called on the worker thread right before (true) and right after (false) it makes a sound
            request, so a test can count what the worker allocates (Tests/support/AllocCounter). Set it while the
            worker is idle. */
        std::function<void (bool starting)> workerProbe;

    private:
        enum class Kind : std::uint8_t { starter, file, ref };

        struct Request
        {
            std::uint64_t id = 0;
            Kind kind = Kind::starter;
            sounds::StarterId starter = sounds::kDefaultStarter;
            std::int32_t seed = 1;
            juce::File file;
            std::optional<std::uint64_t> partStart;
            state::SampleRef ref;
            LoadOrigin origin = LoadOrigin::user;
        };

        struct Job
        {
            std::uint64_t id = 0;
            JobKind kind = JobKind::detect;
            std::uint64_t afterRequest = 0;    // the newest sound request when it was asked for
            std::shared_ptr<const SoundView> view;
            double regionStart = 0.0, regionEnd = 1.0;
        };

        struct Made
        {
            std::unique_ptr<engine::SampleData> data;
            std::shared_ptr<SoundView> view;
        };

        struct DecodedFile
        {
            std::vector<float> left, right;              // a part of 3 minutes or less
            std::vector<std::int16_t> left16, right16;   // a longer part (plan 8.2 #14)
            bool i16 = false;
            int channels = 1;
            double sampleRate = 0.0;
            state::SampleFile record;
            bool cut = false;
            std::string error;   // empty when it worked
            bool missing = false, changed = false;
            std::int64_t frames() const noexcept { return static_cast<std::int64_t> (i16 ? left16.size() : left.size()); }
        };

        std::uint64_t enqueue (Request r);
        std::uint64_t enqueueJob (Job j);
        void run() override;
        void process (const Request& r);
        void runJob (const Job& j);
        Made starterSound (sounds::StarterId id, std::int32_t seed);
        DecodedFile decodeFile (const juce::File& file, const state::SampleFile* expected, std::optional<std::uint64_t> partStart,
                                std::uint64_t requestId);
        static Made fileSound (DecodedFile decoded);

        /** The load's analysis (parts; for a file the key and, at 20 s or less, the pitch). Not analysed when stopped. */
        SoundAnalysis analyseSound (const SoundView& v, const analysis::Stop& stop);

        /** True when a sound request newer than this one has been made, or the thread must end. */
        bool supersededBy (std::uint64_t request) const noexcept;

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
        std::deque<Job> jobs;
        std::uint64_t requested = 0;   // the newest request number
        std::atomic<std::uint64_t> requestedAtomic { 0 };   // the same, for the worker's stop questions
        std::uint64_t newestSelfPublished = 0;   // the newest request that is not a user load
        std::uint64_t finished = 0;    // every request up to this one is finished
        std::uint64_t jobsAsked = 0, jobsDone = 0;
        std::atomic<std::uint64_t> newestJob[4] {};   // per JobKind: the newest job asked for
        bool held = false;             // the tests' setHeld
        std::atomic<std::uint64_t> finishedAtomic { 0 };
        bool working = false;
        std::vector<PendingSound> pending;
        std::vector<LoadReport> reports;
        std::vector<JobResult> results;
        std::shared_ptr<const SoundView> currentView;
        std::shared_ptr<const FileOverview> lastOverview;
        std::uint64_t made = 0, analyses = 0, jobRuns = 0, stops = 0;
        double starterMs = 0.0, fileMs = 0.0, analysisMs = 0.0;

        // The worker's own.
        struct CachedStarter
        {
            sounds::StarterId id;
            std::int32_t seed;
            std::shared_ptr<const sounds::StarterSound> sound;
            std::shared_ptr<const std::vector<analysis::Part>> parts;   // its suggested parts, once made
        };
        std::deque<CachedStarter> starters;   // most recently used last
        std::atomic<int> starterCount { 0 };
        std::unique_ptr<juce::AudioFormatManager> formats;
    };
}
