// model/seeds.json, every case (PLUGIN-PLAN 4 M1 "fixtures model/*", 5.3): the seed hashes, ids, rates, the
// Travel speed curve and its Float32 dead-band edges, the bend and glide curves, JavaScript's number
// printing and rounding, the 96 key x scale snaps and the note names. Integers and strings exact; the
// dead-band decision exact; bendShape, gShape, travelSpeed, travelSpeedToV within 4e-16 relative.

#include "ModelFixture.h"

#include "core/Curves.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Music.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "core/Travel.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    constexpr double kCurveRel = 4e-16;   // the file's tolerance for bendShape, gShape, travelSpeed, travelSpeedToV

    std::string str (double x) { return core::jsNumberToString (x); }

    struct Outcome
    {
        bool ok = true;
        bool exact = true;
        std::string why;

        void exactValue (double got, double want, const std::string& what)
        {
            if (! fixture::same (got, want))
            {
                ok = false;
                exact = false;
                why = what + " = " + str (got) + ", want " + str (want);
            }
        }

        void relative (double got, double want, double rel, const std::string& what)
        {
            if (! fixture::same (got, want))
                exact = false;
            if (! fixture::withinRel (got, want, rel))
            {
                ok = false;
                why = what + " = " + str (got) + ", want " + str (want);
            }
        }

        void text (const std::string& got, const std::string& want, const std::string& what)
        {
            if (got != want)
            {
                ok = false;
                exact = false;
                why = what + " = \"" + got + "\", want \"" + want + "\"";
            }
        }
    };

    Outcome runCase (const JsonValue& c, const std::string& kind)
    {
        Outcome o;
        if (kind == "hash32")
        {
            const auto& args = c["args"];
            std::vector<std::int32_t> ints;
            for (std::size_t i = 0; i < args.size(); ++i)
                ints.push_back (core::toInt32 (fixture::num (args[i])));
            o.exactValue (core::hash32Ints (ints.data(), ints.size()), fixture::num (c["h"]), "hash32 " + core::toJson (args));
        }
        else if (kind == "toInt32")
        {
            const double x = fixture::num (c["x"]);
            o.exactValue (core::toInt32 (x), fixture::num (c["i"]), "toInt32 (" + str (x) + ")");
            o.exactValue (core::toUint32 (x), static_cast<double> (static_cast<std::uint32_t> (core::toInt32 (x))), "toUint32 (" + str (x) + ")");
        }
        else if (kind == "idHash")
        {
            const std::string& s = c["s"].asString();
            o.exactValue (core::idHash (std::string_view (s)), fixture::num (c["h"]), "idHash (\"" + s + "\")");
        }
        else if (kind == "rate")
        {
            const auto& id = c["id"];
            if (id.isString())
            {
                const std::string& code = id.asString();
                o.exactValue (core::rateBeats (std::string_view (code)), fixture::num (c["beats"]), "rateBeats (\"" + code + "\")");
                o.text (core::rateLabel (std::string_view (code)), c["label"].asString(), "rateLabel (\"" + code + "\")");
            }
            else
            {
                const double x = fixture::num (id);
                o.exactValue (core::rateBeatsOr (x), fixture::num (c["beats"]), "rateBeats (" + str (x) + ")");
                o.text (core::jsNumberToString (x), c["label"].asString(), "rateLabel (" + str (x) + ")");
            }
        }
        else if (kind == "rateFromBeats")
        {
            const double beats = fixture::num (c["beats"]);
            const auto& tol = c["tol"];
            const auto got = tol.isNull() ? core::rateFromBeats (beats) : core::rateFromBeats (beats, fixture::num (tol));
            const std::string gotText = got.has_value() ? core::rateCode (*got) : "null";
            const std::string wantText = c["id"].isNull() ? "null" : c["id"].asString();
            o.text (gotText, wantText, "rateFromBeats (" + str (beats) + ")");
        }
        else if (kind == "travelSpeed")
        {
            const double v = fixture::num (c["v"]);
            const double want = fixture::num (c["s"]);
            const double got = core::travelSpeed (v);
            o.relative (got, want, kCurveRel, "travelSpeed (" + str (v) + ")");
            if (fixture::same (got, 0.0) != fixture::same (want, 0.0))   // the dead-band decision, exact
            {
                o.ok = false;
                o.why = "travelSpeed (" + str (v) + "): the dead-band decision differs";
            }
        }
        else if (kind == "travelSpeedToV")
        {
            const double x = fixture::num (c["x"]);
            o.relative (core::travelSpeedToV (x), fixture::num (c["v"]), kCurveRel, "travelSpeedToV (" + str (x) + ")");
        }
        else if (kind == "constants")
        {
            o.exactValue (core::kF32Eps, fixture::num (c["F32_EPS"]), "F32_EPS");
            o.exactValue (core::kVFwdMin, fixture::num (c["V_FWD_MIN"]), "V_FWD_MIN");
            o.exactValue (core::kVBackMax, fixture::num (c["V_BACK_MAX"]), "V_BACK_MAX");
        }
        else if (kind == "bendShape")
        {
            const double u = fixture::num (c["u"]), b = fixture::num (c["bend"]);
            o.relative (core::bendShape (u, b), fixture::num (c["y"]), kCurveRel, "bendShape (" + str (u) + ", " + str (b) + ")");
        }
        else if (kind == "gShape")
        {
            const double u = fixture::num (c["u"]), k = fixture::num (c["c"]);
            o.relative (core::gShape (u, k), fixture::num (c["y"]), kCurveRel, "gShape (" + str (u) + ", " + str (k) + ")");
        }
        else if (kind == "jsNumber")
        {
            const double x = fixture::num (c["x"]);
            o.text (core::jsNumberToString (x), c["s"].asString(), "String (" + str (x) + ")");
        }
        else if (kind == "toFixed")
        {
            const double x = fixture::num (c["x"]);
            const int d = static_cast<int> (fixture::num (c["d"]));
            o.text (core::jsToFixed (x, d), c["s"].asString(), "toFixed (" + str (x) + ", " + std::to_string (d) + ")");
        }
        else if (kind == "toPrecision")
        {
            const double x = fixture::num (c["x"]);
            const int d = static_cast<int> (fixture::num (c["d"]));
            o.text (core::jsToPrecision (x, d), c["s"].asString(), "toPrecision (" + str (x) + ", " + std::to_string (d) + ")");
        }
        else if (kind == "base36")
        {
            const auto u = static_cast<std::uint32_t> (fixture::num (c["u"]));
            o.text (core::toBase36 (u), c["s"].asString(), "toBase36 (" + std::to_string (u) + ")");
        }
        else if (kind == "jsRound")
        {
            const double x = fixture::num (c["x"]);
            o.exactValue (core::jsRound (x), fixture::num (c["r"]), "jsRound (" + str (x) + ")");
        }
        else if (kind == "scale")
        {
            const int key = static_cast<int> (fixture::num (c["key"]));
            const int scale = core::findScale (c["scale"].asString());
            const auto& snap = c["snap"];
            std::string in;
            int snapsWrong = 0;
            for (int p = 0; p < 128; ++p)
            {
                in += core::inScale (p, key, scale) ? '1' : '0';
                if (! fixture::same (core::snapScale (p, key, scale), fixture::num (snap[static_cast<std::size_t> (p)])))
                    ++snapsWrong;
            }
            o.text (in, c["inScale"].asString(), "inScale on key " + std::to_string (key) + " " + c["scale"].asString());
            if (scale < 0 || snap.size() != 128 || snapsWrong != 0)
            {
                o.ok = false;
                o.exact = false;
                o.why = "snapScale on key " + std::to_string (key) + " " + c["scale"].asString() + ": " + std::to_string (snapsWrong) + " wrong";
            }
        }
        else if (kind == "noteName")
        {
            const double p = fixture::num (c["p"]);
            o.text (core::noteName (p), c["name"].asString(), "noteName (" + str (p) + ")");
        }
        else
        {
            o.ok = false;
            o.exact = false;
            o.why = "a case of kind \"" + kind + "\", which this test does not know";
        }
        return o;
    }
}

