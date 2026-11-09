#pragma once

// The instrument settings (BLUEPRINT 1.4, `inst`) as plain data. ParamBridge builds one of these on
// the audio thread every block, so Inst is trivially copyable and holds no strings or heap memory.
//
// Field types follow the prototype: every JavaScript number is a double (polyphony, root and
// bendRange too, because the engine's clamps work on the double), except seed (int32, the value
// `x | 0` leaves), the mod-wheel CCs and the key (int). A JavaScript string choice is an enum, a scale
// is an index into kScales, a mod-wheel target is a lane index.

#include "Lanes.h"
#include "Rates.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace sl::core
{
    class JsonValue;

    // The choices, each in its SLModel list order (the parameters' choice order).
    enum class Engine : std::uint8_t { tape, voice, cloud };
    enum class Motion : std::uint8_t { forward, backward, bounce, mirror, freeze, scatter };
    enum class HeadTune : std::uint8_t { same, detune, octaves, fifths };
    enum class RefreshMode : std::uint8_t { smooth, free, sync };
    enum class AtEnd : std::uint8_t { loop, hold, stop };
    enum class GlideMode : std::uint8_t { off, legato, always };

    // The prototype's strings for them, as saved.
    inline constexpr const char* kEngineCodes[] = { "tape", "voice", "cloud" };
    inline constexpr const char* kMotionCodes[] = { "forward", "backward", "bounce", "mirror", "freeze", "scatter" };
    inline constexpr const char* kHeadTuneCodes[] = { "same", "detune", "octaves", "fifths" };
    inline constexpr const char* kRefreshModeCodes[] = { "smooth", "free", "sync" };
    inline constexpr const char* kAtEndCodes[] = { "loop", "hold", "stop" };
    inline constexpr const char* kGlideModeCodes[] = { "off", "legato", "always" };

    const char* code (Engine) noexcept;
    const char* code (Motion) noexcept;
    const char* code (HeadTune) noexcept;
    const char* code (RefreshMode) noexcept;
    const char* code (AtEnd) noexcept;
    const char* code (GlideMode) noexcept;

    std::optional<Engine> parseEngine (std::string_view) noexcept;
    std::optional<Motion> parseMotion (std::string_view) noexcept;
    std::optional<HeadTune> parseHeadTune (std::string_view) noexcept;
    std::optional<RefreshMode> parseRefreshMode (std::string_view) noexcept;
    std::optional<AtEnd> parseAtEnd (std::string_view) noexcept;
    std::optional<GlideMode> parseGlideMode (std::string_view) noexcept;

    inline constexpr int kNumMacros = 8;

    /** A mod-wheel target that is off (the prototype's 'off'). */
    inline constexpr std::int8_t kModOff = -1;

    /** Vibrato Sync's Free (the prototype's behaviour, PLUGIN-PLAN 8.2 #12). */
    inline constexpr std::int8_t kVibSyncFree = -1;

    struct RefreshSettings
    {
        RefreshMode mode = RefreshMode::smooth;
        double ms = 120.0;
        RateId rate = RateId::r1_16;
        bool lockToBeat = true;
        double skip = 0.0;
    };

    struct RegionSettings
    {
        double start = 0.0;   // fractions of the loaded sound
        double end = 1.0;
        AtEnd atEnd = AtEnd::loop;
        double xfadeMs = 10.0;
    };

    struct OffsetSettings
    {
        double amp = 0.0, vib = 0.0, tone = 0.0, formant = 0.0, hollow = 0.0;
    };

    struct ModWheel
    {
        std::int8_t target = kModOff;   // a lane index (LaneId), or kModOff
        int cc = 1;
    };

    struct Inst
    {
        Engine engine = Engine::voice;
        bool lock = false;
        Motion motion = Motion::forward;
        double speed = 1.0;
        double heads = 1.0;
        double spread = 0.25;
        double grainVoice = 2.0;      // cycles of the sample root
        double grainCloud = 140.0;    // ms
        double grainLock = 4.0;       // wavelengths of the played note
        RefreshSettings refresh;
        double spray = 0.0;
        double pitch = 0.0;
        bool tape = false;
        bool rev = false;
        double drift = 0.0;
        HeadTune headTune = HeadTune::same;
        RegionSettings region;
        RateId jump = RateId::r1_8;
        std::array<double, kNumMacros> macros { 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5 };
        OffsetSettings offsets;
        GlideMode glideMode = GlideMode::legato;
        double glideTime = 0.5;       // beats
        double vibRate = 5.5;         // Hz
        double root = 60.0;
        double cents = 0.0;
        int key = 0;                  // 0 = C .. 11 = B (UI only)
        std::uint8_t scale = 0;       // index into kScales (UI only)
        bool scaleLock = false;       // (UI only)
        double velSens = 0.7;
        double polyphony = 16.0;
        double outGain = 0.0;         // dB
        double bendRange = 2.0;       // semitones
        ModWheel mod1 { static_cast<std::int8_t> (LaneId::vib), 1 };
        ModWheel mod2 { static_cast<std::int8_t> (LaneId::tone), 2 };
        bool liveLegato = false;
        std::int32_t seed = 1;
        std::int8_t vibSync = kVibSyncFree;   // plugin addition: kVibSyncFree, or a RateId
    };

    static_assert (std::is_trivially_copyable_v<Inst>, "the audio thread copies Inst by value");

    /** A partial instrument (SLModel.mergeInst's patch): only the fields that are set change. Arrays
        replace whole (macros), nested settings merge field by field, as mergeInst does. */
    struct InstPatch
    {
        struct Refresh
        {
            std::optional<RefreshMode> mode;
            std::optional<double> ms;
            std::optional<RateId> rate;
            std::optional<bool> lockToBeat;
            std::optional<double> skip;
        };

        struct Region
        {
            std::optional<double> start, end;
            std::optional<AtEnd> atEnd;
            std::optional<double> xfadeMs;
        };

        struct Offsets
        {
            std::optional<double> amp, vib, tone, formant, hollow;
        };

        struct Mod
        {
            std::optional<std::int8_t> target;
            std::optional<int> cc;
        };

        std::optional<Engine> engine;
        std::optional<bool> lock;
        std::optional<Motion> motion;
        std::optional<double> speed, heads, spread, grainVoice, grainCloud, grainLock;
        Refresh refresh;
        std::optional<double> spray, pitch;
        std::optional<bool> tape, rev;
        std::optional<double> drift;
        std::optional<HeadTune> headTune;
        Region region;
        std::optional<RateId> jump;
        std::optional<std::array<double, kNumMacros>> macros;
        Offsets offsets;
        std::optional<GlideMode> glideMode;
        std::optional<double> glideTime, vibRate, root, cents;
        std::optional<int> key;
        std::optional<std::uint8_t> scale;
        std::optional<bool> scaleLock;
        std::optional<double> velSens, polyphony, outGain, bendRange;
        Mod mod1, mod2;
        std::optional<bool> liveLegato;
        std::optional<std::int32_t> seed;
        std::optional<std::int8_t> vibSync;
    };

    /** SLModel.defaultInst () (and the engine's identical copy), plus vibSync Free. */
    Inst defaultInst() noexcept;

    /** The engine's own clamps (sl-engine.js normInst), kept apart from the model's normalizeInst: speed
        -16..16, grainLock 1..64, refresh.ms 5..10000, xfadeMs 0..1000, vibRate 0..50, outGain -60..24, and
        so on. A non-finite number takes its default, the Region edges are swapped when end < start,
        polyphony is rounded with Math.round, and a mod target that is not a lane turns off. A choice
        holding a value outside its list plays as the engine's fallback for an unknown string (voice,
        forward, smooth, loop, same; 1/16 for a rate, which is what rateBeats plays for an unknown id).
        The prototype engine never reads glideMode, glideTime, vibSync, key, scale or scaleLock; the plugin's
        engine indexes by some of them, so here a glideMode, vibSync or scale outside its list reads legato,
        Free or Chromatic, a non-finite glideTime reads 0.5, and key and scaleLock pass through. */
    Inst clampForEngine (const Inst& in) noexcept;

    //==============================================================================================
    // Declared for M1, which implements them in this file (PLUGIN-PLAN 7.1, 7.2).

    /** SLModel.mergeInst. */
    Inst mergeInst (const Inst& inst, const InstPatch& patch);

    /** SLModel.normalizeInst: the model's clamps (speed 0..16, grainLock 1..16, outGain -60..12, ...). */
    Inst normalizeInst (Inst inst);

    /** An instrument from a prototype save or a fixture, along SLModel.normalizeInst's rules. */
    Inst instFromJson (const JsonValue& value);

    /** The object SLModel.normalizeInst returns, field for field. */
    JsonValue instToJson (const Inst& inst);
}
