// Hover ghosts (PLUGIN-PLAN 4 M5 "ghosts", 7.7 S2 and decision 2): hovering any shape, pattern or recipe, and every
// other control or menu item that previews, draws the clip a click would make and changes nothing: 0 undo steps and
// 0 snapshots published.
//
// - Headless, on every fixture clip of model/tools.json (every note, and a seeded half), on all ten lanes: every shape
//   (Each note and Across), every pattern shape, the drawers' pattern and random, Re-roll, Remove, Turn into nodes,
//   every lane-menu item with a ghost, every recipe, and the Connect and Tools previews. Each ghost is the bytes of the
//   direct model call. The clip, the undo history and the document's clip versions (each one is what the processor
//   hands to the SnapshotPublisher) do not move; a click on the same control does move them, so the counters count.
// - In the editor: the mouse on every control of the Shapes, Pattern, Random, Connect and Tools drawers (and the tool
//   panels), the bar's buttons, every item of every lane menu, the right-click menu's shapes and the recipes: the
//   SnapshotPublisher's submit count, the clip and the undo history are unchanged; the roll draws the ghost while the
//   mouse is there and none once it leaves; the hint bar shows the control's text meanwhile.
// - Pictures for review: a ghost from the Shapes drawer, and one from a lane menu.

#include <juce_audio_processors/juce_audio_processors.h>

#include "ToolRig.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Seeds.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "model/ModelFixture.h"
#include "model/Recipes.h"
#include "model/Shapes.h"
#include "model/Tools.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/Drawers.h"
#include "ui/roll/GhostPreview.h"
#include "ui/roll/LaneMenu.h"
#include "ui/roll/RollView.h"
#include "ui/roll/SelectionBar.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;
    using ui::ThemeId;
    using ui::roll::GhostPreview;
    using ui::roll::ToolSettings;
    using Make = GhostPreview::Make;

    /** c and its parents below root are visible (a test editor has no window, so isShowing () is false). */
    bool shownIn (const juce::Component& c, const juce::Component& root)
    {
        for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        {
            if (p == &root)
                return true;   // the editor itself is never made visible in a test
            if (! p->isVisible())
                return false;
        }
        return false;
    }

    /** One hover: what the ghost would draw, and the direct model call it must equal (none: no ghost). */
    struct Hover
    {
        std::string what;
        Make ghost;
        Make direct;
    };
}

