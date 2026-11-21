#pragma once

// The chunks the processor reads and writes (PLUGIN-PLAN 3): PARM, INST, SMPL (a starter or a file reference)
// and UIST here; CLIP is model::ClipCodec's. Every payload is an append-only record: a reader takes the fields
// it knows, gives fields missing at the end their defaults, and keeps whatever follows (a newer version's
// fields) as trailing bytes, which the encoder writes back after its own fields.

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
    // SMPL: u8 kind (0 starter, 1 file).
    //   Starter: str8 id, i32 seed.
    //   File:    str16 path, u64 fileSize, 32-byte SHA-256 of the original bytes, f64 fileSampleRate, u32 channels,
    //            u64 fileFrames, u64 loadedStartFrame, u64 loadedFrames, str16 displayName, u8 keepInProject,
    //            u8 embedAnswer, then the analysis cache: u8 analysed, u8 hasPitch, f64 midi, f64 cents,
    //            u8 hasKey, u8 key, str8 scale, u8 partCount, parts (u8 kind, f64 start, f64 end, f64 bpm,
    //            u8 tempoFound, str8 label).
    // Then (append-only) trailing bytes. M2 writes the file record with an empty analysis cache (M7 fills it);
    // a cache it reads is kept and written back as it was.

    struct AnalysisPart
    {
        std::uint8_t kind = 0;
        double start = 0.0, end = 0.0, bpm = 0.0;
        std::uint8_t tempoFound = 0;
        std::string label;
    };

    struct AnalysisCache
    {
        std::uint8_t analysed = 0, hasPitch = 0;
        double midi = 0.0, cents = 0.0;
        std::uint8_t hasKey = 0, key = 0;
        std::string scale;
        std::vector<AnalysisPart> parts;
    };

    /** SMPL kind 1: a sound file, found again by its path (PLUGIN-PLAN 3, 7.4 decision 4). */
    struct SampleFile
    {
        std::string path;                        // UTF-8, the full path
        std::uint64_t fileSize = 0;
        std::array<std::uint8_t, 32> sha256 {};  // of the original file's bytes
        double fileSampleRate = 0.0;
        std::uint32_t channels = 0;
        std::uint64_t fileFrames = 0;
        std::uint64_t loadedStartFrame = 0;      // the part loaded (0 and fileFrames for a whole file)
        std::uint64_t loadedFrames = 0;
        std::string displayName;
        std::uint8_t keepInProject = 1;          // "Keep the sound in the project" (M7): on by default
        std::uint8_t embedAnswer = 0;            // 0 not asked, 1 yes, 2 no, 3 just the Region (M7)
        AnalysisCache cache;
    };

    inline constexpr std::uint8_t kSampleStarter = 0;
    inline constexpr std::uint8_t kSampleFile = 1;

    struct SampleRef
    {
        std::uint8_t kind = kSampleStarter;
        std::string starterId = "sawopen";   // the default sound (D9)
        std::int32_t seed = 1;
        SampleFile file;                     // kind 1
        Bytes trailing;
    };

    /** True for a starter or a file reference (out filled); false (out untouched) for a kind this version does
        not know or damaged data. */
    bool decodeSmpl (const std::uint8_t* payload, std::size_t size, SampleRef& out);
    Bytes encodeSmpl (const SampleRef& s);

    /** The same sound: the same starter and seed, or the same file (path, size, SHA-256 and loaded part). */
    bool sameSound (const SampleRef& a, const SampleRef& b) noexcept;

    /** The same file's bytes (M7 V3): both file records, the same size and SHA-256, wherever each one is. */
    bool sameBytes (const SampleRef& a, const SampleRef& b) noexcept;

    //==============================================================================================
    // SAUD (M7, optional): the sound kept in the project. u8 encoding (0 the original file's bytes, 1 a FLAC 24-bit
    // excerpt, 2 int16 PCM interleaved), u32 channels, f64 sampleRate, u64 excerptStartFrame, u64 excerptFrames (in the
    // original file's frames; the loaded range for encodings 0 and 2), u64 byteLength, bytes; (append-only). The
    // payload is kept whole as it was read (a newer version's trailing fields with it), so a chunk is written back
    // exactly while its sound is unchanged. An encoding this version does not know reads too (known () is false): it is
    // kept, never played.

    inline constexpr std::uint8_t kSaudOriginal = 0;
    inline constexpr std::uint8_t kSaudFlac24 = 1;
    inline constexpr std::uint8_t kSaudPcm16 = 2;
    inline constexpr std::size_t kSaudHeaderBytes = 1 + 4 + 8 + 8 + 8 + 8;   // 37: every field before the bytes

    struct SaudHeader
    {
        std::uint8_t encoding = kSaudOriginal;
        std::uint32_t channels = 0;
        double sampleRate = 0.0;
        std::uint64_t excerptStartFrame = 0;   // frames of the original file
        std::uint64_t excerptFrames = 0;
        std::uint64_t byteLength = 0;
    };

    /** A SAUD payload read in place: its header, and where its bytes are (inside the payload). */
    struct SaudView
    {
        SaudHeader header;
        const std::uint8_t* bytes = nullptr;
        std::size_t trailingBytes = 0;   // after the bytes: a newer version's fields (kept with the payload)

        bool known() const noexcept { return header.encoding <= kSaudPcm16; }
    };

    /** False (out untouched) for a payload cut short: shorter than its header, or than its byteLength says. */
    bool decodeSaud (const std::uint8_t* payload, std::size_t size, SaudView& out) noexcept;

    /** The 37 header bytes, appended to out (a payload made in place: the header, then the bytes). */
    void writeSaudHeader (Bytes& out, const SaudHeader& h);

    /** Writes the header over the first 37 bytes of payload (which must have them), with byteLength as given. */
    void patchSaudHeader (Bytes& payload, const SaudHeader& h);

    /** A whole payload: the header (byteLength = size), then the bytes. */
    Bytes encodeSaud (SaudHeader h, const void* bytes, std::size_t size);

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
