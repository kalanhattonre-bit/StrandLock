// The inspector's pure helpers against Tests/fixtures/ui/inspector.json (PLUGIN-PLAN 4 M4 "inspector parsing",
// 7.6 R1): fmtPos, parsePos, parseBeats, parsePitch, rateId, fmtNum and laneValueFromText (with laneText) equal the
// prototype's strings exactly and its values within 1e-9. The file also holds the M6 helpers (simulate, peakBins,
// thumbAmps, tabAt) of the roll's note decorations: noteDecorHelpersMatchFixture (M6 T2) checks them, each within the
// file's tolerance (values 1e-9, Float32 tables 2 ulps), so no case is skipped silently.

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "model/Generators.h"
#include "model/LaneFormat.h"
#include "sounds/StarterSounds.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/roll/InspectorText.h"
#include "ui/roll/NoteDecor.h"
#include "model/ModelFixture.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    const std::vector<std::string> kR1Helpers { "fmtPos", "parsePos", "parseBeats", "parsePitch", "rateId", "fmtNum", "laneValueFromText" };
    const std::vector<std::string> kM6Helpers { "simulate", "peakBins", "thumbAmps", "tabAt" };

    bool isOneOf (const std::string& s, const std::vector<std::string>& list)
    {
        for (const auto& x : list)
            if (x == s)
                return true;
        return false;
    }

    model::LaneCtx ctxFrom (const core::JsonValue& c)
    {
        model::LaneCtx ctx;
        ctx.inst = core::instFromJson (c["inst"]);   // a partial instrument, normalized as the helpers normalize it
        ctx.bpm = c.contains ("bpm") ? fixture::num (c["bpm"]) : std::nan ("");
        ctx.travelKind = c["travelKind"].isString() && c["travelKind"].asString() == "path" ? model::TravelKind::path : model::TravelKind::speed;
        return ctx;
    }

    std::string str (const core::JsonValue& v) { return core::toJson (v); }

    /** A nullable number output: null = rejected. */
    bool sameNumber (const std::optional<double>& got, const core::JsonValue& want, double tol, bool& exact)
    {
        if (want.isNull())
            return ! got.has_value();
        if (! got)
            return false;
        const double w = fixture::num (want);
        if (! fixture::same (*got, w))
            exact = false;
        return fixture::withinAbs (*got, w, tol);
    }

    std::string gotText (const std::optional<double>& v) { return v ? core::jsNumberToString (*v) : std::string ("null"); }
}

