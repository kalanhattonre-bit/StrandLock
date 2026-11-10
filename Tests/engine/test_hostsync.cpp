// Host sync through the real path (PLUGIN-PLAN 4 M2, 1.10; the integration step's rows): a fake AudioPlayHead read by
// diag::readHostBlock, HostSync::plan turning it into the engine's transport (split where a host cycle wraps inside
// a block), and MidiRouter::renderBlock playing the block on the real Engine, exactly as processBlock does. Rows:
// note starts, jitter, cycle wraps inside blocks, tempo and cycle together, pre-roll, tempo drift, and processing
// while stopped. The engine-level versions (test_transport.cpp) call setTransport directly; these go through the
// host's numbers.
//
// Note starts are found as in test_transport: short notes on a constant (DC) source sound from their first sample
// and are silent between notes. Group "hostsync".

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "diag/DiagModel.h"
#include "engine/Engine.h"
#include "host/HostSync.h"
#include "host/MidiRouter.h"
#include "model/Clip.h"
#include "model/SnapshotCompiler.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000.0;

    /** 87.890625 bpm at 48 kHz is exactly 2^15 samples a beat, so every host position and every split sample of the
        cycle tests is exact in double: "the wrap 1, 200 and 2047 samples into a block" are those samples. */
    constexpr double kExactBpm = 87.890625;
    constexpr double kExactSpb = 32768.0;

    struct NoteSpec
    {
        double p, s, l;
    };

    model::Clip clipOf (int bars, const std::vector<NoteSpec>& notes, bool loop = true)
    {
        model::Clip c = model::newClip (bars);
        c.loop = loop;
        int i = 0;
        for (const NoteSpec& n : notes)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (n.p, n.s, n.l, "h" + std::to_string (i++))));
        return c;
    }

    /** The clip as the engine plays it (parity settings except loop, which HostSync passes on). */
    std::unique_ptr<core::Snapshot> compileClip (model::Clip c)
    {
        c.beatsPerBar = 4;
        c.offsetBeats = 0;
        c.tieAcrossLoop = false;
        return model::compileSnapshot (c, 64);
    }

    core::Inst tapeInst()
    {
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        return inst;
    }

    std::unique_ptr<engine::SampleData> dcSample (double secs = 4.0)
    {
        return render::monoSample (signals::dc (secs, kSr, 0.5), kSr);
    }

    std::unique_ptr<engine::SampleData> sawSample()
    {
        const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
        return engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw");
    }

    /** Where the fake host is at a block start, and what it says about it. */
    struct Song
    {
        bool playing = true;
        double ppq = 0, bpm = 120;
        bool cycling = false;
        double loopStart = 0, loopEnd = 0;
    };

    /** A host: its play head, HostSync, MidiRouter and a real Engine, as in processBlock. */
    struct Rig
    {
        harness::FakePlayHead head;
        host::HostSync sync;
        std::unique_ptr<engine::Engine> e = std::make_unique<engine::Engine>();
        std::unique_ptr<engine::SampleData> sample;
        std::unique_ptr<core::Snapshot> snap;
        juce::MidiBuffer midi;
        int wraps = 0;                   // blocks HostSync split at a cycle wrap
        std::vector<int> wrapOffsets;    // where in its block each one was

        Rig (const core::Inst& inst, std::unique_ptr<engine::SampleData> s, const model::Clip* clip)
        {
            e->prepare (kSr);
            e->setInst (inst);
            sample = std::move (s);
            e->setSample (sample.get(), 1);
            if (clip != nullptr)
            {
                snap = compileClip (*clip);
                e->setSnapshot (snap.get(), 1);
            }
            sync.prepare (kSr);
            midi.ensureSize (1024);
        }

        /** One host block of n samples into L and R (the play head says `song`); the MIDI added since the last block
            is played at its offsets, then cleared. */
        void block (const Song& song, float* L, float* R, int n)
        {
            head.info.setIsPlaying (song.playing);
            head.info.setBpm (song.bpm);
            head.info.setPpqPosition (song.ppq);
            head.info.setIsLooping (song.cycling);
            if (song.cycling)
                head.info.setLoopPoints (juce::AudioPlayHead::LoopPoints { song.loopStart, song.loopEnd });
            else
                head.info.setLoopPoints (juce::nullopt);

            const auto hb = diag::readHostBlock (&head, n, false);
            const auto t = sync.plan (hb, n, snap != nullptr ? snap->offsetBeats : 0.0, snap == nullptr || snap->loop);
            if (t.wrapAt >= 0)
            {
                ++wraps;
                wrapOffsets.push_back (t.wrapAt);
            }
            host::MidiRouter::renderBlock (*e, t, midi, host::MidiSettings {}, L, R, n);
            midi.clear();
        }
    };

    /** A host that plays straight on (or round a cycle it reports, without splitting its blocks at the cycle end):
        its position at each block start is integrated from its tempo, sample by sample. */
    struct Output
    {
        std::vector<float> L, R;
        std::vector<int> maxVoices;   // per block: voices at its end
    };

    using BpmAt = std::function<double (double ppq)>;

    /** Renders n samples in blocks of `block` from host position ppq0. bpmAt gives the tempo the host reports at a
        block start (and plays through it). A cycle [loopStart, loopEnd) wraps when the host's position passes its
        end, at the exact sample; the host keeps its block sizes. onBlock (index, song) may change what the host
        reports (jitter). */
    Output hostRender (Rig& rig, double ppq0, std::int64_t n, int block, const BpmAt& bpmAt, bool cycling = false, double loopStart = 0,
                       double loopEnd = 0, const std::function<void (int, Song&)>& onBlock = {})
    {
        Output out;
        out.L.assign (static_cast<std::size_t> (n), 0.0f);
        out.R.assign (static_cast<std::size_t> (n), 0.0f);
        double ppq = ppq0;
        int index = 0;
        for (std::int64_t k = 0; k < n; k += block, ++index)
        {
            const int len = static_cast<int> (std::min<std::int64_t> (block, n - k));
            Song song;
            song.ppq = ppq;
            song.bpm = bpmAt (ppq);
            song.cycling = cycling;
            song.loopStart = loopStart;
            song.loopEnd = loopEnd;
            if (onBlock)
                onBlock (index, song);
            rig.block (song, out.L.data() + k, out.R.data() + k, len);
            out.maxVoices.push_back (rig.e->stats().voices);

            // The host's own position at the next block start, at the tempo it reported for this one.
            const double bps = bpmAt (ppq) / 60.0 / kSr;
            for (int i = 0; i < len; ++i)
            {
                ppq += bps;
                if (cycling && ppq >= loopEnd)
                    ppq -= loopEnd - loopStart;
            }
        }
        return out;
    }

    /** The first sample of every stretch of sound that follows silence. */
    std::vector<std::int64_t> soundStarts (const std::vector<float>& L, const std::vector<float>& R, std::int64_t from = 0)
    {
        std::vector<std::int64_t> out;
        bool sounding = false;
        for (std::size_t i = static_cast<std::size_t> (from); i < L.size(); ++i)
        {
            const bool now = L[i] != 0.0f || R[i] != 0.0f;
            if (now && ! sounding)
                out.push_back (static_cast<std::int64_t> (i));
            sounding = now;
        }
        return out;
    }

    /** Every start within [0, 1) sample after its exact time (a hair of rounding below 0 allowed). */
    bool startsOnTime (const std::vector<std::int64_t>& got, const std::vector<double>& exact, std::string& why)
    {
        if (got.size() != exact.size())
        {
            why = fmt ("%zu starts, expected %zu", got.size(), exact.size());
            return false;
        }
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            const double off = static_cast<double> (got[i]) - exact[i];
            if (! (off >= -1e-6 && off < 1))
            {
                why = fmt ("start %zu at sample %lld, the exact time %.4f (%+.4f)", i, static_cast<long long> (got[i]), exact[i], off);
                return false;
            }
        }
        return true;
    }

    double rmsOf (const std::vector<float>& x, std::int64_t a, std::int64_t b)
    {
        double s = 0;
        for (std::int64_t i = a; i < b; ++i)
            s += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
        return b > a ? std::sqrt (s / static_cast<double> (b - a)) : 0;
    }

    int most (const std::vector<int>& v)
    {
        return v.empty() ? 0 : *std::max_element (v.begin(), v.end());
    }

    BpmAt constant (double bpm)
    {
        return [bpm] (double) { return bpm; };
    }

    const std::vector<NoteSpec> kFourShort = { { 60, 0, 0.25 }, { 62, 1, 0.25 }, { 64, 2, 0.25 }, { 65, 3, 0.25 } };
}

