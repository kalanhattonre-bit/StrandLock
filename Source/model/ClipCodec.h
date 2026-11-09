#pragma once

// The CLIP chunk's payload (PLUGIN-PLAN 3): a clip as length-prefixed, append-only records, little-endian.
//
//   payload   u32 settingsLength | i32 bars | i32 beatsPerBar | f64 offsetBeats | u8 loop | u8 tieAcrossLoop | ...
//             u32 noteCount | note record x noteCount
//   note      u32 recordLength | str8 id | f64 p, s, l, vel, glide (NaN = null), gc | str8 glideTo ("" = null)
//             | sample record | u8 laneCount | lane record x laneCount | ...
//   sample    u16 recordLength | str8 motion | str8 travel | str8 start.mode | f64 start.pos | f64 start.range
//             | i32 start.seed | i8 tape | i8 rev | str8 jump | str8 engine | i8 lock | u8 restart | ...
//   lane      u32 recordLength | str8 laneId | u16 nodeCount | nodes (f64 t, f64 v, u8 shape, f64 bend)
//             | u8 hasGen | gen record if hasGen | ...
//   gen       u16 recordLength | str8 kind | str8 shape | str8 rate | f64 d0, d1, warp, amount, smooth
//             | u8 lockToBeat | str8 perNote | i32 seed | u8 invert | str8 key | ...
//
// A record's length counts the bytes after its own length field. str8 = u8 byte count + UTF-8. Enums are the
// prototype's strings ("" = null); i8 tri-states are -1 null, 0, 1. Every note is written with all 10 lanes in
// registry order, then the lane records this version does not know.
//
// Content this version does not know is kept in Note::extras and written back (the compatibility rules of
// plan 3): a record's trailing bytes; a lane record with an unknown id (or a second record of a known lane),
// verbatim; and a record holding a value this version does not know (an enum string, a node shape code above
// 3, a tri-state or flag outside its values) keeps its original bytes, which are written while the decoded
// value is unchanged. The decoder reads the fields it knows, gives missing trailing fields their defaults,
// and rebuilds every note through normalizeClip, so any payload it accepts becomes a valid clip.

#include "Clip.h"
#include "ClipEdit.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sl::model
{
    /** The CLIP chunk's version (PLUGIN-PLAN 3's table). */
    inline constexpr std::uint16_t kClipChunkVersion = 1;

    /** Lane records kept per note for lanes this version does not know (64 lanes per note in all). */
    inline constexpr int kMaxUnknownLanes = kMaxLanesPerNote - core::kNumLanes;

    /** The payload of the CLIP chunk for clip. Strings longer than 255 bytes (never made by the model: see
        kMaxIdBytes) are cut at a UTF-8 boundary; a lane of more than 65535 nodes is cut too. */
    std::vector<std::uint8_t> encodeClip (const Clip& clip);

    /** The bytes one note record takes (as encodeClip writes it). */
    std::size_t encodedNoteSize (const Note& note);

    /** The bytes before the first note record: the settings record and the note count. */
    std::size_t encodedHeaderSize (const Clip& clip);

    /** encodeClip (clip).size (), without building it. */
    std::size_t encodedClipSize (const Clip& clip);

    struct DecodeResult
    {
        std::optional<Clip> clip;   // nothing when the payload could not be read
        std::string error;          // why, in words
    };

    /** The clip a payload holds. Refused whole (nothing, with the reason): a payload above kMaxClipBytes,
        more than kMaxClipNotes notes, more than kMaxLanesPerNote lanes in a note, a record or a count that
        runs past the bytes holding it. Never reads outside [data, data + size), and validates every count
        against the bytes left before it allocates for it. */
    DecodeResult decodeClip (const std::uint8_t* data, std::size_t size);
    DecodeResult decodeClip (const std::vector<std::uint8_t>& bytes);

    //==============================================================================================
    /** The running encoded size of a document's clip versions (PLUGIN-PLAN 3: ClipDocument keeps one). Each
        note's size is cached by its serial; the cache keeps only the notes of the last clip it measured. */
    class ClipSizer
    {
    public:
        /** encodedClipSize (clip), measuring only the notes the last measured clip did not hold. */
        std::size_t measure (const Clip& clip);

        std::size_t cachedNotes() const noexcept { return sizes.size(); }

    private:
        std::unordered_map<std::uint64_t, std::size_t> sizes;
    };

    /** The limits table on an edit (PLUGIN-PLAN 3): the proposed clip is accepted when it holds at most
        kMaxClipNotes notes and encodes to at most kMaxClipBytes, with bytes set to its size. Otherwise the
        result is refused: the clip is before (every note shared), count is the note count the edit would have
        made, bytes its size (0 when the note cap already refused it), and no ids. A proposal that is already
        refused is returned as it is. */
    EditResult limitEdit (const Clip& before, EditResult proposed, ClipSizer& sizer);
    EditResult limitEdit (const Clip& before, Clip proposed, ClipSizer& sizer);
}