SL_TEST ("ui", inspectorHelpersMatchFixture)
{
    section ("Inspector: the pure helpers equal SLPanels' strings and values (ui/inspector.json)");

    core::JsonValue doc;
    if (! fixture::load ("ui/inspector.json", doc))
        return;
    const auto& cases = doc["cases"];
    const double tol = fixture::num (doc["tolerance"]["values"]);
    check (fixture::withinAbs (tol, 1e-9, 1e-15), fmt ("the file's value tolerance is 1e-9 (%g)", tol));

    std::size_t r1 = 0, m6 = 0, other = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const std::string fn = cases[i]["fn"].asString();
        if (isOneOf (fn, kR1Helpers))
            ++r1;
        else if (isOneOf (fn, kM6Helpers))
            ++m6;
        else
            ++other;
    }
    note (fmt ("%zu cases of the six helpers (and rateId, fmtNum); %zu cases of the M6 helpers (simulate, peakBins, thumbAmps, tabAt) "
               "(noteDecorHelpersMatchFixture checks them); %zu of any other helper", r1, m6, other));
    check (other == 0, "every case names a helper this test or noteDecorHelpersMatchFixture knows");

    modelfx::FileTally tally ("ui/inspector.json (R1 helpers)", r1);
    int perFn[7] = {};
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const std::string fn = c["fn"].asString();
        if (! isOneOf (fn, kR1Helpers))
            continue;
        const auto& in = c["input"];
        const auto& want = c["output"];
        bool exact = true, ok = false;
        std::string got;

        if (fn == "fmtPos")
        {
            got = ui::roll::fmtPos (fixture::num (in));
            ok = got == want.asString();
            ++perFn[0];
        }
        else if (fn == "parsePos" || fn == "parseBeats" || fn == "parsePitch")
        {
            const std::string text = in.asString();
            const auto v = fn == "parsePos" ? ui::roll::parsePos (text) : fn == "parseBeats" ? ui::roll::parseBeats (text) : ui::roll::parsePitch (text);
            ok = sameNumber (v, want, tol, exact);
            got = gotText (v);
            ++perFn[fn == "parsePos" ? 1 : fn == "parseBeats" ? 2 : 3];
        }
        else if (fn == "rateId")
        {
            const auto r = ui::roll::rateId (in.asString());
            ok = want.isNull() ? ! r.has_value() : (r && *r == want.asString());
            got = r ? *r : std::string ("null");
            ++perFn[4];
        }
        else if (fn == "fmtNum")
        {
            const double x = fixture::num (in[0]);
            const int d = in[1].isNull() ? -1 : (int) fixture::num (in[1]);
            got = ui::roll::fmtNum (x, d);
            ok = got == want.asString();
            ++perFn[5];
        }
        else
        {
            const auto lane = core::findLane (in[0].asString());
            if (! lane)
            {
                tally.fail ("unknown lane " + str (in));
                continue;
            }
            const auto r = ui::roll::laneValueFromText (*lane, in[1].asString(), ctxFrom (c["ctx"]));
            if (want.contains ("error"))
            {
                ok = ! r.ok && r.text == want["error"].asString();
            }
            else
            {
                const double w = fixture::num (want["v"]);
                ok = r.ok && fixture::withinAbs (r.v, w, tol) && r.text == want["text"].asString();
                exact = r.ok && fixture::same (r.v, w);
            }
            got = r.ok ? "{v " + core::jsNumberToString (r.v) + ", " + r.text + "}" : "{error " + r.text + "}";
            ++perFn[6];
        }

        if (ok)
            tally.pass (exact);
        else
            tally.fail (fn + " " + str (in) + (c.contains ("ctx") ? " ctx " + str (c["ctx"]) : std::string()) + ": got " + got + ", want " + str (want));
    }
    tally.finish();
    note (fmt ("fmtPos %d, parsePos %d, parseBeats %d, parsePitch %d, rateId %d, fmtNum %d, laneValueFromText %d", perFn[0], perFn[1], perFn[2],
               perFn[3], perFn[4], perFn[5], perFn[6]));
    check (perFn[0] > 0 && perFn[1] > 0 && perFn[2] > 0 && perFn[3] > 0 && perFn[6] > 0, "every one of the six helpers has cases in the file");
}

