#pragma once

// The inspector (PLUGIN-PLAN 4 M4 "the inspector (T6)", 7.6 R3; sl-ui-panels.js buildInspector, syncInspector and
// the commit functions): the selected note's Start (bar.beat.sixteenth, Cubase style), Length (beats), Pitch (C3 =
// MIDI 60) and Velocity, and in a lane mode the node last clicked: where it sits in the note and its value in real
// units. Click a box and type a new value; Enter or a click elsewhere keeps it, Escape puts the old one back. With
// several notes selected, Start and Pitch move them all by the same amount and Length and Velocity set them all,
// as in the prototype. A typed value is parsed with InspectorText (R1) and changes the clip through the model's
// functions as one undo step (StrandLockProcessor::editClip); one that cannot be read shows a toast instead.
//
// Typing needs the keyboard, so a click on a box takes it even with Keys off; Enter, Escape or a click anywhere
// else gives it back. The boxes never want the keyboard at rest (the editor's focus rules). Message thread only.

#include "ui/Theme.h"

#include "core/Lanes.h"
#include "model/Clip.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class StrandLockProcessor;

namespace sl::ui::roll
{
    /** One box of the inspector: its caption on the left, the value (click to type), and a unit after it. */
    class InspectorField final : public juce::Component
    {
    public:
        InspectorField (const Theme&, const juce::String& caption, const juce::String& unit, const juce::String& tooltip);
        ~InspectorField() override;

        /** Called with what was typed, when it differs from the value shown. */
        std::function<void (const juce::String&)> onCommit;

        /** The value shown (left alone while it is being typed in). */
        void setValue (const juce::String& text);
        juce::String getValue() const;

        void setCaption (const juce::String& text);
        void setTooltipText (const juce::String& text);

        /** The value box's width (the caption and the unit come on top). */
        void setBoxWidth (int width);
        int getBoxWidth() const noexcept { return boxWidth; }
        int preferredWidth() const;

        bool isBeingEdited() const;

        /** Ends typing as a click elsewhere does: what was typed is kept. */
        void finishEditing();

        /** Types text into the box as Kalan does: opens it, puts the text in and presses Enter. */
        void type (const juce::String& text);

        /** Opens the box for typing, as a click on it does, and leaves it open (the tests press keys into it). */
        void beginTyping();

        /** The text box while the box is open, else null. */
        juce::TextEditor* getTextEditor() const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void enablementChanged() override;

        static constexpr int boxHeight = 24;

    private:
        class Box;

        int captionWidth() const;
        int unitWidth() const;
        void edited();

        const Theme& theme;
        juce::String caption, unit;
        std::unique_ptr<Box> box;
        int boxWidth = 66;
        juce::String shown;   // the value as last set, to tell a typed change from the same text
    };

    class Inspector final : public juce::Component
    {
    public:
        Inspector (const Theme&, StrandLockProcessor&);
        ~Inspector() override;

        /** Reads the document: the primary note's values and the inspector's node (bpm: the readouts' tempo). */
        void sync (double bpm);

        /** Where a value that cannot be read, or an edit the document refuses, is said (MainView's toast). */
        std::function<void (const juce::String&, int ms)> onToast;

        /** After a typed value changed the clip (the roll refreshes). */
        std::function<void()> onEdit;

        /** Ends any typing, keeping what was typed (a press on the roll does this first). */
        void finishEditing();
        bool isEditing() const;

        /** Whether a note's values show (a note is selected), and whether its node's do too. */
        bool showsNote() const noexcept { return hasNote; }
        bool showsNode() const noexcept { return hasNode; }

        /** The width everything needs on one line (the node's part included when it shows). */
        int preferredWidth() const;

        InspectorField& getStart() noexcept { return start; }
        InspectorField& getLength() noexcept { return length; }
        InspectorField& getPitch() noexcept { return pitch; }
        InspectorField& getVelocity() noexcept { return velocity; }
        InspectorField& getNodeTime() noexcept { return nodeTime; }
        InspectorField& getNodeValue() noexcept { return nodeValue; }
        const juce::String& getNodeCaption() const noexcept { return nodeCaption; }
        const juce::String& getEmptyText() const noexcept { return emptyText; }

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int fieldGap = 10;

    private:
        /** The node the inspector shows (sl-ui-panels.js activeNodeCtx, one node: picking several is M5's). */
        struct NodeCtx
        {
            model::NotePtr note;
            core::LaneId lane = core::LaneId::pitch;
            int k = 0;
        };

        model::ClipPtr clip() const;
        model::NotePtr primary() const;
        std::vector<std::string> selectedIds() const;
        std::optional<NodeCtx> nodeCtx() const;
        double total() const;
        int beatsPerBar() const;

        void commitStart (const juce::String&);
        void commitLength (const juce::String&);
        void commitPitch (const juce::String&);
        void commitVelocity (const juce::String&);
        void commitNodeTime (const juce::String&);
        void commitNodeValue (const juce::String&);
        void act (model::Clip proposed, const std::string& label);
        void toast (const juce::String& text, int ms = 2600);

        void fitValueBox();
        void layoutFields();

        const Theme& theme;
        StrandLockProcessor& processor;
        InspectorField start, length, pitch, velocity, nodeTime, nodeValue;
        juce::String nodeCaption, emptyText;
        bool hasNote = false, hasNode = false;
        double bpm = 120.0;
        int nodeLineX = -1;   // the line before the node's part
        int nodeCaptionX = 0, nodeCaptionWidth = 0;
    };
}
