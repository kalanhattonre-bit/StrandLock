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

#include "RollRig.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "core/Travel.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/ModelFixture.h"
#include "model/ToolOps.h"
#include "model/Tools.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/Drawers.h"
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

    /** A roll with no window (RollRig) and the bar's SelectionTools on the same document and undo history. */
    struct ToolRig
    {
        RollRig rig;
        std::vector<std::pair<std::string, int>> toasts;
        std::unique_ptr<SelectionTools> tools;

        explicit ToolRig (const model::Clip& clip) : rig (clip)
        {
            ui::roll::ToolHost host { rig.doc,
                                      [this] (const std::string& label)
                                      {
                                          const bool pushed = rig.history.push (label, rig.doc.current(), rig.doc.nonParam(), {});
                                          if (pushed)
                                          {
                                              ++rig.steps;
                                              rig.labels.push_back (label);
                                          }
                                          return pushed;
                                      },
                                      [this] (const std::string& text, int ms) { toasts.emplace_back (text, ms); } };
            tools = std::make_unique<SelectionTools> (std::move (host), rig.gestures());
        }

        ToolSettings& settings() { return tools->settings(); }
        void select (const std::vector<std::string>& ids, const std::string& primary = {})
        {
            rig.doc.setSelection (ids, primary.empty() ? std::optional<std::string>() : std::optional<std::string> (primary));
        }
        std::vector<std::uint8_t> bytes() const { return model::encodeClip (*rig.doc.current()); }
        std::string lastToast() const { return toasts.empty() ? std::string() : toasts.back().first; }
        std::string lastLabel() const { return rig.labels.empty() ? std::string() : rig.labels.back(); }
    };

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
    check (stepsRight == runs && changed >= runs * 3 / 4, fmt ("%d of %d runs: 1 undo step with the bar's label when the clip changed (%d did), none "
                                                               "otherwise", stepsRight, runs, changed));
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

    // Legato: one chord alone has nowhere to go; notes already touching have nothing to stretch
    ToolRig l (lead);
    const auto chord = model::selectSimilar (lead, "vl-d1", model::SimilarKind::bar, model::Ids::all());
    std::vector<std::string> first;
    for (const auto& n : lead.notes)
        if (juce::exactlyEqual (n->s, lead.notes.front()->s))
            first.push_back (n->id);
    l.select (first);
    l.tools->legato();
    const bool oneChord = l.rig.steps == 0 && l.lastToast() == "No later selected note to stretch to: select the notes that follow as well";
    l.select (allIds (lead));
    l.tools->legato();
    const bool stretched = l.rig.steps == 1 && l.lastToast() == "Each note now reaches the next one: try Connect \xe2\x96\xb8 Nearest";
    l.tools->legato();
    const bool touching = l.rig.steps == 1 && l.lastToast() == "Already touching: nothing to stretch";
    check (oneChord && stretched && touching && ! chord.empty(),
           "Legato: one chord alone says to select the notes that follow; the whole clip stretches (one step); again: already touching");

    // Select similar: the selection only, with a toast; no undo step
    ToolRig s (lead);
    s.select (allIds (lead), "vl-a3");
    s.tools->selectSimilar (model::SimilarKind::pitch);
    const auto sel = s.rig.doc.rollState();
    check (sel.selected.size() == 3 && sel.primary == "vl-a3" && s.lastToast() == "3 notes selected" && s.rig.steps == 0,
           fmt ("Select similar, same pitch as vl-a3: %d notes selected, the primary stays (%s), \"%s\", no undo step", (int) sel.selected.size(),
                sel.primary.c_str(), s.lastToast().c_str()));

    // the lane tools in Build act on the bar's Lane and show its mode
    ToolRig b (lead);
    b.select (allIds (lead));
    b.settings().lane = core::LaneId::formant;
    b.tools->flip();
    const auto st = b.rig.doc.rollState();
    check (st.mode == 6 && b.lastLabel() == "Flip (Formant)" && b.bytes() == model::encodeClip (model::flipLane (lead, b.tools->ids(), core::LaneId::formant)),
           fmt ("Flip in Build acts on the Lane (Formant) and shows its mode (%d): \"%s\"", st.mode, b.lastLabel().c_str()));
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
             "(one undo step each); Walk's panel, Chop's panel, Select similar's menu, the Lane list; shapes arrive in S2");

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

        // the Lane list picks a mode; the S2 drawers say they arrive later
        bar.getLaneBox().setSelectedId (core::laneIndex (core::LaneId::hollow) + 1, juce::sendNotificationSync);
        const bool laneMode = view.getMode() == 7 && roll.getTools().lane() == core::LaneId::hollow;
        bar.getDrawerButton (ToolSettings::Drawer::shapes).onClick();
        const bool later = view.isToastShown() && view.getToastText().contains ("arrive in the next build")
                           && bar.getOpenDrawer() != nullptr && dynamic_cast<ui::roll::ToolsDrawer*> (bar.getOpenDrawer()) != nullptr;
        check (laneMode && later, fmt ("the Lane list's Hollow shows mode 7 (%d); Shapes says it arrives in the next build and keeps Tools open",
                                       view.getMode()));
    }
}

//==================================================================================================
// Pictures for review: the bar with the Connect drawer, the Tools drawer with Walk's panel, and Chop refused.

namespace
{
    enum class BarShot { connect, walk, chopRefused, idle };

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
            if (what != BarShot::idle)
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
