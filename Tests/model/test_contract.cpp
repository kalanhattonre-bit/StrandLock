// The scaffold's contract checks (PLUGIN-PLAN 7.1): the shared definitions M0 and M1 both build on,
// against the prototype's own answers in Tests/fixtures. Group "contract". M1 may extend this file.

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "rt/SpscFifo.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    /** Counts the cases of one kind, prints the first few that differ, then makes one check for them. */
    struct Cases
    {
        int total = 0;
        int bad = 0;

        void fail (const std::string& detail)
        {
            if (bad < 5)
                note ("differs: " + detail);
            ++bad;
        }

        void finish (const std::string& what) const
        {
            check (total > 0 && bad == 0, fmt ("%s: %d of %d cases match", what.c_str(), total - bad, total));
        }
    };

    constexpr std::uint32_t kFifoItems = 200000;   // items the two-thread SpscFifo check sends

    std::string str (double x) { return core::jsNumberToString (x); }

    std::string quoted (const std::string& s) { return "\"" + s + "\""; }

    //==============================================================================================
    // An instrument from a fixture's JSON, with the coercions JavaScript applies before normInst sees
    // the values. The typed Inst cannot hold a string where a number belongs, so this is where a JSON
    // value turns into one: a non-number becomes NaN (the engine's fin () then takes the default), an
    // unknown choice string becomes a value outside its list (clampForEngine then takes the engine's
    // fallback), a bool field takes JavaScript truthiness (`!!x`), and seed is `fin (x, 1) | 0`, the
    // only way an int32 can hold it.

    bool truthy (const JsonValue& v)
    {
        switch (v.type())
        {
            case JsonValue::Type::null:    return false;
            case JsonValue::Type::boolean: return v.asBool();
            case JsonValue::Type::number:  return ! std::isnan (v.asNumber()) && ! fixture::same (v.asNumber(), 0.0);
            case JsonValue::Type::string:  return ! v.asString().empty();
            case JsonValue::Type::array:
            case JsonValue::Type::object:  return true;
        }
        return false;
    }

    template <class E>
    E choiceFrom (const JsonValue& v, std::optional<E> (*parse) (std::string_view) noexcept)
    {
        if (v.isString())
            if (const auto e = parse (v.asString()))
                return *e;
        return static_cast<E> (200);
    }

    core::RateId rateFrom (const JsonValue& v)
    {
        if (v.isString())
            if (const auto r = core::parseRate (v.asString()))
                return *r;
        return static_cast<core::RateId> (200);
    }

    std::int8_t modTargetFrom (const JsonValue& v)
    {
        if (v.isString() && v.asString() == "off")
            return core::kModOff;
        const int lane = v.isString() ? core::laneIndex (v.asString()) : -1;
        return lane >= 0 ? static_cast<std::int8_t> (lane) : static_cast<std::int8_t> (99);
    }

    void readModWheel (const JsonValue& v, core::ModWheel& m)
    {
        if (! v.isObject())
            return;   // Object.assign ({}, default, x || {}): anything but an object leaves the default
        if (const auto* t = v.find ("target"))
            m.target = modTargetFrom (*t);
        if (const auto* c = v.find ("cc"))
            if (std::isfinite (fixture::num (*c)))
                m.cc = static_cast<int> (fixture::num (*c));
    }

    core::Inst instFromCase (const JsonValue& src)
    {
        core::Inst o = core::defaultInst();
        if (! src.isObject())
            return o;   // normInst (null) and normInst (undefined) are the defaults

        for (std::size_t i = 0; i < src.size(); ++i)
        {
            const std::string& k = src.keyAt (i);
            const JsonValue& v = src.valueAt (i);

            if (k == "engine")            o.engine = choiceFrom (v, core::parseEngine);
            else if (k == "lock")         o.lock = truthy (v);
            else if (k == "motion")       o.motion = choiceFrom (v, core::parseMotion);
            else if (k == "speed")        o.speed = fixture::num (v);
            else if (k == "heads")        o.heads = fixture::num (v);
            else if (k == "spread")       o.spread = fixture::num (v);
            else if (k == "grainVoice")   o.grainVoice = fixture::num (v);
            else if (k == "grainCloud")   o.grainCloud = fixture::num (v);
            else if (k == "grainLock")    o.grainLock = fixture::num (v);
            else if (k == "spray")        o.spray = fixture::num (v);
            else if (k == "pitch")        o.pitch = fixture::num (v);
            else if (k == "tape")         o.tape = truthy (v);
            else if (k == "rev")          o.rev = truthy (v);
            else if (k == "drift")        o.drift = fixture::num (v);
            else if (k == "headTune")     o.headTune = choiceFrom (v, core::parseHeadTune);
            else if (k == "jump")         o.jump = rateFrom (v);
            else if (k == "glideMode")    o.glideMode = choiceFrom (v, core::parseGlideMode);
            else if (k == "glideTime")    o.glideTime = fixture::num (v);
            else if (k == "vibRate")      o.vibRate = fixture::num (v);
            else if (k == "root")         o.root = fixture::num (v);
            else if (k == "cents")        o.cents = fixture::num (v);
            else if (k == "key")          o.key = static_cast<int> (std::isfinite (fixture::num (v)) ? fixture::num (v) : 0.0);
            else if (k == "scaleLock")    o.scaleLock = truthy (v);
            else if (k == "velSens")      o.velSens = fixture::num (v);
            else if (k == "polyphony")    o.polyphony = fixture::num (v);
            else if (k == "outGain")      o.outGain = fixture::num (v);
            else if (k == "bendRange")    o.bendRange = fixture::num (v);
            else if (k == "liveLegato")   o.liveLegato = truthy (v);
            else if (k == "seed")         o.seed = std::isfinite (fixture::num (v)) ? core::toInt32 (fixture::num (v)) : 1;
            else if (k == "mod1")         readModWheel (v, o.mod1);
            else if (k == "mod2")         readModWheel (v, o.mod2);
            else if (k == "scale")
            {
                const int s = v.isString() ? core::findScale (v.asString()) : -1;
                o.scale = static_cast<std::uint8_t> (s >= 0 ? s : 200);
            }
            else if (k == "macros")
            {
                if (v.isArray())   // Array.isArray (s.macros) ? s.macros : d.macros, then m[i] for i < 8
                    for (std::size_t m = 0; m < o.macros.size(); ++m)
                        o.macros[m] = m < v.size() ? fixture::num (v[m]) : std::numeric_limits<double>::quiet_NaN();
            }
            else if (k == "refresh" && v.isObject())
            {
                for (std::size_t j = 0; j < v.size(); ++j)
                {
                    const std::string& rk = v.keyAt (j);
                    const JsonValue& rv = v.valueAt (j);
                    if (rk == "mode")             o.refresh.mode = choiceFrom (rv, core::parseRefreshMode);
                    else if (rk == "ms")          o.refresh.ms = fixture::num (rv);
                    else if (rk == "rate")        o.refresh.rate = rateFrom (rv);
                    else if (rk == "lockToBeat")  o.refresh.lockToBeat = ! (rv.isBool() && ! rv.asBool());   // `!== false`
                    else if (rk == "skip")        o.refresh.skip = fixture::num (rv);
                }
            }
            else if (k == "region" && v.isObject())
            {
                for (std::size_t j = 0; j < v.size(); ++j)
                {
                    const std::string& rk = v.keyAt (j);
                    const JsonValue& rv = v.valueAt (j);
                    if (rk == "start")            o.region.start = fixture::num (rv);
                    else if (rk == "end")         o.region.end = fixture::num (rv);
                    else if (rk == "atEnd")       o.region.atEnd = choiceFrom (rv, core::parseAtEnd);
                    else if (rk == "xfadeMs")     o.region.xfadeMs = fixture::num (rv);
                }
            }
            else if (k == "offsets" && v.isObject())
            {
                for (std::size_t j = 0; j < v.size(); ++j)
                {
                    const std::string& ok = v.keyAt (j);
                    const double x = fixture::num (v.valueAt (j));
                    if (ok == "amp")              o.offsets.amp = x;
                    else if (ok == "vib")         o.offsets.vib = x;
                    else if (ok == "tone")        o.offsets.tone = x;
                    else if (ok == "formant")     o.offsets.formant = x;
                    else if (ok == "hollow")      o.offsets.hollow = x;
                }
            }
        }
        return o;
    }

    /** The fields where a and b differ, as text; empty when they are the same field for field. */
    std::string instDiff (const core::Inst& a, const core::Inst& b)
    {
        std::string d;
        auto num = [&d] (const char* name, double x, double y)
        {
            if (! fixture::same (x, y))
                d += fmt (" %s %s/%s", name, str (x).c_str(), str (y).c_str());
        };
        auto whole = [&d] (const char* name, long long x, long long y)
        {
            if (x != y)
                d += fmt (" %s %lld/%lld", name, x, y);
        };

        whole ("engine", (long long) a.engine, (long long) b.engine);
        whole ("lock", a.lock, b.lock);
        whole ("motion", (long long) a.motion, (long long) b.motion);
        num ("speed", a.speed, b.speed);
        num ("heads", a.heads, b.heads);
        num ("spread", a.spread, b.spread);
        num ("grainVoice", a.grainVoice, b.grainVoice);
        num ("grainCloud", a.grainCloud, b.grainCloud);
        num ("grainLock", a.grainLock, b.grainLock);
        whole ("refresh.mode", (long long) a.refresh.mode, (long long) b.refresh.mode);
        num ("refresh.ms", a.refresh.ms, b.refresh.ms);
        whole ("refresh.rate", (long long) a.refresh.rate, (long long) b.refresh.rate);
        whole ("refresh.lockToBeat", a.refresh.lockToBeat, b.refresh.lockToBeat);
        num ("refresh.skip", a.refresh.skip, b.refresh.skip);
        num ("spray", a.spray, b.spray);
        num ("pitch", a.pitch, b.pitch);
        whole ("tape", a.tape, b.tape);
        whole ("rev", a.rev, b.rev);
        num ("drift", a.drift, b.drift);
        whole ("headTune", (long long) a.headTune, (long long) b.headTune);
        num ("region.start", a.region.start, b.region.start);
        num ("region.end", a.region.end, b.region.end);
        whole ("region.atEnd", (long long) a.region.atEnd, (long long) b.region.atEnd);
        num ("region.xfadeMs", a.region.xfadeMs, b.region.xfadeMs);
        whole ("jump", (long long) a.jump, (long long) b.jump);
        for (std::size_t i = 0; i < a.macros.size(); ++i)
            num (fmt ("macros[%d]", (int) i).c_str(), a.macros[i], b.macros[i]);
        num ("offsets.amp", a.offsets.amp, b.offsets.amp);
        num ("offsets.vib", a.offsets.vib, b.offsets.vib);
        num ("offsets.tone", a.offsets.tone, b.offsets.tone);
        num ("offsets.formant", a.offsets.formant, b.offsets.formant);
        num ("offsets.hollow", a.offsets.hollow, b.offsets.hollow);
        whole ("glideMode", (long long) a.glideMode, (long long) b.glideMode);
        num ("glideTime", a.glideTime, b.glideTime);
        num ("vibRate", a.vibRate, b.vibRate);
        num ("root", a.root, b.root);
        num ("cents", a.cents, b.cents);
        whole ("key", a.key, b.key);
        whole ("scale", a.scale, b.scale);
        whole ("scaleLock", a.scaleLock, b.scaleLock);
        num ("velSens", a.velSens, b.velSens);
        num ("polyphony", a.polyphony, b.polyphony);
        num ("outGain", a.outGain, b.outGain);
        num ("bendRange", a.bendRange, b.bendRange);
        whole ("mod1.target", a.mod1.target, b.mod1.target);
        whole ("mod1.cc", a.mod1.cc, b.mod1.cc);
        whole ("mod2.target", a.mod2.target, b.mod2.target);
        whole ("mod2.cc", a.mod2.cc, b.mod2.cc);
        whole ("liveLegato", a.liveLegato, b.liveLegato);
        whole ("seed", a.seed, b.seed);
        whole ("vibSync", a.vibSync, b.vibSync);
        return d;
    }
}

