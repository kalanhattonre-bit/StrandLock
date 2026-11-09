#pragma once

// The JSON boundary of the model (PLUGIN-PLAN 1.2: model/Normalize). Prototype saves, v3 saves and fixture
// inputs go through the prototype's own normalize rules, with JavaScript's coercions: a value of the wrong
// type reads as missing, a missing number takes its default, truthiness where the prototype uses `!!x`.
//
// normalizeNote and normalizeClip also take typed notes: the clip presets, and later the clip codec, rebuild
// their notes through them, so every clip is made valid by the same rules.

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

    //==============================================================================================
    /** SLModel.normalizeSample on a typed value: pos 0..1 (non-finite: 0), range 0..1 (0.25); a choice that is
        not one of its list (an enum cast from a bad integer) reads null, Speed or Region. */
    NoteSample normalizeSample (NoteSample s) noexcept;

    /** SLModel.normalizeNote (n, i) on a typed note: p 0..127 (60), s at least 0 (0), l at least 1e-3 (1), vel
        0..1 (0.8), glide at least 0 (a NaN glide reads 0), gc -1..1 (0), every lane normalizeNodes (lane
        default), every generator normalizeGen, the sample normalizeSample. An empty id is made from the index
        and position: 'n' + base 36 of hash32 (i, round (p * 100), round (s * 1e6), round (l * 1e6)). An id of
        more than 255 UTF-8 bytes, which the CLIP chunk cannot hold, is made the same way, and a link that long
        is dropped (PLUGIN-PLAN 8.2 #27). Otherwise the link is kept as it is (normalizeClip drops a dangling
        one). A new Note (a fresh serial); extras are shared. */
    Note normalizeNote (const Note& n, int index);

    /** SLModel.normalizeClip on a typed clip: bars 1..64, every note normalizeNote (n, its index), a repeated
        id renamed id~1, id~2, ... (when that would pass 255 bytes, the missing-id rule's id, renamed the same
        way if it is taken), and a link to a missing note or to itself dropped. The clip settings are kept,
        with beatsPerBar clamped 1..64 and a non-finite offsetBeats read as 0 (the prototype has neither
        setting); the extras are shared. Every note is a new Note. */
    Clip normalizeClip (const Clip& clip);

    /** SLModel.normalizeNote on JSON (any note-like object, v3 notes too): a string id, or a number's
        String (n); a missing or wrong-typed field takes its default; then normalizeNote. */
    Note noteFromJson (const core::JsonValue& note, int index);

    /** SLModel.normalizeClip on JSON: a v4 clip, a {bars, notes} object or a v3 notes array. */
    Clip clipFromJson (const core::JsonValue& data);
}
