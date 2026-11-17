#pragma once

// The ruler over the roll (PLUGIN-PLAN 4 M4, 7.6 R2; sl-ui.js drawRuler, shell.html #rulerWrap): a tick at every
// beat, a taller one with the bar's number at every bar, scrolled with the roll. While Cubase plays, a small
// triangle in the playhead colour marks where the playhead is (the prototype's triangle marks where its own Play
// starts; in the plugin Cubase's transport decides that, so the triangle follows the playhead instead).

#include "RollGeometry.h"

#include "ui/Theme.h"

#include <optional>

namespace sl::ui::roll
{
    class Ruler final : public juce::Component
    {
    public:
        Ruler (const Theme&, const RollGeometry&);

        /** The playhead in beats while Cubase plays (nothing while stopped). */
        void setPlayhead (std::optional<double> beat);

        void paint (juce::Graphics&) override;

    private:
        const Theme& theme;
        const RollGeometry& geo;
        std::optional<double> playhead;
    };
}
