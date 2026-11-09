#pragma once

// Generators (BLUEPRINT 2.1, Y3, Y4, Y9; sl-model.js "generators"): a pattern or a seeded random line added
// to a lane's drawing, and the lane table the engine plays. The random values follow the seed recipe of
// PLUGIN-PLAN 1.7 bit for bit; the table follows the two Float32 roundings of 1.6 rule 1.

#include "Clip.h"
#include "ClipEdit.h"

#include "core/Lanes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sl::model
{
    /** Generator depth 1 on the pitch lane = +-2 semitones (PITCH_GEN_SCALE). */
    inline constexpr double kPitchGenScale = 1.0 / 6.0;

    /** Turn into nodes: the RDP tolerance (BAKE_EPS), raised only when more than 128 nodes would be needed. */
    inline constexpr double kBakeEps = 0.004;

    /** A partial generator for makeGen: the fields that are set; the rest take normalizeGen's defaults (an
        unset d1 follows d0). */
    struct GenPatch
    {
        std::optional<GenShape> shape;
        std::optional<core::RateId> rate;
        std::optional<double> d0, d1, warp;
        std::optional<bool> lockToBeat;
        std::optional<double> amount, smooth;
        std::optional<PerNote> perNote;
        std::optional<std::int32_t> seed;
        std::optional<bool> invert;
        std::optional<std::string> key;
    };

    /** SLModel.normalizeGen on a typed generator: d0 0..1 (non-finite: 0.3), d1 0..1 (non-finite: d0),
        warp -0.9..3, amount 0..1 (0.5), smooth 0..1 (0); a kind, shape, rate or perNote outside its list
        takes the default (pattern, squiggle, 1/16T, same). */
    Gen normalizeGen (Gen g);

    /** SLModel.makeGen (kind, patch). */
    Gen makeGen (GenKind kind, const GenPatch& patch = {});

    /** SLModel.genEval: the generator as a function of note time t (0..1) -> offset in lane units, for one
        note on one lane. g must be normalized. Holds the prototype's one-step cache of the current random
        step (a speed-up only: every value is a pure function of the step). */
    class GenEval
    {
    public:
        GenEval (const Gen& g, const Note& note, core::LaneId lane);

        /** The offset at note time t. */
        double operator() (double t) const;

        /** The seeded random value of step k (PLUGIN-PLAN 1.7): rand01 (hash32 (...)) * 2 - 1. */
        double randomStep (double k) const noexcept;

        /** The phase at note time t, in rate steps: from the song beat (s + t l) / P when locked to the beat,
            else from the note start, warped: l t^(1 + warp) / P. The step is floor (phase + 1e-9). */
        double phase (double t) const noexcept;

    private:
        GenKind kind;
        GenShape shape;
        double rateBeats, start, length, d0, d1, warp, amount, smooth;
        bool lockToBeat;
        bool different;
        std::uint32_t idh;
        int laneIndex;
        std::int32_t seed;
        double sign, scale;
        mutable double cachedStep;
        mutable double cachedValue = 0.0;
    };

    /** SLModel.genOffset: what g adds to the drawing of note on lane at note time t (g is normalized first). */
    double genOffset (const Gen& g, const Note& note, core::LaneId lane, double t);

    /** SLModel.tableLength: max (2, ceil (l * res) + 1), or 2 when that is not finite. */
    double tableLength (double l, double res) noexcept;

    /** SLModel.laneTable: the lane as the engine plays it, tableLength (note.l, res) Float32 values: the drawing
        sampled (rounded to Float32), plus the generator in double, clamped 0..1 and rounded to Float32 again.
        res below 1 reads 1; a non-finite res reads 256. */
    std::vector<float> laneTable (const Note& note, core::LaneId lane, double res = 256.0);

    /** SLModel.setGen: a copy of the normalized generator on the lane of every note in ids (nullopt clears it). */
    Clip setGen (const Clip& clip, const Ids& ids, core::LaneId lane, const std::optional<Gen>& gen);

    /** SLModel.clearGen: clears one lane's generator (or every lane's, when lane is empty). */
    Clip clearGen (const Clip& clip, const Ids& ids, std::optional<core::LaneId> lane);

    /** SLModel.bakeGen (Turn into nodes): the table at res (at least 8; 256 when not finite) simplified by RDP
        (eps 0.004, raised by 1.25x while more than 128 points remain) into linear nodes; the generator is
        removed. A note without a generator on that lane is copied unchanged. */
    Clip bakeGen (const Clip& clip, const Ids& ids, core::LaneId lane, double res = 256.0);

    /** SLModel.reroll: seed + 1 (int32 wrap) on the generators of one lane, or of every lane when lane is
        empty, and then also on the seed of every 'random' Start. */
    Clip reroll (const Clip& clip, const Ids& ids, std::optional<core::LaneId> lane);
}
