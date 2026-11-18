#pragma once

// The inspector's pure helpers (sl-ui-panels.js SLPanels: fmtPos, parsePos, parseBeats, parsePitch, rateId, fmtNum,
// laneText, laneValueFromText; PLUGIN-PLAN 1.2 "its pure helpers port exactly", M4). They turn what Kalan types into
// positions, lengths, pitches and lane values, and lane values into the readout text. Tested against
// Tests/fixtures/ui/inspector.json. No JUCE: plain strings (UTF-8) in and out.
//
// The prototype parses with regular expressions; the C++ matches the same patterns by hand, with the same
// leftmost-first, greedy choices (a std::regex would read the bytes of a non-ASCII character one by one).

#include "core/Lanes.h"
#include "model/LaneFormat.h"

#include <optional>
#include <string>
#include <string_view>

namespace sl::ui::roll
{
    /** Ticks per beat and per sixteenth in Cubase-style positions (480 and 120). */
    inline constexpr int kTicksPerBeat = 480;
    inline constexpr int kTicksPerSixteenth = 120;

    /** SLPanels.fmtPos: beats from the clip start -> "bar.beat.sixteenth" (all from 1) plus ".tick" when the
        ticks are not 0 ("1.1.1", "2.1.2", "1.1.1.60"). A negative or non-finite position reads as 0. */
    std::string fmtPos (double beats);

    /** SLPanels.parsePos: "2.3.1" -> beats from the clip start (bar 1 = 0). Missing parts count as 1 (ticks 0);
        dots, colons and spaces separate. Nothing when it is not a position. */
    std::optional<double> parsePos (std::string_view text);

    /** SLPanels.rateId: "1/16t", "1/8 dotted", "1 bar" -> a rate id ("1/16T", "1/8D", "1bar"), or nothing. */
    std::optional<std::string> rateId (std::string_view text);

    /** SLPanels.parseBeats: beats from "0.5", "1.5 beats", a note value ("1/8" = 0.5 beat, "3/4" = 3 beats), a rate
        ("1/16T", "1 bar") or a Cubase length "bars.beats.sixteenths(.ticks)" counted from 0. Nothing when it is not
        a length. */
    std::optional<double> parseBeats (std::string_view text);

    /** SLPanels.parsePitch: "C3" = 60 (Cubase), "F#2", "Eb4", "bb1", the sharp and flat signs, or a MIDI number ->
        0..127, or nothing. */
    std::optional<double> parsePitch (std::string_view text);

    /** SLPanels.fmtNum: x to `digits` places (3 when digits < 0) with the trailing zeros dropped (String (+x.toFixed
        (d))); "" for a value that is not finite. */
    std::string fmtNum (double x, int digits = -1);

    /** The inspector's Velocity box (sl-ui-panels.js commitVel): parseFloat (clean (text)), rounded and kept to 1 ..
        127; nothing when the text does not start with a finite number ("102", "90.6" = 91, "300" = 127, "-5" = 1). */
    std::optional<int> parseVelocity (std::string_view text);

    /** The prototype's plainUnits: "st" becomes "semitones" ("1 semitone"), "semitones vibrato" becomes
        "semitones of vibrato", and "1 cycles" / "1 wavelengths" become singular. */
    std::string plainUnits (std::string text);

    /** SLPanels.laneText: SLModel.formatLane in the pickers' own words ("1/8x forwards", not "0.13x"; "Refresh 1/8
        triplet (...)", not "1/8T"; "beats (", not "beat ("), in plain units. */
    std::string laneText (core::LaneId lane, double v, const model::LaneCtx& ctx);

    /** sl-ui.js fmtLane, the roll's live readout: laneText, except that a Refresh line while the Sample tab's Refresh
        is Smooth says what to do instead. */
    std::string laneReadout (core::LaneId lane, double v, const model::LaneCtx& ctx);

    /** What laneValueFromText gives: the lane value (0..1) and laneText of it, or a sentence for the toast. */
    struct LaneValue
    {
        bool ok = false;
        double v = 0.0;
        std::string text;    // laneText (lane, v, ctx) when ok, else the error sentence
    };

    /** SLPanels.laneValueFromText: a typed real value -> the lane value, the inverse of SLModel.mapLane ("+3",
        "-6 dB", "2.4 kHz", "0.5x backwards", "stopped", "25%", "3 heads", "250 ms", "1/16", "Smooth"). The
        instrument in ctx is normalized first. */
    LaneValue laneValueFromText (core::LaneId lane, std::string_view text, const model::LaneCtx& ctx);
}
