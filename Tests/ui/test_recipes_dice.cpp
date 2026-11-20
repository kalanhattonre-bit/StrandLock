// Recipes, Dice, the pins and Preview on the real Sample tab (PLUGIN-PLAN 4 M6 "recipes and Dice" and "Preview", 7.8 T3,
// decisions 3 and 4):
//
// - each recipe button, on model/presets.json's 16 recipe cases (Strand Demo with every note, mixed-settings with three
//   notes selected), gives the fixture's clip exactly and its instrument within (double)(float) of each value (it passes
//   through the parameters, plan 1.6 #12), as one undo step with the prototype's label that Undo takes back whole; the
//   mouse on a recipe draws its notes dashed and changes nothing;
// - Dice through its button is model::dice with the pins and the seed it used, one undo step "Dice"; a pinned setting
//   never changes (each of the 14 pins, and seeded sets of them); the toast is the prototype's;
// - the pins are the person's: they survive closing and reopening (UserSettings, a fresh read of the file) and another
//   project; Unpin all lets them go;
// - Preview's switch, and the notice that Cubase has paused StrandLock: it shows after 250 ms without a block while
//   Preview is on (not at 249 ms), names the Cubase option, and goes when blocks come back; Cubase's Play turns
//   Preview off with the prototype-style toast.
//
// The runner's UserSettings file is one per run: every test here leaves the pins empty again.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Inst.h"
#include "core/JsonLite.h"
#include "model/ClipCodec.h"
#include "model/History.h"
#include "model/Presets.h"
#include "model/Recipes.h"
#include "model/ModelFixture.h"
#include "params/ParamBridge.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/Harness.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/roll/GhostPreview.h"
#include "ui/tabs/SampleTab.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;
    using params::P;
    using ui::MainView;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
        s.setDicePins ({});
    }

    /** Every parameter field of an instrument, as a patch. */
    core::InstPatch fullPatch (const core::Inst& I)
    {
        core::InstPatch p;
        p.engine = I.engine;
        p.lock = I.lock;
        p.motion = I.motion;
        p.speed = I.speed;
        p.heads = I.heads;
        p.spread = I.spread;
        p.grainVoice = I.grainVoice;
        p.grainCloud = I.grainCloud;
        p.grainLock = I.grainLock;
        p.refresh.mode = I.refresh.mode;
        p.refresh.ms = I.refresh.ms;
        p.refresh.rate = I.refresh.rate;
        p.refresh.lockToBeat = I.refresh.lockToBeat;
        p.refresh.skip = I.refresh.skip;
        p.spray = I.spray;
        p.pitch = I.pitch;
        p.tape = I.tape;
        p.rev = I.rev;
        p.drift = I.drift;
        p.headTune = I.headTune;
        p.region.start = I.region.start;
        p.region.end = I.region.end;
        p.region.atEnd = I.region.atEnd;
        p.region.xfadeMs = I.region.xfadeMs;
        p.jump = I.jump;
        p.macros = I.macros;
        p.offsets.amp = I.offsets.amp;
        p.offsets.vib = I.offsets.vib;
        p.offsets.tone = I.offsets.tone;
        p.offsets.formant = I.offsets.formant;
        p.offsets.hollow = I.offsets.hollow;
        p.glideMode = I.glideMode;
        p.glideTime = I.glideTime;
        p.vibRate = I.vibRate;
        p.root = I.root;
        p.cents = I.cents;
        p.velSens = I.velSens;
        p.polyphony = I.polyphony;
        p.outGain = I.outGain;
        p.bendRange = I.bendRange;
        p.liveLegato = I.liveLegato;
        return p;
    }

    /** The INST state fields of an instrument (plan 2.2: seed, the wheels, key, scale, scale lock). */
    core::InstPatch statePatch (const core::Inst& I)
    {
        core::InstPatch p;
        p.seed = I.seed;
        p.mod1.target = I.mod1.target;
        p.mod1.cc = I.mod1.cc;
        p.mod2.target = I.mod2.target;
        p.mod2.cc = I.mod2.cc;
        p.key = I.key;
        p.scale = I.scale;
        p.scaleLock = I.scaleLock;
        return p;
    }

    using Values = std::array<float, params::kNumParams>;

    Values valuesOf (StrandLockProcessor& proc)
    {
        Values v {};
        for (int i = 0; i < params::kNumParams; ++i)
            v[(std::size_t) i] = params::plainValueOf (proc.getParam (i));
        return v;
    }

    /** got against want, the fixture's JSON instrument: every number within (double)(float) of want (equal to it, or to
        its float), every string and switch exactly; walked over want's keys. */
    bool instWithinFloat (const JsonValue& got, const JsonValue& want, const std::string& path, std::string& why)
    {
        if (want.isObject())
        {
            if (! got.isObject())
            {
                why = path + " is not an object";
                return false;
            }
            for (std::size_t i = 0; i < want.size(); ++i)
                if (! instWithinFloat (got[want.keyAt (i)], want.valueAt (i), path + "." + want.keyAt (i), why))
                    return false;
            return true;
        }
        if (want.isArray())
        {
            if (! got.isArray() || got.size() != want.size())
            {
                why = path + ": another length";
                return false;
            }
            for (std::size_t i = 0; i < want.size(); ++i)
                if (! instWithinFloat (got[i], want[i], path + "[" + std::to_string (i) + "]", why))
                    return false;
            return true;
        }
        if (want.isNumber())
        {
            const double w = want.asNumber(), g = got.asNumber (std::nan (""));
            const double f = (double) (float) w;
            if (got.isNumber() && (juce::exactlyEqual (g, w) || juce::exactlyEqual (g, f)))
                return true;
            why = fmt ("%s: %.17g, the fixture %.17g (its float %.17g)", path.c_str(), g, w, f);
            return false;
        }
        if (core::toJson (got) == core::toJson (want))
            return true;
        why = path + ": " + core::toJson (got) + ", the fixture " + core::toJson (want);
        return false;
    }

    /** An editor on a processor, with the Sample tab at hand and its toasts and hints kept. */
    struct Rig
    {
        StrandLockProcessor proc;
        std::unique_ptr<StrandLockEditor> editor;
        std::vector<juce::String> toasts;
        juce::String hint;

        Rig()
        {
            proc.waitForSounds();
            resetUserSettings (proc);
            open();
        }

        ~Rig()
        {
            close();
            resetUserSettings (proc);
        }

        void open()
        {
            editor = std::make_unique<StrandLockEditor> (proc);
            tab().onToast = [this] (const juce::String& text, int) { toasts.push_back (text); };
            tab().onHint = [this] (const juce::String& text) { hint = text; };
        }

        void close()
        {
            if (editor == nullptr)
                return;
            tab().onToast = nullptr;
            tab().onHint = nullptr;
            editor.reset();
        }

        MainView& view() { return editor->getView(); }
        ui::SampleTab& tab() { return editor->getView().getSampleTab(); }

        /** The instrument (every parameter and the INST state) and the clip; then one undo step holds them all, so the
            next step's Undo comes back to exactly them. */
        void set (const core::Inst& inst, const std::optional<model::Clip>& clip = std::nullopt)
        {
            params::applyPatch (proc.getApvts(), fullPatch (inst));
            auto st = proc.getInstState();
            params::applyStateFields (st, statePatch (inst));
            proc.setInstState (st);
            if (clip)
                proc.getDocument().replace (std::make_shared<const model::Clip> (*clip));
            std::vector<P> all;
            for (int i = 0; i < params::kNumParams; ++i)
                if (i != params::index (P::bypass))
                    all.push_back (static_cast<P> (i));
            proc.pushUndoStep ("test settings", all);
            settle();
        }

        void settle()
        {
            view().syncControls();
            view().syncRollControls();
        }
    };

    /** Each DICE_FIELDS name's instrument fields, as instToJson writes them. */
    std::vector<std::string> fieldsOf (const std::string& pin)
    {
        if (pin == "grain")
            return { "grainVoice", "grainCloud", "grainLock" };
        return { pin };
    }

    const char* const kPausedOption = "Studio > Studio Setup > VST Plug-ins > \"Suspend VST 3 plug-in processing when no audio signals are received\"";
}

