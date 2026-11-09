#include "StarterSounds.h"

#include "core/JsCompat.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <utility>

// A port of prototype-v4/src/sl-sounds.js. Each function keeps the JavaScript's name and its operation
// order (a * b * c is (a * b) * c, as JavaScript evaluates it), so the doubles come out the same and the
// one rounding to float32 in finish gives the prototype's samples (PLUGIN-PLAN 1.6 rules 1-2).

namespace sl::sounds
{
    namespace
    {
        using core::jsRound;
        using Buffer = std::vector<double>;

        constexpr double SR = 44100.0;
        constexpr double PI = std::numbers::pi;   // Math.PI
        constexpr double TWO_PI = PI * 2.0;
        constexpr double SQRT2 = std::numbers::sqrt2;   // Math.SQRT2
        constexpr double PEAK = 0.8;
        constexpr double TOP_HZ = 19000.0;   // highest partial any additive sound may contain
        constexpr int CTL = 64;              // amplitude control step (samples) for additive sounds

        const double C3_HZ = 440.0 * std::pow (2.0, (60.0 - 69.0) / 12.0);   // 261.6256 Hz

        double rand01 (std::uint32_t h) noexcept { return static_cast<double> (h) / 4294967296.0; }

        /** The bits of the int32 that `x | 0` makes of a small whole number. */
        std::uint32_t bits (int x) noexcept { return static_cast<std::uint32_t> (x); }

        std::size_t sz (double x) noexcept { return static_cast<std::size_t> (x); }

        //------------------------------------------------------------------------------------------
        // Loop length for pitched sounds: the whole number of periods n of hz nearest `seconds` whose
        // length L (samples) holds n periods almost exactly.
        struct Loop
        {
            double n = 0.0, L = 0.0, f0 = 0.0;
        };

        Loop pitchedLoop (double hz, double seconds)
        {
            const double target = seconds * SR, n0 = jsRound (hz * seconds);
            bool found = false;
            double bestN = 0.0, bestL = 0.0, bestScore = 0.0;
            for (double n = n0 - 8.0; n <= n0 + 8.0; n += 1.0)
            {
                const double L = jsRound (n * SR / hz);
                if (std::fabs (L - target) > 0.02 * SR)
                    continue;
                const double err = std::fabs (L * hz / SR - n);
                const double score = err + std::fabs (L - target) / SR * 0.01;
                if (! found || score < bestScore)
                {
                    found = true;
                    bestN = n;
                    bestL = L;
                    bestScore = score;
                }
            }
            return { bestN, bestL, bestN * SR / bestL };
        }

        //------------------------------------------------------------------------------------------
        // Additive renderer. A partial has whole cycles over the loop, amplitude control values every CTL
        // samples, and start phases for the two channels: partial = amp * sin (phase).
        struct Partial
        {
            double cyc = 0.0;
            const Buffer* amp = nullptr;
            double phL = 0.0, phR = 0.0;
        };

        std::size_t ctlCount (std::size_t L) noexcept { return (L + CTL - 1) / CTL + 1; }

        // x position (0..1, wrapped) of control point c
        double ctlX (std::size_t c, std::size_t L) noexcept
        {
            const double x = static_cast<double> (c * CTL) / static_cast<double> (L);
            return x - std::floor (x);
        }

        void renderPartials (std::size_t L, const std::vector<Partial>& parts, Buffer& outL, Buffer& outR)
        {
            const std::size_t nCtl = (L + CTL - 1) / CTL;
            const double Ld = static_cast<double> (L);
            for (const Partial& P : parts)
            {
                const Buffer& A = *P.amp;
                const double w = TWO_PI * P.cyc / Ld, cw = std::cos (w), sw = std::sin (w);
                double re = std::cos (P.phL), im = std::sin (P.phL);
                const double dR = P.phR - P.phL, cd = std::cos (dR), sd = std::sin (dR);
                std::size_t n = 0;
                for (std::size_t c = 0; c < nCtl; ++c)
                {
                    const double m = 1.0 / std::sqrt (re * re + im * im);   // keep the rotor on the unit circle
                    re *= m;
                    im *= m;
                    const double a0 = A[c], da = (A[c + 1] - a0) / CTL;
                    const std::size_t end = std::min (L, n + CTL);
                    double a = a0;
                    for (; n < end; ++n)
                    {
                        outL[n] += a * im;
                        outR[n] += a * (im * cd + re * sd);
                        a += da;
                        const double t = re * cw - im * sw;
                        im = re * sw + im * cw;
                        re = t;
                    }
                }
            }
        }

