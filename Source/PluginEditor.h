#pragma once

#include "PluginProcessor.h"
#include "ui/StrandLookAndFeel.h"

/** The M0 editor: the suite's logo and wordmark on the dark background, and one empty panel that
    the later milestones fill (top bar, mode bar, tool tabs, the roll, the waveform strip, hints). */
class StrandLockEditor final : public juce::AudioProcessorEditor
{
public:
    explicit StrandLockEditor (StrandLockProcessor&);
    ~StrandLockEditor() override;

    void paint (juce::Graphics&) override;

private:
    strandlock::ui::StrandLookAndFeel lookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockEditor)
};
