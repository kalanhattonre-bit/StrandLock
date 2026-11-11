#include "engine/GrainRig.h"

#include "support/Measure.h"
#include "support/TestSignals.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <utility>

namespace sl::test::grains
{
    namespace
    {
        constexpr double kPi = 3.141592653589793;

        double dB (double m) { return 20 * std::log10 (std::max (1e-20, m)); }

        /** interpPeak (m, k): parabolic interpolation in dB around bin k. */
        double interpPeak (const std::vector<double>& m, std::size_t k)
        {
            const double a = dB (m[k - 1]), b = dB (m[k]), c = dB (m[k + 1]);
            const double den = a - 2 * b + c;
            return static_cast<double> (k) + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0);
        }

        std::int64_t lastStart (const std::vector<float>& x, int order, std::int64_t start)
        {
            return start < 0 ? static_cast<std::int64_t> (x.size()) - (std::int64_t { 1 } << order) : start;
        }
    }

    std::vector<double> spectrum (const std::vector<float>& x, std::int64_t start, int order)
    {
        const std::size_t n = std::size_t { 1 } << order;
        std::vector<std::complex<double>> a (n);
        const auto size = static_cast<std::int64_t> (x.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = 2 * kPi * static_cast<double> (i) / static_cast<double> (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
            const std::int64_t at = start + static_cast<std::int64_t> (i);
            const double v = at >= 0 && at < size ? static_cast<double> (x[static_cast<std::size_t> (at)]) : 0.0;
            a[i] = { v * w, 0.0 };
        }
        measure::fft (a);
        std::vector<double> m (n / 2);
        for (std::size_t k = 0; k < n / 2; ++k)
            m[k] = std::abs (a[k]);
        return m;
    }

    F0 harmonicF0 (const std::vector<float>& x, double sr, double fExp, int order, std::int64_t start, int maxK)
    {
        const std::size_t n = std::size_t { 1 } << order;
        const std::vector<double> m = spectrum (x, lastStart (x, order, start), order);
        const double bin = sr / static_cast<double> (n);
        struct Cand
        {
            int k;
            double f, mag;
        };
        std::vector<Cand> cands;
        double strongest = 0;
        for (int k = 1; k <= maxK; ++k)
        {
            const double fc = fExp * k;
            if (fc > sr * 0.45)
                break;
            const auto lo = static_cast<std::int64_t> (std::max (2.0, std::floor (fc * std::pow (2.0, -0.6 / 12) / bin)));
            const auto hi = static_cast<std::int64_t> (std::min (static_cast<double> (n / 2 - 2), std::ceil (fc * std::pow (2.0, 0.6 / 12) / bin)));
            std::int64_t bk = -1;
            for (std::int64_t q = lo; q <= hi; ++q)
                if (bk < 0 || m[static_cast<std::size_t> (q)] > m[static_cast<std::size_t> (bk)])
                    bk = q;
            if (bk <= lo || bk >= hi)
                continue;   // a true peak must be inside the window
            const auto b = static_cast<std::size_t> (bk);
            cands.push_back ({ k, interpPeak (m, b) * bin, m[b] });
            strongest = std::max (strongest, m[b]);
        }
        double sw = 0, sf = 0;
        F0 out;
        for (const Cand& c : cands)
            if (c.mag >= strongest * 0.01)
            {
                sw += c.mag;
                sf += c.mag * c.f / c.k;
                ++out.used;
            }
        out.f0 = sw > 0 ? sf / sw : 0;
        return out;
    }

    double centroid (const std::vector<float>& x, double sr, int order, std::int64_t start, double fMax)
    {
        const std::size_t n = std::size_t { 1 } << order;
        const std::vector<double> m = spectrum (x, lastStart (x, order, start), order);
        const double bin = sr / static_cast<double> (n);
        double a = 0, b = 0;
        for (std::size_t k = 1; static_cast<double> (k) < std::min (static_cast<double> (n / 2), fMax / bin); ++k)
        {
            a += static_cast<double> (k) * bin * m[k] * m[k];
            b += m[k] * m[k];
        }
        return b > 0 ? a / b : 0;
    }

    double corr (const float* a, const float* b, std::size_t n)
    {
        double ab = 0, aa = 0, bb = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double x = a[i], y = b[i];
            ab += x * y;
            aa += x * x;
            bb += y * y;
        }
        return ab / std::sqrt (std::max (1e-30, aa * bb));
    }

    double cents (double f, double ref)
    {
        return 1200 * std::log2 (f / ref);
    }

    Stats stats (std::vector<double> values)
    {
        values.erase (std::remove_if (values.begin(), values.end(), [] (double v) { return ! std::isfinite (v); }), values.end());
        std::sort (values.begin(), values.end());
        Stats s;
        s.n = static_cast<int> (values.size());
        if (values.empty())
        {
            s.min = s.med = s.max = std::nan ("");
            return s;
        }
        const auto q = [&values] (double p) {
            const auto i = static_cast<std::size_t> (std::floor (p * static_cast<double> (values.size() - 1)));
            return values[std::min (values.size() - 1, i)];
        };
        s.min = values.front();
        s.med = q (0.5);
        s.max = values.back();
        return s;
    }

    double largestDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size())
            return HUGE_VAL;
        double d = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            d = std::max (d, std::abs (static_cast<double> (a[i]) - b[i]));
        return d;
    }

    //==============================================================================================

    Play::Play (double sampleRate, const core::Inst& inst, std::unique_ptr<engine::SampleData> s)
        : e (std::make_unique<engine::Engine>()), sample (std::move (s)), frame (std::make_unique<engine::DisplayFrame>())
    {
        e->prepare (sampleRate);
        e->setRampMs (0);
        e->setInst (inst);
        if (sample != nullptr)
            e->setSample (sample.get(), 1);
    }

    void Play::snapshot (std::unique_ptr<core::Snapshot> s)
    {
        snap = std::move (s);
        e->setSnapshot (snap.get(), 1);
    }

    void Play::play (double bpm, double beat, bool loop)
    {
        engine::Transport t;
        t.playing = true;
        t.bpm = bpm;
        t.beat = beat;
        t.loop = loop;
        e->setTransport (t);
    }

    void Play::render (std::int64_t n, int block, const std::function<void (std::int64_t)>& hook)
    {
        const std::size_t base = L.size();
        L.resize (base + static_cast<std::size_t> (n), 0.0f);
        R.resize (base + static_cast<std::size_t> (n), 0.0f);
        std::int64_t i = 0;
        while (i < n)
        {
            const std::int64_t m = std::min<std::int64_t> (block, n - i);
            e->process (L.data() + base + static_cast<std::size_t> (i), R.data() + base + static_cast<std::size_t> (i), static_cast<int> (m));
            i += m;
            if (hook)
                hook (static_cast<std::int64_t> (base) + i);
        }
    }

    const engine::DisplayFrame& Play::display()
    {
        e->fillDisplay (*frame);
        return *frame;
    }

    double Play::decodeAt (std::int64_t i, std::int64_t w) const
    {
        return rig::decodeAt (L, R, i, w);
    }

    core::Inst instWith (core::Engine engine)
    {
        core::Inst inst = core::defaultInst();
        inst.engine = engine;
        return inst;
    }

    std::unique_ptr<engine::SampleData> mono (std::vector<float> x, double sampleRate)
    {
        return engine::makeSampleData (std::move (x), {}, sampleRate, "test");
    }

    std::unique_ptr<engine::SampleData> ramp (double secs, double sampleRate)
    {
        return rig::sampleOf (signals::rampSource (secs, sampleRate));
    }

    rig::RawNote rawNote (const char* id, double p, double s, double l, core::Engine engine)
    {
        rig::RawNote n;
        n.id = id;
        n.p = p;
        n.s = s;
        n.l = l;
        n.engine = engine;
        return n;
    }
}
