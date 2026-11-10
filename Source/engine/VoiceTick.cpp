// sl-engine.js Voice.tick: the control tick, every 32 samples from the voice's start (PLUGIN-PLAN 1.9: the Scan /
// Travel path). Ported line by line: the same operation order and constants; Math.pow is std::pow (2.0, x),
// Math.exp is core::jsExp (V8's own), Math.round is core::jsRound (plan 1.6).
//
// Plugin changes here: the note's "follow the instrument" markers are resolved every tick (core::resolve, plan
// 1.4); Vibrato Sync (8.2 #12, M8's parameter) sets the vibrato rate from the tempo when it is not Free; and the
// M2 -> M3 switch (playedEngine, Voice.h) plays every note as Tape until the grain engines exist.

#include "engine/Voice.h"

#include "core/Curves.h"
#include "core/JsCompat.h"
#include "core/JsMath.h"
#include "core/Lanes.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "core/Travel.h"
#include "engine/EngineMath.h"

#include <cmath>

namespace sl::engine
{
    namespace
    {
        constexpr int L_PITCH = 0, L_AMP = 1, L_VIB = 2, L_TONE = 3, L_FORMANT = 4, L_HOLLOW = 5, L_TRAVEL = 6, L_HEADS = 7, L_GRAIN = 8,
                      L_REFRESH = 9;

        // StartSnap::mode holds a model::StartMode: region 0, carry 1, song 2, random 3 (test_voice checks the values).
        constexpr std::uint8_t kStartSong = 2;
        constexpr std::uint8_t kStartRandom = 3;

        constexpr double laneDef (int li) noexcept { return core::kLanes[li].def; }

        inline double pow2 (double x) noexcept { return std::pow (2.0, x); }
    }

