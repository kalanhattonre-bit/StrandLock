// M7 "Region precision" (PLUGIN-PLAN 3, 1.6 #12, 2.2, 8.2 #26): a one-bar part on a 10-minute 48 kHz file loops with a
// period equal to the f64 Region length in frames. The part is set the way a part chip sets it (processor::usePart: the
// parameters and the exact f64 edges in INST, one undo step), a live Tape note at the root reads one frame per sample,
// and after 64 passes the output repeats the first pass bit for bit at exactly 63 periods (0 frames of drift). The edges
// are chosen where float32 edges (what the parameters hold) would make a period frames off, and the test shows that
// such a period would not match. Then a host moves the Region start (automation): the parameter's value plays (readInst
// and the period follow it), and the end, which the host left alone, keeps its exact edge.

#include "SongRig.h"

#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"

#include <cmath>
#include <cstring>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kRate = 48000.0;
    constexpr std::int64_t kFrames = 600 * 48000;   // 10 minutes

    /** The engine's loop period for a Region (VoiceTick: L = max (64, min (n - rs, floor ((end - start) x n)))). */
    double periodOf (double start, double end)
    {
        const double n = (double) kFrames;
        const double rs = std::floor (start * n);
        return std::max (64.0, std::min (n - rs, std::floor ((end - start) * n)));
    }

    bool sameRun (const std::vector<float>& x, std::int64_t a, std::int64_t b, int count)
    {
        return a >= 0 && b >= 0 && a + count <= (std::int64_t) x.size() && b + count <= (std::int64_t) x.size()
            && std::memcmp (x.data() + a, x.data() + b, (std::size_t) count * sizeof (float)) == 0;
    }

    /** The lag (-range .. range) at which pass k's window repeats pass 0's bit for bit, or a value past range when none. */
    int driftAt (const std::vector<float>& x, std::int64_t window0, std::int64_t period, int k, int count, int range)
    {
        for (int d = 0; d <= range; ++d)
            for (const int s : { d, -d })
                if (sameRun (x, window0, window0 + (std::int64_t) k * period + s, count))
                    return s;
        return range + 1;
    }

    double rms (const std::vector<float>& x, std::int64_t from, int count)
    {
        double s = 0;
        for (int i = 0; i < count; ++i)
            s += (double) x[(std::size_t) (from + i)] * (double) x[(std::size_t) (from + i)];
        return std::sqrt (s / count);
    }

    /** A clean Tape read: one head at the root, no crossfade at the loop (the period is the Region's length). */
    void cleanTape (StrandLockProcessor& p)
    {
        songs::setPlain (p, params::P::engine, 0.0f);     // Tape
        songs::setPlain (p, params::P::motion, 0.0f);     // Forward
        songs::setPlain (p, params::P::heads, 1.0f);
        songs::setPlain (p, params::P::spread, 0.0f);
        songs::setPlain (p, params::P::drift, 0.0f);
        songs::setPlain (p, params::P::loopXfade, 0.0f);
        songs::setPlain (p, params::P::atEnd, 0.0f);      // Loop
        songs::setPlain (p, params::P::rootNote, 60.0f);
        songs::setPlain (p, params::P::rootCents, 0.0f);
        songs::setPlain (p, params::P::speed, 1.0f);
        songs::setPlain (p, params::P::pitch, 0.0f);
    }
}

