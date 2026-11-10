// With no MIDI and no host position the processor plays exact silence, whatever the host left in its buffer:
// the clip (Strand Demo in a new instance) plays only while the host plays.
// (Moved from the skeleton's inline TestMain; M0 owns this file.)

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <cmath>

namespace
{
    using namespace sl::test;
}

SL_TEST ("plugin", silenceWithoutMidi)
{
    section ("Ten seconds with no MIDI: every sample is exactly zero and finite");

    for (const double rate : { 44100.0, 96000.0 })
    {
        for (const int block : { 1, 64, 4096 })
        {
            StrandLockProcessor proc;
            proc.setPlayConfigDetails (0, 2, rate, block);
            proc.prepareToPlay (rate, block);

            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            juce::Random rng ((juce::int64) rate + block);

            const auto total = (juce::int64) (10.0 * rate);
            long long nonZero = 0, nonFinite = 0;

            for (juce::int64 pos = 0; pos < total; pos += block)
            {
                const int n = (int) std::min<juce::int64> (block, total - pos);
                buffer.setSize (2, n, false, false, true);

                // A host's output buffer can hold anything before the call; the plugin must overwrite it.
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        buffer.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);

                midi.clear();
                proc.processBlock (buffer, midi);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* d = buffer.getReadPointer (ch);
                    for (int i = 0; i < n; ++i)
                    {
                        if (! std::isfinite (d[i]))
                            ++nonFinite;
                        else if (! juce::exactlyEqual (d[i], 0.0f))
                            ++nonZero;
                    }
                }
            }

            proc.releaseResources();
            check (nonZero == 0 && nonFinite == 0,
                   fmt ("%.1f kHz, %d-sample blocks: %lld samples per channel, %lld non-zero, %lld non-finite",
                        rate / 1000.0, block, (long long) total, nonZero, nonFinite));
        }
    }
}
