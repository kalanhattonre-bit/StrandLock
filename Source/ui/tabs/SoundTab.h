#pragma once

#include "TabPanel.h"

class StrandLockProcessor;

namespace sl::ui
{
    /** The Sound tab (M0, M7): tuning and playing settings as plain controls bound to their parameters, and (M7, the
        prototype's Sound pane) "Detect pitch & key", the Key and Scale the roll shades and snaps to, and Scale lock.
        Detect listens to the sound (the Region, when it is a part of the file) on the worker and sets the root (no undo
        step: the root belongs to the sound, plan 2.4) and the key (one undo step). Key, Scale and Scale lock are INST
        state, not parameters (plan 2.2): each change is one undo step ("Key", "Scale", "Scale lock on"). The sound menu
        and Browse are on the waveform strip (ui/strip/WaveStrip). */
    class SoundTab final : public TabPanel
    {
    public:
        SoundTab (const Theme&, StrandLockProcessor&);

        /** The editor's timer: the Key, Scale and Scale lock show the INST state (Undo, a suggested key, a project). */
        void refresh();

        FlatButton& getDetectButton() noexcept { return *detect; }
        juce::ComboBox& getKeyBox() noexcept { return *keyBox; }
        juce::ComboBox& getScaleBox() noexcept { return *scaleBox; }
        FlatButton& getScaleLock() noexcept { return *lock; }

        /** The scale names as the menu shows them (sl-ui.js scaleLabel: "Harm. minor" reads "Harmonic minor"). */
        static juce::String scaleLabel (const char* name);

    private:
        StrandLockProcessor& processor;
        FlatButton* detect = nullptr;
        juce::ComboBox* keyBox = nullptr;
        juce::ComboBox* scaleBox = nullptr;
        FlatButton* lock = nullptr;
        bool syncing = false;
    };
}
