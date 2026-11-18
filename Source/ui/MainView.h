#pragma once

// The editor's whole layout in logical pixels (PLUGIN-PLAN 4 M0): the v4 prototype's page (shell.html) with
// what is built behind it. From the top: the top bar (logo, wordmark, Undo and Redo, theme toggle, Keys, the
// host's tempo, the Grid, UI Scale), the state banner when there is one, the mode bar (1 Build .. 8 Sample with
// the four Sample chips and the Nodes switch), the selection row (what is selected, Select all, Duplicate, Delete,
// Clear all, and the roll's Freehand, Slide into and Multi switches) with the selection bar under it (M5: the Lane,
// the drawer buttons, Connect and Tools, and an open drawer) and the inspector under that, the tool tabs and their
// pane, the piano roll (ui/roll/RollView, M4), the waveform strip with the loaded sound (ui/strip/WaveStrip), and the
// hint bar (ui/HintBar). The Diagnostics tab takes the pane and the roll's room.
//
// The editor scales this component as one piece (the UI Scale) and owns the theme, the Keys switch and the
// drops; this view only shows them and reports clicks through its callbacks. With Keys on, the editor hands
// every key to handleKey: the KeyRouter's shortcuts (M4, 7.6 decision 3) act on the roll.

#include "diag/DiagModel.h"
#include "doc/SampleManager.h"
#include "sounds/StarterSounds.h"
#include "ui/Controls.h"
#include "ui/DiagnosticsPage.h"
#include "ui/HintBar.h"
#include "ui/KeyRouter.h"
#include "ui/Toast.h"
#include "ui/roll/Inspector.h"
#include "ui/roll/RollView.h"
#include "ui/strip/WaveStrip.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class StrandLockProcessor;

namespace sl::ui
{
    class TabPanel;

    /** The top bar's Undo and Redo buttons: a curved arrow (shell.html's undo and redo signs), drawn as a path. */
    class HistoryButton final : public FlatButton
    {
    public:
        HistoryButton (const Theme&, bool isRedo);
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    private:
        bool redo;
    };

    class MainView final : public juce::Component
    {
    public:
        MainView (const Theme&, StrandLockProcessor&);
        ~MainView() override;

        // What the editor does when these are clicked.
        std::function<void()> onThemeToggle;
        std::function<void()> onKeysToggle;
        std::function<void (int)> onUiScale;

        /** The theme changed: icons, texts and colours follow. */
        void themeChanged();
        void setKeysOn (bool on);
        void setUiScalePercent (int percent);

        /** A key the editor received: the KeyRouter runs its shortcut on the roll (with Keys on). True when
            StrandLock keeps the key; false sends it on to Cubase (every key with Keys off, Space always). While an
            inspector box is typed in, every key is the box's and nothing runs; while the Diagnostics tab hides the
            roll, no shortcut runs; with the right-click menu open, Escape closes it. */
        bool handleKey (const juce::KeyPress& key, bool keysOn);

        /** A key went up (the editor's keyStateChanged). */
        void keyReleased();

        /** The editor's timer: the host's tempo, the roll (what the document holds now), the Diagnostics page, the
            loaded sound, the Region and the banner. */
        void tick (const diag::DiagModel::Snapshot&, bool keysOn);

        /** Shows what the roll holds now: the selection row's text and buttons, the switches, the inspector, the
            hint and the Undo and Redo buttons (after every roll event, and on the editor's timer). */
        void syncRollControls();

        void showDropOverlay (bool show, const juce::String& what = {});

        /** A short message over the top of the roll for ms milliseconds (sl-ui.js toast: 2.4 s unless it says). */
        void showToast (const juce::String& text, int ms = 2600);
        const juce::String& getToastText() const noexcept;
        bool isToastShown() const noexcept;

        /** The editor's 30 Hz timer (PLUGIN-PLAN 7.4 decision 1): moves every control to its parameter's value
            where that changed (never one the user is changing). Returns how many moved. */
        int syncControls();
        const std::vector<ParamSynced*>& getSyncedControls() const noexcept { return synced; }

        // The tool tabs (UIST "tab" names: sample macros note glide copy mods sound diag).
        enum Tab { sampleTab, macrosTab, noteTab, slidesTab, copyTab, modsTab, soundTab, diagTab, numTabs };
        void openTab (int tab);
        int getOpenTab() const noexcept { return openTabIndex; }
        void setFolded (bool shouldFold);
        bool isFolded() const noexcept { return folded; }

        /** Mode 1 .. 8; for mode 8 also the Sample sub-lane (0 Travel .. 3 Refresh). */
        void setMode (int mode, int subLane = -1);
        int getMode() const noexcept { return mode; }
        int getSubLane() const noexcept { return subLane; }

