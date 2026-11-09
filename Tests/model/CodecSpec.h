#pragma once

// A second writer of the CLIP payload, written from PLUGIN-PLAN 3's record table and nothing else (each record
// is built on its own and then given its length), for the codec tests: with no hooks it must give exactly
// model::encodeClip's bytes; with hooks it writes what a newer StrandLock might write (forward compatibility
// cases (b)-(d)).

#include "model/Clip.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sl::test::codecspec
{
    using Bytes = std::vector<std::uint8_t>;

    struct Hooks
    {
        /** Appended to every record: the clip settings, each note, sample settings, lane and generator (b). */
        Bytes trailing;

        /** Lane records written after the known lanes of note unknownLaneNote (c); before them when
            unknownLaneFirst. */
        int unknownLaneNote = -1;
        std::vector<Bytes> unknownLanes;
        bool unknownLaneFirst = false;

        /** In note oddNote: this motion string in place of the note's (d) ... */
        int oddNote = -1;
        std::string motion;
        /** ... and this shape code for the first node of lane oddLane (-1: none). */
        int oddLane = -1;
        std::uint8_t shapeCode = 0;
    };

    /** The payload of clip, written field by field from plan 3. pOffsets, when given, gets the offset of each
        note's p field. */
    Bytes write (const model::Clip& clip, const Hooks& hooks = {}, std::vector<std::size_t>* pOffsets = nullptr);

    /** A lane record of any id: count nodes (t = i / (count - 1), v, linear), optionally a generator. */
    Bytes laneRecord (const std::string& laneId, int count, bool withGen);

    /** Bytes a and b differ only inside [at, at + length) (and have the same size). */
    bool differOnlyIn (const Bytes& a, const Bytes& b, std::size_t at, std::size_t length);

    /** The offset of the first occurrence of text in bytes, or npos. */
    std::size_t find (const Bytes& bytes, const std::string& text);
}
