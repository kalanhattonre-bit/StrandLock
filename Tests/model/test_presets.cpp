// model/presets.json (PLUGIN-PLAN 4 M1, 5.3): the 12 clip presets node for node, the 8 sound presets from two
// instruments (and one by its name), recipes 1-8 on Strand Demo and on three notes of mixed-settings, and 60
// seeded Dice rolls with pins. Exact, node values within 1e-12.

#include "ModelFixture.h"

#include "core/Inst.h"
#include "core/JsonLite.h"
#include "model/Presets.h"
#include "model/Recipes.h"
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

    constexpr double kTol = 1e-12;

    /** dice pins as the prototype accepts them: an array of names, or an object of name: true / false. */
    std::vector<std::string> pinsFrom (const JsonValue& v)
    {
        if (v.isArray())
            return modelfx::stringsFrom (v);
        std::vector<std::string> out;
        for (std::size_t i = 0; v.isObject() && i < v.size(); ++i)
            if (v.valueAt (i).isBool() && v.valueAt (i).asBool())
                out.push_back (v.keyAt (i));
        return out;
    }
}

SL_TEST ("model", presetsFixture)
{
    section ("model/presets.json: clip presets, sound presets, recipes 1-8 and Dice");

    JsonValue doc;
    if (! fixture::load ("model/presets.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    std::map<std::string, core::Inst> insts;
    const JsonValue& instTable = doc["insts"];
    for (std::size_t i = 0; i < instTable.size(); ++i)
        insts[instTable.keyAt (i)] = core::instFromJson (instTable.valueAt (i));

    const JsonValue& cases = doc["cases"];
    modelfx::FileTally tally ("model/presets.json", cases.size());
    std::map<std::string, int> perKind;
    std::vector<std::string> clipIds;
    std::set<int> recipesSeen;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const std::string kind = c["kind"].asString();
        ++perKind[kind];
        bool ok = true, exact = true;
        std::string why;
        std::string label = kind;

        if (kind == "clipPreset")
        {
            const std::string id = c["id"].asString();
            label += " " + id;
            clipIds.push_back (id);
            const auto clip = model::loadClipPreset (id);
            const auto byName = model::loadClipPreset (c["name"].asString());
            if (! clip.has_value() || ! byName.has_value())
            {
                ok = false;
                why = "no preset " + id + " (or by its name)";
            }
            else if (clip->bars != static_cast<int> (c["clip"]["bars"].asNumber()))
            {
                ok = false;
                why = "bars " + std::to_string (clip->bars);
            }
            else if (! modelfx::clipNotesClose (clip->notes, c["clip"]["notes"], kTol, exact, why))
            {
                ok = false;
            }
        }
        else if (kind == "soundPreset")
        {
            const std::string id = c["id"].asString(), from = c["from"].asString();
            label += " " + id + " from " + from;
            const auto it = insts.find (from);
            const auto r = it != insts.end() ? model::applySoundPreset (it->second, id) : std::nullopt;
            if (! r.has_value())
            {
                ok = false;
                why = "no preset or inst";
            }
            else if (r->sound != c["result"]["sound"].asString())
            {
                ok = false;
                why = "sound " + r->sound;
            }
            else
            {
                ok = modelfx::jsonSame (core::instToJson (r->inst), c["result"]["inst"], "inst", why);
            }
        }
        else if (kind == "recipe")
        {
            const int n = static_cast<int> (c["n"].asNumber());
            recipesSeen.insert (n);
            const std::string cname = c["clip"].asString(), iname = c["inst"].asString();
            label += " " + std::to_string (n) + " on " + cname;
            std::optional<model::Clip> clip;
            if (cname.rfind ("preset:", 0) == 0)
                clip = model::loadClipPreset (cname.substr (7));
            else if (const auto it = clips.find (cname); it != clips.end())
                clip = it->second;
            const auto inst = insts.find (iname);
            if (! clip.has_value() || inst == insts.end())
            {
                tally.fail (label + ": no clip or inst");
                continue;
            }
            const auto r = model::applyRecipe (*clip, inst->second, n, modelfx::idsFrom (c["ids"]));
            if (! r.has_value())
            {
                ok = false;
                why = "no recipe";
            }
            else if (! modelfx::jsonSame (core::instToJson (r->inst), c["result"]["inst"], "inst", why))
            {
                ok = false;
            }
            else
            {
                std::set<const model::Note*> input;
                for (const auto& note : clip->notes)
                    input.insert (note.get());
                std::vector<model::NotePtr> made;
                std::vector<std::string> order;
                for (const auto& note : r->clip.notes)
                {
                    order.push_back (note->id);
                    if (input.count (note.get()) == 0)
                        made.push_back (note);
                }
                if (! modelfx::clipNotesClose (made, c["result"]["notes"], kTol, exact, why))
                {
                    ok = false;
                    why = "new notes: " + why;
                }
                else if (order != modelfx::stringsFrom (c["result"]["order"]))
                {
                    ok = false;
                    why = "the order differs";
                }
            }
        }
        else if (kind == "dice")
        {
            const std::string iname = c["inst"].asString();
            label += " " + iname + " seed " + core::toJson (c["seed"]) + " pins " + core::toJson (c["pins"]);
            const auto it = insts.find (iname);
            if (it == insts.end())
            {
                ok = false;
                why = "no inst";
            }
            else
            {
                const core::Inst rolled = model::dice (it->second, fixture::num (c["seed"]), pinsFrom (c["pins"]));
                ok = modelfx::jsonSame (core::instToJson (rolled), c["result"], "inst", why);
            }
        }
        else
        {
            ok = false;
            why = "unknown kind";
        }

        if (ok)
            tally.pass (exact);
        else
            tally.fail (label + ": " + why);
    }

    std::vector<std::string> builtIds;
    for (const auto& p : model::clipPresets())
        builtIds.push_back (p.id);
    note (fmt ("cases: %d clip presets, %d sound presets, %d recipes, %d dice", perKind["clipPreset"], perKind["soundPreset"],
               perKind["recipe"], perKind["dice"]));
    check (builtIds == clipIds && builtIds.size() == 12, "the 12 clip presets exist in the prototype's menu order");
    check (model::soundPresets().size() == 8 && recipesSeen.size() == 8 && model::recipes().size() == 8, "8 sound presets and recipes 1-8");
    check (! model::loadClipPreset ("nope").has_value() && ! model::applySoundPreset (core::defaultInst(), "nope").has_value()
               && ! model::applyRecipe (model::newClip (4), core::defaultInst(), 9, model::Ids::all()).has_value(),
           "an unknown clip preset, sound preset or recipe gives nothing (the prototype throws)");
    tally.finish();
}
