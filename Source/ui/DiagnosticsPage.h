#pragma once

// The Diagnostics page (PLUGIN-PLAN 4 M0, 6): everything the DiagModel records, live, in cards, and the Copy
// report button that puts DiagModel::reportText() on the clipboard. Message thread only: the editor's timer
// hands it a fresh snapshot several times a second while it is shown.

#include "diag/DiagModel.h"
#include "ui/Controls.h"

#include <functional>
#include <vector>

class StrandLockProcessor;

namespace sl::ui
{
    class DiagnosticsPage final : public juce::Component
    {
    public:
        DiagnosticsPage (const Theme&, StrandLockProcessor&);

        /** Shows this snapshot (and the processor's state banner and settings). keysOn is the Keys switch now. */
        void update (const diag::DiagModel::Snapshot& snapshot, bool keysOn);

        /** Puts the report on the clipboard (copyText) and returns it. */
        juce::String copyReport();

        /** Where the report goes; the system clipboard unless a test replaces it. */
        std::function<void (const juce::String&)> copyText;

        /** Called after a copy (the editor shows a toast). */
        std::function<void()> onCopied;

        /** The height the page needs at this width. */
        int heightFor (int width) const;

        FlatButton& getCopyButton() noexcept { return copyButton; }

        /** Every line the page shows now, "Card: label: value" (for the tests). */
        juce::StringArray shownText() const;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row
        {
            juce::String label, value;
        };

        struct Card
        {
            juce::String title;
            std::vector<Row> rows;
        };

        struct Placed
        {
            int card = 0;
            juce::Rectangle<int> bounds;
        };

        std::vector<Placed> placeCards (int width, int& totalHeight) const;
        int cardHeight (const Card& card, int width) const;
        static int rowLines (const Row& row, int valueWidth);
        static int labelWidthFor (const Card& card, int cardWidth);

        const Theme& theme;
        StrandLockProcessor& processor;
        std::vector<Card> cards;
        FlatButton copyButton;
        Caption title, intro;

        static constexpr int headerHeight = 40;
        static constexpr int cardGap = 8;
        static constexpr int lineHeight = 16;
    };
}
