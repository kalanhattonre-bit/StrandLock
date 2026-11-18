#pragma once

// The selection bar (PLUGIN-PLAN 4 M5, 7.7 S1; sl-ui-panels.js buildBar, paintBar and the Connect and Tools drawers):
// the line under the selection row that acts on the selected notes. It holds the Lane the lane tools change, the
// drawer buttons (Shapes, Pattern, Random and Turn into nodes arrive in S2), Connect and Tools. Connect opens its
// drawer: Nearest, Furthest, Random, Re-roll and Disconnect, with how many slides join the selected notes. Tools
// opens its drawer: Walk..., Chop..., Reverse, Flip, More, Less (on the Lane), Select similar, Humanize..., Strum...
// and Legato, and the panels of Walk, Chop, Humanize and Strum.
//
// Two halves, as the roll's gestures are:
// - SelectionTools: what each control does, with no JUCE in its decisions, so every action is testable without a
//   window. Each action is one model call (model/Tools.h, through the selection in time order) and one undo step
//   (7.7 decision 1). Walk's and Humanize's sliders change the clip live after their first click, as the prototype's
//   do: every move is shown and sent to the engine with no undo step, the release is one step (decision 2). Random
//   values stay put until Re-roll (decision 3: the seeds are the bar's, only Re-roll changes them).
// - SelectionBar: the JUCE line and its drawer, drawn as shell.html and sl-ui-panels.css draw them. With nothing
//   selected and no drawer open it says how to select; an open drawer stays open, faded and asleep, while nothing is
//   selected, so clearing the selection never moves the roll (the prototype keeps it the same way).
//
// Message thread only. Edits reach the engine through the ClipDocument (the SnapshotPublisher), never directly.

#include "RollGestures.h"

