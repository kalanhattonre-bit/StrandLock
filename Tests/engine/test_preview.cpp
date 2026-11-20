// Preview (PLUGIN-PLAN 4 M6 "Preview", 1.10, 7.8 decision 3; U5): with the host stopped and Kalan's Preview switch on,
// HostSync runs its own clock at the host tempo from the Preview start (the host's position less the clip's offset),
// and MidiRouter plays the block on the real Engine, exactly as processBlock does. Rows:
//
// - note starts: with the host stopped, every note starts within [0, 1) sample of the Preview clock's beat (120, 87 and
//   133 bpm; from ppq 0, from ppq 2.5, from ppq 6.5 with a 4-beat offset on a 2-bar clip, from ppq 2.5 before a 4-beat
//   offset (the pre-roll), with no position at all; up to 3 loop passes);
// - the Preview plays what Cubase would: a Preview from ppq 1.3 renders bit for bit what the host playing from 1.3 does;
// - the clock follows the host: a tempo change carries on from the beat it reached, moving the host's position starts
//   it again there, and a clip that does not loop ends it at the clip's end;
// - Preview never runs while the host plays: the render is the same as without it, the switch goes off, and it stays
//   off after the host stops; Cubase's Play after a Preview plays on Cubase's beat from that block, wherever Preview's
//   clock was, and so does a cursor move while Preview plays (fix pass F1);
// - the processor: Kalan's switch, Cubase's Play turning it off (and counted), a click after that standing, the display
//   frames saying "playing" while it runs; the switch word keeping a click made after a block read it (fix pass F4);
//   and no allocation on the audio thread with Preview on, tempo changes reaching a running Preview and a playing
//   host (group "alloc", so the Debug-CRT run checks it too).
//
// Note starts are found as in test_hostsync: short notes on a constant (DC) source sound from their first sample and
// are silent between notes. Group "engine" (the sanitizer job runs it).