        // Scale the amplitude tables so the summed power at each control point follows P^(1-gamma) of the
        // raw power (gamma 1 = constant loudness, 0 = untouched).
        void levelPower (std::vector<Buffer>& tables, double gamma)
        {
            if (gamma == 0.0)
                return;
            const std::size_t nC = tables[0].size();
            const double nCd = static_cast<double> (nC);
            double mean = 0.0;
            Buffer pw (nC);
            for (std::size_t c = 0; c < nC; ++c)
            {
                double s = 0.0;
                for (const Buffer& T : tables)
                    s += T[c] * T[c];
                pw[c] = s;
                mean += s / nCd;
            }
            for (std::size_t c = 0; c < nC; ++c)
            {
                const double g = std::pow (mean / std::max (pw[c], 1e-20), gamma / 2.0);
                for (Buffer& T : tables)
                    T[c] *= g;
            }
        }

        // Small seeded L/R phase spread per partial (a little width; pitch is untouched). Low partials stay centred.
        double spread (std::uint32_t tag, int k, double width) noexcept
        {
            return (rand01 (hash32 ({ tag, bits (k), 0x5157u })) - 0.5) * width * std::min (1.0, static_cast<double> (k) / 4.0);
        }

        struct HarmonicOptions
        {
            double width = 0.0;
            double gamma = 0.0;
            int phase = 0;   // 0: none, 1: HALF (k * PI), 2: PI / 2
        };

        double phaseOf (const HarmonicOptions& opts, int k) noexcept
        {
            switch (opts.phase)
            {
                case 1:  return static_cast<double> (k) * PI;   // HALF: half a period into the wave
                case 2:  return PI / 2.0;
                default: return 0.0;
            }
        }

        template <class AmpFn>
        std::pair<Buffer, Buffer> harmonicSound (std::uint32_t tag, AmpFn&& ampFn, const HarmonicOptions& opts)
        {
            const Loop loop = pitchedLoop (C3_HZ, 6.0);
            const std::size_t L = sz (loop.L);
            const int kMax = static_cast<int> (std::floor (TOP_HZ / loop.f0));
            const std::size_t nC = ctlCount (L);

            std::vector<Buffer> tables;
            tables.reserve (static_cast<std::size_t> (kMax));
            for (int k = 1; k <= kMax; ++k)
            {
                Buffer T (nC);
                const double hz = static_cast<double> (k) * loop.f0;
                for (std::size_t c = 0; c < nC; ++c)
                    T[c] = ampFn (k, ctlX (c, L), hz);
                tables.push_back (std::move (T));
            }
            levelPower (tables, opts.gamma);

            std::vector<Partial> parts;
            parts.reserve (static_cast<std::size_t> (kMax));
            for (int k = 1; k <= kMax; ++k)
            {
                const double base = phaseOf (opts, k), d = spread (tag, k, opts.width);
                parts.push_back ({ static_cast<double> (k) * loop.n, &tables[static_cast<std::size_t> (k - 1)], base + d / 2.0, base - d / 2.0 });
            }

            Buffer outL (L, 0.0), outR (L, 0.0);
            renderPartials (L, parts, outL, outR);
            return { std::move (outL), std::move (outR) };
        }

        //------------------------------------------------------------------------------------------
        // Shapes used by the sweeps.

