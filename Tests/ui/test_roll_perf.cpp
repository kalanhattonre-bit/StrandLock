// The roll's drawing cost (PLUGIN-PLAN 4 M4 "drawing cost", 7.6 R2): a full repaint of a 512-note clip at
// 1100 x 680 (target 50 ms, printed; gate 250 ms by the plan's timing rule), and moving one note, which repaints at
// most 3 notes' bounds (gated): the note layer draws again only the moved note where it was and where it is now,
// and the note that slides into it; the roll asks to repaint only those and the slide link between them. A
// partial update must give the very picture a full repaint gives: the test paints only the area the roll asked
// for over the picture before the move, as a window would, and compares it pixel for pixel with a full repaint.
//
// The pictures are drawn with JUCE's software renderer (the same on Windows and Linux); the Windows renderer
// (Direct2D) is timed too, and its partial update compared, as figures.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "model/Clip.h"
#include "model/Generators.h"
#include "model/Node.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/Theme.h"
#include "ui/roll/RollView.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::roll::RollView;

    constexpr int kRows = 32, kSteps = 16, kLowPitch = 44;

    std::string noteId (int row, int step)
    {
        return "n" + std::to_string (row) + "-" + std::to_string (step);
    }

    /** 512 notes, all in view in a 1100 x 680 roll: 32 rows (MIDI 44 to 75) by 16 steps a beat apart, each note
        0.75 beat long (so no automatic slides: the gaps are wider than legato's 0.13 beat), with a swell on Volume,
        a rise on Tone and, on every fourth note, a pattern on Formant. The note at row 10, step 7 slides into the
        next one (an explicit link). */
    model::Clip denseClip()
    {
        model::Clip c = model::newClip (4.0);
        for (int r = 0; r < kRows; ++r)
            for (int k = 0; k < kSteps; ++k)
            {
                auto n = model::mkNote ((double) (kLowPitch + r), (double) k, 0.75, noteId (r, k));
                n.c[(std::size_t) core::LaneId::amp] = { model::mkNode (0.0, 0.35), model::mkNode (0.5, 0.95), model::mkNode (1.0, 0.6) };
                n.c[(std::size_t) core::LaneId::tone] = { model::mkNode (0.0, 0.3), model::mkNode (1.0, 0.9) };
                if ((r + k) % 4 == 0)
                    n.gen[(std::size_t) core::LaneId::formant] = model::makeGen (model::GenKind::pattern);
                if (r == 10 && k == 7)
                    n.glideTo = noteId (10, 8);
                c.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
            }
        return c;
    }

    juce::Image shot (RollView& view, const juce::ImageType& type)
    {
        return view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f, type);
    }

    double millisToShoot (RollView& view, const juce::ImageType& type, bool invalidate)
    {
        if (invalidate)
            view.invalidateAll();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const auto image = shot (view, type);
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        return image.isValid() ? t1 - t0 : 1.0e9;
    }

    double median (std::vector<double> v)
    {
        std::sort (v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    }

    struct PixelDiff
    {
        int differing = 0, largest = 0;
        int insideAsked = 0, atAskedEdge = 0;   // of the differing pixels: inside the area asked for, within 2 px of its edge
        juce::Rectangle<int> where;             // around the differing pixels
        juce::String samples;                   // a few of them: x,y a -> b
    };

    PixelDiff compare (const juce::Image& a, const juce::Image& b, const juce::RectangleList<int>* asked = nullptr)
    {
        PixelDiff d;
        if (a.getBounds() != b.getBounds())
        {
            d.differing = -1;
            return d;
        }
        const juce::Image::BitmapData pa (a, juce::Image::BitmapData::readOnly), pb (b, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto ca = pa.getPixelColour (x, y), cb = pb.getPixelColour (x, y);
                const int m = std::max ({ std::abs ((int) ca.getRed() - (int) cb.getRed()), std::abs ((int) ca.getGreen() - (int) cb.getGreen()),
                                          std::abs ((int) ca.getBlue() - (int) cb.getBlue()), std::abs ((int) ca.getAlpha() - (int) cb.getAlpha()) });
                if (m > 0)
                {
                    ++d.differing;
                    const juce::Rectangle<int> px (x, y, 1, 1);
                    d.where = d.where.isEmpty() ? px : d.where.getUnion (px);
                    if (d.differing <= 6)
                        d.samples << x << "," << y << " #" << ca.toDisplayString (true) << "->#" << cb.toDisplayString (true) << "; ";
                    if (asked != nullptr)
                    {
                        d.insideAsked += asked->containsPoint (juce::Point<int> (x, y)) ? 1 : 0;
                        bool edge = false;
                        for (const auto& r : *asked)
                            edge = edge || (r.expanded (2).contains (x, y) && ! r.reduced (2).contains (x, y));
                        d.atAskedEdge += edge ? 1 : 0;
                    }
                }
                d.largest = std::max (d.largest, m);
            }
        return d;
    }

    double areaOf (const juce::RectangleList<int>& list)
    {
        double a = 0.0;
        for (const auto& r : list)
            a += (double) r.getWidth() * (double) r.getHeight();
        return a;
    }

    /** A processor holding the dense clip and a 1100 x 680 roll showing all of it, Build mode, with the note at
        row 10, step 8 selected (it is the one that moves). */
    struct Rig
    {
        StrandLockProcessor proc;
        ui::Theme theme { ui::ThemeId::dark };
        std::unique_ptr<RollView> view;

        Rig()
        {
            proc.getDocument().replace (std::make_shared<const model::Clip> (denseClip()));
            proc.getDocument().setSelection ({ noteId (10, 8) }, noteId (10, 8));
            view = std::make_unique<RollView> (theme, proc);
            view->setBounds (0, 0, 1100, 680);
            // rows 78 down: every note and its lane band in view
            view->setScroll (0.0, (double) (ui::roll::kMaxPitch - 78) * view->getGeometry().rowHeight());
            view->refresh();
        }
    };
}

