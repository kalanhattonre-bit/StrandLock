#pragma once

// The host's transport to the engine (PLUGIN-PLAN 1.10): once per block, and again where a host cycle wraps
// inside the block, HostSync turns what the play head says into engine::Transport.
//
// - Playing: beat = host ppq - the clip's offsetBeats (negative beats pass through: the pre-roll, 8.2 #4; the
//   engine never wraps them into the clip), bpm from the host, loop = the clip's loop setting.
// - Tempo: the host's, or 120 when it gives none or one outside 1..1000.
// - Stopped: playing false, and the host beat only when it moved more than 1e-6 beat since the last block (NaN
//   otherwise), so the engine's free clock keeps running for live Scatter and synced Refresh (8.2 #5).
// - A cycle wrap inside a block (8.2 #24): when the host reports looping and its position plus the block's beats
//   crosses the loop end, the block is split at ceil ((loopEnd - ppq) / beatsPerSample) and the second
//   setTransport carries beat = loopStart - offsetBeats. A host that splits its blocks at the cycle end never
//   meets this.
// - Preview (U5) is M6's.
//
// Audio thread: plain arithmetic, no allocation. MidiRouter::renderBlock (host/MidiRouter.h) plays a block
// with the result.

#include "diag/DiagModel.h"
#include "engine/Engine.h"

#include <limits>

namespace sl::host
{
    inline constexpr double kFallbackBpm = 120.0;
    inline constexpr double kMinHostBpm = 1.0, kMaxHostBpm = 1000.0;

    /** While stopped, the host beat counts as moved past this many beats (else NaN goes to the engine). */
    inline constexpr double kStoppedMoveBeats = 1e-6;

    /** One block's transport: the setTransport at its first sample, and the one at a cycle wrap inside it. */
    struct BlockTransport
    {
        engine::Transport start;
        int wrapAt = -1;             // the first sample after the wrap (1 .. numSamples - 1), or -1
        engine::Transport wrap;
        double beatsPerSample = 0.0; // at the block's tempo (0 while stopped)
    };

    class HostSync
    {
    public:
        /** The sample rate the blocks run at. Forgets the last host beat (the next stopped block passes it on). */
        void prepare (double sampleRate) noexcept;

        /** Forgets the last host beat. */
        void reset() noexcept;

        /** The transport for one block of numSamples (the play head read by diag::readHostBlock). offsetBeats and
            clipLoop are the clip's (the snapshot's) settings. */
        BlockTransport plan (const diag::HostBlock& host, int numSamples, double offsetBeats, bool clipLoop) noexcept;

        /** The host tempo used: the host's when it is in 1..1000, else 120. */
        static double tempoOf (const diag::HostBlock& host) noexcept;

        double sampleRate() const noexcept { return sr; }

    private:
        double sr = 48000.0;
        bool haveLast = false;
        double lastPpq = 0.0;
    };
}
