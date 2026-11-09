#pragma once

// The selection tools (Y2-Y5; sl-model.js "connect" and "tools"): Connect, Disconnect, Walk, Chop, Reverse,
// Flip, More / Less, Select similar, Humanize, Strum and Legato. Each returns a new clip that shares every
// note it did not act on with the input (copy-on-write, ClipEdit.h); the input is never changed.

#include "Clip.h"
#include "ClipEdit.h"
#include "Node.h"

#include "core/Lanes.h"
#include "core/Rates.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sl::model
{
    //==============================================================================================
    // Connect (Y5, D5 = 'next')

    enum class ConnectMode : std::uint8_t { nearest, furthest, random };

    /** SLModel.connect: every selected note links (glideTo) to one note of the next chord in the selection
        (the earliest later starts within 0.05 beat, overlapping by at most 0.13 beat, within 24 semitones):
        the nearest in pitch, the furthest, or a seeded random one (rand01 (hash32 (idHash (n), idHash (m),
        seed))). A note takes at most one incoming link, links from outside the selection included. Links
        inside the selection are replaced; every selected note is a copy. */
    Clip connect (const Clip& clip, const Ids& ids, ConnectMode mode, double seed = 1.0);

    /** SLModel.disconnect: drops every link from a note in ids to a note in ids. */
    Clip disconnect (const Clip& clip, const Ids& ids);

    //==============================================================================================
    // Walk, Chop

    /** What Walk spreads: the Start position (mode set to region), the velocity, or a lane (made flat). */
    struct WalkField
    {
        enum class Kind : std::uint8_t { start, vel, lane };

        Kind kind = Kind::start;
        core::LaneId lane = core::LaneId::pitch;   // for Kind::lane

        static WalkField start() noexcept { return {}; }
        static WalkField vel() noexcept { return { Kind::vel, core::LaneId::pitch }; }
        static WalkField onLane (core::LaneId id) noexcept { return { Kind::lane, id }; }
    };

    /** SLModel.walk: from the first to the last selected note in time order, the field goes from `from` to
        `to` in equal steps (clamped 0..1). */
    Clip walk (const Clip& clip, const Ids& ids, WalkField field, double from, double to);

    /** Chop's Start step: each piece's Start moves by `step` (Region fractions, wrapped into 0..1; the piece's
        Start mode becomes region), or, with spread (the prototype's `true`), by 1 / pieces, so the pieces
        cover the Region evenly. 0 leaves the Start alone. */
    struct ChopStart
    {
        double step = 0.0;
        bool spread = false;

        static ChopStart by (double s) noexcept { return { s, false }; }
        static ChopStart evenly() noexcept { return { 0.0, true }; }
    };

    /** SLModel.chop: every selected note cut into pieces of `step` beats from its start (the last may be
        short). Piece 0 keeps the note's id (links into it stay); the note's own link moves to its last piece;
        the other pieces get ids uniqueId (taken, 'chop:' + id, k), Restart on, and their Different random
        generators keep the original note's values (key = its id). Refused (the clip unchanged, with the
        count) when the clip would hold more than maxNotes notes; maxNotes is at most kMaxClipNotes. A step
        that is not a positive finite number reads 1/16 (rateBeats). */
    EditResult chop (const Clip& clip, const Ids& ids, double step, ChopStart stepStart = {}, int maxNotes = kMaxChopNotes);
    EditResult chop (const Clip& clip, const Ids& ids, core::RateId rate, ChopStart stepStart = {}, int maxNotes = kMaxChopNotes);

    //==============================================================================================
    // Lane edits: Reverse, Flip, More / Less

    /** SLModel.reverseLane: the lane mirrored in time (reverseNodes) on every note in ids. */
    Clip reverseLane (const Clip& clip, const Ids& ids, core::LaneId lane);

    /** SLModel.flipLane: v -> 1 - v of what plays; the lane's generator flips with it (invert toggles). */
    Clip flipLane (const Clip& clip, const Ids& ids, core::LaneId lane);

    /** SLModel.scaleLane (More / Less): values move away from (factor > 1) or towards (< 1) the lane default;
        the generator's depths and amount scale too. On Travel (Speed kind) each direction scales around its
        own 1x (forward around 0.75, backward around 0.25) and never crosses the dead band. factor below 0
        reads 0; a non-finite factor reads 1. */
    Clip scaleLane (const Clip& clip, const Ids& ids, core::LaneId lane, double factor);

    //==============================================================================================
    // Select similar

    enum class SimilarKind : std::uint8_t { pitch, length, bar, everyOther, invert };

    /** SLModel.selectSimilar: ids in time order. pitch: the same p as refId; length: l within 1e-6; bar: starting
        in the same bar (the clip's beatsPerBar, 4 in the prototype); everyOther: every other note of ids (all
        notes when ids is every note or empty), keeping refId's parity; invert: the notes not in ids
        (Ids::all () here means no selection, the prototype's null, so it gives every note). */
    std::vector<std::string> selectSimilar (const Clip& clip, const std::string& refId, SimilarKind kind, const Ids& ids);

    //==============================================================================================
    // Humanize, Strum, Legato

    /** Humanize's amounts: the largest start shift (beats), velocity change and lane shift per lane (0 = none). */
    struct HumanizeAmounts
    {
        double time = 0.0;
        double vel = 0.0;
        std::array<double, core::kNumLanes> lanes {};

        /** The prototype's `lanes: number`: that amount on Volume and Tone. */
        HumanizeAmounts& volumeAndTone (double amount) noexcept
        {
            lanes[static_cast<std::size_t> (core::LaneId::amp)] = amount;
            lanes[static_cast<std::size_t> (core::LaneId::tone)] = amount;
            return *this;
        }
    };

    /** SLModel.humanize: seeded offsets, r (k) = rand01 (hash32 (idHash (id), k, seed)) * 2 - 1, k = 1 start, 2
        velocity, 10 + lane index each lane: start += r * time (at least 0), vel += r * vel (0.05..1), every
        node of a lane += r * amount (0..1). */
    Clip humanize (const Clip& clip, const Ids& ids, const HumanizeAmounts& amounts, double seed = 1.0);

    enum class StrumDir : std::uint8_t { up, down };

    /** SLModel.strum: each chord of the selection (starts within 0.05 beat of its first) is spread by one rate
        step per note, low to high (up) or high to low (down); every note keeps its end (and at least 1/64). */
    Clip strum (const Clip& clip, const Ids& ids, core::RateId rate, StrumDir dir);

    /** SLModel.legato: each selected note reaches the start of the next chord of the selection (at least 1/64). */
    Clip legato (const Clip& clip, const Ids& ids);
}