#include "engine/EngineRender.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "PluginProcessor.h"
#include "core/Inst.h"
#include "diag/DiagModel.h"
#include "engine/Engine.h"
#include "host/HostSync.h"
#include "host/MidiRouter.h"
#include "model/Clip.h"
#include "model/Presets.h"
#include "model/SnapshotCompiler.h"
#include "params/ParameterLayout.h"
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

    struct NoteSpec
    {
        double p, s, l;
    };

    model::Clip clipOf (int bars, const std::vector<NoteSpec>& notes, bool loop = true, double offsetBeats = 0.0)
    {
        model::Clip c = model::newClip (bars);
        c.loop = loop;
        c.offsetBeats = offsetBeats;
        int i = 0;
        for (const NoteSpec& n : notes)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (n.p, n.s, n.l, "pv" + std::to_string (i++))));
        return c;
    }

    /** The clip as the engine plays it (parity settings except loop and the offset, which HostSync reads). */
    std::unique_ptr<core::Snapshot> compileClip (model::Clip c)
    {
        c.beatsPerBar = 4;
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

    /** What the fake host says at a block start. */
    struct Song
    {
        bool present = true;   // false: the play head gives no position at all
        bool playing = false;
        double ppq = 0, bpm = 120;
    };

    /** A host: its play head, HostSync with Preview's switch, MidiRouter and a real Engine, as in processBlock. The
        switch is set before every block and turned off when HostSync ends a Preview itself, as the processor does. */
    struct Rig
    {
        harness::FakePlayHead head;
        host::HostSync sync;
        std::unique_ptr<engine::Engine> e = std::make_unique<engine::Engine>();
        std::unique_ptr<engine::SampleData> sample;
        std::unique_ptr<core::Snapshot> snap;
        juce::MidiBuffer midi;
        bool previewSwitch = false;
        int previewBlocks = 0, ended = 0, blocks = 0;
        std::vector<host::BlockTransport> transports;   // every block's plan, when keep is set
        bool keep = false;

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

        double clipBeats() const
        {
            return snap != nullptr ? (double) std::max (1, snap->bars) * (double) std::max (1, snap->beatsPerBar) : 0.0;
        }

        void block (const Song& song, float* L, float* R, int n)
        {
            head.present = song.present;
            head.info.setIsPlaying (song.playing);
            head.info.setBpm (song.bpm);
            head.info.setPpqPosition (song.ppq);
            head.info.setIsLooping (false);
            head.info.setLoopPoints (juce::nullopt);

            sync.setPreview (previewSwitch);
            const auto hb = diag::readHostBlock (&head, n, false);
            const auto t = sync.plan (hb, n, snap != nullptr ? snap->offsetBeats : 0.0, snap == nullptr || snap->loop, clipBeats());
            if (t.previewEnded)
                previewSwitch = false;
            previewBlocks += t.previewing ? 1 : 0;
            ended += t.previewEnded ? 1 : 0;
            ++blocks;
            if (keep)
                transports.push_back (t);
            host::MidiRouter::renderBlock (*e, t, midi, host::MidiSettings {}, L, R, n);
            midi.clear();
        }
    };

    struct Output
    {
        std::vector<float> L, R;
    };

    /** n samples in blocks of `block`; song (k) says what the host says at the block starting at sample k. */
    Output run (Rig& rig, std::int64_t n, int block, const std::function<Song (std::int64_t)>& song)
    {
        Output out;
        out.L.assign (static_cast<std::size_t> (n), 0.0f);
        out.R.assign (static_cast<std::size_t> (n), 0.0f);
        for (std::int64_t k = 0; k < n; k += block)
        {
            const int len = static_cast<int> (std::min<std::int64_t> (block, n - k));
            rig.block (song (k), out.L.data() + k, out.R.data() + k, len);
        }
        return out;
    }

    /** A host stopped at ppq at this tempo (the play head stands still). */
    std::function<Song (std::int64_t)> stoppedAt (double ppq, double bpm)
    {
        return [ppq, bpm] (std::int64_t) { Song s; s.ppq = ppq; s.bpm = bpm; return s; };
    }

    /** The first sample of every stretch of sound that follows silence. */
    std::vector<std::int64_t> soundStarts (const std::vector<float>& L, const std::vector<float>& R)
    {
        std::vector<std::int64_t> out;
        bool sounding = false;
        for (std::size_t i = 0; i < L.size(); ++i)
        {
            const bool now = ! juce::exactlyEqual (L[i], 0.0f) || ! juce::exactlyEqual (R[i], 0.0f);
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
                why = fmt ("start %zu at sample %lld, the clock's beat at %.4f (%+.4f)", i, static_cast<long long> (got[i]), exact[i], off);
                return false;
            }
        }
        return true;
    }

    /** Where the Preview clock reaches each note of a looping clip: notes at beats s (each shorter than a beat), the clip
        Lb beats long, the clock from beat b0 at spb samples a beat, for n samples. A note sounding at b0 itself is
        chased at sample 0. */
    std::vector<double> clockStarts (const std::vector<double>& noteBeats, double lb, double b0, double spb, std::int64_t n)
    {
        std::vector<double> out;
        for (int m = 0; m < 64; ++m)
            for (const double s : noteBeats)
            {
                const double u = s + lb * m;
                const double at = (u - b0) * spb;
                if (u >= b0 && at < static_cast<double> (n))
                    out.push_back (at);
            }
        std::sort (out.begin(), out.end());
        return out;
    }

    const std::vector<NoteSpec> kFourShort = { { 60, 0, 0.25 }, { 62, 1, 0.25 }, { 64, 2, 0.25 }, { 65, 3, 0.25 } };
    const std::vector<double> kFourBeats = { 0, 1, 2, 3 };
}

