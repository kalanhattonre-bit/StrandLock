// The roll's pictures (PLUGIN-PLAN 4 M4 "snapshots", 7.6 R2): the whole editor with the piano roll in place of
// M0's placeholder, showing the Strand Demo clip as notes, the tool tabs folded so the roll has the room (as the
// prototype opens on a laptop). Dark and light x Build (nothing selected, the first view: centred on the notes),
// Pitch (the lead note selected: its pitch nodes, the segment diamonds, the slide diamonds and its slide link) and
// Tone (the first chord selected: the Tone ribbons in front, with their nodes) x 75 % / 100 % / 200 %, and the
// narrow 900 x 560 window. Extra pictures for review: Cubase playing (the playhead, a key played live), a note
// being dragged (the readout), a box with a ghost preview, and Sample mode's Travel lane.
//
// The writers run in the Release runner (uploaded as StrandLock-UI-Snapshots) and in the Debug runner, where
// JUCE's assertions are live and none may fire. The checks look at the pictures: their size and brightness, the
// notes in the note colour, the active lane's colour in front only in its mode, the slide diamonds, the playhead.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Seeds.h"
#include "engine/DisplayFrame.h"
#include "engine/EngineConfig.h"
#include "model/Clip.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/Theme.h"
#include "ui/roll/RollView.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::ThemeId;
    using ui::Tok;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    int channelDistance (juce::Colour a, juce::Colour b)
    {
        return std::max ({ std::abs ((int) a.getRed() - (int) b.getRed()), std::abs ((int) a.getGreen() - (int) b.getGreen()),
                           std::abs ((int) a.getBlue() - (int) b.getBlue()) });
    }

    int countNear (const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour, int tolerance)
    {
        int n = 0;
        area = area.getIntersection (image.getBounds());
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (channelDistance (pixels.getPixelColour (x, y), colour) <= tolerance)
                    ++n;
        return n;
    }

    double meanLuminance (const juce::Image& image)
    {
        if (! image.isValid())
            return -1.0;
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

    //==============================================================================================
    struct RollShot
    {
        ThemeId theme = ThemeId::dark;
        int mode = 1;               // 1 Build, 2 Pitch, 5 Tone, 8 Sample (its Travel lane)
        int scalePercent = 100;
        int width = 0, height = 0;  // logical size; 0 keeps 1100 x 680
        bool playing = false;       // Cubase plays at beat 5.5 with C3 held on its keyboard
        bool drag = false;          // a note being dragged (its readout)
        bool boxAndGhost = false;   // a box dragged on empty space, and a ghost preview
    };

    struct Shot
    {
        juce::Image image;
        juce::Rectangle<int> canvas;                 // the roll's canvas, image pixels
        std::vector<juce::Point<int>> slideDiamonds; // the primary note's slide diamonds, image pixels
        int playheadX = -1;                          // the playhead's column, image pixels
        juce::Rectangle<int> box;                    // the box being dragged, image pixels
        int visibleNotes = 0;                        // notes the roll shows
    };

    Shot shoot (const RollShot& s)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();   // the strip draws the default sound once it is there
        auto& settings = proc.getUserSettings();
        settings.setTheme (s.theme);
        settings.setUiScalePercent (s.scalePercent);
        settings.setKeysDefault (false);

        Shot out;
        {
            StrandLockEditor editor (proc);
            if (s.width > 0)
                editor.setLogicalSize (s.width, s.height);
            auto& view = editor.getView();
            auto& roll = view.getRoll();
            auto& doc = proc.getDocument();
            view.setFolded (true);

            if (s.mode == 2)
                doc.setSelection ({ "sd-25" }, std::string ("sd-25"));
            else if (s.mode == 5)
                doc.setSelection ({ "sd-1", "sd-2", "sd-3", "sd-4" }, std::string ("sd-4"));
            else if (s.mode == 8)
                doc.setSelection ({ "sd-11", "sd-12", "sd-13" }, std::string ("sd-12"));
            view.setMode (s.mode, 0);
            editor.refreshNow();

            // the Build pictures keep the first view (centred on the notes); the others centre on what they show
            const auto& geo = roll.getGeometry();
            const double centrePitch = s.mode == 2 ? 79.0 : s.mode == 5 ? 63.0 : s.mode == 8 ? 75.0 : -1.0;
            if (centrePitch > 0.0)
                roll.setScroll (0.0, geo.cy (centrePitch) - geo.viewHeight() / 2.0);

            if (s.playing)
            {
                auto frame = std::make_unique<engine::DisplayFrame>();
                frame->playing = true;
                frame->beat = 5.5;
                frame->numVoices = 1;
                frame->voices[0].noteIdHash = core::idHash (u"live:60");
                frame->voices[0].p = 60;
                frame->voices[0].kind = (std::uint8_t) engine::VoiceKind::live;
                frame->voices[0].released = false;
                frame->voices[0].env = 1.0f;
                roll.setDisplayFrame (frame.get());
            }

            auto& gestures = roll.getGestures();
            if (s.drag)
            {
                // the first chord's E3 (sd-3) dragged a beat later and two rows up, not let go yet
                const auto clip = doc.current();   // held: the drag makes new clip versions
                for (const auto& n : clip->notes)
                    if (n->id == "sd-3")
                    {
                        const double x = (n->s + n->l / 2.0) * geo.beatWidth(), y = geo.baseYAt (*n, 0.5);
                        gestures.pointerDown (x, y, {}, 1000.0);
                        gestures.pointerMove (x + geo.beatWidth(), y - 2.0 * geo.rowHeight(), {});
                        break;
                    }
            }
            if (s.boxAndGhost)
            {
                // a box from beat 13, E4 (empty space in the first view) to beat 15, C#4; and a ghost of the
                // c3 chord two semitones up
                const double x0 = 13.0 * geo.beatWidth(), y0 = geo.cy (76.0);
                gestures.pointerDown (x0, y0, {}, 1000.0);
                gestures.pointerMove (x0 + 2.0 * geo.beatWidth(), y0 + 3.0 * geo.rowHeight(), {});
                model::Clip ghost = *doc.current();
                for (auto& n : ghost.notes)
                    if (n->id == "sd-19" || n->id == "sd-20" || n->id == "sd-21" || n->id == "sd-22")
                    {
                        model::Note up = *n;
                        up.p += 2.0;
                        n = std::make_shared<const model::Note> (std::move (up));
                    }
                roll.setGhost (std::make_shared<const model::Clip> (std::move (ghost)));
            }
            editor.refreshNow();
            roll.refresh();

            out.image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);

            // where things are in the picture: the canvas, the slide diamonds, the playhead
            const float scale = (float) editor.getUiScalePercent() / 100.0f;
            const auto canvasInView = roll.getCanvasArea() + roll.getPosition();
            out.canvas = (canvasInView.toFloat() * scale).getSmallestIntegerContainer();
            const auto toImage = [&] (double rx, double ry)
            {
                const auto v = geo.toView (rx, ry);
                return juce::Point<int> (juce::roundToInt ((float) (canvasInView.getX() + v.x) * scale),
                                         juce::roundToInt ((float) (canvasInView.getY() + v.y) * scale));
            };
            const auto& st = roll.getPaintState();
            if (const auto P = st.noteById (st.roll.primary))
                if (const auto gh = geo.glideHandles (*P))
                {
                    out.slideDiamonds.push_back (toImage (gh->start.x, gh->start.y));
                    out.slideDiamonds.push_back (toImage (gh->mid.x, gh->mid.y));
                }
            if (const auto ph = roll.getPlayhead())
                out.playheadX = toImage (*ph * geo.beatWidth(), 0.0).x;
            if (const auto b = gestures.box())
                out.box = juce::Rectangle<int>::leftTopRightBottom (toImage (b->x0, b->y0).x, toImage (b->x0, b->y0).y,
                                                                    toImage (b->x1, b->y1).x, toImage (b->x1, b->y1).y);
            const auto visible = juce::Rectangle<double> (geo.scrollX(), geo.scrollY(), geo.viewWidth(), geo.viewHeight());
            out.visibleNotes = (int) ui::roll::RollPainter (st).visibleNotes (visible).size();
        }
        resetUserSettings (proc);
        return out;
    }

    const char* themeName (ThemeId t) { return t == ThemeId::dark ? "dark" : "light"; }
    const char* modeName (int m) { return m == 1 ? "Build" : m == 2 ? "Pitch" : m == 5 ? "Tone" : "Sample"; }
}