SL_TEST ("songs", regionPrecisionOneBarPart)
{
    section ("Region precision (M7, plan 3): a one-bar part on a 10-minute 48 kHz file loops on the f64 Region length, "
             "0 frames of drift over 64 passes; host automation plays the parameter's value");

    const auto dir = songs::tempFolder ("region-precision");
    const auto file = dir.getChildFile ("ten minutes mono.wav");
    songs::Noise noise (2026);
    const bool written = songs::writeWav16 (file, 1, kRate, kFrames, [&noise] (std::int64_t start, int count, float* const* out)
    {
        for (int i = 0; i < count; ++i)
            out[0][i] = (float) (0.15 * noise.next() + 0.1 * std::sin (2.0 * 3.141592653589793 * 97.0 * (double) (start + i) / kRate));
    });
    check (written, fmt ("a 10-minute mono 48 kHz 16-bit WAV written at test time (%lld bytes)", (long long) file.getSize()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (file);
    p.waitForSounds (120000);
    check (p.getCurrentSound() != nullptr && p.getCurrentSound()->frames == kFrames, "it loads whole (10 minutes)");

    // One bar at 120 bpm (2 s), from near 80 % in, where a float32 edge is up to 1.7 frames off: the first start
    // there whose float32 edges would give a period at least 2 frames off.
    const double bar = 96000.0 / (double) kFrames;
    double start = 0.8, end = start + bar;
    for (int k = 0; k < 20000; ++k)
    {
        const double s = 0.8 + (double) k * 1.0e-7, e = s + bar;
        if (std::abs (periodOf ((double) (float) s, (double) (float) e) - periodOf (s, e)) >= 2.0)
        {
            start = s;
            end = e;
            break;
        }
    }
    const double exactPeriod = periodOf (start, end), floatPeriod = periodOf ((double) (float) start, (double) (float) end);
    check (std::abs (floatPeriod - exactPeriod) >= 2.0,
           fmt ("the part %.9f .. %.9f: %.0f frames long in f64, %.0f with float32 edges", start, end, exactPeriod, floatPeriod));

    analysis::Part part;
    part.kind = analysis::PartKind::loop;
    part.label = "One-bar loop 120 bpm 8:00";
    part.start = start;
    part.end = end;
    part.bpm = 120.0;
    part.tempoFound = true;
    const auto steps = p.historySize();
    const bool used = p.usePart (part);
    const auto inst = p.readInst();
    check (used && p.historySize() == steps + 1 && std::memcmp (&inst.region.start, &start, sizeof (double)) == 0
               && std::memcmp (&inst.region.end, &end, sizeof (double)) == 0
               && juce::exactlyEqual (songs::plainOf (p, params::P::regionStart), (float) start),
           "the part chip's way (usePart): one undo step; the audio thread plays the exact f64 edges while the parameters hold their floats");

    cleanTape (p);
    const int window = 2048;
    const std::int64_t window0 = 24000;   // after the attack and the gains' 20 ms slews
    const auto total = window0 + 63 * (std::int64_t) exactPeriod + window + 64;
    const auto out = harness::render (p, kRate, total, harness::fixedBlocks (512), { harness::noteOn (0, 60, 100) });
    const double level = rms (out.left, window0, window);
    check (level > 0.01, fmt ("a live Tape note at the root plays the part (rms %.4f)", level));
    int worst = 0;
    std::string drifts;
    for (const int k : { 1, 2, 8, 32, 63 })
    {
        const int d = driftAt (out.left, window0, (std::int64_t) exactPeriod, k, window, 8);
        worst = std::max (worst, std::abs (d));
        drifts += fmt ("%s%d", drifts.empty() ? "" : ", ", d);
    }
    check (worst == 0, fmt ("pass 2, 3, 9, 33 and 64 repeat pass 1 bit for bit exactly k periods on: drift %s frames (0 frames over 64 passes)", drifts.c_str()));
    check (! sameRun (out.left, window0, window0 + 63 * (std::int64_t) floatPeriod, window),
           fmt ("negative control: at 63 float32 periods the window does not repeat (float32 edges would be %.0f frames off after 64 passes)",
                63.0 * std::abs (floatPeriod - exactPeriod)));

    // Host automation moves the start: the parameter's value plays.
    const float moved = (float) (start - 0.0004);
    songs::hostSets (p.getParam (params::P::regionStart), moved);
    const auto autoInst = p.readInst();
    const double movedStart = (double) songs::plainOf (p, params::P::regionStart);
    const auto r = p.currentRegion();
    check (std::memcmp (&autoInst.region.start, &movedStart, sizeof (double)) == 0 && std::memcmp (&autoInst.region.end, &end, sizeof (double)) == 0
               && std::memcmp (&r.first, &movedStart, sizeof (double)) == 0 && ! juce::exactlyEqual (movedStart, start),
           fmt ("host automation to %.9f: the audio thread plays the parameter's value; the end, left alone, keeps its exact edge", movedStart));
    const double autoPeriod = periodOf (movedStart, end);
    const auto total2 = window0 + 8 * (std::int64_t) autoPeriod + window + 64;
    const auto out2 = harness::render (p, kRate, total2, harness::fixedBlocks (512), { harness::noteOn (0, 60, 100) });
    const int d2 = driftAt (out2.left, window0, (std::int64_t) autoPeriod, 8, window, 8);
    check (d2 == 0 && std::abs (autoPeriod - exactPeriod) > 100.0,
           fmt ("and the loop follows it: %.0f frames a pass (was %.0f), pass 9 repeats pass 1 at 8 of those periods (drift %d)", autoPeriod, exactPeriod, d2));
    p.releaseResources();
    dir.deleteRecursively();
}
