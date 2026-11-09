#pragma once

// Shapes (Y2; sl-model.js "shapes"): ready-made node lists for a lane, put on every selected note (Each) or
// spread across the selection's time range (Across).

#include "Clip.h"
#include "ClipEdit.h"

#include "core/Inst.h"
#include "core/Lanes.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace sl::model
{
    /** SHAPE_KEYS in order: the 8 common keys, then Travel's Speed-kind keys, then its Path-kind keys. */
    enum class ShapeKey : std::uint8_t
    {
        up, down, swell, dip, squiggle, random, decay, reset,
        tapestop, tapestart, reversehalf, rewind,
        playthrough, playthroughback, fit
    };

    inline constexpr int kNumShapeKeys = 15;

    inline constexpr const char* kShapeKeyCodes[kNumShapeKeys] = {
        "up", "down", "swell", "dip", "squiggle", "random", "decay", "reset",
        "tapestop", "tapestart", "reversehalf", "rewind",
        "playthrough", "playthroughback", "fit"
    };

    /** SHAPE_NAMES, as the menus show them. */
    inline constexpr const char* kShapeKeyNames[kNumShapeKeys] = {
        "Ramp up", "Ramp down", "Swell", "Dip", "Squiggle", "Random", "Decay", "Reset",
        "Tape stop", "Tape start", "Reverse halfway", "Rewind",
        "Play through", "Play through backwards", "Fit (1x)"
    };

    const char* code (ShapeKey) noexcept;
    const char* shapeName (ShapeKey) noexcept;

    /** A saved key; "wobble" (v3's name) reads as squiggle. */
    std::optional<ShapeKey> parseShapeKey (std::string_view code) noexcept;

    /** SLModel.shapeKeyKind: the Travel kind a key belongs to (speed or path), or nothing for a common key. */
    std::optional<TravelKind> shapeKeyKind (ShapeKey key) noexcept;

    /** SLModel.shapeKeysFor: the common keys, plus the Travel kind's own keys on the Travel lane. */
    std::vector<ShapeKey> shapeKeysFor (core::LaneId lane, TravelKind kind = TravelKind::speed);

    enum class ShapeScope : std::uint8_t { each, across };

    //==============================================================================================
    /** v3's patternNodes shapes. */
    enum class PatternShape : std::uint8_t { sine, tri, saw, square, random };

    /** patternNodes' options. A NaN number is "not given" and takes its default (as SLModel's num ()). */
    struct PatternOpts
    {
        PatternShape shape = PatternShape::sine;
        double center = 0.5;
        double d0 = 0.3;
        double d1 = std::numeric_limits<double>::quiet_NaN();   // not given: d0
        double acc = 0.0;                                       // warp: x -> x^(1 / (1 + acc))
        double cycles = 3.0;                                    // at least 0.5
        double seed = 1.0;                                      // the random branch's seed (as `seed | 0`)
    };

    /** v3's patternNodes, its random branch seeded (PLUGIN-PLAN 1.7): seed = hash32 (seed) % 10000, then
        seed = (seed * 9301 + 49297) % 233280 per node. */
    NodeList patternNodes (const PatternOpts& o);

    /** SLModel.seededList: count values rand01 (hash32 (idh, lane, i, seed, 0x5a9e)). */
    std::vector<double> seededList (int count, std::uint32_t idh, int laneIndex, double seed);

    /** applyShape's and shapeNodes' options. A NaN number is "not given". */
    struct ShapeOpts
    {
        double seed = 1.0;                                            // Random's seed
        double count = std::numeric_limits<double>::quiet_NaN();     // Random's node count (5; at least 3)
        double cycles = std::numeric_limits<double>::quiet_NaN();    // Squiggle's cycles (3)
        double bpm = std::numeric_limits<double>::quiet_NaN();       // Fit: the tempo (120, clamped 20..400)
        double sampleSeconds = std::numeric_limits<double>::quiet_NaN();   // Fit: the sound's length (6)
        double from = std::numeric_limits<double>::quiet_NaN();      // Fit: where the line starts (0)
        std::optional<core::RegionSettings> region;                  // Fit: the Region (as given: not normalized)
        std::optional<core::Inst> inst;                              // Fit: the Region of this instrument, when no region is given
    };

    /** SLModel.fitNodes (Path kind 'fit'): the line rises so the head moves through the Region at exactly 1x
        for `beats` beats, wrapping (vertical edges) when the Region loops and holding at its end otherwise. */
    NodeList fitNodes (double beats, const ShapeOpts& o);

    /** SLModel.shapeNodes: the nodes of one key on one lane. kind is the note's Travel kind (read on the
        Travel lane only), idh the note id hash Random uses (0 for Across), beats the note or span length
        Fit uses. Nothing for a key that does not exist on that lane (a Speed key on a Path note). */
    std::optional<NodeList> shapeNodes (ShapeKey key, core::LaneId lane, const ShapeOpts& o, TravelKind kind,
                                        std::uint32_t idh, double beats);

    /** SLModel.applyShape: the shape on the lane of every selected note. Each fits it to every note; Across
        spans the selection's time range and gives each note its slice. On Travel a key of the other kind
        leaves that note alone. Generators are not touched (Reset resets the drawing only). Every selected
        note is a new copy (copy-on-write), the others are shared. Nothing when the key does not exist on
        that lane (a Travel-only key on another lane: the prototype throws). */
    std::optional<Clip> applyShape (const Clip& clip, const Ids& ids, core::LaneId lane, ShapeKey key, ShapeScope scope,
                                    const ShapeOpts& opts = {});
}
