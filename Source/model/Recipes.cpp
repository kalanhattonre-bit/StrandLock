#include "Recipes.h"

#include "Generators.h"
#include "Num.h"
#include "core/JsCompat.h"
#include "core/Seeds.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <unordered_set>

namespace sl::model
{
    using core::Engine;
    using core::HeadTune;
    using core::Motion;
    using core::RateId;
    using core::RefreshMode;

    core::InstPatch straightPatch()
    {
        core::InstPatch p;
        p.engine = Engine::voice;
        p.lock = false;
        p.motion = Motion::forward;
        p.speed = 1.0;
        p.heads = 1.0;
        p.spread = 0.25;
        p.grainVoice = 2.0;
        p.grainCloud = 140.0;
        p.grainLock = 4.0;
        p.refresh.mode = RefreshMode::smooth;
        p.refresh.ms = 120.0;
        p.refresh.rate = RateId::r1_16;
        p.refresh.lockToBeat = true;
        p.refresh.skip = 0.0;
        p.spray = 0.0;
        p.tape = false;
        p.rev = false;
        p.drift = 0.0;
        p.headTune = HeadTune::same;
        p.jump = RateId::r1_8;
        return p;
    }

    namespace
    {
        RecipeLane resetLane (core::LaneId lane)
        {
            RecipeLane r;
            r.lane = lane;
            r.action = RecipeLane::Action::reset;
            return r;
        }

        RecipeLane shapeLane (core::LaneId lane, ShapeKey key)
        {
            RecipeLane r;
            r.lane = lane;
            r.action = RecipeLane::Action::shape;
            r.shape = key;
            return r;
        }

        RecipeLane genLane (core::LaneId lane, const Gen& g, std::optional<double> base)
        {
            RecipeLane r;
            r.lane = lane;
            r.action = RecipeLane::Action::generator;
            r.gen = g;
            r.base = base;
            return r;
        }

        std::vector<Recipe> makeRecipes()
        {
            using core::LaneId;
            std::vector<Recipe> list;

            {
                Recipe r { 1, "Straight", straightPatch(), {} };
                r.notes = { resetLane (LaneId::travel), resetLane (LaneId::heads), resetLane (LaneId::grain), resetLane (LaneId::refresh) };
                list.push_back (std::move (r));
            }
            {
                Recipe r { 2, "Backwards", straightPatch(), {} };
                r.inst.motion = Motion::backward;
                r.notes = { resetLane (LaneId::travel) };
                list.push_back (std::move (r));
            }
            {
                Recipe r { 3, "Frozen choir", straightPatch(), {} };
                r.inst.motion = Motion::freeze;
                r.inst.heads = 4.0;
                r.inst.spread = 0.15;
                r.inst.headTune = HeadTune::detune;
                r.inst.drift = 0.3;
                r.inst.grainVoice = 3.0;
                r.notes = { resetLane (LaneId::travel) };
                list.push_back (std::move (r));
            }
            {
                Recipe r { 4, "Tape scratch", straightPatch(), {} };
                r.inst.engine = Engine::tape;
                GenPatch g;
                g.shape = GenShape::square;
                g.rate = RateId::r1_8;
                g.d0 = 0.5;
                g.d1 = 0.5;
                g.lockToBeat = true;
                r.notes = { genLane (LaneId::travel, makeGen (GenKind::pattern, g), 0.5) };
                list.push_back (std::move (r));
            }
            {
                Recipe r { 5, "Stutter 1/16", straightPatch(), {} };
                r.inst.engine = Engine::tape;
                r.inst.refresh.mode = RefreshMode::sync;
                r.inst.refresh.ms = 120.0;
                r.inst.refresh.rate = RateId::r1_16;
                r.inst.refresh.lockToBeat = true;
                r.inst.refresh.skip = 0.0;
                r.notes = { resetLane (LaneId::refresh) };
                list.push_back (std::move (r));
            }
            {
                Recipe r { 6, "Slow bloom", straightPatch(), {} };
                r.inst.engine = Engine::cloud;
                r.inst.heads = 4.0;
                r.inst.spread = 0.5;
                r.inst.spray = 0.2;
                r.inst.drift = 0.2;
                r.inst.speed = 0.5;
                r.notes = { shapeLane (LaneId::heads, ShapeKey::up) };
                list.push_back (std::move (r));
            }
            {
                Recipe r { 7, "Scatter rain", straightPatch(), {} };
                r.inst.engine = Engine::cloud;
                r.inst.motion = Motion::scatter;
                r.inst.jump = RateId::r1_8T;
                r.inst.grainCloud = 60.0;
                r.inst.spray = 0.3;
                r.inst.heads = 3.0;
                r.inst.spread = 0.5;
                list.push_back (std::move (r));
            }
            {
                Recipe r { 8, "Octave shimmer", straightPatch(), {} };
                r.inst.engine = Engine::cloud;
                r.inst.lock = true;
                r.inst.heads = 3.0;
                r.inst.headTune = HeadTune::octaves;
                r.inst.spread = 0.3;
                r.inst.drift = 0.2;
                r.inst.grainLock = 4.0;
                list.push_back (std::move (r));
            }
            return list;
        }
    }

    const std::vector<Recipe>& recipes()
    {
        static const std::vector<Recipe> list = makeRecipes();
        return list;
    }