//==================================================================================================
SL_TEST ("engine", previewNoteStarts)
{
    section ("Preview with the host stopped: notes start within [0, 1) sample of the Preview clock's beat (120, 87 and 133 bpm; from ppq 0, "
             "from ppq 2.5, from ppq 6.5 with a 4-beat offset on a 2-bar clip, from ppq 2.5 before a 4-beat offset; with no position)");

    // The offset cases (fix pass F2) use an offset that is not a whole number of clip lengths away from the cursor's
    // place in the clip: 6.5 - 4 on the 2-bar clip (its second bar empty) plays from 2.5, where 6.5 is in the empty bar;
    // 2.5 - 4 on the 1-bar clip is the pre-roll, silent until the clip's beat 0, where 2.5 plays at once.
    struct Case
    {
        double ppq, offset;
        int bars;
        const char* what;
    };
    const Case cases[] = { { 0.0, 0.0, 1, "from ppq 0" },
                           { 2.5, 0.0, 1, "from ppq 2.5" },
                           { 6.5, 4.0, 2, "from ppq 6.5, the 2-bar clip offset 4 beats" },
                           { 2.5, 4.0, 1, "from ppq 2.5, the clip offset 4 beats (1.5 beats of pre-roll)" } };
    int blockSize = 333;
    for (const double bpm : { 120.0, 87.0, 133.0 })
    {
        const double spb = 60.0 / bpm * kSr;
        for (const auto& c : cases)
        {
            const model::Clip clip = clipOf (c.bars, kFourShort, true, c.offset);
            const double lb = 4.0 * c.bars;
            Rig rig (tapeInst(), dcSample(), &clip);
            rig.previewSwitch = true;
            const auto n = static_cast<std::int64_t> (12 * spb);
            const auto out = run (rig, n, blockSize, stoppedAt (c.ppq, bpm));
            blockSize = blockSize == 333 ? 512 : blockSize == 512 ? 61 : 333;   // a few block sizes, none dividing a beat
            const double b0 = c.ppq - c.offset;
            std::string why;
            const auto starts = soundStarts (out.L, out.R);
            const bool onTime = startsOnTime (starts, clockStarts (kFourBeats, lb, b0, spb, n), why);
            const bool silentFirst = b0 >= 0.0 || (! starts.empty() && static_cast<double> (starts.front()) >= -b0 * spb - 1e-6);
            check (onTime && silentFirst && rig.previewBlocks == rig.blocks && rig.ended == 0 && rig.sync.isPreviewing(),
                   fmt ("%g bpm, %s: %zu starts, each in [0, 1) sample of the clock's beat from beat %g, nothing before the clip's beat 0; Preview "
                        "played every block (%d of %d) %s", bpm, c.what, starts.size(), b0, rig.previewBlocks, rig.blocks, why.c_str()));

            // a no-op is not a pass: against a clock started at beat 0 (not the Preview start) the same starts fail
            if (! juce::exactlyEqual (b0, 0.0))
            {
                std::string why2;
                check (! startsOnTime (starts, clockStarts (kFourBeats, lb, 0.0, spb, n), why2),
                       fmt ("negative control: measured from beat 0 instead of %g they fail (%s)", b0, why2.c_str()));
            }
            // and with the clip's offset left out (the clock from the cursor's own ppq) they fail too
            if (! juce::exactlyEqual (c.offset, 0.0))
            {
                std::string why3;
                check (! startsOnTime (starts, clockStarts (kFourBeats, lb, c.ppq, spb, n), why3),
                       fmt ("negative control: measured from ppq %g, the offset left out, they fail (%s)", c.ppq, why3.c_str()));
            }
        }
    }

    // the play head gives no position (or no tempo): the clock starts at beat 0 at 120 bpm
    {
        const model::Clip clip = clipOf (1, kFourShort);
        Rig rig (tapeInst(), dcSample(), &clip);
        rig.previewSwitch = true;
        const auto n = static_cast<std::int64_t> (12 * 24000);
        const auto out = run (rig, n, 256, [] (std::int64_t) { Song s; s.present = false; return s; });
        std::string why;
        check (startsOnTime (soundStarts (out.L, out.R), clockStarts (kFourBeats, 4.0, 0.0, 24000.0, n), why),
               "with no position from the host: from beat 0 at 120 bpm, every start in [0, 1) sample of its beat " + why);
    }

    // a no-op is not a pass: with the switch off a stopped host plays no note
    {
        const model::Clip clip = clipOf (1, kFourShort);
        Rig rig (tapeInst(), dcSample(), &clip);
        const auto out = run (rig, 96000, 256, stoppedAt (0.0, 120.0));
        check (soundStarts (out.L, out.R).empty() && rig.previewBlocks == 0, "negative control: Preview off, the host stopped: no note sounds");
    }
}

SL_TEST ("engine", previewPlaysWhatTheHostWould)
{
    section ("A Preview from ppq 1.3 renders bit for bit what the host playing from ppq 1.3 renders (Strand Demo on the Saw, the default Voice engine, 8 s)");

    auto demo = model::loadClipPreset ("strand-demo");
    check (demo.has_value(), "Strand Demo loads");
    if (! demo.has_value())
        return;
    const auto n = static_cast<std::int64_t> (8 * kSr);

    Rig preview (core::defaultInst(), sawSample(), &*demo);
    preview.previewSwitch = true;
    const auto a = run (preview, n, 480, stoppedAt (1.3, 120.0));

    Rig host (core::defaultInst(), sawSample(), &*demo);
    double ppq = 1.3;
    const auto b = run (host, n, 480, [&ppq] (std::int64_t) {
        Song s;
        s.playing = true;
        s.ppq = ppq;
        for (int i = 0; i < 480; ++i)
            ppq += 120.0 / 60.0 / kSr;   // the host's own position at the next block start, sample by sample
        return s;
    });

    double rms = 0;
    for (const float x : a.L)
        rms += static_cast<double> (x) * x;
    rms = std::sqrt (rms / static_cast<double> (a.L.size()));
    check (a.L == b.L && a.R == b.R && rms > 0.01 && preview.previewBlocks == preview.blocks,
           fmt ("the two renders are equal bit for bit (rms %.4f; Preview played %d of %d blocks)", rms, preview.previewBlocks, preview.blocks));
}

