// The selection bar's direct actions through the UI layer (PLUGIN-PLAN 4 M5 "tools through the UI layer", 7.7 S1 and
// decisions 1 to 3): Connect Nearest / Furthest / Random / Re-roll, Disconnect, Walk, Chop, Reverse, Flip, More, Less,
// Select similar, Humanize (and its Re-roll), Strum and Legato, as the bar runs them (SelectionTools, with the bar's
// own settings and the selection in time order).
//
// - Every case of model/tools.json the bar can express (its settings can hold the case's arguments) runs through the
//   bar on the fixture clip: the clip after it encodes to the same bytes as the direct model call, and it is exactly 1
//   undo step with the bar's label (0 when the model call changes nothing). Select similar changes the selection only,
//   to the fixture's ids.
// - A sweep of every action over every fixture clip, with every note and with a seeded half selected: the same bytes
//   as the direct model call with the bar's settings, 1 step each (0 for nothing changed).
// - Chop past 1024 notes is refused: a toast, 0 undo steps, 0 bytes changed; its panel greys the button and says why.
// - Walk's and Humanize's sliders change the clip live after the first click (no undo step while dragging, one on
//   each release); the toasts the prototype shows; the lane tools in Build show their lane.
// - The JUCE bar in a real editor: the drawers open, their buttons run the actions, the Lane picks a mode. And pictures.

#include <juce_audio_processors/juce_audio_processors.h>

#include "ToolRig.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "core/Travel.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/ModelFixture.h"
#include "model/Shapes.h"
#include "model/ToolOps.h"
#include "model/Tools.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/Drawers.h"
#include "ui/roll/LaneMenu.h"
#include "ui/roll/RollView.h"
#include "ui/roll/SelectionBar.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;
    using ui::ThemeId;
    using ui::roll::SelectionTools;
    using ui::roll::ToolSettings;

    std::vector<std::string> allIds (const model::Clip& c)
    {
        std::vector<std::string> ids;
        for (const auto& n : c.notes)
            ids.push_back (n->id);
        return ids;
    }

    std::string joined (const std::vector<std::string>& ids)
    {
        std::string s;
        for (const auto& id : ids)
            s += (s.empty() ? "" : ",") + id;
        return s;
    }

    /** The rate bases the bar's pickers offer (Chop: 1/4 .. 1/64; Strum's Gap: 1/8 .. 1/64): base = RateId / 3. */
    bool baseIn (core::RateId r, int lowest, int highest)
    {
        const int base = (int) r / 3;
        return base >= lowest && base <= highest;
    }

    /** On a slider's steps of 0.005 inside lo .. hi. */
    bool onSlider (double v, double lo, double hi)
    {
        return v >= lo && v <= hi && std::abs (v / 0.005 - std::round (v / 0.005)) < 1e-9;
    }

    /** Runs a tools.json case through the bar: its selection and settings from the case's arguments, then the bar's
        action. False (nothing run) when the bar cannot hold the case's arguments; why says what it cannot hold. */
    bool runThroughBar (const JsonValue& c, ToolRig& t, std::string& label, std::string& why)
    {
        const std::string op = c["op"].asString();
        const JsonValue& a = c["args"];
        const auto clip = t.rig.doc.current();
        std::vector<std::string> ids;
        if (a["ids"].isNull())
            ids = allIds (*clip);
        else
            for (std::size_t i = 0; i < a["ids"].size(); ++i)
                ids.push_back (a["ids"][i].asString());
        auto& S = t.settings();
        auto laneOf = [&a] { return core::findLane (a["lane"].isString() ? a["lane"].asString() : std::string()); };
        auto showLane = [&t] (core::LaneId l)
        {
            const auto& info = core::laneInfo (l);
            if (info.sub)
                t.rig.gestures().setSubLane (l);
            else
                t.rig.gestures().setMode (info.mode);
        };

        if (op == "connect")
        {
            const std::string mode = a["mode"].asString();
            S.connectSeed = fixture::num (a["seed"]);
            t.select (ids);
            t.tools->connect (mode == "random" ? model::ConnectMode::random : mode == "furthest" ? model::ConnectMode::furthest : model::ConnectMode::nearest);
            label = "Connect: " + mode;
            return true;
        }
        if (op == "disconnect")
        {
            t.select (ids);
            t.tools->disconnect();
            label = "Disconnect";
            return true;
        }
        if (op == "chop")
        {
            const auto rate = a["rate"].isString() ? core::parseRate (a["rate"].asString()) : std::nullopt;
            const JsonValue& ss = a["stepStart"];
            const bool step = ss.isBool() && ss.asBool();
            if (! rate || ! baseIn (*rate, 2, 6) || ! a["maxNotes"].isNull() || ! (step || (fixture::isNum (ss) && juce::exactlyEqual (fixture::num (ss), 0.0))))
            {
                why = "Chop's panel holds 1/4 to 1/64, Same spot or Step through the part, and 1024 notes";
                return false;
            }
            S.chop.rate = *rate;
            S.chop.step = step;
            t.select (ids);
            t.tools->chop();
            label = std::string ("Chop ") + core::rateLabel (*rate);
            return true;
        }
        if (op == "walk")
        {
            const std::string field = a["field"].asString();
            const double from = fixture::num (a["from"]), to = fixture::num (a["to"]);
            if (! onSlider (from, 0.0, 1.0) || ! onSlider (to, 0.0, 1.0))
            {
                why = "Walk's sliders hold 0 to 1";
                return false;
            }
            S.walk.field = field == "start" ? model::WalkField::start() : field == "vel" ? model::WalkField::vel()
                                                                                        : model::WalkField::onLane (core::findLane (field).value_or (core::LaneId::pitch));
            S.walk.from = from;
            S.walk.to = to;
            t.select (ids);
            t.tools->walk (true, true);
            label = "Walk " + ui::roll::walkName (S.walk.field);
            return true;
        }
        if (op == "humanize")
        {
            const JsonValue& am = a["amounts"];
            const double time = fixture::num (am["time"]), vel = fixture::num (am["vel"]);
            if (! am["lanes"].isNumber() || ! onSlider (time, 0.0, 0.25) || ! onSlider (vel, 0.0, 0.4) || ! onSlider (am["lanes"].asNumber(), 0.0, 0.25))
            {
                why = "Humanize's sliders hold Timing 0 to 0.25, Velocity 0 to 0.4, and one amount for Volume and Tone";
                return false;
            }
            S.hum.time = time;
            S.hum.vel = vel;
            S.hum.lanes = am["lanes"].asNumber();
            S.hum.seed = fixture::num (a["seed"]);
            t.select (ids);
            t.tools->humanize (true, true);
            label = "Humanize";
            return true;
        }
        if (op == "strum")
        {
            const auto rate = a["rate"].isString() ? core::parseRate (a["rate"].asString()) : std::nullopt;
            if (! rate || ! baseIn (*rate, 3, 6))
            {
                why = "Strum's Gap holds 1/8 to 1/64";
                return false;
            }
            S.strumRate = *rate;
            t.select (ids);
            const bool up = a["dir"].asString() != "down";
            t.tools->strum (up ? model::StrumDir::up : model::StrumDir::down);
            label = up ? "Strum up" : "Strum down";
            return true;
        }
        if (op == "legato")
        {
            t.select (ids);
            t.tools->legato();
            label = "Legato";
            return true;
        }
        if (op == "scaleLane" || op == "reverseLane" || op == "flipLane")
        {
            const auto lane = laneOf();
            const double factor = op == "scaleLane" ? fixture::num (a["factor"]) : 1.0;
            if (! lane || (op == "scaleLane" && ! juce::exactlyEqual (factor, 2.0) && ! juce::exactlyEqual (factor, 0.5)))
            {
                why = "More and Less scale by 2 and 0.5";
                return false;
            }
            showLane (*lane);
            t.select (ids);
            const std::string name = core::laneInfo (*lane).name;
            if (op == "reverseLane")
            {
                t.tools->reverse();
                label = "Reverse (" + name + ")";
            }
            else if (op == "flipLane")
            {
                t.tools->flip();
                label = "Flip (" + name + ")";
            }
            else if (juce::exactlyEqual (factor, 2.0))
            {
                t.tools->more();
                label = "More (" + name + ")";
            }
            else
            {
                t.tools->less();
                label = "Less (" + name + ")";
            }
            return true;
        }
        if (op == "selectSimilar")
        {
            // the bar's Select similar is like the primary note: the case's refId must be a selected note, or none (with
            // nothing selected, the prototype's null ids, when there is no primary note)
            const std::string ref = a["refId"].isString() ? a["refId"].asString() : std::string();
            const std::string kind = a["kind"].asString();
            if (ref.empty() && kind == "invert" && a["ids"].isNull())
                ids.clear();
            if ((! ref.empty() && std::find (ids.begin(), ids.end(), ref) == ids.end()) || (ref.empty() && kind != "invert"))
            {
                why = "Select similar is like the primary note, which is selected";
                return false;
            }
            t.select (ids, ref);
            t.tools->selectSimilar (kind == "pitch" ? model::SimilarKind::pitch : kind == "length" ? model::SimilarKind::length
                                    : kind == "bar" ? model::SimilarKind::bar : kind == "everyOther" ? model::SimilarKind::everyOther
                                                                                                       : model::SimilarKind::invert);
            label.clear();
            return true;
        }
        why = "not a selection bar action";
        return false;
    }

    /** The selection bar's actions for the sweep, each with the direct model call it must equal. */
    struct Action
    {
        const char* name;
        std::function<void (ToolRig&)> run;                                                           // through the bar
        std::function<model::Clip (const model::Clip&, const std::vector<std::string>&)> direct;    // the model call
        std::function<std::string()> label;
    };

    std::vector<Action> sweepActions()
    {
        using model::Clip;
        using Ids = std::vector<std::string>;
        std::vector<Action> list;
        auto add = [&list] (const char* name, std::function<void (ToolRig&)> run, std::function<Clip (const Clip&, const Ids&)> direct, std::string label)
        {
            list.push_back ({ name, std::move (run), std::move (direct), [label] { return label; } });
        };
        add ("Connect nearest", [] (ToolRig& t) { t.tools->connect (model::ConnectMode::nearest); },
             [] (const Clip& c, const Ids& ids) { return model::connect (c, ids, model::ConnectMode::nearest, 1.0); }, "Connect: nearest");
        add ("Connect furthest", [] (ToolRig& t) { t.tools->connect (model::ConnectMode::furthest); },
             [] (const Clip& c, const Ids& ids) { return model::connect (c, ids, model::ConnectMode::furthest, 1.0); }, "Connect: furthest");
        add ("Connect random", [] (ToolRig& t) { t.tools->connect (model::ConnectMode::random); },
             [] (const Clip& c, const Ids& ids) { return model::connect (c, ids, model::ConnectMode::random, 1.0); }, "Connect: random");
        add ("Connect re-roll", [] (ToolRig& t) { t.tools->connectReroll(); },
             [] (const Clip& c, const Ids& ids) { return model::connect (c, ids, model::ConnectMode::random, 2.0); }, "Connect: random (re-rolled)");
        add ("Disconnect", [] (ToolRig& t) { t.tools->disconnect(); },
             [] (const Clip& c, const Ids& ids) { return model::disconnect (c, ids); }, "Disconnect");
        add ("Walk Start", [] (ToolRig& t) { t.tools->walk (true, true); },
             [] (const Clip& c, const Ids& ids) { return model::walk (c, ids, model::WalkField::start(), 0.0, 0.9); }, "Walk Start");
        add ("Walk Velocity", [] (ToolRig& t) { t.tools->setWalkField (model::WalkField::vel()); t.tools->walk (true, true); },
             [] (const Clip& c, const Ids& ids) { return model::walk (c, ids, model::WalkField::vel(), 0.4, 1.0); }, "Walk Velocity");
        add ("Walk Travel", [] (ToolRig& t) { t.tools->setWalkField (model::WalkField::onLane (core::LaneId::travel)); t.tools->walk (true, true); },
             [] (const Clip& c, const Ids& ids)
             { return model::walk (c, ids, model::WalkField::onLane (core::LaneId::travel), core::travelSpeedToV (1.0), core::travelSpeedToV (2.0)); },
             "Walk Travel");
        add ("Chop 1/16", [] (ToolRig& t) { t.tools->chop(); },
             [] (const Clip& c, const Ids& ids) { return model::chop (c, ids, core::RateId::r1_16, model::ChopStart::by (0.0), 1024).clip; }, "Chop 1/16");
        add ("Chop 1/8T stepping", [] (ToolRig& t) { t.settings().chop.rate = core::RateId::r1_8T; t.settings().chop.step = true; t.tools->chop(); },
             [] (const Clip& c, const Ids& ids) { return model::chop (c, ids, core::RateId::r1_8T, model::ChopStart::evenly(), 1024).clip; },
             "Chop 1/8 triplet");
        for (const auto lane : { core::LaneId::tone, core::LaneId::travel, core::LaneId::pitch })
        {
            const std::string ln = core::laneInfo (lane).name;
            auto inLane = [lane] (ToolRig& t)
            {
                if (core::laneInfo (lane).sub)
                    t.rig.gestures().setSubLane (lane);
                else
                    t.rig.gestures().setMode (core::laneInfo (lane).mode);
            };
            add ("Reverse", [inLane] (ToolRig& t) { inLane (t); t.tools->reverse(); },
                 [lane] (const Clip& c, const Ids& ids) { return model::reverseLane (c, ids, lane); }, "Reverse (" + ln + ")");
            add ("Flip", [inLane] (ToolRig& t) { inLane (t); t.tools->flip(); },
                 [lane] (const Clip& c, const Ids& ids) { return model::flipLane (c, ids, lane); }, "Flip (" + ln + ")");
            add ("More", [inLane] (ToolRig& t) { inLane (t); t.tools->more(); },
                 [lane] (const Clip& c, const Ids& ids) { return model::scaleLane (c, ids, lane, 2.0); }, "More (" + ln + ")");
            add ("Less", [inLane] (ToolRig& t) { inLane (t); t.tools->less(); },
                 [lane] (const Clip& c, const Ids& ids) { return model::scaleLane (c, ids, lane, 0.5); }, "Less (" + ln + ")");
        }
        add ("Humanize", [] (ToolRig& t) { t.tools->humanize (true, true); },
             [] (const Clip& c, const Ids& ids)
             {
                 model::HumanizeAmounts h;
                 h.time = 0.03;
                 h.vel = 0.1;
                 h.volumeAndTone (0.05);
                 return model::humanize (c, ids, h, 1.0);
             },
             "Humanize");
        add ("Humanize re-roll", [] (ToolRig& t) { t.tools->humanizeReroll(); },
             [] (const Clip& c, const Ids& ids)
             {
                 model::HumanizeAmounts h;
                 h.time = 0.03;
                 h.vel = 0.1;
                 h.volumeAndTone (0.05);
                 return model::humanize (c, ids, h, 2.0);
             },
             "Humanize: re-roll");
        add ("Strum up", [] (ToolRig& t) { t.tools->strum (model::StrumDir::up); },
             [] (const Clip& c, const Ids& ids) { return model::strum (c, ids, core::RateId::r1_64, model::StrumDir::up); }, "Strum up");
        add ("Strum down 1/16T", [] (ToolRig& t) { t.settings().strumRate = core::RateId::r1_16T; t.tools->strum (model::StrumDir::down); },
             [] (const Clip& c, const Ids& ids) { return model::strum (c, ids, core::RateId::r1_16T, model::StrumDir::down); }, "Strum down");
        add ("Legato", [] (ToolRig& t) { t.tools->legato(); },
             [] (const Clip& c, const Ids& ids) { return model::legato (c, ids); }, "Legato");
        return list;
    }
}

