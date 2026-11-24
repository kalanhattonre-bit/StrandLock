#pragma once

// The comparison of a render with an engine/renders.json case's "out" (PLUGIN-PLAN 5.3, section 5 tolerances): if
// the hash matches, bit for bit; otherwise windows within 1e-5, blockRms within 1e-6 + 1e-4 x rms, blockPeak within
// 1e-5, stats exact, display ids, kinds, engines, motions, released, dir and thinned exact and pos, level, env and
// beat within 1e-6, trace within 1e-6. Shared by test_parity_renders.cpp (M2, M3) and test_tie_loop.cpp (M8 W1).

#include "engine/EngineRender.h"

#include "core/JsonLite.h"

#include <string>

namespace sl::test::render
{
    /** One render against the fixture's out. hashMatch: the whole render is bit for bit the prototype's. False (why
        says where) when it is outside the tolerances. */
    bool renderMatches (const Rendered& r, const core::JsonValue& out, bool& hashMatch, std::string& why);
}
