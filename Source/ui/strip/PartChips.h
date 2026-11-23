#pragma once

// The strip's suggestions (PLUGIN-PLAN 4 M7, 7.9 decision 2; sl-ui.js updateStripText's suggestion buttons and
// sl-ui-sample.js renderChips): one short button each, the full sentence in its tooltip, in the prototype's order.
//   - The suggestion line: "Use A2 as the root" (one click, no undo step: the root belongs to the sound, plan 2.4;
//     shown while the Region is the one it was heard in), "Use key A minor" (one undo step), "Loop cleanly" (a short
//     file whose whole Region loops: the end pulled in a little, one undo step).
//   - E3: "Try Cloud" (the engine, one undo step) or "Cloud suits this sound (on)" (a click hides it and says why).
//   - The part chips (D10): "Use the whole file" when the Region is a part, then one chip per suggested part (the one
//     that is the Region lit); a click sets the Region to it in one undo step. A sound of 8 s or less (the starters)
//     has one "Parts" chip that opens a menu of them instead.
//   - "Load another part..." for a part of a file longer than 10 minutes: the part picker.
// Hovering a part chip draws its box (dashed) on the waveform and says what it is in the hint line. Nothing here
// applies itself: every change is a click (decision 2).

#include "ui/Controls.h"

#include "analysis/Analysis.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class StrandLockProcessor;

namespace sl::ui
{
    class PartChips final : public juce::Component
    {
    public:
        PartChips (const Theme&, StrandLockProcessor&);
        ~PartChips() override;

        /** Makes the chips again when what they show changed (the sound, its parts, the Region, a suggestion, the
            engine): the editor's timer. True when they changed. */
        bool refresh();

        /** The width the chips take in one row (gaps included); 0 with none. */
        int preferredWidth() const;

        /** The rows the chips take at this width, each filled left to right in order (a chip wider than the row has one of its
            own); 0 with none. Fix pass UI7-1: a chip that did not fit was hidden, and a song's last part chip with it. */
        int rowsFor (int width) const;

        static constexpr int chipHeight = 22, gap = 4, rowGap = 4;

        /** The part the mouse is over (the strip draws its box), if any. */
        std::optional<sl::analysis::Part> hoveredPart() const { return hovered; }

        std::function<void (const juce::String&, int)> onToast;
        std::function<void (double start, double end)> onShowPart;   // the strip shows a part (sl-ui-sample.js showPart)
        std::function<void()> onShowAll;                             // the strip shows the whole file
        std::function<void()> onPickPart;                            // "Load another part...": the part picker
        std::function<void (const juce::String&)> onHint;            // the hint line while a chip is hovered ("" = none)
        std::function<void()> onChange;                              // a click changed something the strip shows (it must not
                                                                     // make the chips again: the click runs inside one)

        //==========================================================================================
        // For the tests.

        /** The chips' texts, in order. */
        juce::StringArray texts() const;

        /** The chip showing this text (nullptr when none does). */
        FlatButton* chip (const juce::String& text) const;

        /** Picks part index from the "Parts" menu of a short sound, as the menu does. */
        void choosePart (int index);

        /** Shows a part as hovered (its dashed box and its hint), as the mouse on its chip does; none: nothing hovered. */
        void showHover (std::optional<sl::analysis::Part> part) { setHovered (std::move (part)); }

        void resized() override;

    private:
        struct Chip;
        void rebuild();
        void usePart (const sl::analysis::Part& p);
        void trimRegion();
        void openPartsMenu();
        void setHovered (std::optional<sl::analysis::Part> p);

        const Theme& theme;
        StrandLockProcessor& processor;
        std::vector<std::unique_ptr<Chip>> chips;
        juce::String key;   // what the chips were made for (renderChips' chipKey)
        std::optional<sl::analysis::Part> hovered;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PartChips)
    };
}
