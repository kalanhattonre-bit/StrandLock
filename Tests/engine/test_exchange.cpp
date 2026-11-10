// The version hand-off through the processor (PLUGIN-PLAN 4 M2 "exchange" and "edits while playing"; 1.3, 8.2 #23):
// processBlock on an audio thread of its own while another thread publishes 1,000 snapshots a second (each deleting
// and adding sounding notes) and swaps the sample twice, and the message thread collects 10 times a second.
//
// The checks, after every block on the audio thread: every version the engine reads (its own and every voice's,
// Engine::forEachVersionRead) is alive and is the version the voice says it is (its epoch field matches; a freed
// version is filled with 0xDD in this runner, StrandLockProcessor::kPoisonFreedVersions), and no read epoch is below
// what the exchange may free. The sanitizer job runs this group, so a read of freed memory stops the run there.
//
// Group "engine" (the sanitizer job runs it).

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "engine/EngineRender.h"
#include "params/ParameterLayout.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Lanes.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "model/Node.h"
#include "model/SnapshotCompiler.h"
#include "rt/VersionExchange.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;

    void setParam (StrandLockProcessor& p, params::P which, float plain)
    {
        params::setPlainValue (p.getParam (which), params::index (which), plain);
    }

    std::shared_ptr<const model::Note> noteOf (double p, double s, double l, const std::string& id)
    {
        return std::make_shared<const model::Note> (model::mkNote (p, s, l, id));
    }

    /** Variant v of the 1-bar stress clip: 4 notes always there (their voices follow each version) and 8 long
        notes that come and go from version to version (deleted while they sound, added again). */
    model::Clip stressClip (int v)
    {
        model::Clip c = model::newClip (1);
        for (int b = 0; b < 4; ++b)
            c.notes.push_back (noteOf (48 + 2 * b, b, 0.9, "keep" + std::to_string (b)));
        for (int j = 0; j < 8; ++j)
            if ((v * 7 + j * 3) % 5 < 3)
                c.notes.push_back (noteOf (60 + j, j * 0.5, 2.0, "x" + std::to_string (j)));
        return c;
    }

    /** True when the version a voice reads is alive and is the one its epoch says. */
    bool readsLive (engine::VersionKind kind, const void* version, std::uint64_t epoch)
    {
        if (rt::looksPoisoned (version))
            return false;
        if (kind == engine::VersionKind::snapshot)
            return static_cast<const core::Snapshot*> (version)->epoch == epoch;
        return static_cast<const engine::SampleData*> (version)->epoch == epoch;
    }

    std::unique_ptr<engine::SampleData> sineSample (double hz, double secs)
    {
        signals::ToneOpts one;
        one.partials = 1;
        one.amp = 0.5;
        return render::monoSample (signals::tone (hz, secs, kSr, one), kSr);
    }

    void playAt (harness::FakePlayHead& head, bool playing, double ppq)
    {
        head.info.setIsPlaying (playing);
        head.info.setBpm (120.0);
        head.info.setPpqPosition (ppq);
    }

    double largestStep (const std::vector<float>& x, std::size_t a, std::size_t b)
    {
        double p = 0;
        for (std::size_t i = std::max<std::size_t> (1, a); i < b && i < x.size(); ++i)
            p = std::max (p, std::abs (static_cast<double> (x[i]) - x[i - 1]));
        return p;
    }
}

