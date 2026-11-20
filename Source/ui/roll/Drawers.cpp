#include "Drawers.h"

#include "InspectorText.h"
#include "RollView.h"

#include "core/JsCompat.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "model/LaneFormat.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        constexpr int kItemHeight = 24;           // .slp-bar button: 4 px by 8 px of padding, 12 px text
        constexpr int kGap = 8, kRowGap = 6;       // .slp-line: gap 6px 8px
        constexpr int kSubGap = 10;                // .slp-sub: gap 6px 10px
        constexpr int kSubPadX = 8, kSubPadY = 6;  // .slp-sub: padding 6px 8px
        constexpr int kBaseBoxWidth = 74;          // a rate picker's list
        constexpr int kFieldBoxWidth = 150;        // Walk's field list

        // The prototype's signs as UTF-8 (the source stays ASCII).
        const juce::String kEllipsis = juce::String::charToString ((juce::juce_wchar) 0x2026);
        const juce::String kPlusMinus = juce::String::charToString ((juce::juce_wchar) 0x00b1);
        const juce::String kWarning = juce::String::charToString ((juce::juce_wchar) 0x26a0) + " ";
        const juce::String kDiamond = juce::String::charToString ((juce::juce_wchar) 0x25c6);

        // sl-ui-panels.js RATE_BASES: 1 bar .. 1/64 (a RateId is base * 3 + 0 straight, 1 triplet, 2 dotted)
        const char* const kBaseNames[] = { "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };

        juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
        juce::Font noteFont() { return Theme::px (11.5f); }
        juce::String laneNameOf (core::LaneId l) { return core::laneInfo (l).name; }

        /** sl-ui-panels.js fmtTime: "0:15.15". */
        juce::String fmtTime (double s)
        {
            s = std::max (0.0, s);
            const double m = std::floor (s / 60.0), r = s - m * 60.0;
            return str (core::jsNumberToString (m)) + ":" + (r < 10.0 ? "0" : "") + str (core::jsToFixed (r, 2));
        }

        juce::String rounded (double v) { return str (core::jsNumberToString (core::jsRound (v))); }

        int widthOf (const juce::Font& f, const juce::String& s) { return (int) std::ceil (Theme::textWidth (f, s)); }

        /** A width for a FlatButton (its preferred width with the bar's smaller padding, sl-ui-panels.css). */
        int buttonWidth (const FlatButton& b) { return b.preferredWidth() + 2; }
    }

    void noKeyboardFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        for (auto* child : c.getChildren())
            noKeyboardFocus (*child);
    }

    //==============================================================================================
    NoteText::NoteText (const Theme& t, bool isWarning)
        : theme (t), warn (isWarning)
    {
        setInterceptsMouseClicks (false, false);
    }

    void NoteText::setText (const juce::String& newText)
    {
        if (newText != text)
        {
            text = newText;
            repaint();
        }
    }

    void NoteText::setWarn (bool isWarning)
    {
        if (isWarning != warn)
        {
            warn = isWarning;
            repaint();
        }
    }

    int NoteText::naturalWidth() const
    {
        return widthOf (noteFont(), text) + 2;
    }

    int NoteText::heightFor (int width) const
    {
        if (text.isEmpty())
            return 0;
        if (naturalWidth() <= width)
            return 16;
        juce::AttributedString s;
        s.setText (text);
        s.setFont (noteFont());
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (s, (float) juce::jmax (40, width));
        return juce::jmax (16, (int) std::ceil (layout.getHeight()) + 2);
    }

    void NoteText::paint (juce::Graphics& g)
    {
        juce::AttributedString s;
        s.setText (text);
        s.setFont (noteFont());
        s.setColour (theme (warn ? Tok::warn : Tok::dim));
        s.setJustification (juce::Justification::centredLeft);
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (s, (float) getWidth());
        layout.draw (g, getLocalBounds().toFloat());
    }

    //==============================================================================================
    ArrowButton::ArrowButton (const Theme& t, const juce::String& buttonText)
        : FlatButton (t, buttonText)
    {
    }

    void ArrowButton::setOpen (bool isOpen)
    {
        if (isOpen != open)
        {
            open = isOpen;
            repaint();
        }
    }

    int ArrowButton::widthWithArrow() const
    {
        return preferredWidth() + 12;
    }

    void ArrowButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
    {
        // a FlatButton (shell.html's button: the accent while on, the hover colour under the mouse) whose text is
        // followed by the prototype's small triangle, down while closed and up while open
        const bool enabled = isEnabled(), on = getToggleState();
        const float alpha = enabled ? 1.0f : 0.45f;
        auto r = getLocalBounds().toFloat();
        if (isButtonDown && enabled)
            r = r.translated (0.0f, 1.0f);
        juce::Colour fill = theme (Tok::btn), edge = theme (Tok::btnb), ink = theme (Tok::txt);
        if (on)
        {
            fill = edge = theme (Tok::acc);
            ink = theme (Tok::onacc);
        }
        else if (isMouseOver && enabled)
        {
            fill = theme (Tok::hover);
        }
        juce::Path shape;
        shape.addRoundedRectangle (r.reduced (0.5f), Theme::corner);
        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillPath (shape);
        g.setColour (edge.withMultipliedAlpha (alpha));
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        const auto font = Theme::px (12.0f, true);
        const auto label = getButtonText();
        const float textW = Theme::textWidth (font, label);
        const float x = r.getCentreX() - (textW + 12.0f) * 0.5f;
        g.setColour (ink.withMultipliedAlpha (alpha));
        g.setFont (font);
        g.drawText (label, juce::Rectangle<float> (x, r.getY(), textW + 2.0f, r.getHeight()), juce::Justification::centredLeft, false);
        const float cx = x + textW + 8.5f, cy = r.getCentreY();
        juce::Path tri;
        if (open)
            tri.addTriangle (cx - 3.5f, cy + 2.0f, cx + 3.5f, cy + 2.0f, cx, cy - 2.5f);
        else
            tri.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 2.5f);
        g.fillPath (tri);
    }

    void LineSeparator::paint (juce::Graphics& g)
    {
        g.setColour (theme (Tok::line));
        g.fillRect (getWidth() / 2, (getHeight() - 22) / 2, 1, 22);
    }

    //==============================================================================================
    SegButtons::SegButtons (const Theme& t, const juce::StringArray& labels, int sel)
        : selected (sel)
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto b = std::make_unique<FlatButton> (t, labels[i]);
            b->setJoins (i > 0, i < labels.size() - 1);
            b->setToggleState (i == selected, juce::dontSendNotification);
            b->onClick = [this, i]
            {
                setSelected (i);
                if (onPick)
                    onPick (i);
            };
            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }
    }

    void SegButtons::setSelected (int index)
    {
        selected = index;
        for (int i = 0; i < (int) buttons.size(); ++i)
            buttons[(std::size_t) i]->setToggleState (i == selected, juce::dontSendNotification);
    }

    int SegButtons::preferredWidth() const
    {
        int w = 0;
        for (const auto& b : buttons)
            w += buttonWidth (*b) - 1;
        return w + 1;
    }

    void SegButtons::resized()
    {
        int x = 0;
        for (auto& b : buttons)
        {
            const int w = buttonWidth (*b);
            b->setBounds (x, 0, w, getHeight());
            x += w - 1;   // the joined edges overlap, one line between two buttons
        }
    }

    //==============================================================================================
    ToolSlider::ToolSlider (const Theme& t, const juce::String& captionText, double min, double max, double step, double v,
                            std::function<juce::String (double)> fmt)
        : theme (t), caption (captionText), lo (min), hi (max), stepSize (step), value (v), format (std::move (fmt))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void ToolSlider::setValue (double v)
    {
        if (! juce::exactlyEqual (v, value))
        {
            value = v;
            repaint();
        }
    }

    juce::String ToolSlider::getValueText() const
    {
        return format ? format (value) : juce::String (value);
    }

    float ToolSlider::captionWidth() const
    {
        return Theme::textWidth (Theme::px (10.5f, false, 0.06f), caption.toUpperCase()) + 6.0f;
    }

    float ToolSlider::valueWidth() const
    {
        // .slp-val: at least 4.2 em of 11.5 px, wider when its text is
        return std::max (48.0f, Theme::textWidth (Theme::px (11.5f), getValueText()) + 4.0f);
    }

    int ToolSlider::preferredWidth() const
    {
        return (int) std::ceil (captionWidth() + 4.0f + (float) trackWidth + 6.0f + valueWidth());
    }

    juce::Rectangle<float> ToolSlider::trackArea() const
    {
        return { captionWidth() + 4.0f, 0.0f, (float) trackWidth, (float) getHeight() };
    }

    double ToolSlider::valueAt (float x) const
    {
        // an HTML range input: the thumb's centre runs over the track less its own width, values on the steps
        const auto t = trackArea();
        const float thumb = 14.0f;
        const double f = juce::jlimit (0.0, 1.0, (double) ((x - t.getX() - thumb * 0.5f) / std::max (1.0f, t.getWidth() - thumb)));
        double v = lo + f * (hi - lo);
        if (stepSize > 0.0)
            v = lo + std::round ((v - lo) / stepSize) * stepSize;
        v = std::round (v * 1e9) / 1e9;   // the decimal the input's value text holds
        return juce::jlimit (lo, hi, v);
    }

    void ToolSlider::dragTo (float x)
    {
        dragging = true;
        setValue (valueAt (x));
        if (onInput)
            onInput (value);
    }

    void ToolSlider::release()
    {
        if (! dragging)
            return;
        dragging = false;
        if (onChange)
            onChange (value);
    }

    void ToolSlider::mouseDown (const juce::MouseEvent& e) { dragTo (e.position.x); }
    void ToolSlider::mouseDrag (const juce::MouseEvent& e) { dragTo (e.position.x); }
    void ToolSlider::mouseUp (const juce::MouseEvent&) { release(); }

    void ToolSlider::paint (juce::Graphics& g)
    {
        const float h = (float) getHeight(), cy = h * 0.5f;
        g.setColour (theme (Tok::dim));
        g.setFont (Theme::px (10.5f, false, 0.06f));
        g.drawText (caption.toUpperCase(), juce::Rectangle<float> (0.0f, 0.0f, captionWidth(), h), juce::Justification::centredLeft, false);

        // the track (filled in the accent up to the thumb) and the thumb, as input[type=range] with accent-color
        const auto t = trackArea();
        const float thumb = 14.0f;
        const float f = hi > lo ? (float) ((value - lo) / (hi - lo)) : 0.0f;
        const float tx = t.getX() + thumb * 0.5f + juce::jlimit (0.0f, 1.0f, f) * (t.getWidth() - thumb);
        juce::Path back, fill;
        back.addRoundedRectangle (juce::Rectangle<float> (t.getX() + 2.0f, cy - 2.0f, t.getWidth() - 4.0f, 4.0f), 2.0f);
        fill.addRoundedRectangle (juce::Rectangle<float> (t.getX() + 2.0f, cy - 2.0f, std::max (0.0f, tx - t.getX() - 2.0f), 4.0f), 2.0f);
        g.setColour (theme (Tok::btnb));
        g.fillPath (back);
        g.setColour (theme (Tok::acc));
        g.fillPath (fill);
        juce::Path knob;
        knob.addEllipse (tx - thumb * 0.5f, cy - thumb * 0.5f, thumb, thumb);
        g.fillPath (knob);

        g.setColour (theme (Tok::txt));
        g.setFont (Theme::px (11.5f));
        g.drawText (getValueText(), juce::Rectangle<float> (t.getRight() + 6.0f, 0.0f, valueWidth(), h), juce::Justification::centredLeft, false);
    }

    //==============================================================================================
    RatePicker::RatePicker (const Theme& t, const juce::String& captionText, core::RateId value, std::vector<int> baseList)
        : caption (t, captionText, Caption::Style::label),
          kinds (t, juce::StringArray ("Straight", "Triplet", "Dotted"), (int) value % 3),
          bases (std::move (baseList))
    {
        // sl-ui-panels.js ratePicker: the rate's base, or the first offered when its base is not among them
        const int base = (int) value / 3;
        int shown = 0;
        for (int i = 0; i < (int) bases.size(); ++i)
        {
            baseBox.addItem (kBaseNames[bases[(std::size_t) i]], i + 1);
            if (bases[(std::size_t) i] == base)
                shown = i;
        }
        baseBox.setSelectedId (shown + 1, juce::dontSendNotification);
        baseBox.setTooltip ("How often it repeats: 1 bar down to 1/64");
        baseBox.onChange = [this] { fire(); };
        kinds.onPick = [this] (int) { fire(); };
        addAndMakeVisible (caption);
        addAndMakeVisible (baseBox);
        addAndMakeVisible (kinds);
    }

    core::RateId RatePicker::getRate() const
    {
        const int i = juce::jlimit (0, (int) bases.size() - 1, baseBox.getSelectedId() - 1);
        const int base = bases.empty() ? 4 : bases[(std::size_t) i];
        return static_cast<core::RateId> (base * 3 + juce::jlimit (0, 2, kinds.getSelected()));
    }

    void RatePicker::setRate (core::RateId r)
    {
        const int base = static_cast<int> (r) / 3;
        int shown = 0;
        for (int i = 0; i < (int) bases.size(); ++i)
            if (bases[(std::size_t) i] == base)
                shown = i;
        if (baseBox.getSelectedId() != shown + 1)
            baseBox.setSelectedId (shown + 1, juce::dontSendNotification);
        if (kinds.getSelected() != static_cast<int> (r) % 3)
            kinds.setSelected (static_cast<int> (r) % 3);
    }

    void RatePicker::fire()
    {
        if (onPick)
            onPick (getRate());
    }

    int RatePicker::preferredWidth() const
    {
        return caption.preferredWidth() + 4 + kBaseBoxWidth + 4 + kinds.preferredWidth();
    }

    void RatePicker::resized()
    {
        auto r = getLocalBounds();
        caption.setBounds (r.removeFromLeft (caption.preferredWidth()));
        r.removeFromLeft (4);
        baseBox.setBounds (r.removeFromLeft (kBaseBoxWidth));
        r.removeFromLeft (4);
        kinds.setBounds (r.removeFromLeft (kinds.preferredWidth()));
    }

    //==============================================================================================
    Drawer::Drawer (const Theme& t, SelectionTools& st, RollView& rv)
        : theme (t), tools (st), roll (rv), hooks (&rv.getGhosts(), [&rv] (const juce::String& text) { rv.showHint (text); })
    {
    }

    Drawer::~Drawer() = default;

    FlatButton* Drawer::findButton (const juce::String& text) noexcept
    {
        for (auto& c : owned)
            if (auto* b = dynamic_cast<FlatButton*> (c.get()))
                if (b->getButtonText() == text)
                    return b;
        return nullptr;
    }

    void Drawer::resized()
    {
        (void) heightFor (getWidth());
    }

    int Drawer::layoutItems (const std::vector<FlowItem>& items, int width, bool place)
    {
        return flowLayout (items, { 0, 0, juce::jmax (40, width), 4000 }, kGap, kRowGap, place);
    }

    void Drawer::edited()
    {
        roll.edited();
    }

    FlatButton& Drawer::addButton (const juce::String& text, const juce::String& tip, std::function<void()> click, GhostPreview::Make ghost)
    {
        auto b = std::make_unique<FlatButton> (theme, text);
        b->setTooltip (tip);
        b->onClick = std::move (click);
        auto& ref = *b;
        addAndMakeVisible (ref);
        owned.push_back (std::move (b));
        hooks.add (ref, std::move (ghost));   // its tooltip in the hint bar, and its ghost
        return ref;
    }

    //==============================================================================================
    // Connect (Y5)

    ConnectDrawer::ConnectDrawer (const Theme& t, SelectionTools& st, RollView& rv)
        : Drawer (t, st, rv),
          caption (t, "Connect", Caption::Style::label),
          note (t, false),
          warning (t, true)
    {
        addAndMakeVisible (caption);
        buttons.push_back (&addButton ("Nearest", "Each note slides into the closest pitch among the next notes",
                                       [this] { tools.connect (model::ConnectMode::nearest); edited(); },
                                       [this] { return tools.previewConnect (model::ConnectMode::nearest); }));
        buttons.push_back (&addButton ("Furthest", "Each note slides into the widest jump among the next notes",
                                       [this] { tools.connect (model::ConnectMode::furthest); edited(); },
                                       [this] { return tools.previewConnect (model::ConnectMode::furthest); }));
        buttons.push_back (&addButton ("Random", "Each note slides into any of the next notes",
                                       [this] { tools.connect (model::ConnectMode::random); edited(); },
                                       [this] { return tools.previewConnect (model::ConnectMode::random); }));
        buttons.push_back (&addButton ("Re-roll", "A new random set of slides", [this] { tools.connectReroll(); edited(); },
                                       [this] { return tools.previewConnect (model::ConnectMode::random, true); }));
        buttons.push_back (&addButton ("Disconnect", "Remove the slides between the selected notes", [this] { tools.disconnect(); edited(); },
                                       [this] { return tools.previewDisconnect(); }));
        addAndMakeVisible (note);
        addChildComponent (warning);
        sync();
    }

    FlatButton& ConnectDrawer::getButton (const juce::String& text) noexcept
    {
        for (auto* b : buttons)
            if (b->getButtonText() == text)
                return *b;
        return *buttons.front();
    }

    void ConnectDrawer::sync()
    {
        // sl-ui-panels.js DRAWERS.connect: how many slides join the selected notes, and a warning for slides into
        // a note that starts well inside the one before it (the engine cuts that note short there)
        const auto ids = tools.ids();
        const auto clip = tools.clip();
        const int links = linksAmong (*clip, ids);
        buttons.back()->setEnabled (links > 0);
        if (ids.size() < 2)
            note.setText ("Select two or more notes: Connect joins each note to one that starts as it ends.");
        else if (links > 0)
            note.setText (juce::String (links) + (links == 1 ? " slide" : " slides") + " among the selected notes. Each one can still be curved with its "
                          + kDiamond + " handles in Build or Pitch.");
        else
            note.setText ("Slides join each note to a note that starts as it ends, up to 2 octaves away, and play on one voice.");
        const int bad = insideLinks (*clip, ids);
        warning.setText (bad > 0 ? kWarning + juce::String (bad) + (bad == 1 ? " slide goes" : " slides go")
                                       + " into a note that starts well inside the note before it: that note is cut short where the next one "
                                         "starts. Move the second note later, or Disconnect."
                                 : juce::String());
        const bool showWarning = bad > 0;
        if (warning.isVisible() != showWarning)
        {
            warning.setVisible (showWarning);
            if (onHeightChange)
                onHeightChange();
        }
        resized();
    }

    std::vector<FlowItem> ConnectDrawer::items (int width)
    {
        std::vector<FlowItem> out;
        out.push_back ({ &caption, caption.preferredWidth(), kItemHeight, true });
        for (auto* b : buttons)
            out.push_back ({ b, buttonWidth (*b), kItemHeight, false });
        out.push_back ({ &note, juce::jmin (width, note.naturalWidth()), note.heightFor (width), true });
        if (warning.isVisible())
            out.push_back ({ &warning, juce::jmin (width, warning.naturalWidth()), warning.heightFor (width), true });
        return out;
    }

    int ConnectDrawer::heightFor (int width)
    {
        const bool place = width == getWidth();
        return layoutItems (items (width), width, place);
    }

    //==============================================================================================
    // Tools (T1 to T5)

    ToolsDrawer::ToolsDrawer (const Theme& t, SelectionTools& st, RollView& rv)
        : Drawer (t, st, rv),
          laneCaption (t, {}, Caption::Style::label)
    {
        using Tool = ToolSettings::Tool;
        auto toolButton = [this] (const juce::String& text, const juce::String& tip, Tool tool)
        {
            auto& b = addButton (text, tip, [this, tool]
            {
                tools.toggleTool (tool);
                showPanel();
            });
            buttons.push_back (&b);
        };
        auto separator = [this]
        {
            auto s = std::make_unique<LineSeparator> (theme);
            addAndMakeVisible (*s);
            owned.push_back (std::move (s));
        };
        toolButton ("Walk" + kEllipsis, "Spread a value from the first selected note to the last, e.g. each note reads further into the sound", Tool::walk);
        toolButton ("Chop" + kEllipsis, "Split each selected note into repeats at a rate", Tool::chop);
        separator();
        addAndMakeVisible (laneCaption);
        buttons.push_back (&addButton ("Reverse", "Play the lane's line backwards in time", [this] { tools.reverse(); edited(); },
                                       [this] { return tools.previewLaneTool (0); }));
        buttons.push_back (&addButton ("Flip", "Turn the lane's line upside down", [this] { tools.flip(); edited(); },
                                       [this] { return tools.previewLaneTool (1); }));
        buttons.push_back (&addButton ("More", "Make the lane's line move twice as far from its middle", [this] { tools.more(); edited(); },
                                       [this] { return tools.previewLaneTool (2); }));
        buttons.push_back (&addButton ("Less", "Make the lane's line move half as far from its middle", [this] { tools.less(); edited(); },
                                       [this] { return tools.previewLaneTool (3); }));
        separator();
        {
            auto similar = std::make_unique<ArrowButton> (theme, "Select similar");
            similar->setTooltip ("Select notes like the clicked one: same pitch, same length, every other note, this bar, or flip the selection");
            similar->onClick = [this] { openSimilar(); };
            addAndMakeVisible (*similar);
            buttons.push_back (similar.get());
            hooks.add (*similar, {});
            owned.push_back (std::move (similar));
        }
        toolButton ("Humanize" + kEllipsis, "Nudge timing, velocity, Volume and Tone by small amounts (they stay the same until you Re-roll)", Tool::humanize);
        toolButton ("Strum" + kEllipsis, "Spread the starts of each chord", Tool::strum);
        buttons.push_back (&addButton ("Legato", "Stretch each note to reach the next one (ready for Connect)", [this] { tools.legato(); edited(); },
                                       [this] { return tools.previewLegato(); }));
        showPanel();
    }

    ToolsDrawer::~ToolsDrawer() = default;

    FlatButton& ToolsDrawer::getButton (const juce::String& text) noexcept
    {
        for (auto* b : buttons)
            if (b->getButtonText() == text)
                return *b;
        return *buttons.front();
    }

    void ToolsDrawer::showPanel()
    {
        // the open tool's panel under the buttons (a click on the open tool's button closes it)
        const auto want = tools.settings().tool;
        if (want != panelTool || (panel == nullptr) != (want == ToolSettings::Tool::none))
        {
            if (panel != nullptr)
                removeChildComponent (panel.get());
            panel = makeToolPanel (want, theme, tools, roll, [this] { edited(); });
            panelTool = want;
            if (panel != nullptr)
            {
                addAndMakeVisible (*panel);
                noKeyboardFocus (*panel);
            }
            if (onHeightChange)
                onHeightChange();
        }
        sync();
    }

    void ToolsDrawer::sync()
    {
        using Tool = ToolSettings::Tool;
        const auto L = tools.lane();
        laneCaption.setText (laneNameOf (L) + ":");
        const auto tool = tools.settings().tool;
        const juce::String names[] = { "Walk" + kEllipsis, "Chop" + kEllipsis, "Humanize" + kEllipsis, "Strum" + kEllipsis };
        const Tool kinds[] = { Tool::walk, Tool::chop, Tool::humanize, Tool::strum };
        for (int i = 0; i < 4; ++i)
            getButton (names[i]).setToggleState (tool == kinds[i], juce::dontSendNotification);
        // the lane tools' tooltips name the lane (and on Travel what they do to its direction)
        const bool travel = L == core::LaneId::travel;
        const auto ln = laneNameOf (L);
        getButton ("Reverse").setTooltip ("Play the " + ln + " line backwards in time");
        getButton ("Flip").setTooltip ("Turn the " + ln + " line upside down" + (travel ? " (on Speed, forwards becomes backwards)" : ""));
        getButton ("More").setTooltip ("Make the " + ln + " line move twice as far from its middle" + (travel ? " (never changes direction)" : ""));
        getButton ("Less").setTooltip ("Make the " + ln + " line move half as far from its middle" + (travel ? " (never stops a note)" : ""));
        if (panel != nullptr)
            panel->sync();
        resized();
    }

    std::vector<FlowItem> ToolsDrawer::items (int width)
    {
        std::vector<FlowItem> out;
        bool first = true;
        int sep = 0;
        for (auto& c : owned)
        {
            if (auto* s = dynamic_cast<LineSeparator*> (c.get()))
            {
                out.push_back ({ s, 5, kItemHeight, false });
                // the lane's caption follows the first line
                if (sep++ == 0)
                    out.push_back ({ &laneCaption, laneCaption.preferredWidth(), kItemHeight, false });
                continue;
            }
            auto* b = dynamic_cast<FlatButton*> (c.get());
            if (b == nullptr)
                continue;
            const int w = dynamic_cast<ArrowButton*> (b) != nullptr ? static_cast<ArrowButton*> (b)->widthWithArrow() : buttonWidth (*b);
            out.push_back ({ b, w, kItemHeight, first });
            first = false;
        }
        if (panel != nullptr)
            out.push_back ({ panel.get(), width, panel->heightFor (width), true });
        return out;
    }

    int ToolsDrawer::heightFor (int width)
    {
        const bool place = width == getWidth();
        return layoutItems (items (width), width, place);
    }

    void ToolsDrawer::openSimilar()
    {
        // sl-ui-panels.js openSimilar: a menu under the button, like the primary note
        const auto P = tools.primaryNote();
        const int bpb = tools.clip()->beatsPerBar;
        std::vector<MenuItem> items;
        std::vector<std::function<void()>> actions;
        auto item = [&items, &actions] (std::string label, bool disabled, std::function<void()> fn)
        {
            MenuItem m;
            m.label = std::move (label);
            m.disabled = disabled;
            m.action = (int) actions.size();
            actions.push_back (std::move (fn));
            items.push_back (std::move (m));
        };
        MenuItem header;
        header.kind = MenuItem::Kind::header;
        header.label = P != nullptr ? "Like " + core::noteName (P->p) + " at " + fmtPos (P->s, bpb) : std::string ("Select");
        items.push_back (header);
        const bool none = P == nullptr;
        auto pick = [this] (model::SimilarKind kind) { return [this, kind] { tools.selectSimilar (kind); edited(); }; };
        item ("Same pitch" + (P != nullptr ? " (" + core::noteName (P->p) + ")" : std::string()), none, pick (model::SimilarKind::pitch));
        item ("Same length", none, pick (model::SimilarKind::length));
        item ("Every other note", false, pick (model::SimilarKind::everyOther));
        item ("Everything in this bar", none, pick (model::SimilarKind::bar));
        item ("Flip the selection", false, pick (model::SimilarKind::invert));
        auto& button = getButton ("Select similar");
        roll.openMenuAt (button, { 0, button.getHeight() + 4 }, std::move (items), std::move (actions));
    }

    //==============================================================================================
    // The tool panels.

    ToolPanel::ToolPanel (const Theme& t, SelectionTools& st, RollView& rv, std::function<void()> e)
        : theme (t), tools (st), edited (std::move (e)), hooks (&rv.getGhosts(), [&rv] (const juce::String& text) { rv.showHint (text); })
    {
    }

    ToolPanel::~ToolPanel() = default;

    void ToolPanel::paint (juce::Graphics& g)
    {
        // .slp-sub: a dashed outline in btnb with a 7 px corner
        juce::Path box;
        box.addRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f);
        juce::Path dashes;
        const float pattern[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashes, box, pattern, 2);
        g.setColour (theme (Tok::btnb));
        g.fillPath (dashes);
    }

    void ToolPanel::resized()
    {
        (void) layoutItems (items (getWidth()), getWidth(), true);
    }

    int ToolPanel::layoutItems (const std::vector<FlowItem>& list, int width, bool place)
    {
        const auto inner = juce::Rectangle<int> (kSubPadX, kSubPadY, juce::jmax (40, width - 2 * kSubPadX), 4000);
        return flowLayout (list, inner, kSubGap, kRowGap, place) + 2 * kSubPadY;
    }

    FlatButton* ToolPanel::findButton (const juce::String& text) noexcept
    {
        for (auto& c : owned)
            if (auto* b = dynamic_cast<FlatButton*> (c.get()))
                if (b->getButtonText() == text)
                    return b;
        return nullptr;
    }

    ToolSlider* ToolPanel::findSlider (const juce::String& caption) noexcept
    {
        for (auto& c : owned)
            if (auto* s = dynamic_cast<ToolSlider*> (c.get()))
                if (s->getName() == caption)
                    return s;
        return nullptr;
    }

    namespace
    {
        /** Builds a panel's controls in order: a caption, then whatever it adds. */
        template <class T>
        T& own (std::vector<std::unique_ptr<juce::Component>>& owned, juce::Component& parent, std::unique_ptr<T> c)
        {
            auto& ref = *c;
            parent.addAndMakeVisible (ref);
            owned.push_back (std::move (c));
            return ref;
        }

        FlowItem itemFor (juce::Component& c, bool newRow = false)
        {
            if (auto* b = dynamic_cast<FlatButton*> (&c))
                return { b, buttonWidth (*b), kItemHeight, newRow };
            if (auto* s = dynamic_cast<ToolSlider*> (&c))
                return { s, s->preferredWidth(), kItemHeight, newRow };
            if (auto* r = dynamic_cast<RatePicker*> (&c))
                return { r, r->preferredWidth(), kItemHeight, newRow };
            if (auto* g = dynamic_cast<SegButtons*> (&c))
                return { g, g->preferredWidth(), kItemHeight, newRow };
            if (auto* t = dynamic_cast<Caption*> (&c))
                return { t, t->preferredWidth(), kItemHeight, newRow };
            if (auto* box = dynamic_cast<juce::ComboBox*> (&c))
                return { box, kFieldBoxWidth, kItemHeight, newRow };
            return { &c, c.getWidth(), kItemHeight, newRow };
        }

        //==========================================================================================
        /** Walk (T1): the field, First note and Last note, the Walk button (sl-ui-panels.js TOOL_PANELS.walk). */
        class WalkPanel final : public ToolPanel
        {
        public:
            WalkPanel (const Theme& t, SelectionTools& st, RollView& rv, std::function<void()> e) : ToolPanel (t, st, rv, std::move (e))
            {
                own (owned, *this, std::make_unique<Caption> (theme, "Walk", Caption::Style::label));
                field = &own (owned, *this, std::make_unique<juce::ComboBox> ("Walk field"));
                field->setTooltip ("What changes from the first note to the last");
                field->addItem ("Start in the sound", 1);
                field->addItem ("Velocity", 2);
                for (int i = 0; i < core::kNumLanes; ++i)
                {
                    const auto& info = core::kLanes[i];
                    field->addItem ((info.sub ? juce::String ("Sample: ") : juce::String()) + info.name, 3 + i);
                }
                field->onChange = [this]
                {
                    const int id = field->getSelectedId();
                    tools.setWalkField (id == 1 ? model::WalkField::start() : id == 2 ? model::WalkField::vel()
                                                : model::WalkField::onLane (static_cast<core::LaneId> (juce::jlimit (0, core::kNumLanes - 1, id - 3))));
                    sync();
                    if (edited)
                        edited();
                };
                auto fmt = [this] (double v) { return walkText (v); };
                first = &own (owned, *this, std::make_unique<ToolSlider> (theme, "First note", 0.0, 1.0, 0.005, tools.settings().walk.from, fmt));
                first->setName ("First note");
                first->onInput = [this] (double v) { tools.settings().walk.from = v; tools.walk (false, false); after(); };
                first->onChange = [this] (double v) { tools.settings().walk.from = v; tools.walk (true, false); after(); };
                last = &own (owned, *this, std::make_unique<ToolSlider> (theme, "Last note", 0.0, 1.0, 0.005, tools.settings().walk.to, fmt));
                last->setName ("Last note");
                last->onInput = [this] (double v) { tools.settings().walk.to = v; tools.walk (false, false); after(); };
                last->onChange = [this] (double v) { tools.settings().walk.to = v; tools.walk (true, false); after(); };
                auto go = std::make_unique<FlatButton> (theme, "Walk");
                go->setToggleState (true, juce::dontSendNotification);
                go->setTooltip ("Spread it across the selected notes (one undo step); then the sliders change it live");
                go->onClick = [this] { tools.walk (true, true); after(); };
                hooks.add (own (owned, *this, std::move (go)), [this] { return tools.previewWalk(); });
                note = std::make_unique<NoteText> (theme, false);
                addAndMakeVisible (*note);
                sync();
            }

            int heightFor (int width) override { return layoutItems (items (width), width, false); }

            void sync() override
            {
                const auto f = tools.settings().walk.field;
                const int id = f.kind == model::WalkField::Kind::start ? 1 : f.kind == model::WalkField::Kind::vel ? 2 : 3 + core::laneIndex (f.lane);
                field->setSelectedId (id, juce::dontSendNotification);
                first->setValue (tools.settings().walk.from);
                last->setValue (tools.settings().walk.to);
                first->repaint();
                last->repaint();
                note->setText (f.kind == model::WalkField::Kind::start ? "Each note starts a little further into the sound, so a run of notes walks through it."
                               : f.kind == model::WalkField::Kind::vel ? "Velocity goes from the first value to the last."
                                                                        : "Each note's " + laneNameOf (f.lane) + " line becomes flat at its step.");
                resized();
            }

        protected:
            std::vector<FlowItem> items (int width) override
            {
                std::vector<FlowItem> out;
                for (auto& c : owned)
                    out.push_back (itemFor (*c));
                const int inner = juce::jmax (40, width - 2 * kSubPadX);
                out.push_back ({ note.get(), juce::jmin (inner, note->naturalWidth()), note->heightFor (inner), false });
                return out;
            }

        private:
            juce::String walkText (double v) const
            {
                // sl-ui-panels.js walkText: the Start as a part of the Region and its time, the velocity, or the lane's
                // value in real units
                const auto f = tools.settings().walk.field;
                const auto& rs = tools.rollSettings();
                if (f.kind == model::WalkField::Kind::start)
                {
                    const auto& R = rs.inst.region;
                    return rounded (v * 100.0) + "% (" + fmtTime ((R.start + v * (R.end - R.start)) * rs.sampleSeconds) + ")";
                }
                if (f.kind == model::WalkField::Kind::vel)
                    return rounded (std::max (1.0, core::jsRound (v * 127.0)));
                const auto P = tools.primaryNote();
                return str (laneText (f.lane, v, model::formatCtx (rs.inst, rs.bpm, P.get())));
            }

            void after()
            {
                first->repaint();
                last->repaint();
                if (edited)
                    edited();
            }

            juce::ComboBox* field = nullptr;
            ToolSlider* first = nullptr;
            ToolSlider* last = nullptr;
        };

        //==========================================================================================
        /** Chop (T2): the rate, Same spot or Step through the part, the Chop button and what it would make. */
        class ChopPanel final : public ToolPanel
        {
        public:
            ChopPanel (const Theme& t, SelectionTools& st, RollView& rv, std::function<void()> e) : ToolPanel (t, st, rv, std::move (e))
            {
                own (owned, *this, std::make_unique<Caption> (theme, "Chop", Caption::Style::label));
                auto& rate = own (owned, *this, std::make_unique<RatePicker> (theme, "Rate", tools.settings().chop.rate, std::vector<int> { 2, 3, 4, 5, 6 }));
                rate.getBaseBox().setTooltip ("The length of each repeat");
                rate.onPick = [this] (core::RateId r) { tools.settings().chop.rate = r; sync(); };
                auto& step = own (owned, *this, std::make_unique<SegButtons> (theme, juce::StringArray ("Same spot", "Step through the part"),
                                                                              tools.settings().chop.step ? 1 : 0));
                step.getButton (0).setTooltip ("Every repeat starts where the note started");
                step.getButton (1).setTooltip ("Each repeat starts a little further into the Region");
                step.onPick = [this] (int i) { tools.settings().chop.step = i == 1; sync(); };
                hooks.add (step.getButton (0), {});
                hooks.add (step.getButton (1), {});
                auto go = std::make_unique<FlatButton> (theme, "Chop");
                go->setToggleState (true, juce::dontSendNotification);
                go->setTooltip ("Split the selected notes (one undo step)");
                go->onClick = [this]
                {
                    tools.chop();
                    if (edited)
                        edited();
                };
                button = &own (owned, *this, std::move (go));
                hooks.add (*button, [this] { return tools.previewChop(); });
                note = std::make_unique<NoteText> (theme, false);
                addAndMakeVisible (*note);
                sync();
            }

            int heightFor (int width) override { return layoutItems (items (width), width, false); }

            void sync() override
            {
                // sl-ui-panels.js TOOL_PANELS.chop: the button works unless the result would pass 1024 notes
                const auto r = tools.chopPreview();
                const int selected = (int) tools.ids().size();
                button->setEnabled (! r.refused);
                note->setWarn (r.refused);
                if (r.refused)
                    note->setText (kWarning + "That would make " + str (core::jsNumberToString (r.count)) + " notes; the limit is "
                                   + juce::String (model::kMaxChopNotes) + ". Pick a slower rate or fewer notes.");
                else
                    note->setText ("The " + str (nNotes (selected)) + " become "
                                   + str (core::jsNumberToString (r.count - (double) (tools.clip()->notes.size() - (std::size_t) selected)))
                                   + " pieces. Each piece after the first starts its sound again.");
                resized();
            }

        protected:
            std::vector<FlowItem> items (int width) override
            {
                std::vector<FlowItem> out;
                for (auto& c : owned)
                    out.push_back (itemFor (*c));
                const int inner = juce::jmax (40, width - 2 * kSubPadX);
                out.push_back ({ note.get(), juce::jmin (inner, note->naturalWidth()), note->heightFor (inner), false });
                return out;
            }

        private:
            FlatButton* button = nullptr;
        };

        //==========================================================================================
        /** Humanize (T4): Timing, Velocity, Volume and Tone, the Humanize and Re-roll buttons. */
        class HumanizePanel final : public ToolPanel
        {
        public:
            HumanizePanel (const Theme& t, SelectionTools& st, RollView& rv, std::function<void()> e) : ToolPanel (t, st, rv, std::move (e))
            {
                own (owned, *this, std::make_unique<Caption> (theme, "Humanize", Caption::Style::label));
                auto& H = tools.settings().hum;
                auto add = [this] (const juce::String& name, double max, double value, std::function<juce::String (double)> fmt, double ToolSettings::Humanize::* field)
                {
                    auto& s = own (owned, *this, std::make_unique<ToolSlider> (theme, name, 0.0, max, 0.005, value, std::move (fmt)));
                    s.setName (name);
                    s.onInput = [this, field] (double v) { tools.settings().hum.*field = v; tools.humanize (false, false); after(); };
                    s.onChange = [this, field] (double v) { tools.settings().hum.*field = v; tools.humanize (true, false); after(); };
                    sliders.push_back (&s);
                };
                add ("Timing", 0.25, H.time, [this] (double v) { return kPlusMinus + rounded (v * 60000.0 / tools.rollSettings().bpm) + " ms"; },
                     &ToolSettings::Humanize::time);
                add ("Velocity", 0.4, H.vel, [] (double v) { return kPlusMinus + rounded (v * 127.0); }, &ToolSettings::Humanize::vel);
                add ("Volume and Tone", 0.25, H.lanes, [] (double v) { return kPlusMinus + rounded (v * 100.0) + "%"; },
                     &ToolSettings::Humanize::lanes);
                auto go = std::make_unique<FlatButton> (theme, "Humanize");
                go->setToggleState (true, juce::dontSendNotification);
                go->setTooltip ("Nudge the selected notes (one undo step); then the sliders change it live");
                go->onClick = [this] { tools.humanize (true, true); after(); };
                hooks.add (own (owned, *this, std::move (go)), [this] { return tools.previewHumanize(); });
                auto reroll = std::make_unique<FlatButton> (theme, "Re-roll");
                reroll->setTooltip ("A different set of nudges (one undo step)");
                reroll->onClick = [this] { tools.humanizeReroll(); after(); };
                hooks.add (own (owned, *this, std::move (reroll)), {});
                note = std::make_unique<NoteText> (theme, false);
                note->setText ("The nudges stay the same every time you play, until you Re-roll.");
                addAndMakeVisible (*note);
            }

            int heightFor (int width) override { return layoutItems (items (width), width, false); }

            void sync() override
            {
                const auto& H = tools.settings().hum;
                sliders[0]->setValue (H.time);
                sliders[1]->setValue (H.vel);
                sliders[2]->setValue (H.lanes);
                for (auto* s : sliders)
                    s->repaint();   // Timing's text follows the tempo
                resized();
            }

        protected:
            std::vector<FlowItem> items (int width) override
            {
                std::vector<FlowItem> out;
                for (auto& c : owned)
                    out.push_back (itemFor (*c));
                const int inner = juce::jmax (40, width - 2 * kSubPadX);
                out.push_back ({ note.get(), juce::jmin (inner, note->naturalWidth()), note->heightFor (inner), false });
                return out;
            }

        private:
            void after()
            {
                for (auto* s : sliders)
                    s->repaint();
                if (edited)
                    edited();
            }

            std::vector<ToolSlider*> sliders;
        };

        //==========================================================================================
        /** Strum (T5): the gap, Strum up and Strum down. */
        class StrumPanel final : public ToolPanel
        {
        public:
            StrumPanel (const Theme& t, SelectionTools& st, RollView& rv, std::function<void()> e) : ToolPanel (t, st, rv, std::move (e))
            {
                own (owned, *this, std::make_unique<Caption> (theme, "Strum", Caption::Style::label));
                auto& gap = own (owned, *this, std::make_unique<RatePicker> (theme, "Gap", tools.settings().strumRate, std::vector<int> { 3, 4, 5, 6 }));
                gap.getBaseBox().setTooltip ("The gap between the notes of a chord");
                gap.onPick = [this] (core::RateId r) { tools.settings().strumRate = r; };
                for (const bool up : { true, false })
                {
                    const juce::String label = up ? "Strum up" : "Strum down";
                    auto b = std::make_unique<FlatButton> (theme, label);
                    b->setTooltip (label + ": each note of a chord starts a little after the one before (every note keeps its end)");
                    b->onClick = [this, up]
                    {
                        tools.strum (up ? model::StrumDir::up : model::StrumDir::down);
                        if (edited)
                            edited();
                    };
                    hooks.add (own (owned, *this, std::move (b)),
                               [this, up] { return tools.previewStrum (up ? model::StrumDir::up : model::StrumDir::down); });
                }
                note = std::make_unique<NoteText> (theme, false);
                note->setText ("Up starts from the lowest note, Down from the highest. Notes that start together count as a chord.");
                addAndMakeVisible (*note);
            }

            int heightFor (int width) override { return layoutItems (items (width), width, false); }
            void sync() override { resized(); }

        protected:
            std::vector<FlowItem> items (int width) override
            {
                std::vector<FlowItem> out;
                for (auto& c : owned)
                    out.push_back (itemFor (*c));
                const int inner = juce::jmax (40, width - 2 * kSubPadX);
                out.push_back ({ note.get(), juce::jmin (inner, note->naturalWidth()), note->heightFor (inner), false });
                return out;
            }
        };
    }

    std::unique_ptr<ToolPanel> makeToolPanel (ToolSettings::Tool tool, const Theme& t, SelectionTools& st, RollView& rv, std::function<void()> edited)
    {
        switch (tool)
        {
            case ToolSettings::Tool::walk:     return std::make_unique<WalkPanel> (t, st, rv, std::move (edited));
            case ToolSettings::Tool::chop:     return std::make_unique<ChopPanel> (t, st, rv, std::move (edited));
            case ToolSettings::Tool::humanize: return std::make_unique<HumanizePanel> (t, st, rv, std::move (edited));
            case ToolSettings::Tool::strum:    return std::make_unique<StrumPanel> (t, st, rv, std::move (edited));
            case ToolSettings::Tool::none:     break;
        }
        return nullptr;
    }

    //==============================================================================================
    // Shapes (Y2)

    ShapesDrawer::ShapesDrawer (const Theme& t, SelectionTools& st, RollView& rv)
        : Drawer (t, st, rv),
          lane (st.lane()),
          caption (t, "Shape " + laneNameOf (lane), Caption::Style::label),
          scopeButtons (t, juce::StringArray ("Each note", "Across"), st.scope() == model::ShapeScope::across ? 1 : 0),
          note (t, false)
    {
        // sl-ui-panels.js DRAWERS.shapes: "Shape Tone", Each note / Across (shared by every shape tool), the shapes
        addAndMakeVisible (caption);
        scopeButtons.getButton (0).setTooltip ("The shape fits every selected note");
        scopeButtons.getButton (1).setTooltip ("One long shape across the whole selection (each note gets its slice)");
        scopeButtons.onPick = [this] (int i)
        {
            roll.setScope (i == 1 ? model::ShapeScope::across : model::ShapeScope::each);
            sync();
        };
        addAndMakeVisible (scopeButtons);
        hooks.add (scopeButtons.getButton (0), {});
        hooks.add (scopeButtons.getButton (1), {});
        keys = tools.shapeKeys (lane);
        for (const auto k : keys)
            shapeButtons.push_back (&addButton (model::shapeName (k), shapeTitle (k), [this, k] { tools.applyShape (k); edited(); },
                                                [this, k] { return tools.previewShape (k); }));
        addChildComponent (note);
        sync();
    }

    FlatButton* ShapesDrawer::findShape (model::ShapeKey key) noexcept
    {
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key)
                return shapeButtons[i];
        return nullptr;
    }

    void ShapesDrawer::sync()
    {
        scopeButtons.setSelected (tools.scope() == model::ShapeScope::across ? 1 : 0);
        // on Travel: what the line means for these notes
        juce::String text;
        if (lane == core::LaneId::travel)
        {
            const auto kind = tools.travelKind();
            if (! kind)
                text = "Some notes use Speed, some Path: each shape changes only the notes of its kind.";
            else if (*kind == model::TravelKind::path)
                text = "These notes use Path: the line is where in the Region each note is.";
            else
                text = "These notes use Speed: above the middle line plays forwards, below plays backwards.";
        }
        note.setText (text);
        const bool show = text.isNotEmpty();
        if (note.isVisible() != show)
        {
            note.setVisible (show);
            if (onHeightChange)
                onHeightChange();
        }
        resized();
    }

    std::vector<FlowItem> ShapesDrawer::items (int width)
    {
        std::vector<FlowItem> out;
        out.push_back ({ &caption, caption.preferredWidth(), kItemHeight, true });
        out.push_back ({ &scopeButtons, scopeButtons.preferredWidth(), kItemHeight, false });
        bool first = true;
        for (auto* b : shapeButtons)
        {
            out.push_back ({ b, buttonWidth (*b), kItemHeight, first });
            first = false;
        }
        if (note.isVisible())
            out.push_back ({ &note, juce::jmin (width, note.naturalWidth()), note.heightFor (width), true });
        return out;
    }

    int ShapesDrawer::heightFor (int width)
    {
        const bool place = width == getWidth();
        return layoutItems (items (width), width, place);
    }

    //==============================================================================================
    // Pattern (Y3) and Random (Y4)

    GenDrawer::GenDrawer (const Theme& t, SelectionTools& st, RollView& rv, model::GenKind k)
        : Drawer (t, st, rv),
          kind (k),
          lane (st.lane()),
          caption (t, (k == model::GenKind::pattern ? "Pattern on " : "Random on ") + laneNameOf (lane), Caption::Style::label),
          note (t, false)
    {
        const bool pattern = kind == model::GenKind::pattern;
        const auto& S = tools.settings();
        const auto ln = laneNameOf (lane);
        addAndMakeVisible (caption);

        // Pattern: the shape chips, each with the ghost of the pattern in that shape
        if (pattern)
        {
            juce::StringArray names;
            for (int i = 0; i < (int) std::size (model::kGenShapeCodes); ++i)
                names.add (genShapeName (static_cast<model::GenShape> (i)));
            auto seg = std::make_unique<SegButtons> (theme, names, static_cast<int> (S.pat.shape));
            for (int i = 0; i < names.size(); ++i)
            {
                const auto shape = static_cast<model::GenShape> (i);
                hooks.add (seg->getButton (i), [this, shape] { return tools.previewGen (model::GenKind::pattern, shape); });
            }
            seg->onPick = [this] (int i)
            {
                tools.settings().pat.shape = static_cast<model::GenShape> (i);
                changed (true, "shape");
            };
            shapes = seg.get();
            addAndMakeVisible (*seg);
            owned.push_back (std::move (seg));
        }

        // the rate (1 bar .. 1/64, straight, triplet or dotted)
        {
            auto rp = std::make_unique<RatePicker> (theme, "Rate", pattern ? S.pat.rate : S.rnd.rate, std::vector<int> { 0, 1, 2, 3, 4, 5, 6 });
            rp->getBaseBox().setTooltip (pattern ? "How often it repeats: 1 bar down to 1/64" : "A new random value starts on this grid");
            rp->onPick = [this] (core::RateId r)
            {
                auto& set = tools.settings();
                if (kind == model::GenKind::pattern)
                    set.pat.rate = r;
                else
                    set.rnd.rate = r;
                changed (true, "rate");
            };
            rate = rp.get();
            addAndMakeVisible (*rp);
            owned.push_back (std::move (rp));
        }

        const core::LaneId L = lane;
        auto depthFmt = [L] (double v) { return str (depthText (L, v)); };
        if (pattern)
        {
            const juce::String tip = juce::String ("How far the pattern swings") + (L == core::LaneId::pitch ? " (full = 2 semitones each way)" : "");
            addSlider ("Depth at start", 0.0, 1.0, 0.01, S.pat.d0, depthFmt, tip, [this] (double v, bool final)
            {
                tools.settings().pat.d0 = v;
                changed (final, "depth");
            });
            addSlider ("Depth at end", 0.0, 1.0, 0.01, S.pat.d1, depthFmt, tip, [this] (double v, bool final)
            {
                tools.settings().pat.d1 = v;
                changed (final, "depth");
            });
            warp = &addSlider ("Speed up / slow down", -0.9, 3.0, 0.05, S.pat.warp, [] (double v) { return str (warpText (v)); },
                               "The pattern speeds up or slows down across each note (only with Lock to the beat off)", [this] (double v, bool final)
            {
                tools.settings().pat.warp = v;
                changed (final, "speed-up");
            });
        }
        else
        {
            addSlider ("Amount", 0.0, 1.0, 0.01, S.rnd.amount, depthFmt, "How far the random may move", [this] (double v, bool final)
            {
                tools.settings().rnd.amount = v;
                changed (final, "amount");
            });
            addSlider ("Smooth", 0.0, 1.0, 0.01, S.rnd.smooth, [] (double v) { return str (smoothText (v)); },
                       "Left: jumps in steps. Right: moves smoothly from one value to the next.", [this] (double v, bool final)
            {
                tools.settings().rnd.smooth = v;
                changed (final, "smooth");
            });
        }

        // Same or Different per note (Pattern: for a random shape only)
        {
            const auto now = pattern ? S.pat.perNote : S.rnd.perNote;
            auto seg = std::make_unique<SegButtons> (theme, juce::StringArray ("Same for every note", "Different per note"), now == model::PerNote::different ? 1 : 0);
            if (! pattern)
            {
                seg->getButton (0).setTooltip ("A chord moves together");
                seg->getButton (1).setTooltip ("Every note gets its own random line");
            }
            hooks.add (seg->getButton (0), {});
            hooks.add (seg->getButton (1), {});
            seg->onPick = [this] (int i)
            {
                auto& set = tools.settings();
                const auto v = i == 1 ? model::PerNote::different : model::PerNote::same;
                if (kind == model::GenKind::pattern)
                    set.pat.perNote = v;
                else
                    set.rnd.perNote = v;
                changed (true, "per note");
            };
            perNote = seg.get();
            addChildComponent (*seg);
            owned.push_back (std::move (seg));
        }

        // Lock to the beat: a switch, lit while on
        lock = &addButton ("Lock to the beat",
                           pattern ? "On: every note's pattern stays in step with the bar. Off: it starts again at each note."
                                   : "On: the random steps sit on the song grid. Off: they count from each note's start.",
                           [this]
                           {
                               auto& set = tools.settings();
                               bool& on = kind == model::GenKind::pattern ? set.pat.lockToBeat : set.rnd.lockToBeat;
                               on = ! on;
                               changed (true, on ? "locked to the beat" : "unlocked");
                           });

        apply = &addButton ("Apply", {}, [this]
        {
            tools.applyGen (kind);
            sync();
            edited();
        }, [this] { return tools.previewGen (kind); });
        reroll = &addButton ("Re-roll",
                             pattern ? "New random values (they stay the same until you re-roll again)"
                                     : "New random values (they stay the same on every loop until you re-roll again)",
                             [this]
                             {
                                 tools.rerollGen();
                                 edited();
                             },
                             [this] { return tools.previewReroll(); });
        removeButton = &addButton ("Remove", "Take the pattern or random off the " + ln + " lane (your drawing stays)", [this]
        {
            tools.removeGen();
            edited();
        }, [this] { return tools.previewRemove(); });
        bakeButton = &addButton ("Turn into nodes", "Turn it into ordinary nodes you can move by hand", [this]
        {
            tools.bake();
            edited();
        }, [this] { return tools.previewBake(); });
        addAndMakeVisible (note);

        // Pattern's per-note choice and Re-roll show for a random shape only
        shownRandomShape = ! pattern || isRandomShape (S.pat.shape);
        perNote->setVisible (shownRandomShape);
        reroll->setVisible (shownRandomShape);
        sync();
    }

    ToolSlider& GenDrawer::addSlider (const juce::String& captionText, double min, double max, double step, double value,
                                      std::function<juce::String (double)> fmt, const juce::String& tip, std::function<void (double, bool)> moved)
    {
        auto slider = std::make_unique<ToolSlider> (theme, captionText, min, max, step, value, std::move (fmt));
        slider->setName (captionText);
        slider->onInput = [moved] (double v) { moved (v, false); };
        slider->onChange = [moved] (double v) { moved (v, true); };
        auto& ref = *slider;
        addAndMakeVisible (ref);
        owned.push_back (std::move (slider));
        hooks.add (ref, {}, [tip] { return tip; });
        sliders.push_back (&ref);
        return ref;
    }

    ToolSlider* GenDrawer::findSlider (const juce::String& captionText) noexcept
    {
        for (auto* s : sliders)
            if (s->getName() == captionText)
                return s;
        return nullptr;
    }

    void GenDrawer::changed (bool final, const std::string& what)
    {
        // sl-ui-panels.js genChanged: live once applied, else a ghost of Apply's result
        tools.genChanged (kind, final, what);
        sync();
        edited();
    }

    void GenDrawer::sync()
    {
        const bool pattern = kind == model::GenKind::pattern;
        const auto& S = tools.settings();
        const bool editing = tools.isEditing (kind);
        const int k = (int) tools.ids().size();
        const auto ln = laneNameOf (lane);
        if (pattern)
        {
            shapes->setSelected (static_cast<int> (S.pat.shape));
            rate->setRate (S.pat.rate);
            sliders[0]->setValue (S.pat.d0);
            sliders[1]->setValue (S.pat.d1);
            warp->setValue (S.pat.warp);
            warp->setAlpha (S.pat.lockToBeat ? 0.5f : 1.0f);   // .slp-faded: the speed-up works only unlocked
            lock->setToggleState (S.pat.lockToBeat, juce::dontSendNotification);
            perNote->setSelected (S.pat.perNote == model::PerNote::different ? 1 : 0);
            const bool rnd = isRandomShape (S.pat.shape);
            if (rnd != shownRandomShape)
            {
                shownRandomShape = rnd;
                perNote->setVisible (rnd);
                reroll->setVisible (rnd);
                if (onHeightChange)
                    onHeightChange();
            }
            reroll->setEnabled (tools.anyGen (lane, true));
        }
        else
        {
            rate->setRate (S.rnd.rate);
            sliders[0]->setValue (S.rnd.amount);
            sliders[1]->setValue (S.rnd.smooth);
            lock->setToggleState (S.rnd.lockToBeat, juce::dontSendNotification);
            perNote->setSelected (S.rnd.perNote == model::PerNote::different ? 1 : 0);
            reroll->setEnabled (tools.anyGen (lane));
        }
        const bool has = tools.anyGen (lane);
        removeButton->setEnabled (has);
        bakeButton->setEnabled (has);
        // Apply is lit until the notes have this generator; from then on the controls change it live
        apply->setToggleState (! editing, juce::dontSendNotification);
        apply->setTooltip ("Put this " + juce::String (pattern ? "pattern" : "random") + " on the " + ln + " lane of the " + str (nNotes (k))
                           + " (one undo step)");
        const juce::String word = pattern ? "pattern" : "random";
        note.setText (editing ? "Moving these changes the " + word + " on the " + str (nNotes (k)) + " straight away: each change is one undo step."
                      : pattern ? juce::String ("Choose the settings (the dashed line shows the result), then Apply.")
                                : juce::String ("Choose the settings (the dashed line shows the result), then Apply. Random plays the same on every loop "
                                                "until you Re-roll."));
        for (auto* s : sliders)
            s->repaint();
        resized();
    }

    std::vector<FlowItem> GenDrawer::items (int width)
    {
        std::vector<FlowItem> out;
        auto add = [&out] (juce::Component* c, int w, bool newRow) { out.push_back ({ c, w, kItemHeight, newRow }); };
        auto slider = [&add] (ToolSlider* s, bool newRow) { add (s, s->preferredWidth(), newRow); };
        add (&caption, caption.preferredWidth(), true);
        if (kind == model::GenKind::pattern)
        {
            // Pattern on Tone [shapes] / Rate, Depth at start, Depth at end / Lock to the beat, Speed up, [per note],
            // Apply, [Re-roll], Remove, Turn into nodes
            add (shapes, shapes->preferredWidth(), false);
            add (rate, rate->preferredWidth(), true);
            slider (sliders[0], false);
            slider (sliders[1], false);
            add (lock, buttonWidth (*lock), true);
            slider (warp, false);
            if (perNote->isVisible())
                add (perNote, perNote->preferredWidth(), false);
            add (apply, buttonWidth (*apply), false);
            if (reroll->isVisible())
                add (reroll, buttonWidth (*reroll), false);
        }
        else
        {
            // Random on Tone, Rate / Amount, Smooth, per note, Lock to the beat / Apply, Re-roll, Remove, Turn into nodes
            add (rate, rate->preferredWidth(), false);
            slider (sliders[0], true);
            slider (sliders[1], false);
            add (perNote, perNote->preferredWidth(), false);
            add (lock, buttonWidth (*lock), false);
            add (apply, buttonWidth (*apply), true);
            add (reroll, buttonWidth (*reroll), false);
        }
        add (removeButton, buttonWidth (*removeButton), false);
        add (bakeButton, buttonWidth (*bakeButton), false);
        out.push_back ({ &note, juce::jmin (width, note.naturalWidth()), note.heightFor (width), true });
        return out;
    }

    int GenDrawer::heightFor (int width)
    {
        const bool place = width == getWidth();
        return layoutItems (items (width), width, place);
    }
}
