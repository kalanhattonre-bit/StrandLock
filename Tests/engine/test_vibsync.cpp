// Vibrato Sync (PLUGIN-PLAN 4 M8, 7.10 decision 8, 8.2 #12): parameter 42, vibSync, is Free or one of the 21 rates.
// Free plays the Vibrato speed knob (vibRate, Hz), as the prototype and the M3 build do. A rate plays
// (bpm / 60) / rateBeats Hz at the host's tempo, or the Preview clock's tempo while Preview plays. Rates are quarter-note
// beats, so a synced rate keeps its sound when the clip's beatsPerBar changes ("1 bar" is 4 beats in 3/4 too).
//
// Rows:
// - synced 1/8 at 120 bpm: the vibrato period is 0.25 s within 1 % (and 1/3 s at 90 bpm); a Free vibrato at 5.5 Hz
//   measures 0.1818 s with the same measurement, so a sync that did nothing would fail;
// - Free is bit for bit with M3: the render's hash equals the one the M7 build (whose vibrato is M3's) printed, the same
//   on Windows and Linux; and Free at 4 Hz is bit for bit synced 1/8 at 120 bpm (the same phase steps);
// - a beatsPerBar change keeps a synced rate's period: "1 bar" at 120 bpm is 2 s in 4/4 and in 3/4, bit for bit;
// - the host: a clip note while the host plays, a live key while it is stopped, and the Preview clock, all at 90 bpm
//   through HostSync and MidiRouter as processBlock drives them; and the parameter reaches the engine (choice 0 is Free,
//   choice n is rate n - 1).
//
// The period is measured on the output: a 440 Hz sine on Tape at its root, so the read speed is the pitch. Upward zero
// crossings give the frequency cycle by cycle; the vibrato period is the mean time between upward crossings of that
// frequency through its mean. Group "engine".

