#pragma once

// The part picker (PLUGIN-PLAN 4 M7, question 9.3: "above 10 minutes StrandLock offers to load a part"). For a file
// longer than 10 minutes it covers the waveform while it is open: the whole file drawn coarsely (the worker reads it
// once, doc::SampleManager's overview), a box 10 minutes wide that the mouse drags along it, the times the box holds,
// "Load this part" (the box's frames load as a sound the user picked: one undo step, and SMPL's loaded range names
// them) and Cancel. It opens by itself after such a file loads, and from the strip's "Load another part..." chip.

#include "ui/Controls.h"

#include "doc/SampleManager.h"

#include <cstdint>
#include <functional>
#include <memory>

class StrandLockProcessor;

namespace sl::ui
{
    class PartPicker final : public juce::Component
    {
    public:
        PartPicker (const Theme&, StrandLockProcessor&);

        /** Opens on the sound playing now, when it is a part of a longer file: the box on the part loaded now, and
            the overview asked for. False (and nothing shown) otherwise. */
        bool open();
        void close();
        bool isOpen() const noexcept { return isVisible(); }

        /** The editor's timer: the overview once the worker has made it. */
        void refresh();

        /** The box: where it starts, in frames of the file (the part it would load), and its length. */
        std::uint64_t startFrame() const noexcept { return start; }
        std::uint64_t partFrames() const noexcept { return length; }
        void setStartFrame (std::uint64_t frame);

        /** "Load this part": the box's 10 minutes load (as loadSoundFilePart). */
        void loadChosen();

        /** "3:15 to 13:15 of 14:32". */
        juce::String timesText() const;

        /** True once the overview of this file is there to draw. */
        bool hasOverview() const noexcept { return overview != nullptr; }

        std::function<void (const juce::String&, int)> onToast;
        std::function<void()> onClose;

        FlatButton& getLoadButton() noexcept { return loadButton; }
        FlatButton& getCancelButton() noexcept { return cancelButton; }
        juce::Rectangle<int> getOverviewArea() const noexcept { return area; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        static constexpr int rowHeight = 24;

    private:
        void moveBoxTo (float x);
        float xOfFrame (double frame) const noexcept;

        const Theme& theme;
        StrandLockProcessor& processor;
        FlatButton loadButton, cancelButton;
        juce::String path, name;
        std::uint64_t fileFrames = 0, start = 0, length = 0, maxLength = 0;
        double rate = 0.0;
        std::shared_ptr<const doc::FileOverview> overview;
        juce::Rectangle<int> area;
        float grabOffset = 0.0f;
        bool grabbing = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PartPicker)
    };
}
