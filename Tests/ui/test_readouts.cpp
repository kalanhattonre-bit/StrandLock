// The Sample and Macros tabs' readouts (PLUGIN-PLAN 4 M6 "readouts", 7.8 T1) against Tests/fixtures/ui/readouts.json:
//
// - macroText, offsetText, the knob formats (fmtX, fmtPct, fmtSt, fmtCycles, fmtMs, fmtHeads, signed), parseNum / parseMs /
//   parsePct and KNOB_MATH (toNorm, fromNorm, qLog, qLin, magnetLog) through ui/SampleTabRules equal SLSampleUI's strings
//   and numbers exactly, case by case, with the case count gated;
// - every knob's readout: each value the real Sample and Macros tabs showed under the prototype tests' own DOM (after a
//   drag, a typed value, a double-click, and in each setting of the controls cases) reads the same from
//   rules::knobText, including the two the prototype writes inline (Cloud grain "140 ms", Lock grain "4 waves").

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "params/ParamText.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/SampleTabRules.h"
#include "model/ModelFixture.h"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    namespace rules = sl::ui::rules;

    const std::vector<std::string> kReadoutFns { "macroText", "offsetText", "fmtX", "fmtPct", "fmtSt", "fmtCycles", "fmtMs", "fmtHeads", "signed",
                                                 "parseNum", "parseMs", "parsePct", "toNorm", "fromNorm", "qLog", "qLin", "magnetLog" };
    const std::vector<std::string> kKnobFns { "knobDrag", "knobReset", "knobType", "controls" };

    bool isOneOf (const std::string& s, const std::vector<std::string>& list)
    {
        for (const auto& x : list)
            if (x == s)
                return true;
        return false;
    }

    int offsetIndex (const std::string& k)
    {
        static const char* const names[] = { "amp", "vib", "tone", "formant", "hollow" };
        for (int i = 0; i < 5; ++i)
            if (k == names[i])
                return i;
        return -1;
    }

    std::vector<double> numbers (const core::JsonValue& v)
    {
        std::vector<double> out;
        if (v.isArray())
            for (std::size_t i = 0; i < v.size(); ++i)
                out.push_back (fixture::num (v[i]));
        return out;
    }

    rules::KnobRange rangeOf (const core::JsonValue& s)
    {
        rules::KnobRange r;
        r.min = fixture::num (s["min"]);
        r.max = fixture::num (s["max"]);
        r.log = s["log"].asBool (false);
        r.def = fixture::num (s["def"]);
        return r;
    }

    std::string numText (const std::optional<double>& v) { return v ? core::jsNumberToString (*v) : std::string ("null"); }
}

