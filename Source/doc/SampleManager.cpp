#include "SampleManager.h"

#include "Sha256.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
            the starters are rooted at C3, and a file is until Detect, M7) and the Region. */
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
    }

    //==============================================================================================
    SampleManager::SampleManager (rt::VersionExchange<engine::SampleData>& out)
        : juce::Thread ("StrandLock sounds"), exchange (out)
    {
        formats = std::make_unique<juce::AudioFormatManager>();
        formats->registerBasicFormats();
        startThread();
    }

    SampleManager::~SampleManager()
    {
        stopThread (15000);
    }

    std::uint64_t SampleManager::enqueue (Request r)
    {
        std::uint64_t id = 0;
        {
            const std::lock_guard<std::mutex> hold (lock);
            id = ++requested;
            r.id = id;
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

    std::uint64_t SampleManager::loadFile (const juce::File& file, LoadOrigin origin)
    {
        Request r;
        r.kind = Kind::file;
        r.file = file;
        r.origin = origin;
        return enqueue (std::move (r));
    }

    std::uint64_t SampleManager::loadRef (const state::SampleRef& ref, LoadOrigin origin)
    {
        Request r;
        r.kind = Kind::ref;
        r.ref = ref;
        r.origin = origin;
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

    bool SampleManager::busy() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return working || ! queue.empty();
    }

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
            bool got = false;
            {
                const std::lock_guard<std::mutex> hold (lock);
                if (! queue.empty())
                {
                    // Only the newest one waiting is made; the others are finished as replaced.
                    r = std::move (queue.back());
                    queue.clear();
                    finished = std::max (finished, r.id - 1);
                    finishedAtomic.store (finished, std::memory_order_release);
                    working = true;
                    got = true;
                }
            }
            if (! got)
            {
                finishedChanged.notify_all();
                wait (-1.0);
                continue;
            }
            finishedChanged.notify_all();

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
        for (auto it = starters.begin(); it != starters.end(); ++it)
        {
            if (it->id == id && it->seed == seed)
            {
                sound = it->sound;
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
        starters.push_back ({ id, seed, sound });
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
        {
            view->storage = m.data->storage;
            view->left = m.data->L;
            view->right = m.data->R;
            view->frames = m.data->n;
            view->sampleRate = m.data->sr;
            view->stereo = m.data->stereo;
        }
        view->name = info.name;
        view->builtIn = true;
        view->starter = id;
        view->seed = seed;
        view->ref.kind = state::kSampleStarter;
        view->ref.starterId = info.code;
        view->ref.seed = seed;
        m.view = std::move (view);
        return m;
    }

    SampleManager::DecodedFile SampleManager::decodeFile (const juce::File& file, const state::SampleFile* expected)
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

        // The SHA-256 of the original bytes (plan 3): the project finds the file again only while it is the same.
        rec.fileSize = static_cast<std::uint64_t> (std::max<juce::int64> (0, file.getSize()));
        {
            juce::FileInputStream in (file);
            if (! in.openedOk())
            {
                out.error = "Could not open that file";
                return out;
            }
            Sha256 hash;
            std::vector<char> buffer (1u << 20);
            for (;;)
            {
                if (threadShouldExit())
                {
                    out.error = "stopped";
                    return out;
                }
                const int n = in.read (buffer.data(), static_cast<int> (buffer.size()));
                if (n <= 0)
                    break;
                hash.update (buffer.data(), static_cast<std::size_t> (n));
            }
            rec.sha256 = hash.finish();
        }
        if (expected != nullptr && (expected->fileSize != rec.fileSize || expected->sha256 != rec.sha256))
        {
            out.changed = true;
            out.error = "That file has changed";
            return out;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats->createReaderFor (file));
        if (reader == nullptr)
        {
            out.error = "Could not read that file as sound";
            return out;
        }
        const double sr = reader->sampleRate;
        const int channels = static_cast<int> (std::min (2u, reader->numChannels));
        const juce::int64 length = reader->lengthInSamples;
        if (! (sr > 0.0) || ! std::isfinite (sr) || channels < 1 || length < kMinFileFrames)
        {
            out.error = "Could not read that file as sound";
            return out;
        }
        rec.fileSampleRate = sr;
        rec.channels = reader->numChannels;
        rec.fileFrames = static_cast<std::uint64_t> (length);

        // The part to load: the project's (the same frames again), or the first 10 minutes of a new file.
        const auto maxFrames = static_cast<juce::int64> (std::floor (kMaxFileSeconds * sr));
        juce::int64 start = 0, count = std::min (length, maxFrames);
        if (expected != nullptr && expected->loadedFrames > 0)
        {
            start = static_cast<juce::int64> (std::min<std::uint64_t> (expected->loadedStartFrame, static_cast<std::uint64_t> (length)));
            count = static_cast<juce::int64> (std::min<std::uint64_t> (expected->loadedFrames, static_cast<std::uint64_t> (length - start)));
        }
        if (count < kMinFileFrames)
        {
            out.error = "Could not read that file as sound";
            return out;
        }
        out.cut = start > 0 || count < length;
        rec.loadedStartFrame = static_cast<std::uint64_t> (start);
        rec.loadedFrames = static_cast<std::uint64_t> (count);

        out.left.resize (static_cast<std::size_t> (count));
        if (channels > 1)
            out.right.resize (static_cast<std::size_t> (count));
        constexpr juce::int64 chunk = 1 << 16;
        for (juce::int64 done = 0; done < count; done += chunk)
        {
            if (threadShouldExit())
            {
                out.error = "stopped";
                return out;
            }
            const int n = static_cast<int> (std::min (chunk, count - done));
            float* dest[2] = { out.left.data() + done, channels > 1 ? out.right.data() + done : nullptr };
            if (! reader->read (dest, channels, start + done, n))
            {
                out.error = "Could not read that file as sound";
                return out;
            }
        }
        out.sampleRate = sr;
        return out;
    }

    SampleManager::Made SampleManager::fileSound (DecodedFile decoded)
    {
        Made m;
        const std::string name = decoded.record.displayName;
        m.data = engine::makeSampleData (std::move (decoded.left), std::move (decoded.right), decoded.sampleRate, name);
        if (m.data == nullptr)
            return m;
        auto view = std::make_shared<SoundView>();
        view->storage = m.data->storage;
        view->left = m.data->L;
        view->right = m.data->R;
        view->frames = m.data->n;
        view->sampleRate = m.data->sr;
        view->stereo = m.data->stereo;
        view->name = name;
        view->builtIn = false;
        view->cut = decoded.cut;
        view->ref.kind = state::kSampleFile;
        view->ref.starterId.clear();
        view->ref.file = decoded.record;
        m.view = std::move (view);
        return m;
    }

    //==============================================================================================
    void SampleManager::process (const Request& r)
    {
        LoadReport report;
        report.request = r.id;
        report.origin = r.origin;
        if (r.kind == Kind::starter)
            report.name = sounds::starterInfo (r.starter).name;
        else if (r.kind == Kind::file)
            report.name = r.file.getFileName().toStdString();
        else
            report.name = r.ref.kind == state::kSampleStarter ? r.ref.starterId
                                                              : (r.ref.file.displayName.empty() ? r.ref.file.path : r.ref.file.displayName);

        auto handOver = [&] (Made m, double a, double b)
        {
            m.view->request = r.id;
            report.ok = true;
            report.name = m.view->name;
            if (r.origin == LoadOrigin::user)
            {
                auto p = std::make_unique<PendingSound>();
                p->request = r.id;
                p->data = std::move (m.data);
                p->view = std::move (m.view);
                p->patch = soundLoadPatch (a, b);
                p->regionStartExact = a;
                p->regionEndExact = b;
                p->report = report;
                finish (r.id, report, nullptr, nullptr, std::move (p));
            }
            else
            {
                auto data = std::move (m.data);
                finish (r.id, report, std::move (data), std::move (m.view), nullptr);
            }
        };

        // A starter, by its own request or by a project's reference.
        auto starter = [&] (sounds::StarterId id, std::int32_t seed)
        {
            Made m = starterSound (id, seed);
            if (m.data == nullptr)
            {
                report.message = "Could not make the starter sound";
                finish (r.id, report, nullptr, nullptr, nullptr);
                return;
            }
            handOver (std::move (m), 0.0, 1.0);
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

        // A file: a new one the user picked, or the one a project names.
        const auto t0 = std::chrono::steady_clock::now();
        const bool fromProject = r.kind == Kind::ref;
        const juce::String savedPath = juce::String::fromUTF8 (r.ref.file.path.c_str());
        const bool usablePath = ! fromProject || (savedPath.isNotEmpty() && juce::File::isAbsolutePath (savedPath));
        // juce::File asserts on a relative path, so a saved path from another system is not even made into one.
        const juce::File file = fromProject ? (usablePath ? juce::File (savedPath) : juce::File()) : r.file;
        DecodedFile decoded = usablePath ? decodeFile (file, fromProject ? &r.ref.file : nullptr) : DecodedFile {};
        if (fromProject && decoded.error.empty() && decoded.left.empty())
        {
            decoded.missing = true;
            decoded.error = "Could not find that file";
        }
        {
            const std::lock_guard<std::mutex> hold (lock);
            fileMs = msSince (t0);
        }
        if (decoded.error == "stopped")
        {
            // Shutting down: nothing to report.
            const std::lock_guard<std::mutex> hold (lock);
            finished = std::max (finished, r.id);
            finishedAtomic.store (finished, std::memory_order_release);
            working = false;
            return;
        }

        if (! decoded.error.empty())
        {
            if (! fromProject)
            {
                report.message = decoded.error;
                finish (r.id, report, nullptr, nullptr, nullptr);
                return;
            }
            // The project's file is not there (or not the same file): the default starter plays and the SMPL
            // record is kept as it was (decision 4).
            const std::string& name = r.ref.file.displayName.empty() ? r.ref.file.path : r.ref.file.displayName;
            report.message = decoded.changed
                ? "The sound file \"" + name + "\" has changed since this project was saved, so the " + defaultStarterName() + " plays"
                : "Could not find the sound file \"" + name + "\" (" + r.ref.file.path + "), so the " + defaultStarterName() + " plays";
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

        const bool cut = decoded.cut && ! fromProject;
        const double seconds = static_cast<double> (decoded.left.size()) / decoded.sampleRate;
        Made m = fileSound (std::move (decoded));
        if (m.data == nullptr)
        {
            report.message = "Could not read that file as sound";
            finish (r.id, report, nullptr, nullptr, nullptr);
            return;
        }
        if (fromProject)
            m.view->ref = r.ref;   // the record as the project wrote it (its analysis cache too)

        // The Region of a new file: the whole file, or for a song (longer than 20 s) 4 s from 35 % in.
        double a = 0.0, b = 1.0;
        if (seconds > kLongFileSeconds)
        {
            a = kDefaultPartStart;
            b = kDefaultPartStart + kDefaultPartSeconds / seconds;
        }
        if (cut)
            report.message = m.view->name + " is longer than 10 minutes, so StrandLock loaded its first 10 minutes";
        else if (! fromProject && seconds > kLongFileSeconds)
            report.message = m.view->name + " is a song, so the Region starts as 4 s from 35 % in";
        handOver (std::move (m), a, b);
    }
}