//==================================================================================================
SL_TEST ("ui", ghostsChangeNothing)
{
    section ("Ghosts: hovering every shape, pattern, recipe, lane-menu item and tool preview on every fixture clip and lane draws the "
             "bytes of the direct model call, with 0 undo steps and 0 clip versions (none published)");

    JsonValue doc;
    if (! fixture::load ("model/tools.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;

    int hovers = 0, ghostsDrawn = 0, ghostsRight = 0, ghostsChanged = 0, noneRight = 0, cleared = 0, runs = 0, quiet = 0;
    int shapeHovers = 0, patternHovers = 0, recipeHovers = 0, menuHovers = 0;
    std::string firstBad;
    std::uint32_t seed = 921;
    for (const auto& [name, clip] : clips)
    {
        if (clip.notes.empty())
            continue;
        for (int half = 0; half < 2; ++half)
        {
            std::vector<std::string> sel;
            for (const auto& n : clip.notes)
                if (half == 0 || core::rand01 (core::hash32 ((std::int32_t) seed, (std::int32_t) sel.size(), (std::int32_t) (n->p))) < 0.5)
                    sel.push_back (n->id);
            ++seed;
            if (sel.empty())
                sel.push_back (clip.notes.front()->id);
            for (int li = 0; li < core::kNumLanes; ++li)
            {
                const auto L = static_cast<core::LaneId> (li);
                ToolRig t (clip);
                t.select (sel);
                t.lane (L);
                // the drawers at their first settings (sync reads the primary note's generator into them, as the
                // prototype's loadGenValues does)
                t.settings().pat = ToolSettings::Pattern();
                t.settings().rnd = ToolSettings::Random();
                auto& tools = *t.tools;
                const auto ids = tools.ids();
                const auto start = t.rig.doc.current();
                const auto& rs = t.rig.gestures().settings();
                model::ShapeOpts opts;
                opts.seed = 1.0;
                opts.bpm = rs.bpm;
                opts.sampleSeconds = rs.sampleSeconds;
                opts.inst = rs.inst;

                std::vector<Hover> list;
                for (const auto scope : { model::ShapeScope::each, model::ShapeScope::across })
                    for (const auto k : tools.shapeKeys (L))
                        list.push_back ({ std::string ("shape ") + model::code (k), [&tools, k, scope]
                        {
                            tools.setScope (scope);
                            return tools.previewShape (k);
                        }, [&clip, ids, L, k, scope, opts] { return model::applyShape (clip, ids, L, k, scope, opts); } });
                for (int s = 0; s < 7; ++s)
                {
                    const auto shape = static_cast<model::GenShape> (s);
                    list.push_back ({ std::string ("pattern ") + model::code (shape), [&tools, shape] { return tools.previewGen (model::GenKind::pattern, shape); },
                                      [&clip, ids, L, shape] { return std::optional<model::Clip> (expectedPutGen (clip, ids, L, startPattern (shape))); } });
                }
                list.push_back ({ "Pattern's Apply", [&tools] { return tools.previewGen (model::GenKind::pattern); },
                                  [&clip, ids, L] { return std::optional<model::Clip> (expectedPutGen (clip, ids, L, startPattern (model::GenShape::squiggle))); } });
                list.push_back ({ "Random's Apply", [&tools] { return tools.previewGen (model::GenKind::random); },
                                  [&clip, ids, L] { return std::optional<model::Clip> (expectedPutGen (clip, ids, L, startRandom())); } });
                const bool gen = hasGen (clip, ids, L);
                list.push_back ({ "Re-roll", [&tools] { return tools.previewReroll(); },
                                  [&clip, ids, L] { return std::optional<model::Clip> (model::reroll (clip, ids, L)); } });
                list.push_back ({ "Remove", [&tools] { return tools.previewRemove(); },
                                  [&clip, ids, L, gen] { return gen ? std::optional<model::Clip> (model::clearGen (clip, ids, L)) : std::nullopt; } });
                list.push_back ({ "Turn into nodes", [&tools] { return tools.previewBake(); },
                                  [&clip, ids, L, gen] { return gen ? std::optional<model::Clip> (model::bakeGen (clip, ids, L)) : std::nullopt; } });
                for (int n = 1; n <= 8; ++n)
                    list.push_back ({ "recipe " + std::to_string (n), [&tools, n] { return tools.previewRecipe (n); }, [&clip, ids, n, &rs]
                    {
                        auto r = model::applyRecipe (clip, rs.inst, n, ids);
                        return r ? std::optional<model::Clip> (std::move (r->clip)) : std::nullopt;
                    } });
                for (const auto mode : { model::ConnectMode::nearest, model::ConnectMode::furthest, model::ConnectMode::random })
                    list.push_back ({ "Connect", [&tools, mode] { return tools.previewConnect (mode); },
                                      [&clip, ids, mode] { return std::optional<model::Clip> (model::connect (clip, ids, mode, 1.0)); } });
                list.push_back ({ "Connect's Re-roll", [&tools] { return tools.previewConnect (model::ConnectMode::random, true); },
                                  [&clip, ids] { return std::optional<model::Clip> (model::connect (clip, ids, model::ConnectMode::random, 2.0)); } });
                list.push_back ({ "Disconnect", [&tools] { return tools.previewDisconnect(); }, [&clip, ids]
                {
                    return ui::roll::linksAmong (clip, ids) > 0 ? std::optional<model::Clip> (model::disconnect (clip, ids)) : std::nullopt;
                } });
                list.push_back ({ "Reverse", [&tools] { return tools.previewLaneTool (0); }, [&clip, ids, L] { return std::optional<model::Clip> (model::reverseLane (clip, ids, L)); } });
                list.push_back ({ "Flip", [&tools] { return tools.previewLaneTool (1); }, [&clip, ids, L] { return std::optional<model::Clip> (model::flipLane (clip, ids, L)); } });
                list.push_back ({ "More", [&tools] { return tools.previewLaneTool (2); }, [&clip, ids, L] { return std::optional<model::Clip> (model::scaleLane (clip, ids, L, 2.0)); } });
                list.push_back ({ "Less", [&tools] { return tools.previewLaneTool (3); }, [&clip, ids, L] { return std::optional<model::Clip> (model::scaleLane (clip, ids, L, 0.5)); } });
                list.push_back ({ "Legato", [&tools] { return tools.previewLegato(); }, [&clip, ids] { return std::optional<model::Clip> (model::legato (clip, ids)); } });
                list.push_back ({ "Walk", [&tools] { return tools.previewWalk(); },
                                  [&clip, ids] { return std::optional<model::Clip> (model::walk (clip, ids, model::WalkField::start(), 0.0, 0.9)); } });
                list.push_back ({ "Chop", [&tools] { return tools.previewChop(); }, [&clip, ids]
                {
                    auto r = model::chop (clip, ids, core::RateId::r1_16, model::ChopStart::by (0.0), model::kMaxChopNotes);
                    return r.refused ? std::nullopt : std::optional<model::Clip> (std::move (r.clip));
                } });
                list.push_back ({ "Humanize", [&tools] { return tools.previewHumanize(); }, [&clip, ids]
                {
                    model::HumanizeAmounts a;
                    a.time = 0.03;
                    a.vel = 0.1;
                    a.volumeAndTone (0.05);
                    return std::optional<model::Clip> (model::humanize (clip, ids, a, 1.0));
                } });
                for (const auto dir : { model::StrumDir::up, model::StrumDir::down })
                    list.push_back ({ "Strum", [&tools, dir] { return tools.previewStrum (dir); },
                                      [&clip, ids, dir] { return std::optional<model::Clip> (model::strum (clip, ids, core::RateId::r1_64, dir)); } });

                // the lane menu's items with a ghost (its shapes, the drawers' pattern and random, Remove, Turn into nodes)
                tools.setScope (model::ShapeScope::each);
                const auto menu = ui::roll::laneMenu (tools, L, ids, {}, {}, {});
                for (const auto& item : menu.items)
                {
                    if (item.hover < 0 || item.disabled)
                        continue;
                    const Make ghost = menu.hovers[(std::size_t) item.hover];
                    Make direct;
                    if (const auto key = [&item, &tools, L]() -> std::optional<model::ShapeKey>
                        {
                            for (const auto k : tools.shapeKeys (L))
                                if (item.label == model::shapeName (k))
                                    return k;
                            return std::nullopt;
                        }())
                    {
                        const auto k = *key;
                        direct = [&clip, ids, L, k, opts] { return model::applyShape (clip, ids, L, k, model::ShapeScope::each, opts); };
                    }
                    else if (item.label.rfind ("Pattern:", 0) == 0)
                        direct = [&clip, ids, L] { return std::optional<model::Clip> (expectedPutGen (clip, ids, L, startPattern (model::GenShape::squiggle))); };
                    else if (item.label.rfind ("Random ", 0) == 0)
                        direct = [&clip, ids, L] { return std::optional<model::Clip> (expectedPutGen (clip, ids, L, startRandom())); };
                    else if (item.label == "Remove pattern / random")
                        direct = [&clip, ids, L] { return std::optional<model::Clip> (model::clearGen (clip, ids, L)); };
                    else if (item.label == "Turn into nodes")
                        direct = [&clip, ids, L] { return std::optional<model::Clip> (model::bakeGen (clip, ids, L)); };
                    else
                        direct = [] { return std::optional<model::Clip>(); };
                    // the menu reads the scope as the mouse reaches an item: Each note, as it opened (the shapes above
                    // left it on Across)
                    list.push_back ({ "lane menu " + item.label, [&tools, ghost]
                    {
                        tools.setScope (model::ShapeScope::each);
                        return ghost();
                    }, direct });
                }

                // hover each one as the mouse does (the owner is the control), then leave it
                const int steps0 = t.rig.steps, versions0 = t.versions;
                int item = 0;
                for (const auto& h : list)
                {
                    const void* owner = &list[(std::size_t) item++];
                    ++hovers;
                    shapeHovers += h.what.rfind ("shape ", 0) == 0 ? 1 : 0;
                    patternHovers += h.what.rfind ("pattern ", 0) == 0 || h.what == "Pattern's Apply" ? 1 : 0;
                    recipeHovers += h.what.rfind ("recipe ", 0) == 0 ? 1 : 0;
                    menuHovers += h.what.rfind ("lane menu ", 0) == 0 ? 1 : 0;
                    const int drawnBefore = t.drawn;
                    t.ghosts.hover (owner, h.ghost);
                    const auto shown = t.drawnClip;
                    const auto want = h.direct();
                    if (want)
                    {
                        const bool drew = t.drawn == drawnBefore + 1 && shown != nullptr && t.ghosts.getOwner() == owner;
                        ghostsDrawn += drew ? 1 : 0;
                        const auto wantBytes = model::encodeClip (*want);
                        const bool same = drew && model::encodeClip (*shown) == wantBytes;
                        ghostsRight += same ? 1 : 0;
                        ghostsChanged += same && wantBytes != model::encodeClip (clip) ? 1 : 0;
                        if (! same && firstBad.empty())
                            firstBad = fmt ("%s on %s (%s, %s): %s", h.what.c_str(), name.c_str(), half == 0 ? "every note" : "half",
                                            core::laneInfo (L).name, drew ? "other bytes than the model call" : "no ghost drawn");
                    }
                    else
                    {
                        const bool none = shown == nullptr && ! t.ghosts.isShown();
                        noneRight += none ? 1 : 0;
                        ghostsRight += none ? 1 : 0;
                        if (! none && firstBad.empty())
                            firstBad = fmt ("%s on %s (%s): a ghost where the click would show nothing", h.what.c_str(), name.c_str(), core::laneInfo (L).name);
                    }
                    t.ghosts.leave (owner);
                    cleared += t.drawnClip == nullptr && ! t.ghosts.isShown() ? 1 : 0;
                }
                ++runs;
                const bool nothing = t.rig.steps == steps0 && t.versions == versions0 && t.rig.doc.current() == start;
                quiet += nothing ? 1 : 0;
                if (! nothing && firstBad.empty())
                    firstBad = fmt ("on %s (%s): %d undo steps, %d clip versions while hovering", name.c_str(), core::laneInfo (L).name,
                                    t.rig.steps - steps0, t.versions - versions0);
            }
        }
    }
    note (fmt ("%d hovers in %d runs (%d shapes, %d patterns, %d recipes, %d lane-menu items); %d drew a ghost, %d of them changed "
               "the clip; %d showed none, as the click would change nothing to see%s%s",
               hovers, runs, shapeHovers, patternHovers, recipeHovers, menuHovers, ghostsDrawn, ghostsChanged, noneRight,
               firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (runs >= 100 && quiet == runs, fmt ("%d of %d runs: hovering pushed 0 undo steps and made 0 clip versions (0 snapshots to publish); the "
                                              "clip stayed the same version", quiet, runs));
    check (hovers > 5000 && ghostsRight == hovers && ghostsChanged >= hovers / 3 && shapeHovers > 1000 && patternHovers > 800
               && recipeHovers >= 8 * runs && menuHovers > 1000,
           fmt ("%d of %d hovers: the ghost is the bytes of the direct model call (or none where the click shows nothing); %d of the ghosts "
                "differ from the clip", ghostsRight, hovers, ghostsChanged));
    check (cleared == hovers, fmt ("%d of %d: the ghost goes when the mouse leaves its control", cleared, hovers));

    // the counters count: the same controls' clicks are steps and versions
    const model::Clip& grid = clips["connect-grid"];
    ToolRig t (grid);
    t.select (allIdsOf (grid));
    t.lane (core::LaneId::tone);
    t.tools->applyShape (model::ShapeKey::swell);
    t.tools->applyGen (model::GenKind::pattern);
    t.tools->rerollGen();
    check (t.rig.steps == 3 && t.versions == 3 && t.lastLabel() == "Re-roll (Tone)",
           fmt ("clicking Swell, the Pattern's Apply and Re-roll: %d undo steps and %d clip versions (3 and 3)", t.rig.steps, t.versions));
}

SL_TEST ("ui", ghostsOwnersAndDrawer)
{
    section ("Ghosts: a slider before Apply draws a ghost owned by the drawer, which stays while the mouse is on another control and goes "
             "when it leaves the drawer; a click or an edit takes any ghost away");

    model::Clip clip = model::newClip (4.0);
    for (int i = 0; i < 4; ++i)
        clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60.0 + 4.0 * i, i * 1.0, 1.0, "gh-" + std::to_string (i))));
    ToolRig t (clip);
    t.select (allIdsOf (clip));
    t.lane (core::LaneId::tone, true);

    // Pattern's depth before Apply: a ghost of the result, owned by the drawer; no step, no version
    t.settings().pat.d0 = 0.8;
    t.tools->genChanged (model::GenKind::pattern, false, "depth");
    const bool drawerGhost = t.ghosts.isShown() && t.ghosts.getOwner() == GhostPreview::drawerOwner() && t.rig.steps == 0 && t.versions == 0
                             && model::encodeClip (*t.drawnClip) == model::encodeClip (expectedPutGen (clip, t.tools->ids(), core::LaneId::tone,
                                                                                                    [] { auto g = startPattern (model::GenShape::squiggle); g.d0 = 0.8; return g; }()));
    // the mouse on another control and off it: the drawer's ghost stays; off the drawer: it goes
    const int other = 0;
    t.ghosts.leave (&other);
    const bool stays = t.ghosts.isShown();
    t.ghosts.leave (GhostPreview::drawerOwner());
    const bool gone = ! t.ghosts.isShown() && t.drawnClip == nullptr;
    // Walk's and Humanize's sliders before their first click draw too
    t.tools->walk (false, false);
    const bool walkGhost = t.ghosts.isShown() && t.ghosts.getOwner() == GhostPreview::drawerOwner()
                           && model::encodeClip (*t.drawnClip) == model::encodeClip (model::walk (clip, t.tools->ids(), model::WalkField::start(), 0.0, 0.9));
    t.ghosts.clear();
    t.tools->humanize (false, false);
    const bool humGhost = t.ghosts.isShown() && t.rig.steps == 0 && t.versions == 0;
    // a click takes it away (and makes one step)
    t.tools->applyShape (model::ShapeKey::up);
    const bool clickClears = ! t.ghosts.isShown() && t.rig.steps == 1;
    // a hover of a control that would change nothing to see draws none and takes the old one away
    t.ghosts.show (&other, model::Clip (clip));
    t.ghosts.hover (&other, [] { return std::optional<model::Clip>(); });
    const bool noneClears = ! t.ghosts.isShown();
    check (drawerGhost && stays && gone && walkGhost && humGhost && clickClears && noneClears,
           fmt ("a Pattern slider before Apply draws the drawer's ghost with no step and no version (%d); it stays off another control (%d) "
                "and goes off the drawer (%d); Walk's (%d) and Humanize's (%d) sliders draw theirs; a click takes it away (%d); a hover with "
                "nothing to show clears it (%d)",
                (int) drawerGhost, (int) stays, (int) gone, (int) walkGhost, (int) humGhost, (int) clickClears, (int) noneClears));

    // hover hooks: the hint is the control's tooltip while the mouse is on it
    juce::String hint;
    ui::roll::HoverHooks hooks (&t.ghosts, [&hint] (const juce::String& s) { hint = s; });
    juce::TextButton b ("Swell");
    b.setTooltip ("Rises to the middle, then falls");
    hooks.add (b, [&t] { return t.tools->previewShape (model::ShapeKey::swell); });
    hooks.enter (b);
    const bool on = hint == "Rises to the middle, then falls" && t.ghosts.isShown() && t.ghosts.getOwner() == &b;
    hooks.press (b);
    const bool pressed = ! t.ghosts.isShown() && hint.isNotEmpty();
    hooks.exit (b);
    check (on && pressed && hint.isEmpty() && t.rig.steps == 1,
           fmt ("a hooked button: on it, its tooltip in the hint bar and its ghost (%d); a press takes the ghost away first (%d); off it, the "
                "hint goes (%d)", (int) on, (int) pressed, (int) hint.isEmpty()));
}

//==================================================================================================
namespace
{
    struct EditorRig
    {
        StrandLockProcessor proc;
        std::unique_ptr<StrandLockEditor> editor;

        explicit EditorRig (ThemeId theme = ThemeId::dark, int width = 1100, int height = 680)
        {
            proc.waitForSounds();
            auto& s = proc.getUserSettings();
            s.setTheme (theme);
            s.setUiScalePercent (100);
            s.setKeysDefault (false);
            editor = std::make_unique<StrandLockEditor> (proc);
            editor->setLogicalSize (width, height);
            view().setFolded (true);
            editor->refreshNow();
        }

        ~EditorRig()
        {
            editor.reset();
            proc.getUserSettings().setTheme (state::Theme::dark);
        }

        ui::MainView& view() { return editor->getView(); }
        ui::roll::RollView& roll() { return view().getRoll(); }
        ui::roll::SelectionBar& bar() { return view().getSelectionBar(); }
    };
}

SL_TEST ("ui", ghostsInTheEditor)
{
    section ("Ghosts in the editor: the mouse on every drawer control, bar button, lane-menu item, right-click shape and recipe: the "
             "SnapshotPublisher's submit count, the clip and the undo history unchanged; the roll draws the ghost there and none after");

    EditorRig r;
    auto& view = r.view();
    auto& roll = r.roll();
    auto& bar = r.bar();
    auto& proc = r.proc;
    roll.setMenusShown (false);
    roll.selectAll();
    r.editor->refreshNow();

    const auto published0 = proc.getSnapshotPublisher().stats().submitted;
    const auto version0 = proc.getDocument().version();
    const auto clip0 = proc.getDocument().current();
    const bool undo0 = proc.canUndo();

    int hovered = 0, drewThere = 0, previews = 0, goneAfter = 0, hinted = 0, hintGone = 0;
    auto hoverAll = [&] (ui::roll::HoverHooks& hooks)
    {
        for (auto* c : hooks.controls())
        {
            if (! shownIn (*c, *r.editor))
                continue;
            hooks.enter (*c);
            ++hovered;
            const bool ghost = roll.getPaintState().ghost != nullptr;
            previews += ghost ? 1 : 0;
            drewThere += ghost == roll.getGhosts().isShown() ? 1 : 0;
            if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (c); tip != nullptr && tip->getTooltip().isNotEmpty())
                hinted += view.getHintBar().getHelp() == tip->getTooltip() ? 1 : 0;
            else
                ++hinted;
            hooks.exit (*c);
            goneAfter += roll.getPaintState().ghost == nullptr ? 1 : 0;
            hintGone += view.getHintBar().getHelp().isEmpty() ? 1 : 0;
        }
    };

    int drawers = 0;
    for (const auto L : { core::LaneId::tone, core::LaneId::pitch, core::LaneId::travel, core::LaneId::heads })
    {
        bar.getLaneBox().setSelectedId (core::laneIndex (L) + 1, juce::sendNotificationSync);
        for (const auto d : { ToolSettings::Drawer::shapes, ToolSettings::Drawer::pattern, ToolSettings::Drawer::random,
                              ToolSettings::Drawer::connect, ToolSettings::Drawer::tools })
        {
            bar.showDrawer (d);
            r.editor->refreshNow();
            if (auto* dr = bar.getOpenDrawer())
            {
                ++drawers;
                hoverAll (dr->getHooks());
                if (auto* tools = dynamic_cast<ui::roll::ToolsDrawer*> (dr))
                    for (const juce::String name : { "Walk", "Chop", "Humanize", "Strum" })
                    {
                        tools->getButton (name + juce::String::charToString ((juce::juce_wchar) 0x2026)).onClick();
                        if (auto* panel = tools->getPanel())
                            hoverAll (panel->getHooks());
                    }
            }
        }
    }
    hoverAll (bar.getHooks());

    // every lane menu: the mouse on each item
    int menus = 0, items = 0, itemGhosts = 0, itemGone = 0;
    for (int m = 2; m <= 8; ++m)
    {
        view.getLaneArrow (m).onClick();
        const auto& menu = roll.getMenu();
        if (! menu.empty() && menu.front().kind == ui::roll::MenuItem::Kind::header)
            ++menus;
        for (std::size_t i = 0; i < roll.getMenu().size(); ++i)
        {
            roll.menuHover ((int) i);
            ++items;
            const auto& it = roll.getMenu()[i];
            itemGhosts += (it.hover >= 0 && ! it.disabled) == (roll.getPaintState().ghost != nullptr) ? 1 : 0;
            roll.menuHover (-1);
            itemGone += roll.getPaintState().ghost == nullptr ? 1 : 0;
        }
        roll.closeMenu();
    }

    // the right-click menu's shapes (Tone mode, on a selected note)
    view.setMode (5);
    roll.selectAll();
    int rightShapes = 0, rightGhosts = 0;
    {
        const auto& geo = roll.getGeometry();
        const auto clip = proc.getDocument().current();
        const auto& n = *clip->notes.front();
        const auto at = geo.toView (n.s * geo.beatWidth() + 2.0, geo.baseYAt (n, 0.0));
        roll.rightPress ({ (float) at.x, (float) at.y }, 5000.0);
        for (std::size_t i = 0; i < roll.getMenu().size(); ++i)
            if (roll.getMenu()[i].hover >= 0)
            {
                ++rightShapes;
                roll.menuHover ((int) i);
                rightGhosts += roll.getPaintState().ghost != nullptr ? 1 : 0;
                roll.menuHover (-1);
            }
        roll.closeMenu();
    }

    // the recipes (M6's buttons call the same preview)
    int recipes = 0;
    const int recipeOwner = 0;
    for (int n = 1; n <= 8; ++n)
    {
        roll.getGhosts().hover (&recipeOwner, [&roll, n] { return roll.getTools().previewRecipe (n); });
        recipes += roll.getPaintState().ghost != nullptr ? 1 : 0;
        roll.getGhosts().leave (&recipeOwner);
    }

    const auto published = proc.getSnapshotPublisher().stats().submitted - published0;
    const bool same = proc.getDocument().current() == clip0 && proc.getDocument().version() == version0 && proc.canUndo() == undo0 && ! undo0;
    note (fmt ("%d drawers, %d hooked controls hovered (%d drew a ghost); %d lane menus, %d items; %d right-click shapes; 8 recipes",
               drawers, hovered, previews, menus, items, rightShapes));
    check (published == 0 && same, fmt ("after every hover the SnapshotPublisher had %d new submits (0), the clip the same version, no undo step",
                                        (int) published));
    check (drawers == 20 && hovered > 150 && previews > 80 && drewThere == hovered && goneAfter == hovered,
           fmt ("%d of %d controls: the roll draws exactly the ghost while the mouse is on it (%d drew one) and none after (%d)", drewThere,
                hovered, previews, goneAfter));
    check (hinted == hovered && hintGone == hovered, fmt ("%d of %d controls show their tooltip in the hint bar while the mouse is on them; %d "
                                                          "take it away after", hinted, hovered, hintGone));
    check (menus == 7 && items > 100 && itemGhosts == items && itemGone == items,
           fmt ("%d lane menus: %d of %d items draw a ghost exactly when they have one and are not greyed; %d leave none after", menus, itemGhosts,
                items, itemGone));
    check (rightShapes == 8 && rightGhosts == 8 && recipes == 8,
           fmt ("the right-click menu's %d shapes draw their ghosts (%d); the 8 recipes draw theirs (%d)", rightShapes, rightGhosts, recipes));

    // the same controls' clicks do publish and push steps
    bar.showDrawer (ToolSettings::Drawer::shapes);
    if (auto* shapes = dynamic_cast<ui::roll::ShapesDrawer*> (bar.getOpenDrawer()))
        if (auto* swell = shapes->findShape (model::ShapeKey::swell))
            swell->onClick();
    check (proc.getSnapshotPublisher().stats().submitted > published0 && proc.canUndo() && proc.undoLabel().value_or ("") == "Swell (Tone)",
           fmt ("a click on Swell publishes (%d submits) and is one undo step \"%s\"", (int) (proc.getSnapshotPublisher().stats().submitted - published0),
                proc.undoLabel().value_or ("").c_str()));
}

