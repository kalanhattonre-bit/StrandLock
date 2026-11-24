// F3, notes that hold on across the loop end (PLUGIN-PLAN 4 M8, 7.10 decision 7, 8.2 #11): with the clip's
// tieAcrossLoop on (new clips), a note that crosses the clip end carries on into the next pass instead of being cut
// there, and a slide from the last note into the next pass's first note is allowed (model::Glides across the loop,
// the engine's effLen and its chain steps that wrap). With tieAcrossLoop off the engine is the prototype's.
//
// Rows:
// - the model: Auto Slide joins the last note to the next pass's first note (legato), a whole-loop note to its own
//   next start, and an explicit link to an earlier note slides into it in the next pass; with the tie off none of it;
// - a note crossing the clip end (Tape, the ramp source that encodes the read position): the minimum envelope across
//   the loop point > 0.999, one voice, the playhead continues within 0.002 of the same note played where no loop end
//   cuts it (and the samples are that render's); with the tie off it is cut there (the control);
// - a whole-loop note (the "why" of F3): one voice for three passes, no new attack, the playhead runs on;
// - a slide into the next pass: the voice of the last note takes the first note over at the loop point and lands on
//   its pitch;
// - Play started inside a note's held tail (and inside a chain that crossed the loop end) chases it, the playhead
//   within 0.002 of the straight render;
// - every engine/renders.json case still passes with tieAcrossLoop off; with it on, every case the tie cannot reach
//   renders bit for bit the same, and a case it can reach (a slide over the loop end) changes.
// Group "engine" (and "model" for the first row).

#include "engine/EngineFixture.h"
#include "engine/EngineRender.h"
#include "engine/GrainRig.h"
#include "engine/RenderCompare.h"
#include "engine/VoiceRig.h"
#include "model/ModelFixture.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Snapshot.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "model/Glides.h"
#include "model/SnapshotCompiler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000.0;
    constexpr std::int64_t kBeat = 24000;   // samples per beat at 120 bpm, 48 kHz

    /** a == b for numbers that are not NaN (without a float == the compilers warn about). */
    bool eq (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

    model::NotePtr noteOf (double p, double s, double l, const char* id)
    {
        return std::make_shared<const model::Note> (model::mkNote (p, s, l, id));
    }

    model::Clip clipOf (int bars, std::vector<model::NotePtr> notes, bool tie)
    {
        model::Clip c = model::newClip (bars);
        c.tieAcrossLoop = tie;
        c.notes = std::move (notes);
        return c;
    }

    core::Inst tapeInst (core::GlideMode mode = core::GlideMode::legato)
    {
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        inst.glideMode = mode;
        return inst;
    }

    /** What one play of a clip gave: the audio, and from the display after every 64-sample block, the lowest
        envelope of the first voice inside the windows asked for and the most voices at once from `countFrom` on. */
    struct Played
    {
        std::vector<float> L, R;
        double minEnv = 1;
        int maxVoices = 0;
        int envSeen = 0;   // display frames that had a voice inside the windows
    };

    /** Plays clip (beatsPerBar 4, loop on, tieAcrossLoop as the clip has it) from `beat` at 120 bpm for n samples. */
    Played play (const model::Clip& clip, const core::Inst& inst, std::unique_ptr<engine::SampleData> sample, double beat, std::int64_t n,
                 const std::vector<std::int64_t>& envAt = {}, std::int64_t countFrom = 0)
    {
        grains::Play p (kSr, inst, std::move (sample));
        p.snapshot (render::compileTied (clip, clip.tieAcrossLoop, 256));
        p.play (120, beat, true);
        Played out;
        p.render (n, 64, [&] (std::int64_t i) {
            const engine::DisplayFrame& f = p.display();
            if (i >= countFrom)
                out.maxVoices = std::max (out.maxVoices, static_cast<int> (f.numVoices));
            for (const std::int64_t at : envAt)
                if (i > at - 4800 && i < at + 4800 && f.numVoices > 0)
                {
                    out.minEnv = std::min (out.minEnv, static_cast<double> (f.voices[0].env));
                    ++out.envSeen;
                }
        });
        out.L = std::move (p.L);
        out.R = std::move (p.R);
        return out;
    }

    std::unique_ptr<engine::SampleData> ramp (double secs) { return rig::sampleOf (signals::rampSource (secs, kSr)); }

    double largestDifference (const Played& a, const Played& b, std::int64_t from, std::int64_t to)
    {
        double d = 0;
        for (std::int64_t i = from; i < to && i < static_cast<std::int64_t> (a.L.size()) && i < static_cast<std::int64_t> (b.L.size()); ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            d = std::max ({ d, std::abs (static_cast<double> (a.L[k]) - b.L[k]), std::abs (static_cast<double> (a.R[k]) - b.R[k]) });
        }
        return d;
    }

    /** F3 can reach a clip: a note inside it crosses its end, or a glide or chain step wraps (any Auto Slide mode). */
    bool tieReaches (const core::Snapshot& s)
    {
        const double Lb = std::max (1.0, static_cast<double> (s.bars)) * std::max (1.0, static_cast<double> (s.beatsPerBar));
        for (const core::NoteSnap& x : s.notes)
        {
            if (x.s >= 0 && x.s < Lb && x.s + x.l > Lb)
                return true;
            for (int m = 0; m < 3; ++m)
                if (x.glides[m].wraps || x.chains[m].nextWraps || x.chains[m].prevWraps)
                    return true;
        }
        return false;
    }
}

