#include "analysis/Analysis.h"
#include "analysis/Detail.h"
#include "analysis/Fft.h"

#include "core/JsCompat.h"
#include "core/JsMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

// sl-analysis.js features, suggestParts, estimateTempo and overlapOk, line by line. Features are made on a ~11 kHz
// mono copy of the sound: fine frames (128 samples, about 11.6 ms) give the level, the high band's level and the
// onsets; coarse frames (512 samples, about 46 ms) add spectral flatness and spectral flux from one FFT each. The
// copy is made a block at a time (Detail.h's Decimator) and read again around each onset, never kept whole.

namespace sl::analysis
{
    namespace
    {
        using core::jsRound;
        using detail::Arr;
        using detail::View;

        constexpr double kInf = std::numeric_limits<double>::infinity();

        /** The analysis rates (C++ guards): below about 120 Hz the prototype reads past its spectrum, and the
            11 kHz copy's step must stay a small whole number. Real files are 8 kHz and up. */
        constexpr double kMinRate = 1000.0, kMaxRate = 1.0e7;

        double clamp (double x, double a, double b) noexcept
        {
            return x < a ? a : x > b ? b : x;
        }

        std::int64_t whole (double x) noexcept
        {
            return static_cast<std::int64_t> (x);
        }

        /** pct (arr, q): the q-quantile of the finite values, 0 when there are none. */
        double pct (const std::vector<double>& arr, double q)
        {
            std::vector<double> a;
            a.reserve (arr.size());
            for (const double x : arr)
                if (std::isfinite (x))
                    a.push_back (x);
            if (a.empty())
                return 0;
            std::sort (a.begin(), a.end());
            const double last = static_cast<double> (a.size() - 1);
            return a[static_cast<std::size_t> (std::min (last, std::max (0.0, jsRound (q * last))))];
        }

        /** fmtTime (s): "m:ss". */
        std::string fmtTime (double s)
        {
            const double m = std::floor (s / 60), r = std::floor (s - m * 60);
            return core::jsNumberToString (m) + ":" + (r < 10 ? "0" : "") + core::jsNumberToString (r);
        }

        /** win (c, K)'s result. stdDev is the prototype's `std`. */
        struct Win
        {
            double meanRel, stdDev, flat, flux, onRate, big, sil;
        };

        struct Scored
        {
            std::int64_t s;
            double score;
        };

        struct Cand
        {
            std::int64_t c;
            double score;
        };

        struct HitCand
        {
            std::int64_t i;
            double score, pk;
        };
    }

    const char* partKindName (PartKind kind) noexcept
    {
        switch (kind)
        {
            case PartKind::steady: return "steady";
            case PartKind::hit:    return "hit";
            case PartKind::quiet:  return "quiet";
            case PartKind::loop:   return "loop";
        }
        return "steady";
    }

    bool overlapOk (Span a, const std::vector<Span>& chosen) noexcept
    {
        for (const Span& b : chosen)
        {
            const double ov = std::min (a.e, b.e) - std::max (a.s, b.s);
            if (ov > 0.5 * std::min (a.e - a.s, b.e - b.s) + 1e-9)
                return false;
        }
        return true;
    }