#include "engine/EngineRender.h"
#include "engine/GrainRig.h"
#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "PluginProcessor.h"
#include "core/Inst.h"
#include "core/Rates.h"
#include "diag/DiagModel.h"
#include "engine/Engine.h"
#include "host/HostSync.h"
#include "host/MidiRouter.h"
#include "model/Clip.h"
#include "model/SnapshotCompiler.h"
#include "params/ParamBridge.h"
#include "params/ParameterLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000.0;
    constexpr double kPi = 3.141592653589793;

    // The hash of the Free render (freeScenario) that the M7 build printed before any M8 change (main 8fdd6e4 plus this
    // test alone, run 45489545875): the engine's vibrato there is M3's (M7 added octave levels, which this render never
    // reads). Windows, Linux and the Linux sanitizer build all printed 578be5fd.
    constexpr const char* kM3FreeHash = "578be5fd";

    std::vector<float> sine (double hz, double secs)
    {
        const auto n = static_cast<std::size_t> (std::lround (secs * kSr));
        std::vector<float> x (n);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = static_cast<float> (0.5 * std::sin (2.0 * kPi * hz * static_cast<double> (i) / kSr));
        return x;
    }

    /** Tape at the root (C3), the vibrato's depth from the Vibrato offset (0.5 semitone on every note). */
    core::Inst vibInst (std::int8_t sync, double vibRate = 5.5)
    {
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        inst.offsets.vib = 0.5;
        inst.vibRate = vibRate;
        inst.vibSync = sync;
        return inst;
    }

    std::int8_t rate (core::RateId id) { return static_cast<std::int8_t> (id); }

    struct Period
    {
        double seconds = 0;
        int cycles = 0;
    };

    /** The vibrato's period on x[from, to): see the header. */
    Period vibratoPeriod (const std::vector<float>& x, std::int64_t from, std::int64_t to)
    {
        std::vector<double> up;   // upward zero crossings, in samples
        const auto n = static_cast<std::int64_t> (x.size());
        for (std::int64_t i = std::max<std::int64_t> (1, from); i < std::min (n, to); ++i)
        {
            const double a = x[static_cast<std::size_t> (i - 1)], b = x[static_cast<std::size_t> (i)];
            if (a < 0 && b >= 0)
                up.push_back (static_cast<double> (i - 1) + a / (a - b));
        }
        if (up.size() < 3)
            return {};
        std::vector<double> at, f;   // each cycle's middle and its frequency
        double mean = 0;
        for (std::size_t k = 1; k < up.size(); ++k)
        {
            at.push_back (0.5 * (up[k] + up[k - 1]));
            f.push_back (kSr / (up[k] - up[k - 1]));
            mean += f.back();
        }
        mean /= static_cast<double> (f.size());
        std::vector<double> cross;   // the frequency rising through its mean
        for (std::size_t k = 1; k < f.size(); ++k)
        {
            const double a = f[k - 1] - mean, b = f[k] - mean;
            if (a < 0 && b >= 0)
                cross.push_back (at[k - 1] + (at[k] - at[k - 1]) * (-a) / (b - a));
        }
        if (cross.size() < 2)
            return {};
        Period p;
        p.cycles = static_cast<int> (cross.size()) - 1;
        p.seconds = (cross.back() - cross.front()) / static_cast<double> (p.cycles) / kSr;
        return p;
    }

    struct Output
    {
        std::vector<float> L, R;
    };

    /** A live key at the root for secs, the host stopped at bpm (the engine's free clock), 256-sample blocks. */
    Output liveKey (const core::Inst& inst, double bpm, double secs)
    {
        auto e = std::make_unique<engine::Engine>();
        e->prepare (kSr);
        e->setRampMs (0);
        e->setInst (inst);
        auto sample = render::monoSample (sine (440.0, 12.0), kSr);
        e->setSample (sample.get(), 1);
        render::Script script;
        script.at (0, [bpm] (engine::Engine& en) {
            engine::Transport t;
            t.playing = false;
            t.bpm = bpm;
            en.setTransport (t);
            en.noteOn (60, 1.0);
        });
        Output out;
        script.run (*e, static_cast<std::int64_t> (secs * kSr), render::fixedBlocks (256), out.L, out.R);
        return out;
    }

    /** One clip note at the root (s 0, l beats) in a clip of `bars` bars of beatsPerBar beats, played from beat 0 at
        bpm (loop on, tieAcrossLoop off: the parity settings, but for beatsPerBar). */
    Output clipNote (const core::Inst& inst, int bars, int beatsPerBar, double l, double bpm, double secs)
    {
        model::Clip clip = model::newClip (bars);
        clip.beatsPerBar = beatsPerBar;
        clip.tieAcrossLoop = false;
        clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, l, "vib")));
        auto snap = model::compileSnapshot (clip, 64);

        auto e = std::make_unique<engine::Engine>();
        e->prepare (kSr);
        e->setRampMs (0);
        e->setInst (inst);
        auto sample = render::monoSample (sine (440.0, 30.0), kSr);
        e->setSample (sample.get(), 1);
        e->setSnapshot (snap.get(), 1);
        render::Script script;
        script.at (0, [bpm] (engine::Engine& en) {
            engine::Transport t;
            t.playing = true;
            t.beat = 0;
            t.bpm = bpm;
            en.setTransport (t);
        });
        Output out;
        script.run (*e, static_cast<std::int64_t> (secs * kSr), render::fixedBlocks (256), out.L, out.R);
        return out;
    }

    bool nearRel (double got, double want, double rel) { return std::abs (got / want - 1.0) <= rel; }

    std::string hashText (const Output& r) { return signals::hex8 (rig::hashOf (r.L, r.R)); }

    /** The Free render the M3 hash belongs to: Tape, a live C3 key, 0.5 st of vibrato at 5.5 Hz, 1.5 s. */
    Output freeScenario() { return liveKey (vibInst (core::kVibSyncFree), 120.0, 1.5); }

    //==============================================================================================
    // A host for the HostSync rows: its play head, HostSync with Preview's switch, MidiRouter and the Engine, as
    // processBlock drives them (test_preview's rig, cut down).
    struct HostRig
    {
        harness::FakePlayHead head;
        host::HostSync sync;
        std::unique_ptr<engine::Engine> e = std::make_unique<engine::Engine>();
        std::unique_ptr<engine::SampleData> sample;
        std::unique_ptr<core::Snapshot> snap;
        juce::MidiBuffer midi;

        HostRig (const core::Inst& inst, bool withClip)
        {
            e->prepare (kSr);
            e->setRampMs (0);
            e->setInst (inst);
            sample = render::monoSample (sine (440.0, 30.0), kSr);
            e->setSample (sample.get(), 1);
            if (withClip)
            {
                model::Clip clip = model::newClip (8);
                clip.tieAcrossLoop = false;
                clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, 30, "held")));
                snap = model::compileSnapshot (clip, 64);
                e->setSnapshot (snap.get(), 1);
            }
            sync.prepare (kSr);
            midi.ensureSize (64);
        }

        /** secs of song in 256-sample blocks: the host playing from ppq 0 or stopped there, Preview's switch, and a key
            at the first sample when asked. */
        Output run (bool playing, bool preview, bool key, double bpm, double secs)
        {
            Output out;
            const auto n = static_cast<std::int64_t> (secs * kSr);
            out.L.assign (static_cast<std::size_t> (n), 0.0f);
            out.R.assign (static_cast<std::size_t> (n), 0.0f);
            const double clipBeats = snap != nullptr ? (double) snap->bars * (double) snap->beatsPerBar : 0.0;
            for (std::int64_t k = 0; k < n; k += 256)
            {
                const int len = static_cast<int> (std::min<std::int64_t> (256, n - k));
                head.info.setIsPlaying (playing);
                head.info.setBpm (bpm);
                head.info.setPpqPosition (playing ? (double) k / kSr * bpm / 60.0 : 0.0);
                head.info.setIsLooping (false);
                head.info.setLoopPoints (juce::nullopt);
                sync.setPreview (preview);
                if (key && k == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
                const auto hb = diag::readHostBlock (&head, len, false);
                const auto t = sync.plan (hb, len, 0.0, true, clipBeats);
                host::MidiRouter::renderBlock (*e, t, midi, host::MidiSettings {}, out.L.data() + k, out.R.data() + k, len);
                midi.clear();
            }
            return out;
        }
    };
}