SL_TEST ("model", tieLoopGlides)
{
    section ("F3 in the model: with tieAcrossLoop on, slides and chains go over the loop end; with it off, the prototype's");

    const auto findGlide = [] (const model::GlideMap& g, const char* id) { return g.find (id); };

    // The last note A (C3, beats 3..4) touches the next pass's first note B (G3, beats 0..2) at the loop end.
    for (const bool tie : { true, false })
    {
        const model::Clip clip = clipOf (1, { noteOf (60, 3, 1, "A"), noteOf (67, 0, 2, "B") }, tie);
        const model::GlideMap g = model::computeGlides (clip, tapeInst());
        const model::Glide* a = findGlide (g, "A");
        const model::ChainLinks links = model::buildChainLinks (clip, g);
        if (tie)
        {
            const auto nx = links.next.find ("A");
            const auto pv = links.prev.find ("B");
            check (a != nullptr && a->toId == "B" && a->wraps && ! a->linked && eq (a->d, 7.0) && eq (a->len, 0.5),
                   "tie on, Auto Slide legato: the last note slides into the next pass's first note (A -> B, +7 st over 0.5 beat, wraps)");
            check (nx != links.next.end() && nx->second.id == "B" && nx->second.wraps && pv != links.prev.end() && pv->second.id == "A"
                       && pv->second.wraps && links.chains == model::Chains { { "A", "B" } },
                   "tie on: one chain A -> B whose step wraps into the next pass");
        }
        else
        {
            check (a == nullptr && links.next.empty() && links.chains.size() == 2,
                   "tie off (the prototype): no slide over the loop end, two chains");
        }
    }

    // A whole-loop note joins its own next start (legato); with Auto Slide Off it does not, as touching notes inside a
    // clip do not.
    {
        const model::Clip pad = clipOf (1, { noteOf (60, 0, 4, "P") }, true);
        const model::GlideMap g = model::computeGlides (pad, tapeInst());
        const model::ChainLinks links = model::buildChainLinks (pad, g);
        const model::Glide* p = findGlide (g, "P");
        const auto nx = links.next.find ("P");
        check (p != nullptr && p->toId == "P" && p->wraps && eq (p->d, 0.0) && nx != links.next.end() && nx->second.id == "P"
                   && nx->second.wraps && links.chains == model::Chains { { "P" } },
               "a whole-loop note slides into its own next start (0 st): its chain goes round the clip for ever");
        const model::GlideMap off = model::computeGlides (pad, tapeInst (core::GlideMode::off));
        check (off.empty() && model::buildChainLinks (pad, off).next.empty(), "with Auto Slide Off it starts again each pass (no glide, no link)");

        // A note longer than the loop plays one loop (effLen), so it ends where its next start begins and joins it too.
        const model::Clip longer = clipOf (1, { noteOf (60, 0, 6, "L") }, true);
        const model::GlideMap longGlides = model::computeGlides (longer, tapeInst());
        const model::Glide* l = findGlide (longGlides, "L");
        check (l != nullptr && l->toId == "L" && l->wraps && eq (model::glideEnd (*longer.notes[0], longer), 4.0),
               "a note longer than the loop ends after one loop (glideEnd 4) and joins its own next start");
    }

    // An explicit link to a note that does not start after its source slides into it in the next pass, whatever the
    // Auto Slide mode; with the tie off it stays the prototype's glide that joins nothing.
    for (const bool tie : { true, false })
    {
        model::Note a = model::mkNote (60, 1, 1, "A");
        a.glideTo = "B";
        model::Clip clip = clipOf (1, { std::make_shared<const model::Note> (a), noteOf (64, 0, 1, "B") }, tie);
        const model::GlideMap g = model::computeGlides (clip, tapeInst (core::GlideMode::off));
        const model::Glide* ga = findGlide (g, "A");
        const model::ChainLinks links = model::buildChainLinks (clip, g);
        check (ga != nullptr && ga->linked && ga->toId == "B" && ga->wraps == tie && (links.next.count ("A") == (tie ? 1u : 0u)),
               tie ? "tie on: an explicit link back to an earlier note joins it in the next pass (Auto Slide Off)"
                   : "tie off: the same link is the prototype's glide, joining nothing");
    }

    // The snapshot carries the wrapped steps: notes in time order, B (index 0) and A (index 1).
    {
        const model::Clip clip = clipOf (1, { noteOf (60, 3, 1, "A"), noteOf (67, 0, 2, "B") }, true);
        const auto snap = model::compileSnapshot (clip, 32);
        const int legato = static_cast<int> (core::GlideMode::legato);
        const core::NoteSnap& B = snap->notes[0];
        const core::NoteSnap& A = snap->notes[1];
        check (snap->tieAcrossLoop && A.glides[legato].present && A.glides[legato].wraps && A.glides[legato].toIndex == 0
                   && A.chains[legato].nextIndex == 0 && A.chains[legato].nextWraps && B.chains[legato].prevIndex == 1 && B.chains[legato].prevWraps
                   && B.chains[legato].firstIndex == 1 && ! A.chains[0].nextWraps && A.chains[0].nextIndex == -1,
               "the snapshot: A's legato glide and chain step wrap into B, B's previous note wraps back to A, A heads the chain; "
               "Auto Slide Off has none");
        model::Clip off = clip;
        off.tieAcrossLoop = false;
        bool anyWrap = false;
        const auto offSnap = model::compileSnapshot (off, 32);
        for (const auto& x : offSnap->notes)
            for (int m = 0; m < 3; ++m)
                anyWrap = anyWrap || x.glides[m].wraps || x.chains[m].nextWraps || x.chains[m].prevWraps;
        check (! anyWrap, "with the tie off the snapshot has no step that wraps");
    }
}

