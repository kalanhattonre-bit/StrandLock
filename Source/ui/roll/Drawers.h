#pragma once

// The selection bar's drawers (PLUGIN-PLAN 4 M5, 7.7; sl-ui-panels.js DRAWERS and TOOL_PANELS): the box under the bar
// with the controls of one drawer. S1 builds Connect (Y5) and Tools (T1 to T5, with the Walk, Chop, Humanize and Strum
// panels); S2 adds Shapes, Pattern and Random. A drawer's controls call the bar's SelectionTools, then RollView::edited
// (the roll refreshes, and MainView shows the result in the rows around it). They are laid out as sl-ui-panels.css
// lays them out: lines that wrap (gap 8 px, rows 6 px apart), the tool panel in a dashed box, the notes in 11.5 px dim
// text (a warning in the warn colour).
//
// Nothing here is rebuilt while one of its own controls is calling out: a control that changes what a drawer shows
// changes it in place, and the bar replaces a whole drawer only from its own buttons.

#include "SelectionBar.h"

#include "core/Rates.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <functional>
#include <memory>
#include <vector>

namespace sl::ui::roll
{
    class RollView;

    /** No control holds the keyboard (M0's rule, StrandLockEditor::applyFocusRules): a component made after the editor
        applied it (a drawer, a tool's panel) is set the same way, with everything in it. JUCE's buttons and lists want
        the keyboard by default. */
    void noKeyboardFocus (juce::Component& c);

    //==============================================================================================
    // Small controls of the drawers (not parameters: the bar's own settings).

    /** A line of text in the drawers' note style (.slp-note, or .slp-warn in the warn colour) that wraps to the width
        it is given. */
    class NoteText final : public juce::Component
    {
    public:
        NoteText (const Theme&, bool warn);

        void setText (const juce::String& newText);
        const juce::String& getText() const noexcept { return text; }

        void setWarn (bool isWarning);
        bool isWarning() const noexcept { return warn; }

        int naturalWidth() const;
        int heightFor (int width) const;

        void paint (juce::Graphics&) override;

    private:
        const Theme& theme;
        juce::String text;
        bool warn;
    };

    /** A FlatButton with a small arrow after its text: down (closed) or up (open), as the prototype's "Connect ▾". */
    class ArrowButton final : public FlatButton
    {
    public:
        ArrowButton (const Theme&, const juce::String& buttonText);

        void setOpen (bool isOpen);
        int widthWithArrow() const;
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    private:
        bool open = false;
    };

    /** A thin upright line between groups of buttons (shell.html .sep). */
    class LineSeparator final : public juce::Component
    {
    public:
        explicit LineSeparator (const Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override;

    private:
        const Theme& theme;
    };

    /** Joined buttons, one of them lit (sl-ui-panels.js seg): a choice that is not a parameter. */
    class SegButtons final : public juce::Component
    {
    public:
        SegButtons (const Theme&, const juce::StringArray& labels, int selected);

        std::function<void (int)> onPick;

        void setSelected (int index);
        int getSelected() const noexcept { return selected; }
        int preferredWidth() const;
        FlatButton& getButton (int index) noexcept { return *buttons[(std::size_t) index]; }

        void resized() override;

    private:
        std::vector<std::unique_ptr<FlatButton>> buttons;
        int selected = 0;
    };

    /** A range slider with its caption and its value (sl-ui-panels.js slider): onInput while it is dragged (and on the
        press), onChange once on the release, as an HTML range input's input and change events. */
    class ToolSlider final : public juce::Component
    {
    public:
        ToolSlider (const Theme&, const juce::String& caption, double min, double max, double step, double value,
                    std::function<juce::String (double)> format);

        std::function<void (double)> onInput, onChange;

        void setValue (double v);
        double getValue() const noexcept { return value; }
        juce::String getValueText() const;
        int preferredWidth() const;

        /** The value the track gives at x (this component's pixels), on the slider's steps. */
        double valueAt (float x) const;

        /** As the mouse does: a press at x (onInput), then the release (onChange). */
        void dragTo (float x);
        void release();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        static constexpr int trackWidth = 92;

    private:
        juce::Rectangle<float> trackArea() const;
        float captionWidth() const;
        float valueWidth() const;

        const Theme& theme;
        juce::String caption;
        double lo, hi, stepSize, value;
        std::function<juce::String (double)> format;
        bool dragging = false;
    };

