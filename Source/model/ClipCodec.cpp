#include "ClipCodec.h"

#include "Normalize.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "core/Rates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace sl::model
{
    namespace
    {
        constexpr std::size_t kNodeBytes = 25;   // f64 t, f64 v, u8 shape, f64 bend

        //==========================================================================================
        // Writing. One writer for the bytes and for their count (buf null), so a size is always the size
        // the encoder writes.

        class Out
        {
        public:
            explicit Out (std::vector<std::uint8_t>* target) noexcept : buf (target), base (target != nullptr ? target->size() : 0) {}

            std::size_t size() const noexcept { return count; }

            void bytes (const std::uint8_t* p, std::size_t n)
            {
                if (buf != nullptr && n > 0)
                    buf->insert (buf->end(), p, p + n);
                count += n;
            }

            void bytes (const std::vector<std::uint8_t>& v) { bytes (v.data(), v.size()); }

            void u8 (std::uint8_t v) { bytes (&v, 1); }

            void u16 (std::uint16_t v)
            {
                const std::uint8_t b[2] = { static_cast<std::uint8_t> (v), static_cast<std::uint8_t> (v >> 8) };
                bytes (b, 2);
            }

            void u32 (std::uint32_t v)
            {
                std::uint8_t b[4];
                for (int i = 0; i < 4; ++i)
                    b[i] = static_cast<std::uint8_t> (v >> (8 * i));
                bytes (b, 4);
            }

            void i8 (std::int8_t v) { u8 (static_cast<std::uint8_t> (v)); }
            void i32 (std::int32_t v) { u32 (static_cast<std::uint32_t> (v)); }

            void f64 (double v)
            {
                std::uint64_t u = 0;
                std::memcpy (&u, &v, sizeof u);
                std::uint8_t b[8];
                for (int i = 0; i < 8; ++i)
                    b[i] = static_cast<std::uint8_t> (u >> (8 * i));
                bytes (b, 8);
            }

            /** str8: at most 255 bytes, cut at a UTF-8 boundary. */
            void str8 (std::string_view s)
            {
                std::size_t n = std::min<std::size_t> (s.size(), kMaxIdBytes);
                if (n < s.size())
                    while (n > 0 && (static_cast<unsigned char> (s[n]) & 0xc0u) == 0x80u)
                        --n;
                u8 (static_cast<std::uint8_t> (n));
                bytes (reinterpret_cast<const std::uint8_t*> (s.data()), n);
            }

            /** A record's length field: a placeholder now, filled in by end32 / end16. */
            std::size_t begin32()
            {
                const std::size_t at = count;
                u32 (0);
                return at;
            }

            std::size_t begin16()
            {
                const std::size_t at = count;
                u16 (0);
                return at;
            }

            void end32 (std::size_t at) { patch (at, count - at - 4, 4); }

            /** False (nothing patched) when the record is too long for its u16 length. */
            bool end16 (std::size_t at)
            {
                const std::size_t length = count - at - 2;
                if (length > 0xffffu)
                    return false;
                patch (at, length, 2);
                return true;
            }

            /** Forgets everything written since at. */
            void rewind (std::size_t at)
            {
                if (buf != nullptr)
                    buf->resize (base + at);
                count = at;
            }

        private:
            void patch (std::size_t at, std::size_t value, int width)
            {
                if (buf == nullptr)
                    return;
                for (int i = 0; i < width; ++i)
                    (*buf)[base + at + static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (value >> (8 * i));
            }

            std::vector<std::uint8_t>* buf;
            std::size_t base;
            std::size_t count = 0;
        };

        std::int8_t tri (const std::optional<bool>& v) noexcept
        {
            return v.has_value() ? static_cast<std::int8_t> (*v ? 1 : 0) : static_cast<std::int8_t> (-1);
        }

        // Bit-for-bit sameness, to tell whether a decoded record was edited.
        bool sameBits (double a, double b) noexcept { return std::memcmp (&a, &b, sizeof a) == 0; }

        bool sameNodesBits (const NodeList& a, const NodeList& b) noexcept
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (! sameBits (a[i].t, b[i].t) || ! sameBits (a[i].v, b[i].v) || a[i].shape != b[i].shape || ! sameBits (a[i].bend, b[i].bend))
                    return false;
            return true;
        }

        bool sameGenBits (const Gen& a, const Gen& b) noexcept
        {
            return a.kind == b.kind && a.shape == b.shape && a.rate == b.rate && sameBits (a.d0, b.d0) && sameBits (a.d1, b.d1)
                && sameBits (a.warp, b.warp) && a.lockToBeat == b.lockToBeat && sameBits (a.amount, b.amount) && sameBits (a.smooth, b.smooth)
                && a.perNote == b.perNote && a.seed == b.seed && a.invert == b.invert && a.key == b.key;
        }

        bool sameGenBits (const std::optional<Gen>& a, const std::optional<Gen>& b) noexcept
        {
            return a.has_value() == b.has_value() && (! a.has_value() || sameGenBits (*a, *b));
        }

        bool sameSampleBits (const NoteSample& a, const NoteSample& b) noexcept
        {
            return a.motion == b.motion && a.travel == b.travel && a.start.mode == b.start.mode && sameBits (a.start.pos, b.start.pos)
                && sameBits (a.start.range, b.start.range) && a.start.seed == b.start.seed && a.tape == b.tape && a.rev == b.rev
                && a.jump == b.jump && a.engine == b.engine && a.lock == b.lock && a.restart == b.restart;
        }

        void writeSampleFields (Out& o, const NoteSample& s)
        {
            o.str8 (s.motion.has_value() ? core::code (*s.motion) : "");
            o.str8 (code (s.travel));
            o.str8 (code (s.start.mode));
            o.f64 (s.start.pos);
            o.f64 (s.start.range);
            o.i32 (s.start.seed);
            o.i8 (tri (s.tape));
            o.i8 (tri (s.rev));
            o.str8 (s.jump.has_value() ? core::rateCode (*s.jump) : "");
            o.str8 (s.engine.has_value() ? core::code (*s.engine) : "");
            o.i8 (tri (s.lock));
            o.u8 (s.restart ? 1 : 0);
        }

        void writeSample (Out& o, const NoteSample& s, const NoteExtras* x)
        {
            if (x != nullptr && ! x->sample.original.empty() && sameSampleBits (s, x->decodedSample))
            {
                o.bytes (x->sample.original);
                return;
            }
            std::size_t at = o.begin16();
            writeSampleFields (o, s);
            if (x != nullptr)
                o.bytes (x->sample.trailing);
            if (! o.end16 (at))
            {
                o.rewind (at);   // the kept bytes no longer fit beside the new values: they are dropped
                at = o.begin16();
                writeSampleFields (o, s);
                o.end16 (at);
            }
        }

        void writeGenFields (Out& o, const Gen& g)
        {
            o.str8 (code (g.kind));
            o.str8 (code (g.shape));
            o.str8 (core::rateCode (g.rate));
            o.f64 (g.d0);
            o.f64 (g.d1);
            o.f64 (g.warp);
            o.f64 (g.amount);
            o.f64 (g.smooth);
            o.u8 (g.lockToBeat ? 1 : 0);
            o.str8 (code (g.perNote));
            o.i32 (g.seed);
            o.u8 (g.invert ? 1 : 0);
            o.str8 (g.key);
        }

        void writeGen (Out& o, const Gen& g, const LaneExtras* lx)
        {
            if (lx != nullptr && ! lx->gen.original.empty() && lx->decodedGen.has_value() && sameGenBits (g, *lx->decodedGen))
            {
                o.bytes (lx->gen.original);
                return;
            }
            std::size_t at = o.begin16();
            writeGenFields (o, g);
            if (lx != nullptr)
                o.bytes (lx->gen.trailing);
            if (! o.end16 (at))
            {
                o.rewind (at);
                at = o.begin16();
                writeGenFields (o, g);
                o.end16 (at);
            }
        }

        void writeLane (Out& o, const Note& n, std::size_t lane, const NoteExtras* x)
        {
            const LaneExtras* lx = x != nullptr ? &x->lanes[lane] : nullptr;
            const NodeList& nodes = n.c[lane];
            const std::optional<Gen>& gen = n.gen[lane];
            if (lx != nullptr && ! lx->lane.original.empty() && sameNodesBits (nodes, lx->decodedNodes) && sameGenBits (gen, lx->decodedGen))
            {
                o.bytes (lx->lane.original);
                return;
            }
            const std::size_t at = o.begin32();
            o.str8 (core::kLanes[lane].code);
            const std::size_t count = std::min<std::size_t> (nodes.size(), 0xffffu);
            o.u16 (static_cast<std::uint16_t> (count));
            for (std::size_t i = 0; i < count; ++i)
            {
                o.f64 (nodes[i].t);
                o.f64 (nodes[i].v);
                o.u8 (static_cast<std::uint8_t> (nodes[i].shape));
                o.f64 (nodes[i].bend);
            }
            o.u8 (gen.has_value() ? 1 : 0);
            if (gen.has_value())
                writeGen (o, *gen, lx);
            if (lx != nullptr)
                o.bytes (lx->lane.trailing);
            o.end32 (at);
        }

        void writeNote (Out& o, const Note& n)
        {
            const NoteExtras* x = n.extras.get();
            const std::size_t at = o.begin32();
            o.str8 (n.id);
            o.f64 (n.p);
            o.f64 (n.s);
            o.f64 (n.l);
            o.f64 (n.vel);
            o.f64 (n.glide.has_value() ? *n.glide : std::numeric_limits<double>::quiet_NaN());
            o.f64 (n.gc);
            o.str8 (n.glideTo);
            writeSample (o, n.sample, x);
            const std::size_t unknown = x != nullptr ? std::min<std::size_t> (x->unknownLanes.size(), static_cast<std::size_t> (kMaxUnknownLanes)) : 0;
            o.u8 (static_cast<std::uint8_t> (static_cast<std::size_t> (core::kNumLanes) + unknown));
            for (std::size_t lane = 0; lane < static_cast<std::size_t> (core::kNumLanes); ++lane)
                writeLane (o, n, lane, x);
            for (std::size_t i = 0; i < unknown; ++i)
                o.bytes (x->unknownLanes[i]);
            if (x != nullptr)
                o.bytes (x->noteTrailing);
            o.end32 (at);
        }

        void writeHeader (Out& o, const Clip& c)
        {
            const std::size_t at = o.begin32();
            o.i32 (c.bars);
            o.i32 (c.beatsPerBar);
            o.f64 (c.offsetBeats);
            o.u8 (c.loop ? 1 : 0);
            o.u8 (c.tieAcrossLoop ? 1 : 0);
            if (c.extras != nullptr)
                o.bytes (c.extras->settingsTrailing);
            o.end32 (at);
            o.u32 (static_cast<std::uint32_t> (c.notes.size()));
        }

        //==========================================================================================
        // Reading. In never reads outside its bytes; a read that does not fit returns false and moves nothing.

        class In
        {
        public:
            In() noexcept = default;
            In (const std::uint8_t* p, std::size_t n) noexcept : at (p), end (p + n) {}

            std::size_t left() const noexcept { return static_cast<std::size_t> (end - at); }
            const std::uint8_t* pos() const noexcept { return at; }
            const std::uint8_t* stop() const noexcept { return end; }

            bool u8 (std::uint8_t& v) noexcept
            {
                if (left() < 1)
                    return false;
                v = *at++;
                return true;
            }

            bool u16 (std::uint16_t& v) noexcept
            {
                if (left() < 2)
                    return false;
                v = static_cast<std::uint16_t> (at[0] | (at[1] << 8));
                at += 2;
                return true;
            }

            bool u32 (std::uint32_t& v) noexcept
            {
                if (left() < 4)
                    return false;
                v = 0;
                for (int i = 0; i < 4; ++i)
                    v |= static_cast<std::uint32_t> (at[i]) << (8 * i);
                at += 4;
                return true;
            }

            bool i8 (std::int8_t& v) noexcept
            {
                std::uint8_t u = 0;
                if (! u8 (u))
                    return false;
                v = static_cast<std::int8_t> (u);
                return true;
            }

            bool i32 (std::int32_t& v) noexcept
            {
                std::uint32_t u = 0;
                if (! u32 (u))
                    return false;
                v = static_cast<std::int32_t> (u);
                return true;
            }

            bool f64 (double& v) noexcept
            {
                if (left() < 8)
                    return false;
                std::uint64_t u = 0;
                for (int i = 0; i < 8; ++i)
                    u |= static_cast<std::uint64_t> (at[i]) << (8 * i);
                std::memcpy (&v, &u, sizeof v);
                at += 8;
                return true;
            }

            bool str8 (std::string& s)
            {
                if (left() < 1 || left() - 1 < at[0])
                    return false;
                const std::size_t n = at[0];
                s.assign (reinterpret_cast<const char*> (at + 1), n);
                at += 1 + n;
                return true;
            }

            /** The next n bytes as a reader of their own, skipped here. False when fewer are left. */
            bool sub (std::size_t n, In& out) noexcept
            {
                if (left() < n)
                    return false;
                out = In (at, n);
                at += n;
                return true;
            }

        private:
            const std::uint8_t* at = nullptr;
            const std::uint8_t* end = nullptr;
        };

        enum class Read { ok, missing, corrupt };

        std::optional<bool> triFrom (std::int8_t v, bool& unknown) noexcept
        {
            if (v == 0)
                return false;
            if (v == 1)
                return true;
            if (v != -1)
                unknown = true;
            return std::nullopt;
        }

        bool flagFrom (std::uint8_t v, bool& unknown) noexcept
        {
            if (v > 1)
                unknown = true;
            return v != 0;
        }

        void keepBytes (std::vector<std::uint8_t>& to, const std::uint8_t* from, const std::uint8_t* end)
        {
            to.assign (from, end);
        }

        /** A sample record: missing when its length does not fit (the fields keep their defaults). */
        Read readSample (In& r, NoteSample& out, KeptRecord& kept)
        {
            const std::uint8_t* begin = r.pos();
            std::uint16_t length = 0;
            if (! r.u16 (length))
                return Read::missing;
            In q;
            if (! r.sub (length, q))
                return Read::corrupt;

            NoteSample s = defaultSample();
            bool unknown = false, complete = false;
            do
            {
                std::string str;
                if (! q.str8 (str))
                    break;
                if (! str.empty())
                {
                    s.motion = core::parseMotion (str);
                    unknown = unknown || ! s.motion.has_value();
                }
                if (! q.str8 (str))
                    break;
                if (str == "path")
                    s.travel = TravelKind::path;
                else if (str != "speed")
                    unknown = true;
                if (! q.str8 (str))
                    break;
                if (const auto mode = parseStartMode (str))
                    s.start.mode = *mode;
                else
                    unknown = true;
                if (! q.f64 (s.start.pos) || ! q.f64 (s.start.range) || ! q.i32 (s.start.seed))
                    break;
                std::int8_t t = 0;
                if (! q.i8 (t))
                    break;
                s.tape = triFrom (t, unknown);
                if (! q.i8 (t))
                    break;
                s.rev = triFrom (t, unknown);
                if (! q.str8 (str))
                    break;
                if (! str.empty())
                {
                    s.jump = core::parseRate (str);
                    unknown = unknown || ! s.jump.has_value();
                }
                if (! q.str8 (str))
                    break;
                if (! str.empty())
                {
                    s.engine = core::parseEngine (str);
                    unknown = unknown || ! s.engine.has_value();
                }
                if (! q.i8 (t))
                    break;
                s.lock = triFrom (t, unknown);
                std::uint8_t restart = 0;
                if (! q.u8 (restart))
                    break;
                s.restart = flagFrom (restart, unknown);
                complete = true;
            } while (false);

            out = s;
            if (complete)
                keepBytes (kept.trailing, q.pos(), q.stop());
            if (unknown)
                keepBytes (kept.original, begin, r.pos());
            return Read::ok;
        }

        /** A generator record: missing when its length does not fit. Unread numbers stay NaN, so normalizeGen
            gives them their defaults (and a missing d1 follows d0, as in the prototype). */
        Read readGen (In& r, Gen& out, KeptRecord& kept)
        {
            const std::uint8_t* begin = r.pos();
            std::uint16_t length = 0;
            if (! r.u16 (length))
                return Read::missing;
            In q;
            if (! r.sub (length, q))
                return Read::corrupt;

            const double missing = std::numeric_limits<double>::quiet_NaN();
            Gen g;
            g.d0 = g.d1 = g.warp = g.amount = g.smooth = missing;
            bool unknown = false, complete = false;
            do
            {
                std::string str;
                if (! q.str8 (str))
                    break;
                if (str == "random")
                    g.kind = GenKind::random;
                else if (str != "pattern")
                    unknown = true;
                if (! q.str8 (str))
                    break;
                if (const auto shape = parseGenShape (str))
                    g.shape = *shape;
                else
                    unknown = true;
                if (! q.str8 (str))
                    break;
                if (const auto rate = core::parseRate (str))
                    g.rate = *rate;
                else
                    unknown = true;
                if (! q.f64 (g.d0) || ! q.f64 (g.d1) || ! q.f64 (g.warp) || ! q.f64 (g.amount) || ! q.f64 (g.smooth))
                    break;
                std::uint8_t flag = 0;
                if (! q.u8 (flag))
                    break;
                g.lockToBeat = flagFrom (flag, unknown);
                if (! q.str8 (str))
                    break;
                if (str == "different")
                    g.perNote = PerNote::different;
                else if (str != "same")
                    unknown = true;
                if (! q.i32 (g.seed))
                    break;
                if (! q.u8 (flag))
                    break;
                g.invert = flag == 1;
                unknown = unknown || flag > 1;
                if (! q.str8 (g.key))
                    break;
                complete = true;
            } while (false);

            out = g;
            if (complete)
                keepBytes (kept.trailing, q.pos(), q.stop());
            if (unknown)
                keepBytes (kept.original, begin, r.pos());
            return Read::ok;
        }

        struct NoteBuild
        {
            Note note;
            std::shared_ptr<NoteExtras> extras = std::make_shared<NoteExtras>();
            std::array<bool, core::kNumLanes> seen {};
        };

        bool hasContent (const KeptRecord& k) noexcept { return ! k.trailing.empty() || ! k.original.empty(); }

        /** A lane record (its length always fits: the caller checked the lane count). False when it is corrupt. */
        bool readLane (In& r, NoteBuild& b, std::string& error)
        {
            const std::uint8_t* begin = r.pos();
            std::uint32_t length = 0;
            In q;
            if (! r.u32 (length) || ! r.sub (length, q))
            {
                error = "a lane record runs past its note";
                return false;
            }

            std::string id;
            const std::optional<core::LaneId> lane = q.str8 (id) ? core::findLane (id) : std::nullopt;
            if (! lane.has_value() || b.seen[static_cast<std::size_t> (*lane)])
            {
                if (b.extras->unknownLanes.size() < static_cast<std::size_t> (kMaxUnknownLanes))
                    b.extras->unknownLanes.emplace_back (begin, r.pos());
                return true;
            }
            const auto li = static_cast<std::size_t> (*lane);
            b.seen[li] = true;
            LaneExtras& lx = b.extras->lanes[li];

            NodeList nodes;
            std::optional<Gen> gen;
            bool unknown = false, complete = false;
            do
            {
                std::uint16_t count = 0;
                if (! q.u16 (count))
                    break;
                if (static_cast<std::size_t> (count) * kNodeBytes > q.left())
                {
                    error = "a lane's node count is larger than its record";
                    return false;
                }
                nodes.reserve (count);
                for (std::uint16_t i = 0; i < count; ++i)
                {
                    Node nd;
                    std::uint8_t shape = 0;
                    q.f64 (nd.t);
                    q.f64 (nd.v);
                    q.u8 (shape);
                    q.f64 (nd.bend);
                    if (shape < kNumNodeShapes)
                        nd.shape = static_cast<NodeShape> (shape);
                    else
                        unknown = true;   // reads as smooth
                    nodes.push_back (nd);
                }
                std::uint8_t hasGen = 0;
                if (! q.u8 (hasGen))
                    break;
                unknown = unknown || hasGen > 1;
                if (hasGen != 0)
                {
                    Gen g;
                    const Read got = readGen (q, g, lx.gen);
                    if (got == Read::corrupt)
                    {
                        error = "a generator record runs past its lane";
                        return false;
                    }
                    if (got == Read::missing)
                        break;
                    gen = g;
                }
                complete = true;
            } while (false);

            b.note.c[li] = std::move (nodes);
            b.note.gen[li] = gen;
            if (complete)
                keepBytes (lx.lane.trailing, q.pos(), q.stop());
            if (unknown)
                keepBytes (lx.lane.original, begin, r.pos());
            return true;
        }

        bool readNote (In& in, NoteBuild& b, std::string& error)
        {
            std::uint32_t length = 0;
            In r;
            if (! in.u32 (length) || ! in.sub (length, r))
            {
                error = "a note record runs past the clip";
                return false;
            }

            Note& n = b.note;   // default-constructed: no lane nodes, so a lane the record does not hold is flat at its default
            bool complete = false;
            do
            {
                double glide = 0.0;
                if (! r.str8 (n.id) || ! r.f64 (n.p) || ! r.f64 (n.s) || ! r.f64 (n.l) || ! r.f64 (n.vel) || ! r.f64 (glide))
                    break;
                if (! std::isnan (glide))
                    n.glide = glide;
                if (! r.f64 (n.gc) || ! r.str8 (n.glideTo))
                    break;
                const Read sample = readSample (r, n.sample, b.extras->sample);
                if (sample == Read::corrupt)
                {
                    error = "a sample settings record runs past its note";
                    return false;
                }
                if (sample == Read::missing)
                    break;
                std::uint8_t laneCount = 0;
                if (! r.u8 (laneCount))
                    break;
                if (laneCount > kMaxLanesPerNote)
                {
                    error = "a note holds more than 64 lanes";
                    return false;
                }
                if (static_cast<std::size_t> (laneCount) * 4 > r.left())
                {
                    error = "a note's lane count is larger than its record";
                    return false;
                }
                for (int k = 0; k < laneCount; ++k)
                    if (! readLane (r, b, error))
                        return false;
                complete = true;
            } while (false);

            if (complete)
                keepBytes (b.extras->noteTrailing, r.pos(), r.stop());
            return true;
        }

        bool needsExtras (const NoteExtras& x) noexcept
        {
            if (! x.noteTrailing.empty() || hasContent (x.sample) || ! x.unknownLanes.empty())
                return true;
            for (const auto& lane : x.lanes)
                if (hasContent (lane.lane) || hasContent (lane.gen))
                    return true;
            return false;
        }
    }

    //==============================================================================================
    std::vector<std::uint8_t> encodeClip (const Clip& clip)
    {
        std::vector<std::uint8_t> bytes;
        bytes.reserve (encodedClipSize (clip));   // one allocation of the exact size
        Out o (&bytes);
        writeHeader (o, clip);
        for (const auto& n : clip.notes)
            writeNote (o, *n);
        if (clip.extras != nullptr)
            o.bytes (clip.extras->payloadTrailing);
        return bytes;
    }

    std::size_t encodedNoteSize (const Note& note)
    {
        Out o (nullptr);
        writeNote (o, note);
        return o.size();
    }

    std::size_t encodedHeaderSize (const Clip& clip)
    {
        Out o (nullptr);
        writeHeader (o, clip);
        return o.size() + (clip.extras != nullptr ? clip.extras->payloadTrailing.size() : 0);
    }

    std::size_t encodedClipSize (const Clip& clip)
    {
        std::size_t total = encodedHeaderSize (clip);
        for (const auto& n : clip.notes)
            total += encodedNoteSize (*n);
        return total;
    }

    DecodeResult decodeClip (const std::vector<std::uint8_t>& bytes)
    {
        return decodeClip (bytes.data(), bytes.size());
    }

    DecodeResult decodeClip (const std::uint8_t* data, std::size_t size)
    {
        DecodeResult result;
        if (size > kMaxClipBytes)
        {
            result.error = "the clip is larger than 32 MiB";
            return result;
        }
        In in (data, data != nullptr ? size : 0);

        Clip raw;
        raw.bars = 4;
        raw.beatsPerBar = kDefaultBeatsPerBar;
        auto clipExtras = std::make_shared<ClipExtras>();
        {
            std::uint32_t length = 0;
            In q;
            if (! in.u32 (length) || ! in.sub (length, q))
            {
                result.error = "the clip settings record is cut short";
                return result;
            }
            bool complete = false;
            do
            {
                std::int32_t bars = 0, beatsPerBar = 0;
                std::uint8_t loop = 0, tie = 0;
                if (! q.i32 (bars))
                    break;
                raw.bars = bars;
                if (! q.i32 (beatsPerBar))
                    break;
                raw.beatsPerBar = beatsPerBar;
                if (! q.f64 (raw.offsetBeats))
                    break;
                if (! q.u8 (loop))
                    break;
                raw.loop = loop != 0;
                if (! q.u8 (tie))
                    break;
                raw.tieAcrossLoop = tie != 0;
                complete = true;
            } while (false);
            if (complete)
                keepBytes (clipExtras->settingsTrailing, q.pos(), q.stop());
        }

        std::uint32_t noteCount = 0;
        if (! in.u32 (noteCount))
        {
            result.error = "the note count is missing";
            return result;
        }
        if (noteCount > static_cast<std::uint32_t> (kMaxClipNotes))
        {
            result.error = "the clip holds more than 4096 notes";
            return result;
        }
        if (static_cast<std::size_t> (noteCount) * 4 > in.left())
        {
            result.error = "the note count is larger than the clip";
            return result;
        }

        std::vector<std::shared_ptr<NoteExtras>> extras (noteCount);
        raw.notes.reserve (noteCount);
        for (std::uint32_t i = 0; i < noteCount; ++i)
        {
            NoteBuild b;
            if (! readNote (in, b, result.error))
                return result;
            if (needsExtras (*b.extras))
            {
                b.note.extras = b.extras;
                extras[i] = b.extras;
            }
            raw.notes.push_back (std::make_shared<const Note> (std::move (b.note)));
        }
        keepBytes (clipExtras->payloadTrailing, in.pos(), in.stop());
        if (! clipExtras->settingsTrailing.empty() || ! clipExtras->payloadTrailing.empty())
            raw.extras = clipExtras;

        Clip clip = normalizeClip (raw);   // the same notes in the same order, each rebuilt by normalizeNote
        if (encodedClipSize (clip) > kMaxClipBytes)
        {
            // possible when a payload near the cap leaves out lanes the encoder always writes: refused, so a
            // clip that loads can always be saved again
            result.error = "the clip would encode to more than 32 MiB";
            return result;
        }
        for (std::size_t i = 0; i < clip.notes.size(); ++i)
        {
            NoteExtras* x = extras[i].get();
            if (x == nullptr)
                continue;
            const Note& q = *clip.notes[i];
            x->decodedSample = q.sample;
            for (std::size_t lane = 0; lane < x->lanes.size(); ++lane)
            {
                x->lanes[lane].decodedNodes = q.c[lane];
                x->lanes[lane].decodedGen = q.gen[lane];
            }
        }
        result.clip = std::move (clip);
        return result;
    }

    //==============================================================================================
    std::size_t ClipSizer::measure (const Clip& clip)
    {
        std::unordered_map<std::uint64_t, std::size_t> next;
        next.reserve (clip.notes.size());
        std::size_t total = encodedHeaderSize (clip);
        for (const auto& n : clip.notes)
        {
            const std::uint64_t serial = n->serial.get();
            std::size_t bytes = 0;
            if (const auto it = sizes.find (serial); it != sizes.end())
                bytes = it->second;
            else if (const auto again = next.find (serial); again != next.end())
                bytes = again->second;
            else
                bytes = encodedNoteSize (*n);
            next[serial] = bytes;
            total += bytes;
        }
        sizes = std::move (next);
        return total;
    }

    EditResult limitEdit (const Clip& before, EditResult proposed, ClipSizer& sizer)
    {
        if (proposed.refused)
            return proposed;
        const std::size_t notes = proposed.clip.notes.size();
        const std::size_t bytes = notes > static_cast<std::size_t> (kMaxClipNotes) ? 0 : sizer.measure (proposed.clip);
        if (notes <= static_cast<std::size_t> (kMaxClipNotes) && bytes <= kMaxClipBytes)
        {
            proposed.bytes = bytes;
            return proposed;
        }
        EditResult refused;
        refused.clip = before;
        refused.refused = true;
        refused.count = static_cast<double> (notes);
        refused.bytes = bytes;
        return refused;
    }

    EditResult limitEdit (const Clip& before, Clip proposed, ClipSizer& sizer)
    {
        EditResult r;
        r.clip = std::move (proposed);
        r.count = static_cast<double> (r.clip.notes.size());
        return limitEdit (before, std::move (r), sizer);
    }
}