SL_TEST ("hostsync", noteStartsThroughHostSync)
{
    section ("note starts (PROMPT 14.9) through the play head and HostSync: 120, 87 and 133 bpm, loop passes 1-3, a step 120 -> 87: [0, 1) sample after the beat");

    const model::Clip clip = clipOf (1, kFourShort);
    for (const double bpm : { 120.0, 87.0, 133.0 })
    {
        const double spb = 60.0 / bpm * kSr;
        for (const bool cycle : { false, true })
        {
            Rig rig (tapeInst(), dcSample(), &clip);
            // 3 passes: the clip loops by itself, or the host cycles round its 4 beats (HostSync splits the block)
            const auto out = hostRender (rig, 0.0, static_cast<std::int64_t> (12 * spb), 333, constant (bpm), cycle, 0.0, 4.0);
            std::vector<double> exact;
            for (int b = 0; b < 12; ++b)
                exact.push_back (b * spb);
            std::string why;
            const bool onTime = startsOnTime (soundStarts (out.L, out.R), exact, why);
            check (onTime && most (out.maxVoices) == 1 && (! cycle || rig.wraps == 2),
                   fmt ("%g bpm, %s: 12 starts in 3 passes, each in [0, 1) sample of its beat (most voices %d, cycle wraps split %d) %s", bpm,
                        cycle ? "the host cycling round the clip" : "the clip looping", most (out.maxVoices), rig.wraps, why.c_str()));
        }
    }

    // A step 120 -> 87 from the block holding beat 3.5: the host reports the new tempo at that block's start.
    const int block = 500;
    const std::int64_t stepAt = 84000;   // beat 3.5 at 120 bpm, a block start
    auto bpmAt = [] (double ppq) { return ppq < 3.5 - 1e-9 ? 120.0 : 87.0; };
    Rig rig (tapeInst(), dcSample(), &clip);
    const double spb87 = 60.0 / 87 * kSr;
    const auto out = hostRender (rig, 0.0, static_cast<std::int64_t> (stepAt + 8.5 * spb87), block, bpmAt);
    std::vector<double> exact;
    for (int b = 0; b < 12; ++b)
        exact.push_back (b < 4 ? b * 24000.0 : static_cast<double> (stepAt) + (b - 3.5) * spb87);
    std::string why;
    const bool onTime = startsOnTime (soundStarts (out.L, out.R), exact, why);
    check (onTime, "after the host steps 120 -> 87 at beat 3.5: every start in [0, 1) sample of its beat " + why);

    // a no-op is not a pass: against 120 bpm throughout the same starts fail
    std::vector<double> stale;
    for (int b = 0; b < 12; ++b)
        stale.push_back (b * 24000.0);
    std::string why2;
    check (! startsOnTime (soundStarts (out.L, out.R), stale, why2), "negative control: measured against 120 bpm throughout they fail (" + why2 + ")");
}

