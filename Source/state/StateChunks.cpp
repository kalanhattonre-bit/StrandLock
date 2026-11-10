#include "StateChunks.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sl::state
{
    namespace
    {
        /** Reads the fields of an append-only record one by one: each read happens only while bytes are left
            (a record that ends there is an older, shorter one: the rest keep their defaults); a field cut in
            the middle makes the record damaged. */
        class RecordReader
        {
        public:
            RecordReader (const std::uint8_t* data, std::size_t size) noexcept : r (data, size) {}

            template <class T, class Fn>
            RecordReader& field (T& target, Fn&& read)
            {
                if (damaged || r.atEnd())
                    return *this;
                T v = read (r);
                if (! r.ok())
                    damaged = true;
                else
                    target = v;
                return *this;
            }

            bool finish (Bytes& trailing)
            {
                if (damaged)
                    return false;
                trailing = r.rest();
                return true;
            }

        private:
            ByteReader r;
            bool damaged = false;
        };

        std::uint8_t u8Of (ByteReader& r)    { return static_cast<std::uint8_t> (r.u8()); }
        std::uint16_t u16Of (ByteReader& r)  { return static_cast<std::uint16_t> (r.u16()); }
        std::int32_t i32Of (ByteReader& r)   { return r.i32(); }
        float f32Of (ByteReader& r)          { return r.f32(); }
        double f64Of (ByteReader& r)         { return r.f64(); }
        std::string str8Of (ByteReader& r)   { return r.str8(); }

        template <class T>
        T clampTo (T v, T lo, T hi) noexcept { return v < lo ? lo : v > hi ? hi : v; }

        float finiteOr (float v, float d) noexcept { return std::isfinite (v) ? v : d; }
        double finiteOr (double v, double d) noexcept { return std::isfinite (v) ? v : d; }
    }

    //==============================================================================================
    bool decodeParm (const std::uint8_t* payload, std::size_t size, ParmChunk& out)
    {
        ByteReader r (payload, size);
        const std::size_t count = r.u16();
        // Each entry is at least 5 bytes (an empty id and the float): check the count before reserving.
        if (! r.ok() || count > r.remaining() / 5)
            return false;

        ParmChunk c;
        c.entries.reserve (count);
        for (std::size_t i = 0; i < count; ++i)
        {
            ParmEntry e;
            e.id = r.str8();
            e.value = r.f32();
            if (! r.ok())
                return false;
            c.entries.push_back (std::move (e));
        }
        c.trailing = r.rest();
        out = std::move (c);
        return true;
    }

    Bytes encodeParm (const std::array<float, params::kNumParams>& values, const std::vector<ParmEntry>& extras, const Bytes& trailing)
    {
        Bytes out;
        ByteWriter w (out);
        const int bypass = params::index (params::P::bypass);
        w.u16 (static_cast<std::uint32_t> (params::kNumParams - 1 + static_cast<int> (extras.size())));
        for (int i = 0; i < params::kNumParams; ++i)
        {
            if (i == bypass)
                continue;
            w.str8 (params::kParams[i].id);
            w.f32 (values[static_cast<std::size_t> (i)]);
        }
        for (const auto& e : extras)
        {
            w.str8 (e.id);
            w.f32 (e.value);
        }
        w.bytes (trailing);
        return out;
    }

    //==============================================================================================
    bool decodeInst (const std::uint8_t* payload, std::size_t size, InstState& out)
    {
        InstState s;
        RecordReader rr (payload, size);
        rr.field (s.seed, i32Of)
          .field (s.mod1Target, str8Of)
          .field (s.mod1Cc, u8Of)
          .field (s.mod2Target, str8Of)
          .field (s.mod2Cc, u8Of)
          .field (s.key, u8Of)
          .field (s.scale, str8Of)
          .field (s.scaleLock, u8Of)
          .field (s.regionStartExact, f64Of)
          .field (s.regionEndExact, f64Of)
          .field (s.liveTemplate, str8Of);
        if (! rr.finish (s.trailing))
            return false;
        out = std::move (s);
        return true;
    }

    Bytes encodeInst (const InstState& s)
    {
        Bytes out;
        ByteWriter w (out);
        w.i32 (s.seed);
        w.str8 (s.mod1Target);
        w.u8 (s.mod1Cc);
        w.str8 (s.mod2Target);
        w.u8 (s.mod2Cc);
        w.u8 (s.key);
        w.str8 (s.scale);
        w.u8 (s.scaleLock);
        w.f64 (s.regionStartExact);
        w.f64 (s.regionEndExact);
        w.str8 (s.liveTemplate);
        w.bytes (s.trailing);
        return out;
    }

    //==============================================================================================
    namespace
    {
        std::uint64_t u64Of (ByteReader& r) { return r.u64(); }
        std::uint32_t u32Of (ByteReader& r) { return r.u32(); }
        std::string str16Of (ByteReader& r) { return r.str16(); }

        std::array<std::uint8_t, 32> digestOf (ByteReader& r)
        {
            std::array<std::uint8_t, 32> d {};
            if (const auto* p = r.take (d.size()))
                std::memcpy (d.data(), p, d.size());
            return d;
        }

        /** The analysis cache's parts: u8 count, then each part. The count is checked against the bytes left
            (a part is at least 27 bytes) before anything is reserved. */
        std::vector<AnalysisPart> partsOf (ByteReader& r)
        {
            const std::size_t count = r.u8();
            std::vector<AnalysisPart> parts;
            if (! r.ok() || count > r.remaining() / 27)
            {
                (void) r.take (r.remaining() + 1);   // marks the reader damaged
                return parts;
            }
            parts.reserve (count);
            for (std::size_t i = 0; i < count && r.ok(); ++i)
            {
                AnalysisPart p;
                p.kind = static_cast<std::uint8_t> (r.u8());
                p.start = r.f64();
                p.end = r.f64();
                p.bpm = r.f64();
                p.tempoFound = static_cast<std::uint8_t> (r.u8());
                p.label = r.str8();
                parts.push_back (std::move (p));
            }
            return parts;
        }
    }

    bool decodeSmpl (const std::uint8_t* payload, std::size_t size, SampleRef& out)
    {
        ByteReader head (payload, size);
        const auto kind = head.u8();
        if (! head.ok() || kind > kSampleFile)
            return false;   // a kind this version does not know

        SampleRef s;
        s.kind = static_cast<std::uint8_t> (kind);
        RecordReader rr (payload + 1, size - 1);
        if (kind == kSampleStarter)
        {
            s.starterId.clear();
            rr.field (s.starterId, str8Of)
              .field (s.seed, i32Of);
        }
        else
        {
            s.starterId.clear();
            auto& f = s.file;
            auto& c = f.cache;
            rr.field (f.path, str16Of)
              .field (f.fileSize, u64Of)
              .field (f.sha256, digestOf)
              .field (f.fileSampleRate, f64Of)
              .field (f.channels, u32Of)
              .field (f.fileFrames, u64Of)
              .field (f.loadedStartFrame, u64Of)
              .field (f.loadedFrames, u64Of)
              .field (f.displayName, str16Of)
              .field (f.keepInProject, u8Of)
              .field (f.embedAnswer, u8Of)
              .field (c.analysed, u8Of)
              .field (c.hasPitch, u8Of)
              .field (c.midi, f64Of)
              .field (c.cents, f64Of)
              .field (c.hasKey, u8Of)
              .field (c.key, u8Of)
              .field (c.scale, str8Of)
              .field (c.parts, partsOf);
        }
        if (! rr.finish (s.trailing))
            return false;
        out = std::move (s);
        return true;
    }

    Bytes encodeSmpl (const SampleRef& s)
    {
        Bytes out;
        ByteWriter w (out);
        w.u8 (s.kind);
        if (s.kind == kSampleFile)
        {
            const auto& f = s.file;
            const auto& c = f.cache;
            w.str16 (f.path);
            w.u64 (f.fileSize);
            w.bytes (f.sha256.data(), f.sha256.size());
            w.f64 (f.fileSampleRate);
            w.u32 (f.channels);
            w.u64 (f.fileFrames);
            w.u64 (f.loadedStartFrame);
            w.u64 (f.loadedFrames);
            w.str16 (f.displayName);
            w.u8 (f.keepInProject);
            w.u8 (f.embedAnswer);
            w.u8 (c.analysed);
            w.u8 (c.hasPitch);
            w.f64 (c.midi);
            w.f64 (c.cents);
            w.u8 (c.hasKey);
            w.u8 (c.key);
            w.str8 (c.scale);
            const std::size_t parts = std::min<std::size_t> (c.parts.size(), 255);
            w.u8 (static_cast<std::uint32_t> (parts));
            for (std::size_t i = 0; i < parts; ++i)
            {
                const auto& p = c.parts[i];
                w.u8 (p.kind);
                w.f64 (p.start);
                w.f64 (p.end);
                w.f64 (p.bpm);
                w.u8 (p.tempoFound);
                w.str8 (p.label);
            }
        }
        else
        {
            w.str8 (s.starterId);
            w.i32 (s.seed);
        }
        w.bytes (s.trailing);
        return out;
    }

    bool sameSound (const SampleRef& a, const SampleRef& b) noexcept
    {
        if (a.kind != b.kind)
            return false;
        if (a.kind == kSampleFile)
            return a.file.path == b.file.path && a.file.fileSize == b.file.fileSize && a.file.sha256 == b.file.sha256
                && a.file.loadedStartFrame == b.file.loadedStartFrame && a.file.loadedFrames == b.file.loadedFrames;
        return a.starterId == b.starterId && a.seed == b.seed;
    }

    //==============================================================================================
    bool decodeUist (const std::uint8_t* payload, std::size_t size, UiState& out)
    {
        UiState s;
        RecordReader rr (payload, size);
        rr.field (s.theme, u8Of)
          .field (s.uiScalePercent, u16Of)
          .field (s.logicalWidth, u16Of)
          .field (s.logicalHeight, u16Of)
          .field (s.mode, u8Of)
          .field (s.subLane, str8Of)
          .field (s.tab, str8Of)
          .field (s.folded, u8Of)
          .field (s.zoomH, f32Of)
          .field (s.zoomV, f32Of)
          .field (s.scrollBeat, f64Of)
          .field (s.scrollRow, f32Of)
          .field (s.gridBeats, f32Of)
          .field (s.scope, u8Of)
          .field (s.copyMask, u16Of)
          .field (s.stripViewA, f64Of)
          .field (s.stripViewB, f64Of);
        if (! rr.finish (s.trailing))
            return false;

        const UiState d;
        s.theme = s.theme == 1 ? 1 : 0;
        s.uiScalePercent = clampTo<std::uint16_t> (s.uiScalePercent, kMinUiScalePercent, kMaxUiScalePercent);
        s.logicalWidth = clampTo<std::uint16_t> (s.logicalWidth, 900, 2400);
        s.logicalHeight = clampTo<std::uint16_t> (s.logicalHeight, 560, 1600);
        s.mode = clampTo<std::uint8_t> (s.mode, 1, 8);
        s.folded = s.folded != 0 ? 1 : 0;
        s.zoomH = finiteOr (s.zoomH, d.zoomH);
        s.zoomV = finiteOr (s.zoomV, d.zoomV);
        s.scrollBeat = finiteOr (s.scrollBeat, d.scrollBeat);
        s.scrollRow = finiteOr (s.scrollRow, d.scrollRow);
        s.gridBeats = finiteOr (s.gridBeats, d.gridBeats);
        s.scope = s.scope == 1 ? 1 : 0;
        s.copyMask = static_cast<std::uint16_t> (s.copyMask & 0x3ffu);
        s.stripViewA = finiteOr (s.stripViewA, d.stripViewA);
        s.stripViewB = finiteOr (s.stripViewB, d.stripViewB);
        out = std::move (s);
        return true;
    }

    Bytes encodeUist (const UiState& s)
    {
        Bytes out;
        ByteWriter w (out);
        w.u8 (s.theme);
        w.u16 (s.uiScalePercent);
        w.u16 (s.logicalWidth);
        w.u16 (s.logicalHeight);
        w.u8 (s.mode);
        w.str8 (s.subLane);
        w.str8 (s.tab);
        w.u8 (s.folded);
        w.f32 (s.zoomH);
        w.f32 (s.zoomV);
        w.f64 (s.scrollBeat);
        w.f32 (s.scrollRow);
        w.f32 (s.gridBeats);
        w.u8 (s.scope);
        w.u16 (s.copyMask);
        w.f64 (s.stripViewA);
        w.f64 (s.stripViewB);
        w.bytes (s.trailing);
        return out;
    }
}
