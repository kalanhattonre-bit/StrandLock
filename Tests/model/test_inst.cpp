// core::Inst on the model's side (PLUGIN-PLAN 1.2: mergeInst, normalizeInst, instFromJson, instToJson):
// the instrument cases of model/normalize.json (garbage, non-finite, wrong types, seeds past int32) and the
// full instruments of model/presets.json, exactly. The clip cases of normalize.json are in test_normalize.cpp.

#include "ModelFixture.h"

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <array>
#include <cmath>
#include <string>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    using test::modelfx::jsonSame;
}

SL_TEST ("model", instJson)
{
    section ("Inst: instFromJson + normalizeInst + instToJson against model/normalize.json and model/presets.json");

    JsonValue normalize, presets;
    if (! fixture::load ("model/normalize.json", normalize) || ! fixture::load ("model/presets.json", presets))
        return;

    // normalize.json, kind "inst": the prototype's normalizeInst of each raw input
    const auto cases = fixture::casesOfKind (normalize, "inst");
    int bad = 0;
    for (const auto* c : cases)
    {
        std::string why;
        const JsonValue got = core::instToJson (core::instFromJson ((*c)["input"]));
        if (! jsonSame (got, (*c)["result"], "inst", why))
        {
            note ("differs (" + (*c)["label"].asString() + "): " + why);
            ++bad;
        }
    }
    check (cases.size() == 6 && bad == 0, fmt ("normalize.json: %d of %d instrument cases exactly", static_cast<int> (cases.size()) - bad,
                                               static_cast<int> (cases.size())));

    // presets.json's full instruments read back to themselves, and normalizeInst leaves them unchanged
    const JsonValue& insts = presets["insts"];
    int roundTrips = 0;
    for (std::size_t i = 0; i < insts.size(); ++i)
    {
        std::string why;
        const core::Inst inst = core::instFromJson (insts.valueAt (i));
        const bool same = jsonSame (core::instToJson (inst), insts.valueAt (i), insts.keyAt (i), why)
                       && jsonSame (core::instToJson (core::normalizeInst (inst)), insts.valueAt (i), insts.keyAt (i), why);
        if (! same)
            note ("differs: " + why);
        roundTrips += same ? 1 : 0;
    }
    check (insts.size() == 3 && roundTrips == 3, fmt ("presets.json: %d of %d instruments read back exactly and are normalized", roundTrips,
                                                      static_cast<int> (insts.size())));

    std::string why;
    check (jsonSame (core::instToJson (core::defaultInst()), insts["default"], "default", why), "instToJson (defaultInst ()) is SLModel.defaultInst () " + why);
}

SL_TEST ("model", instMergeNormalize)
{
    section ("Inst: mergeInst merges nested settings field by field; normalizeInst clamps the model's ranges");

    core::InstPatch patch;
    patch.speed = 3.0;
    patch.refresh.ms = 300.0;
    patch.region.end = 0.5;
    patch.mod2.target = core::kModOff;
    patch.macros = std::array<double, core::kNumMacros> { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8 };
    const core::Inst d = core::defaultInst();
    const core::Inst m = core::mergeInst (d, patch);
    check (fixture::same (m.speed, 3.0) && fixture::same (m.refresh.ms, 300.0) && m.refresh.mode == d.refresh.mode && m.refresh.rate == d.refresh.rate
               && fixture::same (m.region.end, 0.5) && fixture::same (m.region.start, 0.0) && m.mod2.target == core::kModOff && m.mod2.cc == 2
               && fixture::same (m.macros[7], 0.8) && fixture::same (m.heads, d.heads) && m.engine == d.engine,
           "mergeInst: the patched fields change, the rest of each nested setting stays, macros replace whole");

    core::Inst raw = d;
    raw.speed = 99.0;
    raw.heads = std::nan ("");
    raw.region.start = 0.9;
    raw.region.end = 0.1;
    raw.engine = static_cast<core::Engine> (7);
    raw.key = 14;
    raw.mod1.target = 42;
    raw.mod1.cc = 300;
    raw.vibSync = 99;
    raw.polyphony = 40.4;
    raw.grainLock = 20.0;
    raw.outGain = 20.0;
    const core::Inst n = core::normalizeInst (raw);
    check (fixture::same (n.speed, 16.0) && fixture::same (n.heads, 1.0) && fixture::same (n.region.start, 0.1) && fixture::same (n.region.end, 0.9)
               && n.engine == core::Engine::voice && n.key == 11 && n.mod1.target == d.mod1.target && n.mod1.cc == 127
               && n.vibSync == core::kVibSyncFree && fixture::same (n.polyphony, 32.0) && fixture::same (n.grainLock, 16.0) && fixture::same (n.outGain, 12.0),
           "normalizeInst: speed 99 -> 16, NaN heads -> 1, the Region swapped, a bad engine -> voice, key 14 -> 11, "
           "a bad mod target -> its default, cc 300 -> 127, polyphony 40.4 -> 32, grainLock 20 -> 16, outGain 20 -> 12 (the model's ranges)");

    // The engine's clamps stay wider (8.2 #18): the same values through clampForEngine.
    const core::Inst e = core::clampForEngine (raw);
    check (fixture::same (e.grainLock, 20.0) && fixture::same (e.outGain, 20.0) && fixture::same (n.grainLock, 16.0),
           "clampForEngine keeps grainLock 20 and outGain 20, which normalizeInst clamps");
}
