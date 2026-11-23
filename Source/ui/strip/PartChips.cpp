#include "PartChips.h"

#include "PluginProcessor.h"
#include "core/JsCompat.h"
#include "core/Music.h"
#include "ui/roll/Drawers.h"

#include <cmath>

namespace sl::ui
{
    namespace
    {
        /** sl-ui-sample.js PART_TIPS. */
        const char* partTip (analysis::PartKind k) noexcept
        {
            switch (k)
            {
                case analysis::PartKind::steady: return "A held, steady stretch: good for pads and leads.";
                case analysis::PartKind::hit:    return "The biggest hit in the file.";
                case analysis::PartKind::quiet:  return "A quiet texture.";
                case analysis::PartKind::loop:   return "One bar at the tempo found in the file.";
            }
            return "";
        }

        /** sl-ui-sample.js regionIsWhole and sameRegion (1e-4: a chip lights for the part it set). */
        bool regionIsWhole (double a, double b) noexcept { return a <= 0.0005 && b >= 0.9995; }
        bool isRegion (const std::pair<double, double>& r, const analysis::Part& p) noexcept
        {
            return std::abs (r.first - p.start) < 1e-4 && std::abs (r.second - p.end) < 1e-4;
        }

        const char* engineWord (core::Engine e) noexcept
        {
            return e == core::Engine::tape ? "Tape" : e == core::Engine::voice ? "Voice" : "Cloud";
        }

        juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

        /** sl-ui.js rootText: "A2", or "A2 +12 cents". */
        juce::String rootWords (int root, int cents)
        {
            return utf8 (core::noteName ((double) root)) + (cents != 0 ? juce::String (" ") + (cents > 0 ? "+" : "") + juce::String (cents) + " cents" : juce::String());
        }

        /** A chip that says when the mouse is on it (a part chip's box on the waveform and its hint). */
        class HoverChip final : public FlatButton
        {
        public:
            using FlatButton::FlatButton;
            std::function<void (bool)> onHover;

            void mouseEnter (const juce::MouseEvent& e) override
            {
                FlatButton::mouseEnter (e);
                if (onHover)
                    onHover (true);
            }

            void mouseExit (const juce::MouseEvent& e) override
            {
                FlatButton::mouseExit (e);
                if (onHover)
                    onHover (false);
            }
        };
    }

    struct PartChips::Chip
    {
        std::unique_ptr<FlatButton> button;
        int width = 0;
    };

    //==============================================================================================
    PartChips::PartChips (const Theme& t, StrandLockProcessor& p)
        : theme (t), processor (p)
    {
        setInterceptsMouseClicks (false, true);
    }

    PartChips::~PartChips() = default;

    juce::StringArray PartChips::texts() const
    {
        juce::StringArray out;
        for (const auto& c : chips)
            out.add (c->button->getButtonText());
        return out;
    }

    FlatButton* PartChips::chip (const juce::String& text) const
    {
        for (const auto& c : chips)
            if (c->button->getButtonText() == text)
                return c->button.get();
        return nullptr;
    }

    int PartChips::preferredWidth() const
    {
        int w = 0;
        for (const auto& c : chips)
            w += (w > 0 ? gap : 0) + c->width;
        return w;
    }

    int PartChips::rowsFor (int width) const
    {
        // the same greedy rows resized () lays out
        if (chips.empty())
            return 0;
        int rows = 1, x = 0;
        for (const auto& c : chips)
        {
            if (x > 0 && x + c->width > width)
            {
                ++rows;
                x = 0;
            }
            x += c->width + gap;
        }
        return rows;
    }

    bool PartChips::refresh()
    {
        // renderChips' chipKey: the sound and its parts, the Region, which suggestions show, the engine and At the end.
        const auto sound = processor.getCurrentSound();
        const auto& s = processor.getSuggestions();
        const auto r = processor.currentRegion();
        const auto inst = processor.readInst();
        juce::String k;
        k << (sound != nullptr ? juce::String ((juce::int64) sound->request) : juce::String ("-")) << "|" << (int) s.analysis.parts.size() << "|"
          << juce::String (r.first, 12) << "|" << juce::String (r.second, 12) << "|" << (processor.showsRootSuggestion() ? 1 : 0)
          << (processor.showsKeySuggestion() ? 1 : 0) << (processor.showsEngineSuggestion() ? 1 : 0) << "|" << (int) inst.engine << "|"
          << (int) inst.region.atEnd << "|" << (s.root.has_value() ? s.root->root * 1000 + s.root->cents : -1) << "|"
          << (s.key.has_value() ? s.key->key * 2 + (int) s.key->scale : -1);
        if (k == key)
            return false;
        key = k;
        rebuild();
        return true;
    }

