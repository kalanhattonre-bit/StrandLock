// M7 V3's pictures (PLUGIN-PLAN 7.9 V3): the Locate banner (a project whose file is found nowhere), the 20-50 MB question
// with its three answers, the line that says only part of the sound was kept (the strip shows that part with the Region on
// it), and the Sound tab's "Keep the sound in the project" row with the library line. Dark and light at 100 %. Both
// runners write them (the Debug one with JUCE's assertions live); m7EmbedPictures checks that each shows what it must, at
// its size and in its theme. The audio files and projects are made at test time (none is committed).

#include "plugin/SongRig.h"

#include "PluginEditor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/SoundBanner.h"
#include "ui/tabs/SoundTab.h"

#include <cmath>
#include <cstring>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;
    using ui::ThemeId;

    enum class Subject { locate, question, partKept, keepRow };

    const char* nameOf (Subject s)
    {
        switch (s)
        {
            case Subject::locate:   return "the Locate banner";
            case Subject::question: return "the 20-50 MB question";
            case Subject::partKept: return "a part kept in the project";
            case Subject::keepRow:  return "the Sound tab's keep row";
        }
        return "";
    }

    juce::MemoryBlock saveOf (StrandLockProcessor& p) { return songs::saveOf (p); }

    /** The files and projects, made once per run. */
    struct Made
    {
        juce::File dir, song, tone;
        juce::MemoryBlock missingProject, partProject;
    };

    bool writeTone (const juce::File& file, double hz, double rate, std::int64_t frames)
    {
        return songs::writeWav16 (file, 1, rate, frames, [hz, rate] (std::int64_t start, int count, float* const* o)
        {
            for (int i = 0; i < count; ++i)
            {
                const double t = (double) (start + i) / rate;
                o[0][i] = (float) ((0.2 + 0.15 * std::sin (2.0 * 3.141592653589793 * t / 23.0)) * std::sin (2.0 * 3.141592653589793 * hz * t));
            }
        });
    }

    const Made& made()
    {
        static const Made m = []
        {
            Made out;
            out.dir = songs::tempFolder ("m7-embed-pictures");
            out.song = out.dir.getChildFile ("big song.wav");          // 4 minutes, mono 16-bit: 21.2 MB, so StrandLock asks
            out.tone = out.dir.getChildFile ("a tone.wav");
            writeTone (out.song, 110.0, 44100.0, 240 * 44100);
            writeTone (out.tone, 220.0, 44100.0, 2 * 44100);

            // A project whose file is gone and that kept nothing of it (the switch off).
            {
                const auto gone = out.dir.getChildFile ("lost song.wav");
                writeTone (gone, 147.0, 44100.0, 30 * 44100);
                StrandLockProcessor p;
                p.waitForSounds();
                p.loadSoundFile (gone);
                p.waitForSounds (30000);
                p.setKeepInProject (false);
                out.missingProject = saveOf (p);
                gone.deleteFile();
            }
            // A project that kept only the Region (Just the Region), its file gone.
            {
                const auto part = out.dir.getChildFile ("part song.wav");
                writeTone (part, 98.0, 44100.0, 240 * 44100);
                StrandLockProcessor p;
                p.waitForSounds();
                p.loadSoundFile (part);
                p.waitForSounds (60000);
                p.setRegionStep (100.0 / 240.0, 104.0 / 240.0, "Region");
                p.answerEmbedQuestion (StrandLockProcessor::EmbedAnswer::region);
                p.waitForEmbed (60000);
                out.partProject = saveOf (p);
                part.deleteFile();
            }
            return out;
        }();
        return m;
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

    Picture shoot (Subject subject, ThemeId theme)
    {
        Picture out;
        const auto& m = made();
        StrandLockProcessor proc;
        auto& settings = proc.getUserSettings();
        settings.setTheme (theme);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        proc.waitForSounds();
        out.logicalHeight = subject == Subject::keepRow ? 1100 : 680;   // the Sound tab whole, with its two new rows
        switch (subject)
        {
            case Subject::locate:
                proc.setStateInformation (m.missingProject.getData(), (int) m.missingProject.getSize());
                break;
            case Subject::partKept:
                proc.setStateInformation (m.partProject.getData(), (int) m.partProject.getSize());
                break;
            case Subject::question:
                proc.loadSoundFile (m.song);
                proc.waitForSounds (60000);
                break;
            case Subject::keepRow:
                proc.loadSoundFile (m.tone);
                proc.waitForSounds (30000);
                proc.waitForEmbed();
                break;
        }
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (out.logicalWidth, out.logicalHeight);
            auto& view = editor.getView();
            view.setFolded (subject != Subject::keepRow);
            if (subject == Subject::keepRow)
                view.openTab (MainView::soundTab);
            editor.refreshNow();
            editor.refreshNow();
            auto& banner = view.getSoundBanner();
            switch (subject)
            {
                case Subject::locate:
                    out.shows = banner.isVisible() && banner.isLocate() && banner.getLocateButton().isVisible() && banner.getText().contains ("lost song.wav");
                    out.what = fmt ("\"%s\" with Locate sample...", banner.getText().toRawUTF8());
                    break;
                case Subject::question:
                    out.shows = banner.isVisible() && banner.isQuestion() && banner.getYesButton().isVisible() && banner.getNoButton().isVisible()
                             && banner.getRegionButton().isVisible() && banner.getText().contains ("21.2 MB")
                             && banner.getYesButton().getRight() <= banner.getNoButton().getX();
                    out.what = fmt ("\"%s\" with Yes, No and Just the Region", banner.getText().toRawUTF8());
                    break;
                case Subject::partKept:
                {
                    const auto& strip = view.getStrip();
                    const auto* shown = strip.shownSound();
                    out.shows = banner.isVisible() && banner.isPartKept() && banner.getLocateButton().isVisible() && shown != nullptr
                             && shown->source == doc::SoundSource::excerpt && banner.getText().contains ("Part of this sound was not saved in the project");
                    out.what = fmt ("\"%s\"; the strip shows the part kept", banner.getText().toRawUTF8());
                    break;
                }
                case Subject::keepRow:
                {
                    auto& tab = view.getSoundTab();
                    const int pane = view.getPaneArea().getHeight();
                    // isVisible, not isShowing: the test's editor is on no screen (it has no window of its own)
                    out.shows = tab.getKeepToggle().isVisible() && tab.getKeepToggle().getToggleState() && tab.getKeepStatus().startsWith ("Kept in the project")
                             && tab.getLibraryToggle().isVisible() && tab.getLibraryToggle().getBottom() <= pane && ! banner.isVisible();
                    out.what = fmt ("the switch on, \"%s\"; \"%s\" in the %d px pane", tab.getKeepStatus().toRawUTF8(), tab.getLibraryStatus().toRawUTF8(), pane);
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

SL_TEST ("ui", m7EmbedPictures)
{
    section ("M7 V3's pictures, dark and light: the Locate banner, the 20-50 MB question, a part kept, the Sound tab's keep row");

    for (const auto subject : { Subject::locate, Subject::question, Subject::partKept, Subject::keepRow })
        for (const auto theme : { ThemeId::dark, ThemeId::light })
        {
            const auto pic = shoot (subject, theme);
            const bool dark = theme == ThemeId::dark;
            const double lum = pic.image.isValid() ? meanLuminance (pic.image) : -1.0;
            const bool size = pic.image.getWidth() == pic.logicalWidth && pic.image.getHeight() == pic.logicalHeight;
            check (size && (dark ? lum < 0.25 : lum > 0.75) && pic.shows,
                   fmt ("%s, %s: %d x %d px, mean luminance %.3f, %s", nameOf (subject), dark ? "dark" : "light", pic.image.getWidth(), pic.image.getHeight(),
                        lum, pic.what.c_str()));
        }
}

SL_SNAPSHOT (m7_locate_dark_100)     { return shoot (Subject::locate, ThemeId::dark).image; }
SL_SNAPSHOT (m7_locate_light_100)    { return shoot (Subject::locate, ThemeId::light).image; }
SL_SNAPSHOT (m7_question_dark_100)   { return shoot (Subject::question, ThemeId::dark).image; }
SL_SNAPSHOT (m7_question_light_100)  { return shoot (Subject::question, ThemeId::light).image; }
SL_SNAPSHOT (m7_part_kept_dark_100)  { return shoot (Subject::partKept, ThemeId::dark).image; }
SL_SNAPSHOT (m7_part_kept_light_100) { return shoot (Subject::partKept, ThemeId::light).image; }
SL_SNAPSHOT (m7_keep_row_dark_100)   { return shoot (Subject::keepRow, ThemeId::dark).image; }
SL_SNAPSHOT (m7_keep_row_light_100)  { return shoot (Subject::keepRow, ThemeId::light).image; }
