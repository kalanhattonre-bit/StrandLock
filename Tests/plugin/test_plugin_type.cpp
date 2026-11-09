// The plugin is an instrument: no audio input, one stereo output, MIDI in, no latency.
// (Moved from the skeleton's inline TestMain; M0 owns this file.)

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

namespace
{
    using namespace sl::test;
    using Set = juce::AudioChannelSet;

    juce::AudioProcessor::BusesLayout layout (std::initializer_list<Set> inputs, std::initializer_list<Set> outputs)
    {
        juce::AudioProcessor::BusesLayout l;
        for (const auto& s : inputs)
            l.inputBuses.add (s);
        for (const auto& s : outputs)
            l.outputBuses.add (s);
        return l;
    }
}

SL_TEST ("probe", pluginType)
{
    section ("An instrument: no audio input, one stereo output, MIDI in");

    StrandLockProcessor proc;
    check (proc.getBusCount (true) == 0 && proc.getTotalNumInputChannels() == 0,
           fmt ("no input bus (%d input buses, %d input channels)", proc.getBusCount (true), proc.getTotalNumInputChannels()));
    check (proc.getBusCount (false) == 1 && proc.getTotalNumOutputChannels() == 2
               && proc.getChannelLayoutOfBus (false, 0) == Set::stereo(),
           fmt ("one stereo output bus (%d output buses, %d output channels)", proc.getBusCount (false), proc.getTotalNumOutputChannels()));
    check (proc.acceptsMidi() && ! proc.producesMidi() && ! proc.isMidiEffect(),
           "accepts MIDI, sends none, and is not a MIDI effect");
    check (proc.getName() == "StrandLock", fmt ("named \"%s\"", proc.getName().toRawUTF8()));
    check (proc.getLatencySamples() == 0 && juce::exactlyEqual (proc.getTailLengthSeconds(), 0.0),
           fmt ("zero latency (%d samples), no tail (%.1f s)", proc.getLatencySamples(), proc.getTailLengthSeconds()));

    check (proc.checkBusesLayoutSupported (layout ({}, { Set::stereo() })), "no input / stereo out supported");
    check (! proc.checkBusesLayoutSupported (layout ({}, { Set::mono() })), "no input / mono out rejected");
    check (! proc.checkBusesLayoutSupported (layout ({ Set::stereo() }, { Set::stereo() })), "stereo in / stereo out rejected");
}