        // For the tests.
        FlatButton& getThemeButton() noexcept { return themeButton; }
        FlatButton& getKeysButton() noexcept { return keysButton; }
        FlatButton& getUndoButton() noexcept { return undoButton; }
        FlatButton& getRedoButton() noexcept { return redoButton; }
        FlatButton& getSelectAllButton() noexcept { return selectAll; }
        FlatButton& getDuplicateButton() noexcept { return duplicate; }
        FlatButton& getDeleteButton() noexcept { return deleteNotes; }
        FlatButton& getClearAllButton() noexcept { return clearAll; }
        FlatButton& getFreehandButton() noexcept { return freehandButton; }
        FlatButton& getSlideIntoButton() noexcept { return linkButton; }
        FlatButton& getMultiButton() noexcept { return multiButton; }
        juce::ComboBox& getGridBox() noexcept { return gridBox; }
        juce::ComboBox& getUiScaleBox() noexcept { return uiScaleBox; }
        TabStrip& getTabStrip() noexcept { return tabStrip; }
        DiagnosticsPage& getDiagnosticsPage() noexcept { return *diagPage; }
        WaveStrip& getStrip() noexcept { return strip; }
        roll::RollView& getRoll() noexcept { return roll; }
        roll::SelectionBar& getSelectionBar() noexcept { return selectionBar; }
        FlatButton& getNodesButton() noexcept { return nodesButton; }
        roll::Inspector& getInspector() noexcept { return inspector; }
        HintBar& getHintBar() noexcept { return hintBar; }
        FlatButton& getModeButton (int modeNumber) noexcept;
        TabPanel* getTabPanel (int tab) const noexcept;
        const juce::String& getTempoText() const noexcept { return tempoText; }
        const juce::String& getHintText() const noexcept { return hintBar.getHint(); }
        const juce::String& getSelectionText() const noexcept { return selInfo.getText(); }
        const juce::String& getBannerText() const noexcept { return bannerText; }
        juce::Rectangle<int> getTopBarArea() const noexcept { return topBarArea; }
        juce::Rectangle<int> getSelectionRowArea() const noexcept { return selRowArea; }
        juce::Rectangle<int> getRollArea() const noexcept { return roll.getBounds(); }
        juce::Rectangle<int> getStripArea() const noexcept { return strip.getBounds(); }
        juce::Rectangle<int> getPaneArea() const noexcept { return paneArea; }
        juce::Viewport& getPaneViewport() noexcept { return paneViewport; }
        juce::Rectangle<int> getLogoArea() const noexcept { return logoArea; }
        juce::Rectangle<int> getWordmarkArea() const noexcept { return wordmarkArea; }

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        /** The grid steps the Grid menu offers, in beats (1/4, 1/8, 1/16, 1/32, Free). */
        static constexpr std::array<double, 5> gridSteps { 1.0, 0.5, 0.25, 0.125, 0.0 };

    private:
        class LaneWrap;
        class FoldButton;

        void buildModeBar();
        void paintModes();
        void updateHint();
        void saveUiState();
        void layoutPane();
        void layoutSelectionRow();
        void showLane (core::LaneId lane); // the selection bar's Lane picks a mode

        /** The selection bar's room follows what it wants (its line while notes are selected or a drawer is open, the
            drawer under it), as the prototype's bar shows and hides. While the mouse is on the roll (or a gesture is
            held) it may grow but not shrink, and when it grows the roll scrolls by as much, so the notes stay where
            they are under the mouse (sl-ui.js holdStart and keepRollStill); it settles when the mouse leaves the roll
            (the editor's timer). */
        void updateBarHeight();
        bool rollHeld();

        void disarmClear();
        int toastTop() const noexcept;   // where the toast sits

        const Theme& theme;
        StrandLockProcessor& processor;

        // Top bar.
        HistoryButton undoButton, redoButton;
        FlatButton themeButton, keysButton;
        Caption tempoLabel, gridLabel, scaleLabel, status;
        juce::ComboBox gridBox, uiScaleBox;
        juce::String tempoText { "-" };

        // Mode bar and selection row.
        FlatButton buildButton, nodesButton;
        std::vector<std::unique_ptr<LaneWrap>> laneWraps;   // modes 2 .. 8
        std::vector<std::unique_ptr<FlatButton>> subChips;  // Travel, Heads, Grain, Refresh
        Caption selInfo;
        FlatButton selectAll, duplicate, deleteNotes, clearAll;
        FlatButton freehandButton, linkButton, multiButton;

        // Tabs.
        TabStrip tabStrip;
        std::unique_ptr<FoldButton> foldButton;
        std::array<std::unique_ptr<juce::Component>, numTabs> panels;   // none for diagTab (the page has its own viewport)
        juce::Component paneContent;   // every panel, so a theme switch and the focus rules reach the hidden ones too
        juce::Viewport paneViewport, diagViewport;
        std::unique_ptr<DiagnosticsPage> diagPage;

        roll::RollView roll;
        roll::SelectionBar selectionBar;   // M5: the tools for the selected notes, under the selection row
        roll::Inspector inspector;
        WaveStrip strip;
        HintBar hintBar;
        std::vector<ParamSynced*> synced;   // every control bound to a parameter (the panels and the strip)
        std::unique_ptr<Toast> toast;
        KeyRouter keyRouter;

        int openTabIndex = sampleTab;
        bool folded = false;
        int mode = 1, subLane = 0;
        bool keysOn = false;
        juce::String bannerText;
        bool dropShown = false;
        juce::String dropText;
        int ticks = 0;
        double clearArmedAt = -1.0;   // when Clear all was clicked once (milliseconds), or -1
        int barHeight = 0;            // the room the selection bar has now (updateBarHeight)

        // Areas from resized(), for paint().
        juce::Rectangle<int> card, topBarArea, bannerArea, modeBarArea, selRowArea, tabsArea, paneArea, hintArea;
        juce::Rectangle<int> logoArea, wordmarkArea;
        std::vector<int> topBarSeparators;
        int selRowSeparator = -1;   // the line between the selection's buttons and the roll's switches
        std::vector<FlowItem> modeItems;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
    };
}