SL_TEST ("model", seedsFixture)
{
    section ("model/seeds.json: hashes, ids, rates, Travel, curves, JavaScript numbers, scales, note names");

    JsonValue doc;
    if (! fixture::load ("model/seeds.json", doc))
        return;

    const auto& cases = doc["cases"];
    modelfx::FileTally tally ("model/seeds.json", cases.size());
    struct Count
    {
        int ran = 0, bad = 0, exact = 0;
    };
    std::map<std::string, Count> kinds;

    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const std::string kind = cases[i]["kind"].asString();
        const Outcome o = runCase (cases[i], kind);
        auto& k = kinds[kind];
        ++k.ran;
        if (o.ok)
        {
            tally.pass (o.exact);
            k.exact += o.exact ? 1 : 0;
        }
        else
        {
            ++k.bad;
            tally.fail (o.why);
        }
    }

    for (const auto& [kind, k] : kinds)
        check (k.bad == 0, fmt ("seeds %s: %d of %d cases within tolerance, %d bit for bit", kind.c_str(), k.ran - k.bad, k.ran, k.exact));
    check (kinds.size() == 17, fmt ("seeds.json holds the 17 kinds the plan lists (%d)", static_cast<int> (kinds.size())));
    tally.finish();
}

SL_TEST ("model", travelConstants)
{
    section ("Travel: the dead band at Float32 precision (PLUGIN-PLAN 1.6 rule 3)");

    // kVFwdMin / kVBackMax are the first Float32 values outside the band, found by stepping the bit pattern.
    const float fwd = static_cast<float> (core::kVFwdMin), back = static_cast<float> (core::kVBackMax);
    check (fixture::same (static_cast<double> (fwd), core::kVFwdMin) && fixture::same (static_cast<double> (back), core::kVBackMax),
           "kVFwdMin and kVBackMax are Float32 values");
    check (core::kVFwdMin > 0.52 + core::kF32Eps && static_cast<double> (core::detail::f32Next (fwd, -1)) <= 0.52 + core::kF32Eps,
           fmt ("kVFwdMin %s is the first Float32 above 0.52 + 2e-8", str (core::kVFwdMin).c_str()));
    check (core::kVBackMax < 0.48 - core::kF32Eps && static_cast<double> (core::detail::f32Next (back, 1)) >= 0.48 - core::kF32Eps,
           fmt ("kVBackMax %s is the first Float32 below 0.48 - 2e-8", str (core::kVBackMax).c_str()));

    // Math.fround (0.48) lies 1.1e-8 below the edge and still plays stopped; the next Float32 down plays.
    const double edge = core::f32 (0.48);
    check (fixture::same (core::travelSpeed (edge), 0.0) && core::travelSpeed (core::kVBackMax) < 0.0 && core::travelSpeed (core::kVFwdMin) > 0.0,
           "Float32 (0.48) is stopped; the first values outside the band play");
    check (fixture::same (core::travelSpeedToV (0.125), core::kVFwdMin) && fixture::same (core::travelSpeedToV (-0.125), core::kVBackMax)
               && fixture::same (core::travelSpeedToV (0.0), 0.5),
           "travelSpeedToV (+-1/8) lands on the first Float32 values outside the band; 0 is 0.5");
    const double smoothEdge = 0.06 - core::kF32Eps;
    check (core::refreshSmooth (std::nextafter (smoothEdge, 0.0)) && ! core::refreshSmooth (smoothEdge) && ! core::refreshSmooth (core::f32 (0.06))
               && core::refreshSmooth (0.05),
           "Refresh Smooth is v < 0.06 - 2e-8: Float32 (0.06) is not Smooth");
}