    //==============================================================================================
    Features features (const Source& source, const Stop& stop)
    {
        Features ft;
        const double sr = source.sampleRate();
        if (! detail::validInput (source) || ! (sr >= kMinRate && sr <= kMaxRate))
            return ft;
        const std::int64_t N = source.numFrames();
        const int nCh = source.numChannels();
        ft.D = whole (std::max (1.0, jsRound (sr / 11025)));
        ft.srd = sr / static_cast<double> (ft.D);
        ft.M = N / ft.D;   // Math.floor (N / D)
        ft.g = 1 / static_cast<double> (ft.D * nCh);

        const std::int64_t H = ft.H;
        ft.nf = ft.M / H;
        ft.tf = static_cast<double> (H) / ft.srd;
        Arr<double> Lf (ft.nf), Hf (ft.nf);

        // coarse frames
        const std::int64_t C = ft.C;
        ft.nc = ft.nf / C;
        ft.tc = ft.tf * static_cast<double> (C);
        constexpr int F = 128 * 4;   // H * C
        const FftPlan P (F);
        std::array<double, F> win {};
        for (std::size_t i = 0; i < win.size(); ++i)
            win[i] = 0.5 - 0.5 * std::cos (2 * std::numbers::pi * static_cast<double> (i) / F);
        const double binHz = ft.srd / F;
        const std::int64_t b0 = whole (std::max (1.0, std::ceil (60 / binHz))), b1 = whole (std::min (F / 2.0 - 1, std::floor (5000 / binHz)));
        constexpr int NB = 16;
        std::array<std::int64_t, NB + 1> bandEdge {};
        for (std::size_t k = 0; k <= NB; ++k)
            bandEdge[k] = whole (jsRound (static_cast<double> (b0)
                                          * std::pow (static_cast<double> (b1) / static_cast<double> (b0), static_cast<double> (k) / NB)));
        Arr<double> Lc (ft.nc), flat (ft.nc), flux (ft.nc);
        std::array<double, F> re {}, im {};
        std::array<double, NB> bands {}, prevB {};

        // The prototype makes every fine frame, then every coarse frame. Coarse frame c is fine frames 4c .. 4c + 3
        // (y[512 c .. 512 c + 511]), and each pass runs in frame order, so making them a block of y at a time,
        // fine frames first in each block, gives the same values.
        detail::Decimator decimator (source, ft.D, ft.g);
        const std::int64_t perRead = std::max<std::int64_t> (1, std::min<std::int64_t> (16, (std::int64_t { 1 } << 20) / (F * ft.D)));
        std::vector<float> y (static_cast<std::size_t> (perRead * F));
        double prev = 0;
        auto fineFrame = [&] (std::int64_t i, const float* yy)
        {
            double e = 0, eh = 0;
            for (std::int64_t j = 0; j < H; ++j)
            {
                const double v = yy[j], dv = v - prev;
                e += v * v;
                eh += dv * dv;
                prev = v;
            }
            Lf[i] = 10 * std::log10 (e / static_cast<double> (H) + 1e-12);
            Hf[i] = 10 * std::log10 (eh / static_cast<double> (H) + 1e-12);
        };
        auto coarseFrame = [&] (std::int64_t c, const float* yc)
        {
            double e = 0;
            for (std::int64_t q = 0; q < C; ++q)
                e += std::pow (10.0, Lf[c * C + q] / 10);
            Lc[c] = 10 * std::log10 (e / static_cast<double> (C) + 1e-12);
            for (std::size_t i = 0; i < static_cast<std::size_t> (F); ++i)
            {
                re[i] = static_cast<double> (yc[i]) * win[i];
                im[i] = 0;
            }
            fft (re.data(), im.data(), P);
            const double* rp = re.data();
            const double* ip = im.data();
            // flatness with the spectrum floored 50 dB under its peak, so a faint noise floor between harmonics does
            // not make a held tone look noisy (noise itself stays ~0.5)
            double pmax = 1e-14;
            for (std::int64_t b = b0; b <= b1; ++b)
            {
                const double pw = rp[b] * rp[b] + ip[b] * ip[b];
                if (pw > pmax)
                    pmax = pw;
            }
            const double pfl = pmax * 1e-5;
            double lsum = 0, asum = 0;
            for (std::int64_t b = b0; b <= b1; ++b)
            {
                const double pw = std::max (pfl, rp[b] * rp[b] + ip[b] * ip[b]);
                lsum += std::log (pw);
                asum += pw;
            }
            const auto nb = static_cast<double> (b1 - b0 + 1);
            flat[c] = core::jsExp (lsum / nb) / (asum / nb);
            // flux: dB change per band, weighted by each band's share of the energy (faint bands barely count)
            double fx = 0, wsum = 0;
            for (std::size_t k = 0; k < static_cast<std::size_t> (NB); ++k)
            {
                double s = 0;
                for (std::int64_t b = bandEdge[k]; b < std::max (bandEdge[k] + 1, bandEdge[k + 1]); ++b)
                    s += rp[b] * rp[b] + ip[b] * ip[b];
                bands[k] = s;
                if (c > 0)
                {
                    const double w = s + prevB[k];
                    fx += w * std::abs (10 * std::log10 ((s + 1e-14) / (prevB[k] + 1e-14)));
                    wsum += w;
                }
            }
            prevB = bands;
            flux[c] = c > 0 && wsum > 0 ? fx / wsum : 0;
        };

        for (std::int64_t c0 = 0; c0 < ft.nc; c0 += perRead)
        {
            if (stop && stop())
            {
                ft.stopped = true;
                return ft;
            }
            const std::int64_t count = std::min (perRead, ft.nc - c0);
            decimator.read (c0 * F, static_cast<int> (count * F), y.data());
            for (std::int64_t k = 0; k < count; ++k)
            {
                const float* yc = y.data() + k * F;
                for (std::int64_t q = 0; q < C; ++q)
                    fineFrame ((c0 + k) * C + q, yc + q * H);
                coarseFrame (c0 + k, yc);
            }
        }
        // the fine frames after the last coarse frame (at most 3)
        if (ft.nf > ft.nc * C)
        {
            const std::int64_t first = ft.nc * C;
            decimator.read (first * H, static_cast<int> ((ft.nf - first) * H), y.data());
            for (std::int64_t i = first; i < ft.nf; ++i)
                fineFrame (i, y.data() + (i - first) * H);
        }

        ft.Lf = std::move (Lf.v);
        ft.Hf = std::move (Hf.v);
        ft.Lc = std::move (Lc.v);
        ft.flat = std::move (flat.v);
        ft.flux = std::move (flux.v);
        return ft;
    }