    std::optional<RecipeResult> applyRecipe (const Clip& clip, const core::Inst& inst, int n, const Ids& ids)
    {
        const auto& list = recipes();
        const auto it = std::find_if (list.begin(), list.end(), [n] (const Recipe& r) { return r.n == n; });
        if (it == list.end())
            return std::nullopt;
        const Recipe& R = *it;

        RecipeResult out;
        out.inst = core::normalizeInst (core::mergeInst (core::normalizeInst (inst), R.inst));
        Clip c = withNotes (clip, clip.notes);
        for (const RecipeLane& e : R.notes)
        {
            const core::LaneId lane = e.lane;
            switch (e.action)
            {
                case RecipeLane::Action::reset:
                    c = editNotes (c, ids, [lane] (Note& q, const Note&)
                    {
                        q.lane (lane) = flat (core::laneDef (lane));
                        q.genOf (lane).reset();
                    });
                    break;
                case RecipeLane::Action::shape:
                    if (auto shaped = applyShape (c, ids, lane, e.shape, ShapeScope::each))
                        c = clearGen (*shaped, ids, lane);
                    break;
                case RecipeLane::Action::generator:
                {
                    const Gen g = normalizeGen (e.gen);
                    c = editNotes (c, ids, [&] (Note& q, const Note&)
                    {
                        if (e.base.has_value())
                            q.lane (lane) = flat (clamp (*e.base, 0.0, 1.0));
                        q.genOf (lane) = g;
                    });
                    break;
                }
            }
        }
        out.clip = std::move (c);
        return out;
    }

    //==============================================================================================
    namespace
    {
        // pick (list, x) = list[Math.min (list.length - 1, Math.floor (x * list.length))]
        template <class T>
        T pick (std::initializer_list<T> list, double x)
        {
            const double len = static_cast<double> (list.size());
            const double i = jsMin (len - 1.0, std::floor (x * len));
            return *(list.begin() + static_cast<std::ptrdiff_t> (i));
        }

        double roundTo (double x, double d)
        {
            return core::jsRound (x * d) / d;
        }
    }

    core::Inst dice (const core::Inst& inst, double seed, const std::vector<std::string>& pins)
    {
        const core::Inst base = core::normalizeInst (inst);
        core::Inst out = base;
        const std::int32_t sd = core::toInt32 (num (seed, 1.0));
        const std::unordered_set<std::string> pinned (pins.begin(), pins.end());
        auto isPinned = [&pinned] (const char* k)
        {
            if (pinned.count (k) != 0)
                return true;
            const bool grainKey = std::strcmp (k, "grainVoice") == 0 || std::strcmp (k, "grainCloud") == 0 || std::strcmp (k, "grainLock") == 0;
            return grainKey && pinned.count ("grain") != 0;
        };
        auto r = [sd] (int i) { return core::rand01 (core::hash32 (0x0d1ce, i, 0, sd)); };

        if (! isPinned ("engine"))
            out.engine = pick ({ Engine::voice, Engine::voice, Engine::cloud, Engine::cloud, Engine::tape }, r (1));
        if (! isPinned ("lock"))
            out.lock = out.engine == Engine::cloud && r (2) < 0.4;
        if (! isPinned ("motion"))
        {
            out.motion = out.engine == Engine::tape
                ? pick ({ Motion::forward, Motion::forward, Motion::backward, Motion::bounce, Motion::mirror, Motion::scatter }, r (3))
                : pick ({ Motion::forward, Motion::forward, Motion::forward, Motion::backward, Motion::bounce, Motion::mirror, Motion::freeze,
                          Motion::scatter }, r (3));
        }
        if (! isPinned ("speed"))
            out.speed = pick ({ 0.25, 0.5, 0.5, 1.0, 1.0, 1.0, 2.0 }, r (4));
        if (! isPinned ("heads"))
            out.heads = 1.0 + std::floor (r (5) * 6.0);
        if (! isPinned ("spread"))
            out.spread = roundTo (r (6) * 0.6, 100.0);
        if (! isPinned ("grainVoice"))
            out.grainVoice = pick ({ 1.0, 1.5, 2.0, 2.0, 3.0, 4.0 }, r (7));
        if (! isPinned ("grainCloud"))
            out.grainCloud = core::jsRound (40.0 * std::pow (2.0, r (8) * 3.3));
        if (! isPinned ("grainLock"))
            out.grainLock = pick ({ 2.0, 3.0, 4.0, 4.0, 6.0, 8.0 }, r (9));
        if (! isPinned ("refresh"))
        {
            out.refresh.mode = pick ({ RefreshMode::smooth, RefreshMode::smooth, RefreshMode::free, RefreshMode::sync }, r (10));
            out.refresh.ms = core::jsRound (30.0 * std::pow (2.0, r (11) * 3.7));
            out.refresh.rate = pick ({ RateId::r1_4, RateId::r1_8, RateId::r1_8T, RateId::r1_16, RateId::r1_16T, RateId::r1_32 }, r (12));
            out.refresh.lockToBeat = true;
            out.refresh.skip = pick ({ 0.0, 0.0, 0.1, 0.25 }, r (13));
        }
        if (! isPinned ("spray"))
            out.spray = roundTo (r (14) * 0.5, 100.0);
        if (! isPinned ("tape"))
            out.tape = r (15) < 0.2;
        if (! isPinned ("rev"))
            out.rev = r (16) < 0.15;
        if (! isPinned ("drift"))
            out.drift = roundTo (r (17) * 0.4, 100.0);
        if (! isPinned ("headTune"))
            out.headTune = pick ({ HeadTune::same, HeadTune::same, HeadTune::detune, HeadTune::octaves, HeadTune::fifths }, r (18));
        if (! isPinned ("jump"))
            out.jump = pick ({ RateId::r1_4, RateId::r1_8, RateId::r1_8T, RateId::r1_16, RateId::r1_16T }, r (19));
        return core::normalizeInst (out);
    }
}
