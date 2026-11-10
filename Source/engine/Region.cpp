#include "engine/Region.h"

#include "engine/Tables.h"

#include <cmath>

namespace sl::engine
{
    void readReg (const SampleData& S, const RegionState& G, double x, double& RL, double& RR) noexcept
    {
        if (G.loop)
        {
            const double L = G.L, X = G.X;
            if (x >= 0 && (G.post ? x < L : x < L - X))   // first pass, plain stretch
            {
                readAbs (S, G.rs + x, RL, RR);
                return;
            }
            double v = x - L * std::floor (x / L);
            if (v >= L)
                v -= L;
            if (X > 0)
            {
                if (G.post)
                {
                    if (v < X && (x >= L || x < 0))
                    {
                        const double w = v * G.invX, gi = fadeIn (w), go = fadeIn (1 - w), kc = 1 / std::sqrt (1 + 2 * G.rho * gi * go);
                        double ol = 0, orr = 0;
                        readAbs (S, G.rs + L + v, ol, orr);
                        readAbs (S, G.rs + v, RL, RR);
                        RL = (RL * gi + ol * go) * kc;
                        RR = (RR * gi + orr * go) * kc;
                        return;
                    }
                }
                else if (v >= L - X)
                {
                    const double w = (v - (L - X)) * G.invX, gi = fadeIn (w), go = fadeIn (1 - w), kc = 1 / std::sqrt (1 + 2 * G.rho * gi * go);
                    double il = 0, ir = 0;
                    readAbs (S, G.rs + v - L, il, ir);
                    readAbs (S, G.rs + v, RL, RR);
                    RL = (RL * go + il * gi) * kc;
                    RR = (RR * go + ir * gi) * kc;
                    return;
                }
            }
            readAbs (S, G.rs + v, RL, RR);
        }
        else
        {
            const double u = x < 0 ? 0 : x > G.L - 1 ? G.L - 1 : x;
            readAbs (S, G.rs + u, RL, RR);
        }
    }

    double seamRho (const SampleData& S, const RegionState& G) noexcept
    {
        const double X = std::floor (G.X);
        if (X < 2)
            return 0;
        const double st = std::floor (X / 2048) > 1 ? std::floor (X / 2048) : 1;   // Math.max(1, Math.floor(X / 2048))
        const double n1 = static_cast<double> (S.n - 1);
        double ab = 0, aa = 0, bb = 0;
        for (double t = 0; t < X; t += st)
        {
            double ia = G.post ? G.rs + t : G.rs - X + t, ib = G.post ? G.rs + G.L + t : G.rs + G.L - X + t;
            ia = ia < 0 ? 0 : ia > n1 ? n1 : ia;
            ib = ib < 0 ? 0 : ib > n1 ? n1 : ib;
            const auto a0 = static_cast<std::int64_t> (ia), b0 = static_cast<std::int64_t> (ib);
            const double a = static_cast<double> (S.L[a0]) + S.R[a0], b = static_cast<double> (S.L[b0]) + S.R[b0];
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        const double r = aa > 1e-20 && bb > 1e-20 ? ab / std::sqrt (aa * bb) : 0;
        return r > 0 ? (r < 1 ? r : 1) : 0;
    }
}