//==================================================================================================
// Pictures for review: a ghost from the Shapes drawer (Swell on Tone), and from the Formant lane menu (Ramp up).

namespace
{
    juce::Image shootGhost (ThemeId theme, bool fromMenu)
    {
        EditorRig r (theme);
        auto& roll = r.roll();
        auto& view = r.view();
        roll.selectAll();
        juce::Image image;
        if (fromMenu)
        {
            view.getLaneArrow (6).onClick();
            r.editor->refreshNow();
            if (auto* panel = roll.getMenuPanel())
            {
                const auto& menu = roll.getMenu();
                for (std::size_t i = 0; i < menu.size(); ++i)
                    if (menu[i].label == "Ramp up")
                    {
                        const auto at = roll.getMenuItemBounds (i).toFloat().getCentre();
                        const auto time = juce::Time (11000);
                        panel->mouseMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                                            juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                                            juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, panel, panel, time, at, time,
                                                            1, false));
                    }
            }
        }
        else
        {
            view.setMode (5);
            r.bar().getDrawerButton (ToolSettings::Drawer::shapes).onClick();
            if (auto* shapes = dynamic_cast<ui::roll::ShapesDrawer*> (r.bar().getOpenDrawer()))
                if (auto* swell = shapes->findShape (model::ShapeKey::swell))
                    shapes->getHooks().enter (*swell);
        }
        r.editor->refreshNow();
        image = r.editor->createComponentSnapshot (r.editor->getLocalBounds(), true, 1.0f);
        roll.closeMenu();
        return image;
    }
}

SL_SNAPSHOT (m5_ghost_shapes_dark)     { return shootGhost (ThemeId::dark, false); }
SL_SNAPSHOT (m5_ghost_shapes_light)    { return shootGhost (ThemeId::light, false); }
SL_SNAPSHOT (m5_ghost_lane_menu_dark)  { return shootGhost (ThemeId::dark, true); }
SL_SNAPSHOT (m5_ghost_lane_menu_light) { return shootGhost (ThemeId::light, true); }
