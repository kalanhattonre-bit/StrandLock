#include "SampleTab.h"

#include "PluginProcessor.h"
#include "core/Rates.h"
#include "params/ParameterLayout.h"

#include <cmath>
#include <cstring>

namespace sl::ui
{
    namespace
    {
        using params::P;
        using rules::Rule;

        constexpr int kControlHeight = 26;   // a button or select at this window height (TabPanel's)
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
    SampleTab::SampleTab (const Theme& t, juce::AudioProcessorValueTreeState& s, StrandLockProcessor& p)
        : TabPanel (t, s), processor (p), settings (p)
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
        actions.toast = [this] (const juce::String& text, int ms)
        {
            if (onToast)
                onToast (text, ms);
        };
        const auto showHint = [this] (const juce::String& text) { hint (text); };

        // Engine and Lock, Motion, Jump every, Re-roll (sl-ui-sample.js buildSampleTab's first row)
        engine = addSeg (P::engine, "Engine",
                         { { { "Tape", "Speed and pitch move together, like tape: backwards sounds reversed and slower drops the pitch." } },
                           { { "Voice", "Stays in tune with the key you play and keeps the sound's character, slowed down, frozen or backwards. Best on one clear note." } },
                           { { "Cloud", "Grains that work on anything: chords, songs, noise. Switch Lock on to play any of it in tune." } } },
                         std::nullopt, -1, std::nullopt).control;
        {
            auto l = std::make_unique<LockToggle> (t, s, actions);
            const int w = l->preferredWidth();
            lock = &add (std::move (l), w, kControlHeight);
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
                                std::nullopt, 4, Rule::freeze);
            motion = seg.control;
            motion->onUserChange = [this] (int index) { motionChosen (index); };
        }
        jump = &choice (P::jump, "Jump every", 118);
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

        // the knobs (the last two belong to Refresh), then M0's Region knobs and the loop crossfade
        newRow();
        for (int i = 0; i < 9; ++i)
        {
            const auto id = static_cast<rules::KnobId> (i);
            if (id == rules::KnobId::refreshMs)
                separator();
            auto k = std::make_unique<SampleKnob> (t, s, id, actions);
            k->onChange = [this] { sync(); };
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
                                std::nullopt, -1, std::nullopt);
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

        tape = &toggle (P::tape, "Tape");
        tape->onUserChange = [this] { actions.commit (tape->getToggleState() ? "Tape switch on" : "Tape switch off", { P::tape }); };
        tapeFader = std::make_unique<Fader> (*tape, [this] { return plainHint ("Tape", kTapeTitle, fadeOf (Rule::tape)); }, showHint);

        rev = &toggle (P::reverseGrains, "Each grain backwards");
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
                           Rule::headTune, -1, std::nullopt).control;
        atEnd = addSeg (P::atEnd, "At the end",
                        { { { "Loop", "At the end of the Region, go round again (with a short crossfade)." } },
                          { { "Hold", "At the end of the Region, stay on the last bit." } },
                          { { "Stop", "At the end of the Region, go quiet." } } },
                        Rule::atEnd, -1, std::nullopt).control;

        sync (true);
    }

    SampleTab::~SampleTab()
    {
        for (auto& seg : segs)
        {
            seg->control->onUserChange = nullptr;
            seg->control->onHoverChange = nullptr;
        }
    }

    SampleTab::Seg& SampleTab::addSeg (P p, const juce::String& caption, std::vector<std::array<const char*, 2>> options,
                                       std::optional<Rule> groupRule, int ruleIndex, std::optional<Rule> optionRule)
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

    //==============================================================================================
    void SampleTab::sync (bool force)
    {
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
