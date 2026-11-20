// The Sample and Macros tabs in the real editor (PLUGIN-PLAN 4 M6 "knobs", 7.8 T1 and decisions 1 and 2), against the
// real sl-ui-sample.js as Tests/fixtures/ui/readouts.json recorded it:
//
// - every control's faded state, tooltip, hint line and (for a knob) readout in four settings on Strand Demo;
// - every knob case (a drag, a double-click, a typed value) through the real knob: the parameter holds the float of the
//   prototype's value, the knob shows the prototype's value and readout exactly, and the undo steps and their labels
//   are the prototype's;
// - each control changes exactly its own parameter (0 others; Lock from another engine also picks Cloud, and Mirror
//   with one head also sets Heads, as in the prototype) and makes exactly 1 undo step, which Undo takes back whole;
// - Lock Grain typed as 4.3 stays 4.3 and plays 4 taps (a drag lands on whole waves);
// - while a knob's value box is typed in, Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z and Ctrl+D run nothing on the roll (fix pass).
// And pictures of both tabs for review.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Inst.h"
#include "core/JsonLite.h"
#include "engine/VoiceRig.h"
#include "model/Clip.h"
#include "model/Presets.h"
#include "params/ParamBridge.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/SampleTabRules.h"
#include "ui/Theme.h"
#include "ui/tabs/MacrosTab.h"
#include "ui/tabs/SampleTab.h"
#include "model/ModelFixture.h"

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;
    using ui::ThemeId;
    namespace rules = sl::ui::rules;
    using params::P;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    /** Every parameter field of an instrument, as a patch (the INST state fields stay as they are). */
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

    using Values = std::array<float, params::kNumParams>;

    Values valuesOf (StrandLockProcessor& proc)
    {
        Values v {};
        for (int i = 0; i < params::kNumParams; ++i)
            v[(std::size_t) i] = params::plainValueOf (proc.getParam (i));
        return v;
    }

    /** The parameters whose value differs (bit for bit). */
    std::vector<P> changedBetween (const Values& a, const Values& b)
    {
        std::vector<P> out;
        for (int i = 0; i < params::kNumParams; ++i)
            if (std::memcmp (&a[(std::size_t) i], &b[(std::size_t) i], sizeof (float)) != 0)
                out.push_back (static_cast<P> (i));
        return out;
    }

    std::string namesOf (const std::vector<P>& list)
    {
        std::string s;
        for (const auto p : list)
            s += (s.empty() ? "" : ", ") + std::string (params::spec (p).id);
        return s.empty() ? std::string ("none") : s;
    }

    /** An editor on a processor, with the tabs at hand and their toasts and steps counted. */
    struct TabRig
    {
        StrandLockProcessor proc;
        std::unique_ptr<StrandLockEditor> editor;
        std::vector<juce::String> toasts;
        juce::String hint;

        TabRig()
        {
            resetUserSettings (proc);
            editor = std::make_unique<StrandLockEditor> (proc);
            sample().onToast = [this] (const juce::String& text, int) { toasts.push_back (text); };
            macros().onToast = [this] (const juce::String& text, int) { toasts.push_back (text); };
            sample().onHint = [this] (const juce::String& text) { hint = text; };
            macros().onHint = [this] (const juce::String& text) { hint = text; };
        }

        ~TabRig()
        {
            sample().onToast = nullptr;
            macros().onToast = nullptr;
            sample().onHint = nullptr;
            macros().onHint = nullptr;
            editor.reset();
            resetUserSettings (proc);
        }

        MainView& view() { return editor->getView(); }
        ui::SampleTab& sample() { return editor->getView().getSampleTab(); }
        ui::MacrosTab& macros() { return editor->getView().getMacrosTab(); }

        /** The instrument (every parameter) and the clip, then the tabs and the controls follow. The history takes the
            values as a step of their own, so the next step's Undo comes back to exactly them (plan 2.4: Undo writes the
            values the history holds). */
        void set (const core::Inst& inst, const std::optional<model::Clip>& clip = std::nullopt)
        {
            params::applyPatch (proc.getApvts(), fullPatch (inst));
            if (clip)
                proc.getDocument().replace (std::make_shared<const model::Clip> (*clip));
            record();
            settle();
        }

        /** Makes the history hold every parameter's value now. */
        void record()
        {
            std::vector<P> all;
            for (int i = 0; i < params::kNumParams; ++i)
                if (i != params::index (P::bypass))
                    all.push_back (static_cast<P> (i));
            proc.pushUndoStep ("test settings", all);
        }

        void settle()
        {
            sample().sync (true);
            macros().sync (true);
            view().syncControls();
        }

        int steps() { return (int) (sample().getSteps().size() + macros().getSteps().size()); }

        std::string lastStep()
        {
            const auto& a = sample().getSteps();
            const auto& b = macros().getSteps();
            // the tab that made the newest step: the caller looks right after one gesture, so one of them grew
            if (! a.empty() && (b.empty() || lastWasSample))
                return a.back();
            return b.empty() ? std::string() : b.back();
        }

        bool lastWasSample = true;

        ui::SampleKnob* knob (const std::string& undoName)
        {
            for (int i = 0; i < 9; ++i)
                if (undoName == sample().getKnob (static_cast<rules::KnobId> (i)).getSpec().undo)
                    return &sample().getKnob (static_cast<rules::KnobId> (i));
            for (int i = 0; i < 8; ++i)
                if (undoName == macros().getMacro (i).getSpec().undo)
                    return &macros().getMacro (i);
            for (int i = 0; i < 5; ++i)
                if (undoName == macros().getOffset (i).getSpec().undo)
                    return &macros().getOffset (i);
            return nullptr;
        }
    };

    const std::string kLockEmoji = "\xf0\x9f\x94\x92 ";   // the prototype's "🔒 Lock": StrandLock draws the padlock

    std::string withoutEmoji (std::string s)
    {
        if (s.rfind (kLockEmoji, 0) == 0)
            s = s.substr (kLockEmoji.size());
        return s;
    }

    std::string utf8 (const juce::String& s) { return s.toStdString(); }
}