    void PartChips::rebuild()
    {
        // the chip under the mouse is about to go: its mouse exit never comes
        if (hovered.has_value())
            setHovered (std::nullopt);
        chips.clear();

        const auto sound = processor.getCurrentSound();
        if (sound == nullptr)
        {
            resized();
            return;
        }
        const auto& s = processor.getSuggestions();
        const auto r = processor.currentRegion();
        const auto inst = processor.readInst();
        const bool whole = regionIsWhole (r.first, r.second);

        auto add = [this] (std::unique_ptr<FlatButton> b, const juce::String& tip, std::function<void()> click, int width = 0)
        {
            b->setTooltip (tip);
            b->setFontPx (12.0f);
            b->onClick = std::move (click);
            auto c = std::make_unique<Chip>();
            c->width = width > 0 ? width : b->preferredWidth();
            c->button = std::move (b);
            addAndMakeVisible (*c->button);
            chips.push_back (std::move (c));
        };
        auto changed = [this]
        {
            if (onChange)
                onChange();
        };

        // The suggestion line (sl-ui.js updateStripText): one short button each, the sentence is its tooltip.
        if (! sound->builtIn && processor.showsRootSuggestion())
        {
            const auto g = *s.root;
            const auto words = rootWords (g.root, g.cents);
            add (std::make_unique<FlatButton> (theme, "Use " + utf8 (core::noteName ((double) g.root)) + " as the root"),
                 (whole ? "It" : "This part") + juce::String (" sounds like ") + words
                     + ". Use it as the root, so the notes play this part in tune (C3 then plays it as recorded)",
                 [this, changed] { processor.acceptRootSuggestion(); changed(); });
        }
        if (! sound->builtIn && processor.showsKeySuggestion())
        {
            const auto name = utf8 (doc::keyName (s.key->key, s.key->scale));
            add (std::make_unique<FlatButton> (theme, "Use key " + name),
                 "It sounds like " + name + ". Click to set the Key and Scale (Sound tab) to " + name + " (one undo step)",
                 [this, changed] { processor.acceptKeySuggestion(); changed(); });
        }
        if (! sound->builtIn && whole && sound->seconds() <= 20.0 && inst.region.atEnd == core::AtEnd::loop)
            add (std::make_unique<FlatButton> (theme, "Loop cleanly"),
                 "Tip: a slightly smaller Region loops cleanly. Click to pull the Region end in a little, so the loop crossfade has sound to "
                 "use (one undo step)",
                 [this, changed] { trimRegion(); changed(); });

        // E3 (renderChips): the engine that suits the file, or a note that it is on already.
        if (processor.showsEngineSuggestion())
        {
            const auto& e = *s.engine;
            auto b = std::make_unique<FlatButton> (theme, e.match ? juce::String (engineWord (e.engine)) + " suits this sound (on)" : utf8 (e.text));
            b->setLaneColour (Tok::acc);
            if (e.match)
                add (std::move (b), utf8 (e.why) + " Click to hide this note.", [this, changed] { processor.dismissEngineSuggestion (true); changed(); });
            else
                add (std::move (b), utf8 (e.why) + " Click to switch the engine (one undo step).", [this, changed] { processor.acceptEngineSuggestion(); changed(); });
        }

        // A part of a file longer than 10 minutes: another 10 minutes of it (M7 V3: only a file read from its own path; a
        // copy found in the library or kept in the project has no other part to load).
        if (! sound->builtIn && sound->cut && ! sound->missing && sound->source == doc::SoundSource::file)
            add (std::make_unique<FlatButton> (theme, "Load another part..."),
                 "This song is longer than 10 minutes, so StrandLock loaded 10 minutes of it. Click to pick the 10 minutes to load",
                 [this] { if (onPickPart) onPickPart(); });

        // The part chips (D10).
        const auto& parts = s.analysis.parts;
        auto wholeChip = [&]
        {
            add (std::make_unique<FlatButton> (theme, "Use the whole file"), "Set the Region back to the whole file (one undo step)",
                 [this, changed]
                 {
                     processor.useWholeFile();
                     if (onShowAll)
                         onShowAll();
                     changed();
                 });
        };
        if (parts.empty())
        {
            if (! whole)
                wholeChip();
        }
        else if (sound->seconds() > 8.0)
        {
            if (! whole)
                wholeChip();
            for (const auto& p : parts)
            {
                auto b = std::make_unique<HoverChip> (theme, utf8 (p.label));
                b->setToggleState (isRegion (r, p), juce::dontSendNotification);
                b->onHover = [this, p] (bool on)
                {
                    if (on)
                        setHovered (p);
                    else if (hovered.has_value() && hovered->label == p.label)
                        setHovered (std::nullopt);
                };
                add (std::move (b), juce::String (partTip (p.kind)) + " Click to set the Region to it (one undo step).",
                     [this, p, changed] { usePart (p); changed(); });
            }
        }
        else
        {
            // a short file (the starter sounds): one chip that opens the suggestions
            const analysis::Part* on = nullptr;
            for (const auto& p : parts)
                if (isRegion (r, p))
                    on = &p;
            auto b = std::make_unique<roll::ArrowButton> (theme, on != nullptr ? utf8 (on->label) : juce::String ("Parts"));
            b->setToggleState (on != nullptr, juce::dontSendNotification);
            const int w = b->widthWithArrow();
            add (std::move (b), "Suggested parts of this sound", [this] { openPartsMenu(); }, w);
        }
        resized();
    }

