#include "ModelFixture.h"

#include "core/JsCompat.h"
#include "core/Lanes.h"
#include "model/Normalize.h"
#include "support/Check.h"
#include "support/FixtureJson.h"

#include <cmath>
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
