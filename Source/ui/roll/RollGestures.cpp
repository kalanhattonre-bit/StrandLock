#include "RollGestures.h"

#include "InspectorText.h"

#include "core/JsCompat.h"
#include "core/Music.h"
#include "model/Generators.h"
#include "model/LaneFormat.h"
#include "model/Node.h"
#include "model/NoteSettings.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        // isDouble's kinds of thing: two presses only make a double-click on the same kind
        constexpr int kWhatNode = 0, kWhatSeg = 1, kWhatNote = 2, kWhatEmpty = 3;
        constexpr double kDoubleMs = 320.0, kDoublePixels = 22.0;

        // The middle dot of the readouts (U+00B7), written as UTF-8 so the source stays ASCII.
        const std::string kDot = "\xc2\xb7";

        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }
        double dist (double x, double y, Point p) noexcept { return jsHypot (x - p.x, y - p.y); }
        std::string num (double v) { return core::jsNumberToString (v); }

        /** Math.sign (x) || 1: the sign, and 1 for 0, -0 and NaN. */
        double signOr1 (double x) noexcept { return x > 0.0 ? 1.0 : x < 0.0 ? -1.0 : 1.0; }

        bool isPitch (const std::optional<core::LaneId>& lane) noexcept { return lane && *lane == core::LaneId::pitch; }

        const model::Note* noteIn (const model::Clip& c, const std::string& id) noexcept
        {
            for (const auto& n : c.notes)
                if (n->id == id)
                    return n.get();
            return nullptr;
        }

        /** sl-ui.js fmtBeats: '1 beat', '1.5 beats' (to 3 decimals). */
        std::string fmtBeats (double x)
        {
            const double r = core::jsRound (x * 1000.0) / 1000.0;
            return num (r) + (r == 1.0 ? " beat" : " beats");
        }

        /** sl-ui.js beatLabel: 'bar 2 beat 1.5'. */
        std::string beatLabel (double b, int beatsPerBar)
        {
            const double bpb = (double) beatsPerBar;
            const double bar = std::floor (b / bpb) + 1.0, beat = b - std::floor (b / bpb) * bpb + 1.0;
            return "bar " + num (bar) + " beat " + num (core::jsRound (beat * 100.0) / 100.0);
        }

        std::string percent (double x)
        {
            return (x > 0.0 ? "+" : "") + num (core::jsRound (x * 100.0)) + "%";
        }

        //==========================================================================================
        // sl-ui.js sameClip: two clips the same to 7 decimals (what a drag back to its start gives), as JSON.stringify
        // with every number rounded (NaN and the infinities print as null, -0 as 0).

        bool near7 (double a, double b) noexcept
        {
            const bool aNull = ! std::isfinite (a), bNull = ! std::isfinite (b);
            if (aNull || bNull)
                return aNull && bNull;
            const double ra = core::jsRound (a * 1e7) / 1e7, rb = core::jsRound (b * 1e7) / 1e7;
            const bool raNull = ! std::isfinite (ra), rbNull = ! std::isfinite (rb);
            if (raNull || rbNull)
                return raNull && rbNull;
            return ra == rb;
        }

        bool near7 (const std::optional<double>& a, const std::optional<double>& b) noexcept
        {
            return a.has_value() == b.has_value() && (! a || near7 (*a, *b));
        }

        bool nodesNear (const model::NodeList& a, const model::NodeList& b) noexcept
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (! near7 (a[i].t, b[i].t) || ! near7 (a[i].v, b[i].v) || a[i].shape != b[i].shape || ! near7 (a[i].bend, b[i].bend))
                    return false;
            return true;
        }

        bool genNear (const std::optional<model::Gen>& a, const std::optional<model::Gen>& b) noexcept
        {
            if (a.has_value() != b.has_value())
                return false;
            if (! a)
                return true;
            return a->kind == b->kind && a->shape == b->shape && a->rate == b->rate && near7 (a->d0, b->d0) && near7 (a->d1, b->d1)
                && near7 (a->warp, b->warp) && a->lockToBeat == b->lockToBeat && near7 (a->amount, b->amount) && near7 (a->smooth, b->smooth)
                && a->perNote == b->perNote && a->seed == b->seed && a->invert == b->invert && a->key == b->key;
        }

        bool sampleNear (const model::NoteSample& a, const model::NoteSample& b) noexcept
        {
            return a.motion == b.motion && a.travel == b.travel && a.start.mode == b.start.mode && near7 (a.start.pos, b.start.pos)
                && near7 (a.start.range, b.start.range) && a.start.seed == b.start.seed && a.tape == b.tape && a.rev == b.rev && a.jump == b.jump
                && a.engine == b.engine && a.lock == b.lock && a.restart == b.restart;
        }

        bool noteNear (const model::Note& a, const model::Note& b) noexcept
        {
            if (a.id != b.id || ! near7 (a.p, b.p) || ! near7 (a.s, b.s) || ! near7 (a.l, b.l) || ! near7 (a.vel, b.vel) || ! near7 (a.glide, b.glide)
                || ! near7 (a.gc, b.gc) || a.glideTo != b.glideTo || ! sampleNear (a.sample, b.sample))
                return false;
            for (int li = 0; li < core::kNumLanes; ++li)
            {
                const auto lane = static_cast<core::LaneId> (li);
                if (! nodesNear (model::noteLane (a, lane), model::noteLane (b, lane)) || ! genNear (a.genOf (lane), b.genOf (lane)))
                    return false;
            }
            return true;
        }

        bool nearlySameClip (const model::Clip& a, const model::Clip& b) noexcept
        {
            if (&a == &b)
                return true;
            if (a.bars != b.bars || a.notes.size() != b.notes.size() || a.beatsPerBar != b.beatsPerBar || ! near7 (a.offsetBeats, b.offsetBeats)
                || a.loop != b.loop || a.tieAcrossLoop != b.tieAcrossLoop)
                return false;
            for (std::size_t i = 0; i < a.notes.size(); ++i)
                if (a.notes[i] != b.notes[i] && ! noteNear (*a.notes[i], *b.notes[i]))
                    return false;
            return true;
        }

        const char* dragLabel (int type) noexcept
        {
            // sl-ui.js DRAG_LABELS, by Drag::Type
            static constexpr const char* labels[] = { "Slide length", "Slide curve", "Move node", "Bend segment", "Change length",
                                                      "Move note start", "Draw lane by hand", "Move notes", "Edit" };
            return type >= 0 && type < 9 ? labels[type] : "Edit";
        }
    }

    //==============================================================================================
    RollGestures::RollGestures (RollHost h)
        : host (std::move (h))
    {
        copyMask.fill (true);
        sync();
    }

    void RollGestures::sync()
    {
        geo.setInst (set.inst);
        geo.setBpm (set.bpm);
        geo.setClip (clip());
    }

    model::NotePtr RollGestures::noteById (const std::string& id) const
    {
        if (id.empty())
            return nullptr;
        const auto c = clip();
        for (const auto& n : c->notes)
            if (n->id == id)
                return n;
        return nullptr;
    }

    model::NotePtr RollGestures::primaryNote() const
    {
        return noteById (roll().primary);
    }

    std::vector<std::string> RollGestures::selectionInTimeOrder() const
    {
        std::vector<std::string> ids;
        for (const auto& n : RollGeometry::selectedNotes (*clip(), roll()))
            ids.push_back (n->id);
        return ids;
    }

    double RollGestures::total() const
    {
        return RollGeometry::totalBeats (*clip());
    }

    double RollGestures::snapR (double b) const
    {
        return set.snap > 0.0 ? core::jsRound (b / set.snap) * set.snap : b;
    }

    double RollGestures::scaleSnap (double p) const
    {
        return set.inst.scaleLock ? core::snapScale (p, set.inst.key, set.inst.scale) : p;
    }

    model::LaneCtx RollGestures::formatCtx (const model::Note* n) const
    {
        return model::formatCtx (set.inst, set.bpm, n);
    }

    bool RollGestures::isDouble (double x, double y, int what, double now)
    {
        const bool d = now - lastTap.t < kDoubleMs && jsHypot (x - lastTap.x, y - lastTap.y) < kDoublePixels && lastTap.what == what;
        lastTap = { d ? 0.0 : now, x, y, what };
        return d;
    }

    void RollGestures::show (model::Clip c)
    {
        model::EditResult r;
        r.clip = std::move (c);
        show (std::move (r));
    }

    void RollGestures::show (model::EditResult r)
    {
        const auto out = host.doc.apply (std::move (r));
        if (out.refused)
            toast (out.message);
        geo.setClip (clip());
    }

    bool RollGestures::setClipLabelled (model::Clip c, const std::string& label)
    {
        show (std::move (c));
        return host.commit ? host.commit (label) : false;
    }

    void RollGestures::toast (const std::string& text) const
    {
        if (host.toast)
            host.toast (text);
    }

    void RollGestures::audition (double p) const
    {
        if (host.audition && std::isfinite (p))
            host.audition ((int) p);
    }

    void RollGestures::showReadout (double x, double y, std::string text)
    {
        shown = Readout { x, y, std::move (text) };
    }

    std::optional<RollGestures::Box> RollGestures::box() const
    {
        if (! drag || drag->t != Drag::Type::box || ! drag->moved)
            return std::nullopt;
        return Box { std::min (drag->x0, drag->x1), std::min (drag->y0, drag->y1), std::max (drag->x0, drag->x1), std::max (drag->y0, drag->y1) };
    }

    //==============================================================================================
    // A press (sl-ui.js rollDown), in its order: 1 the slide handles of the primary note, 2 a node of the active lane
    // (3 a segment's diamond), 4 the end dots of the selected notes in Build, 5 Freehand, 6 a note, 7 empty space.

    void RollGestures::pointerDown (double x, double y, Mods mods, double now)
    {
        sync();
        drag.reset();
        const model::ClipPtr c = clip();
        const doc::RollState st = roll();
        const double bw = geo.beatWidth();
        const double b = x / bw;
        const auto AL = st.activeLane();
        const model::NotePtr P = primaryNote();
        const bool addMode = set.multi || mods.ctrl;
        const bool linkMode = (mods.shift || linkArm) && (st.mode == 1 || isPitch (AL));

        // 1. the slide handles of the primary note
        if (P != nullptr && ! set.free && (st.mode == 1 || isPitch (AL)))
        {
            if (const auto gh = geo.glideHandles (*P))
            {
                if (dist (x, y, gh->start) < kGlideHandleHit)
                {
                    Drag d;
                    d.t = Drag::Type::gstart;
                    d.id = P->id;
                    d.clip0 = c;
                    drag = std::move (d);
                    return;
                }
                if (dist (x, y, gh->mid) < kGlideHandleHit)
                {
                    Drag d;
                    d.t = Drag::Type::gmid;
                    d.id = P->id;
                    d.clip0 = c;
                    d.y0 = y;
                    d.c0 = std::isnan (P->gc) || P->gc == 0.0 ? 0.0 : P->gc;
                    d.d = gh->glide.d;
                    drag = std::move (d);
                    return;
                }
            }
        }

        // 2. a node of the active lane, and 3. a segment's diamond
        if (AL && ! set.free)
        {
            if (const auto h = geo.hitNode (*c, st, x, y, *AL))
            {
                const model::Note& n = *h->note;
                const model::NodeList nd = model::noteLane (n, *AL);
                if (isDouble (x, y, kWhatNode, now))
                {
                    if (h->k > 0 && h->k < (int) nd.size() - 1)
                    {
                        model::NodeList cut = nd;
                        cut.erase (cut.begin() + h->k);
                        host.doc.setActiveNode (std::nullopt);
                        setClipLabelled (model::setLaneNodes (*c, n.id, *AL, cut), "Delete node");
                    }
                    else
                    {
                        toast ("The first and last nodes stay: drag them up or down");
                    }
                    return;
                }
                if (st.primary != n.id)
                    host.doc.setPrimary (n.id);
                host.doc.setActiveNode (doc::ActiveNode { n.id, *AL, h->k, 0 });
                // the node keeps its distance to the pointer, and it moves only once the mouse has moved 3 px
                const Point hp = geo.nodeXY (n, *AL, h->k);
                Drag d;
                d.t = Drag::Type::node;
                d.id = n.id;
                d.lane = *AL;
                d.k = h->k;
                d.nodes0 = nd;
                d.clip0 = c;
                d.label = "Move node";
                d.x0 = x;
                d.y0 = y;
                d.hasDeadZone = true;
                d.gx = x - hp.x;
                d.gy = y - hp.y;
                drag = std::move (d);
                showReadout (x, y, laneReadout (*AL, nd[(std::size_t) h->k].v, formatCtx (&n)));
                return;
            }
            if (P != nullptr)
            {
                const int k = geo.hitSeg (*P, x, y, *AL);
                if (k >= 0)
                {
                    const model::NodeList nd = model::noteLane (*P, *AL);
                    const auto& q = nd[(std::size_t) k];
                    const bool bent = q.shape == model::NodeShape::bend && std::abs (q.bend) > 1e-3;
                    if (isDouble (x, y, kWhatSeg, now))
                    {
                        // a curved segment straightens; on a straight one the double-click adds a node there
                        if (! bent)
                        {
                            addNodeAt (*P, *AL, x, y, true);
                            return;
                        }
                        model::NodeList nn = nd;
                        nn[(std::size_t) k].shape = model::NodeShape::linear;
                        nn[(std::size_t) k].bend = 0.0;
                        setClipLabelled (model::setLaneNodes (*c, P->id, *AL, nn), "Straighten segment");
                        return;
                    }
                    Drag d;
                    d.t = Drag::Type::seg;
                    d.id = P->id;
                    d.lane = *AL;
                    d.k = k;
                    d.nodes0 = nd;
                    d.clip0 = c;
                    d.y0 = y;
                    d.b0 = q.shape == model::NodeShape::bend ? q.bend : 0.0;
                    drag = std::move (d);
                    return;
                }
            }
        }

        // 4. Build: the end dots of the selected notes (sideways = length, up and down = bend that end)
        if (st.mode == 1 && ! linkMode)
        {
            for (const auto& n : RollGeometry::selectedNotes (*c, st))
            {
                const auto& pitch = model::noteLane (*n, core::LaneId::pitch);
                if (dist (x, y, geo.endDot (*n)) < kEndDotHit)
                {
                    Drag d;
                    d.t = Drag::Type::end;
                    d.id = n->id;
                    d.y0 = y;
                    d.v0 = pitch.back().v;
                    d.clip0 = c;
                    d.label = "Change length";
                    drag = std::move (d);
                    return;
                }
                if (dist (x, y, geo.startDot (*n)) < kEndDotHit)
                {
                    Drag d;
                    d.t = Drag::Type::start;
                    d.id = n->id;
                    d.y0 = y;
                    d.v0 = pitch.front().v;
                    d.endBeat = n->s + n->l;
                    d.clip0 = c;
                    drag = std::move (d);
                    return;
                }
            }
        }

        // 5. Freehand: paint the active lane of the primary note; with no note there, the note under the mouse is
        // selected and painted in the same drag
        if (set.free && AL)
        {
            model::NotePtr F = P != nullptr && x >= P->s * bw - 6.0 && x <= (P->s + P->l) * bw + 6.0 ? P : nullptr;
            if (F == nullptr)
            {
                if (const auto hn = geo.hitNote (*c, st, x, y))
                {
                    host.doc.setSelection ({ hn->id }, hn->id);
                    F = noteById (hn->id);
                }
            }
            if (F != nullptr)
            {
                Drag d;
                d.t = Drag::Type::free;
                d.id = F->id;
                d.lane = *AL;
                for (const float v : model::sampleNodes (model::noteLane (*F, *AL), 97))
                    d.arr.push_back ((double) v);
                d.li = -1;
                d.lv = 0.0;
                d.clip0 = clip();
                drag = std::move (d);
                paint (x, y);
                return;
            }
        }

        // 6. a note
        if (const auto h = geo.hitNote (*c, st, x, y))
        {
            // with Shift or Ctrl held, a press on the faint band above a note's line waits: a drag draws a box, a
            // click does what a click on the line does
            if ((mods.shift || mods.ctrl) && ! linkArm && ! geo.onNoteLine (*h, x, y))
            {
                Drag d;
                d.t = Drag::Type::box;
                d.x0 = d.x1 = x;
                d.y0 = d.y1 = y;
                d.shift = mods.shift;
                d.ctrl = mods.ctrl;
                d.clickId = h->id;
                d.clickLink = linkMode;
                d.clickAdd = addMode;
                drag = std::move (d);
                return;
            }
            if (linkMode && P != nullptr && h->id != P->id)
            {
                linkNotes (*P, *h);
                return;
            }
            if (! pickNote (*h, addMode))
                return;
            if (AL && ! set.free && isDouble (x, y, kWhatNote, now) && P != nullptr && h->id == P->id)
            {
                addNodeAt (*h, *AL, x, y, false);
                return;
            }
            if (st.mode != 1)
                return;
            const doc::RollState after = roll();
            Drag d;
            d.t = Drag::Type::move;
            d.b0 = b;
            d.p0 = geo.pitchAt (y);
            d.clip0 = clip();
            d.ids = after.selected;
            for (const auto& id : d.ids)
            {
                const model::Note* n = noteIn (*d.clip0, id);
                d.orig.emplace_back (n != nullptr ? n->s : 0.0, n != nullptr ? n->p : 60.0);
            }
            d.lastP = h->p;
            d.hid = h->id;
            drag = std::move (d);
            return;
        }

        // 7. empty space: a double-click makes a note (Build); a drag draws a box
        if (isDouble (x, y, kWhatEmpty, now))
        {
            if (st.mode != 1)
            {
                toast ("Notes are locked in this mode: press 1 Build to add notes");
                return;
            }
            const double s = set.snap > 0.0 ? std::floor (b / set.snap) * set.snap : b;
            if (s >= total())
                return;
            const double p = scaleSnap ((double) geo.pitchAt (y));
            auto r = model::addNote (*c, p, std::max (0.0, s), std::min (std::max (1.0, minLen()), total() - s));
            if (r.refused || r.ids.empty())
            {
                toast (doc::ClipDocument::refusalText (r, false, 0));
                return;
            }
            const std::string id = r.ids.front();
            const auto out = host.doc.apply (std::move (r));
            geo.setClip (clip());
            if (! out.accepted)
            {
                toast (out.message);
                return;
            }
            host.doc.setSelection ({ id }, id);
            audition (p);
            Drag d;
            d.t = Drag::Type::end;
            d.id = id;
            d.y0 = y;
            d.v0 = 0.5;
            d.clip0 = clip();
            d.label = "Add note";
            d.force = true;
            drag = std::move (d);
            return;
        }
        Drag d;
        d.t = Drag::Type::box;
        d.x0 = d.x1 = x;
        d.y0 = d.y1 = y;
        d.shift = mods.shift;
        d.ctrl = mods.ctrl;
        drag = std::move (d);
    }

    //==============================================================================================
    // A move (sl-ui.js rollMove): every step is shown and played, none is an undo step.

    void RollGestures::pointerMove (double x, double y, Mods mods)
    {
        if (! drag)
            return;
        sync();
        Drag& d = *drag;
        const double bw = geo.beatWidth();
        const double b = x / bw;
        switch (d.t)
        {
            case Drag::Type::move:
            {
                const double db = snapR (b - d.b0);
                const int dp = geo.pitchAt (y) - d.p0;
                if (! d.moved && (std::abs (b - d.b0) * bw > 5.0 || dp != 0))
                    d.moved = true;
                if (! d.moved)
                    return;
                const double T = total();
                model::Clip next = model::editNotes (*d.clip0, model::Ids (d.ids), [&] (model::Note& n, const model::Note&)
                {
                    for (std::size_t i = 0; i < d.ids.size(); ++i)
                    {
                        if (d.ids[i] != n.id)
                            continue;
                        n.p = clampD (scaleSnap (d.orig[i].second + dp), (double) kMinPitch, (double) kMaxPitch);
                        n.s = clampD (d.orig[i].first + db, 0.0, std::max (0.0, T - n.l));
                        break;
                    }
                });
                const model::Note* h = noteIn (next, d.hid);
                const double hp = h != nullptr ? h->p : 0.0, hs = h != nullptr ? h->s : 0.0;
                const bool found = h != nullptr;
                show (std::move (next));
                if (found && hp != d.lastP)
                {
                    d.lastP = hp;
                    audition (hp);
                }
                if (found)
                    showReadout (x, y, core::noteName (hp) + " " + kDot + " " + beatLabel (hs, clip()->beatsPerBar));
                break;
            }
            case Drag::Type::end:
            {
                const model::Note* n0 = noteIn (*d.clip0, d.id);
                if (n0 == nullptr)
                    break;
                const double l = std::max (minLen(), std::min (total() - n0->s, snapR (b) - n0->s));
                model::NotePatch patch;
                patch.l = l;
                model::Clip next = model::updateNotes (*d.clip0, d.id, patch);
                const auto nd = bendEndNodes (*n0, false, y, d.y0, d.v0, mods.shift);
                if (nd)
                    next = model::setLaneNodes (next, d.id, core::LaneId::pitch, *nd);
                const auto ctx = formatCtx (n0);
                show (std::move (next));
                showReadout (x, y, "Length " + fmtBeats (l) + (nd ? " " + kDot + " end " + laneReadout (core::LaneId::pitch, nd->back().v, ctx) : std::string()));
                break;
            }
            case Drag::Type::start:
            {
                const model::Note* n0 = noteIn (*d.clip0, d.id);
                if (n0 == nullptr)
                    break;
                const double ns = clampD (snapR (b), 0.0, d.endBeat - minLen());
                model::NotePatch patch;
                patch.s = ns;
                patch.l = d.endBeat - ns;
                model::Clip next = model::updateNotes (*d.clip0, d.id, patch);
                const auto nd = bendEndNodes (*n0, true, y, d.y0, d.v0, mods.shift);
                if (nd)
                    next = model::setLaneNodes (next, d.id, core::LaneId::pitch, *nd);
                const auto ctx = formatCtx (n0);
                show (std::move (next));
                showReadout (x, y, beatLabel (ns, clip()->beatsPerBar)
                                       + (nd ? " " + kDot + " start " + laneReadout (core::LaneId::pitch, nd->front().v, ctx) : std::string()));
                break;
            }
            case Drag::Type::node:
            {
                const model::Note* n = noteIn (*d.clip0, d.id);
                if (n == nullptr)
                    break;
                if (! d.moved && d.hasDeadZone && jsHypot (x - d.x0, y - d.y0) < 3.0)
                    break;
                const double nx = x - d.gx, ny = y - d.gy;
                model::NodeList nd = d.nodes0;
                const int k = d.k, last = (int) nd.size() - 1;
                const double x0 = n->s * bw, x1 = (n->s + n->l) * bw;
                double t = (nx - x0) / std::max (1.0, x1 - x0);
                t = k == 0 ? 0.0 : k == last ? 1.0 : clampD (t, nd[(std::size_t) k - 1].t + 0.004, nd[(std::size_t) k + 1].t - 0.004);
                double v = clampD (geo.laneValue (*n, d.lane, t, ny), 0.0, 1.0);
                if (d.lane == core::LaneId::pitch && ! mods.shift)
                {
                    v = clampD (0.5 + core::jsRound ((v - 0.5) * 24.0) / 24.0, 0.0, 1.0);   // semitones
                }
                else if (d.lane == core::LaneId::travel && n->sample.travel != model::TravelKind::path && ! mods.shift)
                {
                    for (const double s : { 0.25, 0.5, 0.75 })   // 1/8x backwards, stopped, 1x forwards
                        if (std::abs (v - s) < 0.012)
                            v = s;
                }
                nd[(std::size_t) k].t = t;
                nd[(std::size_t) k].v = v;
                const auto ctx = formatCtx (n);
                show (model::setLaneNodes (*d.clip0, d.id, d.lane, nd));
                d.moved = true;
                showReadout (x, y, laneReadout (d.lane, v, ctx));
                break;
            }
            case Drag::Type::seg:
            {
                model::NodeList nd = d.nodes0;
                const auto k = (std::size_t) d.k;
                const double sgn = signOr1 (nd[k + 1].v - nd[k].v);
                const double bend = clampD (d.b0 + (y - d.y0) / 60.0 * sgn, -1.0, 1.0);
                nd[k].shape = model::NodeShape::bend;
                nd[k].bend = bend;
                show (model::setLaneNodes (*d.clip0, d.id, d.lane, nd));
                d.moved = true;
                showReadout (x, y, "Curve " + percent (bend));
                break;
            }
            case Drag::Type::free:
                paint (x, y);
                break;
            case Drag::Type::gstart:
            {
                const model::Note* n = noteIn (*d.clip0, d.id);
                if (n == nullptr)
                    break;
                const double len = clampD (n->s + n->l - snapR (b), 0.0625, n->l * 0.95);
                model::NotePatch patch;
                patch.glide = std::optional<double> (len);
                show (model::updateNotes (*d.clip0, d.id, patch));
                d.moved = true;
                showReadout (x, y, "Slide " + fmtBeats (len));
                break;
            }
            case Drag::Type::gmid:
            {
                const double gc = clampD (d.c0 + (d.y0 - y) / 60.0 * signOr1 (d.d), -1.0, 1.0);
                model::NotePatch patch;
                patch.gc = gc;
                show (model::updateNotes (*d.clip0, d.id, patch));
                d.moved = true;
                showReadout (x, y, "Slide curve " + percent (gc));
                break;
            }
            case Drag::Type::box:
            {
                d.x1 = x;
                d.y1 = y;
                if (! d.moved && jsHypot (x - d.x0, y - d.y0) > 4.0)
                    d.moved = true;
                // what the box selects is M5's (BoxSelect)
                break;
            }
        }
    }

    //==============================================================================================
    // The release (sl-ui.js rollUp): one undo step for a gesture that changed the clip, none for one that did not.

    void RollGestures::pointerUp()
    {
        shown.reset();
        if (! drag)
            return;
        sync();
        Drag d = std::move (*drag);
        drag.reset();

        if (d.t == Drag::Type::box)
        {
            if (! d.moved && d.clickId)
            {
                bandClick (*d.clickId, d.clickLink, d.clickAdd);
                return;
            }
            if (! d.moved && ! d.shift && ! d.ctrl)
            {
                host.doc.setSelection ({}, std::string());
                host.doc.setActiveNode (std::nullopt);
            }
            return;
        }
        if (d.t == Drag::Type::free)
        {
            // the painted line becomes nodes (Ramer-Douglas-Peucker at 0.012), every segment smooth
            if (const auto n = noteById (d.id))
            {
                model::NodeList pts = model::rdp (model::noteLane (*n, d.lane), 0.012);
                for (auto& q : pts)
                    q.shape = model::NodeShape::smooth;
                show (model::setLaneNodes (*clip(), d.id, d.lane, pts));
            }
        }
        if (d.t == Drag::Type::move && ! d.moved)
            return;
        const model::ClipPtr now = clip();
        // a drag that ended where it started changes nothing: no undo step, and the values go back exactly
        if (! d.force && now != d.clip0 && nearlySameClip (*now, *d.clip0))
        {
            show (*d.clip0);
            return;
        }
        if ((now != d.clip0 || d.force) && host.commit)
            host.commit (d.label.empty() ? std::string (dragLabel ((int) d.t)) : d.label);
    }

    //==============================================================================================
    bool RollGestures::pickNote (const model::Note& h, bool add)
    {
        const doc::RollState st = roll();
        const std::string id = h.id;
        if (add)
        {
            if (st.isSelected (id) && id != st.primary)
            {
                auto s = st.selected;
                s.erase (std::remove (s.begin(), s.end(), id), s.end());
                host.doc.setSelection (std::move (s));
                return false;
            }
            auto s = st.selected;
            s.push_back (id);
            host.doc.setSelection (std::move (s), id);
        }
        else if (! st.isSelected (id))
        {
            host.doc.setSelection ({ id }, id);
        }
        else
        {
            host.doc.setPrimary (id);
        }
        return true;
    }

    void RollGestures::linkNotes (const model::Note& P, const model::Note& h)
    {
        // the earlier note slides into the later one; again = the link goes
        const bool hLater = h.s >= P.s;
        const model::Note& a = hLater ? P : h;
        const model::Note& z = hLater ? h : P;
        const std::string aId = a.id, zId = z.id;
        model::NotePatch patch;
        if (a.glideTo == zId)
        {
            patch.glideTo = std::string();
            toast ("Slide removed");
        }
        else
        {
            patch.glideTo = zId;
            toast (core::noteName (a.p) + " now slides into " + core::noteName (z.p));
        }
        linkArm = false;
        const model::ClipPtr c = clip();
        setClipLabelled (model::updateNotes (*c, aId, patch), "Slide into");
        host.doc.setSelection ({ zId }, zId);
    }

    void RollGestures::bandClick (const std::string& id, bool link, bool add)
    {
        const auto h = noteById (id);
        const auto P = primaryNote();
        if (h == nullptr)
            return;
        if (link && P != nullptr && h->id != P->id)
            linkNotes (*P, *h);
        else
            pickNote (*h, add);
    }

    void RollGestures::addNodeAt (const model::Note& n, core::LaneId lane, double x, double y, bool onLine)
    {
        const double bw = geo.beatWidth();
        const double x0 = n.s * bw, x1 = (n.s + n.l) * bw;
        const double t = clampD ((x - x0) / std::max (1.0, x1 - x0), 0.001, 0.999);
        const model::NodeList& src = model::noteLane (n, lane);
        model::NodeList nd = src;
        int k = -1;
        for (int i = 0; i < (int) nd.size(); ++i)
            if (nd[(std::size_t) i].t > t)
            {
                k = i;
                break;
            }
        if (k <= 0)
            k = (int) nd.size() - 1;
        const model::Node prev = nd[(std::size_t) k - 1];
        const double v = onLine ? model::evalNodes (src, t) : geo.laneValue (n, lane, t, y);
        const auto shape = prev.shape == model::NodeShape::step ? model::NodeShape::step
                         : prev.shape == model::NodeShape::linear ? model::NodeShape::linear : model::NodeShape::smooth;
        nd.insert (nd.begin() + k, model::mkNode (t, clampD (v, 0.0, 1.0), shape, 0.0));
        const std::string id = n.id;
        show (model::setLaneNodes (*clip(), id, lane, nd));
        host.doc.setActiveNode (doc::ActiveNode { id, lane, k, 0 });
        Drag d;
        d.t = Drag::Type::node;
        d.id = id;
        d.lane = lane;
        d.k = k;
        d.clip0 = clip();
        if (const model::Note* m = noteIn (*d.clip0, id))
            d.nodes0 = model::noteLane (*m, lane);
        else
            d.nodes0 = nd;
        d.label = "Add node";
        d.force = true;
        drag = std::move (d);
    }

    void RollGestures::paint (double x, double y)
    {
        Drag& d = *drag;
        const model::Note* n = noteIn (*d.clip0, d.id);
        if (n == nullptr || d.arr.size() < 2)
            return;
        const double bw = geo.beatWidth();
        const double x0 = n->s * bw, x1 = (n->s + n->l) * bw;
        const int M2 = (int) d.arr.size();
        const double fi = core::jsRound ((x - x0) / (x1 - x0) * (double) (M2 - 1));
        const int i = std::isnan (fi) ? 0 : (int) clampD (fi, 0.0, (double) (M2 - 1));
        const double v = clampD (geo.laneValue (*n, d.lane, (double) i / (double) (M2 - 1), y), 0.0, 1.0);
        if (d.li < 0)
        {
            d.arr[(std::size_t) i] = v;
        }
        else
        {
            const int a = d.li, step = a < i ? 1 : -1;
            for (int j = a; j != i + step; j += step)
            {
                const double t = a == i ? 1.0 : (double) (j - a) / (double) (i - a);
                d.arr[(std::size_t) j] = d.lv + (v - d.lv) * t;
            }
        }
        d.li = i;
        d.lv = v;
        model::NodeList nd;
        nd.reserve (d.arr.size());
        for (int j = 0; j < M2; ++j)
            nd.push_back (model::mkNode ((double) j / (double) (M2 - 1), d.arr[(std::size_t) j], model::NodeShape::linear, 0.0));
        show (model::setLaneNodes (*d.clip0, d.id, d.lane, nd));
    }

    std::optional<model::NodeList> RollGestures::bendEndNodes (const model::Note& n, bool start, double y, double y0, double v0, bool shift) const
    {
        const double rh = geo.rowHeight();
        if (std::abs (y - y0) < rh * 0.4)
            return std::nullopt;
        double semi = (v0 - 0.5) * 24.0 + (y0 - y) / rh;
        if (set.snap > 0.0 && ! shift)
            semi = core::jsRound (semi);
        semi = clampD (semi, -12.0, 12.0);
        model::NodeList nd = model::noteLane (n, core::LaneId::pitch);
        const std::size_t k = start ? 0 : nd.size() - 1;
        nd[k].v = 0.5 + semi / 24.0;
        return nd;
    }

    //==============================================================================================
    // Commands.

    void RollGestures::setMode (int m)
    {
        host.doc.setMode ((double) m);
        const auto st = roll();
        if (st.mode == 1)
            set.free = false;
        if (linkArm && ! (st.mode == 1 || isPitch (st.activeLane())))
            linkArm = false;
    }

    void RollGestures::setSubLane (core::LaneId lane)
    {
        host.doc.setSubLane (lane);
        const auto st = roll();
        if (linkArm && ! (st.mode == 1 || isPitch (st.activeLane())))
            linkArm = false;
    }

    void RollGestures::toggleFreehand()
    {
        if (roll().mode == 1 && ! set.free)
        {
            toast ("Freehand draws a lane: pick a lane mode (2 to 8) first");
            return;
        }
        set.free = ! set.free;
    }

    void RollGestures::armLink()
    {
        const auto st = roll();
        if (! (st.mode == 1 || isPitch (st.activeLane())))
        {
            toast ("Slides are made in Build (1) or Pitch (2)");
            return;
        }
        if (st.primary.empty())
        {
            toast ("Select the note that should slide first");
            return;
        }
        linkArm = ! linkArm;
        if (linkArm)
            toast ("Now click the note it should slide into");
    }

    void RollGestures::duplicateSelection()
    {
        sync();
        const auto ids = selectionInTimeOrder();
        if (ids.empty())
            return;
        auto r = model::duplicateNotes (*clip(), ids);
        if (r.refused)
        {
            toast (doc::ClipDocument::refusalText (r, false, 0));
            return;
        }
        const double T = total();
        std::vector<std::string> keep, gone;
        for (const auto& id : r.ids)
        {
            const model::Note* n = noteIn (r.clip, id);
            (n != nullptr && n->s < T ? keep : gone).push_back (id);
        }
        model::Clip next = model::removeNotes (r.clip, gone);
        next = model::editNotes (next, keep, [T] (model::Note& n, const model::Note&) { n.l = std::min (n.l, T - n.s); });
        setClipLabelled (std::move (next), "Duplicate");
        host.doc.setSelection (keep, keep.empty() ? std::string() : keep.front());
        if (keep.empty())
            toast ("No room after the selection: add bars first");
    }

    void RollGestures::deleteSelection()
    {
        sync();
        const auto ids = selectionInTimeOrder();
        if (ids.empty())
            return;
        setClipLabelled (model::removeNotes (*clip(), ids), ids.size() == 1 ? "Delete note" : "Delete notes");
        host.doc.setSelection ({}, std::string());
    }

    void RollGestures::selectAll()
    {
        const auto c = clip();
        std::vector<std::string> ids;
        for (const auto& n : c->notes)
            ids.push_back (n->id);
        const auto st = roll();
        const std::string primary = c->notes.empty() ? std::string() : st.primary.empty() ? c->notes.front()->id : st.primary;
        host.doc.setSelection (std::move (ids), primary);
    }

    void RollGestures::clearSelection()
    {
        host.doc.setSelection ({}, std::string());
    }

    void RollGestures::copyLanes()
    {
        const auto n = primaryNote();
        if (n == nullptr)
        {
            toast ("Select a note to copy from");
            return;
        }
        clipboard = model::copyLanes (*n, copyMask);
        const int count = (int) std::count (copyMask.begin(), copyMask.end(), true);
        toast ("Copied " + std::to_string (count) + (count == 1 ? " lane" : " lanes"));
    }

    void RollGestures::pasteLanes()
    {
        if (! clipboard)
        {
            toast ("Nothing copied yet");
            return;
        }
        sync();
        const auto ids = selectionInTimeOrder();
        if (ids.empty())
        {
            toast ("Select the notes to paste onto");
            return;
        }
        setClipLabelled (model::pasteLanes (*clip(), ids, *clipboard), "Paste lanes");
        toast ("Pasted onto " + std::to_string (ids.size()) + (ids.size() == 1 ? " note" : " notes"));
    }

    //==============================================================================================
    // The right-click menu (sl-ui.js's contextmenu handler and openContextMenu).

    std::vector<MenuItem> RollGestures::contextMenu (double x, double y, double timeMs)
    {
        sync();
        const auto c = clip();
        const auto st = roll();
        const auto h = geo.hitNote (*c, st, x, y);
        if (h != nullptr)
        {
            if (! st.isSelected (h->id))
                host.doc.setSelection ({ h->id }, h->id);
            else
                host.doc.setPrimary (h->id);
        }
        menuX = x;
        menuY = y;
        menuTime = timeMs;
        menuNote = h != nullptr ? h->id : std::string();
        return openMenu (x, y, menuNote);
    }

    std::optional<std::vector<MenuItem>> RollGestures::runMenuItem (const MenuItem& item)
    {
        if (item.disabled || item.action < 0 || item.action >= (int) menuActions.size())
            return std::nullopt;
        const auto fn = menuActions[(std::size_t) item.action];   // a copy: the action may open the menu again
        const bool reopen = menuReopens[(std::size_t) item.action];
        sync();
        fn();
        if (reopen)
            return openMenu (menuX, menuY, menuNote);
        return std::nullopt;
    }

    std::vector<MenuItem> RollGestures::shapeItems (core::LaneId lane, const std::vector<std::string>& ids, const std::string& header)
    {
        std::vector<MenuItem> items;
        const auto P = primaryNote();
        const auto kind = lane == core::LaneId::travel && P != nullptr ? P->sample.travel : model::TravelKind::speed;
        const double seed = std::floor (std::fmod (menuTime, 100000.0)) + 1.0;   // the menu's Random seed: (Date.now () % 100000) + 1

        auto action = [this] (std::function<void()> fn, bool reopen)
        {
            menuActions.push_back (std::move (fn));
            menuReopens.push_back (reopen);
            return (int) menuActions.size() - 1;
        };

        MenuItem h;
        h.kind = MenuItem::Kind::header;
        h.label = header;
        h.colour = lane;
        items.push_back (h);

        MenuItem row;
        row.kind = MenuItem::Kind::row;
        MenuItem each;
        each.label = "Each note";
        each.on = set.scope == model::ShapeScope::each;
        each.keepOpen = true;
        each.action = action ([this] { set.scope = model::ShapeScope::each; }, true);
        MenuItem across;
        across.label = "Across";
        across.on = set.scope == model::ShapeScope::across;
        across.keepOpen = true;
        across.action = action ([this] { set.scope = model::ShapeScope::across; }, true);
        row.row = { each, across };
        items.push_back (row);

        const std::string laneName = core::laneInfo (lane).name;
        for (const auto key : model::shapeKeysFor (lane, kind))
        {
            MenuItem it;
            it.label = model::shapeName (key);
            it.action = action ([this, lane, key, ids, seed, laneName]
            {
                model::ShapeOpts opts;
                opts.seed = seed;
                opts.bpm = set.bpm;
                opts.sampleSeconds = set.sampleSeconds;
                opts.inst = set.inst;
                if (auto r = model::applyShape (*clip(), ids, lane, key, set.scope, opts))
                    setClipLabelled (std::move (*r), std::string (model::shapeName (key)) + " (" + laneName + ")");
            }, false);
            items.push_back (it);
        }
        return items;
    }

    std::vector<MenuItem> RollGestures::openMenu (double x, double y, const std::string& noteId)
    {
        menuActions.clear();
        menuReopens.clear();
        auto action = [this] (std::function<void()> fn)
        {
            menuActions.push_back (std::move (fn));
            menuReopens.push_back (false);
            return (int) menuActions.size() - 1;
        };
        auto header = [] (std::string label)
        {
            MenuItem m;
            m.kind = MenuItem::Kind::header;
            m.label = std::move (label);
            return m;
        };
        auto separator = []
        {
            MenuItem m;
            m.kind = MenuItem::Kind::separator;
            return m;
        };
        auto item = [&action] (std::string label, std::function<void()> fn, bool disabled = false)
        {
            MenuItem m;
            m.label = std::move (label);
            m.disabled = disabled;
            m.action = action (std::move (fn));
            return m;
        };

        const auto st = roll();
        const auto AL = st.activeLane();
        const auto ids = selectionInTimeOrder();
        std::vector<MenuItem> items;
        if (st.mode == 1)
        {
            items.push_back (header ("Build"));
            if (! ids.empty())
            {
                // the prototype starts this label with a link emoji; JUCE's fonts have none
                items.push_back (item ("Slide into the next note I click", [this] { armLink(); }));
                items.push_back (item ("Remove slide links", [this, ids]
                {
                    model::NotePatch patch;
                    patch.glideTo = std::string();
                    setClipLabelled (model::updateNotes (*clip(), ids, patch), "Remove slide");
                }));
                items.push_back (item ("Jump, no slide", [this, ids]
                {
                    model::NotePatch patch;
                    patch.glide = std::optional<double> (0.0);
                    setClipLabelled (model::updateNotes (*clip(), ids, patch), "Slide: jump");
                }));
            }
        }
        else if (! ids.empty() && AL)
        {
            for (auto& m : shapeItems (*AL, ids, std::string (core::laneInfo (*AL).name) + " shape"))
                items.push_back (std::move (m));
        }
        if (! ids.empty())
        {
            items.push_back (separator());
            items.push_back (item ("Copy lanes", [this] { copyLanes(); }));
            items.push_back (item ("Paste lanes", [this] { pasteLanes(); }, ! clipboard.has_value()));
            items.push_back (item ("Duplicate", [this] { duplicateSelection(); }));
            items.push_back (item ("Delete", [this] { deleteSelection(); }));
        }
        if (noteId.empty() && st.mode == 1)
        {
            items.push_back (separator());
            items.push_back (item ("New note here", [this, x, y]
            {
                const double bw = geo.beatWidth();
                const double s = set.snap > 0.0 ? std::floor (x / bw / set.snap) * set.snap : x / bw;
                if (s >= total())
                    return;
                auto r = model::addNote (*clip(), scaleSnap ((double) geo.pitchAt (y)), std::max (0.0, s), std::min (1.0, total() - s));
                if (r.refused || r.ids.empty())
                {
                    toast (doc::ClipDocument::refusalText (r, false, 0));
                    return;
                }
                const std::string id = r.ids.front();
                show (std::move (r));
                if (host.commit)
                    host.commit ("Add note");
                host.doc.setSelection ({ id }, id);
            }));
        }
        if (items.empty())
            items.push_back (header ("Select a note first"));
        return items;
    }
}
