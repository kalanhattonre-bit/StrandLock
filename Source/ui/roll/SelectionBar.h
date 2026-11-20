#pragma once

// The selection bar (PLUGIN-PLAN 4 M5, 7.7 S1 and S2; sl-ui-panels.js buildBar, paintBar and DRAWERS): the line
// under the selection row that acts on the selected notes. It holds the Lane the lane tools change, the drawer
// buttons Shapes, Pattern and Random, Turn into nodes, Connect and Tools. Shapes (Y2) opens its drawer: Each note or
// Across, and the lane's shapes. Pattern (Y3) and Random (Y4) open theirs: a generator's settings, Apply (then the
// controls change it live), Re-roll, Remove and Turn into nodes. Connect opens its drawer: Nearest, Furthest, Random,
// Re-roll and Disconnect, with how many slides join the selected notes. Tools opens its drawer: Walk..., Chop...,
// Reverse, Flip, More, Less (on the Lane), Select similar, Humanize..., Strum... and Legato, and the panels of Walk,
// Chop, Humanize and Strum. Hovering a control that changes notes draws what it would do as a ghost (GhostPreview).
//
// Two halves, as the roll's gestures are:
// - SelectionTools: what each control does, with no JUCE in its decisions, so every action is testable without a
//   window. Each action is one model call (model/Tools.h, through the selection in time order) and one undo step
//   (7.7 decision 1). Walk's and Humanize's sliders change the clip live after their first click, and Pattern's and
//   Random's after Apply, as the prototype's do: every move is shown and sent to the engine with no undo step, the
//   release is one step (decision 2). Before that a move only draws a ghost of the result. Random values stay put
//   until Re-roll (decision 3: the seeds are the bar's and the notes', only Re-roll changes them). The Slides tab's
//   Selected notes slider, Auto and Jump, no slide (S3) act through these tools too.
// - SelectionBar: the JUCE line and its drawer, drawn as shell.html and sl-ui-panels.css draw them. With nothing
//   selected and no drawer open it says how to select; an open drawer stays open, faded and asleep, while nothing is
//   selected, so clearing the selection never moves the roll (the prototype keeps it the same way).
//
// Message thread only. Edits reach the engine through the ClipDocument (the SnapshotPublisher), never directly.

#include "GhostPreview.h"
#include "RollGestures.h"

