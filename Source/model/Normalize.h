#pragma once

// The JSON boundary of the model (PLUGIN-PLAN 1.2: model/Normalize). Prototype saves, v3 saves and fixture
// inputs go through the prototype's own normalize rules, with JavaScript's coercions: a value of the wrong
// type reads as missing, a missing number takes its default, truthiness where the prototype uses `!!x`.
//
// M1 part 1 has the pieces below; normalizeNote and normalizeClip (missing ids, renamed duplicates,
// dangling links) are added by a later part.

#include "Clip.h"
#include "Node.h"

#include "core/JsonLite.h"

#include <optional>

namespace sl::model
{
    /** A prototype node list as normalizeNodes reads it, before its cleaning: an element that is not an object
        reads as {}; t, v and bend are numbers or NaN (missing); the shape is one of the four names, else
        'linear' when lin is truthy (v3), else smooth. Not an array: an empty list. Pass the result to
        normalizeNodes (nodes, def). */
    NodeList nodesFromJson (const core::JsonValue& nodes);

    /** SLModel.normalizeGen on JSON: nothing for a value that is not an object (or array); else the cleaned
        generator (kind 'random' or pattern, a known shape or squiggle, a known rate or 1/16T, depths, warp,
        lockToBeat !== false, perNote 'different' or same, seed Math.trunc | 0, invert === true, a non-empty
        string key or none). */
    std::optional<Gen> genFromJson (const core::JsonValue& gen);

    /** SLModel.normalizeSample on JSON: a known motion or none, Path when travel is 'path', the Start (a known
        mode or region, pos 0..1, range 0..1 (0.25), seed Math.trunc | 0), tape / rev / lock true, false or
        none, a known jump rate or none, a known engine or none, restart truthy. */
    NoteSample sampleFromJson (const core::JsonValue& sample);

    /** JavaScript truthiness of a JSON value (`!!x`). */
    bool jsTruthy (const core::JsonValue& v) noexcept;

    /** A JSON value as SLModel's num () sees it: the number, or NaN for anything that is not a number. */
    double jsonNumber (const core::JsonValue& v) noexcept;
}