//==================================================================================================
SL_TEST ("ui", rollFullRepaintCost)
{
    section ("Roll drawing cost: a full repaint of a 512-note clip at 1100 x 680 (target 50 ms, printed; gate 250 ms)");

    Rig rig;
    auto& view = *rig.view;
    const auto& geo = view.getGeometry();
    const juce::SoftwareImageType software;

    // every note in view
    const auto visible = juce::Rectangle<double> (geo.scrollX(), geo.scrollY(), geo.viewWidth(), geo.viewHeight());
    const auto shown = ui::roll::RollPainter (view.getPaintState()).visibleNotes (visible);
    check (shown.size() == 512 && geo.viewWidth() >= 1040.0 && geo.viewHeight() >= 640.0,
           fmt ("all 512 notes are in view (%d) in a %.0f x %.0f roll", (int) shown.size(), geo.viewWidth(), geo.viewHeight()));

    // cold: the notes' lines and lane tables made as they are drawn; then warm: the cache emptied, every note drawn again
    const double cold = millisToShoot (view, software, false);
    const int coldNotes = view.getCache().lastRender().notesDrawn;
    std::vector<double> warm;
    for (int i = 0; i < 5; ++i)
        warm.push_back (millisToShoot (view, software, true));
    const int warmNotes = view.getCache().lastRender().notesDrawn;
    const double warmMedian = median (warm);
    note (fmt ("software renderer: first paint %.1f ms (%d notes drawn, their lines made), full repaint median %.1f ms of 5 "
               "(min %.1f, max %.1f; %d notes drawn); target 50 ms", cold, coldNotes, warmMedian,
               *std::min_element (warm.begin(), warm.end()), *std::max_element (warm.begin(), warm.end()), warmNotes));
    check (coldNotes == 512 && warmNotes == 512 && view.getCache().lastRender().full,
           fmt ("a full repaint draws all 512 notes (first %d, then %d)", coldNotes, warmNotes));
    check (warmMedian < 250.0, fmt ("full repaint %.1f ms: under the 250 ms gate (5 x the 50 ms target)", warmMedian));
    check (cold < 250.0, fmt ("the first paint, lines made too, %.1f ms: under 250 ms", cold));
    if (warmMedian > 50.0)
        note (fmt ("MISSED TARGET: full repaint %.1f ms against 50 ms (reported in NOTES-M4)", warmMedian));

    // a repaint with nothing changed draws no note again
    const double still = millisToShoot (view, software, false);
    check (view.getCache().lastRender().notesDrawn == 0 && ! view.getCache().lastRender().full,
           fmt ("a repaint with nothing changed draws 0 notes (%d) in %.1f ms: the rows, one image and the handles",
                view.getCache().lastRender().notesDrawn, still));

    // the window's own renderer (Direct2D on Windows), as a figure
    std::vector<double> native;
    for (int i = 0; i < 5; ++i)
        native.push_back (millisToShoot (view, juce::NativeImageType(), true));
    note (fmt ("the native renderer (Direct2D on Windows, software on Linux): full repaint median %.1f ms of 5 (min %.1f)",
               median (native), *std::min_element (native.begin(), native.end())));
}

