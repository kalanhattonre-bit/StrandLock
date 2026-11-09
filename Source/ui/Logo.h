#pragma once

#include "Theme.h"

namespace sl::ui
{
    /** The Lock suite's mark (GrainLock's paintLogo): a 2.2 px ring in the accent with one dot riding it at
        0.28 pi, cut out of the ring in the background colour. The same geometry as the prototype's 22 px SVG. */
    void paintLogo (juce::Graphics&, juce::Rectangle<float> area, const Theme&);

    /** The wordmark STRANDLOCK, in the text colour, Theme::font (17, bold, 0.2) as in every Lock plugin. */
    void paintWordmark (juce::Graphics&, juce::Rectangle<float> area, const Theme&);

    /** The wordmark's font. */
    juce::Font wordmarkFont();

    inline constexpr const char* kWordmark = "STRANDLOCK";
}
