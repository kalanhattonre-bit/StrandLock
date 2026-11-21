#pragma once

// The sound line under the top bar (M7 V3, PLUGIN-PLAN 3 "Embedding" and "Finding the sound on load"): one line in the
// state banner's colours that shows while the sound needs a word from the person.
// - The 20-50 MB question: "song.wav is 32.4 MB. Keep the whole file in the project ...?" with Yes, No and Just the
//   Region. The answer is saved in the project (SMPL), so it is asked once for that file.
// - Locate: the project's file was found nowhere (not at its path, not in the library, not kept in the project). The
//   default sound plays meanwhile, and every setting is kept until a file is chosen with "Locate sample...".
// - Part kept: only a Region excerpt of the sound was kept in the project; "Locate sample..." brings back all of it.
// MainView lays it out and refreshes it on the editor's timer; the processor decides what it says
// (StrandLockProcessor::getSoundNotice). Message thread only.

#include "Controls.h"

#include <functional>

class StrandLockProcessor;

namespace sl::ui
{
    class SoundBanner final : public juce::Component
    {
    public:
        SoundBanner (const Theme&, StrandLockProcessor&);
        ~SoundBanner() override;

        /** Takes what the processor says now. True when the line came or went (MainView lays the page out again). */
        bool refresh();

        bool hasNotice() const noexcept { return kind != Kind::none; }
        const juce::String& getText() const noexcept { return text; }

        /** Where the line's toasts go (MainView shows them): the text and how long, in ms. */
        std::function<void (const juce::String&, int)> onToast;

        /** Opens the chooser for Locate (a test calls locate (file) instead). */
        void chooseFile();

        /** Locates this file as the project's sound (what the chooser calls). */
        void locate (const juce::File& file);

        // For the tests.
        FlatButton& getLocateButton() noexcept { return locateButton; }
        FlatButton& getYesButton() noexcept { return yesButton; }
        FlatButton& getNoButton() noexcept { return noButton; }
        FlatButton& getRegionButton() noexcept { return regionButton; }
        bool isQuestion() const noexcept { return kind == Kind::question; }
        bool isLocate() const noexcept { return kind == Kind::locate; }
        bool isPartKept() const noexcept { return kind == Kind::partKept; }

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int height = 32;

    private:
        enum class Kind { none, question, locate, partKept };

        const Theme& theme;
        StrandLockProcessor& processor;
        Kind kind = Kind::none;
        juce::String text;
        FlatButton locateButton, yesButton, noButton, regionButton;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Rectangle<int> textArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundBanner)
    };
}