SL_TEST ("engine", exchangeUnderLoad)
{
    section ("exchange: 1,000 snapshots a second (deletes and adds) and 2 sample swaps while processBlock renders 10 s on its own thread, collect at 10 Hz");

    StrandLockProcessor proc;
    harness::FakePlayHead head;
    playAt (head, true, 0.0);
    proc.setPlayHead (&head);
    setParam (proc, params::P::engine, 0.0f);   // Tape
    proc.setPlayConfigDetails (0, 2, kSr, kBlock);
    proc.prepareToPlay (kSr, kBlock);

    auto& snaps = proc.getSnapshotExchange();
    auto& samples = proc.getSampleExchange();
    check (snaps.poisoning() && samples.poisoning(), "the processor's exchanges poison what they free in this runner");

    // The versions to publish: 16 variants of the clip, copied for each publish; two sines for the swaps.
    std::vector<std::unique_ptr<core::Snapshot>> variants;
    for (int v = 0; v < 16; ++v)
        variants.push_back (model::compileSnapshot (stressClip (v), 32));
    const auto sineA = sineSample (220.0, 3.0), sineB = sineSample (330.0, 3.0);
    samples.publish (std::make_unique<engine::SampleData> (*sineA));
    snaps.publish (std::make_unique<core::Snapshot> (*variants[0]));

    constexpr std::int64_t kTotal = static_cast<std::int64_t> (10 * kSr);
    constexpr std::int64_t kQuietFrom = static_cast<std::int64_t> (9.5 * kSr);   // the last 0.5 s: empty clips only, no keys
    std::atomic<std::int64_t> audioAt { 0 };
    std::atomic<bool> audioDone { false };
    std::atomic<long long> published { 0 }, swaps { 0 };

    // The audio thread's findings.
    alloc::Counts counted;
    long long blocks = 0, badReads = 0, belowInUse = 0, heldOlder = 0, regressions = 0;
    int voicesAtEnd = -1;
    double quietTail = -1, loudest = 0;

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        midi.ensureSize (1024);
        juce::Random rng (77);
        std::vector<int> held;
        held.reserve (64);
        double tail = 0;
        for (std::int64_t k = 0; k < kTotal; k += kBlock)
        {
            // Keep within 2 ms of the publisher, so the stream really is 1,000 a second of audio.
            while (published.load (std::memory_order_acquire) < k / 48 - 96)
                std::this_thread::yield();

            playAt (head, true, static_cast<double> (k) / 24000.0);
            midi.clear();
            if (k < kQuietFrom)
            {
                if (rng.nextInt (20) == 0 && held.size() < 8)
                {
                    const int note = 40 + rng.nextInt (40);
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (40 + rng.nextInt (80))), rng.nextInt (kBlock));
                    held.push_back (note);
                }
                if (! held.empty() && rng.nextInt (15) == 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOff (1, held.back()), rng.nextInt (kBlock));
                    held.pop_back();
                }
            }
            else
            {
                for (const int note : held)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                held.clear();
            }

            alloc::reset();
            {
                const alloc::Scope counting;
                proc.processBlock (buffer, midi);
            }
            const auto c = alloc::counts();
            counted.news += c.news;
            counted.deletes += c.deletes;
            counted.largest = std::max (counted.largest, c.largest);
            ++blocks;

            // Every version still read is alive and is what the voice says it is, and none is below what the
            // exchange may free.
            const auto& eng = proc.getEngine();
            const std::uint64_t snapIn = snaps.inUse(), sampleIn = samples.inUse();
            bool older = false;
            eng.forEachVersionRead ([&] (engine::VersionKind kind, const void* v, std::uint64_t epoch) {
                if (! readsLive (kind, v, epoch))
                    ++badReads;
                if (epoch < (kind == engine::VersionKind::snapshot ? snapIn : sampleIn))
                    ++belowInUse;
                if (kind == engine::VersionKind::snapshot && epoch < snaps.currentEpoch())
                    older = true;
            });
            heldOlder += older ? 1 : 0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                {
                    const double x = std::abs (static_cast<double> (buffer.getSample (ch, i)));
                    loudest = std::max (loudest, x);
                    if (k >= kQuietFrom + static_cast<std::int64_t> (0.1 * kSr))
                        tail = std::max (tail, x);
                }
            audioAt.store (k + kBlock, std::memory_order_release);
        }
        voicesAtEnd = proc.getEngine().stats().voices;
        quietTail = tail;
        regressions = (long long) (snaps.inUseRegressions() + samples.inUseRegressions());
        audioDone.store (true, std::memory_order_release);
    });

    std::thread publisher ([&]
    {
        // One snapshot per 48 samples of audio (1,000 a second); the sample swaps at 3 s and 7 s.
        for (std::int64_t i = 1; i <= kTotal / 48; ++i)
        {
            while (audioAt.load (std::memory_order_acquire) < (i - 64) * 48 && ! audioDone.load (std::memory_order_acquire))
                std::this_thread::yield();
            const std::int64_t at = i * 48;
            if (at < kQuietFrom)
                snaps.publish (std::make_unique<core::Snapshot> (*variants[static_cast<std::size_t> (i % 16)]));
            else
                snaps.publish (std::make_unique<core::Snapshot> (*model::compileSnapshot (model::newClip (1), 8)));   // every note deleted
            if (at == static_cast<std::int64_t> (3 * kSr) || at == static_cast<std::int64_t> (7 * kSr))
            {
                samples.publish (std::make_unique<engine::SampleData> (at < 5 * kSr ? *sineB : *sineA));
                ++swaps;
            }
            published.store (i, std::memory_order_release);
        }
    });

    // The message thread: collect 10 times a second of audio.
    std::int64_t nextCollect = 4800;
    std::size_t largestPool = 0;
    while (! audioDone.load (std::memory_order_acquire))
    {
        if (audioAt.load (std::memory_order_acquire) >= nextCollect)
        {
            proc.collectVersions();
            largestPool = std::max (largestPool, snaps.poolSize());
            nextCollect += 4800;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    publisher.join();
    audio.join();

    note (fmt ("%lld blocks; %lld snapshots and %lld sample swaps published; %llu snapshots freed (%llu never taken); counting %s", blocks,
               (long long) published.load(), (long long) swaps.load(), (unsigned long long) snaps.freedCount(),
               (unsigned long long) snaps.waitingFreedCount(), alloc::coverage()));
    check (published.load() == kTotal / 48 && swaps.load() == 2 && loudest > 0.01,
           fmt ("10,000 snapshots and 2 sample swaps went in while it played (peak %.3f)", loudest));
    check (counted.news == 0 && counted.deletes == 0,
           fmt ("on the audio thread: %lld allocations and %lld frees (largest %zu bytes)", counted.news, counted.deletes, counted.largest));
    check (badReads == 0 && belowInUse == 0 && regressions == 0,
           fmt ("no version read after it was freed: %lld reads of a poisoned or other version, %lld below the exchange's in-use epoch, %lld in-use regressions",
                badReads, belowInUse, regressions));
    check (heldOlder >= 20,
           fmt ("a no-op is not a pass: in %lld blocks a releasing voice held an older snapshot than the current one (the probe's rule would have freed it)",
                heldOlder));
    note (fmt ("largest snapshot pool seen at a collect: %zu versions", largestPool));
    check (voicesAtEnd == 0 && quietTail == 0.0,
           fmt ("every note deleted while 1,000 snapshots a second still come in: %d voices after 0.5 s, and silence after 100 ms (peak %.3g)", voicesAtEnd,
                quietTail));

    // 100 ms after the last publish, with no voice releasing: one version of each kind.
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        double ppq = static_cast<double> (kTotal) / 24000.0;
        for (int b = 0; b < (int) (0.1 * kSr) / kBlock + 1; ++b)
        {
            playAt (head, true, ppq);
            proc.processBlock (buffer, midi);
            ppq += kBlock / 24000.0;
        }
        proc.collectVersions();
        check (snaps.poolSize() == 1 && samples.poolSize() == 1,
               fmt ("100 ms after the last publish, collect () leaves %zu snapshot and %zu sample versions (1 each)", snaps.poolSize(), samples.poolSize()));
    }

    // Edits published while processBlock is not called (the host stopped calling it): nothing piles up, and the
    // first block after Play plays the newest.
    {
        playAt (head, false, 0.0);
        const std::size_t before = snaps.poolSize();
        model::Clip last = stressClip (3);
        last.notes.push_back (noteOf (100, 0, 4, "newest"));
        const core::Snapshot* newest = nullptr;
        for (int i = 0; i < 50; ++i)
        {
            auto s = i < 49 ? std::make_unique<core::Snapshot> (*variants[static_cast<std::size_t> (i % 16)]) : model::compileSnapshot (last, 32);
            newest = s.get();
            snaps.publish (std::move (s));
        }
        check (snaps.poolSize() <= before + 1, fmt ("50 edits while no block runs: the pool holds %zu versions (it held %zu)", snaps.poolSize(), before));

        playAt (head, true, 0.5);
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        auto frame = std::make_unique<engine::DisplayFrame>();
        proc.getEngine().fillDisplay (*frame);
        bool hasNewest = false;
        for (int i = 0; i < frame->numVoices; ++i)
            hasNewest = hasNewest || frame->voices[i].p == 100;
        check (proc.getAudioSnapshot() == newest && hasNewest,
               "the first block after Play takes the newest edit and plays its note (only the newest version has it)");
    }

    proc.releaseResources();
    proc.setPlayHead (nullptr);
}