//==================================================================================================
SL_TEST ("ui", rollSnapshotsDraw)
{
    section ("Roll snapshots: dark and light x Build, Pitch, Tone x 75 / 100 / 200 %, and 900 x 560: the notes drawn");

    for (const auto theme : { ThemeId::dark, ThemeId::light })
    {
        const auto& P = ui::palette (theme);
        const bool dark = theme == ThemeId::dark;
        for (const int mode : { 1, 2, 5 })
            for (const int scale : { 75, 100, 200 })
            {
                const auto shot = shoot ({ theme, mode, scale });
                const int w = 1100 * scale / 100, h = 680 * scale / 100;
                const double lum = meanLuminance (shot.image);
                // the note lines in the note colour (a selected one in its own colour)
                const int lines = countNear (shot.image, shot.canvas, P[Tok::noteLine], 10) + countNear (shot.image, shot.canvas, P[Tok::noteLineSel], 10);
                const int wantLines = 400 * scale * scale / 10000;
                check (shot.image.getWidth() == w && shot.image.getHeight() == h && (dark ? lum < 0.25 : lum > 0.75)
                           && shot.visibleNotes >= 8 && lines > wantLines,
                       fmt ("%s %s %d %%: %d x %d, mean luminance %.3f, %d notes in view, %d note-line pixels (want > %d)",
                            themeName (theme), modeName (mode), scale, shot.image.getWidth(), shot.image.getHeight(), lum,
                            shot.visibleNotes, lines, wantLines));
            }
        for (const int scale : { 100 })
        {
            const auto narrow = shoot ({ theme, 1, scale, 900, 560 });
            const int lines = countNear (narrow.image, narrow.canvas, P[Tok::noteLine], 10);
            check (narrow.image.getWidth() == 900 && narrow.image.getHeight() == 560 && narrow.canvas.getHeight() >= 150 && lines > 200,
                   fmt ("%s 900 x 560: the roll's canvas is %d x %d and shows %d notes (%d note-line pixels)", themeName (theme),
                        narrow.canvas.getWidth(), narrow.canvas.getHeight(), narrow.visibleNotes, lines));
        }
    }
}

