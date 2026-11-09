#pragma once

// Note names and scales (SLModel NAMES, SCALES, noteName, inScale, snapScale). Cubase naming: MIDI 60 = C3.

#include <string>
#include <string_view>

namespace sl::core
{
    inline constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    struct ScaleInfo
    {
        const char* name;   // as saved: "Harm. minor"
        int numSteps;
        int steps[12];      // semitones above the key; the first numSteps are used
    };

    inline constexpr int kNumScales = 8;

    /** SLModel.SCALES in its order. The list can grow (it is plugin state, not a parameter: PLUGIN-PLAN 2.2). */
    inline constexpr ScaleInfo kScales[kNumScales] = {
        { "Chromatic",   12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Major",        7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",        7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dorian",       7, { 0, 2, 3, 5, 7, 9, 10 } },
        { "Mixolydian",   7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "Harm. minor",  7, { 0, 2, 3, 5, 7, 8, 11 } },
        { "Pent. major",  5, { 0, 2, 4, 7, 9 } },
        { "Pent. minor",  5, { 0, 3, 5, 7, 10 } },
    };

    /** The index of a scale name, or -1 for a name this version does not know. */
    int findScale (std::string_view name) noexcept;

    /** SLModel.noteName: the pitch rounded with Math.round, then name + octave, 60 -> "C3", 61 -> "C#3",
        0 -> "C-2". Empty for a non-finite pitch. */
    std::string noteName (double p);

    /** SLModel.inScale: whether Math.round (p) is in the scale on key (0 = C .. 11 = B). scale is an
        index into kScales; anything else reads as Chromatic, as SLModel does for an unknown name. */
    bool inScale (double p, int key, int scale) noexcept;

    /** SLModel.snapScale: p itself when it is in the scale, else the nearest pitch that is (the lower one
        first at equal distance), searching 1..6 semitones away; p when none is. */
    double snapScale (double p, int key, int scale) noexcept;
}