SL_TEST ("hostsync", jitterThroughHostSync)
{
    section ("jitter: the play head's position off by up to +-0.003 beat (seeded) every block for 64 bars at 120 bpm: 0 doubled, 0 missed, the render unchanged");

    const model::Clip clip = clipOf (1, kFourShort);
    const std::int64_t n = 64 * 4 * 24000;
    Rig plain (tapeInst(), dcSample(), &clip);
    const auto ref = hostRender (plain, 0.0, n, 512, constant (120));

    Rig jittered (tapeInst(), dcSample(), &clip);
    signals::Lcg lcg (11);
    double largest = 0;
    const auto out = hostRender (jittered, 0.0, n, 512, constant (120), false, 0, 0, [&lcg, &largest] (int index, Song& s) {
        if (index == 0)
            return;
        const double off = (lcg() * 2 - 1) * 0.003;
        largest = std::max (largest, std::abs (off));
        s.ppq += off;
    });
    std::vector<double> exact;
    for (int b = 0; b < 256; ++b)
        exact.push_back (b * 24000.0);
    std::string why;
    const auto starts = soundStarts (out.L, out.R);
    const bool onTime = startsOnTime (starts, exact, why);
    check (largest > 0.0029, fmt ("the play head was off by up to %.5f beat", largest));
    check (onTime && most (out.maxVoices) == 1,
           fmt ("256 starts (%zu found), each in [0, 1) sample of its beat, never two voices at once (most %d) %s", starts.size(), most (out.maxVoices), why.c_str()));
    check (out.L == ref.L && out.R == ref.R, "the jittered render equals the unjittered one bit for bit");
}

