// M7 V2's pictures (PLUGIN-PLAN 4 M7, 7.9 V2): the part chips on a song (one chip hovered: its dashed box on the
// waveform), a short file's suggestion line (root, key, Loop cleanly, the engine note), the part picker over a
// 12-minute file, and the Sound tab with Detect pitch & key, Key, Scale and Scale lock. Dark and light at 100 %, the chips
// at 200 % too. Both runners write them (the Debug one with JUCE's assertions live); m7Pictures checks that each shows
// what it must, at its size and in its theme. The audio files are made at test time (none is committed).

#include "plugin/SongRig.h"

#include "PluginEditor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"
#include "ui/MainView.h"
#include "ui/tabs/SoundTab.h"

#include <cmath>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;
    using ui::ThemeId;

    enum class Subject { chips, suggestions, picker, soundTab };

    const char* nameOf (Subject s)
    {
        switch (s)
        {
            case Subject::chips:       return "the part chips on a song";
            case Subject::suggestions: return "a short file's suggestion line";
            case Subject::picker:      return "the part picker";
            case Subject::soundTab:    return "the Sound tab";
        }
        return "";
    }

    /** The files, made once per run. */
    struct Files
    {
        juce::File dir, song, tone, twelve;
    };

    const Files& files()
    {
        static const Files f = []
        {
            Files out;
            out.dir = songs::tempFolder ("m7-pictures");
            out.song = out.dir.getChildFile ("minute song.wav");
            out.tone = out.dir.getChildFile ("a tone.wav");
            out.twelve = out.dir.getChildFile ("long song.wav");
            songs::writeMono16 (out.song, songs::minuteSong (22050.0), 22050.0);
            songs::writeMono16 (out.tone, signals::tone (signals::midiHz (45.23), 2.0, 44100.0), 44100.0);
            songs::writeWav16 (out.twelve, 1, 8000.0, 12 * 60 * 8000, [] (std::int64_t start, int count, float* const* o)
            {
                for (int i = 0; i < count; ++i)
                {
                    const double t = (double) (start + i) / 8000.0;
                    const double swell = 0.2 + 0.18 * std::sin (2.0 * 3.141592653589793 * t / 90.0) + 0.1 * std::sin (2.0 * 3.141592653589793 * t / 17.0);
                    o[0][i] = (float) (swell * std::sin (2.0 * 3.141592653589793 * 110.0 * t));
                }
            });
            return out;
        }();
        return f;
    }

    struct Picture
    {
        juce::Image image;
        int logicalWidth = 1100, logicalHeight = 680;
        bool shows = false;
        std::string what;
    };

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

    Picture shootM7 (Subject subject, ThemeId theme, int scale)
    {
        Picture out;
        const auto& f = files();
        StrandLockProcessor proc;
        auto& settings = proc.getUserSettings();
        settings.setTheme (theme);
        settings.setUiScalePercent (scale);
        settings.setKeysDefault (false);
        proc.waitForSounds();
        out.logicalHeight = subject == Subject::soundTab ? 900 : 680;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (out.logicalWidth, out.logicalHeight);
            auto& view = editor.getView();
            auto& strip = view.getStrip();
            view.setFolded (subject != Subject::soundTab);
            switch (subject)
            {
                case Subject::chips:
                {
                    strip.loadFile (f.song);
                    proc.waitForSounds (30000);
                    proc.waitForAnalysis (30000);
                    editor.refreshNow();
                    const auto& parts = proc.getSuggestions().analysis.parts;
                    if (parts.size() > 1)
                        strip.getChips().showHover (parts[1]);
                    editor.refreshNow();
                    const auto texts = strip.getChips().texts();
                    out.shows = strip.hasChipRow() && texts.size() >= 4 && strip.getChips().hoveredPart().has_value();
                    out.what = fmt ("chips: %s; one hovered", texts.joinIntoString (" | ").toRawUTF8());
                    break;
                }
                case Subject::suggestions:
                {
                    strip.loadFile (f.tone);
                    proc.waitForSounds (30000);
                    editor.refreshNow();
                    const auto texts = strip.getChips().texts();
                    out.shows = texts.contains ("Use A1 as the root") && texts.contains ("Use key A major") && strip.headerText().contains ("key A major");
                    out.what = fmt ("the file line \"%s\"; chips: %s", strip.headerText().toRawUTF8(), texts.joinIntoString (" | ").toRawUTF8());
                    break;
                }
                case Subject::picker:
                {
                    strip.loadFile (f.twelve);
                    proc.waitForSounds (60000);
                    editor.refreshNow();
                    proc.waitForAnalysis (60000);
                    editor.refreshNow();
                    auto& picker = strip.getPicker();
                    picker.setStartFrame (150 * 8000);
                    out.shows = picker.isOpen() && picker.hasOverview();
                    out.what = fmt ("the picker open on \"%s\", the overview drawn", picker.timesText().toRawUTF8());
                    break;
                }
                case Subject::soundTab:
                {
                    strip.loadFile (f.tone);
                    proc.waitForSounds (30000);
                    proc.acceptKeySuggestion();
                    view.openTab (MainView::soundTab);
                    editor.refreshNow();
                    auto& tab = view.getSoundTab();
                    const int pane = view.getPaneArea().getHeight();
                    out.shows = tab.getDetectButton().isShowing() && tab.getKeyBox().getText() == "A" && tab.getScaleBox().getText() == "Major"
                             && tab.getScaleLock().getBottom() <= pane;
                    out.what = fmt ("Detect pitch & key, Key \"%s\", Scale \"%s\", Scale lock in the %d px pane", tab.getKeyBox().getText().toRawUTF8(),
                                    tab.getScaleBox().getText().toRawUTF8(), pane);
                    break;
                }
            }
            out.image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        }
        settings.setTheme (state::Theme::dark);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        return out;
    }
}

