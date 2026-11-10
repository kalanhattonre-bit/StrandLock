// The engine fixtures (PLUGIN-PLAN 5.3, M2's first test row). engine/defs.json: every case, counted against the
// file (the defs case: constants, budgets, HEAD_TUNE, salts, lane defaults and the HANN and FADE tables within
// 1 Float32 ulp; the normInst cases: core::clampForEngine exact). engine/renders.json comes with the Engine (M2
// part 2). Group "engine".

#include "engine/EngineFixture.h"
#include "model/ModelFixture.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "core/Travel.h"
#include "engine/EngineConfig.h"
#include "engine/Tables.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    /** got against a fixture F32 table: same length, every value within maxUlps; bitExact cleared on any difference. */
    bool tableWithin (const float* got, int n, const JsonValue& want, std::uint64_t maxUlps, bool& bitExact, std::string& why)
    {
        std::vector<float> w;
        if (! fixture::f32 (want, w))
        {
            why = "not an F32 table";
            return false;
        }
        if (static_cast<int> (w.size()) != n)
        {
            why = fmt ("length %d, the prototype %d", n, static_cast<int> (w.size()));
            return false;
        }
        std::uint64_t worst = 0;
        int at = -1;
        for (int i = 0; i < n; ++i)
        {
            const std::uint64_t u = fixture::ulps (got[i], w[static_cast<std::size_t> (i)]);
            if (u != 0)
                bitExact = false;
            if (u > worst)
            {
                worst = u;
                at = i;
            }
        }
        if (worst > maxUlps)
        {
            why = fmt ("value %d is %llu ulps off (%.9g, the prototype %.9g)", at, static_cast<unsigned long long> (worst),
                       static_cast<double> (got[at]), static_cast<double> (w[static_cast<std::size_t> (at)]));
            return false;
        }
        return true;
    }

    bool defsCaseMatches (const JsonValue& d, bool& bitExact, std::string& why)
    {
        auto num = [&d] (const char* key) { return d[key].asNumber (std::numeric_limits<double>::quiet_NaN()); };
        if (! (num ("CTRL") == engine::kCtrl && num ("MAX_HEADS") == engine::kMaxHeads && num ("VOICE_GRAINS") == engine::kVoiceGrains
               && num ("CLOUD_GRAINS") == engine::kCloudGrains && num ("LOCK_MAX_N") == engine::kLockMaxN && num ("BUDGET_48K") == engine::kBudget48k
               && num ("HANN_N") == engine::kHannN && num ("FADE_N") == engine::kFadeN && num ("F32_EPS") == core::kF32Eps))
        {
            why = "a constant differs";
            return false;
        }
        const JsonValue& budgets = d["budgets"];
        if (budgets.size() != 5)
        {
            why = "budgets: not 5 rates";
            return false;
        }
        for (std::size_t i = 0; i < budgets.size(); ++i)
            if (engine::budgetFor (std::stod (budgets.keyAt (i))) != static_cast<int> (budgets.valueAt (i).asNumber()))
            {
                why = "budget at " + budgets.keyAt (i);
                return false;
            }
        const char* tunes[4] = { "same", "detune", "octaves", "fifths" };
        for (int t = 0; t < 4; ++t)
        {
            const JsonValue& row = d["HEAD_TUNE"][tunes[t]];
            if (row.size() != static_cast<std::size_t> (engine::kMaxHeads))
            {
                why = std::string ("HEAD_TUNE ") + tunes[t];
                return false;
            }
            for (std::size_t h = 0; h < row.size(); ++h)
                if (row[h].asNumber() != engine::kHeadTune[t][h])
                {
                    why = std::string ("HEAD_TUNE ") + tunes[t];
                    return false;
                }
        }
        const JsonValue& salts = d["salts"];
        if (! (salts["SALT_JUMP"].asNumber() == engine::kSaltJump && salts["SALT_SKIP"].asNumber() == engine::kSaltSkip
               && salts["SALT_SPRAY"].asNumber() == engine::kSaltSpray && salts["SALT_DRIFT"].asNumber() == engine::kSaltDrift
               && salts["SALT_TINY"].asNumber() == engine::kSaltTiny && salts["SALT_START"].asNumber() == engine::kSaltStart))
        {
            why = "a salt differs";
            return false;
        }
        if (d["LANE_IDS"].size() != static_cast<std::size_t> (core::kNumLanes))
        {
            why = "LANE_IDS";
            return false;
        }
        for (int i = 0; i < core::kNumLanes; ++i)
            if (d["LANE_IDS"][i].asString() != core::kLanes[i].code || d["LANE_DEF"][i].asNumber() != core::kLanes[i].def)
            {
                why = "LANE_IDS / LANE_DEF";
                return false;
            }
        std::string w;
        if (! tableWithin (engine::hannTable(), engine::kHannN + 2, d["HANN"], 1, bitExact, w))
        {
            why = "HANN: " + w;
            return false;
        }
        if (! tableWithin (engine::fadeTable(), engine::kFadeN + 2, d["FADE"], 1, bitExact, w))
        {
            why = "FADE: " + w;
            return false;
        }
        return true;
    }
}

SL_TEST ("engine", fixtureEngineDefs)
{
    section ("engine/defs.json: every case (constants, budgets, salts, HANN and FADE tables, normInst clamps)");

    JsonValue doc;
    if (! fixture::load ("engine/defs.json", doc))
        return;
    const JsonValue& cases = doc["cases"];
    modelfx::FileTally tally ("engine/defs.json", cases.size());
    int defsCases = 0, normCases = 0;
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const JsonValue& k = cases[c];
        const std::string& kind = k["kind"].asString();
        if (kind == "defs")
        {
            ++defsCases;
            bool exact = true;
            std::string why;
            if (defsCaseMatches (k, exact, why))
                tally.pass (exact);
            else
                tally.fail ("defs: " + why);
        }
        else if (kind == "normInst")
        {
            ++normCases;
            const auto got = core::clampForEngine (enginefx::instFromEngineJson (k["input"]));
            const auto want = enginefx::instFromEngineJson (k["result"]);
            const std::string diff = enginefx::instDiff (got, want);
            if (diff.empty())
                tally.pass (true);
            else
                tally.fail ("normInst " + core::toJson (k["input"]) + " (got/want)" + diff);
        }
        else
        {
            tally.fail ("a case of an unknown kind: " + kind);
        }
    }
    tally.finish();
    check (defsCases == 1 && normCases == 5, fmt ("the file holds 1 defs case and 5 normInst cases (%d and %d)", defsCases, normCases));

    // A no-op is not a pass: the same comparison must catch a table one ulp further off than the tolerance.
    std::vector<float> wrong (engine::hannTable(), engine::hannTable() + engine::kHannN + 2);
    wrong[1000] = std::nextafter (std::nextafter (wrong[1000], 2.0f), 2.0f);
    bool exact = true;
    std::string why;
    const auto* defs = fixture::casesOfKind (doc, "defs").empty() ? nullptr : fixture::casesOfKind (doc, "defs")[0];
    const bool caught = defs != nullptr && ! tableWithin (wrong.data(), engine::kHannN + 2, (*defs)["HANN"], 1, exact, why);
    check (caught, "negative control: a HANN table with one value 2 ulps off fails the 1-ulp comparison (" + why + ")");
}
