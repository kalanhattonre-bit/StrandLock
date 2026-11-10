#include "engine/EngineFixture.h"

#include "core/JsCompat.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "core/Rates.h"
#include "support/Check.h"
#include "support/FixtureJson.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

namespace sl::test::enginefx
{
    namespace
    {
        using core::JsonValue;

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

        std::string str (double x) { return core::jsNumberToString (x); }
    }

    core::Inst instFromEngineJson (const JsonValue& src)
    {
        core::Inst o = core::defaultInst();
        if (! src.isObject())
            return o;

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
