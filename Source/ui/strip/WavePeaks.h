#pragma once

// The waveform's peaks (sl-ui.js buildPeaks and renderWave). buildPeaks keeps the mid channel's lowest and highest
// value in every block of 256 frames; renderWave then draws one column per pixel of the view: from those block
// peaks when the column spans two blocks or more, else from the samples themselves.
//
// The peaks are made on the message thread (never the audio thread) the first time a sound is shown, and kept
// per sound (a small cache keyed by the sound's samples), so a window opened again, or a sound that comes back
// (Undo, the same starter), does not read the whole sound again. A cached entry holds only the peaks, never the
// samples: it lives while its sound lives, and goes when the sound is freed.

#include "doc/SampleManager.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace sl::ui
{
    class WavePeaks
    {
    public:
        static constexpr int kBlock = 256;   // buildPeaks' B

        /** buildPeaks: the mid ((L + R) * 0.5, in double, stored as float as the Float32Array does) of every
            block of kBlock frames (the last block may be shorter). right may equal left (mono). */
        WavePeaks (const float* left, const float* right, std::int64_t frames);

        /** The peaks of view's sound: from the cache, or made now (message thread). Null for no sound. */
        static std::shared_ptr<const WavePeaks> of (const std::shared_ptr<const doc::SoundView>& view);

        /** How many sounds the cache holds peaks for now (for the tests), and how many peaks it ever made. */
        static int cachedSounds();
        static std::int64_t peaksMade();

        std::int64_t frames() const noexcept { return n; }
        std::size_t blocks() const noexcept { return mn.size(); }
        float blockMin (std::size_t b) const noexcept { return mn[b]; }
        float blockMax (std::size_t b) const noexcept { return mx[b]; }

        /** One column of renderWave: the view a..b (fractions of the sound) drawn width columns wide. */
        struct Column
        {
            float lo = 0.0f, hi = 0.0f;
            bool drawn = false;   // renderWave skips a column past the end (no frames: !(hi >= lo))
        };

        /** renderWave's loop, for the samples left and right of the same sound. */
        std::vector<Column> columns (const float* left, const float* right, double a, double b, int width) const;

    private:
        std::int64_t n = 0;
        std::vector<float> mn, mx;
    };
}
