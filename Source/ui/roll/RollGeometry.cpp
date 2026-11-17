#include "RollGeometry.h"

#include "core/Curves.h"
#include "core/JsCompat.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/Node.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace sl::ui::roll
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        double dist (double x, double y, Point p) noexcept { return jsHypot (x - p.x, y - p.y); }
    }

    double jsHypot (double a, double b) noexcept
    {
        // V8's MathHypot: NaN unless an argument is infinite, both scaled by the larger magnitude, Kahan summation.
        if (std::isinf (a) || std::isinf (b))
            return std::numeric_limits<double>::infinity();
        if (std::isnan (a) || std::isnan (b))
            return std::numeric_limits<double>::quiet_NaN();
        const double absA = std::abs (a), absB = std::abs (b);
        double max = 0.0;
        if (absA > max)
            max = absA;
        if (absB > max)
            max = absB;
        if (max == 0.0)
            return 0.0;
        double sum = 0.0, compensation = 0.0;
        for (const double v : { absA, absB })
        {
            const double n = v / max;
            const double summand = n * n - compensation;
            const double preliminary = sum + summand;
            compensation = (preliminary - sum) - summand;
            sum = preliminary;
        }
        return std::sqrt (sum) * max;
    }

    //==============================================================================================
    bool RollGeometry::Key::operator== (const Key& o) const noexcept
    {
        return bw == o.bw && bpm == o.bpm && vibRate == o.vibRate && hasGlide == o.hasGlide
            && (! hasGlide || (d == o.d && len == o.len && curve == o.curve));
    }

    void RollGeometry::setClip (const model::ClipPtr& c)
    {
        if (c == clip)
            return;
        clip = c;
        // Keep the drawn lines of the notes the new clip still holds (a drag changes one or two notes per move);
        // forget the rest once the cache has grown well past the clip.
        const std::size_t notes = clip != nullptr ? clip->notes.size() : 0;
        if (cache.size() > notes + 256)
        {
            std::unordered_set<std::uint64_t> keep;
            if (clip != nullptr)
                for (const auto& n : clip->notes)
                    keep.insert (n->serial.get());
            for (auto it = cache.begin(); it != cache.end();)
                it = keep.count (it->first) != 0 ? std::next (it) : cache.erase (it);
        }
        // a clip with fewer bars makes the roll narrower: the scroll stays inside it
        setScroll (scrX, scrY);
    }

    void RollGeometry::setInst (const core::Inst& i)
    {
        inst = i;
    }

    void RollGeometry::setBpm (double b)
    {
        bpm = std::isfinite (b) && b > 0.0 ? b : 120.0;
    }

    void RollGeometry::setZoom (double beatWidth, double rowHeight)
    {
        bw = std::isfinite (beatWidth) ? clampD (beatWidth, kMinBeatWidth, kMaxBeatWidth) : kDefaultBeatWidth;
        rh = std::isfinite (rowHeight) ? clampD (rowHeight, kMinRowHeight, kMaxRowHeight) : kDefaultRowHeight;
        setScroll (scrX, scrY);
    }

    //==============================================================================================
    double RollGeometry::contentWidth() const noexcept
    {
        const double beats = clip != nullptr ? totalBeats (*clip) : 0.0;
        return std::ceil (beats * bw + 60.0);
    }

    double RollGeometry::contentHeight() const noexcept
    {
        return std::ceil ((double) kRows * rh);
    }

    void RollGeometry::setViewSize (double width, double height)
    {
        viewW = std::isfinite (width) && width > 0.0 ? width : 0.0;
        viewH = std::isfinite (height) && height > 0.0 ? height : 0.0;
        setScroll (scrX, scrY);
    }

    void RollGeometry::setScroll (double x, double y)
    {
        // what a browser does with a scroller's scrollLeft and scrollTop: 0 .. the content's size less the view's
        const double maxX = std::max (0.0, contentWidth() - viewW), maxY = std::max (0.0, contentHeight() - viewH);
        scrX = std::isfinite (x) ? clampD (x, 0.0, maxX) : 0.0;
        scrY = std::isfinite (y) ? clampD (y, 0.0, maxY) : 0.0;
    }

    void RollGeometry::zoomBy (double h, double v)
    {
        // sl-ui.js zoom (h, v): the beat and row at the view's centre, the new zoom, then the scroll that keeps them
        const double centreBeat = (scrX + viewW / 2.0) / bw, centreRow = (scrY + viewH / 2.0) / rh;
        setZoom (bw * h, core::jsRound (rh * v));
        setScroll (centreBeat * bw - viewW / 2.0, centreRow * rh - viewH / 2.0);
    }

    void RollGeometry::wheelZoom (bool zoomIn, double viewX)
    {
        // sl-ui.js's Ctrl + wheel: the beat under the pointer stays under it
        const double beat = (viewX + scrX) / bw;
        setZoom (bw * (zoomIn ? 1.15 : 1.0 / 1.15), rh);
        setScroll (beat * bw - viewX, scrY);
    }

    void RollGeometry::centreOnNotes()
    {
        // sl-ui.js init: centre on most of the notes (the median pitch, 5 semitones up)
        std::vector<double> ps;
        if (clip != nullptr)
            for (const auto& n : clip->notes)
                ps.push_back (n->p);
        std::sort (ps.begin(), ps.end());
        const double mid = ps.empty() ? 66.0 : ps[ps.size() / 2] + 5.0;
        setScroll (scrX, std::max (0.0, cy (mid) - viewH / 2.0));
    }

    RollGeometry::ViewState RollGeometry::viewState() const noexcept
    {
        ViewState s;
        s.zoomH = bw / kDefaultBeatWidth;
        s.zoomV = rh / kDefaultRowHeight;
        s.scrollBeat = scrX / bw;
        s.scrollRow = scrY / rh;
        return s;
    }

    void RollGeometry::setViewState (const ViewState& s)
    {
        setZoom (kDefaultBeatWidth * s.zoomH, core::jsRound (kDefaultRowHeight * s.zoomV));
        setScroll (s.scrollBeat * bw, s.scrollRow * rh);
    }

    int RollGeometry::pitchAt (double y) const noexcept
    {
        const double r = (double) kMaxPitch - std::floor (y / rh);
        if (std::isnan (r))
            return kMinPitch;
        return (int) clampD (r, (double) kMinPitch, (double) kMaxPitch);
    }

    //==============================================================================================
    void RollGeometry::refreshGlides() const
    {
        if (glidesClip == clip && glidesMode == inst.glideMode && glidesTime == inst.glideTime)
            return;
        glidesClip = clip;
        glidesMode = inst.glideMode;
        glidesTime = inst.glideTime;
        glides = clip != nullptr ? model::computeGlides (*clip, inst) : model::GlideMap();
    }

    const RollGeometry::Entry& RollGeometry::entry (const model::Note& n) const
    {
        refreshGlides();
        const model::Glide* g = glides.find (n.id);

        Key key;
        key.bw = bw;
        key.bpm = bpm;
        key.vibRate = inst.vibRate;
        key.hasGlide = g != nullptr;
        if (g != nullptr)
        {
            key.d = g->d;
            key.len = g->len;
            key.curve = g->curve;
        }

        auto it = cache.find (n.serial.get());
        if (it != cache.end() && it->second.key == key)
            return it->second;

        // sl-ui.js geom (n): the pitch lane in semitones plus the glide into the next note, and the vibrato drawn as a
        // real squiggle (its depth times a sine at the vibrato rate, in real time at the tempo).
        Entry e;
        e.key = key;
        NoteGeom& G = e.g;
        G.res = std::min (400.0, std::max (bw / 3.0, 12.0 / std::max (0.01, n.l)));
        const std::vector<float> P = model::laneTable (n, core::LaneId::pitch, G.res);
        const std::vector<float> V = model::laneTable (n, core::LaneId::vib, G.res);
        const int K = (int) P.size();
        G.K = K;
        G.base.assign ((std::size_t) K, 0.0f);
        G.vib.assign ((std::size_t) K, 0.0f);
        const double dur = n.l * 60.0 / bpm;
        double smin = std::numeric_limits<double>::infinity(), smax = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < K; ++i)
        {
            const double t = (double) i / (double) (K - 1);
            double s = ((double) P[(std::size_t) i] - 0.5) * 24.0;
            if (g != nullptr && g->len > 0.0)
            {
                const double st = n.l - g->len, tb = t * n.l;
                if (tb > st)
                    s += g->d * core::gShape ((tb - st) / g->len, g->curve);
            }
            G.base[(std::size_t) i] = (float) s;
            const double depth = V.size() == P.size() ? (double) V[(std::size_t) i] : 0.0;
            G.vib[(std::size_t) i] = (float) (depth * std::sin (2.0 * kPi * inst.vibRate * t * dur));
            const double a = s + (double) G.vib[(std::size_t) i];
            if (a < smin)
                smin = a;
            if (a > smax)
                smax = a;
        }
        G.smin = smin;
        G.smax = smax;
        if (g != nullptr)
            G.glide = *g;
        e.tables[(std::size_t) core::LaneId::pitch] = P;
        e.tables[(std::size_t) core::LaneId::vib] = V;

        auto& slot = cache[n.serial.get()];
        slot = std::move (e);
        return slot;
    }

    const NoteGeom& RollGeometry::geom (const model::Note& n) const
    {
        return entry (n).g;
    }

    const std::vector<float>& RollGeometry::laneTable (const model::Note& n, core::LaneId lane) const
    {
        const Entry& e = entry (n);
        auto& t = e.tables[(std::size_t) lane];
        if (t.empty())
            t = model::laneTable (n, lane, e.g.res);
        return t;
    }

    //==============================================================================================
    double RollGeometry::baseYAt (const model::Note& n, double t) const
    {
        const NoteGeom& G = geom (n);
        const double fi = clampD (t, 0.0, 1.0) * (double) (G.K - 1);
        const int i = (int) std::min ((double) (G.K - 2), std::floor (fi));
        const double fr = fi - (double) i;
        const double b0 = (double) G.base[(std::size_t) i], b1 = (double) G.base[(std::size_t) i + 1];
        return cy (n.p) - (b0 + (b1 - b0) * fr) * rh;
    }

    double RollGeometry::valueY (const model::Note& n, core::LaneId lane, double t, double v) const
    {
        if (lane == core::LaneId::pitch)
            return cy (n.p) - (v - 0.5) * 24.0 * rh;
        return baseYAt (n, t) - v * laneHeight();
    }

    double RollGeometry::laneValue (const model::Note& n, core::LaneId lane, double t, double y) const
    {
        if (lane == core::LaneId::pitch)
            return 0.5 + ((cy (n.p) - y) / rh) / 24.0;
        return (baseYAt (n, t) - y) / laneHeight();
    }

    Point RollGeometry::nodeXY (const model::Note& n, core::LaneId lane, int k) const
    {
        const model::NodeList& nd = model::noteLane (n, lane);
        const model::Node& q = nd[(std::size_t) k];
        const double x0 = n.s * bw, x1 = (n.s + n.l) * bw;
        return { x0 + (x1 - x0) * q.t, valueY (n, lane, q.t, q.v) };
    }

    std::optional<Point> RollGeometry::segXY (const model::Note& n, core::LaneId lane, int k) const
    {
        const model::NodeList& nd = model::noteLane (n, lane);
        const model::Node& A = nd[(std::size_t) k];
        const model::Node& B = nd[(std::size_t) k + 1];
        const double x0 = n.s * bw, x1 = (n.s + n.l) * bw;
        if ((B.t - A.t) * (x1 - x0) < kMinSegmentPixels)
            return std::nullopt;
        const double t = (A.t + B.t) / 2.0, v = model::evalNodes (nd, t);
        return Point { x0 + (x1 - x0) * t, valueY (n, lane, t, v) };
    }

    std::optional<GlideHandles> RollGeometry::glideHandles (const model::Note& n) const
    {
        const NoteGeom& G = geom (n);
        if (! G.glide || ! (G.glide->len > 0.0))
            return std::nullopt;
        const model::Glide g = *G.glide;
        const double st = n.s + n.l - g.len, tS = (st - n.s) / n.l, tM = (st + g.len / 2.0 - n.s) / n.l;
        GlideHandles h;
        h.glide = g;
        h.start = { st * bw, baseYAt (n, tS) };
        h.mid = { (st + g.len / 2.0) * bw, baseYAt (n, tM) };
        return h;
    }

    NoteBox RollGeometry::noteBox (const model::Note& n, bool withRibbon) const
    {
        const NoteGeom& G = geom (n);
        NoteBox b;
        b.x0 = n.s * bw;
        b.x1 = (n.s + n.l) * bw;
        b.top = cy (n.p) - G.smax * rh - rh * 0.5;
        b.bottom = cy (n.p) - G.smin * rh + rh * 0.5;
        if (withRibbon)
            b.top -= laneHeight();
        return b;
    }

    Point RollGeometry::endDot (const model::Note& n) const
    {
        const NoteGeom& G = geom (n);
        return { (n.s + n.l) * bw - 3.0, cy (n.p) - (double) G.base[(std::size_t) G.K - 1] * rh };
    }

    Point RollGeometry::startDot (const model::Note& n) const
    {
        const NoteGeom& G = geom (n);
        return { n.s * bw + 3.0, cy (n.p) - (double) G.base[0] * rh };
    }

    bool RollGeometry::onNoteLine (const model::Note& n, double x, double y) const
    {
        const double x0 = n.s * bw, x1 = (n.s + n.l) * bw;
        const double yl = baseYAt (n, clampD ((x - x0) / std::max (1.0, x1 - x0), 0.0, 1.0));
        return y >= yl - rh * 0.7 && y <= yl + rh * 0.8;
    }

    //==============================================================================================
    std::vector<model::NotePtr> RollGeometry::selectedNotes (const model::Clip& c, const doc::RollState& state)
    {
        std::vector<model::NotePtr> out;
        for (const auto& n : c.notes)
            if (state.isSelected (n->id))
                out.push_back (n);
        return model::timeOrder (std::move (out));
    }

    std::vector<model::NotePtr> RollGeometry::handleNotes (const model::Clip& c, const doc::RollState& state)
    {
        auto sel = selectedNotes (c, state);
        if (sel.size() <= 16)
            return sel;
        for (const auto& n : c.notes)
            if (! state.primary.empty() && n->id == state.primary)
                return { n };
        return {};
    }

    model::NotePtr RollGeometry::hitNote (const model::Clip& c, const doc::RollState& state, double x, double y) const
    {
        // the later notes first, then (a stable sort) the selected ones before the others
        std::vector<model::NotePtr> list (c.notes.rbegin(), c.notes.rend());
        std::stable_sort (list.begin(), list.end(), [&state] (const model::NotePtr& a, const model::NotePtr& b)
                          { return state.isSelected (a->id) && ! state.isSelected (b->id); });
        const double lh = laneHeight();
        for (const auto& n : list)
        {
            const double x0 = n->s * bw, x1 = (n->s + n->l) * bw;
            if (x < x0 - 5.0 || x > x1 + 5.0)
                continue;
            const double yl = baseYAt (*n, (x - x0) / std::max (1.0, x1 - x0));
            if (y >= yl - lh - 2.0 && y <= yl + rh * 0.8)
                return n;
        }
        return nullptr;
    }

    std::optional<NodeHit> RollGeometry::hitNode (const model::Clip& c, const doc::RollState& state, double x, double y, core::LaneId lane) const
    {
        auto list = handleNotes (c, state);
        if (! state.primary.empty())
        {
            bool primaryInClip = false;
            for (const auto& n : c.notes)
                primaryInClip = primaryInClip || n->id == state.primary;
            if (primaryInClip)
                std::stable_sort (list.begin(), list.end(), [&state] (const model::NotePtr& a, const model::NotePtr& b)
                                  { return a->id == state.primary && b->id != state.primary; });
        }
        for (const auto& n : list)
        {
            const auto& nd = model::noteLane (*n, lane);
            for (int k = 0; k < (int) nd.size(); ++k)
                if (dist (x, y, nodeXY (*n, lane, k)) < kNodeHit)
                    return NodeHit { n, k };
        }
        return std::nullopt;
    }

    int RollGeometry::hitSeg (const model::Note& n, double x, double y, core::LaneId lane) const
    {
        const auto& nd = model::noteLane (n, lane);
        for (int k = 0; k + 1 < (int) nd.size(); ++k)
        {
            const auto xy = segXY (n, lane, k);
            if (xy && dist (x, y, *xy) < kSegmentHit)
                return k;
        }
        return -1;
    }
}
