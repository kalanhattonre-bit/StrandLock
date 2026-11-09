#include "Normalize.h"

#include "Generators.h"
#include "Num.h"
#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/Rates.h"
#include "core/Seeds.h"

#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

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

    //==============================================================================================
    NoteSample normalizeSample (NoteSample s) noexcept
    {
        if (s.motion.has_value() && static_cast<int> (*s.motion) >= static_cast<int> (std::size (core::kMotionCodes)))
            s.motion.reset();
        if (static_cast<int> (s.travel) >= static_cast<int> (std::size (kTravelKindCodes)))
            s.travel = TravelKind::speed;
        if (static_cast<int> (s.start.mode) >= static_cast<int> (std::size (kStartModeCodes)))
            s.start.mode = StartMode::region;
        s.start.pos = clamp (num (s.start.pos, 0.0), 0.0, 1.0);
        s.start.range = clamp (num (s.start.range, 0.25), 0.0, 1.0);
        if (s.jump.has_value() && ! core::isValidRate (*s.jump))
            s.jump.reset();
        if (s.engine.has_value() && static_cast<int> (*s.engine) >= static_cast<int> (std::size (core::kEngineCodes)))
            s.engine.reset();
        return s;
    }

    Note normalizeNote (const Note& n, int index)
    {
        Note out = n;   // a fresh serial; the extras pointer is shared
        out.p = clamp (num (n.p, 60.0), 0.0, 127.0);
        out.s = jsMax (0.0, num (n.s, 0.0));
        out.l = jsMax (1e-3, num (n.l, 1.0));
        if (out.id.empty())
            out.id = "n" + core::toBase36 (core::hash32 (index, core::jsRound (out.p * 100.0), core::jsRound (out.s * 1e6),
                                                         core::jsRound (out.l * 1e6)));
        out.vel = clamp (num (n.vel, 0.8), 0.0, 1.0);
        if (n.glide.has_value())
            out.glide = jsMax (0.0, num (*n.glide, 0.0));
        out.gc = clamp (num (n.gc, 0.0), -1.0, 1.0);
        for (int i = 0; i < core::kNumLanes; ++i)
        {
            const auto lane = static_cast<core::LaneId> (i);
            out.lane (lane) = normalizeNodes (n.lane (lane), core::laneDef (lane));
            auto& g = out.genOf (lane);
            if (g.has_value())
                g = normalizeGen (*g);
        }
        out.sample = normalizeSample (n.sample);
        return out;
    }

    Clip normalizeClip (const Clip& clip)
    {
        std::vector<std::shared_ptr<Note>> notes;
        notes.reserve (clip.notes.size());
        std::unordered_set<std::string> seen;
        for (std::size_t i = 0; i < clip.notes.size(); ++i)
        {
            auto q = std::make_shared<Note> (normalizeNote (*clip.notes[i], static_cast<int> (i)));
            if (seen.count (q->id) != 0)
            {
                int k = 1;
                while (seen.count (q->id + "~" + std::to_string (k)) != 0)
                    ++k;
                q->id = q->id + "~" + std::to_string (k);
            }
            seen.insert (q->id);
            notes.push_back (std::move (q));
        }
        for (auto& n : notes)
            if (! n->glideTo.empty() && (seen.count (n->glideTo) == 0 || n->glideTo == n->id))
                n->glideTo.clear();

        Clip out;
        out.bars = static_cast<int> (clamp (static_cast<double> (clip.bars), static_cast<double> (kMinBars), static_cast<double> (kMaxBars)));
        out.beatsPerBar = clip.beatsPerBar;
        out.offsetBeats = clip.offsetBeats;
        out.loop = clip.loop;
        out.tieAcrossLoop = clip.tieAcrossLoop;
        out.extras = clip.extras;
        out.notes.assign (notes.begin(), notes.end());
        return out;
    }

    namespace
    {
        // (typeof x === 'string' && x) || (typeof x === 'number' && String (x)) || '' (the caller's fallback)
        std::string idOrEmpty (const JsonValue& v)
        {
            if (v.isString())
                return v.asString();
            if (v.isNumber())
                return core::jsNumberToString (v.asNumber());
            return {};
        }
    }

    namespace
    {
        /** The note-like object read with JavaScript's coercions, before normalizeNote cleans it: a wrong-typed
            number is NaN (num () gives its default), a missing glide is null, an invalid id is empty. */
        Note rawNoteFromJson (const JsonValue& n)
        {
            // n = n || {}: a value that is not an object has none of the fields (operator[] reads null)
            Note raw;
            raw.id = idOrEmpty (n["id"]);
            raw.p = jsonNumber (n["p"]);
            raw.s = jsonNumber (n["s"]);
            raw.l = jsonNumber (n["l"]);
            raw.vel = jsonNumber (n["vel"]);
            if (const JsonValue& g = n["glide"]; ! g.isNull())   // n.glide == null: null or missing
                raw.glide = jsonNumber (g);
            raw.gc = jsonNumber (n["gc"]);
            raw.glideTo = idOrEmpty (n["glideTo"]);
            const JsonValue& c = n["c"];
            const JsonValue& gens = n["gen"];
            for (int i = 0; i < core::kNumLanes; ++i)
            {
                const auto lane = static_cast<core::LaneId> (i);
                raw.lane (lane) = nodesFromJson (c[core::kLanes[i].code]);
                raw.genOf (lane) = genFromJson (gens[core::kLanes[i].code]);
            }
            raw.sample = sampleFromJson (n["sample"]);
            return raw;
        }
    }

    Note noteFromJson (const JsonValue& n, int index)
    {
        return normalizeNote (rawNoteFromJson (n), index);
    }

    Clip clipFromJson (const JsonValue& data)
    {
        // Array.isArray (data) ? { notes: data } : (data || {})
        const JsonValue& notes = data.isArray() ? data : data["notes"];
        Clip raw;
        raw.bars = data.isArray() ? 4 : newClip (num (jsonNumber (data["bars"]), 4.0)).bars;
        if (notes.isArray())
            for (std::size_t i = 0; i < notes.size(); ++i)
                raw.notes.push_back (std::make_shared<const Note> (rawNoteFromJson (notes[i])));
        return normalizeClip (raw);
    }
}