    void Voice::tick (std::int64_t kAbs, bool ff) noexcept
    {
        const VoiceContext& E = *ctx;
        const core::Inst& I = *E.inst;
        const Clock& C = *E.clock;
        const SampleData& S = *src.data;
        const double sr = E.sr, Ssr = src.sr, ratio = Ssr / sr;
        const core::NoteSnap& X = *note;
        const bool clip = kind == VoiceKind::clip;

        // settings: a clip note carries its own resolved settings (D6); a live voice follows the inst
        core::ResolvedNote rn;
        if (clip)
            rn = core::resolve (X, I);
        core::Engine engine = clip ? rn.engine : I.engine;
        if (static_cast<int> (engine) > 2)
            engine = I.engine;   // ENGINES[X.engine] ? X.engine : I.engine
        const bool lock = engine == core::Engine::cloud && (clip ? rn.lock : I.lock);
        core::Motion mot = clip ? rn.motion : I.motion;
        if (static_cast<int> (mot) > 5)
            mot = I.motion;      // MOTIONS[X.motion] ? X.motion : I.motion
        TravelPlay travel = clip ? (X.travel == 1 ? TravelPlay::path : TravelPlay::speed) : TravelPlay::speed;
        const bool tape = clip ? rn.tape : I.tape;
        const double jumpBeats = clip ? core::rateBeatsOr (rn.jumpBeats) : core::rateBeats (I.jump);
        const Kc wanted = engine == core::Engine::tape ? Kc::tape : engine == core::Engine::voice ? Kc::voice : lock ? Kc::lock : Kc::cloud;
        const Kc kcNow = playedEngine (wanted);   // the M2 -> M3 switch (Voice.h)
        asTape = kcNow != wanted;
        if (kcNow == Kc::tape)
        {
            if (mot == core::Motion::freeze)
                mot = core::Motion::forward;
            if (travel == TravelPlay::path)
                travel = TravelPlay::tapePath;
        }
        engineName = wanted;   // lock ? 'lock' : engine
        motion = mot;
        rev = clip ? rn.rev : I.rev;

        // region
        RegionState& G = reg;
        const core::RegionSettings& R = I.region;
        const double n = static_cast<double> (S.n);
        G.rs = std::floor (R.start * n);
        G.L = jsMax (64, jsMin (n - G.rs, std::floor ((R.end - R.start) * n)));
        if (G.rs + G.L > n)
            G.rs = jsMax (0, n - G.L);
        G.fold = mot == core::Motion::bounce;
        G.loop = R.atEnd == core::AtEnd::loop && ! G.fold;
        G.stop = R.atEnd == core::AtEnd::stop && ! G.fold;
        // seam fade: post-Region material first, else pre-Region material, else a plain wrap (see readReg)
        const double Xw = G.loop ? jsMin (R.xfadeMs * Ssr / 1000, G.L * 0.25) : 0, room1 = n - (G.rs + G.L) - 3, room0 = G.rs - 1;
        if (Xw <= 0)
        {
            G.X = 0;
            G.post = true;
        }
        else if (room1 >= Xw)
        {
            G.X = Xw;
            G.post = true;
        }
        else if (room0 >= Xw)
        {
            G.X = Xw;
            G.post = false;
        }
        else
        {
            G.post = room1 >= room0;
            G.X = jsMax (0, std::floor (jsMax (room1, room0)));
            if (G.X < 2)
                G.X = 0;
        }
        G.Lp = G.L;
        G.invX = G.X > 0 ? 1 / G.X : 0;
        if (G.X > 0)
        {
            if (G.cRs != G.rs || G.cL != G.L || G.cX != G.X || G.cPost != G.post || G.cSrc != &S)
            {
                G.cRs = G.rs;
                G.cL = G.L;
                G.cX = G.X;
                G.cPost = G.post;
                G.cSrc = &S;
                G.rho = seamRho (S, G);
            }
        }
        else
        {
            G.rho = 0;
        }
        const double L = G.L;

        // time inside the note
        double t = 0, beat;
        if (clip)
        {
            const double u = C.uAt (kAbs);
            if (! released && ! ff)
            {
                const double Uend = U + C.effLen (X);
                // compared in samples (kOf is a pure function of the anchor): a note end that lands exactly on a
                // control tick releases on the same tick in every loop pass, whatever the float rounding of the beat
                if (kAbs >= C.kOf (Uend))
                {
                    const core::NoteSnap* nx = snapshotNote (snap, rn.chain.nextIndex);
                    const double Un = nx != nullptr ? nx->s + static_cast<double> (m) * C.Lb : 0;
                    if (nx != nullptr && nx->s > X.s && Un <= Uend + 4.5 && u < Un + 0.05)
                        bridging = true;
                    else
                        release();
                }
                else if (u < U - 1e-6)
                {
                    release();
                }
            }
            t = X.l > 0 ? clampJs ((u - U) / X.l, 0, 1) : 1;
            beat = C.loopBeat (u);
        }
        else
        {
            beat = C.songBeat (kAbs);
        }

        // lanes: table (drawing + generators) or defaults, then wheels (live voices), offsets and macros
        double* lv = lanes;
        if (clip)
        {
            for (int li = 0; li < core::kNumLanes; ++li)
            {
                const core::LaneTab& tb = X.lanes[li];
                if (tb.data == nullptr || tb.len < 1)
                {
                    lv[li] = laneDef (li);
                    continue;
                }
                const double fi = t * (tb.len - 1);
                const double f0 = std::floor (fi);
                if (! (f0 >= 0 && f0 < tb.len))
                {
                    lv[li] = std::nan ("");   // t is NaN: JS reads tb[NaN] (undefined); fin () below takes the default
                    continue;
                }
                const auto i0 = static_cast<std::int32_t> (f0);
                const std::int32_t i1 = i0 + 1 < tb.len ? i0 + 1 : i0;
                lv[li] = tb.data[i0] + (fi - i0) * (static_cast<double> (tb.data[i1]) - tb.data[i0]);
            }
        }
        else
        {
            for (int li = 0; li < core::kNumLanes; ++li)
                lv[li] = laneDef (li);
        }
        if (! clip)
        {
            if (E.wheelTouched[0] && I.mod1.target >= 0 && I.mod1.target < core::kNumLanes)
                lv[I.mod1.target] = E.wheel[0];
            if (E.wheelTouched[1] && I.mod2.target >= 0 && I.mod2.target < core::kNumLanes)
                lv[I.mod2.target] = E.wheel[1];
        }
        const core::OffsetSettings& O = I.offsets;
        const auto& M = I.macros;
        lv[L_AMP] += O.amp;
        lv[L_VIB] += O.vib;
        lv[L_TONE] += O.tone + (M[6] - 0.5);
        lv[L_FORMANT] += O.formant + (M[7] - 0.5);
        lv[L_HOLLOW] += O.hollow;
        for (int li = 0; li < core::kNumLanes; ++li)
            lv[li] = fin (clampJs (lv[li], 0, 1), laneDef (li));

        // pitch: f = 440*2^((p - 69 + pitchLane + glide + vibrato + bend + inst.pitch + headTune)/12); root = inst.root + cents/100
        double glide = 0;
        if (clip && rn.hasGlide && rn.glideLen > 0)
        {
            if (bridging)
            {
                glide = rn.glideD;
            }
            else
            {
                const double tb = t * X.l, st0 = X.l - rn.glideLen;
                if (tb > st0)
                    glide = rn.glideD * core::gShape ((tb - st0) / rn.glideLen, std::isnan (rn.glideCurve) ? 0 : rn.glideCurve);
            }
        }
        // Vibrato Sync (8.2 #12): Free is the prototype's vibRate; a rate plays (bpm / 60) / rateBeats Hz
        const double vibHz = I.vibSync == core::kVibSyncFree ? I.vibRate : (C.bpm / 60) / core::rateBeats (static_cast<core::RateId> (I.vibSync));
        vibPh += vibHz * kCtrl / sr;
        vibPh -= std::floor (vibPh);
        const double vib = lv[L_VIB] * std::sin (kTau * vibPh);
        const double bend = clip ? 0 : E.bend * I.bendRange;
        const double rootMidi = I.root + I.cents / 100, rootHz = 440 * pow2 ((rootMidi - 69) / 12);
        const double semisNow = fin (X.p - rootMidi + (lv[L_PITCH] - 0.5) * 24 + glide + vib + bend + I.pitch, 0);
        semis = semisNow;
        const double vel = clampJs (fin (X.vel, 0.8), 0, 1), velGain = 1 - I.velSens * (1 - vel);

        // macros and Sample lanes
        const double mSpeed = pow2 ((M[0] - 0.5) * 4);
        const double spread = clampJs (I.spread + (M[1] - 0.5) * 2, 0, 1);
        double H = clampJs (I.heads + (lv[L_HEADS] - 0.5) * 14 + (M[2] - 0.5) * 14, 1, kMaxHeads);
        // a whole count drawn on the Heads lane arrives as e.g. 2.000000358 from its Float32 table: snap it, so no
        // phantom extra head sounds at 1e-7 (and costs a full head's budget)
        const double Hn = core::jsRound (H);
        if (std::abs (H - Hn) < 1e-5)
            H = Hn;
        const double grainMult = pow2 ((lv[L_GRAIN] - 0.5) * 4) * pow2 ((M[4] - 0.5) * 4);
        const double spray = clampJs (I.spray + (M[5] - 0.5) * 2, 0, 1);
        Hf = H;

        // refresh (GrainLock's quick/slow sampling)
        const core::RefreshSettings& RF = I.refresh;
        const bool smooth = RF.mode == core::RefreshMode::smooth || lv[L_REFRESH] < 0.06 - core::kF32Eps;
        bool refEvent = false;
        const bool refFirst = ! smooth && ! refInit;
        if (! smooth)
        {
            const double scale = pow2 ((lv[L_REFRESH] - 0.5) * 6) * pow2 ((M[3] - 0.5) * 6);
            double idx;
            if (RF.mode == core::RefreshMode::sync)
            {
                const double pb = jsMax (1e-4, core::rateBeats (RF.rate) / scale);
                if (RF.lockToBeat)
                {
                    idx = std::floor (beat / pb + 1e-9);
                }
                else
                {
                    refPh += (kCtrl / sr) * (C.bpm / 60) / pb;
                    idx = std::floor (refPh);
                }
            }
            else
            {
                const double per = jsMax (0.005, RF.ms / 1000 / scale);
                refPh += kCtrl / (per * sr);
                idx = std::floor (refPh);
            }
            if (refInit && idx != refIdx)
                refEvent = true;
            refIdx = idx;
            refInit = true;
        }
        else
        {
            refInit = false;
            refPh = 0;
        }

        // scatter jumps (every jump-rate beats, locked to the song beat)
        bool jumpEvent = false;
        if (mot == core::Motion::scatter)
        {
            const double ji = std::floor (beat / jumpBeats + 1e-9);
            if (jInit && ji != jIdx)
                jumpEvent = true;
            jIdx = ji;
            jInit = true;
        }
        else
        {
            jInit = false;
        }

        // engine switch inside a chain: restart the grain schedulers (the playhead carries)
        if (static_cast<int> (kcNow) != lastKc)
        {
            for (Head& h : heads)
                h.clearGrains();
            lastKc = static_cast<int> (kcNow);
        }
        kc = kcNow;

        // start position (first note of the voice; later chain notes carry the playhead)
        const std::int32_t seed = I.seed;
        const bool pathKind = travel == TravelPlay::path;
        const double pathPos = lv[L_TRAVEL] * L;
        if (! posInit)
        {
            const core::StartSnap& sp = X.start;
            double p0 = clampJs (fin (sp.pos, 0), 0, 1) * L;
            if (sp.mode == kStartSong && clip)
            {
                p0 = (X.s * 60 / C.bpm) * Ssr;
                p0 -= L * std::floor (p0 / L);   // song beat inside the clip: every loop pass plays the same
            }
            else if (sp.mode == kStartRandom)
            {
                p0 = (clampJs (fin (sp.pos, 0), 0, 1) + core::rand01 (core::hash32 (nh, kSaltStart, sp.seed, seed)) * clampJs (fin (sp.range, 0), 0, 1)) * L;
            }
            if (! std::isnan (startPos))
                p0 = clampJs (startPos, 0, 1) * L;
            if (pathKind)
                p0 = pathPos;
            for (Head& h : heads)
            {
                h.raw = p0;
                h.scat = 0;
            }
        }

        // heads
        const int Hc = static_cast<int> (jsMin (kMaxHeads, std::ceil (H - 1e-9)));
        const int Hfl = static_cast<int> (std::floor (H + 1e-9));
        const double invSq = 1 / std::sqrt (H);
        int tune = static_cast<int> (I.headTune);
        if (tune < 0 || tune > 3)
            tune = 0;   // HEAD_TUNE[I.headTune] || HEAD_TUNE.same
        const double* ht = kHeadTune[tune];
        const double s = travel == TravelPlay::tapePath ? 1 : core::travelSpeed (lv[L_TRAVEL]);
        const double base = s * I.speed * mSpeed;
        const double offA = 1 - core::jsExp (-kCtrl / (0.02 * sr));
        const double gStepMax = kCtrl / (0.02 * sr);
        double levelSum = 0;
        for (int i = 0; i < kMaxHeads; ++i)
        {
            Head& h = heads[static_cast<std::size_t> (i)];
            // heads that did not render last block still move, so a head that comes in lands where it would be
            if (! first && ! ff && ! h.rendered)
            {
                h.raw += h.spd * kCtrl;
                h.off += h.dOff * kCtrl;
            }
            h.rendered = false;
            h.want = i < Hc;
            const double frac = i < Hfl ? 1 : i < Hc ? H - Hfl : 0;
            h.level = frac * invSq;
            // spread offset (slewed so it never jumps) and pan +-(spread*0.75)
            const double offT = (i / H) * spread * L + h.tiny * Ssr;
            if (! posInit)
            {
                h.off = offT;
                h.dOff = 0;
            }
            else
            {   // exponential approach, at most 0.2 frames per frame (a Tape head bends by 20% at most, no chirp)
                const double cap = 0.2 * kCtrl * ratio;
                const double o1 = h.off + clampJs ((offT - h.off) * offA, -cap, cap);
                h.dOff = (o1 - h.off) / kCtrl;
            }
            const double pan = Hc > 1 ? (static_cast<double> (i) / (Hc - 1) * 2 - 1) * spread * 0.75 : 0;
            double pl = 1, pr = 1;
            if (pan != 0)
            {
                const double a = (pan + 1) * kPi / 4;
                pl = std::cos (a) * kSqrt2;
                pr = std::sin (a) * kSqrt2;
            }
            // speed and pitch per head
            const int dir = mot == core::Motion::mirror ? (i % 2 == 1 ? -1 : 1) : kMotionDir[static_cast<int> (mot)];
            const double eff = clampJs (base * dir * (1 + I.drift * 0.25 * h.drift), -16, 16);
            const double fr = pow2 ((semisNow + ht[i]) / 12);
            if (kcNow == Kc::tape)
                h.spd = clampJs (eff * ratio * fr, -64, 64);
            else if (pathKind)
                h.spd = (pathPos + h.scat - h.raw) / kCtrl;
            else
                h.spd = eff * ratio;
            const double fHz = clampJs (rootHz * fr, 1, sr * 0.45);
            if (kcNow == Kc::voice)
            {
                const double P = jsMax (2, sr / fHz), F = pow2 ((lv[L_FORMANT] - 0.5) * 24 / 12);
                const double len = clampJs (I.grainVoice * grainMult * (sr / rootHz) / F, 4, kVoiceGrains * P - 1);
                h.P = P;
                h.len = len;
                h.step = F * ratio * (tape ? std::abs (eff) : 1);
                h.norm = jsMin (2, 2 * P / len);
                h.est = static_cast<int> (jsMin (kVoiceGrains, std::ceil (len / P) + 1));
            }
            else if (kcNow == Kc::cloud)
            {
                const double len = clampJs (I.grainCloud * grainMult * sr / 1000, 16, 4 * sr);
                h.len = len;
                h.hop = len / 3;
                h.step = fr * ratio * (tape ? std::abs (eff) : 1);
                h.norm = 2.0 / 3;
                h.est = static_cast<int> (jsMin (kCloudGrains, std::ceil (len / h.hop) + 1));
            }
            else if (kcNow == Kc::lock)
            {
                const double P = jsMax (2, sr / fHz);
                h.P = P;
                h.PR = P * ratio;
                h.dth = 1 / P;
                h.nT = static_cast<int> (clampJs (core::jsRound (I.grainLock * grainMult), 1, kLockMaxN));
                const int nBNow = h.xf >= 0 ? h.nB : 0;
                const int most = h.nT > h.nA ? h.nT : h.nA;
                h.est = 2 * (most > nBNow ? most : nBNow);
            }
            else
            {
                h.est = 2;
            }
            h.spray = spray * jsMin (Ssr, L / 2);
            h.smooth = smooth;

            // events: scatter jumps, refresh
            if (jumpEvent)
            {
                const double target = core::rand01 (core::hash32 (nh, i, jIdx, seed, kSaltJump)) * L;
                if (pathKind)
                {
                    h.scat = target;
                    h.spd = (pathPos + h.scat - h.raw) / kCtrl;
                }
                else
                {
                    if (kcNow == Kc::tape && h.on && ! ff)
                    {
                        h.xRaw = h.raw;
                        h.xLen = static_cast<int> (jsMax (1, core::jsRound (kTapeXfadeSeconds * sr)));
                        h.xLeft = h.xLen;
                    }
                    h.raw = target - h.off;
                }
                h.grabRaw = h.raw;
                h.grab = grabPos (G, h.raw + h.off);
                h.regrab = true;
            }
            else if (refEvent)
            {
                const bool skip = core::rand01 (core::hash32 (nh, i, refIdx, seed, kSaltSkip)) < RF.skip;
                if (kcNow == Kc::tape)
                {   // stutter: jump back to where the last grab began; a skipped refresh starts a new grab here
                    if (skip)
                    {
                        h.grabRaw = h.raw;
                    }
                    else if (! pathKind)
                    {
                        if (h.on && ! ff)
                        {
                            h.xRaw = h.raw;
                            h.xLen = static_cast<int> (jsMax (1, core::jsRound (kTapeXfadeSeconds * sr)));
                            h.xLeft = h.xLen;
                        }
                        h.raw = h.grabRaw;
                    }
                }
                else if (! skip)
                {
                    h.grab = grabPos (G, h.raw + h.off);
                    h.regrab = true;
                }
            }
            if (! posInit || refFirst)
            {
                h.grabRaw = h.raw;
                h.grab = grabPos (G, h.raw + h.off);
            }

            // gains: level x pan; thinned heads fade out over 20 ms; every change is slew-limited (no clicks)
            // a head sounds only with a grant from the budget planner; a new voice grants its own heads at its first
            // tick (the planner runs right after, before any sample is rendered)
            if (first && ! ff)
                h.grant = h.want ? h.est : 0;
            const bool act = h.want && h.allowed && h.grant > 0;
            const double tL = act ? h.level * pl : 0, tR = act ? h.level * pr : 0;
            if (first)
            {
                h.g0L = h.g1L = tL;
                h.g0R = h.g1R = tR;
                h.dL = 0;
                h.dR = 0;
            }
            else
            {
                h.g0L = h.g1L;
                h.g0R = h.g1R;
                h.g1L = h.g0L + clampJs (tL - h.g0L, -gStepMax, gStepMax);
                h.g1R = h.g0R + clampJs (tR - h.g0R, -gStepMax, gStepMax);
                h.dL = (h.g1L - h.g0L) / kCtrl;
                h.dR = (h.g1R - h.g0R) / kCtrl;
            }
            if (act && ! h.active)
                h.needPrime = true;
            h.active = act;
            const bool wasOn = h.on;
            h.on = act || h.g0L > 0 || h.g0R > 0 || h.g1L > 0 || h.g1R > 0;
            if (! h.on)
            {
                if (wasOn)
                    h.clearGrains();
                h.grant = 0;
            }
            const double pos = h.raw + h.off;
            if (! G.stop)
                h.stopG = 1;
            else if (first)
                h.stopG = (pos >= 0 && pos < L) ? 1 : 0;
            const double ps = h.spd + h.dOff;
            h.dir = ps > 1e-9 ? 1 : ps < -1e-9 ? -1 : 0;
            if (h.on)
                levelSum += h.level;
        }
        posInit = true;

        // per-voice chain: hollow cut -> (formant EQ: Tape and Cloud) -> low-pass (tone) -> gain
        if (! ff)
        {
            const double hdb = -lv[L_HOLLOW] * 28;
            hL.setPeak (sr, 950, 0.8, hdb);
            hR.setPeak (sr, 950, 0.8, hdb);
            const double fv = lv[L_FORMANT];
            const double fdb = kcNow == Kc::voice ? 0 : 18 * std::abs (fv - 0.5);
            const double fHz = 250 * pow2 (fv * 3.8);
            fL.setPeak (sr, fHz, 4, fdb);
            fR.setPeak (sr, fHz, 4, fdb);
            const double lp = 60 * pow2 (lv[L_TONE] * 8.3);
            lL.setLow (sr, lp, kSqrt1_2);
            lR.setLow (sr, lp, kSqrt1_2);
            lpHz = jsMin (lp, sr * 0.45);
            fmtDb = fdb;
            fmtHz = fHz;
            hollowDb = hdb;
            const double gT = lv[L_AMP] * lv[L_AMP] * 1.3 * velGain;
            if (first)
            {
                g0 = g1 = gT;
                dg = 0;
            }
            else
            {
                g0 = g1;
                g1 = gT;
                dg = (g1 - g0) / kCtrl;
            }
            level = levelSum * gT;
            first = false;
        }
    }
}