SL_TEST ("engine", previewFollowsTheHost)
{
    section ("Preview's clock: a tempo change carries on from the beat it reached; moving the host's position starts it again there; a clip that "
             "does not loop ends it at its end");

    const model::Clip clip = clipOf (1, kFourShort);

    // 120 bpm for 60000 samples (beat 2.5), then 90 bpm (32000 samples a beat), from ppq 0
    {
        Rig rig (tapeInst(), dcSample(), &clip);
        rig.previewSwitch = true;
        const std::int64_t n = 60000 + 9 * 32000;
        const auto out = run (rig, n, 500, [] (std::int64_t k) { Song s; s.bpm = k < 60000 ? 120.0 : 90.0; return s; });
        std::vector<double> exact { 0, 24000, 48000 };
        for (int b = 3; 60000 + (b - 2.5) * 32000 < (double) n; ++b)
            exact.push_back (60000 + (b - 2.5) * 32000);
        std::string why;
        const auto starts = soundStarts (out.L, out.R);
        check (startsOnTime (starts, exact, why) && rig.previewBlocks == rig.blocks,
               fmt ("a tempo change 120 -> 90 at beat 2.5: %zu starts, each in [0, 1) sample of the clock's beat, which carried on from 2.5 %s",
                    starts.size(), why.c_str()));

        // a no-op is not a pass: had the clock started again from the host's position (beat 0) at 90 bpm, they would fail
        std::vector<double> restarted { 0, 24000, 48000 };
        for (int b = 0; 60000 + b * 32000 < (double) n; ++b)
            restarted.push_back (60000 + b * 32000);
        std::string why2;
        check (! startsOnTime (starts, restarted, why2), "negative control: a clock started again at beat 0 does not fit (" + why2 + ")");
    }

    // the host's position moves while stopped (Kalan clicks in Cubase's ruler): from ppq 0, then from ppq 0.5 at 48000
    {
        Rig rig (tapeInst(), dcSample(), &clip);
        rig.previewSwitch = true;
        const std::int64_t n = 48000 + 9 * 24000;
        const auto out = run (rig, n, 400, [] (std::int64_t k) { Song s; s.ppq = k < 48000 ? 0.0 : 0.5; return s; });
        std::vector<double> exact { 0, 24000 };
        for (int b = 1; 48000 + (b - 0.5) * 24000 < (double) n; ++b)
            exact.push_back (48000 + (b - 0.5) * 24000);
        std::string why;
        check (startsOnTime (soundStarts (out.L, out.R), exact, why), "the position moved to ppq 0.5 at sample 48000: the clock starts again there " + why);
    }

    // a clip that does not loop: notes at beats 0 .. 3, then the Preview ends at beat 4 and the switch goes off
    {
        const model::Clip once = clipOf (1, kFourShort, false);
        Rig rig (tapeInst(), dcSample(), &once);
        rig.previewSwitch = true;
        const std::int64_t n = 10 * 24000;
        const auto out = run (rig, n, 333, stoppedAt (0.0, 120.0));
        std::string why;
        const bool fourStarts = startsOnTime (soundStarts (out.L, out.R), { 0, 24000, 48000, 72000 }, why);
        check (fourStarts && rig.ended == 1 && ! rig.previewSwitch && ! rig.sync.isPreviewing(),
               fmt ("loop off: the 4 notes once, then Preview ended at the clip's end (%d end) and the switch went off %s", rig.ended, why.c_str()));
    }
}

