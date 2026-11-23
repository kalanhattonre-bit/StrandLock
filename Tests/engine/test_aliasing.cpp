// Fast reads without aliasing (PLUGIN-PLAN 4 M7 "fast reads (9.6)" and "fast reads: memory and CPU", 7.9 decision 6,
// question 9.6). M3 printed this file's figure (7.5 decision 3); M7 V4 gates it. Played faster than it was recorded, a
// sound's top octaves fold back below half the sample rate as tones that are not part of the note. The figure is the
// energy off the harmonic lines of the played note, in dB of the whole (prototype-v4/tests/test-sounds.js inharmonicDb:
// every bin more than 10 Hz from a multiple of f0, from 20 Hz up), on 2^16 samples from 0.5 s.
//
//  - fastReadsAliasing: the Saw on Tape at +12, +24 and +36 st at 44.1, 48 and 96 kHz, with its octave levels built as the
//    SampleManager's worker builds them: at most -40 dB each, every read from the level the rule chooses; +0 st bit for
//    bit with the levels off; the M3 figure (the levels off) beside each; the measure itself (the band-limited Saw reads
//    < -60 dB, a naive saw > -40 dB). Fix pass F1: also between the octaves, each level read just after its change
//    (about 0.63x of its own rate) and just before the next (about 1.2x), at 44.1 and 48 kHz.
//  - fastReadsParity: every engine/renders.json case with its sound's levels built: switched off, bit for bit with no
//    levels at all (M3's render, which fixtureEngineRendersTape and fixtureEngineRendersGrains compare with the
//    prototype); switched on, every case that reads at or below 1.25x bit for bit with that too. Fix pass F2: which cases
//    read a level, and how often from each, is pinned, so a read at or below 1.25x that takes a level fails.
//  - fastReadsLockLevel (fix pass F2): a Lock note reads at the rate ratio: a 96 kHz sound in a 44.1 kHz session reads
//    level 1, a 44.1 or 48 kHz sound none, whatever the note.
//  - fastReadsSweeps: C3 -> C6 over 2 s on Tape (44.1, 48 and 96 kHz) and Voice notes that read above 1x (the formant
//    raised to +12 st; the formant at +12 st with the Tape switch and Speed 1 -> 4; the plan's C3 -> C6 with the formant
//    at +12 st): the largest step around each level change over the largest steady step before it, < 1.5.
//  - fastReadsLevelsMatchTheSound: the levels themselves (frames, format, bytes, the frame each level stands for, the
//    filter's pass and stop bands, a short sound, the stop question).
//  - fastReadsTapeCpu96k (group cpu): 16 voices x 8 heads Tape at 96 kHz, every head above 2x, levels on against off.
// Groups "engine" and "cpu".

#include "engine/EngineRender.h"
#include "engine/GrainRig.h"
#include "engine/VoiceRig.h"
#include "model/ModelFixture.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"
#include "engine/OctaveLevels.h"
#include "engine/SourceRead.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

#if defined(_WIN32)
    constexpr bool kGateTimes = true;
#else
    constexpr bool kGateTimes = false;
#endif

    /** Energy more than 10 Hz from every multiple of f0 (from 20 Hz up), in dB of the total, over 2^order samples. */
    double offLinesDb (const std::vector<float>& x, double sr, double f0, std::int64_t start, int order)
    {
        const std::vector<double> m = grains::spectrum (x, start, order);
        const double bin = sr / static_cast<double> (std::int64_t { 1 } << order);
        double total = 0, off = 0;
        for (auto i = static_cast<std::size_t> (std::ceil (20 / bin)); i < m.size(); ++i)
        {
            const double hz = static_cast<double> (i) * bin;
            const double k = std::max (1.0, std::floor (hz / f0 + 0.5));
            const double p = m[i] * m[i];
            total += p;
            if (std::abs (hz - k * f0) > 10)
                off += p;
        }
        return 10 * std::log10 (off / total + 1e-30);
    }

    /** a and b hold the same floats, bit for bit (no float compare). */
    bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
    }

    double rmsOf (const std::vector<float>& x)
    {
        double s = 0;
        for (const float v : x)
            s += static_cast<double> (v) * v;
        return x.empty() ? 0.0 : std::sqrt (s / static_cast<double> (x.size()));
    }

    /** The Saw starter as the SampleManager hands it on: its samples and (levels) its octave levels. */
    std::unique_ptr<engine::SampleData> sawSample (const sounds::StarterSound& saw, bool levels)
    {
        auto s = engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw");
        if (levels && s != nullptr)
            engine::addOctaveLevels (*s);
        return s;
    }

    std::string usesText (const engine::Stats& s)
    {
        return fmt ("%lld / %lld / %lld", (long long) s.levelUses[0], (long long) s.levelUses[1], (long long) s.levelUses[2]);
    }
}

