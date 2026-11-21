// M7 "long files" (PLUGIN-PLAN 4 M7, 8.2 #14, 7.9 V2): a part longer than 3 minutes is kept as 16-bit samples.
//   - sixteenBitReadsLikeFloat (engine; it runs under the sanitizers): every read path (readAbs, the fast path, the
//     seams, seamRho) and the whole engine (Tape, Voice, Cloud, Lock) give the same bits from 16-bit samples s as from
//     floats holding s / 32768; one sample one step away gives other bits (the comparison can fail).
//   - longFileLoadsAs16Bit (songs, Release and Linux): a 10-minute stereo 48 kHz WAV made at test time loads as 16-bit
//     with at most its 16-bit bytes (115.2 MB) + 16 MB live on the worker (AllocCounter, counted on the worker through
//     SampleManager::workerProbe), decoded on the worker (the message thread allocates almost nothing meanwhile), the
//     samples as JUCE's reader gives them, and 0 allocations on the audio thread at and after the swap.
//   - sixteenBitSwapAllocatesNothing (alloc, so the Debug CRT counts every heap call too): a 200 s file (16-bit) swapped
//     in while a note plays.
//   - newerRequestsStopTheWorker (songs, decision 1): a newer Detect replaces one still waiting, a new sound drops a
//     waiting Detect, and a file overtaken while the worker reads it stops at the next question and never plays.

#include "SongRig.h"

#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"
#include "engine/Region.h"
#include "engine/SourceRead.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    bool sameBits (double a, double b) noexcept { return std::memcmp (&a, &b, sizeof a) == 0; }

    bool sameFloats (const std::vector<float>& a, const std::vector<float>& b) noexcept
    {
        return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
    }

    double msSince (std::chrono::steady_clock::time_point t0)
    {
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    }

    engine::RegionState regionOf (double rs, double L, double X, bool post, bool loop, bool fold, double rho)
    {
        engine::RegionState g;
        g.rs = rs;
        g.L = L;
        g.X = X;
        g.Lp = L;
        g.invX = X > 0 ? 1 / X : 0;
        g.post = post;
        g.loop = loop;
        g.fold = fold;
        g.rho = rho;
        return g;
    }

    /** The same sound twice: 16-bit samples, and floats holding s / 32768 (stereo, n frames, a seeded wobble). */
    struct Twin
    {
        std::unique_ptr<engine::SampleData> i16, f32;
    };

    Twin twin (std::int64_t n, std::uint32_t seed, std::int64_t nudgeAt = -1)
    {
        songs::Noise noise (seed);
        std::vector<std::int16_t> l16 ((std::size_t) n), r16 ((std::size_t) n);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        for (std::int64_t i = 0; i < n; ++i)
        {
            const double t = (double) i / 48000.0;
            const double v = 0.45 * std::sin (2.0 * 3.141592653589793 * 220.0 * t) + 0.2 * noise.next();
            const double w = 0.4 * std::sin (2.0 * 3.141592653589793 * 331.0 * t) + 0.2 * noise.next();
            l16[(std::size_t) i] = engine::toInt16 ((float) v);
            r16[(std::size_t) i] = engine::toInt16 ((float) w);
        }
        for (std::int64_t i = 0; i < n; ++i)
        {
            l[(std::size_t) i] = (float) l16[(std::size_t) i] / 32768.0f;
            r[(std::size_t) i] = (float) r16[(std::size_t) i] / 32768.0f;
        }
        if (nudgeAt >= 0)
            l16[(std::size_t) nudgeAt] = (std::int16_t) (l16[(std::size_t) nudgeAt] + 1);   // one step away, for the control
        Twin t;
        t.i16 = engine::makeSampleData16 (std::move (l16), std::move (r16), 48000.0, "sixteen");
        t.f32 = engine::makeSampleData (std::move (l), std::move (r), 48000.0, "float");
        return t;
    }

    /** One live note on an engine of each sample, the same instrument: the two outputs. */
    std::pair<std::vector<float>, std::vector<float>> renderBoth (const engine::SampleData& a, const engine::SampleData& b, const core::Inst& inst,
                                                                  double pitch, int samples)
    {
        std::vector<float> out[2];
        const engine::SampleData* src[2] = { &a, &b };
        for (int k = 0; k < 2; ++k)
        {
            auto e = std::make_unique<engine::Engine>();
            e->prepare (48000.0);
            e->setRampMs (0.0);
            e->setSample (src[k], 1);
            e->setInst (inst);
            e->snapRamps();
            e->noteOn (pitch, 0.9);
            std::vector<float> L ((std::size_t) samples), R ((std::size_t) samples);
            for (int at = 0; at < samples; at += 128)
                e->process (L.data() + at, R.data() + at, std::min (128, samples - at));
            L.insert (L.end(), R.begin(), R.end());
            out[k] = std::move (L);
        }
        return { std::move (out[0]), std::move (out[1]) };
    }
}

