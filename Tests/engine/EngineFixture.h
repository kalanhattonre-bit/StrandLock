#pragma once

// Reading the engine fixtures (Tests/fixtures/engine/*.json) into engine types.

#include "core/Inst.h"
#include "core/JsonLite.h"

#include <string>

namespace sl::test::enginefx
{
    /** An instrument as the prototype engine's normInst receives it from a fixture's JSON: what JavaScript does
        to the values before normInst runs. A non-number becomes NaN (normInst's fin () then takes the default), an
        unknown choice string becomes a value outside its enum (core::clampForEngine then takes the engine's
        fallback), a bool field takes JavaScript truthiness, refresh.lockToBeat is `!== false`, and seed is
        `fin (x, 1) | 0`. A missing field keeps defaultInst ()'s value; null or a non-object is the defaults. (The
        same coercion as Tests/model/test_contract.cpp's spot check, kept here for the engine group.) */
    core::Inst instFromEngineJson (const core::JsonValue& src);

    /** The fields where a and b differ, as text ("speed -16/1 ..."); empty when they are equal field for field
        (+0 equals -0, NaN equals NaN). */
    std::string instDiff (const core::Inst& a, const core::Inst& b);
}
