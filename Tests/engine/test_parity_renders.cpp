// engine/renders.json (PLUGIN-PLAN 5.3, the first test row of M2 and of M3): every case with needs "tape" (M2), and
// every case with needs "grains" (M3: Voice and Cloud from part G, Lock from part L), rendered by the C++ engine
// exactly as tools/fixtures/engine.js drives the prototype (Tests/engine/EngineRender), and compared with section
// 5's tolerances: if the hash matches, bit for bit (printed); otherwise windows within 1e-5, blockRms within
// 1e-6 + 1e-4 x rms, blockPeak within 1e-5, stats exact, display ids, kinds, engines, motions, released, dir and
// thinned exact and pos, level, env and beat within 1e-6, trace within 1e-6. A case that waits for M3 part B would
// be named in kWaitForB with its reason; none does. Group "engine".

#include "engine/EngineRender.h"
#include "engine/RenderCompare.h"
#include "engine/VoiceRig.h"
#include "model/ModelFixture.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Seeds.h"

#include <algorithm>
#include <array>
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

    /** The "grains" cases that wait for M3 part B (the complete planner), and why. Part G ran the 28 cases that play
        Voice and Cloud only; part L added the 9 that play Lock (counted in the prototype: they call runLock), the
        budget-16x8-lock12-* cases among them (their stats are the planner's thinning and cuts, which the planner
        already does as the JS). None waits now; a case put here is named with its reason and not run. */
    struct Waiting
    {
        const char* name;
        const char* why;
    };
    constexpr std::array<Waiting, 0> kWaitForB {};
    constexpr std::size_t kGrainCasesNow = 37 - kWaitForB.size();   // 37

    const Waiting* waitingFor (const std::string& name)
    {
        for (const Waiting& w : kWaitForB)
            if (name == w.name)
                return &w;
        return nullptr;
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
        if (render::renderMatches (r, k["out"], hashMatch, why))
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
        const bool caughtSample = ! render::renderMatches (moved, o, hm, why1);
        Rendered counted = firstRender;
        if (! counted.stats.empty())
            counted.stats[counted.stats.size() / 2][1] += 1;
        const bool caughtStats = ! render::renderMatches (counted, o, hm, why2);
        check (caughtSample && caughtStats,
               "negative control: a window sample 2e-5 off and a stats row one voice off both fail the comparison (" + why1 + "; " + why2 + ")");
    }
}

SL_TEST ("engine", fixtureEngineRendersGrains)
{
    section ("engine/renders.json: every case with needs \"grains\" (M3: Voice, Cloud and Lock), section 5 tolerances");

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
    check (grainCases == 37 && waitingFound == kWaitForB.size() && grainCases - waitingFound == kGrainCasesNow,
           fmt ("37 grains cases in the file (%zu); %zu of them wait for part B (%zu named); %zu run now (expected %zu)", grainCases,
                waitingFound, kWaitForB.size(), grainCases - waitingFound, kGrainCasesNow));
    for (const Waiting& w : kWaitForB)
        note (std::string ("waits for M3 part B: ") + w.name + " (" + w.why + ")");

    modelfx::FileTally tally ("engine/renders.json, needs grains (Voice, Cloud and Lock)", kGrainCasesNow);
    const JsonValue* control = nullptr;
    const JsonValue* lockControl = nullptr;
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
        if (render::renderMatches (r, k["out"], hashMatch, why))
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
        if (name == "lock-from-noise")
            lockControl = &k;
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
        const bool caught = rendered && ! render::renderMatches (r, (*control)["out"], hm, why);   // sets why before the message is made
        check (caught, "negative control: voice-c3 played as Tape fails the comparison with the prototype's Voice render (" + why + ")");
    }
    else
    {
        check (false, "negative control: the voice-c3 case is in the file");
    }

    // The same for Lock: lock-from-noise with the Lock switch off (plain Cloud grains on the noise) is far outside
    // the tolerance, so a Lock dispatch that played plain Cloud, or the Tape stand-in, would fail above.
    if (lockControl != nullptr)
    {
        JsonValue asCloud = *lockControl;
        JsonValue inst = asCloud["inst"];
        inst.set ("lock", JsonValue (false));
        asCloud.set ("inst", inst);
        Rendered r;
        std::string why;
        bool hm = false;
        const bool rendered = render::renderFixtureCase (asCloud, clips, r, why);
        const bool caught = rendered && ! render::renderMatches (r, (*lockControl)["out"], hm, why);
        check (caught, "negative control: lock-from-noise played as plain Cloud fails the comparison with the prototype's Lock render (" + why + ")");
    }
    else
    {
        check (false, "negative control: the lock-from-noise case is in the file");
    }
}
