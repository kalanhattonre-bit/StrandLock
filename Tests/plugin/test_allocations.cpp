// processBlock never allocates or frees, with notes, bends and controllers arriving.
// (Moved from the skeleton's inline TestMain; M0 owns this file.) --alloc-minutes sets the length.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include <array>
#include <cmath>

namespace
{
    using namespace sl::test;
}

SL_TEST ("alloc", processBlockAllocatesNothing)
{
    const double minutes = options().allocMinutes;
    section (fmt ("%.1f minutes at 48 kHz / 64-sample blocks with MIDI: no allocation or free on the audio thread", minutes));

    const double rate = 48000.0;
    const int block = 64;
    StrandLockProcessor proc;
    proc.setPlayConfigDetails (0, 2, rate, block);
    proc.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (8192);
    juce::Random rng (99);

    const auto totalBlocks = (juce::int64) (minutes * 60.0 * rate / block);
    std::array<int, 16> held {};
    int numHeld = 0;
    bool silentAndFinite = true;

    alloc::reset();
    const auto startMs = juce::Time::getMillisecondCounterHiRes();

    for (juce::int64 b = 0; b < totalBlocks; ++b)
    {
        // Everything a host does between callbacks happens outside the counted scope.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < block; ++i)
                d[i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.4f;
        }

        midi.clear();
        if (rng.nextInt (6) == 0 && numHeld < (int) held.size())
        {
            const int note = 30 + rng.nextInt (70);
            const juce::uint8 on[3] = { 0x90, (juce::uint8) note, (juce::uint8) (1 + rng.nextInt (127)) };
            midi.addEvent (on, 3, rng.nextInt (block));
            held[(std::size_t) numHeld++] = note;
        }
        if (numHeld > 0 && rng.nextInt (5) == 0)
        {
            const int idx = rng.nextInt (numHeld);
            const juce::uint8 off[3] = { 0x80, (juce::uint8) held[(std::size_t) idx], 0 };
            midi.addEvent (off, 3, rng.nextInt (block));
            held[(std::size_t) idx] = held[(std::size_t) --numHeld];
        }
        if (rng.nextInt (40) == 0)
        {
            const int bend = rng.nextInt (16384);
            const juce::uint8 pb[3] = { 0xe0, (juce::uint8) (bend & 0x7f), (juce::uint8) (bend >> 7) };
            midi.addEvent (pb, 3, 0);
        }
        if (rng.nextInt (60) == 0)
        {
            static const int controllers[] = { 1, 2, 11, 64, 120, 121, 123 };
            const juce::uint8 cc[3] = { 0xb0, (juce::uint8) controllers[rng.nextInt (7)], (juce::uint8) rng.nextInt (128) };
            midi.addEvent (cc, 3, rng.nextInt (block));
        }
        if (rng.nextInt (90) == 0)
        {
            const juce::uint8 pressure[2] = { 0xd0, (juce::uint8) rng.nextInt (128) };
            midi.addEvent (pressure, 2, block - 1);
        }

        {
            const alloc::Scope counting;
            proc.processBlock (buffer, midi);
        }

        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (1);
        for (int i = 0; i < block; ++i)
            silentAndFinite = silentAndFinite && std::isfinite (l[i]) && std::isfinite (r[i])
                                  && juce::exactlyEqual (l[i], 0.0f) && juce::exactlyEqual (r[i], 0.0f);
    }

    const double seconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    const auto c = alloc::counts();
    note (fmt ("%.1f minutes of audio (%lld blocks) in %.1f s; counting %s", minutes, (long long) totalBlocks, seconds, alloc::coverage()));

    check (totalBlocks > 0, fmt ("the run covered %lld blocks", (long long) totalBlocks));
    check (silentAndFinite, "notes, bends and controllers: silent and finite throughout (the scaffold plays nothing)");
    check (c.news == 0 && c.deletes == 0,
           fmt ("allocations inside processBlock: %lld, frees: %lld (largest request %zu bytes)", c.news, c.deletes, c.largest));
}
