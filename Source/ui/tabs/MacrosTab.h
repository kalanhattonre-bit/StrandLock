#pragma once

#include "TabPanel.h"

namespace sl::ui
{
    /** The Macros tab (M0): the 8 macro knobs (Quick Controls 1-8, D8) and the 5 whole-clip offsets (A3), plain
        knobs bound to their parameters. M6 adds Centre all, Reset all, Recipes and Dice. */
    class MacrosTab final : public TabPanel
    {
    public:
        MacrosTab (const Theme&, juce::AudioProcessorValueTreeState&);
    };
}