SL_TEST ("engine", tieLoopNoteCrossingTheEnd)
{
    section ("F3: a note crossing the clip end holds on (min envelope > 0.999, one voice, the playhead continues within 0.002)");

    // A 1-bar clip (4 beats) with one note from beat 3 to beat 5: it crosses the loop point (beat 4, sample 96000) by a
    // beat. The reference: the same note in a 2-bar clip, which no loop end cuts. Tape at C3 on the ramp source.
    const std::int64_t kLoop = 4 * kBeat, n = 116 * kBeat / 20;   // 5.8 beats
    const auto crossing = [] (bool tie) { return clipOf (1, { noteOf (60, 3, 2, "X") }, tie); };
    const Played tied = play (crossing (true), tapeInst(), ramp (8), 0, n, { kLoop }, 3 * kBeat);
    const Played ref = play (clipOf (2, { noteOf (60, 3, 2, "X") }, true), tapeInst(), ramp (8), 0, n, { kLoop }, 3 * kBeat);
    const Played cut = play (crossing (false), tapeInst(), ramp (8), 0, n, { kLoop }, 3 * kBeat);

    const double before = rig::decodeAt (tied.L, tied.R, kLoop - 400), after = rig::decodeAt (tied.L, tied.R, kLoop + 4000);
    const double refAfter = rig::decodeAt (ref.L, ref.R, kLoop + 4000);
    const double expected = static_cast<double> (kLoop + 4000 - 3 * kBeat) / (8 * kSr);   // 1x from the Region start at beat 3
    note (fmt ("min envelope within 0.1 s of the loop point %.6f (%d frames), most voices %d; read position %.6f before, %.6f after "
               "(the uncut note %.6f, 1x %.6f)", tied.minEnv, tied.envSeen, tied.maxVoices, before, after, refAfter, expected));
    check (tied.envSeen > 100 && tied.minEnv > 0.999, fmt ("no new attack and no release at the loop point: min envelope %.6f (> 0.999)", tied.minEnv));
    check (tied.maxVoices == 1, fmt ("one voice from beat 3 on (most %d)", tied.maxVoices));
    check (std::abs (after - refAfter) < 0.002 && std::abs (after - expected) < 0.002,
           fmt ("the playhead continues: %.6f after the loop point, the uncut note's %.6f (+- 0.002)", after, refAfter));
    const double diff = largestDifference (tied, ref, 3 * kBeat, n);
    check (diff <= 1e-6, fmt ("from beat 3 on it plays the uncut note's samples (largest difference %g)", diff));
    check (rig::rms (tied.L, kLoop, kLoop + kBeat) > 0.05, fmt ("it sounds after the loop point (rms %.4f)", rig::rms (tied.L, kLoop, kLoop + kBeat)));

    // A no-op is not a pass: with the tie off the note is cut at the loop point (the prototype), which the same
    // measurements catch.
    const double cutAfter = rig::rms (cut.L, kLoop + 4000, kLoop + kBeat);
    note (fmt ("tie off: min envelope %.6f, rms after the loop point %.6f", cut.minEnv, cutAfter));
    check (cut.minEnv < 0.5 && cutAfter < 1e-4, "negative control: with tieAcrossLoop off the note stops at the loop point (released there)");
}

