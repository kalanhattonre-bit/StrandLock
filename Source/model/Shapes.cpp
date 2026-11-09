#include "Shapes.h"

#include "Num.h"
#include "core/JsCompat.h"
#include "core/Seeds.h"
#include "core/Travel.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace sl::model
{
    namespace
    {
        /** nodesOf ([[t, v, shape, bend], ...]). */
        NodeList nodesOf (std::initializer_list<Node> list) { return NodeList (list); }

        Node n2 (double t, double v) noexcept { return mkNode (t, v); }
        Node n4 (double t, double v, NodeShape s, double bend = 0.0) noexcept { return mkNode (t, v, s, bend); }

        constexpr ShapeKey kCommonKeys[] = { ShapeKey::up, ShapeKey::down, ShapeKey::swell, ShapeKey::dip,
                                             ShapeKey::squiggle, ShapeKey::random, ShapeKey::decay, ShapeKey::reset };
        constexpr ShapeKey kSpeedKeys[] = { ShapeKey::tapestop, ShapeKey::tapestart, ShapeKey::reversehalf, ShapeKey::rewind };
        constexpr ShapeKey kPathKeys[] = { ShapeKey::playthrough, ShapeKey::playthroughback, ShapeKey::fit };

        bool isCommon (ShapeKey key) noexcept { return std::find (std::begin (kCommonKeys), std::end (kCommonKeys), key) != std::end (kCommonKeys); }

        int randomCount (const ShapeOpts& o) noexcept
        {
            return std::max (3, core::toInt32 (num (o.count, 5.0)));   // Math.max (3, num (o.count, 5) | 0)
        }

        PatternOpts squiggle (const ShapeOpts& o, double center, double depth)
        {
            PatternOpts p;
            p.shape = PatternShape::sine;
            p.cycles = num (o.cycles, 3.0);
            p.center = center;
            p.d0 = depth;
            p.d1 = depth;
            p.acc = 0.0;
            return p;
        }

        std::optional<NodeList> speedShape (ShapeKey type, const ShapeOpts& o, int L, std::uint32_t idh)
        {
            constexpr double R = 0.75;
            switch (type)
            {
                case ShapeKey::reset:       return flat (R);
                // no node sits on a dead-band edge (0.48 / 0.52): in a Float32 table those round to either side
                case ShapeKey::up:          return nodesOf ({ n2 (0, R), n2 (1, 0.98) });
                case ShapeKey::down:        return nodesOf ({ n2 (0, R), n2 (1, 0.53) });
                case ShapeKey::swell:       return nodesOf ({ n2 (0, R), n2 (0.5, 0.87), n2 (1, R) });
                case ShapeKey::dip:         return nodesOf ({ n2 (0, R), n2 (0.5, 0.63), n2 (1, R) });
                case ShapeKey::decay:       return nodesOf ({ n2 (0, core::travelSpeedToV (4.0)), n2 (0.25, R), n2 (1, R) });
                case ShapeKey::squiggle:    return patternNodes (squiggle (o, R, 0.24));
                case ShapeKey::random:
                {
                    const int rcount = randomCount (o);
                    const auto r = seededList (rcount, idh, L, num (o.seed, 1.0));
                    NodeList out;
                    for (int i = 0; i < rcount; ++i)
                        out.push_back (mkNode (static_cast<double> (i) / static_cast<double> (rcount - 1),
                                               i == 0 ? R : R + (r[static_cast<std::size_t> (i)] * 2.0 - 1.0) * 0.15));
                    return out;
                }
                // speed falls about linearly from 1x (a bent line in v), reaches the dead band at 7/8, then holds stopped
                case ShapeKey::tapestop:    return nodesOf ({ n4 (0, R, NodeShape::bend, 0.24), n4 (0.875, 0.5, NodeShape::step), n2 (1, 0.5) });
                case ShapeKey::tapestart:   return nodesOf ({ n4 (0, 0.5, NodeShape::step), n4 (0.125, 0.5, NodeShape::bend, -0.24), n2 (1, R) });
                case ShapeKey::reversehalf: return nodesOf ({ n4 (0, R, NodeShape::step), n4 (0.5, 0.25, NodeShape::step), n2 (1, 0.25) });
                case ShapeKey::rewind:      return nodesOf ({ n4 (0, R, NodeShape::step), n4 (0.5, core::travelSpeedToV (-4.0), NodeShape::linear), n2 (1, 0.5) });
                case ShapeKey::playthrough:
                case ShapeKey::playthroughback:
                case ShapeKey::fit:
                default:                    return std::nullopt;
            }
        }

        std::optional<NodeList> pathShape (ShapeKey type, const ShapeOpts& o, int L, std::uint32_t idh, double beats)
        {
            switch (type)
            {
                case ShapeKey::reset:           return flat (core::laneDef (core::LaneId::travel));
                case ShapeKey::up:
                case ShapeKey::playthrough:     return nodesOf ({ n4 (0, 0, NodeShape::linear), n2 (1, 1) });
                case ShapeKey::down:
                case ShapeKey::playthroughback: return nodesOf ({ n4 (0, 1, NodeShape::linear), n2 (1, 0) });
                case ShapeKey::swell:           return nodesOf ({ n2 (0, 0.05), n2 (0.5, 1), n2 (1, 0.05) });
                case ShapeKey::dip:             return nodesOf ({ n2 (0, 1), n2 (0.5, 0.12), n2 (1, 1) });
                case ShapeKey::decay:           return nodesOf ({ n2 (0, 1), n2 (0.15, 0.55), n2 (1, 0) });
                case ShapeKey::squiggle:        return patternNodes (squiggle (o, 0.5, 0.4));
                case ShapeKey::random:
                {
                    const int rcount = randomCount (o);
                    const auto r = seededList (rcount, idh, L, num (o.seed, 1.0));
                    NodeList out;
                    for (int i = 0; i < rcount; ++i)
                        out.push_back (mkNode (static_cast<double> (i) / static_cast<double> (rcount - 1), r[static_cast<std::size_t> (i)], NodeShape::step));
                    return out;
                }
                case ShapeKey::fit:             return fitNodes (jsMax (1e-3, num (beats, 1.0)), o);
                case ShapeKey::tapestop:
                case ShapeKey::tapestart:
                case ShapeKey::reversehalf:
                case ShapeKey::rewind:
                default:                        return std::nullopt;
            }
        }
    }

    //==============================================================================================
    const char* code (ShapeKey key) noexcept
    {
        const auto i = static_cast<int> (key);
        return i < kNumShapeKeys ? kShapeKeyCodes[i] : "";
    }

    const char* shapeName (ShapeKey key) noexcept
    {
        const auto i = static_cast<int> (key);
        return i < kNumShapeKeys ? kShapeKeyNames[i] : "";
    }

    std::optional<ShapeKey> parseShapeKey (std::string_view s) noexcept
    {
        if (s == "wobble")
            return ShapeKey::squiggle;
        for (int i = 0; i < kNumShapeKeys; ++i)
            if (s == kShapeKeyCodes[i])
                return static_cast<ShapeKey> (i);
        return std::nullopt;
    }

    std::optional<TravelKind> shapeKeyKind (ShapeKey key) noexcept
    {
        if (std::find (std::begin (kSpeedKeys), std::end (kSpeedKeys), key) != std::end (kSpeedKeys))
            return TravelKind::speed;
        if (std::find (std::begin (kPathKeys), std::end (kPathKeys), key) != std::end (kPathKeys))
            return TravelKind::path;
        return std::nullopt;
    }

    std::vector<ShapeKey> shapeKeysFor (core::LaneId lane, TravelKind kind)
    {
        std::vector<ShapeKey> keys (std::begin (kCommonKeys), std::end (kCommonKeys));
        if (lane == core::LaneId::travel)
        {
            if (kind == TravelKind::path)
                keys.insert (keys.end(), std::begin (kPathKeys), std::end (kPathKeys));
            else
                keys.insert (keys.end(), std::begin (kSpeedKeys), std::end (kSpeedKeys));
        }
        return keys;
    }

    //==============================================================================================
    NodeList patternNodes (const PatternOpts& o)
    {
        const double center = num (o.center, 0.5), d0 = num (o.d0, 0.3), d1 = num (o.d1, d0), acc = num (o.acc, 0.0);
        auto amp = [&] (double t) { return (d0 + (d1 - d0) * t) / 2.0; };
        auto warpInv = [&] (double x) { return acc == 0.0 ? x : std::pow (x, 1.0 / (1.0 + acc)); };
        const double C = jsMax (0.5, num (o.cycles, 3.0));

        NodeList nodes;
        auto push = [&] (double x, double v, bool lin)
        {
            const double t = clamp (warpInv (clamp (x, 0.0, 1.0)), 0.0, 1.0);
            nodes.push_back (mkNode (t, clamp (v, 0.0, 1.0), lin ? NodeShape::linear : NodeShape::smooth));
        };

        if (o.shape == PatternShape::sine || o.shape == PatternShape::tri)
        {
            const double steps = jsMax (2.0, core::jsRound (C * 4.0));
            constexpr double ph[4] = { 0.0, 1.0, 0.0, -1.0 };
            for (long long i = 0; static_cast<double> (i) <= steps; ++i)
            {
                const double x = static_cast<double> (i) / steps;
                push (x, center + ph[i % 4] * amp (x), o.shape == PatternShape::tri);
            }
        }
        else if (o.shape == PatternShape::saw)
        {
            const double n = jsMax (1.0, core::jsRound (C));
            for (long long c = 0; static_cast<double> (c) < n; ++c)
            {
                const double x0 = static_cast<double> (c) / n, x1 = static_cast<double> (c + 1) / n;
                push (x0, center - amp (x0), true);
                push (x1 - 0.004, center + amp (x1), true);
            }
            push (1.0, center - amp (1.0), true);
        }
        else if (o.shape == PatternShape::square)
        {
            const double n = jsMax (1.0, core::jsRound (C * 2.0));
            for (long long c = 0; static_cast<double> (c) < n; ++c)
            {
                const double x0 = static_cast<double> (c) / n, x1 = static_cast<double> (c + 1) / n;
                const double s = c % 2 != 0 ? -1.0 : 1.0;
                push (x0, center + s * amp (x0), true);
                push (x1 - 0.004, center + s * amp (x1), true);
            }
            push (1.0, center + (std::fmod (n, 2.0) != 0.0 ? -1.0 : 1.0) * amp (1.0), true);
        }
        else
        {
            const double n = jsMax (2.0, core::jsRound (C * 2.0));
            long long seed = static_cast<long long> (core::hash32 (core::toInt32 (num (o.seed, 1.0))) % 10000u);
            for (long long c = 0; static_cast<double> (c) <= n; ++c)
            {
                const double x = static_cast<double> (c) / n;
                seed = (seed * 9301 + 49297) % 233280;
                push (x, center + (static_cast<double> (seed) / 233280.0 * 2.0 - 1.0) * amp (x), false);
            }
        }

        std::stable_sort (nodes.begin(), nodes.end(), [] (const Node& a, const Node& b) { return a.t < b.t; });
        nodes.front().t = 0.0;
        nodes.back().t = 1.0;
        for (std::size_t i = 1; i < nodes.size(); ++i)
            if (nodes[i].t <= nodes[i - 1].t)
                nodes[i].t = jsMin (1.0, nodes[i - 1].t + 1e-4);
        return nodes;
    }

    std::vector<double> seededList (int count, std::uint32_t idh, int laneIndex, double seed)
    {
        std::vector<double> out;
        out.reserve (static_cast<std::size_t> (std::max (0, count)));
        for (int i = 0; i < count; ++i)
            out.push_back (core::rand01 (core::hash32 (idh, laneIndex, i, seed, 0x5a9e)));
        return out;
    }

    NodeList fitNodes (double beats, const ShapeOpts& o)
    {
        std::optional<core::Inst> inst;
        if (o.inst.has_value())
            inst = core::normalizeInst (*o.inst);

        core::RegionSettings region;   // { start 0, end 1, loop }
        if (o.region.has_value())
            region = *o.region;
        else if (inst.has_value())
            region = inst->region;

        const double bpm = clamp (num (o.bpm, 120.0), 20.0, 400.0);
        const double secs = jsMax (0.01, num (o.sampleSeconds, 6.0));
        const double regionSec = jsMax (1e-3, (clamp (num (region.end, 1.0), 0.0, 1.0) - clamp (num (region.start, 0.0), 0.0, 1.0)) * secs);
        const double rate = (beats * 60.0 / bpm) / regionSec;   // Region fractions per note
        const double v = clamp (num (o.from, 0.0), 0.0, 1.0);

        NodeList out { mkNode (0.0, v, NodeShape::linear) };
        if (v + rate <= 1.0)
        {
            out.push_back (mkNode (1.0, v + rate));
            return out;
        }
        double t = (1.0 - v) / rate;
        if (region.atEnd != core::AtEnd::loop)
        {
            out.push_back (mkNode (t, 1.0, NodeShape::linear));
            out.push_back (mkNode (1.0, 1.0));
            return out;
        }
        while (t < 1.0 && out.size() < static_cast<std::size_t> (kMaxNodes - 2))
        {
            out.push_back (mkNode (t, 1.0, NodeShape::linear));
            out.push_back (mkNode (t, 0.0, NodeShape::linear));
            t += 1.0 / rate;
        }
        const Node last = out.back();
        out.push_back (mkNode (1.0, clamp (last.v + (1.0 - last.t) * rate, 0.0, 1.0)));
        return normalizeNodes (out);
    }

    std::optional<NodeList> shapeNodes (ShapeKey type, core::LaneId lane, const ShapeOpts& o, TravelKind kind,
                                        std::uint32_t idh, double beats)
    {
        const int L = core::laneIndex (lane);
        const double def = core::laneDef (lane);
        if (lane == core::LaneId::travel && kind == TravelKind::path)
            return pathShape (type, o, L, idh, beats);
        if (lane == core::LaneId::travel)
            return speedShape (type, o, L, idh);

        const bool P = lane == core::LaneId::pitch;
        constexpr double sm = 1.0 / 24.0;
        switch (type)
        {
            case ShapeKey::reset:    return flat (def);
            case ShapeKey::up:       return P ? nodesOf ({ n2 (0, 0.5), n2 (1, 0.5 + 2.0 * sm) }) : nodesOf ({ n2 (0, 0), n2 (1, 1) });
            case ShapeKey::down:     return P ? nodesOf ({ n2 (0, 0.5), n2 (1, 0.5 - 2.0 * sm) }) : nodesOf ({ n2 (0, 1), n2 (1, 0) });
            case ShapeKey::swell:    return P ? nodesOf ({ n2 (0, 0.5), n2 (0.5, 0.5 + sm), n2 (1, 0.5) }) : nodesOf ({ n2 (0, 0.05), n2 (0.5, 1), n2 (1, 0.05) });
            case ShapeKey::dip:      return P ? nodesOf ({ n2 (0, 0.5), n2 (0.5, 0.5 - sm), n2 (1, 0.5) }) : nodesOf ({ n2 (0, 1), n2 (0.5, 0.12), n2 (1, 1) });
            case ShapeKey::decay:    return P ? nodesOf ({ n2 (0, 0.5 + sm), n2 (0.12, 0.5), n2 (1, 0.5) }) : nodesOf ({ n2 (0, 1), n2 (0.15, 0.55), n2 (1, 0) });
            case ShapeKey::squiggle: return patternNodes (squiggle (o, 0.5, P ? 0.06 : 0.7));
            case ShapeKey::random:
            {
                const int rcount = randomCount (o);
                const auto r = seededList (rcount, idh, L, num (o.seed, 1.0));
                NodeList out;
                for (int i = 0; i < rcount; ++i)
                {
                    const double x = r[static_cast<std::size_t> (i)];
                    out.push_back (mkNode (static_cast<double> (i) / static_cast<double> (rcount - 1),
                                           P ? (i == 0 ? 0.5 : 0.5 + (x * 2.0 - 1.0) * sm) : 0.1 + 0.8 * x));
                }
                return out;
            }
            case ShapeKey::tapestop:
            case ShapeKey::tapestart:
            case ShapeKey::reversehalf:
            case ShapeKey::rewind:
            case ShapeKey::playthrough:
            case ShapeKey::playthroughback:
            case ShapeKey::fit:
            default:
                return std::nullopt;
        }
    }

    std::optional<Clip> applyShape (const Clip& clip, const Ids& ids, core::LaneId lane, ShapeKey key, ShapeScope scope,
                                    const ShapeOpts& opts)
    {
        const bool travel = lane == core::LaneId::travel;
        const std::optional<TravelKind> needKind = shapeKeyKind (key);
        if (! isCommon (key) && ! (travel && needKind.has_value()))
            return std::nullopt;

        const std::vector<NotePtr> sel = selected (clip, ids);
        if (sel.empty())
            return withNotes (clip, clip.notes);

        // On Travel, a key of the other kind leaves the note alone (it is still copied, as in the prototype).
        auto skip = [&] (const Note& n) { return travel && needKind.has_value() && n.sample.travel != *needKind; };

        if (scope == ShapeScope::across)
        {
            double S0 = sel[0]->s, S1 = sel[0]->s + sel[0]->l;
            for (const NotePtr& n : sel)
            {
                S0 = jsMin (S0, n->s);
                S1 = jsMax (S1, n->s + n->l);
            }
            const double span = jsMax (1e-6, S1 - S0);

            ShapeOpts o = opts;
            if (key == ShapeKey::random)
                o.count = clamp (core::jsRound (span) + 1.0, 5.0, 33.0);

            std::optional<NodeList> cache[2];   // one curve per Travel kind (one is enough off Travel)
            auto curveFor = [&] (TravelKind kind) -> const std::optional<NodeList>&
            {
                auto& slot = cache[static_cast<std::size_t> (kind)];
                if (! slot.has_value())
                    slot = shapeNodes (key, lane, o, kind, 0u, span);
                return slot;
            };

            return editNotes (clip, ids, [&] (Note& n, const Note&)
            {
                if (skip (n))
                    return;
                const auto& curve = curveFor (travel ? n.sample.travel : TravelKind::speed);
                if (curve.has_value())
                    n.lane (lane) = sliceNodes (*curve, (n.s - S0) / span, (n.s + n.l - S0) / span);
            });
        }

        return editNotes (clip, ids, [&] (Note& n, const Note&)
        {
            if (skip (n))
                return;
            auto nodes = shapeNodes (key, lane, opts, travel ? n.sample.travel : TravelKind::speed, core::idHash (std::string_view (n.id)), n.l);
            if (nodes.has_value())
                n.lane (lane) = std::move (*nodes);
        });
    }
}
