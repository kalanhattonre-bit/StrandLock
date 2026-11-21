// M7 "part load" (PLUGIN-PLAN 4 M7, 3 "The loaded range", question 9.3): a file longer than 10 minutes loads a part.
//   - partOfLongFileLoads: a 12-minute file made at test time loads its first 10 minutes by itself; the part picked
//     from minute 6 loads those frames (6 minutes, to the end of the file) as 16-bit samples equal to the file's, one
//     undo step; SMPL's loaded range round-trips (its record, and a project saved and reloaded: the same frames, the
//     same Region and part chips, the analysis cache read back instead of analysed again); Undo brings the first part
//     back; a part from minute 1 is 10 minutes long.
//   - partPickerLoadsThePart: in the editor the picker opens by itself after such a file loads, draws the whole file
//     once the worker has read it, and its "Load this part" loads the box's frames.

#include "SongRig.h"

#include "PluginEditor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"

#include <cmath>
#include <cstring>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kRate = 8000.0;
    constexpr std::int64_t kFrames = 12 * 60 * 8000;   // 12 minutes

    bool writeTwelveMinutes (const juce::File& file)
    {
        songs::Noise noise (12);
        return songs::writeWav16 (file, 1, kRate, kFrames, [&noise] (std::int64_t start, int count, float* const* out)
        {
            for (int i = 0; i < count; ++i)
            {
                const double t = (double) (start + i) / kRate;
                const double level = 0.15 + 0.25 * (t / 720.0);   // louder along the file, so every part is its own
                out[0][i] = (float) (level * std::sin (2.0 * 3.141592653589793 * 110.0 * t) + 0.03 * noise.next());
            }
        });
    }

    /** count frames from the sound's frame `from` equal the file's frames from fileFrom (JUCE's reader). */
    bool framesAre (const engine::SampleData* s, std::int64_t from, const juce::File& file, std::int64_t fileFrom, int count)
    {
        if (s == nullptr || s->format != engine::SampleFormat::i16 || from + count > s->n)
            return false;
        const auto want = songs::readFrames16 (file, fileFrom, count, 0);
        return (int) want.size() == count && std::memcmp (want.data(), s->L16 + from, (std::size_t) count * sizeof (std::int16_t)) == 0;
    }

    const engine::SampleData* playedBy (StrandLockProcessor& p)
    {
        // the audio thread's sample: one block hands it over
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        return p.getAudioSample();
    }
}

