#include "LaneMenu.h"

#include "RollView.h"

#include "core/Rates.h"
#include "model/Generators.h"
#include "model/Shapes.h"

#include <cctype>
#include <optional>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        std::string lower (std::string s)
        {
            for (auto& c : s)
                c = (char) std::tolower ((unsigned char) c);
            return s;
        }

        // The prototype's ellipsis (U+2026) as UTF-8 (the source stays ASCII).
        const std::string kEllipsis = "\xe2\x80\xa6";
    }

    LaneMenu laneMenu (SelectionTools& tools, core::LaneId L, const std::vector<std::string>& ids, std::function<void()> reopen,
                       std::function<void (model::ShapeScope)> setScope, std::function<void (ToolSettings::Drawer)> openSettings)
    {
        LaneMenu m;
        const std::string name = core::laneInfo (L).name;
        const auto opts = tools.shapeOpts();   // the Random shape's seed as the menu opened (sl-ui-panels.js opts)

        auto action = [&m] (std::function<void()> fn)
        {
            m.actions.push_back (std::move (fn));
            return (int) m.actions.size() - 1;
        };
        auto item = [&m, &action] (std::string label, bool disabled, std::function<void()> fn, GhostPreview::Make ghost)
        {
            MenuItem it;
            it.label = std::move (label);
            it.disabled = disabled;
            it.action = action (std::move (fn));
            if (ghost)
            {
                m.hovers.push_back (std::move (ghost));
                it.hover = (int) m.hovers.size() - 1;
            }
            m.items.push_back (std::move (it));
        };

        // "Formant on 4 notes", in the lane's colour
        MenuItem header;
        header.kind = MenuItem::Kind::header;
        header.label = name + " on " + nNotes ((int) ids.size());
        header.colour = L;
        m.items.push_back (header);

        // Each note / Across: the menu stays open with the other one lit
        MenuItem row;
        row.kind = MenuItem::Kind::row;
        for (const auto scope : { model::ShapeScope::each, model::ShapeScope::across })
        {
            MenuItem b;
            b.label = scope == model::ShapeScope::each ? "Each note" : "Across";
            b.on = tools.scope() == scope;
            b.keepOpen = true;
            b.action = action ([setScope, reopen, scope]
            {
                if (setScope)
                    setScope (scope);
                if (reopen)
                    reopen();
            });
            row.row.push_back (b);
        }
        m.items.push_back (row);

        // the lane's shapes (on Travel: those of the notes' kind), each with its ghost
        for (const auto k : tools.shapeKeys (L))
        {
            GhostPreview::Make make = [&tools, ids, L, k, opts]
            {
                return model::applyShape (*tools.clip(), ids, L, k, tools.scope(), opts);
            };
            item (model::shapeName (k), false, [&tools, make, k, name]
            {
                if (auto r = make())
                    tools.act (std::move (*r), std::string (model::shapeName (k)) + " (" + name + ")");
                if (k == model::ShapeKey::random)
                    tools.settings().shapeSeed += 1.0;
            }, make);
        }

        MenuItem separator;
        separator.kind = MenuItem::Kind::separator;
        m.items.push_back (separator);

        // the drawers' pattern and random, and the drawers themselves
        const auto& S = tools.settings();
        const bool has = tools.anyGen (L);
        for (const auto kind : { model::GenKind::pattern, model::GenKind::random })
        {
            const bool pattern = kind == model::GenKind::pattern;
            GhostPreview::Make make = [&tools, ids, L, kind]
            {
                return std::optional<model::Clip> (tools.putGen (*tools.clip(), ids, L, kind));
            };
            const std::string label = pattern ? "Pattern: " + lower (genShapeName (S.pat.shape)) + " " + core::rateLabel (S.pat.rate)
                                              : std::string ("Random ") + core::rateLabel (S.rnd.rate);
            item (label, false, [&tools, make, name, pattern]
            {
                if (auto r = make())
                    tools.act (std::move (*r), std::string (pattern ? "Pattern (" : "Random (") + name + ")");
            }, make);
            item (std::string (pattern ? "Pattern settings" : "Random settings") + kEllipsis, false, [&tools, L, openSettings, pattern]
            {
                // the drawer for this lane (sl-ui-panels.js: S.lane = L; openDrawer (...))
                tools.settings().lane = L;
                if (openSettings)
                    openSettings (pattern ? ToolSettings::Drawer::pattern : ToolSettings::Drawer::random);
            }, {});
        }

        item ("Re-roll the random", ! has, [&tools, ids, L, name]
        {
            tools.act (model::reroll (*tools.clip(), ids, L), "Re-roll (" + name + ")");
        }, {});
        item ("Remove pattern / random", ! has, [&tools, ids, L, name]
        {
            tools.act (model::clearGen (*tools.clip(), ids, L), "Remove pattern (" + name + ")");
        }, [&tools, ids, L] { return std::optional<model::Clip> (model::clearGen (*tools.clip(), ids, L)); });
        item ("Turn into nodes", ! has, [&tools, ids, L, name]
        {
            tools.act (model::bakeGen (*tools.clip(), ids, L), "Turn into nodes (" + name + ")");
        }, [&tools, ids, L] { return std::optional<model::Clip> (model::bakeGen (*tools.clip(), ids, L)); });
        return m;
    }

    void openLaneMenu (RollView& roll, core::LaneId L, juce::Component& arrow, std::function<void (ToolSettings::Drawer)> openSettings)
    {
        // sl-ui-panels.js openLaneMenu: the roll shows the lane, then the menu for the selected notes
        auto& tools = roll.getTools();
        tools.showLane (L);
        roll.edited();
        const auto ids = tools.ids();
        if (ids.empty())
        {
            tools.toast (kLaneMenuNothingSelected);
            return;
        }
        juce::Component::SafePointer<juce::Component> where (&arrow);
        auto reopen = [&roll, L, where, openSettings]
        {
            if (auto* a = where.getComponent())
                openLaneMenu (roll, L, *a, openSettings);
        };
        auto menu = laneMenu (tools, L, ids, reopen, [&roll] (model::ShapeScope s) { roll.setScope (s); }, openSettings);
        roll.openMenuAt (arrow, { 0, arrow.getHeight() + 4 }, std::move (menu.items), std::move (menu.actions), std::move (menu.hovers));
    }
}