SL_TEST ("engine", previewNeverWhileHostPlays)
{
    section ("Preview never runs while the host plays: the render is the one without it, the switch goes off at the first playing block, and stays off "
             "after the host stops");

    const model::Clip clip = clipOf (1, kFourShort);
    const auto n = static_cast<std::int64_t> (4 * kSr);
    // the host plays from ppq 0 for 3 s, then stops at ppq 6 for 1 s
    auto song = [] (std::int64_t k) {
        Song s;
        s.playing = k < 144000;
        s.ppq = s.playing ? static_cast<double> (k) / 24000.0 : 6.0;
        return s;
    };

    Rig with (tapeInst(), dcSample(), &clip);
    with.previewSwitch = true;
    with.keep = true;
    const auto a = run (with, n, 256, song);
    Rig without (tapeInst(), dcSample(), &clip);
    const auto b = run (without, n, 256, song);

    bool hostBeats = true;
    for (std::size_t i = 0; i < with.transports.size(); ++i)
    {
        const auto& t = with.transports[i];
        const Song s = song (static_cast<std::int64_t> (i) * 256);
        hostBeats = hostBeats && ! t.previewing && t.start.playing == s.playing && (! s.playing || juce::exactlyEqual (t.start.beat, s.ppq));
    }
    // the notes at beats 0 to 6: the last block that plays (from sample 143872) runs past beat 6 (sample 144000)
    const auto notes = soundStarts (a.L, a.R).size();
    check (a.L == b.L && a.R == b.R && notes == 7,
           fmt ("the switch on while the host plays: the render equals the one with it off, bit for bit (%zu notes, 7 expected: beats 0 to 6)", notes));
    check (with.ended == 1 && with.previewBlocks == 0 && ! with.previewSwitch && hostBeats,
           fmt ("the switch went off at the first playing block (%d end), no block played the Preview clock, every block played the host's beat",
                with.ended));

    // Preview runs while stopped, then the host plays from ppq 8: from that block on the transport is the host's
    {
        Rig rig (tapeInst(), dcSample(), &clip);
        rig.previewSwitch = true;
        rig.keep = true;
        run (rig, static_cast<std::int64_t> (2 * kSr), 480, [] (std::int64_t k) {
            Song s;
            s.playing = k >= 48000;
            s.ppq = s.playing ? 8.0 + static_cast<double> (k - 48000) / 24000.0 : 0.0;
            return s;
        });
        const bool kept = rig.transports.size() == 200;
        const auto first = kept ? rig.transports[100] : host::BlockTransport {};   // the block at sample 48000
        int restarts = 0;
        for (const auto& t : rig.transports)
            restarts += t.restart ? 1 : 0;
        check (kept && rig.previewBlocks == 100 && rig.ended == 1 && first.start.playing && ! first.previewing
                   && juce::exactlyEqual (first.start.beat, 8.0) && ! rig.previewSwitch && first.restart && restarts == 1,
               fmt ("Preview for %d blocks, then the host plays from ppq 8: that block plays beat %g (the host's) and starts the engine's clock "
                    "again there (%d restart), the switch went off", rig.previewBlocks, first.start.beat, restarts));
    }
}

namespace
{
    /** n samples in blocks of up to 240: song (k) says what the host says at the block starting at sample k, and the
        block before sample `at` is cut so that a block starts exactly there. */
    Output runCutAt (Rig& rig, std::int64_t n, std::int64_t at, const std::function<Song (std::int64_t)>& song)
    {
        Output out;
        out.L.assign (static_cast<std::size_t> (n), 0.0f);
        out.R.assign (static_cast<std::size_t> (n), 0.0f);
        for (std::int64_t k = 0; k < n;)
        {
            const int len = static_cast<int> (std::min<std::int64_t> (240, (k < at ? at : n) - k));
            rig.block (song (k), out.L.data() + k, out.R.data() + k, len);
            k += len;
        }
        return out;
    }

    /** The starts at or after sample `from`, and where the 1-bar clip's notes fall on a clock from beat 0 at that sample.
        A note sounding right at `from` (chased there) shows as no start when the sound before it had not ended. */
    void startsFrom (const Output& out, std::int64_t from, double spb, std::vector<std::int64_t>& got, std::vector<double>& want)
    {
        for (const auto s : soundStarts (out.L, out.R))
            if (s >= from)
                got.push_back (s);
        const auto i = static_cast<std::size_t> (from - 1);
        const bool soundingBefore = from > 0 && (! juce::exactlyEqual (out.L[i], 0.0f) || ! juce::exactlyEqual (out.R[i], 0.0f));
        const auto n = static_cast<std::int64_t> (out.L.size());
        for (const double at : clockStarts (kFourBeats, 4.0, 0.0, spb, n - from))
            if (at > 0.0 || ! soundingBefore)
                want.push_back (static_cast<double> (from) + at);
    }
}