    //==============================================================================================
    // Tempo from the autocorrelation of the onset curve, 60-200 bpm, weighted towards 120; refined on the bar lag.
    Tempo estimateTempo (const std::vector<double>& onsets, double tf)
    {
        const View<double> on (onsets);
        const std::int64_t n = on.size();
        double mean = 0;
        std::int64_t strong = 0;
        for (std::int64_t i = 0; i < n; ++i)
        {
            mean += on[i] / static_cast<double> (n);
            if (on[i] >= 6)
                ++strong;
        }
        Arr<double> o (n);
        double e0 = 0;
        for (std::int64_t i = 0; i < n; ++i)
        {
            o[i] = on[i] - mean;
            e0 += o[i] * o[i];
        }
        const Tempo fallback { 120, false };
        if (strong < 8 || ! (e0 > 0))
            return fallback;
        auto ac = [&] (std::int64_t lag)
        {
            if (lag < 0)
                return std::numeric_limits<double>::quiet_NaN();   // o[-1] is undefined in JavaScript: the sum is NaN
            double s = 0;
            for (std::int64_t i = 0; i + lag < n; ++i)
                s += o[i] * o[i + lag];
            return s / e0;
        };
        const double lagMinD = std::floor (60.0 / 200 / tf), lagMaxD = std::min (static_cast<double> (n - 2), std::ceil (60.0 / 60 / tf));
        if (! (lagMaxD > lagMinD + 2))
            return fallback;   // lagMax <= lagMin + 2 (a NaN rate ends there too)
        const std::int64_t lagMin = whole (lagMinD), lagMax = whole (lagMaxD);
        // r[l] for l = lagMin - 1 .. lagMax + 1, kept at rr[l + 1]: lagMin is 0 when a fine frame is longer than 0.3 s,
        // and the prototype's r[-1] then reads as undefined, which compares false like the NaN ac (-1) gives
        Arr<double> rr (lagMax + 3);
        auto r = [&rr] (std::int64_t l) -> double& { return rr[l + 1]; };
        for (std::int64_t l = lagMin - 1; l <= lagMax + 1; ++l)
            r (l) = ac (l);
        std::int64_t best = -1;
        double bestW = -kInf;
        for (std::int64_t l = lagMin; l <= lagMax; ++l)
        {
            if (! (r (l) >= r (l - 1) && r (l) >= r (l + 1)))
                continue;
            const double bpm = 60 / (static_cast<double> (l) * tf), x = std::log2 (bpm / 120) / 0.7;
            const double w = r (l) * core::jsExp (-0.5 * (x * x));   // Math.pow (x, 2) is x * x in V8
            if (w > bestW)
            {
                bestW = w;
                best = l;
            }
        }
        if (best < 0 || r (best) < 0.08)
            return fallback;
        auto interp = [] (double at, double a, double b, double c)
        {
            const double den = a - 2 * b + c;
            return at + (detail::truthy (den) ? 0.5 * (a - c) / den : 0);
        };
        double lag = interp (static_cast<double> (best), r (best - 1), r (best), r (best + 1));
        // refine on the lag of 4 beats (one bar) when the file is long enough
        const std::int64_t l4 = whole (jsRound (4 * lag));
        if (static_cast<double> (l4 + 6) < static_cast<double> (n) / 2)
        {
            std::int64_t bl = l4;
            double bv = -kInf;
            for (std::int64_t l = l4 - 4; l <= l4 + 4; ++l)
            {
                const double v = ac (l);
                if (v > bv)
                {
                    bv = v;
                    bl = l;
                }
            }
            if (bv > 0.5 * r (best))
            {
                // the guard: at the search's edge the parabola can put the lag at 0 or below, where the prototype's
                // octave loops below never end; such a refinement is dropped
                const double refined = interp (static_cast<double> (bl), ac (bl - 1), bv, ac (bl + 1)) / 4;
                const double check = 60 / (refined * tf);
                if (check > 0 && check < kInf)
                    lag = refined;
            }
        }
        double bpm = 60 / (lag * tf);
        while (bpm < 60)
            bpm *= 2;
        while (bpm > 200)
            bpm /= 2;
        if (std::abs (bpm - jsRound (bpm)) < 0.25)
            bpm = jsRound (bpm);
        return { jsRound (bpm * 100) / 100, true };
    }

