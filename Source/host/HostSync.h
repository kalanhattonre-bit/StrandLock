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
// - Preview (U5, M6; plan 7.8 decision 3): while the host is stopped and Kalan's Preview switch is on, HostSync runs
//   its own clock at the host tempo: playing true, the beat counted in samples from the Preview start (the host's
//   position when Preview started, minus offsetBeats; 0 when the host gives none). Moving the host's position while
//   stopped starts it again there; a tempo change carries on from the beat it reached. Preview never runs while the
//   host plays: the switch goes off then, and the block says so (previewEnded). With the clip's loop off it also
//   ends at the clip's end, as the prototype's Play does.
// - Starting the engine's clock again (M6 fix pass F1): when the host starts playing after a block that played the
//   Preview clock, or the host's position moves while Preview plays, the engine is playing already. Its rule for a
//   beat sent while playing (a difference of 0.1 beat or less is the host's jitter and changes nothing) would then
//   keep Preview's clock, up to 0.1 beat off the host's. So the block says restart, and MidiRouter stops the
//   engine's clock before it starts it at the new beat, as a Play from stopped does (the prototype's rechase: stop,
//   then start at the beat).
//
// Audio thread: plain arithmetic, no allocation. MidiRouter::renderBlock (host/MidiRouter.h) plays a block
// with the result.

#include "diag/DiagModel.h"
#include "engine/Engine.h"

#include <atomic>
#include <cstdint>
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
        bool previewing = false;     // the block plays the Preview clock (M6)
        bool previewEnded = false;   // plan turned the Preview switch off itself: the host plays, or the clip ended
        bool restart = false;        // the engine's clock starts again at start.beat: the host plays after a Preview,
                                     // or its position moved while Preview plays (MidiRouter stops the clock first)
    };

    /** Kalan's Preview switch (U5, M6), written by two threads: the message thread's clicks, and the audio thread,
        which turns it off when the host plays or a clip that does not loop has ended. One atomic word: the on bit,
        and a count of clicks above it. The audio thread reads the word once at the start of a block and turns the
        switch off only while it still holds that word, so a click made after the block began always stands. No
        allocation, lock or wait. */
    class PreviewSwitch
    {
    public:
        /** A click (message thread): on or off, with the click count one higher. */
        void set (bool on) noexcept
        {
            std::uint32_t seen = word.load (std::memory_order_acquire);
            while (! word.compare_exchange_weak (seen, (((seen >> 1) + 1u) << 1) | (on ? 1u : 0u), std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            {
            }
        }

        /** The word now: the audio thread reads it once at the start of each block. */
        std::uint32_t read() const noexcept { return word.load (std::memory_order_acquire); }

        static bool isOn (std::uint32_t w) noexcept { return (w & 1u) != 0; }
        bool isOn() const noexcept { return isOn (read()); }

        /** The audio thread turns the switch off, if it still holds `seen` (the word its block read). False when a
            click came after that: the click stands. */
        bool turnOffIf (std::uint32_t seen) noexcept
        {
            std::uint32_t expected = seen;
            return word.compare_exchange_strong (expected, seen & ~1u, std::memory_order_acq_rel, std::memory_order_acquire);
        }

    private:
        std::atomic<std::uint32_t> word { 0 };
    };

    class HostSync
    {
    public:
        /** The sample rate the blocks run at. Forgets the last host beat (the next stopped block passes it on) and
            where a Preview was (the next block starts it again from the host's position). */
        void prepare (double sampleRate) noexcept;

        /** Forgets the last host beat and where a Preview was; the Preview switch stays as it is. */
        void reset() noexcept;

        /** Preview's switch (U5, M6), set before each plan: the processor reads Kalan's switch every block. */
        void setPreview (bool on) noexcept { previewOn = on; }
        bool isPreviewOn() const noexcept { return previewOn; }

        /** The last plan played the Preview clock. */
        bool isPreviewing() const noexcept { return previewRunning; }

        /** Where the Preview clock is now, in song beats (the next block's first sample), while it runs. */
        double previewBeat() const noexcept;

        /** The transport for one block of numSamples (the play head read by diag::readHostBlock). offsetBeats and
            clipLoop are the clip's (the snapshot's) settings; clipBeats is its length in beats (0: not known), where
            a Preview of a clip that does not loop ends. */
        BlockTransport plan (const diag::HostBlock& host, int numSamples, double offsetBeats, bool clipLoop, double clipBeats = 0.0) noexcept;

        /** The host tempo used: the host's when it is in 1..1000, else 120. */
        static double tempoOf (const diag::HostBlock& host) noexcept;

        double sampleRate() const noexcept { return sr; }

    private:
        double sr = 48000.0;
        bool haveLast = false;
        double lastPpq = 0.0;

        // Preview (M6): the switch, and the clock while it runs (its start beat, the samples since then, its tempo)
        bool previewOn = false;
        bool previewRunning = false;
        double previewBeat0 = 0.0;
        std::int64_t previewSamples = 0;
        double previewBpm = kFallbackBpm;
    };
}
