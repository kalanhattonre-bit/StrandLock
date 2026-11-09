#pragma once

// The lane registry (BLUEPRINT 1.2, SLModel.LANES). The registry order is part of the seed contract:
// generators hash the lane index. New lanes are appended, never inserted (PLUGIN-PLAN 1.2).

#include <cstdint>
#include <optional>
#include <string_view>

namespace sl::core
{
    enum class LaneId : std::uint8_t { pitch, amp, vib, tone, formant, hollow, travel, heads, grain, refresh };

    inline constexpr int kNumLanes = 10;

    struct LaneInfo
    {
        LaneId id;
        const char* code;            // the prototype's lane id, as saved in clips: "pitch"
        char key;                    // the key that selects its mode: '2' .. '8'
        int mode;                    // 2 .. 8 (the four Sample sub-lanes share mode 8)
        const char* name;            // "Pitch", "Volume", ...
        double def;                  // default value, 0..1
        const char* group;           // "pitch", "level", "tone" or "sample"
        std::uint32_t colour;        // 0xRRGGBB
        std::uint32_t colourDark;    // the line colour in the dark theme (= colour unless the lane has its own)
        std::uint32_t colourLight;   // the line colour in the light theme (= colour unless the lane has its own)
        bool sub;                    // one of mode 8's sub-lanes
    };

    inline constexpr std::uint32_t kSand = 0xE2C275;

    inline constexpr LaneInfo kLanes[kNumLanes] = {
        { LaneId::pitch,   "pitch",   '2', 2, "Pitch",   0.5,  "pitch",  0x2ECF98, 0x3EE6A8, 0x0B9E74, false },
        { LaneId::amp,     "amp",     '3', 3, "Volume",  0.8,  "level",  0x4F8BFF, 0x4F8BFF, 0x4F8BFF, false },
        { LaneId::vib,     "vib",     '4', 4, "Vibrato", 0.0,  "pitch",  0xE05CFF, 0xE05CFF, 0xE05CFF, false },
        { LaneId::tone,    "tone",    '5', 5, "Tone",    1.0,  "tone",   0xFF9F1C, 0xFF9F1C, 0xFF9F1C, false },
        { LaneId::formant, "formant", '6', 6, "Formant", 0.5,  "tone",   0xFF4D8D, 0xFF4D8D, 0xFF4D8D, false },
        { LaneId::hollow,  "hollow",  '7', 7, "Hollow",  0.0,  "tone",   0x22C3E6, 0x22C3E6, 0x22C3E6, false },
        { LaneId::travel,  "travel",  '8', 8, "Travel",  0.75, "sample", kSand,    kSand,    kSand,    true },
        { LaneId::heads,   "heads",   '8', 8, "Heads",   0.5,  "sample", kSand,    kSand,    kSand,    true },
        { LaneId::grain,   "grain",   '8', 8, "Grain",   0.5,  "sample", kSand,    kSand,    kSand,    true },
        { LaneId::refresh, "refresh", '8', 8, "Refresh", 0.5,  "sample", kSand,    kSand,    kSand,    true },
    };

    /** Mode 8's sub-lanes, in chip order (SLModel.SAMPLE_LANES). */
    inline constexpr LaneId kSampleLanes[4] = { LaneId::travel, LaneId::heads, LaneId::grain, LaneId::refresh };

    struct ModeInfo
    {
        int mode;           // 1 .. 8
        char key;           // '1' .. '8'
        const char* name;   // "Build", "Pitch", ... "Sample"
        int numLanes;       // 0 for Build, 4 for Sample, else 1
        LaneId lanes[4];    // the first numLanes are used
    };

    /** SLModel.MODES: 1 Build, 2..7 one lane each, 8 Sample with its four sub-lanes. */
    inline constexpr ModeInfo kModes[8] = {
        { 1, '1', "Build",   0, { LaneId::pitch,   LaneId::pitch, LaneId::pitch, LaneId::pitch } },
        { 2, '2', "Pitch",   1, { LaneId::pitch,   LaneId::pitch, LaneId::pitch, LaneId::pitch } },
        { 3, '3', "Volume",  1, { LaneId::amp,     LaneId::pitch, LaneId::pitch, LaneId::pitch } },
        { 4, '4', "Vibrato", 1, { LaneId::vib,     LaneId::pitch, LaneId::pitch, LaneId::pitch } },
        { 5, '5', "Tone",    1, { LaneId::tone,    LaneId::pitch, LaneId::pitch, LaneId::pitch } },
        { 6, '6', "Formant", 1, { LaneId::formant, LaneId::pitch, LaneId::pitch, LaneId::pitch } },
        { 7, '7', "Hollow",  1, { LaneId::hollow,  LaneId::pitch, LaneId::pitch, LaneId::pitch } },
        { 8, '8', "Sample",  4, { LaneId::travel,  LaneId::heads, LaneId::grain, LaneId::refresh } },
    };

    /** The lane's index in the registry (the seed contract's lane index). */
    constexpr int laneIndex (LaneId id) noexcept { return static_cast<int> (id); }

    /** The index of a saved lane id string, or -1 when it is not one of today's lanes. */
    int laneIndex (std::string_view code) noexcept;

    /** The lane for a saved id string, or nothing for an id this version does not know. */
    std::optional<LaneId> findLane (std::string_view code) noexcept;

    constexpr const LaneInfo& laneInfo (LaneId id) noexcept { return kLanes[laneIndex (id)]; }

    constexpr double laneDef (LaneId id) noexcept { return kLanes[laneIndex (id)].def; }

    /** SLModel.laneDef: the default of a known lane, 0.5 for an unknown id. */
    double laneDef (std::string_view code) noexcept;
}
