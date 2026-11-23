// The M7 fix pass's tests for keeping the sound with the project, Locate and the library (docs/NOTES-M7.md, Fix). Every
// audio file is made at test time (none is committed).
//   - excerptKeptAfterRoundTrips (songs; M7R-SST-1, M7R-SST-4): a project that keeps only a Region excerpt of its file (the
//     file gone) still keeps that excerpt after another sound and Undo, and after Keep the sound in the project off and on;
//     a short Region on the excerpt gets a root suggestion.
//   - wholeCopyKeptAsItIs (songs; M7R-SST-5, M7R-SST-1): a project that keeps the whole sound as FLAC 24-bit (its file
//     moved before Yes was answered) opens with the file still gone and keeps that copy as it is, also after another sound
//     and Undo: nothing is encoded again.
//   - locateKeepsTrailingFields (songs; M7R-SST-7): Locate of the same file keeps the project's SMPL record whole, with a
//     newer version's trailing bytes, and changes only its path.
//   - libraryCopyStops (songs; M7R-SST-3): a newer sound request stops a library copy from the file's path at its next
//     1 MB block; the .part file goes; the file is copied whole the next time it plays.
//   - unreadableBlobKeptWhileSaudArrives (state; M7R-SST-6): a blob this version cannot read is written back byte for byte
//     while the sound's SAUD is still on its way, and replaced only after an edit.

#include "SongRig.h"

#include "support/Check.h"
#include "support/TestRegistry.h"

#include "state/Crc32.h"
#include "state/StateChunks.h"
#include "state/StateFormat.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using state::Bytes;
    using Answer = StrandLockProcessor::EmbedAnswer;
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

    /** The payload of the container's first chunk with this tag, if it has one. */
    std::optional<Bytes> chunkOf (const Bytes& b, std::uint32_t tag)
    {
        for (const auto& c : state::parseContainer (b.data(), b.size()).chunks)
            if (c.tag == tag && c.payload != nullptr)
                return Bytes (c.payload, c.payload + c.payloadLength);
        return std::nullopt;
    }

    std::optional<Bytes> saudOf (const Bytes& b) { return chunkOf (b, state::kTagSaud); }

    /** The container again with each chunk's payload passed through fn, CRCs made anew. */
    Bytes rebuilt (const Bytes& b, const std::function<Bytes (std::uint32_t, const Bytes&)>& fn)
    {
        Bytes chunks;
        for (const auto& c : state::parseContainer (b.data(), b.size()).chunks)
            state::appendChunk (chunks, c.tag, c.version, c.flags, fn (c.tag, Bytes (c.payload, c.payload + c.payloadLength)));
        return state::makeContainer (chunks);
    }

    /** A mono 32-bit float WAV of frames frames at rate, made chunk by chunk by fill. */
    bool writeFloatWav (const juce::File& file, double rate, std::int64_t frames, const songs::Fill& fill)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        const auto options = juce::AudioFormatWriterOptions {}
                                 .withSampleRate (rate)
                                 .withNumChannels (1)
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;
        constexpr int chunk = 1 << 16;
        std::vector<float> buffer ((std::size_t) chunk);
        float* out[1] = { buffer.data() };
        const float* in[1] = { buffer.data() };
        for (std::int64_t done = 0; done < frames; done += chunk)
        {
            const int n = (int) std::min<std::int64_t> (chunk, frames - done);
            fill (done, n, out);
            if (! writer->writeFromFloatArrays (in, 1, n))
                return false;
        }
        writer.reset();
        return file.existsAsFile();
    }

    /** test_embed's 21 MB file, in short: 110 s of a held A1 (110 Hz, three harmonics) as mono 32-bit float at 48 kHz, so the
        project asks about it (20 to 50 MB) and keeps it as floats (3 minutes or less). */
    bool writeHeldNote (const juce::File& file)
    {
        return writeFloatWav (file, 48000.0, 110 * 48000, [] (std::int64_t start, int count, float* const* o)
        {
            for (int i = 0; i < count; ++i)
            {
                const double t = (double) (start + i) / 48000.0;
                const double w = 2.0 * 3.141592653589793 * 110.0 * t;
                o[0][i] = (float) (0.3 * std::sin (w) + 0.12 * std::sin (2.0 * w) + 0.06 * std::sin (3.0 * w));
            }
        });
    }

    bool writeTone16 (const juce::File& file, double hz, double rate, std::int64_t frames)
    {
        return songs::writeWav16 (file, 1, rate, frames, [hz, rate] (std::int64_t start, int count, float* const* o)
        {
            for (int i = 0; i < count; ++i)
                o[0][i] = 0.3f * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) (start + i) / rate);
        });
    }
}

