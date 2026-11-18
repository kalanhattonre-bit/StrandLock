#include "RollPainter.h"

#include "core/JsCompat.h"
#include "core/Music.h"
#include "model/Glides.h"
#include "model/Node.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        /** sl-ui.js SOUND_LANES: the ribbons every note shows, drawn in this order. */
        constexpr core::LaneId kSoundLanes[] = { core::LaneId::amp, core::LaneId::vib, core::LaneId::tone,
                                                  core::LaneId::formant, core::LaneId::hollow };

        bool isBlack (int p) noexcept
        {
            const int pc = ((p % 12) + 12) % 12;
            return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        }

        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        juce::Point<float> pt (double x, double y) noexcept { return { (float) x, (float) y }; }

        juce::Rectangle<float> rectF (double x, double y, double w, double h) noexcept
        {
            return { (float) x, (float) y, (float) w, (float) h };
        }

        juce::Rectangle<double> around (double x, double y, double r) noexcept
        {
            return { x - r, y - r, 2.0 * r, 2.0 * r };
        }

        /** A square of half-size r turned 45 degrees at (x, y): filled, then a 2 px outline (sl-ui.js diamond). */
        void diamond (juce::Graphics& g, double x, double y, double r, juce::Colour fill, juce::Colour stroke)
        {
            juce::Path p;
            p.addRectangle ((float) -r, (float) -r, (float) (2.0 * r), (float) (2.0 * r));
            p.applyTransform (juce::AffineTransform::rotation ((float) (kPi / 4.0)).translated ((float) x, (float) y));
            g.setColour (fill);
            g.fillPath (p);
            g.setColour (stroke);
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }

        /** The outline of a circle, thickness pixels wide, in the current colour: the path JUCE's software renderer
            fills for Graphics::drawEllipse on a circle, so its pictures are unchanged. Direct2D's own drawEllipse
            skips an ellipse whose area misses the clip even when its outline reaches into it (it tests the area
            alone), so a partial repaint lost the slivers of rings along its edges (run 44200621878: 111 pixels);
            a path is never skipped. */
        void ring (juce::Graphics& g, juce::Rectangle<float> circle, float thickness)
        {
            juce::Path p;
            p.addEllipse (circle.expanded (thickness * 0.5f));
            p.addEllipse (circle.reduced (thickness * 0.5f));
            p.setUsingNonZeroWinding (false);
            g.fillPath (p);
        }

        /** A path stroked in dashes of on and off pixels (canvas setLineDash), in the current colour. */
        void dashed (juce::Graphics& g, const juce::Path& path, float width, float on, float off)
        {
            juce::Path d;
            const float dashes[] = { on, off };
            juce::PathStrokeType (width).createDashedStroke (d, path, dashes, 2);
            g.fillPath (d);
        }

        /** The prototype writes the link emoji after a slide link's start; JUCE's fonts have none, so two links of
            a chain are drawn there instead, about the emoji's size (11 px). baseline as fillText's y. */
        void linkIcon (juce::Graphics& g, double x, double baseline, juce::Colour c)
        {
            juce::Path p;
            p.addRoundedRectangle (-5.2f, -2.1f, 6.6f, 4.2f, 2.1f);
            p.addRoundedRectangle (-1.4f, -2.1f, 6.6f, 4.2f, 2.1f);
            p.applyTransform (juce::AffineTransform::rotation ((float) (-kPi / 4.0)).translated ((float) x + 6.0f, (float) baseline - 5.0f));
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }

        juce::Rectangle<double> linkIconBounds (double x, double baseline) noexcept
        {
            return { x - 1.0, baseline - 13.0, 16.0, 16.0 };
        }

        /** Where a slide link draws: from the end of its note to the start of the note it slides into. */
        struct LinkLine
        {
            double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
        };

        std::optional<LinkLine> linkLine (const PaintState& s, const std::string& id, const model::Glide& g)
        {
            const auto n = s.noteById (id), t = s.noteById (g.toId);
            if (n == nullptr || t == nullptr)
                return std::nullopt;
            const auto& geo = *s.geo;
            const double bw = geo.beatWidth(), rh = geo.rowHeight();
            LinkLine l;
            l.x1 = (n->s + n->l) * bw;
            l.y1 = geo.baseYAt (*n, 1.0);
            l.x2 = t->s * bw;
            l.y2 = geo.cy (t->p) - (model::evalNodes (model::noteLane (*t, core::LaneId::pitch), 0.0) - 0.5) * 24.0 * rh;
            return l;
        }

        /** The slide diamonds show on the primary note in Build and in Pitch mode, unless Freehand is on. */
        bool showsSlideHandles (const PaintState& s)
        {
            const auto lane = s.roll.activeLane();
            return ! s.free && (s.roll.mode == 1 || (lane.has_value() && *lane == core::LaneId::pitch));
        }
    }

    //==============================================================================================
    void PaintState::prepare()
    {
        byId.clear();
        selected.clear();
        if (clip != nullptr)
        {
            byId.reserve (clip->notes.size());
            for (const auto& n : clip->notes)
                byId.emplace (n->id, n);
        }
        for (const auto& id : roll.selected)
            selected.insert (id);
    }

    model::NotePtr PaintState::noteById (const std::string& id) const
    {
        if (id.empty())
            return nullptr;
        const auto it = byId.find (id);
        return it != byId.end() ? it->second : nullptr;
    }

    //==============================================================================================
    Tok RollPainter::laneToken (core::LaneId id) noexcept
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
        return Tok::acc;
    }

    juce::Colour RollPainter::laneColour (const Palette& p, core::LaneId id) noexcept
    {
        return p[laneToken (id)];
    }

    //==============================================================================================
    // sl-ui.js drawRoll: the rows, then the grid.

    void RollPainter::paintRows (juce::Graphics& g, juce::Rectangle<double> visible) const
    {
        const auto& T = *s.colours;
        const auto& geo = *s.geo;
        const double bw = geo.beatWidth(), rh = geo.rowHeight();
        const double sx = visible.getX(), sy = visible.getY(), W = visible.getWidth(), H = visible.getHeight();
        const double total = s.clip != nullptr ? RollGeometry::totalBeats (*s.clip) : 0.0, Wc = total * bw;
        const int beatsPerBar = s.clip != nullptr ? std::max (1, s.clip->beatsPerBar) : model::kDefaultBeatsPerBar;
        const int key = s.inst.key;
        const bool chromatic = s.inst.scale == 0;

        // rows: the key's root, the notes outside the scale, the black keys, and a line under every row
        const int pTop = geo.pitchAt (sy), pBot = geo.pitchAt (sy + H);
        const double xw = std::max (0.0, std::min (sx + W, Wc) - sx);
        for (int p = pBot; p <= pTop; ++p)
        {
            const double y = (double) (kMaxPitch - p) * rh;
            const bool inS = core::inScale ((double) p, key, (int) s.inst.scale);
            const bool isRoot = ! chromatic && ((p - key) % 12 + 12) % 12 == 0;
            g.setColour (isRoot ? T[Tok::rowRoot] : ! inS ? T[Tok::rowOut] : isBlack (p) ? T[Tok::rowBlack] : T[Tok::row]);
            g.fillRect (rectF (sx, y, xw, rh));
            g.setColour (p % 12 == 0 ? T[Tok::octLine] : T[Tok::rowLine]);
            g.fillRect (rectF (sx, y + rh - 1.0, xw, 1.0));
        }

        // the grid: the Grid's steps (at least 6 px apart), beats and bars, up to the clip's end
        const double yT = (double) (kMaxPitch - pTop) * rh;
        const double yB = std::min ((double) kRows * rh, (double) (kMaxPitch - pBot + 1) * rh);
        double sub = s.snap > 0.0 ? s.snap : 0.25;
        while (sub * bw < 6.0 && sub < 4.0)
            sub *= 2.0;
        const double last = std::min (total, (sx + W) / bw) + 1e-6;
        for (double b = std::max (0.0, std::floor (sx / bw / sub) * sub); b <= last; b += sub)
        {
            const double x = core::jsRound (b * bw);
            const double bars = b / (double) beatsPerBar;
            const bool isBar = std::abs (bars - core::jsRound (bars)) < 1e-6;
            const bool isBeat = std::abs (b - core::jsRound (b)) < 1e-6;
            g.setColour (isBar ? T[Tok::gridBar] : isBeat ? T[Tok::gridBeat] : T[Tok::gridSub]);
            g.fillRect (rectF (x, yT, 1.0, yB - yT));
        }
    }

    std::vector<model::NotePtr> RollPainter::visibleNotes (juce::Rectangle<double> visible) const
    {
        std::vector<std::pair<int, model::NotePtr>> vis;
        if (s.clip == nullptr)
            return {};
        const auto& geo = *s.geo;
        const double bw = geo.beatWidth(), rh = geo.rowHeight(), lh = geo.laneHeight();
        const double sx = visible.getX(), sy = visible.getY(), W = visible.getWidth(), H = visible.getHeight();
        for (const auto& n : s.clip->notes)
        {
            const double x0 = n->s * bw, x1 = (n->s + n->l) * bw;
            if (x1 < sx - 20.0 || x0 > sx + W + 20.0)
                continue;
            const NoteGeom& G = geo.geom (*n);
            const double top = geo.cy (n->p) - G.smax * rh - lh - 8.0, bot = geo.cy (n->p) - G.smin * rh + 8.0;
            if (bot < sy || top > sy + H)
                continue;
            // unselected, then selected, then the primary: (selected, primary) in that order
            vis.emplace_back ((s.isSelected (n->id) ? 2 : 0) + (s.isPrimary (n->id) ? 1 : 0), n);
        }
        std::stable_sort (vis.begin(), vis.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<model::NotePtr> out;
        out.reserve (vis.size());
        for (auto& v : vis)
            out.push_back (std::move (v.second));
        return out;
    }

    //==============================================================================================
    // sl-ui.js drawNote and ribbonPath.

    void RollPainter::paintNote (juce::Graphics& g, const model::Note& n, bool isSel) const
    {
        const auto& T = *s.colours;
        const auto& geo = *s.geo;
        const NoteGeom& G = geo.geom (n);
        const int K = G.K;
        if (K < 2 || (int) G.base.size() != K || (int) G.vib.size() != K)
            return;

        const double bw = geo.beatWidth(), rh = geo.rowHeight(), lh = geo.laneHeight();
        const double x0 = n.s * bw, x1 = (n.s + n.l) * bw, base = geo.cy (n.p);
        const auto X = [x0, x1, K] (int i) { return x0 + (x1 - x0) * (double) i / (double) (K - 1); };
        const auto BY = [&G, base, rh] (int i) { return base - (double) G.base[(std::size_t) i] * rh; };
        const auto AL = s.roll.activeLane();
        const int mode = s.roll.mode;

        // Build: the five sound lanes. A lane mode: the others, then its own lane last (Pitch has no ribbon).
        // Sample: the five, then its lane.
        std::array<core::LaneId, 6> order {};
        int count = 0;
        for (const auto id : kSoundLanes)
            if (mode == 1 || mode == 8 || ! AL.has_value() || id != *AL)
                order[(std::size_t) count++] = id;
        if (AL.has_value() && mode != 1 && (mode == 8 || *AL != core::LaneId::pitch))
            order[(std::size_t) count++] = *AL;

        // Refresh does nothing while the Sample tab's Refresh is Smooth: its ribbon is dimmed.
        const bool dimRefresh = s.inst.refresh.mode == core::RefreshMode::smooth;
        const float aAct = T.number (Tok::aAct), aActSel = T.number (Tok::aActSel);
        const float aIdle = T.number (Tok::aIdle), aIdleSel = T.number (Tok::aIdleSel);

        juce::Path path;
        path.preallocateSpace (3 * (2 * K + 2));
        for (int o = 0; o < count; ++o)
        {
            const auto id = order[(std::size_t) o];
            const bool act = AL.has_value() && id == *AL;
            const auto& a = geo.laneTable (n, id);
            if ((int) a.size() != K)
                continue;
            const float dim = id == core::LaneId::refresh && dimRefresh ? 0.35f : 1.0f;

            // ribbonPath: along the lane's line, then back along the note's line
            path.clear();
            path.startNewSubPath (pt (X (0), BY (0) - (double) a[0] * lh));
            for (int i = 1; i < K; ++i)
                path.lineTo (pt (X (i), BY (i) - (double) a[(std::size_t) i] * lh));
            for (int i = K - 1; i >= 0; --i)
                path.lineTo (pt (X (i), BY (i)));
            path.closeSubPath();

            const float alpha = act ? (isSel ? aActSel : aAct)
                                    : mode == 1 ? (isSel ? aIdleSel : aIdle) : aIdle * (mode == 8 ? 0.45f : 0.7f);
            const auto colour = laneColour (T, id);
            g.setColour (colour.withAlpha (dim * alpha));
            g.fillPath (path);

            if (act)
            {
                path.clear();
                path.startNewSubPath (pt (X (0), BY (0) - (double) a[0] * lh));
                for (int i = 1; i < K; ++i)
                    path.lineTo (pt (X (i), BY (i) - (double) a[(std::size_t) i] * lh));
                g.setColour (colour.withAlpha (dim * (isSel ? 1.0f : 0.65f)));
                g.strokePath (path, juce::PathStrokeType (1.6f));
            }
        }

        // Travel (the speed kind): the middle line is "stopped"
        if (AL.has_value() && *AL == core::LaneId::travel && isSel && n.sample.travel != model::TravelKind::path)
        {
            path.clear();
            path.startNewSubPath (pt (X (0), BY (0) - 0.5 * lh));
            for (int i = 1; i < K; ++i)
                path.lineTo (pt (X (i), BY (i) - 0.5 * lh));
            g.setColour (laneColour (T, core::LaneId::travel).withAlpha (0.7f));
            dashed (g, path, 1.0f, 3.0f, 3.0f);
        }

        // the note line: pitch and slide, the vibrato drawn as a real squiggle
        path.clear();
        path.startNewSubPath (pt (X (0), BY (0) - (double) G.vib[0] * rh));
        for (int i = 1; i < K; ++i)
            path.lineTo (pt (X (i), BY (i) - (double) G.vib[(std::size_t) i] * rh));
        const auto lineColour = isSel ? T[Tok::noteLineSel] : T[Tok::noteLine];
        g.setColour (lineColour);
        g.strokePath (path, juce::PathStrokeType (isSel ? 3.2f : 2.4f));

        // the start dot (a ring) and the end dot
        const double ys = BY (0) - (double) G.vib[0] * rh;
        const double ye = BY (K - 1) - (double) G.vib[(std::size_t) (K - 1)] * rh;
        const auto start = around (x0 + 3.0, ys, isSel ? 6.0 : 4.5).toFloat();
        g.setColour (T[Tok::dotFill]);
        g.fillEllipse (start);
        g.setColour (T[Tok::startRing]);
        ring (g, start, 2.2f);
        const auto end = around (x1 - 3.0, ye, isSel ? 6.0 : 4.0).toFloat();
        g.setColour (lineColour);
        g.fillEllipse (end);
        g.setColour (T[Tok::dotFill]);
        ring (g, end, 2.0f);
    }

    juce::Rectangle<double> RollPainter::noteBoundsIn (const RollGeometry& geo, const model::Note& n)
    {
        // The ribbons rise up to a lane band above the note's base line (the line without the vibrato); the line
        // itself is the base plus the squiggle (smin .. smax); the dots sit on the line at both ends.
        const NoteGeom& G = geo.geom (n);
        double hiBase = -std::numeric_limits<double>::infinity(), loBase = std::numeric_limits<double>::infinity();
        for (const float b : G.base)
        {
            hiBase = std::max (hiBase, (double) b);
            loBase = std::min (loBase, (double) b);
        }
        const double rh = geo.rowHeight(), bw = geo.beatWidth(), cy = geo.cy (n.p);
        const double top = cy - std::max (hiBase * rh + geo.laneHeight(), G.smax * rh) - notePad;
        const double bottom = cy - std::min (loBase, G.smin) * rh + notePad;
        const double left = n.s * bw - notePad, right = (n.s + n.l) * bw + notePad;
        if (! std::isfinite (top) || ! std::isfinite (bottom) || ! std::isfinite (left) || ! std::isfinite (right))
            return {};
        return { left, top, right - left, bottom - top };
    }

    //==============================================================================================
    // What is drawn over the notes.

    void RollPainter::paintOverlay (juce::Graphics& g) const
    {
        paintLinks (g);
        paintHandles (g);
        paintGhost (g);
        paintBox (g);
    }

    void RollPainter::paintLinks (juce::Graphics& g) const
    {
        // sl-ui.js drawLinks: a dashed line from the end of a linked note to the start of the note it slides into
        if (s.clip == nullptr)
            return;
        const auto colour = (*s.colours)[Tok::link];
        const float dashes[] = { 4.0f, 4.0f };
        for (const auto& [id, glide] : s.geo->glideMap())
        {
            if (! glide.linked)
                continue;
            const auto l = linkLine (s, id, glide);
            if (! l)
                continue;
            g.setColour (colour);
            g.drawDashedLine (juce::Line<float> (pt (l->x1, l->y1), pt (l->x2, l->y2)), dashes, 2, 1.5f);
            linkIcon (g, l->x1 + 3.0, l->y1 - 4.0, colour);
        }
    }

    void RollPainter::paintHandles (juce::Graphics& g) const
    {
        // sl-ui.js drawHandles
        if (s.clip == nullptr)
            return;
        const auto& T = *s.colours;
        const auto& geo = *s.geo;
        const auto P = s.noteById (s.roll.primary);
        if (P != nullptr && showsSlideHandles (s))
            if (const auto gh = geo.glideHandles (*P))
            {
                diamond (g, gh->start.x, gh->start.y, 5.0, T[Tok::link], T[Tok::link]);
                diamond (g, gh->mid.x, gh->mid.y, 5.0, T[Tok::dotFill], T[Tok::link]);
            }

        const auto AL = s.roll.activeLane();
        if (! AL.has_value())
            return;
        const auto lane = *AL;
        const auto col = laneColour (T, lane);
        for (const auto& n : RollGeometry::handleNotes (*s.clip, s.roll))
        {
            const auto& nd = model::noteLane (*n, lane);
            const int count = (int) nd.size();
            if (count == 0)
                continue;
            // the nodes joined by a dashed line
            juce::Path line;
            for (int k = 0; k < count; ++k)
            {
                const auto q = geo.nodeXY (*n, lane, k);
                if (k == 0)
                    line.startNewSubPath (pt (q.x, q.y));
                else
                    line.lineTo (pt (q.x, q.y));
            }
            g.setColour (col.withAlpha (0.9f));
            dashed (g, line, 1.0f, 3.0f, 3.0f);
            if (count > 64)
                continue;

            // crowded nodes (a Squiggle on a short note): small dots, and below 6 px apart only the ends
            const double gap = count > 1 ? (n->l * geo.beatWidth()) / (double) (count - 1) : 99.0;
            const bool small = gap < 14.0, tiny = gap < 6.0;
            const auto& act = s.roll.activeNode;
            for (int k = 0; k < count; ++k)
            {
                const bool isAct = act.has_value() && act->id == n->id && act->k == k && act->lane == lane;
                if (tiny && ! isAct && k != 0 && k != count - 1)
                    continue;
                const auto q = geo.nodeXY (*n, lane, k);
                const auto dot = around (q.x, q.y, isAct ? 6.5 : small ? 3.0 : 5.5).toFloat();
                g.setColour (col);
                g.fillEllipse (dot);
                g.setColour (T[Tok::handleRing]);
                ring (g, dot, small && ! isAct ? 1.0f : 2.0f);
            }
        }

        // the primary note's segment diamonds (filled when the segment is bent)
        if (P != nullptr && ! s.free)
        {
            const auto& nd = model::noteLane (*P, lane);
            for (int k = 0; k + 1 < (int) nd.size(); ++k)
            {
                const auto xy = geo.segXY (*P, lane, k);
                if (! xy)
                    continue;
                const auto& q = nd[(std::size_t) k];
                const bool bent = q.shape == model::NodeShape::bend && std::abs (q.bend) > 1e-3;
                diamond (g, xy->x, xy->y, 3.6, bent ? col : T[Tok::dotFill], col);
            }
        }
    }

    void RollPainter::paintGhost (juce::Graphics& g) const
    {
        // sl-ui.js drawGhost: the notes of the preview that differ from the clip, dashed
        if (s.ghost == nullptr || s.ghostGeo == nullptr)
            return;
        const auto& gg = *s.ghostGeo;
        const double bw = gg.beatWidth(), rh = gg.rowHeight(), lh = gg.laneHeight();
        const auto AL = s.roll.activeLane();
        g.setColour ((*s.colours)[Tok::ghost].withAlpha (0.75f));
        for (const auto& n : s.ghost->notes)
        {
            if (s.noteById (n->id) == n)
                continue;   // the same note: unchanged
            const NoteGeom& G = gg.geom (*n);
            const int K = G.K;
            if (K < 2 || (int) G.base.size() != K || (int) G.vib.size() != K)
                continue;
            const double x0 = n->s * bw, x1 = (n->s + n->l) * bw, base = gg.cy (n->p);
            const auto X = [x0, x1, K] (int i) { return x0 + (x1 - x0) * (double) i / (double) (K - 1); };
            juce::Path path;
            if (AL.has_value() && *AL != core::LaneId::pitch)
            {
                const auto& a = gg.laneTable (*n, *AL);
                if ((int) a.size() == K)
                {
                    path.startNewSubPath (pt (X (0), base - (double) G.base[0] * rh - (double) a[0] * lh));
                    for (int i = 1; i < K; ++i)
                        path.lineTo (pt (X (i), base - (double) G.base[(std::size_t) i] * rh - (double) a[(std::size_t) i] * lh));
                    dashed (g, path, 1.6f, 5.0f, 4.0f);
                }
            }
            path.clear();
            path.startNewSubPath (pt (X (0), base - (double) G.base[0] * rh - (double) G.vib[0] * rh));
            for (int i = 1; i < K; ++i)
                path.lineTo (pt (X (i), base - (double) G.base[(std::size_t) i] * rh - (double) G.vib[(std::size_t) i] * rh));
            dashed (g, path, 1.6f, 5.0f, 4.0f);
        }
    }

    void RollPainter::paintBox (juce::Graphics& g) const
    {
        if (! s.box)
            return;
        const auto& b = *s.box;
        const double w = b.x1 - b.x0, h = b.y1 - b.y0;
        g.setColour ((*s.colours)[Tok::boxFill]);
        g.fillRect (rectF (b.x0, b.y0, w, h));
        juce::Path outline;
        outline.addRectangle ((float) (b.x0 + 0.5), (float) (b.y0 + 0.5), (float) w, (float) h);
        g.setColour ((*s.colours)[Tok::boxLine]);
        dashed (g, outline, 1.0f, 4.0f, 3.0f);
    }

    std::optional<juce::Rectangle<double>> RollPainter::readoutBox (juce::Rectangle<double> visible) const
    {
        // shell.html #readout: 11.5 px text, 3 px by 7 px padding, at the pointer + (14, -30), kept inside the view
        if (! s.readout || s.readout->text.empty())
            return std::nullopt;
        const auto text = juce::String::fromUTF8 (s.readout->text.c_str());
        const double w = std::ceil ((double) Theme::textWidth (Theme::px (11.5f), text)) + 14.0, h = 21.0;
        const double sx = s.readout->x - visible.getX(), sy = s.readout->y - visible.getY();
        const double left = clampD (sx + 14.0, 4.0, std::max (4.0, visible.getWidth() - w - 4.0));
        const double top = clampD (sy - 30.0, 4.0, std::max (4.0, visible.getHeight() - 24.0));
        return juce::Rectangle<double> (left, top, w, h);
    }

    void RollPainter::paintReadout (juce::Graphics& g, juce::Rectangle<double> visible) const
    {
        const auto box = readoutBox (visible);
        if (! box)
            return;
        const auto r = box->toFloat();
        g.setColour ((*s.colours)[Tok::tipBg]);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour ((*s.colours)[Tok::tipTxt]);
        g.setFont (Theme::px (11.5f));
        g.drawText (juce::String::fromUTF8 (s.readout->text.c_str()), r, juce::Justification::centred, false);
    }

    std::vector<juce::Rectangle<double>> RollPainter::overlayBounds (juce::Rectangle<double> visible) const
    {
        std::vector<juce::Rectangle<double>> out;
        if (s.clip == nullptr || s.geo == nullptr)
            return out;
        const auto& geo = *s.geo;

        // the slide links and their chain marks
        for (const auto& [id, glide] : geo.glideMap())
        {
            if (! glide.linked)
                continue;
            if (const auto l = linkLine (s, id, glide))
            {
                out.push_back (juce::Rectangle<double>::leftTopRightBottom (std::min (l->x1, l->x2), std::min (l->y1, l->y2),
                                                                            std::max (l->x1, l->x2), std::max (l->y1, l->y2))
                                   .expanded (3.0));
                out.push_back (linkIconBounds (l->x1 + 3.0, l->y1 - 4.0));
            }
        }

        // the primary note's slide diamonds and segment diamonds, the nodes of the notes whose nodes show
        const auto P = s.noteById (s.roll.primary);
        if (P != nullptr && showsSlideHandles (s))
            if (const auto gh = geo.glideHandles (*P))
            {
                out.push_back (around (gh->start.x, gh->start.y, 10.0));
                out.push_back (around (gh->mid.x, gh->mid.y, 10.0));
            }
        if (const auto AL = s.roll.activeLane())
        {
            for (const auto& n : RollGeometry::handleNotes (*s.clip, s.roll))
            {
                const auto& nd = model::noteLane (*n, *AL);
                std::optional<juce::Rectangle<double>> nodes;
                for (int k = 0; k < (int) nd.size(); ++k)
                {
                    const auto q = geo.nodeXY (*n, *AL, k);
                    const auto r = around (q.x, q.y, 9.0);
                    nodes = nodes ? nodes->getUnion (r) : r;
                }
                if (nodes)
                    out.push_back (*nodes);
            }
            if (P != nullptr && ! s.free)
            {
                const auto& nd = model::noteLane (*P, *AL);
                for (int k = 0; k + 1 < (int) nd.size(); ++k)
                    if (const auto xy = geo.segXY (*P, *AL, k))
                        out.push_back (around (xy->x, xy->y, 8.0));
            }
        }

        // the ghost's notes that differ from the clip
        if (s.ghost != nullptr && s.ghostGeo != nullptr)
            for (const auto& n : s.ghost->notes)
                if (s.noteById (n->id) != n)
                    out.push_back (noteBoundsIn (*s.ghostGeo, *n));

        // the box and the readout
        if (s.box)
            out.push_back (juce::Rectangle<double>::leftTopRightBottom (s.box->x0, s.box->y0, s.box->x1, s.box->y1).expanded (2.0));
        if (const auto r = readoutBox (visible))
            out.push_back (r->translated (visible.getX(), visible.getY()).expanded (2.0));
        return out;
    }
}
