#pragma once

// The suite's controls (GrainLock's Controls), drawn in the editor's theme and in the prototype's shapes:
// shell.html's buttons, selects and tabs, and sl-ui-sample.js's knob. None of them takes keyboard focus
// (the Keys switch decides that for the whole editor, PluginEditor.cpp).
//
// Parameters (PLUGIN-PLAN 7.4 decision 1): no control uses JUCE's ParameterAttachment or the APVTS
// attachments. Their listener posts a message when the host moves a parameter from the audio thread, and that
// post can allocate there. Here a control writes its parameter on the message thread through a ParamBinding
// (a host gesture around every change), and the editor's 30 Hz timer calls syncToParameter () on every
// ParamSynced control: it moves the control to the parameter's value when that changed, never while the user
// is changing it.

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace sl::ui
{
    //==============================================================================================
    /** One parameter as a control writes it: on the message thread, inside a host gesture
        (beginChangeGesture, the new value notifying the host, endChangeGesture), with the plain value the
        state saves (a float as it is, a choice's index, a switch's 0 or 1). */
    class ParamBinding
    {
    public:
        explicit ParamBinding (juce::RangedAudioParameter& parameter);
        ~ParamBinding();

        ParamBinding (const ParamBinding&) = delete;
        ParamBinding& operator= (const ParamBinding&) = delete;

        juce::RangedAudioParameter& parameter() const noexcept { return param; }
        int index() const noexcept { return paramIndex; }

        /** The plain value now. */
        float plain() const noexcept;

        /** Opens the gesture (a drag starts); a second call while it is open does nothing. */
        void beginGesture();

        /** Sets the plain value (made legal) when it differs. Call it inside the gesture. */
        void setPlain (float plain);

        /** Closes the gesture (the drag ends). */
        void endGesture();

        /** One whole change (a click, a menu pick): begin, set, end; nothing when the value is already this. */
        void setAsCompleteGesture (float plain);

        /** True between beginGesture and endGesture: the user is changing it, so the timer leaves it alone. */
        bool inGesture() const noexcept { return gesture; }

    private:
        juce::RangedAudioParameter& param;
        const int paramIndex;
        bool gesture = false;
    };

    /** A control that shows a parameter. The editor's 30 Hz timer calls syncToParameter on each. */
    class ParamSynced
    {
    public:
        virtual ~ParamSynced() = default;

        /** Moves the control to its parameter's value when that differs from what it shows, unless the user is
            changing it now. True when it moved. */
        virtual bool syncToParameter() = 0;
    };

    /** Every ParamSynced component under parent (the panels, the strip), in tree order. */
    void collectSynced (juce::Component& parent, std::vector<ParamSynced*>& out);

    //==============================================================================================
    /** A rotary slider dragged up/down (or right/left). Hold Ctrl for fine steps. (GrainLock's FineSlider.) */
    class FineSlider final : public juce::Slider
    {
    public:
        FineSlider();

        std::function<void()> onHoverChange;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void startedDragging() override { dragging = true; }
        void stoppedDragging() override { dragging = false; }

    private:
        bool dragging = false;   // only move the value inside a real drag (inside the host gesture)
        double dragProportion = 0.0;
        juce::Point<float> lastDragPosition;
    };

    //==============================================================================================
    /** A parameter knob as the prototype draws it: its name above in small capitals, the dial, the value
        below. Drag up or down; Ctrl for fine steps; double-click puts it back to its default. */
    class Knob final : public juce::Component, public ParamSynced
    {
    public:
        Knob (const Theme&, juce::AudioProcessorValueTreeState&, const juce::String& parameterId,
              const juce::String& displayName, Tok colour = Tok::acc, bool bipolar = false);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool syncToParameter() override;

        int preferredWidth() const;
        static constexpr int preferredHeight = 72;

        juce::Slider& getSlider() noexcept { return slider; }
        juce::RangedAudioParameter& getParameter() noexcept { return binding.parameter(); }
        const ParamBinding& getBinding() const noexcept { return binding; }
        const juce::String& getDisplayName() const noexcept { return name; }

    private:
        void write();

        const Theme& theme;
        ParamBinding binding;
        juce::String name;
        FineSlider slider;
        bool syncing = false;   // the timer is moving the slider: not the user's change
    };

    //==============================================================================================
    /** A button in shell.html's style: the button colours, a 7 px corner, semibold 12 px text. It can carry a
        key chip at its left (the mode buttons' "1".."8"), a small vector icon, a 2 px border in a lane colour
        (filled with that colour while on), and square corners where it joins a neighbour. */
    class FlatButton : public juce::Button
    {
    public:
        enum class Icon { none, sun, moon, keys, folder, notes, nodes, chevronDown, chevronUp, copy };

        FlatButton (const Theme&, const juce::String& buttonText, Icon icon = Icon::none);

        void setKeyChip (const juce::String& keyText);
        void setLaneColour (std::optional<Tok> laneColour);
        void setJoins (bool joinsLeft, bool joinsRight);
        void setIcon (Icon newIcon);
        void setFontPx (float px);

        /** The width the text, the icon and the chip need, with shell.html's padding. */
        int preferredWidth() const;

        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    protected:
        const Theme& theme;

    private:
        Icon icon;
        juce::String keyChip;
        std::optional<Tok> lane;
        bool joinLeft = false, joinRight = false;
        float fontPx = 12.0f;
    };

    /** Draws one of FlatButton's icons inside area. */
    void paintIcon (juce::Graphics&, FlatButton::Icon, juce::Rectangle<float> area, juce::Colour ink, juce::Colour background);

    //==============================================================================================
    /** An on/off parameter as a FlatButton that lights in the accent while on (the prototype's Tape and Lock
        to the beat buttons). */
    class ParamToggle final : public FlatButton, public ParamSynced
    {
    public:
        ParamToggle (const Theme&, juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& buttonText);

        bool syncToParameter() override;
        juce::RangedAudioParameter& getParameter() noexcept { return binding.parameter(); }

    protected:
        using FlatButton::clicked;   // keeps the other overload visible (GCC -Woverloaded-virtual)
        void clicked() override;

    private:
        ParamBinding binding;
    };

    //==============================================================================================
    /** An on/off parameter drawn as a small pill switch with its name underneath (GrainLock's PillToggle). */
    class PillToggle final : public juce::Button, public ParamSynced
    {
    public:
        PillToggle (const Theme&, juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& caption);

        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
        bool syncToParameter() override;

    protected:
        using juce::Button::clicked;
        void clicked() override;

    private:
        const Theme& theme;
        ParamBinding binding;
    };

    //==============================================================================================
    /** A choice parameter as a row of joined buttons (the prototype's .sls-seg) with its caption on the left;
        click one to select it. */
    class SegmentedControl final : public juce::Component, public ParamSynced
    {
    public:
        SegmentedControl (const Theme&, juce::RangedAudioParameter&, const juce::StringArray& labels, const juce::String& caption);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        bool syncToParameter() override;

        int preferredWidth() const;
        int getSelected() const noexcept { return selected; }
        juce::RangedAudioParameter& getParameter() noexcept { return binding.parameter(); }

        /** Selects segment index as a click on it does. */
        void choose (int index);

        /** The area of segment i (for the tests). */
        juce::Rectangle<int> segmentBounds (int index) const;

    private:
        int segmentAt (juce::Point<float> position) const;
        float captionWidth() const;
        std::vector<float> segmentWidths() const;

        const Theme& theme;
        juce::StringArray labels;
        juce::String caption;
        int selected = 0;
        int hovered = -1;
        ParamBinding binding;
    };

    //==============================================================================================
    /** A choice parameter as a drop-down with its caption on the left. */
    class ChoiceBox final : public juce::Component, public ParamSynced
    {
    public:
        ChoiceBox (const Theme&, juce::AudioProcessorValueTreeState&, const juce::String& parameterId,
                   const juce::String& caption, int boxWidth);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool syncToParameter() override;

        int preferredWidth() const;
        juce::ComboBox& getBox() noexcept { return box; }
        juce::RangedAudioParameter& getParameter() noexcept { return binding.parameter(); }

        /** Called after the user picked another item and the parameter took it (the strip makes an undo step). */
        std::function<void()> onUserChange;

    private:
        float captionWidth() const;

        const Theme& theme;
        juce::String caption;
        int boxWidth;
        juce::ComboBox box;
        ParamBinding binding;
        bool syncing = false;
    };

    //==============================================================================================
    /** A row of text tabs (the tool tabs): the open one in the accent with a 2 px underline. */
    class TabStrip final : public juce::Component
    {
    public:
        TabStrip (const Theme&, const juce::StringArray& tabNames);

        /** Called with the tab clicked (also when it is the open one: the panel folds then). */
        std::function<void (int)> onClick;

        void setSelected (int index);
        int getSelected() const noexcept { return selected; }

        /** While folded no tab shows as open. */
        void setFolded (bool isFolded);

        juce::Rectangle<int> tabBounds (int index) const;
        int tabAt (juce::Point<float> position) const;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        const Theme& theme;
        juce::StringArray names;
        int selected = 0;
        int hovered = -1;
        bool folded = false;
    };

    //==============================================================================================
    /** A line of text in one of the prototype's text styles. */
    class Caption final : public juce::Component
    {
    public:
        enum class Style
        {
            label,    // .lbl: 10.5 px dim capitals
            sub,      // .sls-sub and the empty-pane notes: 12 px dim
            text,     // 12 px in the text colour
            strong    // 12 px bold in the text colour
        };

        Caption (const Theme&, const juce::String& text, Style style);

        void setText (const juce::String& newText);
        const juce::String& getText() const noexcept { return text; }

        int preferredWidth() const;
        juce::Font getFont() const;

        void paint (juce::Graphics&) override;

    private:
        const Theme& theme;
        juce::String text;
        Style style;
    };

    //==============================================================================================
    /** GrainLock's preset picker: shows a name, and a pick from its menu calls onPick. (For M6's presets.) */
    class PresetButton final : public juce::Component
    {
    public:
        explicit PresetButton (const Theme&);

        std::function<juce::StringArray()> getNames;
        std::function<int()> getCurrentIndex;
        std::function<void (int)> onPick;

        void setDisplayedName (const juce::String& name);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    private:
        const Theme& theme;
        juce::String displayed;
    };

    /** GrainLock's small chevron button for stepping through presets. */
    class ChevronButton final : public juce::Button
    {
    public:
        ChevronButton (const Theme&, bool pointsRight);
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    private:
        const Theme& theme;
        bool right;
    };

    //==============================================================================================
    /** One item of a flow layout: a component with the size it wants. */
    struct FlowItem
    {
        juce::Component* component = nullptr;   // nullptr = an empty gap of width w
        int width = 0;
        int height = 0;
        bool newRow = false;                    // start a new row before this item
    };

    /** Places items left to right (gap apart), starting a new row (rowGap below) when the next one would not
        fit; each item is centred in its row's height. Returns the height used. With place false nothing moves
        (to measure). rowBottoms, when given, receives where each row ends, from the top of area. */
    int flowLayout (const std::vector<FlowItem>& items, juce::Rectangle<int> area, int gap, int rowGap, bool place,
                    std::vector<int>* rowBottoms = nullptr);
}
