#include "Harness.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace sl::test::harness
{
    namespace
    {
        MidiEvent make (juce::int64 sample, int b0, int b1, int b2)
        {
            MidiEvent e;
            e.sample = sample;
            e.bytes[0] = (std::uint8_t) b0;
            e.bytes[1] = (std::uint8_t) (b1 & 0x7f);
            e.bytes[2] = (std::uint8_t) (b2 & 0x7f);
            e.size = 3;
            return e;
        }

        /** xorshift32: the tests' own seeded numbers. */
        struct Rng
        {
            std::uint32_t s;
            std::uint32_t next() noexcept
            {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                return s;
            }
            int range (int lo, int hi) noexcept { return lo + (int) (next() % (std::uint32_t) (hi - lo + 1)); }
        };
    }

    MidiEvent noteOn (juce::int64 sample, int note, int velocity) { return make (sample, 0x90, note, velocity); }
    MidiEvent noteOff (juce::int64 sample, int note)               { return make (sample, 0x80, note, 0); }
    MidiEvent controller (juce::int64 sample, int number, int value) { return make (sample, 0xb0, number, value); }
    MidiEvent pitchBend (juce::int64 sample, int value14)          { return make (sample, 0xe0, value14 & 0x7f, (value14 >> 7) & 0x7f); }

    juce::Optional<juce::AudioPlayHead::PositionInfo> FakePlayHead::getPosition() const
    {
        if (! present)
            return {};
        return info;
    }

    BlockSizes fixedBlocks (int size)
    {
        return [size] (int) { return size; };
    }

    BlockSizes cycledBlocks (std::vector<int> sizes)
    {
        return [sizes] (int b) { return sizes[(std::size_t) b % sizes.size()]; };
    }

    BlockSizes randomBlocks (int lo, int hi, std::uint32_t seed)
    {
        auto rng = std::make_shared<Rng> (Rng { seed != 0 ? seed : 1u });
        return [rng, lo, hi] (int) { return rng->range (lo, hi); };
    }

    Rendered render (StrandLockProcessor& p, double sampleRate, juce::int64 total, const BlockSizes& sizes,
                     const std::vector<MidiEvent>& events, int maxBlock,
                     const std::function<void (juce::int64, int)>& beforeBlock)
    {
        p.setPlayConfigDetails (0, 2, sampleRate, maxBlock);
        p.prepareToPlay (sampleRate, maxBlock);

        auto sorted = events;
        std::stable_sort (sorted.begin(), sorted.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });

        Rendered out;
        out.left.resize ((std::size_t) total);
        out.right.resize ((std::size_t) total);

        juce::AudioBuffer<float> buffer (2, maxBlock);
        juce::MidiBuffer midi;
        midi.ensureSize (8192);

        std::size_t next = 0;
        juce::int64 pos = 0;
        int block = 0;
        while (pos < total)
        {
            const int n = (int) std::min<juce::int64> (std::clamp (sizes (block), 1, maxBlock), total - pos);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            midi.clear();
            while (next < sorted.size() && sorted[next].sample < pos + n)
            {
                const auto& e = sorted[next++];
                midi.addEvent (e.bytes, e.size, (int) std::max<juce::int64> (0, e.sample - pos));
            }
            if (beforeBlock)
                beforeBlock (pos, n);
            p.processBlock (buffer, midi);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, out.left.begin() + pos);
            std::copy (buffer.getReadPointer (1), buffer.getReadPointer (1) + n, out.right.begin() + pos);
            pos += n;
            ++block;
        }
        out.blocks = block;
        return out;
    }

    std::vector<MidiEvent> chords (double sampleRate, double seconds, int voices, double every, std::uint32_t seed)
    {
        Rng rng { seed != 0 ? seed : 1u };
        std::vector<MidiEvent> ev;
        std::vector<int> held;
        const auto step = (juce::int64) std::llround (every * sampleRate);
        const auto end = (juce::int64) std::llround (seconds * sampleRate);
        for (juce::int64 t = 0; t < end; t += step)
        {
            for (const int note : held)
                ev.push_back (noteOff (t, note));
            held.clear();
            const int root = rng.range (36, 72);
            for (int v = 0; v < voices; ++v)
            {
                int note = root + rng.range (0, 30);
                while (std::find (held.begin(), held.end(), note) != held.end())
                    note = 24 + (note + 7 - 24) % 96;
                held.push_back (note);
                ev.push_back (noteOn (t + rng.range (0, 40), note, rng.range (40, 127)));
            }
        }
        for (const int note : held)
            ev.push_back (noteOff (end, note));
        return ev;
    }

    double maxDifference (const Rendered& a, const Rendered& b)
    {
        if (a.left.size() != b.left.size() || a.right.size() != b.right.size())
            return std::numeric_limits<double>::infinity();
        double m = 0.0;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            m = std::max (m, std::abs ((double) a.left[i] - (double) b.left[i]));
            m = std::max (m, std::abs ((double) a.right[i] - (double) b.right[i]));
        }
        return m;
    }
}
