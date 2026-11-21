#include "analysis/Analysis.h"
#include "analysis/Detail.h"

#include "core/JsCompat.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// sl-analysis.js detectPitch, line by line: YIN on the 5 loudest 2048-sample frames, median (v3's detectPitch),
// with the integrator's two changes (at least half of the examined frames must be periodic, else the next 10
// loudest are examined too and two periodic frames must agree within 20 cents; a silent file is null) and
// decision 3 (plan 7.9: DC is "no clear pitch"; see Analysis.h).

namespace sl::analysis
{
    namespace
    {
        struct Block
        {
            double e;
            std::int64_t s;
        };
    }

    std::optional<Pitch> detectPitch (const Source& source, const Stop& stop, PitchOptions options)
    {
        if (! detail::validInput (source))
            return std::nullopt;
        const double sr = source.sampleRate();
        const std::int64_t n = source.numFrames();   // d.length
        constexpr std::int64_t W = 2048;
        const double maxTd = std::floor (sr / 45), minTd = std::floor (sr / 1500);
        if (static_cast<double> (n) < static_cast<double> (W) + maxTd + 10)
            return std::nullopt;
        const auto maxT = static_cast<std::int64_t> (maxTd), minT = static_cast<std::int64_t> (minTd);

        // the blocks' energies (every 4th sample of each 2048), loudest first, then earliest
        detail::MonoReader mono (source);
        std::vector<Block> blocks;
        blocks.reserve (static_cast<std::size_t> (n / W + 1));
        std::vector<float> buf (static_cast<std::size_t> (W));
        for (std::int64_t s = 0; s + W + maxT < n; s += W)
        {
            if ((s & 0x3ffff) == 0 && stop && stop())
                return std::nullopt;
            mono.read (s, static_cast<int> (W), buf.data());
            double e = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t> (W); i += 4)
                e += static_cast<double> (buf[i]) * static_cast<double> (buf[i]);
            blocks.push_back ({ e, s });
        }
        std::sort (blocks.begin(), blocks.end(), [] (const Block& a, const Block& b) { return a.e > b.e || (a.e == b.e && a.s < b.s); });
        if (! (blocks[0].e > 1e-9))
            return std::nullopt;   // digital silence

        // loud: the blocks above 1e-4 of the loudest, in that order; top: the first 5
        std::vector<std::int64_t> loud;
        for (const Block& b : blocks)
            if (b.e > blocks[0].e * 1e-4)
                loud.push_back (b.s);
        const std::size_t top = std::min<std::size_t> (5, loud.size());
        const std::size_t half = (top + 1) / 2;   // Math.ceil (top.length / 2)

        std::vector<double> res;
        detail::Arr<double> df (maxT + 2), cm (maxT + 2);
        std::vector<float> frame (static_cast<std::size_t> (W + maxT + 1));   // d[st .. st + W + maxT]
        const float* d = frame.data();

        auto examine = [&] (std::int64_t st)
        {
            mono.read (st, static_cast<int> (W + maxT + 1), frame.data());
            for (std::int64_t tau = 1; tau <= maxT + 1; ++tau)
            {
                double s = 0;
                for (std::int64_t j = 0; j < W; ++j)
                {
                    const double x = static_cast<double> (d[j]) - static_cast<double> (d[j + tau]);
                    s += x * x;
                }
                df[tau] = s;
            }
            double run = 0;
            std::int64_t best = -1;
            cm[0] = 1;
            for (std::int64_t tau = 1; tau <= maxT + 1; ++tau)
            {
                run += df[tau];
                cm[tau] = df[tau] * static_cast<double> (tau) / (detail::truthy (run) ? run : 1.0);   // (run || 1)
                if (run == 0 && ! options.prototypeDc)
                    cm[tau] = 1;   // decision 3: no difference at any lag up to here is not a period
            }
            for (std::int64_t tau = minT; tau <= maxT; ++tau)
            {
                if (cm[tau] < 0.15)
                {
                    while (tau + 1 <= maxT && cm[tau + 1] < cm[tau])
                        ++tau;
                    best = tau;
                    break;
                }
            }
            if (best < 0)
            {
                double m = 1e9;
                for (std::int64_t tau = minT; tau <= maxT; ++tau)
                    if (cm[tau] < m)
                    {
                        m = cm[tau];
                        best = tau;
                    }
                if (m > 0.45)
                    return;   // not periodic
            }
            // best >= 1 here: cm[0] is 1, which is neither below 0.15 nor a minimum of 0.45 or less
            const double a = cm[best - 1], b = cm[best], c = cm[best + 1], den = a - 2 * b + c;
            const double sh = detail::truthy (den) ? 0.5 * (a - c) / den : 0;
            res.push_back (sr / (static_cast<double> (best) + std::max (-1.0, std::min (1.0, sh))));
        };

        // the 5 loudest frames; when fewer than half of them are periodic (loud bursts over a held note), the
        // next 10 loudest are examined too, and the fallback below decides
        for (int pass = 0; pass < 2; ++pass)
        {
            std::size_t from = 0, to = top;
            if (pass == 1)
            {
                if (res.size() >= half)
                    break;
                from = 5;
                to = std::min<std::size_t> (15, loud.size());
            }
            for (std::size_t i = from; i < to; ++i)
            {
                if (stop && stop())
                    return std::nullopt;
                examine (loud[i]);
            }
        }
        if (res.empty())
            return std::nullopt;
        std::sort (res.begin(), res.end());
        double hz = res[res.size() / 2];
        if (res.size() < half)
        {
            // fewer than half the frames are periodic (loud bursts over a held note): still a pitch when at least
            // two periodic frames agree within 20 cents (v3 accepted any single frame; noise rarely gives two
            // that agree)
            std::size_t bi = 0, bn = 1;
            bool found = false;
            for (std::size_t i = 0; i < res.size(); ++i)
            {
                std::size_t j = i;
                while (j + 1 < res.size() && 1200 * std::log2 (res[j + 1] / res[i]) <= 20)
                    ++j;
                if (j - i + 1 > bn)
                {
                    bn = j - i + 1;
                    bi = i;
                    found = true;
                }
            }
            if (! found)
                return std::nullopt;
            hz = res[bi + bn / 2];
        }
        const double m = 69 + 12 * std::log2 (hz / 440), midi = core::jsRound (m);
        if (! std::isfinite (m))
            return std::nullopt;   // below 3 kHz a lag of 1 can give 1 / 0 (the prototype returns Infinity)
        return Pitch { static_cast<int> (midi), core::jsRound ((m - midi) * 1000) / 10, hz };
    }
}