SL_TEST ("engine", fastReadsAliasing)
{
    section ("fast reads (9.6): the Saw on Tape at +12, +24 and +36 st at 44.1, 48 and 96 kHz: at most -40 dB of the energy off the "
             "harmonic lines with the octave levels (M3: -27.8 / -19.6 / -15.1 dB at 48 kHz); +0 st bit for bit as with the levels off; "
             "between the octaves too, each level read near 0.63x and near 1.2x of its own rate (fix pass F1)");

    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    struct Point
    {
        int rateHz = 48000;
        int steps = 0;
        bool between = false;   // fix pass F1: a point between the octaves
    };
    std::vector<Point> points;
    for (const int rateHz : { 44100, 48000, 96000 })
        for (const int st : { 0, 12, 24, 36 })
            points.push_back ({ rateHz, st, false });
    // Between the octaves (fix pass F1): each level read just after its change (about 0.63x of its own rate) and just
    // before the next (about 1.2x), where a level's band from 0.4 to 0.5 of its rate imaged or folded at -34 to -40 dB
    // before the fix. At 44.1 kHz: level 1 at 0.630x (+4) and 1.189x (+15), level 2 (+16, +27), level 3 (+28, +39); at
    // 48 kHz: level 1 at 0.650x (+6) and 1.226x (+17), level 2 (+18, +29), level 3 at 0.650x (+30) and 1.093x (+39).
    for (const int st : { 4, 15, 16, 27, 28, 39 })
        points.push_back ({ 44100, st, true });
    for (const int st : { 6, 17, 18, 29, 30, 39 })
        points.push_back ({ 48000, st, true });

    double worst = -1000, worstBetween = -1000;
    double at48[4] = { 0, 0, 0, 0 }, m3At48[4] = { 0, 0, 0, 0 };
    int betweenCount = 0;
    for (const Point& pt : points)
    {
        const double sr = pt.rateHz;
        const auto start = static_cast<std::int64_t> (0.5 * sr);
        const double f0 = signals::midiHz (60 + pt.steps);
        const double rate = saw.sampleRate / sr * std::pow (2.0, pt.steps / 12.0);   // frames of the Saw per output sample
        const int expect = engine::levelFor (rate, engine::kMaxOctaveLevels);
        auto renderNote = [&] (bool levels, engine::Stats& stats)
        {
            grains::Play play (sr, grains::instWith (core::Engine::tape), sawSample (saw, true));
            play.e->setOctaveLevels (levels);
            play.e->noteOn (60 + pt.steps, 1);
            play.render (start + 65536);
            stats = play.e->stats();
            return play.L;
        };
        engine::Stats on, off;
        const std::vector<float> withLevels = renderNote (true, on);
        const std::vector<float> without = renderNote (false, off);
        const double fig = offLinesDb (withLevels, sr, f0, start, 16), m3 = offLinesDb (without, sr, f0, start, 16);
        const double rms = rmsOf (withLevels);
        // the read chose the rule's level for every run (and none at all with the switch off)
        const bool asRule = off.levelReads == 0
                            && (expect == 0 ? on.levelReads == 0
                                            : on.levelReads > 0 && on.levelUses[static_cast<std::size_t> (expect - 1)] == on.levelReads);
        if (pt.steps == 0)
        {
            check (sameBits (withLevels, without) && asRule && rms > 0.01,
                   fmt ("%.1f kHz, +0 st (a read at %.4fx): no level read, bit for bit as with the levels off; %.2f dB off the lines, rms %.3f",
                        sr / 1000, rate, fig, rms));
            continue;
        }
        const double atLevel = rate * engine::kLevelScale[expect];
        check (fig <= -40.0 && asRule && rms > 0.01 && (! pt.between || expect > 0),
               fmt ("%.1f kHz, +%d st%s: %.2f dB off the harmonic lines (at most -40; with the levels off, M3's reads, %.2f dB); a read at %.4fx, "
                    "level %d at %.4fx of its own rate (level reads 1 / 2 / 3: %s); rms %.3f",
                    sr / 1000, pt.steps, pt.between ? " (between the octaves)" : "", fig, m3, rate, expect, atLevel, usesText (on).c_str(), rms));
        if (pt.between)
        {
            worstBetween = std::max (worstBetween, fig);
            ++betweenCount;
        }
        else
        {
            worst = std::max (worst, fig);
        }
        if (! pt.between && pt.rateHz == 48000)
        {
            const auto q = static_cast<std::size_t> (pt.steps / 12);
            at48[q] = fig;
            m3At48[q] = m3;
        }
    }
    check (betweenCount == 12, fmt ("12 points between the octaves measured (%d)", betweenCount));
    note (fmt ("In plain words: at 48 kHz, played one, two and three octaves up, the Tape engine's Saw now has %.4f %%, %.4f %% and %.4f %% of "
               "its sound off the harmonic lines (%.1f, %.1f and %.1f dB), where M3's reads had %.1f %%, %.1f %% and %.1f %% (%.1f, %.1f and %.1f dB); "
               "the worst of the nine cases is %.1f dB, and of the 12 between the octaves %.1f dB.",
               100 * std::pow (10.0, at48[1] / 10), 100 * std::pow (10.0, at48[2] / 10), 100 * std::pow (10.0, at48[3] / 10), at48[1], at48[2], at48[3],
               100 * std::pow (10.0, m3At48[1] / 10), 100 * std::pow (10.0, m3At48[2] / 10), 100 * std::pow (10.0, m3At48[3] / 10), m3At48[1],
               m3At48[2], m3At48[3], worst, worstBetween));

    // The measure itself: the band-limited Saw source reads very low, a naive (aliased) saw high.
    const double source = offLinesDb (saw.left, saw.sampleRate, signals::midiHz (60), 0, 16);
    std::vector<float> naive (1 << 16);
    const double f = signals::midiHz (60);
    for (std::size_t i = 0; i < naive.size(); ++i)
    {
        const double ph = std::fmod (static_cast<double> (i) * f / 44100, 1.0);
        naive[i] = static_cast<float> (1 - 2 * ph);
    }
    const double aliased = offLinesDb (naive, 44100, f, 0, 16);
    check (source < -60 && aliased > -40,
           fmt ("the measure works: the Saw source itself %.1f dB (< -60; the prototype -104.1), a naive saw %.1f dB (> -40)", source, aliased));
}

