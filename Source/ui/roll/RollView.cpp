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

        bool samePicks (const std::vector<doc::NodePick>& a, const std::vector<doc::NodePick>& b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (a[i].id != b[i].id || a[i].ks != b[i].ks)
                    return false;
            return true;
        }

        /** The two states draw the same links, handles, ghost, box and readout (at the same zoom and scroll). */
        bool sameOverlay (const PaintState& a, const PaintState& b)
        {
            return a.clip == b.clip && a.roll.selected == b.roll.selected && a.roll.primary == b.roll.primary
                && a.roll.mode == b.roll.mode && a.roll.subLane == b.roll.subLane && sameNode (a.roll.activeNode, b.roll.activeNode)
                && samePicks (a.roll.picks, b.roll.picks) && a.nodes == b.nodes
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
    /** The roll between the ruler, the keys and the scroll bars: RollView paints it, and its mouse goes to the
        view's press, dragTo, release and rightPress (sl-ui.js rollDown, rollMove, rollUp and the contextmenu
        handler). The middle button does nothing, as in the prototype. JUCE ends a drag with mouseUp also when the
        window loses the mouse (a lost capture), so that ends the gesture too. The cursor is the arrow: the
        prototype's roll has no cursor of its own. */
    class RollView::Canvas final : public juce::Component
    {
    public:
        explicit Canvas (RollView& v) : view (v)
        {
            setOpaque (true);
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }

        void paint (juce::Graphics& g) override { view.paintCanvas (g); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                view.rightPress (e.position, timeOf (e));
            else if (e.mods.isLeftButtonDown())
                view.press (e.position, e.mods, timeOf (e));
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isLeftButtonDown())
                view.dragTo (e.position, e.mods);
        }

        void mouseUp (const juce::MouseEvent&) override { view.release(); }

    private:
        /** The event's own time (milliseconds since 1970, as the prototype's Date.now ()). */
        static double timeOf (const juce::MouseEvent& e) { return (double) e.eventTime.toMilliseconds(); }

        RollView& view;
    };

    //==============================================================================================
    /** The right-click menu (sl-ui.js #menu): a panel of StrandLock's own over the page, not a JUCE PopupMenu. A JUCE
        menu is a window that holds every key while it is open: with Keys on, Space ran the item under the mouse (Delete,
        say) and never reached Cubase. This panel takes no key, so with Keys on Space goes on to Cubase and the other
        keys work as they do without it; Escape closes it (MainView::handleKey). A press anywhere else closes it and
        still does what it does there, as in the prototype; a click on an item runs it (fix pass R4T-1, F3, UI4-1).

        It reads the view's items (getMenu) each time it paints, and lays them out as shell.html's #menu: a 6 px
        padding, headers in small capitals (dim, or in a lane's colour), lines, items that light on hover, and the row
        of two buttons (Each note / Across, the lit one in the accent). A menu taller than the page scrolls with the
        wheel. Rings and rounded outlines are paths (R2's Direct2D rule). */
    class RollView::MenuPanel final : public juce::Component
    {
    public:
        explicit MenuPanel (RollView& v) : view (v) {}

        ~MenuPanel() override { listen (false); }

        /** Opens the menu with its top left corner at `where` (the parent's pixels), kept inside the parent. timeMs:
            the press that opened it, which must not close it again. */
        void open (juce::Point<int> where, double timeMs)
        {
            anchor = where;
            openedAt = timeMs;
            scroll = 0;
            hoverIndex = hoverButton = -1;
            place();
            setVisible (true);
            toFront (false);
            listen (true);
            repaint();
        }

        /** The view's items changed while it stays open (Each note / Across): the same place, the new list. */
        void itemsChanged()
        {
            place();
            repaint();
        }

        void close()
        {
            listen (false);
            hoverIndex = hoverButton = -1;
            setVisible (false);
        }

        /** An item's area in this component's pixels (button 0 or 1: one of a row's buttons), for the tests. */
        juce::Rectangle<int> boundsOf (std::size_t index, int button) const
        {
            for (const auto& s : slots)
                if (s.index == index)
                    return button >= 0 ? rowButton (s, button) : slotArea (s);
            return {};
        }

        /** A press somewhere in StrandLock's windows (the desktop's mouse listener): one outside the menu closes it. */
        void pressedSomewhere (const juce::MouseEvent& e)
        {
            if (! isVisible() || e.eventComponent == this || isParentOf (e.eventComponent))
                return;
            // the desktop's listeners also hear the press that opened the menu, after the roll did
            if ((double) e.eventTime.toMilliseconds() <= openedAt)
                return;
            view.closeMenu();
        }

        bool hitTest (int x, int y) override { return box.contains (x, y); }

        void parentSizeChanged() override
        {
            if (isVisible())
                place();
        }

        void paint (juce::Graphics& g) override
        {
            const Theme& T = view.theme;
            const auto b = box.toFloat();
            juce::Path shape;
            shape.addRoundedRectangle (b, kCorner);
            juce::DropShadow (T (Tok::shadow), 18, { 0, 6 }).drawForPath (g, shape);
            g.setColour (T (Tok::menu));
            g.fillPath (shape);
            {
                const juce::Graphics::ScopedSaveState keep (g);
                g.reduceClipRegion (box.reduced (1));
                const auto& items = view.getMenu();
                for (const auto& s : slots)
                {
                    const auto r = slotArea (s);
                    if (s.index < items.size() && r.getBottom() >= box.getY() && r.getY() <= box.getBottom())
                        paintItem (g, items[s.index], s, r);
                }
            }
            if (contentHeight > box.getHeight())
            {
                // a scroller's bar along the right edge: there is more above or below
                const float h = (float) box.getHeight(), thumb = juce::jmax (16.0f, h * h / (float) contentHeight);
                const float y = (float) box.getY() + (h - thumb) * (float) scroll / (float) (contentHeight - box.getHeight());
                juce::Path bar;
                bar.addRoundedRectangle (juce::Rectangle<float> ((float) box.getRight() - 5.0f, y + 2.0f, 3.0f, thumb - 4.0f), 1.5f);
                g.setColour (T (Tok::btnb));
                g.fillPath (bar);
            }
            juce::Path border;
            border.addRoundedRectangle (b.reduced (0.5f), kCorner - 0.5f);
            g.setColour (T (Tok::line));
            g.strokePath (border, juce::PathStrokeType (1.0f));
        }

        void mouseMove (const juce::MouseEvent& e) override { hoverAt (e.getPosition()); }
        void mouseDrag (const juce::MouseEvent& e) override { hoverAt (e.getPosition()); }
        void mouseExit (const juce::MouseEvent&) override { setHover (-1, -1); }

        void mouseUp (const juce::MouseEvent& e) override
        {
            int button = -1;
            const int index = itemAt (e.getPosition(), button);
            const auto& items = view.getMenu();
            if (index < 0 || index >= (int) items.size())
                return;
            const MenuItem m = items[(std::size_t) index];   // a copy: running it changes the list
            if (m.kind == MenuItem::Kind::row)
            {
                if (button >= 0 && button < (int) m.row.size() && ! m.row[(std::size_t) button].disabled)
                    view.chooseMenuItem (m.row[(std::size_t) button]);
            }
            else if (! m.disabled)
            {
                view.chooseMenuItem (m);
            }
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            // the wheel scrolls a tall menu and never the roll under it (CSS overscroll-behavior: contain)
            const int most = juce::jmax (0, contentHeight - box.getHeight());
            const int next = juce::jlimit (0, most, scroll - juce::roundToInt (w.deltaY * 200.0f));
            if (next != scroll)
            {
                scroll = next;
                hoverAt (e.getPosition());
                repaint();
            }
        }

    private:
        struct Slot
        {
            std::size_t index = 0;   // into the view's items
            int top = 0, height = 0; // in the list, before the scroll
        };

        /** Every press in StrandLock's windows (Desktop's global mouse listener): one outside the menu closes it. */
        struct Outside final : public juce::MouseListener
        {
            explicit Outside (MenuPanel& p) : panel (p) {}
            void mouseDown (const juce::MouseEvent& e) override { panel.pressedSomewhere (e); }
            MenuPanel& panel;
        };

        static constexpr int kPad = 6, kHeaderHeight = 22, kSeparatorHeight = 9, kItemHeight = 28, kRowHeight = 34;
        static constexpr int kMinWidth = 190;
        static constexpr int kShadowSide = 18, kShadowTop = 12, kShadowBottom = 24;   // room for the drop shadow
        static constexpr float kCorner = 10.0f;

        static juce::Font itemFont() { return Theme::px (13.0f, true); }
        static juce::Font headerFont() { return Theme::px (10.5f, false, 0.06f); }
        static juce::String text (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
        static int widthOf (const juce::Font& f, const juce::String& s) { return (int) std::ceil (Theme::textWidth (f, s)); }

        void listen (bool on)
        {
            if (on == listening)
                return;
            listening = on;
            if (on)
                juce::Desktop::getInstance().addGlobalMouseListener (&outside);
            else
                juce::Desktop::getInstance().removeGlobalMouseListener (&outside);
        }

        void layout()
        {
            slots.clear();
            const auto& items = view.getMenu();
            int y = 0, widest = kMinWidth;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto& m = items[i];
                int h = kItemHeight, w = 0;
                switch (m.kind)
                {
                    case MenuItem::Kind::header:
                        h = kHeaderHeight;
                        w = widthOf (headerFont(), text (m.label).toUpperCase()) + 16;
                        break;
                    case MenuItem::Kind::separator:
                        h = kSeparatorHeight;
                        break;
                    case MenuItem::Kind::item:
                        h = kItemHeight;
                        w = widthOf (itemFont(), text (m.label)) + 20;
                        break;
                    case MenuItem::Kind::row:
                    {
                        h = kRowHeight;
                        int each = 0;
                        for (const auto& r : m.row)
                            each = juce::jmax (each, widthOf (itemFont(), text (r.label)) + 22);
                        const int n = (int) m.row.size();
                        w = n * each + 4 * juce::jmax (0, n - 1) + 8;
                        break;
                    }
                }
                slots.push_back ({ i, y, h });
                y += h;
                widest = juce::jmax (widest, w + 2 * kPad);
            }
            contentHeight = y + 2 * kPad;
            boxWidth = widest;
        }

        void place()
        {
            layout();
            auto* parent = getParentComponent();
            if (parent == nullptr)
                return;
            const int pw = parent->getWidth(), ph = parent->getHeight();
            const int w = juce::jmin (boxWidth, juce::jmax (60, pw - 16));
            const int h = juce::jmin (contentHeight, juce::jmax (60, ph - 16));
            const int x = juce::jlimit (4, juce::jmax (4, pw - w - 4), anchor.x);
            const int y = juce::jlimit (4, juce::jmax (4, ph - h - 4), anchor.y);
            box = { kShadowSide, kShadowTop, w, h };
            setBounds (x - kShadowSide, y - kShadowTop, w + 2 * kShadowSide, h + kShadowTop + kShadowBottom);
            scroll = juce::jlimit (0, juce::jmax (0, contentHeight - h), scroll);
        }

        juce::Rectangle<int> slotArea (const Slot& s) const
        {
            return { box.getX() + kPad, box.getY() + kPad + s.top - scroll, box.getWidth() - 2 * kPad, s.height };
        }

        /** One of a row's buttons (as shell.html's .row: 2 px and 4 px of padding, 4 px between). */
        juce::Rectangle<int> rowButton (const Slot& s, int button) const
        {
            const auto& items = view.getMenu();
            const int n = s.index < items.size() ? juce::jmax (1, (int) items[s.index].row.size()) : 1;
            const auto area = slotArea (s).reduced (4, 2);
            const int w = (area.getWidth() - 4 * (n - 1)) / n;
            return { area.getX() + button * (w + 4), area.getY(), w, area.getHeight() };
        }

        int itemAt (juce::Point<int> p, int& button) const
        {
            button = -1;
            if (! box.contains (p))
                return -1;
            const auto& items = view.getMenu();
            for (const auto& s : slots)
            {
                if (s.index >= items.size() || ! slotArea (s).contains (p))
                    continue;
                const auto& m = items[s.index];
                if (m.kind == MenuItem::Kind::item)
                    return (int) s.index;
                if (m.kind == MenuItem::Kind::row)
                {
                    for (int i = 0; i < (int) m.row.size(); ++i)
                        if (rowButton (s, i).contains (p))
                        {
                            button = i;
                            return (int) s.index;
                        }
                }
                return -1;   // a header or a line
            }
            return -1;
        }

        void hoverAt (juce::Point<int> p)
        {
            int button = -1;
            const int index = itemAt (p, button);
            setHover (index, button);
        }

        void setHover (int index, int button)
        {
            if (index == hoverIndex && button == hoverButton)
                return;
            hoverIndex = index;
            hoverButton = button;
            repaint();
            // the item's ghost (sl-ui.js mkMenuItem's hover), or none
            view.menuHover (index, button);
        }

        void paintItem (juce::Graphics& g, const MenuItem& m, const Slot& s, juce::Rectangle<int> r) const
        {
            const Theme& T = view.theme;
            const bool hovered = (int) s.index == hoverIndex;
            switch (m.kind)
            {
                case MenuItem::Kind::header:
                    g.setColour (m.colour ? RollPainter::laneColour (T.colours(), *m.colour) : T (Tok::dim));
                    g.setFont (headerFont());
                    g.drawText (text (m.label).toUpperCase(), r.reduced (8, 0), juce::Justification::centredLeft, true);
                    break;
                case MenuItem::Kind::separator:
                    g.setColour (T (Tok::line));
                    g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1);
                    break;
                case MenuItem::Kind::item:
                {
                    if (hovered && ! m.disabled)
                    {
                        juce::Path lit;
                        lit.addRoundedRectangle (r.toFloat(), 6.0f);
                        g.setColour (T (Tok::hover));
                        g.fillPath (lit);
                    }
                    g.setColour (T (Tok::txt).withMultipliedAlpha (m.disabled ? 0.45f : 1.0f));
                    g.setFont (itemFont());
                    g.drawText (text (m.label), r.reduced (10, 0), juce::Justification::centredLeft, true);
                    break;
                }
                case MenuItem::Kind::row:
                {
                    for (int i = 0; i < (int) m.row.size(); ++i)
                    {
                        const auto& button = m.row[(std::size_t) i];
                        const auto rb = rowButton (s, i).toFloat().reduced (0.5f);
                        juce::Path p;
                        p.addRoundedRectangle (rb, 6.0f);
                        if (button.on || (hovered && hoverButton == i && ! button.disabled))
                        {
                            g.setColour (button.on ? T (Tok::acc) : T (Tok::hover));
                            g.fillPath (p);
                        }
                        g.setColour (button.on ? T (Tok::acc) : T (Tok::btnb));
                        g.strokePath (p, juce::PathStrokeType (1.0f));
                        g.setColour ((button.on ? T (Tok::onacc) : T (Tok::txt)).withMultipliedAlpha (button.disabled ? 0.45f : 1.0f));
                        g.setFont (itemFont());
                        g.drawText (text (button.label), rb, juce::Justification::centred, true);
                    }
                    break;
                }
            }
        }

        RollView& view;
        Outside outside { *this };
        bool listening = false;
        juce::Point<int> anchor;
        double openedAt = 0.0;
        std::vector<Slot> slots;
        int contentHeight = 0, boxWidth = kMinWidth, scroll = 0;
        juce::Rectangle<int> box;
        int hoverIndex = -1, hoverButton = -1;
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
            && exactlyEqual (vibRate, o.vibRate) && exactlyEqual (bpm, o.bpm) && bars == o.bars && beatsPerBar == o.beatsPerBar
            && decor == o.decor;
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

    ToolHost RollView::makeToolHost()
    {
        return ToolHost { processor.getDocument(),
                          [this] (const std::string& label) { return processor.pushUndoStep (label, {}); },
                          [this] (const std::string& text, int ms)
                          {
                              const auto t = juce::String::fromUTF8 (text.c_str());
                              if (onToastFor)
                                  onToastFor (t, ms);
                              else if (onToast)
                                  onToast (t);
                          } };
    }

    RollView::RollView (const Theme& t, StrandLockProcessor& p)
        : theme (t),
          processor (p),
          gestures (std::make_unique<RollGestures> (makeHost())),
          tools (std::make_unique<SelectionTools> (makeToolHost(), *gestures)),
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

        // the selection tools draw their ghosts here
        tools->setGhosts (&ghosts);

        // a key pressed on the piano plays until the mouse lets go (sl-ui.js liveOn / liveOff)
        piano.onPress = [this] (int pitch) { processor.getCommandQueue().push (doc::UiCommand::auditionOn (pitch, kPianoVelocity)); };
        piano.onRelease = [this] (int pitch) { processor.getCommandQueue().push (doc::UiCommand::auditionOff (pitch)); };

        refresh();
    }

    RollView::~RollView()
    {
        // the menu goes first: it sits on MainView and listens to the desktop's mouse
        menuPanel.reset();
        // the window closed in the middle of a drag: the gesture ends as a release would (its undo step), with
        // nobody left to tell; a live slider session of the selection bar ends the same way
        onToast = nullptr;
        onToastFor = nullptr;
        onEdit = nullptr;
        onPress = nullptr;
        onHint = nullptr;
        tools->setGhosts (nullptr);
        if (gestures->isDragging())
            gestures->pointerUp();
        tools->endLive();
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
        // A ghost made from another clip, selection or mode than the document holds now no longer shows what a click
        // would do: Undo, Redo, Escape, a key or a state load changed them under it (review R5T-1). The prototype draws
        // its drawer again on each of these, which takes a drawer button's ghost away; here every ghost goes, and the
        // mouse brings it back by entering the control again.
        if (ghost != nullptr && ghostIsStale())
        {
            ghosts.clear();           // its owner lets go: setGhost (nullptr), which refreshes
            if (ghost != nullptr)
                setGhost (nullptr);   // one set without the preview (the tests)
            return;
        }

        auto& set = gestures->settings();
        set.inst = processor.readInst();
        set.bpm = hostBpm.value_or (120.0);
        if (const auto sound = processor.getCurrentSound())
            if (sound->seconds() > 0.0)
                set.sampleSeconds = sound->seconds();
        gestures->sync();
        const auto& geo = gestures->geometry();

        // the notes' decorations (M6): the sound the thumbnails read, the notes whose simulations are kept
        decor.setSound (processor.getCurrentSound());
        decor.setClip (geo.getClip());

        PaintState next;
        next.colours = &theme.colours();
        next.geo = &geo;
        next.clip = geo.getClip();
        next.roll = processor.getDocument().rollState();
        next.inst = set.inst;
        next.free = set.free;
        next.nodes = set.nodes;
        next.snap = set.snap;
        next.readout = gestures->readout();
        next.box = gestures->box();
        next.decor = &decor;
        next.decorKey = decor.key (set.inst);
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
        k.decor = state.decorKey;
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
            // what it was made from (the preview's model call read the document just now)
            const auto& document = processor.getDocument();
            const auto st = document.rollState();
            ghostBase.clip = document.current();
            ghostBase.selected = st.selected;
            ghostBase.mode = st.mode;
            ghostBase.subLane = st.subLane;
        }
        refresh();
    }

    bool RollView::ghostIsStale() const
    {
        const auto& document = processor.getDocument();
        const auto st = document.rollState();
        return document.current() != ghostBase.clip || st.selected != ghostBase.selected || st.mode != ghostBase.mode
            || (st.mode == 8 && st.subLane != ghostBase.subLane);
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
    // The mouse (R3): JUCE's events in, the gestures' calls out, then a refresh.

    namespace
    {
        Mods modsOf (juce::ModifierKeys m) noexcept
        {
            // the prototype's e.shiftKey and e.ctrlKey || e.metaKey
            return Mods { m.isShiftDown(), m.isCtrlDown() || m.isCommandDown() };
        }
    }

    void RollView::changed()
    {
        refresh();
        if (onEdit)
            onEdit();
    }

    void RollView::toast (const juce::String& text)
    {
        if (onToast)
            onToast (text);
    }

    void RollView::press (juce::Point<float> at, juce::ModifierKeys mods, double timeMs)
    {
        if (onPress)
            onPress();
        // a press while a gesture is still held (a second button) ends that one first
        if (gestures->isDragging())
            gestures->pointerUp();
        closeMenu();   // a press on the roll closes the menu and does what it does (sl-ui.js rollDown)
        const auto p = getGeometry().toRoll ((double) at.x, (double) at.y);
        gestures->pointerDown (p.x, p.y, modsOf (mods), timeMs);
        changed();
    }

    void RollView::dragTo (juce::Point<float> at, juce::ModifierKeys mods)
    {
        if (! gestures->isDragging())
            return;
        const auto p = getGeometry().toRoll ((double) at.x, (double) at.y);
        gestures->pointerMove (p.x, p.y, modsOf (mods));
        changed();
    }

    void RollView::release()
    {
        if (! gestures->isDragging())
            return;
        gestures->pointerUp();
        changed();
    }

    void RollView::rightPress (juce::Point<float> at, double timeMs)
    {
        if (onPress)
            onPress();
        if (gestures->isDragging())
            return;
        // sl-ui.js contextmenu: the note there is selected (or made the primary one), then the menu lists what applies
        const auto p = getGeometry().toRoll ((double) at.x, (double) at.y);
        ownMenu = false;
        ownMenuActions.clear();
        menuItems = gestures->contextMenu (p.x, p.y, timeMs);
        changed();
        if (menusShown)
            showMenu (at, timeMs);
    }

    void RollView::visibilityChanged()
    {
        // hidden in the middle of a drag (the Diagnostics tab took the roll's room): the gesture ends there, and an
        // open menu closes
        if (! isVisible())
        {
            release();
            closeMenu();
        }
    }

    //==============================================================================================
    // The right-click menu: the gestures' items in the menu panel (sl-ui.js openMenu).

    void RollView::showMenu (juce::Point<float> at, double timeMs)
    {
        // over the whole page (MainView), where the click was, kept inside it
        juce::Component* parent = menuParent != nullptr ? menuParent.getComponent() : static_cast<juce::Component*> (this);
        if (menuPanel == nullptr)
            menuPanel = std::make_unique<MenuPanel> (*this);
        if (menuPanel->getParentComponent() != parent)
            parent->addChildComponent (*menuPanel);
        menuPanel->open (parent->getLocalPoint (canvas.get(), at).roundToInt(), timeMs);
    }

    bool RollView::isMenuOpen() const noexcept
    {
        return menuPanel != nullptr && menuPanel->isVisible();
    }

    void RollView::closeMenu()
    {
        menuItems.clear();
        ownMenu = false;
        ownMenuActions.clear();
        ownMenuHovers.clear();
        if (menuPanel != nullptr)
            menuPanel->close();
        ghosts.leave (GhostPreview::menuOwner());   // sl-ui.js closeMenu: setGhost (null)
    }

    void RollView::openMenuAt (const juce::Component& from, juce::Point<int> at, std::vector<MenuItem> items,
                               std::vector<std::function<void()>> actions, std::vector<GhostPreview::Make> hovers)
    {
        closeMenu();
        menuItems = std::move (items);
        ownMenuActions = std::move (actions);
        ownMenuHovers = std::move (hovers);
        ownMenu = true;
        if (! menusShown)
            return;
        juce::Component* parent = menuParent != nullptr ? menuParent.getComponent() : static_cast<juce::Component*> (this);
        if (menuPanel == nullptr)
            menuPanel = std::make_unique<MenuPanel> (*this);
        if (menuPanel->getParentComponent() != parent)
            parent->addChildComponent (*menuPanel);
        // the press on the button that opened it came before now: the next press anywhere else closes it
        menuPanel->open (parent->getLocalPoint (&from, at), (double) juce::Time::currentTimeMillis());
    }

    void RollView::setMenuParent (juce::Component* parent) noexcept
    {
        menuParent = parent;
    }

    juce::Component* RollView::getMenuPanel() noexcept
    {
        return menuPanel.get();
    }

    juce::Rectangle<int> RollView::getMenuItemBounds (std::size_t index, int button) const
    {
        return menuPanel != nullptr ? menuPanel->boundsOf (index, button) : juce::Rectangle<int>();
    }

    void RollView::menuHeardPress (const juce::MouseEvent& e)
    {
        if (menuPanel != nullptr)
            menuPanel->pressedSomewhere (e);
    }

    void RollView::menuHover (int index, int button)
    {
        // the item under the mouse (or one of a row's buttons): its ghost, made off to the side; none clears it
        const MenuItem* m = index >= 0 && index < (int) menuItems.size() ? &menuItems[(std::size_t) index] : nullptr;
        if (m != nullptr && m->kind == MenuItem::Kind::row)
            m = button >= 0 && button < (int) m->row.size() ? &m->row[(std::size_t) button] : nullptr;
        GhostPreview::Make make;
        if (m != nullptr && ! m->disabled && m->hover >= 0)
        {
            if (ownMenu)
            {
                if (m->hover < (int) ownMenuHovers.size())
                    make = ownMenuHovers[(std::size_t) m->hover];
            }
            else
            {
                const MenuItem item = *m;
                make = [this, item] { return gestures->menuItemGhost (item); };
            }
        }
        if (make)
            ghosts.hover (GhostPreview::menuOwner(), make);
        else
            ghosts.leave (GhostPreview::menuOwner());
    }

    void RollView::showHint (const juce::String& text)
    {
        if (onHint)
            onHint (text);
    }

    void RollView::setScope (model::ShapeScope scope)
    {
        gestures->settings().scope = scope;
        saveScope();
    }

    void RollView::chooseMenuItem (const MenuItem& item)
    {
        ghosts.clear();   // sl-ui.js mkMenuItem: a click takes the ghost away first
        if (ownMenu)
        {
            // a caller's menu (Select similar): it closes, then the item's action runs (sl-ui.js mkMenuItem)
            std::function<void()> run;
            if (! item.disabled && item.action >= 0 && item.action < (int) ownMenuActions.size())
                run = ownMenuActions[(std::size_t) item.action];
            closeMenu();   // item may be one of the menu's own items: it is not read after this
            if (run)
                run();
            changed();
            return;
        }
        const auto scope = gestures->settings().scope;
        // a keepOpen item (Each note / Across) gives the menu back with the other one lit
        if (auto again = gestures->runMenuItem (item))
            menuItems = std::move (*again);
        else if (! item.keepOpen)
            menuItems.clear();
        if (gestures->settings().scope != scope)
            saveScope();
        // the panel follows: it closes with the menu, or shows the new list in the same place
        if (isMenuOpen())
        {
            if (menuItems.empty())
                menuPanel->close();
            else
                menuPanel->itemsChanged();
        }
        changed();
    }

    void RollView::saveScope()
    {
        auto ui = processor.getUiState();
        ui.scope = (std::uint8_t) (gestures->settings().scope == model::ShapeScope::across ? 1 : 0);
        processor.setUiState (ui);
    }

    //==============================================================================================
    // Commands.

    void RollView::undo()
    {
        if (gestures->isDragging())
            return;
        // sl-ui.js undo: "Nothing to undo", or the step's name
        const auto label = processor.undoLabel();
        if (! processor.canUndo() || ! processor.undo())
            toast ("Nothing to undo");
        else
            toast ("Undo: " + juce::String::fromUTF8 (label.value_or ("Edit").c_str()));
        changed();
    }

    void RollView::redo()
    {
        if (gestures->isDragging())
            return;
        const auto label = processor.redoLabel();
        if (! processor.canRedo() || ! processor.redo())
            toast ("Nothing to redo");
        else
            toast ("Redo: " + juce::String::fromUTF8 (label.value_or ("Edit").c_str()));
        changed();
    }

    void RollView::selectAll()
    {
        gestures->selectAll();
        changed();
    }

    void RollView::duplicate()
    {
        if (gestures->isDragging())
            return;
        gestures->duplicateSelection();
        changed();
    }

    void RollView::deleteSelection()
    {
        if (gestures->isDragging())
            return;
        gestures->deleteSelection();
        changed();
    }

    void RollView::clearSelection()
    {
        // sl-ui.js Escape: the selection goes (and a menu, though Escape with a menu open only closes the menu:
        // MainView::handleKey)
        closeMenu();
        gestures->clearSelection();
        changed();
    }

    void RollView::clearAll()
    {
        if (gestures->isDragging())
            return;
        // sl-ui.js Clear all: the clip with no notes, one undo step, nothing selected
        model::Clip empty = *processor.getDocument().current();
        empty.notes.clear();
        const auto out = processor.editClip ("Clear all", std::move (empty));
        if (out.refused)
            toast (juce::String::fromUTF8 (out.message.c_str()));
        processor.getDocument().setSelection ({}, std::string());
        changed();
    }

    void RollView::copyLanes()
    {
        gestures->copyLanes();
        changed();
    }

    void RollView::pasteLanes()
    {
        if (gestures->isDragging())
            return;
        gestures->pasteLanes();
        changed();
    }

    void RollView::copyNotes()
    {
        gestures->copyNotes();
        changed();
    }

    void RollView::pasteNotes()
    {
        if (gestures->isDragging())
            return;
        gestures->pasteNotes();
        changed();
    }

    void RollView::toggleFreehand()
    {
        gestures->toggleFreehand();
        changed();
    }

    void RollView::armLink()
    {
        gestures->armLink();
        changed();
    }

    void RollView::setMulti (bool on)
    {
        gestures->settings().multi = on;
        changed();
    }

    void RollView::toggleNodes()
    {
        if (gestures->isDragging())
            return;
        gestures->toggleNodes();
        changed();
    }

    void RollView::edited()
    {
        changed();
    }

    void RollView::setGrid (double beats)
    {
        // the prototype's Grid menu: 1/4, 1/8, 1/16, 1/32, Free; the project keeps it (UIST gridBeats)
        gestures->settings().snap = isGrid (beats) ? beats : 0.25;
        auto ui = processor.getUiState();
        ui.gridBeats = (float) gestures->settings().snap;
        processor.setUiState (ui);
        changed();
    }

    double RollView::getGrid() const noexcept
    {
        return gestures->settings().snap;
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
        // the ruler, with the zoom buttons at its right end (above the vertical scroll bar) while they are this view's;
        // in the editor MainView holds them in the tab row and the ruler runs the whole width (M5 S3, UI4-6)
        const bool ownZoom = zoomButtons[0]->getParentComponent() == this;
        const int zoomBox = ownZoom ? 4 * zoomButtonWidth + 3 * 2 + 2 * 4 : 0;
        const int zoomX = std::max (pianoWidth, W - zoomBox);
        ruler.setBounds (pianoWidth, 0, std::max (0, zoomX - pianoWidth), rulerHeight);
        int x = zoomX + 4;
        for (auto& b : zoomButtons)
        {
            if (! ownZoom)
                break;
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