        // Rises 0 -> 1 linearly over [0, 1-tail), then a cosine back to 0 over the tail, so the loop seam is seamless.
        double riseAndReturn (double x, double tail) noexcept
        {
            const double xo = 1.0 - tail;
            return x < xo ? x / xo : 0.5 + 0.5 * std::cos (PI * (x - xo) / tail);
        }

        // 1/|H|^2 of a 2-pole low-pass
        double lp2mag2 (double r, double q) noexcept
        {
            const double a = 1.0 - r * r;
            return a * a + (r / q) * (r / q);
        }

        //------------------------------------------------------------------------------------------
        // The sounds. Saw-like sounds start half a period into the wave (a pure time shift), so sample 0 sits
        // mid-ramp, not on the edge.

        // Plain band-limited saw: sum sin (k t) / k, static.
        std::pair<Buffer, Buffer> makeSaw (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("saw") });
            return harmonicSound (tag, [] (int k, double, double) { return 1.0 / k; }, { 0.9, 0.0, 1 });
        }

        // Saw whose 24 dB low-pass (slightly resonant) opens from 300 Hz to 14 kHz across 92% of the length,
        // then closes again over the last 0.48 s so the end joins the start.
        std::pair<Buffer, Buffer> makeSawOpen (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("sawopen") });
            const double F_LO = 300.0, OCT = std::log2 (14000.0 / F_LO);
            return harmonicSound (tag, [F_LO, OCT] (int k, double x, double hz)
            {
                const double fc = F_LO * std::pow (2.0, OCT * riseAndReturn (x, 0.08)), r = hz / fc;
                return (1.0 / k) / std::sqrt (lp2mag2 (r, 0.54) * lp2mag2 (r, 1.6));
            }, { 0.9, 0.35, 1 });
        }

        // Band-limited pulse whose width sweeps 50% -> 10% -> 50% across the length (a PWM sweep), loudness half-kept.
        std::pair<Buffer, Buffer> makeSquare (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("square") });
            return harmonicSound (tag, [] (int k, double x, double)
            {
                const double d = 0.5 - 0.4 * (0.5 - 0.5 * std::cos (TWO_PI * x));
                const double g = std::pow (0.25 / (d * (1.0 - d)), 0.25);
                return g * (2.0 / (PI * k)) * std::sin (PI * k * d);
            }, { 0.9, 0.0, 2 });
        }

        // Saw through a 4-formant cascade (vocal tract) gliding from "aah" to "ooh" over 88% of the length and
        // back over the last 0.72 s. Loudness kept constant.
        constexpr double VOWEL_A_F[4] = { 700.0, 1150.0, 2650.0, 3350.0 };
        constexpr double VOWEL_A_B[4] = { 90.0, 110.0, 160.0, 250.0 };
        constexpr double VOWEL_U_F[4] = { 320.0, 680.0, 2400.0, 3200.0 };
        constexpr double VOWEL_U_B[4] = { 70.0, 80.0, 140.0, 240.0 };

        std::pair<Buffer, Buffer> makeVowel (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("vowel") });
            return harmonicSound (tag, [] (int k, double x, double hz)
            {
                const double m = riseAndReturn (x, 0.12);
                double g = 1.0 / k;
                for (int i = 0; i < 4; ++i)
                {
                    const double F = VOWEL_A_F[i] * std::pow (VOWEL_U_F[i] / VOWEL_A_F[i], m);
                    const double B = VOWEL_A_B[i] * std::pow (VOWEL_U_B[i] / VOWEL_A_B[i], m);
                    const double a = F * F - hz * hz;
                    g *= (F * F) / std::sqrt (a * a + (hz * B) * (hz * B));
                }
                return g;
            }, { 0.9, 1.0, 1 });
        }

        // v3's makeDefault (18 partials 1/k^0.9, each with its own slow swell, plus a copy detuned x1.0035 at
        // 0.55), with seeded phases, 6 s long, and every rate rounded to whole cycles over the loop.
        std::pair<Buffer, Buffer> makePad (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("pad") });
            const Loop loop = pitchedLoop (C3_HZ, 6.0);
            const std::size_t L = sz (loop.L);
            const double n = loop.n, secs = loop.L / SR;
            const std::size_t nC = ctlCount (L);

            // Two tables per partial (T and its 0.55 copy T2), sized first so the pointers into it stay valid.
            std::vector<Buffer> tables (36, Buffer (nC, 0.0));
            std::vector<Partial> parts;
            parts.reserve (36);
            for (int k = 1; k <= 18; ++k)
            {
                const double kd = static_cast<double> (k);
                const double rateCyc = std::max (1.0, jsRound ((0.13 + kd * 0.037) * secs));
                Buffer& T = tables[static_cast<std::size_t> (2 * (k - 1))];
                Buffer& T2 = tables[static_cast<std::size_t> (2 * (k - 1) + 1)];
                for (std::size_t c = 0; c < nC; ++c)
                {
                    const double x = ctlX (c, L);
                    T[c] = (1.0 / std::pow (kd, 0.9)) * (0.5 + 0.5 * std::sin (TWO_PI * rateCyc * x + kd * 1.7));
                }
                for (std::size_t c = 0; c < nC; ++c)
                    T2[c] = T[c] * 0.55;

                const double ph = rand01 (hash32 ({ tag, bits (k), 1u })) * TWO_PI, ph2 = rand01 (hash32 ({ tag, bits (k), 2u })) * TWO_PI;
                const double d = spread (tag, k, 0.6), d2 = (rand01 (hash32 ({ tag, bits (k), 3u })) - 0.5) * 1.6;
                parts.push_back ({ kd * n, &T, ph + d / 2.0, ph - d / 2.0 });
                parts.push_back ({ kd * n + std::max (1.0, jsRound (kd * n * 0.0035)), &T2, ph2 + d2 / 2.0, ph2 - d2 / 2.0 });
            }

            Buffer outL (L, 0.0), outR (L, 0.0);
            renderPartials (L, parts, outL, outR);
            return { std::move (outL), std::move (outR) };
        }

        // Add a decaying partial circularly into out (index wraps at L).
        void addDecaying (Buffer& outL, Buffer& outR, std::size_t start, std::size_t len, double hz, double amp, double tau,
                          double ph, double gL, double gR, std::size_t attack)
        {
            const std::size_t L = outL.size();
            const double w = TWO_PI * hz / SR, rho = std::exp (-1.0 / (tau * SR));
            const double cw = std::cos (w) * rho, sw = std::sin (w) * rho;
            double re = std::cos (ph) * amp, im = std::sin (ph) * amp;
            for (std::size_t i = 0; i < len; ++i)
            {
                double v = im;
                if (i < attack)
                    v *= 0.5 - 0.5 * std::cos (PI * static_cast<double> (i) / static_cast<double> (attack));
                std::size_t j = start + i;
                if (j >= L)
                    j -= L;
                outL[j] += v * gL;
                outR[j] += v * gR;
                const double t = re * cw - im * sw;
                im = re * sw + im * cw;
                re = t;
            }
        }

        // Plucked arpeggio, 120 bpm eighth notes: C - Am - F G, around C3. Notes get longer and brighter across the loop.
        constexpr int PLUCK_NOTES[24] = { 60, 64, 67, 72, 67, 64, 60, 55, 57, 60, 64, 69, 64, 60, 57, 52, 53, 57, 60, 65, 55, 59, 62, 67 };

        std::pair<Buffer, Buffer> makePluck (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("pluck") });
            const double L = 6.0 * SR, step = L / 24.0;
            Buffer outL (sz (L), 0.0), outR (sz (L), 0.0);
            for (int i = 0; i < 24; ++i)
            {
                const double p = PLUCK_NOTES[i], id = static_cast<double> (i);
                const double f = 440.0 * std::pow (2.0, (p - 69.0) / 12.0), prog = id / 23.0;
                const double vel = i % 4 == 0 ? 1.0 : 0.72;
                const double tau1 = 0.28 + 0.3 * prog, tilt = 1.5 - 0.6 * prog;
                const double pan = (rand01 (hash32 ({ tag, bits (i), 11u })) - 0.5) * 0.6, ang = (pan + 1.0) * PI / 4.0;
                const double gL = std::cos (ang) * SQRT2, gR = std::sin (ang) * SQRT2;
                const std::size_t start = sz (jsRound (id * step));
                for (int k = 1; k <= 32; ++k)
                {
                    const double kd = static_cast<double> (k);
                    const double hz = kd * f * std::sqrt (1.0 + 0.00006 * kd * kd);
                    if (hz > 18000.0)
                        break;
                    const double amp = vel * std::pow (kd, -tilt) * (0.4 + 0.6 * std::fabs (std::sin (PI * kd * 0.19)));
                    const double tau = tau1 / (1.0 + 0.35 * (kd - 1.0));
                    const double len = std::min (L, std::ceil (tau * 11.5 * SR));
                    addDecaying (outL, outR, start, sz (len), hz, amp, tau, rand01 (hash32 ({ tag, bits (i), bits (k) })) * TWO_PI, gL, gR,
                                 sz (jsRound (0.003 * SR)));
                }
            }
            return { std::move (outL), std::move (outR) };
        }

        // Biquad high-pass (RBJ) run over an array in place.
        void highpass (Buffer& x, double fc, double q)
        {
            const double w = TWO_PI * fc / SR, al = std::sin (w) / (2.0 * q), cs = std::cos (w), a0 = 1.0 + al;
            const double b0 = (1.0 + cs) / 2.0 / a0, b1 = -(1.0 + cs) / a0, b2 = b0, a1 = -2.0 * cs / a0, a2 = (1.0 - al) / a0;
            double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
            for (double& sample : x)
            {
                const double v = sample, y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1;
                x1 = v;
                y2 = y1;
                y1 = y;
                sample = y;
            }
        }

        void addCirc (Buffer& out, std::size_t start, const Buffer& seg, double g)
        {
            const std::size_t L = out.size();
            for (std::size_t i = 0; i < seg.size(); ++i)
            {
                std::size_t j = start + i;
                while (j >= L)
                    j -= L;
                out[j] += seg[i] * g;
            }
        }

        // fade the last `ms` to zero so a truncated tail never steps
        void taper (Buffer& seg, double ms)
        {
            const std::size_t m = std::min (seg.size(), sz (jsRound (ms * 0.001 * SR)));
            for (std::size_t i = 0; i < m; ++i)
                seg[seg.size() - 1 - i] *= 0.5 - 0.5 * std::cos (PI * static_cast<double> (i) / static_cast<double> (m));
        }

        Buffer kick (std::uint32_t tag, int idx, double level)
        {
            const std::size_t len = sz (jsRound (1.6 * SR)), atk = sz (jsRound (0.001 * SR));
            Buffer seg (len, 0.0);
            const std::uint32_t nb = hash32 ({ tag, bits (idx), 21u });
            double lp = 0.0;
            for (std::size_t i = 0; i < len; ++i)
            {
                const double t = static_cast<double> (i) / SR;
                const double ph = TWO_PI * (47.0 * t + 125.0 * 0.035 * (1.0 - std::exp (-t / 0.035)));
                double v = std::sin (ph) * std::exp (-t / 0.3);
                if (i < atk)
                    v *= static_cast<double> (i) / static_cast<double> (atk);
                lp += 0.35 * (noiseAt (nb, static_cast<std::uint32_t> (i)) - lp);
                if (t < 0.006)
                    v += 0.25 * lp * (1.0 - t / 0.006);
                seg[i] = v * level;
            }
            taper (seg, 60.0);
            return seg;
        }

        // bodyLevel 1 = the full snare (tone + noise); 0 = only a second, independent noise layer (used for width).
        Buffer snare (std::uint32_t tag, int idx, double level, int side, double bodyLevel)
        {
            const std::size_t len = sz (jsRound (0.7 * SR));
            Buffer body (len, 0.0), nz (len, 0.0);
            const std::uint32_t nb = hash32 ({ tag, bits (idx), bits (31 + side) });
            const std::size_t atk = sz (jsRound (0.0015 * SR));
            for (std::size_t i = 0; i < len; ++i)
            {
                const double t = static_cast<double> (i) / SR;
                body[i] = std::sin (TWO_PI * 182.0 * t) * std::exp (-t / 0.06) + 0.5 * std::sin (TWO_PI * 331.0 * t) * std::exp (-t / 0.04);
                nz[i] = noiseAt (nb, static_cast<std::uint32_t> (i));
            }
            highpass (nz, 1600.0, 0.7);
            Buffer seg (len, 0.0);
            for (std::size_t i = 0; i < len; ++i)
            {
                const double t = static_cast<double> (i) / SR;
                double v = 0.55 * bodyLevel * body[i] + 0.9 * nz[i] * std::exp (-t / 0.12);
                if (i < atk)
                    v *= static_cast<double> (i) / static_cast<double> (atk);
                seg[i] = v * level;
            }
            taper (seg, 40.0);
            return seg;
        }

        Buffer hat (std::uint32_t tag, int idx, double level, bool open, int side)
        {
            const std::size_t len = sz (jsRound ((open ? 0.9 : 0.18) * SR));
            Buffer seg (len, 0.0);
            const std::uint32_t nb = hash32 ({ tag, bits (idx), bits (41 + side) });
            const double tau = open ? 0.17 : 0.028;
            const std::size_t atk = sz (jsRound (0.0008 * SR));
            for (std::size_t i = 0; i < len; ++i)
                seg[i] = noiseAt (nb, static_cast<std::uint32_t> (i));
            highpass (seg, 7200.0, 0.8);
            highpass (seg, 5200.0, 0.6);
            for (std::size_t i = 0; i < len; ++i)
            {
                double v = seg[i] * std::exp (-static_cast<double> (i) / SR / tau);
                if (i < atk)
                    v *= static_cast<double> (i) / static_cast<double> (atk);
                seg[i] = v * level;
            }
            taper (seg, open ? 50.0 : 15.0);
            return seg;
        }

        // Two bars at 120 bpm on a 16th grid; bar 2 varies (extra kicks, a ghost snare, an open hat).
        struct BeatBar
        {
            std::vector<int> k, s, h, o;
        };

        std::pair<Buffer, Buffer> makeBeat (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("beat") });
            const double L = 4.0 * SR, s16 = SR * 0.5 / 4.0;   // 120 bpm: a beat is 0.5 s, a 16th is 5512.5 samples
            Buffer outL (sz (L), 0.0), outR (sz (L), 0.0);
            const BeatBar bars[2] = {
                { { 0, 8, 10 }, { 4, 12 }, { 0, 2, 4, 6, 8, 10, 12, 14 }, {} },
                { { 0, 7, 8, 11 }, { 4, 12, 15 }, { 0, 2, 4, 6, 8, 10, 12 }, { 14 } },
            };
            int idx = 0;
            for (int b = 0; b < 2; ++b)
            {
                const BeatBar& bar = bars[b];
                auto at = [b, s16] (int st) { return sz (jsRound (static_cast<double> (b * 16 + st) * s16)); };
                for (const int st : bar.k)
                {
                    const Buffer seg = kick (tag, idx++, 1.0);
                    addCirc (outL, at (st), seg, 1.0);
                    addCirc (outR, at (st), seg, 1.0);
                }
                for (const int st : bar.s)
                {
                    const double lv = st == 15 ? 0.35 : 0.8;
                    const int i0 = idx++;
                    const Buffer a = snare (tag, i0, lv, 0, 1.0), c = snare (tag, i0, lv, 1, 0.0);
                    addCirc (outL, at (st), a, 0.85);
                    addCirc (outL, at (st), c, 0.15);
                    addCirc (outR, at (st), a, 0.85);
                    addCirc (outR, at (st), c, -0.15);
                }
                std::vector<int> hats = bar.h;
                hats.insert (hats.end(), bar.o.begin(), bar.o.end());
                for (const int st : hats)
                {
                    const bool open = std::find (bar.o.begin(), bar.o.end(), st) != bar.o.end();
                    const double lv = open ? 0.33 : (st % 4 == 2 ? 0.34 : 0.22);
                    const int i0 = idx++;
                    const double pan = st % 4 == 2 ? 0.3 : 0.12;
                    const Buffer a = hat (tag, i0, lv, open, 0);
                    addCirc (outL, at (st), a, std::cos ((pan + 1.0) * PI / 4.0) * SQRT2);
                    addCirc (outR, at (st), a, std::sin ((pan + 1.0) * PI / 4.0) * SQRT2);
                }
            }
            return { std::move (outL), std::move (outR) };
        }

        // Seeded white noise through a moving state-variable filter (band-pass + some low-pass). The cutoff
        // sweeps between ~200 Hz and ~7 kHz and the resonance breathes, all periodic over the loop. The filter
        // runs twice round the loop and keeps the second pass, so its state at the end matches the start.
        std::pair<Buffer, Buffer> makeNoise (std::int32_t seed)
        {
            const std::uint32_t tag = hash32 ({ static_cast<std::uint32_t> (seed), strHash ("noise") });
            const std::size_t L = sz (6.0 * SR);
            const double Ld = static_cast<double> (L);
            Buffer mid (L, 0.0), side (L, 0.0);
            for (int pass0 = 0; pass0 < 2; ++pass0)
            {
                Buffer& out = pass0 == 0 ? mid : side;
                const int stream = pass0 + 1;
                const std::uint32_t nb = hash32 ({ tag, bits (stream), 51u });
                double ic1 = 0.0, ic2 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0, kq = 1.0, gbp = 1.0;
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (std::size_t i = 0; i < L; ++i)
                    {
                        if (i % 32 == 0)
                        {
                            const double x = static_cast<double> (i) / Ld;
                            const double fc = 250.0 * std::pow (2.0, 4.3 * (0.5 - 0.5 * std::cos (TWO_PI * x)) + 0.6 * std::sin (TWO_PI * 3.0 * x + 0.7));
                            const double q = 1.4 + 1.6 * (0.5 - 0.5 * std::cos (TWO_PI * 2.0 * x + 1.1));
                            const double g = std::tan (PI * std::min (fc, 0.45 * SR) / SR);
                            kq = 1.0 / q;
                            a1 = 1.0 / (1.0 + g * (g + kq));
                            a2 = g * a1;
                            a3 = g * a2;
                            gbp = kq * std::pow (900.0 / fc, 0.3);   // narrow low bands carry less power: lift them part-way
                        }
                        const double v0 = noiseAt (nb, static_cast<std::uint32_t> (i));
                        const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                        ic1 = 2.0 * v1 - ic1;
                        ic2 = 2.0 * v2 - ic2;
                        if (pass == 1)
                            out[i] = 0.8 * gbp * v1 + 0.3 * v2;
                    }
                }
            }
            Buffer outL (L, 0.0), outR (L, 0.0);
            for (std::size_t i = 0; i < L; ++i)
            {
                const double x = static_cast<double> (i) / Ld, a = 0.75 + 0.25 * std::sin (TWO_PI * 2.0 * x);
                outL[i] = a * (mid[i] + 0.4 * side[i]);
                outR[i] = a * (mid[i] - 0.4 * side[i]);
            }
            return { std::move (outL), std::move (outR) };
        }

        std::pair<Buffer, Buffer> makeChannels (StarterId id, std::int32_t seed)
        {
            switch (id)
            {
                case StarterId::sawopen: return makeSawOpen (seed);
                case StarterId::saw:     return makeSaw (seed);
                case StarterId::square:  return makeSquare (seed);
                case StarterId::pad:     return makePad (seed);
                case StarterId::vowel:   return makeVowel (seed);
                case StarterId::pluck:   return makePluck (seed);
                case StarterId::beat:    return makeBeat (seed);
                case StarterId::noise:   return makeNoise (seed);
            }
            return makeSawOpen (seed);
        }

        // finish: scale both channels together to peak 0.8, then the one rounding to float32.
        void finish (const Buffer& L, const Buffer& R, StarterSound& out)
        {
            double m = 0.0;
            for (const Buffer* ch : { &L, &R })
                for (const double x : *ch)
                {
                    const double a = std::fabs (x);
                    if (a > m)
                        m = a;
                }
            const double g = m > 0.0 ? PEAK / m : 0.0;
            out.left.resize (L.size());
            out.right.resize (R.size());
            for (std::size_t i = 0; i < L.size(); ++i)
                out.left[i] = static_cast<float> (L[i] * g);
            for (std::size_t i = 0; i < R.size(); ++i)
                out.right[i] = static_cast<float> (R[i] * g);
        }
    }

    //==============================================================================================
    std::uint32_t fmix32 (std::uint32_t h) noexcept
    {
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
    }

    std::uint32_t hash32 (std::initializer_list<std::uint32_t> args) noexcept
    {
        // h = fmix32 (((h ^ (arg | 0)) + Math.imul (i + 1, 0x632be5ab)) | 0): all of it is arithmetic mod 2^32.
        std::uint32_t h = 0x9e3779b9u;
        std::uint32_t i = 0;
        for (const std::uint32_t a : args)
        {
            h = fmix32 ((h ^ a) + (i + 1u) * 0x632be5abu);
            ++i;
        }
        return h;
    }

    std::uint32_t strHash (std::string_view s) noexcept
    {
        std::uint32_t h = 0x811c9dc5u;
        for (const char c : s)
        {
            h ^= static_cast<std::uint32_t> (static_cast<unsigned char> (c));
            h *= 0x01000193u;
        }
        return h;
    }

    double noiseAt (std::uint32_t base, std::uint32_t i) noexcept
    {
        return rand01 (fmix32 (base + i * 0x9e3779b1u)) * 2.0 - 1.0;
    }

    const StarterInfo& starterInfo (StarterId id) noexcept
    {
        const auto i = static_cast<std::size_t> (id);
        return kStarterList[i < static_cast<std::size_t> (kNumStarters) ? i : 0];
    }

    std::optional<StarterId> parseStarter (std::string_view code) noexcept
    {
        for (const auto& s : kStarterList)
            if (code == s.code)
                return s.id;
        return std::nullopt;
    }

    StarterSound makeStarterSound (StarterId id, std::int32_t seed)
    {
        StarterSound out;
        out.id = static_cast<std::size_t> (id) < static_cast<std::size_t> (kNumStarters) ? id : kDefaultStarter;
        out.seed = seed;
        const auto channels = makeChannels (out.id, seed);
        finish (channels.first, channels.second, out);
        return out;
    }

    std::shared_ptr<const StarterSound> sharedStarterSound (StarterId id, std::int32_t seed)
    {
        static std::mutex lock;
        static std::map<std::pair<int, std::int32_t>, std::weak_ptr<const StarterSound>> made;

        const std::pair<int, std::int32_t> key { static_cast<int> (id), seed };
        {
            const std::lock_guard<std::mutex> hold (lock);
            if (auto it = made.find (key); it != made.end())
                if (auto alive = it->second.lock())
                    return alive;
        }

        // Made outside the lock: two threads asking at once each make it, and the first one is kept.
        auto sound = std::make_shared<const StarterSound> (makeStarterSound (id, seed));
        const std::lock_guard<std::mutex> hold (lock);
        auto& slot = made[key];
        if (auto alive = slot.lock())
            return alive;
        slot = sound;
        return sound;
    }
}