    /** A rate (sl-ui-panels.js ratePicker): a list of bases (1 bar .. 1/64, or some of them) and Straight / Triplet /
        Dotted. onPick gets the rate they make together. */
    class RatePicker final : public juce::Component
    {
    public:
        RatePicker (const Theme&, const juce::String& caption, core::RateId value, std::vector<int> bases);

        std::function<void (core::RateId)> onPick;

        core::RateId getRate() const;
        int preferredWidth() const;
        juce::ComboBox& getBaseBox() noexcept { return baseBox; }
        SegButtons& getKinds() noexcept { return kinds; }

        void resized() override;

    private:
        void fire();

        Caption caption;
        juce::ComboBox baseBox;
        SegButtons kinds;
        std::vector<int> bases;   // 0 = 1 bar, 1 = 1/2 ... 6 = 1/64
    };

    //==============================================================================================
    /** One drawer's controls, laid out in the width it is given. */
    class Drawer : public juce::Component
    {
    public:
        Drawer (const Theme&, SelectionTools&, RollView&);
        ~Drawer() override = default;

        /** The height of the controls at a width (inside the drawer's padding). */
        virtual int heightFor (int width) = 0;

        /** Shows what the tools and the document hold now (texts, which buttons work). */
        virtual void sync() = 0;

        /** Called when the drawer's height may have changed (a tool panel opened or closed). */
        std::function<void()> onHeightChange;

        void resized() override;

    protected:
        /** Places the items in rows (newRow starts a line), as sl-ui-panels.css's lines wrap; returns the height. */
        int layoutItems (const std::vector<FlowItem>& items, int width, bool place);

        /** After an action: the roll refreshes and MainView shows the result. */
        void edited();

        FlatButton& addButton (const juce::String& text, const juce::String& tip, std::function<void()> click);

        const Theme& theme;
        SelectionTools& tools;
        RollView& roll;
        std::vector<std::unique_ptr<juce::Component>> owned;
    };

    /** Connect (Y5): Nearest, Furthest, Random, Re-roll, Disconnect, how many slides join the selected notes, and a
        warning for a slide into a note that starts well inside the one before it. */
    class ConnectDrawer final : public Drawer
    {
    public:
        ConnectDrawer (const Theme&, SelectionTools&, RollView&);

        int heightFor (int width) override;
        void sync() override;

        FlatButton& getButton (const juce::String& text) noexcept;
        const NoteText& getNote() const noexcept { return note; }
        const NoteText& getWarning() const noexcept { return warning; }

    private:
        std::vector<FlowItem> items (int width);

        Caption caption;
        std::vector<FlatButton*> buttons;   // Nearest, Furthest, Random, Re-roll, Disconnect
        NoteText note, warning;
    };

    class ToolPanel;

    /** Tools (T1 to T5): Walk..., Chop..., the Lane's Reverse, Flip, More and Less, Select similar, Humanize...,
        Strum... and Legato, and the open tool's panel under them. */
    class ToolsDrawer final : public Drawer
    {
    public:
        ToolsDrawer (const Theme&, SelectionTools&, RollView&);
        ~ToolsDrawer() override;

        int heightFor (int width) override;
        void sync() override;

        FlatButton& getButton (const juce::String& text) noexcept;
        ToolPanel* getPanel() noexcept { return panel.get(); }

    private:
        std::vector<FlowItem> items (int width);
        void showPanel();
        void openSimilar();

        Caption laneCaption;
        std::vector<FlatButton*> buttons;
        std::unique_ptr<ToolPanel> panel;
        ToolSettings::Tool panelTool = ToolSettings::Tool::none;
    };

    /** The panel of one tool (Walk, Chop, Humanize or Strum) in its dashed box. */
    class ToolPanel : public juce::Component
    {
    public:
        ToolPanel (const Theme&, SelectionTools&, std::function<void()> edited);
        ~ToolPanel() override = default;

        virtual int heightFor (int width) = 0;
        virtual void sync() = 0;

        void paint (juce::Graphics&) override;
        void resized() override;

        FlatButton* findButton (const juce::String& text) noexcept;
        ToolSlider* findSlider (const juce::String& caption) noexcept;
        NoteText* getNote() noexcept { return note.get(); }

    protected:
        int layoutItems (const std::vector<FlowItem>& items, int width, bool place);
        virtual std::vector<FlowItem> items (int width) = 0;

        const Theme& theme;
        SelectionTools& tools;
        std::function<void()> edited;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::unique_ptr<NoteText> note;
    };

    /** Makes the panel of a tool (none: null). */
    std::unique_ptr<ToolPanel> makeToolPanel (ToolSettings::Tool tool, const Theme&, SelectionTools&, std::function<void()> edited);
}