//==================================================================================================
SL_TEST ("ui", selectionToolsFixtureCases)
{
    section ("Tools through the UI layer: every model/tools.json case the selection bar can hold, run through the bar, encodes to the same bytes "
             "as the direct model call; 1 undo step with the bar's label (0 when nothing changes)");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const JsonValue& cases = doc["cases"];
    int ran = 0, same = 0, stepsRight = 0, changed = 0, similarRight = 0, similar = 0, skipped = 0;
    std::map<std::string, int> perOp;
    std::string firstBad;
    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const auto it = clips.find (c["clip"].asString());
        if (it == clips.end())
            continue;
        ToolRig t (it->second);
        std::string label, why;
        if (! runThroughBar (c, t, label, why))
        {
            if (why != "not a selection bar action")
                ++skipped;
            continue;
        }
        ++ran;
        ++perOp[c["op"].asString()];
        const auto direct = toolops::run (c, it->second);
        if (c["op"].asString() == "selectSimilar")
        {
            ++similar;
            std::vector<std::string> want;
            for (std::size_t i = 0; i < c["result"]["ids"].size(); ++i)
                want.push_back (c["result"]["ids"][i].asString());
            const auto got = t.rig.doc.rollState().selected;
            auto sortedGot = got, sortedWant = want;
            std::sort (sortedGot.begin(), sortedGot.end());
            std::sort (sortedWant.begin(), sortedWant.end());
            const bool ok = direct.ids.has_value() && sortedGot == sortedWant && *direct.ids == want && t.rig.steps == 0
                            && t.bytes() == model::encodeClip (it->second);
            similarRight += ok ? 1 : 0;
            same += ok ? 1 : 0;
            stepsRight += ok ? 1 : 0;
            if (! ok && firstBad.empty())
                firstBad = fmt ("case %d selectSimilar: selected [%s], the fixture [%s]", (int) ci, joined (got).c_str(), joined (want).c_str());
            continue;
        }
        const auto wantBytes = model::encodeClip (*direct.clip);
        const bool didChange = wantBytes != model::encodeClip (it->second);
        changed += didChange ? 1 : 0;
        const bool bytesOk = direct.clip.has_value() && t.bytes() == wantBytes;
        const bool stepsOk = didChange ? t.rig.steps == 1 && t.lastLabel() == label : t.rig.steps == 0;
        same += bytesOk ? 1 : 0;
        stepsRight += stepsOk ? 1 : 0;
        if ((! bytesOk || ! stepsOk) && firstBad.empty())
            firstBad = fmt ("case %d %s on %s: bytes %s, %d undo steps (\"%s\", want \"%s\")", (int) ci, c["op"].asString().c_str(), c["clip"].asString().c_str(),
                            bytesOk ? "the same" : "DIFFER", t.rig.steps, t.lastLabel().c_str(), label.c_str());
    }
    std::string ops;
    for (const auto& [op, n] : perOp)
        ops += (ops.empty() ? "" : ", ") + op + " " + std::to_string (n);
    note (fmt ("%d cases through the bar (%s); %d the bar cannot hold (a More of 1.5, a Chop with maxNotes 20, a Walk to 1.2 ...)%s%s", ran, ops.c_str(),
               skipped, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (ran >= 65 && perOp.size() == 11, fmt ("the bar ran %d fixture cases of all 11 tools (%d tools)", ran, (int) perOp.size()));
    check (same == ran, fmt ("%d of %d: the clip after the bar's action encodes to the bytes of the direct model call (Select similar: the fixture's "
                             "ids, the clip untouched)", same, ran));
    check (stepsRight == ran && changed >= 45, fmt ("%d of %d: 1 undo step with the bar's label for each of the %d that changed the clip, none otherwise",
                                                    stepsRight, ran, changed));
    check (similar >= 8 && similarRight == similar, fmt ("Select similar: %d of %d selections are the fixture's", similarRight, similar));
}

SL_TEST ("ui", selectionToolsSweep)
{
    section ("Tools through the UI layer: every action of the bar on every fixture clip (every note, and a seeded half), the same bytes as "
             "the direct model call with the bar's settings; 1 undo step each (0 for nothing changed)");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const auto actions = sweepActions();
    int runs = 0, same = 0, stepsRight = 0, changed = 0;
    std::string firstBad;
    std::uint32_t seed = 515;
    for (const auto& [name, clip] : clips)
    {
        if (clip.notes.empty())
            continue;
        for (int half = 0; half < 2; ++half)
        {
            // every note, or a seeded half of them
            std::vector<std::string> sel;
            for (const auto& n : clip.notes)
                if (half == 0 || core::rand01 (core::hash32 ((std::int32_t) seed, (std::int32_t) sel.size(), (std::int32_t) n->s * 4)) < 0.5)
                    sel.push_back (n->id);
            ++seed;
            if (sel.empty())
                sel.push_back (clip.notes.front()->id);
            for (const auto& a : actions)
            {
                ToolRig t (clip);
                t.select (sel);
                a.run (t);
                // the model call on the clip the action saw, with the selection in time order (what the bar acts on)
                const auto timeOrdered = [&clip, &sel]
                {
                    std::vector<model::NotePtr> notes;
                    for (const auto& n : clip.notes)
                        if (std::find (sel.begin(), sel.end(), n->id) != sel.end())
                            notes.push_back (n);
                    std::vector<std::string> out;
                    for (const auto& n : model::timeOrder (notes))
                        out.push_back (n->id);
                    return out;
                }();
                const auto want = model::encodeClip (a.direct (clip, timeOrdered));
                const bool didChange = want != model::encodeClip (clip);
                const bool bytesOk = t.bytes() == want;
                const bool stepsOk = didChange ? t.rig.steps == 1 && t.lastLabel() == a.label() : t.rig.steps == 0;
                ++runs;
                changed += didChange ? 1 : 0;
                same += bytesOk ? 1 : 0;
                stepsRight += stepsOk ? 1 : 0;
                if ((! bytesOk || ! stepsOk) && firstBad.empty())
                    firstBad = fmt ("%s on %s (%s): bytes %s, %d steps (\"%s\")", a.name, name.c_str(), half == 0 ? "every note" : "half",
                                    bytesOk ? "the same" : "DIFFER", t.rig.steps, t.lastLabel().c_str());
            }
        }
    }
    note (fmt ("%d runs (%d actions x the fixture clips x 2 selections), %d changed the clip%s%s", runs, (int) actions.size(), changed,
               firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (runs >= 250 && same == runs, fmt ("%d of %d runs: the bar's action gives the bytes of the direct model call", same, runs));
    check (stepsRight == runs && changed >= runs / 2 && runs - changed >= 20,
           fmt ("%d of %d runs: 1 undo step with the bar's label when the clip changed (%d did), none otherwise (%d)", stepsRight, runs, changed,
                runs - changed));
}

//==================================================================================================
SL_TEST ("ui", selectionToolsChopRefused)
{
    section ("Chop past 1024 notes is refused: a toast, 0 undo steps, 0 bytes changed; at exactly 1024 it chops");

    // 64 notes of 4 beats: chopped at 1/32 they would make 64 x 32 = 2048 notes, at 1/16 exactly 1024
    model::Clip clip = model::newClip (32.0);
    for (int i = 0; i < 64; ++i)
        clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote ((double) (48 + i % 2 * 7), (double) (i / 2) * 4.0, 4.0,
                                                                                   "ch-" + std::to_string (i))));
    ToolRig t (clip);
    t.select (allIds (clip));
    t.settings().chop.rate = core::RateId::r1_32;
    const auto preview = t.tools->chopPreview();
    const auto before = t.bytes();
    t.tools->chop();
    const bool refused = preview.refused && juce::exactlyEqual (preview.count, 2048.0) && t.rig.steps == 0 && t.bytes() == before
                         && t.lastToast() == "That would make 2048 notes; the limit is 1024. Pick a slower rate or fewer notes."
                         && ! t.toasts.empty() && t.toasts.back().second == 4000;
    check (refused, fmt ("Chop 1/32 of 64 four-beat notes: refused (%s, count %.0f), %d undo steps, the clip's bytes %s, toast \"%s\"",
                         preview.refused ? "refused" : "not refused", preview.count, t.rig.steps, t.bytes() == before ? "unchanged" : "CHANGED",
                         t.lastToast().c_str()));

    t.settings().chop.rate = core::RateId::r1_16;
    t.tools->chop();
    const auto now = t.rig.doc.current();
    check (now->notes.size() == 1024 && t.rig.steps == 1 && t.lastLabel() == "Chop 1/16" && t.rig.doc.rollState().selected.size() == 1024
               && t.lastToast() == "Chopped into 1024 notes",
           fmt ("Chop 1/16 makes exactly 1024 notes (%d), one undo step \"%s\", every piece selected (%d), toast \"%s\"", (int) now->notes.size(),
                t.lastLabel().c_str(), (int) t.rig.doc.rollState().selected.size(), t.lastToast().c_str()));
}

SL_TEST ("ui", selectionToolsLiveSliders)
{
    section ("Walk and Humanize: the first click is one undo step; then each slider move is shown with no undo step and each release is "
             "one; a slider before the first click changes nothing");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const model::Clip& clip = clips["humanized-chord"];
    const auto ids = allIds (clip);

    ToolRig t (clip);
    t.select (ids);
    // a slider before the first click: nothing (S2's ghost shows what it would do)
    t.settings().walk.to = 0.5;
    t.tools->walk (false, false);
    t.tools->walk (true, false);
    const bool nothing = t.rig.steps == 0 && t.bytes() == model::encodeClip (clip);
    // the click: one step
    t.tools->walk (true, true);
    const bool clickStep = t.rig.steps == 1 && t.lastLabel() == "Walk Start" && t.tools->isLive (ToolSettings::Tool::walk);
    // a drag: live, no step; the release: one step; each from the clip the session started from
    t.settings().walk.to = 0.75;
    t.tools->walk (false, false);
    const bool liveNoStep = t.rig.steps == 1 && t.bytes() == model::encodeClip (model::walk (clip, ids, model::WalkField::start(), 0.0, 0.75));
    t.settings().walk.to = 0.8;
    t.tools->walk (false, false);
    t.tools->walk (true, false);
    const bool releaseStep = t.rig.steps == 2 && t.bytes() == model::encodeClip (model::walk (clip, ids, model::WalkField::start(), 0.0, 0.8));
    // a drag left dirty ends with the selection: its last move becomes one step
    t.settings().walk.from = 0.2;
    t.tools->walk (false, false);
    t.select ({ ids.front() });
    t.tools->sync();
    const bool endedStep = t.rig.steps == 3 && ! t.tools->isLive (ToolSettings::Tool::walk);
    // Undo after it lands on the release before
    t.rig.undo();
    const bool undone = t.bytes() == model::encodeClip (model::walk (clip, ids, model::WalkField::start(), 0.0, 0.8));
    check (nothing && clickStep && liveNoStep && releaseStep && endedStep && undone,
           fmt ("Walk: a slider alone changes nothing (%d); the click is \"Walk Start\" (%d); a move is live with no step (%d); the release is one "
                "step (%d); a session left by another selection makes its last move a step (%d); Undo goes back to the release (%d)",
                (int) nothing, (int) clickStep, (int) liveNoStep, (int) releaseStep, (int) endedStep, (int) undone));

    ToolRig h (clip);
    h.select (ids);
    h.tools->humanize (true, true);
    h.settings().hum.vel = 0.3;
    h.tools->humanize (false, false);
    const int liveSteps = h.rig.steps;
    h.tools->humanize (true, false);
    h.tools->humanizeReroll();
    model::HumanizeAmounts a;
    a.time = 0.03;
    a.vel = 0.3;
    a.volumeAndTone (0.05);
    check (liveSteps == 1 && h.rig.steps == 3 && h.lastLabel() == "Humanize: re-roll" && juce::exactlyEqual (h.settings().hum.seed, 2.0)
               && h.bytes() == model::encodeClip (model::humanize (clip, ids, a, 2.0)),
           fmt ("Humanize: the click, a live move (%d step so far), its release and Re-roll (seed %.0f, \"%s\") make %d steps; the re-roll is the "
                "model's Humanize with seed 2 on the clip before the session", liveSteps, h.settings().hum.seed, h.lastLabel().c_str(), h.rig.steps));
}

SL_TEST ("ui", selectionToolsToastsAndLanes)
{
    section ("The selection bar's toasts and counts (Connect, Legato, Select similar, Chop), and the lane tools in Build");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const model::Clip& grid = clips["connect-grid"];
    const model::Clip& lead = clips["voice-lead"];

    ToolRig t (grid);
    t.select (allIds (grid));
    t.tools->connect (model::ConnectMode::nearest);
    const int links = ui::roll::linksAmong (*t.rig.doc.current(), allIds (grid));
    const bool slides = links > 0 && t.lastToast() == std::to_string (links) + " slides among the selected notes" && t.toasts.back().second == 3200;
    t.tools->disconnect();
    const bool gone = ui::roll::linksAmong (*t.rig.doc.current(), allIds (grid)) == 0 && t.lastLabel() == "Disconnect";
    t.select ({ grid.notes.front()->id });
    t.tools->connect (model::ConnectMode::nearest);
    const bool none = t.lastToast() == "No slides made: a note slides into one that starts as it ends (up to 2 octaves away)";
    t.select (allIds (grid));
    t.tools->connectReroll();
    const bool reroll = juce::exactlyEqual (t.settings().connectSeed, 2.0) && t.lastLabel() == "Connect: random (re-rolled)";
    check (slides && gone && none && reroll,
           fmt ("Connect: \"%d slides among the selected notes\"; Disconnect takes them away; one note alone: \"No slides made...\"; Re-roll moves "
                "the seed to 2 (\"Connect: random (re-rolled)\")", links));

    // Legato: one chord alone has nowhere to go; a gap before the next chord stretches; notes already touching have
    // nothing to stretch
    model::Clip gap = model::newClip (4.0);
    gap.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60.0, 0.0, 1.0, "lg-a")));
    gap.notes.push_back (std::make_shared<const model::Note> (model::mkNote (64.0, 0.0, 1.0, "lg-b")));
    gap.notes.push_back (std::make_shared<const model::Note> (model::mkNote (67.0, 2.0, 1.0, "lg-c")));
    ToolRig l (gap);
    l.select ({ "lg-a", "lg-b" });
    l.tools->legato();
    const std::string t1 = l.lastToast();
    const bool oneChord = l.rig.steps == 0 && t1 == "No later selected note to stretch to: select the notes that follow as well";
    l.select (allIds (gap));
    l.tools->legato();
    const std::string t2 = l.lastToast();
    const bool stretched = l.rig.steps == 1 && t2 == "Each note now reaches the next one: try Connect \xe2\x96\xb8 Nearest"
                           && l.bytes() == model::encodeClip (model::legato (gap, l.tools->ids()));
    l.tools->legato();
    const std::string t3 = l.lastToast();
    const bool touching = l.rig.steps == 1 && t3 == "Already touching: nothing to stretch";
    check (oneChord && stretched && touching,
           fmt ("Legato: one chord alone: \"%s\"; the chord and a later note: one step, \"%s\"; again: \"%s\"", t1.c_str(), t2.c_str(), t3.c_str()));

    // Select similar: the selection only, with a toast; no undo step
    ToolRig s (lead);
    s.select (allIds (lead), "vl-a3");
    s.tools->selectSimilar (model::SimilarKind::pitch);
    const auto sel = s.rig.doc.rollState();
    check (sel.selected.size() == 3 && sel.primary == "vl-a3" && s.lastToast() == "3 notes selected" && s.rig.steps == 0,
           fmt ("Select similar, same pitch as vl-a3: %d notes selected, the primary stays (%s), \"%s\", no undo step", (int) sel.selected.size(),
                sel.primary.c_str(), s.lastToast().c_str()));

    // the lane tools in Build act on the bar's Lane and show its mode (Volume: flat at 0.8, it flips to 0.2)
    ToolRig b (lead);
    b.select (allIds (lead));
    b.settings().lane = core::LaneId::amp;
    b.tools->flip();
    const auto st = b.rig.doc.rollState();
    check (st.mode == 3 && b.lastLabel() == "Flip (Volume)" && b.bytes() == model::encodeClip (model::flipLane (lead, b.tools->ids(), core::LaneId::amp)),
           fmt ("Flip in Build acts on the Lane (Volume) and shows its mode (%d): \"%s\"", st.mode, b.lastLabel().c_str()));
    b.settings().lane = core::LaneId::heads;
    b.rig.gestures().setMode (1);
    b.tools->pickLane (core::LaneId::heads);
    const auto st8 = b.rig.doc.rollState();
    check (st8.mode == 8 && st8.subLane == core::LaneId::heads && b.tools->lane() == core::LaneId::heads,
           "picking Sample: Heads in the Lane list shows mode 8 with the Heads chip");
}