namespace
{
    struct MoveResult
    {
        ui::roll::NoteLayerCache::Change change;
        ui::roll::NoteLayerCache::Stats partial;
        PixelDiff diff, diffEach, fullTwice, cache;   // cache: the note layer after the update against after a full one
        double repainted = 0.0, canvas = 0.0;
        bool moved = false;
        int undoSteps = 0;
    };

    /** Moves the note at row 10, step 8 two beats later and three rows up with a drag (R1's gestures), then paints
        only what the roll asked to repaint over the picture before the move, and compares it with a full repaint. */
    MoveResult moveOneNote (const juce::ImageType& type)
    {
        Rig rig;
        auto& view = *rig.view;
        const auto before = shot (view, type);   // the cache now holds every note
        view.takeRepaintLog();

        MoveResult m;
        auto& g = view.getGestures();
        const auto& geo = view.getGeometry();
        const auto X = rig.proc.getDocument().current()->notes[(std::size_t) (10 * kSteps + 8)];
        const double x = (X->s + X->l / 2.0) * geo.beatWidth(), y = geo.baseYAt (*X, 0.5);
        const bool couldUndo = rig.proc.canUndo();
        g.pointerDown (x, y, {}, 1000.0);
        g.pointerMove (x + 2.0 * geo.beatWidth(), y - 3.0 * geo.rowHeight(), {});
        g.pointerUp();
        view.refresh();
        m.change = view.getLastChange();
        m.undoSteps = rig.proc.canUndo() && ! couldUndo ? 1 : 0;
        const auto now = rig.proc.getDocument().current();
        for (const auto& n : now->notes)
            if (n->id == noteId (10, 8))
                m.moved = juce::exactlyEqual (n->s, 10.0) && juce::exactlyEqual (n->p, (double) (kLowPitch + 13));

        // a window repaints only what was asked for, over what it showed before
        const auto asked = view.takeRepaintLog();
        m.repainted = areaOf (asked);
        m.canvas = (double) view.getCanvasArea().getWidth() * (double) view.getCanvasArea().getHeight();
        auto partial = before.createCopy();
        {
            juce::Graphics gr (partial);
            gr.reduceClipRegion (asked);
            view.paintEntireComponent (gr, true);
        }
        m.partial = view.getCache().lastRender();
        const auto layerAfterUpdate = view.getCache().getImage().createCopy();
        // the same, one rectangle at a time (a renderer may clip to several rectangles through a mask)
        auto partialEach = before.createCopy();
        for (const auto& r : asked)
        {
            juce::Graphics gr (partialEach);
            gr.reduceClipRegion (r);
            view.paintEntireComponent (gr, true);
        }

        // and a full repaint of the same state
        view.invalidateAll();
        const auto full = shot (view, type);
        m.cache = compare (layerAfterUpdate, view.getCache().getImage().createCopy());
        m.diff = compare (partial, full, &asked);
        m.diffEach = compare (partialEach, full, &asked);
        view.invalidateAll();
        m.fullTwice = compare (full, shot (view, type));
        return m;
    }
}

