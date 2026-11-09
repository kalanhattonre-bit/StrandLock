#pragma once

// Parameter readouts and text entry (PLUGIN-PLAN 2.1). The readouts reproduce the prototype's text
// character for character: SLSampleUI's macroText, offsetText, fmtX, fmtPct, fmtSt, fmtCycles, fmtMs,
// fmtHeads and signed, and its parseNum / parseMs / parsePct (checked against ui/readouts.json), plus
// SLPanels' parsePitch (ui/inspector.json). Text entry reads what the readout prints, so for every
// parameter text(valueFromText(text(v))) == text(v). Message thread only (these allocate).

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

namespace sl::params
{
    namespace text
    {
        // SLSampleUI's private helpers, ported exactly.
        std::string trimZ (std::string s);
        std::string signedText (double x, int digits);   // SLSampleUI's signed (x, d)
        std::string fmtX (double v);
        std::string fmtPct (double v);
        std::string fmtSt (double v);
        std::string fmtCycles (double v);
        std::string fmtMs (double v);
        std::string fmtHeads (double v);

        /** SLSampleUI.macroText (i, m): what macro i (0 Speed .. 7 Formant) at m does. */
        std::string macroText (int i, double m);

        enum class Offset { amp, vib, tone, formant, hollow };

        /** SLSampleUI.offsetText (k, o): what a whole-clip offset o (-1..1) does. */
        std::string offsetText (Offset k, double o);

        /** SLSampleUI.parseNum: a leading fraction ("1/8", "1 / 4"), else the first number anywhere in the
            text ("+2.5x" -> 2.5, "1,5" -> 1.5); nothing when there is none (or a fraction over 0). */
        std::optional<double> parseNum (std::string_view t);

        /** SLSampleUI.parseMs: parseNum, times 1000 when the number is followed by "s" and the text has no "ms". */
        std::optional<double> parseMs (std::string_view t);

        /** SLSampleUI.parsePct: parseNum / 100. */
        std::optional<double> parsePct (std::string_view t);

        /** SLPanels.parsePitch: "C3" -> 60, "Db3" -> 61, "60" -> 60, "♯"/"♭" accepted; nothing outside 0..127. */
        std::optional<int> parsePitch (std::string_view t);
    }

    /** What the readouts need to know about the plugin: the loaded sound's length, for the Region's time. */
    struct TextContext
    {
        std::atomic<double> soundSeconds { 0.0 };   // 0 = no sound loaded (the Region shows its percent only)
    };

    /** The readout of parameter index at its plain value. */
    std::string paramText (int index, double plain, const TextContext* ctx);

    /** The plain value a typed text means, or nothing when it means none. Not clamped to the range. */
    std::optional<double> paramValueFromText (int index, std::string_view text, const TextContext* ctx);

    /** Half the width of the readout's printed step at plain value v: a value read back from the readout is
        within this of v. Infinity for the readouts that are not one-to-one (macros and offsets, which round
        coarsely or saturate); 0 for choices, switches and whole numbers. Used by the parameter test. */
    double readoutHalfStep (int index, double plain);
}
