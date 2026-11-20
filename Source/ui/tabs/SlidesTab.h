#pragma once

// The Slides tab (PLUGIN-PLAN 4 M5, 7.7 S3 and decision 4; shell.html's "glide" pane, sl-ui.js syncGlideUI and
// wirePanels).
//
// Auto slide (glideMode: Off, Legato for touching notes, Always into the next note) and Slide time (glideTime, in beats)
// are parameters, which M0 left without controls. They are bound the M2 way (7.4 decision 1): a pick writes the
// parameter on the message thread in one host gesture and is one undo step ("Auto slide", "Slide time"), and the
// editor's 30 Hz timer moves them when Cubase's automation does.
//
// Selected notes: the primary note's own slide time on a slider ("auto" when it follows Slide time, "jump" at 0), which
// sets every selected note's while it moves (shown and played, no undo step) and is one undo step "Slide time (notes)"
// when let go; Auto puts them back on Slide time, Jump, no slide makes them jump. These go through the selection bar's
// SelectionTools, as every tool does (7.7 decision 1). Message thread only.

#include "TabPanel.h"

#include <array>

class StrandLockProcessor;

namespace sl::ui
{
    namespace roll
    {
        class RollView;
        class ToolSlider;
    }

    /** Slide time (glideTime, beats) as the prototype's list: 1/32, 1/16, 1/8, 1/4, 1/2. A value the list does not hold
        (automation, a project from elsewhere) shows as the parameter's own text ("0.700 beats"). */
    class SlideTimeBox final : public juce::Component, public ParamSynced
    {
    public:
        SlideTimeBox (const Theme&, juce::AudioProcessorValueTreeState&, const juce::String& caption);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool syncToParameter() override;

        int preferredWidth() const;
        juce::ComboBox& getBox() noexcept { return box; }
        juce::RangedAudioParameter& getParameter() noexcept { return binding.parameter(); }

        /** Called after the user picked another value and the parameter took it (the tab makes the undo step). */
        std::function<void()> onUserChange;

        /** The list's values in beats, and their names (shell.html's gTime options). */
        static constexpr std::array<double, 5> beats { 0.125, 0.25, 0.5, 1.0, 2.0 };
        static constexpr std::array<const char*, 5> names { "1/32", "1/16", "1/8", "1/4", "1/2" };

    private:
        float captionWidth() const;

        const Theme& theme;
        juce::String caption;
        juce::ComboBox box;
        ParamBinding binding;
        float shown = -1.0f;   // the value the box shows now
        bool syncing = false;
    };

    class SlidesTab final : public TabPanel
    {
    public:
        SlidesTab (const Theme&, juce::AudioProcessorValueTreeState&, roll::RollView&, StrandLockProcessor&);

        /** Shows the primary note's slide (MainView: after every roll event and on the editor's timer). */
        void sync();

        // For the tests.
        ChoiceBox& getAutoSlide() noexcept { return *autoSlide; }
        SlideTimeBox& getSlideTime() noexcept { return *slideTime; }
        roll::ToolSlider& getNotesSlider() noexcept { return *notesSlider; }
        FlatButton& getAutoButton() noexcept { return *autoButton; }
        FlatButton& getJumpButton() noexcept { return *jumpButton; }

        /** The slider's text: an em dash with no note selected, "auto", "jump", or "0.5 beats" (sl-ui.js gSelV). */
        juce::String notesText() const;

    private:
        void edited();

        roll::RollView& roll;
        StrandLockProcessor& processor;
        ChoiceBox* autoSlide = nullptr;
        SlideTimeBox* slideTime = nullptr;
        roll::ToolSlider* notesSlider = nullptr;
        FlatButton* autoButton = nullptr;
        FlatButton* jumpButton = nullptr;
        juce::String shownText;
    };
}
