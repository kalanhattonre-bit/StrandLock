#include "SampleTab.h"

namespace sl::ui
{
    SampleTab::SampleTab (const Theme& t, juce::AudioProcessorValueTreeState& s)
        : TabPanel (t, s)
    {
        using params::P;

        // The prototype's order (sl-ui-sample.js buildSampleTab): engine and motion, the knobs, then Refresh.
        segments (P::engine, "Engine");
        toggle (P::lock, "Lock");
        segments (P::motion, "Motion");
        choice (P::jump, "Jump every", 118);

        newRow();
        knob (P::speed, "Speed");
        knob (P::heads, "Heads");
        knob (P::spread, "Spread");
        knob (P::grainVoice, "Voice grain");
        knob (P::grainCloud, "Cloud grain");
        knob (P::grainLock, "Lock grain");
        knob (P::spray, "Spray");
        knob (P::drift, "Drift");
        knob (P::pitch, "Pitch", Tok::lanePitch, true);
        separator();
        knob (P::refreshMs, "Refresh time");
        knob (P::refreshSkip, "Skip");

        newRow();
        segments (P::refreshMode, "Refresh");
        choice (P::refreshRate, "Every", 118);
        toggle (P::refreshBeatLock, "Lock to the beat");
        toggle (P::tape, "Tape");
        toggle (P::reverseGrains, "Each grain backwards");
        segments (P::headTune, "Head tune");

        newRow();
        knob (P::regionStart, "Region start");
        knob (P::regionEnd, "Region end");
        knob (P::loopXfade, "Loop fade");
        segments (P::atEnd, "At the end");
        caption ("M6 makes this the full Sample tab: one Grain knob, faded settings, Recipes and Dice.", Caption::Style::sub);
    }
}
