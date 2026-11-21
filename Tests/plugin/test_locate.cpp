// M7 V3 "locate order" (PLUGIN-PLAN 4 M7's locate row; 3 "Finding the sound on load" and the state tests' part-loaded
// 12-minute file; 7.9 decision 5). Every audio file is made at test time (none is committed).
//   - partLoadedFileReloadsEverywhere (songs): a 12-minute file loaded from minute 6 reloads the same part from its path,
//     from the library (the file moved, the library on) and from the bytes kept in the project (the library off): every
//     frame, the Region and the part chips the same, and the project re-saves byte-identically each time; with neither
//     copy the file is missing (the control).
//   - locateOrder (songs): with the original moved, the library (when on) comes before the bytes kept in the project,
//     those before the Locate banner, which keeps every setting until a file is chosen; Locate is one undo step and
//     plays the file with every setting as it was.
//   - libraryCopiesByHash (songs): off unless turned on; on, a file is copied on the worker as <SHA-256 prefix>.<ext>,
//     once, and the library's size is measured; a file is found there only while the library is on.
//   - locateAnotherFile (songs): Locate given another file plays it with every setting kept, says it may differ, and
//     asks about it as a new file.

#include "SongRig.h"

#include "PluginEditor.h"
#include "doc/Sha256.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/tabs/SoundTab.h"

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using state::Bytes;
    using Notice = StrandLockProcessor::SoundNotice::Kind;

    Bytes bytesOf (const juce::MemoryBlock& m)
    {
        const auto* d = static_cast<const std::uint8_t*> (m.getData());
        return Bytes (d, d + m.getSize());
    }

    Bytes fileBytes (const juce::File& f)
    {
        juce::MemoryBlock m;
        f.loadFileAsData (m);
        return bytesOf (m);
    }

    Bytes saveOf (StrandLockProcessor& p) { return bytesOf (songs::saveOf (p)); }

    void loadInto (StrandLockProcessor& p, const Bytes& b)
    {
        auto exact = std::make_unique<std::uint8_t[]> (b.size());
        std::memcpy (exact.get(), b.data(), b.size());
        p.setStateInformation (exact.get(), (int) b.size());
    }

    std::optional<Bytes> saudOf (const Bytes& b)
    {
        for (const auto& c : state::parseContainer (b.data(), b.size()).chunks)
            if (c.tag == state::kTagSaud && c.payload != nullptr)
                return Bytes (c.payload, c.payload + c.payloadLength);
        return std::nullopt;
    }

    /** The container without its SAUD chunk (a project that kept nothing of its sound). */
    Bytes withoutSaud (const Bytes& b)
    {
        Bytes chunks;
        for (const auto& c : state::parseContainer (b.data(), b.size()).chunks)
            if (c.tag != state::kTagSaud)
                state::appendChunk (chunks, c.tag, c.version, c.flags, Bytes (c.payload, c.payload + c.payloadLength));
        return state::makeContainer (chunks);
    }

    bool sameBits (double a, double b) noexcept { return std::memcmp (&a, &b, sizeof a) == 0; }

    bool sameParts (const std::vector<analysis::Part>& a, const std::vector<analysis::Part>& b)
    {
        if (a.size() != b.size() || a.empty())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].label != b[i].label || a[i].kind != b[i].kind || ! sameBits (a[i].start, b[i].start) || ! sameBits (a[i].end, b[i].end))
                return false;
        return true;
    }

    bool sameSamples (const doc::SoundView& a, const doc::SoundView& b)
    {
        if (a.frames != b.frames || a.format != b.format || a.stereo != b.stereo || a.frames <= 0)
            return false;
        const auto n = (std::size_t) a.frames;
        if (a.format == engine::SampleFormat::i16)
            return std::memcmp (a.left16, b.left16, n * 2) == 0 && std::memcmp (a.right16, b.right16, n * 2) == 0;
        return std::memcmp (a.left, b.left, n * 4) == 0 && std::memcmp (a.right, b.right, n * 4) == 0;
    }

    bool writeTone (const juce::File& file, double hz, double rate, std::int64_t frames)
    {
        return songs::writeWav16 (file, 1, rate, frames, [hz, rate] (std::int64_t start, int count, float* const* o)
        {
            for (int i = 0; i < count; ++i)
                o[0][i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) (start + i) / rate);
        });
    }

    /** test_part_load's 12-minute file (8 kHz mono 16-bit, 11.5 MB: kept whole without asking). */
    bool writeTwelveMinutes (const juce::File& file)
    {
        songs::Noise noise (12);
        return songs::writeWav16 (file, 1, 8000.0, 12 * 60 * 8000, [&noise] (std::int64_t start, int count, float* const* out)
        {
            for (int i = 0; i < count; ++i)
            {
                const double t = (double) (start + i) / 8000.0;
                const double level = 0.15 + 0.25 * (t / 720.0);
                out[0][i] = (float) (level * std::sin (2.0 * 3.141592653589793 * 110.0 * t) + 0.03 * noise.next());
            }
        });
    }

    /** Every parameter's plain value and the INST state's exact Region edges: what "every setting" holds here. */
    struct Settings
    {
        std::array<float, params::kNumParams> values {};
        double startExact = 0.0, endExact = 0.0;

        static Settings of (StrandLockProcessor& p)
        {
            Settings s;
            for (int i = 0; i < params::kNumParams; ++i)
                s.values[(std::size_t) i] = params::plainValueOf (p.getParam (i));
            const auto inst = p.getInstState();
            s.startExact = inst.regionStartExact;
            s.endExact = inst.regionEndExact;
            return s;
        }

        bool operator== (const Settings& o) const
        {
            return std::memcmp (values.data(), o.values.data(), sizeof (float) * values.size()) == 0 && sameBits (startExact, o.startExact)
                && sameBits (endExact, o.endExact);
        }
    };
}