//==================================================================================================
SL_TEST ("ui", readoutsMatchFixture)
{
    section ("Readouts: macroText, offsetText, the knob formats, parsing and KNOB_MATH equal SLSampleUI's exactly (ui/readouts.json)");

    core::JsonValue doc;
    if (! fixture::load ("ui/readouts.json", doc))
        return;
    const auto& cases = doc["cases"];

    std::size_t mine = 0, knobs = 0, other = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const std::string fn = cases[i]["fn"].asString();
        mine += isOneOf (fn, kReadoutFns) ? 1 : 0;
        knobs += isOneOf (fn, kKnobFns) ? 1 : 0;
        other += ! isOneOf (fn, kReadoutFns) && ! isOneOf (fn, kKnobFns) ? 1 : 0;
    }
    check (other == 0, fmt ("every case names a function this test or the knob tests know (%zu unknown)", other));

    modelfx::FileTally tally ("ui/readouts.json (readouts, parsing, KNOB_MATH)", mine);
    std::map<std::string, int> perFn;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const std::string fn = c["fn"].asString();
        if (! isOneOf (fn, kReadoutFns))
            continue;
        ++perFn[fn];
        const auto& in = c["input"];
        const auto& want = c["output"];
        bool ok = false;
        std::string got;

        if (fn == "macroText")
            ok = (got = rules::macroText ((int) in[0].asNumber(), fixture::num (in[1]))) == want.asString();
        else if (fn == "offsetText")
            ok = (got = rules::offsetText (offsetIndex (in[0].asString()), fixture::num (in[1]))) == want.asString();
        else if (fn == "fmtX")      ok = (got = params::text::fmtX (fixture::num (in))) == want.asString();
        else if (fn == "fmtPct")    ok = (got = params::text::fmtPct (fixture::num (in))) == want.asString();
        else if (fn == "fmtSt")     ok = (got = params::text::fmtSt (fixture::num (in))) == want.asString();
        else if (fn == "fmtCycles") ok = (got = params::text::fmtCycles (fixture::num (in))) == want.asString();
        else if (fn == "fmtMs")     ok = (got = params::text::fmtMs (fixture::num (in))) == want.asString();
        else if (fn == "fmtHeads")  ok = (got = params::text::fmtHeads (fixture::num (in))) == want.asString();
        else if (fn == "signed")    ok = (got = params::text::signedText (fixture::num (in[0]), (int) in[1].asNumber())) == want.asString();
        else if (fn == "parseNum" || fn == "parseMs" || fn == "parsePct")
        {
            const std::string typed = in.isString() ? in.asString() : std::string();
            const auto v = fn == "parseNum" ? rules::parseNum (typed) : fn == "parseMs" ? rules::parseMs (typed) : rules::parsePct (typed);
            ok = want.isNull() ? ! v.has_value() : (v.has_value() && fixture::same (*v, fixture::num (want)));
            got = numText (v);
        }
        else if (fn == "toNorm" || fn == "fromNorm")
        {
            const auto r = rangeOf (in[0]);
            const double v = fn == "toNorm" ? rules::toNorm (r, fixture::num (in[1])) : rules::fromNorm (r, fixture::num (in[1]));
            ok = fixture::same (v, fixture::num (want));
            got = core::jsNumberToString (v);
        }
        else if (fn == "qLog")
        {
            const auto marks = numbers (in[0]);
            const double v = rules::qLog (marks, in[1].isNull() ? 0 : (int) in[1].asNumber(), fixture::num (in[2]), in[3].asBool());
            ok = fixture::same (v, fixture::num (want));
            got = core::jsNumberToString (v);
        }
        else if (fn == "qLin")
        {
            const auto marks = numbers (in[2]);
            const double tol = in[3].isNull() ? 0.0 : fixture::num (in[3]);
            const double v = rules::qLin (fixture::num (in[0]), fixture::num (in[1]), marks, tol, fixture::num (in[4]), in[5].asBool());
            ok = fixture::same (v, fixture::num (want));
            got = core::jsNumberToString (v);
        }
        else if (fn == "magnetLog")
        {
            const auto marks = numbers (in[1]);
            const double v = rules::magnetLog (fixture::num (in[0]), marks, fixture::num (in[2]));
            ok = fixture::same (v, fixture::num (want));
            got = core::jsNumberToString (v);
        }

        if (ok)
            tally.pass (true);
        else
            tally.fail (fmt ("%s %s: \"%s\", the prototype %s", fn.c_str(), core::toJson (in).c_str(), got.c_str(), core::toJson (want).c_str()));
    }
    tally.finish();
    std::string counts;
    for (const auto& [fn, n] : perFn)
        counts += fmt ("%s%s %d", counts.empty() ? "" : ", ", fn.c_str(), n);
    note (counts);
    check (perFn["macroText"] >= 300 && perFn["offsetText"] >= 200 && perFn["qLog"] > 0 && perFn["qLin"] > 0 && perFn["toNorm"] > 0,
           "the file holds the readouts and the knob maths (a file without them would pass nothing)");
}

