#include "analysis/Analysis.h"
#include "analysis/Detail.h"

#include "core/JsCompat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numbers>
#include <vector>

// sl-analysis.js detectKey, line by line (v3's detectKey): Goertzel chroma over MIDI 36-95 in up to 40 frames
// of 4096 spread over the sound, then the Krumhansl-Kessler profiles in all 24 keys.

namespace sl::analysis
{
    namespace
    {
        using Chroma = std::array<double, 12>;

        constexpr Chroma KK_MAJOR = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        constexpr Chroma KK_MINOR = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        double corr (const Chroma& a, const Chroma& b) noexcept
        {
            double ma = 0, mb = 0;
            for (std::size_t i = 0; i < 12; ++i)
            {
                ma += a[i] / 12;
                mb += b[i] / 12;
            }
            double n = 0, da = 0, db = 0;
            for (std::size_t i = 0; i < 12; ++i)
            {
                n += (a[i] - ma) * (b[i] - mb);
                da += (a[i] - ma) * (a[i] - ma);   // ** 2 (V8's x ** 2 is x * x)
                db += (b[i] - mb) * (b[i] - mb);
            }
            const double p = da * db;
            return n / std::sqrt (detail::truthy (p) ? p : 1.0);   // da * db || 1
        }
    }

    const char* scaleName (Scale scale) noexcept
    {
        return scale == Scale::minor ? "Minor" : "Major";
    }

    std::optional<Key> detectKey (const Source& source, const Stop& stop)
    {
        if (! detail::validInput (source))
            return std::nullopt;
        const double sr = source.sampleRate();
        const std::int64_t length = source.numFrames();   // d.length
        constexpr int N = 4096;
        const std::int64_t frames = std::min<std::int64_t> (40, length / N);
        if (frames < 1)
            return std::nullopt;
        Chroma chroma {};
        std::vector<float> win (N);
        for (std::size_t i = 0; i < static_cast<std::size_t> (N); ++i)
            win[i] = static_cast<float> (0.5 - 0.5 * std::cos (2 * std::numbers::pi * static_cast<double> (i) / N));
        std::array<double, 60> coefs {};
        for (int m = 36; m < 96; ++m)
            coefs[static_cast<std::size_t> (m - 36)] =
                2 * std::cos (2 * std::numbers::pi * 440 * std::pow (2.0, static_cast<double> (m - 69) / 12) / sr);

        detail::MonoReader mono (source);
        std::vector<float> d (N), xw (N);
        for (std::int64_t f = 0; f < frames; ++f)
        {
            if (stop && stop())
                return std::nullopt;
            const auto st = static_cast<std::int64_t> (std::floor (static_cast<double> (f) * static_cast<double> (length - N)
                                                                   / static_cast<double> (std::max<std::int64_t> (1, frames - 1))));
            mono.read (st, N, d.data());
            for (std::size_t i = 0; i < static_cast<std::size_t> (N); ++i)
                xw[i] = static_cast<float> (static_cast<double> (d[i]) * static_cast<double> (win[i]));
            for (int m = 36; m < 96; ++m)
            {
                const double co = coefs[static_cast<std::size_t> (m - 36)];
                double s1 = 0, s2 = 0;
                for (std::size_t i = 0; i < static_cast<std::size_t> (N); ++i)
                {
                    const double s0 = static_cast<double> (xw[i]) + co * s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                chroma[static_cast<std::size_t> (m % 12)] += std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - co * s1 * s2));
            }
        }
        double tot = 0;
        for (std::size_t i = 0; i < 12; ++i)
            tot += chroma[i];
        if (! (tot > 1e-6))
            return std::nullopt;

        Key best { 0, Scale::major, -2 };
        Chroma rot {};
        for (std::size_t k = 0; k < 12; ++k)
        {
            for (std::size_t i = 0; i < 12; ++i)
                rot[i] = chroma[(i + k) % 12];
            for (const Scale sc : { Scale::major, Scale::minor })
            {
                const double r = corr (rot, sc == Scale::major ? KK_MAJOR : KK_MINOR);
                if (r > best.r)
                    best = { static_cast<int> (k), sc, r };
            }
        }
        return Key { best.key, best.scale, core::jsRound (best.r * 1000) / 1000 };
    }
}