SL_TEST ("engine", vibSyncEighthAt120)
{
    section ("Vibrato Sync: synced 1/8 at 120 bpm has a 0.25 s period (within 1 %), 1/3 s at 90 bpm; Free plays the knob's Hz");

    const auto periodOf = [] (const core::Inst& inst, double bpm, const char* what, double want) {
        const Output r = liveKey (inst, bpm, 3.0);
        const Period p = vibratoPeriod (r.L, static_cast<std::int64_t> (0.1 * kSr), static_cast<std::int64_t> (2.9 * kSr));
        note (fmt ("%s: period %.6f s over %d cycles (expected %.6f s, %+.4f %%)", what, p.seconds, p.cycles, want,
                   (p.seconds / want - 1.0) * 100.0));
        return p;
    };

    const Period at120 = periodOf (vibInst (rate (core::RateId::r1_8)), 120.0, "1/8 at 120 bpm", 0.25);
    check (at120.cycles >= 8 && nearRel (at120.seconds, 0.25, 0.01),
           fmt ("synced 1/8 at 120 bpm: the vibrato period is %.5f s, 0.25 s within 1 %% (%d cycles measured)", at120.seconds, at120.cycles));

    const Period at90 = periodOf (vibInst (rate (core::RateId::r1_8)), 90.0, "1/8 at 90 bpm", 1.0 / 3.0);
    check (at90.cycles >= 6 && nearRel (at90.seconds, 1.0 / 3.0, 0.01),
           fmt ("the tempo sets it: 1/8 at 90 bpm is %.5f s, 1/3 s within 1 %%", at90.seconds));

    const Period dotted = periodOf (vibInst (rate (core::RateId::r1_16D)), 120.0, "1/16 dotted at 120 bpm", 0.1875);
    check (dotted.cycles >= 10 && nearRel (dotted.seconds, 0.1875, 0.01),
           fmt ("a dotted rate: 1/16 dotted at 120 bpm is %.5f s, 0.1875 s within 1 %%", dotted.seconds));

    // A no-op is not a pass: Free plays the knob (5.5 Hz) whatever the tempo, and the same measurement tells it apart.
    const Period freeRun = periodOf (vibInst (core::kVibSyncFree, 5.5), 120.0, "Free at 5.5 Hz", 1.0 / 5.5);
    check (freeRun.cycles >= 10 && nearRel (freeRun.seconds, 1.0 / 5.5, 0.01) && ! nearRel (freeRun.seconds, 0.25, 0.01),
           fmt ("negative control: Free at 5.5 Hz measures %.5f s, not 0.25 s, so a sync that did nothing would fail above", freeRun.seconds));
}

