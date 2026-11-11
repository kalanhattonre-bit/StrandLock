// sl-engine.js Voice.runGrains (PLUGIN-PLAN 1.2, 1.8; M3 part G): the Voice and Cloud grain engines.
//
// Voice is v3's root-grid PSOLA: grains one pitch period apart (h.P), each a Hann window of h.len samples that
// reads the sample at h.step frames per sample. Every grain starts on a whole number of root cycles (Ts) from
// the Region start of its loop pass, so the grains stay in phase and the spacing sets the pitch; the read speed
// h.step is the formant ratio, so the formant moves on its own. Cloud is free grains: a size in ms, three
// overlapping (hop = len / 3), read at the pitch ratio, with Spray scattering each grain's start.
//
// Both: at most `grant` grains per head (the planner's share of the budget), capped at 8 for Voice and 5 for
// Cloud (plan 1.8). When a head is full the oldest grain gives way to the new one: a grain is never dropped,
// because the spacing is the pitch, so above the cap grains get shorter instead. Each grain backwards
// (Voice::rev) plays every grain from its end. Refresh: a smooth head aims each grain where the head will be
// half a grain later; a refreshing head reads from its last grab (h.grab, set by Voice::tick).
//
// Ported line by line: the same operation order and constants. The inline Hermite reads of the JS are
// PlainStretch::read (readReg's value bit for bit on the plain stretch, readAbsFastPaths checks it), the
// fallback readReg; the Hann lookups read hannTable () directly, as the JS reads HANN. Math.round is
// core::jsRound (plan 1.6 rule 5). The source is the voice's own (src: 8.2 #23), the instrument the context's.
// Real time: no allocation; the grain slots are the Head's fixed arrays.

#include "engine/Voice.h"

#include "core/JsCompat.h"
#include "core/Seeds.h"
#include "engine/EngineMath.h"
#include "engine/Tables.h"

#include <cmath>

namespace sl::engine
{
    void Voice::runGrains (Head& h, int run, bool voice) noexcept
    {
        const SampleData& S = *src.data;
        const core::Inst& I = *ctx->inst;
        const RegionState& G = reg;
        double* bL = bufL;
        double* bR = bufR;
        const int p0 = ph;
        const double L = G.L;
        const bool stp = G.stop;
        const double sgStep = 1 / (kSlewSeconds * ctx->sr);
        const double Ts = src.sr / (440 * std::pow (2.0, (I.root + I.cents / 100 - 69) / 12));
        const bool rv = rev;
        const std::int32_t seed = I.seed;
        double raw = h.raw, off = h.off, sg = h.stopG;
        const double spd = h.spd, dOff = h.dOff, g0L = h.g0L, g0R = h.g0R, dL = h.dL, dR = h.dR;
        double* gT = h.gT.data();
        double* gX = h.gX.data();
        double* gS = h.gS.data();
        double* gN = h.gN.data();
        double* gA = h.gA.data();
        // cap = Math.max (1, Math.min (voice ? VOICE_GRAINS : CLOUD_GRAINS, h.grant))
        const int most = voice ? kVoiceGrains : kCloudGrains;
        const int granted = most < h.grant ? most : h.grant;
        const int cap = granted > 1 ? granted : 1;
        // inline reads (mono or stereo) on the plain stretch of any loop pass; the result equals readReg's exactly
        const PlainStretch plain (S, G);
        const float* HANN = hannTable();
        const double hop = voice ? h.P : h.hop;
        const std::int64_t t0 = vt;
        if (h.needPrime)
        {   // start as if the head had been running: the grains whose windows cover now are scheduled too
            h.needPrime = false;
            h.gc = 0;
            h.nextT = static_cast<double> (t0) - hop * std::floor ((h.len - 1e-6) / hop);
        }
        for (int j = 0; j < run; ++j)
        {
            const double t = static_cast<double> (t0 + j);
            const double x = raw + off;
            if (stp)
            {
                sg += (x >= 0 && x < L) ? sgStep : -sgStep;
                sg = sg < 0 ? 0 : sg > 1 ? 1 : sg;
            }
            if (h.active)
            {
                int guard = 0;
                while (h.nextT <= t && guard++ < 16)
                {
                    // a grain's middle plays half a grain after it starts: aim it where the head will be then (no lag)
                    double anchor = h.smooth ? grabPos (G, x + (spd + dOff) * h.len * 0.5) : h.grab;
                    double step = h.step;
                    if (voice)
                    {   // whole root cycles from this pass's Region start
                        const double b0 = G.loop ? G.L * std::floor (anchor / G.L) : 0;
                        anchor = b0 + core::jsRound ((anchor - b0) / Ts) * Ts;
                    }
                    else if (h.spray > 0)
                    {
                        anchor += h.spray * (core::rand01 (core::hash32 (nh, h.i, h.gIdx, seed, kSaltSpray)) * 2 - 1);
                    }
                    const double span = h.len * step;
                    double x0 = anchor - span / 2;
                    // first pass of a loop: a grain never reads before the Region start (that would be the Region's
                    // end), so a note that starts at the start plays its true attack. Voice shifts by whole cycles
                    // (stays in phase).
                    if (G.loop && x0 < 0 && x >= 0 && x < L)
                        x0 += voice ? std::ceil (-x0 / Ts) * Ts : -x0;
                    if (rv)
                    {
                        x0 += span;
                        step = -step;
                    }
                    int g = h.gc;
                    if (g >= cap)
                    {   // full: replace the oldest grain (never drop the new one: spacing is the pitch)
                        g = 0;
                        for (int q = 1; q < h.gc; ++q)
                            if (gT[q] < gT[g])
                                g = q;
                    }
                    else
                    {
                        h.gc++;
                    }
                    gT[g] = h.nextT;
                    gX[g] = x0;
                    gS[g] = step;
                    gN[g] = h.len;
                    gA[g] = h.norm;
                    h.gIdx++;
                    h.nextT += hop;
                }
            }
            else if (h.nextT <= t)
            {
                h.nextT = t + hop;
            }
            double sL = 0, sR = 0;
            for (int g = 0; g < h.gc; ++g)
            {
                const double a = t - gT[g], ln = gN[g];
                if (a >= ln)
                {   // this grain has ended: the last one takes its slot
                    const int e = --h.gc;
                    gT[g] = gT[e];
                    gX[g] = gX[e];
                    gS[g] = gS[e];
                    gN[g] = gN[e];
                    gA[g] = gA[e];
                    --g;
                    continue;
                }
                if (a <= 0)
                    continue;
                const double fi = a / ln * kHannN;
                const int ii = static_cast<int> (fi);   // fi | 0: fi is in (0, HANN_N)
                const double w = (HANN[ii] + (fi - ii) * (static_cast<double> (HANN[ii + 1]) - HANN[ii])) * gA[g];
                const double xr = gX[g] + a * gS[g];
                double l = 0, r = 0;
                if (! plain.read (xr, l, r))   // fast path: plain stretch of this pass (same maths as readAbs)
                    readReg (S, G, xr, l, r);
                sL += l * w;
                sR += r * w;
            }
            if (h.gc > h.peak)
                h.peak = h.gc;
            const int kk = p0 + j + 1;
            bL[j] += sL * (g0L + dL * kk) * sg;
            bR[j] += sR * (g0R + dR * kk) * sg;
            raw += spd;
            off += dOff;
        }
        h.raw = raw;
        h.off = off;
        h.stopG = sg;
    }
}
