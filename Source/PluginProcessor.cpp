#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "core/Version.h"
#include "state/Crc32.h"

#include <cstring>

namespace
{
    void writeLE16 (std::uint8_t* p, std::uint32_t v) noexcept
    {
        p[0] = static_cast<std::uint8_t> (v & 0xffu);
        p[1] = static_cast<std::uint8_t> ((v >> 8) & 0xffu);
    }

    void writeLE32 (std::uint8_t* p, std::uint32_t v) noexcept
    {
        for (int i = 0; i < 4; ++i)
            p[i] = static_cast<std::uint8_t> ((v >> (8 * i)) & 0xffu);
    }
}

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
    // The empty container: the header alone.
    std::uint8_t header[sl::state::kHeaderSize] {};
    std::memcpy (header, sl::state::kMagic, sizeof (sl::state::kMagic));
    writeLE16 (header + 4, static_cast<std::uint32_t> (sl::core::kStateFormatVersion));
    writeLE16 (header + 6, 0);
    writeLE32 (header + 8, static_cast<std::uint32_t> (sl::state::kHeaderSize));
    writeLE32 (header + 12, sl::state::crc32 (header, 12));
    destData.replaceAll (header, sizeof (header));
}

void StrandLockProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Anything that is not a whole, intact StrandLock header is ignored: too short, the wrong magic,
    // a bad header CRC, version 0 or a newer version, or a length past the end of the data.
    if (data == nullptr || sizeInBytes < sl::state::kHeaderSize)
        return;

    const auto* bytes = static_cast<const std::uint8_t*> (data);
    if (std::memcmp (bytes, sl::state::kMagic, sizeof (sl::state::kMagic)) != 0)
        return;
    if (juce::ByteOrder::littleEndianInt (bytes + 12) != sl::state::crc32 (bytes, 12))
        return;

    const std::uint32_t version = juce::ByteOrder::littleEndianShort (bytes + 4);
    const std::uint32_t totalLength = juce::ByteOrder::littleEndianInt (bytes + 8);
    if (version == 0 || version > static_cast<std::uint32_t> (sl::core::kStateFormatVersion))
        return;
    if (totalLength < static_cast<std::uint32_t> (sl::state::kHeaderSize) || totalLength > static_cast<std::uint32_t> (sizeInBytes))
        return;

    // No chunks are read yet (M0 adds them).
    loadedStateVersion = version;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StrandLockProcessor();
}
