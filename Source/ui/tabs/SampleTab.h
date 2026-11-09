#pragma once

#include "TabPanel.h"

namespace sl::ui
{
    /** The Sample tab (M0): every Sample, Refresh and Region parameter as a plain control bound to it. M6 makes
        it the full tab (fading what does nothing now, one Grain knob per engine, Recipes and Dice). */
    class SampleTab final : public TabPanel
    {
    public:
        SampleTab (const Theme&, juce::AudioProcessorValueTreeState&);
    };
}