SL_TEST ("engine", previewHandsOverOnTheBeat)
{
    section ("Cubase's Play after a Preview plays on Cubase's beat from that block, wherever Preview's clock was (4.05, 3.95, 8.08 and 4.5 beats "
             "of Preview on a 1-bar clip, then the host from ppq 0; 120 and 87 bpm); a cursor move while Preview plays starts the clock exactly "
             "there (fix pass F1)");

    const model::Clip clip = clipOf (1, kFourShort);
    for (const double bpm : { 120.0, 87.0 })
    {
        const double spb = 60.0 / bpm * kSr, bps = bpm / 60.0 / kSr;
        for (const double previewBeats : { 4.05, 3.95, 8.08, 4.5 })
        {
            // Preview from Cubase's cursor at ppq 0, then Cubase plays from ppq 0 at sample p, for 7.5 beats (the last note
            // starts at its beat 7, well inside the render)
            const auto p = static_cast<std::int64_t> (std::llround (previewBeats * spb));
            const auto n = p + static_cast<std::int64_t> (std::ceil (7.5 * spb));
            Rig rig (tapeInst(), dcSample(), &clip);
            rig.previewSwitch = true;
            rig.keep = true;
            const auto out = runCutAt (rig, n, p, [p, bpm, bps] (std::int64_t k) {
                Song s;
                s.bpm = bpm;
                s.playing = k >= p;
                s.ppq = s.playing ? static_cast<double> (k - p) * bps : 0.0;
                return s;
            });
            std::vector<std::int64_t> got;
            std::vector<double> want;
            startsFrom (out, p, spb, got, want);
            std::string why;
            const bool onBeat = startsOnTime (got, want, why);
            int restarts = 0;
            for (const auto& t : rig.transports)
                restarts += t.restart ? 1 : 0;
            check (onBeat && ! got.empty() && restarts == 1 && rig.ended == 1 && ! rig.previewSwitch,
                   fmt ("%g bpm, Preview for %g beats, then Cubase plays from ppq 0: %zu starts, each in [0, 1) sample of Cubase's beat (one "
                        "restart of the engine's clock) %s", bpm, previewBeats, got.size(), why.c_str()));

            // a no-op is not a pass: Preview's own clock carried on (the engine kept it before the fix whenever Cubase's beat was
            // within 0.1 beat of it) puts these starts elsewhere
            if (previewBeats < 4.5)
            {
                std::vector<double> carried;
                for (const double at : clockStarts (kFourBeats, 4.0, 0.0, spb, n))
                    if (at >= static_cast<double> (p))
                        carried.push_back (at);
                std::string why2;
                check (! startsOnTime (got, carried, why2),
                       fmt ("negative control: on Preview's clock carried on they fail (%s)", why2.c_str()));
            }
        }
    }

    // the cursor moves while Preview plays, to a place within 0.1 beat of Preview's clock in the clip: Preview from ppq 1
    // for 3.05 beats (its clock at the clip's beat 0.05), then the cursor at ppq 0
    for (const double bpm : { 120.0, 87.0 })
    {
        const double spb = 60.0 / bpm * kSr;
        const auto m = static_cast<std::int64_t> (std::llround (3.05 * spb));
        const auto n = m + static_cast<std::int64_t> (std::ceil (7.5 * spb));
        Rig rig (tapeInst(), dcSample(), &clip);
        rig.previewSwitch = true;
        rig.keep = true;
        const auto out = runCutAt (rig, n, m, [m, bpm] (std::int64_t k) {
            Song s;
            s.bpm = bpm;
            s.ppq = k < m ? 1.0 : 0.0;
            return s;
        });
        std::vector<std::int64_t> got;
        std::vector<double> want;
        startsFrom (out, m, spb, got, want);
        std::string why;
        const bool onBeat = startsOnTime (got, want, why);
        int restarts = 0;
        for (const auto& t : rig.transports)
            restarts += t.restart ? 1 : 0;
        check (onBeat && ! got.empty() && restarts == 1 && rig.previewBlocks == rig.blocks,
               fmt ("%g bpm, the cursor moved to ppq 0 while Preview's clock was at the clip's beat 0.05: %zu starts, each in [0, 1) sample of the "
                    "new clock's beat (one restart) %s", bpm, got.size(), why.c_str()));

        std::vector<double> carried;
        for (const double at : clockStarts (kFourBeats, 4.0, 1.0, spb, n))
            if (at >= static_cast<double> (m))
                carried.push_back (at);
        std::string why2;
        check (! startsOnTime (got, carried, why2), fmt ("negative control: on the old clock carried on they fail (%s)", why2.c_str()));
    }
}

//==================================================================================================
namespace
{
    /** The processor on a play head the test moves, a block at a time. */
    struct ProcRig
    {
        StrandLockProcessor proc;
        harness::FakePlayHead head;
        juce::AudioBuffer<float> buffer { 2, 256 };
        juce::MidiBuffer midi;
        std::unique_ptr<engine::DisplayFrame> frame = std::make_unique<engine::DisplayFrame>();
        double ppq = 0.0;

        ProcRig()
        {
            proc.loadStarterSound (sounds::StarterId::saw);
            proc.waitForSounds();
            proc.setPlayHead (&head);
            proc.setPlayConfigDetails (0, 2, kSr, 256);
            proc.prepareToPlay (kSr, 256);
            proc.waitForSnapshot();
            head.info.setBpm (120.0);
        }

        ~ProcRig() { proc.setPlayHead (nullptr); proc.releaseResources(); }

