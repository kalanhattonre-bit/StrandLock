#include "ToolOps.h"

#include "ModelFixture.h"

#include "core/Lanes.h"
#include "core/Rates.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/Normalize.h"
#include "model/Tools.h"
#include "support/FixtureJson.h"

#include <map>
#include <utility>

namespace sl::test::toolops
{
    using core::JsonValue;

    namespace
    {
        bool isMixed (const JsonValue& v)
        {
            return v.isString() && v.asString() == "mixed";
        }

        template <class E, class Parse>
        void choiceFrom (const JsonValue& patch, const char* key, model::Choice<E>& out, Parse parse)
        {
            const JsonValue* v = patch.find (key);
            if (v == nullptr)
                return;
            if (v->isNull())
            {
                out.emplace();   // null: follow the instrument
                return;
            }
            if (v->isString())
                if (const auto known = parse (v->asString()))
                    out.emplace (*known);
        }

        void boolChoiceFrom (const JsonValue& patch, const char* key, model::Choice<bool>& out)
        {
            const JsonValue* v = patch.find (key);
            if (v == nullptr)
                return;
            if (v->isNull())
                out.emplace();
            else if (v->isBool())
                out.emplace (v->asBool());
        }

        // a number the patch holds ("NaN" stands for a NaN number; applyNotePatch ignores it)
        std::optional<double> numberFrom (const JsonValue& patch, const char* key)
        {
            const JsonValue* v = patch.find (key);
            if (v == nullptr || isMixed (*v) || ! fixture::isNum (*v))
                return std::nullopt;
            return fixture::num (*v);
        }

        std::optional<core::LaneId> laneOf (const JsonValue& v)
        {
            return v.isString() ? core::findLane (v.asString()) : std::nullopt;
        }

        std::optional<core::RateId> rateOf (const JsonValue& v)
        {
            return v.isString() ? core::parseRate (v.asString()) : std::nullopt;
        }
    }

    model::SamplePatch samplePatchFrom (const JsonValue& p)
    {
        model::SamplePatch out;
        if (! p.isObject())
            return out;
        choiceFrom (p, "motion", out.motion, [] (const std::string& s) { return core::parseMotion (s); });
        if (const JsonValue& t = p["travel"]; t.isString())
            out.travel = model::parseTravelKind (t.asString());
        boolChoiceFrom (p, "tape", out.tape);
        boolChoiceFrom (p, "rev", out.rev);
        boolChoiceFrom (p, "lock", out.lock);
        choiceFrom (p, "jump", out.jump, [] (const std::string& s) { return core::parseRate (s); });
        choiceFrom (p, "engine", out.engine, [] (const std::string& s) { return core::parseEngine (s); });
        if (const JsonValue& v = p["restart"]; v.isBool())
            out.restart = v.asBool();

        if (const JsonValue& st = p["start"]; st.isObject() || st.isArray())
        {
            model::StartPatch sp;
            if (const JsonValue& m = st["mode"]; m.isString())
                sp.mode = model::parseStartMode (m.asString());
            sp.pos = numberFrom (st, "pos");
            sp.range = numberFrom (st, "range");
            sp.seed = numberFrom (st, "seed");
            out.start = sp;
        }

        out.vel = numberFrom (p, "vel");
        if (const JsonValue* g = p.find ("glide"); g != nullptr && ! isMixed (*g))
        {
            if (g->isNull())
                out.glide.emplace();
            else if (fixture::isNum (*g))
                out.glide.emplace (fixture::num (*g));
        }
        out.gc = numberFrom (p, "gc");
        return out;
    }

