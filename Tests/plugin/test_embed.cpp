// M7 V3 "embedding" (PLUGIN-PLAN 4 M7's embedding row; 3 "Embedding", "The loaded range" and the state tests' "Embedded
// sample (M7)"; 7.9 decision 5). Every audio file is made at test time (none is committed).
//   - saudChunkRecord (state): the SAUD record round-trips, keeps a newer version's trailing bytes, reads an encoding it
//     does not know (kept, never played), and refuses one cut short; the container writes SAUD only when there is one.
//   - embeddedWavRoundTrip (state): a 2 MB TestSignals WAV is kept whole (its bytes, identical) and, with the file gone,
//     plays from the project bit for bit, and the project re-saves byte-identically; a copy with one byte changed is
//     refused (its SHA-256), and the Locate banner shows.
//   - embedForwardCompatibility (state): kept unreadable blobs and forward compatibility hold with SAUD present: an
//     encoding this version does not know, a SAUD with trailing fields, a SAUD with a bad CRC, a newer formatVersion,
//     and 2000 seeded mutations.
//   - embedAnswerSavedOnce (songs): a 21 MB file asks once; No is saved (no undo step) and survives save and load (no
//     second question); another file asks again; Yes keeps the whole file; Undo keeps each file's answer; with the switch
//     off nothing is kept and nothing is asked.
//   - embedFortyFiveMegabytes (songs, Release): a 45 MB file kept whole saves and loads with the file gone, times printed
//     (target 2 s, gate 10 s).
//   - regionExcerptWithOriginalMissing (songs): "Just the Region" keeps the Region and 1 s either side as FLAC 24-bit,
//     within 2^-23 of the original samples; with the file gone the excerpt plays, a Region set outside it plays its
//     nearest edge, and the banner says part of the sound was not saved.
//   - noExcerptOverFiftyMegabytes (songs): a Region whose excerpt would pass 50 MB is not kept and the person is told; a
//     short Region of the same file is kept.

#include "SongRig.h"

#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "state/Crc32.h"
#include "state/StateChunks.h"
#include "state/StateFormat.h"

#include <chrono>
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

    /** The chunks' tags in order ("PARM INST CLIP SMPL SAUD UIST"). */
    std::string tagsOf (const Bytes& b)
    {
        std::string s;
        for (const auto& c : state::parseContainer (b.data(), b.size()).chunks)
            s += (s.empty() ? "" : " ") + state::tagName (c.tag);
        return s;
    }

    /** The SAUD chunk's payload, if the container has one. */
    std::optional<Bytes> saudOf (const Bytes& b)
    {
        for (const auto& c : state::parseContainer (b.data(), b.size()).chunks)
            if (c.tag == state::kTagSaud && c.payload != nullptr)
                return Bytes (c.payload, c.payload + c.payloadLength);
        return std::nullopt;
    }

    /** The container again with each chunk's payload passed through fn (nothing drops the chunk), CRCs made anew. */
    Bytes rebuilt (const Bytes& b, const std::function<std::optional<Bytes> (std::uint32_t, const Bytes&)>& fn)
    {
        Bytes chunks;
        for (const auto& c : state::parseContainer (b.data(), b.size()).chunks)
        {
            const Bytes payload (c.payload, c.payload + c.payloadLength);
            if (auto next = fn (c.tag, payload))
                state::appendChunk (chunks, c.tag, c.version, c.flags, *next);
        }
        return state::makeContainer (chunks);
    }

    /** The same samples, bit for bit (both channels, either format). */
    bool sameSamples (const doc::SoundView& a, const doc::SoundView& b)
    {
        if (a.frames != b.frames || a.format != b.format || a.stereo != b.stereo || a.frames <= 0)
            return false;
        const auto n = (std::size_t) a.frames;
        if (a.format == engine::SampleFormat::i16)
            return std::memcmp (a.left16, b.left16, n * 2) == 0 && std::memcmp (a.right16, b.right16, n * 2) == 0;
        return std::memcmp (a.left, b.left, n * 4) == 0 && std::memcmp (a.right, b.right, n * 4) == 0;
    }

    /** A WAV of frames frames made chunk by chunk by fill: 16-bit PCM or 32-bit float. */
    bool writeWav (const juce::File& file, int channels, double rate, std::int64_t frames, int bits, const songs::Fill& fill)
    {
        if (bits == 16)
            return songs::writeWav16 (file, channels, rate, frames, fill);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        const auto options = juce::AudioFormatWriterOptions {}
                                 .withSampleRate (rate)
                                 .withNumChannels (channels)
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;
        constexpr int chunk = 1 << 16;
        std::vector<std::vector<float>> buffers ((std::size_t) channels, std::vector<float> ((std::size_t) chunk));
        std::vector<float*> out;
        std::vector<const float*> in;
        for (auto& b : buffers)
        {
            out.push_back (b.data());
            in.push_back (b.data());
        }
        for (std::int64_t done = 0; done < frames; done += chunk)
        {
            const int n = (int) std::min<std::int64_t> (chunk, frames - done);
            fill (done, n, out.data());
            if (! writer->writeFromFloatArrays (in.data(), channels, n))
                return false;
        }
        writer.reset();
        return file.existsAsFile();
    }

    /** A mono tone of frames frames at rate, 16-bit: a held note (a song's worth of it). */
    bool writeTone (const juce::File& file, double hz, double rate, std::int64_t frames, float amp = 0.3f)
    {
        return writeWav (file, 1, rate, frames, 16, [hz, rate, amp] (std::int64_t start, int count, float* const* o)
        {
            for (int i = 0; i < count; ++i)
                o[0][i] = amp * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) (start + i) / rate);
        });
    }

    double msSince (std::chrono::steady_clock::time_point t0)
    {
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    }

    /** count float frames of channel 0 from frame start, as JUCE's reader gives them. */
    std::vector<float> readFloats (const juce::File& file, std::int64_t start, int count)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        std::vector<float> out ((std::size_t) count, 0.0f);
        if (reader != nullptr)
        {
            float* dest[1] = { out.data() };
            reader->read (dest, 1, start, count);
        }
        return out;
    }

    /** A SAUD FLAC payload's channel 0, decoded here (independently of the plugin): each 32-bit value x 2^-31. */
    std::vector<float> flacSamples (const Bytes& saud, int count)
    {
        state::SaudView v;
        std::vector<float> out;
        if (! state::decodeSaud (saud.data(), saud.size(), v) || v.header.encoding != state::kSaudFlac24)
            return out;
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (v.bytes, (std::size_t) v.header.byteLength, false), true));
        if (reader == nullptr || reader->bitsPerSample != 24)
            return out;
        std::vector<int> ints ((std::size_t) count, 0);
        int* dest[1] = { ints.data() };
        if (! reader->read (dest, 1, 0, count, false))
            return out;
        for (const int x : ints)
            out.push_back ((float) ((double) x / 2147483648.0));
        return out;
    }
}

