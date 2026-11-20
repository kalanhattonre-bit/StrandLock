// The M6 table's pictures (PLUGIN-PLAN 4 M6 "snapshots (artifact)", 7.8 T3): dark and light at 100 % and 200 % of
//   - Sample mode (8) with the chips, the chevrons and the per-note thumbnails;
//   - the Sample tab with faded controls (and, since T3, two Dice pins on, the recipes, Dice and Preview);
//   - the Macros tab;
//   - the heads overlay drawn from a fed DisplayFrame;
//   - Note settings showing "mixed".
// Both runners write them (the Release one uploads them; the Debug one draws them with JUCE's assertions live, and the
// CI counts 0). m6TablePictures checks that each one shows what it must, at its size and in its theme. The T1 and T2
// pictures (m6_sample_tab_*, m6_mode8_*, m6_heads_overlay_*, m6_note_settings_* ...) stay as review shots.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Inst.h"
#include "engine/DisplayFrame.h"
#include "model/Clip.h"
#include "model/ClipEdit.h"
#include "model/NoteSettings.h"
#include "params/ParamBridge.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/RollView.h"
#include "ui/tabs/MacrosTab.h"
#include "ui/tabs/NoteSettingsTab.h"
#include "ui/tabs/SampleTab.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;
    using ui::ThemeId;
    using ui::Tok;

    enum class Subject { mode8, sampleTab, macrosTab, heads, noteSettings };

    const char* nameOf (Subject s)
    {
        switch (s)
        {
            case Subject::mode8:        return "Sample mode";
            case Subject::sampleTab:    return "the Sample tab";
            case Subject::macrosTab:    return "the Macros tab";
            case Subject::heads:        return "the heads overlay";
            case Subject::noteSettings: return "Note settings";
        }
        return "";
    }

    /** One picture, and what the editor showed while it was taken. */
    struct Picture
    {
        juce::Image image;
        int logicalWidth = 0, logicalHeight = 0;
        bool shows = false;      // the subject is on screen as the table asks
        std::string what;        // what was seen, for the check's line
    };

    /** Twelve notes a beat apart, each with its own mix of Sample settings (T2's settingsClip): a selection of three is
        "mixed" in some groups. */
    model::Clip settingsClip()
    {
        model::Clip c = model::newClip (4.0);
        for (int i = 0; i < 12; ++i)
        {
            auto n = model::mkNote (60.0 + (double) (i % 5), (double) i, 0.9, "ns-" + std::to_string (i));
            auto& s = n.sample;
            if (i % 3 == 1)
                s.engine = core::Engine::cloud;
            else if (i % 3 == 2)
                s.engine = core::Engine::tape;
            if (i % 5 != 0)
                s.motion = (core::Motion) (i % 6);
            s.travel = i % 4 == 2 ? model::TravelKind::path : model::TravelKind::speed;
            if (i % 4 == 0)
                s.rev = true;
            n.vel = i % 4 == 0 ? 0.8 : 0.5;
            c.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
        }
        return c;
    }

    /** A display frame as the engine fills it: three voices, five heads (both ways, one still, one faint), five grains. */
    std::unique_ptr<engine::DisplayFrame> fedFrame()
    {
        auto f = std::make_unique<engine::DisplayFrame>();
        std::memset (f.get(), 0, sizeof (engine::DisplayFrame));
        f->playing = true;
        f->numVoices = 3;
        auto& v0 = f->voices[0];
        v0.p = 60;
        v0.env = 1.0f;
        v0.numHeads = 3;
        const float pos0[] = { 0.1f, 0.35f, 0.62f };
        const std::int8_t dirs[] = { 1, -1, 0 };
        for (int k = 0; k < 3; ++k)
        {
            v0.heads[k].index = (std::uint8_t) k;
            v0.heads[k].dir = dirs[k];
            v0.heads[k].pos = pos0[k];
            v0.heads[k].level = 0.7f;
        }
        auto& v1 = f->voices[1];
        v1.p = 64;
        v1.env = 1.0f;
        v1.numHeads = 1;
        v1.heads[0].dir = 1;
        v1.heads[0].pos = 0.8f;
        v1.heads[0].level = 0.25f;
        auto& v2 = f->voices[2];
        v2.p = 67;
        v2.env = 0.5f;
        v2.released = true;
        v2.numHeads = 1;
        v2.heads[0].dir = -1;
        v2.heads[0].pos = 0.9f;
        v2.heads[0].level = 1.0f;
        f->numGrains = 5;
        for (int i = 0; i < 5; ++i)
        {
            f->grains[i].pos = 0.2f + 0.1f * (float) i;
            f->grains[i].age = 0.25f * (float) i;
        }
        return f;
    }

    int channelDistance (juce::Colour a, juce::Colour b)
    {
        return std::max ({ std::abs ((int) a.getRed() - (int) b.getRed()), std::abs ((int) a.getGreen() - (int) b.getGreen()),
                           std::abs ((int) a.getBlue() - (int) b.getBlue()) });
    }

    /** How many pixels of area (logical px, scaled to the image) are within tolerance of colour. */
    int countNear (const juce::Image& image, juce::Rectangle<int> logical, float scale, juce::Colour colour, int tolerance)
    {
        const auto area = (logical.toFloat() * scale).toNearestInt().getIntersection (image.getBounds());
        int n = 0;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (channelDistance (pixels.getPixelColour (x, y), colour) <= tolerance)
                    ++n;
        return n;
    }

    double meanLuminance (const juce::Image& image)
    {
        double sum = 0.0;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto c = pixels.getPixelColour (x, y);
                sum += (0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue()) / 255.0;
            }
        return sum / ((double) image.getWidth() * (double) image.getHeight());
    }

    Picture shootM6 (Subject subject, ThemeId theme, int scale)
    {
        Picture out;
        StrandLockProcessor proc;
        proc.waitForSounds();
        auto& settings = proc.getUserSettings();
        settings.setTheme (theme);
        settings.setUiScalePercent (scale);
        settings.setKeysDefault (false);
        settings.setDicePins (subject == Subject::sampleTab ? std::vector<std::string> { "engine", "grain" } : std::vector<std::string> {});

        // the Sample tab whole needs a window 1200 high (a pane shows the whole tab only within 2/5 of the room, MainView)
        const bool tall = subject == Subject::sampleTab || subject == Subject::macrosTab || subject == Subject::noteSettings;
        out.logicalWidth = 1100;
        out.logicalHeight = subject == Subject::sampleTab ? 1200 : tall ? 900 : 680;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (out.logicalWidth, out.logicalHeight);
            auto& view = editor.getView();
            const auto clip = proc.getDocument().current();
            const auto ordered = model::timeOrder (clip->notes);

            switch (subject)
            {
                case Subject::mode8:
                {
                    // Sample mode on Travel, the tabs folded: a few of the demo's notes with their own Motion, two selected
                    view.setFolded (true);
                    model::SamplePatch b, f;
                    b.motion.emplace (core::Motion::backward);
                    f.motion.emplace (core::Motion::freeze);
                    auto edited = model::setNoteSample (*clip, std::vector<std::string> { ordered[3]->id, ordered[8]->id }, b);
                    edited = model::setNoteSample (edited, std::vector<std::string> { ordered[12]->id }, f);
                    proc.editClip ("Motions", edited);
                    proc.getDocument().setSelection ({ ordered[3]->id, ordered[4]->id }, ordered[3]->id);
                    view.setMode (8, 0);
                    view.getRoll().edited();
                    editor.refreshNow();
                    out.image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);   // the drawing simulates the notes
                    const int made = view.getRoll().getDecor().simulationsMade();
                    out.shows = view.getMode() == 8 && view.getSubLane() == 0 && view.getModeButton (8).getToggleState() && made > 0;
                    out.what = fmt ("mode %d on Travel, its button lit, %d note simulations drawn as chevrons and thumbnails", view.getMode(), made);
                    break;
                }
                case Subject::sampleTab:
                {
                    // the default settings, where many controls do nothing (faded), with the Engine and Grain pins on
                    view.openTab (MainView::sampleTab);
                    editor.refreshNow();
                    auto& tab = view.getSampleTab();
                    int faded = 0;
                    for (const auto& c : tab.controlStates())
                        faded += c.faded ? 1 : 0;
                    const int pane = view.getPaneArea().getHeight();
                    const bool whole = tab.getDice().getBottom() <= pane && tab.getRecipe (8).getBottom() <= pane;
                    const bool pinned = tab.getPin ("engine") != nullptr && tab.getPin ("engine")->isPinned() && tab.getPin ("grain")->isPinned();
                    out.shows = faded >= 5 && whole && pinned && tab.getPreview().isVisible();
                    out.what = fmt ("%d controls faded, the whole tab in its %d px pane (Dice ends at %d), the Engine and Grain pins on", faded, pane,
                                    tab.getDice().getBottom());
                    break;
                }
                case Subject::macrosTab:
                {
                    view.openTab (MainView::macrosTab);
                    core::InstPatch patch;
                    patch.macros = std::array<double, 8> { 0.75, 0.5, 0.6, 0.3, 0.5, 0.5, 0.95, 0.5 };
                    patch.offsets.tone = 0.2;
                    patch.offsets.amp = -0.2;
                    params::applyPatch (proc.getApvts(), patch);
                    editor.refreshNow();
                    auto& tab = view.getMacrosTab();
                    const int pane = view.getPaneArea().getHeight();
                    out.shows = tab.getMacro (7).getBottom() <= pane && tab.getOffset (4).getBottom() <= pane && tab.getMacro (0).readout().isNotEmpty();
                    out.what = fmt ("the 8 macros and 5 offsets in the %d px pane; Speed reads \"%s\"", pane, tab.getMacro (0).readout().toRawUTF8());
                    break;
                }
                case Subject::heads:
                {
                    // two selected notes' Start markers, and the live heads and grains of a fed frame
                    std::vector<std::string> ids { ordered[0]->id, ordered[1]->id };
                    model::SamplePatch a, b;
                    model::StartPatch sa, sb;
                    sa.mode = model::StartMode::region;
                    sa.pos = 0.25;
                    sb.mode = model::StartMode::random;
                    sb.pos = 0.55;
                    sb.range = 0.2;
                    a.start = sa;
                    b.start = sb;
                    auto edited = model::setNoteSample (*clip, std::vector<std::string> { ids[0] }, a);
                    edited = model::setNoteSample (edited, std::vector<std::string> { ids[1] }, b);
                    proc.editClip ("Starts", edited);
                    proc.getDocument().setSelection (ids, ids[0]);
                    view.getRoll().edited();
                    editor.refreshNow();
                    const auto frame = fedFrame();
                    auto& strip = view.getStrip();
                    strip.setDisplayFrame (frame.get());
                    const auto area = strip.getWaveArea().toFloat();
                    const auto heads = strip.getOverlay().heads (area, [&strip] (double p) { return strip.xOf (p); });
                    out.image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
                    const int headPixels = countNear (out.image, strip.getBounds(), (float) scale / 100.0f, ui::palette (theme)[Tok::head], 6);
                    out.shows = heads.size() == 5 && headPixels > 30;
                    out.what = fmt ("%zu heads of the fed frame on the strip, %d pixels in the heads' colour", heads.size(), headPixels);
                    break;
                }
                case Subject::noteSettings:
                {
                    proc.editClip ("Settings", settingsClip());
                    view.openTab (MainView::noteTab);
                    proc.getDocument().setSelection ({ "ns-3", "ns-4", "ns-5" }, std::string ("ns-4"));
                    view.getRoll().edited();
                    editor.refreshNow();
                    auto& tab = view.getNoteSettingsTab();
                    int mixed = 0;
                    for (int g = 0; g < ui::NoteSettingsTab::numGroups; ++g)
                        mixed += tab.isMixed (static_cast<ui::NoteSettingsTab::Group> (g)) ? 1 : 0;
                    out.shows = ! tab.isAsleep() && mixed >= 2;
                    out.what = fmt ("3 notes selected, %d groups say \"mixed\"", mixed);
                    break;
                }
            }
            if (! out.image.isValid())
                out.image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        }
        settings.setTheme (state::Theme::dark);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        settings.setDicePins ({});
        return out;
    }
}

