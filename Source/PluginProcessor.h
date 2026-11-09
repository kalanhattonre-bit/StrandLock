#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>

namespace sl::state
{
    /** The binary state's 16-byte header (PLUGIN-PLAN 3), little-endian:
        'S' 'T' 'L' 'K' | u16 formatVersion | u16 flags = 0 | u32 totalLength | u32 headerCrc32,
        where headerCrc32 is the CRC-32 of the 12 bytes before it. The chunks follow it; the scaffold
        writes none (M0 adds PARM, INST, UIST and SMPL, and keeps chunks it does not know). */
    inline constexpr char kMagic[4] = { 'S', 'T', 'L', 'K' };
    inline constexpr int kHeaderSize = 16;
}

/** The scaffold's processor (PLUGIN-PLAN 7.1): a silent instrument (no audio input, one stereo
    output) that accepts MIDI, reports no latency and saves an empty state container. M0 grows it. */
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

    /** The formatVersion of the last state setStateInformation accepted, or 0 if it has accepted none. */
    std::uint32_t getLoadedStateVersion() const noexcept { return loadedStateVersion.load(); }

private:
    std::atomic<std::uint32_t> loadedStateVersion { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockProcessor)
};
