#pragma once

// The 50 v1 parameters in their frozen order (PLUGIN-PLAN 2.1, docs/parameters.json). Ids, ranges and
// choice-list lengths freeze at the first release: a new option is a new parameter, appended at the end.
// The parameter test (Tests/plugin/test_params.cpp) compares every field here with docs/parameters.json.

#include "core/Rates.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace sl::params
{
    /** ParameterID { id, kParamVersionHint } for every v1 parameter (GrainLock's convention). */
    inline constexpr int kParamVersionHint = 1;

    inline constexpr int kNumParams = 50;

    /** Indices into the parameter list (the host's parameter order). */
    enum class P : int
    {
        macro1, macro2, macro3, macro4, macro5, macro6, macro7, macro8,
        ampOffset, vibOffset, toneOffset, formantOffset, hollowOffset,
        outGain,
        engine, lock, motion, speed, heads, spread, grainVoice, grainCloud, grainLock, spray, drift, pitch,
        tape, reverseGrains, headTune, jump,
        refreshMode, refreshMs, refreshRate, refreshBeatLock, refreshSkip,
        regionStart, regionEnd, atEnd, loopXfade,
        glideMode, glideTime,
        vibRate, vibSync, bendRange, velSens, polyphony, liveLegato,
        rootNote, rootCents,
        bypass
    };

    constexpr int index (P p) noexcept { return static_cast<int> (p); }

    enum class Kind : std::uint8_t { real, choice, toggle, whole };   // float, choice, bool, int

    struct ParamSpec
    {
        const char* id;
        const char* name;
        Kind kind;
        float min, max, def;            // plain values; a choice's are indices, a bool's 0 and 1
        bool hasSkew;
        float skewCentre;               // the value at normalised 0.5 (setSkewForCentre) when hasSkew
        const char* const* choices;     // a choice's labels in list order, else nullptr
        int numChoices;
    };

    namespace detail
    {
        constexpr std::array<const char*, core::kNumRates> makeRateChoices() noexcept
        {
            std::array<const char*, core::kNumRates> a {};
            for (int i = 0; i < core::kNumRates; ++i)
                a[static_cast<std::size_t> (i)] = core::kRates[i].label;
            return a;
        }

        constexpr std::array<const char*, core::kNumRates + 1> makeVibSyncChoices() noexcept
        {
            std::array<const char*, core::kNumRates + 1> a {};
            a[0] = "Free";
            for (int i = 0; i < core::kNumRates; ++i)
                a[static_cast<std::size_t> (i + 1)] = core::kRates[i].label;
            return a;
        }
    }

    inline constexpr const char* kEngineChoices[] = { "Tape", "Voice", "Cloud" };
    inline constexpr const char* kMotionChoices[] = { "Forward", "Backward", "Bounce", "Mirror", "Freeze", "Scatter" };
    inline constexpr const char* kHeadTuneChoices[] = { "Same", "Detune", "Octaves", "Fifths" };
    inline constexpr const char* kRefreshModeChoices[] = { "Smooth", "Free", "Synced" };
    inline constexpr const char* kAtEndChoices[] = { "Loop", "Hold", "Stop" };
    inline constexpr const char* kGlideModeChoices[] = { "Off", "Legato", "Always" };
    inline constexpr auto kRateChoices = detail::makeRateChoices();
    inline constexpr auto kVibSyncChoices = detail::makeVibSyncChoices();

    // name, kind, min, max, default, skew?, skew centre, choices, count: docs/parameters.json, field for field.
    inline constexpr ParamSpec kParams[kNumParams] = {
        { "macro1",          "Speed",                 Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "macro2",          "Spread",                Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "macro3",          "Heads",                 Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "macro4",          "Refresh",               Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "macro5",          "Grain",                 Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "macro6",          "Spray",                 Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "macro7",          "Tone",                  Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "macro8",          "Formant",               Kind::real,   0.0f,   1.0f,    0.5f,  false, 0.0f,   nullptr, 0 },
        { "ampOffset",       "Volume Offset",         Kind::real,  -1.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "vibOffset",       "Vibrato Offset",        Kind::real,  -1.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "toneOffset",      "Tone Offset",           Kind::real,  -1.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "formantOffset",   "Formant Offset",        Kind::real,  -1.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "hollowOffset",    "Hollow Offset",         Kind::real,  -1.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "outGain",         "Output Gain",           Kind::real, -60.0f,  12.0f,    0.0f,  true, -12.0f,  nullptr, 0 },
        { "engine",          "Engine",                Kind::choice, 0.0f,   2.0f,    1.0f,  false, 0.0f,   kEngineChoices, 3 },
        { "lock",            "Lock",                  Kind::toggle, 0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "motion",          "Motion",                Kind::choice, 0.0f,   5.0f,    0.0f,  false, 0.0f,   kMotionChoices, 6 },
        { "speed",           "Sample Speed",          Kind::real,   0.0f,  16.0f,    1.0f,  true,   1.0f,  nullptr, 0 },
        { "heads",           "Sample Heads",          Kind::real,   1.0f,   8.0f,    1.0f,  false, 0.0f,   nullptr, 0 },
        { "spread",          "Sample Spread",         Kind::real,   0.0f,   1.0f,    0.25f, false, 0.0f,   nullptr, 0 },
        { "grainVoice",      "Voice Grain",           Kind::real,   0.25f, 16.0f,    2.0f,  true,   2.0f,  nullptr, 0 },
        { "grainCloud",      "Cloud Grain",           Kind::real,   5.0f, 2000.0f, 140.0f,  true, 140.0f,  nullptr, 0 },
        { "grainLock",       "Lock Grain",            Kind::real,   1.0f,  16.0f,    4.0f,  true,   4.0f,  nullptr, 0 },
        { "spray",           "Sample Spray",          Kind::real,   0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "drift",           "Drift",                 Kind::real,   0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "pitch",           "Pitch",                 Kind::real, -48.0f,  48.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "tape",            "Tape",                  Kind::toggle, 0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "reverseGrains",   "Each Grain Backwards",  Kind::toggle, 0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "headTune",        "Head Tune",             Kind::choice, 0.0f,   3.0f,    0.0f,  false, 0.0f,   kHeadTuneChoices, 4 },
        { "jump",            "Jump Every",            Kind::choice, 0.0f,  20.0f,    9.0f,  false, 0.0f,   kRateChoices.data(), core::kNumRates },
        { "refreshMode",     "Refresh Mode",          Kind::choice, 0.0f,   2.0f,    0.0f,  false, 0.0f,   kRefreshModeChoices, 3 },
        { "refreshMs",       "Refresh Time",          Kind::real,   5.0f, 2000.0f, 120.0f,  true, 120.0f,  nullptr, 0 },
        { "refreshRate",     "Refresh Every",         Kind::choice, 0.0f,  20.0f,   12.0f,  false, 0.0f,   kRateChoices.data(), core::kNumRates },
        { "refreshBeatLock", "Refresh Lock to Beat",  Kind::toggle, 0.0f,   1.0f,    1.0f,  false, 0.0f,   nullptr, 0 },
        { "refreshSkip",     "Refresh Skip",          Kind::real,   0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "regionStart",     "Region Start",          Kind::real,   0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "regionEnd",       "Region End",            Kind::real,   0.0f,   1.0f,    1.0f,  false, 0.0f,   nullptr, 0 },
        { "atEnd",           "At the End",            Kind::choice, 0.0f,   2.0f,    0.0f,  false, 0.0f,   kAtEndChoices, 3 },
        { "loopXfade",       "Loop Crossfade",        Kind::real,   0.0f, 500.0f,   10.0f,  true,  50.0f,  nullptr, 0 },
        { "glideMode",       "Auto Slide",            Kind::choice, 0.0f,   2.0f,    1.0f,  false, 0.0f,   kGlideModeChoices, 3 },
        { "glideTime",       "Slide Time",            Kind::real,   0.0f,  16.0f,    0.5f,  true,   0.5f,  nullptr, 0 },
        { "vibRate",         "Vibrato Speed",         Kind::real,   0.1f,  20.0f,    5.5f,  true,   5.5f,  nullptr, 0 },
        { "vibSync",         "Vibrato Sync",          Kind::choice, 0.0f,  21.0f,    0.0f,  false, 0.0f,   kVibSyncChoices.data(), core::kNumRates + 1 },
        { "bendRange",       "Bend Range",            Kind::whole,  0.0f,  48.0f,    2.0f,  false, 0.0f,   nullptr, 0 },
        { "velSens",         "Velocity Sensitivity",  Kind::real,   0.0f,   1.0f,    0.7f,  false, 0.0f,   nullptr, 0 },
        { "polyphony",       "Voices",                Kind::whole,  1.0f,  32.0f,   16.0f,  false, 0.0f,   nullptr, 0 },
        { "liveLegato",      "Live Legato",           Kind::toggle, 0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
        { "rootNote",        "Root Note",             Kind::whole,  0.0f, 127.0f,   60.0f,  false, 0.0f,   nullptr, 0 },
        { "rootCents",       "Root Cents",            Kind::real, -100.0f, 100.0f,   0.0f,  false, 0.0f,   nullptr, 0 },
        { "bypass",          "Bypass",                Kind::toggle, 0.0f,   1.0f,    0.0f,  false, 0.0f,   nullptr, 0 },
    };

    constexpr const ParamSpec& spec (P p) noexcept { return kParams[index (p)]; }

    /** The index of a parameter id, or -1 for an id this version does not have. */
    int findParam (std::string_view id) noexcept;
}