//==================================================================================================
SL_TEST ("ui", knobReadoutsMatchFixture)
{
    section ("Readouts: every knob's readout, as the real Sample and Macros tabs showed it, from rules::knobText");

    core::JsonValue doc;
    if (! fixture::load ("ui/readouts.json", doc))
        return;
    const auto& cases = doc["cases"];

    // the knob named by its undo label, and the grain kind a setting gives it
    const auto knobNamed = [] (const std::string& name) -> std::optional<rules::KnobId>
    {
        for (const auto id : rules::allKnobs())
            if (name == rules::knobSpec (id).undo)
                return id;
        return std::nullopt;
    };
    const auto grainOf = [] (const std::string& ctx) -> std::optional<rules::Kind>
    {
        if (ctx == "cloud")
            return rules::Kind::cloud;
        if (ctx == "lock")
            return rules::Kind::lock;
        if (ctx == "tape")
            return std::nullopt;
        return rules::Kind::voice;
    };

    int tried = 0, same = 0, shown = 0, inline2 = 0;
    const auto compare = [&] (const rules::KnobSpec& s, double value, const std::string& want, const std::string& where)
    {
        ++tried;
        const std::string got = s.off ? std::string ("\xe2\x80\x94") : rules::knobText (s, value);
        if (got == want)
            ++same;
        else if (shown++ < 12)
            note (fmt ("%s: %s at %.17g reads \"%s\", the prototype \"%s\"", where.c_str(), s.undo, value, got.c_str(), want.c_str()));
        if (s.format == rules::KnobFormat::cloudMs || s.format == rules::KnobFormat::lockWaves)
            ++inline2;
    };

    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const std::string fn = c["fn"].asString();
        if (fn != "knobDrag" && fn != "knobReset" && fn != "knobType")
            continue;
        const auto& in = c["input"];
        const auto& out = c["output"];
        const auto id = knobNamed (in[0].asString());
        if (! id)
        {
            check (false, "a knob case names a knob the tabs have: " + in[0].asString());
            continue;
        }
        compare (rules::knobSpec (*id, grainOf (in[1].asString())), fixture::num (out[0]), out[1].asString(), fn + " " + core::toJson (in));
    }

    // the controls cases: each knob's readout in each setting, from the setting's own value
    const auto& insts = doc["insts"];
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        if (c["fn"].asString() != "controls")
            continue;
        const std::string ctx = c["input"].asString();
        const auto inst = core::instFromJson (insts[ctx]);
        const auto& out = c["output"];
        for (std::size_t k = 0; k < out.size(); ++k)
        {
            const auto id = knobNamed (out.keyAt (k));
            const auto& row = out.valueAt (k);
            if (! id || row[4].isNull())
                continue;
            const auto kind = rules::grainKind (inst, nullptr);   // the controls cases play Strand Demo, whose notes follow the instrument
            const auto s = rules::knobSpec (*id, *id == rules::KnobId::grain ? kind : std::optional<rules::Kind> (rules::Kind::voice));
            double v = 0.0;
            switch (s.param)
            {
                case params::P::speed:       v = inst.speed; break;
                case params::P::heads:       v = inst.heads; break;
                case params::P::spread:      v = inst.spread; break;
                case params::P::grainVoice:  v = inst.grainVoice; break;
                case params::P::grainCloud:  v = inst.grainCloud; break;
                case params::P::grainLock:   v = inst.grainLock; break;
                case params::P::spray:       v = inst.spray; break;
                case params::P::drift:       v = inst.drift; break;
                case params::P::pitch:       v = inst.pitch; break;
                case params::P::refreshMs:   v = inst.refresh.ms; break;
                case params::P::refreshSkip: v = inst.refresh.skip; break;
                case params::P::ampOffset:     v = inst.offsets.amp; break;
                case params::P::vibOffset:     v = inst.offsets.vib; break;
                case params::P::toneOffset:    v = inst.offsets.tone; break;
                case params::P::formantOffset: v = inst.offsets.formant; break;
                case params::P::hollowOffset:  v = inst.offsets.hollow; break;
                default:
                    v = inst.macros[(std::size_t) (params::index (s.param) - params::index (params::P::macro1))];
                    break;
            }
            compare (s, v, row[4].asString(), "controls " + ctx + " " + out.keyAt (k));
        }
    }

    check (tried >= 400 && same == tried,
           fmt ("%d of %d knob readouts equal the prototype's (%d of them the Cloud and Lock grain formats it writes inline)", same, tried, inline2));
    check (inline2 >= 20, fmt ("the cases cover the Cloud and Lock grain formats (%d)", inline2));
}
