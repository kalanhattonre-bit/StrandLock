#include "TabPanel.h"

namespace sl::ui
{
    namespace
    {
        constexpr int kControlHeight = 26;   // a button or select at this window height (12px text, 3px padding)
    }

    TabPanel::TabPanel (const Theme& t, juce::AudioProcessorValueTreeState& s)
        : theme (t), state (s)
    {
    }

    juce::StringArray TabPanel::choicesOf (params::P p)
    {
        juce::StringArray list;
        const auto& spec = params::kParams[params::index (p)];
        for (int i = 0; i < spec.numChoices; ++i)
            list.add (spec.choices[i]);
        return list;
    }

    void TabPanel::addItem (std::unique_ptr<juce::Component> c, int width, int height)
    {
        addAndMakeVisible (*c);
        items.push_back ({ c.get(), width, height, breakNext });
        breakNext = false;
        owned.push_back (std::move (c));
    }

    Knob& TabPanel::knob (params::P p, const juce::String& name, Tok colour, bool bipolar)
    {
        boundIds.add (idOf (p));
        auto k = std::make_unique<Knob> (theme, state, idOf (p), name, colour, bipolar);
        auto& ref = *k;
        knobs.push_back (k.get());
        const int w = k->preferredWidth();
        addItem (std::move (k), w, Knob::preferredHeight);
        return ref;
    }

    SegmentedControl& TabPanel::segments (params::P p, const juce::String& captionText)
    {
        boundIds.add (idOf (p));
        auto c = std::make_unique<SegmentedControl> (theme, *state.getParameter (idOf (p)), choicesOf (p), captionText);
        auto& ref = *c;
        const int w = c->preferredWidth();
        addItem (std::move (c), w, kControlHeight);
        return ref;
    }

    ChoiceBox& TabPanel::choice (params::P p, const juce::String& captionText, int boxWidth)
    {
        boundIds.add (idOf (p));
        auto c = std::make_unique<ChoiceBox> (theme, state, idOf (p), captionText, boxWidth);
        auto& ref = *c;
        const int w = c->preferredWidth();
        addItem (std::move (c), w, kControlHeight);
        return ref;
    }

    ParamToggle& TabPanel::toggle (params::P p, const juce::String& text)
    {
        boundIds.add (idOf (p));
        auto c = std::make_unique<ParamToggle> (theme, state, idOf (p), text);
        auto& ref = *c;
        const int w = c->preferredWidth();
        addItem (std::move (c), w, kControlHeight);
        return ref;
    }

    Caption& TabPanel::caption (const juce::String& text, Caption::Style style)
    {
        auto c = std::make_unique<Caption> (theme, text, style);
        auto& ref = *c;
        const int w = c->preferredWidth();
        addItem (std::move (c), w, 18);
        return ref;
    }

    void TabPanel::separator()
    {
        addItem (std::make_unique<Separator> (theme), 9, Knob::preferredHeight);
    }

    int TabPanel::heightFor (int width) const
    {
        const auto area = juce::Rectangle<int> (0, 0, juce::jmax (1, width), 100000).reduced (padX, padY);
        return flowLayout (items, area, gap, rowGap, false) + 2 * padY;
    }

    std::vector<int> TabPanel::rowEnds (int width) const
    {
        const auto area = juce::Rectangle<int> (0, 0, juce::jmax (1, width), 100000).reduced (padX, padY);
        std::vector<int> ends;
        flowLayout (items, area, gap, rowGap, false, &ends);
        for (auto& e : ends)
            e += 2 * padY;
        return ends;
    }

    void TabPanel::resized()
    {
        flowLayout (items, getLocalBounds().reduced (padX, padY), gap, rowGap, true);
    }
}
