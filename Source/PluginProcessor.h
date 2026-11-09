#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace strandlock::state
{
    /** The saved state starts with an 8-byte header: the four bytes 'S' 'T' 'L' 'K', then the format
        version as a little-endian uint32. Version 1 is the header alone; each later version appends
        its data after the header, so a build can always tell which layout it is reading. */
    inline constexpr char magic[4] = { 'S', 'T', 'L', 'K' };
    inline constexpr juce::uint32 currentVersion = 1;
    inline constexpr int headerSize = 8;
}

/** The M0 skeleton: an instrument (no audio input, one stereo output) that accepts MIDI and
    plays silence, with a versioned state and the suite's editor. The engine arrives in later milestones. */
class StrandLockProcessor final : public juce::AudioProcessor
{
public:
    StrandLockProcessor();
    ~StrandLockProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "StrandLock"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** The format version of the last state setStateInformation accepted, or 0 if it has accepted none. */
    juce::uint32 getLoadedStateVersion() const noexcept { return loadedStateVersion.load(); }

private:
    std::atomic<juce::uint32> loadedStateVersion { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockProcessor)
};
