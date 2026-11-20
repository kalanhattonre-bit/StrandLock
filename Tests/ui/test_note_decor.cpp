// The roll's note decorations (PLUGIN-PLAN 4 M6, 7.8 T2 decision 5; sl-ui-panels.js decorate), drawn through M4's
// painter and note-layer cache:
//
// - in every mode, with the prototype's alphas: the chevrons' strength and the thumbnail's alpha by mode and selection,
//   and in pixels an unselected note's chevrons in Sample mode against Volume mode (0.8 against 0.5);
// - the chevrons follow the note's Motion and speed: forwards, backwards, two bars where it stands still (Freeze), a
//   square where it falls silent (At the end: Stop), a second chevron from 2.5x up, Mirror's chevrons with 2 heads,
//   Scatter's jumps; a thumbnail above the line from the sound's peaks;
// - the own-settings dot on exactly the notes with settings of their own; the warning where a slide goes into a note
//   that starts well inside the one before it;
// - a setting the decorations read (the Motion) draws every note again, and a refresh with nothing changed makes no new
//   simulation and draws no note again;
// and pictures of Sample mode (the chips, the chevrons, the thumbnails) and of Build.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Travel.h"
#include "model/Clip.h"
#include "model/Node.h"
#include "model/NoteSettings.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/NoteDecor.h"
#include "ui/roll/RollPainter.h"
#include "ui/roll/RollView.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::ThemeId;
    using ui::roll::RollPainter;
    using ui::roll::RollView;
    using params::P;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    std::shared_ptr<const model::Note> noteOf (model::Note n) { return std::make_shared<const model::Note> (std::move (n)); }

    /** One note for every kind of decoration, two beats apart (128 px wide at the first zoom). */
    model::Clip decorClip()
    {
        // fw and bw start half way into the sound, so either way they stay inside it (At the end: Stop)
        model::Clip c = model::newClip (8.0);
        {
            auto n = model::mkNote (60.0, 0.0, 2.0, "fw");
            n.sample.start.pos = 0.5;
            c.notes.push_back (noteOf (std::move (n)));
        }
        {
            auto n = model::mkNote (62.0, 2.0, 2.0, "bw");
            n.sample.motion = core::Motion::backward;
            n.sample.start.pos = 0.5;
            c.notes.push_back (noteOf (std::move (n)));
        }
        {
            auto n = model::mkNote (64.0, 4.0, 2.0, "fz");
            n.sample.motion = core::Motion::freeze;
            c.notes.push_back (noteOf (std::move (n)));
        }
        {
            auto n = model::mkNote (65.0, 6.0, 2.0, "mr");
            n.sample.motion = core::Motion::mirror;
            c.notes.push_back (noteOf (std::move (n)));
        }
        {
            auto n = model::mkNote (67.0, 8.0, 2.0, "sc");
            n.sample.motion = core::Motion::scatter;
            n.sample.jump = core::RateId::r1_16;
            c.notes.push_back (noteOf (std::move (n)));
        }
        {
            auto n = model::mkNote (69.0, 10.0, 2.0, "fast");
            n.lane (core::LaneId::travel) = model::flat (core::travelSpeedToV (3.0));   // 3x forwards
            c.notes.push_back (noteOf (std::move (n)));
        }
        c.notes.push_back (noteOf (model::mkNote (57.0, 12.0, 16.0, "long")));   // 8 s at 120 bpm: past the 6 s sound
        {
            auto a = model::mkNote (72.0, 14.0, 2.0, "a");
            a.glideTo = "b";   // b starts half a beat into a: the engine cuts a short there
            c.notes.push_back (noteOf (std::move (a)));
            c.notes.push_back (noteOf (model::mkNote (74.0, 14.5, 1.0, "b")));
            auto d = model::mkNote (70.0, 20.0, 1.0, "c");
            d.glideTo = "d";   // d starts after c ends: no warning
            c.notes.push_back (noteOf (std::move (d)));
            c.notes.push_back (noteOf (model::mkNote (71.0, 21.5, 1.0, "d")));
        }
        return c;
    }

    /** A processor holding the clip and a 1100 x 680 roll, with Heads 2 and At the end: Stop. */
    struct DecorRig
    {
        StrandLockProcessor proc;
        ui::Theme theme { ThemeId::dark };
        std::unique_ptr<RollView> view;

        DecorRig()
        {
            proc.waitForSounds();
            params::setPlainValue (proc.getParam (P::heads), params::index (P::heads), 2.0f);
            params::setPlainValue (proc.getParam (P::atEnd), params::index (P::atEnd), (float) core::AtEnd::stop);
            proc.getDocument().replace (std::make_shared<const model::Clip> (decorClip()));
            view = std::make_unique<RollView> (theme, proc);
            view->setBounds (0, 0, 1100, 680);
            view->refresh();
        }

        model::NotePtr note (const std::string& id)
        {
            for (const auto& n : proc.getDocument().current()->notes)
                if (n->id == id)
                    return n;
            return nullptr;
        }

        RollPainter::Decorations decor (const std::string& id, bool selected = false)
        {
            return RollPainter (view->getPaintState()).decorations (*note (id), selected);
        }
    };

    /** The alpha the chevrons put under a note's line: the note alone painted on a clear image (in the given mode), the
        alpha channel summed over the band under its line, where only the decorations draw. */
    double bandAlpha (DecorRig& rig, const std::string& id, int mode, bool selected)
    {
        auto st = rig.view->getPaintState();
        st.roll.mode = mode;
        const auto n = rig.note (id);
        const auto& geo = *st.geo;
        const double x0 = n->s * geo.beatWidth(), x1 = (n->s + n->l) * geo.beatWidth(), base = geo.baseYAt (*n, 0.0);
        juce::Image image (juce::Image::ARGB, (int) (x1 - x0) + 40, 80, true, juce::SoftwareImageType());
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::translation ((float) (20.0 - x0), (float) (40.0 - base)));
            RollPainter (st).paintNote (g, *n, selected);
        }
        double sum = 0.0;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = 40 + 3; y <= 40 + 10; ++y)
            for (int x = 20 + 12; x < image.getWidth() - 20 - 12; ++x)   // clear of the start and end dots
                sum += pixels.getPixelColour (x, y).getFloatAlpha();
        return sum;
    }
}

