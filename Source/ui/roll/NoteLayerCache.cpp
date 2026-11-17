#include "NoteLayerCache.h"

#include "model/Glides.h"

#include <cmath>
#include <unordered_set>
#include <utility>

namespace sl::ui::roll
{
    bool NoteLayerCache::Key::operator== (const Key& o) const noexcept
    {
        using juce::exactlyEqual;
        return exactlyEqual (viewWidth, o.viewWidth) && exactlyEqual (viewHeight, o.viewHeight)
            && exactlyEqual (scrollX, o.scrollX) && exactlyEqual (scrollY, o.scrollY)
            && exactlyEqual (beatWidth, o.beatWidth) && exactlyEqual (rowHeight, o.rowHeight)
            && theme == o.theme && mode == o.mode && lane == o.lane && dimRefresh == o.dimRefresh
            && exactlyEqual (bpm, o.bpm) && exactlyEqual (vibRate, o.vibRate) && exactlyEqual (glideTime, o.glideTime)
            && glideMode == o.glideMode;
    }

    NoteLayerCache::Key NoteLayerCache::keyOf (const PaintState& st)
    {
        Key k;
        if (st.geo != nullptr)
        {
            const auto& geo = *st.geo;
            k.viewWidth = geo.viewWidth();
            k.viewHeight = geo.viewHeight();
            k.scrollX = geo.scrollX();
            k.scrollY = geo.scrollY();
            k.beatWidth = geo.beatWidth();
            k.rowHeight = geo.rowHeight();
            k.bpm = geo.getBpm();
            k.vibRate = geo.getInst().vibRate;
            k.glideTime = geo.getInst().glideTime;
            k.glideMode = (int) geo.getInst().glideMode;
        }
        k.theme = st.colours != nullptr ? (int) st.colours->id : -1;
        k.mode = st.roll.mode;
        const auto lane = st.roll.activeLane();
        k.lane = lane.has_value() ? core::laneIndex (*lane) : -1;
        k.dimRefresh = st.inst.refresh.mode == core::RefreshMode::smooth;
        return k;
    }

    NoteLayerCache::Shown NoteLayerCache::record (const PaintState& st, const model::NotePtr& n, int order)
    {
        Shown r;
        r.note = n;
        r.selected = st.isSelected (n->id);
        r.primary = st.isPrimary (n->id);
        if (const auto* g = st.geo->glideMap().find (n->id))
        {
            r.glide = true;
            r.glideD = g->d;
            r.glideLen = g->len;
            r.glideCurve = g->curve;
        }
        r.order = order;
        return r;
    }

    void NoteLayerCache::took (const PaintState& st)
    {
        shownClip = st.clip;
        shownSelection = st.roll.selected;
        shownPrimary = st.roll.primary;
    }

