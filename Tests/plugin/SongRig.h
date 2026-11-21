#pragma once

// What the M7 song tests share (Tests/plugin/test_long_file, test_part_load, test_region_precision, test_root and the
// strip's chip tests): long WAV files written at test time chunk by chunk (no audio file is committed, and a 10-minute
// file never exists as floats in the test's memory), the frames a file holds read back as JUCE's reader gives them,
// the processor's state saved and loaded, parameters read and set the way a host sets them.

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.h"
#include "engine/SampleData.h"
#include "params/ParameterLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace sl::test::songs
{
    inline juce::File tempFolder (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StrandLockTests").getNonexistentChildFile (name, "", false);
        dir.createDirectory();
        return dir;
    }

    /** Fills count frames from frame start, one pointer per channel. */
    using Fill = std::function<void (std::int64_t start, int count, float* const* out)>;

    /** Writes a 16-bit PCM WAV of frames frames, made chunk by chunk by fill. */
    inline bool writeWav16 (const juce::File& file, int channels, double rate, std::int64_t frames, const Fill& fill)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto options = juce::AudioFormatWriterOptions {}
                           .withSampleRate (rate)
                           .withNumChannels (channels)
                           .withBitsPerSample (16)
                           .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::integral);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;
        constexpr int chunk = 1 << 16;
        std::vector<std::vector<float>> buffers ((std::size_t) channels, std::vector<float> ((std::size_t) chunk));
        std::vector<float*> out;
        std::vector<const float*> in;
        for (auto& b : buffers)
        {
            out.push_back (b.data());
            in.push_back (b.data());
        }
        for (std::int64_t done = 0; done < frames; done += chunk)
        {
            const int n = (int) std::min<std::int64_t> (chunk, frames - done);
            fill (done, n, out.data());
            if (! writer->writeFromFloatArrays (in.data(), channels, n))
                return false;
        }
        writer.reset();
        return file.existsAsFile();
    }

    /** count frames of channel ch from frame start, as JUCE's own reader gives them, as 16-bit values (s / 32768 back to
        s): what a 16-bit sound must hold. */
    inline std::vector<std::int16_t> readFrames16 (const juce::File& file, std::int64_t start, int count, int ch)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        std::vector<std::int16_t> out;
        if (reader == nullptr)
            return out;
        juce::AudioBuffer<float> b ((int) juce::jmax (1u, reader->numChannels), count);
        reader->read (&b, 0, count, start, true, true);
        const int c = juce::jmin (ch, b.getNumChannels() - 1);
        for (int i = 0; i < count; ++i)
            out.push_back (engine::toInt16 (b.getSample (c, i)));
        return out;
    }

    inline juce::MemoryBlock saveOf (StrandLockProcessor& p)
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        return m;
    }

    inline void loadInto (StrandLockProcessor& p, const juce::MemoryBlock& m)
    {
        p.setStateInformation (m.getData(), (int) m.getSize());
    }

    inline float plainOf (StrandLockProcessor& p, params::P id)
    {
        return params::plainValueOf (p.getParam (id));
    }

    /** A host's change (automation, Quick Controls): the normalised value through setValueNotifyingHost. */
    inline void hostSets (juce::RangedAudioParameter& param, float plain)
    {
        param.setValueNotifyingHost (param.convertTo0to1 (plain));
    }

    /** One of StrandLock's own edits that is no undo step of its own (the tests' setup): the plain value in a gesture. */
    inline void setPlain (StrandLockProcessor& p, params::P id, float plain)
    {
        auto& param = p.getParam (id);
        param.beginChangeGesture();
        params::setPlainValue (param, params::index (id), plain);
        param.endChangeGesture();
    }

    /** A small lcg (the tests' own noise: no committed audio). */
    struct Noise
    {
        std::uint32_t s = 1;
        explicit Noise (std::uint32_t seed) : s (seed | 1u) {}
        float next() noexcept
        {
            s = s * 1664525u + 1013904223u;
            return (float) ((double) s / 4294967296.0 * 2.0 - 1.0);
        }
    };

    /** A one-minute "song" with something for each kind of part: a held A2 (0-14 s), a quiet texture (14-22 s), a
        120 bpm beat (22-40 s), one big hit (40 s), then a held chord (41-60 s). Mono. */
    inline std::vector<float> minuteSong (double rate)
    {
        const auto n = (std::size_t) (60.0 * rate);
        std::vector<float> x (n, 0.0f);
        Noise noise (60);
        const double pi2 = 2.0 * 3.141592653589793;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / rate;
            double v = 0.0;
            if (t < 14.0)
                v = 0.3 * (std::sin (pi2 * 110.0 * t) + 0.5 * std::sin (pi2 * 220.0 * t) + 0.25 * std::sin (pi2 * 330.0 * t));
            else if (t < 22.0)
                v = 0.01 * noise.next();
            else if (t < 40.0)
            {
                const double beat = std::fmod (t - 22.0, 0.5), bar = std::fmod (t - 22.0, 2.0);
                const double amp = bar < 0.5 ? 0.8 : 0.5;
                v = amp * std::exp (-beat * 30.0) * (std::sin (pi2 * 60.0 * beat) + 0.3 * noise.next());
            }
            else if (t < 41.0)
                v = 0.95 * std::exp (-(t - 40.0) * 6.0) * noise.next();
            else
                v = 0.15 * (std::sin (pi2 * 220.0 * t) + std::sin (pi2 * 277.18 * t) + std::sin (pi2 * 329.63 * t));
            x[i] = (float) v;
        }
        return x;
    }

    /** Writes mono samples as a 16-bit WAV. */
    inline bool writeMono16 (const juce::File& file, const std::vector<float>& x, double rate)
    {
        return writeWav16 (file, 1, rate, (std::int64_t) x.size(), [&x] (std::int64_t start, int count, float* const* out)
        {
            for (int i = 0; i < count; ++i)
                out[0][i] = x[(std::size_t) (start + i)];
        });
    }
}
