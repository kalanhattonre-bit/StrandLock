#pragma once

// Live MIDI to the engine (PLUGIN-PLAN 1.10, 8.2 #20), and a whole block played sample-accurately: the block is
// rendered up to each event's sample offset before the event is applied, so every event lands on its exact
// sample at any block size, and a host cycle wrap (HostSync) is applied at its own sample the same way.
//
//   note on, velocity v > 0   noteOn (p, v / 127)            (the engine reads > 1 as 0..127; send v / 127)
//   note on, velocity 0       noteOff (p)
//   note off                  noteOff (p)
//   CC mod1.cc / mod2.cc      setController (mod1 | mod2, value / 127)   (both, when both wheels use that CC)
//   CC64                      setController (sustain, value >= 64 ? 1 : 0)
//   CC120, CC123              allNotesOff ()
//   pitch wheel               setController (bend, (v - 8192) / 8192 below centre, / 8191 above: full up is +1)
//
// Everything else is ignored (MIDI Learn is M8). JUCE's VST3 wrapper turns Cubase's controller parameters back
// into 7-bit MIDI, so a CC is one byte of value (risks 6.4).
//
// Written against the engine's public API as a template, so the tests pass a recording fake: Sink needs
// setTransport (const engine::Transport&), noteOn (double, double), noteOff (double), allNotesOff (),
// setController (engine::Ctrl, double) and process (float*, float*, int). Audio thread: no allocation.

#include "HostSync.h"

#include "engine/Engine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

namespace sl::host
{
    /** The CCs the two mod wheels listen to (the INST state, ParamBridge's NonParamAtomics). */
    struct MidiSettings
    {
        int mod1Cc = 1;
        int mod2Cc = 2;
    };

    inline constexpr int kSustainCc = 64;
    inline constexpr int kAllSoundOffCc = 120;
    inline constexpr int kAllNotesOffCc = 123;

    /** The pitch wheel's 14-bit value as the engine's bend: -1 at 0, 0 at 8192, exactly +1 at 16383. */
    constexpr double bendOf (int value14) noexcept
    {
        const int v = (value14 < 0 ? 0 : value14 > 16383 ? 16383 : value14) - 8192;
        return v < 0 ? v / 8192.0 : v / 8191.0;
    }

    class MidiRouter
    {
    public:
        /** One MIDI message (status byte first). */
        template <class Sink>
        static void route (Sink& engine, const std::uint8_t* data, int size, const MidiSettings& settings) noexcept
        {
            if (data == nullptr || size < 3)
                return;
            const int type = data[0] & 0xf0;
            const int d1 = data[1] & 0x7f, d2 = data[2] & 0x7f;
            switch (type)
            {
                case 0x90:
                    if (d2 > 0)
                        engine.noteOn (static_cast<double> (d1), d2 / 127.0);
                    else
                        engine.noteOff (static_cast<double> (d1));
                    break;

                case 0x80:
                    engine.noteOff (static_cast<double> (d1));
                    break;

                case 0xb0:
                    if (d1 == settings.mod1Cc)
                        engine.setController (engine::Ctrl::mod1, d2 / 127.0);
                    if (d1 == settings.mod2Cc)
                        engine.setController (engine::Ctrl::mod2, d2 / 127.0);
                    if (d1 == kSustainCc)
                        engine.setController (engine::Ctrl::sustain, d2 >= 64 ? 1.0 : 0.0);
                    if (d1 == kAllSoundOffCc || d1 == kAllNotesOffCc)
                        engine.allNotesOff();
                    break;

                case 0xe0:
                    engine.setController (engine::Ctrl::bend, bendOf (d1 | (d2 << 7)));
                    break;

                default:
                    break;
            }
        }

        /** One block of n samples into L and R: setTransport (transport.start) at sample 0, then each event in
            the buffer's order, rendering up to its sample first; the cycle wrap's setTransport at its sample
            (events at that sample come after it); then the rest of the block. */
        template <class Sink>
        static void renderBlock (Sink& engine, const BlockTransport& transport, const juce::MidiBuffer& midi,
                                 const MidiSettings& settings, float* L, float* R, int n) noexcept
        {
            int pos = 0;
            auto renderTo = [&] (int at)
            {
                if (at > pos)
                {
                    engine.process (L + pos, R + pos, at - pos);
                    pos = at;
                }
            };

            engine.setTransport (transport.start);
            bool wrapped = transport.wrapAt < 0 || transport.wrapAt >= n;
            for (const auto meta : midi)
            {
                const int at = meta.samplePosition < 0 ? 0 : meta.samplePosition > n ? n : meta.samplePosition;
                if (! wrapped && transport.wrapAt <= at)
                {
                    renderTo (transport.wrapAt);
                    engine.setTransport (transport.wrap);
                    wrapped = true;
                }
                renderTo (at);
                route (engine, meta.data, meta.numBytes, settings);
            }
            if (! wrapped)
            {
                renderTo (transport.wrapAt);
                engine.setTransport (transport.wrap);
            }
            renderTo (n);
        }
    };
}
