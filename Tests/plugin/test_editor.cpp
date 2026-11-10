// The editor (PLUGIN-PLAN 4 M0): the v4 layout at 1100 x 680, the UI Scale (75-200 %) and the window limits
// (900 x 560 to 2400 x 1600 logical), the theme toggle remembered across windows and projects, the tabs bound
// to their parameters, the Keys switch, the drops, the Diagnostics page and its Copy report, and the UI
// snapshots (dark and light at 100 % and 75 %, a 900 x 560 window, the Diagnostics page) with their mean
// luminance. The snapshot writers at the end draw the same pictures for the CI artifact (and, in the Debug
// runner, with jassert live).
//
// The runner's UserSettings is a fresh file in the temp folder; every test here that changes it puts it back
// (dark, 100 %, Keys off) so the next test starts as a new person would.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Version.h"
#include "params/ParamIds.h"
#include "state/Crc32.h"
#include "support/Check.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"
#include "ui/Logo.h"
#include "ui/Theme.h"
#include "ui/tabs/TabPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <set>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;
    using ui::ThemeId;
    using ui::Tok;

    /** The per-user settings as a new person has them: dark, 100 %, Keys off. */
    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    int channelDistance (juce::Colour a, juce::Colour b)
    {
        return std::max ({ std::abs ((int) a.getRed() - (int) b.getRed()),
                           std::abs ((int) a.getGreen() - (int) b.getGreen()),
                           std::abs ((int) a.getBlue() - (int) b.getBlue()) });
    }

    /** How many pixels in area are within tolerance of colour. */
    int countNear (const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour, int tolerance)
    {
        int n = 0;
        area = area.getIntersection (image.getBounds());
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (channelDistance (pixels.getPixelColour (x, y), colour) <= tolerance)
                    ++n;
        return n;
    }

    /** The mean Rec. 709 luma of the image's sRGB values, 0 (black) to 1 (white). */
    double meanLuminance (const juce::Image& image)
    {
        if (! image.isValid())
            return -1.0;
        double sum = 0.0;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                sum += (0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue()) / 255.0;
            }
        return sum / ((double) image.getWidth() * (double) image.getHeight());
    }

    juce::Image renderEditor (StrandLockEditor& e)
    {
        return e.createComponentSnapshot (e.getLocalBounds(), true, 1.0f);
    }

    /** Plays one second through the processor the way Cubase would: playing at 120 bpm in a 4-bar cycle, a C3
        held, the mod wheel and the bend moved; then a key and a drop, so every card of the page has something. */
    void feedHost (StrandLockProcessor& p)
    {
        harness::FakePlayHead head;
        head.info.setIsPlaying (true);
        head.info.setBpm (120.0);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        head.info.setIsLooping (true);
        head.info.setLoopPoints (juce::AudioPlayHead::LoopPoints { 0.0, 16.0 });
        p.setPlayHead (&head);

        const std::vector<harness::MidiEvent> events { harness::noteOn (1000, 60, 100), harness::controller (6000, 1, 64),
                                                       harness::pitchBend (9000, 10000), harness::noteOff (40000, 60) };
        harness::render (p, 48000.0, 48000, harness::fixedBlocks (512), events, 4096,
                         [&head] (juce::int64 start, int)
                         {
                             head.info.setTimeInSamples (start);
                             head.info.setPpqPosition (3.0 + (double) start * 2.0 / 48000.0);
                         });
        p.setPlayHead (nullptr);

        p.getDiag().keyReceived ("A", true);
        const auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StrandLock no such file.wav");
        p.getDiag().filesDropped (juce::StringArray (missing.getFullPathName()));
    }

    /** One picture of the editor. logicalWidth 0 keeps the default size; tab -1 keeps the default tab. */
    struct Shot
    {
        ThemeId theme = ThemeId::dark;
        int scalePercent = 100;
        int logicalWidth = 0, logicalHeight = 0;
        int tab = -1;
        bool feed = false;
        bool banner = false;
    };

    juce::Image shoot (const Shot& s)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();   // the default sound is made on the worker (M2): the strip draws it once it is there
        auto& settings = proc.getUserSettings();
        settings.setTheme (s.theme);
        settings.setUiScalePercent (s.scalePercent);
        settings.setKeysDefault (false);

        if (s.banner)
        {
            // Another plugin's state: kept as it is, with the banner.
            juce::MemoryBlock saved;
            proc.getStateInformation (saved);
            static_cast<char*> (saved.getData())[0] = 'G';
            proc.setStateInformation (saved.getData(), (int) saved.getSize());
        }
        if (s.feed)
            feedHost (proc);

        juce::Image image;
        {
            StrandLockEditor editor (proc);
            if (s.logicalWidth > 0)
                editor.setLogicalSize (s.logicalWidth, s.logicalHeight);
            if (s.tab >= 0)
                editor.getView().openTab (s.tab);
            editor.refreshNow();
            image = renderEditor (editor);
        }
        resetUserSettings (proc);
        return image;
    }

    const Shot kDark100 { ThemeId::dark, 100 };
    const Shot kLight100 { ThemeId::light, 100 };
    const Shot kDark75 { ThemeId::dark, 75 };
    const Shot kLight75 { ThemeId::light, 75 };
    const Shot kNarrow { ThemeId::dark, 100, 900, 560 };
    const Shot kDiagnostics { ThemeId::dark, 100, 0, 0, MainView::diagTab, true };

    std::set<juce::String> idsOf (const juce::StringArray& a)
    {
        return std::set<juce::String> (a.begin(), a.end());
    }

    /** Every component under parent that wants the keyboard. */
    int focusWanters (juce::Component& parent)
    {
        int n = 0;
        for (auto* c : parent.getChildren())
            n += (c->getWantsKeyboardFocus() ? 1 : 0) + focusWanters (*c);
        return n;
    }
}

