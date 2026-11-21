// sl-engine.js Voice.render, renderRun and runTape (PLUGIN-PLAN 1.2). The output buffers are Float32, as the
// worklet's: every out[j] += x reads a float, adds in double and stores a float (plan 1.6 rule 10). A voice's own
// buffers (bufL, bufR) are double, as the JS Float64Arrays.

#include "engine/Voice.h"

#include "engine/EngineMath.h"
#include "engine/Tables.h"

namespace sl::engine
{
    void Voice::render (float* outL, float* outR, int o, int n, std::int64_t k0) noexcept
    {
        if (chasing)
            return;   // a bounded chase is still replaying this voice's ticks: it is silent until chaseEnd
        int j = 0;
        while (j < n && ! done)
        {
            if (ph == 0)
            {
                if (pre)
                    pre = false;
                else
                    tick (k0 + j, false);
            }
            const int run = n - j < kCtrl - ph ? n - j : kCtrl - ph;
            renderRun (outL, outR, o + j, run);
            ph += run;
            if (ph >= kCtrl)
                ph = 0;
            vt += run;
            j += run;
        }
    }

    void Voice::renderRun (float* outL, float* outR, int o, int run) noexcept
    {
        double* bL = bufL;
        double* bR = bufR;
        for (int j = 0; j < run; ++j)
        {
            bL[j] = 0;
            bR[j] = 0;
        }
        for (Head& h : heads)
        {
            h.peak = 0;
            if (! h.on)
                continue;
            h.rendered = true;
            // The engines a head can run (kc is playedEngine (), Voice.h: since M3 part L the engine the note asks for).
            if (kc == Kc::tape)
                runTape (h, run);
            else if (kc == Kc::voice)
                runGrains (h, run, true);
            else if (kc == Kc::cloud)
                runGrains (h, run, false);
            else
                runLock (h, run);
            if (h.peak > h.segPeak)
                h.segPeak = h.peak;
        }
        std::int64_t resets = 0;
        resets += hL.run (bL, run) ? 1 : 0;
        resets += hR.run (bR, run) ? 1 : 0;
        resets += fL.run (bL, run) ? 1 : 0;
        resets += fR.run (bR, run) ? 1 : 0;
        resets += lL.run (bL, run) ? 1 : 0;
        resets += lR.run (bR, run) ? 1 : 0;
        if (resets != 0)
            ctx->filterResets += resets;

        double e = env;
        EnvStage s = st;
        const int p0 = ph;
        const double gStart = g0, gStep = dg, as = aStep, rs = rStep, ss = sStep;
        for (int j = 0; j < run; ++j)
        {
            if (s == EnvStage::attack)
            {
                e += as;
                if (e >= 1)
                {
                    e = 1;
                    s = EnvStage::sustain;
                }
            }
            else if (s == EnvStage::release)
            {
                e -= rs;
                if (e <= 0)
                {
                    e = 0;
                    done = true;
                }
            }
            else if (s == EnvStage::steal)
            {
                e -= ss;
                if (e <= 0)
                {
                    e = 0;
                    done = true;
                }
            }
            const double g = (gStart + gStep * (p0 + j + 1)) * e;
            outL[o + j] = static_cast<float> (outL[o + j] + bL[j] * g);
            outR[o + j] = static_cast<float> (outR[o + j] + bR[j] * g);
        }
        env = e;
        st = s;
    }