SL_TEST ("engine", tieLoopWholeLoopNote)
{
    section ("F3: a whole-loop note carries on into every pass: one voice, no new attack, the playhead runs on (Auto Slide legato)");

    // A 1-bar clip with one note as long as the clip: three passes (12 beats, 6 s) on the 16 s ramp source.
    const std::int64_t n = 12 * kBeat;
    const auto pad = [] (bool tie) { return clipOf (1, { noteOf (60, 0, 4, "P") }, tie); };
    const Played tied = play (pad (true), tapeInst(), ramp (16), 0, n, { 4 * kBeat, 8 * kBeat }, 0);
    const Played again = play (pad (false), tapeInst(), ramp (16), 0, n, { 4 * kBeat, 8 * kBeat }, 0);
    double worst = 0;
    for (const std::int64_t k : { 4 * kBeat + 4000, 8 * kBeat + 4000, 11 * kBeat })
        worst = std::max (worst, std::abs (rig::decodeAt (tied.L, tied.R, k) - static_cast<double> (k) / (16 * kSr)));
    const double restarted = rig::decodeAt (again.L, again.R, 4 * kBeat + 4000);
    note (fmt ("tie on: min envelope %.6f across both loop points, most voices %d, the playhead at most %.6f from 1x since beat 0; "
               "tie off: min envelope %.6f, read position %.6f after the first loop point (restarted)", tied.minEnv, tied.maxVoices, worst,
               again.minEnv, restarted));
    check (tied.envSeen > 200 && tied.minEnv > 0.999 && tied.maxVoices == 1,
           fmt ("one voice through three passes, min envelope %.6f across the loop points (> 0.999)", tied.minEnv));
    check (worst < 0.002, fmt ("the sample is not restarted: the playhead runs on at 1x from beat 0 (within %.6f, +- 0.002)", worst));
    check (again.minEnv < 0.999 && std::abs (restarted - 4000.0 / (16 * kSr)) < 0.002,
           "negative control: with the tie off the note starts again each pass (an attack, the playhead back at the Region start)");

    // With Auto Slide Off the whole-loop note starts again each pass, as touching notes do inside the clip.
    const Played off = play (pad (true), tapeInst (core::GlideMode::off), ramp (16), 0, n, { 4 * kBeat }, 0);
    check (off.minEnv < 0.999, fmt ("Auto Slide Off: the whole-loop note starts again at the loop point (min envelope %.4f)", off.minEnv));
}