//==================================================================================================
namespace
{
    /** Fix pass F2 (and T7-3): the renders.json cases that read an octave level, and how often from each (Stats::levelUses:
        Tape and Lock head runs, Voice and Cloud grains), as runs 44980720214 and 44983293729 printed them on Windows, on
        Linux and under the sanitizers. Every other case reads none. Pinned, so a level chosen for a read at or below 1.25x
        (a case that should read none, or more reads in one of these) fails instead of moving out of the bit-for-bit check. */
    struct LevelCase
    {
        const char* name;
        std::int64_t uses[engine::kMaxOctaveLevels];
    };

    constexpr LevelCase kLevelCases[] = {
        { "tape-forward-c5", { 0, 1500, 0 } },
        { "tape-chain-c3-g3-legato", { 1619, 0, 0 } },
        { "chase-inside-chain", { 840, 0, 0 } },
        { "loop-3-passes-133bpm", { 6636, 0, 0 } },
        { "live-bend-range-7", { 564, 0, 0 } },
        { "live-poly4-6-keys", { 4072, 0, 0 } },
        { "voice-formant-up6", { 263, 0, 0 } },
        { "cloud-tape-switch-2x", { 24, 0, 0 } },
        { "tape-head-tune-octaves", { 1500, 1500, 0 } },
        { "strand-demo-2s", { 6, 0, 0 } },
        { "strand-demo-recipe-6", { 51, 0, 0 } },
        { "inst-change-macros", { 2997, 0, 0 } },
        { "raw-snapshot", { 200, 298, 171 } },
    };
}

