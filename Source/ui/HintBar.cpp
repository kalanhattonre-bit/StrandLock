#include "HintBar.h"

#include <cmath>
#include <string>

namespace sl::ui
{
    namespace
    {
        // The prototype's signs, as UTF-8 (the source stays ASCII): the middle dot between the parts, the filled
        // and the hollow diamond (a slide handle, a segment's bend diamond) and the node dot.
        const std::string kDot = " \xc2\xb7 ";
        const std::string kFilledDiamond = "\xe2\x97\x86";
        const std::string kHollowDiamond = "\xe2\x97\x87";
        const std::string kNodeDot = "\xe2\x97\x8f";
        const std::string kSmallDown = "\xe2\x96\xbe";   // the lane button's menu arrow

        // sl-ui.js SUB_HINT: what each Sample lane's line does
        const char* subHint (core::LaneId lane) noexcept
        {
            switch (lane)
            {
                case core::LaneId::travel:
                    return "Travel: above the middle line plays forwards, below plays backwards, on the line stops (1/8x to 8x). "
                           "Path notes: the line is where in the Region the note is.";
                case core::LaneId::heads:
                    return "Heads: raise the line for more playheads (1 to 8), lower it for fewer. Flat = the Heads knob.";
                case core::LaneId::grain:
                    return "Grain: raise the line for longer grains, lower it for shorter ones. Flat = the Grain knob.";
                case core::LaneId::refresh:
                    return "Refresh: raise the line for faster fresh grabs, lower it for slower; the very bottom = Smooth. "
                           "The line works once Refresh on the Sample tab is Free or Synced (it looks dim until then).";
                case core::LaneId::pitch:
                case core::LaneId::amp:
                case core::LaneId::vib:
                case core::LaneId::tone:
                case core::LaneId::formant:
                case core::LaneId::hollow:
                    break;
            }
            return "";
        }

        juce::Font hintFont() { return Theme::px (11.5f); }
        juce::Font tryFont() { return Theme::px (11.5f, true); }
        constexpr const char* kTryLabel = "Try this: ";
    }

    juce::String modeHint (int mode, std::optional<core::LaneId> lane, bool freehand, bool linkArmed, bool nodes)
    {
        std::string t;
        if (linkArmed)
        {
            t = "SLIDE INTO: click the note the selected note should slide into.";
        }
        else if (mode <= 1 || ! lane.has_value())
        {
            t = "BUILD (1): double-click empty space = new note" + kDot + "drag a note to move it" + kDot
              + "drag its end dots sideways (length) or up/down (bend)" + kDot + kFilledDiamond + " slide handles" + kDot
              + "Shift+click a 2nd note = slide into it" + kDot + "drag on empty space = box select (Shift adds, Ctrl toggles)" + kDot
              + "right-click = menu";
        }
        else
        {
            const std::string name = core::laneInfo (*lane).name;
            const std::string upper = juce::String (name).toUpperCase().toStdString();
            const std::string title = mode >= 8 ? "SAMPLE (8)" + kDot + upper : upper + " (" + std::to_string (mode) + ")";
            if (freehand)
            {
                t = "FREEHAND (" + name + "): paint across the selected note; it turns into nodes when you let go.";
            }
            else if (nodes)
            {
                t = title + kDot + "NODES: drag a box around nodes of the selected notes, then drag one of them to move them together "
                                   "(Shift adds, Ctrl toggles).";
            }
            else
            {
                std::string extra;
                if (mode >= 8)
                    extra = kDot + subHint (*lane);
                else if (*lane == core::LaneId::pitch)
                    extra = kDot + "nodes snap to semitones (hold Shift for free)" + kDot + kFilledDiamond + " slide handles";
                t = title + ": notes are locked, only " + name + " changes" + kDot + "click a note to select it" + kDot + "drag "
                  + kNodeDot + " nodes" + kDot + "double-click the line = add a node" + kDot + "double-click a node = delete" + kDot
                  + "drag the small " + kHollowDiamond + " in a segment = curve it (it fills in), double-click a filled " + kFilledDiamond
                  + " = straighten" + kDot + kSmallDown + " on the lane button = shapes for every selected note" + extra;
            }
        }
        return juce::String::fromUTF8 (t.c_str());
    }

    //==============================================================================================
    HintBar::HintBar (const Theme& t)
        : theme (t), hover (t)
    {
        hover.setVisible (false);
    }

    void HintBar::setHint (const juce::String& newHint)
    {
        if (newHint == hint)
            return;
        hint = newHint;
        hintShown = hintShown && isHintCut();
        updateHover();
        repaint();
    }