SL_TEST ("ui", m6TablePictures)
{
    section ("The M6 table's pictures, dark and light at 100 % and 200 %: Sample mode with the chips, chevrons and thumbnails; the Sample tab "
             "with faded controls; the Macros tab; the heads overlay from a fed frame; Note settings showing \"mixed\"");

    for (const auto subject : { Subject::mode8, Subject::sampleTab, Subject::macrosTab, Subject::heads, Subject::noteSettings })
        for (const auto theme : { ThemeId::dark, ThemeId::light })
            for (const int scale : { 100, 200 })
            {
                const auto pic = shootM6 (subject, theme, scale);
                const bool dark = theme == ThemeId::dark;
                const double lum = pic.image.isValid() ? meanLuminance (pic.image) : -1.0;
                const bool size = pic.image.getWidth() == pic.logicalWidth * scale / 100 && pic.image.getHeight() == pic.logicalHeight * scale / 100;
                check (size && (dark ? lum < 0.25 : lum > 0.75) && pic.shows,
                       fmt ("%s, %s, %d %%: %d x %d px, mean luminance %.3f, %s", nameOf (subject), dark ? "dark" : "light", scale, pic.image.getWidth(),
                            pic.image.getHeight(), lum, pic.what.c_str()));
            }
}

SL_SNAPSHOT (m6_table_mode8_dark_100)          { return shootM6 (Subject::mode8, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m6_table_mode8_light_100)         { return shootM6 (Subject::mode8, ThemeId::light, 100).image; }
SL_SNAPSHOT (m6_table_mode8_dark_200)          { return shootM6 (Subject::mode8, ThemeId::dark, 200).image; }
SL_SNAPSHOT (m6_table_mode8_light_200)         { return shootM6 (Subject::mode8, ThemeId::light, 200).image; }
SL_SNAPSHOT (m6_table_sample_tab_dark_100)     { return shootM6 (Subject::sampleTab, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m6_table_sample_tab_light_100)    { return shootM6 (Subject::sampleTab, ThemeId::light, 100).image; }
SL_SNAPSHOT (m6_table_sample_tab_dark_200)     { return shootM6 (Subject::sampleTab, ThemeId::dark, 200).image; }
SL_SNAPSHOT (m6_table_sample_tab_light_200)    { return shootM6 (Subject::sampleTab, ThemeId::light, 200).image; }
SL_SNAPSHOT (m6_table_macros_tab_dark_100)     { return shootM6 (Subject::macrosTab, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m6_table_macros_tab_light_100)    { return shootM6 (Subject::macrosTab, ThemeId::light, 100).image; }
SL_SNAPSHOT (m6_table_macros_tab_dark_200)     { return shootM6 (Subject::macrosTab, ThemeId::dark, 200).image; }
SL_SNAPSHOT (m6_table_macros_tab_light_200)    { return shootM6 (Subject::macrosTab, ThemeId::light, 200).image; }
SL_SNAPSHOT (m6_table_heads_dark_100)          { return shootM6 (Subject::heads, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m6_table_heads_light_100)         { return shootM6 (Subject::heads, ThemeId::light, 100).image; }
SL_SNAPSHOT (m6_table_heads_dark_200)          { return shootM6 (Subject::heads, ThemeId::dark, 200).image; }
SL_SNAPSHOT (m6_table_heads_light_200)         { return shootM6 (Subject::heads, ThemeId::light, 200).image; }
SL_SNAPSHOT (m6_table_note_settings_dark_100)  { return shootM6 (Subject::noteSettings, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m6_table_note_settings_light_100) { return shootM6 (Subject::noteSettings, ThemeId::light, 100).image; }
SL_SNAPSHOT (m6_table_note_settings_dark_200)  { return shootM6 (Subject::noteSettings, ThemeId::dark, 200).image; }
SL_SNAPSHOT (m6_table_note_settings_light_200) { return shootM6 (Subject::noteSettings, ThemeId::light, 200).image; }
