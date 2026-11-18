#pragma once

#include "Theme.h"

namespace sl::ui
{
    /** Draws every standard JUCE widget StrandLock uses, in the editor's theme. No bitmaps: all vector drawing.
        Built from GrainLock's GrainLookAndFeel; the knob is the prototype's dial (sl-ui-sample.js). After the
        theme changes, call refreshColours() and repaint. */
    class StrandLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit StrandLookAndFeel (const Theme& theme);

        /** Sets the JUCE colour ids from the theme (after a theme switch). */
        void refreshColours();

        const Theme& getTheme() const noexcept { return theme; }

        /** A knob's colour: slider.getProperties()["tok"] holds the Tok of its value arc (the accent when absent);
            "bipolar" draws the arc from twelve o'clock. */
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

        /** Every JUCE drop-down list StrandLock opens (the Grid, UI Scale, the sound and preset menus) gets the
            menuKeyGuard: Space closes the list and picks nothing. JUCE's own rule is that Space picks the item under
            the mouse, as Return does (fix pass R4T-1). */
        void preparePopupMenuWindow (juce::Component& newWindow) override;

        /** What preparePopupMenuWindow adds to a list's window: it keeps Space (closing the list) and leaves every
            other key to the list. A JUCE list holds the keyboard while it is open, so Space cannot reach Cubase then
            anyway; this way it does nothing harmful. */
        static juce::KeyListener& menuKeyGuard();

        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                            bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                            bool isMouseOver, bool isMouseDown) override;
        int getDefaultScrollbarWidth() override { return 8; }

        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    private:
        const Theme& theme;
    };
}