//==================================================================================================
SL_TEST ("ui", editorSizeAndUiScale)
{
    section ("Editor: 1100 x 680, resizable from 900 x 560 to 2400 x 1600 logical, UI Scale 75-200 % remembered");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    {
        StrandLockEditor editor (proc);
        check (editor.getWidth() == 1100 && editor.getHeight() == 680 && editor.getUiScalePercent() == 100,
               fmt ("opens at 1100 x 680 and 100 %% (%d x %d, %d %%)", editor.getWidth(), editor.getHeight(), editor.getUiScalePercent()));

        const auto* limits = editor.getConstrainer();
        const auto limitsAre = [limits] (int a, int b, int c, int d)
        {
            return limits != nullptr && limits->getMinimumWidth() == a && limits->getMinimumHeight() == b
                   && limits->getMaximumWidth() == c && limits->getMaximumHeight() == d;
        };
        check (editor.isResizable() && limitsAre (900, 560, 2400, 1600) && limits != nullptr && juce::exactlyEqual (limits->getFixedAspectRatio(), 0.0),
               fmt ("resizable from 900 x 560 to 2400 x 1600, any shape (%d x %d to %d x %d)",
                    limits != nullptr ? limits->getMinimumWidth() : 0, limits != nullptr ? limits->getMinimumHeight() : 0,
                    limits != nullptr ? limits->getMaximumWidth() : 0, limits != nullptr ? limits->getMaximumHeight() : 0));

        editor.setUiScalePercent (75);
        check (editor.getWidth() == 825 && editor.getHeight() == 510 && limitsAre (675, 420, 1800, 1200)
                   && editor.getLogicalSize() == juce::Point<int> (1100, 680),
               fmt ("75 %%: the window is 825 x 510 (%d x %d), limits 675 x 420 to 1800 x 1200, still 1100 x 680 logical",
                    editor.getWidth(), editor.getHeight()));
        check (editor.getView().getWidth() == 1100 && editor.getView().getHeight() == 680
                   && std::abs (editor.getView().getTransform().mat00 - 0.75f) < 1.0e-6f,
               fmt ("the layout stays 1100 x 680 and is drawn at 0.75 (%d x %d)", editor.getView().getWidth(), editor.getView().getHeight()));

        editor.setUiScalePercent (200);
        check (editor.getWidth() == 2200 && editor.getHeight() == 1360 && limitsAre (1800, 1120, 4800, 3200),
               fmt ("200 %%: 2200 x 1360 (%d x %d), limits 1800 x 1120 to 4800 x 3200", editor.getWidth(), editor.getHeight()));

        editor.setUiScalePercent (300);
        const int high = editor.getUiScalePercent();
        editor.setUiScalePercent (10);
        const int low = editor.getUiScalePercent();
        check (high == 200 && low == 75, fmt ("the scale stays within 75 to 200 %% (300 gives %d, 10 gives %d)", high, low));

        const auto mine = proc.getUserSettings().uiScalePercent();
        check (mine.has_value() && *mine == 75 && proc.getUiState().uiScalePercent == 75,
               fmt ("the choice is this person's setting and the project's copy (%d, %d)", mine.value_or (-1), (int) proc.getUiState().uiScalePercent));

        // A narrow window: the logical size is what the person dragged, at any scale.
        editor.setUiScalePercent (100);
        editor.setSize (900, 560);
        check (editor.getLogicalSize() == juce::Point<int> (900, 560) && proc.getUiState().logicalWidth == 900 && proc.getUiState().logicalHeight == 560,
               fmt ("dragged to 900 x 560: kept in the project (%d x %d)", (int) proc.getUiState().logicalWidth, (int) proc.getUiState().logicalHeight));
        editor.setUiScalePercent (150);
        check (editor.getWidth() == 1350 && editor.getHeight() == 840,
               fmt ("then at 150 %%: 1350 x 840 (%d x %d)", editor.getWidth(), editor.getHeight()));
    }
    {
        StrandLockEditor again (proc);
        check (again.getUiScalePercent() == 150 && again.getWidth() == 1350 && again.getHeight() == 840,
               fmt ("a new window opens at 150 %% and 900 x 560 logical (%d %%, %d x %d)", again.getUiScalePercent(), again.getWidth(), again.getHeight()));
    }
    {
        StrandLockProcessor otherProject;
        StrandLockEditor other (otherProject);
        check (other.getUiScalePercent() == 150 && other.getWidth() == 1650 && other.getHeight() == 1020,
               fmt ("another project opens at this person's 150 %% (%d %%, %d x %d)", other.getUiScalePercent(), other.getWidth(), other.getHeight()));
    }
    resetUserSettings (proc);
}