//==================================================================================================
SL_TEST ("contract", jsCompat)
{
    section ("JsCompat: toInt32, toUint32, Math.round, toFixed, toPrecision, String (x) and Math.fround");

    JsonValue doc;
    if (! fixture::load ("model/seeds.json", doc))
        return;

    Cases ints;
    for (const auto* c : fixture::casesOfKind (doc, "toInt32"))
    {
        ++ints.total;
        const double x = fixture::num ((*c)["x"]);
        const auto want = static_cast<std::int32_t> (fixture::num ((*c)["i"]));
        const auto got = core::toInt32 (x);
        const auto gotU = core::toUint32 (x);
        if (got != want || gotU != static_cast<std::uint32_t> (want))
            ints.fail (fmt ("toInt32 (%s) = %d, toUint32 = %u; want %d", str (x).c_str(), got, gotU, want));
    }
    ints.finish ("toInt32 and toUint32 (`x | 0`, `x >>> 0`)");

    Cases rounds;
    for (const auto* c : fixture::casesOfKind (doc, "jsRound"))
    {
        ++rounds.total;
        const double x = fixture::num ((*c)["x"]);
        const double want = fixture::num ((*c)["r"]);
        const double got = core::jsRound (x);
        if (! fixture::same (got, want))
            rounds.fail (fmt ("jsRound (%s) = %s, want %s", str (x).c_str(), str (got).c_str(), str (want).c_str()));
    }
    rounds.finish ("jsRound (Math.round)");

    auto stringCases = [&doc] (const char* kind, const char* label, auto&& make)
    {
        Cases cases;
        for (const auto* c : fixture::casesOfKind (doc, kind))
        {
            ++cases.total;
            const double x = fixture::num ((*c)["x"]);
            const auto& d = (*c)["d"];
            const std::string want = (*c)["s"].asString();
            const std::string got = make (x, static_cast<int> (d.asNumber()));
            if (got != want)
                cases.fail (fmt ("%s (%s, %d) = %s, want %s", label, str (x).c_str(), (int) d.asNumber(), quoted (got).c_str(), quoted (want).c_str()));
        }
        cases.finish (label);
    };

    stringCases ("toFixed", "jsToFixed", [] (double x, int d) { return core::jsToFixed (x, d); });
    stringCases ("toPrecision", "jsToPrecision", [] (double x, int d) { return core::jsToPrecision (x, d); });
    stringCases ("jsNumber", "jsNumberToString (String (x))", [] (double x, int) { return core::jsNumberToString (x); });

    // Math.fround: no fixture kind holds it, so these are the prototype's own values (node: Math.fround).
    const double overflow = 3.4028235677973366e38;   // 2^128 - 2^103, half way from FLT_MAX to 2^128
    const double fltMax = 3.4028234663852886e38;
    check (fixture::same (core::f32 (0.1), 0.10000000149011612) && fixture::same (core::f32 (0.48), 0.47999998927116394),
           fmt ("f32 (0.1) = %s, f32 (0.48) = %s (Math.fround: 0.10000000149011612, 0.47999998927116394)",
                str (core::f32 (0.1)).c_str(), str (core::f32 (0.48)).c_str()));
    check (std::isinf (core::f32 (overflow)) && core::f32 (overflow) > 0.0 && std::isinf (core::f32 (-overflow)) && core::f32 (-overflow) < 0.0,
           "f32 of 2^128 - 2^103 is an infinity, with its sign (round to nearest, ties to even)");
    check (fixture::same (core::f32 (std::nextafter (overflow, 0.0)), fltMax) && fixture::same (core::f32 (fltMax), fltMax),
           fmt ("f32 just below that is FLT_MAX (%s)", str (core::f32 (std::nextafter (overflow, 0.0))).c_str()));
    check (fixture::same (core::f32 (1e-46), 0.0) && std::signbit (core::f32 (-1e-46)) && std::isnan (core::f32 (std::nan (""))),
           "f32 (1e-46) is 0, f32 (-1e-46) is -0, f32 (NaN) is NaN");
}

