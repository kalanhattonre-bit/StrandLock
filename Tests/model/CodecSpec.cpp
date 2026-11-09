#include "CodecSpec.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "core/Rates.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace sl::test::codecspec
{
    namespace
    {
        void put (Bytes& b, std::uint64_t v, int width)
        {
            for (int i = 0; i < width; ++i)
                b.push_back (static_cast<std::uint8_t> (v >> (8 * i)));
        }

        void u8 (Bytes& b, unsigned v) { put (b, v, 1); }
        void u16 (Bytes& b, unsigned v) { put (b, v, 2); }
        void u32 (Bytes& b, std::uint32_t v) { put (b, v, 4); }
        void i8 (Bytes& b, int v) { put (b, static_cast<std::uint8_t> (static_cast<std::int8_t> (v)), 1); }
        void i32 (Bytes& b, std::int32_t v) { put (b, static_cast<std::uint32_t> (v), 4); }

        void f64 (Bytes& b, double v)
        {
            std::uint64_t u = 0;
            std::memcpy (&u, &v, sizeof u);
            put (b, u, 8);
        }

        void str8 (Bytes& b, const std::string& s)
        {
            u8 (b, static_cast<unsigned> (s.size()));
            b.insert (b.end(), s.begin(), s.end());
        }

        void append (Bytes& b, const Bytes& more) { b.insert (b.end(), more.begin(), more.end()); }

        /** A record: its body with the hooks' trailing bytes, prefixed by the body's length. */
        Bytes record32 (Bytes body, const Hooks& h)
        {
            append (body, h.trailing);
            Bytes out;
            u32 (out, static_cast<std::uint32_t> (body.size()));
            append (out, body);
            return out;
        }

        Bytes record16 (Bytes body, const Hooks& h)
        {
            append (body, h.trailing);
            Bytes out;
            u16 (out, static_cast<unsigned> (body.size()));
            append (out, body);
            return out;
        }

        int tri (const std::optional<bool>& v) { return v.has_value() ? (*v ? 1 : 0) : -1; }

        Bytes sampleRecord (const model::NoteSample& s, const std::string* motion, const Hooks& h)
        {
            Bytes b;
            str8 (b, motion != nullptr ? *motion : (s.motion.has_value() ? std::string (core::kMotionCodes[static_cast<int> (*s.motion)]) : std::string()));
            str8 (b, s.travel == model::TravelKind::path ? "path" : "speed");
            str8 (b, model::kStartModeCodes[static_cast<int> (s.start.mode)]);
            f64 (b, s.start.pos);
            f64 (b, s.start.range);
            i32 (b, s.start.seed);
            i8 (b, tri (s.tape));
            i8 (b, tri (s.rev));
            str8 (b, s.jump.has_value() ? std::string (core::rateCode (*s.jump)) : std::string());
            str8 (b, s.engine.has_value() ? std::string (core::kEngineCodes[static_cast<int> (*s.engine)]) : std::string());
            i8 (b, tri (s.lock));
            u8 (b, s.restart ? 1u : 0u);
            return record16 (std::move (b), h);
        }

        Bytes genRecord (const model::Gen& g, const Hooks& h)
        {
            Bytes b;
            str8 (b, model::kGenKindCodes[static_cast<int> (g.kind)]);
            str8 (b, model::kGenShapeCodes[static_cast<int> (g.shape)]);
            str8 (b, core::rateCode (g.rate));
            f64 (b, g.d0);
            f64 (b, g.d1);
            f64 (b, g.warp);
            f64 (b, g.amount);
            f64 (b, g.smooth);
            u8 (b, g.lockToBeat ? 1u : 0u);
            str8 (b, model::kPerNoteCodes[static_cast<int> (g.perNote)]);
            i32 (b, g.seed);
            u8 (b, g.invert ? 1u : 0u);
            str8 (b, g.key);
            return record16 (std::move (b), h);
        }

        Bytes laneRecordOf (const std::string& laneId, const model::NodeList& nodes, const std::optional<model::Gen>& gen, int oddShape, const Hooks& h)
        {
            Bytes b;
            str8 (b, laneId);
            u16 (b, static_cast<unsigned> (nodes.size()));
            for (std::size_t i = 0; i < nodes.size(); ++i)
            {
                f64 (b, nodes[i].t);
                f64 (b, nodes[i].v);
                u8 (b, i == 0 && oddShape >= 0 ? static_cast<unsigned> (oddShape) : static_cast<unsigned> (nodes[i].shape));
                f64 (b, nodes[i].bend);
            }
            u8 (b, gen.has_value() ? 1u : 0u);
            if (gen.has_value())
                append (b, genRecord (*gen, h));
            return record32 (std::move (b), h);
        }
    }

    Bytes write (const model::Clip& clip, const Hooks& h, std::vector<std::size_t>* pOffsets)
    {
        Bytes out;
        {
            Bytes s;
            i32 (s, clip.bars);
            i32 (s, clip.beatsPerBar);
            f64 (s, clip.offsetBeats);
            u8 (s, clip.loop ? 1u : 0u);
            u8 (s, clip.tieAcrossLoop ? 1u : 0u);
            append (out, record32 (std::move (s), h));
        }
        u32 (out, static_cast<std::uint32_t> (clip.notes.size()));
        for (std::size_t k = 0; k < clip.notes.size(); ++k)
        {
            const model::Note& n = *clip.notes[k];
            const bool odd = static_cast<int> (k) == h.oddNote;
            Bytes b;
            str8 (b, n.id);
            if (pOffsets != nullptr)
                pOffsets->push_back (out.size() + 4 + b.size());   // after the record's length and the id
            f64 (b, n.p);
            f64 (b, n.s);
            f64 (b, n.l);
            f64 (b, n.vel);
            f64 (b, n.glide.has_value() ? *n.glide : std::numeric_limits<double>::quiet_NaN());
            f64 (b, n.gc);
            str8 (b, n.glideTo);
            append (b, sampleRecord (n.sample, odd ? &h.motion : nullptr, h));
            const bool unknownHere = static_cast<int> (k) == h.unknownLaneNote;
            const std::size_t unknown = unknownHere ? h.unknownLanes.size() : 0;
            u8 (b, static_cast<unsigned> (core::kNumLanes + unknown));
            if (unknownHere && h.unknownLaneFirst)
                for (const auto& lane : h.unknownLanes)
                    append (b, lane);
            for (int lane = 0; lane < core::kNumLanes; ++lane)
                append (b, laneRecordOf (core::kLanes[lane].code, n.c[static_cast<std::size_t> (lane)], n.gen[static_cast<std::size_t> (lane)],
                                         odd && lane == h.oddLane ? h.shapeCode : -1, h));
            if (unknownHere && ! h.unknownLaneFirst)
                for (const auto& lane : h.unknownLanes)
                    append (b, lane);
            append (out, record32 (std::move (b), h));
        }
        return out;
    }

    Bytes laneRecord (const std::string& laneId, int count, bool withGen)
    {
        model::NodeList nodes;
        for (int i = 0; i < count; ++i)
        {
            model::Node nd;
            nd.t = count > 1 ? static_cast<double> (i) / static_cast<double> (count - 1) : 0.0;
            nd.v = 0.25 + 0.5 * static_cast<double> (i % 3) / 2.0;
            nd.shape = model::NodeShape::linear;
            nodes.push_back (nd);
        }
        std::optional<model::Gen> gen;
        if (withGen)
            gen = model::Gen {};
        return laneRecordOf (laneId, nodes, gen, -1, Hooks {});
    }

    bool differOnlyIn (const Bytes& a, const Bytes& b, std::size_t at, std::size_t length)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i] && (i < at || i >= at + length))
                return false;
        return true;
    }

    std::size_t find (const Bytes& bytes, const std::string& text)
    {
        const auto it = std::search (bytes.begin(), bytes.end(), text.begin(), text.end(),
                                     [] (std::uint8_t x, char c) { return x == static_cast<std::uint8_t> (c); });
        return it == bytes.end() ? std::string::npos : static_cast<std::size_t> (it - bytes.begin());
    }
}