SL_TEST ("engine", exchangeSampleSwapStep)
{
    section ("exchange: a sample swap 220 -> 330 Hz through the processor under a held key: the stolen voice fades on the old sample, largest step / steady < 1.5");

    const auto a = sineSample (220.0, 2.0), b = sineSample (330.0, 2.0);
    const int swapBlock = 24000 / kBlock;

    // A key held from the first block on `first`; at swapBlock (when given) `second` is published between two blocks.
    auto play = [swapBlock] (const engine::SampleData& first, const engine::SampleData* second) {
        StrandLockProcessor proc;
        setParam (proc, params::P::engine, 0.0f);
        proc.setPlayConfigDetails (0, 2, kSr, kBlock);
        proc.prepareToPlay (kSr, kBlock);
        proc.getSampleExchange().publish (std::make_unique<engine::SampleData> (first));
        std::vector<float> L;
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < 30000 / kBlock; ++blk)
        {
            midi.clear();
            if (blk == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
            if (blk == swapBlock && second != nullptr)
                proc.getSampleExchange().publish (std::make_unique<engine::SampleData> (*second));
            proc.processBlock (buffer, midi);
            L.insert (L.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlock);
        }
        proc.releaseResources();
        return L;
    };
    const auto swapped = play (*a, b.get());
    const std::size_t k = static_cast<std::size_t> (swapBlock * kBlock);
    const double steady = largestStep (swapped, 12000, k - 100);
    const double swap = largestStep (swapped, k - 10, k + 480);
    check (steady > 0 && swap / steady < 1.5, fmt ("largest step at the swap / steady = %.3f (steady %.2e)", swap / steady, steady));

    // a no-op is not a pass: the 330 Hz sine played steadily steps about 1.5 times as far, so a fade read from the
    // new sample (the prototype's way) would show above the line
    const auto onB = play (*b, nullptr);
    const double ratioB = largestStep (onB, 12000, k - 100) / steady;
    check (ratioB >= 1.45, fmt ("negative control: the 330 Hz key's steady step is %.3f times the 220 Hz one", ratioB));
}