//==================================================================================================
SL_TEST ("ui", sampleTabControlsMatchFixture)
{
    section ("Sample and Macros tabs: every control's faded state, tooltip, hint line and readout as the prototype's (ui/readouts.json)");

    core::JsonValue doc;
    if (! fixture::load ("ui/readouts.json", doc))
        return;
    const auto& cases = doc["cases"];
    const auto& insts = doc["insts"];
    const auto demo = model::loadClipPreset ("strand-demo");
    check (demo.has_value(), "Strand Demo loads");
    if (! demo)
        return;

    TabRig r;
    std::map<std::string, std::pair<std::string, std::string>> firstTexts;   // the first setting's tooltip and hint, by control
    int settings = 0, compared = 0, same = 0, fadedSeen = 0, offSeen = 0, shown = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        if (c["fn"].asString() != "controls")
            continue;
        const std::string ctx = c["input"].asString();
        r.set (core::instFromJson (insts[ctx]), *demo);
        ++settings;

        std::map<std::string, ui::ControlState> mine;
        for (const auto& s : r.sample().controlStates())
            mine[utf8 (s.name)] = s;
        for (const auto& s : r.macros().controlStates())
            mine[utf8 (s.name)] = s;

        const auto& out = c["output"];
        check (out.size() == mine.size(), fmt ("%s: the prototype has %zu controls, the tabs %zu", ctx.c_str(), out.size(), mine.size()));
        for (std::size_t k = 0; k < out.size(); ++k)
        {
            const std::string name = withoutEmoji (out.keyAt (k));
            const auto& row = out.valueAt (k);
            auto& first = firstTexts[name];
            const std::string wantTitle = row[2].isString() ? row[2].asString() : first.first;
            const std::string wantHint = row[3].isNull() ? std::string ("(none)") : row[3].isString() ? withoutEmoji (row[3].asString()) : first.second;
            if (settings == 1)
                first = { wantTitle, wantHint };
            ++compared;
            const auto it = mine.find (name);
            if (it == mine.end())
            {
                if (shown++ < 10)
                    note (ctx + ": no control named " + name);
                continue;
            }
            const auto& s = it->second;
            const std::string gotHint = s.hint.has_value() ? utf8 (*s.hint) : std::string ("(none)");
            const bool readoutOk = row[4].isNull() ? ! s.readout.has_value() : s.readout.has_value() && utf8 (*s.readout) == row[4].asString();
            const bool ok = s.faded == (row[0].asNumber() > 0.5) && s.off == (row[1].asNumber() > 0.5) && utf8 (s.title) == wantTitle
                            && gotHint == wantHint && readoutOk;
            if (ok)
                ++same;
            else if (shown++ < 10)
                note (fmt ("%s, %s: faded %d off %d \"%s\" | \"%s\" | \"%s\"; the prototype %s", ctx.c_str(), name.c_str(), (int) s.faded, (int) s.off,
                           utf8 (s.title).c_str(), gotHint.c_str(), s.readout ? utf8 (*s.readout).c_str() : "-", core::toJson (row).c_str()));
            fadedSeen += s.faded ? 1 : 0;
            offSeen += s.off ? 1 : 0;
        }
    }
    check (settings == 4 && compared >= 4 * 50 && same == compared,
           fmt ("%d of %d controls in %d settings show the prototype's faded state, tooltip, hint line and readout", same, compared, settings));
    check (fadedSeen > 40 && offSeen > 0, fmt ("the settings fade %d controls and switch the Grain knob off %d times", fadedSeen, offSeen));
}