SL_TEST ("hostsync", cycleWrapInsideBlocks)
{
    section ("cycle wrap: a host that reports its loop points and does not split blocks (2048 samples), the wrap 1, 200 and 2047 samples into a block");

    constexpr int block = 2048;
    const BpmAt bpm = constant (kExactBpm);
    for (const int w : { 1, 200, 2047 })
    {
        // The host starts w samples before beat 0 (silent pre-roll), so every beat that is a multiple of 2048
        // samples falls w samples into a block.
        const double ppq0 = -w / kExactSpb;

        // (1) A cycle of 2 clip lengths (8 beats) renders equal to the uncycled render, bit for bit.
        {
            const model::Clip clip = clipOf (1, { { 60, 0, 1.5 }, { 64, 1.75, 0.5 }, { 67, 2.5, 1.5 }, { 72, 3.875, 0.5 } });
            const std::int64_t n = w + 3 * 8 * static_cast<std::int64_t> (kExactSpb) + 4096;
            Rig straight (tapeInst(), sawSample(), &clip);
            const auto ref = hostRender (straight, ppq0, n, block, bpm);
            Rig cycled (tapeInst(), sawSample(), &clip);
            const auto out = hostRender (cycled, ppq0, n, block, bpm, true, 0.0, 8.0);
            bool allAtW = cycled.wraps == 3;
            for (const int at : cycled.wrapOffsets)
                allAtW = allAtW && at == w;
            check (allAtW, fmt ("wrap %d: HostSync split %d blocks, each %d samples in", w, cycled.wraps, cycled.wrapOffsets.empty() ? -1 : cycled.wrapOffsets[0]));
            check (out.L == ref.L && out.R == ref.R && rmsOf (ref.L, 0, n) > 0.01,
                   fmt ("wrap %d: 3 passes of a cycle of 2 clip lengths equal the uncycled render bit for bit (rms %.3f)", w, rmsOf (ref.L, 0, n)));

            // a no-op is not a pass: a cycle of 1.5 clip lengths does not
            Rig other (tapeInst(), sawSample(), &clip);
            const auto odd = hostRender (other, ppq0, n, block, bpm, true, 0.0, 6.0);
            check (odd.L != ref.L, fmt ("wrap %d: negative control: a cycle of 1.5 clip lengths differs from the uncycled render", w));
        }

        // (2) A cycle of 1.5 clip lengths (beats 1 to 7) chases at the wrap sample inside a long note, and the note
        // at the cycle end (clip beat 3, song beat 7) never starts there.
        {
            const model::Clip clip = clipOf (1, { { 60, 0, 4 }, { 72, 3, 0.25 } });
            constexpr std::int64_t wrapBlock = 112;   // song beat 7 = 229376 samples = block 112's sample w
            constexpr std::int64_t cycleBlocks = 96;  // 6 beats = 196608 samples = 96 blocks: every wrap is w samples in
            constexpr std::int64_t refBlock = 16;     // song beat 1 = block 16's sample w (the straight render)
            constexpr std::int64_t p72Block = 48;     // song beat 3: the note at clip beat 3 sounds at this block's end
            constexpr std::int64_t later = 4;

            auto headAt = [] (const engine::DisplayFrame& f, int pitch, bool releasedToo) {
                for (int i = 0; i < f.numVoices; ++i)
                    if (f.voices[i].p == pitch && (releasedToo || ! f.voices[i].released) && f.voices[i].numHeads > 0)
                        return static_cast<double> (f.voices[i].heads[0].pos);
                return std::numeric_limits<double>::quiet_NaN();
            };

            // The frame at the end of every block; the host round its cycle [1, 7) or straight on.
            auto frames = [&] (bool cycling, std::vector<std::unique_ptr<engine::DisplayFrame>>& keep, std::vector<int>& offsets) {
                Rig rig (tapeInst(), dcSample (2.0), &clip);
                std::vector<float> L (block), R (block);
                double ppq = ppq0;
                for (std::int64_t b = 0; b < wrapBlock + 2 * cycleBlocks + later + 2; ++b)
                {
                    Song s;
                    s.ppq = ppq;
                    s.bpm = kExactBpm;
                    s.cycling = cycling;
                    s.loopStart = 1.0;
                    s.loopEnd = 7.0;
                    rig.block (s, L.data(), R.data(), block);
                    auto f = std::make_unique<engine::DisplayFrame>();
                    rig.e->fillDisplay (*f);
                    keep.push_back (std::move (f));
                    for (int i = 0; i < block; ++i)
                    {
                        ppq += 1.0 / kExactSpb;
                        if (cycling && ppq >= 7.0)
                            ppq -= 6.0;
                    }
                }
                offsets = rig.wrapOffsets;
            };
            std::vector<std::unique_ptr<engine::DisplayFrame>> straightFrames, cycledFrames;
            std::vector<int> straightOffsets, offsets;
            frames (false, straightFrames, straightOffsets);
            frames (true, cycledFrames, offsets);
            auto at = [] (const std::vector<std::unique_ptr<engine::DisplayFrame>>& v, std::int64_t b) -> const engine::DisplayFrame& {
                return *v[static_cast<std::size_t> (b)];
            };

            bool split = offsets.size() == 3 && straightOffsets.empty();
            for (const int o : offsets)
                split = split && o == w;
            check (split, fmt ("wrap %d: HostSync split the cycle's %zu wraps, each %d samples in", w, offsets.size(), offsets.empty() ? -1 : offsets[0]));

            // At the end of the wrap's block the chased note is 1 beat and the rest of the block into itself: the
            // straight render's playhead at the same song beat, and the formula (Travel 1x on a 2 s sound).
            const double want = headAt (at (straightFrames, refBlock), 60, false);
            const double got = headAt (at (cycledFrames, wrapBlock), 60, false);
            const double wantLater = headAt (at (straightFrames, refBlock + later), 60, false);
            const double gotLater = headAt (at (cycledFrames, wrapBlock + later), 60, false);
            const double expected = (kExactSpb + block - w) / (2.0 * kSr);
            check (std::abs (got - want) <= 0.002 && std::abs (want - expected) <= 0.002,
                   fmt ("wrap %d: at the end of the wrap's block the chased note's playhead is at %.6f of the file, the straight render's %.6f (expected %.6f)", w,
                        got, want, expected));
            check (std::abs (gotLater - wantLater) <= 0.002,
                   fmt ("wrap %d: %lld blocks later still within 0.002 (%.6f against %.6f)", w, static_cast<long long> (later), gotLater, wantLater));

            // No note after the cycle end: the note at song beat 7 would sound at the end of each wrap's block had it
            // started (it lasts 8192 samples); it sounds at the end of block 48 (song beat 3), so the look can see it.
            int p72 = 0;
            for (std::int64_t i = 0; i < 3; ++i)
                p72 += std::isnan (headAt (at (cycledFrames, wrapBlock + i * cycleBlocks), 72, true)) ? 0 : 1;
            const bool p72Seen = ! std::isnan (headAt (at (cycledFrames, p72Block), 72, true));
            check (p72 == 0 && p72Seen,
                   fmt ("wrap %d: the note at the cycle end is in none of the 3 wrap blocks (%d), and is seen where it does play (song beat 3: %s)", w, p72,
                        p72Seen ? "yes" : "no"));
        }
    }
}

