#pragma once

// The editor's commands to the audio thread (PLUGIN-PLAN 1.3): an rt::SpscFifo of plain UiCommands, pushed by
// the message thread (one producer) and drained by processBlock at the start of each block (one consumer).
// Push never blocks or allocates and fails when the queue is full (256 commands waiting: the audio thread has
// stopped, so the command would not be heard anyway).
//
// What processBlock does with each (Integration):
//   auditionOn     engine.noteOn (note, velocity, {startPos})   the piano strip, or holding the mouse on the
//                                                                waveform (startPos = where, 0..1 of the Region)
//   auditionOff    engine.noteOff (note)
//   letGoWheel     engine.releaseController (mod1 or mod2)      "Let go" on the Mod wheels tab
//   allNotesOff    engine.allNotesOff ()
//   previewStart   HostSync starts its own clock at the host tempo while the host is stopped (U5, M6)
//   previewStop    HostSync stops it
//   midiLearnArm   MidiRouter reports the next other CC for that wheel (M8); wheel -1 disarms

#include "rt/SpscFifo.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace sl::doc
{
    enum class UiCommandKind : std::uint8_t
    {
        auditionOn,
        auditionOff,
        letGoWheel,
        allNotesOff,
        previewStart,
        previewStop,
        midiLearnArm
    };

    struct UiCommand
    {
        UiCommandKind kind = UiCommandKind::allNotesOff;
        std::int8_t note = 60;           // auditionOn / auditionOff: MIDI note 0..127 (60 = C3)
        std::int8_t wheel = 0;           // letGoWheel, midiLearnArm: 0 = Mod 1, 1 = Mod 2 (midiLearnArm: -1 disarms)
        double velocity = 0.8;           // auditionOn: 0..1 (the prototype auditions at 0.7 on the strip, 0.8 on the waveform)
        double startPos = std::numeric_limits<double>::quiet_NaN();   // auditionOn: 0..1 of the Region, NaN = none

        static UiCommand auditionOn (int midiNote, double vel, double startPosition = std::numeric_limits<double>::quiet_NaN()) noexcept
        {
            UiCommand c;
            c.kind = UiCommandKind::auditionOn;
            c.note = static_cast<std::int8_t> (midiNote < 0 ? 0 : midiNote > 127 ? 127 : midiNote);
            c.velocity = vel;
            c.startPos = startPosition;
            return c;
        }

        static UiCommand auditionOff (int midiNote) noexcept
        {
            UiCommand c;
            c.kind = UiCommandKind::auditionOff;
            c.note = static_cast<std::int8_t> (midiNote < 0 ? 0 : midiNote > 127 ? 127 : midiNote);
            return c;
        }

        static UiCommand letGoWheel (int wheelIndex) noexcept
        {
            UiCommand c;
            c.kind = UiCommandKind::letGoWheel;
            c.wheel = static_cast<std::int8_t> (wheelIndex == 1 ? 1 : 0);
            return c;
        }

        static UiCommand allNotesOff() noexcept
        {
            UiCommand c;
            c.kind = UiCommandKind::allNotesOff;
            return c;
        }

        static UiCommand previewStart() noexcept
        {
            UiCommand c;
            c.kind = UiCommandKind::previewStart;
            return c;
        }

        static UiCommand previewStop() noexcept
        {
            UiCommand c;
            c.kind = UiCommandKind::previewStop;
            return c;
        }

        static UiCommand midiLearnArm (int wheelIndex) noexcept
        {
            UiCommand c;
            c.kind = UiCommandKind::midiLearnArm;
            c.wheel = static_cast<std::int8_t> (wheelIndex == 0 || wheelIndex == 1 ? wheelIndex : -1);
            return c;
        }
    };

    static_assert (std::is_trivially_copyable_v<UiCommand>, "commands cross threads as plain bytes");

    inline constexpr std::size_t kCommandQueueSize = 256;

    using CommandQueue = rt::SpscFifo<UiCommand, kCommandQueueSize>;
}
