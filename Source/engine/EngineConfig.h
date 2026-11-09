#pragma once

// The engine's constants (PLUGIN-PLAN 1.2's sl-engine.js table, 1.7 and 1.8), named after sl-engine.js in
// camelCase. The seed salts and HEAD_TUNE are part of the seed contract: never change them. engine/defs.json
// holds the prototype's values (the contract test checks every one).
//
// The shared definitions the engine also uses (hash32, rand01, idHash, rateBeats, travelSpeed, gShape, the lane
// ids and defaults, F32_EPS) are the core/ ones (PLUGIN-PLAN 1.2): core/Seeds.h, Rates.h, Travel.h, Curves.h,
// Lanes.h.

#include "core/Inst.h"

#include <cmath>
#include <cstdint>

namespace sl::engine
{
    //==============================================================================================
    // sl-engine.js's own constants.

    inline constexpr int kCtrl = 32;           // CTRL: control rate; lanes, macros, heads and speeds update every 32 samples
    inline constexpr int kMaxHeads = 8;        // MAX_HEADS
    inline constexpr int kVoiceGrains = 8;     // VOICE_GRAINS: at most 8 grains per Voice head (grains get shorter above)
    inline constexpr int kCloudGrains = 5;     // CLOUD_GRAINS: 3 overlap by design, 5 leave room while the size moves
    inline constexpr int kLockMaxN = 12;       // LOCK_MAX_N: 8 heads x 2 captures x 12 taps = 192 = the 48 kHz budget
    inline constexpr int kBudget48k = 192;     // BUDGET_48K: load units per sample at 48 kHz
    inline constexpr int kMinBudget = 16;      // the budget never goes below this (Math.max (16, ...))
    inline constexpr int kHannN = 4096;        // HANN_N: the Hann table holds kHannN + 2 values (engine/Tables)
    inline constexpr int kFadeN = 1024;        // FADE_N: the equal-power fade table holds kFadeN + 2 values

    // Seed salts (PLUGIN-PLAN 1.7). Argument orders differ between them: copy each hash32 call exactly.
    inline constexpr std::int32_t kSaltJump = 0x5ca7;    // SALT_JUMP: Scatter jump targets
    inline constexpr std::int32_t kSaltSkip = 0x51c1;    // SALT_SKIP: Refresh skips
    inline constexpr std::int32_t kSaltSpray = 0x5b7a;   // SALT_SPRAY: grain and Lock grab spray
    inline constexpr std::int32_t kSaltDrift = 0x0d71;   // SALT_DRIFT: per-head speed drift
    inline constexpr std::int32_t kSaltTiny = 0x7177;    // SALT_TINY: the tiny offsets of heads 2..8
    inline constexpr std::int32_t kSaltStart = 0x57a7;   // SALT_START: random Start positions

    /** HEAD_TUNE: the semitone offset of each head, indexed by core::HeadTune (same, detune, octaves, fifths). */
    inline constexpr double kHeadTune[4][kMaxHeads] = {
        { 0, 0, 0, 0, 0, 0, 0, 0 },
        { 0, 0.07, -0.07, 0.14, -0.14, 0.21, -0.21, 0.28 },
        { 0, 12, -12, 24, 0, 12, -12, 24 },
        { 0, 7, 12, 19, -5, 7, 12, 19 },
    };

    /** DIR: each Motion's head direction, indexed by core::Motion (forward, backward, bounce, mirror, freeze,
        scatter). Mirror's odd heads go backwards (the voice tick decides that). */
    inline constexpr int kMotionDir[6] = { 1, -1, 1, 1, 0, 1 };

    /** K_TAPE .. K_LOCK: how a voice reads the sample this tick (its engine, with Lock apart from Cloud). Also the
        display's engine field. */
    enum class Kc : std::uint8_t { tape = 0, voice = 1, cloud = 2, lock = 3 };