    //==============================================================================================
    std::vector<Part> suggestParts (const Source& source, const Stop& stop)
    {
        if (! detail::validInput (source))
            return {};
        const double sr = source.sampleRate();
        const double dur = static_cast<double> (source.numFrames()) / sr;
        if (dur < 1.2 || ! (sr >= kMinRate))
            return {};
        const Features ft = features (source, stop);
        if (ft.stopped)
            return {};
        const std::int64_t nf = ft.nf, C = ft.C, nc = ft.nc;
        const double tf = ft.tf, tc = ft.tc;
        const View<double> Lf (ft.Lf), Hf (ft.Hf), Lc (ft.Lc), flat (ft.flat), flux (ft.flux);
        if (nc < 8)
            return {};

        // levels relative to the loud parts of the file
        const double Lref = pct (ft.Lc, 0.98);
        if (! (Lref > -100))
            return {};
        const double floorDb = Lref - 70, HrefFloor = pct (ft.Hf, 0.98) - 70;
        Arr<std::uint8_t> sil (nc);
        for (std::int64_t c = 0; c < nc; ++c)
            sil[c] = static_cast<std::uint8_t> (Lc[c] < Lref - 60 || Lc[c] < -100 ? 1 : 0);

        // onsets (dB rise over the quietest of the three previous fine frames, in either band)
        Arr<double> rise (nf), on (nf);
        for (std::int64_t i = 0; i < nf; ++i)
        {
            const double l = std::max (Lf[i], floorDb), h = std::max (Hf[i], HrefFloor);
            double ml = kInf, mh = kInf;
            for (std::int64_t q = 1; q <= 3; ++q)
            {
                ml = std::min (ml, i - q >= 0 ? std::max (Lf[i - q], floorDb) : floorDb);
                mh = std::min (mh, i - q >= 0 ? std::max (Hf[i - q], HrefFloor) : HrefFloor);
            }
            rise[i] = std::max (l - ml, h - mh);
            on[i] = std::max (0.0, rise[i] - 3);
        }

        // prefix sums on the coarse grid
        auto pre = [nc] (auto f)
        {
            Arr<double> a (nc + 1);
            for (std::int64_t c = 0; c < nc; ++c)
                a[c + 1] = a[c] + f (c);
            return a;
        };
        Arr<double> onC (nc), bigC (nc);
        for (std::int64_t c = 0; c < nc; ++c)
            for (std::int64_t q = 0; q < C; ++q)
            {
                const double v = on[c * C + q];
                onC[c] += v;
                if (rise[c * C + q] >= 15)
                    bigC[c] += 1;
            }
        const Arr<double> sL = pre ([&] (std::int64_t c) { return Lc[c] - Lref; });
        const Arr<double> sL2 = pre ([&] (std::int64_t c) { return (Lc[c] - Lref) * (Lc[c] - Lref); });
        const Arr<double> sF = pre ([&] (std::int64_t c) { return sil[c] != 0 ? 1.0 : std::min (1.0, flat[c]); });
        const Arr<double> sX = pre ([&] (std::int64_t c) { return flux[c]; });
        const Arr<double> sO = pre ([&] (std::int64_t c) { return onC[c]; });
        const Arr<double> sB = pre ([&] (std::int64_t c) { return bigC[c]; });
        const Arr<double> sS = pre ([&] (std::int64_t c) { return static_cast<double> (sil[c]); });
        auto win = [&] (std::int64_t c, std::int64_t width)
        {
            const auto K = static_cast<double> (width);
            auto m = [&] (const Arr<double>& a) { return (a[c + width] - a[c]) / K; };
            const double meanRel = m (sL), stdDev = std::sqrt (std::max (0.0, m (sL2) - meanRel * meanRel));
            return Win { meanRel, stdDev, m (sF), m (sX), (sO[c + width] - sO[c]) / (K * tc), sB[c + width] - sB[c], m (sS) };
        };

        std::vector<Part> parts;
        std::vector<Span> chosen;
        auto take = [&] (PartKind kind, Span r, std::string label, double bpm, bool tempoFound)
        {
            parts.push_back ({ kind, std::move (label), clamp (r.s / dur, 0, 1), clamp (r.e / dur, 0, 1), bpm, tempoFound });
            chosen.push_back (r);
        };
        // place a region of length len (s) so it starts at t0 if possible, inside the file
        auto place = [dur] (double t0, double len)
        {
            const double s = clamp (t0, 0, std::max (0.0, dur - len));
            return Span { s, std::min (dur, s + len) };
        };

        // refine an onset to ~1.5 ms: first 16-sample step whose energy reaches halfway (dB) from the floor before it
        detail::Decimator decimator (source, ft.D, ft.g);
        std::vector<float> ybuf;
        auto refineOnset = [&] (std::int64_t i)
        {
            const std::int64_t H = ft.H;
            const std::int64_t st = std::max<std::int64_t> (0, (i - 2) * H), en = std::min<std::int64_t> (ft.M - 16, (i + 1) * H);
            const double before = std::max (std::min (Lf[std::max<std::int64_t> (0, i - 3)], Lf[std::max<std::int64_t> (0, i - 2)]), floorDb);
            const double peak = std::max (Lf[i], Lf[std::min (nf - 1, i + 1)]);
            const double thr = before + 0.5 * (peak - before);
            if (st < en)
            {
                // y[st .. st + 16 steps): the steps start at st, st + 16, ... while below en
                const std::int64_t steps = (en - st + 15) / 16;
                ybuf.resize (static_cast<std::size_t> (steps * 16));
                decimator.read (st, static_cast<int> (steps * 16), ybuf.data());
                for (std::int64_t k = 0; k < steps; ++k)
                {
                    const float* yp = ybuf.data() + k * 16;
                    double e = 0;
                    for (int q = 0; q < 16; ++q)
                        e += static_cast<double> (yp[q]) * static_cast<double> (yp[q]);
                    if (10 * std::log10 (e / 16 + 1e-12) >= thr)
                        return static_cast<double> (st + k * 16) / ft.srd;
                }
            }
            return static_cast<double> (i) * tf;
        };

        // ---- loop: one bar at the tempo found from the onset autocorrelation (120 bpm if none is clear)
        const Tempo tempo = estimateTempo (on.v, tf);
        std::vector<Scored> loopScored;
        const double bar = 240 / tempo.bpm, B = bar / tf;
        if (stop && stop())
            return {};
        if (bar <= dur)
        {
            std::vector<double> rising;
            for (std::int64_t i = 0; i < nf; ++i)
                if (on[i] > 0)
                    rising.push_back (on[i]);
            const double oRef = std::max (1.0, pct (rising, 0.9));
            std::vector<std::int64_t> cands;
            for (std::int64_t i = 0; i + 1 < nf; ++i)
                if (on[i] >= 6 && (i == 0 || on[i] >= on[i - 1]) && on[i] > on[i + 1])
                    cands.push_back (i);
            std::sort (cands.begin(), cands.end(), [&on] (std::int64_t a, std::int64_t b) { return on[a] > on[b] || (on[a] == on[b] && a < b); });
            auto grid = [&] (double s, int j)
            {
                const std::int64_t c = whole (jsRound (s + static_cast<double> (j) * B / 8));
                double m = 0;
                for (std::int64_t q = -1; q <= 1; ++q)
                    if (c + q >= 0 && c + q < nf)
                        m = std::max (m, on[c + q]);
                return m;
            };
            const std::size_t tried = std::min<std::size_t> (400, cands.size());
            for (std::size_t k = 0; k < tried; ++k)
            {
                const std::int64_t s = cands[k];
                const auto sd = static_cast<double> (s);
                if (sd + B > static_cast<double> (nf))
                    continue;
                double al = 0, all = 0;
                std::array<double, 8> gA {};
                for (int j = 0; j < 8; ++j)
                {
                    const double v = grid (sd, j);
                    gA[static_cast<std::size_t> (j)] = v;
                    al += v;
                }
                for (std::int64_t i = s; static_cast<double> (i) < std::min (static_cast<double> (nf), jsRound (sd + B)); ++i)
                    all += on[i];
                double rep = 0;
                if (sd + B + 7 * B / 8 + 2 <= static_cast<double> (nf))
                {
                    // the next bar's 8 grid points fit (a file of exactly 2 bars, rotated, still compares)
                    double dsum = 0, ssum = 0;
                    for (int j = 0; j < 8; ++j)
                    {
                        const double v = grid (sd + B, j), a = gA[static_cast<std::size_t> (j)];
                        dsum += std::abs (v - a);
                        ssum += v + a;
                    }
                    rep = ssum > 0 ? 1 - dsum / ssum : 0;
                }
                const std::int64_t c0 = s / C, K2 = std::max<std::int64_t> (1, std::min<std::int64_t> (nc - c0, whole (jsRound (B / static_cast<double> (C)))));
                const double lvl = c0 < nc ? clamp ((win (c0, K2).meanRel + 30) / 24, 0, 1) : 0;
                const double score = std::min (1.5, al / (8 * oRef)) + 0.8 * rep + 0.3 * std::min (1.5, on[s] / oRef) + 0.3 * lvl
                                     - 0.3 * std::min (1.0, std::max (0.0, all - al) / (8 * oRef));
                loopScored.push_back ({ s, score });
            }
            std::sort (loopScored.begin(), loopScored.end(),
                       [] (const Scored& a, const Scored& b) { return a.score > b.score || (a.score == b.score && a.s < b.s); });
        }
        // the best loop's region: the hit avoids it when an almost as good hit does not (a two-bar beat would
        // otherwise lose its downbeat loop to whichever drum hit scored a point higher)
        std::optional<Span> loopBestR;
        if (! loopScored.empty())
            loopBestR = place (refineOnset (loopScored[0].s) - 0.005, bar);

        // ---- hit: the loudest transient. Peak loudness counts most; the rise counts up to 18 dB (beyond that a jump
        // from near silence is no bigger a hit).
        if (stop && stop())
            return {};
        std::vector<HitCand> hitCands;
        auto decays = [&] (std::int64_t at, double top)   // 5 dB down 0.3-0.8 s later
        {
            double m = -kInf;
            for (std::int64_t j = at + whole (jsRound (0.3 / tf)); j < std::min<std::int64_t> (nf, at + whole (jsRound (0.8 / tf))); ++j)
                m = std::max (m, Lf[j]);
            return m <= top - 5;
        };
        for (std::int64_t i = 0; i < nf; ++i)
        {
            if (rise[i] < 9)
                continue;
            bool isMax = true;
            for (std::int64_t q = -3; q <= 3 && isMax; ++q)
                if (q != 0 && i + q >= 0 && i + q < nf && rise[i + q] > rise[i])
                    isMax = false;
            if (! isMax)
                continue;
            double pk = -kInf;
            for (std::int64_t q = 0; q < 9 && i + q < nf; ++q)
                pk = std::max (pk, Lf[i + q]);
            // a rise in the first frames (the file starts on it) is a hit only when it is a transient that decays, so
            // a file that simply starts loud (a held tone) does not get a "Big hit 0:00"
            if (i < 3 && ! decays (i, pk))
                continue;
            hitCands.push_back ({ i, std::min (rise[i], 18.0) + 4 * (pk - Lref), pk });
        }
        std::sort (hitCands.begin(), hitCands.end(),
                   [] (const HitCand& a, const HitCand& b) { return a.score > b.score || (a.score == b.score && a.i < b.i); });
        const double hitTop = hitCands.empty() ? 0 : hitCands[0].score;
        std::vector<HitCand> hitOrder (hitCands.begin(), hitCands.begin() + static_cast<std::ptrdiff_t> (std::min<std::size_t> (200, hitCands.size())));
        // the part lasts until the sound falls 30 dB below the hit or the next strong onset (1 - 3 s)
        auto hitEnd = [&] (const HitCand& h)
        {
            std::int64_t j = h.i + whole (jsRound (0.3 / tf));
            for (; j < nf && static_cast<double> (j - h.i) * tf < 3; ++j)
                if (Lf[j] < h.pk - 30 || rise[j] >= 12)
                    break;
            return clamp (static_cast<double> (j - h.i) * tf + 0.04, 1, 3);
        };
        if (loopBestR)
        {
            // first the near-best (>= 85 % of the top score) hits that leave the best loop free, then the rest
            const std::vector<Span> loopOnly { *loopBestR };
            for (std::size_t k = 0; k < hitOrder.size(); ++k)
            {
                const HitCand& h = hitOrder[k];
                if (h.score >= 0.85 * hitTop && overlapOk (place (refineOnset (h.i) - 0.04, hitEnd (h)), loopOnly))
                {
                    const auto at = hitOrder.begin() + static_cast<std::ptrdiff_t> (k);
                    std::rotate (hitOrder.begin(), at, at + 1);   // the prototype's splice and unshift
                    break;
                }
            }
        }
        for (const HitCand& h : hitOrder)
        {
            const double t = refineOnset (h.i);
            const Span r = place (t - 0.04, hitEnd (h));
            if (r.e - r.s >= 1 && t >= r.s && overlapOk (r, chosen))
            {
                take (PartKind::hit, r, "Big hit " + fmtTime (t), 0, false);
                break;
            }
        }

        // ---- steady: a held, tonal, level stretch
        if (stop && stop())
            return {};
        const double Wsec = clamp (dur / 4, 1, 3);
        const std::int64_t K = std::max<std::int64_t> (2, std::min<std::int64_t> (nc, std::max (whole (jsRound (Wsec / tc)), whole (std::ceil (1 / tc - 1e-9)))));   // >= 1 s at every sample rate
        std::vector<Cand> steady, quiet;
        for (std::int64_t c = 0; c + K <= nc; ++c)
        {
            const Win w = win (c, K);
            const double tonal = 1 - std::min (1.0, w.flat / 0.3);
            const double sScore = 3 * tonal - 0.25 * w.stdDev - 0.5 * w.flux - 0.05 * w.onRate - 1.5 * w.big + 0.04 * w.meanRel
                                  - 0.2 * std::max (0.0, -18 - w.meanRel) - 5 * w.sil;
            if (w.flat < 0.25 && w.meanRel > -30 && w.sil < 0.05 && w.stdDev < 4 && w.big == 0 && sScore > 1)
                steady.push_back ({ c, sScore });
            const double q = clamp ((-w.meanRel - 6) / 24, 0, 1), texture = 0.5 * std::min (1.0, w.flat / 0.3) + 0.5 * std::min (1.0, w.flux / 2);
            const double qScore = 2 * q + texture - 1.0 * w.big - 0.1 * w.stdDev;
            if (w.meanRel < -9 && w.meanRel > -55 && w.sil < 0.2 && w.big == 0)
                quiet.push_back ({ c, qScore });
        }
        auto byScore = [] (const Cand& a, const Cand& b) { return a.score > b.score || (a.score == b.score && a.c < b.c); };
        std::sort (steady.begin(), steady.end(), byScore);
        for (const Cand& s : steady)
        {
            const Span r { static_cast<double> (s.c) * tc, std::min (dur, static_cast<double> (s.c + K) * tc) };
            if (overlapOk (r, chosen))
            {
                take (PartKind::steady, r, "Steady note " + fmtTime (r.s), 0, false);
                break;
            }
        }

        // ---- loop: one bar at the tempo found from the onset autocorrelation (120 bpm if none is clear)
        if (bar <= dur)
        {
            const std::string label = "One-bar loop " + core::jsNumberToString (jsRound (tempo.bpm * 10) / 10) + " bpm" + (tempo.found ? "" : " (guess)");
            bool done = false;
            for (const Scored& c : loopScored)
            {
                const double t = refineOnset (c.s) - 0.005;
                const Span r = place (t, bar);
                if (overlapOk (r, chosen))
                {
                    take (PartKind::loop, r, label + " " + fmtTime (r.s), tempo.bpm, tempo.found);
                    done = true;
                    break;
                }
            }
            if (! done)
            {
                // no usable onsets: a level, slowly changing bar
                const std::int64_t Kb = std::max<std::int64_t> (1, whole (jsRound (bar / tc)));
                std::vector<Cand> alt;
                for (std::int64_t c = 0; c + Kb <= nc; c += 2)
                {
                    const Win w = win (c, Kb);
                    alt.push_back ({ c, -w.flux - 0.2 * w.stdDev - 3 * w.sil + 0.05 * w.meanRel });
                }
                std::sort (alt.begin(), alt.end(), byScore);
                for (const Cand& a : alt)
                {
                    const Span r = place (static_cast<double> (a.c) * tc, bar);
                    if (overlapOk (r, chosen))
                    {
                        take (PartKind::loop, r, label + " " + fmtTime (r.s), tempo.bpm, tempo.found);
                        break;
                    }
                }
            }
        }

        // ---- quiet: a quiet, textured stretch (not silence)
        std::sort (quiet.begin(), quiet.end(), byScore);
        for (const Cand& q : quiet)
        {
            const Span r { static_cast<double> (q.c) * tc, std::min (dur, static_cast<double> (q.c + K) * tc) };
            if (overlapOk (r, chosen))
            {
                take (PartKind::quiet, r, "Quiet texture " + fmtTime (r.s), 0, false);
                break;
            }
        }

        std::sort (parts.begin(), parts.end(), [] (const Part& a, const Part& b) { return a.kind < b.kind; });
        return parts;
    }
}