SL_TEST ("contract", seeds)
{
    section ("Seeds: hash32, idHash and rand01 against model/seeds.json");

    JsonValue doc;
    if (! fixture::load ("model/seeds.json", doc))
        return;

    // hash32 over each case's arguments, every one through toInt32 (some lie outside int32, a few are
    // fractional: plan 1.6 rule 4).
    Cases h;
    std::map<std::string, std::uint32_t> byArgs;
    for (const auto* c : fixture::casesOfKind (doc, "hash32"))
    {
        ++h.total;
        const auto& args = (*c)["args"];
        std::vector<std::int32_t> ints;
        for (std::size_t i = 0; i < args.size(); ++i)
            ints.push_back (core::toInt32 (fixture::num (args[i])));
        const auto want = static_cast<std::uint32_t> (fixture::num ((*c)["h"]));
        const auto got = core::hash32Ints (ints.data(), ints.size());
        byArgs[core::toJson (args)] = want;
        if (got != want)
            h.fail (fmt ("hash32 %s = %u, want %u", core::toJson (args).c_str(), got, want));
    }
    h.finish ("hash32 (MurmurHash3 x86_32 over int32 arguments)");

    // The variadic form takes any numbers and gives the same hash.
    const bool haveArgs = byArgs.count ("[]") != 0 && byArgs.count ("[1,2]") != 0 && byArgs.count ("[53710,1,0,1]") != 0
                       && byArgs.count ("[4294967295]") != 0 && byArgs.count ("[-1]") != 0;
    check (haveArgs, "the fixture has the hash32 cases [], [1,2], [53710,1,0,1], [4294967295] and [-1]");
    if (haveArgs)
    {
        check (core::hash32() == byArgs["[]"], fmt ("hash32 () = %u", core::hash32()));
        check (core::hash32 (1, 2) == byArgs["[1,2]"] && core::hash32 (1.9, 2.0) == byArgs["[1,2]"]
                   && core::hash32 (std::int64_t (4294967297), 2u) == byArgs["[1,2]"],
               "hash32 (1, 2) = hash32 (1.9, 2.0) = hash32 (int64 4294967297, 2u): each argument is `x | 0`");
        check (core::hash32 (53710, 1, 0, 1) == byArgs["[53710,1,0,1]"], "hash32 (53710, 1, 0, 1) as the fixture");
        check (core::hash32 (4294967295.0) == byArgs["[4294967295]"] && core::hash32 (-1) == byArgs["[-1]"]
                   && byArgs["[4294967295]"] == byArgs["[-1]"],
               "hash32 (4294967295.0) = hash32 (-1): the argument wraps to int32");
    }

    check (fixture::same (core::rand01 (0u), 0.0) && fixture::same (core::rand01 (2147483648u), 0.5)
               && fixture::same (core::rand01 (4294967295u), 4294967295.0 / 4294967296.0),
           fmt ("rand01: 0 -> 0, 2^31 -> 0.5, 2^32 - 1 -> %s", str (core::rand01 (4294967295u)).c_str()));

    // idHash over UTF-16 code units, from UTF-8 and from UTF-16 (non-ASCII and surrogate pairs included).
    Cases ids;
    for (const auto* c : fixture::casesOfKind (doc, "idHash"))
    {
        ++ids.total;
        const std::string& s = (*c)["s"].asString();
        const auto want = static_cast<std::uint32_t> (fixture::num ((*c)["h"]));
        std::u16string units;
        core::forEachUtf16Unit (s, [&units] (std::uint32_t u) { units.push_back (static_cast<char16_t> (u)); });
        const auto fromUtf8 = core::idHash (std::string_view (s));
        const auto fromUtf16 = core::idHash (std::u16string_view (units));
        if (fromUtf8 != want || fromUtf16 != want)
            ids.fail (fmt ("idHash (%s) = %u (UTF-8) / %u (UTF-16), want %u", quoted (s).c_str(), fromUtf8, fromUtf16, want));
    }
    ids.finish ("idHash (FNV-1a over UTF-16 code units)");
}

