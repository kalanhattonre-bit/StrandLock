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

        const Bytes& bytesOf (const PayloadPtr& p)
        {
            static const Bytes none;
            return p != nullptr ? *p : none;
        }

        const Bytes& payloadOf (const Payloads& p, std::uint32_t tag)
        {
            const auto it = p.find (tag);
            return it != p.end() ? bytesOf (it->second) : bytesOf (nullptr);
        }

        PayloadPtr payloadPtrOf (const Payloads& p, std::uint32_t tag)
        {
            const auto it = p.find (tag);
            return it != p.end() ? it->second : nullptr;
        }

        /** The same bytes (the same shared payload, or equal content; a missing payload is empty). */
        bool samePayload (const PayloadPtr& a, const PayloadPtr& b)
        {
            return a == b || bytesOf (a) == bytesOf (b);
        }

        /** A container made in place (M7): 16 header bytes kept free, the chunks appended after them, then the header
            written over them, so a 50 MB SAUD is copied once, not twice. */
        void beginContainer (Bytes& out, std::size_t chunkBytes)
        {
            out.clear();
            out.reserve (kHeaderSize + chunkBytes);
            out.resize (kHeaderSize, 0);
        }

        void endContainer (Bytes& out)
        {
            Bytes head;
            head.reserve (kHeaderSize);
            ByteWriter w (head);
            w.bytes (kMagic, sizeof (kMagic));
            w.u16 (static_cast<std::uint32_t> (core::kStateFormatVersion));
            w.u16 (0);
            w.u32 (static_cast<std::uint32_t> (out.size()));
            w.u32 (crc32 (head.data(), 12));
            std::memcpy (out.data(), head.data(), kHeaderSize);
        }

        /** The bytes a chunk takes with its header and padding. */
        std::size_t framed (std::size_t payloadBytes) noexcept { return kChunkHeaderSize + payloadBytes + padTo4 (payloadBytes); }

        /** M7: the chunk is exactly what appendChunk makes of these payload bytes with its own version and flags (its CRC
            good, its padding zero), so it can be written again from them instead of keeping its raw copy. */
        bool reframes (const FoundChunk& c, const Bytes& payload) noexcept
        {
            if (c.truncated || ! c.crcOk || c.payload == nullptr || c.payloadLength != payload.size()
                || c.rawLength != framed (c.payloadLength))
                return false;
            if (c.payloadLength > 0 && std::memcmp (c.payload, payload.data(), c.payloadLength) != 0)
                return false;
            for (std::size_t i = kChunkHeaderSize + c.payloadLength; i < c.rawLength; ++i)
                if (c.raw[i] != 0)
                    return false;
            return true;
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
        std::size_t total = 0;
        for (const auto tag : kWrittenTags)
            total += framed (payloadOf (now, tag).size());
        Bytes out;
        beginContainer (out, total);
        for (const auto tag : kWrittenTags)
        {
            if (isOptionalTag (tag) && payloadPtrOf (now, tag) == nullptr)
                continue;   // M7: no sound kept in the project, no SAUD chunk
            appendChunk (out, tag, writtenVersion (tag), 0, payloadOf (now, tag));
        }
        endContainer (out);
        return out;
    }

    bool KeptLayout::isProjectContent (std::uint32_t tag) noexcept
    {
        return tag != kTagUist;
    }

    KeptLayout KeptLayout::unreadable (const void* data, std::size_t size, const Payloads& atLoad)
    {
        KeptLayout k;
        k.wholeBlob = true;
        const auto* b = static_cast<const std::uint8_t*> (data);
        k.blob.assign (b, b + size);
        for (const auto tag : kWrittenTags)
            if (isProjectContent (tag))
                k.contentAtLoad[tag] = payloadPtrOf (atLoad, tag);
        return k;
    }

    bool KeptLayout::contentUnchanged (const Payloads& now) const
    {
        for (const auto tag : kWrittenTags)
            if (isProjectContent (tag) && ! samePayload (payloadPtrOf (now, tag), payloadPtrOf (contentAtLoad, tag)))
                return false;
        return true;
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
            s.version = c.version;
            s.flags = c.flags;
            s.truncated = c.truncated;
            if (isWrittenTag (c.tag) && seen.insert (c.tag).second)
            {
                s.written = true;
                s.decoded = i < decoded.size() && decoded[i];
                s.atLoad = payloadPtrOf (atLoad, c.tag);
                s.unreadable = ! s.decoded;
                // M7: a SAUD the processor holds byte for byte (up to 50 MB) is written again from its payload; its raw
                // copy would only hold the same bytes a second time.
                s.reframe = c.tag == kTagSaud && s.decoded && s.atLoad != nullptr && reframes (c, *s.atLoad);
            }
            else
            {
                s.unreadable = ! c.crcOk || ! c.withinLimits || c.truncated;
            }
            if (! s.reframe)
                s.raw.assign (c.raw, c.raw + c.rawLength);
            k.slots.push_back (std::move (s));
        }
        return k;
    }

    Bytes KeptLayout::write (const Payloads& now) const
    {
        // UIST is the window's own state (tab, mode, fold, size, theme): a click there is not an edit.
        if (wholeBlob)
            return contentUnchanged (now) ? blob : standardContainer (now);

        // What each slot writes, decided once (samePayload may compare 32 MiB), so the container is sized exactly and
        // made in one buffer (M7: the SAUD chunk can be 50 MB).
        enum class Use : std::uint8_t { raw, cutEnd, reframe, none, encodeKnown, encodeNew };
        std::vector<Use> use (slots.size(), Use::raw);
        std::size_t total = 0;
        std::set<std::uint32_t> emitted;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const Slot& s = slots[i];
            Use u = Use::raw;
            if (! s.written)
            {
                u = s.truncated ? Use::cutEnd : Use::raw;
            }
            else
            {
                emitted.insert (s.tag);
                const PayloadPtr payloadNow = payloadPtrOf (now, s.tag);
                if (samePayload (payloadNow, s.atLoad))
                    u = s.reframe ? Use::reframe : (s.truncated ? Use::cutEnd : Use::raw);
                else if (isOptionalTag (s.tag) && payloadNow == nullptr)
                    u = Use::none;   // M7: the part this chunk held has gone (another sound, nothing kept): not written
                else
                    u = s.decoded ? Use::encodeKnown : Use::encodeNew;
            }
            use[i] = u;
            if (u == Use::raw || u == Use::cutEnd)
                total += s.raw.size();
            else if (u == Use::reframe)
                total += framed (bytesOf (s.atLoad).size());
            else if (u != Use::none)
                total += framed (payloadOf (now, s.tag).size());
        }
        for (const auto tag : kWrittenTags)
            if (emitted.count (tag) == 0 && ! (isOptionalTag (tag) && payloadPtrOf (now, tag) == nullptr))
                total += framed (payloadOf (now, tag).size());

        // A cut end (bytes that hold no whole chunk) is kept too, but written after everything else: in the
        // middle, its header would claim the chunks that follow it as its payload.
        Bytes out;
        beginContainer (out, total);
        const Bytes* cutEnd = nullptr;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const Slot& s = slots[i];
            switch (use[i])
            {
                case Use::raw:         out.insert (out.end(), s.raw.begin(), s.raw.end()); break;
                case Use::cutEnd:      cutEnd = &s.raw; break;
                case Use::reframe:     appendChunk (out, s.tag, s.version, s.flags, bytesOf (s.atLoad)); break;   // the bytes loaded
                case Use::none:        break;
                case Use::encodeKnown: appendChunk (out, s.tag, std::max (s.version, writtenVersion (s.tag)), s.flags, payloadOf (now, s.tag)); break;
                case Use::encodeNew:   appendChunk (out, s.tag, writtenVersion (s.tag), 0, payloadOf (now, s.tag)); break;
            }
        }
        for (const auto tag : kWrittenTags)
            if (emitted.count (tag) == 0 && ! (isOptionalTag (tag) && payloadPtrOf (now, tag) == nullptr))
                appendChunk (out, tag, writtenVersion (tag), 0, payloadOf (now, tag));
        if (cutEnd != nullptr)
            out.insert (out.end(), cutEnd->begin(), cutEnd->end());
        endContainer (out);
        return out;
    }

    bool KeptLayout::keepsUnreadable (const Payloads& now) const
    {
        if (wholeBlob)
            return contentUnchanged (now);
        for (const Slot& s : slots)
            if (s.unreadable && (! s.written || samePayload (payloadPtrOf (now, s.tag), s.atLoad)))
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
