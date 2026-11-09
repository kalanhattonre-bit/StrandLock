#include "Normalize.h"

#include "Num.h"
#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/Rates.h"

#include <cmath>
#include <limits>

namespace sl::model
{
    using core::JsonValue;

    bool jsTruthy (const JsonValue& v) noexcept
    {
        switch (v.type())
        {
            case JsonValue::Type::null:    return false;
            case JsonValue::Type::boolean: return v.asBool();
            case JsonValue::Type::number:  return v.asNumber() != 0.0 && ! std::isnan (v.asNumber());
            case JsonValue::Type::string:  return ! v.asString().empty();
            case JsonValue::Type::array:
            case JsonValue::Type::object:  return true;
        }
        return false;
    }

    double jsonNumber (const JsonValue& v) noexcept
    {
        return v.isNumber() ? v.asNumber() : std::numeric_limits<double>::quiet_NaN();
    }

    namespace
    {
        // x === true || x === false ? x : null
        std::optional<bool> boolOrNull (const JsonValue& v) noexcept
        {
            if (v.isBool())
                return v.asBool();
            return std::nullopt;
        }

        // Math.trunc (num (x, 1)) | 0
        std::int32_t seedOf (const JsonValue& v) noexcept
        {
            return core::toInt32 (std::trunc (num (jsonNumber (v), 1.0)));
        }

        bool isString (const JsonValue& v, const char* s) noexcept
        {
            return v.isString() && v.asString() == s;
        }
    }

    NodeList nodesFromJson (const JsonValue& nodes)
    {
        NodeList out;
        if (! nodes.isArray())
            return out;

        out.reserve (nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            // q = q || {}: an element that is not an object has none of the fields (operator[] reads null)
            const JsonValue& q = nodes[i];
            const JsonValue& shape = q["shape"];
            std::optional<NodeShape> known;
            if (shape.isString())
                known = parseNodeShape (shape.asString());
            out.push_back (mkNode (jsonNumber (q["t"]), jsonNumber (q["v"]),
                                   known.has_value() ? *known : (jsTruthy (q["lin"]) ? NodeShape::linear : NodeShape::smooth),
                                   jsonNumber (q["bend"])));
        }
        return out;
    }

    std::optional<Gen> genFromJson (const JsonValue& g)
    {
        if (! g.isObject() && ! g.isArray())   // ! g || typeof g !== 'object'
            return std::nullopt;

        Gen out;
        const double d0 = clamp (num (jsonNumber (g["d0"]), 0.3), 0.0, 1.0);
        out.kind = isString (g["kind"], "random") ? GenKind::random : GenKind::pattern;

        const JsonValue& shape = g["shape"];
        const auto knownShape = shape.isString() ? parseGenShape (shape.asString()) : std::nullopt;
        out.shape = knownShape.value_or (GenShape::squiggle);

        const JsonValue& rate = g["rate"];
        const auto knownRate = rate.isString() ? core::parseRate (rate.asString()) : std::nullopt;
        out.rate = knownRate.value_or (core::RateId::r1_16T);   // BLUEPRINT 2.1 default

        out.d0 = d0;
        out.d1 = clamp (num (jsonNumber (g["d1"]), d0), 0.0, 1.0);
        out.warp = clamp (num (jsonNumber (g["warp"]), 0.0), -0.9, 3.0);
        out.lockToBeat = ! (g["lockToBeat"].isBool() && ! g["lockToBeat"].asBool());   // !== false
        out.amount = clamp (num (jsonNumber (g["amount"]), 0.5), 0.0, 1.0);
        out.smooth = clamp (num (jsonNumber (g["smooth"]), 0.0), 0.0, 1.0);
        out.perNote = isString (g["perNote"], "different") ? PerNote::different : PerNote::same;
        out.seed = seedOf (g["seed"]);
        out.invert = g["invert"].isBool() && g["invert"].asBool();   // === true
        const JsonValue& key = g["key"];
        out.key = key.isString() ? key.asString() : std::string();   // '' reads as null
        return out;
    }

    NoteSample sampleFromJson (const JsonValue& s)
    {
        // s = s || {}, st = s.start || {}: a value that is not an object has none of the fields
        const JsonValue& st = s["start"];
        NoteSample out;

        const JsonValue& motion = s["motion"];
        out.motion = motion.isString() ? core::parseMotion (motion.asString()) : std::nullopt;
        out.travel = isString (s["travel"], "path") ? TravelKind::path : TravelKind::speed;

        const JsonValue& mode = st["mode"];
        const auto knownMode = mode.isString() ? parseStartMode (mode.asString()) : std::nullopt;
        out.start.mode = knownMode.value_or (StartMode::region);
        out.start.pos = clamp (num (jsonNumber (st["pos"]), 0.0), 0.0, 1.0);
        out.start.range = clamp (num (jsonNumber (st["range"]), 0.25), 0.0, 1.0);
        out.start.seed = seedOf (st["seed"]);

        out.tape = boolOrNull (s["tape"]);
        out.rev = boolOrNull (s["rev"]);
        const JsonValue& jump = s["jump"];
        out.jump = jump.isString() ? core::parseRate (jump.asString()) : std::nullopt;
        const JsonValue& engine = s["engine"];
        out.engine = engine.isString() ? core::parseEngine (engine.asString()) : std::nullopt;
        out.lock = boolOrNull (s["lock"]);
        out.restart = jsTruthy (s["restart"]);
        return out;
    }
}
