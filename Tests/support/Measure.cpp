#include "Measure.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sl::test::measure
{
    void fft (std::vector<std::complex<double>>& a)
    {
        const std::size_t n = a.size();
        if (n < 2)
            return;

        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            std::size_t bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }

        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = -2.0 * std::numbers::pi / (double) len;
            const std::complex<double> wl (std::cos (angle), std::sin (angle));
            for (std::size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (std::size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k];
                    const auto v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    namespace
    {
        /** |FFT| of the last 2^order samples under a Blackman-Harris window, bins 0 .. n/2. */
        std::vector<double> magnitudes (const std::vector<float>& x, int order)
        {
            const std::size_t n = (std::size_t) 1 << order;
            if (x.size() < n)
                return {};
            const std::size_t start = x.size() - n;
            std::vector<std::complex<double>> buf (n);
            for (std::size_t i = 0; i < n; ++i)
            {
                const double t = 2.0 * std::numbers::pi * (double) i / (double) (n - 1);
                const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
                buf[i] = { (double) x[start + i] * w, 0.0 };
            }
            fft (buf);
            std::vector<double> mag (n / 2 + 1);
            for (std::size_t k = 0; k <= n / 2; ++k)
                mag[k] = std::abs (buf[k]);
            return mag;
        }

        double db (double m) { return 20.0 * std::log10 (std::max (1e-30, m)); }
    }

    double fftFundamentalHz (const std::vector<float>& x, double sampleRate, int order, double withinDb)
    {
        const auto mag = magnitudes (x, order);
        if (mag.empty())
            return 0.0;
        const double n = (double) ((std::size_t) 1 << order);
        const double binHz = sampleRate / n;
        const auto lo = (std::size_t) std::ceil (20.0 / binHz);
        const auto hi = std::min (mag.size() - 2, (std::size_t) (std::min (20000.0, 0.5 * sampleRate - 100.0) / binHz));

        double strongest = 0.0;
        for (std::size_t k = lo; k <= hi; ++k)
            strongest = std::max (strongest, mag[k]);
        const double threshold = strongest * std::pow (10.0, -withinDb / 20.0);

        for (std::size_t k = lo + 1; k < hi; ++k)
        {
            const double m = mag[k];
            if (m >= threshold && m >= mag[k - 1] && m >= mag[k + 1])
            {
                const double a = db (mag[k - 1]), b = db (m), c = db (mag[k + 1]);
                const double denom = a - 2.0 * b + c;
                const double delta = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
                return ((double) k + delta) * binHz;
            }
        }
        return 0.0;
    }

    double autocorrFundamentalHz (const std::vector<float>& x, double sampleRate, double fMin, double fMax)
    {
        const int length = 16384;
        const int minLag = (int) std::floor (sampleRate / fMax);
        const int maxLag = (int) std::ceil (sampleRate / fMin);
        if (minLag < 2 || (std::size_t) (length + maxLag + 2) > x.size())
            return 0.0;
        const std::size_t start = x.size() - (std::size_t) (length + maxLag + 2);

        std::vector<double> r ((std::size_t) (maxLag + 2), 0.0);
        for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int i = 0; i < length; ++i)
            {
                const double a = x[start + (std::size_t) i];
                const double b = x[start + (std::size_t) (i + lag)];
                xy += a * b;
                xx += a * a;
                yy += b * b;
            }
            r[(std::size_t) lag] = xy / std::sqrt (std::max (1e-30, xx * yy));
        }

        double best = -1.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
            best = std::max (best, r[(std::size_t) lag]);

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double v = r[(std::size_t) lag];
            if (v >= 0.9 * best && v >= r[(std::size_t) (lag - 1)] && v >= r[(std::size_t) (lag + 1)])
            {
                const double a = r[(std::size_t) (lag - 1)], c = r[(std::size_t) (lag + 1)];
                const double denom = a - 2.0 * v + c;
                const double delta = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
                return sampleRate / ((double) lag + delta);
            }
        }
        return 0.0;
    }

    double centsBetween (double measured, double expected)
    {
        return measured > 0.0 && expected > 0.0 ? 1200.0 * std::log2 (measured / expected) : 1.0e9;
    }

    double rms (const float* x, std::size_t n)
    {
        if (n == 0)
            return 0.0;
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            s += (double) x[i] * (double) x[i];
        return std::sqrt (s / (double) n);
    }

    double peak (const float* x, std::size_t n)
    {
        double p = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            p = std::max (p, (double) std::abs (x[i]));
        return p;
    }

    double largestStep (const float* x, std::size_t n)
    {
        double s = 0.0;
        for (std::size_t i = 1; i < n; ++i)
            s = std::max (s, std::abs ((double) x[i] - (double) x[i - 1]));
        return s;
    }

    double centroidHz (const std::vector<float>& x, double sampleRate, int order)
    {
        const auto mag = magnitudes (x, order);
        if (mag.empty())
            return 0.0;
        const double binHz = sampleRate / (double) ((std::size_t) 1 << order);
        double num = 0.0, den = 0.0;
        for (std::size_t k = 1; k < mag.size(); ++k)
        {
            num += (double) k * binHz * mag[k];
            den += mag[k];
        }
        return den > 0.0 ? num / den : 0.0;
    }
}
