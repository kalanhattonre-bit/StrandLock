#include "Clip.h"

#include "Num.h"
#include "core/JsCompat.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace sl::model
{
    namespace
    {
        template <class E, std::size_t N>
        const char* codeOf (E value, const char* const (&codes)[N]) noexcept
        {
            const auto i = static_cast<std::size_t> (value);
            return i < N ? codes[i] : "";
        }

        template <class E, std::size_t N>
        std::optional<E> parseOf (std::string_view text, const char* const (&codes)[N]) noexcept
        {
            for (std::size_t i = 0; i < N; ++i)
                if (text == codes[i])
                    return static_cast<E> (i);
            return std::nullopt;
        }

        std::atomic<std::uint64_t> serialCounter { 0 };

        /** Every lane flat at its default (mkNote's c). */
        const std::array<NodeList, core::kNumLanes>& flatDefaults()
        {
            static const std::array<NodeList, core::kNumLanes> lists = []
            {
                std::array<NodeList, core::kNumLanes> a;
                for (int i = 0; i < core::kNumLanes; ++i)
                    a[static_cast<std::size_t> (i)] = flat (core::kLanes[i].def);
                return a;
            }();
            return lists;
        }

        // a.s - b.s || a.p - b.p || ...: a difference that is 0 or NaN goes on to the next key.
        int signOf (double d) noexcept
        {
            if (d == 0.0 || std::isnan (d))
                return 0;
            return d < 0.0 ? -1 : 1;
        }
    }

    const char* code (GenKind v) noexcept    { return codeOf (v, kGenKindCodes); }
    const char* code (GenShape v) noexcept   { return codeOf (v, kGenShapeCodes); }
    const char* code (PerNote v) noexcept    { return codeOf (v, kPerNoteCodes); }
    const char* code (StartMode v) noexcept  { return codeOf (v, kStartModeCodes); }
    const char* code (TravelKind v) noexcept { return codeOf (v, kTravelKindCodes); }

    std::optional<GenKind> parseGenKind (std::string_view s) noexcept       { return parseOf<GenKind> (s, kGenKindCodes); }
    std::optional<GenShape> parseGenShape (std::string_view s) noexcept     { return parseOf<GenShape> (s, kGenShapeCodes); }
    std::optional<PerNote> parsePerNote (std::string_view s) noexcept       { return parseOf<PerNote> (s, kPerNoteCodes); }
    std::optional<StartMode> parseStartMode (std::string_view s) noexcept   { return parseOf<StartMode> (s, kStartModeCodes); }
    std::optional<TravelKind> parseTravelKind (std::string_view s) noexcept { return parseOf<TravelKind> (s, kTravelKindCodes); }

    std::uint64_t NoteSerial::next() noexcept
    {
        return serialCounter.fetch_add (1, std::memory_order_relaxed) + 1;
    }

    NoteSample defaultSample() noexcept
    {
        return NoteSample {};
    }

    Note mkNote (double p, double s, double l, std::string id)
    {
        Note n;
        n.id = std::move (id);
        n.p = p;
        n.s = s;
        n.l = l;
        n.vel = 0.8;
        n.glide.reset();
        n.gc = 0.0;
        n.glideTo.clear();
        n.c = flatDefaults();
        for (auto& g : n.gen)
            g.reset();
        n.sample = defaultSample();
        return n;
    }

    Clip newClip (double bars)
    {
        Clip clip;
        clip.bars = static_cast<int> (clamp (core::jsRound (num (bars, 4.0)), static_cast<double> (kMinBars), static_cast<double> (kMaxBars)));
        return clip;
    }

    Note cloneNote (const Note& n)
    {
        Note c = n;   // a fresh serial; the extras pointer is shared
        for (std::size_t i = 0; i < c.c.size(); ++i)
            if (c.c[i].empty())
                c.c[i] = flatDefaults()[i];
        return c;
    }

    Clip cloneClip (const Clip& clip)
    {
        Clip out = clip;
        for (auto& n : out.notes)
            n = std::make_shared<const Note> (cloneNote (*n));
        return out;
    }

    const NodeList& noteLane (const Note& n, core::LaneId lane) noexcept
    {
        const NodeList& nd = n.lane (lane);
        return nd.empty() ? flatDefaults()[static_cast<std::size_t> (lane)] : nd;
    }

    int timeCmp (const Note& a, const Note& b) noexcept
    {
        if (const int ds = signOf (a.s - b.s); ds != 0)
            return ds;
        if (const int dp = signOf (a.p - b.p); dp != 0)
            return dp;
        return core::jsStringLess (a.id, b.id) ? -1 : core::jsStringLess (b.id, a.id) ? 1 : 0;
    }

    std::vector<NotePtr> timeOrder (std::vector<NotePtr> notes)
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const NotePtr& a, const NotePtr& b) { return timeCmp (*a, *b) < 0; });
        return notes;
    }

    namespace
    {
        bool sameNumber (double a, double b) noexcept
        {
            return a == b || (std::isnan (a) && std::isnan (b));
        }

        bool sameSample (const NoteSample& a, const NoteSample& b) noexcept
        {
            return a.motion == b.motion && a.travel == b.travel && a.start.mode == b.start.mode
                && sameNumber (a.start.pos, b.start.pos) && sameNumber (a.start.range, b.start.range) && a.start.seed == b.start.seed
                && a.tape == b.tape && a.rev == b.rev && a.jump == b.jump && a.engine == b.engine && a.lock == b.lock
                && a.restart == b.restart;
        }
    }

    bool sameNodes (const NodeList& a, const NodeList& b) noexcept
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (! sameNumber (a[i].t, b[i].t) || ! sameNumber (a[i].v, b[i].v) || a[i].shape != b[i].shape || ! sameNumber (a[i].bend, b[i].bend))
                return false;
        return true;
    }

    bool sameGen (const Gen& a, const Gen& b) noexcept
    {
        return a.kind == b.kind && a.shape == b.shape && a.rate == b.rate && sameNumber (a.d0, b.d0) && sameNumber (a.d1, b.d1)
            && sameNumber (a.warp, b.warp) && a.lockToBeat == b.lockToBeat && sameNumber (a.amount, b.amount)
            && sameNumber (a.smooth, b.smooth) && a.perNote == b.perNote && a.seed == b.seed && a.invert == b.invert && a.key == b.key;
    }

    bool sameContent (const Note& a, const Note& b) noexcept
    {
        if (a.id != b.id || ! sameNumber (a.p, b.p) || ! sameNumber (a.s, b.s) || ! sameNumber (a.l, b.l) || ! sameNumber (a.vel, b.vel)
            || a.glide.has_value() != b.glide.has_value() || (a.glide && ! sameNumber (*a.glide, *b.glide))
            || ! sameNumber (a.gc, b.gc) || a.glideTo != b.glideTo || ! sameSample (a.sample, b.sample))
            return false;
        for (std::size_t i = 0; i < a.c.size(); ++i)
        {
            if (! sameNodes (a.c[i], b.c[i]))
                return false;
            if (a.gen[i].has_value() != b.gen[i].has_value() || (a.gen[i] && ! sameGen (*a.gen[i], *b.gen[i])))
                return false;
        }
        return true;
    }
}