//==================================================================================================
SL_TEST ("ui", sampleTabKnobsMatchFixture)
{
    section ("Sample and Macros tabs: drags, double-clicks and typed values on the real knobs equal the prototype's (ui/readouts.json)");

    core::JsonValue doc;
    if (! fixture::load ("ui/readouts.json", doc))
        return;
    const auto& cases = doc["cases"];
    const auto& insts = doc["insts"];

    std::size_t mine = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const std::string fn = cases[i]["fn"].asString();
        mine += fn == "knobDrag" || fn == "knobReset" || fn == "knobType" ? 1 : 0;
    }
    modelfx::FileTally tally ("ui/readouts.json (knob gestures in the editor)", mine);

    TabRig r;
    std::string lastCtx;
    int stepsTotal = 0, liveWrites = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const std::string fn = c["fn"].asString();
        if (fn != "knobDrag" && fn != "knobReset" && fn != "knobType")
            continue;
        const auto& in = c["input"];
        const auto& out = c["output"];
        const std::string name = in[0].asString(), ctx = in[1].asString();
        if (ctx != lastCtx)
        {
            r.set (core::instFromJson (insts[ctx]));
            lastCtx = ctx;
        }
        auto* k = r.knob (name);
        if (k == nullptr)
        {
            tally.fail ("no knob " + name);
            continue;
        }
        r.lastWasSample = static_cast<int> (k->getId()) < 9;

        // the knob at its start: the setting's own value (typing), or the case's (a drag, a double-click). The history
        // takes the start as a step of its own: the prototype's harness counts each case's pushes from a fresh start,
        // and the history leaves out a step equal to its newest (so typing 4.3 after the last case's 0.5 left 5 there
        // would push nothing)
        auto& param = r.proc.getParam (k->getSpec().param);
        const auto inst = core::instFromJson (insts[ctx]);
        if (fn == "knobType")
            params::applyPatch (r.proc.getApvts(), fullPatch (inst));
        else
            params::setPlainValue (param, params::index (k->getSpec().param), (float) fixture::num (in[2]));
        r.record();
        r.settle();

        const int stepsBefore = r.steps();
        const std::size_t toastsBefore = r.toasts.size();
        bool box = false;
        int writes = 0;
        struct Counter final : juce::AudioProcessorParameter::Listener
        {
            int* n;
            void parameterValueChanged (int, float) override { ++*n; }
            void parameterGestureChanged (int, bool) override {}
        } counter;
        counter.n = &writes;
        param.addListener (&counter);
        if (fn == "knobDrag")
        {
            const double dy = fixture::num (in[3]);
            const bool shift = in[4].asBool();
            k->press (300.0);
            for (int step = 1; step <= 5; ++step)
                k->dragTo (300.0 - dy * step / 5.0, shift);
            k->release();
        }
        else if (fn == "knobReset")
        {
            k->doubleClick();
        }
        else
        {
            box = k->type (juce::String::fromUTF8 (in[2].asString().c_str()));
        }
        param.removeListener (&counter);
        r.view().syncControls();

        const double want = fixture::num (out[0]);
        const int steps = r.steps() - stepsBefore;
        const std::string label = steps > 0 ? r.lastStep() : std::string();
        const bool hasToast = fn == "knobType" ? out.size() == 6 : out.size() == 5;
        const std::string toast = r.toasts.size() > toastsBefore ? utf8 (r.toasts.back()) : std::string();
        const bool ok = juce::exactlyEqual (params::plainValueOf (param), (float) want) && fixture::same (k->value(), want)
                        && utf8 (k->readout()) == out[1].asString() && steps == (int) out[2].asNumber()
                        && label == (out[3].isNull() ? std::string() : out[3].asString()) && toast == (hasToast ? out[4].asString() : std::string())
                        && (fn != "knobType" || box == out[out.size() - 1].asBool());
        stepsTotal += steps;
        liveWrites += fn == "knobDrag" && steps == 1 && writes >= 1 ? 1 : 0;
        if (ok)
            tally.pass (true);
        else
            tally.fail (fmt ("%s %s: %.17g (param %.9g) \"%s\" %d step(s) \"%s\" toast \"%s\"%s; the prototype %s", fn.c_str(), core::toJson (in).c_str(),
                             k->value(), (double) params::plainValueOf (param), utf8 (k->readout()).c_str(), steps, label.c_str(), toast.c_str(),
                             fn == "knobType" ? (box ? " box" : " no box") : "", core::toJson (out).c_str()));
    }
    tally.finish();
    check (stepsTotal > 150 && liveWrites > 100, fmt ("the cases made %d undo steps; %d drags wrote their parameter while they moved", stepsTotal, liveWrites));
}