        /** blocks of 256 samples: the host playing (its position moving) or stopped; the rms of the last one. */
        double blocks (int count, bool playing)
        {
            double rms = 0.0;
            for (int i = 0; i < count; ++i)
            {
                head.info.setIsPlaying (playing);
                head.info.setPpqPosition (ppq);
                proc.processBlock (buffer, midi);
                if (playing)
                    ppq += 256.0 * 2.0 / kSr;
                rms = buffer.getRMSLevel (0, 0, 256);
            }
            proc.getEngine().fillDisplay (*frame);
            return rms;
        }
    };
}

SL_TEST ("engine", previewThroughTheProcessor)
{
    section ("Preview through the processor: the switch plays the clip while Cubase is stopped; Cubase's Play turns it off (counted); a click after "
             "that stands; the display frames say playing while it runs");

    ProcRig r;
    auto& p = r.proc;

    r.blocks (20, false);
    const bool quiet = ! r.frame->playing && ! p.isPreviewPlaying() && ! p.isPreviewOn();
    p.setPreview (true);
    const double rmsLeft = r.blocks (180, false);
    const double rms = std::max (rmsLeft, (double) r.buffer.getRMSLevel (1, 0, 256));
    check (quiet && p.isPreviewOn() && p.isPreviewPlaying() && r.frame->playing && r.frame->beat > 1.5 && rms > 0.001,
           fmt ("stopped, Preview on: the clip plays (the frame says playing at beat %.3f after 0.96 s, rms %.4f)", r.frame->beat, rms));

    const auto stops0 = p.getPreviewStops();
    r.ppq = 4.0;
    r.blocks (1, true);
    const bool offAtOnce = ! p.isPreviewOn() && ! p.isPreviewPlaying() && p.getPreviewStops() == stops0 + 1;
    r.blocks (40, true);
    check (offAtOnce && r.frame->playing && std::abs (r.frame->beat - (4.0 + 41 * 256 * 2.0 / kSr)) < 0.01,
           fmt ("Cubase plays from ppq 4: Preview went off at the first block (stops %u), the frame follows Cubase's beat (%.3f)", p.getPreviewStops(),
                r.frame->beat));

    p.setPreview (true);   // a click while Cubase plays: turned off again at the next block, and counted
    r.blocks (1, true);
    check (! p.isPreviewOn() && p.getPreviewStops() == stops0 + 2, "Preview clicked on while Cubase plays: off again at the next block, counted");

    r.blocks (30, false);   // Cubase stops: Preview stays off
    check (! p.isPreviewOn() && ! p.isPreviewPlaying() && ! r.frame->playing, "Cubase stops: Preview stays off and nothing plays the clip");

    p.setPreview (true);   // a click after the stop stands
    r.blocks (10, false);
    check (p.isPreviewOn() && p.isPreviewPlaying() && r.frame->playing && p.getPreviewStops() == stops0 + 2,
           "a click after that: Preview plays again from Cubase's position");

    // three clicks before a block: the last one is the switch (a click made while a block runs: previewSwitchKeepsLaterClicks)
    const auto blocksBefore = p.getBlocksProcessed();
    p.setPreview (false);
    p.setPreview (true);
    p.setPreview (false);
    r.blocks (5, false);
    check (! p.isPreviewOn() && ! p.isPreviewPlaying() && ! r.frame->playing && p.getBlocksProcessed() == blocksBefore + 5,
           fmt ("off, on, off before a block: off (%llu blocks counted)", (unsigned long long) (p.getBlocksProcessed() - blocksBefore)));
}

SL_TEST ("engine", previewSwitchKeepsLaterClicks)
{
    section ("Preview's switch word, as processBlock uses it (fix pass F4): the audio thread turns it off only while it still holds the word its "
             "block read, so a click made after the block began always stands");

    host::PreviewSwitch sw;
    const bool startsOff = ! sw.isOn() && sw.read() == 0u;
    sw.set (true);
    const auto word = sw.read();   // a block begins and reads the switch on; Cubase plays, so it will turn it off
    sw.set (false);
    sw.set (true);                 // Kalan clicks off and on again while that block runs
    const auto later = sw.read();
    const bool refused = ! sw.turnOffIf (word);
    check (startsOff && host::PreviewSwitch::isOn (word) && later != word && refused && sw.isOn() && sw.read() == later,
           "the block that read the switch on cannot turn it off after a later click: the click stands, the word as the click left it");

    // the next block reads the newer word: its stop goes through, and the clicks counted so far stay in the word
    const bool off = sw.turnOffIf (later) && ! sw.isOn() && (sw.read() >> 1) == (later >> 1);
    sw.set (true);
    const bool counted = sw.isOn() && (sw.read() >> 1) == (later >> 1) + 1u;
    const auto w2 = sw.read();
    sw.set (false);   // a click off after a block read the word: that block's stop finds a newer word and leaves it be
    const bool offStands = ! sw.turnOffIf (w2) && ! sw.isOn();
    check (off && counted && offStands,
           "a block's stop on the word it read turns the switch off and keeps the click count; a click after that counts on from it; a click "
           "off made during a block stays off");
}

