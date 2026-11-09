#pragma once

// The plugin state's binary container (PLUGIN-PLAN 3). Little-endian, never XML or base64:
//
//   Header (16 bytes): 'S' 'T' 'L' 'K' | u16 formatVersion = 1 | u16 flags = 0 | u32 totalLength | u32 headerCrc32
//   Then chunks:       u32 tag (FourCC) | u16 chunkVersion | u16 flags | u32 payloadLength | u32 payloadCrc32
//                      | payload | zero pad to 4
//
// headerCrc32 is the CRC-32 of the 12 header bytes before it; payloadCrc32 that of the payload. Reading never
// trusts a length: every count and length is checked against the bytes really left before anything is
// allocated or read, so damaged or hostile data can make a load fail but never read out of bounds.
//
// What could not be read is kept and written back (PLUGIN-PLAN 3, Z4): KeptLayout remembers every chunk's
// original bytes in their order, and writes a chunk's original bytes again while the part of the state it
// holds is unchanged since the load (compared through its encoding). An unknown chunk is always written back.
// A blob that could not be read at all (newer formatVersion, wrong magic, bad header CRC, totalLength past the
// end) is written back whole while the project's content is unchanged: every chunk but UIST. UIST is the
// window's own state (tab, mode, fold, size, theme), so opening the editor or clicking a tab is not an edit.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sl::state
{
    using Bytes = std::vector<std::uint8_t>;

    /** A FourCC tag as it is stored: the four characters in order, read as a little-endian u32. */
    constexpr std::uint32_t fourCC (const char (&t)[5]) noexcept
    {
        return static_cast<std::uint32_t> (static_cast<unsigned char> (t[0]))
             | (static_cast<std::uint32_t> (static_cast<unsigned char> (t[1])) << 8)
             | (static_cast<std::uint32_t> (static_cast<unsigned char> (t[2])) << 16)
             | (static_cast<std::uint32_t> (static_cast<unsigned char> (t[3])) << 24);
    }

    inline constexpr char kMagic[4] = { 'S', 'T', 'L', 'K' };
    inline constexpr std::size_t kHeaderSize = 16;
    inline constexpr std::size_t kChunkHeaderSize = 16;

    inline constexpr std::uint32_t kTagParm = fourCC ("PARM");
    inline constexpr std::uint32_t kTagInst = fourCC ("INST");
    inline constexpr std::uint32_t kTagClip = fourCC ("CLIP");
    inline constexpr std::uint32_t kTagSmpl = fourCC ("SMPL");
    inline constexpr std::uint32_t kTagSaud = fourCC ("SAUD");
    inline constexpr std::uint32_t kTagUist = fourCC ("UIST");

    // The one table of limits (PLUGIN-PLAN 3): the decoder refuses past them, every edit stays under them.
    inline constexpr std::size_t kMiB = 1024 * 1024;
    inline constexpr std::size_t kMaxClipBytes = 32 * kMiB;
    inline constexpr std::size_t kMaxSaudBytes = 50'000'000;
    inline constexpr std::size_t kMaxOtherChunkBytes = 1 * kMiB;
    inline constexpr std::size_t kMaxTotalBytes = 32 * kMiB + 50'000'000 + 4 * kMiB;
    inline constexpr std::size_t kMaxStr8 = 255;
    inline constexpr std::size_t kMaxStr16 = 4096;

    /** The largest payload a chunk with this tag may have. */
    std::size_t payloadLimit (std::uint32_t tag) noexcept;

    /** "PARM" for a tag, with any byte that is not printable shown as '?'. */
    std::string tagName (std::uint32_t tag);

    //==============================================================================================
    /** Appends little-endian values to a byte vector. */
    class ByteWriter
    {
    public:
        explicit ByteWriter (Bytes& target) noexcept : out (target) {}

        void u8 (std::uint32_t v);
        void u16 (std::uint32_t v);
        void u32 (std::uint32_t v);
        void u64 (std::uint64_t v);
        void i8 (std::int32_t v)  { u8 (static_cast<std::uint32_t> (v) & 0xffu); }
        void i32 (std::int32_t v) { u32 (static_cast<std::uint32_t> (v)); }
        void f32 (float v);
        void f64 (double v);
        void bytes (const void* data, std::size_t size);
        void bytes (const Bytes& b) { bytes (b.data(), b.size()); }

        /** u8 length + UTF-8; a longer string is cut at 255 bytes, on a character boundary. */
        void str8 (std::string_view s);

        /** u16 length + UTF-8; a longer string is cut at 4096 bytes, on a character boundary. */
        void str16 (std::string_view s);

        std::size_t size() const noexcept { return out.size(); }

    private:
        Bytes& out;
    };

    /** Reads little-endian values from a byte range. A read past the end returns 0 (or an empty string),
        reads nothing, and clears ok() for good: check ok() once after a run of reads. */
    class ByteReader
    {
    public:
        ByteReader (const std::uint8_t* data, std::size_t size) noexcept : p (data), n (size) {}

        std::uint32_t u8() noexcept;
        std::uint32_t u16() noexcept;
        std::uint32_t u32() noexcept;
        std::uint64_t u64() noexcept;
        std::int32_t i8() noexcept;
        std::int32_t i32() noexcept;
        float f32() noexcept;
        double f64() noexcept;
        std::string str8();
        std::string str16();

        /** Points at the next size bytes and skips them; nullptr (and not ok) when fewer are left. */
        const std::uint8_t* take (std::size_t size) noexcept;

        bool ok() const noexcept { return good; }
        std::size_t remaining() const noexcept { return n - pos; }
        std::size_t position() const noexcept { return pos; }
        bool atEnd() const noexcept { return pos == n; }

        /** The bytes not read yet (an append-only record's fields this version does not know). */
        Bytes rest() const { return Bytes (p + pos, p + n); }

    private:
        bool need (std::size_t size) noexcept;

        const std::uint8_t* p;
        std::size_t n;
        std::size_t pos = 0;
        bool good = true;
    };

    //==============================================================================================
    /** Why a blob was not read as a container. */
    enum class HeaderStatus { ok, tooShort, badMagic, badHeaderCrc, badVersion, newerVersion, badLength };

    const char* describe (HeaderStatus status) noexcept;

    /** One chunk as found in a blob. The pointers point into the blob the parse was given. */
    struct FoundChunk
    {
        std::uint32_t tag = 0;
        std::uint16_t version = 0;
        std::uint16_t flags = 0;
        const std::uint8_t* payload = nullptr;
        std::size_t payloadLength = 0;
        const std::uint8_t* raw = nullptr;   // the chunk's own bytes: header, payload, padding
        std::size_t rawLength = 0;
        bool crcOk = false;
        bool withinLimits = false;   // payloadLength <= payloadLimit (tag)
        bool truncated = false;      // the bytes to the end of the container, kept because no whole chunk fits there
    };

    struct ParsedContainer
    {
        HeaderStatus status = HeaderStatus::tooShort;
        std::uint32_t formatVersion = 0;
        std::size_t totalLength = 0;
        std::vector<FoundChunk> chunks;   // in their order; a damaged end is the last, with truncated set
    };

    /** Reads the header and walks the chunks. Never reads outside data .. data + size. */
    ParsedContainer parseContainer (const void* data, std::size_t size);

    /** Appends one chunk (header, payload, its CRC, padding) to out. */
    void appendChunk (Bytes& out, std::uint32_t tag, std::uint16_t version, std::uint16_t flags, const Bytes& payload);

    /** A container of these chunk bytes (each already framed by appendChunk): the header, then the chunks. */
    Bytes makeContainer (const Bytes& chunks);

    //==============================================================================================
    /** The payloads of the chunks this version writes, for one moment of the state: tag -> payload. */
    using Payloads = std::map<std::uint32_t, Bytes>;

    /** The tags this version writes, in the order it writes them into a new container. */
    inline constexpr std::uint32_t kWrittenTags[] = { kTagParm, kTagInst, kTagSmpl, kTagUist };

    /** The chunk version this version writes for a tag. */
    std::uint16_t writtenVersion (std::uint32_t tag) noexcept;

    class KeptLayout
    {
    public:
        /** No load yet: a new container in the standard order. */
        KeptLayout() = default;

        /** After a blob that could not be read at all. atLoad = this version's payloads right after the load. */
        static KeptLayout unreadable (const void* data, std::size_t size, const Payloads& atLoad);

        /** After a container: one slot per found chunk, in order. decoded[i] tells whether chunk i was read into
            the state (the first chunk of a known tag, CRC and limits good, payload decoded). atLoad = this
            version's payloads right after the load. */
        static KeptLayout fromContainer (const ParsedContainer& parsed, const std::vector<bool>& decoded, const Payloads& atLoad);

        /** The bytes to save for the state whose payloads are now. */
        Bytes write (const Payloads& now) const;

        /** True while something that could not be read is still being written back because its part of the
            state is unchanged since the load: the "newer StrandLock / could not be read" banner. */
        bool keepsUnreadable (const Payloads& now) const;

        /** Unknown chunks kept (for the tests and the diagnostics). */
        int unknownChunkCount() const noexcept;

    private:
        struct Slot
        {
            std::uint32_t tag = 0;
            Bytes raw;                 // the chunk exactly as it was loaded
            bool written = false;      // a tag this version writes, and the first chunk with it
            bool decoded = false;      // read into the state
            Bytes atLoad;              // written: this version's payload for that part right after the load
            std::uint16_t version = 0, flags = 0;
            bool unreadable = false;   // kept because it could not be read (bad CRC, past a limit, damaged, undecodable)
            bool truncated = false;    // the cut end of the container: always written last, so it frames nothing after it
        };

        std::vector<Slot> slots;
        bool wholeBlob = false;
        Bytes blob;
        Payloads contentAtLoad;   // whole blob: this version's payloads right after the load, UIST left out

        static Bytes standardContainer (const Payloads& now);

        /** Whether a chunk holds the project's content (PARM, INST, SMPL; later CLIP and SAUD), not UIST. */
        static bool isProjectContent (std::uint32_t tag) noexcept;

        /** Whole blob: every content payload of now equals the one right after the load. */
        bool contentUnchanged (const Payloads& now) const;
    };
}