SL_TEST ("engine", tieLoopSlideIntoNextPass)
{
    section ("F3: a slide into the next pass: the last note's voice takes the first note over at the loop point and lands on its pitch");

    // A (C3, beats 3..4) slides into B (G3, beats 0..2) of the next pass (legato, +7 st over the last 0.5 beat of A).
    // Tape on the C3 tone: in pass 0 B plays on its own voice; from beat 3 one voice plays A and then B.
    const auto slide = [] (bool tie) { return clipOf (1, { noteOf (60, 3, 1, "A"), noteOf (67, 0, 2, "B") }, tie); };
    const std::int64_t n = 6 * kBeat + 4800;
    auto tone = [] { return rig::sampleOf (signals::tone (signals::midiHz (60), 8.0, kSr), kSr); };
    const Played tied = play (slide (true), tapeInst(), tone(), 0, n, { 4 * kBeat }, 3 * kBeat);
    const Played apart = play (slide (false), tapeInst(), tone(), 0, n, { 4 * kBeat }, 3 * kBeat);
    const auto pass1B = static_cast<std::int64_t> (4.25 * static_cast<double> (kBeat));
    const double f = grains::harmonicF0 (tied.L, kSr, signals::midiHz (67), 15, pass1B).f0;
    const double cents = grains::cents (f, signals::midiHz (67));
    note (fmt ("tie on: most voices from beat 3 %d, min envelope across the loop point %.6f, B in pass 1 at %+.3f cents from G3; "
               "tie off: most voices %d, min envelope %.6f", tied.maxVoices, tied.minEnv, cents, apart.maxVoices, apart.minEnv));
    check (tied.maxVoices == 1 && tied.envSeen > 100 && tied.minEnv > 0.999,
           fmt ("one voice from A into the next pass's B, no new attack (min envelope %.6f)", tied.minEnv));
    check (std::abs (cents) <= 5.0, fmt ("the slide lands on G3 (%+.3f cents, +-5)", cents));
    check (apart.maxVoices >= 2 || apart.minEnv < 0.999, "negative control: with the tie off B starts its own voice in the next pass");
}

SL_TEST ("engine", tieLoopChase)
{
    section ("F3: Play inside a note's held tail, and inside a chain that crossed the loop end, chases it (playhead within 0.002)");

    // Play from beat 4.5: the note X (beats 3..5 of a 1-bar clip) is in its held tail, 1.5 beats after its start.
    // The probe: 0.1 s after Play (past the 10 ms chase fade-in), which is sample 4.5 beats + 0.1 s of a render from beat 0.
    const std::int64_t after = 4800, playAt = 9 * kBeat / 2, n = after + 2400;
    {
        const model::Clip clip = clipOf (1, { noteOf (60, 3, 2, "X") }, true);
        const Played straight = play (clip, tapeInst(), ramp (8), 0, playAt + n);
        const Played chased = play (clip, tapeInst(), ramp (8), 4.5, n);
        const double want = rig::decodeAt (straight.L, straight.R, playAt + after);
        const double got = rig::decodeAt (chased.L, chased.R, after);
        note (fmt ("Play at beat 4.5 in X's held tail: read position %.6f, the straight render %.6f", got, want));
        check (std::abs (got - want) < 0.002, fmt ("the held tail is chased: the playhead %.6f, the straight render's %.6f (+- 0.002)", got, want));
        const Played cut = play (clipOf (1, { noteOf (60, 3, 2, "X") }, false), tapeInst(), ramp (8), 4.5, n);
        check (rig::rms (cut.L, 0, n) < 1e-9, "negative control: with the tie off nothing sounds there (the note was cut at the loop end)");
    }
    // Play from beat 4.5 inside B (beats 4..6 of pass 1), which A (beats 3..4) slid into: the chase starts on A in pass 0
    // and hands over to B, so the playhead carries A's (Tape on the ramp source, B at G3 reads 1.498x).
    {
        const model::Clip clip = clipOf (1, { noteOf (60, 3, 1, "A"), noteOf (67, 0, 2, "B") }, true);
        const Played straight = play (clip, tapeInst(), ramp (8), 0, playAt + n);
        const Played chased = play (clip, tapeInst(), ramp (8), 4.5, n);
        const double want = rig::decodeAt (straight.L, straight.R, playAt + after);
        const double got = rig::decodeAt (chased.L, chased.R, after);
        // B chased on its own from its start in pass 1 (beat 4) would read 0.5 beat + 0.1 s at 1.498x from the Region start
        const double alone = static_cast<double> (kBeat / 2 + after) * std::pow (2.0, 7.0 / 12.0) / (8 * kSr);
        note (fmt ("Play at beat 4.5 in B after A slid into it: read position %.6f, the straight render %.6f (B chased on its own: %.6f)", got,
                   want, alone));
        check (std::abs (got - want) < 0.002 && std::abs (got - alone) > 0.01,
               fmt ("the chain over the loop end is chased from A: the playhead %.6f, the straight render's %.6f (+- 0.002), not B's own %.6f", got,
                    want, alone));
    }
}

