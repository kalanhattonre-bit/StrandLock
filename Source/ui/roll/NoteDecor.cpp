#include "NoteDecor.h"

#include "core/JsCompat.h"
#include "core/Rates.h"
#include "core/Seeds.h"
#include "core/Travel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace sl::ui::roll
{
    namespace
    {
        // sl-ui-panels.js clamp, frac and Math.max / Math.min (NaN when either is NaN)
        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }
        double frac (double x) noexcept { return x - std::floor (x); }

        double jsMax (double a, double b) noexcept
        {
            if (std::isnan (a) || std::isnan (b))
                return std::numeric_limits<double>::quiet_NaN();
            return a > b ? a : b;
        }

        double jsMin (double a, double b) noexcept
        {
            if (std::isnan (a) || std::isnan (b))
                return std::numeric_limits<double>::quiet_NaN();
            return a < b ? a : b;
        }

        /** JavaScript's a !== b for two numbers: true when they differ, or when either is NaN (+0 and -0 are equal). */
        bool jsNotEqual (double a, double b) noexcept { return a < b || a > b || std::isnan (a) || std::isnan (b); }

        /** Two doubles with the same bits (NaN equal to NaN), for the caches' keys. */
        bool sameBits (double a, double b) noexcept
        {
            std::uint64_t x = 0, y = 0;
            std::memcpy (&x, &a, sizeof x);
            std::memcpy (&y, &b, sizeof y);
            return x == y;
        }

        /** FNV-1a 64 over the bytes of the values added. */
        struct Fnv64
        {
            std::uint64_t h = 14695981039346656037ull;

            void bytes (const void* p, std::size_t n) noexcept
            {
                const auto* b = static_cast<const unsigned char*> (p);
                for (std::size_t i = 0; i < n; ++i)
                {
                    h ^= (std::uint64_t) b[i];
                    h *= 1099511628211ull;
                }
            }

            void num (double v) noexcept { bytes (&v, sizeof v); }
            void num (std::int64_t v) noexcept { bytes (&v, sizeof v); }
        };
    }

    //==============================================================================================
    // sl-ui-panels.js's pure helpers.

    double tabAt (const std::vector<float>& a, double t) noexcept
    {
        const std::size_t L = a.size();
        if (L < 2)
            return L != 0 ? (double) a[0] : 0.5;
        if (std::isnan (t))
            return std::numeric_limits<double>::quiet_NaN();   // a[NaN] is undefined: the prototype's sum is NaN
        const double f = clampD (t, 0.0, 1.0) * (double) (L - 1);
        const std::size_t i = std::min (L - 2, (std::size_t) std::floor (f));
        return (double) a[i] + ((double) a[i + 1] - (double) a[i]) * (f - (double) i);
    }

    NoteSim simulate (const model::Note& n, const core::Inst& I, double bpm, double secs, const std::vector<float>& T,
                      const std::vector<float>& P, int N)
    {
        N = std::max (1, N);
        const auto& R = I.region;
        const double regLen = jsMax (1e-6, R.end - R.start), regSec = jsMax (1e-3, regLen * jsMax (0.01, secs));
        const double noteSec = jsMax (1e-6, n.l * 60.0 / jsMax (1.0, bpm)), dt = noteSec / (double) N;
        const core::Engine engine = n.sample.engine.value_or (I.engine);
        const bool tape = engine == core::Engine::tape;
        core::Motion motion = n.sample.motion.value_or (I.motion);
        if (tape && motion == core::Motion::freeze)
            motion = core::Motion::forward;
        const bool path = n.sample.travel == model::TravelKind::path && ! tape;
        const double spd = I.speed * std::pow (2.0, (I.macros[0] - 0.5) * 4.0);
        const auto& st = n.sample.start;
        double u = st.mode == model::StartMode::song     ? frac ((n.s * 60.0 / jsMax (1.0, bpm)) / regSec)
                 : st.mode == model::StartMode::random   ? clampD (st.pos + st.range * 0.5, 0.0, 1.0)
                                                          : st.pos;
        const double jumpB = jsMax (1e-3, core::rateBeats (n.sample.jump.value_or (I.jump)));
        const std::uint32_t idh = core::idHash (std::string_view (n.id));
        double lastJ = std::floor (n.s / jumpB + 1e-9);
        bool silent = false;

        NoteSim out;
        out.N = N;
        const auto count = (std::size_t) N + 1;
        out.pos.assign (count, 0.0f);
        out.dir.assign (count, 0);
        out.sp.assign (count, 0.0f);
        out.jump.assign (count, 0);
        for (int i = 0; i <= N; ++i)
        {
            const auto k = (std::size_t) i;
            const double t = (double) i / (double) N;
            double vel = 0.0;
            if (path)
            {
                const double ta = jsMax (0.0, t - 1.0 / (double) N), tb = jsMin (1.0, t + 1.0 / (double) N);
                u = tabAt (T, t);
                vel = (tabAt (T, tb) - tabAt (T, ta)) / jsMax (1e-9, (tb - ta) * noteSec) * regSec;
            }
            else
            {
                // Tape plays Path as 1x; the Speed kind reads its Travel line as a Float32 (Math.fround)
                double s = n.sample.travel == model::TravelKind::path ? 1.0 : core::travelSpeed ((double) (float) tabAt (T, t));
                s *= spd;
                if (motion == core::Motion::backward)
                    s = -s;
                else if (motion == core::Motion::freeze)
                    s = 0.0;
                if (tape)
                    s *= std::pow (2.0, (n.p + (tabAt (P, t) - 0.5) * 24.0 + I.pitch - I.root - I.cents / 100.0) / 12.0);
                vel = clampD (s, -16.0, 16.0);
                if (i > 0)
                    u += vel * dt / regSec;
                if (motion == core::Motion::scatter && i < N)   // a jump exactly at the note's end is not inside it
                {
                    const double j = std::floor ((n.s + t * n.l) / jumpB + 1e-9);
                    if (jsNotEqual (j, lastJ))
                    {
                        lastJ = j;
                        u = core::rand01 (core::hash32 (idh, j, I.seed, 0x5ca7));
                        out.jump[k] = 1;
                    }
                }
            }
            double ue = u;
            int d = vel > 1e-9 ? 1 : vel < -1e-9 ? -1 : 0;
            if (motion == core::Motion::bounce && ! path)
            {
                const double w = u - 2.0 * std::floor (u / 2.0);
                if (w > 1.0)
                {
                    ue = 2.0 - w;
                    d = -d;
                }
                else
                {
                    ue = w;
                }
            }
            else if (R.atEnd == core::AtEnd::loop && ! path)
            {
                ue = frac (u);
            }
            else if (u > 1.0 || u < 0.0)
            {
                if (R.atEnd == core::AtEnd::hold || path)
                {
                    ue = clampD (u, 0.0, 1.0);
                    d = 0;
                }
                else
                {
                    silent = true;
                }
            }
            out.pos[k] = silent ? std::numeric_limits<float>::quiet_NaN() : (float) (R.start + ue * regLen);
            out.dir[k] = (std::int8_t) (silent ? 0 : d);
            out.sp[k] = silent || d == 0 ? 0.0f : (float) std::abs (vel);
        }
        return out;
    }

    std::vector<float> peakBins (const float* left, const float* right, std::int64_t frames)
    {
        if (left == nullptr || frames <= 0)
            return std::vector<float> (1, 0.0f);   // Math.ceil (0 / 128) = 0: one bin, empty
        const float* R = right != nullptr ? right : left;
        const auto n = (double) frames;
        const std::int64_t NB = std::max<std::int64_t> (1, std::min<std::int64_t> (65536, (std::int64_t) std::ceil (n / 128.0)));
        const double step = n / (double) NB;
        std::vector<float> bins ((std::size_t) NB, 0.0f);
        double mx = 0.0;
        for (std::int64_t b = 0; b < NB; ++b)
        {
            const auto i1 = std::min (frames, (std::int64_t) std::floor ((double) (b + 1) * step));
            double pk = 0.0;
            for (auto i = (std::int64_t) std::floor ((double) b * step); i < i1; ++i)
            {
                double v = ((double) left[i] + (double) R[i]) * 0.5;
                if (v < 0.0)
                    v = -v;
                if (v > pk)
                    pk = v;
            }
            bins[(std::size_t) b] = (float) pk;   // a Float32Array keeps the float; the maximum stays a double
            if (pk > mx)
                mx = pk;
        }
        if (mx > 0.0)
            for (auto& v : bins)
                v = (float) ((double) v / mx);
        return bins;
    }

    std::vector<float> thumbAmps (const NoteSim& sim, const std::vector<float>& bins, double secs, double regLen)
    {
        const int N = sim.N;
        std::vector<float> out ((std::size_t) std::max (0, N), 0.0f);
        if (bins.empty() || (int) sim.pos.size() < N + 1)
            return out;
        const auto NB = (std::int64_t) bins.size();
        const double sliver = std::min (regLen, 0.06 / jsMax (0.01, secs));
        const auto binOf = [NB] (double x, std::int64_t lo)
        {
            const double f = std::floor (x * (double) NB);
            const double c = clampD (f, (double) lo, (double) (NB - 1));
            return (std::int64_t) c;
        };
        for (int i = 0; i < N; ++i)
        {
            const auto k = (std::size_t) i;
            const double a = (double) sim.pos[k], b = (double) sim.pos[k + 1];
            if (std::isnan (a))
            {
                out[k] = std::numeric_limits<float>::quiet_NaN();
                continue;
            }
            double lo = 0.0, hi = 0.0;
            if ((double) sim.sp[k] < 0.03)   // the Float32 read as a number, against the double 0.03
                lo = hi = a + ((double) (i % 5) / 5.0) * sliver;   // frozen: a short sliver repeats
            else if (std::isnan (b) || sim.jump[k + 1] != 0 || std::abs (b - a) > regLen * 0.5)
                lo = hi = a;
            else
            {
                lo = std::min (a, b);
                hi = std::max (a, b);
            }
            const std::int64_t b0 = binOf (lo, 0), b1 = binOf (hi, b0);
            const std::int64_t stp = std::max<std::int64_t> (1, (std::int64_t) std::ceil ((double) (b1 - b0 + 1) / 24.0));
            float pk = 0.0f;
            for (std::int64_t j = b0; j <= b1; j += stp)
                if (bins[(std::size_t) j] > pk)
                    pk = bins[(std::size_t) j];
            out[k] = pk;
        }
        return out;
    }

    int simPoints (double noteWidth) noexcept
    {
        const double r = core::jsRound (noteWidth / 3.0);
        return (int) clampD (std::isnan (r) ? 4.0 : r, 4.0, 200.0);
    }

    float thumbAlpha (int mode, bool selected) noexcept
    {
        return mode == 8 || mode == 1 ? (selected ? 0.42f : 0.24f) : (selected ? 0.22f : 0.11f);
    }

    float chevronStrength (int mode, bool selected) noexcept
    {
        return mode == 8 ? (selected ? 1.0f : 0.8f) : (selected ? 0.8f : 0.5f);
    }

    bool hasOwnSettings (const model::Note& n) noexcept
    {
        const auto& s = n.sample;
        return s.engine.has_value() || s.motion.has_value() || s.tape.has_value() || s.rev.has_value() || s.jump.has_value()
            || s.lock.has_value();
    }

    bool slideWarns (const model::Note& n, const model::Note& m) noexcept
    {
        return m.s > n.s + 1e-6 && m.s < n.s + n.l - 0.13;
    }

    //==============================================================================================
    void NoteDecor::setSound (const std::shared_ptr<const doc::SoundView>& s)
    {
        if (s == sound)
            return;
        sound = s;
        peaks.clear();
        haveBins = false;
        secs = 6.0;
        if (sound != nullptr)
        {
            if (sound->seconds() > 0.0)
                secs = sound->seconds();
            if (sound->left != nullptr && sound->frames > 0)
            {
                peaks = peakBins (sound->left, sound->right, sound->frames);
                haveBins = true;
            }
        }
    }

    void NoteDecor::setClip (const model::ClipPtr& c)
    {
        if (c == clip)
            return;
        clip = c;
        if (clip == nullptr)
        {
            entries.clear();
            return;
        }
        std::unordered_set<std::uint64_t> keep;
        keep.reserve (clip->notes.size());
        for (const auto& n : clip->notes)
            keep.insert (n->serial.get());
        for (auto it = entries.begin(); it != entries.end();)
            it = keep.count (it->first) != 0 ? std::next (it) : entries.erase (it);
    }

    const NoteDecor::Entry& NoteDecor::entry (const model::Note& n, const core::Inst& I, double bpm, const std::vector<float>& travel,
                                              const std::vector<float>& pitch, int N, double tableRes)
    {
        auto& e = entries[n.serial.get()];
        const auto& R = I.region;
        const void* soundNow = haveBins ? (const void*) peaks.data() : nullptr;
        const bool same = e.N == N && e.sound == soundNow && sameBits (e.regionStart, R.start) && sameBits (e.regionEnd, R.end)
                       && e.atEnd == (int) R.atEnd && sameBits (e.speed, I.speed) && sameBits (e.macro0, I.macros[0])
                       && e.motion == (int) I.motion && e.engine == (int) I.engine && e.jump == (int) I.jump && e.seed == I.seed
                       && sameBits (e.root, I.root) && sameBits (e.cents, I.cents) && sameBits (e.pitch, I.pitch) && sameBits (e.bpm, bpm)
                       && sameBits (e.secs, secs) && sameBits (e.tableRes, tableRes) && ! e.sim.pos.empty();
        if (same)
            return e;
        e.regionStart = R.start;
        e.regionEnd = R.end;
        e.atEnd = (int) R.atEnd;
        e.speed = I.speed;
        e.macro0 = I.macros[0];
        e.motion = (int) I.motion;
        e.engine = (int) I.engine;
        e.jump = (int) I.jump;
        e.seed = I.seed;
        e.root = I.root;
        e.cents = I.cents;
        e.pitch = I.pitch;
        e.bpm = bpm;
        e.secs = secs;
        e.tableRes = tableRes;
        e.N = N;
        e.sound = soundNow;
        e.sim = simulate (n, I, bpm, secs, travel, pitch, N);
        if (haveBins)
            e.amps = thumbAmps (e.sim, peaks, secs, R.end - R.start);
        else
            e.amps.clear();
        ++made;
        return e;
    }

    std::uint64_t NoteDecor::key (const core::Inst& I) const noexcept
    {
        // what simulate reads, the Mirror chevrons' heads, and the sound
        Fnv64 h;
        const auto& R = I.region;
        for (const double v : { R.start, R.end, I.speed, I.macros[0], I.macros[2], I.heads, I.root, I.cents, I.pitch, secs })
            h.num (v);
        for (const std::int64_t v : { (std::int64_t) R.atEnd, (std::int64_t) I.motion, (std::int64_t) I.engine, (std::int64_t) I.jump,
                                      (std::int64_t) I.seed, (std::int64_t) (std::uintptr_t) (haveBins ? peaks.data() : nullptr) })
            h.num (v);
        return h.h;
    }
}