SL_TEST ("alloc", previewAllocatesNothing)
{
    section ("20 s at 48 kHz / 64-sample blocks with Preview: on and off, Cubase stopped and playing, tempo changes while Preview runs and while "
             "Cubase plays, cursor moves while Preview runs, Cubase's Play after a Preview: no allocation or free on the audio thread");

    const double rate = 48000.0;
    const int block = 64;
    StrandLockProcessor proc;
    harness::FakePlayHead head;
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (0, 2, rate, block);
    proc.prepareToPlay (rate, block);
    proc.waitForSounds();
    proc.waitForSnapshot();

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    const auto total = static_cast<juce::int64> (20.0 * rate / block);
    double ppq = 0.0, lastBpm = 120.0;
    int previewBlocks = 0, tempoInPreview = 0, tempoWhilePlaying = 0, movesInPreview = 0, handOvers = 0;
    long long switches = 0;
    bool finite = true, playedBefore = false, previewBefore = false;

    alloc::reset();
    for (juce::int64 b = 0; b < total; ++b)
    {
        // every 2 s: Cubase plays for 0.5 s of each 2; the switch goes on every second; the cursor moves now and then;
        // the tempo changes every 433 blocks, so it changes while Preview runs and while Cubase plays (fix pass F5)
        const juce::int64 inTwo = b % (juce::int64) (2.0 * rate / block);
        const bool playing = inTwo >= (juce::int64) (1.5 * rate / block);
        if (inTwo == 0 || inTwo == (juce::int64) (0.7 * rate / block))
        {
            proc.setPreview (true);   // the message thread's click (outside the counted scope)
            ++switches;
        }
        bool moved = false;
        if (b % 997 == 0)
        {
            const double cursor = (double) (b % 7);   // Kalan moves Cubase's cursor
            moved = ! juce::exactlyEqual (cursor, ppq);
            ppq = cursor;
        }
        const double bpm = (b / 433) % 2 == 0 ? 120.0 : 97.0;
        const bool tempoChanged = ! juce::exactlyEqual (bpm, lastBpm);
        head.info.setIsPlaying (playing);
        head.info.setBpm (bpm);
        head.info.setPpqPosition (ppq);
        if (playing)
            ppq += block * 2.0 / rate;

        {
            const alloc::Scope counting;
            proc.processBlock (buffer, midi);
        }
        const bool previewNow = proc.isPreviewPlaying();
        previewBlocks += previewNow ? 1 : 0;
        tempoInPreview += tempoChanged && previewBefore && previewNow ? 1 : 0;   // the Preview clock's tempo branch
        tempoWhilePlaying += tempoChanged && playedBefore && playing ? 1 : 0;   // the engine's re-anchor to the host
        movesInPreview += moved && previewBefore && previewNow ? 1 : 0;         // the Preview clock starts again
        handOvers += playing && previewBefore ? 1 : 0;                          // Cubase's Play after a Preview block
        previewBefore = previewNow;
        playedBefore = playing;
        lastBpm = bpm;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
        if (b % 1500 == 0)
            proc.collectVersions();
    }
    const auto c = alloc::counts();
    note (fmt ("%lld blocks, %d of them Preview's, %lld clicks on, %u stops by Cubase's Play; %d tempo changes while Preview ran, %d while Cubase "
               "played, %d cursor moves while Preview ran, %d times Cubase's Play came after a Preview block; counting %s", (long long) total,
               previewBlocks, switches, proc.getPreviewStops(), tempoInPreview, tempoWhilePlaying, movesInPreview, handOvers, alloc::coverage()));
    check (previewBlocks > total / 3 && proc.getPreviewStops() > 0 && finite,
           fmt ("Preview played %d blocks and Cubase's Play stopped it %u times; every sample finite", previewBlocks, proc.getPreviewStops()));
    check (tempoInPreview > 0 && tempoWhilePlaying > 0 && movesInPreview > 0 && handOvers > 0,
           fmt ("the run reached every branch: %d tempo changes while Preview ran, %d while Cubase played, %d cursor moves while Preview ran, %d "
                "hand-overs to Cubase's Play", tempoInPreview, tempoWhilePlaying, movesInPreview, handOvers));
    check (c.news == 0 && c.deletes == 0, fmt ("allocations in processBlock: %lld, frees: %lld (largest request %zu bytes)", c.news, c.deletes, c.largest));
    proc.setPlayHead (nullptr);
}