SL_TEST ("hostsync", tempoAndCycleTogether)
{
    section ("tempo and cycle together: a tempo ramp across 3 cycle passes of a 1-bar clip, the wrap reported 0.003 beat late, then 0.003 early: every note once per pass");

    const model::Clip clip = clipOf (1, kFourShort);

    // The host's song: a tempo falling 120 -> 90 bpm over the 12 beats it plays (3 passes of the 4-beat cycle),
    // integrated per sample and reported at each block start. The first block after the first wrap reports its
    // position `late` beat late, the first after the second wrap `early` beat early. A host that splits ends a
    // block at the cycle end (Cubase with ASIO-Guard off); one that does not leaves the split to HostSync.
    struct Result
    {
        std::size_t starts = 0;
        int mostVoices = 0;
        int splits = 0;
    };
    auto run = [&clip] (bool hostSplits, double late, double early, double loopStart) {
        Rig rig (tapeInst(), dcSample (8.0), &clip);
        std::vector<float> L, R;
        Result r;
        double songBeat = 0, ppq = 0;
        int wrapsSoFar = 0;
        bool justWrapped = false;
        std::int64_t k = 0;
        while (songBeat < 12.0 - 1e-9)
        {
            const double bpm = 120.0 - 30.0 * songBeat / 12.0;
            const double bps = bpm / 60.0 / kSr;
            // the song ends after 12 beats: the last block stops short of the third wrap
            const double remaining = std::floor ((12.0 - songBeat) / bps);
            if (remaining < 1)
                break;
            int len = remaining < 512 ? static_cast<int> (remaining) : 512;
            if (hostSplits)
            {
                const double toEnd = std::ceil ((4.0 - ppq) / bps);
                if (toEnd >= 1 && toEnd < len)
                    len = static_cast<int> (toEnd);
            }
            Song s;
            s.ppq = ppq + (justWrapped ? (wrapsSoFar == 1 ? late : early) : 0.0);
            s.bpm = bpm;
            s.cycling = true;
            s.loopStart = loopStart;
            s.loopEnd = 4;
            justWrapped = false;
            L.resize (static_cast<std::size_t> (k + len));
            R.resize (static_cast<std::size_t> (k + len));
            rig.block (s, L.data() + k, R.data() + k, len);
            r.mostVoices = std::max (r.mostVoices, rig.e->stats().voices);
            for (int i = 0; i < len; ++i)
            {
                ppq += bps;
                songBeat += bps;
                if (ppq >= 4.0)
                {
                    ppq -= 4.0 - loopStart;
                    ++wrapsSoFar;
                    justWrapped = true;
                }
            }
            k += len;
        }
        r.starts = soundStarts (L, R).size();
        r.splits = rig.wraps;
        return r;
    };

    for (const bool hostSplits : { true, false })
    {
        const Result r = run (hostSplits, 0.003, -0.003, 0.0);
        check (r.starts == 12 && r.mostVoices == 1 && (hostSplits || r.splits == 2),
               fmt ("%s: %zu note starts in 3 passes (12 expected), never two voices at once (most %d), HostSync split %d blocks",
                    hostSplits ? "the host splits its block at the cycle end" : "the host does not split", r.starts, r.mostVoices, r.splits));
    }

    // a no-op is not a pass: a host whose cycle starts at beat 0.3 (past the 0.25-beat downbeat) really skips the
    // downbeat of passes 2 and 3, and the count shows it
    const Result lost = run (true, 0.0, 0.0, 0.3);
    check (lost.starts == 10, fmt ("negative control: a cycle from beat 0.3 skips 2 downbeats (%zu starts)", lost.starts));
}

