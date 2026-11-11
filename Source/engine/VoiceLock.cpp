// sl-engine.js Voice.lockGrab, lockRho and runLock (PLUGIN-PLAN 1.2, 1.8; M3 part L): Cloud with the Lock switch,
// GrainLock's trick.
//
// A head captures a stretch of the sample at its position (capA) and plays y = sum over k of w_k seg[(th + k)
// periods]: N taps one period P of the played note apart, each read at the same phase th of its own period. The
// sum repeats exactly every period, so any material (noise, a chord) plays in tune. The taps are Hann-weighted
// across the N-period capture (w = HANN at (th + k) / N), which removes the seam where th wraps from 1 to 0, and
// the sum is scaled by sqrt (2 / N). N is the Grain setting in periods (h.nT, at most 12) and at most half the
// head's grant from the planner, so a head costs N taps, or N + N' while it crossfades.
//
// The re-grab: a head grabs a new capture (capB) where it is now when Refresh asks (Voice::tick sets h.regrab at
// a refresh that is not skipped, or at a Scatter jump), or by itself every nA periods with Refresh Smooth. The two
// captures crossfade over one period with equal-power fades and GrainLock's correlation correction
// 1 / sqrt (1 + 2 rho gi go): rho is the two captures' correlation over one period (lockRho), so correlated
// material crossfades at constant amplitude and uncorrelated material at constant power. At the end capB becomes
// capA. Spray scatters each grab around the head, with the grain engines' hash (gIdx counts the grabs).
//
// Ported line by line: the same operation order and constants. The inline Hermite reads of the JS are
// PlainStretch::read (readReg's value bit for bit on the plain stretch, readAbsFastPaths checks it), the fallback
// readReg; the tap weights read hannTable () directly, as the JS reads HANN; lockRho calls hannTab, as the JS
// does. The source is the voice's own (src: 8.2 #23), the instrument the context's. Real time: no allocation;
// the captures are the Head's fields.

#include "engine/Voice.h"

#include "core/Seeds.h"
#include "engine/Tables.h"

#include <cmath>

namespace sl::engine
{
    double Voice::lockGrab (Head& h, double x) noexcept
    {
        double c = grabPos (reg, x);
        if (h.spray > 0)
            c += h.spray * (core::rand01 (core::hash32 (nh, h.i, h.gIdx, ctx->inst->seed, kSaltSpray)) * 2 - 1);
        h.gIdx++;
        return c;
    }

