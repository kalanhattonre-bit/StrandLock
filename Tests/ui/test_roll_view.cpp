// The roll's view (PLUGIN-PLAN 4 M4, 7.6 R2) besides its pictures: it stands where M0's placeholder was and shows
// the demo clip; the piano keys play through the CommandQueue while held (sl-ui.js liveOn / liveOff); the zoom
// buttons, the wheel and the scroll bars move the view (sl-ui.js zoom, its Ctrl + wheel, the scroller), which the
// project keeps (UIST); the first view centres on the notes unless the project saved one; the playhead comes from
// the engine's display frames; the mode buttons reach the document; and a refresh repaints only what changed.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "core/Seeds.h"
#include "doc/CommandQueue.h"
#include "engine/DisplayFrame.h"
#include "engine/EngineConfig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/Theme.h"
#include "ui/roll/RollView.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::ThemeId;
    using ui::Tok;
    using ui::roll::RollView;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                 juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                 &c, &c, now, at, now, 1, false);
    }

    /** One notch of a Windows mouse wheel as JUCE reports it (WM_MOUSEWHEEL 120: 0.5 x 120 / 256). */
    constexpr float kNotch = 60.0f / 256.0f;

    void spin (RollView& view, juce::Component& over, juce::Point<float> at, float deltaY, juce::ModifierKeys mods, float deltaX = 0.0f)
    {
        juce::MouseWheelDetails wheel {};
        wheel.deltaX = deltaX;
        wheel.deltaY = deltaY;
        view.mouseWheelMove (mouse (over, at, mods).getEventRelativeTo (&view), wheel);
    }

    std::vector<doc::UiCommand> drain (StrandLockProcessor& p)
    {
        std::vector<doc::UiCommand> out;
        doc::UiCommand c;
        while (p.getCommandQueue().pop (c))
            out.push_back (c);
        return out;
    }

    bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }
}

//==================================================================================================
SL_TEST ("ui", rollViewShowsTheDemoClip)
{
    section ("Roll view: it takes the placeholder's place and shows the demo clip, centred on its notes");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    StrandLockEditor editor (proc);
    editor.refreshNow();
    auto& roll = editor.getView().getRoll();
    const auto& geo = roll.getGeometry();
    const auto clip = proc.getDocument().current();

    check (clip != nullptr && clip->notes.size() == 26 && roll.isVisible() && editor.getView().getRollArea() == roll.getBounds()
               && roll.getCanvasArea().getWidth() > 900 && roll.getCanvasArea().getHeight() >= 120,
           fmt ("the roll fills the placeholder's area: %d x %d, its canvas %d x %d; the clip holds %d notes (Strand Demo has 26)",
                roll.getWidth(), roll.getHeight(), roll.getCanvasArea().getWidth(), roll.getCanvasArea().getHeight(),
                clip != nullptr ? (int) clip->notes.size() : -1));

    // the prototype's first view: the row 5 semitones above the median pitch (67 + 5 = C4) in the middle, whole pixels
    const double want = std::round (std::max (0.0, geo.cy (72.0) - geo.viewHeight() / 2.0));
    const auto visible = juce::Rectangle<double> (geo.scrollX(), geo.scrollY(), geo.viewWidth(), geo.viewHeight());
    const int shown = (int) ui::roll::RollPainter (roll.getPaintState()).visibleNotes (visible).size();
    check (juce::exactlyEqual (geo.scrollY(), want) && juce::exactlyEqual (geo.scrollX(), 0.0) && shown >= 8,
           fmt ("the first view centres C4: scroll %.0f (want %.0f), %d of the 26 notes in view", geo.scrollY(), want, shown));

    // the playhead is the display slot's, not the placeholder's: none before Cubase plays
    check (! roll.getPlayhead().has_value(), "no playhead before Cubase plays");
}

