#include "SlidesTab.h"

#include "PluginProcessor.h"
#include "model/Clip.h"
#include "core/JsCompat.h"
#include "params/ParamText.h"
#include "params/ParameterLayout.h"
#include "ui/roll/Drawers.h"
#include "ui/roll/RollView.h"

#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace sl::ui
{
    namespace
    {
        constexpr int kButtonHeight = 26;    // shell.html's buttons and selects at this window height
        constexpr int kTimeBoxWidth = 96;    // room for "0.700 beats" when automation sets a time the list does not hold

        juce::Font labelText() { return Theme::px (10.5f, false, 0.06f); }   // .lbl

        /** sl-ui.js fmtBeats: '1 beat', '1.5 beats' (to 3 decimals). */
        juce::String fmtBeats (double x)
        {
            const double r = core::jsRound (x * 1000.0) / 1000.0;
            return juce::String (core::jsNumberToString (r)) + (juce::exactlyEqual (r, 1.0) ? " beat" : " beats");
        }
    }

    //==============================================================================================
    SlideTimeBox::SlideTimeBox (const Theme& t, juce::AudioProcessorValueTreeState& state, const juce::String& captionText)
        : theme (t), caption (captionText), binding (*state.getParameter (params::kParams[params::index (params::P::glideTime)].id))
    {
        for (std::size_t i = 0; i < names.size(); ++i)
            box.addItem (names[i], (int) i + 1);
        box.setTitle (captionText);
        box.setTooltip ("How long each slide takes. A note with a time of its own (Selected notes) keeps it");
        box.onChange = [this]
        {
            // a pick is one whole host gesture (7.4 decision 1); the tab makes it one undo step
            const int i = box.getSelectedItemIndex();
            if (syncing || i < 0 || i >= (int) beats.size())
                return;
            const float v = (float) beats[(std::size_t) i];
            if (juce::exactlyEqual (v, binding.plain()))
                return;
            binding.setAsCompleteGesture (v);
            shown = binding.plain();
            if (onUserChange)
                onUserChange();
        };
        addAndMakeVisible (box);
        syncToParameter();
    }

    bool SlideTimeBox::syncToParameter()
    {
        // the editor's 30 Hz timer: the parameter's value, unless the list is open (the person is choosing)
        const float now = binding.plain();
        if (box.isPopupActive() || juce::exactlyEqual (now, shown))
            return false;
        const juce::ScopedValueSetter<bool> hold (syncing, true);
        shown = now;
        for (std::size_t i = 0; i < beats.size(); ++i)
            if (juce::exactlyEqual ((float) beats[i], now))
            {
                box.setSelectedItemIndex ((int) i, juce::dontSendNotification);
                return true;
            }
        box.setText (juce::String (params::paramText (params::index (params::P::glideTime), (double) now, nullptr)), juce::dontSendNotification);
        return true;
    }

    float SlideTimeBox::captionWidth() const
    {
        return std::ceil (Theme::textWidth (labelText(), caption.toUpperCase())) + 8.0f;
    }

    int SlideTimeBox::preferredWidth() const
    {
        return (int) captionWidth() + kTimeBoxWidth;
    }

    void SlideTimeBox::resized()
    {
        box.setBounds (getLocalBounds().withTrimmedLeft ((int) captionWidth()));
    }

    void SlideTimeBox::paint (juce::Graphics& g)
    {
        g.setColour (theme (Tok::dim));
        g.setFont (labelText());
        g.drawText (caption.toUpperCase(), juce::Rectangle<float> (0.0f, 0.0f, captionWidth(), (float) getHeight()),
                    juce::Justification::centredLeft, false);
    }

    //==============================================================================================
    SlidesTab::SlidesTab (const Theme& t, juce::AudioProcessorValueTreeState& s, roll::RollView& r, StrandLockProcessor& p)
        : TabPanel (t, s), roll (r), processor (p)
    {
        using params::P;

        // shell.html's glide pane: Auto slide, Slide time | Selected notes (a slider and its value), Auto, Jump, no slide
        autoSlide = &choice (P::glideMode, "Auto slide", 182);
        autoSlide->setLabels ({ "Off", "Legato (touching notes)", "Always (next note)" });
        autoSlide->getBox().setTooltip ("Which notes slide into the next one by themselves: none, the notes that touch, or every note");
        autoSlide->onUserChange = [this] { processor.pushUndoStep ("Auto slide", { params::P::glideMode }); };

        auto timeBox = std::make_unique<SlideTimeBox> (t, s, "Slide time");
        timeBox->onUserChange = [this] { processor.pushUndoStep ("Slide time", { params::P::glideTime }); };
        bound (P::glideTime);
        const int tw = timeBox->preferredWidth();
        slideTime = &add (std::move (timeBox), tw, kButtonHeight);

        shortSeparator();

        // the selected notes' own time: live while it moves, one undo step when let go (through the selection tools)
        auto slider = std::make_unique<roll::ToolSlider> (t, "Selected notes", 0.0, 2.0, 0.0625, 0.5, [this] (double) { return notesText(); });
        slider->onInput = [this] (double v)
        {
            roll.getTools().slideTime (v, false);
            edited();
        };
        slider->onChange = [this] (double v)
        {
            roll.getTools().slideTime (v, true);
            edited();
        };
        const int sw = slider->preferredWidthFor (fmtBeats (1.9375));   // the longest it reads: "1.938 beats"
        notesSlider = &add (std::move (slider), sw, kButtonHeight);

        const auto button = [this, &t] (const juce::String& text, const juce::String& tip, std::function<void()> click) -> FlatButton&
        {
            auto b = std::make_unique<FlatButton> (t, text);
            b->setTooltip (tip);
            b->onClick = std::move (click);
            const int w = b->preferredWidth();
            return add (std::move (b), w, kButtonHeight);
        };
        autoButton = &button ("Auto", "Use the default slide time (Slide time above)", [this]
        {
            roll.getTools().slideAuto();
            edited();
        });
        jumpButton = &button ("Jump, no slide", "Jump straight to the next note (it still plays on the same voice)", [this]
        {
            roll.getTools().slideJump();
            edited();
        });

        // M8 W1 (F3): the clip's tieAcrossLoop, lit while on
        shortSeparator();
        holdButton = &button ("Hold over the loop end",
                              "On: a note that crosses the end of the clip carries on into the next time round, and the last note can "
                              "slide into the first. Off: every note stops at the end of the clip",
                              [this] { toggleHold(); });
        holdButton->setClickingTogglesState (false);
        sync();
    }

    void SlidesTab::toggleHold()
    {
        const auto clip = processor.getDocument().current();
        if (clip == nullptr)
            return;
        model::Clip next = *clip;
        next.tieAcrossLoop = ! clip->tieAcrossLoop;
        const std::string label = next.tieAcrossLoop ? "Hold over the loop end on" : "Hold over the loop end off";
        processor.editClip (label, std::move (next));   // one undo step; the publisher plays the new clip
        edited();
    }

    juce::String SlidesTab::notesText() const
    {
        const auto n = roll.getTools().primaryNote();
        if (n == nullptr)
            return juce::String::fromUTF8 ("\xe2\x80\x94");   // the em dash
        if (! n->glide)
            return "auto";
        if (juce::exactlyEqual (*n->glide, 0.0))
            return "jump";
        return fmtBeats (*n->glide);
    }

    void SlidesTab::edited()
    {
        roll.edited();   // the roll refreshes, then MainView shows the result (and syncs this tab)
    }

    void SlidesTab::sync()
    {
        // sl-ui.js syncGlideUI: no note, a greyed slider; else it sits at the note's own time, or at Slide time while the
        // note follows it
        const auto n = roll.getTools().primaryNote();
        const bool has = n != nullptr;
        if (notesSlider->isEnabled() != has)
        {
            notesSlider->setEnabled (has);
            notesSlider->setAlpha (has ? 1.0f : 0.45f);
        }
        if (has)
            notesSlider->setValue (n->glide ? *n->glide : (double) params::plainValueOf (processor.getParam (params::P::glideTime)));
        const auto text = notesText();
        if (text != shownText)
        {
            shownText = text;
            notesSlider->repaint();
        }
        const auto clip = processor.getDocument().current();
        const bool hold = clip != nullptr && clip->tieAcrossLoop;
        if (holdButton->getToggleState() != hold)
            holdButton->setToggleState (hold, juce::dontSendNotification);
    }
}