SL_TEST ("ui", editorThemeToggleRemembered)
{
    section ("Editor: the sun/moon button switches dark and light, remembered across windows and projects");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    {
        StrandLockEditor editor (proc);
        auto& button = editor.getView().getThemeButton();
        const auto darkImage = renderEditor (editor);
        check (editor.getThemeId() == ThemeId::dark && button.getButtonText() == "Light",
               fmt ("a new person starts dark; the button offers \"%s\"", button.getButtonText().toRawUTF8()));

        button.onClick();
        const auto lightImage = renderEditor (editor);
        const auto mine = proc.getUserSettings().theme();
        check (editor.getThemeId() == ThemeId::light && button.getButtonText() == "Dark" && mine.has_value()
                   && *mine == state::Theme::light && proc.getUiState().theme == 1,
               "one click: light, saved for this person and in the project (UIST)");

        const auto bgDark = darkImage.getPixelAt (4, 300), bgLight = lightImage.getPixelAt (4, 300);
        check (channelDistance (bgDark, juce::Colour (0xff151618)) <= 1 && channelDistance (bgLight, juce::Colour (0xfff3f4f7)) <= 1,
               fmt ("the window's edge is #151618 dark (#%s) and #F3F4F7 light (#%s)",
                    bgDark.toDisplayString (false).toRawUTF8(), bgLight.toDisplayString (false).toRawUTF8()));
        const int lightTop = countNear (lightImage, editor.getView().getTopBarArea().reduced (2), juce::Colour (0xffffffff), 0);
        check (lightTop > 10000, fmt ("the light top bar is white (%d pixels)", lightTop));
    }
    {
        StrandLockEditor reopened (proc);
        check (reopened.getThemeId() == ThemeId::light, "closing and reopening the window keeps light");
    }
    {
        StrandLockProcessor newProject;
        StrandLockEditor fresh (newProject);
        check (fresh.getThemeId() == ThemeId::light, "a new project opens light too");
        fresh.getView().getThemeButton().onClick();
        check (fresh.getThemeId() == ThemeId::dark && proc.getTheme() == state::Theme::dark,
               "and switching there switches it for this person everywhere");
    }
    resetUserSettings (proc);
}

