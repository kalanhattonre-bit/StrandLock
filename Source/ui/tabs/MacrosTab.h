#pragma once

// The Macros tab (PLUGIN-PLAN 4 M6, 7.8 T1; sl-ui-sample.js buildMacros): the 8 macro knobs (Quick Controls 1-8, D8)
// with "Centre all", and the 5 whole-clip offsets (A3) with "Reset all", in the prototype's four rows. Each knob reads
// its effect in words (macroText, offsetText), snaps as the prototype's do (centre and zero pull the knob in), takes no
// typing, and a double-click centres it. A macro that does nothing now is faded and says why (the Refresh macro while
// Refresh is Smooth ...). Each finished gesture and each button is one undo step (decision 1); the editor's 30 Hz timer
// moves the knobs when Cubase's Quick Controls do. Message thread only.

#include "SampleControls.h"
#include "SampleTab.h"
#include "TabPanel.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

class StrandLockProcessor;

namespace sl::ui
{
    class MacrosTab final : public TabPanel
    {
    public:
        MacrosTab (const Theme&, juce::AudioProcessorValueTreeState&, StrandLockProcessor&);

        /** Shows what each knob does with the settings now (MainView: on the editor's timer and after every edit). */
        void sync (bool force = false);

        std::function<void (const juce::String&)> onHint;
        std::function<void (const juce::String&, int)> onToast;

        // For the tests.
        SampleKnob& getMacro (int i) const noexcept { return *macros[(std::size_t) i]; }
        SampleKnob& getOffset (int i) const noexcept { return *offsets[(std::size_t) i]; }
        FlatButton& getCentreAll() const noexcept { return *centreAll; }
        FlatButton& getResetAll() const noexcept { return *resetAll; }

        /** Every control as the prototype's fixture names it (ui/readouts.json controls). */
        std::vector<ControlState> controlStates() const;

        /** The undo steps the tab's controls made, in order (their labels; the tests count them). */
        const std::vector<std::string>& getSteps() const noexcept { return steps; }

    private:
        void centre();
        void zero();

        StrandLockProcessor& processor;
        TabActions actions;
        SettingsWatch settings;
        std::vector<std::string> steps;
        std::array<SampleKnob*, 8> macros {};
        std::array<SampleKnob*, 5> offsets {};
        FlatButton* centreAll = nullptr;
        FlatButton* resetAll = nullptr;
    };
}
