// One Tape voice against the prototype's Voice, path by path (M2 part 1, before the Engine and renders.json):
// every Motion, Freeze as Forward, Scatter, Refresh Synced and Free with Skip, fractional heads with Spread, Drift
// and Head tune, Hold, Stop, the pre seam, the wheels, bend, offsets, macros, root and cents on a live key, and on
// clip notes the lane tables (pitch, Volume, Vibrato, Tone, Formant, Hollow, Travel), the Song and Random Starts
// and Tape's Path-as-1x. Each case renders 1 s of the Saw starter at 48 kHz, the voice driven directly (a live
// key from noteOn; a clip voice started as addVoice starts it), and compares the RMS of both channels, 12 samples
// and head 1's position with the JS Voice driven the same way (the values below come from the scratch script
// e1/voices.js, which loads the prototype through tools/fixtures/common.js). Samples within 1e-5 (the plan's
// render windows), RMS within 1e-6 relative, the position within 1e-9 relative; the hash is printed (bit-exact
// share). Group "engine".

#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "engine/Voice.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    struct ParityRow
    {
        const char* name;
        const char* hash;
        double rmsL, rmsR;
        double L[10];
        double R[2];
        double pos;
    };

    constexpr int kAtL[10] = { 500, 3000, 9000, 15000, 21000, 27000, 33000, 39000, 45000, 47900 };
    constexpr int kAtR[2] = { 9000, 33000 };

    // The JS Voice's results (e1/voices.js on the unchanged prototype, node 24.16.0 on Windows).
    const ParityRow kRows[] = {
        { "live-forward", "4452a35c", 0.3016487292117108, 0.30164790901539923, { 0.2698465883731842, -0.38385269045829773, -0.0605163499712944, 0.29922133684158325, -0.5104174613952637, -0.15466776490211487, 0.14484085142612457, 0.4461163580417633, -0.29732152819633484, -0.08286209404468536 }, { -0.05539320409297943, 0.13863937556743622 }, 44099.99999998242 },
        { "live-backward", "8f411f08", 0.30164790901274324, 0.3016487292110273, { -0.30477607250213623, 0.35637637972831726, 0.05539320409297943, -0.2128443568944931, 0.4378754794597626, 0.1927204579114914, -0.13863937556743622, -0.44782763719558716, 0.2752509117126465, 0.0838037058711052 }, { 0.0605163499712944, -0.14484085142612457 }, -44099.99999998242 },
        { "live-bounce", "d332a04a", 0.30175442486327597, 0.30176903504486075, { 0.45011135935783386, -0.12151496112346649, 0.18816323578357697, 0.4465489387512207, -0.2606741786003113, 0.04545134678483009, 0.27473941445350647, -0.029106851667165756, -0.3430267572402954, 0.4357948899269104 }, { 0.12666769325733185, 0.3000326156616211 }, 44099.99999998242 },
        { "live-mirror-2", "0db00533", 0.25340190970796167, 0.2688938799921521, { -0.03993357717990875, -0.1717252880334854, -0.08661152422428131, 0.0031594547908753157, -0.21111921966075897, -0.10861591249704361, -0.06754482537508011, 0.6290369033813477, -0.15800707042217255, -0.10511565953493118 }, { -0.13643021881580353, -0.13393856585025787 }, 44099.99999998242 },
        { "live-freeze-as-forward", "4452a35c", 0.3016487292117108, 0.30164790901539923, { 0.2698465883731842, -0.38385269045829773, -0.0605163499712944, 0.29922133684158325, -0.5104174613952637, -0.15466776490211487, 0.14484085142612457, 0.4461163580417633, -0.29732152819633484, -0.08286209404468536 }, { -0.05539320409297943, 0.13863937556743622 }, 44099.99999998242 },
        { "live-scatter-1/8T-seed2", "1c131d0f", 0.3021262330521778, 0.3021312418981224, { 0.2698465883731842, -0.38385269045829773, -0.32749849557876587, -0.009946123696863651, 0.444208562374115, -0.32350215315818787, 0.25851014256477356, -0.5745067000389099, -0.3409804105758667, -0.07699739187955856 }, { -0.31953033804893494, 0.2052869349718094 }, 28595.602189066354 },
        { "live-refresh-sync-1/16", "4356bc22", 0.300789788903836, 0.3007847048833526, { 0.2698465883731842, -0.38385269045829773, -0.27777037024497986, -0.38385269045829773, -0.27777037024497986, -0.38385269045829773, -0.27777037024497986, -0.38385269045829773, -0.27777037024497986, -0.06599412858486176 }, { -0.27356916666030884, -0.27356916666030884 }, 5497.799999999389 },
        { "live-refresh-free-skip", "3447014a", 0.3003330199224472, 0.3003320445560144, { 0.2698465883731842, -0.022922677919268608, -0.3930632174015045, 0.3300088346004486, 0.31724151968955994, 0.023035643622279167, -0.33393362164497375, 0.015735892578959465, -0.32978054881095886, -0.08527372777462006 }, { -0.36688974499702454, -0.2658677101135254 }, 17640.000000006337 },
        { "live-heads-2.5-spread-drift-octaves", "950dd4ff", 0.3149433536485035, 0.252771368246936, { 0.12252280116081238, 0.05403025820851326, 0.08242160081863403, 0.27002814412117004, 0.2698814868927002, 0.14177078008651733, 0.8387593626976013, 0.6281688213348389, 0.45878463983535767, -0.21708810329437256 }, { -0.035024866461753845, 0.7752823233604431 }, 44502.63871289409 },
        { "live-hold", "82a341d5", 0.17388611649296232, 0.17358215615305178, { -0.20149768888950348, 0.19319769740104675, 0.4532812237739563, 0.06841328740119934, 0.06841328740119934, 0.06841328740119934, 0.06841328740119934, 0.06841328740119934, 0.06841328740119934, 0.06841328740119934 }, { 0.5088797807693481, 0.0673326924443245 }, 44099.99999998242 },
        { "live-stop", "34b0e06d", 0.16428819820835858, 0.16427635329876256, { -0.20149768888950348, 0.19319769740104675, 0.4532812237739563, 0.02510230988264084, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }, { 0.5088797807693481, 0.0 }, 44099.99999998242 },
        { "live-pre-seam", "86d45cf6", 0.3002462734132141, 0.3002451143269218, { -0.29417115449905396, 0.13300307095050812, 0.4315915107727051, 0.17546914517879486, 0.4449796676635742, -0.2792665958404541, 0.47827860713005066, -0.20986948907375336, -0.5654289126396179, -0.34062299132347107 }, { 0.4030578136444092, 0.5978490114212036 }, 44099.99999998242 },
        { "live-wheels-bend-offsets", "5ecebe5f", 0.03579273488037985, 0.03218647714228252, { 0.03388563171029091, 0.011069517582654953, 0.04118921607732773, -0.01676095277070999, 0.004185343626886606, -0.012479518540203571, 0.03136076033115387, -0.038447532802820206, 0.059232693165540695, 0.03877763822674751 }, { 0.04612847790122032, 0.0321170799434185 }, -146630.36487962175 },
        { "clip-lanes", "4b8d7fff", 0.14526691103405318, 0.14525704478488025, { -0.10665664821863174, -0.06553161889314651, -0.15762704610824585, 0.2462698370218277, 0.041557613760232925, 0.03955021873116493, 0.07941916584968567, 0.1270991563796997, -0.028310494497418404, 0.09219802170991898 }, { -0.10927518457174301, 0.08190484344959259 }, 63974.16773211689 },
        { "clip-start-song", "2084fa8b", 0.2169297356378121, 0.21692943302744436, { 0.057942938059568405, 0.4461163878440857, -0.2973215579986572, 0.021379778161644936, 0.3271128535270691, 0.0, 0.0, 0.0, 0.0, 0.0 }, { -0.2752509117126465, 0.0 }, 57770.99999992177 },
        { "clip-start-random", "7f74cd53", 0.30146333959292226, 0.30147111108812713, { -0.3806774616241455, 0.029715444892644882, 0.31794682145118713, -0.38596487045288086, -0.07856997102499008, 0.26089200377464294, -0.5594947934150696, -0.18297524750232697, 0.13464410603046417, 0.3096286356449127 }, { 0.37392088770866394, -0.446030855178833 }, 134047.37052400655 },
        { "clip-path-as-1x", "6f554fa6", 0.3016487380879835, 0.3016479180819615, { 0.2698466181755066, -0.38385269045829773, -0.0605163499712944, 0.29922133684158325, -0.5104175209999084, -0.15466777980327606, 0.14484086632728577, 0.4461163878440857, -0.2973215579986572, -0.08286209404468536 }, { -0.05539320409297943, 0.13863937556743622 }, 44099.99999998242 },
    };

    constexpr std::int64_t kN = 48000;

    rig::RawNote rawNote (const char* id, double p, double s, double l)
    {
        rig::RawNote n;
        n.id = id;
        n.p = p;
        n.s = s;
        n.l = l;
        return n;
    }

    rig::LaneSpec laneFn (std::function<double (double)> fn)
    {
        rig::LaneSpec s;
        s.set = true;
        s.fn = std::move (fn);
        return s;
    }

    rig::LaneSpec laneValue (double v)
    {
        rig::LaneSpec s;
        s.set = true;
        s.value = v;
        return s;
    }

    /** Sets up case i on the rig the way e1/voices.js sets it up on the JS engine; returns the voice and its start. */
    engine::Voice& setUp (int i, rig::VoiceRig& rg, std::int64_t& k0)
    {
        core::Inst I = core::defaultInst();
        I.engine = core::Engine::tape;
        k0 = 0;
        int p = 60;
        double vel = 1;
        bool clip = false;
        rig::RawNote X;
        switch (i)
        {
            case 0: break;
            case 1: I.motion = core::Motion::backward; break;
            case 2: I.motion = core::Motion::bounce; I.region.start = 0.2; I.region.end = 0.3; break;
            case 3: I.motion = core::Motion::mirror; I.heads = 2; break;
            case 4: I.motion = core::Motion::freeze; break;
            case 5: I.motion = core::Motion::scatter; I.jump = core::RateId::r1_8T; I.seed = 2; break;
            case 6: I.refresh.mode = core::RefreshMode::sync; I.refresh.rate = core::RateId::r1_16; break;
            case 7: I.refresh.mode = core::RefreshMode::free; I.refresh.ms = 60; I.refresh.skip = 0.4; break;
            case 8: I.heads = 2.5; I.spread = 0.6; I.drift = 0.6; I.headTune = core::HeadTune::octaves; break;
            case 9: I.region.start = 0.5; I.region.end = 0.55; I.region.atEnd = core::AtEnd::hold; break;
            case 10: I.region.start = 0.5; I.region.end = 0.55; I.region.atEnd = core::AtEnd::stop; break;
            case 11: I.region.start = 0.95; I.region.end = 1; I.region.xfadeMs = 10; break;
            case 12:
                p = 64;
                vel = 0.6;
                I.mod1.target = static_cast<std::int8_t> (core::LaneId::travel);
                I.mod1.cc = 1;
                I.bendRange = 7;
                I.offsets.amp = -0.2;
                I.offsets.tone = -0.3;
                I.offsets.hollow = 0.4;
                I.macros = { 0.7, 0.5, 0.6, 0.5, 0.5, 0.5, 0.4, 0.6 };
                I.velSens = 0.5;
                I.pitch = 3;
                I.cents = 30;
                I.root = 59;
                rg.ctx.wheel[0] = 0.25;
                rg.ctx.wheelTouched[0] = true;
                rg.ctx.bend = 0.5;
                break;
            case 13:
                clip = true;
                X = rawNote ("ln", 62, 0, 2);
                X.lanes[static_cast<std::size_t> (core::LaneId::pitch)] = laneFn ([] (double u) { return 0.5 + 0.1 * u; });
                X.lanes[static_cast<std::size_t> (core::LaneId::amp)] = laneFn ([] (double u) { return 0.9 - 0.4 * u; });
                X.lanes[static_cast<std::size_t> (core::LaneId::vib)] = laneValue (0.3);
                X.lanes[static_cast<std::size_t> (core::LaneId::tone)] = laneFn ([] (double u) { return 0.6 + 0.3 * u; });
                X.lanes[static_cast<std::size_t> (core::LaneId::formant)] = laneValue (0.8);
                X.lanes[static_cast<std::size_t> (core::LaneId::hollow)] = laneValue (0.5);
                X.lanes[static_cast<std::size_t> (core::LaneId::travel)] =
                    laneFn ([] (double u) { return 0.75 + 0.1 * std::sin (2 * 3.141592653589793 * 2 * u); });
                break;
            case 14:
                clip = true;
                X = rawNote ("sg", 60, 1.5, 1);
                X.start.mode = 2;   // song
                break;
            case 15:
                clip = true;
                X = rawNote ("rd", 60, 0, 2);
                X.start.mode = 3;   // random
                X.start.pos = 0.2;
                X.start.range = 0.5;
                X.start.seed = 3;
                break;
            default:
                clip = true;
                X = rawNote ("pa", 60, 0, 2);
                X.travel = 1;   // path
                X.lanes[static_cast<std::size_t> (core::LaneId::travel)] = laneFn ([] (double u) { return 0.1 + 0.8 * u; });
                break;
        }
        rg.setInst (I);
        const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
        rg.setSample (engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw"));
        if (! clip)
            return rg.live (p, vel, 0);
        rg.setSnapshot (rig::rawSnapshot ({ X }, 4, false));
        rg.play (120, 0, false);
        engine::Voice& v = rg.clip (0);
        k0 = v.startK;
        return v;
    }

    bool closeRel (double a, double b, double rel)
    {
        return std::abs (a - b) <= rel * std::max (1.0, std::max (std::abs (a), std::abs (b)));
    }
}