    double Voice::lockRho (const Head& h) const noexcept
    {
        const SampleData& S = *src.data;
        const RegionState& G = reg;
        const double PR = h.PR;
        double ab = 0, aa = 0, bb = 0;
        for (int q = 0; q < 16; ++q)
        {
            const double th = (q + 0.5) / 16;
            double a = 0, b = 0;
            for (int k = 0; k < h.nA; ++k)
            {
                const double p = th + k, w = h.nA > 1 ? hannTab (p / h.nA) : 1;
                double l = 0, r = 0;
                readReg (S, G, h.capA + p * PR, l, r);
                a += (l + r) * w;
            }
            for (int k = 0; k < h.nB; ++k)
            {
                const double p = th + k, w = h.nB > 1 ? hannTab (p / h.nB) : 1;
                double l = 0, r = 0;
                readReg (S, G, h.capB + p * PR, l, r);
                b += (l + r) * w;
            }
            a *= h.nA > 1 ? std::sqrt (2.0 / h.nA) : 1;
            b *= h.nB > 1 ? std::sqrt (2.0 / h.nB) : 1;
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        const double r = aa > 1e-20 && bb > 1e-20 ? ab / std::sqrt (aa * bb) : 0;
        return r > 0 ? (r < 1 ? r : 1) : 0;
    }

    void Voice::runLock (Head& h, int run) noexcept
    {
        const SampleData& S = *src.data;
        const RegionState& G = reg;
        double* bL = bufL;
        double* bR = bufR;
        const int p0 = ph;
        const double L = G.L;
        const bool stp = G.stop;
        const double sgStep = 1 / (kSlewSeconds * ctx->sr);
        const int halfGrant = h.grant >> 1;
        const int half = halfGrant > 1 ? halfGrant : 1;   // Math.max (1, h.grant >> 1)
        double raw = h.raw, off = h.off, sg = h.stopG;
        const double spd = h.spd, dOff = h.dOff, g0L = h.g0L, g0R = h.g0R, dL = h.dL, dR = h.dR, PR = h.PR, dth = h.dth;
        // inline reads (mono or stereo) on the plain stretch of any loop pass; the result equals readReg's exactly
        const PlainStretch plain (S, G);
        const float* HANN = hannTable();
        if (h.needPrime)
        {
            h.needPrime = false;
            h.capA = lockGrab (h, raw + off);
            h.nA = h.nT < half ? h.nT : half;
            h.xf = -1;
            h.th = 0;
            h.periods = 0;
            h.regrab = false;
        }
        for (int j = 0; j < run; ++j)
        {
            const double x = raw + off;
            if (stp)
            {
                sg += (x >= 0 && x < L) ? sgStep : -sgStep;
                sg = sg < 0 ? 0 : sg > 1 ? 1 : sg;
            }
            if (h.regrab && h.xf < 0 && h.active)
            {
                h.regrab = false;
                h.capB = lockGrab (h, x);
                const int nb = h.nT < half ? h.nT : half;
                h.nB = nb > 1 ? nb : 1;   // Math.max (1, Math.min (h.nT, half))
                h.xf = 0;
                h.rho = lockRho (h);
            }
            const double th = h.th;
            int N = h.nA;
            double aL = 0, aR = 0;
            for (int k = 0; k < N; ++k)
            {
                const double p = th + k;
                double w = 1;
                if (N > 1)
                {
                    const double fi = p / N * kHannN;
                    const int ii = static_cast<int> (fi);   // fi | 0: fi is in [0, HANN_N]
                    w = HANN[ii] + (fi - ii) * (static_cast<double> (HANN[ii + 1]) - HANN[ii]);
                }
                const double xr = h.capA + p * PR;
                double l = 0, r = 0;
                if (! plain.read (xr, l, r))
                    readReg (S, G, xr, l, r);
                aL += l * w;
                aR += r * w;
            }
            double gn = N > 1 ? std::sqrt (2.0 / N) : 1;
            aL *= gn;
            aR *= gn;
            int load = N;
            if (h.xf >= 0)
            {   // the re-grab: capA crossfades into capB over one period
                N = h.nB;
                double bl = 0, br = 0;
                for (int k = 0; k < N; ++k)
                {
                    const double p = th + k;
                    double w = 1;
                    if (N > 1)
                    {
                        const double fi = p / N * kHannN;
                        const int ii = static_cast<int> (fi);
                        w = HANN[ii] + (fi - ii) * (static_cast<double> (HANN[ii + 1]) - HANN[ii]);
                    }
                    const double xr = h.capB + p * PR;
                    double l = 0, r = 0;
                    if (! plain.read (xr, l, r))
                        readReg (S, G, xr, l, r);
                    bl += l * w;
                    br += r * w;
                }
                gn = N > 1 ? std::sqrt (2.0 / N) : 1;
                const double xw = h.xf, gi = fadeIn (xw), go = fadeIn (1 - xw);
                // flat-power correction from the two captures' correlation (GrainLock's seam rule): correlated
                // material crossfades at constant amplitude, uncorrelated at constant power (the JS `kc`; renamed so
                // it does not hide the member Voice::kc)
                const double kRho = 1 / std::sqrt (1 + 2 * h.rho * gi * go);
                aL = (aL * go + bl * gn * gi) * kRho;
                aR = (aR * go + br * gn * gi) * kRho;
                load += N;
                h.xf += dth;
                if (h.xf >= 1)
                {
                    h.capA = h.capB;
                    h.nA = h.nB;
                    h.xf = -1;
                    h.periods = 0;
                }
            }
            if (load > h.peak)
                h.peak = load;
            h.th = th + dth;
            if (h.th >= 1)
            {
                h.th -= 1;
                h.periods++;
                if (h.smooth && h.periods >= h.nA && h.xf < 0)
                    h.regrab = true;
            }
            const int kk = p0 + j + 1;
            bL[j] += aL * (g0L + dL * kk) * sg;
            bR[j] += aR * (g0R + dR * kk) * sg;
            raw += spd;
            off += dOff;
        }
        h.raw = raw;
        h.off = off;
        h.stopG = sg;
    }
}