SL_TEST ("engine", sixteenBitReadsLikeFloat)
{
    section ("16-bit samples (M7, plan 8.2 #14): every read path and every engine give the bits floats holding s / 32768 give");

    const std::int64_t n = 24000;
    const auto t = twin (n, 7);
    check (t.i16 != nullptr && t.f32 != nullptr && t.i16->format == engine::SampleFormat::i16 && t.i16->stereo && t.f32->stereo
               && t.i16->sampleBytes() == n * 4 && t.f32->sampleBytes() == n * 8,
           fmt ("the twin sounds: 16-bit (%lld bytes) and float (%lld bytes), both stereo", (long long) t.i16->sampleBytes(),
                (long long) t.f32->sampleBytes()));

    // readAbs at random positions, inside, at the edges and far outside.
    songs::Noise pick (99);
    int readsBad = 0, reads = 0;
    for (int k = 0; k < 4000; ++k)
    {
        const double x = k < 3800 ? ((double) pick.next() * 0.5 + 0.5) * (double) n : (double) pick.next() * 4e12;
        double l1 = 0, r1 = 0, l2 = 0, r2 = 0;
        engine::readAbs (*t.i16, x, l1, r1);
        engine::readAbs (*t.f32, x, l2, r2);
        ++reads;
        if (! sameBits (l1, l2) || ! sameBits (r1, r2))
            ++readsBad;
    }
    check (readsBad == 0, fmt ("readAbs: %d reads, %d differ", reads, readsBad));

    // The fast path and readReg (both seams, Hold, Bounce) and seamRho.
    const engine::RegionState regions[] = { regionOf (3000, 9000, 480, true, true, false, 0.3), regionOf (9000, 6000, 480, false, true, false, 0.7),
                                            regionOf (0, (double) n, 0, true, true, false, 0), regionOf (5000, 4000, 0, true, false, false, 0),
                                            regionOf (2000, 7000, 0, true, false, true, 0) };
    int pathBad = 0, paths = 0, fastHits = 0;
    for (const auto& g : regions)
    {
        const engine::PlainStretch p16 (*t.i16, g), p32 (*t.f32, g);
        for (int k = 0; k < 3000; ++k)
        {
            const double x = ((double) pick.next() * 1.5) * g.L;
            double a1 = 0, b1 = 0, a2 = 0, b2 = 0;
            const bool f1 = p16.read (x, a1, b1), f2 = p32.read (x, a2, b2);
            double c1 = 0, d1 = 0, c2 = 0, d2 = 0;
            engine::readReg (*t.i16, g, x, c1, d1);
            engine::readReg (*t.f32, g, x, c2, d2);
            ++paths;
            fastHits += f1 ? 1 : 0;
            if (f1 != f2 || (f1 && (! sameBits (a1, a2) || ! sameBits (b1, b2))) || ! sameBits (c1, c2) || ! sameBits (d1, d2))
                ++pathBad;
        }
        if (! sameBits (engine::seamRho (*t.i16, g), engine::seamRho (*t.f32, g)))
            ++pathBad;
    }
    check (pathBad == 0 && fastHits > 1000,
           fmt ("the fast path, readReg (post and pre seams, Hold, Bounce) and seamRho: %d positions (%d on the fast path), %d differ", paths, fastHits,
                pathBad));

    // The whole engine, one live note, each engine (Tape, Voice, Cloud, Cloud with Lock), up and down.
    struct Case { const char* name; core::Engine engine; bool lock; double pitch; };
    const Case cases[] = { { "Tape", core::Engine::tape, false, 67.0 }, { "Voice", core::Engine::voice, false, 55.0 },
                           { "Cloud", core::Engine::cloud, false, 72.0 }, { "Lock", core::Engine::cloud, true, 60.0 } };
    for (const auto& c : cases)
    {
        auto inst = core::defaultInst();
        inst.engine = c.engine;
        inst.lock = c.lock;
        inst.heads = 3;
        inst.drift = 0.3;
        inst.region.start = 0.1;
        inst.region.end = 0.6;
        const auto [a, b] = renderBoth (*t.i16, *t.f32, inst, c.pitch, 24000);
        double peak = 0;
        for (const float v : a)
            peak = std::max (peak, (double) std::abs (v));
        check (sameFloats (a, b) && peak > 0.01, fmt ("%s at MIDI %.0f: half a second from 16-bit and from float, bit for bit (peak %.3f)", c.name, c.pitch, peak));
    }

    // A no-op is not a pass: one sample one step away (inside the Region the note plays) gives other bits.
    const auto nudged = twin (n, 7, 7200);
    auto inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    inst.region.start = 0.1;
    inst.region.end = 0.6;
    const auto [a, b] = renderBoth (*nudged.i16, *nudged.f32, inst, 60.0, 24000);
    check (! sameFloats (a, b), "negative control: the 16-bit sound with one sample one step up renders other bits");
}