    void PartChips::resized()
    {
        // One row, centred in the height, when they fit; else the rows rowsFor counts, each filled left to right in order,
        // as the prototype's strip wraps its chips (fix pass UI7-1: a chip that did not fit was hidden). The strip gives a
        // file's chips the height of all their rows, so every chip shows; one wider than the whole row is cut to it. Beside
        // the file line (a starter's one row) a chip that would need a second row stays hidden.
        const int width = getWidth();
        const int rows = rowsFor (width);
        const int top = rows <= 1 ? juce::jmax (0, (getHeight() - chipHeight) / 2) : 0;
        int x = 0, row = 0;
        for (auto& c : chips)
        {
            if (x > 0 && x + c->width > width)
            {
                ++row;
                x = 0;
            }
            const int y = top + row * (chipHeight + rowGap);
            c->button->setVisible (y + chipHeight <= getHeight());
            c->button->setBounds (x, y, juce::jlimit (0, juce::jmax (0, width), c->width), chipHeight);
            x += c->width + gap;
        }
    }

    //==============================================================================================
    void PartChips::setHovered (std::optional<analysis::Part> p)
    {
        hovered = std::move (p);
        if (onHint)
            onHint (hovered.has_value() ? "Suggested part: " + utf8 (hovered->label) + ". " + partTip (hovered->kind)
                                              + " The dashed box on the waveform shows it."
                                        : juce::String());
        if (auto* parent = getParentComponent())
            parent->repaint();
    }

    void PartChips::usePart (const analysis::Part& p)
    {
        // sl-ui-sample.js usePart: the Region (one undo step), the view, a root suggestion for it (the processor asks),
        // and for a loop at another tempo the tempo to set.
        processor.usePart (p);
        if (onShowPart)
            onShowPart (p.start, p.end);
        if (p.kind == analysis::PartKind::loop && p.bpm > 0.0)
        {
            const auto snap = processor.getDiag().snapshot();
            if (snap.hasBpm && std::abs (p.bpm - snap.bpm) > 0.5 && onToast)
                onToast ("This bar is " + utf8 (core::jsNumberToString (core::jsRound (p.bpm * 10.0) / 10.0)) + " bpm: set the Tempo in Cubase to "
                             + utf8 (core::jsNumberToString (core::jsRound (p.bpm))) + " to keep it in time",
                         4500);
        }
    }

    void PartChips::choosePart (int index)
    {
        const auto& parts = processor.getSuggestions().analysis.parts;
        if (index >= 0 && index < (int) parts.size())
        {
            const auto p = parts[(std::size_t) index];
            usePart (p);
            if (onChange)
                onChange();
        }
    }

    void PartChips::openPartsMenu()
    {
        // renderChips' menu for a short file: "Suggested parts", each part (the Region's lit), then Use the whole file.
        const auto& parts = processor.getSuggestions().analysis.parts;
        const auto r = processor.currentRegion();
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        menu.addSectionHeader ("Suggested parts");
        for (int i = 0; i < (int) parts.size(); ++i)
            menu.addItem (i + 1, utf8 (parts[(std::size_t) i].label), true, isRegion (r, parts[(std::size_t) i]));
        menu.addSeparator();
        menu.addItem (1000, "Use the whole file", ! regionIsWhole (r.first, r.second));
        auto* target = chips.empty() ? static_cast<juce::Component*> (this) : static_cast<juce::Component*> (chips.back()->button.get());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withMaximumNumColumns (1),
                            [safe = juce::Component::SafePointer<PartChips> (this)] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                if (result == 1000)
                                {
                                    safe->processor.useWholeFile();
                                    if (safe->onShowAll)
                                        safe->onShowAll();
                                    if (safe->onChange)
                                        safe->onChange();
                                    return;
                                }
                                safe->choosePart (result - 1);
                            });
    }

    void PartChips::trimRegion()
    {
        // sl-ui.js trimRegion: cut = max (0.004, 3 * (xfadeMs || 10) / 1000 / max (0.05, seconds))
        const auto sound = processor.getCurrentSound();
        if (sound == nullptr)
            return;
        const auto r = processor.currentRegion();
        const auto inst = processor.readInst();
        const double xf = inst.region.xfadeMs > 0.0 ? inst.region.xfadeMs : 10.0;
        const double cut = std::max (0.004, 3.0 * xf / 1000.0 / std::max (0.05, sound->seconds()));
        const double start = std::min (r.first, 1.0 - 2.0 * cut);
        const double end = std::max (r.first + 0.01, std::min (r.second, 1.0 - cut));
        processor.setRegionStep (start, end, "Trim the Region");
    }
}
