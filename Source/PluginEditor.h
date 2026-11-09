#pragma once

#include "PluginProcessor.h"
#include "ui/MainView.h"
#include "ui/StrandLookAndFeel.h"
#include "ui/Theme.h"

/** StrandLock's editor (M0): the v4 layout (ui/MainView), drawn at 1100 x 680 logical pixels and scaled as one
    piece by the UI Scale (75 to 200 %, remembered per person); the window keeps between 900 x 560 and
    2400 x 1600 logical pixels. It owns the theme (dark or light, remembered per person and in the project),
    the Keys switch and the drops.

    Keys: OFF, StrandLock never takes the keyboard (no component wants focus, and a click does not take it
    from Cubase). ON, a click gives StrandLock the keyboard and it keeps the keys it receives, except Space,
    which always goes on to Cubase. Every key that reaches the editor is recorded on the Diagnostics page.
    Drops: files and text dropped anywhere on the window are recorded there too (no sound loads yet: M2). */
class StrandLockEditor final : public juce::AudioProcessorEditor,
                               public juce::FileDragAndDropTarget,
                               public juce::TextDragAndDropTarget,
                               private juce::Timer
{
public:
    explicit StrandLockEditor (StrandLockProcessor&);
    ~StrandLockEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    bool isInterestedInTextDrag (const juce::String& text) override;
    void textDragEnter (const juce::String& text, int x, int y) override;
    void textDragExit (const juce::String& text) override;
    void textDropped (const juce::String& text, int x, int y) override;

    //==============================================================================================
    // What the top bar's controls call (and the tests).

    void setThemeId (sl::ui::ThemeId theme);
    sl::ui::ThemeId getThemeId() const noexcept { return theme.id(); }

    /** 75 .. 200; the window keeps its logical size and grows or shrinks with it. */
    void setUiScalePercent (int percent);
    int getUiScalePercent() const noexcept { return uiScalePercent; }

    void setKeysOn (bool on);
    bool isKeysOn() const noexcept { return keysOn; }

    /** The window's size in logical pixels (before the UI Scale). */
    juce::Point<int> getLogicalSize() const noexcept { return logicalSize; }

    /** Sets the window's size in logical pixels (clamped to 900 x 560 .. 2400 x 1600). */
    void setLogicalSize (int width, int height);

    /** What the timer does: the Diagnostics snapshot, the tempo, the loaded sound, the banner. */
    void refreshNow();

    sl::ui::MainView& getView() noexcept { return view; }
    const sl::ui::Theme& getTheme() const noexcept { return theme; }

    static constexpr int timerHz = 15;

private:
    void timerCallback() override;
    void applyLimits();
    void applyFocusRules();
    double desktopScale() const;

    StrandLockProcessor& proc;   // (AudioProcessorEditor already has a member named processor)
    sl::ui::Theme theme;
    sl::ui::StrandLookAndFeel lookAndFeel;   // declared before the view so it outlives it
    sl::ui::MainView view;
    juce::TooltipWindow tooltips { this, 700 };

    int uiScalePercent = 100;
    bool keysOn = false;
    juce::Point<int> logicalSize { sl::ui::Theme::baseWidth, sl::ui::Theme::baseHeight };
    bool constructed = false;   // resized() only records the size once the constructor has set it
    bool rescaling = false;     // while the UI Scale changes the window, the logical size stays

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StrandLockEditor)
};