//==================================================================================================
SL_TEST ("ui", sampleTabEachControlOwnParameter)
{
    section ("Sample and Macros tabs: each control changes exactly its own parameter and makes one undo step (decision 1)");

    TabRig r;
    const auto init = model::applySoundPreset (core::defaultInst(), "init");
    check (init.has_value(), "the init sound preset");
    if (! init)
        return;

    int controls = 0, good = 0, undone = 0, shown = 0;
    std::function<void()> prepare;   // run before the values are taken (a knob away from its default)
    const auto run = [&] (const std::string& what, const core::Inst& start, std::vector<P> expected, const std::string& wantLabel,
                          const std::function<void()>& gesture, bool sampleTab = true)
    {
        r.set (start);
        if (prepare)
        {
            prepare();
            r.record();
            r.settle();
        }
        r.lastWasSample = sampleTab;
        const auto before = valuesOf (r.proc);
        const int stepsBefore = r.steps();
        const auto seedBefore = r.proc.getInstState().seed;
        gesture();
        const auto after = valuesOf (r.proc);
        const auto changed = changedBetween (before, after);
        const int steps = r.steps() - stepsBefore;
        const std::string label = steps > 0 ? r.lastStep() : std::string();
        const bool ok = changed == expected && steps == 1 && label == wantLabel && r.proc.undoLabel().value_or ("") == wantLabel;
        // Undo takes the whole step back: every parameter as before (and the seed for Re-roll)
        const bool back = r.proc.undo() && changedBetween (before, valuesOf (r.proc)).empty() && r.proc.getInstState().seed == seedBefore;
        ++controls;
        good += ok ? 1 : 0;
        undone += back ? 1 : 0;
        if ((! ok || ! back) && shown++ < 12)
            note (fmt ("%s: changed %s (want %s), %d step(s) \"%s\" (want \"%s\"), undo %s", what.c_str(), namesOf (changed).c_str(),
                       namesOf (expected).c_str(), steps, label.c_str(), wantLabel.c_str(), back ? "restored it" : "did NOT restore it"));
    };
    const auto with = [&init] (const core::InstPatch& patch) { return core::normalizeInst (core::mergeInst (init->inst, patch)); };
    const core::Inst I0 = init->inst;
    core::InstPatch cloudPatch;
    cloudPatch.engine = core::Engine::cloud;
    const core::Inst cloud = with (cloudPatch);

    // every knob: a drag, a double-click back, a typed value (where it takes typing)
    for (const auto id : rules::allKnobs())
    {
        const bool onSample = static_cast<int> (id) < 9;
        const core::Inst& start = id == rules::KnobId::spray ? cloud : I0;
        auto* k = onSample ? &r.sample().getKnob (id) : static_cast<int> (id) < 17 ? &r.macros().getMacro (static_cast<int> (id) - 9)
                                                                                   : &r.macros().getOffset (static_cast<int> (id) - 17);
        r.set (start);
        const auto spec = k->getSpec();
        prepare = nullptr;
        run (std::string ("drag ") + spec.undo, start, { spec.param }, spec.undo, [k] {
            k->press (300.0);
            k->dragTo (290.0, false);
            k->dragTo (260.0, false);
            k->release();
        }, onSample);
        // the double-click starts from the knob's far end (set as automation would), and goes back to the default
        auto& param = r.proc.getParam (spec.param);
        prepare = [&param, spec] { params::setPlainValue (param, params::index (spec.param), (float) spec.range.max); };
        run (std::string ("double-click ") + spec.undo, start, { spec.param }, std::string ("Reset ") + spec.undo, [k] { k->doubleClick(); }, onSample);
        prepare = nullptr;
        if (spec.parse != rules::KnobParse::none)
            run (std::string ("type ") + spec.undo, start, { spec.param }, spec.undo, [k] { k->type ("3"); }, onSample);
    }

    // the rows of buttons, the switches and the lists
    auto& s = r.sample();
    for (int i = 0; i < 3; ++i)
        if (i != 1)
            run (std::string ("Engine ") + params::kEngineChoices[i], I0, { P::engine }, std::string ("Engine: ") + params::kEngineChoices[i],
                 [&s, i] { s.getEngine().choose (i); });
    run ("Lock from Voice (Cloud with Lock, as the prototype)", I0, { P::engine, P::lock }, "Engine: Cloud with Lock", [&s] { s.getLock().toggle(); });
    run ("Lock on Cloud", cloud, { P::lock }, "Lock on", [&s] { s.getLock().toggle(); });
    for (int i = 1; i < 6; ++i)
    {
        const bool mirror = i == static_cast<int> (core::Motion::mirror);
        run (std::string ("Motion ") + params::kMotionChoices[i], I0, mirror ? std::vector<P> { P::motion, P::heads } : std::vector<P> { P::motion },
             std::string ("Motion: ") + params::kMotionChoices[i] + (mirror ? " (2 heads)" : ""), [&s, i] { s.getMotion().choose (i); });
    }
    core::InstPatch twoHeads;
    twoHeads.heads = 2.0;
    run ("Motion Mirror with 2 heads", with (twoHeads), { P::motion }, "Motion: Mirror", [&s] { s.getMotion().choose (3); });
    run ("Jump every", I0, { P::jump }, "Jump every 1/4 triplet", [&s] { s.getJump().getBox().setSelectedItemIndex (7, juce::sendNotificationSync); });
    run ("Re-roll (the seed, no parameter)", I0, {}, "Re-roll Scatter, Spray, Drift and Skip", [&s] { s.getReroll().onClick(); });
    for (int i = 1; i < 3; ++i)
        run (std::string ("Refresh ") + params::kRefreshModeChoices[i], I0, { P::refreshMode }, std::string ("Refresh: ") + params::kRefreshModeChoices[i],
             [&s, i] { s.getRefreshMode().choose (i); });
    run ("Refresh every", I0, { P::refreshRate }, "Refresh every 1/8", [&s] { s.getRefreshRate().getBox().setSelectedItemIndex (9, juce::sendNotificationSync); });
    run ("Lock to the beat", I0, { P::refreshBeatLock }, "Refresh: free of the beat", [&s] { s.getBeatLock().setToggleState (false, juce::sendNotificationSync); });
    run ("Tape switch", I0, { P::tape }, "Tape switch on", [&s] { s.getTape().setToggleState (true, juce::sendNotificationSync); });
    run ("Each grain backwards", I0, { P::reverseGrains }, "Each grain backwards on", [&s] { s.getRev().setToggleState (true, juce::sendNotificationSync); });
    for (int i = 1; i < 4; ++i)
        run (std::string ("Head tune ") + params::kHeadTuneChoices[i], I0, { P::headTune }, std::string ("Head tune: ") + params::kHeadTuneChoices[i],
             [&s, i] { s.getHeadTune().choose (i); });
    for (int i = 1; i < 3; ++i)
        run (std::string ("At the end ") + params::kAtEndChoices[i], I0, { P::atEnd }, std::string ("At the end: ") + params::kAtEndChoices[i],
             [&s, i] { s.getAtEnd().choose (i); });
    const P region[] = { P::regionStart, P::regionEnd, P::loopXfade };
    const char* regionNames[] = { "Region start", "Region end", "Loop fade" };
    for (int i = 0; i < 3; ++i)
        run (regionNames[i], I0, { region[i] }, regionNames[i], [&s, i] {
            auto& slider = s.getRegionKnob (i).getSlider();
            slider.onDragStart();
            slider.setValue (slider.getMinimum() + (slider.getMaximum() - slider.getMinimum()) * 0.37, juce::sendNotificationSync);
            slider.onDragEnd();
        });

    // the Macros tab's two buttons: every macro (offset) they move, one step
    auto& m = r.macros();
    core::InstPatch moved;
    moved.macros = std::array<double, 8> { 0.2, 0.5, 0.9, 0.5, 0.5, 0.75, 0.5, 0.5 };
    moved.offsets.tone = -0.4;
    moved.offsets.hollow = 0.25;
    run ("Centre all", with (moved), { P::macro1, P::macro3, P::macro6 }, "Centre the macros", [&m] { m.getCentreAll().onClick(); }, false);
    run ("Reset all", with (moved), { P::toneOffset, P::hollowOffset }, "Reset the offsets", [&m] { m.getResetAll().onClick(); }, false);

    check (good == controls && controls >= 80, fmt ("%d of %d control gestures changed exactly their own parameters with one undo step and its label", good, controls));
    check (undone == controls, fmt ("%d of %d undo steps put every parameter back", undone, controls));

    // a click that changes nothing makes no step (the prototype returns early)
    r.set (I0);
    const int stepsBefore = r.steps();
    s.getEngine().choose (1);                                      // Voice again
    s.getKnob (rules::KnobId::speed).doubleClick();               // at its default
    s.getKnob (rules::KnobId::speed).press (300.0);
    s.getKnob (rules::KnobId::speed).dragTo (299.0, false);        // inside the 2 px dead zone
    s.getKnob (rules::KnobId::speed).release();
    m.getCentreAll().onClick();                                    // already centred: a toast
    check (r.steps() == stepsBefore && ! r.toasts.empty() && r.toasts.back() == "The macros are already centred",
           fmt ("a click that changes nothing makes no undo step (%d made; the toast \"%s\")", r.steps() - stepsBefore,
                r.toasts.empty() ? "" : r.toasts.back().toRawUTF8()));
}