SL_TEST ("ui", editorLayout)
{
    section ("Editor: the v4 layout (logo, wordmark, top bar, modes, tabs, roll, waveform strip, hints)");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();   // the default sound is made on the worker (M2)
    StrandLockEditor editor (proc);
    editor.refreshNow();
    auto& view = editor.getView();
    const auto image = renderEditor (editor);
    const auto& dark = ui::palette (ThemeId::dark);

    const int ring = countNear (image, view.getLogoArea(), dark[Tok::acc], 12);
    const int word = countNear (image, view.getWordmarkArea(), dark[Tok::txt], 12);
    check (ring >= 20, fmt ("the logo's ring in the accent #B58CFF (%d pixels)", ring));
    check (word >= 30, fmt ("the STRANDLOCK wordmark in the text colour (%d pixels)", word));
    check (view.getLogoArea().getWidth() == 22 && view.getWordmarkArea().getX() > view.getLogoArea().getRight(),
           "the 22 px logo, then the wordmark to its right");

    // The bars, top to bottom, and the room the roll keeps.
    const auto top = view.getTopBarArea(), pane = view.getPaneArea(), roll = view.getRollArea(), strip = view.getStripArea();
    check (top.getY() < pane.getY() && pane.getBottom() <= roll.getY() && roll.getBottom() <= strip.getY() && strip.getBottom() < 680,
           fmt ("top bar %d, pane %d-%d, roll %d-%d, strip %d-%d", top.getY(), pane.getY(), pane.getBottom(), roll.getY(), roll.getBottom(), strip.getY(), strip.getBottom()));
    check (roll.getHeight() >= 150 && pane.getHeight() >= 80, fmt ("the roll keeps %d px, the pane %d px", roll.getHeight(), pane.getHeight()));

    // The mode bar: 1 Build lit, 2-8 in their lane colours.
    check (view.getMode() == 1 && view.getModeButton (1).getToggleState() && ! view.getModeButton (2).getToggleState(),
           "mode 1 Build is on");
    view.getModeButton (3).onClick();
    check (view.getMode() == 3 && view.getModeButton (3).getToggleState() && ! view.getModeButton (1).getToggleState()
               && proc.getUiState().mode == 3 && view.getHintText().startsWith ("VOLUME (3)"),
           fmt ("clicking 3 Volume makes it the mode (UIST %d) and the hint says \"%s\"", (int) proc.getUiState().mode,
                view.getHintText().substring (0, 24).toRawUTF8()));
    view.setMode (8, 2);
    check (proc.getUiState().mode == 8 && proc.getUiState().subLane == "grain" && view.getHintText().contains ("GRAIN"),
           fmt ("mode 8 with the Grain chip: UIST \"%s\"", proc.getUiState().subLane.c_str()));
    view.setMode (1);

    // The waveform strip draws the loaded starter (the Saw that opens up, D9).
    const auto loaded = proc.getCurrentSound();
    check (loaded != nullptr && view.getStrip().shownSound() == loaded.get() && view.getStrip().headerText().startsWith ("Saw that opens up"),
           fmt ("the strip shows the loaded sound (\"%s\")", view.getStrip().headerText().toRawUTF8()));
    // The prototype draws the wave at 80 % over the strip's background (sl-ui.js renderWave: globalAlpha 0.8).
    const auto blended = dark[Tok::stripBg].interpolatedWith (dark[Tok::wave], ui::WaveStrip::waveAlpha);
    const int wavePixels = countNear (image, strip, blended, 3);
    const int fullWave = countNear (image, strip, dark[Tok::wave], 3);
    check (wavePixels > 3000 && fullWave < 100,
           fmt ("the waveform is drawn in the wave colour at 80 %% over the strip, #%s (%d pixels; %d at full opacity)",
                blended.toDisplayString (false).toRawUTF8(), wavePixels, fullWave));

    // The host's tempo, read only: none before the host plays.
    check (view.getTempoText() == "-", fmt ("tempo before the host plays: \"%s\"", view.getTempoText().toRawUTF8()));
    feedHost (proc);
    editor.refreshNow();
    check (view.getTempoText() == "120", fmt ("tempo after a block at 120 bpm: \"%s\"", view.getTempoText().toRawUTF8()));
}

