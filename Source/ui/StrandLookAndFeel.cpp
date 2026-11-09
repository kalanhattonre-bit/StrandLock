#include "StrandLookAndFeel.h"

#include <cmath>

namespace sl::ui
{
    StrandLookAndFeel::StrandLookAndFeel (const Theme& t)
        : theme (t)
    {
        refreshColours();
    }

    void StrandLookAndFeel::refreshColours()
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme (Tok::bg));

        setColour (juce::PopupMenu::backgroundColourId, theme (Tok::menu));
        setColour (juce::PopupMenu::textColourId, theme (Tok::txt));
        setColour (juce::PopupMenu::headerTextColourId, theme (Tok::dim));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme (Tok::hover));
        setColour (juce::PopupMenu::highlightedTextColourId, theme (Tok::txt));

        setColour (juce::ComboBox::backgroundColourId, theme (Tok::btn));
        setColour (juce::ComboBox::outlineColourId, theme (Tok::btnb));
        setColour (juce::ComboBox::textColourId, theme (Tok::txt));
        setColour (juce::ComboBox::arrowColourId, theme (Tok::dim));
        setColour (juce::ComboBox::focusedOutlineColourId, theme (Tok::acc));

        setColour (juce::Label::textColourId, theme (Tok::txt));
        setColour (juce::Slider::rotarySliderFillColourId, theme (Tok::acc));
        setColour (juce::Slider::rotarySliderOutlineColourId, theme (Tok::btnb));

        setColour (juce::ScrollBar::thumbColourId, theme (Tok::btnb));
        setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

        setColour (juce::TooltipWindow::backgroundColourId, theme (Tok::tipBg));
        setColour (juce::TooltipWindow::textColourId, theme (Tok::tipTxt));
        setColour (juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);
    }

    void StrandLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
    {
        // The prototype's dial (sl-ui-sample.js): a 40-unit square with a 270 degree track of radius 15 and
        // stroke 4, the value arc in the knob's colour, a cap of radius 10.5 and a dot that turns with it.
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        if (size <= 6.0f)
            return;

        const float unit = size / 40.0f;
        const auto centre = bounds.getCentre();
        const float radius = 15.0f * unit;
        const bool enabled = slider.isEnabled();
        const auto& props = slider.getProperties();
        const bool bipolar = (bool) props["bipolar"];
        const auto colourTok = props.contains ("tok") ? static_cast<Tok> ((int) props["tok"]) : Tok::acc;
        const auto stroke = juce::PathStrokeType (4.0f * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (theme (Tok::btnb));
        g.strokePath (track, stroke);

        const float valueAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float originAngle = bipolar ? 0.5f * (rotaryStartAngle + rotaryEndAngle) : rotaryStartAngle;
        if (std::abs (valueAngle - originAngle) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                               juce::jmin (originAngle, valueAngle), juce::jmax (originAngle, valueAngle), true);
            g.setColour (enabled ? theme (colourTok) : theme (Tok::dim));
            g.strokePath (arc, stroke);
        }

        if (bipolar)
        {
            const float midAngle = 0.5f * (rotaryStartAngle + rotaryEndAngle);
            const auto mid = centre + juce::Point<float> (std::sin (midAngle), -std::cos (midAngle)) * (radius + 3.4f * unit);
            g.setColour (theme (Tok::dim));
            g.fillEllipse (juce::Rectangle<float> (2.2f * unit, 2.2f * unit).withCentre (mid));
        }

        const auto cap = juce::Rectangle<float> (21.0f * unit, 21.0f * unit).withCentre (centre);
        g.setColour (theme (Tok::btn));
        g.fillEllipse (cap);
        g.setColour (slider.isMouseButtonDown() ? theme (colourTok) : theme (Tok::line));
        g.drawEllipse (cap, juce::jmax (1.0f, unit));

        const auto dot = centre + juce::Point<float> (std::sin (valueAngle), -std::cos (valueAngle)) * (7.0f * unit);
        g.setColour (enabled ? theme (Tok::txt) : theme (Tok::dim));
        g.fillEllipse (juce::Rectangle<float> (4.4f * unit, 4.4f * unit).withCentre (dot));
    }

    void StrandLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        // A select of shell.html: the button colours, a 7 px corner, a small arrow on the right.
        const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        g.setColour (theme (Tok::btn));
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (box.isMouseOver (true) && box.isEnabled() ? theme (Tok::dim) : theme (Tok::btnb));
        g.drawRoundedRectangle (r, Theme::corner, 1.0f);

        const float h = (float) height;
        const float cx = (float) width - 12.0f;
        const float cy = h * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
        arrow.lineTo (cx, cy + 2.0f);
        arrow.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (box.isEnabled() ? theme (Tok::txt) : theme (Tok::dim));
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font StrandLookAndFeel::getComboBoxFont (juce::ComboBox& box)
    {
        // A box can be asked for its font before it has a size; FontOptions asserts on a zero height.
        return Theme::px (juce::jlimit (8.0f, 12.0f, (float) box.getHeight() * 0.5f), true);
    }

    void StrandLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (4, 1, juce::jmax (0, box.getWidth() - 24), box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, box.isEnabled() ? theme (Tok::txt) : theme (Tok::dim));
    }

    void StrandLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
    {
        // Three short diagonal strokes in the corner, in the accent while it is being used.
        g.setColour (isMouseOver || isMouseDragging ? theme (Tok::acc) : theme (Tok::dim));
        const float size = (float) juce::jmin (w, h);
        for (int i = 1; i <= 3; ++i)
        {
            const float d = size * 0.28f * (float) i;
            g.drawLine ((float) w - d, (float) h - 2.0f, (float) w - 2.0f, (float) h - d, 1.2f);
        }
    }

    juce::Font StrandLookAndFeel::getPopupMenuFont()
    {
        return Theme::px (13.0f);
    }

    void StrandLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (theme (Tok::menu));
        g.setColour (theme (Tok::line));
        g.drawRect (0, 0, width, height, 1);
    }

    void StrandLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                           bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                           bool isMouseOver, bool isMouseDown)
    {
        if (thumbSize <= 0)
            return;

        auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize)
                                         : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height);
        const auto r = thumb.toFloat().reduced (1.5f);
        g.setColour (isMouseOver || isMouseDown ? theme (Tok::dim) : theme (Tok::btnb));
        g.fillRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f);
    }

    void StrandLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto r = juce::Rectangle<float> ((float) width, (float) height);
        g.setColour (theme (Tok::tipBg).withMultipliedAlpha (1.0f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme (Tok::tipTxt));
        g.setFont (Theme::px (11.5f));
        g.drawFittedText (text, r.reduced (7.0f, 3.0f).toNearestInt(), juce::Justification::centredLeft, 4, 1.0f);
    }
}
