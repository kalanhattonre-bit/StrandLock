#pragma once

#include "PluginProcessor.h"
#include "ui/StrandLookAndFeel.h"

/** The scaffold's editor: the suite's logo and wordmark on the dark background, and one empty panel
    that M0 fills (top bar, mode bar, tool tabs, the roll, the waveform strip, hints). */
class StrandLockEditor final : public juce::AudioProcessorEditor
{
public:
    explicit StrandLockEditor (StrandLockProcessor&);
    ~StrandLockEditor() override;

    void paint (juce::Graphics&) override;

private:
    sl::ui::StrandLookAndFeel lookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockEditor)
};
