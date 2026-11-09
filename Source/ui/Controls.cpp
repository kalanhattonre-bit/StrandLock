#include "Controls.h"

#include <cmath>

namespace sl::ui
{
    namespace
    {
        // shell.html's button: padding 3px 7px at this window height, font 12px, weight 600, radius 7px.
        constexpr float kPadX = 8.0f;
        constexpr float kChipLeft = 5.0f;
        constexpr float kIconSize = 12.0f;
        constexpr float kDisabledAlpha = 0.45f;   // button:disabled{opacity:.45}

        juce::Font labelFont()   { return Theme::px (10.5f, false, 0.06f); }
        juce::Font chipFont()    { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold).withPointHeight (10.0f)); }

        juce::Path roundedBox (juce::Rectangle<float> r, float corner, bool squareLeft, bool squareRight)
        {
            juce::Path p;
            p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), corner, corner,
                                   ! squareLeft, ! squareRight, ! squareLeft, ! squareRight);
            return p;
        }
    }

    //==============================================================================================
    FineSlider::FineSlider()
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        setScrollWheelEnabled (true);
        setPopupMenuEnabled (false);
        // The prototype's dial: 270 degrees from bottom-left to bottom-right.
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }

    void FineSlider::mouseDown (const juce::MouseEvent& e)
    {
        dragProportion = valueToProportionOfLength (getValue());
        lastDragPosition = e.position;
        juce::Slider::mouseDown (e);   // starts the host automation gesture
    }

    void FineSlider::mouseDrag (const juce::MouseEvent& e)
    {
        // Replaces Slider's own drag so Ctrl can switch to fine steps mid-drag without a jump.
        if (! dragging || ! isEnabled() || e.mods.isPopupMenu())
            return;

        const auto delta = e.position - lastDragPosition;
        lastDragPosition = e.position;

        const bool fine = e.mods.isCtrlDown() || e.mods.isCommandDown();
        const double pixelsForFullRange = fine ? 1600.0 : 200.0;

        dragProportion = juce::jlimit (0.0, 1.0, dragProportion + (double) (delta.x - delta.y) / pixelsForFullRange);
        setValue (proportionOfLengthToValue (dragProportion), juce::sendNotificationSync);
    }

    void FineSlider::mouseEnter (const juce::MouseEvent& e)
    {
        juce::Slider::mouseEnter (e);
        if (onHoverChange)
            onHoverChange();
    }

    void FineSlider::mouseExit (const juce::MouseEvent& e)
    {
        juce::Slider::mouseExit (e);
        if (onHoverChange)
            onHoverChange();
    }

    //==============================================================================================
    Knob::Knob (const Theme& t, juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                const juce::String& displayName, Tok colour, bool bipolar)
        : theme (t), parameter (*state.getParameter (parameterId)), name (displayName)
    {
        slider.getProperties().set ("bipolar", bipolar);
        slider.getProperties().set ("tok", static_cast<int> (colour));
        slider.onHoverChange = [this] { repaint(); };
        slider.onValueChange = [this] { repaint(); };
        slider.onDragStart = [this] { repaint(); };
        slider.onDragEnd = [this] { repaint(); };
        slider.setTitle (displayName);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);
    }

    int Knob::preferredWidth() const
    {
        const float w = Theme::textWidth (labelFont(), name.toUpperCase()) + 8.0f;
        return juce::jlimit (64, 96, (int) std::ceil (w));
    }

    void Knob::resized()
    {
        // label 16, dial 36, value 16 (sl-ui-sample.css: .sls-klbl, .sls-dial, .sls-kval)
        slider.setBounds (getLocalBounds().withTrimmedTop (17).withHeight (38).withSizeKeepingCentre (38, 38));
    }

    void Knob::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();
        const auto labelArea = area.removeFromTop (16);
        const auto valueArea = area.removeFromBottom (16);

        g.setColour (theme (Tok::dim));
        g.setFont (labelFont());
        g.drawFittedText (name.toUpperCase(), labelArea, juce::Justification::centred, 1, 0.8f);

        g.setColour (slider.isMouseOverOrDragging() ? theme (Tok::acc) : theme (Tok::txt));
        g.setFont (Theme::px (11.0f, slider.isMouseButtonDown()));
        g.drawFittedText (slider.getTextFromValue (slider.getValue()), valueArea, juce::Justification::centred, 1, 0.8f);
    }

    //==============================================================================================
    FlatButton::FlatButton (const Theme& t, const juce::String& buttonText, Icon i)
        : juce::Button (buttonText), theme (t), icon (i)
    {
    }

    void FlatButton::setKeyChip (const juce::String& keyText)  { keyChip = keyText; repaint(); }
    void FlatButton::setLaneColour (std::optional<Tok> c)       { lane = c; repaint(); }
    void FlatButton::setJoins (bool l, bool r)                  { joinLeft = l; joinRight = r; repaint(); }
    void FlatButton::setIcon (Icon i)                           { icon = i; repaint(); }
    void FlatButton::setFontPx (float px)                       { fontPx = px; repaint(); }

    int FlatButton::preferredWidth() const
    {
        float w = 2.0f * kPadX;
        const auto label = getButtonText();
        if (label.isNotEmpty())
            w += Theme::textWidth (Theme::px (fontPx, true), label);
        if (icon != Icon::none)
            w += kIconSize + (label.isNotEmpty() ? 5.0f : 0.0f);
        if (keyChip.isNotEmpty())
            w += Theme::textWidth (chipFont(), keyChip) + 6.0f + 6.0f;
        if (lane.has_value())
            w += 2.0f;
        return (int) std::ceil (w);
    }

    void FlatButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
    {
        const bool enabled = isEnabled();
        const bool on = getToggleState();
        const float alpha = enabled ? 1.0f : kDisabledAlpha;
        const auto fade = [alpha] (juce::Colour c) { return c.withMultipliedAlpha (alpha); };

        auto r = getLocalBounds().toFloat();
        if (isButtonDown && enabled)
            r = r.translated (0.0f, 1.0f);   // button:active{transform:translateY(1px)}

        const float border = lane.has_value() ? 2.0f : 1.0f;
        const auto box = r.reduced (border * 0.5f);
        const auto shape = roundedBox (box, Theme::corner, joinLeft, joinRight);

        juce::Colour fill = theme (Tok::btn), edge = theme (Tok::btnb), ink = theme (Tok::txt);
        if (lane.has_value())
        {
            edge = theme (*lane);
            if (on)
            {
                fill = theme (*lane);
                ink = theme (Tok::onLane);
            }
        }
        else if (on)
        {
            fill = edge = theme (Tok::acc);
            ink = theme (Tok::onacc);
        }
        if (! on && isMouseOver && enabled)
            fill = theme (Tok::hover);

        g.setColour (fade (fill));
        g.fillPath (shape);
        g.setColour (fade (edge));
        g.strokePath (shape, juce::PathStrokeType (border));

        auto content = r.reduced (kPadX - 1.0f, 0.0f);
        if (keyChip.isNotEmpty())
        {
            const float cw = Theme::textWidth (chipFont(), keyChip) + 6.0f;
            const auto chip = juce::Rectangle<float> (r.getX() + kChipLeft + border - 1.0f, r.getCentreY() - 7.0f, cw, 14.0f);
            g.setColour (fade (theme (Tok::kbdBg)));
            g.fillRoundedRectangle (chip, 3.0f);
            g.setColour (fade (ink));
            g.setFont (chipFont());
            g.drawText (keyChip, chip, juce::Justification::centred, false);
            content = content.withLeft (chip.getRight() + 6.0f);
        }

        const auto label = getButtonText();
        const auto font = Theme::px (fontPx, true);
        const float textW = label.isNotEmpty() ? Theme::textWidth (font, label) : 0.0f;
        const float iconW = icon != Icon::none ? kIconSize + (label.isNotEmpty() ? 5.0f : 0.0f) : 0.0f;
        const float total = textW + iconW;
        float x = keyChip.isNotEmpty() ? content.getX() : content.getCentreX() - total * 0.5f;

        if (icon != Icon::none)
        {
            paintIcon (g, icon, juce::Rectangle<float> (x, r.getCentreY() - kIconSize * 0.5f, kIconSize, kIconSize), fade (ink), fill);
            x += iconW;
        }

        if (label.isNotEmpty())
        {
            g.setColour (fade (ink));
            g.setFont (font);
            g.drawText (label, juce::Rectangle<float> (x, r.getY(), juce::jmax (textW + 2.0f, content.getRight() - x), r.getHeight()),
                        juce::Justification::centredLeft, true);
        }
    }

    void paintIcon (juce::Graphics& g, FlatButton::Icon icon, juce::Rectangle<float> a, juce::Colour ink, juce::Colour background)
    {
        using Icon = FlatButton::Icon;
        const auto c = a.getCentre();
        const float s = juce::jmin (a.getWidth(), a.getHeight());
        const auto thin = juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (ink);

        switch (icon)
        {
            case Icon::none:
                break;

            case Icon::sun:
            {
                g.fillEllipse (juce::Rectangle<float> (s * 0.42f, s * 0.42f).withCentre (c));
                for (int i = 0; i < 8; ++i)
                {
                    const float ang = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                    const juce::Point<float> dir (std::sin (ang), -std::cos (ang));
                    g.drawLine (juce::Line<float> (c + dir * (s * 0.32f), c + dir * (s * 0.48f)), 1.3f);
                }
                break;
            }

            case Icon::moon:
            {
                const auto disc = juce::Rectangle<float> (s * 0.82f, s * 0.82f).withCentre (c);
                g.fillEllipse (disc);
                g.setColour (background);
                g.fillEllipse (disc.translated (s * 0.26f, -s * 0.16f));
                break;
            }

            case Icon::keys:
            {
                const auto body = juce::Rectangle<float> (s, s * 0.66f).withCentre (c);
                g.drawRoundedRectangle (body, 1.8f, 1.2f);
                for (int row = 0; row < 2; ++row)
                    for (int k = 0; k < 4; ++k)
                        g.fillRect (juce::Rectangle<float> (1.4f, 1.4f).withCentre ({ body.getX() + body.getWidth() * (0.2f + 0.2f * (float) k),
                                                                                       body.getY() + body.getHeight() * (0.32f + 0.26f * (float) row) }));
                g.fillRect (juce::Rectangle<float> (body.getWidth() * 0.5f, 1.3f).withCentre ({ c.x, body.getY() + body.getHeight() * 0.8f }));
                break;
            }

            case Icon::folder:
            {
                juce::Path p;
                const auto b = juce::Rectangle<float> (s, s * 0.74f).withCentre (c);
                p.startNewSubPath (b.getX(), b.getBottom());
                p.lineTo (b.getX(), b.getY());
                p.lineTo (b.getX() + b.getWidth() * 0.38f, b.getY());
                p.lineTo (b.getX() + b.getWidth() * 0.48f, b.getY() + b.getHeight() * 0.18f);
                p.lineTo (b.getRight(), b.getY() + b.getHeight() * 0.18f);
                p.lineTo (b.getRight(), b.getBottom());
                p.closeSubPath();
                g.strokePath (p, thin);
                break;
            }

            case Icon::notes:
            {
                g.fillEllipse (juce::Rectangle<float> (s * 0.34f, s * 0.26f).withCentre ({ a.getX() + s * 0.25f, a.getBottom() - s * 0.16f }));
                g.fillEllipse (juce::Rectangle<float> (s * 0.34f, s * 0.26f).withCentre ({ a.getX() + s * 0.78f, a.getBottom() - s * 0.28f }));
                g.drawLine (a.getX() + s * 0.4f, a.getBottom() - s * 0.16f, a.getX() + s * 0.4f, a.getY() + s * 0.12f, 1.2f);
                g.drawLine (a.getX() + s * 0.93f, a.getBottom() - s * 0.28f, a.getX() + s * 0.93f, a.getY(), 1.2f);
                g.drawLine (a.getX() + s * 0.4f, a.getY() + s * 0.12f, a.getX() + s * 0.93f, a.getY(), 1.6f);
                break;
            }

            case Icon::nodes:
            {
                g.drawEllipse (juce::Rectangle<float> (s * 0.84f, s * 0.84f).withCentre (c), 1.2f);
                g.fillEllipse (juce::Rectangle<float> (s * 0.34f, s * 0.34f).withCentre (c));
                break;
            }

            case Icon::chevronDown:
            case Icon::chevronUp:
            {
                const float dir = icon == Icon::chevronDown ? 1.0f : -1.0f;
                juce::Path p;
                p.addTriangle (c.x - s * 0.28f, c.y - dir * s * 0.14f, c.x + s * 0.28f, c.y - dir * s * 0.14f, c.x, c.y + dir * s * 0.2f);
                g.fillPath (p);
                break;
            }

            case Icon::copy:
            {
                const auto back = juce::Rectangle<float> (s * 0.6f, s * 0.7f).withPosition (a.getX() + s * 0.08f, a.getY() + s * 0.06f);
                const auto front = back.translated (s * 0.3f, s * 0.22f);
                g.drawRoundedRectangle (back, 1.5f, 1.2f);
                g.setColour (background);
                g.fillRoundedRectangle (front, 1.5f);
                g.setColour (ink);
                g.drawRoundedRectangle (front, 1.5f, 1.2f);
                break;
            }
        }
    }

    //==============================================================================================
    ParamToggle::ParamToggle (const Theme& t, juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& buttonText)
        : FlatButton (t, buttonText)
    {
        setClickingTogglesState (true);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, *this);
    }

    //==============================================================================================
    PillToggle::PillToggle (const Theme& t, juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& caption)
        : juce::Button (caption), theme (t)
    {
        setClickingTogglesState (true);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, *this);
    }

    void PillToggle::paintButton (juce::Graphics& g, bool isMouseOver, bool)
    {
        auto area = getLocalBounds().toFloat();
        const auto labelArea = area.removeFromBottom (15.0f);

        const bool on = getToggleState();
        const float w = juce::jmin (30.0f, area.getWidth() - 4.0f);
        const float h = w * 0.52f;
        const auto pill = juce::Rectangle<float> (w, h).withCentre (area.getCentre());

        g.setColour (on ? theme (Tok::acc) : theme (Tok::btnb));
        g.fillRoundedRectangle (pill, h * 0.5f);
        if (isMouseOver)
        {
            g.setColour (theme (Tok::txt).withAlpha (0.35f));
            g.drawRoundedRectangle (pill.expanded (1.5f), h * 0.5f + 1.5f, 1.0f);
        }

        const float d = h - 4.0f;
        const auto thumb = juce::Rectangle<float> (d, d).withCentre ({ on ? pill.getRight() - h * 0.5f : pill.getX() + h * 0.5f,
                                                                       pill.getCentreY() });
        g.setColour (on ? theme (Tok::onacc) : theme (Tok::dim));
        g.fillEllipse (thumb);

        g.setColour (on ? theme (Tok::txt) : theme (Tok::dim));
        g.setFont (labelFont());
        g.drawFittedText (getButtonText().toUpperCase(), labelArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    //==============================================================================================
    SegmentedControl::SegmentedControl (const Theme& t, juce::RangedAudioParameter& parameter,
                                        const juce::StringArray& segmentLabels, const juce::String& captionText)
        : theme (t),
          labels (segmentLabels),
          caption (captionText),
          attachment (parameter, [this] (float value)
                      {
                          selected = juce::roundToInt (value);
                          repaint();
                      })
    {
        setTitle (captionText);
        attachment.sendInitialUpdate();
    }

    float SegmentedControl::captionWidth() const
    {
        return caption.isEmpty() ? 0.0f : std::ceil (Theme::textWidth (labelFont(), caption.toUpperCase())) + 8.0f;
    }

    std::vector<float> SegmentedControl::segmentWidths() const
    {
        std::vector<float> widths;
        const auto font = Theme::px (12.0f, true);
        for (const auto& label : labels)
            widths.push_back (std::ceil (Theme::textWidth (font, label)) + 2.0f * kPadX);
        return widths;
    }

    int SegmentedControl::preferredWidth() const
    {
        float w = captionWidth();
        for (float s : segmentWidths())
            w += s;
        return (int) std::ceil (w);
    }

    juce::Rectangle<int> SegmentedControl::segmentBounds (int index) const
    {
        const auto widths = segmentWidths();
        float total = 0.0f;
        for (float s : widths)
            total += s;
        const float x0 = captionWidth();
        const float scale = total > 0.0f ? juce::jmax (0.0f, (float) getWidth() - x0) / total : 0.0f;
        float x = x0;
        for (int i = 0; i < (int) widths.size(); ++i)
        {
            const float w = widths[(std::size_t) i] * juce::jmin (1.0f, scale);
            if (i == index)
                return juce::Rectangle<float> (x, 0.0f, w, (float) getHeight()).toNearestInt();
            x += w;
        }
        return {};
    }

    int SegmentedControl::segmentAt (juce::Point<float> position) const
    {
        for (int i = 0; i < labels.size(); ++i)
            if (segmentBounds (i).toFloat().contains (position))
                return i;
        return -1;
    }

    void SegmentedControl::paint (juce::Graphics& g)
    {
        if (caption.isNotEmpty())
        {
            g.setColour (theme (Tok::dim));
            g.setFont (labelFont());
            g.drawText (caption.toUpperCase(), juce::Rectangle<float> (0.0f, 0.0f, captionWidth(), (float) getHeight()),
                        juce::Justification::centredLeft, false);
        }

        const auto font = Theme::px (12.0f, true);
        for (int i = 0; i < labels.size(); ++i)
        {
            const auto seg = segmentBounds (i).toFloat().reduced (0.5f);
            const bool on = i == selected;
            const bool first = i == 0, last = i == labels.size() - 1;
            const auto shape = roundedBox (seg, Theme::corner, ! first, ! last);

            g.setColour (on ? theme (Tok::acc) : (i == hovered ? theme (Tok::hover) : theme (Tok::btn)));
            g.fillPath (shape);
            g.setColour (on ? theme (Tok::acc) : theme (Tok::btnb));
            g.strokePath (shape, juce::PathStrokeType (1.0f));

            g.setColour (on ? theme (Tok::onacc) : theme (Tok::txt));
            g.setFont (font);
            g.drawFittedText (labels[i], seg.toNearestInt(), juce::Justification::centred, 1, 0.75f);
        }
    }

    void SegmentedControl::mouseDown (const juce::MouseEvent& e)
    {
        const int index = segmentAt (e.position);
        if (index >= 0 && index != selected)
            attachment.setValueAsCompleteGesture ((float) index);
    }

    void SegmentedControl::mouseMove (const juce::MouseEvent& e)
    {
        const int index = segmentAt (e.position);
        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }

    void SegmentedControl::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    //==============================================================================================
    ChoiceBox::ChoiceBox (const Theme& t, juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                          const juce::String& captionText, int width)
        : theme (t), caption (captionText), boxWidth (width)
    {
        if (auto* choices = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId)))
            box.addItemList (choices->choices, 1);
        box.setTitle (captionText);
        addAndMakeVisible (box);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, parameterId, box);
    }

    float ChoiceBox::captionWidth() const
    {
        return caption.isEmpty() ? 0.0f : std::ceil (Theme::textWidth (labelFont(), caption.toUpperCase())) + 8.0f;
    }

    int ChoiceBox::preferredWidth() const
    {
        return (int) captionWidth() + boxWidth;
    }

    void ChoiceBox::resized()
    {
        box.setBounds (getLocalBounds().withTrimmedLeft ((int) captionWidth()));
    }

    void ChoiceBox::paint (juce::Graphics& g)
    {
        if (caption.isEmpty())
            return;
        g.setColour (theme (Tok::dim));
        g.setFont (labelFont());
        g.drawText (caption.toUpperCase(), juce::Rectangle<float> (0.0f, 0.0f, captionWidth(), (float) getHeight()),
                    juce::Justification::centredLeft, false);
    }

    //==============================================================================================
    TabStrip::TabStrip (const Theme& t, const juce::StringArray& tabNames)
        : theme (t), names (tabNames)
    {
    }

    void TabStrip::setSelected (int index)
    {
        index = juce::jlimit (0, juce::jmax (0, names.size() - 1), index);
        if (index != selected)
        {
            selected = index;
            repaint();
        }
    }

    void TabStrip::setFolded (bool isFolded)
    {
        if (folded != isFolded)
        {
            folded = isFolded;
            repaint();
        }
    }

    juce::Rectangle<int> TabStrip::tabBounds (int index) const
    {
        // .tab{padding:6px 8px}; the font is the button's 12px semibold
        const auto font = Theme::px (12.0f, true);
        float x = 0.0f;
        for (int i = 0; i < names.size(); ++i)
        {
            const float w = std::ceil (Theme::textWidth (font, names[i])) + 16.0f;
            if (i == index)
                return juce::Rectangle<float> (x, 0.0f, w, (float) getHeight()).toNearestInt();
            x += w;
        }
        return {};
    }

    int TabStrip::tabAt (juce::Point<float> position) const
    {
        for (int i = 0; i < names.size(); ++i)
            if (tabBounds (i).toFloat().contains (position))
                return i;
        return -1;
    }

    void TabStrip::paint (juce::Graphics& g)
    {
        const auto font = Theme::px (12.0f, true);
        g.setFont (font);
        for (int i = 0; i < names.size(); ++i)
        {
            auto tab = tabBounds (i).toFloat();
            const bool open = i == selected && ! folded;
            g.setColour (open ? theme (Tok::acc) : (i == hovered ? theme (Tok::txt) : theme (Tok::dim)));
            g.drawText (names[i], tab, juce::Justification::centred, false);
            if (open)
                g.fillRect (tab.removeFromBottom (2.0f));
        }
    }

    void TabStrip::mouseDown (const juce::MouseEvent& e)
    {
        const int index = tabAt (e.position);
        if (index < 0)
            return;
        if (index != selected)
        {
            selected = index;
            repaint();
        }
        if (onClick)
            onClick (index);
    }

    void TabStrip::mouseMove (const juce::MouseEvent& e)
    {
        const int index = tabAt (e.position);
        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }

    void TabStrip::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    //==============================================================================================
    Caption::Caption (const Theme& t, const juce::String& captionText, Style s)
        : theme (t), text (captionText), style (s)
    {
        setInterceptsMouseClicks (false, false);
    }

    void Caption::setText (const juce::String& newText)
    {
        if (newText != text)
        {
            text = newText;
            repaint();
        }
    }

    juce::Font Caption::getFont() const
    {
        switch (style)
        {
            case Style::label:  return labelFont();
            case Style::sub:    return Theme::px (12.0f);
            case Style::text:   return Theme::px (12.0f);
            case Style::strong: return Theme::px (12.0f, true);
        }
        return Theme::px (12.0f);
    }

    int Caption::preferredWidth() const
    {
        const auto shown = style == Style::label ? text.toUpperCase() : text;
        return (int) std::ceil (Theme::textWidth (getFont(), shown)) + 2;
    }

    void Caption::paint (juce::Graphics& g)
    {
        g.setColour (style == Style::label || style == Style::sub ? theme (Tok::dim) : theme (Tok::txt));
        g.setFont (getFont());
        g.drawFittedText (style == Style::label ? text.toUpperCase() : text, getLocalBounds(),
                          juce::Justification::centredLeft, juce::jmax (1, getHeight() / 15), 0.9f);
    }

    //==============================================================================================
    PresetButton::PresetButton (const Theme& t)
        : theme (t)
    {
    }

    void PresetButton::setDisplayedName (const juce::String& name)
    {
        if (name != displayed)
        {
            displayed = name;
            repaint();
        }
    }

    void PresetButton::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool over = isMouseOver (true);

        g.setColour (theme (Tok::btn));
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (over ? theme (Tok::dim) : theme (Tok::btnb));
        g.drawRoundedRectangle (r, Theme::corner, 1.0f);

        const float h = r.getHeight();
        paintIcon (g, FlatButton::Icon::chevronDown, juce::Rectangle<float> (h * 0.5f, h * 0.5f).withCentre ({ r.getRight() - h * 0.5f, r.getCentreY() }),
                   theme (Tok::txt), theme (Tok::btn));

        g.setColour (theme (Tok::txt));
        g.setFont (Theme::px (12.0f, true));
        g.drawFittedText (displayed, r.reduced (h, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    void PresetButton::mouseDown (const juce::MouseEvent&)
    {
        const auto names = getNames ? getNames() : juce::StringArray();
        const int current = getCurrentIndex ? getCurrentIndex() : -1;

        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, i == current);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                      .withMinimumWidth (getWidth())
                                                      .withMaximumNumColumns (1),
                            [safe = juce::Component::SafePointer<PresetButton> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0 && safe->onPick)
                                    safe->onPick (result - 1);
                            });
    }

    ChevronButton::ChevronButton (const Theme& t, bool pointsRight)
        : juce::Button (pointsRight ? "Next preset" : "Previous preset"), theme (t), right (pointsRight)
    {
    }

    void ChevronButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isButtonDown ? theme (Tok::hover) : theme (Tok::btn));
        g.fillRoundedRectangle (r, Theme::corner);
        g.setColour (isMouseOver ? theme (Tok::dim) : theme (Tok::btnb));
        g.drawRoundedRectangle (r, Theme::corner, 1.0f);

        const auto c = r.getCentre();
        const float s = r.getHeight() * 0.16f;
        juce::Path chevron;
        const float dir = right ? 1.0f : -1.0f;
        chevron.startNewSubPath (c.x - dir * s * 0.6f, c.y - s);
        chevron.lineTo (c.x + dir * s * 0.6f, c.y);
        chevron.lineTo (c.x - dir * s * 0.6f, c.y + s);
        g.setColour (isMouseOver ? theme (Tok::acc) : theme (Tok::txt));
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==============================================================================================
    int flowLayout (const std::vector<FlowItem>& items, juce::Rectangle<int> area, int gap, int rowGap, bool place)
    {
        // First split the items into rows, then place each row centred on its own height.
        struct Row { std::size_t first = 0, last = 0; int height = 0; };
        std::vector<Row> rows;
        int x = area.getX();
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            const int w = juce::jmin (it.width, area.getWidth());
            const bool startRow = rows.empty() || it.newRow || (x > area.getX() && x + w > area.getRight());
            if (startRow)
            {
                rows.push_back ({ i, i, 0 });
                x = area.getX();
            }
            rows.back().last = i;
            rows.back().height = juce::jmax (rows.back().height, it.height);
            x += w + gap;
        }

        int y = area.getY();
        for (std::size_t r = 0; r < rows.size(); ++r)
        {
            const auto& row = rows[r];
            if (place)
            {
                int rx = area.getX();
                for (std::size_t i = row.first; i <= row.last; ++i)
                {
                    const auto& it = items[i];
                    const int w = juce::jmin (it.width, area.getWidth());
                    if (it.component != nullptr)
                        it.component->setBounds (rx, y + (row.height - it.height) / 2, w, it.height);
                    rx += w + gap;
                }
            }
            y += row.height + (r + 1 < rows.size() ? rowGap : 0);
        }
        return y - area.getY();
    }
}