SL_TEST ("ui", editorTabsBoundToParameters)
{
    section ("Editor: the Macros, Sample and Sound tabs are plain controls bound to their parameters");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    StrandLockEditor editor (proc);
    auto& view = editor.getView();

    auto* macros = view.getTabPanel (MainView::macrosTab);
    auto* sample = view.getTabPanel (MainView::sampleTab);
    auto* sound = view.getTabPanel (MainView::soundTab);
    check (macros != nullptr && sample != nullptr && sound != nullptr, "the three tabs exist");
    if (macros == nullptr || sample == nullptr || sound == nullptr)
        return;

    const std::set<juce::String> wantMacros { "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8",
                                              "ampOffset", "vibOffset", "toneOffset", "formantOffset", "hollowOffset" };
    check (idsOf (macros->getBoundIds()) == wantMacros && macros->getBoundIds().size() == 13 && macros->getKnobs().size() == 13,
           fmt ("Macros: the 8 macros and the 5 offsets, as 13 knobs (%d)", (int) macros->getKnobs().size()));

    // Every parameter has a control, except the host's bypass and the two Slides settings (M5's tab).
    std::multiset<juce::String> all;
    for (auto* tab : { macros, sample, sound })
        for (const auto& id : tab->getBoundIds())
            all.insert (id);
    int twice = 0, unknown = 0;
    for (const auto& id : all)
    {
        twice += all.count (id) > 1 ? 1 : 0;
        unknown += params::findParam (id.toStdString()) < 0 ? 1 : 0;
    }
    std::set<juce::String> without;
    for (const auto& spec : params::kParams)
        if (all.count (spec.id) == 0)
            without.insert (spec.id);
    check (twice == 0 && unknown == 0 && without == std::set<juce::String> { "bypass", "glideMode", "glideTime" },
           fmt ("every parameter but bypass, Auto slide and Slide time has exactly one control (%d without, %d twice, %d unknown)",
                (int) without.size(), twice, unknown));

    // Turning each knob moves its own parameter, end to end.
    int moved = 0, total = 0;
    juce::String firstFailure;
    for (auto* tab : { macros, sample, sound })
        for (auto* knob : tab->getKnobs())
        {
            ++total;
            auto& slider = knob->getSlider();
            auto& param = knob->getParameter();
            slider.setValue (slider.getMaximum(), juce::sendNotificationSync);
            const float atMax = param.getValue();
            slider.setValue (slider.getMinimum(), juce::sendNotificationSync);
            const float atMin = param.getValue();
            const bool ok = std::abs (atMax - 1.0f) < 1.0e-4f && std::abs (atMin) < 1.0e-4f;
            moved += ok ? 1 : 0;
            if (! ok && firstFailure.isEmpty())
                firstFailure = param.getParameterID();
            param.setValueNotifyingHost (param.getDefaultValue());
        }
    check (moved == total && total == 13 + 14 + 7,
           fmt ("all %d knobs move their own parameter from 0 to 1 (%d did)%s%s", total, moved,
                firstFailure.isEmpty() ? "" : "; first that did not: ", firstFailure.toRawUTF8()));

    // A bipolar knob lights its arc from twelve o'clock, so its default must sit there; otherwise the knob shows
    // a lit arc at its default (sl-ui-sample.js starts the arc at the default).
    int bipolar = 0;
    juce::String offCentre;
    for (auto* tab : { macros, sample, sound })
        for (auto* knob : tab->getKnobs())
            if ((bool) knob->getSlider().getProperties()["bipolar"])
            {
                ++bipolar;
                auto& param = knob->getParameter();
                const double at = knob->getSlider().valueToProportionOfLength (param.convertFrom0to1 (param.getDefaultValue()));
                if (std::abs (at - 0.5) > 1.0e-4)
                    offCentre << param.getParameterID() << " (" << juce::String (at, 3) << ") ";
            }
    check (bipolar == 15 && offCentre.isEmpty(),
           fmt ("the 15 bipolar knobs (8 macros, 5 offsets, Pitch, Cents) have their default at twelve o'clock (%d bipolar; off centre: %s)",
                bipolar, offCentre.isEmpty() ? "none" : offCentre.toRawUTF8()));

    // A knob shows the parameter's own readout (once the editor's timer has moved the knobs back to their defaults).
    view.syncControls();
    auto* speedKnob = sample->getKnobs().empty() ? nullptr : sample->getKnobs().front();
    check (speedKnob != nullptr && speedKnob->getParameter().getParameterID() == "speed"
               && speedKnob->getSlider().getTextFromValue (speedKnob->getSlider().getValue()) == "1x",
           fmt ("the Sample tab's first knob is Speed reading \"%s\"",
                speedKnob != nullptr ? speedKnob->getSlider().getTextFromValue (speedKnob->getSlider().getValue()).toRawUTF8() : ""));

    // Tabs: a click opens one; a click on the open one folds the pane; the choice is in the project.
    auto& tabs = view.getTabStrip();
    tabs.onClick (MainView::macrosTab);
    const bool opened = view.getOpenTab() == MainView::macrosTab && ! view.isFolded() && proc.getUiState().tab == "macros";
    tabs.onClick (MainView::macrosTab);
    const bool foldedNow = view.isFolded() && proc.getUiState().folded == 1;
    tabs.onClick (MainView::soundTab);
    check (opened && foldedNow && view.getOpenTab() == MainView::soundTab && ! view.isFolded() && proc.getUiState().tab == "sound",
           fmt ("Macros opens, a second click folds it, Sound opens (UIST tab \"%s\")", proc.getUiState().tab.c_str()));
    {
        StrandLockEditor again (proc);
        check (again.getView().getOpenTab() == MainView::soundTab, "a new window opens on the same tab");
        again.getView().setFolded (true);
    }
    {
        StrandLockEditor third (proc);
        check (third.getView().isFolded() && third.getView().getOpenTab() == MainView::soundTab && proc.getUiState().folded == 1,
               fmt ("folded there, the next window opens folded on that tab and UIST keeps it (folded %s, UIST %d)",
                    third.getView().isFolded() ? "yes" : "no", (int) proc.getUiState().folded));
        third.getView().getTabStrip().onClick (MainView::soundTab);
        check (! third.getView().isFolded() && proc.getUiState().folded == 0, "a click on the tab unfolds it again");
    }
}

