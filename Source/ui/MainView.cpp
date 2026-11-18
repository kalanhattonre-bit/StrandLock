#include "MainView.h"

#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "core/Version.h"
#include "params/ParamIds.h"
#include "ui/Logo.h"
#include "ui/tabs/MacrosTab.h"
#include "ui/tabs/SampleTab.h"
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
        constexpr int kBarPadX = 8, kItemHeight = 26, kGap = 5, kModeGap = 4;
        constexpr int kHistoryButtonWidth = 30, kGridBoxWidth = 70;
        constexpr double kClearArmMs = 2500.0;   // sl-ui.js Clear all: the second click within 2.5 s

        const char* const kTabNames[] = { "Sample", "Macros", "Note settings", "Slides", "Copy / Paste", "Mod wheels", "Sound", "Diagnostics" };
        const char* const kTabIds[] = { "sample", "macros", "note", "glide", "copy", "mods", "sound", "diag" };

        // What each tab that is not built yet will hold, and when.
        const char* const kComing[] = {
            nullptr,
            nullptr,
            "Settings for the selected notes (engine, motion, the knobs per note) arrive in M6.",
            "Slides between notes, Auto slide and Slide time arrive in M5.",
            "Copying lanes from one note and pasting them onto others arrives in M5.",
            "The two mod wheels, MIDI Learn and Let go arrive in M8.",
            nullptr,
            nullptr,
        };

        constexpr const char* kTryThis = "play C3 on your keyboard, then open the Diagnostics tab.";

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
            arrow.setTooltip ("Shapes, patterns and random for the selected notes (M5)");
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
        addAndMakeVisible (inspector);

        // Tabs and panes.
        tabStrip.onClick = [this] (int index)
        {
            if (index == openTabIndex && ! folded)
                setFolded (true);
            else
                openTab (index);
        };
        addAndMakeVisible (tabStrip);
        foldButton = std::make_unique<FoldButton> (t);
        foldButton->setTooltip ("Fold the panel away to give the roll more room (or click the open tab)");
        foldButton->onClick = [this] { setFolded (! folded); };
        addAndMakeVisible (*foldButton);

        auto& apvts = processor.getApvts();
        panels[sampleTab] = std::make_unique<SampleTab> (t, apvts);
        panels[macrosTab] = std::make_unique<MacrosTab> (t, apvts);
        panels[soundTab] = std::make_unique<SoundTab> (t, apvts);
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
        roll.onToast = [this] (const juce::String& text) { showToast (text); };
        roll.onEdit = [this] { syncRollControls(); };
        roll.onPress = [this] { inspector.finishEditing(); };
        inspector.onToast = [this] (const juce::String& text, int ms) { showToast (text, ms); };
        inspector.onEdit = [this]
        {
            roll.refresh();
            syncRollControls();
        };
        addAndMakeVisible (strip);
        strip.onToast = [this] (const juce::String& text, int ms) { showToast (text, ms); };
        hintBar.setTryThis (kTryThis);
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
        roll.onToast = nullptr;
        roll.onEdit = nullptr;
        roll.onPress = nullptr;
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
            wrap->arrow.onClick = [this] { showToast ("Shapes, patterns and random for the selected notes arrive in M5"); };
            addAndMakeVisible (*wrap);
            modeItems.push_back ({ wrap.get(), wrap->preferredWidth(), kItemHeight, false });
            laneWraps.push_back (std::move (wrap));
        }

        for (int i = 0; i < 4; ++i)
        {
            const auto& lane = core::laneInfo (core::kSampleLanes[i]);
            auto chip = std::make_unique<FlatButton> (theme, lane.name);
            chip->setLaneColour (Tok::laneSample);
            chip->onClick = [this, i] { setMode (8, i); };
            addAndMakeVisible (*chip);
            modeItems.push_back ({ chip.get(), chip->preferredWidth(), kItemHeight - 2, false });
            subChips.push_back (std::move (chip));
        }

        modeItems.push_back ({ nullptr, 1, kItemHeight, false });
        nodesButton.setEnabled (false);
        nodesButton.setTooltip ("Nodes: pick and drag the nodes of the selected notes (M5)");
        addAndMakeVisible (nodesButton);
        modeItems.push_back ({ &nodesButton, nodesButton.preferredWidth(), kItemHeight, false });
    }

    FlatButton& MainView::getModeButton (int modeNumber) noexcept
    {
        if (modeNumber <= 1 || modeNumber > 8)
            return buildButton;
        return laneWraps[(std::size_t) (modeNumber - 2)]->lane;
    }

    TabPanel* MainView::getTabPanel (int tab) const noexcept
    {
        if (tab < 0 || tab >= numTabs)
            return nullptr;
        return dynamic_cast<TabPanel*> (panels[(std::size_t) tab].get());
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

    void MainView::updateHint()
    {
        // sl-ui.js modeHint: the mode's line, or Freehand's, or Slide into's while it waits for a click
        const auto st = processor.getDocument().rollState();
        const auto& g = roll.getGestures();
        hintBar.setHint (modeHint (st.mode, st.activeLane(), g.settings().free, g.isLinkArmed()));
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
    }

    bool MainView::handleKey (const juce::KeyPress& key, bool keysNow)
    {
        return keyRouter.route (key, keysNow);
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

    void MainView::layoutSelectionRow()
    {
        // Row 1: what is selected and the buttons for it; at the right end, after a line, the roll's switches. The
        // text keeps the room of its longest form, so the buttons never move when the selection changes, and the
        // switches stay put when Clear all asks for its second click. Row 2: the inspector.
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

        const auto row2 = selRowArea.withTrimmedTop (kSelRowHeight).withTrimmedBottom (1).reduced (kBarPadX, 0);
        inspector.setBounds (row2.getX(), row2.getY(), row2.getWidth(), row2.getHeight() - 3);
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

        // The selection row and the inspector's line under it (always there, so selecting never moves the roll).
        selRowArea = area.removeFromTop (kSelRowHeight + kInspectorRowHeight);
        layoutSelectionRow();

        tabsArea = area.removeFromTop (kTabsHeight);
        tabStrip.setBounds (tabsArea.withTrimmedLeft (6).withTrimmedRight (70).withTrimmedBottom (1));
        foldButton->setBounds (tabsArea.getRight() - 64, tabsArea.getY() + 4, 56, kTabsHeight - 8);

        hintArea = area.removeFromBottom (kHintHeight);
        hintBar.setBounds (hintArea);
        strip.setBounds (area.removeFromBottom (WaveStrip::heightFor (getHeight())));

        // What is left holds the pane and the roll, or the Diagnostics page.
        const bool diag = openTabIndex == diagTab && ! folded;
        diagViewport.setVisible (diag);
        roll.setVisible (! diag);
        paneViewport.setVisible (! diag && ! folded);
        if (diag)
        {
            paneArea = area;
            diagViewport.setBounds (area);
            layoutPane();
        }
        else
        {
            int paneHeight = 0;
            if (! folded)
            {
                paneHeight = 32;
                if (auto* tp = dynamic_cast<TabPanel*> (panels[(std::size_t) openTabIndex].get()))
                {
                    // The pane takes up to 2/5 of the room, cut between two rows of controls, never through one:
                    // the first row end past that share while the roll keeps 110 px, else the last one before it.
                    const int most = juce::jmax (80, area.getHeight() * 2 / 5);
                    auto ends = tp->rowEnds (area.getWidth());
                    paneHeight = ends.empty() ? 32 : ends.back();
                    if (paneHeight > most)
                    {
                        ends = tp->rowEnds (area.getWidth() - paneViewport.getScrollBarThickness());
                        int up = 0, down = ends.empty() ? most : ends.front();
                        for (int e : ends)
                        {
                            if (e >= most && up == 0)
                                up = e;
                            if (e <= most)
                                down = e;
                        }
                        paneHeight = up > 0 && area.getHeight() - up >= 110 ? up : down;
                        // It scrolls: JUCE draws no thumb in a scroll bar shorter than 32 px plus twice its
                        // thickness, so a pane cut to one row (the state banner at 900 x 560) would hide that
                        // there is more (fix pass R2-7).
                        paneHeight = juce::jmax (paneHeight, kMinScrollingPane);
                    }
                }
            }
            paneArea = area.removeFromTop (paneHeight);
            paneViewport.setBounds (paneArea);
            if (! folded)
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
