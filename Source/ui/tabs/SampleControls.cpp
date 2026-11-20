#include "SampleControls.h"

#include "params/ParameterLayout.h"
#include "ui/StrandLookAndFeel.h"

#include <cmath>

namespace sl::ui
{
    namespace
    {
        constexpr float kFadedAlpha = 0.42f;        // sl-ui-sample.css .sls-faded
        constexpr float kFadedHoverAlpha = 0.6f;    // .sls-faded:hover
        constexpr int kLabelHeight = 16;            // .sls-klbl min-height
        constexpr int kDialSize = 36;               // .sls-dial
        constexpr int kValueHeight = 16;            // .sls-kval
        constexpr int kMinWidth = 64, kMaxWidth = 96;   // .sls-knob min-width, max-width

        // the dial (sl-ui-sample.js knob): a 40-unit square, the track from 135 to 405 degrees (SVG, clockwise from three
        // o'clock) of radius 15, a cap of radius 10.5, a dot at radius 7, the bipolar mark at radius 18.4 at the top
        constexpr float kA0 = 135.0f, kSweep = 270.0f;

        juce::Font labelFont() { return Theme::px (10.5f, false, 0.05f); }
        juce::Font valueFont() { return Theme::px (11.0f); }

        Tok tokOf (rules::KnobColour c) noexcept
        {
            switch (c)
            {
                case rules::KnobColour::sample:  return Tok::laneSample;
                case rules::KnobColour::pitch:   return Tok::lanePitch;
                case rules::KnobColour::amp:     return Tok::laneAmp;
                case rules::KnobColour::vib:     return Tok::laneVib;
                case rules::KnobColour::tone:    return Tok::laneTone;
                case rules::KnobColour::formant: return Tok::laneFormant;
                case rules::KnobColour::hollow:  return Tok::laneHollow;
            }
            return Tok::laneSample;
        }

        /** An SVG angle (degrees clockwise from three o'clock) as JUCE's arc angle (radians clockwise from twelve). */
        float arcAngle (float svgDegrees) noexcept
        {
            return (svgDegrees + 90.0f) * juce::MathConstants<float>::pi / 180.0f;
        }

        juce::Point<float> pointAt (juce::Point<float> centre, float svgDegrees, float radius) noexcept
        {
            const float a = svgDegrees * juce::MathConstants<float>::pi / 180.0f;
            return { centre.x + radius * std::cos (a), centre.y + radius * std::sin (a) };
        }

        /** A sample readout per format, for the knob's width (the prototype's knob grows with its text up to 96 px). */
        juce::String widestText (const rules::KnobSpec& s)
        {
            switch (s.format)
            {
                case rules::KnobFormat::x:         return "0.125x";
                case rules::KnobFormat::heads:     return "4.55 heads";
                case rules::KnobFormat::pct:       return "100%";
                case rules::KnobFormat::cycles:    return "0.25 cycles";
                case rules::KnobFormat::cloudMs:   return "2000 ms";
                case rules::KnobFormat::lockWaves: return "12 waves";
                case rules::KnobFormat::st:        return "-12 semitones";
                case rules::KnobFormat::ms:        return "1.25 s";
                case rules::KnobFormat::macro:
                case rules::KnobFormat::offset:    return "+2.1 oct brighter";
            }
            return {};
        }

        juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
    }

    //==============================================================================================
    HoverWatch::HoverWatch (juce::Component& c, std::function<void (bool)> changed)
        : target (&c), onChange (std::move (changed))
    {
        c.addMouseListener (this, true);
    }

    HoverWatch::~HoverWatch()
    {
        if (auto* c = target.getComponent())
            c->removeMouseListener (this);
    }

    void HoverWatch::update()
    {
        // moving onto a child the parent hears its exit with the child already under the mouse: still over
        auto* c = target.getComponent();
        const bool now = c != nullptr && c->isMouseOver (true);
        if (now == over)
            return;
        over = now;
        if (onChange)
            onChange (now);
    }