SL_TEST ("contract", rates)
{
    section ("Rates: the 21 ids, their beats and labels, unknown ids, rateFromBeats");

    JsonValue doc;
    if (! fixture::load ("model/seeds.json", doc))
        return;

    const auto rateCases = fixture::casesOfKind (doc, "rate");
    check (rateCases.size() >= (std::size_t) core::kNumRates, fmt ("the fixture lists the rates (%d cases)", (int) rateCases.size()));
    if (rateCases.size() < (std::size_t) core::kNumRates)
        return;

    // The first 21 cases are SLModel.RATES in order: the parameters' choice order.
    Cases order;
    for (int i = 0; i < core::kNumRates; ++i)
    {
        ++order.total;
        const auto& c = *rateCases[(std::size_t) i];
        const auto id = static_cast<core::RateId> (i);
        if (c["id"].asString() != core::rateCode (id) || ! fixture::same (core::rateBeats (id), fixture::num (c["beats"]))
            || c["label"].asString() != core::rateLabel (id) || core::parseRate (c["id"].asString()) != id)
            order.fail (fmt ("rate %d: %s %s %s, want %s %s %s", i, core::rateCode (id), str (core::rateBeats (id)).c_str(),
                             core::rateLabel (id), c["id"].asString().c_str(), str (fixture::num (c["beats"])).c_str(),
                             c["label"].asString().c_str()));
    }
    order.finish ("the 21 rates in SLModel.RATES order, with beats and labels");

    // The rest: unknown strings (0.25 beats, labelled as themselves) and numbers (beats when > 0).
    Cases others;
    for (std::size_t i = (std::size_t) core::kNumRates; i < rateCases.size(); ++i)
    {
        ++others.total;
        const auto& c = *rateCases[i];
        const double wantBeats = fixture::num (c["beats"]);
        const std::string& wantLabel = c["label"].asString();
        if (c["id"].isString())
        {
            const std::string& id = c["id"].asString();
            if (core::parseRate (id).has_value() || ! fixture::same (core::rateBeats (std::string_view (id)), wantBeats) || core::rateLabel (std::string_view (id)) != wantLabel)
                others.fail (fmt ("rate %s: beats %s label %s", quoted (id).c_str(), str (core::rateBeats (std::string_view (id))).c_str(),
                                  quoted (core::rateLabel (std::string_view (id))).c_str()));
        }
        else
        {
            const double x = fixture::num (c["id"]);
            if (! fixture::same (core::rateBeatsOr (x), wantBeats) || core::jsNumberToString (x) != wantLabel)
                others.fail (fmt ("rate %s: beats %s", str (x).c_str(), str (core::rateBeatsOr (x)).c_str()));
        }
    }
    others.finish ("unknown rate ids read 1/16 and label as themselves; numbers pass through when positive");

    Cases nearest;
    for (const auto* c : fixture::casesOfKind (doc, "rateFromBeats"))
    {
        ++nearest.total;
        const double beats = fixture::num ((*c)["beats"]);
        const auto& tol = (*c)["tol"];
        const auto got = tol.isNull() ? core::rateFromBeats (beats) : core::rateFromBeats (beats, fixture::num (tol));
        const auto& want = (*c)["id"];
        const bool ok = want.isNull() ? ! got.has_value() : (got.has_value() && want.asString() == core::rateCode (*got));
        if (! ok)
            nearest.fail (fmt ("rateFromBeats (%s, %s) = %s, want %s", str (beats).c_str(), tol.isNull() ? "default" : str (fixture::num (tol)).c_str(),
                               got ? core::rateCode (*got) : "null", want.isNull() ? "null" : want.asString().c_str()));
    }
    nearest.finish ("rateFromBeats");
}

