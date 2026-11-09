#pragma once

// The one rate type (BLUEPRINT 1.5, decision A1): Jump every, Refresh every, Vibrato Sync and every
// generator use it. Rates are quarter-note beats: "1bar" is 4 beats whatever the clip's beatsPerBar.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sl::core
{
    /** The 21 rates in SLModel.RATES order. The order is the parameters' choice order: never reorder. */
    enum class RateId : std::uint8_t
    {
        r1bar,  r1barT,  r1barD,
        r1_2,   r1_2T,   r1_2D,
        r1_4,   r1_4T,   r1_4D,
        r1_8,   r1_8T,   r1_8D,
        r1_16,  r1_16T,  r1_16D,
        r1_32,  r1_32T,  r1_32D,
        r1_64,  r1_64T,  r1_64D
    };

    inline constexpr int kNumRates = 21;

    struct RateInfo
    {
        RateId id;
        const char* code;      // the prototype's id, as saved: "1/16T"
        double beats;          // quarter-note beats: 1bar = 4, T = x 2/3, D = x 1.5
        const char* label;     // rateLabel: "1/16 triplet", "1 bar dotted"
    };

    // beats * (2 / 3) and beats * 1.5, computed as SLModel computes them (1barT is 2.6666666666666665).
    inline constexpr RateInfo kRates[kNumRates] = {
        { RateId::r1bar,  "1bar",  4.0,                   "1 bar" },
        { RateId::r1barT, "1barT", 4.0 * (2.0 / 3.0),     "1 bar triplet" },
        { RateId::r1barD, "1barD", 4.0 * 1.5,             "1 bar dotted" },
        { RateId::r1_2,   "1/2",   2.0,                   "1/2" },
        { RateId::r1_2T,  "1/2T",  2.0 * (2.0 / 3.0),     "1/2 triplet" },
        { RateId::r1_2D,  "1/2D",  2.0 * 1.5,             "1/2 dotted" },
        { RateId::r1_4,   "1/4",   1.0,                   "1/4" },
        { RateId::r1_4T,  "1/4T",  1.0 * (2.0 / 3.0),     "1/4 triplet" },
        { RateId::r1_4D,  "1/4D",  1.0 * 1.5,             "1/4 dotted" },
        { RateId::r1_8,   "1/8",   0.5,                   "1/8" },
        { RateId::r1_8T,  "1/8T",  0.5 * (2.0 / 3.0),     "1/8 triplet" },
        { RateId::r1_8D,  "1/8D",  0.5 * 1.5,             "1/8 dotted" },
        { RateId::r1_16,  "1/16",  0.25,                  "1/16" },
        { RateId::r1_16T, "1/16T", 0.25 * (2.0 / 3.0),    "1/16 triplet" },
        { RateId::r1_16D, "1/16D", 0.25 * 1.5,            "1/16 dotted" },
        { RateId::r1_32,  "1/32",  0.125,                 "1/32" },
        { RateId::r1_32T, "1/32T", 0.125 * (2.0 / 3.0),   "1/32 triplet" },
        { RateId::r1_32D, "1/32D", 0.125 * 1.5,           "1/32 dotted" },
        { RateId::r1_64,  "1/64",  0.0625,                "1/64" },
        { RateId::r1_64T, "1/64T", 0.0625 * (2.0 / 3.0),  "1/64 triplet" },
        { RateId::r1_64D, "1/64D", 0.0625 * 1.5,          "1/64 dotted" },
    };

    /** What an unknown rate id plays (SLModel.rateBeats and the engine's copy): 1/16. */
    inline constexpr double kUnknownRateBeats = 0.25;

    /** True for the 21 ids; false for anything else (a value cast from a bad integer). */
    constexpr bool isValidRate (RateId id) noexcept { return static_cast<int> (id) < kNumRates; }

    /** Beats per step of a rate; an invalid id plays as an unknown one, 0.25. */
    double rateBeats (RateId id) noexcept;

    /** rateBeats for a saved id string: one of the 21 ids, else 0.25 ("1/3", "", "1/16t"). */
    double rateBeats (std::string_view code) noexcept;

    /** rateBeats for a number in place of an id: a positive finite number passes through as beats, anything else
        is 0.25. */
    double rateBeatsOr (double beats) noexcept;

    /** The id for a saved string, or nothing when it is not one of the 21 (case matters: "1/16t" is not one). */
    std::optional<RateId> parseRate (std::string_view code) noexcept;

    /** "1/16T"; "" for an invalid id. */
    const char* rateCode (RateId id) noexcept;

    /** "1/16 triplet"; "" for an invalid id. */
    const char* rateLabel (RateId id) noexcept;

    /** SLModel.rateLabel for a saved string: the label of one of the 21 ids, else the string itself. */
    std::string rateLabel (std::string_view code);

    /** The rate whose beats are nearest beats in ratio (|rateBeats / beats - 1|, first in list order on a tie),
        if that error is at most tol; nothing otherwise (also for 0, negative or NaN beats). */
    std::optional<RateId> rateFromBeats (double beats, double tol = 0.01) noexcept;
}