//==================================================================================================
SL_TEST ("state", saudChunkRecord)
{
    section ("SAUD (M7 V3, plan 3): the record round-trips, keeps a newer version's fields, reads an unknown encoding, refuses one cut short");

    state::SaudHeader h;
    h.encoding = state::kSaudFlac24;
    h.channels = 2;
    h.sampleRate = 44100.25;
    h.excerptStartFrame = 123456789012ull;
    h.excerptFrames = 98765;
    const Bytes audio = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    Bytes payload = state::encodeSaud (h, audio.data(), audio.size());
    check (payload.size() == state::kSaudHeaderBytes + audio.size() && state::kSaudHeaderBytes == 37,
           fmt ("a payload is its 37 header bytes and its bytes (%d)", (int) payload.size()));

    state::SaudView v;
    const bool read = state::decodeSaud (payload.data(), payload.size(), v);
    check (read && v.known() && v.header.encoding == h.encoding && v.header.channels == 2 && juce::exactlyEqual (v.header.sampleRate, 44100.25)
               && v.header.excerptStartFrame == h.excerptStartFrame && v.header.excerptFrames == h.excerptFrames && v.header.byteLength == audio.size()
               && v.bytes == payload.data() + state::kSaudHeaderBytes && std::memcmp (v.bytes, audio.data(), audio.size()) == 0 && v.trailingBytes == 0,
           "every field reads back, and the bytes are read in place");

    // A newer version's fields after the bytes: read, and counted (the payload is kept whole).
    Bytes newer = payload;
    newer.insert (newer.end(), { 0xaa, 0xbb, 0xcc });
    state::SaudView vn;
    check (state::decodeSaud (newer.data(), newer.size(), vn) && vn.trailingBytes == 3 && vn.header.byteLength == audio.size(),
           "trailing fields of a newer version are left as they are (3 bytes)");

    // An encoding this version does not know reads too: it is kept, never played.
    Bytes unknown = payload;
    unknown[0] = 7;
    state::SaudView vu;
    check (state::decodeSaud (unknown.data(), unknown.size(), vu) && ! vu.known(), "encoding 7 reads, and known () says it cannot be played");

    // Cut short: in the header, or fewer bytes than byteLength says. A no-op is not a pass: one byte fewer is refused.
    int refused = 0;
    for (std::size_t n = 0; n < payload.size(); ++n)
    {
        state::SaudView vc;
        refused += state::decodeSaud (payload.data(), n, vc) ? 0 : 1;
    }
    check (refused == (int) payload.size(), fmt ("every prefix shorter than the payload is refused (%d of %d)", refused, (int) payload.size()));
    Bytes lying = payload;
    lying[29] = 0xff;   // byteLength's low byte: 255 bytes claimed, 9 there
    state::SaudView vl;
    check (! state::decodeSaud (lying.data(), lying.size(), vl), "a byteLength past the payload's end is refused");

    // The container: SAUD is written only when there is one, after SMPL (plan 3's table order).
    state::Payloads now;
    for (const auto tag : { state::kTagParm, state::kTagInst, state::kTagClip, state::kTagSmpl, state::kTagUist })
        now[tag] = state::payload (Bytes { 1, 2, 3 });
    const state::KeptLayout fresh {};
    const auto without = fresh.write (now);
    now[state::kTagSaud] = state::payload (payload);
    const auto with = fresh.write (now);
    check (tagsOf (without) == "PARM INST CLIP SMPL UIST" && tagsOf (with) == "PARM INST CLIP SMPL SAUD UIST",
           fmt ("without a sound kept no SAUD chunk is written (%s); with one, it follows SMPL (%s)", tagsOf (without).c_str(), tagsOf (with).c_str()));
    const auto back = saudOf (with);
    check (back.has_value() && *back == payload, "the SAUD chunk holds the payload byte for byte");
}