//==================================================================================================
SL_TEST ("ui", selectionBarInTheEditor)
{
    section ("The selection bar in the editor: nothing selected it says how; Connect and Tools open their drawers, whose buttons run the actions "
             "(one undo step each); Walk's panel, Chop's panel, Select similar's menu, the Lane list, the Shapes drawer");

    StrandLockProcessor proc;
    {
        auto& s = proc.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }
    {
        StrandLockEditor editor (proc);
        auto& view = editor.getView();
        auto& roll = view.getRoll();
        auto& bar = view.getSelectionBar();
        roll.setMenusShown (false);
        editor.refreshNow();
        const auto clip0 = proc.getDocument().current();

        const int lineOnly = view.getSelectionRowArea().getHeight();
        const bool idle = bar.isIdleShown() && ! bar.getDrawerButton (ToolSettings::Drawer::connect).isVisible();
        roll.selectAll();
        const bool awake = ! bar.isIdleShown() && bar.getDrawerButton (ToolSettings::Drawer::connect).isVisible();
        check (idle && awake && bar.getIdleText().startsWith ("Select notes"),
               "with nothing selected the bar says how to select; with notes selected it shows the Lane, the drawers, Connect and Tools");

        // Connect
        bar.getDrawerButton (ToolSettings::Drawer::connect).onClick();
        auto* connect = dynamic_cast<ui::roll::ConnectDrawer*> (bar.getOpenDrawer());
        const int withDrawer = view.getSelectionRowArea().getHeight();
        const bool opened = connect != nullptr && withDrawer > lineOnly && bar.getDrawerButton (ToolSettings::Drawer::connect).getToggleState();
        const auto ids = roll.getTools().ids();
        if (connect != nullptr)
            connect->getButton ("Nearest").onClick();
        const auto afterConnect = proc.getDocument().current();
        const bool connected = model::encodeClip (*afterConnect) == model::encodeClip (model::connect (*clip0, ids, model::ConnectMode::nearest, 1.0))
                               && proc.undoLabel().value_or ("") == "Connect: nearest";
        const bool note = connect != nullptr && connect->getNote().getText().contains ("among the selected notes");
        check (opened && connected && note,
               fmt ("Connect opens its drawer (the rows grow from %d to %d px); Nearest is one undo step \"%s\" equal to the model's connect; the "
                    "drawer counts the slides (\"%s\")", lineOnly, withDrawer, proc.undoLabel().value_or ("").c_str(),
                    connect != nullptr ? connect->getNote().getText().toRawUTF8() : ""));

        // Tools: Walk's panel and its button, a slider drag (live) and its release, Chop, Select similar's menu
        bar.getDrawerButton (ToolSettings::Drawer::tools).onClick();
        auto* tools = dynamic_cast<ui::roll::ToolsDrawer*> (bar.getOpenDrawer());
        bool walked = false, slid = false, chopped = false, menu = false;
        if (tools != nullptr)
        {
            tools->getButton ("Walk" + juce::String::charToString ((juce::juce_wchar) 0x2026)).onClick();
            auto* panel = tools->getPanel();
            auto* go = panel != nullptr ? panel->findButton ("Walk") : nullptr;
            if (go != nullptr)
            {
                go->onClick();
                walked = proc.undoLabel().value_or ("") == "Walk Start";
            }
            auto* lastNote = panel != nullptr ? panel->findSlider ("Last note") : nullptr;
            if (lastNote != nullptr)
            {
                const auto before = proc.getDocument().current();
                lastNote->dragTo (10.0f + (float) lastNote->getWidth() * 0.5f);
                const bool live = proc.getDocument().current() != before && proc.undoLabel().value_or ("") == "Walk Start" && ! proc.canRedo();
                lastNote->release();
                slid = live && proc.canUndo();
            }
            tools->getButton ("Chop" + juce::String::charToString ((juce::juce_wchar) 0x2026)).onClick();
            auto* chopPanel = tools->getPanel();
            auto* chopButton = chopPanel != nullptr ? chopPanel->findButton ("Chop") : nullptr;
            if (chopButton != nullptr && chopButton->isEnabled())
            {
                chopButton->onClick();
                chopped = proc.undoLabel().value_or ("") == "Chop 1/16";
            }
            tools->getButton ("Select similar").onClick();
            const auto& items = roll.getMenu();
            menu = items.size() == 6 && items[0].kind == ui::roll::MenuItem::Kind::header && items[5].label == "Flip the selection";
            if (menu)
            {
                const auto selectedBefore = proc.getDocument().rollState().selected.size();
                roll.chooseMenuItem (items[5]);
                menu = proc.getDocument().rollState().selected.size() != selectedBefore || selectedBefore == proc.getDocument().current()->notes.size();
            }
        }
        check (tools != nullptr && walked && slid && chopped && menu,
               fmt ("Tools: Walk... opens Walk's panel, its Walk button is one step (%d); a drag of Last note is live and its release one step (%d); "
                    "Chop... and Chop make \"Chop 1/16\" (%d); Select similar opens its menu and Flip the selection runs (%d)", (int) walked,
                    (int) slid, (int) chopped, (int) menu));

        // M0's rule holds for what the bar makes later: no control in an open drawer or tool panel wants the keyboard
        std::function<int (const juce::Component&)> wanting = [&wanting] (const juce::Component& c)
        {
            int n = c.getWantsKeyboardFocus() ? 1 : 0;
            for (auto* child : c.getChildren())
                n += wanting (*child);
            return n;
        };
        const int wanters = wanting (bar);
        check (wanters == 0 && ! editor.isKeysOn(), fmt ("with Keys off no control of the bar, its drawer or its tool panel wants the keyboard (%d do)",
                                                          wanters));

        // the Lane list picks a mode; Shapes opens its drawer for that lane (S2)
        bar.getLaneBox().setSelectedId (core::laneIndex (core::LaneId::hollow) + 1, juce::sendNotificationSync);
        const bool laneMode = view.getMode() == 7 && roll.getTools().lane() == core::LaneId::hollow;
        bar.getDrawerButton (ToolSettings::Drawer::shapes).onClick();
        auto* shapes = dynamic_cast<ui::roll::ShapesDrawer*> (bar.getOpenDrawer());
        const bool shapesOpen = shapes != nullptr && shapes->getShapeButtons().size() == 8 && bar.getDrawerButton (ToolSettings::Drawer::shapes).getToggleState();
        check (laneMode && shapesOpen, fmt ("the Lane list's Hollow shows mode 7 (%d); Shapes opens its drawer with Hollow's 8 shapes (%d)",
                                            view.getMode(), shapes != nullptr ? (int) shapes->getShapeButtons().size() : -1));
    }
}