//==================================================================================================
SL_TEST ("ui", recipesThroughTheSampleTab)
{
    section ("Recipes 1-8 through the Sample tab's buttons (model/presets.json's 16 cases): the fixture's clip exactly, its instrument within "
             "(double)(float), one undo step each with the prototype's label, which Undo takes back whole");

    JsonValue doc;
    if (! fixture::load ("model/presets.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    std::map<std::string, core::Inst> insts;
    const JsonValue& instTable = doc["insts"];
    for (std::size_t i = 0; i < instTable.size(); ++i)
        insts[instTable.keyAt (i)] = core::instFromJson (instTable.valueAt (i));

    Rig rig;
    auto& tab = rig.tab();
    int cases = 0, clipsExact = 0, clipsBitForBit = 0, instsWithin = 0, oneStep = 0, undone = 0, toasts = 0, notesChanged = 0, unchanged = 0;
    std::string firstFail;
    const JsonValue& all = doc["cases"];
    for (std::size_t ci = 0; ci < all.size(); ++ci)
    {
        const JsonValue& c = all[ci];
        if (c["kind"].asString() != "recipe")
            continue;
        ++cases;
        const int n = static_cast<int> (c["n"].asNumber());
        const std::string cname = c["clip"].asString(), iname = c["inst"].asString();
        std::optional<model::Clip> clip;
        if (cname.rfind ("preset:", 0) == 0)
            clip = model::loadClipPreset (cname.substr (7));
        else if (const auto it = clips.find (cname); it != clips.end())
            clip = it->second;
        const auto inst = insts.find (iname);
        if (! clip.has_value() || inst == insts.end())
        {
            firstFail = firstFail.empty() ? "no clip or inst for case " + std::to_string (ci) : firstFail;
            continue;
        }

        rig.set (inst->second, clip);
        const auto ids = modelfx::idsFrom (c["ids"]);
        rig.proc.getDocument().setSelection (ids.isAll() ? std::vector<std::string> {} : ids.list(), std::string());
        rig.settle();
        const auto before = rig.proc.getDocument().current();
        const auto bytesBefore = model::encodeClip (*before);
        const auto valuesBefore = valuesOf (rig.proc);
        const auto stepsBefore = tab.getSteps().size();
        const auto toastsBefore = rig.toasts.size();

        tab.getRecipe (n).onClick();   // the button's click

        // the clip, every note: as the fixture has it (the notes the recipe made) or as it was (the others), in the
        // fixture's order. The document keeps the clip it had when the recipe changed nothing in it (ClipDocument's
        // sameClip), so the notes are compared, not their objects.
        const auto after = rig.proc.getDocument().current();
        std::map<std::string, model::Note> madeById;
        bool decoded = true;
        std::string why;
        const JsonValue& madeJson = c["result"]["notes"];
        for (std::size_t k = 0; k < madeJson.size(); ++k)
        {
            model::Note note;
            if (modelfx::noteFrom (madeJson[k], note, why))
                madeById[note.id] = note;
            else
                decoded = false;
        }
        const auto order = modelfx::stringsFrom (c["result"]["order"]);
        bool exact = true;
        bool clipOk = decoded && after->notes.size() == order.size();
        for (std::size_t k = 0; clipOk && k < order.size(); ++k)
        {
            const auto& got = *after->notes[k];
            if (got.id != order[k])
            {
                clipOk = false;
                why = "note " + std::to_string (k) + " is " + got.id + ", the fixture's " + order[k];
            }
            else if (const auto it = madeById.find (got.id); it != madeById.end())
            {
                // the fixture's own tolerance (plan 5.3, model/presets.json: exact, node values within 1e-12): glibc's pow
                // rounds a few Strand Demo nodes 1 ulp from V8's, in the clip preset itself (model/presetsFixture counts them)
                clipOk = modelfx::notesClose (got, it->second, 1e-12, 0.0, exact, why);
            }
            else
            {
                const auto* was = modelfx::findNote (*before, got.id);
                clipOk = was != nullptr && modelfx::bitIdentical (got, *was, why);
            }
        }
        clipsExact += clipOk ? 1 : 0;
        clipsBitForBit += clipOk && exact ? 1 : 0;
        notesChanged += after != before ? 1 : 0;

        std::string whyInst;
        const bool instOk = instWithinFloat (core::instToJson (rig.proc.readInst()), c["result"]["inst"], "inst", whyInst);
        instsWithin += instOk ? 1 : 0;

        // one undo step with the prototype's label; none when the recipe changed nothing at all (the history keeps no
        // step equal to the one before, as the prototype's does)
        const std::string label = "Recipe " + std::to_string (n) + " " + model::recipes()[(std::size_t) n - 1].name;
        const bool changedSomething = after != before || valuesOf (rig.proc) != valuesBefore;
        unchanged += changedSomething ? 0 : 1;
        const bool stepOk = changedSomething ? tab.getSteps().size() == stepsBefore + 1 && tab.getSteps().back() == label
                                                   && rig.proc.undoLabel().value_or ("") == label
                                             : tab.getSteps().size() == stepsBefore;
        oneStep += stepOk ? 1 : 0;
        const bool toastOk = rig.toasts.size() == toastsBefore + 1 && rig.toasts.back().startsWith ("Recipe " + juce::String (n) + " ")
                             && rig.toasts.back().endsWith (". A note keeps what it has in its own Note settings.");
        toasts += toastOk ? 1 : 0;

        if (changedSomething)
            rig.proc.undo();
        const bool undoOk = model::encodeClip (*rig.proc.getDocument().current()) == bytesBefore && valuesOf (rig.proc) == valuesBefore;
        undone += undoOk ? 1 : 0;

        if (firstFail.empty() && ! (clipOk && instOk && stepOk && toastOk && undoOk))
            firstFail = fmt ("recipe %d on %s: clip %s%s, inst %s, step %s, toast %s, undo %s", n, cname.c_str(), clipOk ? "ok" : "differs: ", why.c_str(),
                             instOk ? "ok" : whyInst.c_str(), stepOk ? "ok" : "wrong", toastOk ? "ok" : rig.toasts.empty() ? "none" : rig.toasts.back().toRawUTF8(),
                             undoOk ? "ok" : "differs");
        rig.proc.getDocument().setSelection ({}, std::string());
    }
    note (fmt ("%d recipe cases: %d changed the notes (recipes 7 and 8 never do), %d changed nothing at all", cases, notesChanged, unchanged));
    check (cases == 16 && clipsExact == cases && instsWithin == cases,
           fmt ("%d of %d clips the fixture's exactly (node values within 1e-12, %d of them bit for bit), %d of %d instruments within "
                "(double)(float) %s", clipsExact, cases, clipsBitForBit, instsWithin, cases, firstFail.c_str()));
    check (oneStep == cases && toasts == cases && undone == cases && notesChanged > 0 && unchanged < cases,
           fmt ("%d of %d made one undo step with the prototype's label (none when nothing changed) and its toast; Undo brought back the clip's bytes "
                "and every parameter %d times", oneStep, cases, undone));

    // a no-op is not a pass: a recipe really changes the instrument (from Strand Demo's own settings, recipe 8 is Cloud
    // with Lock on 3 heads)
    rig.set (core::defaultInst());
    tab.getRecipe (8).onClick();
    const auto i8 = rig.proc.readInst();
    check (i8.engine == core::Engine::cloud && i8.lock && juce::exactlyEqual (i8.heads, 3.0) && i8.headTune == core::HeadTune::octaves,
           "recipe 8 on the default instrument plays Cloud with Lock, 3 heads, Octaves");
}

SL_TEST ("ui", recipeHoverDrawsAndChangesNothing)
{
    section ("The mouse on a recipe button draws its notes dashed on the roll and says what it does in the hint line; it pushes no undo step and "
             "publishes no snapshot; a press takes the dashed notes away");

    Rig rig;
    auto& tab = rig.tab();
    auto& roll = rig.view().getRoll();
    auto* hooks = tab.getRecipeHooks();
    rig.proc.waitForSnapshot();
    const auto clip = rig.proc.getDocument().current();
    const auto label = rig.proc.undoLabel();
    const auto published = rig.proc.getSnapshotPublisher().stats().submitted;
    int drawn = 0, hinted = 0, differ = 0, sameAsModel = 0;
    for (int n = 1; n <= 8; ++n)
    {
        auto& button = tab.getRecipe (n);
        const bool entered = hooks != nullptr && hooks->enter (button);
        // the would-be clip goes to the roll (sl-ui-sample.js A.setGhost (applyRecipe (...).clip)), and the roll draws
        // dashed only the notes that differ: recipes 1 to 6 change the notes' Sample lines, 7 and 8 never do
        const auto& ghost = roll.getGhosts();
        drawn += entered && ghost.isShown() ? 1 : 0;
        if (ghost.getClip() != nullptr)
        {
            differ += model::sameClip (*ghost.getClip(), *clip) ? 0 : 1;
            const auto direct = model::applyRecipe (*clip, rig.proc.readInst(), n, model::Ids::all());
            sameAsModel += direct.has_value() && model::encodeClip (direct->clip) == model::encodeClip (*ghost.getClip()) ? 1 : 0;
        }
        hinted += rig.hint.startsWith ("Recipe " + juce::String (n) + " ") && rig.hint.endsWith ("; the dashed lines preview the notes.") ? 1 : 0;
        if (hooks != nullptr)
            hooks->exit (button);
    }
    note (fmt ("%d of the 8 would-be clips differ from Strand Demo's notes", differ));
    check (drawn == 8 && sameAsModel == 8 && hinted == 8,
           fmt ("8 recipes: %d handed the roll their would-be notes, %d of them the bytes of model::applyRecipe on every note, %d said what they do",
                drawn, sameAsModel, hinted));
    rig.proc.waitForSnapshot();
    check (rig.proc.getDocument().current() == clip && rig.proc.undoLabel() == label && rig.proc.getSnapshotPublisher().stats().submitted == published,
           "the hovers changed no note, pushed no undo step and published no snapshot");

    if (hooks != nullptr)
    {
        hooks->enter (tab.getRecipe (1));
        const bool shown = roll.getGhosts().isShown();
        hooks->press (tab.getRecipe (1));
        check (shown && ! roll.getGhosts().isShown(), "a press on a recipe takes its dashed notes away before it acts");
    }
}

SL_TEST ("ui", diceKeepsPinnedSettings)
{
    section ("Dice through its button: model::dice with the pins and the seed it used, one undo step \"Dice\"; a pinned setting never changes "
             "(each of the 14 pins alone, and 120 seeded sets of them)");

    Rig rig;
    auto& tab = rig.tab();
    const auto& fields = model::kDiceFields;
    check (tab.getPinButtons().size() == std::size (fields), fmt ("a pin for each of the %zu Dice fields (%zu)", std::size (fields), tab.getPinButtons().size()));
    int matched = 0, pinnedKept = 0, oneStep = 0, rolls = 0;
    int unpinnedChanged = 0, unpinnedSeen = 0;
    std::string firstFail;
    juce::Random rng (2024);

    const auto roll = [&] (const std::vector<std::string>& wanted)
    {
        // the pins through their buttons
        for (const auto& b : tab.getPinButtons())
        {
            const bool want = std::find (wanted.begin(), wanted.end(), b->getField()) != wanted.end();
            if (want != b->isPinned())
                b->onClick();
        }
        const auto pins = tab.getPins();
        const auto before = rig.proc.readInst();
        const auto beforeJson = core::instToJson (before);
        const auto steps = tab.getSteps().size();
        tab.getDice().onClick();
        ++rolls;
        const auto after = core::instToJson (rig.proc.readInst());
        const auto expected = core::instToJson (model::dice (before, (double) tab.getLastDiceSeed(), pins));
        std::string why;
        const bool same = instWithinFloat (after, expected, "inst", why);
        matched += same ? 1 : 0;
        bool kept = true;
        for (const auto& pin : pins)
            for (const auto& f : fieldsOf (pin))
                kept = kept && core::toJson (after[f]) == core::toJson (beforeJson[f]);
        pinnedKept += kept ? 1 : 0;
        for (const char* f : fields)
            if (std::find (pins.begin(), pins.end(), std::string (f)) == pins.end())
                for (const auto& key : fieldsOf (f))
                {
                    ++unpinnedSeen;
                    unpinnedChanged += core::toJson (after[key]) != core::toJson (beforeJson[key]) ? 1 : 0;
                }
        const bool step = tab.getSteps().size() <= steps + 1 && (tab.getSteps().size() == steps || tab.getSteps().back() == "Dice");
        oneStep += step ? 1 : 0;
        if (firstFail.empty() && ! (same && kept && step))
            firstFail = fmt ("roll %d (seed %d): %s%s%s", rolls, (int) tab.getLastDiceSeed(), same ? "" : why.c_str(), kept ? "" : " a pinned field changed",
                             step ? "" : " not one step");
    };

    // each pin alone, then seeded sets of pins, from two instruments
    for (const char* f : fields)
        roll ({ f });
    rig.set (core::defaultInst());
    for (int i = 0; i < 120; ++i)
    {
        std::vector<std::string> some;
        for (const char* f : fields)
            if (rng.nextInt (3) == 0)
                some.push_back (f);
        roll (some);
    }
    check (rolls == 134 && matched == rolls && pinnedKept == rolls && oneStep == rolls,
           fmt ("%d rolls: %d gave model::dice of the instrument before with the pins and the roll's seed, %d left every pinned field as it was, %d "
                "were at most one undo step \"Dice\" %s", rolls, matched, pinnedKept, oneStep, firstFail.c_str()));
    // a no-op is not a pass: the unpinned settings do change
    check (unpinnedChanged > unpinnedSeen / 3, fmt ("the unpinned fields changed %d times in %d (Dice rolls them)", unpinnedChanged, unpinnedSeen));

    // the toast says what was rolled (sl-ui-sample.js roll)
    tab.unpinAll();
    rig.toasts.clear();
    tab.getDice().onClick();
    const auto I = rig.proc.readInst();
    const juce::String engineName = I.engine == core::Engine::tape ? "Tape" : I.engine == core::Engine::voice ? "Voice" : "Cloud";
    const bool toastOk = ! rig.toasts.empty() && rig.toasts.back().startsWith ("Dice: " + engineName)
                         && rig.toasts.back().contains (juce::String::fromUTF8 (" \xc2\xb7 ")) && rig.toasts.back().endsWith (". Undo steps back.");
    check (toastOk, "the toast: \"" + (rig.toasts.empty() ? std::string() : rig.toasts.back().toStdString()) + "\"");
}

SL_TEST ("ui", dicePinsArePerPerson)
{
    section ("The Dice pins are the person's (decision 4): kept in UserSettings at once, they survive closing and reopening and show in another "
             "project; Unpin all lets them go");

    Rig rig;
    for (const char* f : { "engine", "grain", "refresh" })
        if (auto* pin = rig.tab().getPin (f))
            pin->onClick();
    const std::vector<std::string> want { "engine", "grain", "refresh" };
    const auto file = rig.proc.getUserSettings().getFile();
    check (rig.proc.getUserSettings().dicePins() == want && rig.tab().getDiceInfo() == "Pinned: Engine, Grain, Refresh" && rig.tab().getUnpinAll().isVisible(),
           "three pins: kept at once, \"" + rig.tab().getDiceInfo().toStdString() + "\", Unpin all shows");

    // closed and opened again, and a fresh read of the file (as a new Cubase would)
    rig.close();
    {
        state::UserSettings fresh (file);
        check (fresh.dicePins() == want, "a fresh read of the settings file holds the three pins");
    }
    rig.open();
    int lit = 0;
    for (const auto& b : rig.tab().getPinButtons())
        lit += b->isPinned() == (std::find (want.begin(), want.end(), b->getField()) != want.end()) ? 1 : 0;
    check (lit == (int) rig.tab().getPinButtons().size() && rig.tab().getPins() == want,
           fmt ("the editor opened again: %d of %zu pins as they were", lit, rig.tab().getPinButtons().size()));

    // another project (a new processor) shows them too
    {
        StrandLockProcessor other;
        StrandLockEditor second (other);
        auto* pin = second.getView().getSampleTab().getPin ("grain");
        check (pin != nullptr && pin->isPinned() && second.getView().getSampleTab().getPins() == want, "another project's StrandLock shows the same pins");
    }

    // the stored list holds Dice fields only, each once (the prototype filters the same way)
    rig.proc.getUserSettings().setDicePins ({ "engine", "nope", "engine", "jump" });
    check (rig.proc.getUserSettings().dicePins() == std::vector<std::string> { "engine", "jump" }, "an unknown name and a repeat are left out");

    rig.tab().unpinAll();
    {
        state::UserSettings fresh (file);
        check (fresh.dicePins().empty() && rig.tab().getPins().empty() && ! rig.tab().getUnpinAll().isVisible()
                   && rig.tab().getDiceInfo() == "Pin a setting (the small pin next to its name) to keep it when you roll.",
               "Unpin all: no pin in the file, the prototype's sentence, Unpin all hidden");
    }
}

SL_TEST ("ui", previewSwitchAndPausedNotice)
{
    section ("Preview on the Sample tab: the switch; with no block for 250 ms while it is on the tab says Cubase has paused StrandLock and names "
             "the option (not at 249 ms), and the notice goes when blocks come back; Cubase's Play turns Preview off with a toast");

    // the watch alone
    {
        ui::PreviewWatch w;
        const bool a = ! w.update (0.0, 5, false) && ! w.update (1000.0, 5, false);   // off: never
        const bool b = ! w.update (1000.0, 5, true) && ! w.update (1249.0, 5, true) && w.update (1250.0, 5, true);
        const bool c = ! w.update (1251.0, 6, true) && ! w.update (1500.0, 6, true) && w.update (1501.0, 6, true);
        const bool d = ! w.update (1502.0, 6, false) && ! w.update (1503.0, 6, true) && ! w.update (1752.0, 6, true) && w.update (1753.0, 6, true);
        check (a && b && c && d, "PreviewWatch: never while off; paused from 250 ms after Preview went on or the last block, not at 249 ms");
    }

    Rig rig;
    auto& p = rig.proc;
    auto& tab = rig.tab();
    double now = 10000.0;
    tab.clock = [&now] { return now; };
    harness::FakePlayHead head;
    head.info.setIsPlaying (false);
    head.info.setBpm (120.0);
    head.info.setPpqPosition (0.0);
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    rig.view().openTab (MainView::sampleTab);
    rig.editor->setLogicalSize (1100, 900);

    tab.getPreview().onClick();
    const bool switched = p.isPreviewOn() && tab.getPreview().getToggleState() && tab.getPreview().getButtonText() == "Stop preview";
    // blocks arrive: no notice
    for (int i = 0; i < 10; ++i)
    {
        p.processBlock (buffer, midi);
        now += 33.0;
        tab.sync();
    }
    const bool playing = p.isPreviewPlaying() && ! tab.isNoticeShown();
    check (switched && playing, "Preview on: the switch is on and says Stop preview, blocks arrive, the clip plays, no notice");

    // no block from now on: at 249 ms nothing, at 250 ms the notice, above the rows
    const auto firstRowY = tab.getEngine().getY();
    now += 249.0;
    tab.sync();
    const bool notYet = ! tab.isNoticeShown();
    now += 1.0;
    tab.sync();
    const auto text = tab.getNotice().getText();
    const bool shown = tab.isNoticeShown() && text.startsWith ("Cubase has paused StrandLock while it is stopped") && text.contains (kPausedOption);
    const bool above = shown && tab.getNotice().getBottom() < tab.getEngine().getY() && tab.getEngine().getY() > firstRowY
                       && tab.getNotice().getHeight() >= 20;
    check (notYet && shown && above,
           fmt ("no block for 249 ms: no notice; for 250 ms: \"%s\" above the rows (the first row moved from %d to %d px)", text.toRawUTF8(), firstRowY,
                tab.getEngine().getY()));

    // a block comes back: the notice goes, the rows come back up
    p.processBlock (buffer, midi);
    tab.sync();
    check (! tab.isNoticeShown() && tab.getEngine().getY() == firstRowY, "a block arrives: the notice goes and the rows move back");

    // Preview off: no notice however long no block comes
    tab.getPreview().onClick();
    now += 5000.0;
    tab.sync();
    check (! p.isPreviewOn() && ! tab.isNoticeShown() && tab.getPreview().getButtonText() == "Preview", "Preview off: no notice after 5 s without a block");

    // Cubase plays while Preview is on: the audio thread turns it off, and the tab says why
    tab.getPreview().onClick();
    rig.toasts.clear();
    head.info.setIsPlaying (true);
    p.processBlock (buffer, midi);
    tab.sync();
    check (! p.isPreviewOn() && ! tab.getPreview().getToggleState() && ! rig.toasts.empty()
               && rig.toasts.back() == "Cubase is playing, so Preview stopped. Preview plays while Cubase is stopped.",
           "Cubase plays: Preview goes off, the button with it, and the toast says why");

    tab.clock = nullptr;
    p.setPlayHead (nullptr);
    p.releaseResources();
}