    //==============================================================================================
    /** The value under the dial: its readout, and a box to type one when the knob takes typing (sl-ui-sample.js: click
        the value to type one; Enter or a click elsewhere keeps it, Escape puts the old one back). */
    class SampleKnob::ValueLabel final : public juce::Label
    {
    public:
        explicit ValueLabel (SampleKnob& k) : owner (k)
        {
            setFont (valueFont());
            setJustificationType (juce::Justification::centred);
            setBorderSize (juce::BorderSize<int> (0, 3, 0, 3));
            setMinimumHorizontalScale (0.75f);
            applyColours();
        }

        void applyColours()
        {
            const auto& th = owner.theme;
            const auto clear = juce::Colours::transparentBlack;
            setColour (juce::Label::textColourId, th (Tok::txt));
            setColour (juce::Label::backgroundColourId, clear);
            setColour (juce::Label::outlineColourId, clear);
            setColour (juce::Label::textWhenEditingColourId, th (Tok::txt));
            setColour (juce::Label::backgroundWhenEditingColourId, th (Tok::btn));
            setColour (juce::Label::outlineWhenEditingColourId, th (Tok::acc));
            setColour (juce::TextEditor::textColourId, th (Tok::txt));
            setColour (juce::TextEditor::backgroundColourId, th (Tok::btn));
            setColour (juce::TextEditor::outlineColourId, th (Tok::acc));
            setColour (juce::TextEditor::focusedOutlineColourId, th (Tok::acc));
            setColour (juce::TextEditor::highlightColourId, th (Tok::acc).withAlpha (0.35f));
            setColour (juce::TextEditor::highlightedTextColourId, th (Tok::txt));
            setColour (juce::CaretComponent::caretColourId, th (Tok::txt));
        }

        void paint (juce::Graphics& g) override
        {
            // .sls-kval:hover: the hover colour behind the value of a knob that takes typing
            if (isEditableOnSingleClick() && isMouseOver (true) && ! isBeingEdited())
            {
                const float w = juce::jmin ((float) getWidth(), Theme::textWidth (valueFont(), getText()) + 8.0f);
                g.setColour (owner.theme (Tok::hover));
                g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (w, (float) getHeight()), 4.0f);
            }
            juce::Label::paint (g);
        }

        void mouseEnter (const juce::MouseEvent& e) override { juce::Label::mouseEnter (e); repaint(); }
        void mouseExit (const juce::MouseEvent& e) override  { juce::Label::mouseExit (e); repaint(); }

    protected:
        void editorShown (juce::TextEditor* ed) override
        {
            if (ed == nullptr)
                return;
            ed->setJustification (juce::Justification::centred);
            ed->setIndents (2, 0);
            ed->setSelectAllWhenFocused (true);
            ed->selectAll();
            owner.repaint();
        }

        void editorAboutToBeHidden (juce::TextEditor*) override { owner.repaint(); }

        void textWasEdited() override { owner.typed (getText()); }

        void lookAndFeelChanged() override
        {
            juce::Label::lookAndFeelChanged();
            applyColours();
        }