//==================================================================================================
SL_TEST ("state", embeddedWavRoundTrip)
{
    section ("Embedded sample (M7 V3, plan 3): a 2 MB TestSignals WAV is kept whole; with the file gone it plays from the project, bit for bit");

    const auto dir = songs::tempFolder ("embed-wav");
    const auto file = dir.getChildFile ("two tones.wav");
    const double sr = 44100.0;
    const auto left = signals::tone (220.0, 11.9, sr);
    signals::ToneOpts opts;
    opts.seed = 9;
    const auto right = signals::tone (330.0, 11.9, sr, opts);
    const bool written = writeWav (file, 2, sr, (std::int64_t) left.size(), 16, [&] (std::int64_t start, int count, float* const* o)
    {
        for (int i = 0; i < count; ++i)
        {
            o[0][i] = left[(std::size_t) (start + i)];
            o[1][i] = right[(std::size_t) (start + i)];
        }
    });
    const auto original = fileBytes (file);
    check (written && original.size() > 2000000 && original.size() < 2200000,
           fmt ("a stereo 16-bit WAV of TestSignals tones (seeds 7 and 9) written at test time: %d bytes", (int) original.size()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (file);
    p.waitForSounds (30000);
    p.waitForEmbed();
    const auto saved = saveOf (p);
    const auto saud = saudOf (saved);
    state::SaudView v;
    const bool read = saud.has_value() && state::decodeSaud (saud->data(), saud->size(), v);
    const auto ref = p.getSampleRef();
    check (tagsOf (saved) == "PARM INST CLIP SMPL SAUD UIST" && read && v.header.encoding == state::kSaudOriginal && ref.file.keepInProject == 1,
           fmt ("Keep the sound in the project is on by default: the save holds SAUD encoding 0 (%s)", tagsOf (saved).c_str()));
    check (read && v.header.byteLength == original.size() && std::memcmp (v.bytes, original.data(), original.size()) == 0,
           "its bytes are the file's, identical (no question below 20 MB)");
    check (read && v.header.channels == 2 && juce::exactlyEqual (v.header.sampleRate, sr) && v.header.excerptStartFrame == 0
               && v.header.excerptFrames == (std::uint64_t) left.size(),
           fmt ("its header: 2 channels, 44.1 kHz, the loaded range 0 + %llu frames", (unsigned long long) v.header.excerptFrames));
    const auto pSound = p.getCurrentSound();

    // The file goes: the project plays the copy it kept.
    const auto movedDir = dir.getChildFile ("elsewhere");
    movedDir.createDirectory();
    const auto moved = movedDir.getChildFile ("two tones.wav");
    check (file.moveFileTo (moved) && ! file.existsAsFile(), "the file is moved away");
    StrandLockProcessor q;
    q.waitForSounds();
    loadInto (q, saved);
    q.waitForSounds (30000);
    const auto qSound = q.getCurrentSound();
    bool toldWhere = false;
    for (const auto& r : q.takeSoundReports())
        toldWhere = toldWhere || r.message.find ("the copy kept in the project") != std::string::npos;
    check (qSound != nullptr && pSound != nullptr && qSound->source == doc::SoundSource::embedded && ! qSound->missing && sameSamples (*qSound, *pSound),
           "with the file gone it plays from the bytes kept in the project: the same samples, bit for bit, as from the file");
    check (toldWhere && q.getSoundNotice().kind == Notice::none, "the toast says where it plays from; no banner is needed (all of it was kept)");
    check (saveOf (q) == saved && q.getStateBanner().isEmpty(), "the project re-saves byte-identically (the SMPL record and the SAUD as they were)");

    // A no-op is not a pass: a copy with one byte changed is not the saved file (its SHA-256), so it is refused.
    const auto damaged = rebuilt (saved, [] (std::uint32_t tag, const Bytes& payload) -> std::optional<Bytes>
    {
        auto b = payload;
        if (tag == state::kTagSaud && b.size() > state::kSaudHeaderBytes + 5000)
            b[state::kSaudHeaderBytes + 5000] ^= 0x01;
        return b;
    });
    StrandLockProcessor r;
    r.waitForSounds();
    loadInto (r, damaged);
    r.waitForSounds (30000);
    const auto rSound = r.getCurrentSound();
    check (rSound != nullptr && rSound->missing && rSound->builtIn && r.getSoundNotice().kind == Notice::locate,
           "negative control: a copy whose bytes are not the saved file's is not played; the Locate banner shows");
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("state", embedForwardCompatibility)
{
    section ("Kept unreadable blobs and forward compatibility (plan 3) hold with SAUD present");

    const auto dir = songs::tempFolder ("embed-compat");
    const auto file = dir.getChildFile ("short tone.wav");
    check (writeTone (file, 220.0, 22050.0, 6615), "a 0.3 s mono 16-bit WAV written at test time");
    Bytes saved;
    {
        StrandLockProcessor p;
        p.waitForSounds();
        p.loadSoundFile (file);
        p.waitForSounds (30000);
        p.waitForEmbed();
        saved = saveOf (p);
    }
    check (saudOf (saved).has_value(), "its project holds SAUD (the file's bytes)");
    file.deleteFile();

    // (a) An encoding this version does not know: kept and written back unchanged, never played (the sound is missing).
    {
        const auto blob = rebuilt (saved, [] (std::uint32_t tag, const Bytes& payload) -> std::optional<Bytes>
        {
            auto b = payload;
            if (tag == state::kTagSaud)
                b[0] = 9;
            return b;
        });
        StrandLockProcessor p;
        p.waitForSounds();
        loadInto (p, blob);
        p.waitForSounds (30000);
        const bool missing = p.getCurrentSound() != nullptr && p.getCurrentSound()->missing;
        const bool kept = saveOf (p) == blob;
        params::setPlainValue (p.getParam (params::P::pitch), params::index (params::P::pitch), 5.0f);
        const auto edited = saveOf (p);
        const auto stillThere = saudOf (edited);
        check (missing && kept && p.getStateBanner().isEmpty() && stillThere.has_value() && stillThere == saudOf (blob) && edited != blob,
               "encoding 9: re-saved byte-identically with no banner, the sound missing (nothing it can play); after a parameter edit the SAUD is still written as it was");
        p.loadStarterSound (sounds::StarterId::pluck);
        p.waitForSounds (30000);
        check (! saudOf (saveOf (p)).has_value(), "after another sound is loaded it is no longer written (it kept the old sound)");
    }

    // (b) A newer version's trailing fields after the bytes: played from, and written back as they were.
    {
        const auto blob = rebuilt (saved, [] (std::uint32_t tag, const Bytes& payload) -> std::optional<Bytes>
        {
            auto b = payload;
            if (tag == state::kTagSaud)
                b.insert (b.end(), { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 });
            return b;
        });
        StrandLockProcessor p;
        p.waitForSounds();
        loadInto (p, blob);
        p.waitForSounds (30000);
        const auto s = p.getCurrentSound();
        check (s != nullptr && s->source == doc::SoundSource::embedded && ! s->missing && saveOf (p) == blob,
               "a SAUD with 8 trailing bytes plays (the file is gone) and re-saves byte-identically");
    }

    // (c) A SAUD with a bad CRC: skipped on its own (the sound is missing), kept with the banner while its sound is unchanged.
    {
        auto blob = saved;
        const auto parsed = state::parseContainer (saved.data(), saved.size());
        for (const auto& c : parsed.chunks)
            if (c.tag == state::kTagSaud)
                blob[(std::size_t) (c.raw - saved.data()) + 12] ^= 0x01;   // the chunk's CRC field
        StrandLockProcessor p;
        p.waitForSounds();
        loadInto (p, blob);
        p.waitForSounds (30000);
        const bool missing = p.getCurrentSound() != nullptr && p.getCurrentSound()->missing;
        check (missing && saveOf (p) == blob && p.getStateBanner().isNotEmpty(),
               "a SAUD with a bad CRC: the sound is missing, the blob is re-saved byte-identically with the banner");
        p.loadStarterSound (sounds::StarterId::pluck);
        p.waitForSounds (30000);
        const auto after = saveOf (p);
        check (! saudOf (after).has_value() && p.getStateBanner().isEmpty(), "after another sound is loaded the damaged SAUD goes, and the banner with it");
    }

    // (d) A newer formatVersion: nothing loads, the blob is kept whole and written back, with the banner.
    {
        auto blob = saved;
        blob[4] = 2;
        const auto crc = state::crc32 (blob.data(), 12);
        for (int i = 0; i < 4; ++i)
            blob[12 + (std::size_t) i] = (std::uint8_t) ((crc >> (8 * i)) & 0xffu);
        StrandLockProcessor p;
        p.waitForSounds();
        loadInto (p, blob);
        check (saveOf (p) == blob && p.getStateBanner().isNotEmpty(), "formatVersion 2 with a SAUD inside: kept whole, written back, with the banner");
    }

    // (e) 2000 seeded single-byte mutations of the project (CRCs made anew half the time): nothing escapes, every load
    // leaves the parameters in range, and every save is the blob kept or a container this version reads.
    {
        StrandLockProcessor p;
        p.waitForSounds();
        songs::Noise rng (2026);
        int escaped = 0, illegal = 0, unsound = 0;
        for (int i = 0; i < 2000; ++i)
        {
            auto b = saved;
            const auto at = (std::size_t) (((double) rng.next() * 0.5 + 0.5) * (double) (b.size() - 1));
            b[at] ^= (std::uint8_t) (1u + (std::uint32_t) (((double) rng.next() * 0.5 + 0.5) * 254.0));
            if ((i & 1) == 1)
                b = rebuilt (b, [] (std::uint32_t, const Bytes& payload) -> std::optional<Bytes> { return payload; });
            try
            {
                loadInto (p, b);
            }
            catch (...)
            {
                ++escaped;
            }
            for (int k = 0; k < params::kNumParams; ++k)
            {
                const float x = params::plainValueOf (p.getParam (k));
                const auto& spec = params::kParams[k];
                illegal += (! std::isfinite (x) || x < spec.min || x > spec.max) ? 1 : 0;
            }
            const auto s = saveOf (p);
            unsound += (s == b || state::parseContainer (s.data(), s.size()).status == state::HeaderStatus::ok) ? 0 : 1;
        }
        check (escaped == 0 && illegal == 0 && unsound == 0,
               fmt ("2000 mutations of a project with SAUD: %d exceptions, %d parameters out of range, %d unsound saves", escaped, illegal, unsound));
    }
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", embedAnswerSavedOnce)
{
    section ("The 20-50 MB question (M7 V3, decision 5): asked once per file, its answer saved in SMPL; another file asks again");

    const auto dir = songs::tempFolder ("embed-answer");
    const auto a = dir.getChildFile ("song a.wav");
    const auto b = dir.getChildFile ("song b.wav");
    const std::int64_t frames = 240 * 44100;   // 4 minutes, mono 16-bit: 21.2 MB
    check (writeTone (a, 110.0, 44100.0, frames) && writeTone (b, 147.0, 44100.0, frames) && a.getSize() > 20000000 && b.getSize() > 20000000
               && a.getSize() < 50000000,
           fmt ("two 4-minute mono 16-bit WAVs written at test time (%lld bytes each)", (long long) a.getSize()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (a);
    p.waitForSounds (60000);
    const auto asked = p.getSoundNotice();
    check (asked.kind == Notice::question && asked.text.find ("song a.wav") != std::string::npos && asked.text.find ("21.2 MB") != std::string::npos,
           fmt ("a 21.2 MB file: StrandLock asks (\"%s\")", asked.text.c_str()));
    p.waitForEmbed (60000);
    {
        const auto s = saudOf (saveOf (p));
        state::SaudView v;
        check (s.has_value() && state::decodeSaud (s->data(), s->size(), v) && v.header.encoding == state::kSaudFlac24,
               "until it is answered the project keeps the Region (FLAC 24-bit), never the whole file unasked");
    }

    const auto steps = p.historySize();
    check (p.answerEmbedQuestion (Answer::no) && p.getSoundNotice().kind == Notice::none && p.getSampleRef().file.embedAnswer == 2
               && p.historySize() == steps,
           "No: the question goes, SMPL's embedAnswer is 2, and it is no undo step");
    check (! p.answerEmbedQuestion (Answer::yes), "a second answer is refused: there is no question any more");
    p.waitForEmbed (60000);
    const auto saved = saveOf (p);
    check (! saudOf (saved).has_value() && p.getEmbedStatus().find ("You chose No") != std::string::npos,
           fmt ("after No nothing is kept: no SAUD chunk; the Sound tab says \"%s\"", p.getEmbedStatus().c_str()));

    // Save and load: the answer survives, no second question.
    StrandLockProcessor q;
    q.waitForSounds();
    loadInto (q, saved);
    q.waitForSounds (60000);
    check (q.getSampleRef().file.embedAnswer == 2 && q.getSoundNotice().kind == Notice::none && ! saudOf (saveOf (q)).has_value(),
           "a reload keeps the answer: no question, nothing kept");

    // Another file starts a new SMPL: the question comes back. Yes keeps the whole file (read again on the worker).
    q.loadSoundFile (b);
    q.waitForSounds (60000);
    check (q.getSoundNotice().kind == Notice::question && q.getSampleRef().file.embedAnswer == 0, "another file: the question comes back");
    check (q.answerEmbedQuestion (Answer::yes) && q.waitForEmbed (60000), "Yes");
    {
        const auto s = saudOf (saveOf (q));
        state::SaudView v;
        const auto want = fileBytes (b);
        check (s.has_value() && state::decodeSaud (s->data(), s->size(), v) && v.header.encoding == state::kSaudOriginal
                   && v.header.byteLength == want.size() && std::memcmp (v.bytes, want.data(), want.size()) == 0,
               fmt ("after Yes the whole file is kept, its bytes identical (%d bytes)", (int) want.size()));
    }

    // Undo and Redo keep each file's answer (they are not undo steps).
    q.undo();
    q.waitForSounds (60000);
    q.waitForEmbed (60000);
    const bool backToA = q.getSampleRef().file.displayName == "song a.wav" && q.getSampleRef().file.embedAnswer == 2
                      && q.getSoundNotice().kind == Notice::none && ! saudOf (saveOf (q)).has_value();
    q.redo();
    q.waitForSounds (60000);
    q.waitForEmbed (60000);
    const bool backToB = q.getSampleRef().file.displayName == "song b.wav" && q.getSampleRef().file.embedAnswer == 1
                      && q.getSoundNotice().kind == Notice::none && saudOf (saveOf (q)).has_value();
    check (backToA && backToB, "Undo brings song a back with its No (nothing kept), Redo song b with its Yes (kept whole): neither asks again");

    // With the switch off nothing is kept and nothing is asked.
    StrandLockProcessor r;
    r.waitForSounds();
    r.setKeepInProject (false);
    r.loadSoundFile (a);
    r.waitForSounds (60000);
    r.waitForEmbed (60000);
    check (r.getSoundNotice().kind == Notice::none && ! saudOf (saveOf (r)).has_value() && r.getSampleRef().file.keepInProject == 0,
           "Keep the sound in the project off: no question, no SAUD, and SMPL says keepInProject 0");
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", embedFortyFiveMegabytes)
{
    section ("Embedded sample (plan 3): a 45 MB file kept whole saves and loads with the file gone (target 2 s, gate 10 s)");

    const auto dir = songs::tempFolder ("embed-45");
    const auto file = dir.getChildFile ("forty five.wav");
    const std::int64_t frames = 234 * 48000;   // stereo 16-bit 48 kHz: 44.9 MB
    songs::Noise noise (45);
    const bool written = writeWav (file, 2, 48000.0, frames, 16, [&noise] (std::int64_t start, int count, float* const* o)
    {
        for (int i = 0; i < count; ++i)
        {
            const double t = (double) (start + i) / 48000.0;
            o[0][i] = (float) (0.3 * std::sin (2.0 * 3.141592653589793 * 220.0 * t)) + 0.02f * noise.next();
            o[1][i] = (float) (0.3 * std::sin (2.0 * 3.141592653589793 * 330.0 * t)) + 0.02f * noise.next();
        }
    });
    check (written && file.getSize() > 44000000 && file.getSize() < 46000000, fmt ("a 45 MB WAV written at test time (%lld bytes)", (long long) file.getSize()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (file);
    p.waitForSounds (60000);
    check (p.answerEmbedQuestion (Answer::yes) && p.waitForEmbed (60000), "it asks (45 MB); Yes");
    const auto t0 = std::chrono::steady_clock::now();
    const auto saved = saveOf (p);
    const double saveMs = msSince (t0);
    const auto s = saudOf (saved);
    check (s.has_value() && s->size() == state::kSaudHeaderBytes + (std::size_t) file.getSize(),
           fmt ("the save holds the whole file: a %d-byte SAUD (the limit is 50,000,000)", s.has_value() ? (int) s->size() : -1));

    file.deleteFile();
    StrandLockProcessor q;
    q.waitForSounds();
    const auto t1 = std::chrono::steady_clock::now();
    loadInto (q, saved);
    const double loadMs = msSince (t1);
    q.waitForSounds (60000);
    const auto ps = p.getCurrentSound(), qs = q.getCurrentSound();
    check (qs != nullptr && ps != nullptr && qs->source == doc::SoundSource::embedded && sameSamples (*qs, *ps),
           "with the file gone it loads from the project: every sample the same as from the file");
    note (fmt ("the 45 MB project saved in %.0f ms and loaded (its sound decoded from the project) in %.0f ms (target 2000 ms, gate 10000 ms)", saveMs, loadMs));
    check (saveMs <= 10000.0 && loadMs <= 10000.0, fmt ("neither past 10 s (%.0f ms, %.0f ms)", saveMs, loadMs));
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", regionExcerptWithOriginalMissing)
{
    section ("A Region excerpt (FLAC 24-bit, plan 3): within 2^-23 of the original; with the file gone it plays, a Region outside it plays its nearest edge, the banner says so");

    const auto dir = songs::tempFolder ("embed-region");
    const auto file = dir.getChildFile ("float song.wav");
    const double sr = 48000.0;
    const std::int64_t frames = 130 * 48000;   // mono 32-bit float: 25 MB, under 3 minutes (kept as floats)
    songs::Noise noise (24);
    const bool written = writeWav (file, 1, sr, frames, 32, [&noise, sr] (std::int64_t start, int count, float* const* o)
    {
        for (int i = 0; i < count; ++i)
            o[0][i] = (float) (0.4 * std::sin (2.0 * 3.141592653589793 * 220.0 * (double) (start + i) / sr)) + 0.05f * noise.next();
    });
    check (written && file.getSize() > 20000000 && file.getSize() < 50000000, fmt ("a 130 s mono float WAV written at test time (%lld bytes)", (long long) file.getSize()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (file);
    p.waitForSounds (60000);
    p.setRegionStep (10.0 / 130.0, 14.0 / 130.0, "Region");
    check (p.answerEmbedQuestion (Answer::region) && p.waitForEmbed (60000), "it asks (25 MB); Just the Region, with the Region at 0:10 .. 0:14");
    const auto saved = saveOf (p);
    const auto s = saudOf (saved);
    state::SaudView v;
    const bool read = s.has_value() && state::decodeSaud (s->data(), s->size(), v);
    const auto n = (double) frames;
    const auto rs = (std::uint64_t) std::floor (10.0 / 130.0 * n), re = (std::uint64_t) std::ceil (14.0 / 130.0 * n);
    const auto start = read ? v.header.excerptStartFrame : 0, count = read ? v.header.excerptFrames : 0;
    check (read && v.header.encoding == state::kSaudFlac24 && v.header.channels == 1 && juce::exactlyEqual (v.header.sampleRate, sr)
               && start <= rs && start + count >= re && start + 48000 >= rs && start + count <= re + 48000 + 1,
           fmt ("SAUD encoding 1: frames %llu + %llu, the Region (%llu .. %llu) with up to 1 s either side", (unsigned long long) start,
                (unsigned long long) count, (unsigned long long) rs, (unsigned long long) re));

    // Within 2^-23 of the original samples (decoded here, apart from the plugin); a frame off would be far off.
    const auto want = readFloats (file, (std::int64_t) start, (int) count);
    const auto got = flacSamples (*s, (int) count);
    double worst = got.size() == want.size() && ! got.empty() ? 0.0 : 1.0, shifted = 0.0;
    for (std::size_t i = 0; i < got.size() && i < want.size(); ++i)
    {
        worst = std::max (worst, std::abs ((double) got[i] - (double) want[i]));
        if (i + 1 < want.size())
            shifted = std::max (shifted, std::abs ((double) got[i] - (double) want[i + 1]));
    }
    const double lsb = std::ldexp (1.0, -23);
    check (worst <= lsb && worst > 0.0, fmt ("the FLAC 24-bit excerpt decodes within 2^-23 of the original floats: at most %.3g (2^-23 = %.3g)", worst, lsb));
    check (shifted > 1000.0 * lsb, fmt ("negative control: one frame off, the difference is %.3g", shifted));

    // The file goes: the excerpt is the sample.
    file.deleteFile();
    StrandLockProcessor q;
    q.waitForSounds();
    loadInto (q, saved);
    q.waitForSounds (60000);
    q.prepareToPlay (48000.0, 64);
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    q.processBlock (buffer, midi);
    const auto qs = q.getCurrentSound();
    const auto* played = q.getAudioSample();
    bool same = qs != nullptr && played != nullptr && played->n == (std::int64_t) count && played->format == engine::SampleFormat::f32
             && played->excerpt.active && got.size() == (std::size_t) count;
    for (std::size_t i = 0; same && i < got.size(); ++i)
        same = std::memcmp (&played->L[i], &got[i], sizeof (float)) == 0;
    check (qs != nullptr && qs->source == doc::SoundSource::excerpt && same,
           "with the file gone the excerpt plays: its frames as the FLAC holds them, bit for bit (so within 2^-23 of the original)");
    const auto notice = q.getSoundNotice();
    check (notice.kind == Notice::partKept && notice.text.find ("Part of this sound was not saved in the project") != std::string::npos,
           fmt ("the banner: \"%s\"", notice.text.c_str()));

    // The Region (a fraction of the loaded range) maps onto the excerpt; outside it, the nearest edge plays.
    const auto inside = q.readInstFor (played);
    const double wantStart = ((10.0 / 130.0) * n - (double) start) / (double) count, wantEnd = ((14.0 / 130.0) * n - (double) start) / (double) count;
    check (std::abs (inside.region.start - wantStart) < 1e-9 && std::abs (inside.region.end - wantEnd) < 1e-9,
           fmt ("the Region 0:10 .. 0:14 plays as %.6f .. %.6f of the excerpt", inside.region.start, inside.region.end));
    songs::hostSets (q.getParam (params::P::regionStart), 0.0f);
    songs::hostSets (q.getParam (params::P::regionEnd), 0.05f);
    const auto before = q.readInstFor (played);
    songs::hostSets (q.getParam (params::P::regionStart), 0.9f);
    songs::hostSets (q.getParam (params::P::regionEnd), 1.0f);
    const auto after = q.readInstFor (played);
    const auto unmapped = q.readInstFor (nullptr);
    check (juce::exactlyEqual (before.region.start, 0.0) && juce::exactlyEqual (before.region.end, 0.0) && juce::exactlyEqual (after.region.start, 1.0)
               && juce::exactlyEqual (after.region.end, 1.0),
           "automation reaching before the excerpt (0 .. 0.05) plays its first edge, after it (0.9 .. 1) its last: kept inside, never blocked");
    check (std::abs (unmapped.region.start - (double) 0.9f) < 1e-12 && juce::exactlyEqual (unmapped.region.end, 1.0),
           "negative control: for any other sample the Region is the parameters' own (0.9 .. 1)");
    q.releaseResources();
    dir.deleteRecursively();
}

//==================================================================================================
SL_TEST ("songs", noExcerptOverFiftyMegabytes)
{
    section ("No excerpt is written above 50 MB (plan 3): the person is told; a shorter Region of the same file is kept");

    const auto dir = songs::tempFolder ("embed-cap");
    const auto file = dir.getChildFile ("noise song.wav");
    const std::int64_t frames = 271 * 48000;   // stereo 16-bit white noise: 52 MB, so no question and the Region is kept
    songs::Noise noise (52);
    const bool written = writeWav (file, 2, 48000.0, frames, 16, [&noise] (std::int64_t, int count, float* const* o)
    {
        for (int i = 0; i < count; ++i)
        {
            o[0][i] = 0.9f * noise.next();
            o[1][i] = 0.9f * noise.next();
        }
    });
    check (written && file.getSize() > 50000000, fmt ("a 52 MB WAV of white noise written at test time (%lld bytes)", (long long) file.getSize()));

    StrandLockProcessor p;
    p.waitForSounds();
    p.loadSoundFile (file);
    p.waitForSounds (60000);
    p.setRegionStep (0.0, 1.0, "Region");
    p.waitForEmbed (60000);
    const auto saved = saveOf (p);
    bool told = false;
    for (const auto& r : p.takeSoundReports())
        told = told || r.message.find ("too long to keep in the project") != std::string::npos;
    check (p.getSoundNotice().kind == Notice::none, "above 50 MB nothing is asked");
    check (! saudOf (saved).has_value() && p.getEmbedStatus().find ("too long") != std::string::npos && told,
           fmt ("the whole file as the Region would need more than 50 MB even as FLAC: nothing is kept, and the toast and the Sound tab say so (\"%s\")",
                p.getEmbedStatus().c_str()));

    // A no-op is not a pass: a 2 s Region of the same file is kept.
    p.setRegionStep (0.5, 0.5 + 2.0 / 271.0, "Region");
    p.waitForEmbed (60000);
    const auto s = saudOf (saveOf (p));
    state::SaudView v;
    const bool read = s.has_value() && state::decodeSaud (s->data(), s->size(), v);
    check (read && v.header.encoding == state::kSaudFlac24 && s->size() <= state::kMaxSaudBytes && v.header.excerptFrames <= (std::uint64_t) (4 * 48000 + 2),
           fmt ("a 2 s Region is kept: %d bytes, %llu frames (it and 1 s either side)", s.has_value() ? (int) s->size() : -1,
                read ? (unsigned long long) v.header.excerptFrames : 0ull));
    dir.deleteRecursively();
}
