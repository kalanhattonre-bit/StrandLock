#pragma once

// The editor's whole layout in logical pixels (PLUGIN-PLAN 4 M0): the v4 prototype's page (shell.html) with
// what M0 has behind it. From the top: the top bar (logo, wordmark, theme toggle, Keys, the host's tempo, UI
// Scale), the state banner when there is one, the mode bar (1 Build .. 8 Sample with the four Sample chips),
// the selection row, the tool tabs and their pane, the roll (a placeholder until M4), the waveform strip with
// the loaded sound (ui/strip/WaveStrip), and the hint bar. The Diagnostics tab takes the pane and the roll's room.
//
// The editor scales this component as one piece (the UI Scale) and owns the theme, the Keys switch and the
// drops; this view only shows them and reports clicks through its callbacks.

#include "diag/DiagModel.h"
#include "doc/SampleManager.h"
#include "sounds/StarterSounds.h"
#include "ui/Controls.h"
#include "ui/DiagnosticsPage.h"
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

    /** The empty roll until M4: ruler, piano keys, rows and grid in the theme's colours, the host's playhead,
        and one line saying what comes when. */
    class RollPlaceholder final : public juce::Component
    {
    public:
        explicit RollPlaceholder (const Theme&);

        /** Where the host is, in beats, while it plays (nothing while stopped). */
        void setPlayhead (std::optional<double> beat);

        void paint (juce::Graphics&) override;

        static constexpr int pianoWidth = 40, rulerHeight = 20, rowHeight = 14, bars = 4, topNote = 79;

    private:
        const Theme& theme;
        std::optional<double> playhead;
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

        /** The editor's timer: the host's tempo and position, the Diagnostics page, the loaded sound, the
            Region and the banner. */
        void tick (const diag::DiagModel::Snapshot&, bool keysOn);

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
        juce::ComboBox& getUiScaleBox() noexcept { return uiScaleBox; }
        TabStrip& getTabStrip() noexcept { return tabStrip; }
        DiagnosticsPage& getDiagnosticsPage() noexcept { return *diagPage; }
        WaveStrip& getStrip() noexcept { return strip; }
        RollPlaceholder& getRoll() noexcept { return roll; }
        FlatButton& getModeButton (int modeNumber) noexcept;
        TabPanel* getTabPanel (int tab) const noexcept;
        const juce::String& getTempoText() const noexcept { return tempoText; }
        const juce::String& getHintText() const noexcept { return hintText; }
        const juce::String& getBannerText() const noexcept { return bannerText; }
        juce::Rectangle<int> getTopBarArea() const noexcept { return topBarArea; }
        juce::Rectangle<int> getRollArea() const noexcept { return roll.getBounds(); }
        juce::Rectangle<int> getStripArea() const noexcept { return strip.getBounds(); }
        juce::Rectangle<int> getPaneArea() const noexcept { return paneArea; }
        juce::Viewport& getPaneViewport() noexcept { return paneViewport; }
        juce::Rectangle<int> getLogoArea() const noexcept { return logoArea; }
        juce::Rectangle<int> getWordmarkArea() const noexcept { return wordmarkArea; }

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        class LaneWrap;
        class FoldButton;
        class ToastView;

        void buildModeBar();
        void paintModes();
        void updateHint();
        void saveUiState();
        void layoutPane();
        int toastTop() const noexcept;   // where the toast sits

        const Theme& theme;
        StrandLockProcessor& processor;

        // Top bar.
        FlatButton themeButton, keysButton;
        Caption tempoLabel, scaleLabel, status;
        juce::ComboBox uiScaleBox;
        juce::String tempoText { "-" };

        // Mode bar and selection row.
        FlatButton buildButton, nodesButton;
        std::vector<std::unique_ptr<LaneWrap>> laneWraps;   // modes 2 .. 8
        std::vector<std::unique_ptr<FlatButton>> subChips;  // Travel, Heads, Grain, Refresh
        Caption selInfo;
        FlatButton selectAll, duplicate, deleteNotes, clearAll;

        // Tabs.
        TabStrip tabStrip;
        std::unique_ptr<FoldButton> foldButton;
        std::array<std::unique_ptr<juce::Component>, numTabs> panels;   // none for diagTab (the page has its own viewport)
        juce::Component paneContent;   // every panel, so a theme switch and the focus rules reach the hidden ones too
        juce::Viewport paneViewport, diagViewport;
        std::unique_ptr<DiagnosticsPage> diagPage;

        RollPlaceholder roll;
        WaveStrip strip;
        std::vector<ParamSynced*> synced;   // every control bound to a parameter (the panels and the strip)
        std::unique_ptr<ToastView> toast;

        int openTabIndex = sampleTab;
        bool folded = false;
        int mode = 1, subLane = 0;
        bool keysOn = false;
        juce::String hintText, bannerText;
        bool dropShown = false;
        juce::String dropText;
        int ticks = 0;

        // Areas from resized(), for paint().
        juce::Rectangle<int> card, topBarArea, bannerArea, modeBarArea, selRowArea, tabsArea, paneArea, hintArea;
        juce::Rectangle<int> logoArea, wordmarkArea;
        std::vector<int> topBarSeparators;
        std::vector<FlowItem> modeItems;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
    };
}
