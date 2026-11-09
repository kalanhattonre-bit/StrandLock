#pragma once

// What the M0 tabs share: a panel of controls that flow left to right and wrap (shell.html's .pane), and the
// helpers that make each kind of control for a parameter.

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/ParamIds.h"
#include "ui/Controls.h"

#include <memory>
#include <vector>

namespace sl::ui
{
    /** A thin vertical line between groups of knobs (.sls-ksep). */
    class Separator final : public juce::Component
    {
    public:
        explicit Separator (const Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme (Tok::line));
            g.fillRect (juce::Rectangle<float> ((float) getWidth() * 0.5f - 0.5f, 4.0f, 1.0f, juce::jmax (0.0f, (float) getHeight() - 8.0f)));
        }

    private:
        const Theme& theme;
    };

    class TabPanel : public juce::Component
    {
    public:
        TabPanel (const Theme&, juce::AudioProcessorValueTreeState&);

        /** The height the controls need when the panel is this wide. */
        int heightFor (int width) const;

        /** Where each row of controls ends (with the padding below it) when the panel is this wide. A pane cut at
            one of these never shows half a control. */
        std::vector<int> rowEnds (int width) const;

        void resized() override;

        /** Every knob on the panel (for the tests). */
        const std::vector<Knob*>& getKnobs() const noexcept { return knobs; }

        /** The ids of every parameter a control on the panel is bound to, in the order they were added. */
        const juce::StringArray& getBoundIds() const noexcept { return boundIds; }

        static constexpr int padX = 8, padY = 6, gap = 6, rowGap = 6;

    protected:
        static const char* idOf (params::P p) noexcept { return params::kParams[params::index (p)].id; }
        static juce::StringArray choicesOf (params::P p);

        Knob& knob (params::P p, const juce::String& name, Tok colour = Tok::laneSample, bool bipolar = false);
        SegmentedControl& segments (params::P p, const juce::String& caption);
        ChoiceBox& choice (params::P p, const juce::String& caption, int boxWidth);
        ParamToggle& toggle (params::P p, const juce::String& text);
        Caption& caption (const juce::String& text, Caption::Style style);
        void separator();
        void newRow() { breakNext = true; }

        const Theme& theme;
        juce::AudioProcessorValueTreeState& state;

    private:
        void addItem (std::unique_ptr<juce::Component> c, int width, int height);

        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<FlowItem> items;
        std::vector<Knob*> knobs;
        juce::StringArray boundIds;
        bool breakNext = false;
    };
}