SL_TEST ("ui", selectionBarSmallWindow)
{
    section ("The selection bar on a small window (900 x 560, the tabs folded): the Tools drawer with Walk's panel keeps its least room and "
             "scrolls, and its scroll bar shows (JUCE draws no thumb on a bar shorter than 32 px plus twice its thickness)");

    StrandLockProcessor proc;
    {
        auto& s = proc.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }
    {
        StrandLockEditor editor (proc);
        editor.setLogicalSize (900, 560);
        auto& view = editor.getView();
        auto& roll = view.getRoll();
        auto& bar = view.getSelectionBar();
        roll.setMenusShown (false);
        view.setFolded (true);
        editor.refreshNow();
        roll.selectAll();
        view.setMode (5);
        bar.getDrawerButton (ToolSettings::Drawer::tools).onClick();
        auto* tools = dynamic_cast<ui::roll::ToolsDrawer*> (bar.getOpenDrawer());
        if (tools != nullptr)
            tools->getButton ("Walk" + juce::String::charToString ((juce::juce_wchar) 0x2026)).onClick();
        editor.refreshNow();

        auto& vp = bar.getDrawerView();
        auto& sb = vp.getVerticalScrollBar();
        const auto* content = vp.getViewedComponent();
        const int box = bar.getDrawerArea().getHeight();
        const int contentHeight = content != nullptr ? content->getHeight() : 0;
        const int thumbFrom = 32 + 2 * vp.getScrollBarThickness();
        const bool scrolls = content != nullptr && contentHeight > vp.getHeight();
        const bool barShown = sb.isVisible() && sb.getHeight() >= thumbFrom;
        check (tools != nullptr && tools->getPanel() != nullptr && scrolls && barShown && box >= ui::roll::SelectionBar::drawerMinHeight,
               fmt ("Walk's panel open: the drawer's box is %d px (at least %d), its contents %d px in a %d px view, so it scrolls; its scroll bar is "
                    "%s and %d px long (a thumb from %d px); the roll's canvas keeps %d px",
                    box, ui::roll::SelectionBar::drawerMinHeight, contentHeight, vp.getHeight(), sb.isVisible() ? "shown" : "hidden", sb.getHeight(),
                    thumbFrom, roll.getCanvas().getHeight()));
    }
}

//==================================================================================================
// S2: the drawers (Shapes, Pattern, Random, Turn into nodes) and the lane buttons' menus, through the UI layer.

namespace
{
    using IdList = std::vector<std::string>;
    using DirectFn = std::function<std::optional<model::Clip> (const model::Clip&, const IdList&)>;

    /** One S2 action for the sweep: what it runs through the bar, and the direct model call it must equal (nothing:
        the bar changes nothing). chain > 0: that many clicks, each one undo step, the last one with the label. */
    struct DrawerAction
    {
        std::string name;
        std::function<void (ToolRig&)> run;
        DirectFn direct;
        std::string label;
        int chain = 0;
    };

    /** The Pattern and Random drawers with settings other than their first ones, and the generators those make. */
    ToolSettings::Pattern otherPattern()
    {
        ToolSettings::Pattern P;
        P.shape = model::GenShape::sawdown;
        P.rate = core::RateId::r1_8D;
        P.d0 = 0.65;
        P.d1 = 0.15;
        P.warp = 0.5;
        P.lockToBeat = false;
        P.perNote = model::PerNote::different;
        return P;
    }

    model::Gen otherPatternGen()
    {
        model::GenPatch p;
        p.shape = model::GenShape::sawdown;
        p.rate = core::RateId::r1_8D;
        p.d0 = 0.65;
        p.d1 = 0.15;
        p.warp = 0.5;
        p.lockToBeat = false;
        p.perNote = model::PerNote::different;
        return model::makeGen (model::GenKind::pattern, p);
    }

    ToolSettings::Random otherRandom()
    {
        ToolSettings::Random R;
        R.rate = core::RateId::r1_4T;
        R.amount = 0.8;
        R.smooth = 0.4;
        R.perNote = model::PerNote::different;
        R.lockToBeat = false;
        return R;
    }

    model::Gen otherRandomGen()
    {
        model::GenPatch p;
        p.rate = core::RateId::r1_4T;
        p.amount = 0.8;
        p.smooth = 0.4;
        p.perNote = model::PerNote::different;
        p.lockToBeat = false;
        return model::makeGen (model::GenKind::random, p);
    }

    /** The bar on these notes with this Lane (the roll in Build), and the drawers' settings at their first values:
        sync reads the primary note's generator into them, as the prototype's loadGenValues does, and the sweep sets
        them itself. */
    void prepare (ToolRig& t, const IdList& sel, core::LaneId lane)
    {
        t.select (sel);
        t.lane (lane);
        t.settings().pat = ToolSettings::Pattern();
        t.settings().rnd = ToolSettings::Random();
    }

    /** The shapes' options the bar must use in a new window: seed 1, the roll's tempo, sound length and instrument. */
    model::ShapeOpts firstShapeOpts (ToolRig& t)
    {
        const auto& rs = t.rig.gestures().settings();
        model::ShapeOpts o;
        o.seed = 1.0;
        o.bpm = rs.bpm;
        o.sampleSeconds = rs.sampleSeconds;
        o.inst = rs.inst;
        return o;
    }

    std::optional<model::ShapeKey> shapeNamed (const std::string& label)
    {
        for (int k = 0; k < model::kNumShapeKeys; ++k)
            if (label == model::shapeName (static_cast<model::ShapeKey> (k)))
                return static_cast<model::ShapeKey> (k);
        return std::nullopt;
    }

    /** The lane menu of the bar's notes (setScope: the bar's scope; nothing reopens, no drawer opens). */
    ui::roll::LaneMenu menuOf (ToolRig& t, core::LaneId lane)
    {
        auto& tools = *t.tools;
        return ui::roll::laneMenu (tools, lane, tools.ids(), {}, [&tools] (model::ShapeScope s) { tools.setScope (s); }, {});
    }