SL_TEST ("ui", inspectorLaneTextRoundTrips)
{
    section ("Inspector: a readout typed back gives the value it shows (laneText, laneValueFromText)");

    // Every lane at 101 values: the readout typed back reads the same readout again. A parser that drops the sign, the
    // unit or the "k" of kHz turns this red.
    model::LaneCtx ctx;
    ctx.inst = core::defaultInst();
    ctx.inst.refresh.mode = core::RefreshMode::free;
    ctx.bpm = 96.0;
    int ran = 0, same = 0;
    std::string firstBad;
    for (int li = 0; li < core::kNumLanes; ++li)
    {
        const auto lane = static_cast<core::LaneId> (li);
        for (int k = 0; k <= 100; ++k)
        {
            const double v = k / 100.0;
            const std::string shown = ui::roll::laneText (lane, v, ctx);
            const auto back = ui::roll::laneValueFromText (lane, shown, ctx);
            ++ran;
            if (back.ok && back.text == shown)
                ++same;
            else if (firstBad.empty())
                firstBad = std::string (core::laneInfo (lane).code) + " " + core::jsNumberToString (v) + " \"" + shown + "\" -> \"" + back.text + "\"";
        }
    }
    note (fmt ("%d of %d readouts read back to themselves%s%s", same, ran, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    // One readout cannot come back, in the prototype too (measured in node with SLPanels): Tone at 0.49 reads "1.0 kHz",
    // which types back as 1000 Hz. So the prototype's count is 1009 of 1010.
    check (same == 1009, fmt ("1009 of the 1010 readouts read back to themselves, as in the prototype (%d)", same));
}

SL_TEST ("ui", inspectorPositionsCountTheClipsBars)
{
    section ("Inspector: the Start box counts the clip's beats per bar, as the ruler and the drag's readout do (fix pass F5)");

    // The prototype's bars always hold 4 beats (the fixture's cases); a clip of the plugin can hold other bars (its
    // CLIP chunk's beatsPerBar). By hand: with 3 beats per bar beat 3 is bar 2's first beat, beat 4.25 is bar 2,
    // beat 2, its second sixteenth.
    struct Case
    {
        int bpb;
        double beats;
        const char* text;
    };
    const Case cases[] = { { 3, 0.0, "1.1.1" }, { 3, 3.0, "2.1.1" }, { 3, 4.25, "2.2.2" }, { 3, 11.75, "4.3.4" }, { 3, 2.0625, "1.3.1.30" },
                           { 6, 7.5, "2.2.3" }, { 5, 5.0, "2.1.1" }, { 7, 13.0, "2.7.1" }, { 4, 4.0, "2.1.1" }, { 2, 7.0, "4.2.1" } };
    int right = 0;
    std::string firstBad;
    for (const auto& c : cases)
    {
        const auto shown = ui::roll::fmtPos (c.beats, c.bpb);
        const auto back = ui::roll::parsePos (c.text, c.bpb);
        const bool ok = shown == c.text && back.has_value() && fixture::same (*back, c.beats);
        right += ok ? 1 : 0;
        if (! ok && firstBad.empty())
            firstBad = fmt ("%d beats per bar, beat %g: \"%s\", \"%s\" reads as %s", c.bpb, c.beats, shown.c_str(), c.text,
                            back ? core::jsNumberToString (*back).c_str() : "nothing");
    }
    const int count = (int) (sizeof (cases) / sizeof (cases[0]));
    check (right == count, fmt ("%d of %d positions shown and read with the clip's beats per bar as worked out by hand%s%s", right, count,
                                firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));

    // every sixteenth of 24 beats, 1 to 7 beats per bar: shown, then typed back, it is the same beat (sixteenths are
    // exact in binary); and 4 beats per bar is the fixture's own rule
    int roundTrips = 0, sameAsDefault = 0, tried = 0;
    for (int bpb = 1; bpb <= 7; ++bpb)
        for (int k = 0; k <= 24 * 4; ++k)
        {
            const double b = k / 4.0;
            ++tried;
            const auto back = ui::roll::parsePos (ui::roll::fmtPos (b, bpb), bpb);
            roundTrips += back.has_value() && fixture::same (*back, b) ? 1 : 0;
            sameAsDefault += bpb != 4 || ui::roll::fmtPos (b, 4) == ui::roll::fmtPos (b) ? 1 : 0;
        }
    check (roundTrips == tried && sameAsDefault == tried,
           fmt ("%d of %d sixteenths with 1 to 7 beats per bar read back as themselves; with 4 the text is the prototype's", roundTrips, tried));
}

//==================================================================================================
// M6 T2: the roll's note decorations (sl-ui-panels.js simulate, peakBins, thumbAmps and tabAt), ported in
// ui/roll/NoteDecor.

namespace
{
    /** got against a fixture F32 list: the same length, and every value within maxUlps Float32 ulps (NaN against NaN
        counts as equal). exact is cleared when any value differs at all. */
    bool floatsMatch (const std::vector<float>& got, const core::JsonValue& want, std::uint64_t maxUlps, bool& exact, std::string& why)
    {
        std::vector<float> w;
        if (! fixture::f32 (want, w))
        {
            why = "the fixture's list is not an F32 list";
            return false;
        }
        if (w.size() != got.size())
        {
            why = fmt ("%zu values, the fixture has %zu", got.size(), w.size());
            return false;
        }
        for (std::size_t i = 0; i < w.size(); ++i)
        {
            const auto u = fixture::ulps (got[i], w[i]);
            if (u != 0)
                exact = false;
            if (u > maxUlps)
            {
                why = fmt ("value %zu is %.9g, the fixture's %.9g", i, (double) got[i], (double) w[i]);
                return false;
            }
        }
        return true;
    }

    /** got against a fixture list of small integers, exactly. */
    template <class T>
    bool intsMatch (const std::vector<T>& got, const core::JsonValue& want, std::string& why)
    {
        if (want.size() != got.size())
        {
            why = fmt ("%zu flags, the fixture has %zu", got.size(), want.size());
            return false;
        }
        for (std::size_t i = 0; i < got.size(); ++i)
            if ((int) got[i] != (int) fixture::num (want[i]))
            {
                why = fmt ("flag %zu is %d, the fixture's %d", i, (int) got[i], (int) fixture::num (want[i]));
                return false;
            }
        return true;
    }

    /** The label tools/fixtures/ui.js gives a simulate case's instrument (init, scatter, stop, bounce, tape-hold). */
    std::string simLabel (const core::Inst& inst)
    {
        if (inst.motion == core::Motion::scatter)
            return "scatter";
        if (inst.region.atEnd == core::AtEnd::stop)
            return "stop";
        if (inst.motion == core::Motion::bounce)
            return "bounce";
        if (inst.engine == core::Engine::tape)
            return "tape-hold";
        return "init";
    }
}

SL_TEST ("ui", noteDecorHelpersMatchFixture)
{
    section ("Note decorations: simulate, peakBins, thumbAmps and tabAt equal SLPanels' (ui/inspector.json, M6 T2)");

    core::JsonValue doc;
    if (! fixture::load ("ui/inspector.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const auto& cases = doc["cases"];
    const double tol = fixture::num (doc["tolerance"]["values"]);
    const auto maxUlps = (std::uint64_t) fixture::num (doc["tolerance"]["tablesUlps"]);
    check (maxUlps == 2, fmt ("the file's Float32 tolerance is 2 ulps (%llu)", (unsigned long long) maxUlps));

    std::size_t m6 = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
        if (isOneOf (cases[i]["fn"].asString(), kM6Helpers))
            ++m6;
    modelfx::FileTally tally ("ui/inspector.json (M6 helpers)", m6);

    // a starter sound's peak bins, made once
    std::map<std::string, std::vector<float>> binsMade;
    const auto binsOf = [&binsMade] (const std::string& starter, int seed) -> const std::vector<float>*
    {
        const auto id = sounds::parseStarter (starter);
        if (! id)
            return nullptr;
        const auto key = starter + "/" + std::to_string (seed);
        auto it = binsMade.find (key);
        if (it == binsMade.end())
        {
            const auto s = sounds::makeStarterSound (*id, seed);
            it = binsMade.emplace (key, ui::roll::peakBins (s.left.data(), s.right.empty() ? nullptr : s.right.data(),
                                                            (std::int64_t) s.frames()))
                     .first;
        }
        return &it->second;
    };

    // simulate first (thumbAmps reads its results), then the rest
    std::map<std::string, ui::roll::NoteSim> sims;
    int perFn[4] = {};
    int silentPoints = 0, jumpPoints = 0, stillPoints = 0, backwardPoints = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < cases.size(); ++i)
        {
            const auto& c = cases[i];
            const std::string fn = c["fn"].asString();
            if (! isOneOf (fn, kM6Helpers) || (fn == "simulate") != (pass == 0))
                continue;
            const auto& in = c["input"];
            const auto& want = c["output"];
            bool exact = true, ok = false;
            std::string why;

            if (fn == "simulate")
            {
                ++perFn[0];
                const auto clip = clips.find (in["clip"].asString());
                const model::Note* n = clip != clips.end() ? modelfx::findNote (clip->second, in["note"].asString()) : nullptr;
                if (n == nullptr)
                {
                    tally.fail ("simulate: no note " + str (in["note"]));
                    continue;
                }
                const core::Inst inst = core::instFromJson (in["inst"]);
                const double res = fixture::num (in["res"]);
                const auto T = model::laneTable (*n, core::LaneId::travel, res);
                const auto P = model::laneTable (*n, core::LaneId::pitch, res);
                const auto r = ui::roll::simulate (*n, inst, fixture::num (in["bpm"]), fixture::num (in["secs"]), T, P, (int) fixture::num (in["N"]));
                sims[simLabel (inst) + ":" + n->id] = r;
                ok = r.N == (int) fixture::num (want["N"]) && floatsMatch (r.pos, want["pos"], maxUlps, exact, why)
                  && intsMatch (r.dir, want["dir"], why) && floatsMatch (r.sp, want["sp"], maxUlps, exact, why)
                  && intsMatch (r.jump, want["jump"], why);
                for (std::size_t k = 0; k < r.pos.size(); ++k)
                {
                    silentPoints += std::isnan (r.pos[k]) ? 1 : 0;
                    jumpPoints += r.jump[k] != 0 ? 1 : 0;
                    stillPoints += ! std::isnan (r.pos[k]) && r.dir[k] == 0 ? 1 : 0;
                    backwardPoints += r.dir[k] < 0 ? 1 : 0;
                }
                if (! ok)
                    why = simLabel (inst) + ":" + n->id + ": " + why;
            }
            else if (fn == "peakBins")
            {
                ++perFn[1];
                const auto* bins = binsOf (in["starter"].asString(), (int) fixture::num (in["seed"]));
                ok = bins != nullptr && floatsMatch (*bins, want, maxUlps, exact, why);
                if (bins == nullptr)
                    why = "no starter " + str (in["starter"]);
            }
            else if (fn == "thumbAmps")
            {
                ++perFn[2];
                const auto sim = sims.find (in["simulate"].asString());
                const auto* bins = binsOf (in["starter"].asString(), (int) fixture::num (in["seed"]));
                if (sim == sims.end() || bins == nullptr)
                {
                    tally.fail ("thumbAmps: no simulate case " + str (in["simulate"]) + " or no starter " + str (in["starter"]));
                    continue;
                }
                const auto amps = ui::roll::thumbAmps (sim->second, *bins, fixture::num (in["secs"]), fixture::num (in["regLen"]));
                ok = floatsMatch (amps, want, maxUlps, exact, why);
            }
            else
            {
                ++perFn[3];
                std::vector<float> table;
                if (! fixture::f32 (in[0], table))
                {
                    tally.fail ("tabAt: the table is not an F32 list");
                    continue;
                }
                const double got = ui::roll::tabAt (table, fixture::num (in[1]));
                const double w = fixture::num (want);
                ok = fixture::withinAbs (got, w, tol);
                exact = fixture::same (got, w);
                if (! ok)
                    why = "got " + core::jsNumberToString (got) + ", want " + core::jsNumberToString (w);
            }

            if (ok)
                tally.pass (exact);
            else
                tally.fail (fn + " " + why);
        }
    tally.finish();
    note (fmt ("simulate %d, peakBins %d, thumbAmps %d, tabAt %d; the simulations hold %d silent points (At the end: Stop), %d Scatter "
               "jumps, %d still points and %d backward points", perFn[0], perFn[1], perFn[2], perFn[3], silentPoints, jumpPoints, stillPoints,
               backwardPoints));
    check (perFn[0] > 0 && perFn[1] > 0 && perFn[2] > 0 && perFn[3] > 0, "each of the four helpers has cases in the file");
    check (silentPoints > 0 && jumpPoints > 0 && stillPoints > 0 && backwardPoints > 0,
           "the cases reach Stop's silence, Scatter's jumps, a note standing still and one playing backwards");
}
