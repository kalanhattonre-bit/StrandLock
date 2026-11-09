#include "PluginEditor.h"

#include <cmath>
#include <functional>

using sl::ui::Theme;
using sl::ui::ThemeId;

namespace
{
    int scaledBy (int logical, int percent)
    {
        return juce::roundToInt ((double) logical * (double) percent / 100.0);
    }

    void stopWantingFocus (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            child->setWantsKeyboardFocus (false);
            stopWantingFocus (*child);
        }
    }
}

StrandLockEditor::StrandLockEditor (StrandLockProcessor& p)
    : AudioProcessorEditor (&p), proc (p), theme (p.getTheme()), lookAndFeel (theme), view (theme, p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    // The person's own UI Scale wins; else this project's copy (UIST); else 100 %.
    auto& settings = proc.getUserSettings();
    const auto ui = proc.getUiState();
    uiScalePercent = juce::jlimit (Theme::minScalePercent, Theme::maxScalePercent,
                                   settings.uiScalePercent().value_or ((int) ui.uiScalePercent));
    keysOn = settings.keysDefault();
    logicalSize = { juce::jlimit (Theme::minWidth, Theme::maxWidth, (int) ui.logicalWidth),
                    juce::jlimit (Theme::minHeight, Theme::maxHeight, (int) ui.logicalHeight) };

    view.onThemeToggle = [this] { setThemeId (theme.isDark() ? ThemeId::light : ThemeId::dark); };
    view.onKeysToggle = [this] { setKeysOn (! keysOn); };
    view.onUiScale = [this] (int percent) { setUiScalePercent (percent); };
    view.setKeysOn (keysOn);
    view.setUiScalePercent (uiScalePercent);

    // Size first: setResizeLimits clamps the current size, and a 0 x 0 editor would become the minimum.
    setSize (scaledBy (logicalSize.x, uiScalePercent), scaledBy (logicalSize.y, uiScalePercent));
    setResizable (true, true);
    applyLimits();
    applyFocusRules();

    constructed = true;
    refreshNow();
    startTimerHz (timerHz);
}

StrandLockEditor::~StrandLockEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void StrandLockEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme (sl::ui::Tok::bg));
}

void StrandLockEditor::resized()
{
    // The view is laid out in logical pixels and drawn scaled; ceil so it always covers the whole window.
    const double s = (double) uiScalePercent / 100.0;
    const int w = (int) std::ceil ((double) getWidth() / s - 1.0e-6);
    const int h = (int) std::ceil ((double) getHeight() / s - 1.0e-6);
    view.setTransform (juce::AffineTransform::scale ((float) s));
    view.setBounds (0, 0, juce::jmax (1, w), juce::jmax (1, h));

    if (constructed && ! rescaling)
    {
        logicalSize = { juce::jlimit (Theme::minWidth, Theme::maxWidth, juce::roundToInt ((double) getWidth() / s)),
                        juce::jlimit (Theme::minHeight, Theme::maxHeight, juce::roundToInt ((double) getHeight() / s)) };
        auto ui = proc.getUiState();
        ui.logicalWidth = (std::uint16_t) logicalSize.x;
        ui.logicalHeight = (std::uint16_t) logicalSize.y;
        proc.setUiState (ui);
    }
}

void StrandLockEditor::applyLimits()
{
    setResizeLimits (scaledBy (Theme::minWidth, uiScalePercent), scaledBy (Theme::minHeight, uiScalePercent),
                     scaledBy (Theme::maxWidth, uiScalePercent), scaledBy (Theme::maxHeight, uiScalePercent));
}

void StrandLockEditor::setLogicalSize (int width, int height)
{
    setSize (scaledBy (juce::jlimit (Theme::minWidth, Theme::maxWidth, width), uiScalePercent),
             scaledBy (juce::jlimit (Theme::minHeight, Theme::maxHeight, height), uiScalePercent));
}

void StrandLockEditor::setUiScalePercent (int percent)
{
    percent = juce::jlimit (Theme::minScalePercent, Theme::maxScalePercent, percent);
    view.setUiScalePercent (percent);
    if (percent == uiScalePercent)
        return;

    uiScalePercent = percent;
    proc.getUserSettings().setUiScalePercent (percent);
    auto ui = proc.getUiState();
    ui.uiScalePercent = (std::uint16_t) percent;
    proc.setUiState (ui);

    // The window keeps its logical size: new limits first (they may clamp it on the way), then the size.
    {
        const juce::ScopedValueSetter<bool> hold (rescaling, true);
        applyLimits();
        setSize (scaledBy (logicalSize.x, percent), scaledBy (logicalSize.y, percent));
    }
    resized();
    proc.getDiag().setDisplayScale (desktopScale(), uiScalePercent);
}