SL_TEST ("engine", tieLoopRendersStillPass)
{
    section ("F3: every engine/renders.json case passes with tieAcrossLoop off; with it on, the cases it cannot reach render the same");

    core::JsonValue doc;
    if (! fixture::load ("engine/renders.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const core::JsonValue& cases = doc["cases"];

    modelfx::FileTally tally ("engine/renders.json with tieAcrossLoop off", cases.size());
    int unreached = 0, unreachedSame = 0, reached = 0, reachedChanged = 0;
    std::string changedNames;
    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const core::JsonValue& c = cases[ci];
        const std::string name = c["name"].asString();
        render::Rendered off, on;
        std::string why;
        render::FixtureOptions tieOff, tieOn;
        tieOn.tieAcrossLoop = true;
        if (! render::renderFixtureCase (c, clips, off, why, tieOff) || ! render::renderFixtureCase (c, clips, on, why, tieOn))
        {
            tally.fail (name + ": " + why);
            continue;
        }
        bool hashMatch = false;
        if (render::renderMatches (off, c["out"], hashMatch, why))
            tally.pass (hashMatch);
        else
            tally.fail (name + ": " + why);

        // Can the tie reach this case? Its clips (the first one, and any a "snapshot" event sets) compiled with it on.
        bool reaches = false;
        std::vector<const core::JsonValue*> refs;
        if (c.contains ("clip"))
            refs.push_back (&c["clip"]);
        for (std::size_t e = 0; e < c["events"].size(); ++e)
            if (c["events"][e][1].asString() == "snapshot")
                refs.push_back (&c["events"][e][2]);
        for (const core::JsonValue* ref : refs)
        {
            model::Clip clip;
            const core::Inst inst = enginefx::instFromEngineJson (c["inst"]);
            if (render::clipOf (*ref, ref == &c["clip"] ? c.find ("recipe") : nullptr, inst, clips, clip))
                reaches = reaches || tieReaches (*render::compileTied (clip, true, 32));
        }
        if (c.contains ("snapshot"))
        {
            auto raw = render::rawSnapshotFromJson (c["snapshot"], why);
            reaches = reaches || (raw != nullptr && tieReaches (*raw));
        }
        const bool same = rig::hashOf (off.L, off.R) == rig::hashOf (on.L, on.R) && off.stats == on.stats;
        if (reaches)
        {
            ++reached;
            if (! same)
            {
                ++reachedChanged;
                changedNames += " " + name;
            }
        }
        else
        {
            ++unreached;
            unreachedSame += same ? 1 : 0;
            if (! same)
                note (name + ": the tie cannot reach it, yet its render changed with tieAcrossLoop on");
        }
    }
    tally.finish();
    note (fmt ("tieAcrossLoop on: %d cases the tie cannot reach, %d of them bit for bit; %d it can reach (a note crossing the end or a "
               "slide over it), %d of them changed%s", unreached, unreachedSame, reached, reachedChanged, changedNames.c_str()));
    check (unreached > 0 && unreachedSame == unreached,
           fmt ("with tieAcrossLoop on, every case the tie cannot reach renders bit for bit the same (%d of %d)", unreachedSame, unreached));

    // A no-op is not a pass: loop-3-passes-133bpm with Auto Slide Always slides its last note into the next pass's
    // first one, so the same comparison sees the tie change it.
    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        if (cases[ci]["name"].asString() != "loop-3-passes-133bpm")
            continue;
        core::JsonValue c = cases[ci];
        core::JsonValue inst = c["inst"];
        inst.set ("glideMode", core::JsonValue ("always"));
        c.set ("inst", inst);
        render::Rendered off, on;
        std::string why;
        render::FixtureOptions tieOn;
        tieOn.tieAcrossLoop = true;
        const bool rendered = render::renderFixtureCase (c, clips, off, why) && render::renderFixtureCase (c, clips, on, why, tieOn);
        check (rendered && rig::hashOf (off.L, off.R) != rig::hashOf (on.L, on.R),
               "negative control: loop-3-passes-133bpm with Auto Slide Always (its last note slides into the next pass) changes with the tie on");
    }
}