SL_TEST ("ui", rollMoveRepaintsThreeNotes)
{
    section ("Roll drawing cost: moving one note repaints at most 3 notes' bounds (gated), and the picture equals a full repaint");

    const auto m = moveOneNote (juce::SoftwareImageType());
    check (m.moved && m.undoSteps == 1, fmt ("the drag moved the note 2 beats later and 3 rows up, one undo step (%s)", m.moved ? "moved" : "NOT moved"));
    check (! m.change.all && m.change.notes == 2,
           fmt ("2 notes' drawings changed: the moved note and the note that slides into it (%d%s)", m.change.notes, m.change.all ? ", all" : ""));
    check (! m.change.all && m.change.rects.size() <= 3 && m.change.rects.size() >= 2,
           fmt ("the note layer marks %d notes' bounds (at most 3: where the note was, where it is, the sliding note)", (int) m.change.rects.size()));
    // inside those bounds every note they touch is drawn again (clipped to them): the dense clip has about 9 rows
    // of neighbours in each, so the count is checked against a full repaint's 512
    check (! m.partial.full && m.partial.rects >= 1 && m.partial.notesDrawn * 4 < 512,
           fmt ("the update drew %d note drawings (clipped to the bounds) in %d rectangles (%.0f device pixels) in %.2f ms, "
                "under a quarter of a full repaint's 512", m.partial.notesDrawn, m.partial.rects, m.partial.pixels, m.partial.ms));
    check (m.repainted > 0.0 && m.repainted < 0.15 * m.canvas,
           fmt ("the roll asked to repaint %.0f pixels, %.1f %% of the %.0f-pixel roll (the notes' bounds and the slide link)",
                m.repainted, 100.0 * m.repainted / std::max (1.0, m.canvas), m.canvas));
    check (m.diff.differing == 0,
           fmt ("repainting only that over the picture before gives the full repaint's picture: %d pixels differ (largest %d)",
                m.diff.differing, m.diff.largest));

    // the window's own renderer, as a figure
    const auto n = moveOneNote (juce::NativeImageType());
    note (fmt ("native renderer: %d note drawings, %d rectangles, %.2f ms; %d pixels differ from its full repaint (largest %d)",
               n.partial.notesDrawn, n.partial.rects, n.partial.ms, n.diff.differing, n.diff.largest));
    note (fmt ("native renderer: of those, %d inside the area asked for, %d within 2 px of its edge; around (%d, %d, %d x %d); %s",
               n.diff.insideAsked, n.diff.atAskedEdge, n.diff.where.getX(), n.diff.where.getY(), n.diff.where.getWidth(), n.diff.where.getHeight(),
               n.diff.samples.toRawUTF8()));
    note (fmt ("the note layer itself after the update against after a full repaint: %d pixels differ (largest %d) native, %d software",
               n.cache.differing, n.cache.largest, m.cache.differing));
    note (fmt ("repainted one rectangle at a time: %d pixels differ (largest %d) with the native renderer, %d with software",
               n.diffEach.differing, n.diffEach.largest, m.diffEach.differing));
    note (fmt ("native renderer: two full repaints of the same state differ in %d pixels (largest %d); software: %d",
               n.fullTwice.differing, n.fullTwice.largest, m.fullTwice.differing));
    check (n.moved && ! n.change.all && n.change.rects.size() <= 3,
           fmt ("with the native renderer too the move marks %d notes' bounds", (int) n.change.rects.size()));
}