    std::vector<DrawerAction> drawerActions (core::LaneId L, const std::vector<model::ShapeKey>& keys, const model::ShapeOpts& opts, bool genNow)
    {
        using model::Clip;
        std::vector<DrawerAction> list;
        const std::string ln = core::laneInfo (L).name;
        for (const auto scope : { model::ShapeScope::each, model::ShapeScope::across })
            for (const auto k : keys)
                list.push_back ({ std::string ("Shape ") + model::code (k) + (scope == model::ShapeScope::across ? " across" : ""),
                                  [scope, k] (ToolRig& t)
                                  {
                                      t.tools->setScope (scope);
                                      t.tools->applyShape (k);
                                  },
                                  [L, k, scope, opts] (const Clip& c, const IdList& ids) { return model::applyShape (c, ids, L, k, scope, opts); },
                                  std::string (model::shapeName (k)) + " (" + ln + ")" });
        for (int s = 0; s < 7; ++s)
        {
            const auto shape = static_cast<model::GenShape> (s);
            list.push_back ({ std::string ("Pattern ") + model::code (shape),
                              [shape] (ToolRig& t)
                              {
                                  t.settings().pat.shape = shape;
                                  t.tools->applyGen (model::GenKind::pattern);
                              },
                              [L, shape] (const Clip& c, const IdList& ids) { return std::optional<Clip> (expectedPutGen (c, ids, L, startPattern (shape))); },
                              "Pattern (" + ln + ")" });
        }
        list.push_back ({ "Pattern with other settings",
                          [] (ToolRig& t)
                          {
                              t.settings().pat = otherPattern();
                              t.tools->applyGen (model::GenKind::pattern);
                          },
                          [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (expectedPutGen (c, ids, L, otherPatternGen())); },
                          "Pattern (" + ln + ")" });
        list.push_back ({ "Random", [] (ToolRig& t) { t.tools->applyGen (model::GenKind::random); },
                          [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (expectedPutGen (c, ids, L, startRandom())); },
                          "Random (" + ln + ")" });
        list.push_back ({ "Random with other settings",
                          [] (ToolRig& t)
                          {
                              t.settings().rnd = otherRandom();
                              t.tools->applyGen (model::GenKind::random);
                          },
                          [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (expectedPutGen (c, ids, L, otherRandomGen())); },
                          "Random (" + ln + ")" });
        list.push_back ({ "Re-roll", [] (ToolRig& t) { t.tools->rerollGen(); },
                          [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::reroll (c, ids, L)); }, "Re-roll (" + ln + ")" });
        list.push_back ({ "Remove", [] (ToolRig& t) { t.tools->removeGen(); },
                          [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::clearGen (c, ids, L)); },
                          "Remove pattern (" + ln + ")" });
        list.push_back ({ "Turn into nodes", [] (ToolRig& t) { t.tools->bake(); },
                          [L, genNow] (const Clip& c, const IdList& ids) { return genNow ? std::optional<Clip> (model::bakeGen (c, ids, L)) : std::nullopt; },
                          "Turn into nodes (" + ln + ")" });
        if (! genNow)
        {
            // notes with nothing on the lane yet: Apply first, then the button (two clicks, two undo steps)
            list.push_back ({ "Random then Re-roll",
                              [] (ToolRig& t)
                              {
                                  t.tools->applyGen (model::GenKind::random);
                                  t.tools->rerollGen();
                              },
                              [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::reroll (expectedPutGen (c, ids, L, startRandom()), ids, L)); },
                              "Re-roll (" + ln + ")", 2 });
            list.push_back ({ "Pattern then Remove",
                              [] (ToolRig& t)
                              {
                                  t.tools->applyGen (model::GenKind::pattern);
                                  t.tools->removeGen();
                              },
                              [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::clearGen (expectedPutGen (c, ids, L, startPattern()), ids, L)); },
                              "Remove pattern (" + ln + ")", 2 });
            list.push_back ({ "Pattern then Turn into nodes",
                              [] (ToolRig& t)
                              {
                                  t.tools->applyGen (model::GenKind::pattern);
                                  t.tools->bake();
                              },
                              [L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::bakeGen (expectedPutGen (c, ids, L, startPattern()), ids, L)); },
                              "Turn into nodes (" + ln + ")", 2 });
        }
        return list;
    }

    /** What a lane-menu item must make (sl-ui-panels.js openLaneMenu) and its undo label; nothing for the items that
        open a drawer. */
    std::optional<std::pair<DirectFn, std::string>> menuDirect (const std::string& label, core::LaneId L, const model::ShapeOpts& opts)
    {
        using model::Clip;
        const std::string ln = core::laneInfo (L).name;
        if (const auto k = shapeNamed (label))
        {
            const auto key = *k;
            return std::make_pair (DirectFn ([L, key, opts] (const Clip& c, const IdList& ids)
                                             { return model::applyShape (c, ids, L, key, model::ShapeScope::each, opts); }),
                                   label + " (" + ln + ")");
        }
        if (label.rfind ("Pattern: ", 0) == 0)
            return std::make_pair (DirectFn ([L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (expectedPutGen (c, ids, L, startPattern())); }),
                                   "Pattern (" + ln + ")");
        if (label.rfind ("Random 1", 0) == 0)
            return std::make_pair (DirectFn ([L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (expectedPutGen (c, ids, L, startRandom())); }),
                                   "Random (" + ln + ")");
        if (label == "Re-roll the random")
            return std::make_pair (DirectFn ([L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::reroll (c, ids, L)); }),
                                   "Re-roll (" + ln + ")");
        if (label == "Remove pattern / random")
            return std::make_pair (DirectFn ([L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::clearGen (c, ids, L)); }),
                                   "Remove pattern (" + ln + ")");
        if (label == "Turn into nodes")
            return std::make_pair (DirectFn ([L] (const Clip& c, const IdList& ids) { return std::optional<Clip> (model::bakeGen (c, ids, L)); }),
                                   "Turn into nodes (" + ln + ")");
        return std::nullopt;
    }

    /** A menu's items as text: a separator "---", a row "[Each note|Across]", a greyed item with "(off)" after it. */
    std::vector<std::string> menuText (const std::vector<ui::roll::MenuItem>& items)
    {
        std::vector<std::string> out;
        for (const auto& it : items)
        {
            if (it.kind == ui::roll::MenuItem::Kind::separator)
                out.push_back ("---");
            else if (it.kind == ui::roll::MenuItem::Kind::row)
            {
                std::string s = "[";
                for (std::size_t i = 0; i < it.row.size(); ++i)
                    s += (i > 0 ? "|" : "") + it.row[i].label;
                out.push_back (s + "]");
            }
            else
                out.push_back (it.label + (it.disabled ? " (off)" : ""));
        }
        return out;
    }

    std::string joinedText (const std::vector<std::string>& lines)
    {
        std::string s;
        for (const auto& l : lines)
            s += (s.empty() ? "" : "; ") + l;
        return s;
    }

    /** x positions on a slider where each gives another value, spread over its track. */
    std::vector<float> slideStops (const ui::roll::ToolSlider& s, int count)
    {
        std::vector<float> xs;
        std::vector<double> values;
        for (int x = 0; x <= s.getWidth(); ++x)
        {
            const double v = s.valueAt ((float) x);
            if (values.empty() || ! juce::exactlyEqual (v, values.back()))
            {
                values.push_back (v);
                xs.push_back ((float) x);
            }
        }
        std::vector<float> out;
        if (xs.size() < 2)
            return out;
        for (int i = 0; i < count; ++i)
            out.push_back (xs[(std::size_t) ((double) (xs.size() - 1) * (0.1 + 0.8 * i / juce::jmax (1, count - 1)))]);
        return out;
    }
}

