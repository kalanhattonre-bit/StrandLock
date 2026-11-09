// model/generators.json (PLUGIN-PLAN 4 M1, 5.3): Pattern and Random generators turned into lane tables
// (7 shapes x 5 rates incl. triplets and dotted, lock to beat on and off, warp, smooth, Same / Different,
// key, invert, the pitch lane's 1/6 scale), the seeded value of every random step, Re-roll and Turn into
// nodes. Tables within 2 Float32 ulps; steps exact; reroll seeds exact; baked nodes within 1e-12.

#include "ModelFixture.h"

#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    constexpr std::uint64_t kTableUlps = 2;
    constexpr double kNodeTol = 1e-12;

    model::Ids idsFrom (const JsonValue& v)
    {
        if (v.isNull())
            return model::Ids::all();
        std::vector<std::string> list;
        for (std::size_t i = 0; i < v.size(); ++i)
            list.push_back (v[i].asString());
        return model::Ids (std::move (list));
    }

    std::optional<core::LaneId> laneOrAll (const JsonValue& v) { return v.isNull() ? std::nullopt : core::findLane (v.asString()); }

    /** Every note of out that is not in ids must be the input's own NotePtr (copy-on-write), and every one in ids a new one. */
    bool sharedAsPrototype (const model::Clip& in, const model::Clip& out, const model::Ids& ids, std::string& why)
    {
        if (in.notes.size() != out.notes.size())
        {
            why = "the note count changed";
            return false;
        }
        const auto set = model::idSet (in, ids);
        for (std::size_t i = 0; i < in.notes.size(); ++i)
        {
            const bool edited = set.count (in.notes[i]->id) != 0;
            if (edited == (in.notes[i] == out.notes[i]))
            {
                why = "note " + in.notes[i]->id + (edited ? " was not copied" : " was copied but not edited");
                return false;
            }
        }
        return true;
    }
}

