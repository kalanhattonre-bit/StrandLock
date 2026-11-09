#pragma once

// What the audio thread tells the Diagnostics page (PLUGIN-PLAN 1.1, 1.3): small events through a lock-free
// single-producer ring, drained by DiagModel on the message thread. Totals that must never be lost (block
// counts, wraps, MIDI counts) are kept in DiagModel's atomics instead; the ring only carries the recent
// history (block starts, wraps, MIDI), so a full ring (no page open) loses nothing that matters.

#include "rt/SpscFifo.h"

#include <cstdint>

namespace sl::diag
{
    enum class EventKind : std::uint8_t
    {
        blockStart,   // a = samples, b = flags (kPlaying | kLooping | kHasPosition | kNonRealtime), x = ppq, y = bpm
        cycleWrap,    // x = the ppq the block before ended at, y = the ppq after the wrap, b = 1 when looping (else a jump)
        wrapInBlock,  // x = the block's start ppq, y = its end ppq (a block that ran over the cycle end)
        noteOn,       // a = note, b = velocity
        noteOff,      // a = note, b = velocity
        controller,   // a = number, b = value
        pitchBend,    // a = 0..16383
        otherMidi     // a = status byte
    };

    struct Event
    {
        EventKind kind = EventKind::blockStart;
        std::int32_t a = 0;
        std::int32_t b = 0;
        double x = 0.0;
        double y = 0.0;
        std::uint64_t block = 0;   // the block it happened in (counting from prepare)
        std::int32_t sample = 0;   // the sample in that block
    };

    inline constexpr std::int32_t kPlaying = 1, kLooping = 2, kHasPosition = 4, kNonRealtime = 8;

    using DiagLog = rt::SpscFifo<Event, 4096>;
}
