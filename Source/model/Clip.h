#pragma once

// Notes and clips (BLUEPRINT 2.1, sl-model.js "notes and clips"; PLUGIN-PLAN 1.2, 1.3, 3).
//
// Versions are immutable: a Clip holds shared pointers to const Notes, and an edit copies only the notes
// it changes (ClipEdit.h). Untouched notes are shared between the old and the new clip, exactly as the
// prototype shares note objects, so never change a Note once a NotePtr points at it.
//
// JavaScript nulls: a "follow the instrument" setting is an empty std::optional; a missing link or key is
// an empty string (the prototype never keeps an empty id: it reads '' as null).

#include "Node.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "core/Rates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sl::model
{
    //==============================================================================================
    // Limits (PLUGIN-PLAN 3: one table of limits, used by the decoder and enforced by every edit).

    inline constexpr int kMaxClipNotes = 4096;
    inline constexpr std::size_t kMaxClipBytes = 32u * 1024u * 1024u;
    inline constexpr int kMaxLanesPerNote = 64;     // lane records per note in the CLIP chunk
    inline constexpr int kMaxChopNotes = 1024;      // chop's default maxNotes
    inline constexpr int kMinBars = 1, kMaxBars = 64;

    /** The shortest note the tools make: a 1/64 note, in beats (MIN_LEN). */
    inline constexpr double kMinLen = 0.0625;

    /** BEATS_PER_BAR: the prototype's bars are always 4 beats; the plugin's clip setting defaults to it. */
    inline constexpr int kDefaultBeatsPerBar = 4;

    //==============================================================================================
    // Generators (BLUEPRINT 2.1, Y9): a pattern or random line riding on a lane's drawing.

    enum class GenKind : std::uint8_t { pattern, random };
    enum class GenShape : std::uint8_t { squiggle, triangle, sawup, sawdown, square, randsteps, smoothrandom };
    enum class PerNote : std::uint8_t { same, different };

    inline constexpr const char* kGenKindCodes[] = { "pattern", "random" };
    inline constexpr const char* kGenShapeCodes[] = { "squiggle", "triangle", "sawup", "sawdown", "square", "randsteps", "smoothrandom" };
    inline constexpr const char* kPerNoteCodes[] = { "same", "different" };

    /** A generator as SLModel.normalizeGen returns it. Fields set by hand may be out of range (or NaN for
        "missing": a NaN d1 follows d0): normalizeGen (Generators.h) cleans them, as the prototype does
        before every use. */
    struct Gen
    {
        GenKind kind = GenKind::pattern;
        GenShape shape = GenShape::squiggle;
        core::RateId rate = core::RateId::r1_16T;   // BLUEPRINT 2.1 default
        double d0 = 0.3;                            // depth at the note start (lane units)
        double d1 = 0.3;                            // depth at the note end
        double warp = 0.0;                          // -0.9 .. 3, unlocked phase only
        bool lockToBeat = true;
        double amount = 0.5;                        // Random: 1 = +-0.5
        double smooth = 0.0;                        // Random: 0 steps .. 1 cosine glide
        PerNote perNote = PerNote::same;
        std::int32_t seed = 1;
        bool invert = false;                        // Flip mirrors the generator with the drawing
        std::string key;                            // the id 'different' hashes; empty = the note's own id
    };

    //==============================================================================================
    // Per-note sample settings (S7): empty = follow the instrument.

    enum class StartMode : std::uint8_t { region, carry, song, random };
    enum class TravelKind : std::uint8_t { speed, path };

    inline constexpr const char* kStartModeCodes[] = { "region", "carry", "song", "random" };
    inline constexpr const char* kTravelKindCodes[] = { "speed", "path" };

    struct StartSpec
    {
        StartMode mode = StartMode::region;
        double pos = 0.0;          // 0..1 of the Region
        double range = 0.25;       // Random's range
        std::int32_t seed = 1;     // Random's seed (Re-roll adds 1)
    };

    struct NoteSample
    {
        std::optional<core::Motion> motion;
        TravelKind travel = TravelKind::speed;
        StartSpec start;
        std::optional<bool> tape;
        std::optional<bool> rev;
        std::optional<core::RateId> jump;
        std::optional<core::Engine> engine;
        std::optional<bool> lock;
        bool restart = false;
    };

    const char* code (GenKind) noexcept;
    const char* code (GenShape) noexcept;
    const char* code (PerNote) noexcept;
    const char* code (StartMode) noexcept;
    const char* code (TravelKind) noexcept;

    std::optional<GenKind> parseGenKind (std::string_view) noexcept;
    std::optional<GenShape> parseGenShape (std::string_view) noexcept;
    std::optional<PerNote> parsePerNote (std::string_view) noexcept;
    std::optional<StartMode> parseStartMode (std::string_view) noexcept;
    std::optional<TravelKind> parseTravelKind (std::string_view) noexcept;

    //==============================================================================================
    /** A number unique for the process and never reused (PLUGIN-PLAN 1.3: the SnapshotCache key). Every Note
        object gets a fresh one when it is made, copied or assigned to, so a note whose content may have
        changed never keeps an old serial. (There is no move: a move copies, and takes a fresh one too.) */
    class NoteSerial
    {
    public:
        NoteSerial() noexcept : value (next()) {}
        NoteSerial (const NoteSerial&) noexcept : value (next()) {}
        NoteSerial& operator= (const NoteSerial&) noexcept { value = next(); return *this; }

        std::uint64_t get() const noexcept { return value; }

    private:
        static std::uint64_t next() noexcept;
        std::uint64_t value;
    };

    //==============================================================================================
    /** What a newer CLIP chunk gave a note that this version does not know (PLUGIN-PLAN 3, compatibility
        rules): kept so a re-save writes it back. Filled by the clip codec; null on every note it did not
        decode with such content. Operations that copy a note (Duplicate, Chop, Paste) copy the pointer. */
    struct KeptRecord
    {
        /** The bytes after the fields this version reads, written back after the known fields. */
        std::vector<std::uint8_t> trailing;

        /** The whole record as it was read, when it held a value this version does not know (an enum
            string, a node shape code above 3); empty otherwise. Written back while the decoded value is
            unchanged. */
        std::vector<std::uint8_t> original;
    };

    struct LaneExtras
    {
        KeptRecord lane;                 // the lane record
        NodeList decodedNodes;           // what the lane record decoded to (to tell whether it was edited)
        KeptRecord gen;                  // its generator record
        std::optional<Gen> decodedGen;
    };

    struct NoteExtras
    {
        std::vector<std::uint8_t> noteTrailing;              // after the note record's known fields
        KeptRecord sample;                                    // the sample settings record
        NoteSample decodedSample;
        std::array<LaneExtras, core::kNumLanes> lanes;        // registry order
        std::vector<std::vector<std::uint8_t>> unknownLanes; // lane records whose id is not one of the 10, verbatim, in order
    };

    /** The clip settings record's bytes past the fields this version reads. */
    struct ClipExtras
    {
        std::vector<std::uint8_t> settingsTrailing;
    };

    //==============================================================================================
    struct Note
    {
        std::string id;                    // UTF-8; hashed and compared as UTF-16 (core::idHash, jsStringLess)
        double p = 60.0;                   // MIDI pitch (60 = C3)
        double s = 0.0;                    // start, beats
        double l = 1.0;                    // length, beats
        double vel = 0.8;
        std::optional<double> glide;       // the note's own glide length in beats; empty = inst.glideTime
        double gc = 0.0;                   // glide curve -1..1
        std::string glideTo;               // explicit link to another note's id; empty = none

        std::array<NodeList, core::kNumLanes> c;            // the drawing, registry order (mkNote: flat at the default)
        std::array<std::optional<Gen>, core::kNumLanes> gen;
        NoteSample sample;

        NoteSerial serial;
        std::shared_ptr<const NoteExtras> extras;

        NodeList& lane (core::LaneId id) noexcept { return c[static_cast<std::size_t> (id)]; }
        const NodeList& lane (core::LaneId id) const noexcept { return c[static_cast<std::size_t> (id)]; }
        std::optional<Gen>& genOf (core::LaneId id) noexcept { return gen[static_cast<std::size_t> (id)]; }
        const std::optional<Gen>& genOf (core::LaneId id) const noexcept { return gen[static_cast<std::size_t> (id)]; }
    };

    using NotePtr = std::shared_ptr<const Note>;

    struct Clip
    {
        int bars = 4;
        int beatsPerBar = kDefaultBeatsPerBar;
        double offsetBeats = 0.0;
        bool loop = true;
        bool tieAcrossLoop = true;   // F3; the engine honours it from M8; parity tests set false
        std::vector<NotePtr> notes;
        std::shared_ptr<const ClipExtras> extras;
    };

    using ClipPtr = std::shared_ptr<const Clip>;

    //==============================================================================================
    /** SLModel.defaultSample (): everything follows the instrument, Speed travel, Start at the Region start. */
    NoteSample defaultSample() noexcept;

    /** SLModel.mkNote (p, s, l, id): vel 0.8, no glide, every lane flat at its default, no generators. Every
        C++ note is given its id (PLUGIN-PLAN 1.2: freshId is not ported). */
    Note mkNote (double p, double s, double l, std::string id);

    /** SLModel.newClip (bars): bars rounded (Math.round) and clamped 1..64 (4 when not finite), no notes. */
    Clip newClip (double bars = 4.0);

    /** SLModel.cloneNote: a deep copy (a fresh serial); a lane with no nodes becomes flat at its default.
        The extras pointer is shared. */
    Note cloneNote (const Note& n);

    /** SLModel.cloneClip: the same settings, every note cloned. */
    Clip cloneClip (const Clip& clip);

    /** SLModel.noteLane: the note's nodes on a lane, or the lane flat at its default when it has none. */
    const NodeList& noteLane (const Note& n, core::LaneId lane) noexcept;

    /** SLModel.timeCmp: by start, then pitch, then id in JavaScript string order. Negative = a first. */
    int timeCmp (const Note& a, const Note& b) noexcept;

    /** SLModel.timeOrder: the notes sorted by timeCmp (stable). */
    std::vector<NotePtr> timeOrder (std::vector<NotePtr> notes);

    /** The two notes hold the same content (everything but the serial and the extras pointer). */
    bool sameContent (const Note& a, const Note& b) noexcept;
    bool sameGen (const Gen& a, const Gen& b) noexcept;
    bool sameNodes (const NodeList& a, const NodeList& b) noexcept;
}
