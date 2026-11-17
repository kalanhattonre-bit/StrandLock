// The inspector's pure helpers against Tests/fixtures/ui/inspector.json (PLUGIN-PLAN 4 M4 "inspector parsing",
// 7.6 R1): fmtPos, parsePos, parseBeats, parsePitch, rateId, fmtNum and laneValueFromText (with laneText) equal the
// prototype's strings exactly and its values within 1e-9. The file also holds the M6 helpers (simulate, peakBins,
// thumbAmps, tabAt), which M6 tests; they are counted apart so no case is skipped silently.

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "model/LaneFormat.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/roll/InspectorText.h"
#include "model/ModelFixture.h"

#include <cmath>
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
    check (tol == 1e-9 || std::abs (tol - 1e-9) < 1e-15, fmt ("the file's value tolerance is 1e-9 (%g)", tol));

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
               "left for M6; %zu of any other helper", r1, m6, other));
    check (other == 0, "every case names a helper this test or M6 knows");

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