SL_TEST ("ui", selectionToolsDrawerSweep)
{
    section ("Tools through the UI layer (S2): every drawer action and lane-menu item on every fixture clip (every note, and a seeded half) and "
             "every lane gives the bytes of the direct model call, with 1 undo step and its label each (0 when nothing changes)");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    int runs = 0, same = 0, stepsRight = 0, changed = 0, chains = 0, noBake = 0, noBakeToast = 0, menuItems = 0, greyed = 0, menuRuns = 0;
    std::map<std::string, int> families;
    std::string firstBad;
    std::uint32_t seed = 733;
    for (const auto& [name, clip] : clips)
    {
        if (clip.notes.empty())
            continue;
        for (int half = 0; half < 2; ++half)
        {
            IdList sel;
            for (const auto& n : clip.notes)
                if (half == 0 || core::rand01 (core::hash32 ((std::int32_t) seed, (std::int32_t) sel.size(), (std::int32_t) n->p)) < 0.5)
                    sel.push_back (n->id);
            ++seed;
            if (sel.empty())
                sel.push_back (clip.notes.front()->id);
            const IdList ids = inTimeOrder (clip, sel);
            const auto before = model::encodeClip (clip);
            for (int li = 0; li < core::kNumLanes; ++li)
            {
                const auto L = static_cast<core::LaneId> (li);
                ToolRig probe (clip);
                prepare (probe, sel, L);
                const auto opts = firstShapeOpts (probe);
                const bool genNow = hasGen (clip, ids, L);
                auto list = drawerActions (L, probe.tools->shapeKeys (L), opts, genNow);

                // the lane menu's items that act: its shapes, the drawers' pattern and random, Re-roll, Remove, Turn into nodes
                const auto menu = menuOf (probe, L);
                for (std::size_t i = 0; i < menu.items.size(); ++i)
                {
                    const auto& item = menu.items[i];
                    if (item.kind != ui::roll::MenuItem::Kind::item)
                        continue;
                    auto direct = menuDirect (item.label, L, opts);
                    if (! direct)
                        continue;
                    ++menuItems;
                    if (item.disabled)
                    {
                        ++greyed;
                        continue;
                    }
                    list.push_back ({ "Menu " + item.label,
                                      [i, L] (ToolRig& t)
                                      {
                                          auto m = menuOf (t, L);
                                          if (i < m.items.size() && m.items[i].action >= 0 && m.items[i].action < (int) m.actions.size())
                                              m.actions[(std::size_t) m.items[i].action]();
                                      },
                                      std::move (direct->first), direct->second });
                }

                for (const auto& a : list)
                {
                    ToolRig t (clip);
                    prepare (t, sel, L);
                    a.run (t);
                    const auto want = a.direct (clip, ids);
                    const auto wantBytes = want ? model::encodeClip (*want) : before;
                    const bool didChange = wantBytes != before;
                    const bool bytesOk = t.bytes() == wantBytes;
                    const bool stepsOk = a.chain > 0 ? t.rig.steps == a.chain && t.lastLabel() == a.label
                                                     : didChange ? t.rig.steps == 1 && t.lastLabel() == a.label : t.rig.steps == 0;
                    ++runs;
                    ++families[a.name.substr (0, a.name.find (' '))];
                    changed += didChange ? 1 : 0;
                    chains += a.chain > 0 ? 1 : 0;
                    menuRuns += a.name.rfind ("Menu ", 0) == 0 ? 1 : 0;
                    same += bytesOk ? 1 : 0;
                    stepsRight += stepsOk ? 1 : 0;
                    if (! want)
                    {
                        // Turn into nodes with nothing on the lane: the prototype's toast, nothing changed
                        ++noBake;
                        noBakeToast += t.lastToast() == "No pattern or random on " + std::string (core::laneInfo (L).name) + " for the selected notes" ? 1 : 0;
                    }
                    if ((! bytesOk || ! stepsOk) && firstBad.empty())
                        firstBad = fmt ("%s on %s (%s, %s): bytes %s, %d undo steps (\"%s\", want \"%s\")", a.name.c_str(), name.c_str(),
                                        half == 0 ? "every note" : "half", core::laneInfo (L).name, bytesOk ? "the same" : "DIFFER", t.rig.steps,
                                        t.lastLabel().c_str(), a.label.c_str());
                }
            }
        }
    }
    std::string fams;
    for (const auto& [f, n] : families)
        fams += (fams.empty() ? "" : ", ") + f + " " + std::to_string (n);
    note (fmt ("%d runs (%s); %d changed the clip; %d chains of two clicks; %d lane-menu items, %d greyed with nothing on the lane; Turn into "
               "nodes with nothing to turn: %d%s%s",
               runs, fams.c_str(), changed, chains, menuItems, greyed, noBake, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (runs >= 5000 && same == runs, fmt ("%d of %d runs: the drawer action or menu item gives the bytes of the direct model call", same, runs));
    check (stepsRight == runs && changed >= runs / 2 && runs - changed >= 100,
           fmt ("%d of %d runs: 1 undo step with its label when the clip changed (%d did), 2 for each chain (%d), none otherwise (%d)", stepsRight,
                runs, changed, chains, runs - changed));
    check (menuItems > 1000 && greyed > 100 && menuRuns == menuItems - greyed && chains > 100,
           fmt ("the lane menus: %d items run, %d greyed (nothing on the lane to re-roll, remove or turn)", menuRuns, greyed));
    check (noBake > 0 && noBakeToast == noBake,
           fmt ("%d of %d: Turn into nodes with no pattern or random on the lane says so and changes nothing", noBakeToast, noBake));
}

SL_TEST ("ui", selectionToolsGenLive)
{
    section ("Live sliders (decision 2): before Apply a Pattern or Random control only draws a ghost; after Apply each slider move is a new "
             "clip version for the engine with no undo step, and the release is exactly 1 undo step; the drawers read the primary note's own "
             "generator");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const model::Clip& clip = clips["voice-lead"];
    const auto all = allIds (clip);
    const auto tone = core::LaneId::tone;

    ToolRig t (clip);
    prepare (t, all, tone);
    t.lane (tone, true);
    const auto ids = t.tools->ids();
    auto patternAt = [&clip, &ids, tone] (double d0)
    {
        auto g = startPattern();
        g.d0 = d0;
        return model::encodeClip (expectedPutGen (clip, ids, tone, g));
    };

    // before Apply: a slider (its move and its release) draws a ghost of the result and changes nothing
    t.settings().pat.d0 = 0.6;
    t.tools->genChanged (model::GenKind::pattern, false, "depth");
    t.tools->genChanged (model::GenKind::pattern, true, "depth");
    const bool preview = t.rig.steps == 0 && t.versions == 0 && t.ghosts.isShown() && t.drawnClip != nullptr
                         && model::encodeClip (*t.drawnClip) == patternAt (0.6) && t.bytes() == model::encodeClip (clip);

    // Apply: one step, and from then on the drawer's controls change the notes live
    t.tools->applyGen (model::GenKind::pattern);
    const auto applied = t.bytes();
    const int versionsApplied = t.versions;
    const bool apply = t.rig.steps == 1 && t.lastLabel() == "Pattern (Tone)" && applied == patternAt (0.6) && t.tools->isEditing (model::GenKind::pattern)
                       && t.tools->isLiveGen() && ! t.ghosts.isShown();

    // a drag of Depth at start: six moves, each a new clip version (each one goes to the engine) and no undo step
    int movesRight = 0;
    const double path[] = { 0.55, 0.4, 0.25, 0.1, 0.3, 0.75 };
    for (const double v : path)
    {
        t.settings().pat.d0 = v;
        t.tools->genChanged (model::GenKind::pattern, false, "depth");
        t.tools->sync();
        movesRight += t.bytes() == patternAt (v) && t.rig.steps == 1 && t.tools->isLiveGen() ? 1 : 0;
    }
    const int moveVersions = t.versions - versionsApplied;
    // the release: exactly one undo step
    t.tools->genChanged (model::GenKind::pattern, true, "depth");
    const bool released = t.rig.steps == 2 && t.lastLabel() == "Pattern depth (Tone)" && t.bytes() == patternAt (0.75);
    t.rig.undo();
    t.tools->sync();
    const bool undone = t.bytes() == applied;
    check (preview && apply && movesRight == 6 && moveVersions == 6 && released && undone,
           fmt ("Pattern: before Apply its slider draws the ghost and changes nothing (%d); Apply is one step (%d); 6 moves, %d right with no step, "
                "%d clip versions sent; the release is one step \"%s\" (%d steps); Undo goes back to Apply's clip (%d)",
                (int) preview, (int) apply, movesRight, moveVersions, t.lastLabel().c_str(), t.rig.steps, (int) undone));

    // Random's Amount and Smooth the same way
    ToolRig r (clip);
    prepare (r, all, tone);
    r.lane (tone, true);
    r.tools->applyGen (model::GenKind::random);
    const int rv = r.versions;
    for (const double v : { 0.2, 0.9, 0.7 })
    {
        r.settings().rnd.amount = v;
        r.tools->genChanged (model::GenKind::random, false, "amount");
        r.tools->sync();
    }
    const bool liveSteps = r.rig.steps == 1 && r.versions - rv == 3;
    r.tools->genChanged (model::GenKind::random, true, "amount");
    auto g = startRandom();
    g.amount = 0.7;
    const bool amount = r.rig.steps == 2 && r.lastLabel() == "Random amount (Tone)" && r.bytes() == model::encodeClip (expectedPutGen (clip, ids, tone, g));
    r.settings().rnd.smooth = 0.5;
    r.tools->genChanged (model::GenKind::random, false, "smooth");
    r.tools->genChanged (model::GenKind::random, true, "smooth");
    g.smooth = 0.5;
    const bool smooth = r.rig.steps == 3 && r.lastLabel() == "Random smooth (Tone)" && r.bytes() == model::encodeClip (expectedPutGen (clip, ids, tone, g));
    check (liveSteps && amount && smooth,
           fmt ("Random: after Apply 3 Amount moves are 3 clip versions with no step (%d); the release is one step \"Random amount (Tone)\" (%d); "
                "Smooth the same (%d); %d steps in all", (int) liveSteps, (int) amount, (int) smooth, r.rig.steps));

    // another selection ends the session: its last move becomes a step
    r.settings().rnd.amount = 0.3;
    r.tools->genChanged (model::GenKind::random, false, "amount");
    r.select ({ all.front() });
    const bool ended = r.rig.steps == 4 && ! r.tools->isLiveGen() && r.lastLabel() == "Random amount (Tone)";
    check (ended, fmt ("a move left without its release ends with another selection as one step (%d steps, \"%s\")", r.rig.steps, r.lastLabel().c_str()));

    // the drawers read the primary note's generator on the lane (sl-ui-panels.js loadGenValues)
    const model::Clip& gens = clips["gens"];
    ToolRig s (gens);
    s.select ({ "gn-1", "gn-2" }, "gn-2");
    s.lane (core::LaneId::formant);
    const auto& P = s.settings().pat;
    const bool readPattern = P.shape == model::GenShape::sawup && P.rate == core::RateId::r1_4T && juce::exactlyEqual (P.warp, 0.8) && ! P.lockToBeat
                             && s.tools->isEditing (model::GenKind::pattern) && ! s.tools->isEditing (model::GenKind::random);
    s.select ({ "gn-1", "gn-2" }, "gn-1");
    s.lane (core::LaneId::tone);
    const auto& R = s.settings().rnd;
    const bool readRandom = juce::exactlyEqual (R.smooth, 0.5) && juce::exactlyEqual (R.amount, 0.5) && R.rate == core::RateId::r1_16T
                            && s.tools->isEditing (model::GenKind::random);
    s.lane (core::LaneId::amp);
    const bool readAmp = s.settings().pat.shape == model::GenShape::triangle && s.settings().pat.rate == core::RateId::r1_8
                         && juce::exactlyEqual (s.settings().pat.d0, 0.4) && juce::exactlyEqual (s.settings().pat.d1, 0.1);
    check (readPattern && readRandom && readAmp,
           fmt ("the drawers show the primary note's generator: gn-2's Formant pattern (Saw up, 1/4 triplet, speeds up 0.8, unlocked: %d), "
                "gn-1's Tone random (Smooth 50%%: %d), gn-1's Volume pattern (Triangle 1/8, 40%% to 10%%: %d)",
                (int) readPattern, (int) readRandom, (int) readAmp));
}

SL_TEST ("ui", selectionToolsRandomSeeds)
{
    section ("Random stays put until Re-roll (decision 3): the same clip and seed give the same clip (on two windows, and after Undo); Re-roll "
             "moves every note's seed on by one and changes the clip; Amount keeps the seeds; the Random shape deals new values on each click");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const model::Clip& clip = clips["voice-lead"];
    const auto all = allIds (clip);
    const auto tone = core::LaneId::tone;

    auto randomOn = [&clip, &all, tone]
    {
        auto t = std::make_unique<ToolRig> (clip);
        prepare (*t, all, tone);
        t->settings().rnd.perNote = model::PerNote::different;
        t->tools->applyGen (model::GenKind::random);
        return t;
    };
    auto tables = [tone] (const model::Clip& c)
    {
        std::vector<std::vector<float>> out;
        for (const auto& n : c.notes)
            out.push_back (model::laneTable (*n, tone));
        return out;
    };
    auto seedsAre = [tone] (const model::Clip& c, std::int32_t want)
    {
        for (const auto& n : c.notes)
            if (! n->genOf (tone) || n->genOf (tone)->seed != want)
                return false;
        return true;
    };

    auto a = randomOn();
    auto b = randomOn();
    const auto first = a->bytes();
    const auto tablesFirst = tables (*a->rig.doc.current());
    const bool twoWindows = first == b->bytes() && tablesFirst == tables (*b->rig.doc.current()) && seedsAre (*a->rig.doc.current(), 1);
    // Different per note: the notes' lines differ (two notes of the same length and drawing)
    const bool different = tablesFirst.size() > 2 && tablesFirst[1] != tablesFirst[2];

    // Undo, then Random again: the same clip
    a->rig.undo();
    a->tools->sync();
    a->tools->applyGen (model::GenKind::random);
    const bool again = a->bytes() == first;

    // Re-roll: every note's seed + 1, a clip with other values (the model's reroll)
    a->tools->rerollGen();
    const auto rolled = a->rig.doc.current();
    const bool rerolled = seedsAre (*rolled, 2) && a->bytes() != first && tables (*rolled) != tablesFirst
                          && a->bytes() == model::encodeClip (model::reroll (*b->rig.doc.current(), b->tools->ids(), tone)) && a->lastLabel() == "Re-roll (Tone)";

    // Amount after the re-roll keeps the seeds (the random line keeps its steps, scaled)
    a->settings().rnd.amount = 0.9;
    a->tools->genChanged (model::GenKind::random, true, "amount");
    const bool kept = seedsAre (*a->rig.doc.current(), 2) && a->lastLabel() == "Random amount (Tone)";
    check (twoWindows && different && again && rerolled && kept,
           fmt ("Random on Tone, Different per note: two windows give the same bytes and tables, seed 1 (%d), each note its own line (%d); Undo and "
                "Random again: the same clip (%d); Re-roll: seed 2 on every note and other values (%d); Amount keeps seed 2 (%d)",
                (int) twoWindows, (int) different, (int) again, (int) rerolled, (int) kept));

    // the Random shape (Shapes): the same seed gives the same nodes; each click moves the seed on, so the next deals new ones
    ToolRig s1 (clip), s2 (clip);
    prepare (s1, all, core::LaneId::amp);
    prepare (s2, all, core::LaneId::amp);
    const auto opts = firstShapeOpts (s1);
    s1.tools->applyShape (model::ShapeKey::random);
    s2.tools->applyShape (model::ShapeKey::random);
    const auto once = s1.bytes();
    const bool sameShape = once == s2.bytes() && juce::exactlyEqual (s1.settings().shapeSeed, 2.0)
                           && once == model::encodeClip (*model::applyShape (clip, s1.tools->ids(), core::LaneId::amp, model::ShapeKey::random, model::ShapeScope::each, opts));
    const auto afterOnce = s1.rig.doc.current();
    s1.tools->applyShape (model::ShapeKey::random);
    auto opts2 = opts;
    opts2.seed = 2.0;
    const bool next = s1.bytes() != once
                      && s1.bytes() == model::encodeClip (*model::applyShape (*afterOnce, s1.tools->ids(), core::LaneId::amp, model::ShapeKey::random, model::ShapeScope::each, opts2))
                      && juce::exactlyEqual (s1.settings().shapeSeed, 3.0);
    check (sameShape && next, fmt ("the Random shape: the same seed, the same nodes on two windows (%d); the second click uses seed 2 and deals "
                                   "other nodes (%d)", (int) sameShape, (int) next));
}

SL_TEST ("ui", selectionToolsLaneMenu)
{
    section ("The lane buttons' menus (Y2-Y4, sl-ui-panels.js openLaneMenu): the prototype's items in its order; greyed with no pattern or random "
             "on the lane; Each note / Across keeps the menu open; Pattern settings... and Random settings... open that drawer for the lane");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const model::Clip& grid = clips["connect-grid"];
    const std::string dots = "\xe2\x80\xa6";
    const auto tone = core::LaneId::tone;

    ToolRig t (grid);
    prepare (t, { grid.notes[0]->id, grid.notes[1]->id, grid.notes[2]->id }, tone);
    int reopened = 0, opened = 0;
    auto openedWhich = ToolSettings::Drawer::none;
    auto& tools = *t.tools;
    auto menu = ui::roll::laneMenu (tools, tone, tools.ids(), [&reopened] { ++reopened; }, [&tools] (model::ShapeScope s) { tools.setScope (s); },
                                    [&opened, &openedWhich] (ToolSettings::Drawer d) { ++opened; openedWhich = d; });
    const std::vector<std::string> want { "Tone on 3 notes", "[Each note|Across]", "Ramp up", "Ramp down", "Swell", "Dip", "Squiggle", "Random", "Decay",
                                          "Reset", "---", "Pattern: squiggle 1/16 triplet", "Pattern settings" + dots, "Random 1/16 triplet",
                                          "Random settings" + dots, "Re-roll the random (off)", "Remove pattern / random (off)", "Turn into nodes (off)" };
    const auto got = menuText (menu.items);
    // the mouse on a shape, the pattern, the random, Remove and Turn into nodes draws their ghost; on the others nothing
    int ghosts = 0, noGhost = 0;
    for (const auto& it : menu.items)
    {
        const bool has = it.hover >= 0 && it.hover < (int) menu.hovers.size();
        const bool should = it.kind == ui::roll::MenuItem::Kind::item && it.label.find ("settings") == std::string::npos && it.label != "Re-roll the random";
        ghosts += has && should ? 1 : 0;
        noGhost += ! has && ! should ? 1 : 0;
    }
    const bool header = ! menu.items.empty() && menu.items[0].kind == ui::roll::MenuItem::Kind::header && menu.items[0].colour == tone
                        && menu.items[1].row.size() == 2 && menu.items[1].row[0].on && ! menu.items[1].row[1].on && menu.items[1].row[1].keepOpen;
    check (got == want && header && ghosts == 12 && noGhost == (int) menu.items.size() - 12,
           fmt ("Tone on 3 notes with nothing on the lane: %s (the header in Tone's colour, Each note lit: %d); 12 items draw a ghost (%d), the "
                "header, the row, the line, the two settings items and Re-roll none (%d)", joinedText (got).c_str(), (int) header, ghosts, noGhost));

    // Across: the scope changes and the menu opens again in its place
    const auto& across = menu.items[1].row[1];
    menu.actions[(std::size_t) across.action]();
    const bool scoped = tools.scope() == model::ShapeScope::across && reopened == 1;
    // Pattern settings...: the drawer opens for this lane (the bar's Lane becomes it)
    t.settings().lane = core::LaneId::amp;
    menu.actions[(std::size_t) menu.items[12].action]();
    const bool patternDrawer = opened == 1 && openedWhich == ToolSettings::Drawer::pattern && t.settings().lane == tone;
    menu.actions[(std::size_t) menu.items[14].action]();
    const bool randomDrawer = opened == 2 && openedWhich == ToolSettings::Drawer::random;
    check (scoped && patternDrawer && randomDrawer && t.rig.steps == 0,
           fmt ("Across sets the scope and reopens the menu (%d); Pattern settings... opens the Pattern drawer on Tone (%d), Random settings... the "
                "Random one (%d); none of them is an undo step", (int) scoped, (int) patternDrawer, (int) randomDrawer));

    // the drawers' settings name the menu's pattern and random
    t.settings().pat.shape = model::GenShape::sawup;
    t.settings().pat.rate = core::RateId::r1_8D;
    t.settings().rnd.rate = core::RateId::r1bar;
    const auto named = ui::roll::laneMenu (tools, tone, tools.ids(), {}, {}, {});
    check (named.items.size() > 13 && named.items[11].label == "Pattern: saw up 1/8 dotted" && named.items[13].label == "Random 1 bar",
           fmt ("with Saw up at 1/8 dotted and Random at 1 bar the items read \"%s\" and \"%s\"", named.items.size() > 13 ? named.items[11].label.c_str() : "",
                named.items.size() > 13 ? named.items[13].label.c_str() : ""));

    // with a random on Tone (gens: gn-1): Re-roll, Remove and Turn into nodes work, each one undo step
    const model::Clip& gens = clips["gens"];
    ToolRig g (gens);
    prepare (g, { "gn-1", "gn-5" }, tone);
    auto gm = menuOf (g, tone);
    const auto gt = menuText (gm.items);
    const bool on = gt.size() == 18 && gt[15] == "Re-roll the random" && gt[16] == "Remove pattern / random" && gt[17] == "Turn into nodes"
                    && gm.items[0].label == "Tone on 2 notes";
    gm.actions[(std::size_t) gm.items[15].action]();
    const bool rerolled = g.rig.steps == 1 && g.lastLabel() == "Re-roll (Tone)" && g.bytes() == model::encodeClip (model::reroll (gens, g.tools->ids(), tone));
    check (on && rerolled, fmt ("gn-1 and gn-5, gn-1 with a random on Tone: %s; Re-roll is one step equal to the model's (%d)", joinedText (gt).c_str(),
                                (int) rerolled));

    // Travel: the notes' kind's shapes (Speed: 12, Path: 11, both: 15); the other lanes the 8 common shapes
    const model::Clip& s4 = clips["shapes4"];
    auto shapeItems = [&s4] (const IdList& sel, core::LaneId lane)
    {
        ToolRig r (s4);
        prepare (r, sel, lane);
        int n = 0;
        const auto m = menuOf (r, lane);
        for (const auto& it : m.items)
            n += shapeNamed (it.label) ? 1 : 0;
        return n;
    };
    const int speed = shapeItems ({ "s4-a", "s4-d" }, core::LaneId::travel), path = shapeItems ({ "s4-c" }, core::LaneId::travel),
              both = shapeItems ({ "s4-a", "s4-c" }, core::LaneId::travel), heads = shapeItems ({ "s4-c" }, core::LaneId::heads),
              pitch = shapeItems ({ "s4-a", "s4-c" }, core::LaneId::pitch);
    check (speed == 12 && path == 11 && both == 15 && heads == 8 && pitch == 8,
           fmt ("shapes on Travel: Speed notes %d (12), a Path note %d (11), both kinds %d (15); on Heads %d and Pitch %d (8)", speed, path, both,
                heads, pitch));
}

SL_TEST ("ui", selectionBarDrawersInTheEditor)
{
    section ("The S2 drawers in the editor: Shapes' buttons, Pattern's Apply, shape chips, Re-roll, Remove and Turn into nodes, Random's Apply and "
             "the bar's Turn into nodes run their model calls (bytes identical, one undo step each); a Pattern slider after Apply is live (the "
             "SnapshotPublisher takes every move) and one undo step on release; the lane arrows open their menus");

    StrandLockProcessor proc;
    {
        auto& s = proc.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }
    {
        StrandLockEditor editor (proc);
        auto& view = editor.getView();
        auto& roll = view.getRoll();
        auto& bar = view.getSelectionBar();
        roll.setMenusShown (false);
        view.setFolded (true);
        editor.refreshNow();
        roll.selectAll();
        bar.getLaneBox().setSelectedId (core::laneIndex (core::LaneId::tone) + 1, juce::sendNotificationSync);
        editor.refreshNow();
        const auto tone = core::LaneId::tone;
        const auto clip0 = proc.getDocument().current();
        const auto ids = roll.getTools().ids();
        const auto& rs = roll.getGestures().settings();
        model::ShapeOpts opts;
        opts.seed = 1.0;
        opts.bpm = rs.bpm;
        opts.sampleSeconds = rs.sampleSeconds;
        opts.inst = rs.inst;
        auto bytesNow = [&proc] { return model::encodeClip (*proc.getDocument().current()); };
        auto label = [&proc] { return proc.undoLabel().value_or (""); };

        // Shapes: every shape button is one step equal to the model's applyShape; Undo puts the clip back
        bar.getDrawerButton (ToolSettings::Drawer::shapes).onClick();
        std::vector<model::ShapeKey> keys;
        if (auto* sd = dynamic_cast<ui::roll::ShapesDrawer*> (bar.getOpenDrawer()))
            keys = sd->getKeys();
        int shapesRight = 0;
        double seedNow = 1.0;
        for (const auto k : keys)
        {
            auto* sd = dynamic_cast<ui::roll::ShapesDrawer*> (bar.getOpenDrawer());
            auto* b = sd != nullptr ? sd->findShape (k) : nullptr;
            if (b == nullptr)
                continue;
            auto o = opts;
            o.seed = seedNow;
            b->onClick();
            editor.refreshNow();
            const auto want = model::applyShape (*clip0, ids, tone, k, model::ShapeScope::each, o);
            const bool ok = want && bytesNow() == model::encodeClip (*want) && label() == std::string (model::shapeName (k)) + " (Tone)";
            if (k == model::ShapeKey::random)
                seedNow += 1.0;
            proc.undo();
            editor.refreshNow();
            shapesRight += ok && proc.getDocument().current() != nullptr && bytesNow() == model::encodeClip (*clip0) ? 1 : 0;
        }
        check (keys.size() == 8 && shapesRight == 8, fmt ("Shapes on Tone: %d of %d shape buttons are one undo step equal to the model's applyShape",
                                                         shapesRight, (int) keys.size()));

        // Pattern: Apply, the shape chips (live: one step each), Re-roll on a random shape, Remove, Apply, Turn into nodes
        bar.getDrawerButton (ToolSettings::Drawer::pattern).onClick();
        auto gen = [&bar] { return dynamic_cast<ui::roll::GenDrawer*> (bar.getOpenDrawer()); };
        bool applied = false, chips = true, rerolled = false, removed = false, baked = false;
        if (gen() != nullptr && gen()->getKind() == model::GenKind::pattern && gen()->getCaption().getText() == "Pattern on Tone")
        {
            gen()->getApply().onClick();
            editor.refreshNow();
            applied = bytesNow() == model::encodeClip (expectedPutGen (*clip0, ids, tone, startPattern())) && label() == "Pattern (Tone)";
            for (const int s : { 1, 2, 3, 4, 0, 5, 6 })
            {
                const auto shape = static_cast<model::GenShape> (s);
                gen()->getShapes()->getButton (s).onClick();
                editor.refreshNow();
                chips = chips && bytesNow() == model::encodeClip (expectedPutGen (*clip0, ids, tone, startPattern (shape))) && label() == "Pattern shape (Tone)";
            }
            // Smooth random: its Re-roll shows
            auto& rr = gen()->getReroll();
            if (rr.isVisible() && rr.isEnabled())
            {
                const auto before = proc.getDocument().current();
                rr.onClick();
                editor.refreshNow();
                rerolled = bytesNow() == model::encodeClip (model::reroll (*before, ids, tone)) && label() == "Re-roll (Tone)";
            }
            {
                const auto before = proc.getDocument().current();
                gen()->getRemove().onClick();
                editor.refreshNow();
                removed = bytesNow() == model::encodeClip (model::clearGen (*before, ids, tone)) && label() == "Remove pattern (Tone)"
                          && ! gen()->getRemove().isEnabled() && ! bar.getBakeButton().isEnabled();
            }
            gen()->getApply().onClick();
            editor.refreshNow();
            {
                const auto before = proc.getDocument().current();
                gen()->getBake().onClick();
                editor.refreshNow();
                baked = bytesNow() == model::encodeClip (model::bakeGen (*before, ids, tone)) && label() == "Turn into nodes (Tone)";
            }
        }
        check (applied && chips && rerolled && removed && baked,
               fmt ("Pattern on Tone: Apply (%d), each shape chip after it one live step (%d), Re-roll on Smooth random (%d), Remove (%d, the "
                    "buttons grey), Apply and Turn into nodes (%d): each the bytes of its model call", (int) applied, (int) chips, (int) rerolled,
                    (int) removed, (int) baked));

        // a drag of Depth at start after Apply: every move published, no undo step until the release, which is exactly one
        gen()->getApply().onClick();
        editor.refreshNow();
        const auto appliedClip = proc.getDocument().current();
        const std::string appliedLabel = label();
        int moves = 0, held = 0;
        bool slid = false, oneStep = false;
        std::uint64_t published = 0;
        if (auto* depth = gen() != nullptr ? gen()->findSlider ("Depth at start") : nullptr)
        {
            const auto stops = slideStops (*depth, 5);
            const auto submitted0 = proc.getSnapshotPublisher().stats().submitted;
            for (const float x : stops)
            {
                depth->dragTo (x);
                editor.refreshNow();
                ++moves;
                held += label() == appliedLabel && ! proc.canRedo() ? 1 : 0;
            }
            published = proc.getSnapshotPublisher().stats().submitted - submitted0;
            const double v = depth->getValue();
            depth->release();
            editor.refreshNow();
            auto gd = startPattern (model::GenShape::smoothrandom);   // the shape the chips left picked
            gd.d0 = v;
            slid = bytesNow() == model::encodeClip (expectedPutGen (*appliedClip, ids, tone, gd)) && label() == "Pattern depth (Tone)";
            proc.undo();
            editor.refreshNow();
            oneStep = proc.getDocument().current() != nullptr && model::encodeClip (*proc.getDocument().current()) == model::encodeClip (*appliedClip)
                      && label() == appliedLabel;
        }
        check (moves == 5 && held == 5 && published >= 5 && slid && oneStep,
               fmt ("Depth at start dragged after Apply: %d moves, %d with no new undo step, %d clips handed to the SnapshotPublisher; the release "
                    "is one step \"Pattern depth (Tone)\" (%d); one Undo goes back to Apply's clip (%d)", moves, held, (int) published, (int) slid,
                    (int) oneStep));

        // Random: Apply, then the bar's Turn into nodes
        bar.getDrawerButton (ToolSettings::Drawer::random).onClick();
        bool randomApplied = false, barBaked = false;
        if (gen() != nullptr && gen()->getKind() == model::GenKind::random)
        {
            const auto before = proc.getDocument().current();
            gen()->getApply().onClick();
            editor.refreshNow();
            randomApplied = bytesNow() == model::encodeClip (expectedPutGen (*before, ids, tone, startRandom())) && label() == "Random (Tone)"
                            && bar.getBakeButton().isEnabled();
            const auto rolled = proc.getDocument().current();
            bar.getBakeButton().onClick();
            editor.refreshNow();
            barBaked = bytesNow() == model::encodeClip (model::bakeGen (*rolled, ids, tone)) && label() == "Turn into nodes (Tone)";
        }
        check (randomApplied && barBaked, fmt ("Random's Apply (%d) and the bar's Turn into nodes (%d): one step each, the model's bytes",
                                               (int) randomApplied, (int) barBaked));

        // the lane arrows: each opens its lane's menu and shows the lane; with nothing selected a toast says how
        int menus = 0;
        for (int m = 2; m <= 8; ++m)
        {
            view.getLaneArrow (m).onClick();
            const auto& items = roll.getMenu();
            const auto lane = m == 8 ? proc.getDocument().rollState().subLane : core::kModes[m - 1].lanes[0];
            const std::string head = std::string (core::laneInfo (lane).name) + " on " + ui::roll::nNotes ((int) ids.size());
            menus += ! items.empty() && items.front().label == head && view.getMode() == m ? 1 : 0;
            roll.closeMenu();
        }
        roll.clearSelection();
        editor.refreshNow();
        view.getLaneArrow (3).onClick();
        const bool told = roll.getMenu().empty() && view.isToastShown() && view.getToastText() == ui::roll::kLaneMenuNothingSelected;
        check (menus == 7 && told, fmt ("%d of 7 lane arrows open their lane's menu (\"Volume on %d notes\" ...) and show the lane; with nothing "
                                        "selected the arrow says \"%s\"", menus, (int) ids.size(), view.getToastText().toRawUTF8()));
    }
}

//==================================================================================================
// Pictures for review: the bar with the Connect drawer, the Tools drawer with Walk's panel, Chop refused, and (S2) the
// Shapes drawer on Travel, the Pattern drawer after Apply on a random shape, and the Random drawer on Pitch.

namespace
{
    enum class BarShot { connect, walk, chopRefused, idle, shapesTravel, pattern, random };

    juce::Image shootBar (ThemeId theme, BarShot what, int width = 1100, int height = 680)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();
        auto& settings = proc.getUserSettings();
        settings.setTheme (theme);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        juce::Image image;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (width, height);
            auto& view = editor.getView();
            auto& roll = view.getRoll();
            auto& bar = view.getSelectionBar();
            roll.setMenusShown (false);
            view.setFolded (true);
            editor.refreshNow();
            if (what == BarShot::chopRefused)
            {
                // 64 notes of 4 beats: Chop at 1/32 would make 2048
                model::Clip clip = model::newClip (32.0);
                for (int i = 0; i < 64; ++i)
                    clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote ((double) (60 + i % 5), (double) (i / 2) * 4.0, 4.0,
                                                                                               "ch-" + std::to_string (i))));
                proc.editClip ("Start", clip);
                roll.getTools().settings().chop.rate = core::RateId::r1_32;
            }
            if (what == BarShot::shapesTravel || what == BarShot::pattern || what == BarShot::random)
            {
                // the lane drawers (S2): Shapes on Travel, Pattern on Tone after Apply with Random steps picked, Random on Pitch
                roll.selectAll();
                const auto lane = what == BarShot::shapesTravel ? core::LaneId::travel : what == BarShot::pattern ? core::LaneId::tone : core::LaneId::pitch;
                bar.getLaneBox().setSelectedId (core::laneIndex (lane) + 1, juce::sendNotificationSync);
                bar.getDrawerButton (what == BarShot::shapesTravel ? ToolSettings::Drawer::shapes
                                     : what == BarShot::pattern     ? ToolSettings::Drawer::pattern
                                                                    : ToolSettings::Drawer::random).onClick();
                if (auto* gen = dynamic_cast<ui::roll::GenDrawer*> (bar.getOpenDrawer()))
                {
                    gen->getApply().onClick();
                    if (what == BarShot::pattern && gen->getShapes() != nullptr)
                        gen->getShapes()->getButton (static_cast<int> (model::GenShape::randsteps)).onClick();
                }
            }
            else if (what != BarShot::idle)
            {
                roll.selectAll();
                view.setMode (5);
                bar.getDrawerButton (what == BarShot::connect ? ToolSettings::Drawer::connect : ToolSettings::Drawer::tools).onClick();
                if (auto* connect = dynamic_cast<ui::roll::ConnectDrawer*> (bar.getOpenDrawer()))
                    connect->getButton ("Nearest").onClick();
                if (auto* tools = dynamic_cast<ui::roll::ToolsDrawer*> (bar.getOpenDrawer()))
                    tools->getButton ((what == BarShot::walk ? "Walk" : "Chop") + juce::String::charToString ((juce::juce_wchar) 0x2026)).onClick();
            }
            editor.refreshNow();
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        }
        settings.setTheme (state::Theme::dark);
        return image;
    }
}