SL_TEST ("engine", voiceParityWithPrototype)
{
    section ("one Tape voice against the prototype's Voice: 17 paths of the tick, render and runTape");

    const int count = static_cast<int> (sizeof (kRows) / sizeof (kRows[0]));
    int ok = 0, exact = 0;
    for (int i = 0; i < count; ++i)
    {
        const ParityRow& w = kRows[i];
        rig::VoiceRig rg (48000);
        std::int64_t k0 = 0;
        engine::Voice& v = setUp (i, rg, k0);
        std::vector<float> L (static_cast<std::size_t> (kN)), R (static_cast<std::size_t> (kN));
        for (std::int64_t j = 0; j < kN; j += 128)
            if (! v.done)
                v.render (L.data(), R.data(), static_cast<int> (j), static_cast<int> (std::min<std::int64_t> (128, kN - j)), k0 + j);
        const double rl = rig::rms (L, 0, kN), rr = rig::rms (R, 0, kN);
        double worst = 0;
        for (int q = 0; q < 10; ++q)
            worst = std::max (worst, std::abs (static_cast<double> (L[static_cast<std::size_t> (kAtL[q])]) - w.L[q]));
        for (int q = 0; q < 2; ++q)
            worst = std::max (worst, std::abs (static_cast<double> (R[static_cast<std::size_t> (kAtR[q])]) - w.R[q]));
        const double pos = v.heads[0].raw + v.heads[0].off;
        const std::string hash = signals::hex8 (rig::hashOf (L, R));
        const bool within = worst <= 1e-5 && closeRel (rl, w.rmsL, 1e-6) && closeRel (rr, w.rmsR, 1e-6) && closeRel (pos, w.pos, 1e-9) && rl > 0.01;
        if (hash == w.hash)
            ++exact;
        if (within)
            ++ok;
        check (within, fmt ("%s: samples within %.2e, rms %.9f / %.9f (the prototype %.9f / %.9f), head 1 at %.6f (%.6f)%s", w.name, worst, rl,
                            rr, w.rmsL, w.rmsR, pos, w.pos, hash == w.hash ? ", bit for bit" : ""));
    }
    note (fmt ("%d of %d cases within tolerance, %d bit for bit", ok, count, exact));
    check (count == 17, fmt ("17 cases (%d)", count));
}
