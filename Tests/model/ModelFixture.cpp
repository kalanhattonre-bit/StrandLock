#include "ModelFixture.h"

#include "core/JsCompat.h"
#include "core/Lanes.h"
#include "model/Normalize.h"
#include "support/Check.h"
#include "support/FixtureJson.h"

#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

namespace sl::test::modelfx
{
    using core::JsonValue;

    bool nodesFrom (const JsonValue& tuples, model::NodeList& out, std::string& why)
    {
        out.clear();
        if (! tuples.isArray())
        {
            why = "nodes are not an array";
            return false;
        }
        for (std::size_t i = 0; i < tuples.size(); ++i)
        {
            const JsonValue& q = tuples[i];
            if (! q.isArray() || q.size() != 4 || ! fixture::isNum (q[0]) || ! fixture::isNum (q[1]) || ! q[2].isNumber() || ! fixture::isNum (q[3]))
            {
                why = "node " + std::to_string (i) + " is not [t, v, shape, bend]";
                return false;
            }
            const double shape = q[2].asNumber();
            if (! (shape >= 0.0 && shape <= 3.0) || ! fixture::same (std::floor (shape), shape))
            {
                why = "node " + std::to_string (i) + " has shape code " + core::jsNumberToString (shape);
                return false;
            }
            out.push_back (model::mkNode (fixture::num (q[0]), fixture::num (q[1]), static_cast<model::NodeShape> (static_cast<int> (shape)),
                                          fixture::num (q[3])));
        }
        return true;
    }

    bool noteFrom (const JsonValue& v, model::Note& out, std::string& why)
    {
        if (! v.isObject() || ! v["id"].isString() || ! v["p"].isNumber() || ! v["s"].isNumber() || ! v["l"].isNumber())
        {
            why = "a note needs id, p, s and l";
            return false;
        }
        out = model::mkNote (v["p"].asNumber(), v["s"].asNumber(), v["l"].asNumber(), v["id"].asString());
        out.vel = fixture::num (v["vel"]);
        if (v["glide"].isNull())
            out.glide.reset();
        else
            out.glide = fixture::num (v["glide"]);
        out.gc = fixture::num (v["gc"]);
        out.glideTo = v["glideTo"].isString() ? v["glideTo"].asString() : std::string();
        out.sample = model::sampleFromJson (v["sample"]);

        if (const auto* c = v.find ("c"))
        {
            for (std::size_t i = 0; i < c->size(); ++i)
            {
                const auto lane = core::findLane (c->keyAt (i));
                if (! lane.has_value())
                {
                    why = "unknown lane " + c->keyAt (i);
                    return false;
                }
                if (! nodesFrom (c->valueAt (i), out.lane (*lane), why))
                    return false;
            }
        }
        if (const auto* g = v.find ("gen"))
        {
            for (std::size_t i = 0; i < g->size(); ++i)
            {
                const auto lane = core::findLane (g->keyAt (i));
                if (! lane.has_value())
                {
                    why = "unknown generator lane " + g->keyAt (i);
                    return false;
                }
                out.genOf (*lane) = model::genFromJson (g->valueAt (i));
            }
        }
        return true;
    }

