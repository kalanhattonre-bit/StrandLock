// model/shapes.json (PLUGIN-PLAN 4 M1, 5.3): every key of shapeKeysFor on all 10 lanes, Each and Across,
// Travel in both kinds, Random with seeds 1 and 7, Fit with looping, holding and stopping Regions. Node
// lists within 1e-12; every selected note is a new copy and the others are shared (copy-on-write).

#include "ModelFixture.h"

#include "core/Inst.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "model/ClipEdit.h"
#include "model/Shapes.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    constexpr double kNodeTol = 1e-12;

    model::ShapeOpts optsFrom (const JsonValue& o)
    {
        model::ShapeOpts opts;
        if (const auto* v = o.find ("seed"))           opts.seed = fixture::num (*v);
        if (const auto* v = o.find ("count"))          opts.count = fixture::num (*v);
        if (const auto* v = o.find ("cycles"))         opts.cycles = fixture::num (*v);
        if (const auto* v = o.find ("bpm"))            opts.bpm = fixture::num (*v);
        if (const auto* v = o.find ("sampleSeconds"))  opts.sampleSeconds = fixture::num (*v);
        if (const auto* v = o.find ("from"))           opts.from = fixture::num (*v);
        if (const auto* r = o.find ("region"))
        {
            // The Region as given (fitNodes clamps its edges); an atEnd other than 'loop' holds.
            core::RegionSettings region;
            region.start = r->contains ("start") ? fixture::num ((*r)["start"]) : 0.0;
            region.end = r->contains ("end") ? fixture::num ((*r)["end"]) : 1.0;
            const auto atEnd = (*r)["atEnd"].isString() ? core::parseAtEnd ((*r)["atEnd"].asString()) : std::nullopt;
            region.atEnd = atEnd.value_or (core::AtEnd::hold);
            opts.region = region;
        }
        if (const auto* i = o.find ("inst"))
            opts.inst = core::instFromJson (*i);
        return opts;
    }
}

SL_TEST ("model", shapesFixture)
{
    section ("model/shapes.json: every shape key on every lane, Each and Across, both Travel kinds, Fit");

    JsonValue doc;
    if (! fixture::load ("model/shapes.json", doc))
        return;

    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    const auto& cases = doc["cases"];
    modelfx::FileTally tally ("model/shapes.json", cases.size());
    std::set<std::pair<std::string, std::string>> keysOnLanes;
    int fitCases = 0, acrossCases = 0;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const std::string laneCode = c["lane"].asString(), keyCode = c["key"].asString(), scopeCode = c["scope"].asString();
        const std::string label = laneCode + " " + keyCode + " " + scopeCode + " " + core::toJson (c["ids"]) + " " + core::toJson (c["opts"]);
        const auto lane = core::findLane (laneCode);
        const auto key = model::parseShapeKey (keyCode);
        const auto it = clips.find (c["clip"].asString());
        if (! lane.has_value() || ! key.has_value() || it == clips.end() || (scopeCode != "each" && scopeCode != "across"))
        {
            tally.fail (label + ": the case cannot be read");
            continue;
        }
        keysOnLanes.insert ({ laneCode, keyCode });
        fitCases += *key == model::ShapeKey::fit ? 1 : 0;
        acrossCases += scopeCode == "across" ? 1 : 0;

        model::Ids ids = model::Ids::all();
        if (c["ids"].isArray())
        {
            std::vector<std::string> list;
            for (std::size_t i = 0; i < c["ids"].size(); ++i)
                list.push_back (c["ids"][i].asString());
            ids = model::Ids (std::move (list));
        }

        const model::Clip& in = it->second;
        const auto out = model::applyShape (in, ids, *lane, *key, scopeCode == "across" ? model::ShapeScope::across : model::ShapeScope::each,
                                            optsFrom (c["opts"]));
        if (! out.has_value())
        {
            tally.fail (label + ": applyShape refused the key");
            continue;
        }

        bool exact = true, ok = true;
        std::string why;
        const JsonValue& notes = c["notes"];
        for (std::size_t i = 0; i < notes.size() && ok; ++i)
        {
            const model::Note* n = modelfx::findNote (*out, notes.keyAt (i));
            if (n == nullptr)
            {
                why = "no note " + notes.keyAt (i);
                ok = false;
            }
            else if (! modelfx::nodesMatch (n->lane (*lane), notes.valueAt (i), kNodeTol, exact, why))
            {
                why = notes.keyAt (i) + ": " + why;
                ok = false;
            }
        }

        // copy-on-write: the selected notes are new copies (even one a Speed-only key leaves alone), the rest shared
        const auto set = model::idSet (in, ids);
        for (std::size_t i = 0; i < in.notes.size() && ok; ++i)
        {
            const bool edited = set.count (in.notes[i]->id) != 0;
            if (out->notes.size() != in.notes.size() || edited == (out->notes[i] == in.notes[i]))
            {
                why = "note " + in.notes[i]->id + (edited ? " was not copied" : " was not shared");
                ok = false;
            }
        }
        if (ok && notes.size() != model::selected (in, ids).size())
        {
            why = "the case lists another number of notes";
            ok = false;
        }

        if (ok)
            tally.pass (exact);
        else
            tally.fail (label + ": " + why);
    }

    note (fmt ("%d lane x key pairs, %d Across cases, %d Fit cases", static_cast<int> (keysOnLanes.size()), acrossCases, fitCases));
    // 9 lanes x the 8 common keys + Travel's 8 common, 4 Speed and 3 Path keys, + wobble on pitch
    check (keysOnLanes.size() == 9 * 8 + 15 + 1, fmt ("every key of shapeKeysFor on every lane (%d pairs)", static_cast<int> (keysOnLanes.size())));
    tally.finish();
}

SL_TEST ("model", shapeKeys)
{
    section ("Shape keys: shapeKeysFor, shapeKeyKind, and keys that do not exist on a lane");

    const auto amp = model::shapeKeysFor (core::LaneId::amp);
    const auto speed = model::shapeKeysFor (core::LaneId::travel, model::TravelKind::speed);
    const auto path = model::shapeKeysFor (core::LaneId::travel, model::TravelKind::path);
    check (amp.size() == 8 && speed.size() == 12 && path.size() == 11 && speed[8] == model::ShapeKey::tapestop && path[10] == model::ShapeKey::fit,
           "8 common keys; Travel adds 4 Speed keys or 3 Path keys");
    check (model::shapeKeyKind (model::ShapeKey::rewind) == model::TravelKind::speed && model::shapeKeyKind (model::ShapeKey::playthrough) == model::TravelKind::path
               && ! model::shapeKeyKind (model::ShapeKey::swell).has_value() && model::parseShapeKey ("wobble") == model::ShapeKey::squiggle
               && std::string (model::shapeName (model::ShapeKey::fit)) == "Fit (1x)",
           "shapeKeyKind, 'wobble' reads as squiggle, names");

    model::Clip clip = model::newClip (4);
    clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 0, 1, "a")));
    check (! model::applyShape (clip, model::Ids::all(), core::LaneId::amp, model::ShapeKey::tapestop, model::ShapeScope::each).has_value()
               && model::applyShape (clip, model::Ids::all(), core::LaneId::travel, model::ShapeKey::tapestop, model::ShapeScope::each).has_value(),
           "a Speed key is refused on Volume and accepted on Travel");
    const auto none = model::applyShape (clip, model::Ids ("nope"), core::LaneId::amp, model::ShapeKey::up, model::ShapeScope::each);
    check (none.has_value() && none->notes.size() == 1 && none->notes[0] == clip.notes[0], "no selected note: the same notes, shared");
}
