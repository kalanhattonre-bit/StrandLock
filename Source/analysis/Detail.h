#pragma once

// What the analysis files share (internal to Source/analysis): sl-analysis.js validInput, clean and mono, and
// features ()' ~11 kHz copy, each read from a Source in blocks instead of from whole-file arrays. A mono or
// 11 kHz sample is made from the same samples with the same operations as the prototype's arrays hold.

#include "analysis/Analysis.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sl::analysis::detail
{
    /** validInput (channels, sr): at least one channel, at least one frame, and a positive rate. */
    inline bool validInput (const Source& source)
    {
        return source.numChannels() > 0 && source.numFrames() > 0 && source.sampleRate() > 0;
    }

    /** clean (): a NaN, an infinity or a value at or beyond 1e30 (either sign) reads as 0. The comparison is made
        in double, as JavaScript makes it (1e30 is not a float). */
    inline float cleanSample (float v) noexcept
    {
        const double x = v;
        return x > -1e30 && x < 1e30 ? v : 0.0f;
    }

    /** A number's truthiness in JavaScript: false for 0 and for NaN. */
    inline bool truthy (double x) noexcept
    {
        return x == x && x != 0.0;
    }

    /** A typed array (Float64Array, Float32Array, Uint8Array: zero-filled) indexed by a signed number, as the
        prototype indexes its arrays. */
    template <class T>
    class Arr
    {
    public:
        explicit Arr (std::int64_t n = 0) : v (static_cast<std::size_t> (n > 0 ? n : 0), T {}) {}

        T& operator[] (std::int64_t i) noexcept { return v[static_cast<std::size_t> (i)]; }
        const T& operator[] (std::int64_t i) const noexcept { return v[static_cast<std::size_t> (i)]; }
        std::int64_t size() const noexcept { return static_cast<std::int64_t> (v.size()); }
        T* data() noexcept { return v.data(); }

        std::vector<T> v;
    };

    /** A read-only view of a vector, indexed by a signed number. */
    template <class T>
    class View
    {
    public:
        explicit View (const std::vector<T>& vec) noexcept : p (vec.data()), n (static_cast<std::int64_t> (vec.size())) {}

        const T& operator[] (std::int64_t i) const noexcept { return p[i]; }
        std::int64_t size() const noexcept { return n; }

    private:
        const T* p;
        std::int64_t n;
    };

    /** mono (clean (channels)), a block at a time: per frame, each channel's cleaned sample added into a float in
        channel order (the prototype's Float32Array d, d[i] += x[i]), then divided by the channel count when
        there is more than one. */
    class MonoReader
    {
    public:
        explicit MonoReader (const Source& s) : source (s), channels (s.numChannels()) {}

        /** out[k] = d[start + k] for k < count. */
        void read (std::int64_t start, int count, float* out)
        {
            if (scratch.size() < static_cast<std::size_t> (count))
                scratch.resize (static_cast<std::size_t> (count));
            float* x = scratch.data();
            for (int k = 0; k < count; ++k)
                out[k] = 0.0f;
            for (int c = 0; c < channels; ++c)
            {
                source.read (c, start, count, x);
                for (int k = 0; k < count; ++k)
                    out[k] = static_cast<float> (static_cast<double> (out[k]) + static_cast<double> (cleanSample (x[k])));
            }
            if (channels > 1)
                for (int k = 0; k < count; ++k)
                    out[k] = static_cast<float> (static_cast<double> (out[k]) / static_cast<double> (channels));
        }

    private:
        const Source& source;
        int channels;
        std::vector<float> scratch;
    };

    /** features ()' y, a block at a time: y[j] for j in [j0, j0 + count) from frames [j0 D, (j0 + count) D) of
        each channel. Per channel in order, y[j] += s * g, with s the sum in double of that channel's D cleaned
        samples from 0 (the prototype's D === 4 branch adds the same four values in the same order: 0 + x is x). */
    class Decimator
    {
    public:
        Decimator (const Source& s, std::int64_t decimation, double gain)
            : source (s), channels (s.numChannels()), D (decimation), g (gain)
        {
        }

        void read (std::int64_t j0, int count, float* y)
        {
            const std::int64_t frames = static_cast<std::int64_t> (count) * D;
            if (static_cast<std::int64_t> (scratch.size()) < frames)
                scratch.resize (static_cast<std::size_t> (frames));
            float* x = scratch.data();
            for (int j = 0; j < count; ++j)
                y[j] = 0.0f;
            for (int c = 0; c < channels; ++c)
            {
                source.read (c, j0 * D, static_cast<int> (frames), x);
                std::int64_t i = 0;
                for (int j = 0; j < count; ++j)
                {
                    double s = 0;
                    for (std::int64_t q = 0; q < D; ++q)
                        s += static_cast<double> (cleanSample (x[i++]));
                    y[j] = static_cast<float> (static_cast<double> (y[j]) + s * g);
                }
            }
        }

    private:
        const Source& source;
        int channels;
        std::int64_t D;
        double g;
        std::vector<float> scratch;
    };
}