    //==============================================================================================
    NoteLayerCache::Change NoteLayerCache::noteChanges (const PaintState& st)
    {
        Change out;
        if (needsFull || ! haveShown || st.clip == nullptr || st.geo == nullptr)
        {
            needsFull = true;
            out.all = true;
            return out;
        }
        // the same clip version and the same selection: nothing a note draws changed (the slides follow the clip
        // and the slide settings, which are in the Key)
        if (st.clip == shownClip && st.roll.selected == shownSelection && st.roll.primary == shownPrimary)
            return out;

        // The notes whose drawing differs from what the image shows, and whether the others kept their order.
        const auto& notes = st.clip->notes;
        std::vector<std::pair<int, const Shown*>> changed;   // (index in the clip, what was shown, or null)
        std::unordered_set<std::string> present;
        present.reserve (notes.size());
        int lastOrder = -1;
        bool inOrder = true;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(std::size_t) i];
            if (! present.insert (n->id).second)
            {
                inOrder = false;   // two notes with one id: draw everything again
                break;
            }
            const Shown now = record (st, n, i);
            const auto it = shown.find (n->id);
            const Shown* was = it != shown.end() ? &it->second : nullptr;
            if (was != nullptr && was->note == n && was->selected == now.selected && was->primary == now.primary
                && was->glide == now.glide && juce::exactlyEqual (was->glideD, now.glideD)
                && juce::exactlyEqual (was->glideLen, now.glideLen) && juce::exactlyEqual (was->glideCurve, now.glideCurve))
            {
                inOrder = inOrder && was->order > lastOrder;
                lastOrder = was->order;
                continue;
            }
            changed.emplace_back (i, was);
        }
        std::vector<std::string> gone;
        if (inOrder)
            for (const auto& entry : shown)
                if (present.count (entry.first) == 0)
                    gone.push_back (entry.first);

        out.notes = (int) (changed.size() + gone.size());
        if (! inOrder || out.notes > maxChangedNotes)
        {
            needsFull = true;
            out.all = true;
            return out;
        }

        // Each changed note's old bounds and new bounds (one rectangle when they overlap).
        std::vector<std::pair<std::string, Shown>> updates;
        for (const auto& [i, was] : changed)
        {
            const auto& n = notes[(std::size_t) i];
            Shown now = record (st, n, i);
            now.bounds = RollPainter::noteBoundsIn (*st.geo, *n);
            const auto& before = was != nullptr ? was->bounds : std::optional<juce::Rectangle<double>>();
            if (before && before->intersects (*now.bounds))
                out.rects.push_back (before->getUnion (*now.bounds));
            else
            {
                if (before)
                    out.rects.push_back (*before);
                out.rects.push_back (*now.bounds);
            }
            updates.emplace_back (n->id, std::move (now));
        }
        for (const auto& id : gone)
        {
            const auto it = shown.find (id);
            if (it != shown.end())
            {
                if (it->second.bounds)
                    out.rects.push_back (*it->second.bounds);
                shown.erase (it);
            }
        }
        for (auto& [id, now] : updates)
            shown[id] = std::move (now);
        for (int i = 0; i < (int) notes.size(); ++i)
            shown[notes[(std::size_t) i]->id].order = i;

        took (st);
        pending.insert (pending.end(), out.rects.begin(), out.rects.end());
        return out;
    }

    //==============================================================================================
    void NoteLayerCache::paint (juce::Graphics& g, const PaintState& st, juce::Rectangle<double> visible)
    {
        if (st.geo == nullptr || st.colours == nullptr)
            return;
        auto& context = g.getInternalContext();
        const float sc = context.getPhysicalPixelScaleFactor();
        if (! std::isfinite (sc) || ! (sc > 0.0f))
            return;
        const int w = (int) std::ceil (visible.getWidth() * (double) sc - 1e-6);
        const int h = (int) std::ceil (visible.getHeight() * (double) sc - 1e-6);
        if (w <= 0 || h <= 0)
            return;

        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const auto type = context.getPreferredImageTypeForTemporaryImages();
        const int typeId = type != nullptr ? type->getTypeID() : -1;
        bool full = needsFull || ! juce::exactlyEqual (sc, scale) || keyOf (st) != key;
        if (! image.isValid() || image.getWidth() != w || image.getHeight() != h || typeId != imageType)
        {
            image = type != nullptr ? juce::Image (juce::Image::ARGB, w, h, true, *type) : juce::Image (juce::Image::ARGB, w, h, true);
            image.setBackupEnabled (false);
            imageType = typeId;
            full = true;
        }
        // a graphics device that went away took the image's pixels with it (as JUCE's cached component images)
        if (auto data = image.getPixelData())
            if (const auto* backup = data->getBackupExtensions())
                if (backup->needsBackup() && ! backup->canBackup())
                    full = true;

        if (full)
        {
            scale = sc;
            key = keyOf (st);
            render (st, visible, true);
            needsFull = false;
            pending.clear();
        }
        else if (! pending.empty())
        {
            render (st, visible, false);
            pending.clear();
        }
        else
        {
            stats = Stats();
        }
        stats.ms = juce::Time::getMillisecondCounterHiRes() - t0;

        // one pixel of the image to one device pixel, no resampling
        g.setColour (juce::Colours::black);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale));
    }

    void NoteLayerCache::render (const PaintState& st, juce::Rectangle<double> visible, bool full)
    {
        const RollPainter painter (st);
        const auto vis = painter.visibleNotes (visible);
        const double sx = visible.getX(), sy = visible.getY(), s = (double) scale;
        const auto toImage = juce::AffineTransform::translation ((float) -sx, (float) -sy).scaled (scale);
        stats = Stats();
        stats.full = full;

        if (full)
        {
            image.clear (image.getBounds());
            {
                juce::Graphics g (image);
                g.addTransform (toImage);
                for (const auto& n : vis)
                    painter.paintNote (g, *n, st.isSelected (n->id));
            }
            stats.notesDrawn = (int) vis.size();
            stats.pixels = (double) image.getWidth() * (double) image.getHeight();

            // what the image shows now: every note of the clip, with the bounds of those it drew
            shown.clear();
            if (st.clip != nullptr)
            {
                shown.reserve (st.clip->notes.size());
                for (int i = 0; i < (int) st.clip->notes.size(); ++i)
                {
                    const auto& n = st.clip->notes[(std::size_t) i];
                    shown[n->id] = record (st, n, i);
                }
            }
            for (const auto& n : vis)
                shown[n->id].bounds = painter.noteBounds (*n);
            haveShown = true;
            took (st);
            return;
        }

        // the marked rectangles in device pixels, without overlaps
        juce::RectangleList<int> area;
        for (const auto& r : pending)
        {
            const auto d = juce::Rectangle<double> ((r.getX() - sx) * s, (r.getY() - sy) * s, r.getWidth() * s, r.getHeight() * s);
            if (! std::isfinite (d.getX()) || ! std::isfinite (d.getY()) || ! std::isfinite (d.getRight()) || ! std::isfinite (d.getBottom()))
                continue;
            const auto clipped = d.getIntersection (image.getBounds().toDouble());
            if (! clipped.isEmpty())
                area.add (clipped.getSmallestIntegerContainer().getIntersection (image.getBounds()));
        }
        area.consolidate();

        // Each rectangle is drawn on its own, in a patch of the layer's image type the rectangle's size, with no
        // clip, then put in place of what the layer showed there (cleared, then the patch drawn at a whole-pixel
        // offset). Drawing into the layer through a clip would be shorter, but Direct2D draws a clip's edge rows
        // differently from the same rows drawn unclipped (CI run 44195077235: 111 pixels along the edges), and
        // the patch is the same drawing as a full redraw, moved by whole pixels.
        const auto patchType = image.getPixelData()->createType();
        for (const auto& r : area)
        {
            const auto inRoll = juce::Rectangle<double> ((double) r.getX() / s + sx, (double) r.getY() / s + sy,
                                                         (double) r.getWidth() / s, (double) r.getHeight() / s);
            juce::Image patch (juce::Image::ARGB, r.getWidth(), r.getHeight(), true, *patchType);
            {
                juce::Graphics pg (patch);
                pg.addTransform (toImage.translated ((float) -r.getX(), (float) -r.getY()));
                for (const auto& n : vis)
                    if (painter.noteBounds (*n).intersects (inRoll))
                    {
                        painter.paintNote (pg, *n, st.isSelected (n->id));
                        ++stats.notesDrawn;
                    }
            }
            image.clear (r);
            {
                juce::Graphics g (image);
                g.drawImageAt (patch, r.getX(), r.getY());
            }
            ++stats.rects;
            stats.pixels += (double) r.getWidth() * (double) r.getHeight();
        }
    }
}