SL_TEST ("ui", rollViewPianoPlaysKeys)
{
    section ("Roll view: a piano key plays while held (auditionOn at 0.8, auditionOff on release) and lights up");

    StrandLockProcessor proc;
    ui::Theme theme (ThemeId::dark);
    RollView view (theme, proc);
    view.setBounds (0, 0, 1100, 680);
    drain (proc);

    auto& piano = view.getPiano();
    const auto& geo = view.getGeometry();
    const float yC3 = (float) (geo.cy (60.0) - geo.scrollY());
    check (piano.pitchAt (yC3) == 60 && piano.pitchAt (yC3 - (float) geo.rowHeight()) == 61 && piano.pitchAt (yC3 + (float) geo.rowHeight()) == 59,
           fmt ("the keys follow the rows: y %.1f is C3, a row up C#3, a row down B2", (double) yC3));

    piano.mouseDown (mouse (piano, { 20.0f, yC3 }));
    const auto down = drain (proc);
    const bool held = piano.isHeld (60) && piano.getPressed() == 60;
    const auto picture = piano.createComponentSnapshot (piano.getLocalBounds(), true, 1.0f, juce::SoftwareImageType());
    const auto lit = picture.getPixelAt (30, juce::roundToInt (yC3));
    piano.mouseUp (mouse (piano, { 20.0f, yC3 }, juce::ModifierKeys()));
    const auto up = drain (proc);
    check (down.size() == 1 && down[0].kind == doc::UiCommandKind::auditionOn && down[0].note == 60 && near (down[0].velocity, 0.8, 1e-12),
           fmt ("pressing C3 sends one auditionOn of note 60 at 0.8 through the CommandQueue (%d commands)", (int) down.size()));
    check (held && lit == theme (Tok::pianoHeld),
           fmt ("while held the key is drawn in the held colour (#%s)", lit.toDisplayString (false).toRawUTF8()));
    check (up.size() == 1 && up[0].kind == doc::UiCommandKind::auditionOff && up[0].note == 60 && piano.getPressed() == -1 && ! piano.isHeld (60),
           fmt ("letting go sends one auditionOff of note 60 (%d commands)", (int) up.size()));

    // a key played live in Cubase (the display frame's live voices) lights up too
    auto frame = std::make_unique<engine::DisplayFrame>();
    frame->numVoices = 2;
    frame->voices[0].noteIdHash = core::idHash (u"live:64");
    frame->voices[0].p = 64;
    frame->voices[0].kind = (std::uint8_t) engine::VoiceKind::live;
    frame->voices[1].p = 67;
    frame->voices[1].kind = (std::uint8_t) engine::VoiceKind::clip;
    view.setDisplayFrame (frame.get());
    check (piano.isHeld (64) && ! piano.isHeld (67), "a live key (E3) lights up; a clip note (G3) does not");
    frame->voices[0].released = true;
    view.setDisplayFrame (frame.get());
    check (! piano.isHeld (64), "released, it goes dark again");
}

SL_TEST ("ui", rollViewZoomAndScroll)
{
    section ("Roll view: H- H+ V- V+, Ctrl + wheel, the wheel and the scroll bars move the view; the project keeps it");

    StrandLockProcessor proc;
    ui::Theme theme (ThemeId::dark);
    RollView view (theme, proc);
    view.setBounds (0, 0, 1100, 680);
    const auto& geo = view.getGeometry();

    view.getZoomButton (1).onClick();   // H+
    const double h1 = geo.beatWidth();
    view.getZoomButton (3).onClick();   // V+
    const double v1 = geo.rowHeight();
    auto ui = proc.getUiState();
    check (near (h1, 64.0 * 1.3, 1e-9) && juce::exactlyEqual (v1, 18.0) && near (ui.zoomH, 1.3, 1e-6) && near (ui.zoomV, 18.0 / 14.0, 1e-6),
           fmt ("H+ makes beats 1.3 times wider (%.2f px), V+ rows 1.25 times taller, rounded (%.0f px); UIST zoom %.4f, %.4f",
                h1, v1, (double) ui.zoomH, (double) ui.zoomV));
    view.getZoomButton (0).onClick();   // H-
    view.getZoomButton (2).onClick();   // V-
    check (near (geo.beatWidth(), 64.0, 1e-9) && juce::exactlyEqual (geo.rowHeight(), 14.0)
               && juce::exactlyEqual (geo.scrollX(), std::round (geo.scrollX())) && juce::exactlyEqual (geo.scrollY(), std::round (geo.scrollY())),
           fmt ("H- and V- bring them back (%.2f, %.0f px); the scroll stays on whole pixels (%.1f, %.1f)", geo.beatWidth(), geo.rowHeight(),
                geo.scrollX(), geo.scrollY()));

    // the wheel: a notch scrolls about 100 px; with Shift sideways; with Ctrl it zooms in time around the pointer
    // (beats twice as wide first: the 4 bars of the demo clip are only 34 px wider than the view)
    view.zoomBy (2.0, 1.0);
    auto& canvas = view.getCanvas();
    const double y0 = geo.scrollY();
    spin (view, canvas, { 300.0f, 100.0f }, -kNotch, juce::ModifierKeys());
    const double down = geo.scrollY() - y0;
    spin (view, canvas, { 300.0f, 100.0f }, kNotch, juce::ModifierKeys());
    check (near (down, 100.0, 1.0) && juce::exactlyEqual (geo.scrollY(), y0),
           fmt ("a notch down scrolls %.0f px down, a notch up back (%.0f)", down, geo.scrollY()));
    view.setScroll (200.0, geo.scrollY());
    spin (view, canvas, { 300.0f, 100.0f }, -kNotch, juce::ModifierKeys::shiftModifier);
    check (near (geo.scrollX(), 300.0, 1.0), fmt ("Shift + a notch scrolls sideways: x 200 -> %.0f", geo.scrollX()));
    const double beatUnder = (300.0 + geo.scrollX()) / geo.beatWidth();
    spin (view, canvas, { 300.0f, 100.0f }, kNotch, juce::ModifierKeys::ctrlModifier);
    const double after = (300.0 + geo.scrollX()) / geo.beatWidth();
    check (near (geo.beatWidth(), 128.0 * 1.15, 1e-9) && near (after, beatUnder, 1.0 / geo.beatWidth()),
           fmt ("Ctrl + a notch up zooms in time 1.15 times (%.2f px a beat); the beat under the pointer stays (%.4f -> %.4f)",
                geo.beatWidth(), beatUnder, after));
    const double bw = geo.beatWidth(), yPiano = geo.scrollY();
    spin (view, view.getPiano(), { 20.0f, 100.0f }, -kNotch, juce::ModifierKeys::ctrlModifier);
    check (juce::exactlyEqual (geo.beatWidth(), bw) && near (geo.scrollY() - yPiano, 100.0, 1.0),
           fmt ("over the keys the wheel only scrolls up and down (%.0f px), even with Ctrl", geo.scrollY() - yPiano));

    // the scroll bars
    auto& vbar = view.getScrollBar (true);
    vbar.setCurrentRangeStart (140.0, juce::sendNotificationSync);
    auto& hbar = view.getScrollBar (false);
    hbar.setCurrentRangeStart (64.0, juce::sendNotificationSync);
    ui = proc.getUiState();
    check (juce::exactlyEqual (geo.scrollY(), 140.0) && juce::exactlyEqual (geo.scrollX(), 64.0)
               && near (ui.scrollRow, 140.0 / geo.rowHeight(), 1e-5) && near (ui.scrollBeat, 64.0 / geo.beatWidth(), 1e-12),
           fmt ("the scroll bars move the view to (%.0f, %.0f); UIST keeps beat %.4f, row %.4f", geo.scrollX(), geo.scrollY(),
                ui.scrollBeat, (double) ui.scrollRow));
    check (juce::exactlyEqual (vbar.getMaximumRangeLimit(), geo.contentHeight()) && juce::exactlyEqual (vbar.getCurrentRangeSize(), geo.viewHeight()),
           fmt ("the vertical bar spans the roll (%.0f px) and shows the view (%.0f px)", vbar.getMaximumRangeLimit(), vbar.getCurrentRangeSize()));
}

