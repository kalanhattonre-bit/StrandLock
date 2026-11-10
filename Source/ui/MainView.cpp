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
        constexpr int kHintHeight = 30;
        constexpr int kBarPadX = 8, kItemHeight = 26, kGap = 5, kModeGap = 4;

        const char* const kTabNames[] = { "Sample", "Macros", "Note settings", "Slides", "Copy / Paste", "Mod wheels", "Sound", "Diagnostics" };
        const char* const kTabIds[] = { "sample", "macros", "note", "glide", "copy", "mods", "sound", "diag" };

        // What each tab that M0 does not build will hold, and when.
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

        const char* const kModeHints[] = {
            "BUILD (1): the piano roll (new notes, moving and stretching them) arrives in M4. For now every note plays the loaded sound.",
            "PITCH (2): drawing a note's pitch line arrives in M4.",
            "VOLUME (3): drawing a note's volume line arrives in M4.",
            "VIBRATO (4): drawing a note's vibrato arrives in M4.",
            "TONE (5): drawing a note's tone line (bright or dark) arrives in M4.",
            "FORMANT (6): drawing a note's formant line arrives in M4.",
            "HOLLOW (7): drawing a note's hollow line arrives in M4.",
            "SAMPLE (8): how each note moves through the sound (Travel, Heads, Grain, Refresh) arrives in M6.",
        };

        constexpr const char* kTryThis = "play C3 on your keyboard, then open the Diagnostics tab.";

        // The banner when the clip's lanes would need more memory than a snapshot may hold (doc/SnapshotPublisher.h).
        constexpr const char* kTooDetailed = "This clip is too detailed to play, so its notes are silent: it needs fewer notes or simpler lanes";

        const int kScales[] = { 75, 100, 125, 150, 175, 200 };

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

        /** A pane that M0 leaves empty: one plain line saying what comes there, and when. */
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

    /** A short message over the top of the roll that fades after a few seconds. */
    class MainView::ToastView final : public juce::Component, private juce::Timer
    {
    public:
        explicit ToastView (const Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); }

        void show (const juce::String& message, int ms)
        {
            text = message;
            setVisible (true);
            repaint();
            startTimer (juce::jmax (500, ms));
        }

        int preferredWidth() const { return (int) std::ceil (Theme::textWidth (Theme::px (12.0f), text)) + 26; }
        const juce::String& getText() const noexcept { return text; }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (theme (Tok::toastBg));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (theme (Tok::toastTxt));
            g.setFont (Theme::px (12.0f));
            g.drawFittedText (text, getLocalBounds().reduced (12, 4), juce::Justification::centred, 3, 0.9f);
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            setVisible (false);
        }

        const Theme& theme;
        juce::String text;
    };

    //==============================================================================================
    RollPlaceholder::RollPlaceholder (const Theme& t)
        : theme (t)
    {
        setInterceptsMouseClicks (false, false);
    }

    void RollPlaceholder::setPlayhead (std::optional<double> beat)
    {
        const bool same = beat.has_value() == playhead.has_value() && (! beat.has_value() || juce::exactlyEqual (*beat, *playhead));
        if (! same)
        {
            playhead = beat;
            repaint();
        }
    }

    void RollPlaceholder::paint (juce::Graphics& g)
    {
        const auto all = getLocalBounds();
        g.setColour (theme (Tok::roll));
        g.fillRect (all);

        const auto grid = all.withTrimmedLeft (pianoWidth).withTrimmedTop (rulerHeight);
        const auto piano = juce::Rectangle<int> (0, rulerHeight, pianoWidth, all.getHeight() - rulerHeight);
        const auto ruler = juce::Rectangle<int> (pianoWidth, 0, all.getWidth() - pianoWidth, rulerHeight);
        const int beats = bars * 4;
        const float beatWidth = (float) grid.getWidth() / (float) beats;

        // Rows: one per semitone from topNote down, the black keys' rows darker, a line under every B.
        for (int note = topNote, y = grid.getY(); y < grid.getBottom(); --note, y += rowHeight)
        {
            const int pc = ((note % 12) + 12) % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            g.setColour (black ? theme (Tok::rowBlack) : theme (Tok::row));
            g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);
            g.setColour (pc == 0 ? theme (Tok::octLine) : theme (Tok::rowLine));
            g.fillRect (grid.getX(), y + rowHeight - 1, grid.getWidth(), 1);

            // The piano key for the row, and the C names (Cubase: 60 = C3).
            g.setColour (black ? theme (Tok::pianoBlack) : theme (Tok::pianoWhite));
            g.fillRect (piano.getX(), y, black ? pianoWidth * 3 / 5 : pianoWidth, rowHeight);
            if (black)
            {
                g.setColour (theme (Tok::pianoWhite));
                g.fillRect (piano.getX() + pianoWidth * 3 / 5, y, pianoWidth * 2 / 5, rowHeight);
            }
            g.setColour (theme (Tok::pianoOut));
            g.fillRect (piano.getX(), y + rowHeight - 1, pianoWidth, 1);
            if (pc == 0)
            {
                g.setColour (theme (Tok::pianoText));
                g.setFont (Theme::px (9.5f));
                g.drawText (core::noteName (note), juce::Rectangle<int> (piano.getX(), y, pianoWidth - 3, rowHeight),
                            juce::Justification::centredRight, false);
            }
        }

        // Grid: sixteenths, beats, bars.
        for (int i = 0; i <= beats * 4; ++i)
        {
            const float x = (float) grid.getX() + beatWidth * (float) i / 4.0f;
            const Tok tok = i % 16 == 0 ? Tok::gridBar : i % 4 == 0 ? Tok::gridBeat : Tok::gridSub;
            g.setColour (theme (tok));
            g.fillRect (juce::Rectangle<float> (x, (float) grid.getY(), 1.0f, (float) grid.getHeight()));
        }

        // Ruler and corner.
        g.setColour (theme (Tok::hdr));
        g.fillRect (ruler);
        g.fillRect (0, 0, pianoWidth, rulerHeight);
        g.setColour (theme (Tok::line));
        g.fillRect (0, rulerHeight - 1, all.getWidth(), 1);
        g.fillRect (pianoWidth - 1, 0, 1, all.getHeight());
        g.setFont (Theme::px (11.0f));
        for (int b = 0; b < bars; ++b)
        {
            const float x = (float) ruler.getX() + beatWidth * 4.0f * (float) b;
            g.setColour (theme (Tok::rulerTick));
            g.fillRect (juce::Rectangle<float> (x, 0.0f, 1.0f, (float) rulerHeight));
            g.setColour (theme (Tok::rulerText));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 4.0f, 0.0f, 30.0f, (float) rulerHeight),
                        juce::Justification::centredLeft, false);
        }

        // The host's playhead while Cubase plays, folded onto these 4 bars.
        if (playhead.has_value())
        {
            const double beat = std::fmod (std::fmod (*playhead, (double) beats) + (double) beats, (double) beats);
            const float x = (float) grid.getX() + beatWidth * (float) beat;
            g.setColour (theme (Tok::playhead));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, (float) all.getHeight()));
        }

        // What comes here, and when.
        const juce::String message ("The piano roll arrives in M4. For now StrandLock plays the loaded sound on every note Cubase sends.");
        const auto font = Theme::px (12.0f);
        const float w = juce::jmin ((float) grid.getWidth() - 24.0f, Theme::textWidth (font, message) + 28.0f);
        const auto box = juce::Rectangle<float> (w, 30.0f).withCentre (grid.toFloat().getCentre());
        g.setColour (theme (Tok::tipBg));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (theme (Tok::tipTxt));
        g.setFont (font);
        g.drawFittedText (message, box.reduced (12.0f, 2.0f).toNearestInt(), juce::Justification::centred, 2, 0.85f);
    }

    //==============================================================================================
    MainView::MainView (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          themeButton (t, "Light", FlatButton::Icon::sun),
          keysButton (t, "Keys", FlatButton::Icon::keys),
          tempoLabel (t, "Tempo (host)", Caption::Style::label),
          scaleLabel (t, "UI Scale", Caption::Style::label),
          status (t, juce::String ("Probe build ") + core::kPluginVersion, Caption::Style::sub),
          buildButton (t, "Build"),
          nodesButton (t, "Nodes", FlatButton::Icon::nodes),
          selInfo (t, "No notes selected", Caption::Style::sub),
          selectAll (t, "Select all"),
          duplicate (t, "Duplicate"),
          deleteNotes (t, "Delete"),
          clearAll (t, "Clear all"),
          tabStrip (t, juce::StringArray (kTabNames, numTabs)),
          roll (t),
          strip (t, p)
    {
        // Top bar.
        themeButton.setTooltip ("Dark or light (remembered for every project)");
        themeButton.onClick = [this] { if (onThemeToggle) onThemeToggle(); };
        keysButton.setTooltip ("Computer keyboard. Off: every key goes to Cubase. On: StrandLock takes the keys when you click it (never Space).");
        keysButton.onClick = [this] { if (onKeysToggle) onKeysToggle(); };
        for (int s : kScales)
            uiScaleBox.addItem (juce::String (s) + " %", s);
        uiScaleBox.setTooltip ("Make everything in StrandLock bigger or smaller");
        uiScaleBox.setTitle ("UI Scale");
        uiScaleBox.onChange = [this] { if (onUiScale && uiScaleBox.getSelectedId() > 0) onUiScale (uiScaleBox.getSelectedId()); };
        for (juce::Component* c : { (juce::Component*) &themeButton, (juce::Component*) &keysButton, (juce::Component*) &tempoLabel,
                                    (juce::Component*) &scaleLabel, (juce::Component*) &status, (juce::Component*) &uiScaleBox })
            addAndMakeVisible (c);

        // Mode bar and selection row.
        buildModeBar();
        selInfo.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (selInfo);
        for (auto* b : { &selectAll, &duplicate, &deleteNotes, &clearAll })
        {
            b->setEnabled (false);
            b->setTooltip ("Notes arrive with the piano roll in M4");
            addAndMakeVisible (*b);
        }

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
        paneViewport.setScrollBarThickness (8);
        paneViewport.setViewedComponent (&paneContent, false);
        addChildComponent (paneViewport);

        diagPage = std::make_unique<DiagnosticsPage> (t, p);
        diagPage->onCopied = [this] { showToast ("Report copied: paste it into your reply"); };
        diagViewport.setScrollBarsShown (true, false);
        diagViewport.setScrollBarThickness (8);
        diagViewport.setViewedComponent (diagPage.get(), false);
        addChildComponent (diagViewport);

        addAndMakeVisible (roll);
        addAndMakeVisible (strip);
        strip.onToast = [this] (const juce::String& text, int ms) { showToast (text, ms); };
        toast = std::make_unique<ToastView> (t);
        addChildComponent (*toast);

        // Every control bound to a parameter, for the 30 Hz timer (decision 1). The tree never changes after this.
        collectSynced (*this, synced);

        // Where the person left the page (UIST).
        const auto ui = processor.getUiState();
        mode = juce::jlimit (1, 8, (int) ui.mode);
        for (int i = 0; i < 4; ++i)
            if (ui.subLane == core::laneInfo (core::kSampleLanes[i]).code)
                subLane = i;
        for (int i = 0; i < numTabs; ++i)
            if (ui.tab == kTabIds[i])
                openTabIndex = i;
        // openTab unfolds the pane (and saves that), so the saved fold is kept aside and put back after it.
        const bool startFolded = ui.folded != 0;
        tabStrip.setSelected (openTabIndex);

        themeChanged();
        paintModes();
        updateHint();
        openTab (openTabIndex);
        if (startFolded)
            setFolded (true);
    }

    MainView::~MainView()
    {
        paneViewport.setViewedComponent (nullptr, false);
        diagViewport.setViewedComponent (nullptr, false);
    }

    void MainView::buildModeBar()
    {
        buildButton.setKeyChip ("1");
        buildButton.setTooltip ("Build: add, move and stretch notes (the roll arrives in M4)");
        buildButton.onClick = [this] { setMode (1); };
        addAndMakeVisible (buildButton);
        modeItems.push_back ({ &buildButton, buildButton.preferredWidth(), kItemHeight, false });
        modeItems.push_back ({ nullptr, 1, kItemHeight, false });

        for (int m = 2; m <= 8; ++m)
        {
            const auto& info = core::kModes[m - 1];
            auto wrap = std::make_unique<LaneWrap> (theme, info, laneTok (info.lanes[0]));
            wrap->lane.setTooltip (m == 8 ? juce::String ("Sample: how each note moves through the sound")
                                          : "Draw " + juce::String (info.name) + " on the notes (M4)");
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
        paintModes();
        updateHint();
        saveUiState();
    }

    void MainView::updateHint()
    {
        hintText = kModeHints[mode - 1];
        if (mode == 8)
            hintText = hintText.replace ("SAMPLE (8)", "SAMPLE (8), " + juce::String (core::laneInfo (core::kSampleLanes[subLane]).name).toUpperCase());
        repaint (hintArea);
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

        roll.setPlayhead (s.hasPosition && s.playing && s.hasPpq ? std::optional<double> (s.ppq) : std::nullopt);
        strip.refresh();

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
        const int top = (roll.isVisible() ? roll.getY() : paneArea.getY()) + 10;
        toast->setBounds (card.getCentreX() - w / 2, top, w, 14 + 16 * lines);
        toast->toFront (false);
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

    void MainView::resized()
    {
        auto area = getLocalBounds().withTrimmedLeft (kSideMargin).withTrimmedRight (kSideMargin)
                                    .withTrimmedTop (kTopMargin).withTrimmedBottom (kTopMargin);
        card = area;

        // Top bar: logo, wordmark | theme, Keys | tempo | UI Scale ......... status
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
            topBarSeparators.push_back (x);
            x += 1 + kGap;
            place (themeButton, themeButton.preferredWidth());
            place (keysButton, keysButton.preferredWidth());
            topBarSeparators.push_back (x);
            x += 1 + kGap;
            place (tempoLabel, tempoLabel.preferredWidth(), 18);
            x += 64 + kGap;   // the tempo box, painted
            topBarSeparators.push_back (x);
            x += 1 + kGap;
            place (scaleLabel, scaleLabel.preferredWidth(), 18);
            place (uiScaleBox, 84);
            const int sw = status.preferredWidth();
            status.setBounds (juce::jmax (x, r.getRight() - sw), cy - 9, juce::jmin (sw, juce::jmax (0, r.getRight() - x)), 18);
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

        selRowArea = area.removeFromTop (kSelRowHeight);
        {
            auto r = selRowArea.reduced (kBarPadX, 0);
            const int cy = r.getCentreY();
            int x = r.getX();
            selInfo.setBounds (x, cy - 9, selInfo.preferredWidth(), 18);
            x = selInfo.getRight() + kGap;
            for (auto* b : { &selectAll, &duplicate, &deleteNotes, &clearAll })
            {
                const int w = b->preferredWidth();
                b->setBounds (x, cy - kItemHeight / 2, w, kItemHeight);
                x += w + kGap;
            }
        }

        tabsArea = area.removeFromTop (kTabsHeight);
        tabStrip.setBounds (tabsArea.withTrimmedLeft (6).withTrimmedRight (70).withTrimmedBottom (1));
        foldButton->setBounds (tabsArea.getRight() - 64, tabsArea.getY() + 4, 56, kTabsHeight - 8);

        hintArea = area.removeFromBottom (kHintHeight);
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
            toast->setBounds (toast->getBounds().withY ((roll.isVisible() ? roll.getY() : paneArea.getY()) + 10));
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

        // Hint bar: the mode's hint, and "Try this" on the right.
        {
            auto r = hintArea.reduced (kBarPadX, 0);
            const auto bold = Theme::px (11.5f, true), plain = Theme::px (11.5f);
            const float tryWidth = Theme::textWidth (bold, "Try this: ") + Theme::textWidth (plain, kTryThis);
            auto tryArea = r.removeFromRight (juce::jmin ((int) std::ceil (tryWidth) + 2, r.getWidth() / 2));
            g.setColour (theme (Tok::dim));
            g.setFont (plain);
            g.drawFittedText (hintText, r.withTrimmedRight (12), juce::Justification::centredLeft, 1, 0.9f);
            g.setColour (theme (Tok::acc));
            g.setFont (bold);
            const int tw = (int) std::ceil (Theme::textWidth (bold, "Try this: "));
            g.drawText ("Try this: ", tryArea.removeFromLeft (tw), juce::Justification::centredLeft, false);
            g.setColour (theme (Tok::txt));
            g.setFont (plain);
            g.drawText (kTryThis, tryArea, juce::Justification::centredLeft, true);
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
