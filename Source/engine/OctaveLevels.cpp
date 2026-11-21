// The octave levels (M7 V4; OctaveLevels.h): the read kernel and the decimation filter (built at static-init time),
// and the builder the SampleManager's worker runs after a decode.

#include "engine/OctaveLevels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace sl::engine
{
    namespace detail
    {
        namespace
        {
            constexpr double kPiD = 3.14159265358979323846;

            /** The modified Bessel function I0 (the Kaiser window), by its series. */
            double besselI0 (double x) noexcept
            {
                double sum = 1, term = 1;
                for (int k = 1; k < 64; ++k)
                {
                    const double q = x / (2.0 * k);
                    term *= q * q;
                    sum += term;
                    if (term < 1e-21 * sum)
                        break;
                }
                return sum;
            }

            /** sin (pi t) / (pi t), exactly 1 at 0 and exactly 0 at every other whole number. */
            double sincPi (double t) noexcept
            {
                const double whole = std::round (t);
                if (std::abs (t - whole) < 1e-12)
                    return std::abs (whole) < 0.5 ? 1.0 : 0.0;
                return std::sin (kPiD * t) / (kPiD * t);
            }

            /** The Kaiser window over -half .. half at t (its ends included), 0 outside. */
            double kaiser (double t, double half, double beta) noexcept
            {
                const double u = t / half;
                if (! (u >= -1.0 && u <= 1.0))
                    return 0.0;
                return besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - u * u))) / besselI0 (beta);
            }
        }

        LevelTables::LevelTables() noexcept
        {
            // The read: a sinc in a Kaiser window (beta 5) over 8 frames; row k is the fraction f = k / 512, the taps those
            // of frames i - 3 .. i + 4, each row scaled to sum to 1 so a steady value reads as itself. Rows 0 and 512 are
            // a single 1 (f = 0 reads frame i exactly, f = 1 frame i + 1).
            constexpr double kernelBeta = 5.0;
            constexpr double kernelHalf = kLevelTaps / 2.0;
            for (int k = 0; k <= kLevelPhases; ++k)
            {
                const double f = static_cast<double> (k) / kLevelPhases;
                double w[kLevelTaps] {};
                double sum = 0;
                for (int j = 0; j < kLevelTaps; ++j)
                {
                    const double t = f - static_cast<double> (j - 3);
                    w[j] = sincPi (t) * kaiser (t, kernelHalf, kernelBeta);
                    sum += w[j];
                }
                for (int j = 0; j < kLevelTaps; ++j)
                    kernel[k * kLevelTaps + j] = static_cast<float> (w[j] / sum);
            }

            // The decimation filter: a windowed-sinc low-pass of 75 taps, its band edge half way between a fifth and a
            // quarter of the rate (0.225), in a Kaiser window for 60 dB (beta 0.1102 (60 - 8.7)): flat to 0.2, at least
            // 60 dB down from 0.25, the new half rate. Scaled to sum to 1 (a steady value keeps its level).
            constexpr double cutoff = 0.225;
            constexpr double filterBeta = 0.1102 * (60.0 - 8.7);
            double h[2 * kLevelHalf + 1] {};
            double sum = 0;
            for (int k = -kLevelHalf; k <= kLevelHalf; ++k)
            {
                const double t = static_cast<double> (k);
                const double v = 2.0 * cutoff * sincPi (2.0 * cutoff * t) * kaiser (t, kLevelHalf, filterBeta);
                h[k + kLevelHalf] = v;
                sum += v;
            }
            for (int k = 0; k < 2 * kLevelHalf + 1; ++k)
                filter[k] = static_cast<float> (h[k] / sum);
        }

        // Built during static initialisation, before main and long before any audio thread exists.
        const LevelTables kLevelTables;
    }

    namespace
    {
        constexpr int kBlock = 4096;   // output frames per block (the stop question is asked before each)

        // The filter's taps split by parity: out[j] = sum over m of h[2m] x[2 (j + m)] + h[2m + 1] x[2 (j + m) + 1].
        constexpr int kEvenFirst = -(kLevelHalf / 2), kEvenLast = kLevelHalf / 2;              // m: -18 .. 18 (k = -36 .. 36)
        constexpr int kOddFirst = -((kLevelHalf + 1) / 2), kOddLast = (kLevelHalf - 1) / 2;    // m: -19 .. 18 (k = -37 .. 37)

        inline float valueOf (float v) noexcept { return v; }
        inline float valueOf (std::int16_t v) noexcept { return static_cast<float> (v) * (1.0f / 32768.0f); }

        inline void put (float& out, float v) noexcept { out = v; }
        inline void put (std::int16_t& out, float v) noexcept { out = toInt16 (v); }

        /** x[i] with the edges held (i before the start reads frame 0, past the end the last frame). */
        template <class T>
        inline float heldAt (const T* x, std::int64_t n, std::int64_t i) noexcept
        {
            return valueOf (x[i < 0 ? 0 : i >= n ? n - 1 : i]);
        }

        struct Scratch
        {
            std::vector<float> even, odd, acc;
            Scratch()
                : even (static_cast<std::size_t> (kBlock + kEvenLast - kEvenFirst)),
                  odd (static_cast<std::size_t> (kBlock + kOddLast - kOddFirst)),
                  acc (static_cast<std::size_t> (kBlock))
            {
            }
        };

        /** One channel of one level: y[j] = the filtered x at frame 2j, j = 0 .. outN - 1. Each tap is one pass over a block
            of outputs (out[t] += c x[...]), so every output sums its taps in the same order and the compiler may use its
            vector registers across t. False when stop said so. */
        template <class In, class Out>
        bool decimate (const In* x, std::int64_t n, Out* y, std::int64_t outN, Scratch& s, const std::function<bool()>& stop)
        {
            const float* h = levelFilter();
            float* even = s.even.data();
            float* odd = s.odd.data();
            float* acc = s.acc.data();
            for (std::int64_t j0 = 0; j0 < outN; j0 += kBlock)
            {
                if (stop && stop())
                    return false;
                const int count = static_cast<int> (std::min<std::int64_t> (kBlock, outN - j0));
                const int nEven = count + kEvenLast - kEvenFirst, nOdd = count + kOddLast - kOddFirst;
                for (int t = 0; t < nEven; ++t)
                    even[t] = heldAt (x, n, 2 * (j0 + kEvenFirst + t));
                for (int t = 0; t < nOdd; ++t)
                    odd[t] = heldAt (x, n, 2 * (j0 + kOddFirst + t) + 1);
                for (int t = 0; t < count; ++t)
                    acc[t] = 0.0f;
                for (int m = kEvenFirst; m <= kEvenLast; ++m)
                {
                    const float c = h[2 * m + kLevelHalf];
                    const float* from = even + (m - kEvenFirst);
                    for (int t = 0; t < count; ++t)
                        acc[t] += c * from[t];
                }
                for (int m = kOddFirst; m <= kOddLast; ++m)
                {
                    const float c = h[2 * m + 1 + kLevelHalf];
                    const float* from = odd + (m - kOddFirst);
                    for (int t = 0; t < count; ++t)
                        acc[t] += c * from[t];
                }
                Out* out = y + j0;
                for (int t = 0; t < count; ++t)
                    put (out[t], acc[t]);
            }
            return true;
        }

        /** The levels' samples: one vector per level and channel, in the sound's format. */
        template <class T>
        struct Storage
        {
            std::vector<T> left[kMaxOctaveLevels], right[kMaxOctaveLevels];
        };

        template <class T>
        LevelSet build (const T* left, const T* right, std::int64_t n, bool stereo, const std::function<bool()>& stop)
        {
            LevelSet set;
            int count = 0;
            std::int64_t frames[kMaxOctaveLevels] {};
            std::int64_t below = n;
            for (int L = 0; L < kMaxOctaveLevels; ++L)
            {
                const std::int64_t next = (below + 1) / 2;   // frames 0, 2, 4 .. of the level below
                if (next < kLevelMinFrames)
                    break;
                frames[L] = next;
                below = next;
                ++count;
            }
            if (count == 0)
                return set;

            auto storage = std::make_shared<Storage<T>>();
            for (int L = 0; L < count; ++L)
            {
                storage->left[L].resize (static_cast<std::size_t> (frames[L]));
                if (stereo)
                    storage->right[L].resize (static_cast<std::size_t> (frames[L]));
            }
            Scratch scratch;
            const T* fromL = left;
            const T* fromR = right;
            std::int64_t fromN = n;
            for (int L = 0; L < count; ++L)
            {
                T* toL = storage->left[L].data();
                if (! decimate (fromL, fromN, toL, frames[L], scratch, stop))
                {
                    set.stopped = true;
                    return set;
                }
                T* toR = toL;
                if (stereo)
                {
                    toR = storage->right[L].data();
                    if (! decimate (fromR, fromN, toR, frames[L], scratch, stop))
                    {
                        set.stopped = true;
                        return set;
                    }
                }
                fromL = toL;
                fromR = toR;
                fromN = frames[L];
            }

            for (int L = 0; L < count; ++L)
            {
                OctaveLevel& q = set.levels[static_cast<std::size_t> (L)];
                q.n = frames[L];
                const T* l = storage->left[L].data();
                const T* r = stereo ? storage->right[L].data() : l;
                if constexpr (std::is_same_v<T, std::int16_t>)
                {
                    q.L16 = l;
                    q.R16 = r;
                }
                else
                {
                    q.L = l;
                    q.R = r;
                }
            }
            set.count = count;
            set.storage = std::shared_ptr<const void> (storage, storage.get());
            return set;
        }
    }

    LevelSet buildOctaveLevels (const SampleData& s, const std::function<bool()>& stop)
    {
        if (! s.hasSamples() || s.n < 2 * kLevelMinFrames)
            return {};
        if (s.format == SampleFormat::i16)
            return build (s.L16, s.R16, s.n, s.stereo, stop);
        return build (s.L, s.R, s.n, s.stereo, stop);
    }
}