SL_TEST ("ui", rollViewFirstViewAndProject)
{
    section ("Roll view: the first view centres on the notes; a saved view comes back with the project");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    double scrollY = 0.0;
    {
        StrandLockEditor editor (proc);
        auto& roll = editor.getView().getRoll();
        roll.zoomBy (1.3, 1.25);
        scrollY = roll.getGeometry().scrollY();
    }
    const auto ui = proc.getUiState();
    {
        StrandLockEditor again (proc);
        const auto& geo = again.getView().getRoll().getGeometry();
        check (near (geo.beatWidth(), 64.0 * 1.3, 1e-4) && juce::exactlyEqual (geo.rowHeight(), 18.0) && near (geo.scrollY(), scrollY, 1.0),
               fmt ("closing and opening the window keeps the zoom (%.2f, %.0f px) and the scroll (%.0f, was %.0f; UIST row %.3f)",
                    geo.beatWidth(), geo.rowHeight(), geo.scrollY(), scrollY, (double) ui.scrollRow));
    }

    // a project that saved another view
    StrandLockProcessor other;
    auto saved = other.getUiState();
    saved.zoomH = 2.0f;
    saved.zoomV = 1.25f;
    saved.scrollBeat = 4.0;
    saved.scrollRow = 30.0f;
    other.setUiState (saved);
    {
        StrandLockEditor editor (other);
        const auto& geo = editor.getView().getRoll().getGeometry();
        check (juce::exactlyEqual (geo.beatWidth(), 128.0) && juce::exactlyEqual (geo.rowHeight(), 18.0)
                   && juce::exactlyEqual (geo.scrollX(), 512.0) && juce::exactlyEqual (geo.scrollY(), 540.0),
               fmt ("UIST zoom 2 x 1.25, beat 4, row 30: %.0f px beats, %.0f px rows, scroll (%.0f, %.0f)", geo.beatWidth(), geo.rowHeight(),
                    geo.scrollX(), geo.scrollY()));
    }
    resetUserSettings (proc);
}

