#pragma once

// Driving the processor the way a host does (PLUGIN-PLAN 7.2, M0): MIDI events at absolute sample positions,
// blocks of any size (fixed, a list, seeded random), a play head the test controls. Owned by M0.

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <functional>
#include <vector>

class StrandLockProcessor;

namespace sl::test::harness
{
    /** One MIDI message at an absolute sample position. */
    struct MidiEvent
    {
        juce::int64 sample = 0;
        std::uint8_t bytes[3] {};
        int size = 3;
    };

    MidiEvent noteOn (juce::int64 sample, int note, int velocity);
    MidiEvent noteOff (juce::int64 sample, int note);
    MidiEvent controller (juce::int64 sample, int number, int value);
    MidiEvent pitchBend (juce::int64 sample, int value14);

    /** A play head whose position the test sets before each block (present = false gives the host's "none"). */
    class FakePlayHead final : public juce::AudioPlayHead
    {
    public:
        juce::AudioPlayHead::PositionInfo info;
        bool present = true;

        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;
    };

    /** The size of block number b. */
    using BlockSizes = std::function<int (int block)>;

    BlockSizes fixedBlocks (int size);
    BlockSizes cycledBlocks (std::vector<int> sizes);
    BlockSizes randomBlocks (int lo, int hi, std::uint32_t seed);

    struct Rendered
    {
        std::vector<float> left, right;
        int blocks = 0;
    };

    /** Prepares p at sampleRate (blocks up to maxBlock) and renders total samples, cut into blocks by sizes,
        with every event in the block that holds its sample. beforeBlock (when given) runs before each block with
        its first sample and size (to move a play head). */
    Rendered render (StrandLockProcessor& p, double sampleRate, juce::int64 total, const BlockSizes& sizes,
                     const std::vector<MidiEvent>& events, int maxBlock = 4096,
                     const std::function<void (juce::int64 start, int size)>& beforeBlock = {});

    /** Seeded chords: every `every` seconds a new chord of `voices` notes (old ones released), for `seconds`. */
    std::vector<MidiEvent> chords (double sampleRate, double seconds, int voices, double every, std::uint32_t seed);

    /** The largest |a - b| over two renders (infinite when their lengths differ). */
    double maxDifference (const Rendered& a, const Rendered& b);
}
