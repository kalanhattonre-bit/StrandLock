#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace strandlock::ui
{
    /** Every colour and font the interface uses. Change the look here and nowhere else.

        The surfaces and text are the Lock suite's dark theme, exactly as GrainLock, CycleLock and
        PulseLock have them; only the accent is StrandLock's own (orchid). A later milestone turns
        this into two palettes (dark and light) with a runtime switch, still all in this file. */
    struct Theme
    {
        // Surfaces, darkest to lightest.
        static inline const juce::Colour background   { 0xff151618 };
        static inline const juce::Colour display      { 0xff0f1012 };
        static inline const juce::Colour panel        { 0xff1d1e21 };
        static inline const juce::Colour panelRaised  { 0xff27282c };
        static inline const juce::Colour outline      { 0xff33353a };
        static inline const juce::Colour track        { 0xff3a3c42 };

        // Text.
        static inline const juce::Colour text         { 0xffece8e1 };
        static inline const juce::Colour textDim      { 0xff9a9ea6 };
        static inline const juce::Colour textFaint    { 0xff5d6169 };

        // The one accent colour, and the dark ink drawn on top of it.
        static inline const juce::Colour accent       { 0xffb58cff };
        static inline const juce::Colour onAccent     { 0xff170a2a };

        static juce::Colour accentAlpha (float alpha) { return accent.withAlpha (alpha); }

        static juce::String fontName()
        {
           #if JUCE_WINDOWS
            return "Segoe UI";
           #else
            return juce::Font::getDefaultSansSerifFontName();
           #endif
        }

        /** tracking is extra letter spacing as a proportion of the height (0.1 = airy capitals). */
        static juce::Font font (float height, bool bold = false, float tracking = 0.0f)
        {
            return juce::Font (juce::FontOptions (fontName(), height, bold ? juce::Font::bold : juce::Font::plain)
                                   .withKerningFactor (tracking));
        }

        // Base editor size; the window scales this whole layout from 75% to 200%.
        static constexpr int baseWidth = 1100;
        static constexpr int baseHeight = 680;
        static constexpr float corner = 6.0f;
    };
}
