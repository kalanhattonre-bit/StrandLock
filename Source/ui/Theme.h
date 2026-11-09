#pragma once

// Every colour StrandLock draws comes from here (PLUGIN-PLAN 4 M0). The two palettes are the 77 CSS tokens of
// the prototype's shell.html: the bare :root block is the dark theme and :root[data-theme="light"] the light
// one. The values are kept as the CSS text, so a test can compare them with Tests/fixtures/ui/theme.json
// string for string, and read once into colours.
//
// Each editor has one Theme object. Its components hold a reference to it and ask it for a token's colour
// while they paint, so switching the theme is one call and a repaint.

#include <juce_gui_basics/juce_gui_basics.h>

#include "state/UserSettings.h"

#include <array>
#include <cstddef>

namespace sl::ui
{
    using ThemeId = state::Theme;

    /** The tokens in shell.html's order (the CSS name with its dashes turned into camel case). */
    enum class Tok : int
    {
        bg, panel, panel2, line, txt, dim, acc, onacc, warn, btn, btnb, roll, hdr, menu, hover, shadow, toastBg,
        toastTxt, row, rowBlack, rowOut, rowRoot, rowLine, octLine, gridBar, gridBeat, gridSub, noteLine,
        noteLineSel, startRing, dotFill, pianoWhite, pianoBlack, pianoText, pianoOut, pianoHeld, rulerText,
        rulerTick, link, handleRing, playhead, boxFill, boxLine, nodeSel, ghost, lanePitch, laneAmp, laneVib,
        laneTone, laneFormant, laneHollow, laneSample, onLane, aAct, aActSel, aIdle, aIdleSel, stripBg, wave,
        stripShade, regionEdge, regionBand, head, headTxt, grain, stripTxt, stripTxtB, startMark, meterBg,
        meterOk, meterHot, tipBg, tipTxt, dropBg, kbdBg, chipWarnBg, chipWarnTxt
    };

    inline constexpr int kNumTokens = 77;
    static_assert (static_cast<int> (Tok::chipWarnTxt) == kNumTokens - 1, "one Tok per shell.html token");

    /** One token as shell.html writes it: its CSS name (without the leading --), the dark and the light value. */
    struct TokenText
    {
        const char* name;
        const char* dark;
        const char* light;
    };

    /** All 77, in shell.html's order. */
    extern const std::array<TokenText, kNumTokens> kTokens;

    /** A CSS value read: "#rrggbb", "rgba(r,g,b,a)", or a plain number (the four a-* opacities). */
    struct CssValue
    {
        bool ok = false;
        bool isNumber = false;
        juce::Colour colour;
        double number = 0.0;
    };

    CssValue parseCss (const char* text);

    /** The WCAG relative luminance of an sRGB colour (0 for black, 1 for white); alpha is ignored. */
    double relativeLuminance (juce::Colour c) noexcept;

    /** The WCAG contrast ratio of two colours (1 to 21); alpha is ignored. */
    double contrastRatio (juce::Colour a, juce::Colour b) noexcept;

    /** One theme's 77 tokens, read. A number token has a transparent colour; a colour token has number 0. */
    struct Palette
    {
        ThemeId id = ThemeId::dark;
        std::array<juce::Colour, kNumTokens> colours {};
        std::array<float, kNumTokens> numbers {};

        juce::Colour operator[] (Tok t) const noexcept { return colours[static_cast<std::size_t> (t)]; }
        float number (Tok t) const noexcept { return numbers[static_cast<std::size_t> (t)]; }
    };

    /** The palette of a theme, read from kTokens the first time it is asked for. */
    const Palette& palette (ThemeId id);

    /** The theme one editor draws with. */
    class Theme
    {
    public:
        explicit Theme (ThemeId initial = ThemeId::dark) : current (&palette (initial)) {}

        ThemeId id() const noexcept { return current->id; }
        bool isDark() const noexcept { return current->id == ThemeId::dark; }
        void set (ThemeId t) { current = &palette (t); }

        juce::Colour operator() (Tok t) const noexcept { return (*current)[t]; }
        float number (Tok t) const noexcept { return current->number (t); }
        const Palette& colours() const noexcept { return *current; }

        static juce::String fontName();

        /** A font by JUCE height (ascent plus descent), as GrainLock sizes its text. tracking is extra letter
            spacing as a proportion of the height. */
        static juce::Font font (float height, bool bold = false, float tracking = 0.0f);

        /** A font sized as the prototype's CSS: px is the font-size (the em), so 12 here matches font-size:12px. */
        static juce::Font px (float cssPixels, bool bold = false, float tracking = 0.0f);

        /** The width of text in a font, in logical pixels. */
        static float textWidth (const juce::Font& f, const juce::String& text);

        // The layout is drawn at 1100 x 680 logical pixels. The window keeps between 900 x 560 and 2400 x 1600
        // logical pixels, and the UI Scale (75 to 200 %) multiplies all of it.
        static constexpr int baseWidth = 1100;
        static constexpr int baseHeight = 680;
        static constexpr int minWidth = 900;
        static constexpr int minHeight = 560;
        static constexpr int maxWidth = 2400;
        static constexpr int maxHeight = 1600;
        static constexpr int minScalePercent = 75;
        static constexpr int maxScalePercent = 200;

        static constexpr float corner = 7.0f;         // buttons and boxes (shell.html: border-radius 7px)
        static constexpr float frameCorner = 10.0f;   // the window's card (top bar and hint bar corners)

        static inline const juce::Colour darkAccent { 0xffb58cff };
        static inline const juce::Colour lightAccent { 0xff7b4fe0 };

    private:
        const Palette* current;
    };
}