SL_TEST ("ui", editorKeysSwitch)
{
    section ("Editor: Keys off never takes the keyboard; on takes it when clicked; Space always goes to Cubase");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    StrandLockEditor editor (proc);
    const auto keysSeen = [&proc] { return proc.getDiag().snapshot().keys; };

    check (! editor.isKeysOn() && ! editor.getWantsKeyboardFocus() && ! editor.getMouseClickGrabsKeyboardFocus() && focusWanters (editor) == 0,
           fmt ("Keys starts off: neither the editor nor any of its %d controls wants the keyboard (%d do)",
                (int) editor.getNumChildComponents(), focusWanters (editor)));

    const bool aOff = editor.keyPressed (juce::KeyPress ('a'));
    const bool spaceOff = editor.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    auto keys = keysSeen();
    check (! aOff && ! spaceOff && keys.size() == 2 && ! keys[0].keysSwitchOn && ! keys[1].keysSwitchOn,
           fmt ("off: a key that reaches it anyway is recorded (%d) and left for Cubase", (int) keys.size()));

    editor.getView().getKeysButton().onClick();
    check (editor.isKeysOn() && editor.getView().getKeysButton().getToggleState() && editor.getWantsKeyboardFocus()
               && editor.getMouseClickGrabsKeyboardFocus() && focusWanters (editor) == 0 && proc.getUserSettings().keysDefault(),
           "Keys on: the editor takes the keyboard when clicked (its controls pass the click up), and the next window starts on");

    const bool aOn = editor.keyPressed (juce::KeyPress ('a'));
    const bool undoOn = editor.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0));
    const bool spaceOn = editor.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    keys = keysSeen();
    check (aOn && undoOn && ! spaceOn && keys.size() == 5 && keys[2].keysSwitchOn && keys[4].keysSwitchOn,
           fmt ("on: A and Ctrl+Z are kept, Space still goes to Cubase, all recorded (%d keys)", (int) keys.size()));
    check (keys.size() == 5 && keys[3].key.containsIgnoreCase ("Z") && keys[4].key.containsIgnoreCase ("space"),
           fmt ("recorded as \"%s\" and \"%s\"", keys.size() == 5 ? keys[3].key.toRawUTF8() : "", keys.size() == 5 ? keys[4].key.toRawUTF8() : ""));

    // A theme switch rebuilds the drop-downs' labels: they still must not want the keyboard.
    editor.setThemeId (ThemeId::light);
    check (focusWanters (editor) == 0, fmt ("after a theme switch no control wants the keyboard (%d do)", focusWanters (editor)));
    {
        StrandLockEditor next (proc);
        check (next.isKeysOn() && next.getWantsKeyboardFocus(), "a new window starts with Keys on");
    }
    editor.setKeysOn (false);
    check (! editor.getWantsKeyboardFocus() && ! proc.getUserSettings().keysDefault(), "Keys off again");
    resetUserSettings (proc);
}