//==================================================================================================
SL_TEST ("songs", partLoadedFileReloadsEverywhere)
{
    section ("A part-loaded 12-minute file (from minute 6) reloads the same part from its path, the library and the bytes kept in the project");

    const auto dir = songs::tempFolder ("locate-part");
    const auto lib = dir.getChildFile ("library");
    const auto file = dir.getChildFile ("twelve minutes.wav");
    check (writeTwelveMinutes (file), fmt ("a 12-minute mono 8 kHz 16-bit WAV written at test time (%lld bytes)", (long long) file.getSize()));
    const std::uint64_t from = 6 * 60 * 8000;

    StrandLockProcessor p;
    p.getSampleManager().setLibraryFolder (lib);
    p.waitForSounds();
    p.setLibraryOn (true);
    p.loadSoundFilePart (file, from);
    p.waitForSounds (60000);
    const auto parts = p.getSuggestions().analysis.parts;
    if (! parts.empty())
        p.usePart (parts.front());   // a part chip sets the Region
    p.waitForEmbed (60000);
    p.waitForAnalysis (60000);        // the library copy is a job on the worker
    const auto saved = saveOf (p);
    const auto pView = p.getCurrentSound();
    const auto pRegion = p.currentRegion();
    const auto pParts = p.getSuggestions().analysis.parts;
    const auto ref = p.getSampleRef();
    {
        const auto s = saudOf (saved);
        state::SaudView v;
        const auto want = fileBytes (file);
        check (s.has_value() && state::decodeSaud (s->data(), s->size(), v) && v.header.encoding == state::kSaudOriginal
                   && v.header.byteLength == want.size() && std::memcmp (v.bytes, want.data(), want.size()) == 0
                   && v.header.excerptStartFrame == from && v.header.excerptFrames == from,
               "the project keeps the whole file (11.5 MB, no question), its header naming the part loaded: minute 6, 6 minutes");
        const auto copy = p.getSampleManager().libraryFileFor (ref.file);
        check (copy.existsAsFile() && fileBytes (copy) == want && copy.getParentDirectory() == lib,
               fmt ("the library holds a copy, %s", copy.getFileName().toRawUTF8()));
    }
    check (pView != nullptr && ref.file.loadedStartFrame == from && ref.file.loadedFrames == from && ! pParts.empty(),
           fmt ("the part from minute 6 is loaded (%llu + %llu frames), the Region on a part chip (%d chips)", (unsigned long long) ref.file.loadedStartFrame,
                (unsigned long long) ref.file.loadedFrames, (int) pParts.size()));

    // Each reload: the same frames, the same Region and chips, read from the cache; the project re-saves byte-identically.
    auto reload = [&] (const char* where, const Bytes& state, bool libraryOn, doc::SoundSource want)
    {
        StrandLockProcessor q;
        q.getSampleManager().setLibraryFolder (lib);
        q.waitForSounds();
        q.setLibraryOn (libraryOn);
        const auto analysed = q.getSampleManager().analysesRun();
        loadInto (q, state);
        q.waitForSounds (60000);
        const auto v = q.getCurrentSound();
        const auto r = q.currentRegion();
        const auto qRef = q.getSampleRef();
        const bool frames = v != nullptr && pView != nullptr && v->source == want && ! v->missing && sameSamples (*v, *pView);
        const bool region = sameBits (r.first, pRegion.first) && sameBits (r.second, pRegion.second);
        const bool chips = sameParts (q.getSuggestions().analysis.parts, pParts) && q.getSampleManager().analysesRun() == analysed;
        const bool range = qRef.file.loadedStartFrame == from && qRef.file.loadedFrames == from;
        check (frames && region && chips && range && saveOf (q) == state,
               fmt ("%s: every frame of the part the same %s, the Region the same bits %s, the %d chips the same (from the cache) %s, the range %s, "
                    "re-saved byte-identically", where, frames ? "yes" : "NO", region ? "yes" : "NO", (int) pParts.size(), chips ? "yes" : "NO",
                    range ? "yes" : "NO"));
        return v != nullptr && v->missing;
    };

    reload ("from its path", saved, false, doc::SoundSource::file);
    const auto movedDir = dir.getChildFile ("moved");
    movedDir.createDirectory();
    check (file.moveFileTo (movedDir.getChildFile (file.getFileName())), "the file is moved away");
    reload ("from the library (on), the file moved", saved, true, doc::SoundSource::library);
    reload ("from the bytes kept in the project (the library off)", saved, false, doc::SoundSource::embedded);

    // A no-op is not a pass: with the library off and nothing kept, the moved file is missing.
    {
        StrandLockProcessor q;
        q.getSampleManager().setLibraryFolder (lib);
        q.waitForSounds();
        q.setLibraryOn (false);
        loadInto (q, withoutSaud (saved));
        q.waitForSounds (60000);
        check (q.getCurrentSound() != nullptr && q.getCurrentSound()->missing && q.getSoundNotice().kind == Notice::locate,
               "negative control: with neither copy it is missing, and the Locate banner shows");
    }
    p.setLibraryOn (false);
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", locateOrder)
{
    section ("Locate order (plan 3): the library by its SHA-256, then the bytes kept in the project, then the Locate banner with every setting kept");

    const auto dir = songs::tempFolder ("locate-order");
    const auto lib = dir.getChildFile ("library");
    const auto file = dir.getChildFile ("a tone.wav");
    check (writeTone (file, 110.0, 44100.0, 44100), "a 1 s tone written at test time");

    StrandLockProcessor p;
    p.getSampleManager().setLibraryFolder (lib);
    p.waitForSounds();
    p.setLibraryOn (true);
    p.loadSoundFile (file);
    p.waitForSounds (30000);
    songs::setPlain (p, params::P::rootNote, 45.0f);
    songs::setPlain (p, params::P::rootCents, 12.0f);
    songs::setPlain (p, params::P::drift, 0.4f);
    p.setRegionStep (0.2, 0.6, "Region");
    p.waitForEmbed();
    p.waitForAnalysis();
    const auto withCopy = saveOf (p);
    const auto settings = Settings::of (p);
    p.setKeepInProject (false);
    p.waitForEmbed();
    const auto bare = saveOf (p);
    p.setLibraryOn (false);
    check (saudOf (withCopy).has_value() && ! saudOf (bare).has_value(), "two projects: one keeps the file, one (the switch off) keeps nothing");

    const auto movedDir = dir.getChildFile ("moved");
    movedDir.createDirectory();
    const auto moved = movedDir.getChildFile ("a tone.wav");
    check (file.moveFileTo (moved), "the file is moved away");

    auto open = [&] (const Bytes& state, bool libraryOn)
    {
        auto q = std::make_unique<StrandLockProcessor>();
        q->getSampleManager().setLibraryFolder (lib);
        q->waitForSounds();
        q->setLibraryOn (libraryOn);
        loadInto (*q, state);
        q->waitForSounds (30000);
        return q;
    };

    // 1. The library comes first, by its SHA-256 (even when the project kept a copy too).
    {
        auto q = open (withCopy, true);
        const auto v = q->getCurrentSound();
        check (v != nullptr && v->source == doc::SoundSource::library && ! v->missing && Settings::of (*q) == settings && saveOf (*q) == withCopy,
               "the library on: the copy in the library plays (before the one kept in the project), every setting as saved, re-saved byte-identically");
        q->setLibraryOn (false);
    }
    // 2. Then the bytes kept in the project.
    {
        auto q = open (withCopy, false);
        const auto v = q->getCurrentSound();
        check (v != nullptr && v->source == doc::SoundSource::embedded && ! v->missing && Settings::of (*q) == settings && saveOf (*q) == withCopy,
               "the library off: the bytes kept in the project play, every setting as saved");
    }
    // 3. Then the Locate banner, which keeps every setting until a file is chosen.
    auto r = open (bare, false);
    {
        const auto v = r->getCurrentSound();
        const auto notice = r->getSoundNotice();
        check (v != nullptr && v->missing && v->builtIn && notice.kind == Notice::locate && notice.text.find ("a tone.wav") != std::string::npos,
               fmt ("nothing to find: the default sound plays and the banner says \"%s\"", notice.text.c_str()));
        check (Settings::of (*r) == settings && saveOf (*r) == bare && r->getSampleRef().file.path == file.getFullPathName().toStdString(),
               "every setting is kept (the Region and its exact edges, the root, the cents, Drift; the SMPL record): the project re-saves byte-identically");
    }
    // A no-op is not a pass: the library holds the file, but is off, so it is not looked in.
    {
        auto q = open (bare, false);
        check (q->getCurrentSound() != nullptr && q->getCurrentSound()->missing && p.getSampleManager().libraryFileFor (p.getSampleRef().file).existsAsFile(),
               "negative control: the library holds the file but is off: not looked in");
    }

    // Locate: the banner's file brings the sound back, with every setting as it was, as one undo step.
    {
        StrandLockEditor editor (*r);
        editor.refreshNow();
        auto& banner = editor.getView().getSoundBanner();
        check (banner.isVisible() && banner.isLocate() && banner.getLocateButton().isVisible() && editor.getView().getRollArea().getHeight() > 0,
               "the editor shows the banner with Locate sample...");
        const auto steps = r->historySize();
        banner.locate (moved);
        r->waitForSounds (30000);
        editor.refreshNow();
        const auto v = r->getCurrentSound();
        check (v != nullptr && v->source == doc::SoundSource::file && ! v->missing && r->getSampleRef().file.path == moved.getFullPathName().toStdString()
                   && Settings::of (*r) == settings,
               "Locate sample... with the moved file: it plays from there, every setting as it was");
        check (r->historySize() == steps + 1 && r->undoLabel().value_or ("") == "Locate a tone.wav" && ! banner.isVisible(),
               fmt ("one undo step (\"%s\"), and the banner goes", r->undoLabel().value_or ("").c_str()));
        r->undo();
        r->waitForSounds (30000);
        editor.refreshNow();
        check (r->getCurrentSound() != nullptr && r->getCurrentSound()->missing && banner.isVisible() && Settings::of (*r) == settings,
               "Undo goes back to the missing file and its banner, the settings untouched");
    }
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", libraryCopiesByHash)
{
    section ("The library (plan 3): off unless turned on; on, each file is copied once by its SHA-256 on the worker, and its size shown");

    const auto dir = songs::tempFolder ("library");
    const auto lib = dir.getChildFile ("Samples");
    const auto a = dir.getChildFile ("first.wav");
    const auto b = dir.getChildFile ("second.wav");
    check (writeTone (a, 220.0, 44100.0, 22050) && writeTone (b, 330.0, 44100.0, 33075), "two short WAVs written at test time");

    StrandLockProcessor p;
    p.getSampleManager().setLibraryFolder (lib);
    p.waitForSounds();
    check (! p.isLibraryOn(), "the library is off unless the person turns it on");
    p.loadSoundFile (a);
    p.waitForSounds (30000);
    p.waitForAnalysis();
    check (p.getSampleManager().libraryCopies() == 0 && ! lib.exists(), "off: nothing is copied");

    p.setLibraryOn (true);
    p.waitForAnalysis();
    const auto copyA = p.getSampleManager().libraryFileFor (p.getSampleRef().file);
    const auto hex = doc::toHex (p.getSampleRef().file.sha256).substr (0, 16);
    check (copyA.existsAsFile() && fileBytes (copyA) == fileBytes (a) && copyA.getFileName().toStdString() == hex + ".wav"
               && p.getSampleManager().libraryCopies() == 1,
           fmt ("turned on: the file playing now is copied as %s (the first 16 hex digits of its SHA-256)", copyA.getFileName().toRawUTF8()));
    check (p.getLibraryBytes() == (std::int64_t) a.getSize(), fmt ("the library's size is measured: %lld bytes", (long long) p.getLibraryBytes()));

    p.loadSoundFile (a);
    p.waitForSounds (30000);
    p.waitForAnalysis();
    check (p.getSampleManager().libraryCopies() == 1, "the same file loaded again is not copied again");
    p.loadSoundFile (b);
    p.waitForSounds (30000);
    p.waitForAnalysis();
    check (p.getSampleManager().libraryCopies() == 2 && p.getLibraryBytes() == (std::int64_t) (a.getSize() + b.getSize()),
           fmt ("another file is: 2 copies, %lld bytes", (long long) p.getLibraryBytes()));
    {
        StrandLockEditor editor (p);
        editor.refreshNow();
        auto& tab = editor.getView().getSoundTab();
        check (tab.getLibraryToggle().getToggleState() && tab.getLibraryStatus().contains ("MB") && tab.getLibraryStatus().startsWith ("On"),
               fmt ("the Sound tab's library line: \"%s\"", tab.getLibraryStatus().toRawUTF8()));
    }
    p.setLibraryOn (false);
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", locateAnotherFile)
{
    section ("Locate given another file: it plays with every setting kept, the toast says it may differ, and it is asked about as a new file");

    const auto dir = songs::tempFolder ("locate-other");
    const auto a = dir.getChildFile ("old take.wav");
    const auto b = dir.getChildFile ("new take.wav");
    check (writeTone (a, 110.0, 44100.0, 44100) && writeTone (b, 220.0, 44100.0, 66150), "two WAVs written at test time");

    Bytes bare;
    {
        StrandLockProcessor p;
        p.waitForSounds();
        p.loadSoundFile (a);
        p.waitForSounds (30000);
        p.setRegionStep (0.1, 0.5, "Region");
        p.setKeepInProject (false);
        bare = saveOf (p);
    }
    a.deleteFile();

    StrandLockProcessor q;
    q.waitForSounds();
    loadInto (q, bare);
    q.waitForSounds (30000);
    const auto settings = Settings::of (q);
    q.takeSoundReports();
    check (q.getSoundNotice().kind == Notice::locate, "the old take is missing: the Locate banner");
    q.locateSound (b);
    q.waitForSounds (30000);
    bool told = false;
    for (const auto& r : q.takeSoundReports())
        told = told || r.message.find ("not the file this project was saved with") != std::string::npos;
    const auto ref = q.getSampleRef();
    const auto v = q.getCurrentSound();
    check (v != nullptr && ! v->missing && v->source == doc::SoundSource::file && ref.file.displayName == "new take.wav" && ref.file.fileSize == (std::uint64_t) b.getSize()
               && ref.file.cache.analysed == 1 && Settings::of (q) == settings && told,
           "the new take plays, its record and analysis its own, every setting as it was, and the toast says it may sound different");
    check (ref.file.keepInProject == 0 && ref.file.embedAnswer == 0 && q.getSoundNotice().kind == Notice::none,
           "the project's switch stays (off) and the new file has no answer of its own yet");
    dir.deleteRecursively();
}