//==================================================================================================
SL_TEST ("ui", lockGrainTypedPlaysFourTaps)
{
    section ("Decision 2: Lock Grain typed as 4.3 stays 4.3 and plays 4 taps; a drag lands on whole waves");

    TabRig r;
    core::InstPatch lockPatch;
    lockPatch.engine = core::Engine::cloud;
    lockPatch.lock = true;
    const auto lockInst = core::normalizeInst (core::mergeInst (core::defaultInst(), lockPatch));
    r.set (lockInst);
    auto& grain = r.sample().getKnob (rules::KnobId::grain);

    const auto tapsNow = [&r]
    {
        test::rig::VoiceRig rig (48000.0);
        rig.setInst (r.proc.readInst());
        rig.setSample (test::rig::sampleOf (std::vector<float> (48000, 0.0f), 48000.0));
        auto& v = rig.live (60, 1.0);
        return v.heads[0].nT;
    };

    const bool typed = grain.type ("4.3");
    const float held = params::plainValueOf (r.proc.getParam (P::grainLock));
    const auto inst = r.proc.readInst();
    const int taps = tapsNow();
    check (typed && grain.getSpec().param == P::grainLock && juce::exactlyEqual (held, 4.3f) && fixture::same (grain.value(), 4.3)
               && grain.readout() == "4.3 waves" && fixture::same (inst.grainLock, (double) 4.3f) && taps == 4,
           fmt ("typed \"4.3\": the parameter holds %.9g, the knob shows %.17g (\"%s\"), the engine reads %.9g and plays %d taps", (double) held,
                grain.value(), grain.readout().toRawUTF8(), inst.grainLock, taps));

    // a drag from 4 by 10 px lands on 5 waves (qLin (1, 0.5)): whole waves, never 4.3 or 4.5
    params::setPlainValue (r.proc.getParam (P::grainLock), params::index (P::grainLock), 4.0f);
    r.settle();
    grain.press (300.0);
    grain.dragTo (290.0, false);
    grain.release();
    const int dragged = tapsNow();
    check (juce::exactlyEqual (params::plainValueOf (r.proc.getParam (P::grainLock)), 5.0f) && grain.readout() == "5 waves" && dragged == 5,
           fmt ("a 10 px drag from 4 waves lands on %.9g (\"%s\") and plays %d taps", (double) params::plainValueOf (r.proc.getParam (P::grainLock)),
                grain.readout().toRawUTF8(), dragged));

    // the same typed value through the parameter's own text (Cubase's typed entry) is kept too, and 4.5 would play 5
    auto& p = r.proc.getParam (P::grainLock);
    p.setValueNotifyingHost (p.getValueForText ("4.3"));
    const int hostTaps = tapsNow();
    params::setPlainValue (p, params::index (P::grainLock), 4.5f);
    const int halfTaps = tapsNow();
    check (std::abs (params::plainValueOf (p) - 4.5f) < 1e-6f && hostTaps == 4 && halfTaps == 5,
           fmt ("typed into Cubase as \"4.3\" it plays %d taps; 4.5 (what a 0.5 grid would make of it) plays %d", hostTaps, halfTaps));
}