#include "core/Lanes.h"
#include "core/Rates.h"
#include "doc/ClipDocument.h"
#include "model/Clip.h"
#include "model/Generators.h"
#include "model/Shapes.h"
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

    /** The bar's own settings (sl-ui-panels.js S). They live as long as the editor window, as the prototype's do with
        its page. */
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

        /** S.pat: the Pattern drawer's settings (a primary note's own pattern on the lane is read into them). */
        struct Pattern
        {
            model::GenShape shape = model::GenShape::squiggle;
            core::RateId rate = core::RateId::r1_16T;
            double d0 = 0.3, d1 = 0.3, warp = 0.0;
            bool lockToBeat = true;
            model::PerNote perNote = model::PerNote::same;
        };
        Pattern pat;

        /** S.rnd: the Random drawer's settings. */
        struct Random
        {
            core::RateId rate = core::RateId::r1_16T;
            double amount = 0.5, smooth = 0.0;
            model::PerNote perNote = model::PerNote::same;
            bool lockToBeat = true;
        };
        Random rnd;

        double shapeSeed = 1.0;   // S.shapeSeed: the Random shape's seed; each Random click moves it on by one
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

    /** sl-ui-panels.js GEN_NAMES: "Squiggle", "Saw up", "Random steps" ... */
    const char* genShapeName (model::GenShape shape) noexcept;

    /** sl-ui-panels.js SHAPE_TITLES: what a shape does, for its tooltip and the hint bar. */
    const char* shapeTitle (model::ShapeKey key) noexcept;

    /** sl-ui-panels.js depthText: "+-0.6 semitones" on Pitch (full depth = 2 semitones each way), else "30%". */
    std::string depthText (core::LaneId lane, double v);

    /** sl-ui-panels.js warpText ("even", "speeds up", "slows down") and smoothText ("steps", "smooth", "40%"). */
    std::string warpText (double v);
    std::string smoothText (double v);

    /** A pattern shape whose values are random (Random steps, Smooth random): its drawer offers Re-roll and Same or
        Different per note. */
    bool isRandomShape (model::GenShape shape) noexcept;

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

        /** Shows a lane's mode on the roll (sl-ui-panels.js showLane: mode 8 with the sub-lane, or the lane's mode),
            through onShowLane when it is set. The lane menu shows its lane this way. */
        void showLane (core::LaneId lane);

        /** What showLane calls: MainView sets it so its mode bar follows; unset, the gestures change the mode directly. */
        std::function<void (core::LaneId lane)> onShowLane;

        /** Opens a drawer, or closes it when it is the open one (sl-ui-panels.js drawerBtn). */
        void toggleDrawer (ToolSettings::Drawer d);

        /** Opens a drawer (sl-ui-panels.js openDrawer): the lane drawers show their lane on the roll and read the
            primary note's generator into their settings. The lane menu's Pattern settings... and Random settings...
            open theirs. */
        void openDrawer (ToolSettings::Drawer d);

        /** Opens a tool's panel in the Tools drawer, or closes it when it is the open one (toolBtn). */
        void toggleTool (ToolSettings::Tool t);

        /** Where the ghosts are drawn (the view's), or none. */
        void setGhosts (GhostPreview* g) noexcept { ghosts = g; }
        GhostPreview* getGhosts() const noexcept { return ghosts; }

        /** One model call's result as one undo step called label (7.7 decision 1): the document applies it (its caps
            can refuse it: then a toast, no step, the clip unchanged), and any preview goes. A result equal to the clip
            pushes no step. False when refused. The lane menu's items act through it, as the prototype's do. */
        bool act (model::Clip proposed, const std::string& label);

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
            move draws a ghost of what Walk would do (owned by the drawer). */
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

        //==========================================================================================
        // Slides (the Slides tab, M5 S3: sl-ui.js wirePanels' Selected notes slider, Auto and Jump, no slide)

        /** The selected notes' own slide time in beats (model::updateNotes glide), live as Walk's sliders are: every move
            is rebuilt from the clip the drag started from, shown and played with no undo step; final (the release) is one
            step "Slide time (notes)". Nothing with no note selected. */
        void slideTime (double beats, bool final);

        /** Auto: the selected notes follow Slide time again (glide null, curve 0), one step "Slide: auto". */
        void slideAuto();

        /** Jump, no slide: glide 0 (the next note still plays on the same voice), one step "Slide: jump". */
        void slideJump();

        //==========================================================================================
        // Shapes (Y2)

        /** sl-ui-panels.js travelKind: the selected notes' Travel kind (Speed with nothing selected), or nothing when
            some use Speed and some Path. */
        std::optional<model::TravelKind> travelKind() const;

        /** sl-ui-panels.js shapeKeys: a lane's shapes; on Travel the selected notes' kind's (both kinds when mixed). */
        std::vector<model::ShapeKey> shapeKeys (core::LaneId lane) const;

        /** sl-ui-panels.js shapeOpts: the Random shape's seed, the tempo, the sound's length and the instrument. */
        model::ShapeOpts shapeOpts() const;

        /** Each note or Across, shared by every shape tool (the roll's setting; the view keeps it in UIST). */
        model::ShapeScope scope() const noexcept;
        void setScope (model::ShapeScope s) noexcept;

        /** What a shape would make on a lane of the notes in ids (the click, and its ghost): model::applyShape with
            the scope and shapeOpts. Nothing for a key the lane does not have, or with no notes. */
        std::optional<model::Clip> shapeClip (model::ShapeKey key, core::LaneId lane, const std::vector<std::string>& ids) const;

        /** A shape on the Lane of the selected notes (the Shapes drawer): the roll shows the lane, one undo step
            "Ramp up (Tone)"; a Random click moves the shape seed on by one, so the next one deals new values. */
        void applyShape (model::ShapeKey key);

        //==========================================================================================
        // Pattern (Y3) and Random (Y4): a generator on the Lane

        /** sl-ui-panels.js putGen: the drawer's pattern (or random) on a lane of every note in ids, each note keeping
            its own seed and 'Different' key (and its invert when it had the same kind), so random stays put (Y7).
            shape: the pattern shape a chip would pick (its ghost); nothing: the drawer's. */
        model::Clip putGen (const model::Clip& base, const std::vector<std::string>& ids, core::LaneId lane, model::GenKind kind,
                            std::optional<model::GenShape> shape = std::nullopt) const;

        /** The generator the drawer would make (model::makeGen with the drawer's settings). */
        model::Gen drawerGen (model::GenKind kind, std::optional<model::GenShape> shape = std::nullopt) const;

        /** The primary note's generator on a lane (sl-ui-panels.js primaryGen), or nothing. */
        std::optional<model::Gen> primaryGen (core::LaneId lane) const;

        /** A selected note has a generator on the lane (randomish: a random one, or a pattern of a random shape). */
        bool anyGen (core::LaneId lane, bool randomish = false) const;

        /** The primary note has this kind of generator on the Lane: the drawer's controls change it live. */
        bool isEditing (model::GenKind kind) const;

        /** A drawer control changed (sl-ui-panels.js genChanged): while editing, a live session ("Pattern depth
            (Tone)": a move is shown and played with no undo step, final is one step); else a ghost of what Apply would
            do, owned by the drawer. */
        void genChanged (model::GenKind kind, bool final, const std::string& what);

        /** Apply (applyGen): the drawer's pattern or random on the Lane of the selected notes, one undo step "Pattern
            (Tone)" / "Random (Tone)"; from then on the drawer's controls change it live. */
        void applyGen (model::GenKind kind);

        /** Re-roll: every selected note's generator on the Lane takes seed + 1 (model::reroll), one step "Re-roll
            (Tone)". */
        void rerollGen();

        /** Remove: the generator off the Lane (model::clearGen), one step "Remove pattern (Tone)". */
        void removeGen();

        /** Turn into nodes (model::bakeGen): one step "Turn into nodes (Tone)", or a toast when no selected note has a
            pattern or random on the Lane. */
        void bake();

        /** sl-ui-panels.js loadGenValues: the primary note's generator on the Lane, read into the drawer's settings. */
        void loadGenValues();

        //==========================================================================================
        // Ghosts: what each control would do, made off to the side (never applied). Nothing when there is nothing
        // to show (no notes selected, nothing to remove, a refused Chop).

        std::optional<model::Clip> previewShape (model::ShapeKey key) const;
        std::optional<model::Clip> previewGen (model::GenKind kind, std::optional<model::GenShape> shape = std::nullopt) const;
        std::optional<model::Clip> previewReroll() const;
        std::optional<model::Clip> previewRemove() const;
        std::optional<model::Clip> previewBake() const;
        std::optional<model::Clip> previewConnect (model::ConnectMode mode, bool reroll = false) const;
        std::optional<model::Clip> previewDisconnect() const;
        std::optional<model::Clip> previewWalk() const;
        std::optional<model::Clip> previewChop() const;
        std::optional<model::Clip> previewHumanize() const;
        std::optional<model::Clip> previewStrum (model::StrumDir dir) const;
        std::optional<model::Clip> previewLegato() const;

        /** Reverse, Flip, More or Less on the Lane (0, 1, 2, 3). */
        std::optional<model::Clip> previewLaneTool (int which) const;

        /** A recipe's notes (sl-ui-sample.js: model::applyRecipe on the selected notes, or on every note with none
            selected), for M6's recipe buttons. */
        std::optional<model::Clip> previewRecipe (int n) const;

        /** Ends a live slider session (its last move becomes one undo step when it was not one yet). */
        void endLive();

        /** True while Walk's or Humanize's sliders change the clip live. */
        bool isLive (ToolSettings::Tool tool) const noexcept;

        /** True while a Pattern or Random control changes the clip live (after Apply). */
        bool isLiveGen() const noexcept;

        /** A short message in the toast, for ms milliseconds. */
        void toast (const std::string& text, int ms = 2400) const;

    private:
        struct Live
        {
            std::string key;                   // "walk", "hum", "gen:pattern:tone" (sl-ui-panels.js live.key)
            model::ClipPtr base;               // the clip the session started from
            std::vector<std::string> ids;      // the selection it acts on
            std::string label;
            model::ClipPtr last;               // what its last step made
            bool dirty = false;                // the last step is not an undo step yet
        };

        bool act (model::EditResult result, const std::string& label);
        void liveRun (const std::string& key, const std::string& label, const std::function<model::Clip (const model::Clip&)>& make, bool final);
        void ghost (std::optional<model::Clip> clip) const;
        void doConnect (model::ConnectMode mode, const std::string& label);
        void humRun (bool final, bool click, const std::string& label);
        void laneTool (const std::string& name, const std::function<model::Clip (const model::Clip&, core::LaneId)>& make);
        model::Clip walkClip (const model::Clip& base) const;
        model::Clip humanizeClip (const model::Clip& base) const;

        ToolHost host;
        RollGestures& gestures;
        ToolSettings set;
        std::optional<Live> live;
        GhostPreview* ghosts = nullptr;
        int lastMode = 0, lastLane = -1;   // the mode and lane sync last saw (a change ends a live session)

        // what sync last saw: another clip, selection, primary or lane reads the primary's generator again
        model::ClipPtr seenClip;
        std::vector<std::string> seenSelection;
        std::string seenPrimary;
        int seenLane = -1;
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

        /** Opens a drawer (the lane menu's Pattern settings... and Random settings...): it stays open when it is. */
        void showDrawer (ToolSettings::Drawer d);

        static constexpr int lineHeight = 30;
        static constexpr int drawerPadX = 8, drawerPadY = 7, drawerMaxHeight = 320;
        static constexpr int drawerScrollBar = 8;

        /** The least room an open drawer takes. The prototype's fitLayout keeps 60 px (PART_MIN); this is 62, so a
            drawer that scrolls shows its scroll bar: JUCE draws no thumb on a bar shorter than 32 px plus its smallest
            thumb (twice the bar's thickness), and the drawer's padding takes 14 px. */
        static constexpr int drawerMinHeight = 2 * drawerPadY + 32 + 2 * drawerScrollBar;

        /** The most the open drawer may take (MainView: what leaves the roll its room on this window); a taller drawer
            scrolls. At least drawerMinHeight. */
        void setDrawerMax (int px);
        int getDrawerMax() const noexcept { return drawerMax; }

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

        /** The view that scrolls a drawer taller than its room. */
        juce::Viewport& getDrawerView() noexcept { return drawerView; }

        /** The line's buttons' hover hooks (their ghosts and hints), for the tests. */
        HoverHooks& getHooks() noexcept { return *hooks; }

        /** The mouse left the open drawer: its own preview goes (sl-ui-panels.js D.drawer's mouseleave). */
        void drawerLeft();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class LaneBox;
        class DrawerWatch;

        void openDrawer (ToolSettings::Drawer d);
        void rebuildDrawer();
        void drawerHeightChanged();
        int drawerKey() const;   // what a lane drawer is built for: the lane, and on Travel the notes' kind

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
        std::unique_ptr<DrawerWatch> drawerWatch;                   // the mouse leaving the drawer
        std::unique_ptr<HoverHooks> hooks;                          // the line's buttons: their ghosts and hints
        ToolSettings::Drawer shownDrawer = ToolSettings::Drawer::none;
        int shownKey = -1;                                          // drawerKey () when the drawer was built
        juce::Rectangle<int> drawerArea;
        bool idle = true;
        int lastWantedHeight = -1;
        int drawerMax = drawerMaxHeight;

        /** What the open drawer showed at its last sync: sync () runs on the editor's timer, and the drawer is synced
            again only when one of these changed (its own controls sync it themselves). */
        struct Seen
        {
            model::ClipPtr clip;
            std::vector<std::string> ids;
            int lane = -1;
            double bpm = 0.0, seconds = 0.0, regionStart = 0.0, regionEnd = 0.0;
            int scope = 0;   // Each note or Across (a menu can change it while the Shapes drawer is open)

            bool operator== (const Seen& o) const noexcept;
        };
        Seen seen;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SelectionBar)
    };
}