SL_TEST ("hostsync", preRollThroughHostSync)
{
    section ("pre-roll: the host plays from ppq -2, the clip's loop on and off: silence before ppq 0, the first note in [0, 1) sample of it");

    for (const bool loop : { true, false })
    {
        // a 2-bar clip: a note at beat 0 and one sounding through the clip's last 2 beats
        const model::Clip clip = clipOf (2, { { 60, 0, 1 }, { 67, 6, 2 } }, loop);
        Rig rig (tapeInst(), dcSample(), &clip);
        const auto out = hostRender (rig, -2.0, 48000 + 24000, 128, constant (120));
        const double before = std::max (rmsOf (out.L, 0, 48000), rmsOf (out.R, 0, 48000));
        const std::int64_t first = render::firstSounding (out.L, out.R, 0, 72000);
        check (before == 0.0 && first == 48000,
               fmt ("loop %s: rms before ppq 0 = %g, the first sound at sample %lld (ppq 0 = 48000)", loop ? "on" : "off", before, static_cast<long long> (first)));
    }
    // a no-op is not a pass: from ppq 6.5 the last note sounds at once
    const model::Clip clip = clipOf (2, { { 60, 0, 1 }, { 67, 6, 2 } });
    Rig rig (tapeInst(), dcSample(), &clip);
    const auto out = hostRender (rig, 6.5, 4800, 128, constant (120));
    check (rmsOf (out.L, 0, 4800) > 0.01, fmt ("negative control: from ppq 6.5 the last note sounds at once (rms %.3f)", rmsOf (out.L, 0, 4800)));
}