    // Tape: Hermite read at rate (f/root) x speed; Scatter jumps and Refresh stutters crossfade 5 ms.
    void Voice::runTape (Head& h, int run) noexcept
    {
        if (h.lv != 0 || h.lvPrev != 0)
        {
            runTapeLevels (h, run);   // M7 V4: faster than 1.25x this tick or the last (the M3 reads below are untouched)
            return;
        }
        const SampleData& S = *src.data;
        const RegionState& G = reg;
        double* bL = bufL;
        double* bR = bufR;
        const int p0 = ph;
        const double L = G.L;
        const bool stp = G.stop;
        const double sgStep = 1 / (kSlewSeconds * ctx->sr);
        double raw = h.raw, off = h.off, sg = h.stopG;
        const double spd = h.spd, dOff = h.dOff, g0L = h.g0L, g0R = h.g0R, dL = h.dL, dR = h.dR;
        // inline reads (mono or stereo) on the plain stretch of any loop pass; the result equals readReg's exactly
        const PlainStretch plain (S, G);
        for (int j = 0; j < run; ++j)
        {
            const int kk = p0 + j + 1;
            const double gl = g0L + dL * kk, gr = g0R + dR * kk;
            double x = raw + off;
            if (stp)
            {
                sg += (x >= 0 && x < L) ? sgStep : -sgStep;
                sg = sg < 0 ? 0 : sg > 1 ? 1 : sg;
            }
            if (G.fold)
                x = mapPos (G, x);
            double a = 0, b = 0;
            if (! plain.read (x, a, b))
                readReg (S, G, x, a, b);
            if (h.xLeft > 0)
            {
                const double w = static_cast<double> (h.xLeft) / h.xLen;
                double xo = h.xRaw + off;
                if (G.fold)
                    xo = mapPos (G, xo);
                double ol = 0, orr = 0;
                readReg (S, G, xo, ol, orr);
                const double gi = fadeIn (1 - w), go = fadeIn (w);
                a = a * gi + ol * go;
                b = b * gi + orr * go;
                h.xRaw += spd;
                h.xLeft--;
                h.peak = 2;
            }
            else if (h.peak < 1)
            {
                h.peak = 1;
            }
            bL[j] += a * gl * sg;
            bR[j] += b * gr * sg;
            raw += spd;
            off += dOff;
        }
        h.raw = raw;
        h.off = off;
        h.stopG = sg;
    }

    // M7 V4 (fast reads, OctaveLevels.h): runTape's loop with every read from the head's octave level. When the level
    // changed at this tick (h.lvPrev to h.lv), each read is the two levels' reads crossfaded with the weight
    // (ph + j + 1) / 32, so by the tick's end only the new level plays: a pitch sweep never steps. The two levels are the
    // same sound at the same frame (each level's frame j is the sound's frame j x 2^L), so the fade is a plain line.
    void Voice::runTapeLevels (Head& h, int run) noexcept
    {
        const SampleData& S = *src.data;
        const RegionState& G = reg;
        double* bL = bufL;
        double* bR = bufR;
        const int p0 = ph;
        const double L = G.L;
        const bool stp = G.stop;
        const double sgStep = 1 / (kSlewSeconds * ctx->sr);
        double raw = h.raw, off = h.off, sg = h.stopG;
        const double spd = h.spd, dOff = h.dOff, g0L = h.g0L, g0R = h.g0R, dL = h.dL, dR = h.dR;
        const int to = h.lv, from = h.lvPrev;
        const bool fading = to != from;
        // one read at x: the new level's, faded in over the tick from the old level's (level 0 is the sound itself)
        auto readAt = [&S, &G, to, from, fading] (double x, double w, double& a, double& b) noexcept {
            if (to > 0)
                readRegLevel (S, G, to, x, a, b);
            else
                readReg (S, G, x, a, b);
            if (! fading)
                return;
            double oa = 0, ob = 0;
            if (from > 0)
                readRegLevel (S, G, from, x, oa, ob);
            else
                readReg (S, G, x, oa, ob);
            a = oa + (a - oa) * w;
            b = ob + (b - ob) * w;
        };
        for (int j = 0; j < run; ++j)
        {
            const int kk = p0 + j + 1;
            const double gl = g0L + dL * kk, gr = g0R + dR * kk;
            const double wNew = static_cast<double> (kk) / kCtrl;
            double x = raw + off;
            if (stp)
            {
                sg += (x >= 0 && x < L) ? sgStep : -sgStep;
                sg = sg < 0 ? 0 : sg > 1 ? 1 : sg;
            }
            if (G.fold)
                x = mapPos (G, x);
            double a = 0, b = 0;
            readAt (x, wNew, a, b);
            if (h.xLeft > 0)
            {
                const double w = static_cast<double> (h.xLeft) / h.xLen;
                double xo = h.xRaw + off;
                if (G.fold)
                    xo = mapPos (G, xo);
                double ol = 0, orr = 0;
                readAt (xo, wNew, ol, orr);
                const double gi = fadeIn (1 - w), go = fadeIn (w);
                a = a * gi + ol * go;
                b = b * gi + orr * go;
                h.xRaw += spd;
                h.xLeft--;
                h.peak = 2;
            }
            else if (h.peak < 1)
            {
                h.peak = 1;
            }
            bL[j] += a * gl * sg;
            bR[j] += b * gr * sg;
            raw += spd;
            off += dOff;
        }
        h.raw = raw;
        h.off = off;
        h.stopG = sg;
        if (to > 0)
            ctx->countLevel (to);
    }
}
