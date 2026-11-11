// engine/renders.json (PLUGIN-PLAN 5.3, the first test row of M2 and of M3): every case with needs "tape" (M2), and
// every case with needs "grains" that plays Voice and Cloud only (M3 part G), rendered by the C++ engine exactly as
// tools/fixtures/engine.js drives the prototype (Tests/engine/EngineRender), and compared with section 5's
// tolerances: if the hash matches, bit for bit (printed); otherwise windows within 1e-5, blockRms within
// 1e-6 + 1e-4 x rms, blockPeak within 1e-5, stats exact, display ids, kinds, engines, motions, released, dir and
// thinned exact and pos, level, env and beat within 1e-6, trace within 1e-6. The grains cases that play Lock wait
// for M3 part L (kWaitForLock below names each one and why). Group "engine".

#include "engine/EngineRender.h"
#include "engine/VoiceRig.h"
#include "model/ModelFixture.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Seeds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;
    using render::Rendered;

    const char* kKindCodes[] = { "clip", "live" };
    const char* kKcCodes[] = { "tape", "voice", "cloud", "lock" };

    /** The fixture's per-256 RMS and peak of a render, as tools/fixtures/engine.js computes them. */
    void blockMeasures (const Rendered& r, std::vector<float>& rms, std::vector<float>& pk)
    {
        const auto n = static_cast<std::int64_t> (r.L.size());
        const std::int64_t nb = (n + 255) / 256;
        rms.assign (static_cast<std::size_t> (nb * 2), 0.0f);
        pk.assign (static_cast<std::size_t> (nb * 2), 0.0f);
        for (std::int64_t b = 0; b < nb; ++b)
        {
            const std::int64_t a = b * 256, z = std::min (n, a + 256);
            double sl = 0, sr2 = 0;
            float pl = 0, pr = 0;
            for (std::int64_t i = a; i < z; ++i)
            {
                const float lf = r.L[static_cast<std::size_t> (i)], rf = r.R[static_cast<std::size_t> (i)];
                const double l = lf, rr = rf;
                sl += l * l;
                sr2 += rr * rr;
                pl = std::max (pl, std::abs (lf));
                pr = std::max (pr, std::abs (rf));
            }
            rms[static_cast<std::size_t> (2 * b)] = static_cast<float> (std::sqrt (sl / static_cast<double> (z - a)));
            rms[static_cast<std::size_t> (2 * b + 1)] = static_cast<float> (std::sqrt (sr2 / static_cast<double> (z - a)));
            pk[static_cast<std::size_t> (2 * b)] = pl;
            pk[static_cast<std::size_t> (2 * b + 1)] = pr;
        }
    }

    bool audioMatches (const Rendered& r, const JsonValue& o, std::string& why)
    {
        std::vector<float> rms, pk, wantRms, wantPk;
        blockMeasures (r, rms, pk);
        if (! fixture::f32 (o["blockRms"], wantRms) || ! fixture::f32 (o["blockPeak"], wantPk) || wantRms.size() != rms.size()
            || wantPk.size() != pk.size())
        {
            why = "blockRms / blockPeak: not the same length";
            return false;
        }
        for (std::size_t i = 0; i < rms.size(); ++i)
        {
            const double w = wantRms[i];
            if (! (std::abs (rms[i] - w) <= 1e-6 + 1e-4 * w))
            {
                why = fmt ("blockRms[%zu] %.9g, the prototype %.9g", i, static_cast<double> (rms[i]), w);
                return false;
            }
            if (! (std::abs (pk[i] - wantPk[i]) <= 1e-5))
            {
                why = fmt ("blockPeak[%zu] %.9g, the prototype %.9g", i, static_cast<double> (pk[i]), static_cast<double> (wantPk[i]));
                return false;
            }
        }
        const JsonValue& windows = o["windows"];
        for (std::size_t wi = 0; wi < windows.size(); ++wi)
        {
            const auto at = static_cast<std::size_t> (windows[wi]["at"].asNumber());
            std::vector<float> wl, wr;
            if (! fixture::f32 (windows[wi]["L"], wl) || ! fixture::f32 (windows[wi]["R"], wr) || at + wl.size() > r.L.size() || wr.size() != wl.size())
            {
                why = "a window cannot be read";
                return false;
            }
            for (std::size_t i = 0; i < wl.size(); ++i)
                if (! (std::abs (r.L[at + i] - wl[i]) <= 1e-5 && std::abs (r.R[at + i] - wr[i]) <= 1e-5))
                {
                    why = fmt ("window at %zu, frame %zu: %.9g %.9g, the prototype %.9g %.9g", at, i, static_cast<double> (r.L[at + i]),
                               static_cast<double> (r.R[at + i]), static_cast<double> (wl[i]), static_cast<double> (wr[i]));
                    return false;
                }
        }
        return true;
    }

    bool statsMatch (const Rendered& r, const JsonValue& o, std::string& why)
    {
        const JsonValue& stats = o["stats"];
        if (stats.size() != r.stats.size())
        {
            why = fmt ("%zu stats rows, the prototype %zu", r.stats.size(), stats.size());
            return false;
        }
        for (std::size_t i = 0; i < stats.size(); ++i)
            for (std::size_t f = 0; f < 8; ++f)
                if (static_cast<double> (r.stats[i][f]) != stats[i][f].asNumber())
                {
                    why = "stats row " + core::toJson (stats[i]) + " got [";
                    for (std::size_t g = 0; g < 8; ++g)
                        why += (g != 0 ? "," : "") + std::to_string (r.stats[i][g]);
                    why += "]";
                    return false;
                }
        return true;
    }

    bool displayMatches (const Rendered& r, const JsonValue& o, std::string& why)
    {
        const JsonValue& display = o["display"];
        if (display.size() != r.display.size())
        {
            why = fmt ("%zu display rows, the prototype %zu", r.display.size(), display.size());
            return false;
        }
        for (std::size_t i = 0; i < display.size(); ++i)
        {
            const JsonValue& row = display[i];
            const engine::DisplayFrame& f = r.display[i];
            const std::string at = " (display at " + core::toJson (row[0]) + ")";
            if (! (std::abs (f.beat - row[1].asNumber()) <= 1e-6) || f.playing != row[2].asBool())
            {
                why = fmt ("beat %.9g playing %d, the prototype %.9g %d", f.beat, f.playing ? 1 : 0, row[1].asNumber(), row[2].asBool() ? 1 : 0) + at;
                return false;
            }
            const JsonValue& voices = row[3];
            if (voices.size() != f.numVoices || row[4].asNumber() != f.numGrains)
            {
                why = fmt ("%d voices and %d grains, the prototype %zu and %g", f.numVoices, f.numGrains, voices.size(), row[4].asNumber()) + at;
                return false;
            }
            for (std::size_t vi = 0; vi < voices.size(); ++vi)
            {
                const JsonValue& wv = voices[vi];
                const engine::DisplayVoice& gv = f.voices[vi];
                const bool same = gv.noteIdHash == core::idHash (std::string_view (wv[0].asString())) && gv.p == wv[1].asNumber()
                                  && wv[2].asString() == kKindCodes[gv.kind % 2] && wv[3].asString() == kKcCodes[gv.engine % 4]
                                  && wv[4].asString() == core::kMotionCodes[gv.motion % 6] && std::abs (gv.env - wv[5].asNumber()) <= 1e-6
                                  && gv.released == wv[6].asBool() && wv[7].size() == gv.numHeads;
                if (! same)
                {
                    why = fmt ("voice %zu: p %d %s %s %s env %.9g released %d heads %d, the prototype ", vi, gv.p, kKindCodes[gv.kind % 2],
                               kKcCodes[gv.engine % 4], core::kMotionCodes[gv.motion % 6], static_cast<double> (gv.env), gv.released ? 1 : 0,
                               gv.numHeads)
                          + core::toJson (wv).substr (0, 160) + at;
                    return false;
                }
                for (std::size_t hi = 0; hi < wv[7].size(); ++hi)
                {
                    const JsonValue& wh = wv[7][hi];
                    const engine::DisplayHead& gh = gv.heads[hi];
                    if (! (gh.index == wh[0].asNumber() && std::abs (gh.pos - wh[1].asNumber()) <= 1e-6 && gh.dir == wh[2].asNumber()
                           && std::abs (gh.level - wh[3].asNumber()) <= 1e-6 && gh.thinned == wh[4].asBool()))
                    {
                        why = fmt ("voice %zu head %zu: [%d, %.9g, %d, %.9g, %d], the prototype ", vi, hi, gh.index, static_cast<double> (gh.pos), gh.dir,
                                   static_cast<double> (gh.level), gh.thinned ? 1 : 0)
                              + core::toJson (wh) + at;
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool traceMatches (const Rendered& r, const JsonValue& o, std::string& why)
    {
        std::vector<float> want;
        if (! fixture::f32 (o["trace"]["head0pos"], want) || want.size() != r.trace.size())
        {
            why = fmt ("trace: %zu values, and the prototype's cannot be read or has another length", r.trace.size());
            return false;
        }
        for (std::size_t i = 0; i < want.size(); ++i)
            if (! fixture::withinAbs (r.trace[i], want[i], 1e-6))
            {
                why = fmt ("trace[%zu] %.9g, the prototype %.9g", i, static_cast<double> (r.trace[i]), static_cast<double> (want[i]));
                return false;
            }
        return true;
    }

    /** The "grains" cases that wait for M3 part L, and why: each one plays a Lock voice (counted in the prototype:
        these 9 cases call runLock, the other 28 never do), and Lock plays as Tape until L (engine::playedEngine). */
    struct Waiting
    {
        const char* name;
        const char* why;
    };
    constexpr Waiting kWaitForLock[] = {
        { "lock-from-noise", "a Lock note (part L)" },
        { "lock-from-chord", "a Lock note (part L)" },
        { "budget-16x8-lock12-48k", "16 Lock N = 12 voices (part L; the budget row is part B's)" },
        { "budget-16x8-lock12-96k", "16 Lock N = 12 voices (part L; the budget row is part B's)" },
        { "budget-16x8-lock12-clip-44.1k", "16 Lock N = 12 voices (part L; the budget row is part B's)" },
        { "budget-16x8-lock12-clip-48k", "16 Lock N = 12 voices (part L; the budget row is part B's)" },
        { "budget-16x8-lock12-clip-96k", "16 Lock N = 12 voices (part L; the budget row is part B's)" },
        { "strand-demo-recipe-8", "recipe 8 is Cloud with Lock (part L)" },
        { "raw-snapshot", "note raw-c is Cloud with Lock (part L), and the harness has no raw-snapshot reader yet" },
    };
    constexpr std::size_t kGrainCasesNow = 37 - std::size (kWaitForLock);   // 28

    const Waiting* waitingFor (const std::string& name)
    {
        for (const Waiting& w : kWaitForLock)
            if (name == w.name)
                return &w;
        return nullptr;
    }

    /** One render against the fixture's out. hashMatch: the whole render is bit for bit the prototype's. */
    bool renderMatches (const Rendered& r, const JsonValue& o, bool& hashMatch, std::string& why)
    {
        hashMatch = signals::hex8 (rig::hashOf (r.L, r.R)) == o["hash"].asString();
        const bool statsOnly = ! o.contains ("blockRms");
        if (! statsOnly && ! audioMatches (r, o, why))
            return false;
        if (! statsMatch (r, o, why))
            return false;
        if (! statsOnly && ! displayMatches (r, o, why))
            return false;
        if (o.contains ("trace") && ! traceMatches (r, o, why))
            return false;
        return true;
    }
}

SL_TEST ("engine", fixtureEngineRendersTape)
{
    section ("engine/renders.json: every case with needs \"tape\" (M2), section 5 tolerances");

    JsonValue doc;
    if (! fixture::load ("engine/renders.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const JsonValue& cases = doc["cases"];
    std::size_t tapeCases = 0, grainCases = 0;
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const std::string& needs = cases[c]["needs"].asString();
        tapeCases += needs == "tape" ? 1 : 0;
        grainCases += needs == "grains" ? 1 : 0;
    }
    check (cases.size() == 73 && tapeCases == 36 && grainCases == 37,
           fmt ("the file holds 73 cases: 36 tape and 37 grains (%zu, %zu, %zu); fixtureEngineRendersGrains runs the grains cases",
                cases.size(), tapeCases, grainCases));

    modelfx::FileTally tally ("engine/renders.json, needs tape", tapeCases);
    const JsonValue* firstTape = nullptr;
    Rendered firstRender;
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const JsonValue& k = cases[c];
        if (k["needs"].asString() != "tape")
            continue;
        const std::string name = k["name"].asString();
        Rendered r;
        std::string why;
        if (! render::renderFixtureCase (k, clips, r, why))
        {
            tally.fail (name + ": " + why);
            continue;
        }
        bool hashMatch = false;
        if (renderMatches (r, k["out"], hashMatch, why))
        {
            tally.pass (hashMatch);
            if (! hashMatch)
                note (name + ": within tolerance, not bit for bit (hash " + signals::hex8 (rig::hashOf (r.L, r.R)) + ", the prototype's "
                      + k["out"]["hash"].asString() + ")");
        }
        else
        {
            tally.fail (name + ": " + why);
        }
        if (firstTape == nullptr && k["out"].contains ("windows"))
        {
            firstTape = &k;
            firstRender = std::move (r);
        }
    }
    tally.finish();

    // A no-op is not a pass: the same comparison rejects the first case's render with one window sample moved by
    // twice the tolerance, and its stats with one voice more.
    if (firstTape != nullptr && ! firstRender.L.empty())
    {
        const JsonValue& o = (*firstTape)["out"];
        Rendered moved = firstRender;
        const auto at = static_cast<std::size_t> (o["windows"][0]["at"].asNumber()) + 100;
        moved.L[at] += 2e-5f;
        bool hm = false;
        std::string why1, why2;
        const bool caughtSample = ! renderMatches (moved, o, hm, why1);
        Rendered counted = firstRender;
        if (! counted.stats.empty())
            counted.stats[counted.stats.size() / 2][1] += 1;
        const bool caughtStats = ! renderMatches (counted, o, hm, why2);
        check (caughtSample && caughtStats,
               "negative control: a window sample 2e-5 off and a stats row one voice off both fail the comparison (" + why1 + "; " + why2 + ")");
    }
}

SL_TEST ("engine", fixtureEngineRendersGrains)
{
    section ("engine/renders.json: every case with needs \"grains\" that plays Voice and Cloud (M3 part G), section 5 tolerances");

    JsonValue doc;
    if (! fixture::load ("engine/renders.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const JsonValue& cases = doc["cases"];

    // The waiting list names grains cases of the file, each once, so the count below is the file's count minus it.
    std::size_t grainCases = 0, waitingFound = 0;
    for (std::size_t c = 0; c < cases.size(); ++c)
        if (cases[c]["needs"].asString() == "grains")
        {
            ++grainCases;
            waitingFound += waitingFor (cases[c]["name"].asString()) != nullptr ? 1 : 0;
        }
    check (grainCases == 37 && waitingFound == std::size (kWaitForLock) && grainCases - waitingFound == kGrainCasesNow,
           fmt ("37 grains cases in the file (%zu); %zu of them wait for part L (%zu named); %zu run now (expected %zu)", grainCases,
                waitingFound, std::size (kWaitForLock), grainCases - waitingFound, kGrainCasesNow));
    for (const Waiting& w : kWaitForLock)
        note (std::string ("waits for M3 part L: ") + w.name + " (" + w.why + ")");

    modelfx::FileTally tally ("engine/renders.json, needs grains (Voice and Cloud)", kGrainCasesNow);
    const JsonValue* control = nullptr;
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const JsonValue& k = cases[c];
        if (k["needs"].asString() != "grains" || waitingFor (k["name"].asString()) != nullptr)
            continue;
        const std::string name = k["name"].asString();
        Rendered r;
        std::string why;
        if (! render::renderFixtureCase (k, clips, r, why))
        {
            tally.fail (name + ": " + why);
            continue;
        }
        bool hashMatch = false;
        if (renderMatches (r, k["out"], hashMatch, why))
        {
            tally.pass (hashMatch);
            if (! hashMatch)
                note (name + ": within tolerance, not bit for bit (hash " + signals::hex8 (rig::hashOf (r.L, r.R)) + ", the prototype's "
                      + k["out"]["hash"].asString() + ")");
        }
        else
        {
            tally.fail (name + ": " + why);
        }
        if (name == "voice-c3")
            control = &k;
    }
    tally.finish();

    // A no-op is not a pass: the voice-c3 case played by the Tape engine (the M2 stand-in) is far outside the
    // tolerance, so a dispatch that still played Tape, or grains that read the wrong place, would fail above.
    if (control != nullptr)
    {
        JsonValue asTape = *control;
        JsonValue inst = asTape["inst"];
        inst.set ("engine", JsonValue ("tape"));
        asTape.set ("inst", inst);
        Rendered r;
        std::string why;
        bool hm = false;
        const bool rendered = render::renderFixtureCase (asTape, clips, r, why);
        const bool caught = rendered && ! renderMatches (r, (*control)["out"], hm, why);   // sets why before the message is made
        check (caught, "negative control: voice-c3 played as Tape fails the comparison with the prototype's Voice render (" + why + ")");
    }
    else
    {
        check (false, "negative control: the voice-c3 case is in the file");
    }
}
