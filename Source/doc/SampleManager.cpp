#include "SampleManager.h"

#include "Sha256.h"
#include "core/JsCompat.h"
#include "core/Music.h"
#include "ui/SampleTabRules.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace sl::doc
{
    namespace
    {
        double msSince (std::chrono::steady_clock::time_point t0)
        {
            return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        }

        /** The patch a sound the user loads sets (sl-ui.js switchSound): the sound's own root and cents (60 and 0:
            the starters are rooted at C3, and a new file is until its root is accepted or detected) and the Region. */
        core::InstPatch soundLoadPatch (double regionStart, double regionEnd)
        {
            core::InstPatch p;
            p.root = 60.0;
            p.cents = 0.0;
            p.region.start = regionStart;
            p.region.end = regionEnd;
            return p;
        }

        std::string defaultStarterName() { return sounds::starterInfo (sounds::kDefaultStarter).name; }

        /** A stretch of a sound (frames start .. start + count) as the analysis reads it (analysis::Source): floats as
            they are, a 16-bit sample as s / 32768 (exact in float: the float a 16-bit file decodes to). */
        class ViewSource final : public analysis::Source
        {
        public:
            ViewSource (const SoundView& v, std::int64_t start, std::int64_t count) noexcept : view (v), from (start), length (count) {}

            int numChannels() const override { return view.channels; }
            std::int64_t numFrames() const override { return length; }
            double sampleRate() const override { return view.sampleRate; }

            void read (int c, std::int64_t start, int count, float* out) const override
            {
                const std::int64_t at = from + start;
                if (view.format == engine::SampleFormat::i16)
                {
                    const std::int16_t* p = (c == 0 || view.right16 == nullptr ? view.left16 : view.right16) + at;
                    for (int k = 0; k < count; ++k)
                        out[k] = static_cast<float> (p[k]) * (1.0f / 32768.0f);
                    return;
                }
                const float* p = (c == 0 || view.right == nullptr ? view.left : view.right) + at;
                std::memcpy (out, p, static_cast<std::size_t> (count) * sizeof (float));
            }

        private:
            const SoundView& view;
            std::int64_t from, length;
        };

        /** sl-ui.js regionWhole: the Region is the whole sound (within 0.0005 of each edge). */
        bool regionWhole (double start, double end) noexcept { return start <= 0.0005 && end >= 0.9995; }

        /** sl-ui.js regionChannels: frames floor (start x n) .. ceil (end x n), kept inside the sound. */
        std::pair<std::int64_t, std::int64_t> regionFrames (std::int64_t frames, double start, double end)
        {
            if (! (std::isfinite (start) && std::isfinite (end)))
                return { 0, frames };
            const double n = static_cast<double> (frames);
            const double a = std::clamp (std::floor (start * n), 0.0, n);
            const double b = std::clamp (std::ceil (end * n), a, n);
            return { static_cast<std::int64_t> (a), static_cast<std::int64_t> (b) };
        }

        /** sl-ui.js noiseLike: the median spectral flatness of the sound's louder moments (within 40 dB of the loudest)
            is above 0.1. White noise reads about 0.56; held notes, chords and the starters other than Noise stay under
            0.015. */
        bool noiseLike (const analysis::Source& source, const analysis::Stop& stop)
        {
            const auto f = analysis::features (source, stop);
            if (f.stopped || f.nc <= 0)
                return false;
            const auto nc = static_cast<std::size_t> (f.nc);
            double top = -1e9;
            for (std::size_t c = 0; c < nc; ++c)
                top = std::max (top, f.Lc[c]);
            std::vector<double> a;
            for (std::size_t c = 0; c < nc; ++c)
                if (f.Lc[c] > top - 40 && std::isfinite (f.flat[c]))
                    a.push_back (f.flat[c]);
            std::sort (a.begin(), a.end());
            return ! a.empty() && a[a.size() >> 1] > 0.1;
        }

        /** SLSampleUI.pickPart over the analysis's parts (one copy of the rule: ui/SampleTabRules). */
        std::optional<analysis::Part> pickPartOf (const std::vector<analysis::Part>& parts)
        {
            std::vector<std::optional<ui::rules::Part>> list;
            for (const auto& p : parts)
            {
                ui::rules::Part q;
                q.kind = analysis::partKindName (p.kind);
                q.label = p.label;
                q.start = p.start;
                q.end = p.end;
                q.tempoFound = p.tempoFound;
                q.bpm = p.bpm;
                list.emplace_back (std::move (q));
            }
            const auto chosen = ui::rules::pickPart (&list);
            if (! chosen.has_value())
                return std::nullopt;
            for (const auto& p : parts)
                if (chosen->kind == analysis::partKindName (p.kind) && juce::exactlyEqual (chosen->start, p.start)
                    && juce::exactlyEqual (chosen->end, p.end))
                    return p;
            return std::nullopt;
        }

        /** sl-ui.js fmtTime for the toasts: minutes and whole seconds ("6:00"). */
        std::string minutesText (double seconds)
        {
            const double s = std::max (0.0, std::floor (seconds + 1e-9));
            const auto m = static_cast<long long> (std::floor (s / 60.0));
            const auto r = static_cast<long long> (s - static_cast<double> (m) * 60.0);
            return std::to_string (m) + ":" + (r < 10 ? "0" : "") + std::to_string (r);
        }

        /** The common fields of a view over the sample the engine plays (the samples are shared, never copied). */
        void viewOf (SoundView& view, const engine::SampleData& data)
        {
            view.storage = data.storage;
            view.format = data.format;
            view.left = data.L;
            view.right = data.R;
            view.left16 = data.L16;
            view.right16 = data.R16;
            view.frames = data.n;
            view.sampleRate = data.sr;
            view.stereo = data.stereo;
            view.excerpt = data.excerpt;
        }

        //==========================================================================================
        // M7 V3: keeping the sound with the project.

        /** The 24-bit sample nearest to v x 2^23 (half away from zero), kept inside -2^23 .. 2^23 - 1; NaN gives 0. A
            16-bit sample s / 32768 gives s x 256 exactly, so a 16-bit sound keeps every bit. */
        int toInt24 (double v) noexcept
        {
            const double y = v * 8388608.0;
            if (std::isnan (y))
                return 0;
            if (y >= 8388607.0)
                return 8388607;
            if (y <= -8388608.0)
                return -8388608;
            return static_cast<int> (std::lround (y));
        }

        /** An output stream into a byte vector from offset base on, with positions counted from there (the FLAC writer
            seeks back to write its header at the end). It refuses (and says so in overflow) anything past limit bytes. */
        class BytesOutput final : public juce::OutputStream
        {
        public:
            BytesOutput (state::Bytes& target, std::size_t offset, std::size_t limitBytes, bool& overflowed) noexcept
                : out (target), base (offset), limit (limitBytes), overflow (overflowed) {}

            void flush() override {}

            bool setPosition (juce::int64 newPosition) override
            {
                if (newPosition < 0 || static_cast<std::size_t> (newPosition) > out.size() - base)
                    return false;
                pos = static_cast<std::size_t> (newPosition);
                return true;
            }

            juce::int64 getPosition() override { return static_cast<juce::int64> (pos); }

            bool write (const void* data, std::size_t size) override
            {
                if (pos + size > limit)
                {
                    overflow = true;
                    return false;
                }
                const std::size_t at = base + pos;
                if (at + size > out.size())
                    out.resize (at + size);
                if (size > 0)
                    std::memcpy (out.data() + at, data, size);
                pos += size;
                return true;
            }

        private:
            state::Bytes& out;
            const std::size_t base, limit;
            bool& overflow;
            std::size_t pos = 0;
        };

        /** A file record's extension, lower case with its dot (".wav"), from its name or else its path; "" for none. */
        juce::String extensionOf (const state::SampleFile& f)
        {
            const auto name = juce::String::fromUTF8 ((f.displayName.empty() ? f.path : f.displayName).c_str());
            const int dot = name.lastIndexOfChar ('.');
            const int slash = juce::jmax (name.lastIndexOfChar ('/'), name.lastIndexOfChar ('\\'));
            if (dot <= slash + 1 || dot < 0)
                return {};
            return name.substring (dot).toLowerCase();
        }

        /** The file a record names, when its path is a usable absolute path here (juce::File asserts on others). */
        std::optional<juce::File> fileAt (const std::string& path)
        {
            const auto p = juce::String::fromUTF8 (path.c_str());
            if (p.isEmpty() || ! juce::File::isAbsolutePath (p))
                return std::nullopt;
            return juce::File (p);
        }
    }

    //==============================================================================================
    juce::File SampleManager::defaultLibraryFolder()
    {
       #if STRANDLOCK_TESTS
        // One folder per test run, chosen once, so no run sees another's (or Kalan's) library.
        static const juce::File runFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                .getChildFile ("StrandLockTests")
                                                .getNonexistentChildFile ("library", "", false);
        return runFolder;
       #else
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("StrandLock").getChildFile ("Samples");
       #endif
    }

    void SampleManager::setLibraryFolder (const juce::File& folder)
    {
        const std::lock_guard<std::mutex> hold (lock);
        library = folder;
    }

    juce::File SampleManager::libraryFolder() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return library;
    }

    juce::File SampleManager::libraryFileFor (const state::SampleFile& f) const
    {
        const auto name = juce::String (toHex (f.sha256).substr (0, 16)) + extensionOf (f);
        return libraryFolder().getChildFile (name);
    }

    //==============================================================================================
    std::string keyName (int key, analysis::Scale scale)
    {
        const int k = key >= 0 && key < 12 ? key : 0;
        return std::string (core::kNoteNames[k]) + (scale == analysis::Scale::minor ? " minor" : " major");
    }

    std::string SoundAnalysis::detectedText() const
    {
        if (! analysed)
            return {};
        if (key.has_value())
            return "key " + keyName (key->key, key->scale);
        return pitch.has_value() ? std::string() : std::string ("no clear pitch");
    }

    state::AnalysisCache toCache (const SoundAnalysis& a)
    {
        state::AnalysisCache c;
        if (! a.analysed)
            return c;
        c.analysed = 1;
        if (a.pitch.has_value())
        {
            c.hasPitch = 1;
            c.midi = static_cast<double> (a.pitch->midi);
            c.cents = a.pitch->cents;
        }
        if (a.key.has_value())
        {
            c.hasKey = 1;
            c.key = static_cast<std::uint8_t> (std::clamp (a.key->key, 0, 11));
            c.scale = analysis::scaleName (a.key->scale);
        }
        for (const auto& p : a.parts)
        {
            if (c.parts.size() >= 255)
                break;
            state::AnalysisPart q;
            q.kind = static_cast<std::uint8_t> (p.kind);
            q.start = p.start;
            q.end = p.end;
            q.bpm = p.bpm;
            q.tempoFound = p.tempoFound ? 1 : 0;
            q.label = p.label.size() > state::kMaxStr8 ? p.label.substr (0, state::kMaxStr8) : p.label;
            c.parts.push_back (std::move (q));
        }
        return c;
    }

    std::optional<SoundAnalysis> fromCache (const state::AnalysisCache& c)
    {
        if (c.analysed != 1 || c.hasPitch > 1 || c.hasKey > 1)
            return std::nullopt;
        SoundAnalysis a;
        a.analysed = true;
        if (c.hasPitch == 1)
        {
            if (! (std::isfinite (c.midi) && std::isfinite (c.cents)) || c.midi < 0.0 || c.midi > 127.0
                || ! juce::exactlyEqual (c.midi, std::floor (c.midi)) || std::abs (c.cents) > 50.0)
                return std::nullopt;
            analysis::Pitch p;
            p.midi = static_cast<int> (c.midi);
            p.cents = c.cents;
            p.hz = 440.0 * std::pow (2.0, (c.midi + c.cents / 100.0 - 69.0) / 12.0);
            a.pitch = p;
        }
        if (c.hasKey == 1)
        {
            if (c.key > 11)
                return std::nullopt;
            analysis::Key k;
            k.key = c.key;
            if (c.scale == analysis::scaleName (analysis::Scale::major))
                k.scale = analysis::Scale::major;
            else if (c.scale == analysis::scaleName (analysis::Scale::minor))
                k.scale = analysis::Scale::minor;
            else
                return std::nullopt;
            a.key = k;
        }
        for (const auto& q : c.parts)
        {
            if (q.kind > static_cast<std::uint8_t> (analysis::PartKind::loop) || q.tempoFound > 1
                || ! (std::isfinite (q.start) && std::isfinite (q.end) && std::isfinite (q.bpm)) || q.start < 0.0 || q.end > 1.0 || q.end < q.start)
                return std::nullopt;
            analysis::Part p;
            p.kind = static_cast<analysis::PartKind> (q.kind);
            p.label = q.label;
            p.start = q.start;
            p.end = q.end;
            p.bpm = q.bpm;
            p.tempoFound = q.tempoFound == 1;
            a.parts.push_back (std::move (p));
        }
        return a;
    }

    RootHeard pitchToRoot (const analysis::Pitch& m)
    {
        // const r = Math.round(m.midi), c = clamp(Math.round(((m.midi - r) * 100) + (m.cents || 0)), -50, 50)
        const double midi = static_cast<double> (m.midi);
        const double r = core::jsRound (midi);
        const double cents = std::isfinite (m.cents) ? m.cents : 0.0;
        const double c = std::clamp (core::jsRound ((midi - r) * 100 + cents), -50.0, 50.0);
        RootHeard out;
        out.root = static_cast<int> (std::clamp (r, 0.0, 127.0));
        out.cents = static_cast<int> (c);
        return out;
    }

    //==============================================================================================
    SampleManager::SampleManager (rt::VersionExchange<engine::SampleData>& out)
        : juce::Thread ("StrandLock sounds"), exchange (out)
    {
        formats = std::make_unique<juce::AudioFormatManager>();
        formats->registerBasicFormats();
        library = defaultLibraryFolder();
        startThread();
    }

    SampleManager::~SampleManager()
    {
        stopThread (15000);
    }

    bool SampleManager::supersededBy (std::uint64_t request) const noexcept
    {
        return threadShouldExit() || requestedAtomic.load (std::memory_order_acquire) > request;
    }

    std::uint64_t SampleManager::enqueue (Request r)
    {
        std::uint64_t id = 0;
        {
            const std::lock_guard<std::mutex> hold (lock);
            id = ++requested;
            requestedAtomic.store (id, std::memory_order_release);
            r.id = id;
            if (r.origin != LoadOrigin::user)
                newestSelfPublished = id;
            queue.push_back (std::move (r));
        }
        notify();
        return id;
    }

    std::uint64_t SampleManager::loadStarter (sounds::StarterId id, std::int32_t seed, LoadOrigin origin)
    {
        Request r;
        r.kind = Kind::starter;
        r.starter = id;
        r.seed = seed;
        r.origin = origin;
        return enqueue (std::move (r));
    }

    std::uint64_t SampleManager::loadFile (const juce::File& file, LoadOrigin origin, std::optional<std::uint64_t> partStart, bool keepOriginal)
    {
        Request r;
        r.kind = Kind::file;
        r.file = file;
        r.partStart = partStart;
        r.origin = origin;
        r.keepOriginal = keepOriginal;
        return enqueue (std::move (r));
    }

    std::uint64_t SampleManager::loadRef (const state::SampleRef& ref, LoadOrigin origin, state::PayloadPtr saud, bool keepOriginal)
    {
        Request r;
        r.kind = Kind::ref;
        r.ref = ref;
        r.origin = origin;
        r.saud = std::move (saud);
        r.keepOriginal = keepOriginal;
        return enqueue (std::move (r));
    }

    std::uint64_t SampleManager::locate (const juce::File& file, const state::SampleRef& project, bool keepOriginal)
    {
        Request r;
        r.kind = Kind::locate;
        r.file = file;
        r.ref = project;
        r.origin = LoadOrigin::user;
        r.keepOriginal = keepOriginal;
        return enqueue (std::move (r));
    }

    bool SampleManager::waitFor (std::uint64_t request, int timeoutMs)
    {
        std::unique_lock<std::mutex> hold (lock);
        return finishedChanged.wait_for (hold, std::chrono::milliseconds (std::max (0, timeoutMs)),
                                         [this, request] { return finished >= request; });
    }

    bool SampleManager::waitIdle (int timeoutMs)
    {
        std::unique_lock<std::mutex> hold (lock);
        return finishedChanged.wait_for (hold, std::chrono::milliseconds (std::max (0, timeoutMs)),
                                         [this] { return finished >= requested; });
    }

    bool SampleManager::waitForSelfPublished (int timeoutMs)
    {
        std::unique_lock<std::mutex> hold (lock);
        return finishedChanged.wait_for (hold, std::chrono::milliseconds (std::max (0, timeoutMs)),
                                         [this] { return finished >= newestSelfPublished; });
    }

    bool SampleManager::busy() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return working || ! queue.empty();
    }

    bool SampleManager::isNewest (std::uint64_t request) const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return request >= requested;
    }

    void SampleManager::setHeld (bool nowHeld)
    {
        {
            const std::lock_guard<std::mutex> hold (lock);
            held = nowHeld;
        }
        notify();
    }

    //==============================================================================================
    std::uint64_t SampleManager::enqueueJob (Job j)
    {
        std::uint64_t id = 0;
        {
            const std::lock_guard<std::mutex> hold (lock);
            if (j.view == nullptr)
            {
                j.view = currentView;
                if (j.view == nullptr)
                    return 0;
            }
            id = ++jobsAsked;
            j.id = id;
            j.afterRequest = requested;
            newestJob[static_cast<int> (j.kind)].store (id, std::memory_order_release);
            jobs.push_back (std::move (j));
        }
        notify();
        return id;
    }

    std::uint64_t SampleManager::detect (double regionStart, double regionEnd)
    {
        Job j;
        j.kind = JobKind::detect;
        j.regionStart = regionStart;
        j.regionEnd = regionEnd;
        {
            const std::lock_guard<std::mutex> hold (lock);
            if (currentView == nullptr || currentView->builtIn || ! currentView->hasSamples())
                return 0;
            j.view = currentView;
        }
        return enqueueJob (std::move (j));
    }

    std::uint64_t SampleManager::suggestRoot (double regionStart, double regionEnd)
    {
        Job j;
        j.kind = JobKind::rootSuggestion;
        j.regionStart = regionStart;
        j.regionEnd = regionEnd;
        {
            const std::lock_guard<std::mutex> hold (lock);
            const auto& v = currentView;
            if (v == nullptr || v->builtIn || ! v->hasSamples() || regionWhole (regionStart, regionEnd))
                return 0;
            const double lenS = (regionEnd - regionStart) * v->seconds();
            if (! (lenS >= 0.05 && lenS <= 30.0))
                return 0;
            j.view = v;
        }
        return enqueueJob (std::move (j));
    }

    std::uint64_t SampleManager::requestOverview()
    {
        Job j;
        j.kind = JobKind::overview;
        {
            const std::lock_guard<std::mutex> hold (lock);
            const auto& v = currentView;
            // only a file read from its path: another part is loaded from there (M7 V3: not from a copy)
            if (v == nullptr || v->builtIn || v->missing || ! v->cut || v->ref.kind != state::kSampleFile || v->source != SoundSource::file)
                return 0;
            j.view = v;
        }
        return enqueueJob (std::move (j));
    }

    std::uint64_t SampleManager::embedOriginal()
    {
        Job j;
        j.kind = JobKind::embed;
        j.original = true;
        {
            const std::lock_guard<std::mutex> hold (lock);
            const auto& v = currentView;
            if (v == nullptr || v->builtIn || v->missing || v->ref.kind != state::kSampleFile || ! v->hasSamples())
                return 0;
            j.view = v;
        }
        return enqueueJob (std::move (j));
    }

    std::uint64_t SampleManager::embedExcerpt (std::int64_t fromFrame, std::int64_t frames)
    {
        Job j;
        j.kind = JobKind::embed;
        j.original = false;
        {
            const std::lock_guard<std::mutex> hold (lock);
            const auto& v = currentView;
            if (v == nullptr || v->builtIn || v->missing || v->ref.kind != state::kSampleFile || ! v->hasSamples())
                return 0;
            j.fromFrame = std::clamp<std::int64_t> (fromFrame, 0, v->frames);
            j.frameCount = std::clamp<std::int64_t> (frames, 0, v->frames - j.fromFrame);
            if (j.frameCount < 8)
                return 0;
            j.view = v;
        }
        return enqueueJob (std::move (j));
    }

    std::vector<EmbedResult> SampleManager::takeEmbedResults()
    {
        const std::lock_guard<std::mutex> hold (lock);
        std::vector<EmbedResult> out;
        out.swap (embedResults);
        return out;
    }

    std::uint64_t SampleManager::copyToLibrary()
    {
        if (! isLibraryOn())
            return 0;
        Job j;
        j.kind = JobKind::library;
        {
            const std::lock_guard<std::mutex> hold (lock);
            if (currentView == nullptr)
                return 0;
            j.view = currentView;
        }
        return enqueueJob (std::move (j));
    }

    bool SampleManager::waitJobs (int timeoutMs)
    {
        std::unique_lock<std::mutex> hold (lock);
        return finishedChanged.wait_for (hold, std::chrono::milliseconds (std::max (0, timeoutMs)),
                                         [this] { return jobsDone >= jobsAsked; });
    }

    std::vector<JobResult> SampleManager::takeResults()
    {
        const std::lock_guard<std::mutex> hold (lock);
        std::vector<JobResult> out;
        out.swap (results);
        return out;
    }

    std::shared_ptr<const FileOverview> SampleManager::overview() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return lastOverview;
    }

    //==============================================================================================
    std::vector<PendingSound> SampleManager::takePending()
    {
        const std::lock_guard<std::mutex> hold (lock);
        std::vector<PendingSound> out;
        out.swap (pending);
        return out;
    }

    bool SampleManager::commit (PendingSound sound)
    {
        const std::lock_guard<std::mutex> hold (lock);
        return commitLocked (sound.request, std::move (sound.data), std::move (sound.view), std::move (sound.report));
    }

    std::shared_ptr<const SoundView> SampleManager::current() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return currentView;
    }

    std::vector<LoadReport> SampleManager::takeReports()
    {
        const std::lock_guard<std::mutex> hold (lock);
        std::vector<LoadReport> out;
        out.swap (reports);
        return out;
    }

    int SampleManager::cachedStarters() const { return starterCount.load(); }

    std::uint64_t SampleManager::startersMade() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return made;
    }

    double SampleManager::lastStarterMs() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return starterMs;
    }

    double SampleManager::lastFileMs() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return fileMs;
    }

    double SampleManager::lastAnalysisMs() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return analysisMs;
    }

    std::uint64_t SampleManager::analysesRun() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return analyses;
    }

    std::uint64_t SampleManager::jobsRun() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return jobRuns;
    }

    std::uint64_t SampleManager::stopsMade() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return stops;
    }

    std::uint64_t SampleManager::lastRequest() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return requested;
    }

    //==============================================================================================
    void SampleManager::keepReport (LoadReport report)
    {
        // Nobody may read them (no editor open): keep the newest few.
        reports.push_back (std::move (report));
        while (reports.size() > kMaxReports)
            reports.erase (reports.begin());
    }

    bool SampleManager::commitLocked (std::uint64_t request, std::unique_ptr<engine::SampleData> data,
                                      std::shared_ptr<const SoundView> view, LoadReport report)
    {
        if (request < requested || data == nullptr || view == nullptr)
            return false;   // the user (or a project) asked for something newer since
        exchange.publish (std::move (data));
        exchange.collect();
        currentView = std::move (view);
        keepReport (std::move (report));
        if (onCommitted)
            onCommitted (*currentView);
        return true;
    }

    void SampleManager::run()
    {
        while (! threadShouldExit())
        {
            Request r;
            Job j;
            bool gotRequest = false, gotJob = false;
            {
                const std::lock_guard<std::mutex> hold (lock);
                if (! held && ! queue.empty())
                {
                    // Only the newest one waiting is made; the others are finished as replaced.
                    r = std::move (queue.back());
                    queue.clear();
                    finished = std::max (finished, r.id - 1);
                    finishedAtomic.store (finished, std::memory_order_release);
                    working = true;
                    gotRequest = true;
                }
                else if (! held)
                {
                    // Sounds first; then the oldest job still worth running (a newer sound request, or a newer job of the
                    // same kind, makes one stale).
                    while (! jobs.empty() && ! gotJob)
                    {
                        Job front = std::move (jobs.front());
                        jobs.pop_front();
                        const bool stale = front.afterRequest < requested
                                        || newestJob[static_cast<int> (front.kind)].load (std::memory_order_acquire) > front.id;
                        if (stale)
                        {
                            ++jobsDone;
                            continue;
                        }
                        j = std::move (front);
                        gotJob = true;
                    }
                }
            }
            if (! gotRequest && ! gotJob)
            {
                finishedChanged.notify_all();
                wait (-1.0);
                continue;
            }
            finishedChanged.notify_all();

            if (gotJob)
            {
                try
                {
                    runJob (j);
                }
                catch (...)
                {
                    // Out of memory, most likely: the job gives nothing.
                }
                {
                    const std::lock_guard<std::mutex> hold (lock);
                    ++jobsDone;
                }
                finishedChanged.notify_all();
                continue;
            }

            if (workerProbe)
                workerProbe (true);
            try
            {
                process (r);
            }
            catch (...)
            {
                // Out of memory, most likely (a very long file): the request fails, the sound stays.
                LoadReport report;
                report.request = r.id;
                report.origin = r.origin;
                report.message = "Could not load that sound (not enough memory)";
                finish (r.id, std::move (report), nullptr, nullptr, nullptr);
            }
            if (workerProbe)
                workerProbe (false);
        }
    }

    void SampleManager::finish (std::uint64_t request, LoadReport report, std::unique_ptr<engine::SampleData> data,
                                std::shared_ptr<const SoundView> view, std::unique_ptr<PendingSound> forOwner)
    {
        bool tellOwner = false;
        {
            const std::lock_guard<std::mutex> hold (lock);
            if (forOwner != nullptr)
            {
                if (request >= requested)
                {
                    pending.push_back (std::move (*forOwner));
                    tellOwner = true;
                }
            }
            else if (data != nullptr)
            {
                commitLocked (request, std::move (data), std::move (view), std::move (report));
            }
            else if (request >= requested)
            {
                keepReport (std::move (report));   // a failure: nothing changes but the toast
            }
            finished = std::max (finished, request);
            finishedAtomic.store (finished, std::memory_order_release);
            working = false;
        }
        finishedChanged.notify_all();
        if (tellOwner && onPendingReady)
            onPendingReady();
    }

    //==============================================================================================
    SampleManager::Made SampleManager::starterSound (sounds::StarterId id, std::int32_t seed)
    {
        std::shared_ptr<const sounds::StarterSound> sound;
        std::shared_ptr<const std::vector<analysis::Part>> parts;
        for (auto it = starters.begin(); it != starters.end(); ++it)
        {
            if (it->id == id && it->seed == seed)
            {
                sound = it->sound;
                parts = it->parts;
                starters.erase (it);
                break;
            }
        }
        if (sound == nullptr)
        {
            const auto t0 = std::chrono::steady_clock::now();
            sound = sounds::sharedStarterSound (id, seed);
            const double ms = msSince (t0);
            const std::lock_guard<std::mutex> hold (lock);
            ++made;
            starterMs = ms;
        }
        starters.push_back ({ id, seed, sound, parts });
        while (starters.size() > static_cast<std::size_t> (kStarterCacheSize))
            starters.pop_front();
        starterCount.store (static_cast<int> (starters.size()));

        const auto& info = sounds::starterInfo (id);
        const std::shared_ptr<const void> storage (sound, sound.get());
        Made m;
        m.data = engine::makeSampleData (storage, sound->left.data(), sound->right.data(), static_cast<std::int64_t> (sound->left.size()),
                                         static_cast<std::int64_t> (sound->right.size()), sound->sampleRate, info.name);
        auto view = std::make_shared<SoundView>();
        if (m.data != nullptr)
            viewOf (*view, *m.data);
        view->channels = sound->right.empty() ? 1 : 2;   // SLSounds.make: channels [L, R]
        view->name = info.name;
        view->builtIn = true;
        view->starter = id;
        view->seed = seed;
        view->ref.kind = state::kSampleStarter;
        view->ref.starterId = info.code;
        view->ref.seed = seed;
        if (parts != nullptr)
        {
            view->analysis.analysed = true;
            view->analysis.parts = *parts;
        }
        m.view = std::move (view);
        return m;
    }

    std::unique_ptr<juce::AudioFormatReader> SampleManager::readerForBytes (const std::uint8_t* data, std::size_t size,
                                                                           const juce::String& fileName) const
    {
        // AudioFormatManager::createReaderFor (File) tries the formats whose extensions the file has, in their order.
        const int dot = fileName.lastIndexOfChar ('.');
        if (data == nullptr || size == 0 || dot < 0)
            return nullptr;
        const auto ext = fileName.substring (dot);
        for (auto* format : *formats)
        {
            bool handles = false;
            for (const auto& e : format->getFileExtensions())
                handles = handles || ext.equalsIgnoreCase (e);
            if (! handles)
                continue;
            if (auto* r = format->createReaderFor (new juce::MemoryInputStream (data, size, false), true))
                return std::unique_ptr<juce::AudioFormatReader> (r);
        }
        return nullptr;
    }

    SampleManager::DecodedFile SampleManager::decodeFile (const juce::File& file, const state::SampleFile* expected,
                                                         std::optional<std::uint64_t> partStart, std::uint64_t requestId, bool keepOriginal)
    {
        DecodedFile out;
        auto& rec = out.record;
        if (expected != nullptr)
            rec = *expected;
        rec.path = file.getFullPathName().toStdString();
        if (expected == nullptr)
            rec.displayName = file.getFileName().toStdString();

        if (! file.existsAsFile())
        {
            out.missing = true;
            out.error = "Could not find that file";
            return out;
        }
        if (rec.path.size() > state::kMaxStr16)
        {
            out.error = "The file's path is too long to keep in the project";
            return out;
        }

        // The SHA-256 of the original bytes (plan 3): the project finds the file again only while it is the same. A file
        // the project keeps whole without asking (M7 V3, at most 20 MB) is read into memory once, after a SAUD header's room:
        // hashed and decoded from there, its bytes stay with the sound, ready to save. (A larger one is read again only if
        // the person says Yes.)
        rec.fileSize = static_cast<std::uint64_t> (std::max<juce::int64> (0, file.getSize()));
        const bool inMemory = keepOriginal && rec.fileSize > 0 && rec.fileSize <= kEmbedWithoutAsking;
        std::shared_ptr<state::Bytes> payload;
        {
            juce::FileInputStream in (file);
            if (! in.openedOk())
            {
                out.error = "Could not open that file";
                return out;
            }
            Sha256 hash;
            if (inMemory)
            {
                const auto total = static_cast<std::size_t> (rec.fileSize);
                payload = std::make_shared<state::Bytes> (state::kSaudHeaderBytes + total, std::uint8_t { 0 });
                std::uint8_t* bytes = payload->data() + state::kSaudHeaderBytes;
                std::size_t done = 0;
                while (done < total)
                {
                    if (supersededBy (requestId))
                    {
                        out.error = "stopped";
                        return out;
                    }
                    const auto want = static_cast<int> (std::min<std::size_t> (std::size_t { 1 } << 20, total - done));
                    const int n = in.read (bytes + done, want);
                    if (n <= 0)
                        break;
                    hash.update (bytes + done, static_cast<std::size_t> (n));
                    done += static_cast<std::size_t> (n);
                }
                if (done != total || ! in.isExhausted())
                {
                    out.error = "Could not read that file";   // it changed while it was read
                    return out;
                }
            }
            else
            {
                std::vector<char> buffer (1u << 20);
                for (;;)
                {
                    if (supersededBy (requestId))
                    {
                        out.error = "stopped";
                        return out;
                    }
                    const int n = in.read (buffer.data(), static_cast<int> (buffer.size()));
                    if (n <= 0)
                        break;
                    hash.update (buffer.data(), static_cast<std::size_t> (n));
                }
            }
            rec.sha256 = hash.finish();
        }
        if (expected != nullptr && (expected->fileSize != rec.fileSize || expected->sha256 != rec.sha256))
        {
            out.changed = true;
            out.error = "That file has changed";
            return out;
        }

        std::unique_ptr<juce::AudioFormatReader> reader;
        if (payload != nullptr)
            reader = readerForBytes (payload->data() + state::kSaudHeaderBytes, static_cast<std::size_t> (rec.fileSize), file.getFileName());
        else
            reader.reset (formats->createReaderFor (file));
        if (reader == nullptr)
        {
            out.error = "Could not read that file as sound";
            return out;
        }
        out = decodeReader (*reader, std::move (out), expected, partStart, requestId);
        if (out.error.empty() && payload != nullptr)
        {
            // The SAUD header (encoding 0): the file's channels and rate, and the loaded range (plan 3).
            state::SaudHeader h;
            h.encoding = state::kSaudOriginal;
            h.channels = out.record.channels;
            h.sampleRate = out.record.fileSampleRate;
            h.excerptStartFrame = out.record.loadedStartFrame;
            h.excerptFrames = out.record.loadedFrames;
            h.byteLength = out.record.fileSize;
            state::patchSaudHeader (*payload, h);
            out.original = std::move (payload);
        }
        return out;
    }

    SampleManager::DecodedFile SampleManager::decodeEmbedded (const state::PayloadPtr& saud, const state::SampleFile& expected,
                                                             std::uint64_t requestId)
    {
        DecodedFile out;
        out.record = expected;
        state::SaudView v;
        if (saud == nullptr || ! state::decodeSaud (saud->data(), saud->size(), v) || ! v.known())
        {
            out.error = "nothing kept";
            return out;
        }
        const auto& h = v.header;
        const juce::String name = juce::String::fromUTF8 ((expected.displayName.empty() ? expected.path : expected.displayName).c_str());

        if (h.encoding == state::kSaudOriginal)
        {
            // The file's own bytes: they must be the saved file (its size and SHA-256), and decode as the file does.
            if (h.byteLength != expected.fileSize || sha256 (v.bytes, static_cast<std::size_t> (h.byteLength)) != expected.sha256)
            {
                out.error = "The copy kept in the project is not the saved file";
                return out;
            }
            auto reader = readerForBytes (v.bytes, static_cast<std::size_t> (h.byteLength), name);
            if (reader == nullptr)
            {
                out.error = "Could not read the copy kept in the project";
                return out;
            }
            out = decodeReader (*reader, std::move (out), &expected, std::nullopt, requestId);
            if (out.error.empty())
            {
                out.source = SoundSource::embedded;
                out.original = saud;
            }
            return out;
        }

        // The samples themselves (FLAC 24-bit, or 16-bit PCM): at most 10 minutes of the file at its own rate.
        const double sr = h.sampleRate;
        const auto frames = static_cast<std::int64_t> (h.excerptFrames);
        const int channels = static_cast<int> (std::min<std::uint32_t> (2u, h.channels));
        if (! (std::isfinite (sr) && sr > 0.0) || channels < 1 || frames < kMinFileFrames
            || static_cast<double> (frames) > std::floor (kMaxFileSeconds * sr)
            || (expected.fileFrames > 0 && h.excerptStartFrame + h.excerptFrames > expected.fileFrames))
        {
            out.error = "The sound kept in the project does not fit its file";
            return out;
        }
        out.i16 = static_cast<double> (frames) / sr > kInt16Seconds;
        out.channels = channels;
        out.sampleRate = sr;
        const auto n = static_cast<std::size_t> (frames);
        if (out.i16)
        {
            out.left16.resize (n);
            if (channels > 1)
                out.right16.resize (n);
        }
        else
        {
            out.left.resize (n);
            if (channels > 1)
                out.right.resize (n);
        }
        // value (channel c, frame i) as the float it stands for: exact for 16 and 24 bits (a power-of-two scale)
        auto put = [&out] (int c, std::size_t i, float value)
        {
            if (out.i16)
                (c == 0 ? out.left16 : out.right16)[i] = engine::toInt16 (value);
            else
                (c == 0 ? out.left : out.right)[i] = value;
        };

        if (h.encoding == state::kSaudPcm16)
        {
            const auto fileChannels = static_cast<std::uint64_t> (h.channels);
            if (h.byteLength != h.excerptFrames * fileChannels * 2u)
            {
                out.error = "The sound kept in the project is damaged";
                return out;
            }
            for (std::size_t i = 0; i < n; ++i)
            {
                if ((i & 0xffffu) == 0 && supersededBy (requestId))
                {
                    out.error = "stopped";
                    return out;
                }
                for (int c = 0; c < channels; ++c)
                {
                    const std::size_t at = (i * static_cast<std::size_t> (fileChannels) + static_cast<std::size_t> (c)) * 2u;
                    const auto s = static_cast<std::int16_t> (static_cast<std::uint16_t> (v.bytes[at] | (v.bytes[at + 1] << 8)));
                    put (c, i, static_cast<float> (s) * (1.0f / 32768.0f));
                }
            }
        }
        else
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (v.bytes, static_cast<std::size_t> (h.byteLength), false), true));
            if (reader == nullptr || reader->usesFloatingPointData || reader->lengthInSamples < frames || reader->numChannels < 1)
            {
                out.error = "Could not read the sound kept in the project";
                return out;
            }
            // Left-justified 32-bit values: v x 2^-31 is the sample (exact for 24 bits and fewer).
            constexpr int chunk = 1 << 16;
            std::vector<int> l (static_cast<std::size_t> (chunk)), r (static_cast<std::size_t> (chunk));
            for (std::int64_t done = 0; done < frames; done += chunk)
            {
                if (supersededBy (requestId))
                {
                    out.error = "stopped";
                    return out;
                }
                const int count = static_cast<int> (std::min<std::int64_t> (chunk, frames - done));
                int* dest[2] = { l.data(), channels > 1 ? r.data() : nullptr };
                if (! reader->read (dest, channels, done, count, false))
                {
                    out.error = "Could not read the sound kept in the project";
                    return out;
                }
                for (int i = 0; i < count; ++i)
                {
                    const auto at = static_cast<std::size_t> (done + i);
                    put (0, at, static_cast<float> (l[static_cast<std::size_t> (i)]) * (1.0f / 2147483648.0f));
                    if (channels > 1)
                        put (1, at, static_cast<float> (r[static_cast<std::size_t> (i)]) * (1.0f / 2147483648.0f));
                }
            }
        }

        // The whole loaded range plays as the sound itself; a part of it (a Region excerpt) maps the Region onto it.
        out.cut = expected.loadedStartFrame > 0 || (expected.fileFrames > 0 && expected.loadedFrames < expected.fileFrames);
        const bool whole = expected.loadedFrames == 0
                        || (h.excerptStartFrame == expected.loadedStartFrame && h.excerptFrames == expected.loadedFrames);
        out.source = whole ? SoundSource::embedded : SoundSource::excerpt;
        out.excerpt = ! whole;
        out.excerptStartFrame = h.excerptStartFrame;
        out.excerptFrames = h.excerptFrames;
        if (whole && expected.loadedFrames == 0)
        {
            out.record.loadedStartFrame = h.excerptStartFrame;
            out.record.loadedFrames = h.excerptFrames;
        }
        return out;
    }

    SampleManager::DecodedFile SampleManager::decodeReader (juce::AudioFormatReader& reader, DecodedFile out, const state::SampleFile* expected,
                                                           std::optional<std::uint64_t> partStart, std::uint64_t requestId)
    {
        auto& rec = out.record;
        const double sr = reader.sampleRate;
        const int channels = static_cast<int> (std::min (2u, reader.numChannels));
        const juce::int64 length = reader.lengthInSamples;
        if (! (sr > 0.0) || ! std::isfinite (sr) || channels < 1 || length < kMinFileFrames)
        {
            out.error = "Could not read that file as sound";
            return out;
        }
        rec.fileSampleRate = sr;
        rec.channels = reader.numChannels;
        rec.fileFrames = static_cast<std::uint64_t> (length);

        // The part to load (question 9.3, M7): the project's (the same frames again, at most 10 minutes), the one the
        // user picked, or the first 10 minutes of a longer file.
        const auto maxFrames = static_cast<juce::int64> (std::floor (kMaxFileSeconds * sr));
        juce::int64 start = 0, count = std::min (length, maxFrames);
        if (expected != nullptr && expected->loadedFrames > 0)
        {
            start = static_cast<juce::int64> (std::min<std::uint64_t> (expected->loadedStartFrame, static_cast<std::uint64_t> (length)));
            count = static_cast<juce::int64> (std::min<std::uint64_t> (expected->loadedFrames, static_cast<std::uint64_t> (length - start)));
            count = std::min (count, maxFrames);
        }
        else if (partStart.has_value() && length > maxFrames)
        {
            // the part the user picked: from there, 10 minutes or up to the end of the file
            start = static_cast<juce::int64> (std::min<std::uint64_t> (*partStart, static_cast<std::uint64_t> (length - kMinFileFrames)));
            count = std::min (maxFrames, length - start);
        }
        if (count < kMinFileFrames)
        {
            out.error = "Could not read that file as sound";
            return out;
        }
        out.cut = start > 0 || count < length;
        rec.loadedStartFrame = static_cast<std::uint64_t> (start);
        rec.loadedFrames = static_cast<std::uint64_t> (count);
        out.channels = channels;

        // A part longer than 3 minutes is kept as 16-bit samples (plan 8.2 #14): each chunk is decoded to floats and
        // converted, so the whole part never exists as floats.
        constexpr juce::int64 chunk = 1 << 16;
        out.i16 = static_cast<double> (count) / sr > kInt16Seconds;
        if (out.i16)
        {
            out.left16.resize (static_cast<std::size_t> (count));
            if (channels > 1)
                out.right16.resize (static_cast<std::size_t> (count));
            std::vector<float> scratchL (static_cast<std::size_t> (chunk)), scratchR (static_cast<std::size_t> (channels > 1 ? chunk : 0));
            for (juce::int64 done = 0; done < count; done += chunk)
            {
                if (supersededBy (requestId))
                {
                    out.error = "stopped";
                    return out;
                }
                const int n = static_cast<int> (std::min (chunk, count - done));
                float* dest[2] = { scratchL.data(), channels > 1 ? scratchR.data() : nullptr };
                if (! reader.read (dest, channels, start + done, n))
                {
                    out.error = "Could not read that file as sound";
                    return out;
                }
                std::int16_t* l = out.left16.data() + done;
                for (int i = 0; i < n; ++i)
                    l[i] = engine::toInt16 (scratchL[static_cast<std::size_t> (i)]);
                if (channels > 1)
                {
                    std::int16_t* rr = out.right16.data() + done;
                    for (int i = 0; i < n; ++i)
                        rr[i] = engine::toInt16 (scratchR[static_cast<std::size_t> (i)]);
                }
            }
        }
        else
        {
            out.left.resize (static_cast<std::size_t> (count));
            if (channels > 1)
                out.right.resize (static_cast<std::size_t> (count));
            for (juce::int64 done = 0; done < count; done += chunk)
            {
                if (supersededBy (requestId))
                {
                    out.error = "stopped";
                    return out;
                }
                const int n = static_cast<int> (std::min (chunk, count - done));
                float* dest[2] = { out.left.data() + done, channels > 1 ? out.right.data() + done : nullptr };
                if (! reader.read (dest, channels, start + done, n))
                {
                    out.error = "Could not read that file as sound";
                    return out;
                }
            }
        }
        out.sampleRate = sr;
        return out;
    }

    SampleManager::Made SampleManager::fileSound (DecodedFile decoded)
    {
        Made m;
        const std::string name = decoded.record.displayName;
        const int channels = decoded.channels;
        if (decoded.i16)
            m.data = engine::makeSampleData16 (std::move (decoded.left16), std::move (decoded.right16), decoded.sampleRate, name);
        else
            m.data = engine::makeSampleData (std::move (decoded.left), std::move (decoded.right), decoded.sampleRate, name);
        if (m.data == nullptr)
            return m;
        // M7 V3: a Region excerpt kept in the project plays the Region mapped onto it (ParamBridge::readInst).
        if (decoded.excerpt && decoded.record.loadedFrames > 0)
        {
            m.data->excerpt.active = true;
            m.data->excerpt.base = static_cast<double> (decoded.record.loadedStartFrame) - static_cast<double> (decoded.excerptStartFrame);
            m.data->excerpt.span = static_cast<double> (decoded.record.loadedFrames);
        }
        auto view = std::make_shared<SoundView>();
        viewOf (*view, *m.data);
        view->channels = channels;
        view->name = name;
        view->builtIn = false;
        view->cut = decoded.cut;
        view->ref.kind = state::kSampleFile;
        view->ref.starterId.clear();
        view->ref.file = decoded.record;
        view->source = decoded.source;
        view->original = std::move (decoded.original);
        view->excerptStartFrame = decoded.excerptStartFrame;
        view->excerptFrames = decoded.excerptFrames;
        m.view = std::move (view);
        return m;
    }

    //==============================================================================================
    SoundAnalysis SampleManager::analyseSound (const SoundView& v, const analysis::Stop& stop)
    {
        const auto stopped = [&stop] { return stop && stop(); };
        SoundAnalysis a;
        if (! v.hasSamples() || v.frames <= 0)
        {
            a.analysed = true;
            return a;
        }
        const ViewSource whole (v, 0, v.frames);
        a.parts = analysis::suggestParts (whole, stop);
        if (stopped())
            return {};
        if (! v.builtIn)
        {
            // sl-ui.js runDetect at a load: the Region is the whole file, so a song's pitch is not asked for (a whole
            // song's pitch means nothing); the key is the whole file's, and dropped when it sounds like noise.
            const double secs = v.seconds();
            if (secs <= kLongFileSeconds && secs >= 0.05)
                a.pitch = analysis::detectPitch (whole, stop);
            if (stopped())
                return {};
            a.key = analysis::detectKey (whole, stop);
            if (stopped())
                return {};
            if (! a.pitch.has_value() && a.key.has_value())
            {
                const bool noisy = noiseLike (whole, stop);
                if (stopped())
                    return {};
                if (noisy)
                    a.key.reset();
            }
        }
        a.analysed = true;
        return a;
    }

    void SampleManager::runJob (const Job& j)
    {
        if (j.kind == JobKind::embed)
        {
            runEmbed (j);
            return;
        }
        if (j.kind == JobKind::library)
        {
            runLibrary (j);
            return;
        }
        const auto kindIndex = static_cast<int> (j.kind);
        const analysis::Stop stop = [this, &j, kindIndex]
        {
            return supersededBy (j.afterRequest) || newestJob[kindIndex].load (std::memory_order_acquire) > j.id;
        };
        const auto stopped = [this, &stop]
        {
            if (! stop())
                return false;
            const std::lock_guard<std::mutex> hold (lock);
            ++stops;
            return true;
        };
        const SoundView& v = *j.view;

        if (j.kind == JobKind::overview)
        {
            // The whole file, coarsely: the mid's lowest and highest value in each of kOverviewBins stretches.
            auto ov = std::make_shared<FileOverview>();
            ov->path = v.ref.file.path;
            const juce::String path = juce::String::fromUTF8 (ov->path.c_str());
            if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
                return;
            std::unique_ptr<juce::AudioFormatReader> reader (formats->createReaderFor (juce::File (path)));
            if (reader == nullptr || reader->lengthInSamples <= 0)
                return;
            const juce::int64 length = reader->lengthInSamples;
            const int channels = static_cast<int> (std::min (2u, reader->numChannels));
            ov->fileFrames = static_cast<std::uint64_t> (length);
            ov->sampleRate = reader->sampleRate;
            std::vector<float> lo (static_cast<std::size_t> (kOverviewBins), 1e9f), hi (static_cast<std::size_t> (kOverviewBins), -1e9f);
            constexpr juce::int64 chunk = 1 << 16;
            std::vector<float> l (static_cast<std::size_t> (chunk)), r (static_cast<std::size_t> (chunk));
            for (juce::int64 done = 0; done < length; done += chunk)
            {
                if (stopped())
                    return;
                const int n = static_cast<int> (std::min (chunk, length - done));
                float* dest[2] = { l.data(), channels > 1 ? r.data() : nullptr };
                if (! reader->read (dest, channels, done, n))
                    return;
                for (int i = 0; i < n; ++i)
                {
                    const auto k = static_cast<std::size_t> (static_cast<juce::int64> (done + i) * kOverviewBins / length);
                    const auto at = static_cast<std::size_t> (i);
                    const float mid = channels > 1 ? (l[at] + r[at]) * 0.5f : l[at];
                    lo[k] = std::min (lo[k], mid);
                    hi[k] = std::max (hi[k], mid);
                }
            }
            for (std::size_t k = 0; k < lo.size(); ++k)
                if (lo[k] > hi[k])
                    lo[k] = hi[k] = 0.0f;
            ov->lo = std::move (lo);
            ov->hi = std::move (hi);
            ov->ok = true;
            {
                const std::lock_guard<std::mutex> hold (lock);
                lastOverview = std::move (ov);
            }
            if (onJobDone)
                onJobDone();
            return;
        }

        JobResult res;
        res.kind = j.kind;
        res.job = j.id;
        res.soundRequest = v.request;
        res.regionStart = j.regionStart;
        res.regionEnd = j.regionEnd;
        res.whole = regionWhole (j.regionStart, j.regionEnd);
        res.longFile = v.seconds() > kLongFileSeconds;
        // The Region is a fraction of the loaded range; a Region excerpt kept in the project holds part of it (M7 V3).
        const auto [fa, fb] = regionFrames (v.frames, v.toSound (j.regionStart), v.toSound (j.regionEnd));
        const ViewSource listen (v, res.whole && ! v.excerpt.active ? 0 : fa, res.whole && ! v.excerpt.active ? v.frames : fb - fa);
        const double lenS = static_cast<double> (listen.numFrames()) / v.sampleRate;

        if (j.kind == JobKind::detect)
        {
            // sl-ui.js runDetect 'button': the pitch of the Region (not of a whole song), the whole sound's key, and no
            // key for noise.
            if (! (res.longFile && res.whole) && lenS >= 0.05 && lenS <= 60.0)
            {
                res.listened = true;
                res.pitch = analysis::detectPitch (listen, stop);
            }
            if (stopped())
                return;
            const ViewSource whole (v, 0, v.frames);
            res.key = analysis::detectKey (whole, stop);
            if (stopped())
                return;
            if (! res.pitch.has_value())
            {
                res.noisy = noiseLike (whole, stop);
                if (stopped())
                    return;
                if (res.noisy)
                    res.key.reset();
            }
        }
        else
        {
            // sl-ui.js suggestRoot: a part of a file, 0.05 .. 30 s long.
            if (! v.builtIn && ! res.whole && lenS >= 0.05 && lenS <= 30.0)
            {
                res.listened = true;
                res.pitch = analysis::detectPitch (listen, stop);
            }
            if (stopped())
                return;
        }
        {
            const std::lock_guard<std::mutex> hold (lock);
            ++jobRuns;
            results.push_back (std::move (res));
        }
        if (onJobDone)
            onJobDone();
    }

    //==============================================================================================
    // M7 V3: keeping the sound with the project, and the library.

    void SampleManager::runEmbed (const Job& j)
    {
        const auto kindIndex = static_cast<int> (JobKind::embed);
        const auto stop = [this, &j, kindIndex]
        {
            return supersededBy (j.afterRequest) || newestJob[kindIndex].load (std::memory_order_acquire) > j.id;
        };
        const SoundView& v = *j.view;
        const auto& f = v.ref.file;
        EmbedResult res;
        res.job = j.id;
        res.soundRequest = v.request;
        res.sound = v.ref;

        auto deliver = [this, &res]
        {
            {
                const std::lock_guard<std::mutex> hold (lock);
                ++jobRuns;
                embedResults.push_back (std::move (res));
            }
            if (onJobDone)
                onJobDone();
        };
        auto countStop = [this]
        {
            const std::lock_guard<std::mutex> hold (lock);
            ++stops;
        };

        // A FLAC 24-bit copy of frames [from, from + count) of the sound as it plays: each sample rounded to the nearest
        // 24-bit step, so a 16-bit sound keeps every bit and a float one is within 2^-24 of it. Never over 50 MB.
        auto excerptOf = [&] (std::int64_t from, std::int64_t count)
        {
            const int channels = v.stereo ? 2 : 1;
            const double sr = v.sampleRate;
            if (count < 8 || ! (std::isfinite (sr) && sr > 0.0))
                return;   // nothing to keep
            // SAUD's header holds the true rate; FLAC's own field takes it when it is a rate FLAC's encoder accepts (a whole
            // number, and above 65535 a multiple of 10), else 48 kHz, which nothing reads.
            const auto wholeRate = static_cast<std::uint32_t> (std::min (sr, 1.0e6));
            const bool flacTakesIt = juce::exactlyEqual (static_cast<double> (wholeRate), sr) && wholeRate >= 1u
                                  && (wholeRate < 65536u || (wholeRate < 655360u && wholeRate % 10u == 0u));
            const double flacRate = flacTakesIt ? sr : 48000.0;
            auto payload = std::make_shared<state::Bytes>();
            const std::size_t room = state::kMaxSaudBytes - state::kSaudHeaderBytes;
            const auto raw = static_cast<std::size_t> (count) * static_cast<std::size_t> (channels) * 3u + 65536u;
            payload->reserve (state::kSaudHeaderBytes + std::min (room, raw));
            payload->resize (state::kSaudHeaderBytes, 0);
            bool overflow = false, failed = false;
            {
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<BytesOutput> (*payload, state::kSaudHeaderBytes, room, overflow);
                juce::FlacAudioFormat flac;
                const auto options = juce::AudioFormatWriterOptions {}
                                         .withSampleRate (flacRate)
                                         .withNumChannels (channels)
                                         .withBitsPerSample (24);
                auto writer = flac.createWriterFor (stream, options);
                if (writer == nullptr)
                    return;
                constexpr int block = 1 << 15;
                std::vector<int> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (channels > 1 ? block : 0));
                for (std::int64_t done = 0; done < count && ! overflow; done += block)
                {
                    if (stop())
                    {
                        countStop();
                        res.stopped = true;
                        return;
                    }
                    const int n = static_cast<int> (std::min<std::int64_t> (block, count - done));
                    for (int i = 0; i < n; ++i)
                    {
                        // left-justified in 32 bits, as FlacWriter takes them (it shifts them back down by 8)
                        const std::int64_t at = from + done + i;
                        l[static_cast<std::size_t> (i)] = toInt24 (v.sample (0, at)) * 256;
                        if (channels > 1)
                            r[static_cast<std::size_t> (i)] = toInt24 (v.sample (1, at)) * 256;
                    }
                    const int* chans[3] = { l.data(), channels > 1 ? r.data() : nullptr, nullptr };
                    if (! writer->write (chans, n))
                        failed = ! overflow;
                    if (failed)
                        break;
                }
                writer.reset();   // finishes the stream: FLAC's header goes back at its start
            }
            if (overflow)
            {
                res.tooLong = true;
                return;
            }
            if (failed)
                return;
            state::SaudHeader h;
            h.encoding = state::kSaudFlac24;
            h.channels = static_cast<std::uint32_t> (channels);
            h.sampleRate = sr;
            h.excerptStartFrame = v.fileFrameOf (from);
            h.excerptFrames = static_cast<std::uint64_t> (count);
            h.byteLength = static_cast<std::uint64_t> (payload->size() - state::kSaudHeaderBytes);
            state::patchSaudHeader (*payload, h);
            res.header = h;
            res.saud = std::move (payload);
        };

        if (v.ref.kind != state::kSampleFile || ! v.hasSamples() || v.frames <= 0)
        {
            deliver();
            return;
        }

        if (j.original)
        {
            // The file's own bytes: kept with the sound since its load, or read again from its path, else from the library
            // copy, each only while its size and SHA-256 are the saved file's.
            if (v.original != nullptr)
            {
                state::SaudView sv;
                if (state::decodeSaud (v.original->data(), v.original->size(), sv))
                    res.header = sv.header;
                res.saud = v.original;
                deliver();
                return;
            }
            std::vector<juce::File> places;
            if (const auto at = fileAt (f.path))
                places.push_back (*at);
            if (isLibraryOn())
                places.push_back (libraryFileFor (f));
            for (const auto& file : places)
            {
                if (! file.existsAsFile() || static_cast<std::uint64_t> (file.getSize()) != f.fileSize || f.fileSize > kEmbedMost || f.fileSize == 0)
                    continue;
                juce::FileInputStream in (file);
                if (! in.openedOk())
                    continue;
                const auto total = static_cast<std::size_t> (f.fileSize);
                auto payload = std::make_shared<state::Bytes> (state::kSaudHeaderBytes + total, std::uint8_t { 0 });
                std::uint8_t* bytes = payload->data() + state::kSaudHeaderBytes;
                Sha256 hash;
                std::size_t done = 0;
                while (done < total)
                {
                    if (stop())
                    {
                        countStop();
                        res.stopped = true;
                        deliver();
                        return;
                    }
                    const auto want = static_cast<int> (std::min<std::size_t> (std::size_t { 1 } << 20, total - done));
                    const int n = in.read (bytes + done, want);
                    if (n <= 0)
                        break;
                    hash.update (bytes + done, static_cast<std::size_t> (n));
                    done += static_cast<std::size_t> (n);
                }
                if (done != total || hash.finish() != f.sha256)
                    continue;
                state::SaudHeader h;
                h.encoding = state::kSaudOriginal;
                h.channels = f.channels;
                h.sampleRate = f.fileSampleRate;
                h.excerptStartFrame = f.loadedStartFrame;
                h.excerptFrames = f.loadedFrames;
                h.byteLength = f.fileSize;
                state::patchSaudHeader (*payload, h);
                res.header = h;
                res.saud = std::move (payload);
                deliver();
                return;
            }
            // Neither can be read: the sound as it plays, the whole of it.
            res.fromMemory = true;
            excerptOf (0, v.frames);
            deliver();
            return;
        }

        excerptOf (j.fromFrame, j.frameCount);
        deliver();
    }

    void SampleManager::runLibrary (const Job& j)
    {
        const SoundView& v = *j.view;
        const auto folder = libraryFolder();
        auto measure = [this, &folder]
        {
            std::int64_t total = 0;
            if (folder.isDirectory())
                for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
                    total += entry.getFileSize();
            librarySize.store (total, std::memory_order_release);
        };

        const auto& f = v.ref.file;
        if (isLibraryOn() && v.ref.kind == state::kSampleFile && ! v.missing && f.fileSize > 0)
        {
            const auto target = libraryFileFor (f);
            const bool there = target.existsAsFile() && static_cast<std::uint64_t> (target.getSize()) == f.fileSize;
            if (! there && folder.createDirectory().wasOk())
            {
                // Written next to it first, then renamed, so a copy cut short never looks like a whole one.
                const auto part = target.getSiblingFile (target.getFileName() + ".part");
                bool ok = false;
                state::SaudView sv;
                if (v.original != nullptr && state::decodeSaud (v.original->data(), v.original->size(), sv)
                    && sv.header.encoding == state::kSaudOriginal && sv.header.byteLength == f.fileSize)
                {
                    // the bytes read at the load (their SHA-256 was the file's then)
                    ok = part.replaceWithData (sv.bytes, static_cast<std::size_t> (sv.header.byteLength));
                }
                else if (const auto from = fileAt (f.path); from.has_value() && from->existsAsFile())
                {
                    // from the file at its path, hashed while it is copied
                    juce::FileInputStream in (*from);
                    part.deleteFile();
                    juce::FileOutputStream os (part);
                    if (in.openedOk() && os.openedOk())
                    {
                        Sha256 hash;
                        std::vector<char> buffer (1u << 20);
                        ok = true;
                        for (;;)
                        {
                            const int n = in.read (buffer.data(), static_cast<int> (buffer.size()));
                            if (n <= 0)
                                break;
                            hash.update (buffer.data(), static_cast<std::size_t> (n));
                            ok = ok && os.write (buffer.data(), static_cast<std::size_t> (n));
                        }
                        os.flush();
                        ok = ok && os.getStatus().wasOk() && hash.finish() == f.sha256;
                    }
                }
                if (ok && part.moveFileTo (target))
                    libraryCopied.fetch_add (1, std::memory_order_acq_rel);
                else
                    part.deleteFile();
            }
        }
        measure();
        {
            const std::lock_guard<std::mutex> hold (lock);
            ++jobRuns;
        }
        if (onJobDone)
            onJobDone();
    }

    //==============================================================================================
    void SampleManager::process (const Request& r)
    {
        LoadReport report;
        report.request = r.id;
        report.origin = r.origin;
        if (r.kind == Kind::starter)
            report.name = sounds::starterInfo (r.starter).name;
        else if (r.kind == Kind::file || r.kind == Kind::locate)
            report.name = r.file.getFileName().toStdString();
        else
            report.name = r.ref.kind == state::kSampleStarter ? r.ref.starterId
                                                              : (r.ref.file.displayName.empty() ? r.ref.file.path : r.ref.file.displayName);

        const analysis::Stop stop = [this, id = r.id] { return supersededBy (id); };

        // A request a newer one overtook: nothing to report, the newer one comes next.
        auto dropStopped = [&]
        {
            {
                const std::lock_guard<std::mutex> hold (lock);
                ++stops;
                finished = std::max (finished, r.id);
                finishedAtomic.store (finished, std::memory_order_release);
                working = false;
            }
            finishedChanged.notify_all();
        };

        auto handOver = [&] (Made m, double a, double b, std::optional<RootHeard> heard, std::optional<analysis::Part> part)
        {
            m.view->request = r.id;
            m.view->origin = r.origin;
            report.ok = true;
            report.name = m.view->name;
            if (r.origin == LoadOrigin::user)
            {
                auto p = std::make_unique<PendingSound>();
                p->request = r.id;
                p->data = std::move (m.data);
                p->view = std::move (m.view);
                p->patch = r.kind == Kind::locate ? core::InstPatch {} : soundLoadPatch (a, b);   // Locate keeps every setting
                p->locate = r.kind == Kind::locate;
                p->regionStartExact = a;
                p->regionEndExact = b;
                p->report = report;
                p->rootHeard = heard;
                p->part = std::move (part);
                finish (r.id, report, nullptr, nullptr, std::move (p));
            }
            else
            {
                auto data = std::move (m.data);
                finish (r.id, report, std::move (data), std::move (m.view), nullptr);
            }
        };

        // A starter, by its own request or by a project's reference. Its suggested parts (kept with it in the cache of
        // 4) are made right after it is handed on, as a job's result, so a state load waiting for the starter never
        // waits for them (decision 3: the corruption tests' slowest loads make a starter).
        auto starter = [&] (sounds::StarterId id, std::int32_t seed)
        {
            Made m = starterSound (id, seed);
            if (m.data == nullptr)
            {
                report.message = "Could not make the starter sound";
                finish (r.id, report, nullptr, nullptr, nullptr);
                return;
            }
            const bool needParts = ! m.view->analysis.analysed;
            std::shared_ptr<const SoundView> view = m.view;
            if (needParts)
            {
                const std::lock_guard<std::mutex> hold (lock);
                ++jobsAsked;   // counted before the hand-over, so waitJobs never misses it
            }
            handOver (std::move (m), 0.0, 1.0, std::nullopt, std::nullopt);
            if (! needParts)
                return;
            auto a = supersededBy (r.id) ? SoundAnalysis {} : analyseSound (*view, stop);
            {
                const std::lock_guard<std::mutex> hold (lock);
                if (a.analysed)
                {
                    JobResult res;
                    res.kind = JobKind::parts;
                    res.soundRequest = r.id;
                    res.parts = a.parts;
                    results.push_back (std::move (res));
                    ++jobRuns;
                }
                else
                {
                    ++stops;
                }
                ++jobsDone;
            }
            if (a.analysed)
            {
                for (auto& c : starters)
                    if (c.id == id && c.seed == seed)
                        c.parts = std::make_shared<const std::vector<analysis::Part>> (a.parts);
                if (onJobDone)
                    onJobDone();
            }
            finishedChanged.notify_all();
        };

        if (r.kind == Kind::starter)
        {
            starter (r.starter, r.seed);
            return;
        }

        if (r.kind == Kind::ref && r.ref.kind == state::kSampleStarter)
        {
            if (const auto id = sounds::parseStarter (r.ref.starterId))
            {
                starter (*id, r.ref.seed);
                return;
            }
            // A starter this version does not have: the default plays, and the reference is kept.
            Made m = starterSound (sounds::kDefaultStarter, 1);
            report.message = "This project uses a built-in sound this StrandLock does not have, so the " + defaultStarterName() + " plays";
            report.ok = false;
            if (m.data == nullptr)
            {
                finish (r.id, report, nullptr, nullptr, nullptr);
                return;
            }
            m.view->ref = r.ref;
            m.view->missing = true;
            m.view->request = r.id;
            auto data = std::move (m.data);
            finish (r.id, report, std::move (data), std::move (m.view), nullptr);
            return;
        }

        // A file: a new one the user picked, the one a project names, or the one Locate picked for it (M7 V3).
        const auto t0 = std::chrono::steady_clock::now();
        const bool fromProject = r.kind == Kind::ref;
        const bool locating = r.kind == Kind::locate;
        const juce::String savedPath = juce::String::fromUTF8 (r.ref.file.path.c_str());
        const bool usablePath = ! fromProject || (savedPath.isNotEmpty() && juce::File::isAbsolutePath (savedPath));
        // juce::File asserts on a relative path, so a saved path from another system is not even made into one.
        const juce::File file = fromProject ? (usablePath ? juce::File (savedPath) : juce::File()) : r.file;
        const std::string projectName = r.ref.file.displayName.empty() ? r.ref.file.path : r.ref.file.displayName;

        DecodedFile decoded;
        bool otherFile = false;   // Locate picked another file than the one the project was saved with
        if (locating)
        {
            decoded = decodeFile (file, &r.ref.file, std::nullopt, r.id, r.keepOriginal);
            if (decoded.changed)
            {
                // Another file: loaded as a new one, from the project's part when it is long enough.
                otherFile = true;
                std::optional<std::uint64_t> part;
                if (r.ref.file.loadedStartFrame > 0)
                    part = r.ref.file.loadedStartFrame;
                decoded = decodeFile (file, nullptr, part, r.id, r.keepOriginal);
            }
        }
        else if (usablePath)
        {
            decoded = decodeFile (file, fromProject ? &r.ref.file : nullptr, r.partStart, r.id, r.keepOriginal);
        }
        if (fromProject && decoded.error.empty() && decoded.frames() == 0)
        {
            decoded.missing = true;
            decoded.error = "Could not find that file";
        }
        if (decoded.error == "stopped")
        {
            // Shutting down, or a newer request came: nothing to report.
            dropStopped();
            return;
        }

        // M7 V3 (plan 3): a project's file that is not at its path, or is not the same file there any more, is looked for
        // in the library (when it is on) by its SHA-256, then in the copy the project kept (SAUD).
        std::string foundIn;   // "": at its path; else "library", "project" (the whole copy) or "part" (a Region excerpt)
        std::string why;
        if (fromProject && ! decoded.error.empty())
        {
            why = decoded.changed ? "has changed since this project was saved" : "is not at " + r.ref.file.path;
            if (isLibraryOn())
            {
                const auto lib = libraryFileFor (r.ref.file);
                if (lib.existsAsFile())
                {
                    auto fromLibrary = decodeFile (lib, &r.ref.file, std::nullopt, r.id, r.keepOriginal);
                    if (fromLibrary.error == "stopped")
                    {
                        dropStopped();
                        return;
                    }
                    if (fromLibrary.error.empty() && fromLibrary.frames() > 0)
                    {
                        fromLibrary.source = SoundSource::library;
                        decoded = std::move (fromLibrary);
                        foundIn = "library";
                    }
                }
            }
            if (foundIn.empty() && r.saud != nullptr)
            {
                auto fromSaud = decodeEmbedded (r.saud, r.ref.file, r.id);
                if (fromSaud.error == "stopped")
                {
                    dropStopped();
                    return;
                }
                if (fromSaud.error.empty() && fromSaud.frames() > 0)
                {
                    decoded = std::move (fromSaud);
                    foundIn = decoded.source == SoundSource::excerpt ? "part" : "project";
                }
            }
        }

        if (! decoded.error.empty())
        {
            {
                const std::lock_guard<std::mutex> hold (lock);
                fileMs = msSince (t0);
            }
            if (! fromProject)
            {
                report.message = locating ? "Could not use that file: " + decoded.error : decoded.error;
                finish (r.id, report, nullptr, nullptr, nullptr);
                return;
            }
            // The project's file is not there (or not the same file), in the library or in the project: the default
            // starter plays and the SMPL record is kept as it was (decision 4) until Locate finds it (M7 V3).
            report.message = decoded.changed
                ? "The sound file \"" + projectName + "\" has changed since this project was saved, so the " + defaultStarterName() + " plays"
                : "Could not find the sound file \"" + projectName + "\" (" + r.ref.file.path + "), so the " + defaultStarterName() + " plays";
            report.ok = false;
            Made m = starterSound (sounds::kDefaultStarter, 1);
            if (m.view == nullptr || m.data == nullptr)
            {
                finish (r.id, report, nullptr, nullptr, nullptr);
                return;
            }
            m.view->ref = r.ref;
            m.view->missing = true;
            m.view->request = r.id;
            auto data = std::move (m.data);
            finish (r.id, report, std::move (data), std::move (m.view), nullptr);
            return;
        }

        const bool keepsRecord = fromProject || (locating && ! otherFile);   // the project's record (and its cache) stay
        const bool cut = decoded.cut && ! keepsRecord;
        const std::uint64_t partFrom = decoded.record.loadedStartFrame;
        const double fileSeconds = static_cast<double> (decoded.record.fileFrames) / decoded.sampleRate;
        const double seconds = static_cast<double> (decoded.frames()) / decoded.sampleRate;
        Made m = fileSound (std::move (decoded));
        if (m.data == nullptr)
        {
            report.message = "Could not read that file as sound";
            finish (r.id, report, nullptr, nullptr, nullptr);
            return;
        }
        if (fromProject)
            m.view->ref = r.ref;   // the record as the project wrote it (its analysis cache too)
        else if (locating && otherFile)
            m.view->ref.file.keepInProject = r.ref.file.keepInProject;   // the project's switch; the question is asked again

        // M7, decision 1: the analysis, unless the project's cache can be read (then it is not analysed again).
        if (auto cached = keepsRecord ? fromCache (r.ref.file.cache) : std::nullopt)
        {
            m.view->analysis = std::move (*cached);
        }
        else
        {
            const auto t1 = std::chrono::steady_clock::now();
            auto a = analyseSound (*m.view, stop);
            if (! a.analysed)
            {
                dropStopped();
                return;
            }
            if (m.view->source == SoundSource::excerpt)
            {
                // Only a part of the loaded range is here (M7 V3): its parts as fractions of the loaded range; the project's
                // cache stays as it was, since it would describe only this part.
                for (auto& p : a.parts)
                {
                    p.start = std::clamp (m.view->toLoaded (p.start), 0.0, 1.0);
                    p.end = std::clamp (m.view->toLoaded (p.end), p.start, 1.0);
                }
                m.view->analysis = std::move (a);
            }
            else
            {
                m.view->analysis = std::move (a);
                m.view->ref.file.cache = toCache (m.view->analysis);
            }
            const double ms = msSince (t1);
            const std::lock_guard<std::mutex> hold (lock);
            ++analyses;
            analysisMs = ms;
        }

        // The Region of a new file (U2): the whole file, or for a song (longer than 20 s) the part pickPart chooses
        // (a held note first, then a loop, a quiet texture, a hit), or 4 s from 35 % in when nothing was suggested.
        // A project's sound, and one Locate found, keep the Region they have.
        double a = 0.0, b = 1.0;
        std::optional<analysis::Part> part;
        std::optional<RootHeard> heard;
        if (! fromProject && ! locating)
        {
            if (seconds > kLongFileSeconds)
            {
                part = pickPartOf (m.view->analysis.parts);
                if (part.has_value())
                {
                    a = part->start;
                    b = part->end;
                }
                else
                {
                    a = kDefaultPartStart;
                    b = kDefaultPartStart + kDefaultPartSeconds / seconds;
                }
                // sl-ui.js suggestRoot for the new Region: its pitch, when it is 0.05 .. 30 s long.
                const double lenS = (b - a) * seconds;
                if (lenS >= 0.05 && lenS <= 30.0)
                {
                    const auto [fa, fb] = regionFrames (m.view->frames, a, b);
                    const ViewSource listen (*m.view, fa, fb - fa);
                    if (const auto pitch = analysis::detectPitch (listen, stop))
                    {
                        heard = pitchToRoot (*pitch);
                        heard->regionStart = a;
                        heard->regionEnd = b;
                    }
                    if (stop())
                    {
                        dropStopped();
                        return;
                    }
                }
            }
            else if (m.view->analysis.pitch.has_value())
            {
                heard = pitchToRoot (*m.view->analysis.pitch);   // a short file: the whole file's pitch
                heard->regionStart = 0.0;
                heard->regionEnd = 1.0;
            }
        }
        {
            const std::lock_guard<std::mutex> hold (lock);
            fileMs = msSince (t0);
        }

        if (! foundIn.empty())
            report.message = "The sound file \"" + projectName + "\" " + why + ", so StrandLock plays "
                           + (foundIn == "library" ? std::string ("the copy in your StrandLock library")
                              : foundIn == "project" ? std::string ("the copy kept in the project")
                                                     : std::string ("the part of it kept in the project (the Region and up to 1 s either side). "
                                                                    "Use Locate sample... to play all of it"));
        else if (locating)
            report.message = otherFile ? "That is not the file this project was saved with, so it may sound different. Every setting stays as it was"
                                       : "Found " + m.view->name + ": it plays with every setting as it was";
        else if (cut && partFrom == 0)
            report.message = m.view->name + " is longer than 10 minutes, so StrandLock loaded its first 10 minutes";
        else if (cut)
            report.message = "StrandLock loaded " + m.view->name + " from " + minutesText (static_cast<double> (partFrom) / m.view->sampleRate)
                           + " to " + minutesText (static_cast<double> (partFrom) / m.view->sampleRate + seconds) + " (the song is "
                           + minutesText (fileSeconds) + " long)";
        else if (! fromProject && seconds > kLongFileSeconds)
            report.message = part.has_value()
                ? m.view->name + " is a song, so the Region starts on a suggested part (" + part->label
                      + "). Drag the box to choose your own, or Use the whole file to take it all"
                : m.view->name + " is a song, so the Region starts as 4 s from 35 % in";
        if (! report.message.empty())
            report.toastMs = 5000;
        handOver (std::move (m), a, b, heard, std::move (part));
    }
}