    JsonValue mixedToJson (const model::MixedSettings& m)
    {
        auto field = [] (const auto& mixed, auto toJson) -> JsonValue
        {
            if (mixed.mixed)
                return JsonValue ("mixed");
            return toJson (mixed.value);
        };
        auto number = [] (double x) { return JsonValue (x); };
        auto optBool = [] (const std::optional<bool>& b) { return b.has_value() ? JsonValue (*b) : JsonValue(); };

        auto out = JsonValue::object();
        out.set ("count", JsonValue (m.count));
        auto start = JsonValue::object();
        start.set ("mode", field (m.startMode, [] (model::StartMode s) { return JsonValue (model::code (s)); }));
        start.set ("pos", field (m.startPos, number));
        start.set ("range", field (m.startRange, number));
        out.set ("start", std::move (start));
        out.set ("motion", field (m.motion, [] (const std::optional<core::Motion>& v) { return v ? JsonValue (core::code (*v)) : JsonValue(); }));
        out.set ("travel", field (m.travel, [] (model::TravelKind t) { return JsonValue (model::code (t)); }));
        out.set ("tape", field (m.tape, optBool));
        out.set ("rev", field (m.rev, optBool));
        out.set ("jump", field (m.jump, [] (const std::optional<core::RateId>& v) { return v ? JsonValue (core::rateCode (*v)) : JsonValue(); }));
        out.set ("engine", field (m.engine, [] (const std::optional<core::Engine>& v) { return v ? JsonValue (core::code (*v)) : JsonValue(); }));
        out.set ("lock", field (m.lock, optBool));
        out.set ("restart", field (m.restart, [] (bool b) { return JsonValue (b); }));
        out.set ("vel", field (m.vel, number));
        out.set ("glide", field (m.glide, [] (const std::optional<double>& v) { return v ? JsonValue (*v) : JsonValue(); }));
        out.set ("gc", field (m.gc, number));
        return out;
    }

