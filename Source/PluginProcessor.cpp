#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

StrandLockProcessor::StrandLockProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void StrandLockProcessor::prepareToPlay (double, int) {}

void StrandLockProcessor::releaseResources() {}

bool StrandLockProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no audio input at all, and one stereo output.
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void StrandLockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI is accepted and, until the engine arrives, plays nothing. The host's output buffer can
    // hold anything when it arrives, so it is always overwritten.
    buffer.clear();
}

juce::AudioProcessorEditor* StrandLockProcessor::createEditor()
{
    return new StrandLockEditor (*this);
}

void StrandLockProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream out (destData, false);
    out.write (strandlock::state::magic, sizeof (strandlock::state::magic));
    out.writeInt ((int) strandlock::state::currentVersion);   // little-endian
}

void StrandLockProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Anything that is not a StrandLock state (too short, the wrong magic, version 0) is ignored.
    if (data == nullptr || sizeInBytes < strandlock::state::headerSize)
        return;

    const auto* bytes = static_cast<const char*> (data);
    if (std::memcmp (bytes, strandlock::state::magic, sizeof (strandlock::state::magic)) != 0)
        return;

    const juce::uint32 version = juce::ByteOrder::littleEndianInt (bytes + 4);
    if (version == 0)
        return;

    // Version 1 holds nothing after the header. A newer version's extra data is left for the build
    // that understands it.
    loadedStateVersion = version;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StrandLockProcessor();
}