SL_TEST ("ui", rollSnapshotsShowTheMode)
{
    section ("Roll snapshots: the lane in front is the mode's; Pitch shows the slide diamonds; the playhead while Cubase plays");

    for (const auto theme : { ThemeId::dark, ThemeId::light })
    {
        const auto& P = ui::palette (theme);
        const auto build = shoot ({ theme, 1 });
        const auto tone = shoot ({ theme, 5 });
        const auto pitch = shoot ({ theme, 2 });

        // Tone's line at full strength over the selected chord; in Build every ribbon is faint
        const int toneInTone = countNear (tone.image, tone.canvas, P[Tok::laneTone], 12);
        const int toneInBuild = countNear (build.image, build.canvas, P[Tok::laneTone], 12);
        check (toneInTone > 150 && toneInBuild * 5 < toneInTone,
               fmt ("%s: the Tone colour at full strength: %d pixels in Tone mode, %d in Build", themeName (theme), toneInTone, toneInBuild));

        // the lead note's slide diamonds: the start one filled with the link colour, the middle one ringed with it
        int linkPixels = 0;
        for (const auto& c : pitch.slideDiamonds)
            linkPixels += countNear (pitch.image, juce::Rectangle<int> (c.x - 7, c.y - 7, 15, 15), P[Tok::link], 16);
        check (pitch.slideDiamonds.size() == 2 && linkPixels > 40,
               fmt ("%s Pitch: the primary note's 2 slide diamonds (%d) are drawn in the link colour (%d pixels)", themeName (theme),
                    (int) pitch.slideDiamonds.size(), linkPixels));
        const int linkInBuild = countNear (build.image, build.canvas, P[Tok::link], 16);
        note (fmt ("%s Build, nothing selected: %d link-colour pixels (the dashed slide link and its chain mark only)", themeName (theme), linkInBuild));

        // Cubase playing at beat 5.5: the playhead, a 2 px line down the roll in the playhead colour
        const auto playing = shoot ({ theme, 1, 100, 0, 0, true });
        const auto column = juce::Rectangle<int> (playing.playheadX - 2, playing.canvas.getY(), 5, playing.canvas.getHeight());
        const int playheadPixels = countNear (playing.image, column, P[Tok::playhead], 10);
        check (playing.playheadX > playing.canvas.getX() && playheadPixels > playing.canvas.getHeight(),
               fmt ("%s: while Cubase plays the playhead is at x %d with %d pixels of %s down the roll (%d px tall)", themeName (theme),
                    playing.playheadX, playheadPixels, P[Tok::playhead].toDisplayString (false).toRawUTF8(), playing.canvas.getHeight()));
        const int none = countNear (build.image, column, P[Tok::playhead], 10);
        check (none < 10, fmt ("%s: stopped, no playhead there (%d pixels)", themeName (theme), none));

        // a box dragged on empty space: its dashed outline in the box colour
        const auto boxed = shoot ({ theme, 1, 100, 0, 0, false, false, true });
        const int outline = countNear (boxed.image, boxed.box.expanded (2), P[Tok::boxLine], 24) - countNear (boxed.image, boxed.box.reduced (3), P[Tok::boxLine], 24);
        check (boxed.box.getWidth() > 100 && outline > 100,
               fmt ("%s: the box being dragged (%d x %d) has its dashed outline in the box colour (%d pixels)", themeName (theme),
                    boxed.box.getWidth(), boxed.box.getHeight(), outline));
    }
}