    OpResult run (const JsonValue& c, const model::Clip& in)
    {
        OpResult r;
        const std::string op = c["op"].asString();
        const JsonValue& a = c["args"];
        const model::Ids ids = modelfx::idsFrom (a["ids"]);
        const auto lane = laneOf (a["lane"]);
        auto fail = [&r] (const std::string& why) { r.why = why; };

        if (op == "connect")
        {
            const std::string mode = a["mode"].asString();
            const auto m = mode == "random" ? model::ConnectMode::random : mode == "furthest" ? model::ConnectMode::furthest : model::ConnectMode::nearest;
            r.clip = model::connect (in, ids, m, fixture::num (a["seed"]));
        }
        else if (op == "disconnect")
        {
            r.clip = model::disconnect (in, ids);
        }
        else if (op == "chop")
        {
            const JsonValue& ss = a["stepStart"];
            const model::ChopStart start = ss.isBool() && ss.asBool() ? model::ChopStart::evenly() : model::ChopStart::by (fixture::num (ss));
            const int maxNotes = a["maxNotes"].isNumber() ? static_cast<int> (a["maxNotes"].asNumber()) : model::kMaxChopNotes;
            model::EditResult e;
            if (const auto rate = rateOf (a["rate"]))
                e = model::chop (in, ids, *rate, start, maxNotes);
            else if (a["rate"].isNumber())
                e = model::chop (in, ids, a["rate"].asNumber(), start, maxNotes);
            else
                fail ("chop: the rate cannot be read");
            r.clip = std::move (e.clip);
            r.refused = e.refused;
            r.count = e.count;
        }
        else if (op == "walk")
        {
            const std::string field = a["field"].asString();
            model::WalkField f = model::WalkField::start();
            if (field == "vel")
                f = model::WalkField::vel();
            else if (field != "start")
            {
                if (const auto l = core::findLane (field))
                    f = model::WalkField::onLane (*l);
                else
                    fail ("walk: unknown field " + field);
            }
            r.clip = model::walk (in, ids, f, fixture::num (a["from"]), fixture::num (a["to"]));
        }
        else if (op == "humanize")
        {
            const JsonValue& am = a["amounts"];
            model::HumanizeAmounts h;
            h.time = fixture::num (am["time"]);
            h.vel = fixture::num (am["vel"]);
            const JsonValue& lanes = am["lanes"];
            if (lanes.isNumber())
                h.volumeAndTone (lanes.asNumber());
            for (std::size_t i = 0; lanes.isObject() && i < lanes.size(); ++i)
            {
                if (const auto l = core::findLane (lanes.keyAt (i)))
                    h.lanes[static_cast<std::size_t> (*l)] = fixture::num (lanes.valueAt (i));
                else
                    fail ("humanize: unknown lane " + lanes.keyAt (i));
            }
            r.clip = model::humanize (in, ids, h, fixture::num (a["seed"]));
        }
        else if (op == "strum")
        {
            const auto rate = rateOf (a["rate"]);
            if (! rate.has_value())
                fail ("strum: the rate cannot be read");
            r.clip = model::strum (in, ids, rate.value_or (core::RateId::r1_16), a["dir"].asString() == "down" ? model::StrumDir::down : model::StrumDir::up);
        }
        else if (op == "legato")
        {
            r.clip = model::legato (in, ids);
        }
        else if (op == "scaleLane" || op == "reverseLane" || op == "flipLane")
        {
            if (! lane.has_value())
                fail (op + ": unknown lane");
            else if (op == "scaleLane")
                r.clip = model::scaleLane (in, ids, *lane, fixture::num (a["factor"]));
            else if (op == "reverseLane")
                r.clip = model::reverseLane (in, ids, *lane);
            else
                r.clip = model::flipLane (in, ids, *lane);
        }
        else if (op == "selectSimilar")
        {
            const std::string kind = a["kind"].asString();
            const auto k = kind == "pitch" ? model::SimilarKind::pitch : kind == "length" ? model::SimilarKind::length
                         : kind == "bar" ? model::SimilarKind::bar : kind == "everyOther" ? model::SimilarKind::everyOther
                                                                       : model::SimilarKind::invert;
            if (kind != "invert" && k == model::SimilarKind::invert)
                fail ("selectSimilar: unknown kind " + kind);
            r.ids = model::selectSimilar (in, a["refId"].isString() ? a["refId"].asString() : std::string(), k, ids);
        }
        else if (op == "setNoteSample")
        {
            r.clip = model::setNoteSample (in, ids, samplePatchFrom (a["patch"]));
        }
        else if (op == "getMixed")
        {
            r.hasMixed = true;
            r.mixed = model::getMixed (in, ids);
        }
        else if (op == "setLaneNodes")
        {
            if (! lane.has_value())
                fail ("setLaneNodes: unknown lane");
            else
                r.clip = model::setLaneNodes (in, a["id"].asString(), *lane, model::nodesFromJson (a["nodes"]));
        }
        else if (op == "setLaneNodesMany")
        {
            std::map<std::string, model::NodeList> map;
            const JsonValue& m = a["map"];
            for (std::size_t i = 0; i < m.size(); ++i)
                map[m.keyAt (i)] = model::nodesFromJson (m.valueAt (i));
            if (! lane.has_value())
                fail ("setLaneNodesMany: unknown lane");
            else
                r.clip = model::setLaneNodesMany (in, *lane, map);
        }
        else if (op == "duplicateNotes")
        {
            const JsonValue& off = a["offset"];
            auto e = model::duplicateNotes (in, ids, off.isNumber() ? std::optional<double> (off.asNumber()) : std::nullopt);
            r.clip = std::move (e.clip);
            r.refused = e.refused;
            r.count = e.count;
            r.ids = std::move (e.ids);
        }
        else if (op == "addNote")
        {
            const JsonValue& patch = a["patch"];
            const model::SamplePatch sp = samplePatchFrom (patch);
            auto e = model::addNote (in, fixture::num (a["p"]), fixture::num (a["s"]), fixture::num (a["l"]), patch.isObject() ? &sp : nullptr);
            r.clip = std::move (e.clip);
            r.refused = e.refused;
            r.count = e.count;
            r.ids = std::move (e.ids);
        }
        else if (op == "removeNotes")
        {
            r.clip = model::removeNotes (in, ids);
        }
        else if (op == "updateNotes")
        {
            const JsonValue& p = a["patch"];
            model::NotePatch np;
            if (p.contains ("p"))   np.p = fixture::num (p["p"]);
            if (p.contains ("s"))   np.s = fixture::num (p["s"]);
            if (p.contains ("l"))   np.l = fixture::num (p["l"]);
            if (p.contains ("vel")) np.vel = fixture::num (p["vel"]);
            if (p.contains ("gc"))  np.gc = fixture::num (p["gc"]);
            if (const JsonValue* g = p.find ("glide"))
            {
                if (g->isNull())
                    np.glide.emplace();
                else
                    np.glide.emplace (fixture::num (*g));
            }
            if (const JsonValue* t = p.find ("glideTo"))
                np.glideTo = t->isString() ? t->asString() : std::string();
            r.clip = model::updateNotes (in, ids, np);
        }
        else if (op == "setGen")
        {
            if (! lane.has_value())
                fail ("setGen: unknown lane");
            else
                r.clip = model::setGen (in, ids, *lane, model::genFromJson (a["gen"]));
        }
        else if (op == "clearGen")
        {
            if (! a["lane"].isNull() && ! lane.has_value())
                fail ("clearGen: unknown lane");
            else
                r.clip = model::clearGen (in, ids, lane);
        }
        else
        {
            fail ("unknown op " + op);
        }

        r.ran = r.why.empty();
        return r;
    }
}
