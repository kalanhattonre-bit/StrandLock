// The audio thread never allocates or frees (PLUGIN-PLAN 4 M0): the probe playing chords, with bends,
// controllers, a moving and cycling host position, host parameter changes and the diagnostics fed, for
// --alloc-minutes of audio (10 by default, the Release run; the Debug-CRT run passes 3, and there malloc is
// counted too). Cubase sends automation and Quick Control moves inside process(), on the audio thread, just
// before processBlock (JUCE's VST3 wrapper calls setValueNotifyingHost there), so those are counted too. A
// second run does the same with the editor open, its knobs' attachments listening, from a thread of its own.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterLayout.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <thread>

namespace
{
    using namespace sl::test;

    /** The parameters the host moves in the first run: one of each kind (float, whole number, choice, switch). */
    constexpr sl::params::P kMoved[] = { sl::params::P::outGain, sl::params::P::rootNote, sl::params::P::polyphony,
                                         sl::params::P::engine, sl::params::P::lock };
}

SL_TEST ("alloc", processBlockAllocatesNothing)
{
    const double minutes = options().allocMinutes;
    section (fmt ("%.1f minutes at 48 kHz / 64-sample blocks, the probe playing, the host moving parameters: no allocation or free on the audio thread", minutes));

    const double rate = 48000.0;
    const int block = 64;
    StrandLockProcessor proc;
    harness::FakePlayHead head;
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (0, 2, rate, block);
    proc.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (8192);
    juce::Random rng (99);

    const auto totalBlocks = (juce::int64) (minutes * 60.0 * rate / block);
    std::array<int, 24> held {};
    int numHeld = 0;
    bool finite = true;
    double peak = 0.0;
    double ppq = 0.0;
    long long hostMoves = 0;

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

        // The host position: playing a 4-bar cycle at 120 bpm for 50 s, then stopped for 10 s, and so on.
        const bool playing = (b * block / (juce::int64) rate) % 60 < 50;
        head.info.setIsPlaying (playing);
        head.info.setIsLooping (true);
        head.info.setBpm (120.0);
        head.info.setLoopPoints (juce::AudioPlayHead::LoopPoints { 0.0, 16.0 });
        head.info.setPpqPosition (ppq);
        if (playing)
        {
            ppq += block * 2.0 / rate;
            if (ppq >= 16.0)
                ppq -= 16.0;
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

        // Now and then the host moves a parameter (Output Gain ramps, the root, the voice count, the engine and
        // Lock change). It does so inside the counted scope, just before processBlock, as the VST3 wrapper does.
        juce::RangedAudioParameter* moved = nullptr;
        float movedTo = 0.0f;
        if (rng.nextInt (100) == 0)
        {
            moved = &proc.getParam (kMoved[rng.nextInt ((int) std::size (kMoved))]);
            movedTo = rng.nextFloat();
            ++hostMoves;
        }
        proc.setNonRealtime (rng.nextInt (2000) == 0);

        {
            const alloc::Scope counting;
            if (moved != nullptr)
                moved->setValueNotifyingHost (movedTo);
            proc.processBlock (buffer, midi);
        }

        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (1);
        for (int i = 0; i < block; ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max (peak, (double) std::max (std::abs (l[i]), std::abs (r[i])));
        }

        // The editor's timer drains the diagnostics on the message thread (outside the counted scope).
        if (b % 1500 == 0)
            proc.getDiag().snapshot();
    }

    const double seconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    const auto c = alloc::counts();
    note (fmt ("%.1f minutes of audio (%lld blocks) in %.1f s; counting %s", minutes, (long long) totalBlocks, seconds, alloc::coverage()));

    check (totalBlocks > 0 && hostMoves > 0, fmt ("the run covered %lld blocks and %lld host parameter changes", (long long) totalBlocks, hostMoves));
    check (finite && peak > 0.01 && peak <= 0.98, fmt ("the probe played (peak %.3f, at most 0.98) and every sample was finite", peak));
    check (c.news == 0 && c.deletes == 0,
           fmt ("allocations on the audio thread (host parameter changes and processBlock): %lld, frees: %lld (largest request %zu bytes)",
                c.news, c.deletes, c.largest));
}