#include "core/Lanes.h"
#include "core/Rates.h"
#include "doc/ClipDocument.h"
#include "model/Clip.h"
#include "model/Tools.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sl::ui::roll
{
    class RollView;

    //==============================================================================================
    /** Where the tools write: the document, the undo history (StrandLockProcessor::pushUndoStep (label, {}) in the
        plugin), and the toast (with how long it shows, in milliseconds). */
    struct ToolHost
    {
        doc::ClipDocument& doc;
        std::function<bool (const std::string& label)> commit;
        std::function<void (const std::string& text, int ms)> toast;
    };

    /** The bar's own settings (sl-ui-panels.js S, the parts the direct actions use; S2 adds Shapes', Pattern's and
        Random's). They live as long as the editor window, as the prototype's do with its page. */
    struct ToolSettings
    {
        enum class Drawer { none, shapes, pattern, random, connect, tools };
        enum class Tool { none, walk, chop, humanize, strum };

        core::LaneId lane = core::LaneId::tone;   // S.lane: the Lane outside a lane mode (a lane mode shows its own)
        Drawer drawer = Drawer::none;             // the open drawer
        Tool tool = Tool::none;                   // the open tool panel in the Tools drawer

        struct Walk
        {
            model::WalkField field = model::WalkField::start();
            double from = 0.0, to = 0.9;
        };
        Walk walk;

        struct Chop
        {
            core::RateId rate = core::RateId::r1_16;
            bool step = false;   // false: Same spot; true: Step through the part
        };
        Chop chop;

        struct Humanize
        {
            double time = 0.03, vel = 0.1, lanes = 0.05;
            double seed = 1.0;
        };
        Humanize hum;

        core::RateId strumRate = core::RateId::r1_64;
        double connectSeed = 1.0;
    };

    /** How many links a clip holds among a set of notes (sl-ui-panels.js linksAmong), and how many of those go into a
        note that starts well inside the note before it (insideLinks: the engine cuts that note short). */
    int linksAmong (const model::Clip& clip, const std::vector<std::string>& ids);
    int insideLinks (const model::Clip& clip, const std::vector<std::string>& ids);

    /** sl-ui-panels.js walkDefaults: the First and Last values Walk starts from for a field. */
    std::pair<double, double> walkDefaults (model::WalkField field);

    /** The same field (start, velocity, or the same lane). */
    bool sameField (model::WalkField a, model::WalkField b) noexcept;

    /** "Start", "Velocity" or the lane's name (sl-ui-panels.js walkName). */
    std::string walkName (model::WalkField field);

    /** "1 note" / "3 notes" (sl-ui-panels.js nNotes). */
    std::string nNotes (int k);

    //==============================================================================================
    class SelectionTools
    {
    public:
        SelectionTools (ToolHost host, RollGestures& gestures);

        SelectionTools (const SelectionTools&) = delete;
        SelectionTools& operator= (const SelectionTools&) = delete;

        ToolSettings& settings() noexcept { return set; }
        const ToolSettings& settings() const noexcept { return set; }

        /** The lane the lane tools change (sl-ui-panels.js lane ()): the lane mode's own, else the bar's Lane. */
        core::LaneId lane() const;

        /** The selected notes' ids in time order: what every action acts on. */
        std::vector<std::string> ids() const;

        /** The clip now, the primary note (null: none) and the roll's settings (the instrument, the tempo, the sound's
            length), for the drawers' texts. */
        model::ClipPtr clip() const;
        model::NotePtr primaryNote() const;
        const RollSettings& rollSettings() const noexcept { return gestures.settings(); }

        /** Picks the lane in the bar's Lane list: it becomes the Lane, and the roll shows its mode (showLane). */
        void pickLane (core::LaneId lane);

        /** Shows a lane's mode on the roll (sl-ui-panels.js showLane: mode 8 with the sub-lane, or the lane's mode).
            MainView sets it so its mode bar follows; unset, the gestures change the mode directly. */
        std::function<void (core::LaneId lane)> onShowLane;

        /** Opens a drawer, or closes it when it is the open one (sl-ui-panels.js drawerBtn). */
        void toggleDrawer (ToolSettings::Drawer d);

        /** Opens a tool's panel in the Tools drawer, or closes it when it is the open one (toolBtn). */
        void toggleTool (ToolSettings::Tool t);

        /** What the document holds now, after the roll or Undo changed it: a live slider session the selection or the
            clip left behind ends (sl-ui-panels.js wire), and a lane mode's lane becomes the Lane. Call it on every
            refresh. */
        void sync();

        //==========================================================================================
        // Connect (Y5)

        /** Each selected note slides into one note of the next chord (model::connect, the bar's seed), one undo step
            "Connect: nearest" (furthest, random), and a toast with how many slides there are now. */
        void connect (model::ConnectMode mode);

        /** A new random set: the seed moves on by one, then Connect: random ("Connect: random (re-rolled)"). */
        void connectReroll();

        /** The slides between the selected notes go (model::disconnect), one step "Disconnect". */
        void disconnect();

        //==========================================================================================
        // Tools (T1 to T5)

        /** Walk (model::walk with the bar's field, First and Last). click: the Walk button, one undo step "Walk Start"
            (Velocity, Tone ...), and the sliders change it live from then on. final false: a slider move of that
            session, shown and played with no undo step; final true: its release, one step. Without a session a slider
            only sets the value (S2's ghost shows what Walk would do). */
        void walk (bool final, bool click);

        /** The Walk field: First and Last take the field's defaults, and a live session ends. */
        void setWalkField (model::WalkField field);

        /** Chop (model::chop, the bar's rate and Start step, at most 1024 notes): one undo step "Chop 1/16", the
            pieces join the selection, and a toast. Refused (a toast, no step, the clip unchanged) past 1024 notes. */
        void chop();

        /** What Chop would do now (its panel's text and whether the button works). */
        model::EditResult chopPreview() const;

        /** The lane tools on the Lane (outside a lane mode the roll first shows the Lane's mode): Reverse
            (model::reverseLane), Flip (flipLane), More and Less (scaleLane by 2 and 0.5); one step each, "Reverse
            (Tone)". */
        void reverse();
        void flip();
        void more();
        void less();

        /** Select similar (model::selectSimilar, like the primary note): the selection only, no undo step; a toast
            with how many. */
        void selectSimilar (model::SimilarKind kind);

        /** Humanize (model::humanize with the bar's amounts and seed), live as Walk; label "Humanize". */
        void humanize (bool final, bool click);

        /** A different set of nudges: the seed moves on by one, then Humanize as one step "Humanize: re-roll". */
        void humanizeReroll();

        /** Strum up or down (model::strum, the bar's gap): one step "Strum up" / "Strum down". */
        void strum (model::StrumDir dir);

        /** Legato (model::legato): one step, or a toast saying why nothing would stretch. */
        void legato();

        /** Ends a live slider session (its last move becomes one undo step when it was not one yet). */
        void endLive();

        /** True while Walk's or Humanize's sliders change the clip live. */
        bool isLive (ToolSettings::Tool tool) const noexcept;

        /** A short message in the toast, for ms milliseconds. */
        void toast (const std::string& text, int ms = 2400) const;

    private:
        enum class LiveKey { walk, hum };

        struct Live
        {
            LiveKey key = LiveKey::walk;
            model::ClipPtr base;               // the clip the session started from
            std::vector<std::string> ids;      // the selection it acts on
            std::string label;
            model::ClipPtr last;               // what its last step made
            bool dirty = false;                // the last step is not an undo step yet
        };

        bool act (model::Clip proposed, const std::string& label);
        bool act (model::EditResult result, const std::string& label);
        void liveRun (LiveKey key, const std::string& label, const std::function<model::Clip (const model::Clip&)>& make, bool final);
        void doConnect (model::ConnectMode mode, const std::string& label);
        void humRun (bool final, bool click, const std::string& label);
        void laneTool (const std::string& name, const std::function<model::Clip (const model::Clip&, core::LaneId)>& make);
        void showLane (core::LaneId lane);
        model::Clip walkClip (const model::Clip& base) const;
        model::Clip humanizeClip (const model::Clip& base) const;

        ToolHost host;
        RollGestures& gestures;
        ToolSettings set;
        std::optional<Live> live;
        int lastMode = 0, lastLane = -1;   // the mode and lane sync last saw (a change ends a live session)
    };

    //==============================================================================================
    class Drawer;
    class ArrowButton;

    /** The JUCE half: the bar's line and the open drawer under it. */
    class SelectionBar final : public juce::Component
    {
    public:
        SelectionBar (const Theme&, RollView&);
        ~SelectionBar() override;

        /** Shows what the document holds now (the selection, the lane, the open drawer's controls). MainView calls it
            after every roll event and on its timer. */
        void sync();

        /** The height the bar wants at a width: its line, and the open drawer's box under it. */
        int heightFor (int width) const;

        /** Called when the height the bar wants changed (a drawer opened or closed, a tool panel opened). */
        std::function<void()> onHeightChange;

        static constexpr int lineHeight = 30;
        static constexpr int drawerPadX = 8, drawerPadY = 7, drawerMaxHeight = 220;

        // For the tests.
        FlatButton& getDrawerButton (ToolSettings::Drawer d) noexcept;
        FlatButton& getBakeButton() noexcept { return *bakeButton; }
        juce::ComboBox& getLaneBox() noexcept;
        Drawer* getOpenDrawer() noexcept { return drawer.get(); }
        SelectionTools& getTools() noexcept;
        const juce::String& getIdleText() const noexcept { return idleText; }
        bool isIdleShown() const noexcept { return idle; }

        /** The drawer's box (this component's pixels), or empty when none is open. */
        juce::Rectangle<int> getDrawerArea() const noexcept { return drawerArea; }

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class LaneBox;

        void openDrawer (ToolSettings::Drawer d);
        void rebuildDrawer();
        void drawerHeightChanged();

        const Theme& theme;
        RollView& roll;
        juce::String idleText;
        std::unique_ptr<LaneBox> laneBox;
        Caption laneLabel;
        std::vector<std::unique_ptr<ArrowButton>> drawerButtons;   // Shapes, Pattern, Random, Connect, Tools
        std::unique_ptr<FlatButton> bakeButton;                     // Turn into nodes (S2)
        std::unique_ptr<juce::Component> lineGap;                   // the line between Turn into nodes and Connect
        std::unique_ptr<Drawer> drawer;                             // the open drawer's contents, or none
        juce::Viewport drawerView;                                  // scrolls a drawer taller than the most it may take
        ToolSettings::Drawer shownDrawer = ToolSettings::Drawer::none;
        juce::Rectangle<int> drawerArea;
        bool idle = true;
        int lastWantedHeight = -1;

        /** What the open drawer showed at its last sync: sync () runs on the editor's timer, and the drawer is synced
            again only when one of these changed (its own controls sync it themselves). */
        struct Seen
        {
            model::ClipPtr clip;
            std::vector<std::string> ids;
            int lane = -1;
            double bpm = 0.0, seconds = 0.0, regionStart = 0.0, regionEnd = 0.0;

            bool operator== (const Seen& o) const noexcept;
        };
        Seen seen;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SelectionBar)
    };
}