    void HintBar::setTryThis (const juce::String& text)
    {
        if (text == tryThis)
            return;
        tryThis = text;
        repaint();
    }

    juce::Rectangle<int> HintBar::tryArea() const
    {
        const auto r = getLocalBounds().reduced (padX, 0);
        if (tryThis.isEmpty())
            return r.withWidth (0).withX (r.getRight());
        const float want = Theme::textWidth (tryFont(), kTryLabel) + Theme::textWidth (hintFont(), tryThis);
        const int w = juce::jmin ((int) std::ceil (want) + 2, r.getWidth() / 2);
        return r.withLeft (r.getRight() - w);
    }

    juce::Rectangle<int> HintBar::hintArea() const
    {
        const auto r = getLocalBounds().reduced (padX, 0);
        const auto t = tryArea();
        return r.withRight (t.getWidth() > 0 ? t.getX() - 12 : r.getRight());
    }

    bool HintBar::isHintCut() const
    {
        return Theme::textWidth (hintFont(), hint) > (float) hintArea().getWidth();
    }

    void HintBar::paint (juce::Graphics& g)
    {
        // the mode's hint, dim, cut with an ellipsis when it does not fit (shell.html #hint)
        g.setColour (theme (Tok::dim));
        g.setFont (hintFont());
        g.drawText (hint, hintArea(), juce::Justification::centredLeft, true);

        // "Try this:" in the accent, then its text (shell.html #tryThis)
        auto t = tryArea();
        if (t.getWidth() > 0)
        {
            const int tw = (int) std::ceil (Theme::textWidth (tryFont(), kTryLabel));
            g.setColour (theme (Tok::acc));
            g.setFont (tryFont());
            g.drawText (kTryLabel, t.removeFromLeft (tw), juce::Justification::centredLeft, false);
            g.setColour (theme (Tok::txt));
            g.setFont (hintFont());
            g.drawText (tryThis, t, juce::Justification::centredLeft, true);
        }
    }

    void HintBar::resized()
    {
        hintShown = hintShown && isHintCut();
        updateHover();
    }

    void HintBar::mouseEnter (const juce::MouseEvent&)
    {
        showHover (isHintCut());
    }

    void HintBar::mouseExit (const juce::MouseEvent&)
    {
        showHover (false);
    }

    void HintBar::showHover (bool show)
    {
        hintShown = show;
        updateHover();
    }

    void HintBar::setHelp (const juce::String& text)
    {
        if (text == help)
            return;
        help = text;
        updateHover();
    }

    void HintBar::updateHover()
    {
        // a control's text wins over the whole hint (sl-ui.js updateHint: the override covers the bar)
        const bool show = help.isNotEmpty() || hintShown;
        const juce::String& shown = help.isNotEmpty() ? help : hint;
        if (! show || getParentComponent() == nullptr || hover.getParentComponent() != getParentComponent())
        {
            hover.setVisible (false);
            return;
        }
        // as wide as the bar, from its bottom edge up: tall enough for the whole text, wrapped
        const int innerWidth = juce::jmax (40, getWidth() - 2 * padX);
        juce::AttributedString text;
        text.setText (shown);
        text.setFont (hintFont());
        text.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (text, (float) innerWidth);
        const int height = juce::jmax (getHeight() + 2, (int) std::ceil (layout.getHeight()) + 12);
        hover.text = shown;
        hover.setBounds (getX() - 1, getBottom() + 1 - height, getWidth() + 2, height);
        hover.setVisible (true);
        hover.toFront (false);
        hover.repaint();
    }

    void HintBar::Hover::paint (juce::Graphics& g)
    {
        // shell.html #hintHover: the panel colour, a 1 px accent outline, the card's round bottom corners
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        juce::Path box;
        box.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), Theme::frameCorner, Theme::frameCorner,
                                 false, false, true, true);
        g.setColour (theme (Tok::panel));
        g.fillPath (box);
        g.setColour (theme (Tok::acc));
        g.strokePath (box, juce::PathStrokeType (1.0f));

        juce::AttributedString as;
        as.setText (text);
        as.setFont (hintFont());
        as.setColour (theme (Tok::txt));
        as.setWordWrap (juce::AttributedString::byWord);
        as.setJustification (juce::Justification::centredLeft);
        juce::TextLayout layout;
        const auto inner = getLocalBounds().toFloat().reduced ((float) HintBar::padX, 5.0f);
        layout.createLayout (as, inner.getWidth());
        layout.draw (g, inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (inner.getHeight(), layout.getHeight())));
    }
}
