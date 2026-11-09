// model/tools.json (PLUGIN-PLAN 4 M1, 5.3): every selection tool (Connect nearest / furthest / random,
// Disconnect, Chop with its refusal, Walk, Humanize, Strum, Legato, More / Less, Reverse, Flip, Select
// similar), note settings, add / duplicate / remove / update and the generator edits. For each case: the notes
// the C++ made new (copy-on-write) are exactly the notes the prototype made new, in clip order, with ids, links,
// flags, integers, p, s and l exact and every other value within 1e-12; then the order of every id, and the
// refusal, count, ids or mixed values the case lists.

#include "ModelFixture.h"
#include "ToolOps.h"

#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    // the file's tolerance: ids, order, links, flags, integers, p, s and l exact, node values within 1e-12; it names no
    // other value, so vel, glide, gc, the Start position and generator depths are held to the node values' 1e-12
    constexpr double kTol = 1e-12;

    std::string labelOf (const JsonValue& c)
    {
        std::string s = c["op"].asString() + " " + c["clip"].asString() + " " + core::toJson (c["args"]);
        if (s.size() > 200)
            s = s.substr (0, 200) + "...";
        return s;
    }

    std::string joined (const std::vector<std::string>& ids)
    {
        std::string s;
        for (const auto& id : ids)
            s += (s.empty() ? "" : ",") + id;
        return s;
    }
}

SL_TEST ("model", toolsFixture)
{
    section ("model/tools.json: the selection tools, note settings and edits, the notes each makes new and the rest shared");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    const JsonValue& cases = doc["cases"];
    modelfx::FileTally tally ("model/tools.json", cases.size());
    std::map<std::string, int> perOp;
    int sharedChecked = 0;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const std::string label = labelOf (c);
        const auto it = clips.find (c["clip"].asString());
        if (it == clips.end())
        {
            tally.fail (label + ": no clip " + c["clip"].asString());
            continue;
        }
        const model::Clip& in = it->second;
        ++perOp[c["op"].asString()];

        const toolops::OpResult r = toolops::run (c, in);
        if (! r.ran)
        {
            tally.fail (label + ": " + r.why);
            continue;
        }

        const JsonValue& res = c["result"];
        bool exact = true, ok = true;
        std::string why;

        if (res.contains ("notes") || res.contains ("order"))
        {
            if (! r.clip.has_value())
            {
                ok = false;
                why = "the op returned no clip";
            }
            else
            {
                // the notes the C++ made new: every note of the result that is not one of the input's
                std::set<const model::Note*> input;
                for (const auto& n : in.notes)
                    input.insert (n.get());
                std::vector<model::NotePtr> made;
                for (const auto& n : r.clip->notes)
                    if (input.count (n.get()) == 0)
                        made.push_back (n);
                sharedChecked += static_cast<int> (r.clip->notes.size() - made.size());

                if (res.contains ("notes") && ! modelfx::clipNotesClose (made, res["notes"], kTol, kTol, exact, why))
                {
                    ok = false;
                    why = "new notes: " + why;
                }
                if (ok && res.contains ("order"))
                {
                    std::vector<std::string> order;
                    for (const auto& n : r.clip->notes)
                        order.push_back (n->id);
                    const auto want = modelfx::stringsFrom (res["order"]);
                    if (order != want)
                    {
                        ok = false;
                        why = "order " + joined (order) + ", want " + joined (want);
                    }
                }
            }
        }
        if (ok && res.contains ("refused") && (! r.refused.has_value() || *r.refused != res["refused"].asBool()))
        {
            ok = false;
            why = "refused differs";
        }
        if (ok && res.contains ("count") && (! r.count.has_value() || ! fixture::same (*r.count, res["count"].asNumber())))
        {
            ok = false;
            why = "count " + (r.count ? core::jsNumberToString (*r.count) : std::string ("none")) + ", want " + core::toJson (res["count"]);
        }
        if (ok && res.contains ("ids"))
        {
            const auto want = modelfx::stringsFrom (res["ids"]);
            if (! r.ids.has_value() || *r.ids != want)
            {
                ok = false;
                why = "ids " + (r.ids ? joined (*r.ids) : std::string ("none")) + ", want " + joined (want);
            }
        }
        if (ok && res.contains ("mixed"))
        {
            const JsonValue got = r.mixed.has_value() ? toolops::mixedToJson (*r.mixed) : JsonValue();
            if (! r.hasMixed || ! modelfx::jsonSame (got, res["mixed"], "mixed", why))
                ok = false;
        }

        if (ok)
            tally.pass (exact);
        else
            tally.fail (label + ": " + why);
    }

    std::string ops;
    for (const auto& [op, n] : perOp)
        ops += (ops.empty() ? "" : ", ") + op + " " + std::to_string (n);
    note ("cases per op: " + ops);
    note (fmt ("%d result notes were the input's own (shared, copy-on-write)", sharedChecked));
    check (perOp.size() == 21, fmt ("all 21 ops of tools.json were run (%d)", static_cast<int> (perOp.size())));
    tally.finish();
}
