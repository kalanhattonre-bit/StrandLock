#include "MacrosTab.h"

#include "PluginProcessor.h"
#include "params/ParameterLayout.h"

namespace sl::ui
{
    namespace
    {
        using params::P;

        constexpr int kButtonHeight = 26;   // shell.html's buttons at this window height
    }

    MacrosTab::MacrosTab (const Theme& t, juce::AudioProcessorValueTreeState& s, StrandLockProcessor& p)
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
        actions.hint = [this] (const juce::String& text)
        {
            if (onHint)
                onHint (text);
        };
        actions.toast = [this] (const juce::String& text, int ms)
        {
            if (onToast)
                onToast (text, ms);
        };

        const auto button = [this, &t] (const juce::String& text, const juce::String& tip, std::function<void()> click) -> FlatButton&
        {
            auto b = std::make_unique<FlatButton> (t, text);
            b->setTooltip (tip);
            b->onClick = std::move (click);
            const int w = b->preferredWidth();
            return add (std::move (b), w, kButtonHeight);
        };
        const auto knobRow = [this, &t, &s] (rules::KnobId id) -> SampleKnob&
        {
            auto k = std::make_unique<SampleKnob> (t, s, id, actions);
            k->onChange = [this] { sync(); };
            bound (k->getSpec().param);
            const int w = k->preferredWidth();
            return add (std::move (k), w, SampleKnob::preferredHeight);
        };

        // sl-ui-sample.js buildMacros: MACROS, its line and Centre all; the 8 knobs; WHOLE-CLIP OFFSETS, its line and
        // Reset all; the 5 offsets
        caption ("Macros", Caption::Style::label);
        caption ("Each knob moves every note at once. Centre = no change.", Caption::Style::sub);
        centreAll = &button ("Centre all", "Put all 8 macros back in the middle (no change)", [this] { centre(); });
        newRow();
        for (int i = 0; i < 8; ++i)
            macros[(std::size_t) i] = &knobRow (static_cast<rules::KnobId> (static_cast<int> (rules::KnobId::macro1) + i));
        newRow();
        caption ("Whole-clip offsets", Caption::Style::label);
        caption ("Added to every note's line.", Caption::Style::sub);
        resetAll = &button ("Reset all", "Set all 5 offsets back to no change", [this] { zero(); });
        newRow();
        for (int i = 0; i < 5; ++i)
            offsets[(std::size_t) i] = &knobRow (static_cast<rules::KnobId> (static_cast<int> (rules::KnobId::ampOffset) + i));

        sync (true);
    }

    void MacrosTab::centre()
    {
        // sl-ui-sample.js: every macro to 0.5, one undo step; a toast when they are there already
        std::vector<P> changed;
        for (auto* k : macros)
            if (! juce::exactlyEqual (params::plainValueOf (processor.getParam (k->getSpec().param)), 0.5f))
                changed.push_back (k->getSpec().param);
        if (changed.empty())
        {
            actions.toast ("The macros are already centred", 2400);
            return;
        }
        for (auto* k : macros)
        {
            ParamBinding b (processor.getParam (k->getSpec().param));
            b.setAsCompleteGesture (0.5f);
        }
        actions.commit ("Centre the macros", changed);
    }

    void MacrosTab::zero()
    {
        std::vector<P> changed;
        for (auto* k : offsets)
            if (! juce::exactlyEqual (params::plainValueOf (processor.getParam (k->getSpec().param)), 0.0f))
                changed.push_back (k->getSpec().param);
        if (changed.empty())
        {
            actions.toast ("The offsets are already at no change", 2400);
            return;
        }
        for (auto* k : offsets)
        {
            ParamBinding b (processor.getParam (k->getSpec().param));
            b.setAsCompleteGesture (0.0f);
        }
        actions.commit ("Reset the offsets", changed);
    }

    void MacrosTab::sync (bool force)
    {
        if (! settings.update (force))
            return;
        for (auto* k : macros)
            k->setSpec (k->getSpec(), k->getSpec().rule.has_value() ? &rules::fadeOf (settings.fades(), *k->getSpec().rule) : nullptr);
        for (auto* k : offsets)
            k->setSpec (k->getSpec(), nullptr);
    }

    std::vector<ControlState> MacrosTab::controlStates() const
    {
        std::vector<ControlState> out;
        const auto knob = [&out] (const SampleKnob& k)
        {
            ControlState c;
            c.name = k.getSpec().undo;
            c.faded = k.isFaded() && k.getAlpha() < 1.0f;
            c.off = k.isOff();
            c.title = k.titleText();
            c.hint = k.hintText();
            c.readout = k.readout();
            out.push_back (c);
        };
        for (const auto* k : macros)
            knob (*k);
        for (const auto* k : offsets)
            knob (*k);
        for (auto* b : { centreAll, resetAll })
        {
            ControlState c;
            c.name = b->getButtonText();
            c.title = b->getTooltip();
            out.push_back (c);
        }
        return out;
    }
}