SL_TEST ("engine", fastReadsParity)
{
    section ("fast reads (9.6): every engine/renders.json case with its sound's octave levels: switched off, bit for bit with no levels at all "
             "(M3's render, which the fixture tests compare with the prototype); switched on, every case that reads at or below 1.25x bit for "
             "bit with it too; which cases read a level, and how often from each, as pinned (fix pass F2)");

    JsonValue doc;
    if (! fixture::load ("engine/renders.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const JsonValue& cases = doc["cases"];
    int rendered = 0, offSame = 0, onSame = 0, fastCases = 0, protoBits = 0, protoBitsM3 = 0, pinnedSeen = 0;
    std::vector<std::string> problems, fastNames, unpinned;
    render::FixtureOptions levelsOff, levelsOn;
    levelsOff.buildLevels = true;
    levelsOff.levelsOn = false;
    levelsOn.buildLevels = true;
    levelsOn.levelsOn = true;
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const JsonValue& k = cases[c];
        const std::string name = k["name"].asString();
        render::Rendered m3, off, on;
        std::string why;
        if (! render::renderFixtureCase (k, clips, m3, why) || ! render::renderFixtureCase (k, clips, off, why, levelsOff)
            || ! render::renderFixtureCase (k, clips, on, why, levelsOn))
        {
            problems.push_back (name + ": " + why);
            continue;
        }
        ++rendered;
        const bool sameOff = sameBits (m3.L, off.L) && sameBits (m3.R, off.R) && m3.stats == off.stats && off.last.levelReads == 0;
        offSame += sameOff ? 1 : 0;
        if (! sameOff)
            problems.push_back (name + ": with the levels switched off it is not M3's render");
        const std::string hash = k["out"]["hash"].asString();
        protoBits += signals::hex8 (rig::hashOf (off.L, off.R)) == hash ? 1 : 0;
        protoBitsM3 += signals::hex8 (rig::hashOf (m3.L, m3.R)) == hash ? 1 : 0;
        // Fix pass F2: the level reads this case must make (none unless it is in the pinned table).
        {
            std::int64_t want[engine::kMaxOctaveLevels] = { 0, 0, 0 };
            for (const auto& pinned : kLevelCases)
                if (name == pinned.name)
                {
                    ++pinnedSeen;
                    for (std::size_t L = 0; L < static_cast<std::size_t> (engine::kMaxOctaveLevels); ++L)
                        want[L] = pinned.uses[L];
                }
            bool same = true;
            for (std::size_t L = 0; L < static_cast<std::size_t> (engine::kMaxOctaveLevels); ++L)
                same = same && on.last.levelUses[L] == want[L];
            if (! same)
                unpinned.push_back (name + " (" + usesText (on.last) + fmt (", pinned %lld / %lld / %lld)", (long long) want[0], (long long) want[1],
                                                                            (long long) want[2]));
        }
        if (on.last.levelReads == 0)
        {
            const bool same = sameBits (m3.L, on.L) && sameBits (m3.R, on.R) && m3.stats == on.stats;
            onSame += same ? 1 : 0;
            if (! same)
                problems.push_back (name + ": it reads no level, but it is not M3's render");
        }
        else
        {
            ++fastCases;
            fastNames.push_back (name + " (" + usesText (on.last) + ")");
            // still the note: as loud as M3's within 20 dB (white noise read 4x keeps a fifth of its band: about -7 dB)
            const double a = rmsOf (on.L), b = rmsOf (m3.L);
            if (! (std::isfinite (a) && a > 0.1 * b))
                problems.push_back (name + fmt (": with the levels it plays at rms %.4f against M3's %.4f", a, b));
        }
    }
    std::string list;
    for (const auto& p : problems)
        list += (list.empty() ? "" : "; ") + p;
    check (rendered == 73 && problems.empty(), fmt ("73 cases rendered three ways (%d)%s%s", rendered, list.empty() ? "" : ": ", list.c_str()));
    check (offSame == rendered && rendered > 0,
           fmt ("levels built and switched off: %d of %d cases bit for bit with no levels at all (samples and stats)", offSame, rendered));
    check (onSame == rendered - fastCases && fastCases > 0 && fastCases < rendered,
           fmt ("levels on: the %d cases that read at or below 1.25x are bit for bit with M3's render (%d); %d cases read a level", rendered - fastCases,
                onSame, fastCases));
    std::string wrongUses;
    for (const auto& u : unpinned)
        wrongUses += (wrongUses.empty() ? "" : ", ") + u;
    const int pinnedCount = static_cast<int> (sizeof (kLevelCases) / sizeof (kLevelCases[0]));
    check (unpinned.empty() && pinnedSeen == pinnedCount && fastCases == pinnedCount,
           fmt ("fix pass F2: the level reads of every case as pinned: the %d pinned cases found (%d), %d cases read a level, the other %d none%s%s",
                pinnedCount, pinnedSeen, fastCases, rendered - fastCases, wrongUses.empty() ? "" : "; NOT as pinned: ", wrongUses.c_str()));
    note (fmt ("bit for bit with the prototype's own hash: %d cases with the levels switched off, %d with no levels (the counts M3 printed: "
               "22 + 26 on Windows, 13 + 11 on Linux)", protoBits, protoBitsM3));
    std::string names;
    for (const auto& n : fastNames)
        names += (names.empty() ? "" : ", ") + n;
    note ("the cases that read a level (level reads 1 / 2 / 3): " + names);
}

//==================================================================================================
SL_TEST ("engine", fastReadsLockLevel)
{
    section ("fast reads (fix pass F2): a Lock note's taps read at the rate ratio (the sound's rate over the session's), whatever the note: "
             "in a 44.1 kHz session a 96 kHz sound reads level 1 (2.18x), a 44.1 kHz sound (1x) and a 48 kHz one (1.09x) read none");

    const double sr = 44100;
    struct Row
    {
        double soundRate;
        int expect;
    };
    for (const Row row : { Row { 96000.0, 1 }, Row { 44100.0, 0 }, Row { 48000.0, 0 } })
        for (const int key : { 36, 60, 84 })
        {
            auto sample = grains::mono (signals::tone (signals::midiHz (45), 1.0, row.soundRate), row.soundRate);
            engine::addOctaveLevels (*sample);
            core::Inst inst = grains::instWith (core::Engine::cloud);
            inst.lock = true;
            grains::Play play (sr, inst, std::move (sample));
            play.e->noteOn (key, 1);
            play.render (static_cast<std::int64_t> (0.5 * sr));
            const engine::Stats st = play.e->stats();
            const bool asRule = row.expect == 0 ? st.levelReads == 0
                                                : st.levelReads > 0 && st.levelUses[static_cast<std::size_t> (row.expect - 1)] == st.levelReads;
            const double rms = rmsOf (play.L);
            check (asRule && rms > 0.01,
                   fmt ("Lock, a %.1f kHz sound, key %d: its taps read at %.4fx, level %d (level reads 1 / 2 / 3: %s); rms %.3f", row.soundRate / 1000,
                        key, row.soundRate / sr, row.expect, usesText (st).c_str(), rms));
        }
}

//==================================================================================================
// The sweeps. A sweep is rendered in 32-sample blocks, one control tick each, its setting moved before each block. The
// level a block read is the highest whose Stats::levelUses grew in it: for Tape the head's level that tick (none grew:
// level 0, the sound itself); for grains the newest grains' (none grew: the level stays, as no new grain started).

namespace
{
    struct Change
    {
        std::int64_t at = 0;   // the first sample of the block that read the new level
        int from = 0, to = 0;
        double steady = 0, around = 0;
    };

    struct Sweep
    {
        double sr = 48000;
        std::vector<float> L;
        std::vector<Change> changes;   // after the first 50 ms (the note's own start)
        engine::Stats last;
    };

    constexpr int kBlock = 32;

    std::int64_t samplesOf (double sr, double millis) { return static_cast<std::int64_t> (std::llround (millis * sr / 1000)); }

    /** The largest step around a change (5 ms before to 15 ms after it) and the steady one in the 30 ms before that. */
    void measureChange (const std::vector<float>& x, double sr, Change& c)
    {
        c.steady = rig::maxStep (x, c.at - samplesOf (sr, 35), c.at - samplesOf (sr, 5));
        c.around = rig::maxStep (x, c.at - samplesOf (sr, 5), c.at + samplesOf (sr, 15));
    }

    Sweep runSweep (double sr, const core::Inst& inst, std::unique_ptr<engine::SampleData> sample, int note, double seconds, bool tape,
                    const std::function<void (engine::Engine&, double)>& move)
    {
        Sweep s;
        s.sr = sr;
        auto e = std::make_unique<engine::Engine>();
        e->prepare (sr);
        e->setRampMs (0);
        e->setInst (inst);
        e->setSample (sample.get(), 1);
        move (*e, 0.0);
        e->noteOn (note, 1);
        const auto blocks = static_cast<std::int64_t> (std::ceil (seconds * sr / kBlock));
        s.L.assign (static_cast<std::size_t> (blocks * kBlock), 0.0f);
        std::vector<float> R (static_cast<std::size_t> (kBlock));
        int level = 0;
        for (std::int64_t b = 0; b < blocks; ++b)
        {
            if (b > 0)
                move (*e, static_cast<double> (b * kBlock) / sr);
            const auto before = e->stats().levelUses;
            e->process (s.L.data() + b * kBlock, R.data(), kBlock);
            const auto after = e->stats().levelUses;
            int now = tape ? 0 : level;
            for (int L = engine::kMaxOctaveLevels; L >= 1; --L)
                if (after[static_cast<std::size_t> (L - 1)] > before[static_cast<std::size_t> (L - 1)])
                {
                    now = L;
                    break;
                }
            if (now != level && b * kBlock >= samplesOf (sr, 50))
            {
                Change c;
                c.at = b * kBlock;
                c.from = level;
                c.to = now;
                s.changes.push_back (c);
            }
            level = now;
        }
        for (auto& c : s.changes)
            measureChange (s.L, sr, c);
        s.last = e->stats();
        return s;
    }

    /** Checks a sweep: the level changes expected, each one's step ratio under 1.5. */
    void checkSweep (const std::string& what, const Sweep& s, std::size_t expected)
    {
        std::string rows;
        bool ok = s.changes.size() == expected;
        for (const auto& c : s.changes)
        {
            const double r = c.steady > 0 ? c.around / c.steady : 1e9;
            ok = ok && r < 1.5 && c.steady > 1e-4;
            rows += fmt ("%slevel %d -> %d at %.3f s: %.3f", rows.empty() ? "" : "; ", c.from, c.to, static_cast<double> (c.at) / s.sr, r);
        }
        check (ok, fmt ("%s: %zu level changes (expected %zu)%s%s", what.c_str(), s.changes.size(), expected, rows.empty() ? "" : "; ", rows.c_str()));
    }
}

SL_TEST ("engine", fastReadsSweeps)
{
    section ("fast reads (9.6): sweeps across the level changes: C3 -> C6 over 2 s on Tape, and Voice notes reading above 1x: the largest step at "
             "each level change / the steady largest step before it < 1.5");

    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    const double rates[] = { 44100, 48000, 96000 };
    const std::size_t tapeChanges[] = { 3, 3, 2 };       // a read from 1x (0.92x, 0.46x) to 8x (7.35x, 3.67x): past 1.25x, 2.5x, 5x
    const std::size_t formantChanges[] = { 1, 1, 0 };    // a Voice read at 1 .. 2x the rate ratio: past 1.25x at 44.1 and 48 kHz
    const std::size_t speedChanges[] = { 2, 2, 2 };      // 2 .. 8x the ratio with the Tape switch (Speed 1 -> 4)
    Sweep tape48;
    for (int ri = 0; ri < 3; ++ri)
    {
        const double sr = rates[ri];
        const double khz = sr / 1000;

        // Tape: one live C3 bent up three octaves over 2 s (Bend range 36), the plan's sweep.
        core::Inst tapeInst = grains::instWith (core::Engine::tape);
        tapeInst.bendRange = 36;
        const Sweep t = runSweep (sr, tapeInst, sawSample (saw, true), 60, 2.2, true,
                                  [] (engine::Engine& e, double secs) { e.setController (engine::Ctrl::bend, std::min (1.0, secs / 2)); });
        checkSweep (fmt ("Tape, C3 -> C6 over 2 s at %.1f kHz", khz), t, tapeChanges[ri]);
        if (ri == 1)
            tape48 = t;

        // Voice: the formant raised from 0 to +12 st over 2 s (the grains read at 1x to 2x the rate ratio).
        const core::Inst voice = grains::instWith (core::Engine::voice);
        const Sweep v = runSweep (sr, voice, sawSample (saw, true), 60, 2.2, false, [voice] (engine::Engine& e, double secs)
        {
            core::Inst now = voice;
            now.offsets.formant = std::min (0.5, secs / 4);
            e.setInst (now);
        });
        checkSweep (fmt ("Voice, the formant 0 -> +12 st over 2 s at %.1f kHz", khz), v, formantChanges[ri]);

        // Voice with the formant at +12 st and the Tape switch, Speed 1 -> 4 over 2 s: the grains read at 2x to 8x the ratio.
        core::Inst fast = grains::instWith (core::Engine::voice);
        fast.tape = true;
        fast.offsets.formant = 0.5;
        const Sweep f = runSweep (sr, fast, sawSample (saw, true), 60, 2.2, false, [fast] (engine::Engine& e, double secs)
        {
            core::Inst now = fast;
            now.speed = 1 + 3 * std::min (1.0, secs / 2);
            e.setInst (now);
        });
        checkSweep (fmt ("Voice, the formant at +12 st, the Tape switch, Speed 1 -> 4 over 2 s at %.1f kHz", khz), f, speedChanges[ri]);

        // The plan's Voice sweep: C3 -> C6 over 2 s with the formant at +12 st. A Voice note's grains read at the formant's
        // speed (the pitch sets their spacing), so the level is the formant's all along: level 1 at 44.1 and 48 kHz, none at
        // 96 kHz, and no change.
        core::Inst plain = grains::instWith (core::Engine::voice);
        plain.offsets.formant = 0.5;
        plain.bendRange = 36;
        const Sweep p = runSweep (sr, plain, sawSample (saw, true), 60, 2.2, false,
                                  [] (engine::Engine& e, double secs) { e.setController (engine::Ctrl::bend, std::min (1.0, secs / 2)); });
        const bool formantLevel = ri < 2 ? p.last.levelUses[0] > 0 && p.last.levelUses[1] == 0 && p.last.levelUses[2] == 0 : p.last.levelReads == 0;
        check (p.changes.empty() && formantLevel,
               fmt ("Voice, C3 -> C6 over 2 s with the formant at +12 st at %.1f kHz: the grains read the formant's level all along (level reads "
                    "1 / 2 / 3: %s), no change",
                    khz, usesText (p.last).c_str()));
    }

    // A no-op is not a pass: one sample nudged by twice the steady step inside a change window reads above 1.5.
    if (! tape48.changes.empty())
    {
        Sweep clicked = tape48;
        Change& c = clicked.changes.front();
        const auto at = static_cast<std::size_t> (c.at + samplesOf (48000, 2));
        clicked.L[at] = static_cast<float> (clicked.L[at] + 2.0 * c.steady);
        measureChange (clicked.L, 48000, c);
        check (c.around / c.steady > 1.5, fmt ("negative control: a click of twice the steady step at the first change reads %.2f (> 1.5)", c.around / c.steady));
    }
    else
    {
        check (false, "negative control: the Tape sweep at 48 kHz changed level");
    }
}

//==================================================================================================
SL_TEST ("engine", fastReadsLevelsMatchTheSound)
{
    section ("octave levels: 3 levels of ceil (n / 2^L) frames in the sound's own format, 7/8 of its bytes; level L read at the sound's frame x "
             "is the sound at x (a 500 Hz and a 700 Hz tone within 2e-3 at 1000 places, one level frame off far from it); flat to 0.15 of the "
             "rate, 60 dB down from 0.2 (0.4 of the level's own rate, fix pass F1); a short sound gets fewer; a stop ends the build with none");

    const double sr = 48000;
    const auto n = static_cast<std::int64_t> (2 * sr) + 7;   // an odd length: each level keeps the last frame
    std::vector<float> left (static_cast<std::size_t> (n)), right (static_cast<std::size_t> (n));
    for (std::int64_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        left[static_cast<std::size_t> (i)] = static_cast<float> (0.5 * std::sin (2 * 3.141592653589793 * 500 * t));
        right[static_cast<std::size_t> (i)] = static_cast<float> (0.4 * std::sin (2 * 3.141592653589793 * 700 * t + 0.3));
    }
    std::vector<std::int16_t> left16 (left.size()), right16 (right.size());
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left16[i] = engine::toInt16 (left[i]);
        right16[i] = engine::toInt16 (right[i]);
    }
    auto f32 = engine::makeSampleData (left, right, sr, "tones");
    auto i16 = engine::makeSampleData16 (left16, right16, sr, "tones 16");
    const bool built = engine::addOctaveLevels (*f32) && engine::addOctaveLevels (*i16);

    // frames, formats, bytes
    bool shape = built && f32->numLevels == 3 && i16->numLevels == 3;
    std::int64_t expectFrames = n;
    for (int L = 1; L <= 3 && shape; ++L)
    {
        expectFrames = (expectFrames + 1) / 2;
        const auto& a = f32->levels[static_cast<std::size_t> (L - 1)];
        const auto& b = i16->levels[static_cast<std::size_t> (L - 1)];
        shape = a.n == expectFrames && b.n == expectFrames && a.L != nullptr && a.R != nullptr && a.R != a.L && a.L16 == nullptr
                && b.L16 != nullptr && b.R16 != nullptr && b.L == nullptr;
    }
    const auto eighths = [] (const engine::SampleData& s) { return 8 * s.levelBytes() <= 7 * s.sampleBytes() + 64; };
    check (shape && eighths (*f32) && eighths (*i16) && f32->levelStorage != nullptr,
           fmt ("float and 16-bit stereo sounds of %lld frames: 3 levels of %lld, %lld and %lld frames in their own format; the levels take %lld of "
                "%lld bytes (float) and %lld of %lld (16-bit)",
                (long long) n, (long long) ((n + 1) / 2), (long long) ((n + 3) / 4), (long long) ((n + 7) / 8), (long long) f32->levelBytes(),
                (long long) f32->sampleBytes(), (long long) i16->levelBytes(), (long long) i16->sampleBytes()));

    // The frame each level stands for: level L read at the sound's frame x is the sound at x (tones well inside every band),
    // and one level-3 frame off (8 frames of the sound) is far from it (the control).
    std::uint32_t seed = 12345;
    auto next = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<double> (seed >> 8) / 16777216.0;
    };
    double worst[3] = { 0, 0, 0 }, worst16[3] = { 0, 0, 0 }, offFrame = 0;
    for (int k = 0; k < 1000; ++k)
    {
        const double x = 200 + next() * static_cast<double> (n - 400);
        double wantL = 0, wantR = 0;
        engine::readAbs (*f32, x, wantL, wantR);
        for (int L = 1; L <= 3; ++L)
        {
            double a = 0, b = 0;
            engine::readLevel (*f32, L, x, a, b);
            worst[L - 1] = std::max (worst[L - 1], std::max (std::abs (a - wantL), std::abs (b - wantR)));
            engine::readLevel (*i16, L, x, a, b);
            worst16[L - 1] = std::max (worst16[L - 1], std::max (std::abs (a - wantL), std::abs (b - wantR)));
        }
        double a = 0, b = 0;
        engine::readLevel (*f32, 3, x + 8, a, b);
        offFrame = std::max (offFrame, std::max (std::abs (a - wantL), std::abs (b - wantR)));
    }
    const bool aligned = worst[0] < 2e-3 && worst[1] < 2e-3 && worst[2] < 2e-3 && worst16[0] < 2e-3 && worst16[1] < 2e-3 && worst16[2] < 2e-3;
    check (aligned && offFrame > 0.1,
           fmt ("each level read at the sound's frame x gives the sound at x: within %.2e / %.2e / %.2e (float) and %.2e / %.2e / %.2e (16-bit), "
                "under 2e-3; a level-3 read one level frame off is up to %.3f away (the control, > 0.1)",
                worst[0], worst[1], worst[2], worst16[0], worst16[1], worst16[2], offFrame));

    // The filter's bands, on level 1: a tone at 0.15 of the rate keeps its level, one at 0.21 is 60 dB down (fix pass F1:
    // 0.42 of the level's own rate, which a read at 1.25x would fold; the filter before the fix passed it at -0.6 dB).
    auto levelOneGain = [] (double fraction)
    {
        std::vector<float> x (48000);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.5 * std::sin (2 * 3.141592653589793 * fraction * static_cast<double> (i)));
        auto s = engine::makeSampleData (x, {}, 48000.0, "band");
        engine::addOctaveLevels (*s);
        const auto& q = s->levels[0];
        double a = 0;
        std::int64_t count = 0;
        for (std::int64_t j = 100; j < q.n - 100; ++j, ++count)
            a += static_cast<double> (q.L[j]) * q.L[j];
        return std::sqrt (a / static_cast<double> (count)) / (0.5 / std::sqrt (2.0));
    };
    const double pass = levelOneGain (0.15), stop = levelOneGain (0.21);
    check (std::abs (20 * std::log10 (pass)) < 0.05 && 20 * std::log10 (stop) < -55,
           fmt ("level 1 keeps a tone at 0.15 of the rate (%.3f dB) and takes out one at 0.21 (%.1f dB, under -55)", 20 * std::log10 (pass),
                20 * std::log10 (stop)));

    // A short sound gets fewer levels (each keeps at least 8 frames); one of 15 frames none.
    auto shortOne = engine::makeSampleData (std::vector<float> (20, 0.25f), {}, sr, "short");
    auto tiny = engine::makeSampleData (std::vector<float> (15, 0.25f), {}, sr, "tiny");
    engine::addOctaveLevels (*shortOne);
    engine::addOctaveLevels (*tiny);
    check (shortOne->numLevels == 1 && shortOne->levels[0].n == 10 && tiny->numLevels == 0 && tiny->levelStorage == nullptr,
           fmt ("20 frames: %d level of %lld frames; 15 frames: %d levels", shortOne->numLevels, (long long) shortOne->levels[0].n, tiny->numLevels));

    // The stop question: asked between blocks; true ends the build with no levels.
    auto stopped = engine::makeSampleData (left, right, sr, "stopped");
    int asked = 0;
    const bool finished = engine::addOctaveLevels (*stopped, [&asked] { return ++asked >= 3; });
    check (! finished && asked == 3 && stopped->numLevels == 0 && stopped->levelStorage == nullptr,
           fmt ("a stop that fires on its third question: the build ends with no levels (asked %d times)", asked));
}

