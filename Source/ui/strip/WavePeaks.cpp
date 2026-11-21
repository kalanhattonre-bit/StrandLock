#include "WavePeaks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace sl::ui
{
    namespace
    {
        /** The cache: a few sounds' peaks, each with a weak hold on the samples it was made from. */
        struct Entry
        {
            std::weak_ptr<const void> storage;
            const void* samples = nullptr;   // the left channel (float or 16-bit)
            std::int64_t frames = 0;
            std::shared_ptr<const WavePeaks> peaks;
        };

        constexpr std::size_t kCacheSize = 8;

        struct Cache
        {
            std::mutex lock;
            std::vector<Entry> entries;   // most recently used last
            std::int64_t made = 0;
        };

        Cache& cache()
        {
            static Cache c;
            return c;
        }

        const void* samplesOf (const doc::SoundView& v) noexcept
        {
            return v.format == engine::SampleFormat::i16 ? static_cast<const void*> (v.left16) : static_cast<const void*> (v.left);
        }
    }

    template <class Mid>
    void WavePeaks::build (const Mid& mid)
    {
        const auto nb = static_cast<std::size_t> ((n + kBlock - 1) / kBlock);   // Math.ceil (n / B)
        mn.assign (nb, 0.0f);
        mx.assign (nb, 0.0f);
        for (std::size_t b = 0; b < nb; ++b)
        {
            double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
            const std::int64_t e = std::min<std::int64_t> (n, static_cast<std::int64_t> (b + 1) * kBlock);
            for (std::int64_t i = static_cast<std::int64_t> (b) * kBlock; i < e; ++i)
            {
                const double v = mid (i);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            mn[b] = std::isinf (lo) ? 0.0f : (float) lo;
            mx[b] = std::isinf (hi) ? 0.0f : (float) hi;
        }
    }

    WavePeaks::WavePeaks (const float* left, const float* right, std::int64_t frames)
        : n (left != nullptr && frames > 0 ? frames : 0)
    {
        if (n == 0)
            return;
        const float* R = right != nullptr ? right : left;
        build ([left, R] (std::int64_t i) { return ((double) left[i] + (double) R[i]) * 0.5; });
    }

    WavePeaks::WavePeaks (const doc::SoundView& view)
        : n (view.hasSamples() && view.frames > 0 ? view.frames : 0)
    {
        if (n == 0)
            return;
        build ([&view] (std::int64_t i) { return view.mid (i); });
    }

    std::shared_ptr<const WavePeaks> WavePeaks::of (const std::shared_ptr<const doc::SoundView>& view)
    {
        if (view == nullptr || ! view->hasSamples() || view->frames <= 0)
            return nullptr;

        const void* samples = samplesOf (*view);
        auto& c = cache();
        {
            const std::lock_guard<std::mutex> hold (c.lock);
            // Sounds that are gone take their peaks with them.
            c.entries.erase (std::remove_if (c.entries.begin(), c.entries.end(), [] (const Entry& e) { return e.storage.expired(); }),
                             c.entries.end());
            for (auto it = c.entries.begin(); it != c.entries.end(); ++it)
            {
                if (it->samples == samples && it->frames == view->frames && it->storage.lock() == view->storage)
                {
                    auto peaks = it->peaks;
                    std::rotate (it, it + 1, c.entries.end());   // the newest use last
                    return peaks;
                }
            }
        }

        auto peaks = std::make_shared<const WavePeaks> (*view);
        const std::lock_guard<std::mutex> hold (c.lock);
        ++c.made;
        if (view->storage != nullptr)
        {
            c.entries.push_back ({ view->storage, samples, view->frames, peaks });
            while (c.entries.size() > kCacheSize)
                c.entries.erase (c.entries.begin());
        }
        return peaks;
    }

    int WavePeaks::cachedSounds()
    {
        auto& c = cache();
        const std::lock_guard<std::mutex> hold (c.lock);
        int live = 0;
        for (const auto& e : c.entries)
            live += e.storage.expired() ? 0 : 1;
        return live;
    }

    std::int64_t WavePeaks::peaksMade()
    {
        auto& c = cache();
        const std::lock_guard<std::mutex> hold (c.lock);
        return c.made;
    }

    template <class Mid>
    std::vector<WavePeaks::Column> WavePeaks::columnsOf (const Mid& mid, double a, double b, int width) const
    {
        std::vector<Column> out (static_cast<std::size_t> (std::max (0, width)));
        if (n == 0 || width <= 0)
            return out;
        const double W = (double) width, len = (double) n;
        const auto nb = static_cast<std::int64_t> (mn.size());
        for (int x = 0; x < width; ++x)
        {
            // renderWave: i0 = floor ((a + (b - a) * x / W) * n), i1 = max (i0 + 1, floor ((a + (b - a) * (x + 1) / W) * n))
            const auto i0 = static_cast<std::int64_t> (std::floor ((a + (b - a) * (double) x / W) * len));
            const auto i1 = std::max<std::int64_t> (i0 + 1, static_cast<std::int64_t> (std::floor ((a + (b - a) * (double) (x + 1) / W) * len)));
            double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
            if (i1 - i0 >= 2 * kBlock)
            {
                const std::int64_t end = std::min<std::int64_t> (nb, (i1 + kBlock - 1) / kBlock);   // Math.ceil (i1 / B)
                for (std::int64_t blk = std::max<std::int64_t> (0, i0 / kBlock); blk < end; ++blk)
                {
                    if (mn[(std::size_t) blk] < lo) lo = mn[(std::size_t) blk];
                    if (mx[(std::size_t) blk] > hi) hi = mx[(std::size_t) blk];
                }
            }
            else
            {
                for (std::int64_t i = std::max<std::int64_t> (0, i0); i < std::min (n, i1); ++i)
                {
                    const double v = mid (i);
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
            }
            if (! (hi >= lo))
                continue;
            auto& col = out[(std::size_t) x];
            col.lo = (float) lo;
            col.hi = (float) hi;
            col.drawn = true;
        }
        return out;
    }

    std::vector<WavePeaks::Column> WavePeaks::columns (const float* left, const float* right, double a, double b, int width) const
    {
        if (left == nullptr)
            return std::vector<Column> (static_cast<std::size_t> (std::max (0, width)));
        const float* R = right != nullptr ? right : left;
        return columnsOf ([left, R] (std::int64_t i) { return ((double) left[i] + (double) R[i]) * 0.5; }, a, b, width);
    }

    std::vector<WavePeaks::Column> WavePeaks::columns (const doc::SoundView& view, double a, double b, int width) const
    {
        if (! view.hasSamples() || view.frames != n)
            return std::vector<Column> (static_cast<std::size_t> (std::max (0, width)));
        return columnsOf ([&view] (std::int64_t i) { return view.mid (i); }, a, b, width);
    }
}