SL_TEST ("model", generatorsFixture)
{
    section ("model/generators.json: lane tables, random steps, Re-roll, Turn into nodes");

    JsonValue doc;
    if (! fixture::load ("model/generators.json", doc))
        return;

    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    const auto& cases = doc["cases"];
    modelfx::FileTally tally ("model/generators.json", cases.size());
    long long tableValues = 0, tableSame = 0, stepsChecked = 0;
    std::map<std::string, int> ops;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        bool exact = true;
        std::string why;
        const std::string op = c["op"].isString() ? c["op"].asString() : "table";
        ++ops[op];

        if (op == "table")
        {
            model::Note n;
            const auto lane = core::findLane (c["lane"].asString());
            if (! modelfx::noteFrom (c["note"], n, why) || ! lane.has_value())
            {
                tally.fail ("case " + std::to_string (ci) + ": " + (why.empty() ? "unknown lane" : why));
                continue;
            }
            const std::string label = n.id + " on " + c["lane"].asString();
            const auto table = model::laneTable (n, *lane, fixture::num (c["res"]));
            {
                std::vector<float> want;
                if (fixture::table (c["table"], want) && want.size() == table.size())
                    for (std::size_t j = 0; j < table.size(); ++j, ++tableValues)
                        tableSame += fixture::ulps (table[j], want[j]) == 0 ? 1 : 0;
            }
            if (! modelfx::tableMatch (table, c["table"], kTableUlps, exact, why))
            {
                tally.fail (label + ": laneTable: " + why);
                continue;
            }

            // the seeded random value of every step, exactly (plan 1.7)
            if (const auto* steps = c.find ("steps"))
            {
                const auto& g = n.genOf (*lane);
                if (! g.has_value())
                {
                    tally.fail (label + ": the note has no generator");
                    continue;
                }
                const model::GenEval ev (model::normalizeGen (*g), n, *lane);
                const double k0 = fixture::num (c["k0"]);
                bool stepsOk = steps->size() > 0;
                for (std::size_t i = 0; i < steps->size() && stepsOk; ++i, ++stepsChecked)
                {
                    const double k = k0 + static_cast<double> (i);
                    const double got = ev.randomStep (k), want = fixture::num ((*steps)[i]);
                    if (! fixture::same (got, want))
                    {
                        why = fmt ("step %s: %s, want %s", core::jsNumberToString (k).c_str(), core::jsNumberToString (got).c_str(),
                                   core::jsNumberToString (want).c_str());
                        stepsOk = false;
                    }
                }
                if (! stepsOk)
                {
                    tally.fail (label + ": random steps: " + why);
                    continue;
                }
            }
            tally.pass (exact);
        }
        else if (op == "reroll")
        {
            const auto it = clips.find (c["clip"].asString());
            if (it == clips.end())
            {
                tally.fail ("reroll: unknown clip " + c["clip"].asString());
                continue;
            }
            const model::Clip& in = it->second;
            const model::Ids ids = idsFrom (c["ids"]);
            const model::Clip out = model::reroll (in, ids, laneOrAll (c["lane"]));
            const JsonValue& after = c["after"];
            bool ok = sharedAsPrototype (in, out, ids, why);
            if (ok && after.size() != model::selected (in, ids).size())
            {
                why = "the case lists another number of notes";
                ok = false;
            }
            for (std::size_t i = 0; i < after.size() && ok; ++i)
            {
                const std::string& id = after.keyAt (i);
                const JsonValue& want = after.valueAt (i);
                const model::Note* n = modelfx::findNote (out, id);
                if (n == nullptr)
                {
                    why = "no note " + id;
                    ok = false;
                    break;
                }
                for (int L = 0; L < core::kNumLanes && ok; ++L)
                {
                    const auto& g = n->gen[static_cast<std::size_t> (L)];
                    const JsonValue* seed = want["gens"].find (core::kLanes[L].code);
                    if (g.has_value() != (seed != nullptr) || (seed != nullptr && ! fixture::same (static_cast<double> (g->seed), fixture::num (*seed))))
                    {
                        why = id + " " + core::kLanes[L].code + " seed " + (g.has_value() ? std::to_string (g->seed) : std::string ("none"));
                        ok = false;
                    }
                }
                if (ok && ! fixture::same (static_cast<double> (n->sample.start.seed), fixture::num (want["startSeed"])))
                {
                    why = id + " start seed " + std::to_string (n->sample.start.seed);
                    ok = false;
                }
            }
            if (ok)
                tally.pass (true);
            else
                tally.fail ("reroll " + c["clip"].asString() + ": " + why);
        }
        else if (op == "bakeGen")
        {
            model::Clip in;
            if (c["clip"].isString())
            {
                const auto it = clips.find (c["clip"].asString());
                if (it == clips.end())
                {
                    tally.fail ("bakeGen: unknown clip " + c["clip"].asString());
                    continue;
                }
                in = it->second;
            }
            else if (! modelfx::clipFrom (c["clip"], in, why))
            {
                tally.fail ("bakeGen: " + why);
                continue;
            }
            const std::string id = c["note"].asString();
            const auto lane = core::findLane (c["lane"].asString());
            const model::Note* before = modelfx::findNote (in, id);
            if (! lane.has_value() || before == nullptr)
            {
                tally.fail ("bakeGen: no note " + id + " or lane " + c["lane"].asString());
                continue;
            }
            const double res = fixture::num (c["res"]);
            const model::Clip out = model::bakeGen (in, model::Ids (id), *lane, res);
            const model::Note* n = modelfx::findNote (out, id);
            const std::string label = "bakeGen " + id + " on " + c["lane"].asString();
            if (n == nullptr || ! modelfx::nodesMatch (n->lane (*lane), c["nodes"], kNodeTol, exact, why))
            {
                tally.fail (label + ": " + why);
                continue;
            }
            if (n->genOf (*lane).has_value() || ! c["genAfter"].isNull())
            {
                tally.fail (label + ": the generator is still there");
                continue;
            }
            // nodesAtBaseEps: how many points RDP keeps at 0.004 (more than 128 means the raised eps was used)
            if (const auto* atEps = c.find ("nodesAtBaseEps"))
            {
                const auto tab = model::laneTable (*before, *lane, res);
                model::NodeList points;
                for (std::size_t j = 0; j < tab.size(); ++j)
                    points.push_back (model::mkNode (static_cast<double> (j) / static_cast<double> (tab.size() - 1), static_cast<double> (tab[j])));
                const auto kept = model::rdp (points, model::kBakeEps).size();
                if (! fixture::same (static_cast<double> (kept), fixture::num (*atEps)) || kept <= static_cast<std::size_t> (model::kMaxNodes))
                {
                    tally.fail (label + fmt (": RDP at 0.004 keeps %d points, want %s (over 128)", static_cast<int> (kept),
                                             core::jsNumberToString (fixture::num (*atEps)).c_str()));
                    continue;
                }
            }
            tally.pass (exact);
        }
        else
        {
            tally.fail ("unknown op " + op);
        }
    }

    note (fmt ("lane tables: %lld of %lld Float32 values bit for bit; %lld random steps checked exactly", tableSame, tableValues, stepsChecked));
    check (ops["table"] > 200 && ops["reroll"] == 6 && ops["bakeGen"] == 9,
           fmt ("the file's cases: %d tables, %d Re-rolls, %d bakes", ops["table"], ops["reroll"], ops["bakeGen"]));
    tally.finish();
}