SL_SNAPSHOT (m5_bar_connect_dark)        { return shootBar (ThemeId::dark, BarShot::connect); }
SL_SNAPSHOT (m5_bar_connect_light)       { return shootBar (ThemeId::light, BarShot::connect); }
SL_SNAPSHOT (m5_bar_walk_dark)           { return shootBar (ThemeId::dark, BarShot::walk); }
SL_SNAPSHOT (m5_bar_walk_light)          { return shootBar (ThemeId::light, BarShot::walk); }
SL_SNAPSHOT (m5_bar_chop_refused_light)  { return shootBar (ThemeId::light, BarShot::chopRefused); }
SL_SNAPSHOT (m5_bar_walk_narrow_dark)    { return shootBar (ThemeId::dark, BarShot::walk, 900, 560); }
SL_SNAPSHOT (m5_bar_idle_dark)           { return shootBar (ThemeId::dark, BarShot::idle); }
SL_SNAPSHOT (m5_bar_shapes_travel_light) { return shootBar (ThemeId::light, BarShot::shapesTravel); }
SL_SNAPSHOT (m5_bar_pattern_dark)        { return shootBar (ThemeId::dark, BarShot::pattern); }
SL_SNAPSHOT (m5_bar_pattern_light)       { return shootBar (ThemeId::light, BarShot::pattern); }
SL_SNAPSHOT (m5_bar_random_dark)         { return shootBar (ThemeId::dark, BarShot::random); }
SL_SNAPSHOT (m5_bar_pattern_narrow_dark) { return shootBar (ThemeId::dark, BarShot::pattern, 900, 560); }
