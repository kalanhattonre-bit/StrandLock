#include "Generators.h"

#include "Num.h"
#include "core/JsCompat.h"
#include "core/Rates.h"
#include "core/Seeds.h"

#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <utility>

namespace sl::model
{
    Gen normalizeGen (Gen g)
    {
        const double d0 = clamp (num (g.d0, 0.3), 0.0, 1.0);
        g.kind = g.kind == GenKind::random ? GenKind::random : GenKind::pattern;
        if (static_cast<std::size_t> (g.shape) >= std::size (kGenShapeCodes))
            g.shape = GenShape::squiggle;
        if (! core::isValidRate (g.rate))
            g.rate = core::RateId::r1_16T;
        g.d0 = d0;
        g.d1 = clamp (num (g.d1, d0), 0.0, 1.0);
        g.warp = clamp (num (g.warp, 0.0), -0.9, 3.0);
        g.amount = clamp (num (g.amount, 0.5), 0.0, 1.0);
        g.smooth = clamp (num (g.smooth, 0.0), 0.0, 1.0);
        g.perNote = g.perNote == PerNote::different ? PerNote::different : PerNote::same;
        if (g.key.size() > kMaxIdBytes)   // the CLIP chunk holds at most 255 bytes (8.2 #27): the note's own id
            g.key.clear();
        return g;   // seed is an int32 already (Math.trunc (x) | 0); invert and key need nothing
    }

    Gen makeGen (GenKind kind, const GenPatch& patch)
    {
        // normalizeGen (Object.assign ({ kind }, patch)): an unset number is "missing" (NaN here), so it
        // takes normalizeGen's default; an unset d1 follows d0.
        const double missing = std::numeric_limits<double>::quiet_NaN();
        Gen g;
        g.kind = kind;
        g.shape = patch.shape.value_or (GenShape::squiggle);
        g.rate = patch.rate.value_or (core::RateId::r1_16T);
        g.d0 = patch.d0.value_or (missing);
        g.d1 = patch.d1.value_or (missing);
        g.warp = patch.warp.value_or (missing);
        g.lockToBeat = patch.lockToBeat.value_or (true);
        g.amount = patch.amount.value_or (missing);
        g.smooth = patch.smooth.value_or (missing);
        g.perNote = patch.perNote.value_or (PerNote::same);
        g.seed = patch.seed.value_or (1);
        g.invert = patch.invert.value_or (false);
        g.key = patch.key.value_or (std::string());
        return normalizeGen (std::move (g));
    }

    //==============================================================================================
    GenEval::GenEval (const Gen& g, const Note& note, core::LaneId lane)
        : kind (g.kind),
          shape (g.shape),
          rateBeats (core::rateBeats (g.rate)),
          start (num (note.s, 0.0)),
          length (num (note.l, 1.0)),
          d0 (g.d0),
          d1 (g.d1),
          warp (g.warp),
          amount (g.amount),
          smooth (g.smooth),
          lockToBeat (g.lockToBeat),
          different (g.perNote == PerNote::different),
          idh (g.perNote == PerNote::different ? core::idHash (std::string_view (g.key.empty() ? note.id : g.key)) : 0u),
          laneIndex (core::laneIndex (lane)),
          seed (g.seed),
          sign (g.invert ? -1.0 : 1.0),
          scale (lane == core::LaneId::pitch ? kPitchGenScale : 1.0),
          cachedStep (std::numeric_limits<double>::quiet_NaN())
    {
    }

    double GenEval::randomStep (double k) const noexcept
    {
        const std::uint32_t h = different ? core::hash32 (idh, laneIndex, k, seed) : core::hash32 (laneIndex, k, seed);
        return core::rand01 (h) * 2.0 - 1.0;
    }

    double GenEval::phase (double t) const noexcept
    {
        if (lockToBeat)
            return (start + t * length) / rateBeats;
        return (length * (warp == 0.0 ? t : std::pow (t, 1.0 + warp))) / rateBeats;
    }

    double GenEval::operator() (double t) const
    {
        constexpr double pi = std::numbers::pi;   // Math.PI
        const double ph = phase (t);
        const double k = std::floor (ph + 1e-9);
        const double f = clamp (ph - k, 0.0, 1.0);

        // rnd (k): the one-step cache; rndNoCache: the other steps
        auto rnd = [this] (double kk)
        {
            if (kk == cachedStep)
                return cachedValue;
            const double v = randomStep (kk);
            cachedStep = kk;
            cachedValue = v;
            return v;
        };

        if (kind == GenKind::random)
        {
            const double cur = rnd (k);
            double val = cur;
            if (smooth > 0.0 && f < smooth)
            {
                const double prev = randomStep (k - 1.0);
                const double w = (1.0 - std::cos (pi * f / smooth)) / 2.0;
                val = prev + (cur - prev) * w;
            }
            return sign * val * amount * 0.5 * scale;
        }

        double w = 0.0;
        switch (shape)
        {
            case GenShape::triangle:     w = f < 0.25 ? 4.0 * f : f < 0.75 ? 2.0 - 4.0 * f : 4.0 * f - 4.0; break;
            case GenShape::sawup:        w = 2.0 * f - 1.0; break;
            case GenShape::sawdown:      w = 1.0 - 2.0 * f; break;
            case GenShape::square:       w = f < 0.5 ? 1.0 : -1.0; break;
            case GenShape::randsteps:    w = rnd (k); break;
            case GenShape::smoothrandom:
            {
                const double a = rnd (k), b = randomStep (k + 1.0);
                w = a + (b - a) * (1.0 - std::cos (pi * f)) / 2.0;
                break;
            }
            case GenShape::squiggle:
            default:                     w = std::sin (2.0 * pi * f); break;
        }
        return sign * w * (d0 + (d1 - d0) * t) * 0.5 * scale;
    }

