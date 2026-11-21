#pragma once

// The decoded sample the engine plays (PLUGIN-PLAN 1.1, 1.3): the prototype's source { L, R, n, sr, stereo }.
// Made on the message side (SampleManager's worker, or a test), handed to the audio thread through
// rt::VersionExchange<SampleData>, never changed afterwards, and freed on the message side once no voice
// reads it (a voice stolen by a sample swap finishes its fade on the old one, 8.2 #23).
//
// The audio thread reads only L, R (or L16, R16), n, sr, stereo and format. The pointers point into `storage`,
// which keeps the samples alive: a shared starter sound, a decoded file, or the vectors makeSampleData was given.
// So a starter sound is never copied, and the waveform strip can share the same samples.
//
// Long files (M7, plan 8.2 #14): a sound longer than 3 minutes is stored as 16-bit samples, half the memory of
// floats (a 10-minute stereo 48 kHz song is 115.2 MB instead of 230.4 MB). A 16-bit sample s reads as s / 32768,
// which is exact in float and in double and is the value JUCE's own 16-bit decoder gives. So the engine reads a
// 16-bit sound as exactly the doubles a float sound holding s / 32768 would give, and plays it bit for bit the
// same (test_long_file checks it). Every read path goes through SourceRead.h's helpers, which pick the format.

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sl::engine
{
    /** How the samples are stored: 32-bit floats (L, R), or 16-bit integers (L16, R16) for long files (M7). */
    enum class SampleFormat : std::uint8_t { f32 = 0, i16 = 1 };

    /** A 16-bit sample s stands for s / 32768 (a power of two, so the product is exact). */
    inline constexpr double kInt16Scale = 1.0 / 32768.0;

    /** The 16-bit sample nearest to v x 32768 (half away from zero), kept inside -32768 .. 32767; NaN gives 0. A
        16-bit file decoded to floats (s / 32768) comes back to the same s. */
    inline std::int16_t toInt16 (float v) noexcept
    {
        const double y = static_cast<double> (v) * 32768.0;
        if (std::isnan (y))
            return 0;
        if (y >= 32767.0)
            return 32767;
        if (y <= -32768.0)
            return -32768;
        return static_cast<std::int16_t> (std::lround (y));
    }

    struct SampleData
    {
        std::uint64_t epoch = 0;                  // stamped by VersionExchange::publish (first: a live one is never 0xDD.. there)
        const float* L = nullptr;                 // f32: n frames
        const float* R = nullptr;                 // f32: n frames; the same pointer as L when not stereo (JS R: stereo ? R : L)
        const std::int16_t* L16 = nullptr;        // i16: n frames
        const std::int16_t* R16 = nullptr;        // i16: n frames; the same pointer as L16 when not stereo
        std::int64_t n = 0;                       // frames, >= 8
        double sr = 48000.0;                      // the file's rate as given; the engine plays clamp (sr, 4000, 768000),
                                                  // or its own rate when sr is not finite (JS fin (s.sampleRate, this.sr))
        bool stereo = false;                      // R holds samples that differ from L somewhere
        SampleFormat format = SampleFormat::f32;
        std::string name;                         // display name (message side only)
        std::shared_ptr<const void> storage;      // keeps the samples alive

        /** True when the left channel is there in the stored format. */
        bool hasSamples() const noexcept { return format == SampleFormat::i16 ? L16 != nullptr : L != nullptr; }

        /** Frame i (0 .. n-1) of the left (channel 0) or right channel, as the double the engine reads. */
        double at (int channel, std::int64_t i) const noexcept
        {
            if (format == SampleFormat::i16)
                return static_cast<double> ((channel == 0 ? L16 : R16)[i]) * kInt16Scale;
            return static_cast<double> ((channel == 0 ? L : R)[i]);
        }

        /** The bytes the samples take (both channels when stereo). */
        std::int64_t sampleBytes() const noexcept
        {
            const std::int64_t each = format == SampleFormat::i16 ? 2 : 4;
            return n * each * (stereo ? 2 : 1);
        }
    };

    /** The sample over samples someone else owns (storage keeps them alive), along the prototype's setSample
        rules: fewer than 8 frames (or no left channel) is no sound at all (nullptr: the engine plays silence);
        a right channel of another length, the same pointer, or identical samples is mono (R = L, stereo false).
        Samples are taken as they are (the prototype only cleans arrays that are not Float32Arrays). Allocates:
        message side only. */
    inline std::unique_ptr<SampleData> makeSampleData (std::shared_ptr<const void> storage, const float* left, const float* right,
                                                       std::int64_t frames, std::int64_t rightFrames, double sampleRate, std::string name)
    {
        if (left == nullptr || frames < 8)
            return nullptr;
        bool stereo = right != nullptr && rightFrames == frames && right != left;
        if (stereo)
        {
            stereo = false;
            for (std::int64_t i = 0; i < frames; ++i)
                if (left[i] != right[i])   // NaN != NaN, as JavaScript's !==
                {
                    stereo = true;
                    break;
                }
        }
        auto s = std::make_unique<SampleData>();
        s->L = left;
        s->R = stereo ? right : left;
        s->n = frames;
        s->sr = sampleRate;
        s->stereo = stereo;
        s->name = std::move (name);
        s->storage = std::move (storage);
        return s;
    }

    /** The same over two vectors it takes over (right may be empty: mono). */
    inline std::unique_ptr<SampleData> makeSampleData (std::vector<float> left, std::vector<float> right, double sampleRate, std::string name)
    {
        struct Channels
        {
            std::vector<float> left, right;
        };
        auto owned = std::make_shared<Channels>();
        owned->left = std::move (left);
        owned->right = std::move (right);
        const float* l = owned->left.empty() ? nullptr : owned->left.data();
        const float* r = owned->right.empty() ? nullptr : owned->right.data();
        const auto frames = static_cast<std::int64_t> (owned->left.size());
        const auto rightFrames = static_cast<std::int64_t> (owned->right.size());
        return makeSampleData (std::shared_ptr<const void> (owned, owned.get()), l, r, frames, rightFrames, sampleRate,
                               std::move (name));
    }

    /** A 16-bit sample (M7, long files) over samples someone else owns, by the same rules as makeSampleData: fewer
        than 8 frames is no sound; a right channel of another length, the same pointer or identical samples is mono. */
    inline std::unique_ptr<SampleData> makeSampleData16 (std::shared_ptr<const void> storage, const std::int16_t* left,
                                                         const std::int16_t* right, std::int64_t frames, std::int64_t rightFrames,
                                                         double sampleRate, std::string name)
    {
        if (left == nullptr || frames < 8)
            return nullptr;
        bool stereo = right != nullptr && rightFrames == frames && right != left;
        if (stereo)
        {
            stereo = false;
            for (std::int64_t i = 0; i < frames; ++i)
                if (left[i] != right[i])
                {
                    stereo = true;
                    break;
                }
        }
        auto s = std::make_unique<SampleData>();
        s->format = SampleFormat::i16;
        s->L16 = left;
        s->R16 = stereo ? right : left;
        s->n = frames;
        s->sr = sampleRate;
        s->stereo = stereo;
        s->name = std::move (name);
        s->storage = std::move (storage);
        return s;
    }

    /** The same over two 16-bit vectors it takes over (right may be empty: mono). */
    inline std::unique_ptr<SampleData> makeSampleData16 (std::vector<std::int16_t> left, std::vector<std::int16_t> right, double sampleRate,
                                                         std::string name)
    {
        struct Channels
        {
            std::vector<std::int16_t> left, right;
        };
        auto owned = std::make_shared<Channels>();
        owned->left = std::move (left);
        owned->right = std::move (right);
        const std::int16_t* l = owned->left.empty() ? nullptr : owned->left.data();
        const std::int16_t* r = owned->right.empty() ? nullptr : owned->right.data();
        const auto frames = static_cast<std::int64_t> (owned->left.size());
        const auto rightFrames = static_cast<std::int64_t> (owned->right.size());
        return makeSampleData16 (std::shared_ptr<const void> (owned, owned.get()), l, r, frames, rightFrames, sampleRate,
                                 std::move (name));
    }
}