SL_TEST ("engine", vibSyncFreeBitForBit)
{
    section ("Vibrato Sync: Free is bit for bit with M3 (the M7 build's hash), and Free at 4 Hz is synced 1/8 at 120 bpm");

    const Output freeRender = freeScenario();
    const std::string hash = hashText (freeRender);
    double rms = 0;
    for (const float v : freeRender.L)
        rms += static_cast<double> (v) * v;
    rms = std::sqrt (rms / static_cast<double> (std::max<std::size_t> (1, freeRender.L.size())));
    const std::string recorded = kM3FreeHash;
    note (fmt ("Free render (Tape, live C3, 0.5 st at 5.5 Hz, 1.5 s): hash %s, rms %.6f; the M7 build's %s", hash.c_str(), rms, recorded.c_str()));
    check (rms > 0.1, fmt ("the Free render sounds (rms %.4f)", rms));
    check (hash == recorded, fmt ("Free is bit for bit with M3: hash %s, the M7 build's %s", hash.c_str(), recorded.c_str()));
    // A no-op is not a pass: a vibrato a tenth of a hertz faster is another hash.
    const std::string nudged = hashText (liveKey (vibInst (core::kVibSyncFree, 5.6), 120.0, 1.5));
    check (nudged != recorded, fmt ("negative control: Free at 5.6 Hz hashes %s, not the M3 render's", nudged.c_str()));

    // The same phase steps: (120 / 60) / 0.5 = 4 Hz exactly, so synced 1/8 at 120 bpm renders Free at 4 Hz bit for bit.
    const Output free4 = liveKey (vibInst (core::kVibSyncFree, 4.0), 120.0, 1.5);
    const Output sync8 = liveKey (vibInst (rate (core::RateId::r1_8)), 120.0, 1.5);
    const double diff = std::max (grains::largestDifference (free4.L, sync8.L), grains::largestDifference (free4.R, sync8.R));
    check (free4.L.size() == sync8.L.size() && juce::exactlyEqual (diff, 0.0),
           fmt ("Free at 4.0 Hz and synced 1/8 at 120 bpm render bit for bit (largest difference %g)", diff));
    // ... and a different tempo is a different vibrato (the comparison can fail)
    const Output sync8at90 = liveKey (vibInst (rate (core::RateId::r1_8)), 90.0, 1.5);
    const double moved = std::max (grains::largestDifference (free4.L, sync8at90.L), grains::largestDifference (free4.R, sync8at90.R));
    check (moved > 1e-3, fmt ("negative control: synced 1/8 at 90 bpm differs from Free at 4 Hz (largest difference %.4f)", moved));
}

SL_TEST ("engine", vibSyncKeepsAcrossBeatsPerBar)
{
    section ("Vibrato Sync: a beatsPerBar change keeps a synced rate's period (\"1 bar\" is 4 beats in 3/4 too)");

    // An 8-bar clip with one 22-beat note: it ends inside the clip in 4/4 (32 beats) and in 3/4 (24 beats).
    const core::Inst inst = vibInst (rate (core::RateId::r1bar));
    const Output four = clipNote (inst, 8, 4, 22, 120.0, 11.2);
    const Output three = clipNote (inst, 8, 3, 22, 120.0, 11.2);
    const auto a = static_cast<std::int64_t> (0.3 * kSr), b = static_cast<std::int64_t> (10.6 * kSr);
    const Period p4 = vibratoPeriod (four.L, a, b), p3 = vibratoPeriod (three.L, a, b);
    note (fmt ("\"1 bar\" at 120 bpm: %.6f s in 4/4, %.6f s in 3/4 (%d and %d cycles); a bar of 3 beats would be 1.5 s", p4.seconds, p3.seconds,
               p4.cycles, p3.cycles));
    check (p4.cycles >= 4 && nearRel (p4.seconds, 2.0, 0.01), fmt ("4/4: \"1 bar\" at 120 bpm is %.5f s, 2 s within 1 %%", p4.seconds));
    check (p3.cycles >= 4 && nearRel (p3.seconds, 2.0, 0.01) && ! nearRel (p3.seconds, 1.5, 0.01),
           fmt ("3/4: \"1 bar\" is still 4 beats, %.5f s (2 s within 1 %%, not 1.5 s)", p3.seconds));
    const double diff = std::max (grains::largestDifference (four.L, three.L), grains::largestDifference (four.R, three.R));
    check (juce::exactlyEqual (diff, 0.0), fmt ("the note renders bit for bit in 4/4 and 3/4 (largest difference %g)", diff));
}

