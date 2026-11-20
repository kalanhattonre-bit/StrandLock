#include "MainView.h"

#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "core/Version.h"
#include "params/ParamIds.h"
#include "ui/Logo.h"
#include "ui/roll/LaneMenu.h"
#include "ui/tabs/CopyPasteTab.h"
#include "ui/tabs/MacrosTab.h"
#include "ui/tabs/NoteSettingsTab.h"
#include "ui/tabs/SampleTab.h"
#include "ui/tabs/SlidesTab.h"
#include "ui/tabs/SoundTab.h"

#include <cmath>

namespace sl::ui
{
    namespace
    {
        constexpr int kSideMargin = 12, kTopMargin = 8;
        constexpr int kTopBarHeight = 40, kBannerHeight = 28, kSelRowHeight = 36, kTabsHeight = 30;
        constexpr int kInspectorRowHeight = 32;   // the inspector's line under the selection row (M4)
        constexpr int kHintHeight = 30;
        constexpr int kPaneScrollBar = 8;
        constexpr int kMinScrollingPane = 32 + 2 * 2 * kPaneScrollBar;   // LookAndFeel_V2: minimum thumb = 2 x thickness
        constexpr int kComingHeight = 32;   // a tab that is not built yet: its one line
        constexpr int kBarPadX = 8, kItemHeight = 26, kGap = 5, kModeGap = 4;
        constexpr int kHistoryButtonWidth = 30, kGridBoxWidth = 70;
        constexpr int kZoomGap = 10;   // between the zoom buttons and Hide / Show in the tab row
        constexpr double kClearArmMs = 2500.0;   // sl-ui.js Clear all: the second click within 2.5 s

        const char* const kTabNames[] = { "Sample", "Macros", "Note settings", "Slides", "Copy / Paste", "Mod wheels", "Sound", "Diagnostics" };
        const char* const kTabIds[] = { "sample", "macros", "note", "glide", "copy", "mods", "sound", "diag" };

        // What each tab that is not built yet will hold, and when.
        const char* const kComing[] = {
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            "The two mod wheels, MIDI Learn and Let go arrive in M8.",
            nullptr,
            nullptr,
        };

        // Decision 6: a click on a tab while the window is too short for its controls
        constexpr const char* kTooShort = "This window is too short to show the tab. Make the window taller and the tab opens by itself.";

        // The banner when the clip's lanes would need more memory than a snapshot may hold (doc/SnapshotPublisher.h).
        constexpr const char* kTooDetailed = "This clip is too detailed to play, so its notes are silent: it needs fewer notes or simpler lanes";

        const int kScales[] = { 75, 100, 125, 150, 175, 200 };

        // shell.html's Grid menu
        const char* const kGridNames[] = { "1/4", "1/8", "1/16", "1/32", "Free" };

        // The clear button's two words (sl-ui.js: the first click arms it)
        constexpr const char* kClearAll = "Clear all";
        constexpr const char* kClickAgain = "Click again to clear";

        Tok laneTok (core::LaneId id)
        {
            switch (id)
            {
                case core::LaneId::pitch:   return Tok::lanePitch;
                case core::LaneId::amp:     return Tok::laneAmp;
                case core::LaneId::vib:     return Tok::laneVib;
                case core::LaneId::tone:    return Tok::laneTone;
                case core::LaneId::formant: return Tok::laneFormant;
                case core::LaneId::hollow:  return Tok::laneHollow;
                case core::LaneId::travel:
                case core::LaneId::heads:
                case core::LaneId::grain:
                case core::LaneId::refresh: return Tok::laneSample;
            }
            return Tok::laneSample;
        }

        /** "Slide into" and the ellipsis (U+2026), as the prototype's button. */
        juce::String slideIntoText()
        {
            return "Slide into" + juce::String::charToString ((juce::juce_wchar) 0x2026);
        }

        /** A pane that is not built yet: one plain line saying what comes there, and when. */
        class ComingPanel final : public juce::Component
        {
        public:
            ComingPanel (const Theme& t, const juce::String& text) : line (t, text, Caption::Style::sub)
            {
                addAndMakeVisible (line);
            }

            void resized() override { line.setBounds (getLocalBounds().reduced (8, 6).withHeight (20)); }

        private:
            Caption line;
        };
    }

    //==============================================================================================
    HistoryButton::HistoryButton (const Theme& t, bool isRedo)
        : FlatButton (t, {}), redo (isRedo)
    {
    }