//==================================================================================================
// The pictures the plan's table asks for (PROMPT 14.10), then the ones for review.

SL_SNAPSHOT (roll_build_dark_75)    { return shoot ({ ThemeId::dark, 1, 75 }).image; }
SL_SNAPSHOT (roll_build_dark_100)   { return shoot ({ ThemeId::dark, 1, 100 }).image; }
SL_SNAPSHOT (roll_build_dark_200)   { return shoot ({ ThemeId::dark, 1, 200 }).image; }
SL_SNAPSHOT (roll_build_light_75)   { return shoot ({ ThemeId::light, 1, 75 }).image; }
SL_SNAPSHOT (roll_build_light_100)  { return shoot ({ ThemeId::light, 1, 100 }).image; }
SL_SNAPSHOT (roll_build_light_200)  { return shoot ({ ThemeId::light, 1, 200 }).image; }
SL_SNAPSHOT (roll_pitch_dark_75)    { return shoot ({ ThemeId::dark, 2, 75 }).image; }
SL_SNAPSHOT (roll_pitch_dark_100)   { return shoot ({ ThemeId::dark, 2, 100 }).image; }
SL_SNAPSHOT (roll_pitch_dark_200)   { return shoot ({ ThemeId::dark, 2, 200 }).image; }
SL_SNAPSHOT (roll_pitch_light_75)   { return shoot ({ ThemeId::light, 2, 75 }).image; }
SL_SNAPSHOT (roll_pitch_light_100)  { return shoot ({ ThemeId::light, 2, 100 }).image; }
SL_SNAPSHOT (roll_pitch_light_200)  { return shoot ({ ThemeId::light, 2, 200 }).image; }
SL_SNAPSHOT (roll_tone_dark_75)     { return shoot ({ ThemeId::dark, 5, 75 }).image; }
SL_SNAPSHOT (roll_tone_dark_100)    { return shoot ({ ThemeId::dark, 5, 100 }).image; }
SL_SNAPSHOT (roll_tone_dark_200)    { return shoot ({ ThemeId::dark, 5, 200 }).image; }
SL_SNAPSHOT (roll_tone_light_75)    { return shoot ({ ThemeId::light, 5, 75 }).image; }
SL_SNAPSHOT (roll_tone_light_100)   { return shoot ({ ThemeId::light, 5, 100 }).image; }
SL_SNAPSHOT (roll_tone_light_200)   { return shoot ({ ThemeId::light, 5, 200 }).image; }
SL_SNAPSHOT (roll_narrow_dark_900x560)  { return shoot ({ ThemeId::dark, 1, 100, 900, 560 }).image; }
SL_SNAPSHOT (roll_narrow_light_900x560) { return shoot ({ ThemeId::light, 1, 100, 900, 560 }).image; }

SL_SNAPSHOT (roll_playing_dark)          { return shoot ({ ThemeId::dark, 1, 100, 0, 0, true }).image; }
SL_SNAPSHOT (roll_playing_light)         { return shoot ({ ThemeId::light, 1, 100, 0, 0, true }).image; }
SL_SNAPSHOT (roll_drag_readout_dark)     { return shoot ({ ThemeId::dark, 1, 100, 0, 0, false, true }).image; }
SL_SNAPSHOT (roll_box_ghost_light)       { return shoot ({ ThemeId::light, 1, 100, 0, 0, false, false, true }).image; }
SL_SNAPSHOT (roll_sample_travel_dark)    { return shoot ({ ThemeId::dark, 8, 100 }).image; }
SL_SNAPSHOT (roll_sample_travel_light)   { return shoot ({ ThemeId::light, 8, 100 }).image; }