SL_TEST ("engine", vibSyncFollowsHostAndPreview)
{
    section ("Vibrato Sync through HostSync and MidiRouter: the host playing, stopped, and the Preview clock at 90 bpm (1/8 = 1/3 s)");

    const core::Inst inst = vibInst (rate (core::RateId::r1_8));
    const double want = 1.0 / 3.0;
    const auto a = static_cast<std::int64_t> (0.1 * kSr), b = static_cast<std::int64_t> (2.9 * kSr);

    {
        HostRig rig (inst, true);
        const Period p = vibratoPeriod (rig.run (true, false, false, 90.0, 3.0).L, a, b);
        note (fmt ("host playing at 90 bpm, a clip note: %.6f s over %d cycles", p.seconds, p.cycles));
        check (p.cycles >= 6 && nearRel (p.seconds, want, 0.01), fmt ("the host playing at 90 bpm: a clip note's 1/8 vibrato is %.5f s (1/3 s within 1 %%)", p.seconds));
    }
    {
        HostRig rig (inst, false);
        const Period p = vibratoPeriod (rig.run (false, false, true, 90.0, 3.0).L, a, b);
        note (fmt ("host stopped at 90 bpm, a live key: %.6f s over %d cycles", p.seconds, p.cycles));
        check (p.cycles >= 6 && nearRel (p.seconds, want, 0.01), fmt ("the host stopped at 90 bpm: a live key's 1/8 vibrato is %.5f s (1/3 s within 1 %%)", p.seconds));
    }
    {
        HostRig rig (inst, true);
        const Output r = rig.run (false, true, false, 90.0, 3.0);
        const Period p = vibratoPeriod (r.L, a, b);
        note (fmt ("Preview at 90 bpm, a clip note: %.6f s over %d cycles; Preview running: %s", p.seconds, p.cycles, rig.sync.isPreviewing() ? "yes" : "no"));
        check (rig.sync.isPreviewing() && p.cycles >= 6 && nearRel (p.seconds, want, 0.01),
               fmt ("the Preview clock at 90 bpm: the clip note's 1/8 vibrato is %.5f s (1/3 s within 1 %%)", p.seconds));
        // A no-op is not a pass: without Preview the stopped host plays no clip note at all.
        HostRig quiet (inst, true);
        const Output q = quiet.run (false, false, false, 90.0, 0.5);
        const bool silent = std::all_of (q.L.begin(), q.L.end(), [] (float v) { return juce::exactlyEqual (v, 0.0f); });
        check (silent, "negative control: with the host stopped and Preview off, the clip note does not play");
    }

    // The parameter: choice 0 is Free, choice n is rate n - 1 (1/8 is choice 10, 1/64 dotted choice 21).
    StrandLockProcessor proc;
    auto pick = [&proc] (float plain) {
        params::setPlainValue (proc.getParam (params::P::vibSync), params::index (params::P::vibSync), plain);
        return proc.readInst().vibSync;
    };
    const std::int8_t freeChoice = pick (0.0f), eighth = pick (10.0f), last = pick (21.0f), bar = pick (1.0f);
    check (freeChoice == core::kVibSyncFree && eighth == rate (core::RateId::r1_8) && last == rate (core::RateId::r1_64D) && bar == rate (core::RateId::r1bar),
           fmt ("the vibSync parameter reaches the engine: choice 0 Free (%d), 1 \"1 bar\" (%d), 10 \"1/8\" (%d), 21 \"1/64 dotted\" (%d)", freeChoice, bar, eighth, last));
}
