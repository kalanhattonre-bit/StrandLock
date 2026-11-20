#include "SampleTab.h"

#include "PluginProcessor.h"
#include "core/JsCompat.h"
#include "core/Rates.h"
#include "model/Recipes.h"
#include "params/ParamBridge.h"
#include "params/ParamText.h"
#include "params/ParameterLayout.h"
#include "ui/roll/GhostPreview.h"
#include "ui/roll/RollView.h"
#include "ui/roll/SelectionBar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sl::ui
{
    namespace
    {
        using params::P;
        using rules::Rule;

        constexpr int kControlHeight = 26;   // a button or select at this window height (TabPanel's)
        constexpr int kDiceInfoMax = 560;    // what Dice keeps: at most this wide, then two lines
        constexpr float kFadedAlpha = 0.42f, kFadedOnAlpha = 0.6f;

        juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

        // sl-ui-sample.js buildSampleTab's words
        const juce::String kLockTitle = "Cloud only: each grab repeats one wave of the note you play, so anything plays exactly in tune (GrainLock's trick).";
        const juce::String kJumpTitle = "How often Scatter jumps, locked to the beat.";
        const juce::String kEveryTitle = "Synced Refresh: a fresh grab every this note value.";
        const juce::String kBeatTitle = "Synced Refresh lines up with the song's beat (off = counts from each note's start).";
        const juce::String kTapeTitle = "Pitch follows speed in Voice and Cloud too: grains read faster or slower with the heads.";
        const juce::String kRevTitle = "Plays what is inside each grain backwards, whichever way the heads travel.";
        const juce::String kRerollBase = "New random spots for Scatter jumps, Spray, Drift, Skip and random Starts (one undo step). They stay the same on every loop until you Re-roll again.";
        const juce::String kRefreshTitle = "How often a head takes a fresh grab of the sound; in between, the last grab repeats. In Tape it repeats the last chunk (stutter, beat repeat).";

        // sl-ui-sample.js buildRecipes and roll (T3)
        const juce::String kDiceTitle = "Roll new Sample settings within musical limits. Pinned settings stay. Each roll is one undo step and stays put until the next roll.";
        const juce::String kDiceHint = "Dice: rolls new Sample settings (engine, motion, heads, grain, refresh...) within musical limits. A pinned setting stays. Undo steps back.";
        const juce::String kUnpinTitle = "Let Dice roll every setting again";
        const juce::String kNoPins = "Pin a setting (the small pin next to its name) to keep it when you roll.";
        const juce::String kScopeAll = "One click sets the Sample tab for every note. Some recipes also set Sample lines (Travel, Heads, Grain, Refresh): on every note (select notes to limit that).";

        // Preview (decision 3)
        const juce::String kPreviewTitle = "Preview: while Cubase is stopped, plays the clip at Cubase's tempo from where Cubase's cursor is. It stops when Cubase plays.";
        const juce::String kPreviewOnTitle = "Stop preview: Preview is playing the clip while Cubase is stopped. Click to stop it.";
        const juce::String kPausedText = "Cubase has paused StrandLock while it is stopped, so Preview cannot play. To use Preview, turn off this option in Cubase: "
                                         "Studio > Studio Setup > VST Plug-ins > \"Suspend VST 3 plug-in processing when no audio signals are received\".";
        const juce::String kStoppedByCubase = "Cubase is playing, so Preview stopped. Preview plays while Cubase is stopped.";

        /** sl-ui-sample.js RECIPE_TIPS. */
        const char* recipeTip (int n) noexcept
        {
            switch (n)
            {
                case 1: return "Plays the sound straight through with one head (Voice).";
                case 2: return "Plays backwards.";
                case 3: return "Holds still with 4 slightly detuned heads.";
                case 4: return "Tape: the heads rock forwards and backwards every 1/8.";
                case 5: return "Tape: repeats the last 1/16 like a beat repeat.";
                case 6: return "Cloud at half speed: the heads grow from 1 to more across each note.";
                case 7: return "Cloud: 3 heads jump to new spots every 1/8 triplet.";
                case 8: return "Cloud with Lock: 3 heads an octave apart, in tune on anything.";
                default: return "";
            }
        }

        /** sl-ui-sample.js PIN_NAME. */
        juce::String pinName (const std::string& field)
        {
            static const std::pair<const char*, const char*> names[] = {
                { "engine", "Engine" }, { "lock", "Lock" }, { "motion", "Motion" }, { "speed", "Speed" }, { "heads", "Heads" },
                { "spread", "Spread" }, { "grain", "Grain" }, { "refresh", "Refresh" }, { "spray", "Spray" }, { "tape", "Tape switch" },
                { "rev", "Each grain backwards" }, { "drift", "Drift" }, { "headTune", "Head tune" }, { "jump", "Jump every" }
            };
            for (const auto& [f, name] : names)
                if (field == f)
                    return name;
            return juce::String (field);
        }

        /** sl-ui-sample.js recipeLanes: what a recipe does to the notes' Sample lines, in words ("resets the Travel,
            Heads, Grain and Refresh lines", "puts a pattern on the Travel line", "shapes the Heads line"). */
        juce::String recipeLanes (const model::Recipe& r)
        {
            const auto list = [] (const juce::StringArray& w)
            {
                if (w.size() > 1)
                {
                    juce::StringArray head (w);
                    head.remove (head.size() - 1);
                    return head.joinIntoString (", ") + " and " + w[w.size() - 1] + " lines";
                }
                return w[0] + " line";
            };
            juce::StringArray reset, shaped, pattern;
            for (const auto& lane : r.notes)
            {
                const juce::String word (core::laneInfo (lane.lane).name);
                if (lane.action == model::RecipeLane::Action::reset)
                    reset.add (word);
                else if (lane.action == model::RecipeLane::Action::shape)
                    shaped.add (word);
                else
                    pattern.add (word);
            }
            juce::StringArray out;
            if (! reset.isEmpty())
                out.add ("resets the " + list (reset));
            if (! shaped.isEmpty())
                out.add ("shapes the " + list (shaped));
            if (! pattern.isEmpty())
                out.add ("puts a pattern on the " + list (pattern));
            return out.joinIntoString (" and ");
        }

        const model::Recipe* recipeOf (int n)
        {
            for (const auto& r : model::recipes())
                if (r.n == n)
                    return &r;
            return nullptr;
        }

        /** "the 3 selected notes" / "the 1 selected note" / "every note" (sl-ui-sample.js applyRecipeN's sel). */
        juce::String notesWord (std::size_t selected)
        {
            if (selected == 0)
                return "every note";
            return "the " + juce::String ((int) selected) + (selected == 1 ? " selected note" : " selected notes");
        }

        /** The Sample settings a recipe and Dice set (sl-model.js STRAIGHT's keys, which are DICE_FIELDS' too): exactly
            these parameters change, as the prototype's patch changes exactly these fields. */
        core::InstPatch samplePatch (const core::Inst& I)
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
            p.tape = I.tape;
            p.rev = I.rev;
            p.drift = I.drift;
            p.headTune = I.headTune;
            p.jump = I.jump;
            return p;
        }

        std::vector<P> paramsOf (const std::vector<params::ParamChange>& changed)
        {
            std::vector<P> out;
            for (const auto& c : changed)
                out.push_back (static_cast<P> (c.first));
            return out;
        }

        /** The rate's words as the parameter lists them ("1/8 triplet"). */
        juce::String rateLabel (const juce::RangedAudioParameter& p)
        {
            const int i = juce::roundToInt (params::plainValueOf (p));
            return i >= 0 && i < core::kNumRates ? juce::String (core::kRates[i].label) : juce::String();
        }

        /** sl-ui-sample.js toggle () and rateSel (): the hint line's text. */
        juce::String plainHint (const juce::String& name, const juce::String& title, const rules::Fade* st)
        {
            if (st != nullptr && st->faded)
                return name + ": does nothing now. " + str (st->reason);
            return name + ": " + title;
        }
    }

    //==============================================================================================
    bool SettingsWatch::update (bool force)
    {
        std::array<float, params::kNumParams> now {};
        for (int i = 0; i < params::kNumParams; ++i)
            now[(std::size_t) i] = params::plainValueOf (processor.getParam (i));
        const auto inst = processor.readInst();
        auto clip = processor.getDocument().current();
        const bool same = read && ! force && clip == clipNow && std::memcmp (now.data(), values.data(), sizeof (float) * now.size()) == 0
                          && inst.seed == seed && inst.mod1.target == mod1 && inst.mod2.target == mod2;
        if (same)
            return false;
        read = true;
        values = now;
        seed = inst.seed;
        mod1 = inst.mod1.target;
        mod2 = inst.mod2.target;
        clipNow = std::move (clip);
        current = inst;
        fadesNow = rules::fades (current, clipNow.get());
        grain = rules::grainKind (current, clipNow.get());
        return true;
    }

    //==============================================================================================
    Fader::Fader (juce::Component& c, std::function<juce::String()> text, std::function<void (const juce::String&)> show)
        : target (c), hintText (std::move (text)), showHint (std::move (show)), watch (c, [this] (bool over) { hoverChanged (over); })
    {
    }

    void Fader::setFaded (bool shouldFade)
    {
        if (faded == shouldFade)
            return;
        faded = shouldFade;
        apply();
    }

    void Fader::apply()
    {
        const float alpha = faded ? (hovering ? 0.6f : kFadedAlpha) : 1.0f;
        if (! juce::exactlyEqual (target.getAlpha(), alpha))
            target.setAlpha (alpha);
    }

    void Fader::hoverChanged (bool over)
    {
        hovering = over;
        apply();
        if (showHint && hintText)
            showHint (over ? hintText() : juce::String());
    }

    //==============================================================================================
    bool PreviewWatch::update (double nowMs, std::uint64_t blocks, bool previewOn) noexcept
    {
        if (! previewOn)
        {
            on = false;
            paused = false;
            return false;
        }
        if (! on || blocks != lastBlocks)
        {
            // Preview went on, or a block arrived: the 250 ms count from now
            on = true;
            lastBlocks = blocks;
            since = nowMs;
        }
        paused = nowMs - since >= pausedAfterMs;
        return paused;
    }

    //==============================================================================================
    SampleTab::SampleTab (const Theme& t, juce::AudioProcessorValueTreeState& s, StrandLockProcessor& p, roll::RollView& r)
        : TabPanel (t, s), processor (p), roll (r), settings (p), notice (t)
    {
        actions.commit = [this] (const std::string& label, const std::vector<P>& changed)
        {
            const bool pushed = processor.pushUndoStep (label, changed);
            if (pushed)
                steps.push_back (label);
            sync();
            return pushed;
        };
        actions.hint = [this] (const juce::String& text) { hint (text); };
        actions.toast = [this] (const juce::String& text, int ms) { toast (text, ms); };
        const auto showHint = [this] (const juce::String& text) { hint (text); };

        pins = processor.getUserSettings().dicePins();   // decision 4: the person's pins, from every project before

        // Engine and Lock, Motion, Jump every, Re-roll (sl-ui-sample.js buildSampleTab's first row), and Preview
        engine = addSeg (P::engine, "Engine",
                         { { { "Tape", "Speed and pitch move together, like tape: backwards sounds reversed and slower drops the pitch." } },
                           { { "Voice", "Stays in tune with the key you play and keeps the sound's character, slowed down, frozen or backwards. Best on one clear note." } },
                           { { "Cloud", "Grains that work on anything: chords, songs, noise. Switch Lock on to play any of it in tune." } } },
                         std::nullopt, -1, std::nullopt, "engine").control;
        {
            auto l = std::make_unique<LockToggle> (t, s, actions);
            const int w = l->preferredWidth();
            lock = l.get();
            auto group = std::make_unique<PinGroup> (std::move (l), w, makePin ("lock"));
            const int gw = group->preferredWidth();
            add (std::move (group), gw, kControlHeight);
            bound (P::lock);
            lockFader = std::make_unique<Fader> (*lock, [this] { return plainHint ("Lock", kLockTitle, fadeOf (Rule::lock)); }, showHint);
        }
        {
            auto& seg = addSeg (P::motion, "Motion",
                                { { { "\xe2\x96\xb6 Forward", "The heads play forwards through the Region." } },
                                  { { "\xe2\x97\x80 Backward", "The heads play backwards." } },
                                  { { "\xe2\x87\x84 Bounce", "The heads turn round at the edges of the Region." } },
                                  { { "\xe2\x86\x94 Mirror", "Every other head plays backwards (needs 2 heads or more; picking it with 1 head sets Heads to 2)." } },
                                  { { "\xe2\x9d\x84 Freeze", "The heads hold still where the note starts." } },
                                  { { "\xe2\x81\x99 Scatter", "Each head jumps to a new spot every \xe2\x80\x98Jump every\xe2\x80\x99, locked to the beat." } } },
                                std::nullopt, 4, Rule::freeze, "motion");
            motion = seg.control;
            motion->onUserChange = [this] (int index) { motionChosen (index); };
        }
        jump = &choice (P::jump, "Jump every", 118);
        jump->setCaptionAccessory (&makePin ("jump"), PinButton::width);
        setItemWidth (*jump, jump->preferredWidth());
        jump->onUserChange = [this]
        {
            actions.commit ("Jump every " + rateLabel (jump->getParameter()).toStdString(), { P::jump });
        };
        jumpFader = std::make_unique<Fader> (*jump, [this] { return plainHint ("Jump every", kJumpTitle, fadeOf (Rule::jump)); }, showHint);
        {
            auto b = std::make_unique<FlatButton> (t, juce::String::fromUTF8 ("\xe2\x86\xbb Re-roll"));
            b->onClick = [this] { rerollClicked(); };
            const int w = b->preferredWidth();
            reroll = &add (std::move (b), w, kControlHeight);
            rerollFader = std::make_unique<Fader> (*reroll, [this]
            {
                const auto* st = fadeOf (Rule::reroll);
                return "Re-roll: " + (st != nullptr && st->faded ? "does nothing now. " + str (st->reason) : kRerollBase);
            }, showHint);
        }
        {
            // Preview (U5, decision 3): the prototype's Play, while Cubase is stopped
            auto b = std::make_unique<FlatButton> (t, "Stop preview", FlatButton::Icon::stop);
            const int w = b->preferredWidth();   // the wider of its two texts
            b->setButtonText ("Preview");
            b->setIcon (FlatButton::Icon::play);
            b->setTooltip (kPreviewTitle);
            b->onClick = [this] { togglePreview(); };
            preview = &add (std::move (b), w, kControlHeight);
            previewHover = std::make_unique<HoverWatch> (*preview, [this] (bool over)
            {
                hint (over ? (processor.isPreviewOn() ? kPreviewOnTitle : kPreviewTitle) : juce::String());
            });
        }

        // the knobs (the last two belong to Refresh), then M0's Region knobs and the loop crossfade
        newRow();
        for (int i = 0; i < 9; ++i)
        {
            const auto id = static_cast<rules::KnobId> (i);
            if (id == rules::KnobId::refreshMs)
                separator();
            auto k = std::make_unique<SampleKnob> (t, s, id, actions);
            k->onChange = [this] { sync(); };
            if (const char* field = k->getSpec().pin)
                k->setPin (&makePin (field));
            const int w = k->preferredWidth();
            knobs[(std::size_t) i] = &add (std::move (k), w, SampleKnob::preferredHeight);
            for (const auto param : rules::knobParams (id))
                bound (param);
        }
        separator();
        {
            const P region[] = { P::regionStart, P::regionEnd, P::loopXfade };
            const char* names[] = { "Region start", "Region end", "Loop fade" };
            for (int i = 0; i < 3; ++i)
            {
                auto& k = knob (region[i], names[i]);
                const auto param = region[i];
                const std::string label = names[i];
                k.onGestureEnd = [this, param, label] { actions.commit (label, { param }); };
                regionKnobs[(std::size_t) i] = &k;
            }
        }

        // Refresh with Every and Lock to the beat, Tape, Each grain backwards, Head tune, At the end
        newRow();
        {
            auto& seg = addSeg (P::refreshMode, "Refresh",
                                { { { "Smooth", "Always fresh: the heads read the sound continuously." } },
                                  { { "Free", "A fresh grab every Refresh time (5 ms to 2 s)." } },
                                  { { "Synced", "A fresh grab every note value (1 bar to 1/64, triplet or dotted)." } } },
                                std::nullopt, -1, std::nullopt, "refresh");
            refreshMode = seg.control;
            refreshMode->setCaptionTooltip (kRefreshTitle);
        }
        refreshRate = &choice (P::refreshRate, "Every", 118);
        refreshRate->onUserChange = [this]
        {
            actions.commit ("Refresh every " + rateLabel (refreshRate->getParameter()).toStdString(), { P::refreshRate });
        };
        rateFader = std::make_unique<Fader> (*refreshRate, [this] { return plainHint ("Every", kEveryTitle, fadeOf (Rule::refreshRate)); }, showHint);

        beatLock = &toggle (P::refreshBeatLock, "Lock to the beat");
        beatLock->onUserChange = [this]
        {
            actions.commit (beatLock->getToggleState() ? "Refresh: locked to the beat" : "Refresh: free of the beat", { P::refreshBeatLock });
        };
        beatFader = std::make_unique<Fader> (*beatLock, [this] { return plainHint ("Lock to the beat", kBeatTitle, fadeOf (Rule::refreshRate)); }, showHint);

        tape = &pinnedToggle (P::tape, "Tape", "tape");
        tape->onUserChange = [this] { actions.commit (tape->getToggleState() ? "Tape switch on" : "Tape switch off", { P::tape }); };
        tapeFader = std::make_unique<Fader> (*tape, [this] { return plainHint ("Tape", kTapeTitle, fadeOf (Rule::tape)); }, showHint);

        rev = &pinnedToggle (P::reverseGrains, "Each grain backwards", "rev");
        rev->onUserChange = [this]
        {
            actions.commit (rev->getToggleState() ? "Each grain backwards on" : "Each grain backwards off", { P::reverseGrains });
        };
        revFader = std::make_unique<Fader> (*rev, [this] { return plainHint ("Each grain backwards", kRevTitle, fadeOf (Rule::rev)); }, showHint);

        headTune = addSeg (P::headTune, "Head tune",
                           { { { "Same", "Every head plays the same pitch." } },
                             { { "Detune", "A gentle detune between the heads." } },
                             { { "Octaves", "Heads an octave apart." } },
                             { { "Fifths", "Heads a fifth and an octave apart." } } },
                           Rule::headTune, -1, std::nullopt, "headTune").control;
        atEnd = addSeg (P::atEnd, "At the end",
                        { { { "Loop", "At the end of the Region, go round again (with a short crossfade)." } },
                          { { "Hold", "At the end of the Region, stay on the last bit." } },
                          { { "Stop", "At the end of the Region, go quiet." } } },
                        Rule::atEnd, -1, std::nullopt).control;

        // Recipes 1-8 and Dice (sl-ui-sample.js buildRecipes: #recipesBox, under a line)
        buildRecipes();

        // the notice that Cubase has paused StrandLock (decision 3) sits above the rows while it shows
        notice.setText (kPausedText);
        addChildComponent (notice);
        seenStops = processor.getPreviewStops();
        processor.getUserSettings().addChangeListener (this);

        syncPins();
        sync (true);
    }

    SampleTab::~SampleTab()
    {
        processor.getUserSettings().removeChangeListener (this);
        for (auto& seg : segs)
        {
            seg->control->onUserChange = nullptr;
            seg->control->onHoverChange = nullptr;
            seg->control->setCaptionAccessory (nullptr, 0);
        }
        // the pins go before the controls that show them (the panel's own items go last)
        jump->setCaptionAccessory (nullptr, 0);
        for (auto* k : knobs)
            k->setPin (nullptr);
        diceHover.reset();
        previewHover.reset();
        recipeHooks.reset();
    }

    void SampleTab::releaseRoll()
    {
        recipeHooks.reset();
    }

    SampleTab::Seg& SampleTab::addSeg (P p, const juce::String& caption, std::vector<std::array<const char*, 2>> options,
                                       std::optional<Rule> groupRule, int ruleIndex, std::optional<Rule> optionRule, const char* pinField)
    {
        auto seg = std::make_unique<Seg>();
        seg->caption = caption;
        seg->groupRule = groupRule;
        seg->ruleIndex = ruleIndex;
        seg->optionRule = optionRule;
        juce::StringArray labels;
        for (const auto& o : options)
        {
            seg->texts.push_back (juce::String::fromUTF8 (o[0]));
            seg->titles.push_back (juce::String::fromUTF8 (o[1]));
            labels.add (seg->texts.back());
        }
        auto& c = segments (p, caption);
        c.setLabels (labels);
        if (pinField != nullptr)
            c.setCaptionAccessory (&makePin (pinField), PinButton::width);   // sl-ui-sample.js group (label, pin, ...)
        setItemWidth (c, c.preferredWidth());
        seg->control = &c;
        auto* raw = seg.get();
        c.onUserChange = [this, raw, p] (int index) { segChosen (*raw, p, index); };
        c.onHoverChange = [this, raw] (int index) { segHovered (*raw, index); };
        // the group's own fade (Head tune, At the end); its hint is each segment's
        seg->fader = std::make_unique<Fader> (c, std::function<juce::String()> (), std::function<void (const juce::String&)> ());
        segs.push_back (std::move (seg));
        return *raw;
    }

    void SampleTab::segChosen (const Seg& seg, P p, int index)
    {
        // sl-ui-sample.js setEngine and the Refresh, Head tune and At the end rows: "Engine: Cloud", one undo step
        const auto& spec = params::spec (p);
        const std::string name = index >= 0 && index < spec.numChoices ? spec.choices[index] : "";
        actions.commit (seg.caption.toStdString() + ": " + name, { p });
    }

    void SampleTab::segHovered (Seg& seg, int index)
    {
        paintSegFades (seg);
        hint (index >= 0 ? segHint (seg, index) : juce::String());
    }

    juce::String SampleTab::segHint (const Seg& seg, int index) const
    {
        // sl-ui-sample.js seg (): a faded option says why, as "Motion ❄ Freeze: does nothing now. ..."
        const auto* st = index == seg.ruleIndex ? fadeOf (seg.optionRule) : fadeOf (seg.groupRule);
        const auto& text = seg.texts[(std::size_t) index];
        if (st != nullptr && st->faded)
            return seg.caption + " " + text + ": does nothing now. " + str (st->reason);
        return text + ": " + seg.titles[(std::size_t) index];
    }

    juce::String SampleTab::segTitle (const Seg& seg, int index) const
    {
        const auto* own = index == seg.ruleIndex ? fadeOf (seg.optionRule) : nullptr;
        const auto* st = own != nullptr ? own : fadeOf (seg.groupRule);
        return str (rules::fadeTitle (seg.titles[(std::size_t) index].toStdString(), st));
    }

    void SampleTab::paintSegFades (Seg& seg)
    {
        auto& c = *seg.control;
        const auto* own = fadeOf (seg.optionRule);
        for (int i = 0; i < (int) seg.texts.size(); ++i)
        {
            const bool fadedOption = i == seg.ruleIndex && own != nullptr && own->faded;
            // .sls-seg>button.sls-faded{opacity:.42}, .on.sls-faded and :hover {opacity:.6}
            const float alpha = fadedOption ? (c.getSelected() == i || c.getHovered() == i ? kFadedOnAlpha : kFadedAlpha) : 1.0f;
            c.setSegmentAlpha (i, alpha);
        }
    }

    void SampleTab::motionChosen (int index)
    {
        // sl-ui-sample.js setMotion: Mirror with too few heads sets Heads to 2 in the same click (S2); Freeze on Tape says
        // it plays forwards
        const auto inst = processor.readInst();
        std::vector<P> changed { P::motion };
        std::string label = std::string ("Motion: ") + params::kMotionChoices[juce::jlimit (0, 5, index)];
        if (index == static_cast<int> (core::Motion::mirror))
        {
            const double need = 2.0 - (inst.macros[2] - 0.5) * 14.0;
            if (inst.heads + 1e-9 < need)
            {
                const double heads = juce::jlimit (1.0, 8.0, std::ceil (need * 100.0) / 100.0);
                ParamBinding b (processor.getParam (P::heads));
                b.setAsCompleteGesture ((float) heads);
                changed.push_back (P::heads);
                label += " (2 heads)";
                actions.toast ("Mirror needs 2 heads or more: Heads set to 2", 2400);
            }
        }
        if (index == static_cast<int> (core::Motion::freeze) && inst.engine == core::Engine::tape)
            actions.toast ("Freeze does nothing in Tape (it plays forwards): pick Voice or Cloud to freeze", 4000);
        actions.commit (label, changed);
    }

    void SampleTab::rerollClicked()
    {
        // sl-ui-sample.js rerollInst: inst.seed + 1 (one undo step; Undo brings the previous roll back)
        auto st = processor.getInstState();
        const auto next = static_cast<std::int32_t> (static_cast<std::uint32_t> (st.seed) + 1u);
        st.seed = next;
        processor.setInstState (st);
        actions.commit ("Re-roll Scatter, Spray, Drift and Skip", {});
        actions.toast ("Re-rolled: new Scatter jumps, Spray, Drift and Skip (roll " + juce::String (next)
                           + "). They stay like this until you Re-roll again.",
                       3500);
    }

    void SampleTab::hint (const juce::String& text)
    {
        if (onHint)
            onHint (text);
    }

    void SampleTab::toast (const juce::String& text, int ms)
    {
        if (onToast)
            onToast (text, ms);
    }

    juce::String SampleTab::rerollTitle() const
    {
        return str (rules::fadeTitle (kRerollBase.toStdString(), fadeOf (Rule::reroll))) + "\n\nRoll " + juce::String (settings.inst().seed);
    }

    const rules::Fade* SampleTab::fadeOf (std::optional<Rule> r) const
    {
        return r.has_value() ? &rules::fadeOf (settings.fades(), *r) : nullptr;
    }

    SampleKnob& SampleTab::getKnob (rules::KnobId id) const
    {
        return *knobs[(std::size_t) juce::jlimit (0, 8, static_cast<int> (id))];
    }

    PinButton* SampleTab::getPin (const std::string& field) const noexcept
    {
        for (const auto& b : pinButtons)
            if (b->getField() == field)
                return b.get();
        return nullptr;
    }

    //==============================================================================================
    // T3: the pins, the recipes, Dice and Preview.

    PinButton& SampleTab::makePin (const char* field)
    {
        auto b = std::make_unique<PinButton> (theme, std::string (field), pinName (field));
        auto* raw = b.get();
        b->onClick = [this, raw] { setPinned (raw->getField(), ! raw->isPinned()); };
        pinButtons.push_back (std::move (b));
        return *raw;
    }

    ParamToggle& SampleTab::pinnedToggle (P p, const juce::String& text, const char* pinField)
    {
        // sl-ui-sample.js toggle ({ pin }): the button and its pin side by side; the pin does not fade with the button
        auto c = std::make_unique<ParamToggle> (theme, state, idOf (p), text);
        auto& ref = *c;
        const int w = c->preferredWidth();
        auto group = std::make_unique<PinGroup> (std::move (c), w, makePin (pinField));
        const int gw = group->preferredWidth();
        add (std::move (group), gw, kControlHeight);
        bound (p);
        return ref;
    }

    void SampleTab::buildRecipes()
    {
        // a line across the tab (#recipesBox's border-top), then Recipes 1-8 and what they act on
        newRow();
        add (std::make_unique<TabRule> (theme), 100000, 1);
        newRow();
        caption ("Recipes", Caption::Style::label);
        recipeHooks = std::make_unique<roll::HoverHooks> (&roll.getGhosts(), [this] (const juce::String& text) { hint (text); });
        for (const auto& r : model::recipes())
        {
            const int n = r.n;
            if (n < 1 || n > 8)
                continue;
            auto b = std::make_unique<RecipeButton> (theme, n, juce::String (r.name));
            const auto lanes = r.notes.empty() ? juce::String() : " It also " + recipeLanes (r) + ".";
            b->setTooltip (juce::String (recipeTip (n)) + lanes);
            b->onClick = [this, n] { applyRecipe (n); };
            const int w = b->widthNeeded();
            auto& ref = add (std::move (b), w, kControlHeight);
            recipes[(std::size_t) n - 1] = &ref;
            // the mouse on a recipe: its notes dashed on the roll, and what it does in the hint line (a press takes the
            // dashed notes away first)
            recipeHooks->add (ref, [this, n] { return roll.getTools().previewRecipe (n); }, [this, n] { return recipeHint (n); });
        }
        {
            // the whole width of the tab (the flow makes a wider item as wide as the row), two lines at most
            auto t = std::make_unique<SubText> (theme);
            recipeScope = &add (std::move (t), 100000, SubText::twoLines);
        }

        // Dice, what it keeps, Unpin all
        newRow();
        {
            auto b = std::make_unique<FlatButton> (theme, "Dice", FlatButton::Icon::dice);
            b->setFontPx (13.0f);   // .sls-dice{font-size:13px}
            b->setTooltip (kDiceTitle);
            b->onClick = [this] { rollDice(); };
            const int w = b->preferredWidth();
            dice = &add (std::move (b), w, kControlHeight);
            diceHover = std::make_unique<HoverWatch> (*dice, [this] (bool over) { hint (over ? kDiceHint : juce::String()); });
        }
        {
            auto t = std::make_unique<SubText> (theme);
            diceInfo = &add (std::move (t), kDiceInfoMax, SubText::twoLines);
        }
        {
            auto b = std::make_unique<FlatButton> (theme, "Unpin all");
            b->setTooltip (kUnpinTitle);
            b->onClick = [this] { unpinAll(); };
            const int w = b->preferredWidth();
            unpin = &add (std::move (b), w, kControlHeight);
        }
    }

    juce::String SampleTab::recipeHint (int n) const
    {
        // sl-ui-sample.js: the recipe's mouseenter
        const auto* r = recipeOf (n);
        if (r == nullptr)
            return {};
        const auto on = notesWord (processor.getDocument().rollState().selected.size());
        return "Recipe " + juce::String (n) + " " + juce::String (r->name) + ": " + recipeTip (n) + " One click sets the Sample tab for every note"
             + (r->notes.empty() ? juce::String() : " and " + recipeLanes (*r) + " of " + on) + "; the dashed lines preview the notes.";
    }

    void SampleTab::applyRecipe (int n)
    {
        // sl-ui-sample.js applyRecipeN: SLModel.applyRecipe on the selected notes (every note with none selected), the
        // notes and the instrument as one undo step (commitBoth)
        const auto* recipe = recipeOf (n);
        if (recipe == nullptr)
            return;
        roll.getGhosts().clear();   // A.setGhost (null), then the recipe
        const auto ids = roll.getTools().ids();
        const auto before = processor.getDocument().current();
        auto result = model::applyRecipe (*before, processor.readInst(), n, ids.empty() ? model::Ids::all() : model::Ids (ids));
        if (! result.has_value())
        {
            toast ("That recipe could not be applied", 2400);
            return;
        }
        const auto out = processor.getDocument().apply (std::move (result->clip));
        if (! out.accepted)
        {
            toast (str (out.message), 3500);
            return;
        }
        const auto changed = params::applyPatch (state, samplePatch (result->inst));
        actions.commit ("Recipe " + std::to_string (n) + " " + recipe->name, paramsOf (changed));
        if (processor.getDocument().current() != before)
            roll.edited();   // the notes changed: the roll and the rows around it show them

        const auto sel = notesWord (ids.size());
        toast ("Recipe " + juce::String (n) + " " + juce::String (recipe->name) + ": the Sample tab is set for every note"
                   + (recipe->notes.empty() ? juce::String() : ", and it " + recipeLanes (*recipe) + " of " + sel)
                   + ". A note keeps what it has in its own Note settings.",
               5000);
    }

    void SampleTab::rollDice()
    {
        // sl-ui-sample.js roll: a UI seed (the rolled values are stored, so they stay put, Y7), SLModel.dice with the
        // pins, one undo step "Dice"
        ++rolls;
        const auto now = juce::Time::currentTimeMillis();   // Date.now ()
        lastSeed = core::toInt32 ((double) (now % 1000003) + (double) rolls * 7919.0);
        const auto rolled = model::dice (processor.readInst(), (double) lastSeed, pins);
        const auto changed = params::applyPatch (state, samplePatch (rolled));
        actions.commit ("Dice", paramsOf (changed));

        static const char* const engines[] = { "Tape", "Voice", "Cloud" };
        static const char* const motions[] = { "Forward", "Backward", "Bounce", "Mirror", "Freeze", "Scatter" };
        juce::StringArray parts;
        parts.add (juce::String (engines[juce::jlimit (0, 2, static_cast<int> (rolled.engine))])
                   + (rolled.engine == core::Engine::cloud && rolled.lock ? " with Lock" : ""));
        parts.add (motions[juce::jlimit (0, 5, static_cast<int> (rolled.motion))]);
        parts.add (str (params::text::fmtHeads (rolled.heads)));
        if (rolled.refresh.mode != core::RefreshMode::smooth)
            parts.add ("Refresh " + (rolled.refresh.mode == core::RefreshMode::sync ? juce::String (core::rateLabel (rolled.refresh.rate))
                                                                                    : str (params::text::fmtMs (rolled.refresh.ms))));
        toast ("Dice: " + parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")) + ". Undo steps back.", 3500);
    }

    void SampleTab::setPinned (const std::string& field, bool pinned)
    {
        // sl-ui-sample.js pinBtn's click: kept at once (decision 4: per person, UserSettings)
        auto list = pins;
        const auto it = std::find (list.begin(), list.end(), field);
        if (pinned && it == list.end())
            list.push_back (field);
        else if (! pinned && it != list.end())
            list.erase (it);
        else
            return;
        pins = list;
        processor.getUserSettings().setDicePins (pins);
        syncPins();
    }

    void SampleTab::unpinAll()
    {
        if (pins.empty())
            return;
        pins.clear();
        processor.getUserSettings().setDicePins (pins);
        syncPins();
    }

    void SampleTab::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // another StrandLock window changed the person's pins
        const auto now = processor.getUserSettings().dicePins();
        if (now == pins)
            return;
        pins = now;
        syncPins();
    }

    void SampleTab::syncPins()
    {
        // sl-ui-sample.js syncPins: each pin lit and titled, what Dice keeps (in DICE_FIELDS' order), Unpin all only with
        // a pin
        for (const auto& b : pinButtons)
            b->setPinned (std::find (pins.begin(), pins.end(), b->getField()) != pins.end());
        juce::StringArray names;
        for (const char* f : model::kDiceFields)
            if (std::find (pins.begin(), pins.end(), std::string (f)) != pins.end())
                names.add (pinName (f));
        const auto text = names.isEmpty() ? kNoPins : "Pinned: " + names.joinIntoString (", ");
        diceInfo->setText (text);
        unpin->setVisible (! pins.empty());

        // Unpin all right after the text (the prototype's inline row): the box takes the text's width, at most 560 px (two
        // lines). Measured here, on a pin's click, never on the editor's tick.
        const int w = juce::jlimit (40, kDiceInfoMax, (int) std::ceil (Theme::textWidth (Theme::px (11.5f), text)) + 4);
        if (resizeItem (*diceInfo, w))
            layoutChanged();
    }

    void SampleTab::syncRecipeScope()
    {
        // sl-ui-sample.js syncRecipeScope: what the Sample lines part of a recipe acts on (the text only when it changes)
        const auto n = processor.getDocument().rollState().selected.size();
        if (n == scopeShown)
            return;
        scopeShown = n;
        recipeScope->setText (n == 0 ? kScopeAll
                                     : "One click sets the Sample tab for every note. Some recipes also set Sample lines (Travel, Heads, Grain, Refresh): only on the "
                                           + juce::String ((int) n) + (n == 1 ? " selected note." : " selected notes."));
    }

    void SampleTab::togglePreview()
    {
        processor.setPreview (! processor.isPreviewOn());
        syncPreview();
    }

    void SampleTab::syncPreview()
    {
        // Cubase's Play turned Preview off since the last look: say why
        const auto stops = processor.getPreviewStops();
        if (stops != seenStops)
        {
            seenStops = stops;
            toast (kStoppedByCubase, 3500);
        }

        // decision 3: no block for 250 ms while Preview is on means Cubase has paused StrandLock
        const bool on = processor.isPreviewOn();
        const double now = clock ? clock() : juce::Time::getMillisecondCounterHiRes();
        const bool paused = previewWatch.update (now, processor.getBlocksProcessed(), on);
        const int state = on ? (paused ? 2 : 1) : 0;
        if (state == previewShown)
            return;
        previewShown = state;
        preview->setToggleState (on, juce::dontSendNotification);
        preview->setButtonText (on ? "Stop preview" : "Preview");
        preview->setIcon (on ? FlatButton::Icon::stop : FlatButton::Icon::play);
        preview->setTooltip (on ? kPreviewOnTitle : kPreviewTitle);
        showNotice (state == 2);
    }

    void SampleTab::showNotice (bool show)
    {
        if (show == notice.isVisible())
            return;
        notice.setVisible (show);
        if (! show)
        {
            setTopInset (0);
            return;
        }
        noticeWidth = juce::jmax (40, getWidth() - 2 * padX);
        noticeHeight = notice.heightFor (noticeWidth);
        setTopInset (noticeHeight + rowGap);   // the rows move down under it (and the pane takes the new height)
    }

    void SampleTab::resized()
    {
        if (notice.isVisible())
        {
            // the notice wraps at the tab's width: measured again only when that changed
            const int w = juce::jmax (40, getWidth() - 2 * padX);
            if (w != noticeWidth)
            {
                noticeWidth = w;
                noticeHeight = notice.heightFor (w);
            }
            notice.setBounds (padX, padY, w, noticeHeight);
            if (getTopInset() != noticeHeight + rowGap)
            {
                setTopInset (noticeHeight + rowGap);   // lays the rows out again (and calls this again)
                return;
            }
        }
        TabPanel::resized();
    }

    //==============================================================================================
    void SampleTab::sync (bool force)
    {
        syncPreview();
        syncRecipeScope();
        if (! settings.update (force))
            return;
        applyFades();
    }

    void SampleTab::applyFades()
    {
        // the knobs: the Grain knob follows the engine (dyn); each its rule's fade
        for (int i = 0; i < 9; ++i)
        {
            const auto spec = rules::knobSpec (static_cast<rules::KnobId> (i), settings.grainKind());
            knobs[(std::size_t) i]->setSpec (spec, fadeOf (spec.rule));
        }

        const auto fadedBy = [this] (Rule r) { return rules::fadeOf (settings.fades(), r).faded; };
        lockFader->setFaded (fadedBy (Rule::lock));
        lock->setTooltip (str (rules::fadeTitle (kLockTitle.toStdString(), fadeOf (Rule::lock))));
        jumpFader->setFaded (fadedBy (Rule::jump));
        jump->getBox().setTooltip (str (rules::fadeTitle (kJumpTitle.toStdString(), fadeOf (Rule::jump))));
        rerollFader->setFaded (fadedBy (Rule::reroll));
        reroll->setTooltip (rerollTitle());
        rateFader->setFaded (fadedBy (Rule::refreshRate));
        refreshRate->getBox().setTooltip (str (rules::fadeTitle (kEveryTitle.toStdString(), fadeOf (Rule::refreshRate))));
        beatFader->setFaded (fadedBy (Rule::refreshRate));
        beatLock->setTooltip (str (rules::fadeTitle (kBeatTitle.toStdString(), fadeOf (Rule::refreshRate))));
        tapeFader->setFaded (fadedBy (Rule::tape));
        tape->setTooltip (str (rules::fadeTitle (kTapeTitle.toStdString(), fadeOf (Rule::tape))));
        revFader->setFaded (fadedBy (Rule::rev));
        rev->setTooltip (str (rules::fadeTitle (kRevTitle.toStdString(), fadeOf (Rule::rev))));

        for (auto& seg : segs)
        {
            const auto* g = fadeOf (seg->groupRule);
            seg->fader->setFaded (g != nullptr && g->faded);
            juce::StringArray tips;
            for (int i = 0; i < (int) seg->texts.size(); ++i)
                tips.add (segTitle (*seg, i));
            seg->control->setSegmentTooltips (tips);
            paintSegFades (*seg);
        }
    }

    //==============================================================================================
    std::vector<ControlState> SampleTab::controlStates() const
    {
        std::vector<ControlState> out;
        const auto plain = [&out] (const juce::String& name, bool faded, const juce::String& title, const juce::String& hintText)
        {
            ControlState c;
            c.name = name;
            c.faded = faded;
            c.title = title;
            c.hint = hintText;
            out.push_back (c);
        };
        for (const auto& seg : segs)
            for (int i = 0; i < (int) seg->texts.size(); ++i)
                plain (seg->caption + ": " + seg->texts[(std::size_t) i],
                       seg->fader->isFaded() || seg->control->getSegmentAlpha (i) < 1.0f, seg->control->getSegmentTooltip (i), segHint (*seg, i));
        plain ("Lock", lockFader->isFaded() && lock->getAlpha() < 1.0f, lock->getTooltip(), lockFader->hint());
        plain ("Jump every", jumpFader->isFaded() && jump->getAlpha() < 1.0f, jump->getBox().getTooltip(), jumpFader->hint());
        plain (reroll->getButtonText(), rerollFader->isFaded() && reroll->getAlpha() < 1.0f, reroll->getTooltip(), rerollFader->hint());
        plain ("Every", rateFader->isFaded() && refreshRate->getAlpha() < 1.0f, refreshRate->getBox().getTooltip(), rateFader->hint());
        plain ("Lock to the beat", beatFader->isFaded() && beatLock->getAlpha() < 1.0f, beatLock->getTooltip(), beatFader->hint());
        plain ("Tape", tapeFader->isFaded() && tape->getAlpha() < 1.0f, tape->getTooltip(), tapeFader->hint());
        plain ("Each grain backwards", revFader->isFaded() && rev->getAlpha() < 1.0f, rev->getTooltip(), revFader->hint());
        for (const auto* k : knobs)
        {
            ControlState c;
            c.name = k->getSpec().undo;
            c.faded = k->isFaded() && k->getAlpha() < 1.0f;
            c.off = k->isOff();
            c.title = k->titleText();
            c.hint = k->hintText();
            c.readout = k->readout();
            out.push_back (c);
        }
        return out;
    }
}
