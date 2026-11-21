#pragma once

// The prototype's own radix-2 FFT (sl-analysis.js plan and fft), not juce::dsp (the plugin has no juce_dsp),
// so suggestParts' spectra come out of the same operations in the same order (PLUGIN-PLAN 1.2).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace sl::analysis
{
    /** plan (n): the bit-reversal table and the twiddles cs[i] = cos (2 pi i / n) and sn[i] = -sin (2 pi i / n)
        for i < n / 2. n is a power of two, at least 2. (The prototype keeps one plan per size in a Map; a plan
        is the same numbers either way.) */
    struct FftPlan
    {
        int n = 0;
        std::vector<std::uint32_t> rev;
        std::vector<double> cs, sn;

        explicit FftPlan (int size)
            : n (size),
              rev (static_cast<std::size_t> (size)),
              cs (static_cast<std::size_t> (size / 2)),
              sn (static_cast<std::size_t> (size / 2))
        {
            int bits = 0;   // Math.log2 (n) | 0, without the logarithm: n is a power of two
            while ((2 << bits) <= n)
                ++bits;
            std::uint32_t* r = rev.data();
            for (int i = 0; i < n; ++i)
            {
                std::uint32_t v = 0;
                for (int b = 0; b < bits; ++b)
                    v |= static_cast<std::uint32_t> ((i >> b) & 1) << (bits - 1 - b);
                r[i] = v;
            }
            double* c = cs.data();
            double* s = sn.data();
            for (int i = 0; i < n / 2; ++i)
            {
                c[i] = std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n));
                s[i] = -std::sin (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (n));
            }
        }
    };

    /** fft (re, im, p): in place, over p.n values: the bit-reversal swaps, then the butterflies, as written. */
    inline void fft (double* re, double* im, const FftPlan& p) noexcept
    {
        const int n = p.n;
        const std::uint32_t* rev = p.rev.data();
        const double* cs = p.cs.data();
        const double* sn = p.sn.data();
        for (int i = 0; i < n; ++i)
        {
            const auto j = static_cast<int> (rev[i]);
            if (j > i)
            {
                double t = re[i];
                re[i] = re[j];
                re[j] = t;
                t = im[i];
                im[i] = im[j];
                im[j] = t;
            }
        }
        for (int size = 2; size <= n; size <<= 1)
        {
            const int half = size >> 1, step = n / size;
            for (int s = 0; s < n; s += size)
            {
                for (int j = 0, k = 0; j < half; ++j, k += step)
                {
                    const int a = s + j, b = a + half;
                    const double wr = cs[k], wi = sn[k];
                    const double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }
}
