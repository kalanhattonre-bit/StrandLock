#pragma once

// The chunks M0 reads and writes (PLUGIN-PLAN 3): PARM, INST, SMPL (a starter reference) and UIST. CLIP is
// kept as opaque bytes until M2 wires the clip codec in. Every payload is an append-only record: a reader
// takes the fields it knows, gives fields missing at the end their defaults, and keeps whatever follows
// (a newer version's fields) as trailing bytes, which the encoder writes back after its own fields.

#include "StateFormat.h"
#include "params/ParamIds.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sl::state
{
    //==============================================================================================
    // PARM: u16 count, then per parameter: str8 id (ASCII), f32 plain value. By id, not index; bypass excluded.

    struct ParmEntry
    {
        std::string id;
        float value = 0.0f;
    };

    struct ParmChunk
    {
        std::vector<ParmEntry> entries;   // as stored, in order
        Bytes trailing;
    };

    bool decodeParm (const std::uint8_t* payload, std::size_t size, ParmChunk& out);

    /** values[i] is parameter i's plain value; bypass (49) is not written. extras (entries whose ids this
        version does not have) follow the 49, then trailing. */
    Bytes encodeParm (const std::array<float, params::kNumParams>& values, const std::vector<ParmEntry>& extras, const Bytes& trailing);

    //==============================================================================================
    // INST: i32 seed; str8 mod1Target, u8 mod1Cc, str8 mod2Target, u8 mod2Cc; u8 key, str8 scale, u8 scaleLock;
    // f64 regionStartExact, f64 regionEndExact (NaN = none); str8 liveTemplateNoteId; (append-only).
    // Strings are kept verbatim (a lane id or scale this version does not know survives a re-save); what the
    // audio thread plays from them is ParamBridge's NonParamAtomics.

    struct InstState
    {
        std::int32_t seed = 1;
        std::string mod1Target = "vib";
        std::uint8_t mod1Cc = 1;
        std::string mod2Target = "tone";
        std::uint8_t mod2Cc = 2;
        std::uint8_t key = 0;
        std::string scale = "Chromatic";
        std::uint8_t scaleLock = 0;
        double regionStartExact = std::numeric_limits<double>::quiet_NaN();
        double regionEndExact = std::numeric_limits<double>::quiet_NaN();
        std::string liveTemplate;
        Bytes trailing;
    };

    bool decodeInst (const std::uint8_t* payload, std::size_t size, InstState& out);
    Bytes encodeInst (const InstState& s);

    //==============================================================================================
    // SMPL: u8 kind (0 starter, 1 file). Starter: str8 id, i32 seed. M0 reads starters; a file reference (M7)
    // is not read yet, so that chunk is kept as it is and the built-in sound plays.

    struct SampleRef
    {
        std::uint8_t kind = 0;
        std::string starterId = "saw";   // M0 plays the starter Saw (the probe's sound); M2 makes sawopen the default
        std::int32_t seed = 1;
        Bytes trailing;
    };

    /** True for a starter reference; false (out untouched) for a file reference or damaged data. */
    bool decodeSmpl (const std::uint8_t* payload, std::size_t size, SampleRef& out);
    Bytes encodeSmpl (const SampleRef& s);

    //==============================================================================================
    // UIST: u8 theme (0 dark, 1 light), u16 uiScalePercent, u16 logicalWidth, u16 logicalHeight, u8 mode (1..8),
    // str8 subLane, str8 tab, u8 folded, f32 zoomH, f32 zoomV, f64 scrollBeat, f32 scrollRow, f32 gridBeats
    // (0 = free), u8 scope (0 each, 1 across), u16 copyMask, f64 stripViewA, f64 stripViewB; (append-only).

    struct UiState
    {
        std::uint8_t theme = 0;
        std::uint16_t uiScalePercent = 100;
        std::uint16_t logicalWidth = 1100;
        std::uint16_t logicalHeight = 680;
        std::uint8_t mode = 1;
        std::string subLane = "travel";
        std::string tab = "sample";
        std::uint8_t folded = 0;
        float zoomH = 1.0f;
        float zoomV = 1.0f;
        double scrollBeat = 0.0;
        float scrollRow = 0.0f;
        float gridBeats = 0.25f;
        std::uint8_t scope = 0;
        std::uint16_t copyMask = 0x3ff;   // all 10 lanes
        double stripViewA = 0.0;
        double stripViewB = 1.0;
        Bytes trailing;
    };

    inline constexpr int kMinUiScalePercent = 75;
    inline constexpr int kMaxUiScalePercent = 200;

    /** Decodes and makes every field usable: theme 0 or 1, UI scale 75..200 %, the window inside 900 x 560 ..
        2400 x 1600, mode 1..8, finite zooms and positions. Strings are kept as they were. */
    bool decodeUist (const std::uint8_t* payload, std::size_t size, UiState& out);
    Bytes encodeUist (const UiState& s);
}
