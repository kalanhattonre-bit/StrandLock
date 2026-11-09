#pragma once

#include "Theme.h"

namespace strandlock::ui
{
    /** Draws every standard JUCE widget StrandLock uses. No bitmaps: all vector drawing.
        Copied from GrainLock's GrainLookAndFeel, so the suite's controls look the same everywhere. */
    class StrandLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        StrandLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    };
}