SL_TEST ("contract", lanesAndModes)
{
    section ("Lanes: the registry order and defaults the engine and the generators hash");

    JsonValue doc;
    if (! fixture::load ("engine/defs.json", doc))
        return;

    const auto defs = fixture::casesOfKind (doc, "defs");
    check (defs.size() == 1, fmt ("engine/defs.json has its defs case (%d)", (int) defs.size()));
    if (defs.size() != 1)
        return;

    const auto& ids = (*defs[0])["LANE_IDS"];
    const auto& defaults = (*defs[0])["LANE_DEF"];
    Cases lanes;
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        ++lanes.total;
        const std::string& code = ids[i].asString();
        const bool inRange = i < (std::size_t) core::kNumLanes;
        if (! inRange || code != core::kLanes[i].code || ! fixture::same (core::kLanes[i].def, fixture::num (defaults[i]))
            || core::laneIndex (code) != (int) i || core::findLane (code) != core::kLanes[i].id
            || static_cast<std::size_t> (core::laneIndex (core::kLanes[i].id)) != i || ! fixture::same (core::laneDef (code), core::kLanes[i].def))
            lanes.fail (fmt ("lane %d: %s", (int) i, code.c_str()));
    }
    check (ids.size() == (std::size_t) core::kNumLanes, fmt ("%d lanes (the engine has %d)", core::kNumLanes, (int) ids.size()));
    lanes.finish ("lane ids, indices and defaults (engine LANE_IDS, LANE_DEF)");

    check (core::laneIndex ("nope") == -1 && ! core::findLane ("nope").has_value() && fixture::same (core::laneDef ("nope"), 0.5),
           "an unknown lane id has index -1, no LaneId, and default 0.5 (SLModel.laneDef)");

    // SLModel.MODES: 1 Build, then each lane that is not a Sample sub-lane, then 8 Sample with the four.
    const char* names[8] = { "Build", "Pitch", "Volume", "Vibrato", "Tone", "Formant", "Hollow", "Sample" };
    int modeErrors = 0;
    for (int m = 0; m < 8; ++m)
    {
        const auto& mode = core::kModes[m];
        bool ok = mode.mode == m + 1 && mode.key == (char) ('1' + m) && std::string (mode.name) == names[m];
        if (m == 0)
            ok = ok && mode.numLanes == 0;
        else if (m < 7)
            ok = ok && mode.numLanes == 1 && mode.lanes[0] == core::kLanes[m - 1].id && ! core::kLanes[m - 1].sub
                    && core::kLanes[m - 1].mode == m + 1 && std::string (core::kLanes[m - 1].name) == names[m];
        else
            for (int j = 0; j < 4; ++j)
                ok = ok && mode.numLanes == 4 && mode.lanes[j] == core::kSampleLanes[j] && core::laneInfo (core::kSampleLanes[j]).sub;
        modeErrors += ok ? 0 : 1;
    }
    check (modeErrors == 0, fmt ("the 8 modes, their keys, names and lanes (%d wrong)", modeErrors));
}