SL_TEST ("hostsync", tempoDriftThroughHostSync)
{
    section ("tempo drift: the host ramps 120 -> 60 bpm over 8 bars (ppq integrated per sample), its tempo at each block start, 512-sample blocks");

    std::vector<NoteSpec> notes;
    for (int b = 0; b < 32; ++b)
        notes.push_back ({ 60.0 + (b % 12), static_cast<double> (b), 0.25 });
    const model::Clip clip = clipOf (8, notes);

    // The host's position per sample, integrated from a tempo that falls linearly with the beat.
    auto bpmAt = [] (double ppq) { return 120 - 60 * std::min (std::max (ppq, 0.0), 32.0) / 32; };
    std::vector<double> ppq (1, 0.0);
    while (ppq.back() < 31.5)
        ppq.push_back (ppq.back() + bpmAt (ppq.back()) / 60 / kSr);
    const auto n = static_cast<std::int64_t> (ppq.size());
    std::vector<double> hostStart;
    for (int b = 0; b < 32; ++b)
    {
        const auto it = std::lower_bound (ppq.begin(), ppq.end(), static_cast<double> (b));
        hostStart.push_back (static_cast<double> (it - ppq.begin()));
    }

    // The play head reports the integrated position and the tempo of the moment at each block start.
    Rig rig (tapeInst(), dcSample(), &clip);
    std::vector<float> L (static_cast<std::size_t> (n)), R (static_cast<std::size_t> (n));
    for (std::int64_t k = 0; k < n; k += 512)
    {
        const int len = static_cast<int> (std::min<std::int64_t> (512, n - k));
        Song s;
        s.ppq = ppq[static_cast<std::size_t> (k)];
        s.bpm = bpmAt (s.ppq);
        rig.block (s, L.data() + k, R.data() + k, len);
    }
    const auto starts = soundStarts (L, R);
    double worst = 0;
    bool all = starts.size() == 32;
    for (std::size_t i = 0; all && i < starts.size(); ++i)
    {
        const double off = static_cast<double> (starts[i]) - hostStart[i];
        worst = std::max (worst, std::abs (off));
        all = std::abs (off) <= 1;
    }
    const double last = starts.size() == 32 ? static_cast<double> (starts.back()) - hostStart.back() : 1e9;
    check (all && std::abs (last) <= 2,
           fmt ("32 starts (%zu): each within 1 sample of the host's position (worst %.0f), the last %+.0f", starts.size(), worst, last));

    // a no-op is not a pass: a host that reports only the tempo (no position) leaves the engine on its own clock
    Rig own (tapeInst(), dcSample(), &clip);
    std::vector<float> L2 (static_cast<std::size_t> (n)), R2 (static_cast<std::size_t> (n));
    for (std::int64_t k = 0; k < n; k += 512)
    {
        const int len = static_cast<int> (std::min<std::int64_t> (512, n - k));
        own.head.info.setPpqPosition (k == 0 ? juce::Optional<double> (0.0) : juce::Optional<double> ());
        own.head.info.setIsPlaying (true);
        own.head.info.setBpm (bpmAt (ppq[static_cast<std::size_t> (k)]));
        const auto hb = diag::readHostBlock (&own.head, len, false);
        const auto t = own.sync.plan (hb, len, 0.0, true);
        host::MidiRouter::renderBlock (*own.e, t, own.midi, host::MidiSettings {}, L2.data() + k, R2.data() + k, len);
    }
    const auto drift = soundStarts (L2, R2);
    const double driftLast = drift.size() == 32 ? static_cast<double> (drift.back()) - hostStart.back() : 1e9;
    check (std::abs (driftLast) > 50, fmt ("negative control: with no position from the host the last start is %+.0f samples off", driftLast));
}