SL_TEST ("engine", editsWhilePlaying)
{
    section ("edits while playing, through the document: a Volume lane edit heard within one tick on the same voice; a deleted note releases, then silence");

    // The level of one long Tape note on a DC source, with its Volume lane flat at v, from Play.
    auto clipWith = [] (double volume) {
        model::Clip c = model::newClip (4);
        model::Note n = model::mkNote (60, 0, 16, "long");
        n.lane (core::LaneId::amp) = model::NodeList { { 0.0, volume, model::NodeShape::linear, 0.0 }, { 1.0, volume, model::NodeShape::linear, 0.0 } };
        c.notes.push_back (std::make_shared<const model::Note> (n));
        return c;
    };
    const auto dc = render::monoSample (signals::dc (8.0, kSr, 0.25), kSr);

    struct Run
    {
        std::vector<float> L;
        int voicesBefore = 0, voicesAfter = 0;
        std::uint32_t idBefore = 0, idAfter = 0;
        bool releasedAfter = true;
        float envAfter = 0;
    };
    // Plays clip `from` for `editAt` samples, then (when `to` is given) edits the clip to it between two blocks
    // and plays on to `total`.
    auto play = [&dc] (const model::Clip& from, const model::Clip* to, int editAt, int total) {
        Run r;
        StrandLockProcessor proc;
        harness::FakePlayHead head;
        proc.setPlayHead (&head);
        setParam (proc, params::P::engine, 0.0f);
        proc.editClip ("start", from);
        proc.waitForSnapshot();
        proc.setPlayConfigDetails (0, 2, kSr, 64);
        proc.prepareToPlay (kSr, 64);
        proc.getSampleExchange().publish (std::make_unique<engine::SampleData> (*dc));
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        auto frame = std::make_unique<engine::DisplayFrame>();
        auto look = [&] (int& voices, std::uint32_t& id, bool& released, float& env) {
            proc.getEngine().fillDisplay (*frame);
            voices = frame->numVoices;
            id = voices > 0 ? frame->voices[0].noteIdHash : 0;
            released = voices > 0 ? frame->voices[0].released : true;
            env = voices > 0 ? frame->voices[0].env : 0.0f;
        };
        for (int k = 0; k < total; k += 64)
        {
            if (to != nullptr && k == editAt)
            {
                bool rel = false;
                float env = 0;
                look (r.voicesBefore, r.idBefore, rel, env);
                proc.editClip ("edit", *to);
                proc.waitForSnapshot();   // compiled and handed over: the next block takes it
            }
            playAt (head, true, k / 24000.0);
            proc.processBlock (buffer, midi);
            r.L.insert (r.L.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 64);
            if (to != nullptr && k == editAt + 64 * 4)
                look (r.voicesAfter, r.idAfter, r.releasedAfter, r.envAfter);
        }
        proc.releaseResources();
        proc.setPlayHead (nullptr);
        return r;
    };
    auto meanOf = [] (const std::vector<float>& x, int a, int b) {
        double s = 0;
        for (int i = a; i < b; ++i)
            s += x[static_cast<std::size_t> (i)];
        return s / (b - a);
    };

    // Volume 0.6 -> 0.3 at sample 24000 (a block start). The expected ratio is what the two values play when
    // played from the start.
    const int editAt = 24000, total = 36000;
    const auto loud = play (clipWith (0.6), nullptr, 0, total);
    const auto soft = play (clipWith (0.3), nullptr, 0, total);
    const double expected = meanOf (soft.L, 12000, 20000) / meanOf (loud.L, 12000, 20000);
    const auto soft2 = clipWith (0.3);
    const auto edited = play (clipWith (0.6), &soft2, editAt, total);
    const double before = meanOf (edited.L, editAt - 4000, editAt);
    const double after = meanOf (edited.L, editAt + 64, editAt + 64 + 2048);   // one tick (32 samples) after the block that took it
    const double ratio = after / before;
    check (std::abs (expected - 1.0) > 0.1 && std::abs (ratio - expected) <= 0.01,
           fmt ("the level from the tick after the edit is %.4f of before (expected %.4f, from the two values played from the start)", ratio, expected));
    check (edited.voicesBefore == 1 && edited.voicesAfter == 1 && edited.idAfter == edited.idBefore && ! edited.releasedAfter && edited.envAfter > 0.999f,
           fmt ("the same voice plays on: %d voice before, %d after, same note id, not released, envelope %.4f (no new attack)", edited.voicesBefore,
                edited.voicesAfter, edited.envAfter));

    // Deleting the note: the voice releases (60 ms), then silence.
    const model::Clip empty = model::newClip (4);
    const auto deleted = play (clipWith (0.6), &empty, editAt, total);
    const double right = std::abs (meanOf (deleted.L, editAt + 64, editAt + 128));
    double silenceAfter = 0;
    for (int i = editAt + 4800; i < total; ++i)
        silenceAfter = std::max (silenceAfter, std::abs (static_cast<double> (deleted.L[static_cast<std::size_t> (i)])));
    check (deleted.voicesAfter == 1 && deleted.releasedAfter && right > 0.01,
           fmt ("the deleted note's voice releases (%d voice, released %d) and still sounds as it fades (%.3f)", deleted.voicesAfter,
                deleted.releasedAfter ? 1 : 0, right));
    check (silenceAfter == 0.0, fmt ("100 ms after the delete: silence (peak %.3g)", silenceAfter));
}