    bool clipFrom (const JsonValue& v, model::Clip& out, std::string& why)
    {
        if (! v.isObject() || ! v["bars"].isNumber() || ! v["notes"].isArray())
        {
            why = "a clip needs bars and notes";
            return false;
        }
        out = model::Clip {};
        out.bars = static_cast<int> (v["bars"].asNumber());
        const JsonValue& notes = v["notes"];
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            model::Note n;
            if (! noteFrom (notes[i], n, why))
            {
                why = "note " + std::to_string (i) + ": " + why;
                return false;
            }
            out.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
        }
        return true;
    }

    bool clipsFrom (const JsonValue& doc, std::map<std::string, model::Clip>& out)
    {
        out.clear();
        const JsonValue& clips = doc["clips"];
        bool ok = clips.isObject();
        for (std::size_t i = 0; i < clips.size(); ++i)
        {
            std::string why;
            model::Clip clip;
            if (! clipFrom (clips.valueAt (i), clip, why))
            {
                check (false, "fixture clip " + clips.keyAt (i) + " reads: " + why);
                ok = false;
                continue;
            }
            out.emplace (clips.keyAt (i), std::move (clip));
        }
        return ok;
    }

    const model::Note* findNote (const model::Clip& clip, const std::string& id) noexcept
    {
        for (const auto& n : clip.notes)
            if (n->id == id)
                return n.get();
        return nullptr;
    }

    std::string nodesText (const model::NodeList& nodes)
    {
        std::string s = "[";
        for (std::size_t i = 0; i < nodes.size() && i < 8; ++i)
        {
            if (i > 0)
                s += ",";
            s += "[" + core::jsNumberToString (nodes[i].t) + "," + core::jsNumberToString (nodes[i].v) + ","
               + std::to_string (static_cast<int> (nodes[i].shape)) + "," + core::jsNumberToString (nodes[i].bend) + "]";
        }
        if (nodes.size() > 8)
            s += ",... (" + std::to_string (nodes.size()) + " nodes)";
        return s + "]";
    }

    bool nodesMatch (const model::NodeList& got, const JsonValue& want, double tol, bool& bitExact, std::string& why)
    {
        model::NodeList expected;
        if (! nodesFrom (want, expected, why))
            return false;
        if (got.size() != expected.size())
        {
            why = "got " + std::to_string (got.size()) + " nodes, want " + std::to_string (expected.size()) + ": got " + nodesText (got)
                + " want " + nodesText (expected);
            bitExact = false;
            return false;
        }
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            const auto& a = got[i];
            const auto& b = expected[i];
            if (a.shape != b.shape)
            {
                why = "node " + std::to_string (i) + " shape " + std::to_string (static_cast<int> (a.shape)) + ", want "
                    + std::to_string (static_cast<int> (b.shape));
                bitExact = false;
                return false;
            }
            if (! fixture::same (a.t, b.t) || ! fixture::same (a.v, b.v) || ! fixture::same (a.bend, b.bend))
                bitExact = false;
            if (! fixture::withinAbs (a.t, b.t, tol) || ! fixture::withinAbs (a.v, b.v, tol) || ! fixture::withinAbs (a.bend, b.bend, tol))
            {
                why = "node " + std::to_string (i) + " is [" + core::jsNumberToString (a.t) + "," + core::jsNumberToString (a.v) + ","
                    + core::jsNumberToString (a.bend) + "], want [" + core::jsNumberToString (b.t) + "," + core::jsNumberToString (b.v)
                    + "," + core::jsNumberToString (b.bend) + "]";
                return false;
            }
        }
        return true;
    }

    bool tableMatch (const std::vector<float>& got, const JsonValue& want, std::uint64_t maxUlps, bool& bitExact, std::string& why)
    {
        std::vector<float> expected;
        if (! fixture::table (want, expected))
        {
            why = "the expected table cannot be read";
            return false;
        }
        if (got.size() != expected.size())
        {
            why = "table length " + std::to_string (got.size()) + ", want " + std::to_string (expected.size());
            bitExact = false;
            return false;
        }
        std::uint64_t worst = 0;
        std::size_t worstAt = 0;
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            const std::uint64_t d = fixture::ulps (got[i], expected[i]);
            if (d > worst)
            {
                worst = d;
                worstAt = i;
            }
        }
        if (worst > 0)
            bitExact = false;
        if (worst > maxUlps)
        {
            why = "table value " + std::to_string (worstAt) + " is " + core::jsNumberToString (got[worstAt]) + ", want "
                + core::jsNumberToString (expected[worstAt]) + " (" + std::to_string (worst) + " ulps)";
            return false;
        }
        return true;
    }

    //==============================================================================================
    namespace
    {
        /** Compares fields one by one and keeps the first difference. */
        struct FieldCompare
        {
            double tol;
            bool& bitExact;
            std::string& why;
            bool ok = true;

            void fail (const std::string& what)
            {
                if (ok)
                    why = what;
                ok = false;
            }

            void number (const std::string& what, double a, double b, bool exactField)
            {
                if (! fixture::same (a, b))
                    bitExact = false;
                if (! (exactField ? fixture::same (a, b) : fixture::withinAbs (a, b, tol)))
                    fail (what + " is " + core::jsNumberToString (a) + ", want " + core::jsNumberToString (b));
            }

            void text (const std::string& what, const std::string& a, const std::string& b)
            {
                if (a != b)
                {
                    bitExact = false;
                    fail (what + " is '" + a + "', want '" + b + "'");
                }
            }

            void flag (const std::string& what, bool a, bool b)
            {
                text (what, a ? "true" : "false", b ? "true" : "false");
            }

            void nodes (const std::string& what, const model::NodeList& a, const model::NodeList& b)
            {
                if (a.size() != b.size())
                {
                    bitExact = false;
                    fail (what + ": " + std::to_string (a.size()) + " nodes, want " + std::to_string (b.size()) + ": got " + nodesText (a)
                          + " want " + nodesText (b));
                    return;
                }
                for (std::size_t i = 0; i < a.size() && ok; ++i)
                {
                    const std::string at = what + " node " + std::to_string (i);
                    text (at + " shape", model::code (a[i].shape), model::code (b[i].shape));
                    number (at + " t", a[i].t, b[i].t, false);
                    number (at + " v", a[i].v, b[i].v, false);
                    number (at + " bend", a[i].bend, b[i].bend, false);
                }
            }
        };

        template <class E>
        std::string optCode (const std::optional<E>& v)
        {
            return v.has_value() ? std::string (code (*v)) : std::string ("null");
        }

        std::string optRate (const std::optional<core::RateId>& v)
        {
            return v.has_value() ? std::string (core::rateCode (*v)) : std::string ("null");
        }

        std::string optBool (const std::optional<bool>& v)
        {
            return v.has_value() ? std::string (*v ? "true" : "false") : std::string ("null");
        }
    }

    bool notesClose (const model::Note& got, const model::Note& want, double tol, bool& bitExact, std::string& why)
    {
        using core::code;
        using model::code;

        FieldCompare f { tol, bitExact, why };
        f.text ("id", got.id, want.id);
        f.number ("p", got.p, want.p, true);
        f.number ("s", got.s, want.s, true);
        f.number ("l", got.l, want.l, true);
        f.number ("vel", got.vel, want.vel, false);
        if (got.glide.has_value() != want.glide.has_value())
            f.text ("glide", got.glide ? core::jsNumberToString (*got.glide) : "null", want.glide ? core::jsNumberToString (*want.glide) : "null");
        else if (got.glide.has_value())
            f.number ("glide", *got.glide, *want.glide, false);
        f.number ("gc", got.gc, want.gc, false);
        f.text ("glideTo", got.glideTo, want.glideTo);

        const model::NoteSample& a = got.sample;
        const model::NoteSample& b = want.sample;
        f.text ("sample.motion", optCode (a.motion), optCode (b.motion));
        f.text ("sample.travel", code (a.travel), code (b.travel));
        f.text ("sample.start.mode", code (a.start.mode), code (b.start.mode));
        f.number ("sample.start.pos", a.start.pos, b.start.pos, false);
        f.number ("sample.start.range", a.start.range, b.start.range, false);
        f.number ("sample.start.seed", a.start.seed, b.start.seed, true);
        f.text ("sample.tape", optBool (a.tape), optBool (b.tape));
        f.text ("sample.rev", optBool (a.rev), optBool (b.rev));
        f.text ("sample.jump", optRate (a.jump), optRate (b.jump));
        f.text ("sample.engine", optCode (a.engine), optCode (b.engine));
        f.text ("sample.lock", optBool (a.lock), optBool (b.lock));
        f.flag ("sample.restart", a.restart, b.restart);

        for (int i = 0; i < core::kNumLanes && f.ok; ++i)
        {
            const auto lane = static_cast<core::LaneId> (i);
            const std::string name = core::kLanes[i].code;
            f.nodes ("c." + name, model::noteLane (got, lane), model::noteLane (want, lane));
            const auto& ga = got.genOf (lane);
            const auto& gb = want.genOf (lane);
            if (ga.has_value() != gb.has_value())
            {
                f.text ("gen." + name, ga ? "a generator" : "null", gb ? "a generator" : "null");
                continue;
            }
            if (! ga.has_value())
                continue;
            const std::string g = "gen." + name + ".";
            f.text (g + "kind", code (ga->kind), code (gb->kind));
            f.text (g + "shape", code (ga->shape), code (gb->shape));
            f.text (g + "rate", core::rateCode (ga->rate), core::rateCode (gb->rate));
            f.number (g + "d0", ga->d0, gb->d0, false);
            f.number (g + "d1", ga->d1, gb->d1, false);
            f.number (g + "warp", ga->warp, gb->warp, false);
            f.flag (g + "lockToBeat", ga->lockToBeat, gb->lockToBeat);
            f.number (g + "amount", ga->amount, gb->amount, false);
            f.number (g + "smooth", ga->smooth, gb->smooth, false);
            f.text (g + "perNote", code (ga->perNote), code (gb->perNote));
            f.number (g + "seed", ga->seed, gb->seed, true);
            f.flag (g + "invert", ga->invert, gb->invert);
            f.text (g + "key", ga->key, gb->key);
        }
        if (! f.ok)
            why = got.id + ": " + why;
        return f.ok;
    }

    bool clipNotesClose (const std::vector<model::NotePtr>& got, const JsonValue& want, double tol, bool& bitExact, std::string& why)
    {
        if (! want.isArray() || got.size() != want.size())
        {
            bitExact = false;
            why = "got " + std::to_string (got.size()) + " notes, want " + std::to_string (want.size());
            return false;
        }
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            model::Note expected;
            if (! noteFrom (want[i], expected, why))
            {
                why = "fixture note " + std::to_string (i) + ": " + why;
                return false;
            }
            if (! notesClose (*got[i], expected, tol, bitExact, why))
                return false;
        }
        return true;
    }

    namespace
    {
        bool bits (double a, double b) noexcept
        {
            return std::memcmp (&a, &b, sizeof (double)) == 0;
        }

        bool bitsNodes (const model::NodeList& a, const model::NodeList& b) noexcept
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (! bits (a[i].t, b[i].t) || ! bits (a[i].v, b[i].v) || a[i].shape != b[i].shape || ! bits (a[i].bend, b[i].bend))
                    return false;
            return true;
        }

        bool bitsGen (const model::Gen& a, const model::Gen& b) noexcept
        {
            return a.kind == b.kind && a.shape == b.shape && a.rate == b.rate && bits (a.d0, b.d0) && bits (a.d1, b.d1) && bits (a.warp, b.warp)
                && a.lockToBeat == b.lockToBeat && bits (a.amount, b.amount) && bits (a.smooth, b.smooth) && a.perNote == b.perNote
                && a.seed == b.seed && a.invert == b.invert && a.key == b.key;
        }
    }

    bool bitIdentical (const model::Note& a, const model::Note& b, std::string& why)
    {
        auto differ = [&why, &a] (const std::string& what) { why = a.id + ": " + what + " differs"; return false; };
        if (a.id != b.id)
            return differ ("id");
        if (! bits (a.p, b.p) || ! bits (a.s, b.s) || ! bits (a.l, b.l) || ! bits (a.vel, b.vel) || ! bits (a.gc, b.gc))
            return differ ("p, s, l, vel or gc");
        if (a.glide.has_value() != b.glide.has_value() || (a.glide.has_value() && ! bits (*a.glide, *b.glide)))
            return differ ("glide");
        if (a.glideTo != b.glideTo)
            return differ ("glideTo");
        const auto& x = a.sample;
        const auto& y = b.sample;
        if (x.motion != y.motion || x.travel != y.travel || x.start.mode != y.start.mode || ! bits (x.start.pos, y.start.pos)
            || ! bits (x.start.range, y.start.range) || x.start.seed != y.start.seed || x.tape != y.tape || x.rev != y.rev || x.jump != y.jump
            || x.engine != y.engine || x.lock != y.lock || x.restart != y.restart)
            return differ ("the sample settings");
        for (std::size_t i = 0; i < a.c.size(); ++i)
        {
            if (! bitsNodes (a.c[i], b.c[i]))
                return differ (std::string ("lane ") + core::kLanes[i].code);
            if (a.gen[i].has_value() != b.gen[i].has_value() || (a.gen[i].has_value() && ! bitsGen (*a.gen[i], *b.gen[i])))
                return differ (std::string ("generator ") + core::kLanes[i].code);
        }
        if (a.extras != b.extras)
            return differ ("extras");
        return true;
    }

    model::Ids idsFrom (const JsonValue& v)
    {
        if (v.isString())
            return model::Ids (v.asString());
        if (v.isArray())
            return model::Ids (stringsFrom (v));
        return model::Ids::all();
    }

    std::vector<std::string> stringsFrom (const JsonValue& v)
    {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < v.size(); ++i)
            out.push_back (v[i].isString() ? v[i].asString() : core::toJson (v[i]));
        return out;
    }

    bool jsonSame (const JsonValue& a, const JsonValue& b, const std::string& path, std::string& why)
    {
        if (a.type() != b.type())
        {
            why = path + ": " + core::toJson (a) + " vs " + core::toJson (b);
            return false;
        }
        switch (a.type())
        {
            case JsonValue::Type::null:
                return true;
            case JsonValue::Type::boolean:
            case JsonValue::Type::string:
                if (core::toJson (a) != core::toJson (b))
                {
                    why = path + ": " + core::toJson (a) + " vs " + core::toJson (b);
                    return false;
                }
                return true;
            case JsonValue::Type::number:
                if (! fixture::same (a.asNumber(), b.asNumber()))
                {
                    why = path + ": " + core::toJson (a) + " vs " + core::toJson (b);
                    return false;
                }
                return true;
            case JsonValue::Type::array:
                if (a.size() != b.size())
                {
                    why = path + ": " + std::to_string (a.size()) + " vs " + std::to_string (b.size()) + " elements";
                    return false;
                }
                for (std::size_t i = 0; i < a.size(); ++i)
                    if (! jsonSame (a[i], b[i], path + "[" + std::to_string (i) + "]", why))
                        return false;
                return true;
            case JsonValue::Type::object:
                if (a.size() != b.size())
                {
                    why = path + ": " + std::to_string (a.size()) + " vs " + std::to_string (b.size()) + " keys";
                    return false;
                }
                for (std::size_t i = 0; i < a.size(); ++i)
                {
                    const JsonValue* other = b.find (a.keyAt (i));
                    if (other == nullptr)
                    {
                        why = path + "." + a.keyAt (i) + " is missing";
                        return false;
                    }
                    if (! jsonSame (a.valueAt (i), *other, path + "." + a.keyAt (i), why))
                        return false;
                }
                return true;
        }
        return false;
    }

    //==============================================================================================
    FileTally::FileTally (std::string fileName, std::size_t casesInFile)
        : file (std::move (fileName)), inFile (casesInFile)
    {
    }

    void FileTally::pass (bool bitExact)
    {
        ++passed;
        if (bitExact)
            ++exact;
    }

    void FileTally::fail (const std::string& detail)
    {
        if (failed < 8)
            note ("outside tolerance: " + detail);
        ++failed;
    }

    void FileTally::finish()
    {
        const int total = passed + failed;
        note (fmt ("%s: %d cases run (the file has %d), %d bit for bit, %d outside tolerance",
                   file.c_str(), total, static_cast<int> (inFile), exact, failed));
        check (total > 0 && static_cast<std::size_t> (total) == inFile,
               fmt ("%s: every case was run (%d of %d)", file.c_str(), total, static_cast<int> (inFile)));
        check (failed == 0 && total > 0,
               fmt ("%s: %d of %d cases within the plan's tolerance (%d bit for bit)", file.c_str(), passed, total, exact));
    }
}
