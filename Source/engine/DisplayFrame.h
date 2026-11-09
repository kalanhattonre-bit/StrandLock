#pragma once

// The display frame (PLUGIN-PLAN 1.5): the worklet's {type: 'display'} message as plain data. The audio thread
// fills one with Engine::fillDisplay (a port of getDisplay () and stats () that writes into the frame instead of
// allocating) straight into DisplaySlot's back buffer; the editor's 30 Hz timer takes the newest with
// takeLatest () and maps noteIdHash to its own clip's notes. No strings cross threads.
//
// The processor writes a frame after a block once displayEvery (sr) samples have passed since the last one
// (the worklet's rule: max (128, round (sr / 30))).

#include "core/JsCompat.h"
#include "engine/EngineConfig.h"
#include "rt/LatestSlot.h"

#include <cstdint>
#include <type_traits>

namespace sl::engine
{
    inline constexpr int kDisplayVoices = 64;    // every voice of the pool
    inline constexpr int kDisplayGrains = 128;   // getDisplay stops at 128 grains

    /** One head that is on (JS heads[]: {i, pos, dir, level, thinned}). */
    struct DisplayHead
    {
        std::uint8_t index;   // h.i, 0..7
        std::int8_t dir;      // h.dir: 1, -1 or 0
        bool thinned;         // h.want && !h.allowed
        float pos;            // (G.rs + mapPos (G, h.raw + h.off)) / S.n: 0..1 of the file
        float level;          // 0.5 (h.g1L + h.g1R) h.stopG
    };

    /** One voice that is not done (JS voices[]). */
    struct DisplayVoice
    {
        std::uint32_t noteIdHash;   // idHash of v.note.id ("live:" + p for a live key)
        std::int16_t p;             // v.note.p
        std::uint8_t kind;          // VoiceKind: clip, live
        std::uint8_t engine;        // Kc: tape, voice, cloud, lock (v.engineName)
        std::uint8_t motion;        // core::Motion as played (Tape plays Freeze as Forward)
        bool released;
        float env;
        std::uint8_t numHeads;      // heads[0 .. numHeads)
        DisplayHead heads[kMaxHeads];
    };

    /** One grain (JS grains[]: a Lock head's capture, or a Voice / Cloud grain). */
    struct DisplayGrain
    {
        float pos;   // 0..1 of the file
        float age;   // 0..1
    };

    /** stats () at the frame (JS d.stats). */
    struct DisplayStats
    {
        std::int32_t voices, load, thinnedHeads, maxLoad, budget, cuts, steals;
    };

    struct DisplayFrame
    {
        std::int64_t sampleTime;   // Engine::sampleTime () when it was filled
        double beat;               // songBeat (k)
        bool playing;
        std::uint16_t numVoices;   // voices[0 .. numVoices), in the engine's voice order
        DisplayVoice voices[kDisplayVoices];
        std::uint16_t numGrains;   // grains[0 .. numGrains)
        DisplayGrain grains[kDisplayGrains];
        DisplayStats stats;
        float peakL, peakR;        // the largest |output| per channel since the frame before (after the limiter)
    };

    static_assert (std::is_trivially_copyable_v<DisplayFrame>, "the display frame crosses threads as plain bytes");

    /** The audio thread's newest frame for the editor (a triple buffer, about 8.5 KB a frame). */
    using DisplaySlot = rt::LatestSlot<DisplayFrame>;

    /** Samples between two display frames: max (128, round (sr / 30)) (the worklet's `every`). */
    inline int displayEvery (double sampleRate) noexcept
    {
        const double r = core::jsRound (sampleRate / 30.0);
        return r > 128.0 ? static_cast<int> (r) : 128;
    }
}
