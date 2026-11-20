#include "GhostPreview.h"

#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        // Two addresses that stand for the open drawer and the open menu as owners of a preview.
        const char drawerTag = 0;
        const char menuTag = 0;
    }

    GhostPreview::GhostPreview (Draw d)
        : draw (std::move (d))
    {
    }

    const void* GhostPreview::drawerOwner() noexcept { return &drawerTag; }
    const void* GhostPreview::menuOwner() noexcept { return &menuTag; }

    void GhostPreview::hover (const void* who, const Make& make)
    {
        show (who, make ? make() : std::optional<model::Clip>());
    }

    void GhostPreview::show (const void* who, std::optional<model::Clip> clip)
    {
        // sl-ui-panels.js ghost: A.setGhost (c || null); ghostOwner = c ? owner : null
        if (! clip)
        {
            clear();
            return;
        }
        shown = std::make_shared<const model::Clip> (std::move (*clip));
        owner = who;
        ++drawn;
        if (draw)
            draw (shown);
    }

    void GhostPreview::leave (const void* who)
    {
        if (shown != nullptr && owner == who)
            clear();
    }

    void GhostPreview::clear()
    {
        if (shown == nullptr && owner == nullptr)
            return;
        shown = nullptr;
        owner = nullptr;
        if (draw)
            draw (nullptr);
    }

    //==============================================================================================
    struct HoverHooks::Hook final : public juce::MouseListener
    {
        Hook (HoverHooks& h, juce::Component& c, GhostPreview::Make m, std::function<juce::String()> t)
            : hooks (h), target (&c), make (std::move (m)), text (std::move (t))
        {
            c.addMouseListener (this, false);
        }

        ~Hook() override
        {
            if (auto* c = target.getComponent())
                c->removeMouseListener (this);
        }

        void mouseEnter (const juce::MouseEvent&) override { hooks.entered (*this); }
        void mouseExit (const juce::MouseEvent&) override { hooks.exited (*this); }
        void mouseDown (const juce::MouseEvent&) override { hooks.pressed (*this); }

        juce::String hintText() const
        {
            if (text)
                return text();
            if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (target.getComponent()))
                return tip->getTooltip();
            return {};
        }

        HoverHooks& hooks;
        juce::Component::SafePointer<juce::Component> target;
        const void* key = target.getComponent();   // the control's address: it owns the preview it draws
        GhostPreview::Make make;
        std::function<juce::String()> text;
    };

    HoverHooks::HoverHooks (GhostPreview* g, Hint h)
        : ghosts (g), hint (std::move (h))
    {
    }

    HoverHooks::~HoverHooks()
    {
        // the controls go with their drawer: a preview or a hint one of them shows goes too (sl-ui-panels.js
        // renderDrawer: "a preview owned by a button that is about to be replaced goes")
        for (auto& h : hooks)
        {
            if (ghosts != nullptr)
                ghosts->leave (h->key);
            if (hintOwner == h->key && hint)
                hint ({});
        }
        hintOwner = nullptr;
        hooks.clear();
    }

    void HoverHooks::add (juce::Component& c, GhostPreview::Make make, std::function<juce::String()> text)
    {
        hooks.push_back (std::make_unique<Hook> (*this, c, std::move (make), std::move (text)));
    }

    HoverHooks::Hook* HoverHooks::find (const juce::Component& c) const noexcept
    {
        for (const auto& h : hooks)
            if (h->key == &c)
                return h.get();
        return nullptr;
    }

    bool HoverHooks::enter (const juce::Component& c)
    {
        auto* h = find (c);
        if (h == nullptr)
            return false;
        entered (*h);
        return true;
    }

    bool HoverHooks::exit (const juce::Component& c)
    {
        auto* h = find (c);
        if (h == nullptr)
            return false;
        exited (*h);
        return true;
    }

    bool HoverHooks::press (const juce::Component& c)
    {
        auto* h = find (c);
        if (h == nullptr)
            return false;
        pressed (*h);
        return true;
    }

    std::vector<juce::Component*> HoverHooks::controls() const
    {
        std::vector<juce::Component*> out;
        for (const auto& h : hooks)
            if (auto* c = h->target.getComponent())
                out.push_back (c);
        return out;
    }

    void HoverHooks::entered (Hook& h)
    {
        // a greyed control can do nothing now, so it draws no preview (its text still says what it does), as a greyed
        // menu item draws none
        const auto* c = h.target.getComponent();
        if (ghosts != nullptr && h.make && c != nullptr && c->isEnabled())
            ghosts->hover (h.key, h.make);
        const auto t = h.hintText();
        if (t.isNotEmpty() && hint)
        {
            hintOwner = h.key;
            hint (t);
        }
    }

    void HoverHooks::exited (Hook& h)
    {
        if (ghosts != nullptr)
            ghosts->leave (h.key);
        if (hintOwner == h.key)
        {
            hintOwner = nullptr;
            if (hint)
                hint ({});
        }
    }

    void HoverHooks::pressed (Hook&)
    {
        // a click takes the preview away before it acts (sl-ui-panels.js button)
        if (ghosts != nullptr)
            ghosts->clear();
    }
}