//==================================================================================================
SL_TEST ("ui", noteDecorAlphasByMode)
{
    section ("Note decorations: drawn in every mode with the prototype's alphas (decision 5)");

    DecorRig rig;
    // sl-ui-panels.js decorate: thumbnails (mode 8 or 1 ? .42 / .24 : .22 / .11), chevrons (mode 8 ? 1 / .8 : .8 / .5)
    int right = 0, total = 0;
    for (int mode = 1; mode <= 8; ++mode)
    {
        rig.view->setMode (mode, 0);
        rig.view->refresh();
        for (const bool sel : { false, true })
        {
            ++total;
            const auto d = rig.decor ("fw", sel);
            const float thumb = mode == 8 || mode == 1 ? (sel ? 0.42f : 0.24f) : (sel ? 0.22f : 0.11f);
            const float strong = mode == 8 ? (sel ? 1.0f : 0.8f) : (sel ? 0.8f : 0.5f);
            right += std::abs (d.thumbAlpha - thumb) < 1.0e-6f && std::abs (d.strength - strong) < 1.0e-6f && ! d.chevrons.empty() ? 1 : 0;
        }
    }
    check (right == total, fmt ("%d of %d: in modes 1 to 8, selected and not, the thumbnail's alpha and the chevrons' strength are the "
                                "prototype's, and the chevrons are drawn", right, total));

    // in pixels: the chevrons of an unselected note in Sample mode (0.8) against Volume mode (0.5); a selected one in
    // Volume mode (0.8) as strong as an unselected one in Sample mode
    rig.view->setMode (8, 0);
    rig.view->refresh();
    const double sample = bandAlpha (rig, "fw", 8, false), volume = bandAlpha (rig, "fw", 3, false), volumeSel = bandAlpha (rig, "fw", 3, true);
    const double ratio = volume > 0.0 ? sample / volume : 0.0;
    check (volume > 1.0 && std::abs (ratio - 1.6) < 0.04 && std::abs (volumeSel / sample - 1.0) < 0.02,
           fmt ("under the line the chevrons cover %.1f (Sample mode) and %.1f (Volume) alpha: %.3f times (0.8 / 0.5 = 1.6); selected in Volume "
                "%.1f", sample, volume, ratio, volumeSel));
}

