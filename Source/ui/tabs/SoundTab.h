#pragma once

#include "TabPanel.h"

namespace sl::ui
{
    /** The Sound tab (M0): tuning and playing settings as plain controls bound to their parameters. The sound
        menu and Browse come with M2, key, scale and Detect pitch with M7. */
    class SoundTab final : public TabPanel
    {
    public:
        SoundTab (const Theme&, juce::AudioProcessorValueTreeState&);
    };
}