//==================================================================================================
SL_TEST ("songs", excerptKeptAfterRoundTrips)
{
    section ("A project that keeps only a Region excerpt (its file gone) keeps it after another sound and Undo, and after Keep the sound "
             "in the project off and on (fix pass M7R-SST-1); a short Region on the excerpt gets a root suggestion (M7R-SST-4)");

    const auto dir = songs::tempFolder ("fix-excerpt");
    const auto file = dir.getChildFile ("held note.wav");
    const bool written = writeHeldNote (file);   // before the check: its message is made before its condition on some compilers
    check (written && file.getSize() > 20000000 && file.getSize() < 50000000,
           fmt ("a 110 s mono float WAV written at test time (%lld bytes)", (long long) file.getSize()));

    Bytes project;
    {
        StrandLockProcessor p;
        p.waitForSounds();
        p.loadSoundFile (file);
        p.waitForSounds (60000);
        p.setRegionStep (40.0 / 110.0, 44.0 / 110.0, "Region");
        const bool answered = p.answerEmbedQuestion (Answer::region) && p.waitForEmbed (60000);
        project = saveOf (p);
        check (answered, "it asks (21 MB): Just the Region, the Region at 0:40 .. 0:44");
    }
    const auto kept = saudOf (project);
    state::SaudView v;
    check (kept.has_value() && state::decodeSaud (kept->data(), kept->size(), v) && v.header.encoding == state::kSaudFlac24
               && v.header.excerptFrames <= (std::uint64_t) (6 * 48000 + 2),
           fmt ("the project keeps the Region and 1 s either side as FLAC 24-bit (%llu frames)", kept.has_value() ? (unsigned long long) v.header.excerptFrames : 0ull));
    file.deleteFile();

    StrandLockProcessor q;
    q.waitForSounds();
    loadInto (q, project);
    q.waitForSounds (60000);
    {
        const auto s = q.getCurrentSound();
        check (s != nullptr && s->source == doc::SoundSource::excerpt && saudOf (saveOf (q)) == kept,
               "with the file gone the excerpt plays, and the save keeps it");
    }

    // Another sound, then Undo: the excerpt plays again (Undo finds the SAUD), and the project keeps it again.
    q.loadStarterSound (sounds::StarterId::pluck);
    q.waitForSounds (30000);
    const bool starterKeepsNothing = ! saudOf (saveOf (q)).has_value();
    const bool undone = q.undo();
    q.waitForSounds (60000);
    const auto back = q.getCurrentSound();
    const auto afterUndo = saveOf (q);
    check (starterKeepsNothing && undone && back != nullptr && back->source == doc::SoundSource::excerpt && saudOf (afterUndo) == kept,
           fmt ("a starter keeps nothing (%s); after Undo the excerpt plays (%s) and the save keeps it, byte for byte (%s); the whole project as it "
                "was loaded: %s",
                starterKeepsNothing ? "yes" : "NO", back != nullptr && back->source == doc::SoundSource::excerpt ? "yes" : "NO",
                saudOf (afterUndo) == kept ? "yes" : "NO", afterUndo == project ? "byte for byte" : "not byte for byte"));

    // Keep the sound in the project off (nothing kept), then on again (the excerpt again: it is all StrandLock has).
    q.setKeepInProject (false);
    const bool offKeepsNothing = ! saudOf (saveOf (q)).has_value();
    q.setKeepInProject (true);
    const auto afterOn = saveOf (q);
    check (offKeepsNothing && saudOf (afterOn) == kept && q.getEmbedStatus().find ("the Region") != std::string::npos,
           fmt ("the switch off keeps nothing (%s); on again, the excerpt is kept, byte for byte (%s); the Sound tab: \"%s\"", offKeepsNothing ? "yes" : "NO",
                saudOf (afterOn) == kept ? "yes" : "NO", q.getEmbedStatus().c_str()));

    // M7R-SST-4: a 0.5 s Region on the excerpt (6 s of a 110 s file) is long enough for a root suggestion. Measured on the
    // whole file's length it would be 0.03 s and no job would be asked.
    const double a = 41.0 / 110.0, b = 41.5 / 110.0;
    const auto job = q.getSampleManager().suggestRoot (a, b);
    q.setRegionStep (a, b, "Region");
    q.waitForAnalysis (30000);
    const auto& sug = q.getSuggestions();
    check (job != 0 && sug.root.has_value() && sug.root->root == 45 && q.showsRootSuggestion(),
           fmt ("a root suggestion for the 0.5 s Region on the excerpt: %s", sug.root.has_value() ? fmt ("MIDI %d %+d cents", sug.root->root, sug.root->cents).c_str()
                                                                                                    : "none"));
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", wholeCopyKeptAsItIs)
{
    section ("A project that keeps the whole sound as FLAC 24-bit (the file moved before Yes) opens with the file still gone and keeps that "
             "copy as it is, also after another sound and Undo: nothing is encoded again (fix pass M7R-SST-5)");

    const auto dir = songs::tempFolder ("fix-whole");
    const auto file = dir.getChildFile ("moved early.wav");
    const bool written = writeHeldNote (file);
    check (written && file.getSize() > 20000000, fmt ("a 110 s mono float WAV written at test time (%lld bytes)", (long long) file.getSize()));

    Bytes project;
    {
        StrandLockProcessor p;
        p.waitForSounds();
        p.loadSoundFile (file);
        p.waitForSounds (60000);
        const auto away = dir.getChildFile ("elsewhere");
        away.createDirectory();
        const bool moved = file.moveFileTo (away.getChildFile (file.getFileName()));
        const bool answered = p.answerEmbedQuestion (Answer::yes) && p.waitForEmbed (60000);
        project = saveOf (p);
        check (moved && answered, "the file moved before the question was answered; Yes");
        away.deleteRecursively();   // the file is gone
    }
    const auto kept = saudOf (project);
    state::SaudView v;
    check (kept.has_value() && state::decodeSaud (kept->data(), kept->size(), v) && v.header.encoding == state::kSaudFlac24
               && v.header.excerptStartFrame == 0 && v.header.excerptFrames == (std::uint64_t) (110 * 48000),
           "the project keeps the whole sound as it played (FLAC 24-bit of all of it: the file could not be read again)");

    StrandLockProcessor q;
    q.waitForSounds();
    loadInto (q, project);
    q.waitForSounds (60000);
    const auto atLoad = q.getEmbeddedPayload();
    const bool idle = q.waitForEmbed (60000);
    const auto afterWait = q.getEmbeddedPayload();
    const auto again = saveOf (q);
    const auto s = q.getCurrentSound();
    check (s != nullptr && s->source == doc::SoundSource::embedded && idle && atLoad != nullptr && afterWait == atLoad && again == project
               && q.getEmbedStatus().find ("as it plays") != std::string::npos,
           fmt ("with the file still gone it plays from the copy (%s); the copy kept is the project's own, not made again (%s); re-saved byte for "
                "byte (%s); the Sound tab: \"%s\"",
                s != nullptr && s->source == doc::SoundSource::embedded ? "yes" : "NO", afterWait == atLoad ? "yes" : "NO", again == project ? "yes" : "NO",
                q.getEmbedStatus().c_str()));

    // Another sound and Undo: the copy comes back with the sound, as it is (M7R-SST-1 for a whole copy).
    q.loadStarterSound (sounds::StarterId::pluck);
    q.waitForSounds (30000);
    q.undo();
    q.waitForSounds (60000);
    q.waitForEmbed (60000);
    check (q.getEmbeddedPayload() == atLoad && saudOf (saveOf (q)) == kept, "after another sound and Undo the same copy is kept, not made again");
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", locateKeepsTrailingFields)
{
    section ("Locate of the same file keeps the project's SMPL record whole, a newer version's trailing fields too, and changes only its "
             "path (fix pass M7R-SST-7, plan 3's append-only rule)");

    const auto dir = songs::tempFolder ("fix-locate");
    const auto file = dir.getChildFile ("a tone.wav");
    const bool written = writeTone16 (file, 220.0, 44100.0, 44100);
    check (written, "a 1 s tone written at test time");

    Bytes bare;
    {
        StrandLockProcessor p;
        p.waitForSounds();
        p.loadSoundFile (file);
        p.waitForSounds (30000);
        p.setRegionStep (0.2, 0.6, "Region");
        p.setKeepInProject (false);   // the project names the file and keeps nothing: Locate is the only way back
        p.waitForEmbed();
        bare = saveOf (p);
    }
    const Bytes trailing { 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58 };
    const auto project = rebuilt (bare, [&trailing] (std::uint32_t tag, const Bytes& payload)
    {
        auto b = payload;
        if (tag == state::kTagSmpl)
            b.insert (b.end(), trailing.begin(), trailing.end());
        return b;
    });
    const auto movedDir = dir.getChildFile ("moved");
    movedDir.createDirectory();
    const auto moved = movedDir.getChildFile ("a tone.wav");
    check (file.moveFileTo (moved), "the file is moved away");

    StrandLockProcessor q;
    q.waitForSounds();
    loadInto (q, project);
    q.waitForSounds (30000);
    state::SampleRef loaded;
    const auto loadedSmpl = chunkOf (project, state::kTagSmpl);
    const bool readLoaded = loadedSmpl.has_value() && state::decodeSmpl (loadedSmpl->data(), loadedSmpl->size(), loaded);
    check (readLoaded && loaded.trailing == trailing && q.getSoundNotice().kind == Notice::locate,
           "a project whose SMPL record ends with 8 bytes a newer version appended: its file is gone, the Locate banner shows");

    q.locateSound (moved);
    q.waitForSounds (30000);
    const auto saved = saveOf (q);
    const auto savedSmpl = chunkOf (saved, state::kTagSmpl);
    state::SampleRef after;
    const bool readAfter = savedSmpl.has_value() && state::decodeSmpl (savedSmpl->data(), savedSmpl->size(), after);
    const auto s = q.getCurrentSound();
    check (s != nullptr && ! s->missing && readAfter && after.trailing == trailing && after.file.path == moved.getFullPathName().toStdString()
               && after.file.sha256 == loaded.file.sha256 && after.file.cache.parts.size() == loaded.file.cache.parts.size(),
           fmt ("Locate found it at its new place; the saved record keeps the 8 trailing bytes (%s), with the new path and the same file and analysis",
                readAfter && after.trailing == trailing ? "yes" : "NO"));
    {
        auto expect = loaded;
        expect.file.path = after.file.path;
        check (readAfter && state::encodeSmpl (after) == state::encodeSmpl (expect),
               "the whole record is the project's, but for its path (byte for byte)");
    }
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", libraryCopyStops)
{
    section ("A newer sound request stops a library copy at its next 1 MB block (fix pass M7R-SST-3): the .part file goes, nothing is "
             "counted, and the file is copied whole the next time it plays");

    const auto dir = songs::tempFolder ("fix-library");
    const auto lib = dir.getChildFile ("Samples");
    const auto file = dir.getChildFile ("three megabytes.wav");
    const bool written = writeTone16 (file, 330.0, 44100.0, 34 * 44100);
    check (written && file.getSize() > 2900000, fmt ("a 34 s mono 16-bit WAV written at test time (%lld bytes: three 1 MB blocks)", (long long) file.getSize()));

    StrandLockProcessor p;
    auto& sm = p.getSampleManager();
    sm.setLibraryFolder (lib);
    p.waitForSounds();
    p.setKeepInProject (false);   // the file is not read into memory, so the copy reads it from its path
    p.setLibraryOn (true);
    p.waitForAnalysis();          // the library job for the starter playing now (nothing to copy)

    // The first block of the copy asks for another sound, as a load or an Undo would while a long file is copied.
    std::atomic<int> blocks { 0 };
    sm.libraryProbe = [&blocks, &sm]
    {
        if (blocks.fetch_add (1) == 0)
            sm.loadStarter (sounds::StarterId::pluck, 1, doc::LoadOrigin::startup);
    };
    const auto stopsBefore = sm.stopsMade();
    p.loadSoundFile (file);
    p.waitForSounds (30000);      // the file plays: its copy is asked for
    p.waitForAnalysis (30000);    // the copy job: stopped by the newer request at its first question
    p.waitForSounds (30000);      // and the starter that stopped it plays
    int filesThere = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (lib, false, "*", juce::File::findFiles))
    {
        (void) entry;
        ++filesThere;
    }
    check (blocks.load() == 1 && sm.libraryCopies() == 0 && filesThere == 0 && sm.stopsMade() > stopsBefore,
           fmt ("the copy stopped at its first block (asked %d times), nothing copied or left behind (%d files in the library, no .part), counted as "
                "a stop",
                blocks.load(), filesThere));

    // A no-op is not a pass: with nothing newer asked, the same file is copied whole.
    sm.libraryProbe = nullptr;
    p.loadSoundFile (file);
    p.waitForSounds (30000);
    p.waitForAnalysis (30000);
    const auto copy = sm.libraryFileFor (p.getSampleRef().file);
    check (sm.libraryCopies() == 1 && copy.existsAsFile() && fileBytes (copy) == fileBytes (file),
           fmt ("the next time it plays it is copied whole, as %s", copy.getFileName().toRawUTF8()));
    p.setLibraryOn (false);
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("state", unreadableBlobKeptWhileSaudArrives)
{
    section ("A blob this version cannot read is written back byte for byte, unedited, while the sound's SAUD is still on its way, and "
             "replaced after an edit (fix pass M7R-SST-6: a SAUD the worker makes by itself is not an edit)");

    const auto dir = songs::tempFolder ("fix-blob");
    const auto file = dir.getChildFile ("short tone.wav");
    const bool written = writeTone16 (file, 220.0, 22050.0, 6615);
    check (written, "a 0.3 s mono 16-bit WAV written at test time");

    // A project of a newer formatVersion: nothing in it can be read, so it is kept whole.
    Bytes blob;
    {
        StrandLockProcessor p;
        p.waitForSounds();
        p.loadSoundFile (file);
        p.waitForSounds (30000);
        p.waitForEmbed();
        blob = saveOf (p);
    }
    blob[4] = 2;
    const auto crc = state::crc32 (blob.data(), 12);
    for (int i = 0; i < 4; ++i)
        blob[12 + (std::size_t) i] = (std::uint8_t) ((crc >> (8 * i)) & 0xffu);

    // An instance playing the tone with nothing kept yet: the switch on asks the worker for the file's bytes, and the blob
    // arrives before they do.
    StrandLockProcessor q;
    q.waitForSounds();
    q.setKeepInProject (false);
    q.loadSoundFile (file);
    q.waitForSounds (30000);
    q.setKeepInProject (true);
    loadInto (q, blob);
    const auto first = saveOf (q);    // the save waits for the SAUD, which arrives now
    const auto second = saveOf (q);
    check (first == blob && second == blob && q.getStateBanner().isNotEmpty() && q.getEmbeddedPayload() != nullptr,
           fmt ("the blob is written back byte for byte (%s, %s) with its banner, while the sound's own SAUD arrived (%s)", first == blob ? "yes" : "NO",
                second == blob ? "yes" : "NO", q.getEmbeddedPayload() != nullptr ? "yes" : "NO"));

    // The control: a parameter edit replaces it with a container this version writes, SAUD and all.
    params::setPlainValue (q.getParam (params::P::pitch), params::index (params::P::pitch), 5.0f);
    const auto edited = saveOf (q);
    check (edited != blob && state::parseContainer (edited.data(), edited.size()).status == state::HeaderStatus::ok && saudOf (edited).has_value()
               && q.getStateBanner().isEmpty(),
           "negative control: after a parameter edit the project is written anew (with its SAUD), and the banner goes");
    dir.deleteRecursively();
}
