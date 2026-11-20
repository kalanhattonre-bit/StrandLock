#pragma once

// The Copy / Paste tab (PLUGIN-PLAN 4 M5, 7.7 S3; shell.html's "copy" pane, sl-ui.js copyLanes and pasteLanes).
//
// Lanes: a chip per lane says which lanes Copy takes (lit in the lane's colour while it is copied; the project keeps
// the choice in UIST copyMask, and the right-click menu's Copy lanes and Ctrl+C use it too). Copy takes them from the
// primary note, Paste puts them on every selected note: node times are fractions of the note, so a pasted lane
// stretches to each note's length. The text after them says what is held.
//
// Notes (the prototype pastes only lanes; the plan caps a paste at 4096 notes, so M1 made model::pasteNotes): Copy
// notes, Paste notes (the copies start where the selected notes end, links inside the group kept) and Duplicate.
//
// Every paste and duplicate is one model call and one undo step through the roll's gestures (model::pasteLanes,
// pasteNotes, duplicateNotes; 7.7 decision 1). One that would pass 4096 notes is refused with a toast and changes
// nothing (decision 5). Message thread only.

#include "TabPanel.h"

#include "core/Lanes.h"

#include <array>

class StrandLockProcessor;

namespace sl::ui
{
    namespace roll
    {
        class RollView;
    }

    class CopyPasteTab final : public TabPanel
    {
    public:
        CopyPasteTab (const Theme&, juce::AudioProcessorValueTreeState&, roll::RollView&, StrandLockProcessor&);

        /** Shows the chips and what is held now (MainView: after every roll event and on the editor's timer). */
        void sync();

        // For the tests.
        FlatButton& getChip (core::LaneId lane) noexcept { return *chips[(std::size_t) core::laneIndex (lane)]; }
        FlatButton& getCopyButton() noexcept { return *copyButton; }
        FlatButton& getPasteButton() noexcept { return *pasteButton; }
        FlatButton& getCopyNotesButton() noexcept { return *copyNotesButton; }
        FlatButton& getPasteNotesButton() noexcept { return *pasteNotesButton; }
        FlatButton& getDuplicateButton() noexcept { return *duplicateButton; }
        const juce::String& getLanesText() const noexcept { return lanesText->getText(); }
        const juce::String& getNotesText() const noexcept { return notesText->getText(); }

        /** sl-ui.js clipInfo: "nothing copied", "holding: Pitch, Volume", or "holding: 6 lanes" past four. */
        static juce::String lanesHeld (const std::array<bool, core::kNumLanes>* has);

    private:
        void chipClicked (core::LaneId lane);
        void show (Caption& text, const juce::String& words);

        roll::RollView& roll;
        StrandLockProcessor& processor;
        std::array<FlatButton*, core::kNumLanes> chips {};
        FlatButton* copyButton = nullptr;
        FlatButton* pasteButton = nullptr;
        FlatButton* copyNotesButton = nullptr;
        FlatButton* pasteNotesButton = nullptr;
        FlatButton* duplicateButton = nullptr;
        Caption* lanesText = nullptr;
        Caption* notesText = nullptr;
    };
}
