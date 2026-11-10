#pragma once

#include "TabPanel.h"

namespace sl::ui
{
    /** The Sound tab (M0): tuning and playing settings as plain controls bound to their parameters. The sound
        menu and Browse are on the waveform strip (ui/strip/WaveStrip); key, scale and Detect pitch come with M7. */
    class SoundTab final : public TabPanel
    {
    public:
        SoundTab (const Theme&, juce::AudioProcessorValueTreeState&);
    };
}