SL_TEST ("ui", m7Pictures)
{
    section ("M7 V2's pictures, dark and light: the part chips (one hovered), the suggestion line, the part picker, the Sound tab");

    for (const auto subject : { Subject::chips, Subject::suggestions, Subject::picker, Subject::soundTab })
        for (const auto theme : { ThemeId::dark, ThemeId::light })
        {
            const int scale = 100;
            const auto pic = shootM7 (subject, theme, scale);
            const bool dark = theme == ThemeId::dark;
            const double lum = pic.image.isValid() ? meanLuminance (pic.image) : -1.0;
            const bool size = pic.image.getWidth() == pic.logicalWidth * scale / 100 && pic.image.getHeight() == pic.logicalHeight * scale / 100;
            check (size && (dark ? lum < 0.25 : lum > 0.75) && pic.shows,
                   fmt ("%s, %s: %d x %d px, mean luminance %.3f, %s", nameOf (subject), dark ? "dark" : "light", pic.image.getWidth(), pic.image.getHeight(),
                        lum, pic.what.c_str()));
        }
}

SL_SNAPSHOT (m7_chips_dark_100)        { return shootM7 (Subject::chips, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m7_chips_light_100)       { return shootM7 (Subject::chips, ThemeId::light, 100).image; }
SL_SNAPSHOT (m7_chips_dark_200)        { return shootM7 (Subject::chips, ThemeId::dark, 200).image; }
SL_SNAPSHOT (m7_suggestions_dark_100)  { return shootM7 (Subject::suggestions, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m7_suggestions_light_100) { return shootM7 (Subject::suggestions, ThemeId::light, 100).image; }
SL_SNAPSHOT (m7_picker_dark_100)       { return shootM7 (Subject::picker, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m7_picker_light_100)      { return shootM7 (Subject::picker, ThemeId::light, 100).image; }
SL_SNAPSHOT (m7_sound_tab_dark_100)    { return shootM7 (Subject::soundTab, ThemeId::dark, 100).image; }
SL_SNAPSHOT (m7_sound_tab_light_100)   { return shootM7 (Subject::soundTab, ThemeId::light, 100).image; }