SL_TEST ("hostsync", stoppedProcessingThroughHostSync)
{
    section ("stopped processing: live Scatter 1/8T while the host is stopped: one jump per 1/3 beat of the free clock, 0..32 samples after each grid line; "
             "while it plays, on the song's beat");

    core::Inst inst = tapeInst();
    inst.motion = core::Motion::scatter;
    inst.jump = core::RateId::r1_8T;

    for (const bool playing : { false, true })
    {
        Rig rig (inst, dcSample (20.0), nullptr);
        const double n = static_cast<double> (rig.sample->n);
        auto frame = std::make_unique<engine::DisplayFrame>();
        float L = 0, R = 0;
        std::vector<std::int64_t> jumps;
        double lastPos = std::numeric_limits<double>::quiet_NaN();
        const double ppq0 = playing ? 0.37 : 0.0;   // playing: from beat 0.37, so the grid lines fall inside the blocks
        for (std::int64_t k = 0; k < 96000; ++k)
        {
            if (k == 1000)
            {
                const juce::uint8 on[3] = { 0x90, 60, 127 };
                rig.midi.addEvent (on, 3, 0);
            }
            Song s;
            s.playing = playing;
            s.bpm = 120;
            s.ppq = playing ? ppq0 + static_cast<double> (k) / 24000.0 : 0.0;   // stopped: the position stands still
            rig.block (s, &L, &R, 1);
            rig.e->fillDisplay (*frame);
            double pos = std::numeric_limits<double>::quiet_NaN();
            if (frame->numVoices == 1 && frame->voices[0].numHeads >= 1)
                pos = frame->voices[0].heads[0].pos;
            if (! std::isnan (pos) && ! std::isnan (lastPos) && std::abs ((pos - lastPos) * n) > 100)
                jumps.push_back (k);
            lastPos = pos;
        }
        // The grid lines: every 8000 samples of the free clock from sample 0 (stopped), or of the song from beat 0.37.
        std::vector<std::int64_t> lines;
        for (int j = 1; j < 40; ++j)
        {
            const double beat = j / 3.0;
            const double at = (beat - ppq0) * 24000.0;
            if (at > 1000 && at < 96000 - 32)
                lines.push_back (static_cast<std::int64_t> (std::ceil (at - 1e-9)));
        }
        bool onGrid = jumps.size() == lines.size();
        std::string why;
        for (std::size_t j = 0; onGrid && j < jumps.size(); ++j)
        {
            const std::int64_t late = jumps[j] - lines[j];
            onGrid = late >= 0 && late <= 32;
            if (! onGrid)
                why = fmt (": jump %zu at %lld, its grid line %lld", j, static_cast<long long> (jumps[j]), static_cast<long long> (lines[j]));
        }
        check (onGrid, fmt ("%s: %zu jumps (%zu grid lines), each 0..32 samples after its grid line%s", playing ? "playing from beat 0.37" : "stopped",
                            jumps.size(), lines.size(), why.c_str()));
    }
}