SL_TEST ("songs", longFileLoadsAs16Bit)
{
    section ("Long files (M7): a 10-minute stereo 48 kHz WAV loads as 16-bit, with at most 115.2 MB + 16 MB live on the worker, "
             "and the audio thread allocates nothing when it swaps to it");

    const auto dir = songs::tempFolder ("long-file");
    const auto file = dir.getChildFile ("ten minutes.wav");
    const double sr = 48000.0;
    const std::int64_t frames = 600 * 48000;   // exactly 10 minutes: the whole file loads
    {
        const auto t0 = std::chrono::steady_clock::now();
        double phaseL = 0.0, phaseR = 0.0;
        const bool written = songs::writeWav16 (file, 2, sr, frames, [&] (std::int64_t start, int count, float* const* out)
        {
            for (int i = 0; i < count; ++i)
            {
                const double t = (double) (start + i) / sr;
                const double swell = 0.55 + 0.45 * std::sin (2.0 * 3.141592653589793 * t / 37.0);   // sections a few seconds long
                out[0][i] = (float) (0.3 * swell * std::sin (phaseL));
                out[1][i] = (float) (0.25 * swell * std::sin (phaseR) + 0.05 * std::sin (3.0 * phaseR));
                phaseL += 2.0 * 3.141592653589793 * 220.0 / sr;
                phaseR += 2.0 * 3.141592653589793 * 329.63 / sr;
                if (phaseL > 6.283185307179586)
                    phaseL -= 6.283185307179586;
                if (phaseR > 6.283185307179586)
                    phaseR -= 6.283185307179586;
            }
        });
        check (written && file.getSize() > 115000000, fmt ("a 10-minute stereo 48 kHz 16-bit WAV written at test time: %lld bytes in %.0f ms",
                                                           (long long) file.getSize(), msSince (t0)));
    }

    StrandLockProcessor p;
    p.waitForSounds();
    p.prepareToPlay (sr, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    auto block = [&] (int noteOn)
    {
        buffer.clear();
        midi.clear();
        if (noteOn >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, noteOn, (juce::uint8) 100), 0);
        p.processBlock (buffer, midi);
    };
    block (60);   // the starter plays a note: the swap steals it
    for (int i = 0; i < 8; ++i)
        block (-1);

    // The worker's allocations while it makes the request (its own thread's counts).
    struct Probe
    {
        std::thread::id thread;
        alloc::Counts counts;
        int runs = 0;
        std::unique_ptr<alloc::Scope> scope;
    } probe;
    auto& manager = p.getSampleManager();
    manager.workerProbe = [&probe] (bool starting)
    {
        if (starting)
        {
            probe.thread = std::this_thread::get_id();
            alloc::reset();
            probe.scope = std::make_unique<alloc::Scope>();
            return;
        }
        probe.counts = alloc::counts();
        probe.scope.reset();
        ++probe.runs;
    };

    const auto t0 = std::chrono::steady_clock::now();
    p.loadSoundFile (file);
    alloc::reset();
    {
        const alloc::Scope counting;
        manager.waitIdle (120000);
    }
    const auto messageThread = alloc::counts();
    const double loadMs = msSince (t0);
    manager.workerProbe = nullptr;

    const std::int64_t bytes16 = frames * 2 * 2;   // 115,200,000
    const double mb = 1.0e6;
    check (probe.runs == 1 && probe.thread != std::this_thread::get_id() && probe.counts.peakLiveBytes >= bytes16,
           fmt ("decoded on the worker (another thread), which allocated the samples itself: peak live %.2f MB there", (double) probe.counts.peakLiveBytes / mb));
    check (probe.counts.peakLiveBytes <= bytes16 + 16000000,
           fmt ("at most the 16-bit bytes + 16 MB live on the worker: %.2f MB against %.2f MB (the largest block %.2f MB; %s)",
                (double) probe.counts.peakLiveBytes / mb, (double) (bytes16 + 16000000) / mb, (double) probe.counts.largest / mb, alloc::coverage()));
    check (messageThread.peakLiveBytes < 4000000,
           fmt ("the message thread meanwhile: %.3f MB live at most (it decodes nothing)", (double) messageThread.peakLiveBytes / mb));
    note (fmt ("the load took %.0f ms (hash, decode and analysis %.0f ms on the worker, the analysis %.0f ms)", loadMs, manager.lastFileMs(),
               manager.lastAnalysisMs()));

    // The swap: the message thread hands it over; the next blocks take it, play a note on it, and allocate nothing.
    p.deliverSoundLoads();
    alloc::reset();
    long long audioCalls = 0;
    for (int i = 0; i < 40; ++i)
    {
        const alloc::Scope counting;
        block (i == 4 ? 64 : -1);
    }
    const auto audio = alloc::counts();
    audioCalls = audio.news + audio.deletes;
    const auto* played = p.getAudioSample();
    double peak = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        peak = std::max (peak, (double) buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    check (played != nullptr && played->format == engine::SampleFormat::i16 && played->n == frames && played->stereo
               && played->sampleBytes() == bytes16,
           fmt ("the audio thread plays it as 16-bit: %lld frames, stereo, %lld bytes", played != nullptr ? (long long) played->n : -1LL,
                played != nullptr ? (long long) played->sampleBytes() : -1LL));
    check (audioCalls == 0 && peak > 1e-4,
           fmt ("0 allocations and frees on the audio thread in the 40 blocks of the swap and a note on it (%lld; the last block's peak %.4f; %s)",
                audioCalls, peak, alloc::coverage()));

    // The samples are the file's, as JUCE's reader gives them, at the start, inside and at the end, both channels.
    bool same = played != nullptr;
    const std::int64_t at[] = { 0, 1234567, frames / 2 + 17, frames - 4096 };
    for (const auto a : at)
        for (int ch = 0; ch < 2 && same; ++ch)
        {
            const auto want = songs::readFrames16 (file, a, 4096, ch);
            const std::int16_t* got = (ch == 0 ? played->L16 : played->R16) + a;
            same = want.size() == 4096 && std::memcmp (want.data(), got, 4096 * sizeof (std::int16_t)) == 0;
        }
    check (same, "the 16-bit samples are the file's (4 x 4096 frames of each channel against JUCE's reader)");

    const auto ref = p.getSampleRef();
    const auto view = p.getCurrentSound();
    check (ref.kind == state::kSampleFile && ref.file.fileFrames == (std::uint64_t) frames && ref.file.loadedStartFrame == 0
               && ref.file.loadedFrames == (std::uint64_t) frames && view != nullptr && ! view->cut && view->format == engine::SampleFormat::i16
               && view->left16 == played->L16 && ref.file.cache.analysed == 1,
           fmt ("SMPL: the whole file (%llu frames) loaded, not a part; the strip's view shares the 16-bit samples; analysed on the worker (%d parts)",
                (unsigned long long) ref.file.loadedFrames, (int) ref.file.cache.parts.size()));
    p.releaseResources();
    dir.deleteRecursively();
}

SL_TEST ("alloc", sixteenBitSwapAllocatesNothing)
{
    section ("A 16-bit sound (a 200 s file) swapped in while a note plays: 0 allocations on the audio thread (M7)");

    const auto dir = songs::tempFolder ("swap16");
    const auto file = dir.getChildFile ("two hundred seconds.wav");
    const double rate = 8000.0;
    const std::int64_t frames = 200 * 8000;
    const bool written = songs::writeWav16 (file, 1, rate, frames, [rate] (std::int64_t start, int count, float* const* out)
    {
        for (int i = 0; i < count; ++i)
            out[0][i] = (float) (0.3 * std::sin (2.0 * 3.141592653589793 * 110.0 * (double) (start + i) / rate));
    });
    check (written, "a 200 s mono 8 kHz 16-bit WAV written at test time");

    StrandLockProcessor p;
    p.waitForSounds();
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    auto block = [&] (int noteOn)
    {
        buffer.clear();
        midi.clear();
        if (noteOn >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, noteOn, (juce::uint8) 100), 0);
        p.processBlock (buffer, midi);
    };
    block (60);
    p.loadSoundFile (file);
    p.waitForSounds (60000);   // the worker decodes; the message thread hands it over
    alloc::reset();
    for (int i = 0; i < 30; ++i)
    {
        const alloc::Scope counting;
        block (i == 3 ? 67 : -1);
    }
    const auto c = alloc::counts();
    const auto* played = p.getAudioSample();
    check (played != nullptr && played->format == engine::SampleFormat::i16 && played->n == frames,
           fmt ("the audio thread took the 16-bit sound (%lld frames)", played != nullptr ? (long long) played->n : -1LL));
    check (c.news == 0 && c.deletes == 0, fmt ("0 allocations and frees in the 30 blocks of the swap (%lld, %lld; %s)", c.news, c.deletes, alloc::coverage()));
    p.releaseResources();
    dir.deleteRecursively();
}