    /** A voice's kind: a clip note, or a key played live (the display's kind field). */
    enum class VoiceKind : std::uint8_t { clip = 0, live = 1 };

    // Times the prototype writes inline (sl-engine.js), in seconds.
    inline constexpr double kAttackSeconds = 0.008;      // aStep = 1 / (0.008 sr)
    inline constexpr double kReleaseSeconds = 0.06;      // rStep = 1 / (0.06 sr)
    inline constexpr double kStealSeconds = 0.005;       // sStep = 1 / (0.005 sr)
    inline constexpr double kChaseFadeSeconds = 0.010;   // a chased voice's fade-in: aStep = 1 / (0.010 sr)
    inline constexpr double kSlewSeconds = 0.02;         // head gains, spread offsets, thinning fades, the Stop gate
    inline constexpr double kTapeXfadeSeconds = 0.005;   // a Tape Scatter jump or Refresh stutter crossfades 5 ms
    inline constexpr double kLimiterCeiling = 0.9;       // the output limiter: instant attack, 80 ms release
    inline constexpr double kLimiterReleaseSeconds = 0.08;
    inline constexpr double kOutputClamp = 0.98;         // the output never leaves +-0.98
    inline constexpr double kOutputGuard = 1e6;          // a sample outside +-1e6 (or NaN) becomes 0
    inline constexpr double kQuietLevel = 1e-3;          // plan(): a voice with level x env below this is "silent"
    inline constexpr double kSoftBudget = 0.75;          // plan(): steady heads are planned to floor (0.75 B)
    inline constexpr int kChaseMaxTicks = 1 << 17;       // chase(): the prototype's cap on replayed ticks

    // Sample rates the engine accepts (Engine constructor and setSample clamps).
    inline constexpr double kDefaultSampleRate = 48000.0;
    inline constexpr double kMinSampleRate = 8000.0, kMaxSampleRate = 384000.0;     // the engine's own rate
    inline constexpr double kMinSourceRate = 4000.0, kMaxSourceRate = 768000.0;     // a sample's rate

    /** this.budget = max (16, floor (BUDGET_48K x 48000 / sr)): 208 at 44.1 kHz, 192 at 48, 96 at 96 kHz. */
    inline int budgetFor (double sampleRate) noexcept
    {
        const double b = std::floor (kBudget48k * 48000 / sampleRate);
        return b > kMinBudget ? static_cast<int> (b) : kMinBudget;
    }

    //==============================================================================================
    // The plugin's additions (PLUGIN-PLAN 1.2, 1.8, 2.3, 8.2).

    inline constexpr int kMaxPolyphony = 32;     // inst.polyphony is 1..32: voices that are not stealing
    inline constexpr int kVoicePool = 64;        // preallocated Voice objects: 32 playable + room for steal tails
    inline constexpr int kMaxStarts = 256;       // scanStarts' fixed array per 32-sample segment (more are deferred)
    inline constexpr int kWaitList = kVoicePool * kMaxHeads;   // plan()'s (head, voice) wait list: 512 pairs

    // Chase cost (8.2 #22): bounded by samples, never by measured time.
    inline constexpr int kChaseTicksNow = 4096;      // ticks one chase moment may run at once, shared in doChase order
    inline constexpr int kChaseTicksPer32 = 256;     // the rest: this many ticks per 32 output samples
    inline constexpr int kChaseCoarseAfter = 4096;   // ticks further than this before the chase point are coarse:
    inline constexpr int kChaseCoarseEvery = 16;     // tick once per 16, then ffAdvance (16 x 32 = 512 samples)

    inline constexpr double kRampMs = 20.0;          // the ramp20ms smoothers (engine/InstSmoother, 2.3)

    static_assert (kVoicePool >= 2 * kMaxPolyphony, "every playable voice can leave a steal tail");
    static_assert (kMaxHeads * 2 * kLockMaxN == kBudget48k, "8 heads x 2 captures x 12 taps = the 48 kHz budget");
}