SL_TEST ("contract", music)
{
    section ("Music: note names (60 = C3) and the 8 scales");

    JsonValue doc;
    if (! fixture::load ("model/seeds.json", doc))
        return;

    Cases names;
    for (const auto* c : fixture::casesOfKind (doc, "noteName"))
    {
        ++names.total;
        const double p = fixture::num ((*c)["p"]);
        const std::string got = core::noteName (p);
        if (got != (*c)["name"].asString())
            names.fail (fmt ("noteName (%s) = %s, want %s", str (p).c_str(), got.c_str(), (*c)["name"].asString().c_str()));
    }
    names.finish ("noteName");

    Cases scales;
    for (const auto* c : fixture::casesOfKind (doc, "scale"))
    {
        ++scales.total;
        const int key = static_cast<int> (fixture::num ((*c)["key"]));
        const int scale = core::findScale ((*c)["scale"].asString());
        const std::string& in = (*c)["inScale"].asString();
        const auto& snap = (*c)["snap"];

        std::string gotIn;
        int snapErrors = 0;
        for (int p = 0; p < 128; ++p)
        {
            gotIn += core::inScale (p, key, scale) ? '1' : '0';
            if (! fixture::same (core::snapScale (p, key, scale), fixture::num (snap[(std::size_t) p])))
                ++snapErrors;
        }
        if (scale < 0 || gotIn != in || snap.size() != 128 || snapErrors != 0)
            scales.fail (fmt ("key %d %s: inScale %s, %d snaps wrong", key, (*c)["scale"].asString().c_str(), gotIn == in ? "right" : "wrong", snapErrors));
    }
    scales.finish ("inScale and snapScale over MIDI 0..127, every key and scale");
}

SL_TEST ("contract", instClamps)
{
    section ("Inst: defaultInst and clampForEngine against the engine's normInst (engine/defs.json)");

    JsonValue doc;
    if (! fixture::load ("engine/defs.json", doc))
        return;

    const auto cases = fixture::casesOfKind (doc, "normInst");
    check (cases.size() >= 5, fmt ("engine/defs.json has the normInst cases (%d)", (int) cases.size()));
    if (cases.empty())
        return;

    // normInst ({}) is the engine's defaultInst (), which equals SLModel's.
    const auto defaults = core::defaultInst();
    const std::string defaultDiff = instDiff (defaults, instFromCase ((*cases[0])["result"]));
    check ((*cases[0])["input"].isObject() && (*cases[0])["input"].size() == 0 && defaultDiff.empty(),
           "defaultInst () is normInst ({}) field for field" + (defaultDiff.empty() ? std::string() : ":" + defaultDiff));
    check (defaults.vibSync == core::kVibSyncFree, "vibSync, the plugin's addition, defaults to Free (the prototype)");

    const std::string idempotent = instDiff (core::clampForEngine (defaults), defaults);
    check (idempotent.empty(), "clampForEngine leaves the defaults unchanged" + idempotent);

    Cases clamps;
    for (const auto* c : cases)
    {
        ++clamps.total;
        const auto got = core::clampForEngine (instFromCase ((*c)["input"]));
        const auto want = instFromCase ((*c)["result"]);
        const std::string diff = instDiff (got, want);
        if (! diff.empty())
            clamps.fail (core::toJson ((*c)["input"]) + " -> (got/want)" + diff);
    }
    clamps.finish ("clampForEngine (engine normInst: clamps, non-finite defaults, Region swap, unknown choices)");

    // Values a typed Inst can hold that the fixture's JSON cannot: a choice outside its list.
    core::Inst odd = defaults;
    odd.engine = static_cast<core::Engine> (9);
    odd.jump = static_cast<core::RateId> (40);
    odd.refresh.rate = static_cast<core::RateId> (21);
    odd.glideMode = static_cast<core::GlideMode> (3);
    odd.vibSync = 21;
    odd.scale = 8;
    odd.mod1.target = 10;
    odd.mod2.target = -5;
    const auto fixed = core::clampForEngine (odd);
    check (fixed.engine == core::Engine::voice && fixed.jump == core::RateId::r1_16 && fixed.refresh.rate == core::RateId::r1_16
               && fixed.glideMode == core::GlideMode::legato && fixed.vibSync == core::kVibSyncFree && fixed.scale == 0
               && fixed.mod1.target == core::kModOff && fixed.mod2.target == core::kModOff,
           "choices outside their lists read as the engine's fallbacks (voice, 1/16, legato, Free, Chromatic, off)");
}