    void HistoryButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
    {
        FlatButton::paintButton (g, isMouseOver, isButtonDown);

        // the prototype's undo and redo signs: half a circle over the top, the arrow head at its end pointing down
        auto r = getLocalBounds().toFloat();
        if (isButtonDown && isEnabled())
            r = r.translated (0.0f, 1.0f);
        const auto c = r.getCentre().translated (0.0f, 1.5f);
        const float radius = 4.6f;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        const float tipX = redo ? c.x + radius : c.x - radius;
        juce::Path head;
        head.addTriangle (tipX - 3.2f, c.y - 0.6f, tipX + 3.2f, c.y - 0.6f, tipX, c.y + 3.6f);
        const auto ink = theme (Tok::txt).withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f);
        g.setColour (ink);
        g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillPath (head);
    }

    //==============================================================================================
    /** A lane mode button and its menu arrow, joined (shell.html's .lanewrap). */
    class MainView::LaneWrap final : public juce::Component
    {
    public:
        LaneWrap (const Theme& t, const core::ModeInfo& info, Tok colour)
            : modeNumber (info.mode), lane (t, info.name), arrow (t, {}, FlatButton::Icon::chevronDown)
        {
            lane.setKeyChip (juce::String::charToString ((juce::juce_wchar) info.key));
            lane.setLaneColour (colour);
            lane.setJoins (false, true);
            lane.setClickingTogglesState (false);
            arrow.setLaneColour (colour);
            arrow.setJoins (true, false);
            arrow.setTooltip ("Shapes, patterns and random for the selected notes");
            addAndMakeVisible (lane);
            addAndMakeVisible (arrow);
        }

        int preferredWidth() const { return lane.preferredWidth() + arrowWidth; }

        void resized() override
        {
            auto r = getLocalBounds();
            arrow.setBounds (r.removeFromRight (arrowWidth));
            lane.setBounds (r);
        }

        const int modeNumber;
        FlatButton lane, arrow;
        static constexpr int arrowWidth = 20;
    };

    /** The tab row's "Hide" / "Show" at its right end (shell.html's .tab.fold). */
    class MainView::FoldButton final : public juce::Button
    {
    public:
        explicit FoldButton (const Theme& t) : juce::Button ("Hide"), theme (t) {}

        void paintButton (juce::Graphics& g, bool isMouseOver, bool) override
        {
            const bool foldedNow = getToggleState();
            const auto ink = isMouseOver ? theme (Tok::txt) : theme (Tok::dim);
            auto r = getLocalBounds().toFloat();
            paintIcon (g, foldedNow ? FlatButton::Icon::chevronDown : FlatButton::Icon::chevronUp,
                       r.removeFromLeft (14.0f).withSizeKeepingCentre (10.0f, 10.0f), ink, theme (Tok::panel2));
            g.setColour (ink);
            g.setFont (Theme::px (12.0f, true));
            g.drawText (foldedNow ? "Show" : "Hide", r, juce::Justification::centredLeft, false);
        }

    private:
        const Theme& theme;
    };

    //==============================================================================================
    MainView::MainView (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          undoButton (t, false),
          redoButton (t, true),
          themeButton (t, "Light", FlatButton::Icon::sun),
          keysButton (t, "Keys", FlatButton::Icon::keys),
          tempoLabel (t, "Tempo (host)", Caption::Style::label),
          gridLabel (t, "Grid", Caption::Style::label),
          scaleLabel (t, "UI Scale", Caption::Style::label),
          status (t, juce::String ("Test build ") + core::kPluginVersion, Caption::Style::sub),
          buildButton (t, "Build"),
          nodesButton (t, "Nodes", FlatButton::Icon::nodes),
          selInfo (t, "No notes selected", Caption::Style::sub),
          selectAll (t, "Select all"),
          duplicate (t, "Duplicate"),
          deleteNotes (t, "Delete"),
          clearAll (t, kClearAll),
          freehandButton (t, "Freehand"),
          linkButton (t, slideIntoText()),
          multiButton (t, "Multi"),
          tabStrip (t, juce::StringArray (kTabNames, numTabs)),
          roll (t, p),
          selectionBar (t, roll),
          inspector (t, p),
          strip (t, p),
          hintBar (t)
    {
        // Top bar: Undo and Redo (with Keys off too: PLUGIN-PLAN 7.6 decision 3), then M0's controls and the Grid.
        undoButton.onClick = [this] { roll.undo(); };
        redoButton.onClick = [this] { roll.redo(); };
        themeButton.setTooltip ("Dark or light (remembered for every project)");
        themeButton.onClick = [this] { if (onThemeToggle) onThemeToggle(); };
        keysButton.setTooltip ("Computer keyboard. Off: every key goes to Cubase. On: StrandLock takes the keys when you click it (never Space).");
        keysButton.onClick = [this]
        {
            if (onKeysToggle)
                onKeysToggle();
            // sl-ui.js's Keys button says what the keys do now (A to K and Z / X are the live keyboard, M8)
            showToast (keysOn ? "Keys on: 1 to 8 change mode, Ctrl+Z undo, Delete deletes the selected notes. Space still goes to Cubase."
                              : "Keys off: every key goes to Cubase");
        };
        for (int i = 0; i < (int) gridSteps.size(); ++i)
            gridBox.addItem (kGridNames[i], i + 1);
        gridBox.setTooltip ("Where notes land when you add, move or stretch them (Free: anywhere)");
        gridBox.setTitle ("Grid");
        gridBox.onChange = [this]
        {
            const int i = gridBox.getSelectedId() - 1;
            if (i >= 0 && i < (int) gridSteps.size())
                roll.setGrid (gridSteps[(std::size_t) i]);
        };
        for (int s : kScales)
            uiScaleBox.addItem (juce::String (s) + " %", s);
        uiScaleBox.setTooltip ("Make everything in StrandLock bigger or smaller");
        uiScaleBox.setTitle ("UI Scale");
        uiScaleBox.onChange = [this] { if (onUiScale && uiScaleBox.getSelectedId() > 0) onUiScale (uiScaleBox.getSelectedId()); };
        for (juce::Component* c : { (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &themeButton,
                                    (juce::Component*) &keysButton, (juce::Component*) &tempoLabel, (juce::Component*) &gridLabel,
                                    (juce::Component*) &gridBox, (juce::Component*) &scaleLabel, (juce::Component*) &status,
                                    (juce::Component*) &uiScaleBox })
            addAndMakeVisible (c);

        // Mode bar and selection row (sl-ui.js wire's selection row and the top bar's roll switches).
        buildModeBar();
        selInfo.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (selInfo);
        selectAll.setTooltip ("Select every note (Ctrl+A while Keys is on)");
        selectAll.onClick = [this] { roll.selectAll(); };
        duplicate.setTooltip ("Duplicate the selected notes (Ctrl+D while Keys is on)");
        duplicate.onClick = [this] { roll.duplicate(); };
        deleteNotes.setTooltip ("Delete the selected notes (Delete while Keys is on)");
        deleteNotes.onClick = [this] { roll.deleteSelection(); };
        clearAll.setTooltip ("Remove every note (click twice)");
        clearAll.onClick = [this]
        {
            // two clicks: the first arms it for 2.5 s, the second clears (one undo step)
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (clearArmedAt >= 0.0 && now - clearArmedAt < kClearArmMs)
            {
                disarmClear();
                roll.clearAll();
                return;
            }
            clearArmedAt = now;
            clearAll.setButtonText (kClickAgain);
            layoutSelectionRow();
            repaint (selRowArea);
        };
        freehandButton.setTooltip ("Paint the active lane of the selected note by hand");
        freehandButton.onClick = [this] { roll.toggleFreehand(); };
        linkButton.setTooltip ("Make the selected note slide into the next note you click (or Shift+click it)");
        linkButton.onClick = [this] { roll.armLink(); };
        multiButton.setTooltip ("Each click adds a note to the selection (same as Ctrl+click)");
        multiButton.onClick = [this] { roll.setMulti (! multiButton.getToggleState()); };
        for (auto* b : { &selectAll, &duplicate, &deleteNotes, &clearAll, &freehandButton, &linkButton, &multiButton })
            addAndMakeVisible (*b);
        // the selection bar (M5) under the selection row: a drawer that opens or closes moves what is below it
        addAndMakeVisible (selectionBar);
        selectionBar.onHeightChange = [this] { updateBarHeight(); };
        roll.getTools().onShowLane = [this] (core::LaneId lane) { showLane (lane); };
        addAndMakeVisible (inspector);

        // Tabs and panes. While the window is too short for the open tab (decision 6), a click on a tab or Show says so.
        tabStrip.onClick = [this] (int index)
        {
            if (index == openTabIndex && ! folded && ! autoFolded)
                setFolded (true);
            else
                openTab (index);
            tooShortToast();
        };
        addAndMakeVisible (tabStrip);
        foldButton = std::make_unique<FoldButton> (t);
        foldButton->setTooltip ("Fold the panel away to give the roll more room (or click the open tab)");
        foldButton->onClick = [this]
        {
            if (! (autoFolded && ! folded))
                setFolded (! folded);
            tooShortToast();
        };
        addAndMakeVisible (*foldButton);

        // The roll's zoom buttons (the view owns them) at the right end of the tab row: at the right end of the ruler
        // they hid its last bar numbers (M4 review UI4-6, decision 6).
        for (int i = 0; i < 4; ++i)
            addAndMakeVisible (roll.getZoomButton (i));

        auto& apvts = processor.getApvts();
        {
            // M6 T1: the full Sample and Macros tabs, whose controls fade and explain themselves in the hint line; T3: the
            // Sample tab's recipes draw their notes dashed on the roll
            auto sp = std::make_unique<SampleTab> (t, apvts, processor, roll);
            auto mp = std::make_unique<MacrosTab> (t, apvts, processor);
            samplePanel = sp.get();
            macrosPanel = mp.get();
            samplePanel->onHint = [this] (const juce::String& text) { hintBar.setHelp (text); };
            samplePanel->onToast = [this] (const juce::String& text, int ms) { showToast (text, ms); };
            samplePanel->onLayoutChange = [this] { resized(); };
            macrosPanel->onHint = [this] (const juce::String& text) { hintBar.setHelp (text); };
            macrosPanel->onToast = [this] (const juce::String& text, int ms) { showToast (text, ms); };
            macrosPanel->onLayoutChange = [this] { resized(); };
            panels[sampleTab] = std::move (sp);
            panels[macrosTab] = std::move (mp);
        }
        panels[soundTab] = std::make_unique<SoundTab> (t, apvts);
        {
            // M5 S3: the Slides and Copy / Paste tabs act on the roll's notes
            auto slidesPanel = std::make_unique<SlidesTab> (t, apvts, roll, processor);
            auto copyPanel = std::make_unique<CopyPasteTab> (t, apvts, roll, processor);
            slides = slidesPanel.get();
            copyPaste = copyPanel.get();
            slides->onLayoutChange = [this] { resized(); };
            copyPaste->onLayoutChange = [this] { resized(); };
            panels[slidesTab] = std::move (slidesPanel);
            panels[copyTab] = std::move (copyPanel);
        }
        {
            // M6 T2: the Note settings tab (S7) changes the selected notes through the roll's tools
            auto ns = std::make_unique<NoteSettingsTab> (t, apvts, roll, processor);
            noteSettings = ns.get();
            noteSettings->onLayoutChange = [this] { resized(); };
            panels[noteTab] = std::move (ns);
        }
        for (int i = 0; i < numTabs; ++i)
            if (kComing[i] != nullptr)
                panels[(std::size_t) i] = std::make_unique<ComingPanel> (t, kComing[i]);

        for (auto& panel : panels)
            if (panel != nullptr)
                paneContent.addChildComponent (*panel);
        paneViewport.setScrollBarsShown (true, false);
        paneViewport.setScrollBarThickness (kPaneScrollBar);
        paneViewport.setViewedComponent (&paneContent, false);
        addChildComponent (paneViewport);

        diagPage = std::make_unique<DiagnosticsPage> (t, p);
        diagPage->onCopied = [this] { showToast ("Report copied: paste it into your reply"); };
        diagViewport.setScrollBarsShown (true, false);
        diagViewport.setScrollBarThickness (8);
        diagViewport.setViewedComponent (diagPage.get(), false);
        addChildComponent (diagViewport);

        // The roll, its inspector and the hint bar: every roll event shows its result in the rows above and below.
        addAndMakeVisible (roll);
        roll.setMenuParent (this);   // the right-click menu opens over the whole page, as the prototype's #menu
        roll.onToast = [this] (const juce::String& text) { showToast (text); };
        roll.onToastFor = [this] (const juce::String& text, int ms) { showToast (text, ms); };
        roll.onEdit = [this] { syncRollControls(); };
        roll.onPress = [this] { inspector.finishEditing(); };
        roll.onHint = [this] (const juce::String& text) { hintBar.setHelp (text); };   // a control's text while the mouse is on it
        inspector.onToast = [this] (const juce::String& text, int ms) { showToast (text, ms); };
        inspector.onEdit = [this]
        {
            roll.refresh();
            syncRollControls();
        };
        addAndMakeVisible (strip);
        strip.onToast = [this] (const juce::String& text, int ms) { showToast (text, ms); };
        strip.onEdit = [this]
        {
            // a click on the waveform set the selected notes' Start (M6): the roll and the rows show it
            roll.refresh();
            syncRollControls();
        };
        addAndMakeVisible (hintBar);
        toast = std::make_unique<Toast> (t);
        addChildComponent (*toast);
        addChildComponent (hintBar.getHover());

        // The keys (with Keys on): the prototype's shortcuts, on the roll.
        KeyActions keys;
        keys.undo = [this] { roll.undo(); };
        keys.redo = [this] { roll.redo(); };
        keys.deleteSelection = [this] { roll.deleteSelection(); };
        keys.selectAll = [this] { roll.selectAll(); };
        keys.duplicate = [this] { roll.duplicate(); };
        keys.copyLanes = [this] { roll.copyLanes(); };
        keys.pasteLanes = [this] { roll.pasteLanes(); };
        keys.clearSelection = [this] { roll.clearSelection(); };
        keys.armLink = [this] { roll.armLink(); };
        keys.toggleNodes = [this] { roll.toggleNodes(); };
        keys.setMode = [this] (int m) { setMode (m); };
        keyRouter.actions = std::move (keys);

        // Every control bound to a parameter, for the 30 Hz timer (decision 1). The tree never changes after this.
        collectSynced (*this, synced);

        // Where the person left the page (UIST).
        const auto ui = processor.getUiState();
        mode = juce::jlimit (1, 8, (int) ui.mode);
        for (int i = 0; i < 4; ++i)
            if (ui.subLane == core::laneInfo (core::kSampleLanes[i]).code)
                subLane = i;
        roll.setMode (mode, subLane);
        for (int i = 0; i < (int) gridSteps.size(); ++i)
            if (juce::exactlyEqual (gridSteps[(std::size_t) i], roll.getGrid()))
                gridBox.setSelectedId (i + 1, juce::dontSendNotification);
        for (int i = 0; i < numTabs; ++i)
            if (ui.tab == kTabIds[i])
                openTabIndex = i;
        // openTab unfolds the pane (and saves that), so the saved fold is kept aside and put back after it.
        const bool startFolded = ui.folded != 0;
        tabStrip.setSelected (openTabIndex);

        themeChanged();
        paintModes();
        syncRollControls();
        openTab (openTabIndex);
        if (startFolded)
            setFolded (true);
    }

    MainView::~MainView()
    {
        if (samplePanel != nullptr)
        {
            samplePanel->onLayoutChange = nullptr;
            samplePanel->onHint = nullptr;
            samplePanel->onToast = nullptr;
            samplePanel->releaseRoll();   // the roll goes before the panels: the recipes' hover hooks let go of its ghosts now
        }
        if (macrosPanel != nullptr)
        {
            macrosPanel->onLayoutChange = nullptr;
            macrosPanel->onHint = nullptr;
            macrosPanel->onToast = nullptr;
        }
        if (slides != nullptr)
            slides->onLayoutChange = nullptr;
        if (copyPaste != nullptr)
            copyPaste->onLayoutChange = nullptr;
        if (noteSettings != nullptr)
            noteSettings->onLayoutChange = nullptr;
        strip.onEdit = nullptr;
        roll.onToast = nullptr;
        roll.onToastFor = nullptr;
        roll.onEdit = nullptr;
        roll.onPress = nullptr;
        roll.onHint = nullptr;
        roll.getTools().onShowLane = nullptr;
        selectionBar.onHeightChange = nullptr;
        inspector.onToast = nullptr;
        inspector.onEdit = nullptr;
        paneViewport.setViewedComponent (nullptr, false);
        diagViewport.setViewedComponent (nullptr, false);
    }

    void MainView::buildModeBar()
    {
        buildButton.setKeyChip ("1");
        buildButton.setTooltip ("Build: add, move and stretch notes");
        buildButton.onClick = [this] { setMode (1); };
        addAndMakeVisible (buildButton);
        modeItems.push_back ({ &buildButton, buildButton.preferredWidth(), kItemHeight, false });
        modeItems.push_back ({ nullptr, 1, kItemHeight, false });

        for (int m = 2; m <= 8; ++m)
        {
            const auto& info = core::kModes[m - 1];
            auto wrap = std::make_unique<LaneWrap> (theme, info, laneTok (info.lanes[0]));
            wrap->lane.setTooltip (m == 8 ? juce::String ("Sample: how each note moves through the sound")
                                          : "Draw " + juce::String (info.name) + " on the notes (notes stay put)");
            wrap->lane.onClick = [this, m] { setMode (m); };
            // sl-ui-panels.js hookLaneArrows: the lane's menu under its arrow (the Sample button's: the Sample lane shown)
            auto* arrow = &wrap->arrow;
            wrap->arrow.onClick = [this, m, arrow]
            {
                const auto lane = m == 8 ? processor.getDocument().rollState().subLane : core::kModes[m - 1].lanes[0];
                roll::openLaneMenu (roll, lane, *arrow, [this] (roll::ToolSettings::Drawer d)
                {
                    selectionBar.showDrawer (d);
                    syncRollControls();
                });
            };
            addAndMakeVisible (*wrap);
            modeItems.push_back ({ wrap.get(), wrap->preferredWidth(), kItemHeight, false });
            laneWraps.push_back (std::move (wrap));
        }

        for (int i = 0; i < 4; ++i)
        {
            const auto& lane = core::laneInfo (core::kSampleLanes[i]);
            auto chip = std::make_unique<FlatButton> (theme, lane.name);
            chip->setTooltip (sampleLaneHint (core::kSampleLanes[i]));   // sl-ui.js: each chip's title is its SUB_HINT
            chip->setLaneColour (Tok::laneSample);
            chip->onClick = [this, i] { setMode (8, i); };
            addAndMakeVisible (*chip);
            modeItems.push_back ({ chip.get(), chip->preferredWidth(), kItemHeight - 2, false });
            subChips.push_back (std::move (chip));
        }

        modeItems.push_back ({ nullptr, 1, kItemHeight, false });
        // the Nodes switch (M5, sl-ui.js toggleNodes): greyed in Build, lit while on
        nodesButton.setTooltip ("Nodes: a box on empty space picks nodes of the selected notes; then drag one to move them together "
                                "(N while Keys is on)");
        nodesButton.onClick = [this] { roll.toggleNodes(); };
        addAndMakeVisible (nodesButton);
        modeItems.push_back ({ &nodesButton, nodesButton.preferredWidth(), kItemHeight, false });
    }

    FlatButton& MainView::getModeButton (int modeNumber) noexcept
    {
        if (modeNumber <= 1 || modeNumber > 8)
            return buildButton;
        return laneWraps[(std::size_t) (modeNumber - 2)]->lane;
    }

    FlatButton& MainView::getLaneArrow (int modeNumber) noexcept
    {
        return laneWraps[(std::size_t) (juce::jlimit (2, 8, modeNumber) - 2)]->arrow;
    }

    TabPanel* MainView::getTabPanel (int tab) const noexcept
    {
        if (tab < 0 || tab >= numTabs)
            return nullptr;
        return dynamic_cast<TabPanel*> (panels[(std::size_t) tab].get());
    }

    juce::Button& MainView::getFoldButton() noexcept
    {
        return *foldButton;
    }

    void MainView::tooShortToast()
    {
        if (autoFolded)
            showToast (kTooShort, 3200);
    }

    void MainView::paintModes()
    {
        buildButton.setToggleState (mode == 1, juce::dontSendNotification);
        for (auto& w : laneWraps)
        {
            const bool on = w->modeNumber == mode;
            w->lane.setToggleState (on, juce::dontSendNotification);
            w->arrow.setToggleState (on, juce::dontSendNotification);
        }
        for (int i = 0; i < (int) subChips.size(); ++i)
            subChips[(std::size_t) i]->setToggleState (mode == 8 && subLane == i, juce::dontSendNotification);
        // sl-ui.js paintModes: the Nodes switch lit while on, greyed in Build (it stays on there, doing nothing)
        nodesButton.setToggleState (roll.getGestures().settings().nodes, juce::dontSendNotification);
        nodesButton.setEnabled (mode != 1);
    }

    void MainView::setMode (int newMode, int newSubLane)
    {
        mode = juce::jlimit (1, 8, newMode);
        if (newSubLane >= 0 && newSubLane < 4)
            subLane = newSubLane;
        roll.setMode (mode, subLane);
        paintModes();
        saveUiState();
        syncRollControls();
    }

    void MainView::showLane (core::LaneId lane)
    {
        // sl-ui-panels.js showLane: a Sample lane shows mode 8 with that sub-lane, any other lane its own mode
        const auto& info = core::laneInfo (lane);
        if (info.sub)
        {
            for (int i = 0; i < 4; ++i)
                if (core::kSampleLanes[i] == lane && (mode != 8 || subLane != i))
                    setMode (8, i);
            return;
        }
        if (mode != info.mode)
            setMode (info.mode);
    }

    void MainView::updateHint()
    {
        // sl-ui.js modeHint: the mode's line, or Freehand's, or Slide into's while it waits for a click, or the
        // Nodes switch's
        const auto st = processor.getDocument().rollState();
        const auto& g = roll.getGestures();
        hintBar.setHint (modeHint (st.mode, st.activeLane(), g.settings().free, g.isLinkArmed(), g.settings().nodes));
    }

    void MainView::syncRollControls()
    {
        // the selection row (sl-ui.js updateSelInfo): how many notes are selected, Duplicate and Delete with some
        const auto st = processor.getDocument().rollState();
        const int n = (int) st.selected.size();
        selInfo.setText (n == 0 ? juce::String ("No notes selected") : juce::String (n) + (n == 1 ? " note selected" : " notes selected"));
        duplicate.setEnabled (n > 0);
        deleteNotes.setEnabled (n > 0);

        // the roll's switches: lit while on (sl-ui.js's .on)
        const auto& g = roll.getGestures();
        freehandButton.setToggleState (g.settings().free, juce::dontSendNotification);
        linkButton.setToggleState (g.isLinkArmed(), juce::dontSendNotification);
        multiButton.setToggleState (g.settings().multi, juce::dontSendNotification);
        nodesButton.setToggleState (g.settings().nodes, juce::dontSendNotification);

        // the selection bar (M5): its Lane, its drawer and what the drawer says about the selection now; its room (it
        // settles here, on the editor's timer, once the mouse has left the roll)
        selectionBar.sync();
        updateBarHeight();

        // the Slides and Copy / Paste tabs (M5 S3): the primary note's slide, what Copy holds
        if (slides != nullptr)
            slides->sync();
        if (copyPaste != nullptr)
            copyPaste->sync();

        // the Note settings tab (M6 T2): the selected notes' own settings, "mixed" where they differ
        if (noteSettings != nullptr)
            noteSettings->sync();

        // the Sample and Macros tabs (M6 T1): what each control does with the settings and the notes now
        if (samplePanel != nullptr)
            samplePanel->sync();
        if (macrosPanel != nullptr)
            macrosPanel->sync();

        // Undo and Redo (sl-ui.js historyChanged): enabled when there is a step, the step's name in the tooltip
        const auto undoLabel = processor.undoLabel(), redoLabel = processor.redoLabel();
        undoButton.setEnabled (processor.canUndo());
        redoButton.setEnabled (processor.canRedo());
        undoButton.setTooltip (undoLabel ? "Undo: " + juce::String::fromUTF8 (undoLabel->c_str()) + " (Ctrl+Z while Keys is on)"
                                         : juce::String ("Nothing to undo"));
        redoButton.setTooltip (redoLabel ? "Redo: " + juce::String::fromUTF8 (redoLabel->c_str()) + " (Ctrl+Shift+Z while Keys is on)"
                                         : juce::String ("Nothing to redo"));

        inspector.sync (g.settings().bpm);
        updateHint();

        // Try this (U3, M6 decision 6): one suggestion for the mode, the Sample lane and the primary note's settings
        {
            const auto clip = processor.getDocument().current();
            const model::Note* primary = nullptr;
            if (clip != nullptr && ! st.primary.empty())
                for (const auto& note : clip->notes)
                    if (note->id == st.primary)
                        primary = note.get();
            hintBar.setTryThis (tryThis (st.mode, st.subLane, primary, processor.readInst()));
        }
    }

    bool MainView::handleKey (const juce::KeyPress& key, bool keysNow)
    {
        // While an inspector box is typed in, the keys are the box's: a key the box leaves (Ctrl+Z with no typing
        // of its own to undo, Ctrl+Y, Ctrl+D) runs no shortcut and goes nowhere else, with Keys on or off (fix pass
        // F2; sl-ui-panels.js stops a field's keys, and sl-ui.js runs no shortcut while a text field has the focus).
        if (inspector.isEditing())
            return true;

        // With the right-click menu open, Escape closes it and keeps the selection (sl-ui.js's first key rule).
        if (keysNow && roll.isMenuOpen() && key.isKeyCode (juce::KeyPress::escapeKey))
        {
            roll.closeMenu();
            return true;
        }

        // While the Diagnostics tab hides the roll, no shortcut runs: the keys are only recorded there (fix pass
        // UI4-4). With Keys on they are still kept, all but Space.
        if (! roll.isVisible())
            return KeyRouter::keeps (key, keysNow);

        return keyRouter.route (key, keysNow);
    }

    void MainView::keyReleased()
    {
        keyRouter.keyReleased();
    }

    void MainView::disarmClear()
    {
        clearArmedAt = -1.0;
        if (clearAll.getButtonText() != kClearAll)
        {
            clearAll.setButtonText (kClearAll);
            layoutSelectionRow();
            repaint (selRowArea);
        }
    }

    void MainView::saveUiState()
    {
        auto ui = processor.getUiState();
        ui.mode = (std::uint8_t) mode;
        ui.subLane = core::laneInfo (core::kSampleLanes[subLane]).code;
        ui.tab = kTabIds[openTabIndex];
        ui.folded = folded ? 1 : 0;
        processor.setUiState (ui);
    }

    void MainView::openTab (int tab)
    {
        openTabIndex = juce::jlimit (0, numTabs - 1, tab);
        folded = false;
        tabStrip.setSelected (openTabIndex);
        tabStrip.setFolded (false);
        foldButton->setToggleState (false, juce::dontSendNotification);

        for (int i = 0; i < numTabs; ++i)
            if (panels[(std::size_t) i] != nullptr)
                panels[(std::size_t) i]->setVisible (i == openTabIndex);
        paneViewport.setViewPosition (0, 0);

        saveUiState();
        resized();
        repaint();
    }

    void MainView::setFolded (bool shouldFold)
    {
        folded = shouldFold;
        tabStrip.setFolded (folded);
        foldButton->setToggleState (folded, juce::dontSendNotification);
        saveUiState();
        resized();
        repaint();
    }

    void MainView::themeChanged()
    {
        const bool dark = theme.isDark();
        // shell.html's theme button names the theme it switches to.
        themeButton.setButtonText (dark ? "Light" : "Dark");
        themeButton.setIcon (dark ? FlatButton::Icon::sun : FlatButton::Icon::moon);
        resized();
        repaint();
    }

    void MainView::setKeysOn (bool on)
    {
        keysOn = on;
        keysButton.setToggleState (on, juce::dontSendNotification);
    }

    void MainView::setUiScalePercent (int percent)
    {
        uiScaleBox.setSelectedId (percent, juce::dontSendNotification);
        if (uiScaleBox.getSelectedId() != percent)
            uiScaleBox.setText (juce::String (percent) + " %", juce::dontSendNotification);
    }

    void MainView::tick (const diag::DiagModel::Snapshot& s, bool keysNow)
    {
        const juce::String tempo = s.hasBpm ? juce::String (s.bpm, juce::exactlyEqual (s.bpm, std::floor (s.bpm)) ? 0 : 2) : juce::String ("-");
        if (tempo != tempoText)
        {
            tempoText = tempo;
            repaint (topBarArea);
        }

        // the roll: the host's tempo (the vibrato's squiggle), then what the document holds now (the playhead comes
        // from the display frames, at 30 Hz: PluginEditor::takeDisplayFrame), and the rows that show it
        roll.setHostBpm (s.hasBpm ? std::optional<double> (s.bpm) : std::nullopt);
        roll.refresh();
        syncRollControls();
        strip.refresh();

        // Clear all lets go of its first click after 2.5 s
        if (clearArmedAt >= 0.0 && juce::Time::getMillisecondCounterHiRes() - clearArmedAt >= kClearArmMs)
            disarmClear();

        if (openTabIndex == diagTab && ! folded)
        {
            diagPage->update (s, keysNow);
            layoutPane();
        }

        // The banner needs the state encoded, so it is checked twice a second, not on every tick. It also says when
        // the clip is too detailed to play (only a hand-made project can hold one: the document refuses such edits).
        if (ticks++ % 8 == 0)
        {
            auto banner = processor.getStateBanner();
            if (processor.getSnapshotPublisher().stats().tooDetailed)
                banner << (banner.isEmpty() ? "" : ". ") << kTooDetailed;
            if (banner != bannerText)
            {
                bannerText = banner;
                resized();
                repaint();
            }
        }
    }

    void MainView::showDropOverlay (bool show, const juce::String& what)
    {
        if (show != dropShown || what != dropText)
        {
            dropShown = show;
            dropText = what;
            repaint();
        }
    }

    void MainView::showToast (const juce::String& text, int ms)
    {
        toast->show (text, ms);
        // One line when it fits the card, else up to three lines (a missing file's toast names its whole path).
        const int most = juce::jmax (100, juce::jmin (640, card.getWidth() - 40));
        const int want = toast->preferredWidth();
        const int lines = want <= most ? 1 : juce::jmin (3, (want + most - 1) / most);
        const int w = juce::jmin (want, most);
        const int top = toastTop();
        toast->setBounds (card.getCentreX() - w / 2, top, w, 14 + 16 * lines);
        toast->toFront (false);
    }

    int MainView::toastTop() const noexcept
    {
        // Just under the roll's ruler, so the bar numbers stay readable (fix pass R2-6) and, in a 560 px window, the
        // roll's note under it too; on the pane when the roll is hidden.
        return roll.isVisible() ? roll.getY() + roll::RollView::rulerHeight + 4 : paneArea.getY() + 10;
    }

    const juce::String& MainView::getToastText() const noexcept
    {
        return toast->getText();
    }

    bool MainView::isToastShown() const noexcept
    {
        return toast->isVisible();
    }

    int MainView::syncControls()
    {
        int moved = 0;
        for (auto* c : synced)
            moved += c->syncToParameter() ? 1 : 0;
        return moved;
    }

    void MainView::layoutPane()
    {
        // The open tab's controls in the pane, scrolling when they are taller than it.
        if (openTabIndex == diagTab)
        {
            const int w = diagViewport.getWidth();
            int h = diagPage->heightFor (w);
            const int inner = h > diagViewport.getHeight() ? w - diagViewport.getScrollBarThickness() : w;
            h = diagPage->heightFor (inner);
            if (diagPage->getWidth() != inner || diagPage->getHeight() != h)
                diagPage->setSize (inner, h);
            return;
        }

        auto* panel = panels[(std::size_t) openTabIndex].get();
        if (panel == nullptr)
            return;
        const int w = paneViewport.getWidth();
        int h = 32;
        if (auto* tp = dynamic_cast<TabPanel*> (panel))
        {
            h = tp->heightFor (w);
            if (h > paneViewport.getHeight())
                h = tp->heightFor (w - paneViewport.getScrollBarThickness());
        }
        const int inner = h > paneViewport.getHeight() ? w - paneViewport.getScrollBarThickness() : w;
        panel->setBounds (0, 0, inner, h);
        paneContent.setSize (inner, h);
    }

    bool MainView::rollHeld()
    {
        // the mouse is on the roll, or a gesture is held there (a drag that started on the roll)
        return roll.isMouseOverOrDragging (true) || roll.getGestures().isDragging();
    }

    void MainView::updateBarHeight()
    {
        const int want = selectionBar.heightFor (juce::jmax (100, card.getWidth() - 2 * kBarPadX));
        if (want == barHeight)
            return;
        const bool held = rollHeld();
        if (want < barHeight && held)
            return;   // settles when the mouse leaves the roll (tick)
        const int rollTop = roll.getY();
        barHeight = want;
        resized();
        repaint();
        // the bar grew (or shrank) above the roll under the mouse: the roll scrolls by as much, so its notes stay put
        if (held && roll.isVisible())
            if (const int dy = roll.getY() - rollTop; dy != 0)
                roll.scrollBy (0.0, (double) dy);
    }

    void MainView::layoutSelectionRow()
    {
        // Row 1: what is selected and the buttons for it; at the right end, after a line, the roll's switches. The
        // text keeps the room of its longest form, so the buttons never move when the selection changes, and the
        // switches stay put when Clear all asks for its second click. Row 2: the selection bar (M5) and its drawer.
        // Row 3: the inspector.
        if (selRowArea.isEmpty())
            return;
        const auto row1 = selRowArea.withHeight (kSelRowHeight).reduced (kBarPadX, 0);
        const int cy = row1.getCentreY();
        int x = row1.getX();
        const int infoWidth = juce::jmax (selInfo.preferredWidth(),
                                          (int) std::ceil (Theme::textWidth (selInfo.getFont(), "4096 notes selected")) + 2);
        selInfo.setBounds (x, cy - 9, infoWidth, 18);
        x = selInfo.getRight() + kGap;
        for (auto* b : { &selectAll, &duplicate, &deleteNotes, &clearAll })
        {
            const int w = b->preferredWidth();
            b->setBounds (x, cy - kItemHeight / 2, w, kItemHeight);
            x += w + kGap;
        }
        int right = row1.getRight();
        for (auto* b : { &multiButton, &linkButton, &freehandButton })
        {
            const int w = b->preferredWidth();
            b->setBounds (right - w, cy - kItemHeight / 2, w, kItemHeight);
            right -= w + kGap;
        }
        selRowSeparator = right - 1;

        const int barWidth = selRowArea.getWidth() - 2 * kBarPadX;
        selectionBar.setBounds (selRowArea.getX() + kBarPadX, selRowArea.getY() + kSelRowHeight, barWidth, barHeight);

        // row 3 is not there while an open drawer has taken it on a short window (decision 6)
        inspector.setVisible (! inspectorFolded);
        if (inspectorFolded)
            return;
        const auto row3 = selRowArea.withTrimmedTop (kSelRowHeight + barHeight).withTrimmedBottom (1).reduced (kBarPadX, 0);
        inspector.setBounds (row3.getX(), row3.getY(), row3.getWidth(), row3.getHeight() - 3);
    }

    int MainView::paneHeightFor (int width, int room) const
    {
        // Plan 7.7 decision 6 (M4 review UI4-7): the pane shows whole rows of controls, never a row cut through, and the
        // roll keeps rollFloor under it. The whole panel when it fits in 2/5 of the room; else it scrolls, showing whole
        // rows (at least kMinScrollingPane, so its scroll bar has a thumb): up to the first row end past 2/5 of the room,
        // or the last one before it (M0's rule). 0 when not even one row fits: the tabs fold until the window is taller.
        const auto* tp = dynamic_cast<const TabPanel*> (panels[(std::size_t) openTabIndex].get());
        if (tp == nullptr)
            return room - kComingHeight >= rollFloor ? kComingHeight : 0;
        const int most = juce::jmax (80, room * 2 / 5);
        const auto whole = tp->rowEnds (width);
        if (! whole.empty() && whole.back() <= most && room - whole.back() >= rollFloor)
            return whole.back();
        int pick = 0;
        for (const int e : tp->rowEnds (width - paneViewport.getScrollBarThickness()))
        {
            if (e < kMinScrollingPane || room - e < rollFloor)
                continue;
            pick = e;
            if (e >= most)
                break;
        }
        return pick;
    }

    void MainView::resized()
    {
        auto area = getLocalBounds().withTrimmedLeft (kSideMargin).withTrimmedRight (kSideMargin)
                                    .withTrimmedTop (kTopMargin).withTrimmedBottom (kTopMargin);
        card = area;

        // Top bar: logo, wordmark | Undo, Redo | theme, Keys | tempo | Grid | UI Scale ......... status
        topBarArea = area.removeFromTop (kTopBarHeight);
        {
            topBarSeparators.clear();
            auto r = topBarArea.reduced (kBarPadX, 0);
            const int cy = r.getCentreY();
            logoArea = juce::Rectangle<int> (r.getX(), cy - 11, 22, 22);
            const int markWidth = (int) std::ceil (Theme::textWidth (wordmarkFont(), kWordmark)) + 4;
            wordmarkArea = juce::Rectangle<int> (logoArea.getRight() + 8, cy - 12, markWidth, 24);
            int x = wordmarkArea.getRight() + 8;
            const auto place = [&x, cy] (juce::Component& c, int w, int h = kItemHeight)
            {
                c.setBounds (x, cy - h / 2, w, h);
                x += w + kGap;
            };
            const auto separator = [&x, this]
            {
                topBarSeparators.push_back (x);
                x += 1 + kGap;
            };
            separator();
            place (undoButton, kHistoryButtonWidth);
            place (redoButton, kHistoryButtonWidth);
            separator();
            place (themeButton, themeButton.preferredWidth());
            place (keysButton, keysButton.preferredWidth());
            separator();
            place (tempoLabel, tempoLabel.preferredWidth(), 18);
            x += 64 + kGap;   // the tempo box, painted
            separator();
            place (gridLabel, gridLabel.preferredWidth(), 18);
            place (gridBox, kGridBoxWidth);
            separator();
            place (scaleLabel, scaleLabel.preferredWidth(), 18);
            place (uiScaleBox, 84);
            // the build's name at the right end, when there is room for it
            const int sw = status.preferredWidth();
            status.setVisible (x + sw <= r.getRight());
            status.setBounds (r.getRight() - sw, cy - 9, sw, 18);
        }

        bannerArea = {};
        if (bannerText.isNotEmpty())
            bannerArea = area.removeFromTop (kBannerHeight);

        // Mode bar: wraps onto a second row when the window is narrow (as shell.html's flex-wrap does).
        {
            const int inner = area.getWidth() - 2 * kBarPadX;
            const int h = flowLayout (modeItems, { 0, 0, inner, 1000 }, kModeGap, kGap, false);
            modeBarArea = area.removeFromTop (h + 14);
            flowLayout (modeItems, modeBarArea.reduced (kBarPadX, 7), kModeGap, kGap, true);
        }

        // The selection row, the selection bar while it shows (updateBarHeight: notes selected or a drawer open) and the
        // inspector's line (always there). An open drawer takes at most what leaves the roll a quarter of the window
        // (140 to 400 px; at least 62 px for the drawer, which scrolls), as the prototype's fitLayout shares the room;
        // the pane keeps a scrolling row when a tab is open. A new width can change the drawer's height: it takes it now
        // unless the roll holds the bar's room still.
        {
            const int below = kTabsHeight + kHintHeight + WaveStrip::heightFor (getHeight());
            const bool paneOpen = ! folded && openTabIndex != diagTab;
            const int rollMin = juce::jlimit (140, 400, juce::roundToInt ((double) getHeight() * 0.26));
            const int room = area.getHeight() - kSelRowHeight - kInspectorRowHeight - roll::SelectionBar::lineHeight - 4 - below
                           - (paneOpen ? kMinScrollingPane : 0);
            selectionBar.setDrawerMax (room - rollMin);
            const int want = selectionBar.heightFor (juce::jmax (100, area.getWidth() - 2 * kBarPadX));
            if (want > barHeight || ! rollHeld())
                barHeight = want;
        }

        // Plan 7.7 decision 6: the pane and the roll share what the bars leave. An open tab shows whole rows of its
        // controls above the roll's floor, or the tabs fold while the window is too short for that (the person's fold
        // stays as it is); with the tabs folded and a drawer open, a window too short for the roll's floor gives the
        // inspector's line to the roll.
        const int stripHeight = WaveStrip::heightFor (getHeight());
        const bool diag = openTabIndex == diagTab && ! folded;
        const int shared = area.getHeight() - kSelRowHeight - barHeight - kInspectorRowHeight - kTabsHeight - kHintHeight - stripHeight;
        int paneHeight = 0;
        autoFolded = false;
        if (! folded && ! diag)
        {
            paneHeight = paneHeightFor (area.getWidth(), shared);
            autoFolded = paneHeight == 0;
        }
        const bool drawerShown = barHeight > roll::SelectionBar::lineHeight;
        inspectorFolded = ! diag && paneHeight == 0 && drawerShown && shared < rollFloor;

        selRowArea = area.removeFromTop (kSelRowHeight + barHeight + (inspectorFolded ? 0 : kInspectorRowHeight));
        layoutSelectionRow();

        // The tab row: the tabs, the roll's zoom buttons, Hide / Show. The tabs show folded while the pane is.
        tabsArea = area.removeFromTop (kTabsHeight);
        foldButton->setBounds (tabsArea.getRight() - 64, tabsArea.getY() + 4, 56, kTabsHeight - 8);
        {
            const int zoomWidth = 4 * roll::RollView::zoomButtonWidth + 3 * 2;
            int x = foldButton->getX() - kZoomGap - zoomWidth;
            tabStrip.setBounds (tabsArea.withTrimmedLeft (6).withRight (x - 8).withTrimmedBottom (1));
            for (int i = 0; i < 4; ++i)
            {
                auto& b = roll.getZoomButton (i);
                b.setBounds (x, tabsArea.getY() + 4, roll::RollView::zoomButtonWidth, kTabsHeight - 8);
                b.setVisible (! diag);
                x += roll::RollView::zoomButtonWidth + 2;
            }
        }
        const bool foldedNow = folded || autoFolded;
        tabStrip.setFolded (foldedNow);
        foldButton->setToggleState (foldedNow, juce::dontSendNotification);
        {
            const juce::String tip = autoFolded ? juce::String ("The window is too short for this tab: make it taller to open it")
                                   : folded     ? "Show the " + juce::String (kTabNames[openTabIndex]) + " panel again"
                                                : juce::String ("Fold the panel away to give the roll more room (or click the open tab)");
            if (foldButton->getTooltip() != tip)
                foldButton->setTooltip (tip);
        }

        hintArea = area.removeFromBottom (kHintHeight);
        hintBar.setBounds (hintArea);
        strip.setBounds (area.removeFromBottom (stripHeight));

        // What is left holds the pane and the roll, or the Diagnostics page.
        diagViewport.setVisible (diag);
        roll.setVisible (! diag);
        paneViewport.setVisible (paneHeight > 0);
        if (diag)
        {
            paneArea = area;
            diagViewport.setBounds (area);
            layoutPane();
        }
        else
        {
            paneArea = area.removeFromTop (paneHeight);
            paneViewport.setBounds (paneArea);
            if (paneHeight > 0)
                layoutPane();
            roll.setBounds (area);
        }

        if (toast->isVisible())
            toast->setBounds (toast->getBounds().withY (toastTop()));
    }

    void MainView::paint (juce::Graphics& g)
    {
        g.fillAll (theme (Tok::bg));

        // The card: every bar inside one rounded outline, the bars split by 1 px lines.
        juce::Path outline;
        outline.addRoundedRectangle (card.toFloat(), Theme::frameCorner);
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (outline);
            g.setColour (theme (Tok::panel));
            g.fillRect (topBarArea);
            g.fillRect (selRowArea);
            g.fillRect (hintArea);
            g.setColour (theme (Tok::panel2));
            g.fillRect (modeBarArea);
            g.fillRect (tabsArea);
            g.fillRect (paneArea);
            if (! bannerArea.isEmpty())
            {
                g.setColour (theme (Tok::chipWarnBg));
                g.fillRect (bannerArea);
            }

            g.setColour (theme (Tok::line));
            for (auto* r : { &topBarArea, &bannerArea, &modeBarArea, &selRowArea, &tabsArea, &paneArea })
                if (! r->isEmpty())
                    g.fillRect (card.getX(), r->getBottom() - 1, card.getWidth(), 1);
            g.fillRect (card.getX(), strip.getBottom(), card.getWidth(), 1);
        }
        g.setColour (theme (Tok::line));
        g.strokePath (outline, juce::PathStrokeType (1.0f));

        // Top bar: the logo and wordmark, the separators and the host's tempo.
        paintLogo (g, logoArea.toFloat(), theme);
        paintWordmark (g, wordmarkArea.toFloat(), theme);
        g.setColour (theme (Tok::line));
        for (int x : topBarSeparators)
            g.fillRect (x, topBarArea.getCentreY() - 11, 1, 22);
        {
            const auto box = juce::Rectangle<float> ((float) tempoLabel.getRight() + (float) kGap, (float) topBarArea.getCentreY() - 13.0f, 64.0f, 26.0f);
            g.setColour (theme (Tok::btn));
            g.fillRoundedRectangle (box.reduced (0.5f), Theme::corner);
            g.setColour (theme (Tok::btnb));
            g.drawRoundedRectangle (box.reduced (0.5f), Theme::corner, 1.0f);
            g.setColour (theme (Tok::txt));
            g.setFont (Theme::px (12.0f, true));
            g.drawText (tempoText, box.reduced (7.0f, 0.0f), juce::Justification::centredLeft, false);
        }

        // The selection row: the line between the selection's buttons and the roll's switches.
        g.setColour (theme (Tok::line));
        if (selRowSeparator >= 0)
            g.fillRect (selRowSeparator, selRowArea.getY() + kSelRowHeight / 2 - 11, 1, 22);

        if (! bannerArea.isEmpty())
        {
            g.setColour (theme (Tok::chipWarnTxt));
            g.setFont (Theme::px (12.0f, true));
            g.drawFittedText (bannerText, bannerArea.reduced (kBarPadX, 0), juce::Justification::centredLeft, 1, 0.8f);
        }

        // Mode bar separators (the two empty flow items).
        g.setColour (theme (Tok::line));
        {
            int seen = 0;
            for (const auto& it : modeItems)
            {
                if (it.component != nullptr)
                    continue;
                ++seen;
                const auto* before = seen == 1 ? static_cast<juce::Component*> (&buildButton) : static_cast<juce::Component*> (subChips.back().get());
                g.fillRect (before->getRight() + kModeGap, before->getBounds().getCentreY() - 11, 1, 22);
            }
        }
    }

    void MainView::paintOverChildren (juce::Graphics& g)
    {
        if (! dropShown)
            return;

        // shell.html's #drop: the whole window tinted, a dashed accent border and one line of text.
        const auto r = getLocalBounds().toFloat().reduced (4.0f);
        g.setColour (theme (Tok::dropBg));
        g.fillRect (getLocalBounds());
        juce::Path border;
        border.addRectangle (r);
        juce::Path dashed;
        const float dashes[] = { 10.0f, 7.0f };
        juce::PathStrokeType (4.0f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (theme (Tok::acc));
        g.fillPath (dashed);
        g.setFont (Theme::px (20.0f, true));
        g.drawFittedText (dropText.isNotEmpty() ? dropText : juce::String ("Drop it: the Diagnostics page shows what arrived"),
                          getLocalBounds().reduced (40), juce::Justification::centred, 3, 0.8f);
    }
}