SL_TEST ("alloc", hostChangesWithEditorOpen)
{
    section ("One minute with the editor open: host parameter changes and processBlock on an audio thread of their own");

    const double rate = 48000.0;
    const int block = 64;
    StrandLockProcessor proc;
    harness::FakePlayHead head;
    head.info.setIsPlaying (true);
    head.info.setBpm (120.0);
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (0, 2, rate, block);
    proc.prepareToPlay (rate, block);

    // The editor lives on this thread (the message thread). Its knobs, switches and menus listen to their
    // parameters through JUCE's ParameterAttachment, which posts a message when a change comes from another thread.
    StrandLockEditor editor (proc);

    const int totalBlocks = (int) (60.0 * rate / block);
    alloc::Counts change, process;
    long long moves = 0;
    bool finite = true;

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        midi.ensureSize (1024);
        juce::Random rng (2024);
        const auto add = [] (alloc::Counts& to, const alloc::Counts& more)
        {
            to.news += more.news;
            to.deletes += more.deletes;
            to.largest = std::max (to.largest, more.largest);
        };

        for (int b = 0; b < totalBlocks; ++b)
        {
            head.info.setPpqPosition ((double) b * block * 2.0 / rate);
            midi.clear();
            if (b % 40 == 0)
            {
                const juce::uint8 on[3] = { 0x90, (juce::uint8) (40 + rng.nextInt (40)), 100 };
                midi.addEvent (on, 3, rng.nextInt (block));
            }
            if (b % 40 == 30)
            {
                const juce::uint8 off[3] = { 0xb0, 123, 0 };   // all notes off
                midi.addEvent (off, 3, 0);
            }

            // Every 4th block the host moves one of the 50 parameters, as Cubase's automation would.
            juce::RangedAudioParameter* moved = nullptr;
            float movedTo = 0.0f;
            if (b % 4 == 0)
            {
                moved = &proc.getParam (rng.nextInt (sl::params::kNumParams));
                movedTo = rng.nextFloat();
                ++moves;
            }

            alloc::reset();
            {
                const alloc::Scope counting;
                if (moved != nullptr)
                    moved->setValueNotifyingHost (movedTo);
            }
            add (change, alloc::counts());
            alloc::reset();
            {
                const alloc::Scope counting;
                proc.processBlock (buffer, midi);
            }
            add (process, alloc::counts());

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
        }
    });
    audio.join();
    proc.setPlayHead (nullptr);

    note (fmt ("%d blocks, %lld host parameter changes; counting %s", totalBlocks, moves, alloc::coverage()));
    check (moves > 0 && finite, fmt ("the host moved parameters %lld times and every sample was finite", moves));
    check (process.news == 0 && process.deletes == 0,
           fmt ("allocations inside processBlock with the editor open: %lld, frees: %lld", process.news, process.deletes));

    // Each attachment posts at most one message until the message thread handles it, and that thread is
    // waiting here, so at most one per parameter. What JUCE's message queue allocates for those posts is
    // reported here and in NOTES-M0; anything per change (our listener, the diagnostics) would pass the bound.
    note (fmt ("host parameter changes with the editor open: %lld allocations, %lld frees (largest %zu bytes), from JUCE's "
               "ParameterAttachment posting to the message queue (NOTES-M0)", change.news, change.deletes, change.largest));
    check (change.news <= sl::params::kNumParams && change.deletes <= sl::params::kNumParams,
           fmt ("the host's parameter changes allocate nothing per change: %lld allocations and %lld frees for %lld changes "
                "(at most one each per parameter: JUCE's queue)", change.news, change.deletes, moves));
}