SL_TEST ("contract", jsonRoundTrip)
{
    section ("JsonLite: every fixture parses, and every case writes back byte for byte (JSON.stringify)");

    const auto files = fixture::listFiles();
    check (files.size() >= 20, fmt ("%d fixture files under %s", (int) files.size(), fixture::dir().c_str()));

    for (const auto& rel : files)
    {
        std::string text;
        if (! fixture::readText (fixture::dir() + "/" + rel, text))
        {
            check (false, rel + " can be read");
            continue;
        }

        JsonValue doc;
        core::JsonError error;
        if (! core::parseJson (text, doc, &error))
        {
            check (false, fmt ("%s parses (%s at byte %zu)", rel.c_str(), error.message.c_str(), error.offset));
            continue;
        }

        std::vector<std::string_view> lines;
        for (std::size_t start = 0; start < text.size();)
        {
            const std::size_t end = std::min (text.find ('\n', start), text.size());
            lines.push_back (std::string_view (text).substr (start, end - start));
            start = end + 1;
        }

        const auto& cases = doc["cases"];
        if (cases.size() == 0)
        {
            // A prototype save: the whole file is one JSON.stringify line.
            const std::string again = core::toJson (doc);
            check (! lines.empty() && again == lines[0], fmt ("%s: the whole document writes back as its one line (%d bytes)", rel.c_str(), (int) again.size()));
            continue;
        }

        // Case lines start with '{' (the clips and insts tables' lines start with a key); the cases are the last of them.
        std::vector<std::string_view> caseLines;
        for (std::size_t i = 1; i < lines.size(); ++i)
        {
            auto line = lines[i];
            if (line.empty() || line[0] != '{')
                continue;
            if (line.back() == ',')
                line.remove_suffix (1);
            caseLines.push_back (line);
        }

        if (caseLines.size() < cases.size())
        {
            check (false, fmt ("%s: %d case lines for %d cases", rel.c_str(), (int) caseLines.size(), (int) cases.size()));
            continue;
        }

        const std::size_t first = caseLines.size() - cases.size();
        int bad = 0;
        for (std::size_t i = 0; i < cases.size(); ++i)
        {
            const std::string again = core::toJson (cases[i]);
            const auto want = caseLines[first + i];
            if (again == want)
                continue;
            if (bad < 2)
            {
                std::size_t at = 0;
                while (at < again.size() && at < want.size() && again[at] == want[at])
                    ++at;
                const std::size_t from = at > 30 ? at - 30 : 0;
                note (fmt ("%s case %d differs at byte %d: ...%s... vs ...%s...", rel.c_str(), (int) i, (int) at,
                           again.substr (from, 70).c_str(), std::string (want.substr (from, 70)).c_str()));
            }
            ++bad;
        }
        check (bad == 0, fmt ("%s: %d of %d cases write back byte for byte", rel.c_str(), (int) cases.size() - bad, (int) cases.size()));
    }
}

