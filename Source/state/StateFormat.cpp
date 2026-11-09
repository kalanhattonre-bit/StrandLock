#include "StateFormat.h"

#include "Crc32.h"
#include "core/Version.h"

#include <algorithm>
#include <set>

namespace sl::state
{
    namespace
    {
        std::uint32_t le16 (const std::uint8_t* p) noexcept
        {
            return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8);
        }

        std::uint32_t le32 (const std::uint8_t* p) noexcept
        {
            return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8)
                 | (static_cast<std::uint32_t> (p[2]) << 16) | (static_cast<std::uint32_t> (p[3]) << 24);
        }

        std::size_t padTo4 (std::size_t n) noexcept { return (4 - n % 4) % 4; }

        bool isWrittenTag (std::uint32_t tag) noexcept
        {
            return std::find (std::begin (kWrittenTags), std::end (kWrittenTags), tag) != std::end (kWrittenTags);
        }

        /** The longest prefix of s of at most limit bytes that does not cut a UTF-8 sequence. */
        std::string_view cutUtf8 (std::string_view s, std::size_t limit) noexcept
        {
            if (s.size() <= limit)
                return s;
            std::size_t n = limit;
            while (n > 0 && (static_cast<unsigned char> (s[n]) & 0xc0u) == 0x80u)
                --n;
            return s.substr (0, n);
        }

        const Bytes& payloadOf (const Payloads& p, std::uint32_t tag)
        {
            static const Bytes none;
            const auto it = p.find (tag);
            return it != p.end() ? it->second : none;
        }
    }

    std::size_t payloadLimit (std::uint32_t tag) noexcept
    {
        if (tag == kTagClip)
            return kMaxClipBytes;
        if (tag == kTagSaud)
            return kMaxSaudBytes;
        return kMaxOtherChunkBytes;
    }

    std::string tagName (std::uint32_t tag)
    {
        std::string s;
        for (int i = 0; i < 4; ++i)
        {
            const char c = static_cast<char> ((tag >> (8 * i)) & 0xffu);
            s += (c >= 0x20 && c < 0x7f) ? c : '?';
        }
        return s;
    }

    //==============================================================================================
    void ByteWriter::u8 (std::uint32_t v)  { out.push_back (static_cast<std::uint8_t> (v & 0xffu)); }
    void ByteWriter::u16 (std::uint32_t v) { u8 (v); u8 (v >> 8); }
    void ByteWriter::u32 (std::uint32_t v) { u16 (v & 0xffffu); u16 (v >> 16); }
    void ByteWriter::u64 (std::uint64_t v) { u32 (static_cast<std::uint32_t> (v & 0xffffffffu)); u32 (static_cast<std::uint32_t> (v >> 32)); }

    void ByteWriter::f32 (float v)
    {
        std::uint32_t b = 0;
        std::memcpy (&b, &v, sizeof (b));
        u32 (b);
    }

    void ByteWriter::f64 (double v)
    {
        std::uint64_t b = 0;
        std::memcpy (&b, &v, sizeof (b));
        u64 (b);
    }

    void ByteWriter::bytes (const void* data, std::size_t size)
    {
        const auto* b = static_cast<const std::uint8_t*> (data);
        out.insert (out.end(), b, b + size);
    }

    void ByteWriter::str8 (std::string_view s)
    {
        const auto cut = cutUtf8 (s, kMaxStr8);
        u8 (static_cast<std::uint32_t> (cut.size()));
        bytes (cut.data(), cut.size());
    }

    void ByteWriter::str16 (std::string_view s)
    {
        const auto cut = cutUtf8 (s, kMaxStr16);
        u16 (static_cast<std::uint32_t> (cut.size()));
        bytes (cut.data(), cut.size());
    }

    //==============================================================================================
    bool ByteReader::need (std::size_t size) noexcept
    {
        if (! good || size > n - pos)
        {
            good = false;
            return false;
        }
        return true;
    }

    std::uint32_t ByteReader::u8() noexcept
    {
        if (! need (1))
            return 0;
        return p[pos++];
    }

    std::uint32_t ByteReader::u16() noexcept
    {
        if (! need (2))
            return 0;
        const auto v = le16 (p + pos);
        pos += 2;
        return v;
    }

    std::uint32_t ByteReader::u32() noexcept
    {
        if (! need (4))
            return 0;
        const auto v = le32 (p + pos);
        pos += 4;
        return v;
    }

    std::uint64_t ByteReader::u64() noexcept
    {
        if (! need (8))
            return 0;
        const std::uint64_t lo = le32 (p + pos), hi = le32 (p + pos + 4);
        pos += 8;
        return lo | (hi << 32);
    }

    std::int32_t ByteReader::i8() noexcept
    {
        const auto v = u8();
        return v >= 0x80u ? static_cast<std::int32_t> (v) - 0x100 : static_cast<std::int32_t> (v);
    }

    std::int32_t ByteReader::i32() noexcept
    {
        return static_cast<std::int32_t> (u32());
    }

    float ByteReader::f32() noexcept
    {
        const std::uint32_t b = u32();
        float v = 0.0f;
        std::memcpy (&v, &b, sizeof (v));
        return v;
    }

    double ByteReader::f64() noexcept
    {
        const std::uint64_t b = u64();
        double v = 0.0;
        std::memcpy (&v, &b, sizeof (v));
        return v;
    }

    std::string ByteReader::str8()
    {
        const std::size_t length = u8();
        const auto* b = take (length);
        return b != nullptr ? std::string (reinterpret_cast<const char*> (b), length) : std::string();
    }

    std::string ByteReader::str16()
    {
        const std::size_t length = u16();
        if (length > kMaxStr16)
        {
            good = false;
            return {};
        }
        const auto* b = take (length);
        return b != nullptr ? std::string (reinterpret_cast<const char*> (b), length) : std::string();
    }

    const std::uint8_t* ByteReader::take (std::size_t size) noexcept
    {
        if (! need (size))
            return nullptr;
        const auto* b = p + pos;
        pos += size;
        return b;
    }

    //==============================================================================================
    const char* describe (HeaderStatus s) noexcept
    {
        switch (s)
        {
            case HeaderStatus::ok:           return "a StrandLock state";
            case HeaderStatus::tooShort:     return "too short for a StrandLock state";
            case HeaderStatus::badMagic:     return "not a StrandLock state (wrong magic)";
            case HeaderStatus::badHeaderCrc: return "a damaged header (CRC)";
            case HeaderStatus::badVersion:   return "format version 0";
            case HeaderStatus::newerVersion: return "saved by a newer StrandLock (format version)";
            case HeaderStatus::badLength:    return "a total length past the end of the data";
        }
        return "unknown";
    }

    ParsedContainer parseContainer (const void* data, std::size_t size)
    {
        ParsedContainer out;
        const auto* p = static_cast<const std::uint8_t*> (data);
        if (p == nullptr || size < kHeaderSize)
        {
            out.status = HeaderStatus::tooShort;
            return out;
        }
        if (std::memcmp (p, kMagic, sizeof (kMagic)) != 0)
        {
            out.status = HeaderStatus::badMagic;
            return out;
        }
        if (le32 (p + 12) != crc32 (p, 12))
        {
            out.status = HeaderStatus::badHeaderCrc;
            return out;
        }

        out.formatVersion = le16 (p + 4);
        if (out.formatVersion == 0)
        {
            out.status = HeaderStatus::badVersion;
            return out;
        }
        if (out.formatVersion > static_cast<std::uint32_t> (core::kStateFormatVersion))
        {
            out.status = HeaderStatus::newerVersion;
            return out;
        }

        const std::size_t total = le32 (p + 8);
        if (total < kHeaderSize || total > size || total > kMaxTotalBytes)
        {
            out.status = HeaderStatus::badLength;
            return out;
        }

        out.status = HeaderStatus::ok;
        out.totalLength = total;

        std::size_t pos = kHeaderSize;
        while (pos < total)
        {
            const std::size_t left = total - pos;
            FoundChunk c;
            c.raw = p + pos;

            bool whole = left >= kChunkHeaderSize;
            if (whole)
            {
                c.tag = le32 (p + pos);
                c.version = static_cast<std::uint16_t> (le16 (p + pos + 4));
                c.flags = static_cast<std::uint16_t> (le16 (p + pos + 6));
                c.payloadLength = le32 (p + pos + 8);
                const std::size_t room = left - kChunkHeaderSize;
                whole = c.payloadLength <= room && padTo4 (c.payloadLength) <= room - c.payloadLength;
            }
            else if (left >= 4)
            {
                c.tag = le32 (p + pos);
            }

            if (! whole)
            {
                // No whole chunk fits in what is left: keep those bytes as they are, as the last slot.
                c.payload = nullptr;
                c.payloadLength = 0;
                c.rawLength = left;
                c.truncated = true;
                out.chunks.push_back (c);
                break;
            }

            c.payload = p + pos + kChunkHeaderSize;
            c.crcOk = le32 (p + pos + 12) == crc32 (c.payload, c.payloadLength);
            c.withinLimits = c.payloadLength <= payloadLimit (c.tag);
            c.rawLength = kChunkHeaderSize + c.payloadLength + padTo4 (c.payloadLength);
            out.chunks.push_back (c);
            pos += c.rawLength;
        }
        return out;
    }

    void appendChunk (Bytes& out, std::uint32_t tag, std::uint16_t version, std::uint16_t flags, const Bytes& payload)
    {
        ByteWriter w (out);
        w.u32 (tag);
        w.u16 (version);
        w.u16 (flags);
        w.u32 (static_cast<std::uint32_t> (payload.size()));
        w.u32 (crc32 (payload.data(), payload.size()));
        w.bytes (payload);
        for (std::size_t i = 0; i < padTo4 (payload.size()); ++i)
            w.u8 (0);
    }

    Bytes makeContainer (const Bytes& chunks)
    {
        Bytes out;
        out.reserve (kHeaderSize + chunks.size());
        ByteWriter w (out);
        w.bytes (kMagic, sizeof (kMagic));
        w.u16 (static_cast<std::uint32_t> (core::kStateFormatVersion));
        w.u16 (0);
        w.u32 (static_cast<std::uint32_t> (kHeaderSize + chunks.size()));
        w.u32 (crc32 (out.data(), 12));
        w.bytes (chunks);
        return out;
    }

    std::uint16_t writtenVersion (std::uint32_t) noexcept
    {
        return 1;   // every chunk this version writes is version 1 (PLUGIN-PLAN 3)
    }

    //==============================================================================================
    Bytes KeptLayout::standardContainer (const Payloads& now)
    {
        Bytes chunks;
        for (const auto tag : kWrittenTags)
            appendChunk (chunks, tag, writtenVersion (tag), 0, payloadOf (now, tag));
        return makeContainer (chunks);
    }

    KeptLayout KeptLayout::unreadable (const void* data, std::size_t size, const Payloads& atLoad)
    {
        KeptLayout k;
        k.wholeBlob = true;
        const auto* b = static_cast<const std::uint8_t*> (data);
        k.blob.assign (b, b + size);
        k.blobAtLoad = standardContainer (atLoad);
        return k;
    }

    KeptLayout KeptLayout::fromContainer (const ParsedContainer& parsed, const std::vector<bool>& decoded, const Payloads& atLoad)
    {
        KeptLayout k;
        std::set<std::uint32_t> seen;
        k.slots.reserve (parsed.chunks.size());
        for (std::size_t i = 0; i < parsed.chunks.size(); ++i)
        {
            const FoundChunk& c = parsed.chunks[i];
            Slot s;
            s.tag = c.tag;
            s.raw.assign (c.raw, c.raw + c.rawLength);
            s.version = c.version;
            s.flags = c.flags;
            s.truncated = c.truncated;
            if (isWrittenTag (c.tag) && seen.insert (c.tag).second)
            {
                s.written = true;
                s.decoded = i < decoded.size() && decoded[i];
                s.atLoad = payloadOf (atLoad, c.tag);
                s.unreadable = ! s.decoded;
            }
            else
            {
                s.unreadable = ! c.crcOk || ! c.withinLimits || c.truncated;
            }
            k.slots.push_back (std::move (s));
        }
        return k;
    }

    Bytes KeptLayout::write (const Payloads& now) const
    {
        if (wholeBlob)
        {
            Bytes fresh = standardContainer (now);
            return fresh == blobAtLoad ? blob : fresh;
        }

        // A cut end (bytes that hold no whole chunk) is kept too, but written after everything else: in the
        // middle, its header would claim the chunks that follow it as its payload.
        Bytes chunks;
        const Bytes* cutEnd = nullptr;
        std::set<std::uint32_t> emitted;
        for (const Slot& s : slots)
        {
            if (! s.written)
            {
                if (s.truncated)
                    cutEnd = &s.raw;
                else
                    chunks.insert (chunks.end(), s.raw.begin(), s.raw.end());
                continue;
            }

            emitted.insert (s.tag);
            const Bytes& payload = payloadOf (now, s.tag);
            if (payload == s.atLoad)
            {
                if (s.truncated)
                    cutEnd = &s.raw;
                else
                    chunks.insert (chunks.end(), s.raw.begin(), s.raw.end());
            }
            else if (s.decoded)
                appendChunk (chunks, s.tag, std::max (s.version, writtenVersion (s.tag)), s.flags, payload);
            else
                appendChunk (chunks, s.tag, writtenVersion (s.tag), 0, payload);
        }
        for (const auto tag : kWrittenTags)
            if (emitted.count (tag) == 0)
                appendChunk (chunks, tag, writtenVersion (tag), 0, payloadOf (now, tag));
        if (cutEnd != nullptr)
            chunks.insert (chunks.end(), cutEnd->begin(), cutEnd->end());
        return makeContainer (chunks);
    }

    bool KeptLayout::keepsUnreadable (const Payloads& now) const
    {
        if (wholeBlob)
            return standardContainer (now) == blobAtLoad;
        for (const Slot& s : slots)
            if (s.unreadable && (! s.written || payloadOf (now, s.tag) == s.atLoad))
                return true;
        return false;
    }

    int KeptLayout::unknownChunkCount() const noexcept
    {
        int n = 0;
        for (const Slot& s : slots)
            if (! s.written && s.tag != kTagClip)
                ++n;
        return n;
    }
}