SL_TEST ("model", generatorEdits)
{
    section ("Generators: setGen, clearGen and makeGen follow normalizeGen; edits are copy-on-write");

    model::Clip clip = model::newClip (4);
    clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, 1, "a")));
    clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (62, 1, 1, "b")));

    model::GenPatch patch;
    patch.d0 = 5.0;       // clamped to 1
    patch.seed = 7;
    const model::Gen g = model::makeGen (model::GenKind::random, patch);
    check (g.kind == model::GenKind::random && fixture::same (g.d0, 1.0) && fixture::same (g.d1, 1.0) && g.seed == 7
               && g.rate == core::RateId::r1_16T && fixture::same (g.amount, 0.5) && g.lockToBeat,
           "makeGen: d0 5 clamps to 1, d1 follows d0, the defaults rate 1/16T and amount 0.5");

    const model::Clip withGen = model::setGen (clip, model::Ids ("b"), core::LaneId::tone, g);
    check (withGen.notes[0] == clip.notes[0] && withGen.notes[1] != clip.notes[1] && withGen.notes[1]->genOf (core::LaneId::tone).has_value()
               && ! clip.notes[1]->genOf (core::LaneId::tone).has_value(),
           "setGen puts the generator on b only; a is shared, the input is unchanged");

    const model::Clip cleared = model::clearGen (withGen, model::Ids::all(), std::nullopt);
    check (! cleared.notes[1]->genOf (core::LaneId::tone).has_value() && withGen.notes[1]->genOf (core::LaneId::tone).has_value(),
           "clearGen on every lane removes it from the copy, not from the input");

    // A generator that pushes the drawing outside 0..1 is clamped in the table (laneTable's second rounding).
    model::Note loud = model::mkNote (60, 0, 2, "c");
    model::GenPatch square;
    square.shape = model::GenShape::square;
    square.rate = core::RateId::r1_8;
    square.d0 = 1.0;
    loud.genOf (core::LaneId::amp) = model::makeGen (model::GenKind::pattern, square);
    const auto table = model::laneTable (loud, core::LaneId::amp, 64);
    const auto [lo, hi] = std::minmax_element (table.begin(), table.end());
    check (table.size() == 129 && fixture::same (*hi, 1.0) && fixture::same (*lo, static_cast<double> (static_cast<float> (0.3))),
           fmt ("a 1/8 square of depth 1 on Volume 0.8 over 2 beats at res 64: 129 values from %s to %s (0.3 .. 1, clamped)",
                core::jsNumberToString (*lo).c_str(), core::jsNumberToString (*hi).c_str()));
}