SL_TEST ("songs", newerRequestsStopTheWorker)
{
    section ("Decision 1: a newer request stops the worker's decode and analysis, and a newer job replaces an older one");

    const auto dir = songs::tempFolder ("stops");
    const auto tone = dir.getChildFile ("tone.wav");
    const auto song = dir.getChildFile ("four minutes.wav");
    songs::Noise noise (4);
    const bool written = songs::writeMono16 (tone, test::signals::tone (test::signals::midiHz (57.0), 2.0, 44100.0), 44100.0)
                      && songs::writeWav16 (song, 1, 22050.0, 240 * 22050, [&noise] (std::int64_t, int count, float* const* out)
                         {
                             for (int i = 0; i < count; ++i)
                                 out[0][i] = 0.2f * noise.next();
                         });
    check (written, "a 2 s tone and a 4-minute file written at test time");

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (tone);
    p.waitForSounds (30000);
    auto& m = p.getSampleManager();

    // Two Detects asked for while the worker is busy: only the newer one runs.
    const auto ran0 = m.jobsRun();
    m.setHeld (true);
    p.detectPitchAndKey();
    p.detectPitchAndKey();
    m.setHeld (false);
    p.waitForAnalysis (30000);
    check (m.jobsRun() == ran0 + 1 && ! p.getSuggestions().listening, fmt ("two Detects in a row: %d ran (the newer replaces the older)", (int) (m.jobsRun() - ran0)));

    // A Detect and then a new sound: the job is dropped, the sound plays.
    const auto ran1 = m.jobsRun();
    m.setHeld (true);
    p.detectPitchAndKey();
    p.loadStarterSound (sounds::StarterId::pluck);
    m.setHeld (false);
    p.waitForSounds (30000);
    p.waitForAnalysis (30000);
    const auto now = p.getCurrentSound();
    check (m.jobsRun() <= ran1 + 1 && now != nullptr && now->builtIn && now->starter == sounds::StarterId::pluck && ! p.getSuggestions().listening,
           "a Detect that a new sound overtook gives nothing (only the starter's parts may run), and the new sound plays");

    // A file whose request is overtaken while the worker makes it: its decode stops at the next question, nothing of it
    // is published, and the newer sound plays.
    const auto stops0 = m.stopsMade();
    bool asked = false;
    m.workerProbe = [&m, &asked] (bool starting)
    {
        if (starting && ! asked)
        {
            asked = true;
            m.loadStarter (sounds::StarterId::vowel, 1, doc::LoadOrigin::user);   // a newer pick, while the file is being made
        }
    };
    p.loadSoundFile (song);
    p.waitForSounds (60000);
    m.workerProbe = nullptr;
    const auto after = p.getCurrentSound();
    check (asked && m.stopsMade() > stops0 && after != nullptr && after->builtIn && after->starter == sounds::StarterId::vowel,
           fmt ("the 4-minute file overtaken while it was read: %d stop(s), it never plays, the newer pick does",
                (int) (m.stopsMade() - stops0)));
    dir.deleteRecursively();
}