SL_TEST ("ui", editorDropsAndDiagnostics)
{
    section ("Editor: drops anywhere load the sound and are recorded with the file's length; the Diagnostics page is live; Copy report");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    StrandLockEditor editor (proc);
    auto& view = editor.getView();
    auto& page = view.getDiagnosticsPage();

    check (editor.isInterestedInFileDrag (juce::StringArray ("x.wav")) && editor.isInterestedInTextDrag ("some text"),
           "the whole window takes file and text drags");

    // A half-second stereo WAV at 48 kHz, a text file and a missing file.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("StrandLockTests");
    dir.createDirectory();
    const auto wavFile = dir.getNonexistentChildFile ("editor drop", ".wav", false);
    {
        juce::AudioBuffer<float> half (2, 24000);
        for (int i = 0; i < 24000; ++i)
            for (int ch = 0; ch < 2; ++ch)
                half.setSample (ch, i, 0.2f * std::sin (0.03f * (float) i));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wavFile);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
        check (writer != nullptr && writer->writeFromAudioSampleBuffer (half, 0, 24000), "a test WAV is written");
    }
    const auto textFile = dir.getNonexistentChildFile ("notes", ".txt", false);
    textFile.replaceWithText ("not a sound");
    const auto missing = dir.getChildFile ("gone.wav");

    juce::StringArray files;
    files.add (wavFile.getFullPathName());
    files.add (textFile.getFullPathName());
    files.add (missing.getFullPathName());
    editor.fileDragEnter (files, 10, 10);
    editor.filesDropped (files, 10, 10);

    const auto s = proc.getDiag().snapshot();
    const bool wavOk = s.drops.size() == 3 && s.drops[0].readable && s.drops[0].frames == 24000 && s.drops[0].channels == 2
                       && std::abs (s.drops[0].seconds - 0.5) < 1.0e-9 && s.drops[0].what == wavFile.getFullPathName();
    check (wavOk, fmt ("the WAV arrives with its path and length (%lld frames, %.3f s)", s.drops.empty() ? 0LL : (long long) s.drops[0].frames,
                       s.drops.empty() ? 0.0 : s.drops[0].seconds));
    check (s.drops.size() == 3 && ! s.drops[1].readable && s.drops[1].bytes > 0 && ! s.drops[2].readable && s.drops[2].bytes < 0,
           "the text file is not a sound; the missing file is not found");
    // From M2 the first file dropped loads as the sound (sl-ui.js), rooted at C3; the page still records the drop.
    proc.waitForSounds();
    editor.refreshNow();
    const auto loaded = proc.getCurrentSound();
    check (loaded != nullptr && ! loaded->builtIn && loaded->name == wavFile.getFileName().toStdString()
               && view.getToastText() == "Loaded " + wavFile.getFileName(),
           fmt ("the dropped WAV is the sound now (toast \"%s\")", view.getToastText().toRawUTF8()));

    view.openTab (MainView::diagTab);
    editor.refreshNow();
    const auto shown = page.shownText().joinIntoString ("\n");
    check (shown.contains (wavFile.getFileName()) && shown.contains ("24000 frames, 0.500 s at 48000 Hz, 2 channels"),
           "the page shows the file's name and length");

    editor.textDropped ("Audio 01, Event 3", 5, 5);
    check (proc.getDiag().snapshot().drops.size() == 4 && page.shownText().joinIntoString ("\n").contains ("Audio 01, Event 3"),
           "a text drop is recorded and shown");

    // Live: what the host plays shows on the next refresh.
    feedHost (proc);
    editor.refreshNow();
    const auto live = page.shownText().joinIntoString ("\n");
    check (live.contains ("120.000 bpm") && live.contains ("C3 (60), velocity 100") && live.contains ("Mod wheel: 64")
               && live.contains ("512 x 93; 384 x 1") && live.contains ("Cycle wraps: 0"),
           "after a second of playing: the tempo, the last note, the mod wheel and the block sizes are on the page");

    // Copy report: what DiagModel::reportText gives, to the clipboard.
    juce::String copied;
    page.copyText = [&copied] (const juce::String& text) { copied = text; };
    page.getCopyButton().onClick();
    check (copied.startsWith (juce::String ("StrandLock ") + core::kPluginVersion + " diagnostics report")
               && copied.contains ("Blocks: 94") && copied.contains ("Dropped file: " + wavFile.getFullPathName()),
           fmt ("Copy report puts the report on the clipboard (%d characters)", copied.length()));

    wavFile.deleteFile();
    textFile.deleteFile();
}

SL_TEST ("ui", editorStateBanner)
{
    section ("Editor: a state this version cannot read shows the banner");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    juce::MemoryBlock saved;
    proc.getStateInformation (saved);
    static_cast<char*> (saved.getData())[0] = 'G';
    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    StrandLockEditor editor (proc);
    editor.refreshNow();
    check (proc.getStateBanner().isNotEmpty() && editor.getView().getBannerText() == proc.getStateBanner(),
           fmt ("the banner reads \"%s\"", editor.getView().getBannerText().substring (0, 60).toRawUTF8()));

    StrandLockProcessor clean;
    StrandLockEditor quiet (clean);
    quiet.refreshNow();
    check (quiet.getView().getBannerText().isEmpty(), "a readable state shows none");
}

SL_TEST ("ui", editorKeepsUnreadableState)
{
    section ("Editor: opening the window and using it never replaces a state this version cannot read (PLUGIN-PLAN 3 (e))");

    // A project saved by a newer StrandLock: format version 2, header CRC good.
    StrandLockProcessor proc;
    resetUserSettings (proc);
    juce::MemoryBlock block;
    proc.getStateInformation (block);
    auto* bytes = static_cast<std::uint8_t*> (block.getData());
    bytes[4] = (std::uint8_t) (core::kStateFormatVersion + 1);
    const auto crc = state::crc32 (bytes, 12);
    for (int i = 0; i < 4; ++i)
        bytes[12 + i] = (std::uint8_t) ((crc >> (8 * i)) & 0xffu);
    const juce::MemoryBlock newer (block);
    proc.setStateInformation (newer.getData(), (int) newer.getSize());

    const auto savesNewer = [&proc, &newer]
    {
        juce::MemoryBlock now;
        proc.getStateInformation (now);
        return now == newer;
    };
    check (savesNewer() && proc.getStateBanner().isNotEmpty(), "it loads nothing, is kept and shows the banner");

    {
        StrandLockEditor editor (proc);   // opening writes the window's state (UIST)
        auto& view = editor.getView();
        const bool afterOpen = savesNewer();
        view.getTabStrip().onClick (MainView::macrosTab);
        view.getTabStrip().onClick (MainView::diagTab);
        view.getTabStrip().onClick (MainView::diagTab);   // folds the pane
        view.getModeButton (3).onClick();
        view.getThemeButton().onClick();
        editor.setUiScalePercent (125);
        editor.setSize (1300, 800);
        editor.refreshNow();
        check (afterOpen && savesNewer() && proc.getStateBanner().isNotEmpty() && view.getBannerText().isNotEmpty(),
               fmt ("after opening the window, two tabs, folding, mode 3, the theme, 125 %% and a resize: still saved byte for byte "
                    "(after opening %s, at the end %s), banner shown", afterOpen ? "yes" : "NO", savesNewer() ? "yes" : "NO"));
    }
    {
        StrandLockEditor again (proc);
        check (savesNewer(), "closing the window and opening it again keeps it too");

        auto& gain = proc.getParam (params::P::outGain);
        gain.setValueNotifyingHost (0.3f);
        juce::MemoryBlock after;
        proc.getStateInformation (after);
        for (int i = 0; i < 8; ++i)
            again.refreshNow();   // the view looks at the banner on every 8th tick
        check (after != newer && state::parseContainer (after.getData(), after.getSize()).status == state::HeaderStatus::ok
                   && proc.getStateBanner().isEmpty() && again.getView().getBannerText().isEmpty(),
               "a parameter edit replaces it with this version's state, and the banner goes");
    }
    resetUserSettings (proc);
}