SL_TEST ("ui", noteDecorFollowTheNotes)
{
    section ("Note decorations: the chevrons follow each note's Motion and speed; the thumbnail, the own-settings dot and the slide warning");

    DecorRig rig;
    rig.view->setMode (8, 0);
    rig.view->refresh();
    const double bw = rig.view->getGeometry().beatWidth();

    const auto fw = rig.decor ("fw"), bw2 = rig.decor ("bw"), fz = rig.decor ("fz"), mr = rig.decor ("mr"), sc = rig.decor ("sc"),
               fast = rig.decor ("fast"), longNote = rig.decor ("long");
    const auto allDir = [] (const RollPainter::Decorations& d, int dir)
    {
        if (d.chevrons.empty())
            return false;
        for (const auto& c : d.chevrons)
            if (c.dir != dir)
                return false;
        return true;
    };
    bool spacing = fw.chevrons.size() >= 3;
    for (std::size_t i = 1; i < fw.chevrons.size(); ++i)
        spacing = spacing && std::abs (fw.chevrons[i].x - fw.chevrons[i - 1].x - 14.0) < 1.0e-9;   // 14 / sqrt (1x)
    check (allDir (fw, 1) && fw.stops.empty() && fw.mirrors.empty() && fw.jumps.empty() && spacing,
           fmt ("Forward: %d chevrons pointing right, 14 px apart at 1x, nothing else", (int) fw.chevrons.size()));
    check (allDir (bw2, -1) && bw2.stops.empty(), fmt ("Backward: %d chevrons pointing left", (int) bw2.chevrons.size()));
    check (fz.chevrons.empty() && fz.stops.size() >= 4 && fz.stops.size() % 2 == 0,
           fmt ("Freeze: no chevrons, %d bars in pairs (standing still)", (int) fz.stops.size()));
    bool mirrored = ! mr.mirrors.empty() && mr.mirrors.size() == mr.chevrons.size();
    for (std::size_t i = 0; mirrored && i < mr.mirrors.size(); ++i)
        mirrored = mr.mirrors[i].dir == -mr.chevrons[i].dir && std::abs (mr.mirrors[i].y - mr.chevrons[i].y - 6.0) < 1.0e-9
                && std::abs (mr.mirrors[i].scale - 0.75) < 1.0e-12;
    check (mirrored, fmt ("Mirror with 2 heads: under each of the %d chevrons a smaller one pointing the other way", (int) mr.chevrons.size()));
    check (! sc.jumps.empty(), fmt ("Scatter at 1/16: %d jump marks", (int) sc.jumps.size()));
    int pairs = 0;
    for (std::size_t i = 1; i < fast.chevrons.size(); ++i)
        pairs += std::abs (fast.chevrons[i].x - fast.chevrons[i - 1].x - 3.6) < 1.0e-9 ? 1 : 0;
    check (allDir (fast, 1) && pairs >= 3, fmt ("3x forwards: chevrons in pairs 3.6 px apart (%d pairs of %d chevrons)", pairs, (int) fast.chevrons.size()));
    {
        // At the end: Stop, a 16-beat note (8 s) on the 6 s sound: chevrons, then a square where it falls silent
        const bool silent = ! longNote.stops.empty() && std::abs (longNote.stops.back().getWidth() - 5.0f) < 1.0e-4f && ! longNote.chevrons.empty()
                         && longNote.chevrons.back().x < (double) longNote.stops.back().getX();
        const double at = longNote.stops.empty() ? 0.0 : ((double) longNote.stops.back().getCentreX() - 12.0 * bw) / (16.0 * bw);
        check (silent && at > 0.6 && at < 0.9, fmt ("At the end: Stop: chevrons, then the square where the note falls silent, %.0f %% into it", 100.0 * at));
    }
    {
        // the thumbnail: columns above the line from the sound's peaks (the starter sound is loaded)
        const auto& geo = rig.view->getGeometry();
        const auto n = rig.note ("fw");
        bool above = ! fw.columns.empty();
        for (const auto& r : fw.columns)
            above = above && (double) r.getBottom() <= geo.baseYAt (*n, 0.0) - 3.0 + 1.0e-6;
        check (above && fw.columns.size() <= (std::size_t) ui::roll::simPoints (2.0 * bw),
               fmt ("the thumbnail: %d columns, all above the line", (int) fw.columns.size()));
    }

    // the own-settings dot on exactly the notes with settings of their own
    int dots = 0, own = 0, agree = 0;
    for (const auto& n : rig.proc.getDocument().current()->notes)
    {
        const auto d = RollPainter (rig.view->getPaintState()).decorations (*n, false);
        dots += d.ownDot ? 1 : 0;
        own += ui::roll::hasOwnSettings (*n) ? 1 : 0;
        agree += d.ownDot.has_value() == ui::roll::hasOwnSettings (*n) ? 1 : 0;
    }
    check (agree == (int) rig.proc.getDocument().current()->notes.size() && dots == 4 && own == 4,
           fmt ("the dot on the %d notes with settings of their own and no other (%d dots)", own, dots));

    // the slide warning
    const auto a = rig.decor ("a"), c = rig.decor ("c");
    check (a.warning.has_value() && std::abs (a.warning->x - 14.5 * bw) < 1.0e-9 && ! c.warning.has_value(),
           "a slide into a note that starts half a beat in: the warning where that note starts; into a note after the end: none");
}

