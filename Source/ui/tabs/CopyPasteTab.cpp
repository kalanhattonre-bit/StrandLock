#include "CopyPasteTab.h"

#include "PluginProcessor.h"
#include "ui/roll/RollPainter.h"
#include "ui/roll/RollView.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace sl::ui
{
    namespace
    {
        constexpr int kButtonHeight = 26;   // shell.html's buttons at this window height
        constexpr int kChipHeight = 24;     // .chip: padding 4px 8px and a 2 px border (the mode bar's Sample chips)

        juce::String notesWord (std::size_t n)
        {
            return juce::String ((int) n) + (n == 1 ? " note" : " notes");
        }
    }

    juce::String CopyPasteTab::lanesHeld (const std::array<bool, core::kNumLanes>* has)
    {
        if (has == nullptr)
            return "nothing copied";
        juce::StringArray names;
        for (int i = 0; i < core::kNumLanes; ++i)
            if ((*has)[(std::size_t) i])
                names.add (core::kLanes[i].name);
        return "holding: " + (names.size() > 4 ? juce::String (names.size()) + " lanes" : names.joinIntoString (", "));
    }

    CopyPasteTab::CopyPasteTab (const Theme& t, juce::AudioProcessorValueTreeState& s, roll::RollView& r, StrandLockProcessor& p)
        : TabPanel (t, s), roll (r), processor (p)
    {
        // the lanes Copy takes, as the project kept them (UIST copyMask: bit i is lane i of the registry)
        auto& g = roll.getGestures();
        const unsigned mask = processor.getUiState().copyMask;
        for (int i = 0; i < core::kNumLanes; ++i)
            g.copyMask[(std::size_t) i] = ((mask >> i) & 1u) != 0;

        // shell.html: Lanes to copy, a chip per lane in its colour, Copy, Paste, and what is held
        caption ("Lanes to copy", Caption::Style::label);
        for (int i = 0; i < core::kNumLanes; ++i)
        {
            const auto lane = static_cast<core::LaneId> (i);
            const juce::String name (core::laneInfo (lane).name);
            auto chip = std::make_unique<FlatButton> (t, name);
            chip->setLaneColour (roll::RollPainter::laneToken (lane));
            chip->setTooltip ("Copy " + name);
            chip->onClick = [this, lane] { chipClicked (lane); };
            const int w = chip->preferredWidth();
            chips[(std::size_t) i] = &add (std::move (chip), w, kChipHeight);
        }
        const auto button = [this, &t] (const juce::String& text, const juce::String& tip, std::function<void()> click) -> FlatButton&
        {
            auto b = std::make_unique<FlatButton> (t, text);
            b->setTooltip (tip);
            b->onClick = std::move (click);
            const int w = b->preferredWidth();
            return add (std::move (b), w, kButtonHeight);
        };
        copyButton = &button ("Copy", "Copy the chosen lanes of the selected note (Ctrl+C while Keys is on)", [this] { roll.copyLanes(); });
        pasteButton = &button ("Paste", "Paste them onto every selected note (Ctrl+V while Keys is on)", [this] { roll.pasteLanes(); });
        lanesText = &caption (lanesHeld (nullptr), Caption::Style::label);

        // the notes themselves: Copy notes, Paste notes and Duplicate, and what is held
        newRow();
        caption ("Notes", Caption::Style::label);
        copyNotesButton = &button ("Copy notes", "Copy the selected notes", [this] { roll.copyNotes(); });
        pasteNotesButton = &button ("Paste notes",
                                    "Paste the copied notes right after the selected notes (or after the copied ones). To put them "
                                    "somewhere else, right-click an empty spot in Build: Paste notes here",
                                    [this] { roll.pasteNotes(); });
        duplicateButton = &button ("Duplicate", "Duplicate the selected notes (Ctrl+D while Keys is on)", [this] { roll.duplicate(); });
        notesText = &caption ("no notes copied", Caption::Style::label);
        sync();
    }

    void CopyPasteTab::chipClicked (core::LaneId lane)
    {
        // sl-ui.js: a chip takes its lane out of the copy or puts it back; the project keeps the choice
        auto& g = roll.getGestures();
        const auto i = (std::size_t) core::laneIndex (lane);
        g.copyMask[i] = ! g.copyMask[i];
        unsigned mask = 0;
        for (std::size_t k = 0; k < g.copyMask.size(); ++k)
            if (g.copyMask[k])
                mask |= 1u << k;
        auto ui = processor.getUiState();
        ui.copyMask = (std::uint16_t) mask;
        processor.setUiState (ui);
        sync();
    }

    void CopyPasteTab::show (Caption& text, const juce::String& words)
    {
        if (text.getText() == words)
            return;
        text.setText (words);
        setItemWidth (text, text.preferredWidth());
    }

    void CopyPasteTab::sync()
    {
        const auto& g = roll.getGestures();
        for (std::size_t i = 0; i < chips.size(); ++i)
            chips[i]->setToggleState (g.copyMask[i], juce::dontSendNotification);
        const auto& held = g.laneClipboard();
        show (*lanesText, lanesHeld (held ? &held->has : nullptr));
        const auto n = g.noteClipboard().size();
        show (*notesText, n == 0 ? juce::String ("no notes copied") : "holding: " + notesWord (n));
    }
}