SL_TEST ("songs", partOfLongFileLoads)
{
    section ("Part load (M7): a 12-minute file loads its first 10 minutes, the part picked from minute 6 loads, and SMPL's range round-trips");

    const auto dir = songs::tempFolder ("part-load");
    const auto file = dir.getChildFile ("twelve minutes.wav");
    check (writeTwelveMinutes (file), fmt ("a 12-minute mono 8 kHz 16-bit WAV written at test time (%lld bytes)", (long long) file.getSize()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.prepareToPlay (48000.0, 64);

    // By itself: its first 10 minutes, and the toast says so.
    p.loadSoundFile (file);
    p.waitForSounds (60000);
    {
        const auto ref = p.getSampleRef();
        const auto view = p.getCurrentSound();
        bool said = false;
        for (const auto& r : p.takeSoundReports())
            said = said || r.message.find ("first 10 minutes") != std::string::npos;
        check (ref.file.fileFrames == (std::uint64_t) kFrames && ref.file.loadedStartFrame == 0 && ref.file.loadedFrames == 4800000u && view != nullptr
                   && view->cut && view->format == engine::SampleFormat::i16 && said,
               fmt ("by itself it loads its first 10 minutes (%llu of %llu frames, 16-bit) and the toast says so",
                    (unsigned long long) ref.file.loadedFrames, (unsigned long long) ref.file.fileFrames));
    }
    const auto stepsBefore = p.historySize();

    // The part from minute 6: those frames, to the end of the file (6 minutes), as one undo step.
    const std::uint64_t from = 6 * 60 * 8000;
    p.loadSoundFilePart (file, from);
    p.waitForSounds (60000);
    const auto ref = p.getSampleRef();
    const auto* played = playedBy (p);
    check (ref.file.loadedStartFrame == from && ref.file.loadedFrames == (std::uint64_t) kFrames - from && ref.file.fileFrames == (std::uint64_t) kFrames
               && played != nullptr && played->n == kFrames - (std::int64_t) from,
           fmt ("the part from minute 6 loads: SMPL's range %llu + %llu frames, the sound %lld frames", (unsigned long long) ref.file.loadedStartFrame,
                (unsigned long long) ref.file.loadedFrames, played != nullptr ? (long long) played->n : -1LL));
    check (framesAre (played, 0, file, (std::int64_t) from, 4096) && framesAre (played, 1000000, file, (std::int64_t) from + 1000000, 4096)
               && framesAre (played, played != nullptr ? played->n - 4096 : 0, file, kFrames - 4096, 4096),
           "its 16-bit samples are the file's frames from minute 6 (the start, the middle, the end against JUCE's reader)");
    check (p.historySize() == stepsBefore + 1 && p.undoLabel().value_or ("") == "Load twelve minutes.wav (from 6:00)",
           fmt ("one undo step: \"%s\"", p.undoLabel().value_or ("").c_str()));

    // SMPL's record on its own, and the whole project.
    {
        state::SampleRef back;
        const auto bytes = state::encodeSmpl (ref);
        check (state::decodeSmpl (bytes.data(), bytes.size(), back) && back.file.loadedStartFrame == ref.file.loadedStartFrame
                   && back.file.loadedFrames == ref.file.loadedFrames && back.file.fileFrames == ref.file.fileFrames && state::encodeSmpl (back) == bytes,
               "SMPL's loaded range round-trips through its record");
    }
    const auto saved = songs::saveOf (p);
    const auto pRegion = p.currentRegion();
    const auto pParts = p.getSuggestions().analysis.parts;
    StrandLockProcessor q;
    q.waitForSounds();
    const auto analysedBefore = q.getSampleManager().analysesRun();
    songs::loadInto (q, saved);
    q.waitForSounds (60000);
    q.prepareToPlay (48000.0, 64);
    const auto* qPlayed = playedBy (q);
    const auto qRef = q.getSampleRef();
    const bool sameFrames = played != nullptr && qPlayed != nullptr && qPlayed->n == played->n
                         && std::memcmp (qPlayed->L16, played->L16, (std::size_t) played->n * sizeof (std::int16_t)) == 0;
    check (qRef.file.loadedStartFrame == from && qRef.file.loadedFrames == ref.file.loadedFrames && sameFrames && songs::saveOf (q) == saved,
           "a reload decodes the same part from the path: the same range, every frame the same, and it re-saves byte-identically");
    const auto qRegion = q.currentRegion();
    const auto& qParts = q.getSuggestions().analysis.parts;
    bool partsSame = qParts.size() == pParts.size() && ! pParts.empty();
    for (std::size_t i = 0; partsSame && i < pParts.size(); ++i)
        partsSame = qParts[i].label == pParts[i].label && std::memcmp (&qParts[i].start, &pParts[i].start, sizeof (double)) == 0
                 && std::memcmp (&qParts[i].end, &pParts[i].end, sizeof (double)) == 0;
    check (std::memcmp (&qRegion.first, &pRegion.first, sizeof (double)) == 0 && std::memcmp (&qRegion.second, &pRegion.second, sizeof (double)) == 0
               && partsSame && q.getSampleManager().analysesRun() == analysedBefore,
           fmt ("the Region (%.9f .. %.9f) and the %d part chips on the same frames, read from the cache (no new analysis)", qRegion.first, qRegion.second,
                (int) qParts.size()));

    // Undo: the first 10 minutes again.
    p.undo();
    p.waitForSounds (60000);
    const auto undone = p.getSampleRef();
    const auto* first = playedBy (p);
    check (undone.file.loadedStartFrame == 0 && undone.file.loadedFrames == 4800000u && first != nullptr && first->n == 4800000
               && framesAre (first, 0, file, 0, 4096),
           "Undo brings the first 10 minutes back");

    // A part from minute 1 is 10 minutes long (the limit), not to the end.
    p.loadSoundFilePart (file, 60 * 8000);
    p.waitForSounds (60000);
    const auto ten = p.getSampleRef();
    check (ten.file.loadedStartFrame == 480000u && ten.file.loadedFrames == 4800000u && framesAre (playedBy (p), 0, file, 480000, 4096),
           fmt ("a part from minute 1 is 10 minutes (%llu frames)", (unsigned long long) ten.file.loadedFrames));
    p.releaseResources();
    q.releaseResources();
    dir.deleteRecursively();
}

SL_TEST ("songs", partPickerLoadsThePart)
{
    section ("The part picker (M7): it opens after a file longer than 10 minutes loads, draws the whole file, and loads the box's part");

    const auto dir = songs::tempFolder ("part-picker");
    const auto file = dir.getChildFile ("picker song.wav");
    check (writeTwelveMinutes (file), "a 12-minute WAV written at test time");

    StrandLockProcessor p;
    p.waitForSounds();
    p.getUserSettings().setTheme (state::Theme::dark);
    p.getUserSettings().setUiScalePercent (100);
    StrandLockEditor editor (p);
    auto& strip = editor.getView().getStrip();
    strip.loadFile (file);
    p.waitForSounds (60000);
    editor.refreshNow();
    auto& picker = strip.getPicker();
    check (picker.isOpen() && picker.startFrame() == 0 && picker.partFrames() == 4800000u,
           fmt ("the picker opened by itself on the part loaded (%s)", picker.timesText().toRawUTF8()));
    check (strip.getChips().chip ("Load another part...") != nullptr, "the chips offer \"Load another part...\" too");

    p.waitForAnalysis (60000);   // the worker reads the whole file for the overview
    editor.refreshNow();
    check (picker.hasOverview() && p.getOverview() != nullptr && p.getOverview()->fileFrames == (std::uint64_t) kFrames,
           "it draws the whole file once the worker has read it");

    picker.setStartFrame (6 * 60 * 8000);
    check (picker.timesText() == "6:00 to 12:00 of 12:00" && picker.partFrames() == 2880000u,
           fmt ("the box moved to minute 6: \"%s\" (to the end of the file)", picker.timesText().toRawUTF8()));
    picker.getLoadButton().onClick();   // the button's click, at once (no message loop runs here)
    p.waitForSounds (60000);
    editor.refreshNow();
    const auto ref = p.getSampleRef();
    check (! picker.isOpen() && ref.file.loadedStartFrame == 2880000u && ref.file.loadedFrames == 2880000u,
           fmt ("\"Load this part\" loaded it (%llu + %llu frames) and the picker closed; it does not open again for a part of the same file",
                (unsigned long long) ref.file.loadedStartFrame, (unsigned long long) ref.file.loadedFrames));
    check (strip.headerText().contains ("6:00 to 12:00 of 12:00"), fmt ("the file line says which part: \"%s\"", strip.headerText().toRawUTF8()));
    dir.deleteRecursively();
}