SL_TEST ("contract", jsonEdges)
{
    section ("JsonLite: what JSON.parse refuses, escapes, numbers, duplicate keys");

    const char* refused[] = { "", " ", "{", "}", "[1,]", "[1 2]", "{\"a\":1,}", "{a:1}", "{\"a\" 1}", "01", "1.", ".5", "-", "+1",
                              "1e", "1e+", "\"abc", "\"a\nb\"", "\"\\x\"", "\"\\u12\"", "\"\\u12g4\"", "tru", "nul", "falsy", "[1]x",
                              "NaN", "Infinity", "-Infinity", "[\"a\" \"b\"]", "{\"a\":}", "{,}", "[,1]" };
    int accepted = 0;
    for (const char* text : refused)
    {
        JsonValue v;
        if (core::parseJson (text, v))
        {
            note (fmt ("accepted: %s", quoted (text).c_str()));
            ++accepted;
        }
    }
    check (accepted == 0, fmt ("%d malformed texts refused (%d accepted)", (int) std::size (refused), accepted));

    // Accepted texts and what JSON.stringify writes for them (values from node).
    struct Good { const char* text; const char* stringified; };
    const Good good[] = {
        { "0", "0" }, { "-0", "0" }, { " [ ] ", "[]" }, { "{}", "{}" }, { "1E5", "100000" }, { "1e+21", "1e+21" }, { "1e21", "1e+21" },
        { "123.456e-2", "1.23456" }, { "0.1", "0.1" }, { "1e-7", "1e-7" }, { "5e-324", "5e-324" }, { "1.7976931348623157e308", "1.7976931348623157e+308" },
        { "1e400", "null" }, { "-1e400", "null" }, { "1e-400", "0" }, { "4294967296", "4294967296" }, { "[true,false,null]", "[true,false,null]" },
        { "\"\\u00e9\"", "\"\xc3\xa9\"" }, { "\"\\ud83d\\ude00\"", "\"\xf0\x9f\x98\x80\"" }, { "\"\\ud800\"", "\"\\ud800\"" },
        { "\"\\udc00\\ud800\"", "\"\\udc00\\ud800\"" }, { "\"\\u0001\\u001f\\u007f\"", "\"\\u0001\\u001f\x7f\"" },
        { "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", "\"\\\"\\\\/\\b\\f\\n\\r\\t\"" }, { "\"\xe6\x97\xa5\"", "\"\xe6\x97\xa5\"" },
        { "{\"a\":1,\"b\":2,\"a\":3}", "{\"a\":3,\"b\":2}" }, { "{ \"x\" : [ 1 , { \"y\" : \"z\" } ] }", "{\"x\":[1,{\"y\":\"z\"}]}" },
    };
    int wrong = 0;
    for (const auto& g : good)
    {
        JsonValue v;
        core::JsonError error;
        const bool parsed = core::parseJson (g.text, v, &error);
        const std::string out = parsed ? core::toJson (v) : std::string ("(refused: ") + error.message + ")";
        if (out != g.stringified)
        {
            note (fmt ("%s -> %s, want %s", quoted (g.text).c_str(), out.c_str(), g.stringified));
            ++wrong;
        }
    }
    check (wrong == 0, fmt ("%d texts parse and write back as JSON.stringify does (%d wrong)", (int) std::size (good), wrong));

    // Numbers come back bit for bit; -0 stays -0 inside (it only prints as 0).
    JsonValue n;
    const bool parsedNumbers = core::parseJson ("[-0,0.1,2.2250738585072011e-308,4.9406564584124654e-324,9007199254740993]", n);
    check (parsedNumbers && n.size() == 5 && std::signbit (n[0].asNumber()) && fixture::same (n[1].asNumber(), 0.1)
               && fixture::same (n[2].asNumber(), 2.2250738585072011e-308) && fixture::same (n[3].asNumber(), 4.9406564584124654e-324)
               && fixture::same (n[4].asNumber(), 9007199254740992.0),
           "numbers parse to the nearest double (-0, 0.1, the largest subnormal, the smallest, 2^53 + 1 -> 2^53)");

    // Nesting deeper than kMaxJsonDepth is refused instead of running out of stack.
    const std::string deepOk = std::string (100, '[') + std::string (100, ']');
    const std::string tooDeep = std::string (5000, '[') + std::string (5000, ']');
    JsonValue d;
    check (core::parseJson (deepOk, d) && ! core::parseJson (tooDeep, d), fmt ("100 levels parse, 5000 are refused (limit %d)", core::kMaxJsonDepth));

    // Building and lookup.
    JsonValue o;
    o.set ("b", 1);
    o.set ("a", JsonValue::array());
    o.set ("b", "two");
    JsonValue arr;
    arr.push (true);
    arr.push (nullptr);
    arr.push (2.5);
    o.set ("c", arr);
    check (core::toJson (o) == "{\"b\":\"two\",\"a\":[],\"c\":[true,null,2.5]}" && o["b"].asString() == "two" && o["c"][2].asNumber() > 2.0
               && o["missing"].isNull() && o["c"][7].isNull() && o.keyAt (1) == "a",
           "set keeps the first position of a key, push appends, a missing member or index reads as null");
}

SL_TEST ("contract", spscFifo)
{
    section ("rt::SpscFifo: fills to its capacity, keeps order, wraps, and hands items from one thread to another");

    sl::rt::SpscFifo<int, 8> f;
    int pushed = 0;
    for (int i = 0; i < 20; ++i)
        if (f.push (i))
            ++pushed;
    check (pushed == 8 && f.size() == 8 && sl::rt::SpscFifo<int, 8>::capacity() == 8,
           fmt ("an 8-slot ring takes 8 items and refuses the 9th (%d taken, size %d)", pushed, (int) f.size()));

    int value = -1, inOrder = 0;
    for (int i = 0; i < 8; ++i)
        if (f.pop (value) && value == i)
            ++inOrder;
    value = -1;
    const bool emptyPop = f.pop (value);
    check (inOrder == 8 && ! emptyPop && value == -1 && f.empty(), fmt ("they come out in order (%d of 8), then pop fails and leaves its argument", inOrder));

    int wrapErrors = 0;
    for (int round = 0; round < 1000; ++round)
    {
        for (int j = 0; j < 5; ++j)
            wrapErrors += f.push (round * 5 + j) ? 0 : 1;
        for (int j = 0; j < 5; ++j)
            wrapErrors += (f.pop (value) && value == round * 5 + j) ? 0 : 1;
    }
    check (wrapErrors == 0, fmt ("5000 items through it in rounds of 5, across the wrap: %d errors", wrapErrors));

    // One producer thread, one consumer (this one): every item arrives once, in order.
    sl::rt::SpscFifo<std::uint32_t, 64> q;
    std::thread producer ([&q]
    {
        for (std::uint32_t i = 0; i < kFifoItems;)
        {
            if (q.push (i))
                ++i;
            else
                std::this_thread::yield();
        }
    });

    std::uint32_t expected = 0, mismatches = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (60);
    while (expected < kFifoItems && std::chrono::steady_clock::now() < deadline)
    {
        std::uint32_t item = 0;
        if (q.pop (item))
        {
            mismatches += item == expected ? 0u : 1u;
            ++expected;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();
    check (expected == kFifoItems && mismatches == 0,
           fmt ("%u items from another thread: %u received, %u out of order", kFifoItems, expected, mismatches));
}