//==================================================================================================
SL_TEST ("cpu", fastReadsTapeCpu96k)
{
    section ("CPU, fast reads (9.6): 16 voices x 8 heads Tape at 96 kHz, every head above 2x, 64-sample blocks: with the octave levels against "
             "without (M3's reads), printed (target at most 15 % slower), gated faster than real time on Windows");

    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    inst.heads = 8;
    inst.spread = 0.5;
    inst.drift = 0.3;
    inst.polyphony = 16;
    const double sr = 96000, audioSeconds = 6;
    const auto n = static_cast<std::int64_t> (audioSeconds * sr);
    // C3 + 28 .. 43 st: the Saw (44.1 kHz) read at 2.3x to 5.5x at 96 kHz, above 2x even with Drift's 7.5 % either way
    const int firstKey = 88;
    struct Run
    {
        double best = 1e300;
        int voices = 0;
        std::int64_t levelReads = 0;
    };
    auto bench = [&] (bool levels)
    {
        Run r;
        auto sample = sawSample (saw, true);
        std::vector<float> L (64), R (64);
        for (int pass = 0; pass < 2; ++pass)
        {
            auto e = std::make_unique<engine::Engine>();
            e->prepare (sr);
            e->setOctaveLevels (levels);
            e->setInst (inst);
            e->setSample (sample.get(), 1);
            for (int i = 0; i < 16; ++i)
                e->noteOn (firstKey + i, 0.8);
            const auto t0 = std::chrono::steady_clock::now();
            for (std::int64_t k = 0; k < n; k += 64)
                e->process (L.data(), R.data(), 64);
            r.best = std::min (r.best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
            r.voices = e->stats().voices;
            r.levelReads = e->stats().levelReads;
        }
        return r;
    };
    const Run off = bench (false), on = bench (true);
    const double xOn = audioSeconds / on.best, xOff = audioSeconds / off.best;
    const double slower = (on.best / off.best - 1) * 100;
    note (fmt ("with the levels %.2f x real time, without (M3's reads) %.2f x: %.1f %% %s (target at most 15 %% slower)", xOn, xOff, std::abs (slower),
               slower >= 0 ? "slower" : "faster"));
    check (on.voices == 16 && off.voices == 16 && on.levelReads > 0 && off.levelReads == 0,
           fmt ("16 voices sound both ways (%d, %d); with the levels the heads read them (%lld level reads), without none", on.voices, off.voices,
                (long long) on.levelReads));
    if (kGateTimes)
        check (xOn > 1.0, fmt ("faster than real time with the levels (%.2f x)", xOn));
    else
        note ("not gated on this platform (the plan gates CPU on Windows)");
}