SL_TEST ("ui", rollViewPlayheadFromDisplayFrames)
{
    section ("Roll view: the playhead is the display slot's (#FF7A00 while Cubase plays, none while stopped)");

    StrandLockProcessor proc;
    ui::Theme theme (ThemeId::dark);
    RollView view (theme, proc);
    view.setBounds (0, 0, 1100, 680);
    view.setScroll (0.0, view.getGeometry().scrollY());
    view.takeRepaintLog();

    auto frame = std::make_unique<engine::DisplayFrame>();
    frame->playing = true;
    frame->beat = 5.5;
    view.setDisplayFrame (frame.get());
    const auto log = view.takeRepaintLog();
    const int x = view.getCanvasArea().getX() + (int) std::floor (5.5 * view.getGeometry().beatWidth());
    check (view.getPlayhead().has_value() && juce::exactlyEqual (*view.getPlayhead(), 5.5) && log.containsPoint ({ x, view.getCanvasArea().getCentreY() })
               && log.getBounds().getWidth() <= 6,
           fmt ("playing at beat 5.5: the playhead is there and only a %d px strip at x %d is repainted", log.getBounds().getWidth(), x));

    auto picture = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f, juce::SoftwareImageType());
    const auto at = picture.getPixelAt (x, view.getCanvasArea().getCentreY());
    check (at == theme (Tok::playhead) && theme (Tok::playhead) == juce::Colour (0xffff7a00),
           fmt ("drawn in #FF7A00 (#%s)", at.toDisplayString (false).toRawUTF8()));

    frame->playing = false;
    view.setDisplayFrame (frame.get());
    check (! view.getPlayhead().has_value(), "stopped: no playhead");
}

SL_TEST ("ui", rollViewModesReachTheDocument)
{
    section ("Roll view: the mode buttons set the document's mode and the Sample sub-lane");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    StrandLockEditor editor (proc);
    auto& view = editor.getView();
    auto& doc = proc.getDocument();

    view.getModeButton (5).onClick();
    const auto tone = doc.rollState();
    view.setMode (8, 2);
    const auto grain = doc.rollState();
    view.setMode (1);
    const auto build = doc.rollState();
    check (tone.mode == 5 && tone.activeLane() == core::LaneId::tone && grain.mode == 8 && grain.subLane == core::LaneId::grain
               && build.mode == 1 && ! build.activeLane().has_value(),
           fmt ("Tone: mode %d; Sample with Grain: mode %d, sub-lane %s; Build: mode %d", tone.mode, grain.mode,
                core::laneInfo (grain.subLane).code, build.mode));
    resetUserSettings (proc);
}

SL_TEST ("ui", rollViewRepaintsWhatChanged)
{
    section ("Roll view: a refresh repaints only what changed; the view, the zoom or the theme repaint everything");

    StrandLockProcessor proc;
    ui::Theme theme (ThemeId::dark);
    RollView view (theme, proc);
    view.setBounds (0, 0, 1100, 680);
    const juce::SoftwareImageType software;
    (void) view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f, software);
    view.takeRepaintLog();
    const double canvas = (double) view.getCanvasArea().getWidth() * (double) view.getCanvasArea().getHeight();
    const auto areaOf = [] (const juce::RectangleList<int>& l)
    {
        double a = 0.0;
        for (const auto& r : l)
            a += (double) r.getWidth() * (double) r.getHeight();
        return a;
    };

    // nothing changed: nothing repainted
    view.refresh();
    check (view.takeRepaintLog().isEmpty(), "a refresh with nothing changed repaints nothing");

    // selecting a note: its bounds (it is drawn selected now) and nothing far from it
    proc.getDocument().setSelection ({ "sd-3" }, std::string ("sd-3"));
    view.refresh();
    const auto selected = view.takeRepaintLog();
    const auto change = view.getLastChange();
    (void) view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f, software);
    const auto stats = view.getCache().lastRender();
    check (! change.all && change.notes == 1 && ! selected.isEmpty() && areaOf (selected) < 0.2 * canvas && ! stats.full && stats.notesDrawn >= 1,
           fmt ("selecting a note repaints %.1f %% of the roll; the note layer draws %d notes again in %d rectangles, not all",
                100.0 * areaOf (selected) / canvas, stats.notesDrawn, stats.rects));

    // the zoom: everything
    view.zoomBy (1.3, 1.0);
    const auto zoomed = view.takeRepaintLog();
    (void) view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f, software);
    check (zoomed.containsRectangle (view.getCanvasArea()) && view.getCache().lastRender().full,
           "H+ repaints the whole roll and draws every note again");

    // the theme: every note again in the new colours
    theme.set (ThemeId::light);
    const auto light = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f, software);
    check (view.getCache().lastRender().full && light.getPixelAt (view.getCanvasArea().getRight() - 2, view.getCanvasArea().getBottom() - 2) != juce::Colour (0xff0f1012),
           "a theme switch draws every note again (the cache's key holds the theme)");
}