void StrandLockEditor::setThemeId (ThemeId t)
{
    proc.setTheme (t);   // this person's choice (every project) and this project's copy
    theme.set (t);
    lookAndFeel.refreshColours();
    sendLookAndFeelChange();
    view.themeChanged();
    applyFocusRules();   // a look-and-feel change rebuilds the drop-downs' labels
    repaint();
}

void StrandLockEditor::setKeysOn (bool on)
{
    keysOn = on;
    proc.getUserSettings().setKeysDefault (on);
    view.setKeysOn (on);
    applyFocusRules();
}

void StrandLockEditor::applyFocusRules()
{
    // No control ever holds the keyboard; the editor itself does, and only while Keys is on.
    stopWantingFocus (*this);
    setWantsKeyboardFocus (keysOn);
    setMouseClickGrabsKeyboardFocus (keysOn);

    // Keys off: a click on StrandLock should not take the keyboard from Cubase at all. The host's window for
    // the editor is the top-level component; when it does not grab focus on a click, Windows does not
    // activate it (WM_MOUSEACTIVATE answers MA_NOACTIVATE), so Space, 1-8 and the rest stay with Cubase.
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
        top->setMouseClickGrabsKeyboardFocus (keysOn);

    if (! keysOn && hasKeyboardFocus (true))
        giveAwayKeyboardFocus();
}

void StrandLockEditor::parentHierarchyChanged()
{
    applyFocusRules();
}

bool StrandLockEditor::keyPressed (const juce::KeyPress& key)
{
    proc.getDiag().keyReceived (key.getTextDescription(), keysOn);

    // Space always goes on to Cubase (play and stop), whatever the Keys switch says.
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
        return false;
    return keysOn;
}

//==================================================================================================
// Drops: recorded on the Diagnostics page (checklist step 6); loading the sound comes with M2.

bool StrandLockEditor::isInterestedInFileDrag (const juce::StringArray&) { return true; }

void StrandLockEditor::fileDragEnter (const juce::StringArray& files, int, int)
{
    view.showDropOverlay (true, files.size() == 1 ? juce::String ("Drop it: the Diagnostics page shows what arrived")
                                                  : juce::String (files.size()) + " files: the Diagnostics page shows what arrived");
}

void StrandLockEditor::fileDragExit (const juce::StringArray&)
{
    view.showDropOverlay (false);
}

void StrandLockEditor::filesDropped (const juce::StringArray& files, int, int)
{
    view.showDropOverlay (false);
    proc.getDiag().filesDropped (files);

    const auto snap = proc.getDiag().snapshot();
    juce::String message;
    if (files.size() == 1)
        message = "Got " + (juce::File::isAbsolutePath (files[0]) ? juce::File (files[0]).getFileName() : files[0]);
    else
        message = "Got " + juce::String (files.size()) + " files";
    if (files.size() == 1 && ! snap.drops.empty() && snap.drops.back().readable)
        message << " (" << juce::String (snap.drops.back().seconds, 2) << " s)";
    message << ". Loading sounds arrives in M2.";

    view.openTab (sl::ui::MainView::diagTab);
    view.showToast (message);
    view.tick (snap, keysOn);
}

bool StrandLockEditor::isInterestedInTextDrag (const juce::String&) { return true; }

void StrandLockEditor::textDragEnter (const juce::String&, int, int)
{
    view.showDropOverlay (true, "Drop it: the Diagnostics page shows the text that arrived");
}

void StrandLockEditor::textDragExit (const juce::String&)
{
    view.showDropOverlay (false);
}

void StrandLockEditor::textDropped (const juce::String& text, int, int)
{
    view.showDropOverlay (false);
    proc.getDiag().textDropped (text);
    view.openTab (sl::ui::MainView::diagTab);
    view.showToast ("Got text, not a file: see the Diagnostics page");
    refreshNow();
}

//==================================================================================================

double StrandLockEditor::desktopScale() const
{
    // Windows display scaling: the host's scale of this editor times the window's own (per-monitor) scale.
    double s = (double) juce::Component::getApproximateScaleFactorForComponent (this);
    if (auto* peer = getPeer())
        s *= peer->getPlatformScaleFactor();
    return s;
}

void StrandLockEditor::refreshNow()
{
    proc.getDiag().setDisplayScale (desktopScale(), uiScalePercent);
    const auto snap = proc.getDiag().snapshot();
    view.tick (snap, keysOn);
}

void StrandLockEditor::timerCallback()
{
    refreshNow();
}