//==================================================================================================
SL_TEST ("ui", sampleKnobTypingKeepsKeys)
{
    section ("While a Sample tab knob's value box is typed in, its keys are the box's: Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z and Ctrl+D run nothing on the "
             "roll, with Keys on and off, on each of the 9 knobs that take typing (fix pass M6R-PAR-1, as M4's F2 did for the inspector)");

    TabRig r;
    auto& editor = *r.editor;
    auto& doc = r.proc.getDocument();
    using M = juce::ModifierKeys;

    model::Clip clip = model::newClip (2);
    clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60.0, 0.0, 1.0, "a")));
    clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (62.0, 2.0, 1.0, "b")));
    r.set (core::defaultInst(), clip);
    r.view().openTab (MainView::sampleTab);

    // a step a Ctrl+Z on the roll would undo and one a Ctrl+Y would redo: Speed typed as 2, then 3, then Undo; and note a
    // selected, for a Ctrl+D to duplicate
    auto& speed = r.sample().getKnob (rules::KnobId::speed);
    const bool typed = speed.type ("2") && speed.type ("3") && r.proc.undo();
    doc.setSelection ({ "a" }, std::string ("a"));
    r.view().syncRollControls();
    r.settle();
    const auto clip0 = doc.current();
    const auto undo0 = r.proc.undoLabel(), redo0 = r.proc.redoLabel();
    const auto values0 = valuesOf (r.proc);
    check (typed && undo0.has_value() && redo0.has_value() && clip0->notes.size() == 2,
           fmt ("set up: an undo step (\"%s\") and a redo step (\"%s\"), note a selected", undo0.value_or ("").c_str(), redo0.value_or ("").c_str()));

    // a key goes to the box's text editor first, then up its parents to the editor, as JUCE's window hands a key over
    // (ComponentPeer::handleKeyPress). The box has no typing of its own to undo or redo, and it knows no Ctrl+D, so
    // before the fix these keys reached the roll's Undo, Redo and Duplicate.
    const juce::KeyPress keys[] = { juce::KeyPress ('z', M::commandModifier, 0), juce::KeyPress ('y', M::commandModifier, 0),
                                    juce::KeyPress ('z', M::commandModifier | M::shiftModifier, 0), juce::KeyPress ('d', M::commandModifier, 0) };
    int boxes = 0, kept = 0, stayed = 0;
    std::string firstFail;
    for (const bool keysOn : { true, false })
    {
        editor.setKeysOn (keysOn);
        for (int i = 0; i < 9; ++i)
        {
            auto& knob = r.sample().getKnob (static_cast<rules::KnobId> (i));
            knob.beginTyping();
            juce::Component* const box = knob.getTextEditor();
            if (box == nullptr)
            {
                firstFail = firstFail.empty() ? std::string (knob.getSpec().undo) + ": no box opened" : firstFail;
                continue;
            }
            ++boxes;
            bool allKept = true;
            for (const auto& key : keys)
            {
                bool used = false;
                for (juce::Component* c = box; c != nullptr && ! used; c = c->getParentComponent())
                    used = c->keyPressed (key);
                allKept = allKept && used;
            }
            const bool same = doc.current() == clip0 && r.proc.undoLabel() == undo0 && r.proc.redoLabel() == redo0
                              && changedBetween (values0, valuesOf (r.proc)).empty() && doc.rollState().selected == std::vector<std::string> { "a" };
            kept += allKept && same ? 1 : 0;
            stayed += knob.isTyping() && r.view().isTyping() ? 1 : 0;
            if (firstFail.empty() && ! (allKept && same))
                firstFail = fmt ("%s with Keys %s: %s%s (undo \"%s\", %zu notes)", knob.getSpec().undo, keysOn ? "on" : "off",
                                 allKept ? "" : "a key went on past StrandLock ", same ? "" : "the roll or the history changed",
                                 r.proc.undoLabel().value_or ("").c_str(), doc.current()->notes.size());
            knob.finishTyping();
        }
    }
    const bool closed = ! r.view().isTyping() && doc.current() == clip0 && r.proc.undoLabel() == undo0;
    check (boxes == 18 && kept == 18 && stayed == 18 && closed,
           fmt ("%d of 18 boxes (9 knobs, Keys on and off) kept all 4 keys with the clip, the undo history and every parameter unchanged; %d "
                "stayed open; closed with nothing typed, nothing changed %s", kept, stayed, firstFail.c_str()));

    // with no box open the shortcuts reach the roll again: Ctrl+D duplicates the selected note (one step)
    editor.setKeysOn (true);
    editor.keyPressed (juce::KeyPress ('D', M::commandModifier, 0));
    check (doc.current()->notes.size() == 3 && r.proc.undoLabel().value_or ("") == "Duplicate",
           fmt ("no box open, Keys on: Ctrl+D duplicates the selected note (%zu notes, undo \"%s\")", doc.current()->notes.size(),
                r.proc.undoLabel().value_or ("").c_str()));
    editor.setKeysOn (false);
}