    private:
        SampleKnob& owner;
    };

    //==============================================================================================
    SampleKnob::SampleKnob (const Theme& t, juce::AudioProcessorValueTreeState& s, rules::KnobId knobId, TabActions& a)
        : theme (t), state (s), id (knobId), actions (a), spec (rules::knobSpec (knobId))
    {
        for (const auto p : rules::knobParams (id))
            bindings[params::index (p)] = std::make_unique<ParamBinding> (*state.getParameter (params::spec (p).id));

        label = std::make_unique<ValueLabel> (*this);
        addAndMakeVisible (*label);
        hover = std::make_unique<HoverWatch> (*this, [this] (bool over) { hoverChanged (over); });
        setTitle (spec.label);
        setSpec (spec, nullptr);
        shown = value();
        label->setText (readout(), juce::dontSendNotification);
    }

    SampleKnob::~SampleKnob()
    {
        if (label != nullptr && label->isBeingEdited())
            label->hideEditor (true);
        hover.reset();
    }

    ParamBinding& SampleKnob::binding() const
    {
        auto it = bindings.find (params::index (spec.param));
        if (it == bindings.end())
            it = bindings.begin();   // every spec of a knob names one of its own parameters
        return *it->second;
    }

    void SampleKnob::setSpec (const rules::KnobSpec& s, const rules::Fade* f)
    {
        const bool paramChanged = s.param != spec.param || s.off != spec.off;
        spec = s;
        fade = f != nullptr ? std::optional<rules::Fade> (*f) : std::nullopt;
        faded = spec.off || (fade.has_value() && fade->faded);
        const bool editable = ! spec.off && spec.parse != rules::KnobParse::none;
        if (editable != label->isEditableOnSingleClick())
        {
            label->setEditable (editable, false, false);
            // a label that can be edited wants the keyboard (juce::Label::setEditable); at rest no control may (the
            // editor's focus rules): the box it opens takes the keyboard only while it is typed in
            label->setWantsKeyboardFocus (false);
            label->setMouseCursor (editable ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
        }
        setMouseCursor (spec.off ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
        const auto title = titleText();
        if (getTooltip() != title)
        {
            setTooltip (title);
            label->setTooltip (title);
        }
        if (paramChanged && ! drag.has_value() && ! isTyping())
        {
            shown = value();
            label->setText (readout(), juce::dontSendNotification);
        }
        refreshLook();
        repaint();
    }

    double SampleKnob::value() const
    {
        const float p = binding().plain();
        if (exactParam == params::index (spec.param) && juce::exactlyEqual ((float) exact, p))
            return exact;
        return (double) p;
    }

    juce::String SampleKnob::readout() const
    {
        if (spec.off)
            return juce::String::fromUTF8 ("\xe2\x80\x94");   // the em dash
        return str (rules::knobText (spec, value()));
    }

    juce::String SampleKnob::hintText() const
    {
        return str (rules::knobHint (spec, fade.has_value() ? &*fade : nullptr));
    }

    juce::String SampleKnob::titleText() const
    {
        return str (rules::knobTitle (spec, fade.has_value() ? &*fade : nullptr));
    }

    bool SampleKnob::syncToParameter()
    {
        // the editor's 30 Hz timer: the parameter's value, unless the knob is being dragged or typed in
        if (drag.has_value() || isTyping())
            return false;
        const double v = value();
        const auto text = readout();
        if (juce::exactlyEqual (v, shown) && text == label->getText())
            return false;
        shown = v;
        label->setText (text, juce::dontSendNotification);
        repaint();
        return true;
    }

    void SampleKnob::refreshLook()
    {
        const float alpha = faded ? (hovering ? kFadedHoverAlpha : kFadedAlpha) : 1.0f;
        if (! juce::exactlyEqual (getAlpha(), alpha))
            setAlpha (alpha);
    }

    void SampleKnob::hoverChanged (bool over)
    {
        hovering = over;
        refreshLook();
        if (actions.hint)
            actions.hint (over ? hintText() : juce::String());
    }

    void SampleKnob::write (double v, bool inGesture)
    {
        auto& b = binding();
        exact = v;
        exactParam = params::index (spec.param);
        if (inGesture)
            b.setPlain ((float) v);
        else
            b.setAsCompleteGesture ((float) v);
        shown = value();
        label->setText (readout(), juce::dontSendNotification);
        repaint();
    }

    //==============================================================================================
    void SampleKnob::press (double y)
    {
        if (spec.off)
        {
            if (actions.toast)
                actions.toast (spec.offWhy, 3500);   // sl-ui-sample.js: the dial says why it does nothing
            return;
        }
        drag = rules::beginDrag (spec, y, value());
        repaint();
    }

    void SampleKnob::dragTo (double y, bool fine)
    {
        if (! drag.has_value())
            return;
        const auto v = rules::dragTo (spec, *drag, y, fine);
        if (! v)
            return;
        binding().beginGesture();   // the first move opens the host's gesture (a second call does nothing)
        write (*v, true);
        if (onChange)
            onChange();
    }

    void SampleKnob::release()
    {
        if (! drag.has_value())
            return;
        const auto d = *drag;
        drag.reset();
        for (auto& entry : bindings)
            entry.second->endGesture();
        if (rules::dragCommits (d) && actions.commit)
            actions.commit (spec.undo, { spec.param });
        repaint();
    }

    void SampleKnob::doubleClick()
    {
        if (spec.off || juce::exactlyEqual (spec.range.def, value()))
            return;
        write (spec.range.def, false);
        if (actions.commit)
            actions.commit (std::string ("Reset ") + spec.undo, { spec.param });
        if (onChange)
            onChange();
    }

    void SampleKnob::typed (const juce::String& text)
    {
        const auto parsed = rules::knobParse (spec, text.toStdString());
        if (! parsed || ! std::isfinite (*parsed))
        {
            if (actions.toast)
                actions.toast (str (rules::typingHelp (spec)), 2400);
            label->setText (readout(), juce::dontSendNotification);
            return;
        }
        const auto& ps = params::spec (spec.param);
        const double v = juce::jlimit ((double) ps.min, (double) ps.max, rules::typedValue (spec, *parsed));   // normalizeInst's clamp
        if (juce::exactlyEqual (v, value()))
        {
            label->setText (readout(), juce::dontSendNotification);
            return;
        }
        write (v, false);
        if (actions.commit)
            actions.commit (spec.undo, { spec.param });
        if (onChange)
            onChange();
    }

    bool SampleKnob::type (const juce::String& text)
    {
        if (! label->isEditableOnSingleClick())
            return false;
        label->showEditor();
        if (auto* ed = label->getCurrentTextEditor())
            ed->setText (text, false);
        label->hideEditor (false);
        return true;
    }

    void SampleKnob::beginTyping()
    {
        if (label->isEditableOnSingleClick())
            label->showEditor();
    }

    bool SampleKnob::isTyping() const
    {
        return label != nullptr && label->isBeingEdited();
    }

    juce::TextEditor* SampleKnob::getTextEditor() const
    {
        return label->getCurrentTextEditor();
    }

    //==============================================================================================
    juce::Rectangle<int> SampleKnob::dialBounds() const
    {
        return { (getWidth() - kDialSize) / 2, kLabelHeight + 1, kDialSize, kDialSize };
    }

    juce::Rectangle<int> SampleKnob::valueBounds() const
    {
        return { 0, getHeight() - kValueHeight, getWidth(), kValueHeight };
    }

    int SampleKnob::preferredWidth() const
    {
        if (spec.macroRow)
            return kMaxWidth;
        const float name = Theme::textWidth (labelFont(), juce::String (spec.label).toUpperCase()) + 8.0f;
        const float text = Theme::textWidth (valueFont(), widestText (spec)) + 8.0f;
        return juce::jlimit (kMinWidth, kMaxWidth, (int) std::ceil (juce::jmax (name, text)));
    }

    void SampleKnob::resized()
    {
        label->setBounds (valueBounds());
    }

    void SampleKnob::lookAndFeelChanged()
    {
        label->applyColours();
        repaint();
    }

    void SampleKnob::paint (juce::Graphics& g)
    {
        // the name in small capitals (.sls-klbl)
        g.setColour (theme (Tok::dim));
        g.setFont (labelFont());
        g.drawFittedText (juce::String (spec.label).toUpperCase(), juce::Rectangle<int> (0, 0, getWidth(), kLabelHeight),
                          juce::Justification::centred, 1, 0.8f);

        // the dial (sl-ui-sample.js knob and arcD)
        const auto area = dialBounds().toFloat();
        const float unit = area.getWidth() / 40.0f;
        const auto centre = area.getCentre();
        const float radius = 15.0f * unit;
        const auto colour = theme (tokOf (spec.colour));
        const auto stroke = juce::PathStrokeType ((spec.macroRow ? 4.5f : 4.0f) * unit, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, arcAngle (kA0), arcAngle (kA0 + kSweep), true);
        g.setColour (theme (Tok::btnb));
        g.strokePath (track, stroke);

        const double v = drag.has_value() ? drag->last : shown;
        const float n = (float) rules::toNorm (spec.range, v);
        const float n0 = spec.bipolar ? (float) rules::toNorm (spec.range, spec.range.def) : 0.0f;
        if (! spec.off)
        {
            const float a = kA0 + kSweep * juce::jmin (n0, n), b = kA0 + kSweep * juce::jmax (n0, n);
            if (b - a >= 0.6f)
            {
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, arcAngle (a), arcAngle (b), true);
                g.setColour (colour);
                g.strokePath (arc, stroke);
            }
        }

        const auto cap = juce::Rectangle<float> (21.0f * unit, 21.0f * unit).withCentre (centre);
        g.setColour (theme (Tok::btn));
        g.fillEllipse (cap);
        g.setColour (drag.has_value() ? colour : theme (Tok::line));   // .sls-drag .cap: the knob's colour while dragged
        g.drawEllipse (cap, unit);

        if (spec.bipolar)
        {
            g.setColour (theme (Tok::dim));
            g.fillEllipse (juce::Rectangle<float> (2.2f * unit, 2.2f * unit).withCentre (pointAt (centre, kA0 + kSweep * 0.5f, radius + 3.4f * unit)));
        }

        g.setColour (theme (Tok::txt));
        g.fillEllipse (juce::Rectangle<float> (4.4f * unit, 4.4f * unit).withCentre (pointAt (centre, kA0 + kSweep * n, 7.0f * unit)));
    }

    void SampleKnob::mouseDown (const juce::MouseEvent& e)
    {
        if (e.eventComponent != this || ! e.mods.isLeftButtonDown() || ! dialBounds().toFloat().contains (e.position))
            return;
        press ((double) e.position.y);
    }

    void SampleKnob::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.eventComponent == this)
            dragTo ((double) e.position.y, e.mods.isShiftDown());
    }

    void SampleKnob::mouseUp (const juce::MouseEvent& e)
    {
        if (e.eventComponent == this)
            release();
    }

    void SampleKnob::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.eventComponent == this && dialBounds().toFloat().contains (e.position))
            doubleClick();
    }

    //==============================================================================================
    LockToggle::LockToggle (const Theme& t, juce::AudioProcessorValueTreeState& s, TabActions& a)
        : FlatButton (t, "Lock", Icon::padlock),
          actions (a),
          lock (*s.getParameter (params::spec (params::P::lock).id)),
          engine (*s.getParameter (params::spec (params::P::engine).id))
    {
        setClickingTogglesState (false);
        setTitle ("Lock");
        syncToParameter();
    }

    bool LockToggle::syncToParameter()
    {
        // lit only while it plays: Cloud with Lock (sl-ui-sample.js get: I.engine === 'cloud' && I.lock)
        const bool on = juce::roundToInt (engine.plain()) == static_cast<int> (core::Engine::cloud) && lock.plain() >= 0.5f;
        if (on == getToggleState())
            return false;
        setToggleState (on, juce::dontSendNotification);
        return true;
    }

    void LockToggle::toggle()
    {
        const bool cloud = juce::roundToInt (engine.plain()) == static_cast<int> (core::Engine::cloud);
        if (! cloud)
        {
            engine.setAsCompleteGesture ((float) static_cast<int> (core::Engine::cloud));
            lock.setAsCompleteGesture (1.0f);
            if (actions.commit)
                actions.commit ("Engine: Cloud with Lock", { params::P::engine, params::P::lock });
            if (actions.toast)
                actions.toast ("Lock works with Cloud: switched to Cloud with Lock", 2400);
        }
        else
        {
            const bool was = lock.plain() >= 0.5f;
            lock.setAsCompleteGesture (was ? 0.0f : 1.0f);
            if (actions.commit)
                actions.commit (was ? "Lock off" : "Lock on", { params::P::lock });
        }
        syncToParameter();
    }
}
