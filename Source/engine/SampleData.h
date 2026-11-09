#pragma once

// The decoded sample the engine plays (PLUGIN-PLAN 1.1, 1.3): the prototype's source { L, R, n, sr, stereo }.
// Made on the message side (SampleManager's worker, or a test), handed to the audio thread through
// rt::VersionExchange<SampleData>, never changed afterwards, and freed on the message side once no voice
// reads it (a voice stolen by a sample swap finishes its fade on the old one, 8.2 #23).
//
// The audio thread reads only L, R, n, sr, stereo and format. L and R are raw pointers into `storage`, which
// keeps the samples alive: a shared starter sound, a decoded file, or the vectors makeSampleData was given.
// So a starter sound is never copied, and the waveform strip can share the same samples.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sl::engine
{
    /** How the samples are stored. M7 adds 16-bit storage for long files (8.2 #14); the engine's readAbs is a
        template over the source, so that is a second read path, not a change here. */
    enum class SampleFormat : std::uint8_t { f32 = 0 };

    struct SampleData
    {
        std::uint64_t epoch = 0;                  // stamped by VersionExchange::publish (first: a live one is never 0xDD.. there)
        const float* L = nullptr;                 // n frames
        const float* R = nullptr;                 // n frames; the same pointer as L when not stereo (JS R: stereo ? R : L)
        std::int64_t n = 0;                       // frames, >= 8
        double sr = 48000.0;                      // the file's rate as given; the engine plays clamp (sr, 4000, 768000),
                                                  // or its own rate when sr is not finite (JS fin (s.sampleRate, this.sr))
        bool stereo = false;                      // R holds samples that differ from L somewhere
        SampleFormat format = SampleFormat::f32;
        std::string name;                         // display name (message side only)
        std::shared_ptr<const void> storage;      // keeps L and R alive
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
}