    double genOffset (const Gen& g, const Note& note, core::LaneId lane, double t)
    {
        return GenEval (normalizeGen (g), note, lane) (t);
    }

    double tableLength (double l, double res) noexcept
    {
        const double n = std::ceil (l * res) + 1.0;
        return std::isfinite (n) ? jsMax (2.0, n) : 2.0;
    }

    std::vector<float> laneTable (const Note& note, core::LaneId lane, double res)
    {
        const double r = jsMax (1.0, num (res, 256.0));
        return laneTableOfLength (note, lane, tableLength (note.l, r));
    }

    std::vector<float> laneTableOfLength (const Note& note, core::LaneId lane, double N)
    {
        // sampleNodes takes N | 0, as in JavaScript (the same as N for every real note length).
        std::vector<float> out = sampleNodes (noteLane (note, lane), core::toInt32 (N));
        const auto& g0 = note.genOf (lane);
        if (g0.has_value())
        {
            const GenEval ev (normalizeGen (*g0), note, lane);
            for (std::size_t j = 0; j < out.size(); ++j)
                out[j] = static_cast<float> (clamp (static_cast<double> (out[j]) + ev (static_cast<double> (j) / (N - 1.0)), 0.0, 1.0));
        }
        return out;
    }

    Clip setGen (const Clip& clip, const Ids& ids, core::LaneId lane, const std::optional<Gen>& gen)
    {
        const std::optional<Gen> g = gen.has_value() ? std::optional<Gen> (normalizeGen (*gen)) : std::nullopt;
        return editNotes (clip, ids, [&] (Note& n, const Note&) { n.genOf (lane) = g; });
    }

    Clip clearGen (const Clip& clip, const Ids& ids, std::optional<core::LaneId> lane)
    {
        return editNotes (clip, ids, [&] (Note& n, const Note&)
        {
            if (lane.has_value())
                n.genOf (*lane).reset();
            else
                for (auto& g : n.gen)
                    g.reset();
        });
    }

    Clip bakeGen (const Clip& clip, const Ids& ids, core::LaneId lane, double res)
    {
        const double r = jsMax (8.0, num (res, 256.0));
        return editNotes (clip, ids, [&] (Note& n, const Note&)
        {
            if (! n.genOf (lane).has_value())
                return;
            const std::vector<float> tab = laneTable (n, lane, r);
            const std::size_t N = tab.size();
            std::vector<double> ts (N), vs (N);
            for (std::size_t j = 0; j < N; ++j)
            {
                ts[j] = static_cast<double> (j) / static_cast<double> (N - 1);
                vs[j] = static_cast<double> (tab[j]);
            }
            double eps = kBakeEps;
            auto idx = rdpIdx (ts, vs, eps);
            while (idx.size() > static_cast<std::size_t> (kMaxNodes))
            {
                eps *= 1.25;
                idx = rdpIdx (ts, vs, eps);
            }
            NodeList baked;
            baked.reserve (idx.size());
            for (const std::size_t i : idx)
                baked.push_back (mkNode (ts[i], vs[i], NodeShape::linear));
            n.lane (lane) = std::move (baked);
            n.genOf (lane).reset();
        });
    }

    Clip reroll (const Clip& clip, const Ids& ids, std::optional<core::LaneId> lane)
    {
        auto bump = [] (std::int32_t s) { return static_cast<std::int32_t> (static_cast<std::uint32_t> (s) + 1u); };   // (s + 1) | 0
        return editNotes (clip, ids, [&] (Note& n, const Note&)
        {
            for (int i = 0; i < core::kNumLanes; ++i)
            {
                if (lane.has_value() && i != core::laneIndex (*lane))
                    continue;
                auto& g = n.gen[static_cast<std::size_t> (i)];
                if (g.has_value())
                    g->seed = bump (g->seed);
            }
            if (! lane.has_value() && n.sample.start.mode == StartMode::random)
                n.sample.start.seed = bump (n.sample.start.seed);
        });
    }
}