SL_TEST ("ui", noteDecorRedrawWhenTheyChange)
{
    section ("Note decorations: a setting they read draws every note again; nothing changed, nothing made or drawn again");

    DecorRig rig;
    auto& view = *rig.view;
    view.setMode (8, 0);
    view.refresh();
    view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);   // every note drawn, every simulation made
    const int made = view.getDecor().simulationsMade();
    view.takeRepaintLog();

    // nothing changed: no simulation, no note drawn again
    view.refresh();
    view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    const bool quiet = view.getDecor().simulationsMade() == made && view.getCache().lastRender().notesDrawn == 0;
    check (quiet && made > 0, fmt ("a refresh with nothing changed: %d simulations made before, none since, %d notes drawn again", made,
                                   view.getCache().lastRender().notesDrawn));

    // the Sample tab's Motion to Backward: every note following it turns round, and every note is drawn again
    params::setPlainValue (rig.proc.getParam (P::motion), params::index (P::motion), (float) core::Motion::backward);
    view.refresh();
    const auto asked = view.takeRepaintLog();
    view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
    const auto fw = rig.decor ("fw");
    bool turned = ! fw.chevrons.empty();
    for (const auto& c : fw.chevrons)
        turned = turned && c.dir == -1;
    check (turned && asked.containsRectangle (view.getCanvasArea()) && view.getCache().lastRender().full,
           "Motion Backward: the note that follows the Sample tab now points left, the whole roll is repainted and every note drawn again");
}

//==================================================================================================
// Pictures for review: Sample mode with the chips, the chevrons and the thumbnails (dark and light, 100 % and 200 %),
// and Build with them (the prototype's fresh1100.png shows both there).

namespace
{
    juce::Image shootDecor (ThemeId theme, int scale, int mode)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();
        auto& settings = proc.getUserSettings();
        settings.setTheme (theme);
        settings.setUiScalePercent (scale);
        settings.setKeysDefault (false);
        juce::Image image;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (1100, 680);
            auto& view = editor.getView();
            view.setFolded (true);
            // a few of the demo's notes with their own Motion, two of them selected
            const auto clip = proc.getDocument().current();
            const auto ordered = model::timeOrder (clip->notes);
            model::SamplePatch b, f;
            b.motion.emplace (core::Motion::backward);
            f.motion.emplace (core::Motion::freeze);
            auto edited = model::setNoteSample (*clip, std::vector<std::string> { ordered[3]->id, ordered[8]->id }, b);
            edited = model::setNoteSample (edited, std::vector<std::string> { ordered[12]->id }, f);
            proc.editClip ("Motions", edited);
            proc.getDocument().setSelection ({ ordered[3]->id, ordered[4]->id }, ordered[3]->id);
            view.setMode (mode, 0);
            view.getRoll().edited();
            editor.refreshNow();
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        }
        resetUserSettings (proc);
        return image;
    }
}

SL_SNAPSHOT (m6_mode8_dark)       { return shootDecor (ThemeId::dark, 100, 8); }
SL_SNAPSHOT (m6_mode8_light)      { return shootDecor (ThemeId::light, 100, 8); }
SL_SNAPSHOT (m6_mode8_dark_200)   { return shootDecor (ThemeId::dark, 200, 8); }
SL_SNAPSHOT (m6_mode8_light_200)  { return shootDecor (ThemeId::light, 200, 8); }
SL_SNAPSHOT (m6_build_decor_dark) { return shootDecor (ThemeId::dark, 100, 1); }
