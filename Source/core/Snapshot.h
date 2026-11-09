#pragma once

// The compiled clip the audio thread reads (PLUGIN-PLAN 1.4, BLUEPRINT 3.2 extended). model::compileSnapshot
// makes it; the engine reads it. It holds plain numbers, raw table pointers and index links, so the audio
// thread never touches a string, a map or a reference count.
//
// What changed from the JS snapshot: the JS bakes the instrument's defaults into each note and compiles the
// chains of the current Auto Slide mode. Those inst values are automatable parameters in the plugin, so a note
// here keeps "follow the instrument" markers and the glides and chains of all three Auto Slide modes, and
// resolve () turns them into what the JS snapshot holds, given the instrument of the moment.

#include "Inst.h"
#include "Lanes.h"
#include "Version.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sl::core
{
    /** One lane table: Float32 values 0..1, len >= 2. A flat lane (every value equal) is stored as 2 values;
        the engine's lookup tb[i0] + f * (tb[i1] - tb[i0]) gives the same float either way. */
    struct LaneTab
    {
        const float* data = nullptr;
        std::int32_t len = 0;
    };

    /** The note's Start (S7): mode is a model::StartMode (region, carry, song, random). */
    struct StartSnap
    {
        std::uint8_t mode = 0;
        double pos = 0.0;
        double range = 0.25;
        std::int32_t seed = 1;
    };

    /** The glide out of a note in one Auto Slide mode. Its length is resolved at the tick (resolve ()). */
    struct GlideSnap
    {
        bool present = false;
        double d = 0.0;               // semitones
        double curve = 0.0;
        std::int32_t toIndex = -1;    // the target note's index in Snapshot::notes
    };

    /** Where a note sits in its chain (one voice per chain) in one Auto Slide mode. -1 = none. */
    struct ChainSnap
    {
        std::int32_t firstIndex = -1;   // the chain's first note (the JS chain.id), an index in Snapshot::notes
        std::int32_t index = 0;         // the note's place in the chain
        std::int32_t prevIndex = -1;
        std::int32_t nextIndex = -1;
    };

    /** Follow markers: -1 = follow the instrument. */
    inline constexpr std::int8_t kFollow = -1;

    struct NoteSnap
    {
        std::uint32_t idHash = 0;         // FNV-1a over the id's UTF-16 code units
        std::int32_t idIndex = -1;        // the id string in Snapshot::ids
        double p = 60.0, s = 0.0, l = 1.0, vel = 0.8;
        std::int8_t engine = kFollow;     // an Engine, or kFollow
        std::int8_t lock = kFollow;       // 0, 1 or kFollow
        std::int8_t motion = kFollow;     // a Motion, or kFollow
        std::int8_t tape = kFollow;       // 0, 1 or kFollow
        std::int8_t rev = kFollow;        // 0, 1 or kFollow
        std::uint8_t travel = 0;          // 0 speed, 1 path (the note's kind, even on Tape)
        double jumpBeats = 0.0;           // NaN = follow inst.jump
        bool restart = false;
        StartSnap start;
        double glide = 0.0;               // the note's own glide length (beats), NaN = follow inst.glideTime
        GlideSnap glides[3];              // one per Auto Slide mode: [0] off, [1] legato, [2] always
        ChainSnap chains[3];
        LaneTab lanes[kNumLanes];         // registry order: the drawing plus its generator, Float32, 0..1
    };

    /** One note's 10 lane tables. tables[k] holds 2 values when the lane is flat; fullLength[k] is the length
        the prototype's table has (max (2, ceil (l * res) + 1)), which the fixtures compare. */
    struct NoteTables
    {
        std::array<std::vector<float>, kNumLanes> lanes;
        std::array<std::int32_t, kNumLanes> fullLength {};
    };

    struct Snapshot
    {
        std::uint32_t version = static_cast<std::uint32_t> (kSnapshotVersion);
        std::int32_t bars = 4, beatsPerBar = 4, res = 256;
        double offsetBeats = 0.0;
        bool loop = true, tieAcrossLoop = true;
        std::vector<NoteSnap> notes;                          // sorted by (s, p, id), as compileSnapshot
        std::vector<std::string> ids;                         // UTF-8, ids[i] is notes[i]'s id
        std::vector<std::pair<std::uint32_t, std::int32_t>> byHash;   // (idHash, index), sorted, for following a voice by id
        std::vector<std::shared_ptr<const NoteTables>> tables;       // tables[i] is notes[i]'s; NoteSnap::lanes point into them
        std::uint64_t epoch = 0, clipVersion = 0;
    };

    /** A note's settings resolved with an instrument: exactly what the JS snapshot baked in. */
    struct ResolvedNote
    {
        Engine engine = Engine::voice;
        bool lock = false;
        Motion motion = Motion::forward;
        bool tape = false;
        bool rev = false;
        double jumpBeats = 0.25;
        bool hasGlide = false;
        double glideD = 0.0, glideLen = 0.0, glideCurve = 0.0;
        std::int32_t glideTo = -1;
        ChainSnap chain;
    };

    /** PLUGIN-PLAN 1.4: engine = the note's or inst.engine; lock only on Cloud (the note's or inst.lock);
        motion, tape and rev the note's or the instrument's; jump = rateBeats (inst.jump) when the note follows;
        the glide and chain of inst.glideMode, and the glide length min (max (0, the note's glide or
        inst.glideTime), l * 0.95). A glideMode outside the three reads legato. */
    ResolvedNote resolve (const NoteSnap& note, const Inst& inst) noexcept;
}