//==================================================================================================
// Pictures for review: the Sample tab with its faded controls, in the default settings and in two others, and the
// Macros tab. A tall window shows the whole tab.

namespace
{
    struct TabShot
    {
        ThemeId theme = ThemeId::dark;
        int scale = 100;
        int width = 1100, height = 900;
        int tab = MainView::sampleTab;
        int setting = 0;   // 0 the default, 1 Tape on Bounce with 3 heads, 2 Cloud with Lock on Scatter
        bool hoverFreeze = false;
    };

    juce::Image shootTab (const TabShot& s)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();
        auto& settings = proc.getUserSettings();
        settings.setTheme (s.theme);
        settings.setUiScalePercent (s.scale);
        settings.setKeysDefault (false);

        juce::Image image;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (s.width, s.height);
            editor.getView().openTab (s.tab);
            core::InstPatch patch;
            if (s.setting == 1)
            {
                patch.engine = core::Engine::tape;
                patch.motion = core::Motion::bounce;
                patch.heads = 3.0;
                patch.refresh.mode = core::RefreshMode::sync;
                patch.headTune = core::HeadTune::fifths;
            }
            else if (s.setting == 2)
            {
                patch.engine = core::Engine::cloud;
                patch.lock = true;
                patch.motion = core::Motion::scatter;
                patch.heads = 2.5;
                patch.refresh.mode = core::RefreshMode::free;
                patch.refresh.skip = 0.25;
                patch.spray = 0.2;
                patch.macros = std::array<double, 8> { 0.75, 0.5, 0.6, 0.3, 0.5, 0.5, 0.95, 0.5 };
                patch.offsets.tone = 0.2;
                patch.offsets.amp = -0.2;
            }
            params::applyPatch (proc.getApvts(), patch);
            editor.refreshNow();
            auto& motion = editor.getView().getSampleTab().getMotion();
            if (s.hoverFreeze && motion.onHoverChange)
                motion.onHoverChange (4);   // the hint line says why Freeze does nothing in Tape
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        }
        resetUserSettings (proc);
        return image;
    }
}

SL_SNAPSHOT (m6_sample_tab_dark)          { return shootTab ({}); }
SL_SNAPSHOT (m6_sample_tab_light)         { return shootTab ({ ThemeId::light }); }
SL_SNAPSHOT (m6_sample_tab_tape_dark)     { return shootTab ({ ThemeId::dark, 100, 1100, 900, MainView::sampleTab, 1, true }); }
SL_SNAPSHOT (m6_sample_tab_lock_light)    { return shootTab ({ ThemeId::light, 100, 1100, 900, MainView::sampleTab, 2 }); }
SL_SNAPSHOT (m6_sample_tab_dark_200)      { return shootTab ({ ThemeId::dark, 200, 1100, 760 }); }
SL_SNAPSHOT (m6_sample_tab_narrow_light)  { return shootTab ({ ThemeId::light, 100, 900, 700 }); }
SL_SNAPSHOT (m6_macros_tab_dark)          { return shootTab ({ ThemeId::dark, 100, 1100, 900, MainView::macrosTab, 2 }); }
SL_SNAPSHOT (m6_macros_tab_light)         { return shootTab ({ ThemeId::light, 100, 1100, 900, MainView::macrosTab }); }