SL_TEST ("ui", uiSnapshotsLuminance)
{
    section ("UI snapshots: dark and light at 100 % and 75 %, a 900 x 560 window, the Diagnostics page");

    struct Case { const char* name; Shot shot; int width, height; bool dark; };
    const Case cases[] = {
        { "dark 100 %", kDark100, 1100, 680, true },
        { "light 100 %", kLight100, 1100, 680, false },
        { "dark 75 %", kDark75, 825, 510, true },
        { "light 75 %", kLight75, 825, 510, false },
        { "900 x 560 window", kNarrow, 900, 560, true },
        { "Diagnostics page", kDiagnostics, 1100, 680, true },
    };

    for (const auto& c : cases)
    {
        const auto image = shoot (c.shot);
        const double lum = meanLuminance (image);
        const bool sized = image.isValid() && image.getWidth() == c.width && image.getHeight() == c.height;
        check (sized && (c.dark ? lum < 0.25 : lum > 0.75),
               fmt ("%s: %d x %d, mean luminance %.3f (%s)", c.name, image.getWidth(), image.getHeight(), lum,
                    c.dark ? "dark: below 0.25" : "light: above 0.75"));
    }

    // The Diagnostics picture is the page, not the roll: no piano keys at the roll's left edge.
    const auto diag = shoot (kDiagnostics);
    const auto normal = shoot (kDark100);
    const auto& dark = ui::palette (ThemeId::dark);
    const juce::Rectangle<int> keys (14, 420, 36, 100);
    const int keysDiag = countNear (diag, keys, dark[Tok::pianoWhite], 2);
    const int keysRoll = countNear (normal, keys, dark[Tok::pianoWhite], 2);
    check (keysRoll > 500 && keysDiag < 50, fmt ("the Diagnostics page takes the roll's place (piano key pixels %d without it, %d with it)", keysRoll, keysDiag));
}

//==================================================================================================
// The snapshots the CI uploads (StrandLock-UI-Snapshots) and the Debug runner draws with jassert live.

SL_SNAPSHOT (strandlock_dark_100)        { return shoot (kDark100); }
SL_SNAPSHOT (strandlock_light_100)       { return shoot (kLight100); }
SL_SNAPSHOT (strandlock_dark_75)         { return shoot (kDark75); }
SL_SNAPSHOT (strandlock_light_75)        { return shoot (kLight75); }
SL_SNAPSHOT (strandlock_narrow_900x560)  { return shoot (kNarrow); }
SL_SNAPSHOT (strandlock_diagnostics)     { return shoot (kDiagnostics); }

// Extra pictures for review: the other tabs and the banner.
SL_SNAPSHOT (strandlock_diagnostics_light) { return shoot ({ ThemeId::light, 100, 0, 0, MainView::diagTab, true }); }
SL_SNAPSHOT (strandlock_macros_light)      { return shoot ({ ThemeId::light, 100, 0, 0, MainView::macrosTab }); }
SL_SNAPSHOT (strandlock_sound_dark)        { return shoot ({ ThemeId::dark, 100, 0, 0, MainView::soundTab }); }
SL_SNAPSHOT (strandlock_slides_dark_150)   { return shoot ({ ThemeId::dark, 150, 0, 0, MainView::slidesTab }); }
SL_SNAPSHOT (strandlock_banner_light_narrow) { return shoot ({ ThemeId::light, 100, 900, 560, -1, false, true }); }
