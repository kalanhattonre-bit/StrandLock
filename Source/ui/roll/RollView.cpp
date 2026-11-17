#include "RollView.h"

#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "doc/CommandQueue.h"
#include "engine/EngineConfig.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        constexpr double kAuditionMs = 220.0;       // sl-ui.js audition: a note a gesture plays stops 220 ms later
        constexpr double kAuditionVelocity = 0.7;   // ... and plays at 0.7
        constexpr double kPianoVelocity = 0.8;      // a piano key (sl-ui.js liveOn (..., 0.8))

        const char* const kZoomTips[] = { "Zoom out in time", "Zoom in on time", "Smaller rows", "Taller rows" };

        juce::String zoomText (int i)
        {
            // the prototype's H-, H+, V-, V+ (its minus is U+2212)
            const auto minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
            return juce::String (i < 2 ? "H" : "V") + (i % 2 == 0 ? minus : juce::String ("+"));
        }

        bool sameNode (const std::optional<doc::ActiveNode>& a, const std::optional<doc::ActiveNode>& b)
        {
            return a.has_value() == b.has_value() && (! a || (a->id == b->id && a->lane == b->lane && a->k == b->k));
        }

        bool sameReadout (const std::optional<Readout>& a, const std::optional<Readout>& b)
        {
            return a.has_value() == b.has_value()
                && (! a || (juce::exactlyEqual (a->x, b->x) && juce::exactlyEqual (a->y, b->y) && a->text == b->text));
        }

        bool sameBox (const std::optional<RollGestures::Box>& a, const std::optional<RollGestures::Box>& b)
        {
            return a.has_value() == b.has_value()
                && (! a || (juce::exactlyEqual (a->x0, b->x0) && juce::exactlyEqual (a->y0, b->y0)
                            && juce::exactlyEqual (a->x1, b->x1) && juce::exactlyEqual (a->y1, b->y1)));
        }

        /** The two states draw the same links, handles, ghost, box and readout (at the same zoom and scroll). */
        bool sameOverlay (const PaintState& a, const PaintState& b)
        {
            return a.clip == b.clip && a.roll.selected == b.roll.selected && a.roll.primary == b.roll.primary
                && a.roll.mode == b.roll.mode && a.roll.subLane == b.roll.subLane && sameNode (a.roll.activeNode, b.roll.activeNode)
                && a.free == b.free && a.ghost == b.ghost && sameReadout (a.readout, b.readout) && sameBox (a.box, b.box);
        }

        bool isGrid (double beats) noexcept
        {
            // the prototype's Grid menu: 1/4, 1/8, 1/16, 1/32, Free
            for (const double g : { 1.0, 0.5, 0.25, 0.125, 0.0 })
                if (juce::exactlyEqual (beats, g))
                    return true;
            return false;
        }
    }

    //==============================================================================================
    /** The roll between the ruler, the keys and the scroll bars: RollView paints it. R3 wires its mouse. */
    class RollView::Canvas final : public juce::Component
    {
    public:
        explicit Canvas (RollView& v) : view (v)
        {
            setOpaque (true);
        }

        void paint (juce::Graphics& g) override { view.paintCanvas (g); }

    private:
        RollView& view;
    };

    //==============================================================================================
    bool RollView::ViewKey::operator== (const ViewKey& o) const noexcept
    {
        using juce::exactlyEqual;
        return exactlyEqual (viewWidth, o.viewWidth) && exactlyEqual (viewHeight, o.viewHeight)
            && exactlyEqual (scrollX, o.scrollX) && exactlyEqual (scrollY, o.scrollY)
            && exactlyEqual (beatWidth, o.beatWidth) && exactlyEqual (rowHeight, o.rowHeight)
            && mode == o.mode && lane == o.lane && exactlyEqual (snap, o.snap) && key == o.key && scale == o.scale
            && refreshMode == o.refreshMode && glideMode == o.glideMode && exactlyEqual (glideTime, o.glideTime)
            && exactlyEqual (vibRate, o.vibRate) && exactlyEqual (bpm, o.bpm) && bars == o.bars && beatsPerBar == o.beatsPerBar;
    }

    RollHost RollView::makeHost()
    {
        return RollHost { processor.getDocument(),
                          [this] (const std::string& label) { return processor.pushUndoStep (label, {}); },
                          [this] (int pitch) { audition (pitch); },
                          [this] (const std::string& text)
                          {
                              if (onToast)
                                  onToast (juce::String::fromUTF8 (text.c_str()));
                          } };
    }

    RollView::RollView (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          gestures (std::make_unique<RollGestures> (makeHost())),
          canvas (std::make_unique<Canvas> (*this)),
          ruler (t, gestures->geometry()),
          piano (t, gestures->geometry())
    {
        // The Grid and Each note / Across as the project saved them (R3's controls change them).
        const auto ui = processor.getUiState();
        auto& set = gestures->settings();
        set.snap = isGrid ((double) ui.gridBeats) ? (double) ui.gridBeats : 0.25;
        set.scope = ui.scope == 1 ? model::ShapeScope::across : model::ShapeScope::each;

        addAndMakeVisible (*canvas);
        addAndMakeVisible (ruler);
        addAndMakeVisible (piano);
        for (auto* bar : { &hbar, &vbar })
        {
            bar->setAutoHide (false);
            bar->addListener (this);
            addAndMakeVisible (*bar);
        }
        for (int i = 0; i < 4; ++i)
        {
            auto b = std::make_unique<FlatButton> (t, zoomText (i));
            b->setFontPx (10.5f);
            b->setTooltip (kZoomTips[i]);
            b->onClick = [this, i] { zoomBy (i == 0 ? 1.0 / 1.3 : i == 1 ? 1.3 : 1.0, i == 2 ? 0.8 : i == 3 ? 1.25 : 1.0); };
            addAndMakeVisible (*b);
            zoomButtons[(std::size_t) i] = std::move (b);
        }

        // a key pressed on the piano plays until the mouse lets go (sl-ui.js liveOn / liveOff)
        piano.onPress = [this] (int pitch) { processor.getCommandQueue().push (doc::UiCommand::auditionOn (pitch, kPianoVelocity)); };
        piano.onRelease = [this] (int pitch) { processor.getCommandQueue().push (doc::UiCommand::auditionOff (pitch)); };

        refresh();
    }

    RollView::~RollView()
    {
        stopTimer();
        hbar.removeListener (this);
        vbar.removeListener (this);
        // nothing keeps sounding after the window closes
        piano.release();
        for (const auto& o : pendingOffs)
            processor.getCommandQueue().push (doc::UiCommand::auditionOff (o.pitch));
    }

    juce::Component& RollView::getCanvas() noexcept
    {
        return *canvas;
    }

    juce::Rectangle<int> RollView::getCanvasArea() const noexcept
    {
        return canvas->getBounds();
    }

    //==============================================================================================
    // Refresh: the document and the view, taken once; repaint what changed.

    void RollView::refresh()
    {
        auto& set = gestures->settings();
        set.inst = processor.readInst();
        set.bpm = hostBpm.value_or (120.0);
        if (const auto sound = processor.getCurrentSound())
            if (sound->seconds() > 0.0)
                set.sampleSeconds = sound->seconds();
        gestures->sync();
        const auto& geo = gestures->geometry();

        PaintState next;
        next.colours = &theme.colours();
        next.geo = &geo;
        next.clip = geo.getClip();
        next.roll = processor.getDocument().rollState();
        next.inst = set.inst;
        next.free = set.free;
        next.snap = set.snap;
        next.readout = gestures->readout();
        next.box = gestures->box();
        if (ghost != nullptr && ghostGeo != nullptr)
        {
            ghostGeo->setInst (set.inst);
            ghostGeo->setBpm (set.bpm);
            ghostGeo->setZoom (geo.beatWidth(), geo.rowHeight());
            next.ghost = ghost;
            next.ghostGeo = ghostGeo.get();
        }
        next.prepare();
        const PaintState prev = std::move (state);
        state = std::move (next);

        piano.setScale (state.inst.key, (int) state.inst.scale);
        if (firstView && geo.viewWidth() > 0.0 && geo.viewHeight() > 0.0)
            applyFirstView();

        // the notes: what the cache says changed (it also marks them for the next paint)
        lastChange = cache.noteChanges (state);
        const auto& change = lastChange;

        const auto k = viewKey();
        if (! haveKey || k != lastKey)
        {
            // the view, the zoom, the mode, the scale, the tempo, the slide settings or the clip's length changed
            haveKey = true;
            lastKey = k;
            updateScrollBars();
            repaintAll();
            overlay = RollPainter (state).overlayBounds (visible());
            return;
        }

        if (change.all)
            repaintCanvasArea (canvas->getLocalBounds());
        else
            for (const auto& r : change.rects)
                repaintRoll (r);

        // the links, the handles, the ghost, the box and the readout: where they were and where they are now
        if (! sameOverlay (prev, state))
        {
            auto now = RollPainter (state).overlayBounds (visible());
            for (const auto& r : overlay)
                repaintRoll (r);
            for (const auto& r : now)
                repaintRoll (r);
            overlay = std::move (now);
        }
    }

    RollView::ViewKey RollView::viewKey() const
    {
        const auto& geo = getGeometry();
        ViewKey k;
        k.viewWidth = geo.viewWidth();
        k.viewHeight = geo.viewHeight();
        k.scrollX = geo.scrollX();
        k.scrollY = geo.scrollY();
        k.beatWidth = geo.beatWidth();
        k.rowHeight = geo.rowHeight();
        k.mode = state.roll.mode;
        const auto lane = state.roll.activeLane();
        k.lane = lane.has_value() ? core::laneIndex (*lane) : -1;
        k.snap = state.snap;
        k.key = state.inst.key;
        k.scale = (int) state.inst.scale;
        k.refreshMode = (int) state.inst.refresh.mode;
        k.glideMode = (int) state.inst.glideMode;
        k.glideTime = state.inst.glideTime;
        k.vibRate = state.inst.vibRate;
        k.bpm = geo.getBpm();
        k.bars = state.clip != nullptr ? state.clip->bars : 0;
        k.beatsPerBar = state.clip != nullptr ? state.clip->beatsPerBar : 0;
        return k;
    }

    juce::Rectangle<double> RollView::visible() const
    {
        const auto& geo = getGeometry();
        return { geo.scrollX(), geo.scrollY(), geo.viewWidth(), geo.viewHeight() };
    }

    void RollView::repaintAll()
    {
        repaintCanvasArea (canvas->getLocalBounds());
        ruler.repaint();
        piano.repaint();
        repaintLog.add (ruler.getBounds());
        repaintLog.add (piano.getBounds());
    }

    void RollView::repaintRoll (juce::Rectangle<double> rollArea)
    {
        const auto& geo = getGeometry();
        const auto v = rollArea.translated (-geo.scrollX(), -geo.scrollY());
        if (! std::isfinite (v.getX()) || ! std::isfinite (v.getY()) || ! std::isfinite (v.getRight()) || ! std::isfinite (v.getBottom()))
            return;
        const auto clipped = v.getIntersection (canvas->getLocalBounds().toDouble());
        if (! clipped.isEmpty())
            repaintCanvasArea (clipped.getSmallestIntegerContainer().expanded (1));
    }

    void RollView::repaintCanvasArea (juce::Rectangle<int> area)
    {
        area = area.getIntersection (canvas->getLocalBounds());
        if (area.isEmpty())
            return;
        canvas->repaint (area);
        repaintLog.add (area + canvas->getPosition());
    }

    juce::RectangleList<int> RollView::takeRepaintLog()
    {
        juce::RectangleList<int> out (repaintLog);
        repaintLog.clear();
        return out;
    }

    void RollView::invalidateAll()
    {
        cache.invalidateAll();
        repaintAll();
    }

    //==============================================================================================
    // What the editor and MainView hand over.

    void RollView::setDisplayFrame (const engine::DisplayFrame* frame)
    {
        std::optional<double> now;
        std::vector<int> live;
        if (frame != nullptr)
        {
            if (frame->playing && std::isfinite (frame->beat))
                now = frame->beat;
            for (int i = 0; i < (int) frame->numVoices && i < engine::kDisplayVoices; ++i)
            {
                const auto& v = frame->voices[i];
                if (v.kind == (std::uint8_t) engine::VoiceKind::live && ! v.released)
                    live.push_back ((int) v.p);
            }
        }
        piano.setLiveKeys (std::move (live));

        const bool same = now.has_value() == playhead.has_value() && (! now || juce::exactlyEqual (*now, *playhead));
        if (same)
            return;
        // the line where it was and where it is now (sl-ui.js movePH: 2 px wide, centred on the beat)
        const auto strip = [this] (std::optional<double> beat)
        {
            if (! beat)
                return;
            const auto& geo = getGeometry();
            const double x = *beat * geo.beatWidth() - geo.scrollX();
            if (std::isfinite (x) && x > -8.0 && x < (double) canvas->getWidth() + 8.0)
                repaintCanvasArea ({ (int) std::floor (x) - 2, 0, 5, canvas->getHeight() });
        };
        strip (playhead);
        playhead = now;
        strip (playhead);
        ruler.setPlayhead (playhead);
    }

    void RollView::setHostBpm (std::optional<double> bpm)
    {
        hostBpm = bpm.has_value() && std::isfinite (*bpm) && *bpm > 0.0 ? bpm : std::nullopt;
    }

    void RollView::setMode (int mode, int subLaneIndex)
    {
        // Mode 8's sub-lane is the document's own (setting it makes the mode 8), so it is set only for mode 8.
        const int m = juce::jlimit (1, 8, mode);
        if (m == 8 && subLaneIndex >= 0 && subLaneIndex < 4)
            gestures->setSubLane (core::kSampleLanes[subLaneIndex]);
        gestures->setMode (m);
        refresh();
    }

    void RollView::setGhost (model::ClipPtr ghostClip)
    {
        if (ghostClip == ghost)
            return;
        ghost = std::move (ghostClip);
        if (ghost != nullptr)
        {
            if (ghostGeo == nullptr)
                ghostGeo = std::make_unique<RollGeometry>();
            ghostGeo->setClip (ghost);
        }
        refresh();
    }

    //==============================================================================================
    // Zoom and scroll.

    void RollView::zoomBy (double h, double v)
    {
        gestures->geometry().zoomBy (h, v);
        viewChanged();
    }

    void RollView::setScroll (double x, double y)
    {
        gestures->geometry().setScroll (x, y);
        viewChanged();
    }

    void RollView::wheel (double deltaX, double deltaY, bool ctrl, bool shift, double viewX, bool overPiano)
    {
        if (overPiano)
        {
            // sl-ui.js: the wheel over the keys scrolls the roll up and down
            scrollBy (0.0, -deltaY * wheelPixels);
            return;
        }
        if (ctrl)
        {
            // Ctrl + the wheel zooms in time; the beat under the pointer stays under it
            if (juce::exactlyEqual (deltaY, 0.0))
                return;
            gestures->geometry().wheelZoom (deltaY > 0.0, viewX);
            viewChanged();
            return;
        }
        // the browser's scroller: up and down, sideways with Shift or a sideways wheel
        if (shift)
            scrollBy (-deltaY * wheelPixels, 0.0);
        else if (std::abs (deltaX) > std::abs (deltaY))
            scrollBy (-deltaX * wheelPixels, 0.0);
        else
            scrollBy (0.0, -deltaY * wheelPixels);
    }

    void RollView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const auto onCanvas = e.getEventRelativeTo (canvas.get());
        wheel ((double) w.deltaX, (double) w.deltaY, e.mods.isCtrlDown() || e.mods.isCommandDown(), e.mods.isShiftDown(),
               (double) onCanvas.position.x, e.originalComponent == &piano);
    }

    void RollView::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
    {
        const auto& geo = getGeometry();
        if (bar == &vbar)
            setScroll (geo.scrollX(), newRangeStart);
        else if (bar == &hbar)
            setScroll (newRangeStart, geo.scrollY());
    }

    void RollView::roundScroll()
    {
        // whole pixels, as a browser keeps a scroller's scroll: the rows and the grid stay sharp
        auto& geo = gestures->geometry();
        geo.setScroll (std::round (geo.scrollX()), std::round (geo.scrollY()));
    }

    void RollView::viewChanged()
    {
        roundScroll();
        saveView();
        refresh();
    }

    void RollView::saveView()
    {
        // UIST keeps the zoom and the scroll with the project
        const auto vs = getGeometry().viewState();
        auto ui = processor.getUiState();
        ui.zoomH = (float) vs.zoomH;
        ui.zoomV = (float) vs.zoomV;
        ui.scrollBeat = vs.scrollBeat;
        ui.scrollRow = (float) vs.scrollRow;
        processor.setUiState (ui);
    }

    void RollView::applyFirstView()
    {
        // The project's saved view, else the prototype's first view: centred on most of the notes. UIST's defaults
        // (zoom 1, scroll 0) cannot say "never saved"; the top left corner at zoom 1 is the top of the roll (C6),
        // which no first view would choose, so those defaults read as "never saved".
        firstView = false;
        auto& geo = gestures->geometry();
        const auto ui = processor.getUiState();
        const bool saved = ! (juce::exactlyEqual (ui.zoomH, 1.0f) && juce::exactlyEqual (ui.zoomV, 1.0f)
                              && juce::exactlyEqual (ui.scrollBeat, 0.0) && juce::exactlyEqual (ui.scrollRow, 0.0f));
        if (saved)
            geo.setViewState ({ (double) ui.zoomH, (double) ui.zoomV, ui.scrollBeat, (double) ui.scrollRow });
        else
            geo.centreOnNotes();
        roundScroll();
        saveView();
    }

    void RollView::updateScrollBars()
    {
        const auto& geo = getGeometry();
        hbar.setRangeLimits (0.0, std::max (geo.contentWidth(), geo.viewWidth()), juce::dontSendNotification);
        hbar.setCurrentRange (geo.scrollX(), geo.viewWidth(), juce::dontSendNotification);
        vbar.setRangeLimits (0.0, std::max (geo.contentHeight(), geo.viewHeight()), juce::dontSendNotification);
        vbar.setCurrentRange (geo.scrollY(), geo.viewHeight(), juce::dontSendNotification);
    }

    //==============================================================================================
    // Auditions from the gestures (a new note, a note moved to another row).

    void RollView::audition (int pitch)
    {
        processor.getCommandQueue().push (doc::UiCommand::auditionOn (pitch, kAuditionVelocity));
        pendingOffs.push_back ({ pitch, juce::Time::getMillisecondCounterHiRes() + kAuditionMs });
        if (! isTimerRunning())
            startTimer (20);
    }

    void RollView::auditionOff (int pitch)
    {
        // a key still held on the piano keeps sounding (sl-ui.js: unless held)
        if (piano.getPressed() != pitch)
            processor.getCommandQueue().push (doc::UiCommand::auditionOff (pitch));
    }

    void RollView::timerCallback()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto it = pendingOffs.begin(); it != pendingOffs.end();)
        {
            if (it->due <= now)
            {
                auditionOff (it->pitch);
                it = pendingOffs.erase (it);
            }
            else
            {
                ++it;
            }
        }
        if (pendingOffs.empty())
            stopTimer();
    }

    //==============================================================================================
    // Layout and painting.

    void RollView::resized()
    {
        const int W = getWidth(), H = getHeight();
        // the ruler, with the zoom buttons at its right end (above the vertical scroll bar)
        const int zoomBox = 4 * zoomButtonWidth + 3 * 2 + 2 * 4;
        const int zoomX = std::max (pianoWidth, W - zoomBox);
        ruler.setBounds (pianoWidth, 0, std::max (0, zoomX - pianoWidth), rulerHeight);
        int x = zoomX + 4;
        for (auto& b : zoomButtons)
        {
            b->setBounds (x, (rulerHeight - zoomButtonHeight) / 2, zoomButtonWidth, zoomButtonHeight);
            x += zoomButtonWidth + 2;
        }
        // the keys down the left, the roll and its scroll bars
        const int cw = std::max (0, W - pianoWidth - scrollBarThickness), ch = std::max (0, H - rulerHeight - scrollBarThickness);
        piano.setBounds (0, rulerHeight, pianoWidth, std::max (0, H - rulerHeight));
        canvas->setBounds (pianoWidth, rulerHeight, cw, ch);
        vbar.setBounds (pianoWidth + cw, rulerHeight, scrollBarThickness, ch);
        hbar.setBounds (pianoWidth, rulerHeight + ch, cw, scrollBarThickness);

        gestures->geometry().setViewSize ((double) cw, (double) ch);
        roundScroll();
        refresh();
    }

    void RollView::paint (juce::Graphics& g)
    {
        // what the children leave: the corner over the keys and the zoom buttons' box (the header colour, lined
        // like the ruler), and the square where the scroll bars meet
        g.fillAll (theme (Tok::roll));
        const int zoomX = ruler.getRight();
        g.setColour (theme (Tok::hdr));
        g.fillRect (0, 0, pianoWidth, rulerHeight);
        g.fillRect (zoomX, 0, getWidth() - zoomX, rulerHeight);
        g.setColour (theme (Tok::line));
        g.fillRect (pianoWidth - 1, 0, 1, rulerHeight);
        g.fillRect (0, rulerHeight - 1, pianoWidth, 1);
        g.fillRect (zoomX, 0, 1, rulerHeight);
        g.fillRect (zoomX, rulerHeight - 1, getWidth() - zoomX, 1);
    }

    void RollView::paintCanvas (juce::Graphics& g)
    {
        g.fillAll (theme (Tok::roll));
        if (state.geo == nullptr)
            return;
        state.colours = &theme.colours();   // the theme may have changed since the refresh
        const auto v = visible();
        const RollPainter painter (state);
        const auto toRoll = juce::AffineTransform::translation ((float) -v.getX(), (float) -v.getY());

        // the rows and the grid, the notes (the cache), then the links, the handles, the ghost and the box
        {
            const juce::Graphics::ScopedSaveState keep (g);
            g.addTransform (toRoll);
            painter.paintRows (g, v);
        }
        cache.paint (g, state, v);
        {
            const juce::Graphics::ScopedSaveState keep (g);
            g.addTransform (toRoll);
            painter.paintOverlay (g);
        }
        painter.paintReadout (g, v);

        // the playhead while Cubase plays (shell.html #ph: 2 px, the playhead colour)
        if (playhead)
        {
            const double x = *playhead * getGeometry().beatWidth() - v.getX();
            if (std::isfinite (x) && x >= -2.0 && x <= (double) canvas->getWidth() + 2.0)
            {
                g.setColour (theme (Tok::playhead));
                g.fillRect (juce::Rectangle<float> ((float) x - 1.0f, 0.0f, 2.0f, (float) canvas->getHeight()));
            }
        }
    }
}
