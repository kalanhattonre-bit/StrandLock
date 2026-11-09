// Offline verification for StrandLock. Runs the shipped processor and editor with no host and exits
// non-zero if any check fails, so CI fails with it.
//
//   StrandLockTests                 run everything
//   StrandLockTests --only alloc    run just the allocation check (used for the Debug-CRT build)
//   StrandLockTests --snapshot DIR  render the editor to PNGs in DIR instead of testing

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

//==============================================================================
// Allocation counter. Counting is switched on only for the calling thread, and only
// around processBlock, so it measures exactly what the audio thread would allocate.

namespace alloccount
{
    std::atomic<long long> count { 0 };
    thread_local bool active = false;

    inline void note() noexcept
    {
        if (active)
            count.fetch_add (1, std::memory_order_relaxed);
    }

    struct Scope
    {
        Scope() noexcept  { active = true; }
        ~Scope() noexcept { active = false; }
    };

    const char* coverage = "operator new/new[]";
}

void* operator new (std::size_t size)
{
    alloccount::note();
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    alloccount::note();
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept                 { std::free (p); }
void operator delete[] (void* p) noexcept               { std::free (p); }
void operator delete (void* p, std::size_t) noexcept    { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept  { std::free (p); }

#if defined (__linux__) && defined (__GLIBC__)
// glibc lets the executable interpose the C allocator, which also catches JUCE's HeapBlock
// (it calls malloc directly, never operator new).
extern "C"
{
    void* __libc_malloc (size_t);
    void* __libc_calloc (size_t, size_t);
    void* __libc_realloc (void*, size_t);
    void* __libc_memalign (size_t, size_t);
    void  __libc_free (void*);

    void* malloc (size_t size) noexcept                 { alloccount::note(); return __libc_malloc (size); }
    void* calloc (size_t n, size_t size) noexcept       { alloccount::note(); return __libc_calloc (n, size); }
    void* realloc (void* p, size_t size) noexcept       { alloccount::note(); return __libc_realloc (p, size); }
    void  free (void* p) noexcept                       { __libc_free (p); }
    void* memalign (size_t align, size_t size) noexcept { alloccount::note(); return __libc_memalign (align, size); }
    void* aligned_alloc (size_t align, size_t size) noexcept { alloccount::note(); return __libc_memalign (align, size); }

    int posix_memalign (void** out, size_t align, size_t size) noexcept
    {
        alloccount::note();
        void* p = __libc_memalign (align, size);
        if (p == nullptr)
            return ENOMEM;
        *out = p;
        return 0;
    }
}
 #define STRANDLOCK_INSTALL_ALLOC_HOOK() alloccount::coverage = "malloc/calloc/realloc/memalign + operator new (glibc interposition)"

#elif defined (_MSC_VER) && defined (_DEBUG)
 #include <crtdbg.h>
// The debug CRT reports every heap allocation, including malloc calls that bypass operator new.
static int crtAllocHook (int allocType, void*, size_t, int, long, const unsigned char*, int)
{
    if (allocType == _HOOK_ALLOC || allocType == _HOOK_REALLOC)
        alloccount::note();
    return 1;
}
 #define STRANDLOCK_INSTALL_ALLOC_HOOK() (_CrtSetAllocHook (crtAllocHook), alloccount::coverage = "every CRT heap allocation (debug CRT hook)")

#else
 #define STRANDLOCK_INSTALL_ALLOC_HOOK() (void) 0
#endif

//==============================================================================
namespace
{
    using strandlock::ui::Theme;

    int failures = 0;

    /** printf-style formatting into a juce::String. (juce::String::formatted is wide-char on Windows,
        where %s would read a narrow string as UTF-16 and print garbage.) */
    juce::String fmt (const char* format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start (args, format);
        std::vsnprintf (buffer, sizeof (buffer), format, args);
        va_end (args);
        return juce::String (buffer);
    }

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        std::fflush (stdout);
        if (! ok)
            ++failures;
    }

    void section (const char* name)
    {
        std::printf ("\n== %s ==\n", name);
        std::fflush (stdout);
    }

    using Set = juce::AudioChannelSet;

    juce::AudioProcessor::BusesLayout layout (std::initializer_list<Set> inputs, std::initializer_list<Set> outputs)
    {
        juce::AudioProcessor::BusesLayout l;
        for (const auto& s : inputs)
            l.inputBuses.add (s);
        for (const auto& s : outputs)
            l.outputBuses.add (s);
        return l;
    }

    //==========================================================================
    void testPluginType()
    {
        section ("1. An instrument: no audio input, one stereo output, MIDI in");

        StrandLockProcessor proc;
        check (proc.getBusCount (true) == 0 && proc.getTotalNumInputChannels() == 0,
               fmt ("no input bus (%d input buses, %d input channels)", proc.getBusCount (true), proc.getTotalNumInputChannels()));
        check (proc.getBusCount (false) == 1 && proc.getTotalNumOutputChannels() == 2
                   && proc.getChannelLayoutOfBus (false, 0) == Set::stereo(),
               fmt ("one stereo output bus (%d output buses, %d output channels)", proc.getBusCount (false), proc.getTotalNumOutputChannels()));
        check (proc.acceptsMidi() && ! proc.producesMidi() && ! proc.isMidiEffect(),
               "accepts MIDI, sends none, and is not a MIDI effect");
        check (proc.getName() == "StrandLock", fmt ("named \"%s\"", proc.getName().toRawUTF8()));
        check (proc.getLatencySamples() == 0 && juce::exactlyEqual (proc.getTailLengthSeconds(), 0.0),
               fmt ("zero latency (%d samples), no tail (%.1f s)", proc.getLatencySamples(), proc.getTailLengthSeconds()));

        check (proc.checkBusesLayoutSupported (layout ({}, { Set::stereo() })), "no input / stereo out supported");
        check (! proc.checkBusesLayoutSupported (layout ({}, { Set::mono() })), "no input / mono out rejected");
        check (! proc.checkBusesLayoutSupported (layout ({ Set::stereo() }, { Set::stereo() })), "stereo in / stereo out rejected");
    }

    //==========================================================================
    void testSilence()
    {
        section ("2. Ten seconds with no MIDI: every sample is exactly zero and finite");

        for (const double rate : { 44100.0, 96000.0 })
        {
            for (const int block : { 1, 64, 4096 })
            {
                StrandLockProcessor proc;
                proc.setPlayConfigDetails (0, 2, rate, block);
                proc.prepareToPlay (rate, block);

                juce::AudioBuffer<float> buffer (2, block);
                juce::MidiBuffer midi;
                juce::Random rng ((juce::int64) rate + block);

                const juce::int64 total = (juce::int64) (10.0 * rate);
                long long nonZero = 0, nonFinite = 0;

                for (juce::int64 pos = 0; pos < total; pos += block)
                {
                    const int n = (int) std::min<juce::int64> (block, total - pos);
                    buffer.setSize (2, n, false, false, true);

                    // A host's output buffer can hold anything before the call; the plugin must overwrite it.
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < n; ++i)
                            buffer.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);

                    midi.clear();
                    proc.processBlock (buffer, midi);

                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float* d = buffer.getReadPointer (ch);
                        for (int i = 0; i < n; ++i)
                        {
                            if (! std::isfinite (d[i]))
                                ++nonFinite;
                            else if (! juce::exactlyEqual (d[i], 0.0f))
                                ++nonZero;
                        }
                    }
                }

                proc.releaseResources();
                check (nonZero == 0 && nonFinite == 0,
                       fmt ("%.1f kHz, %d-sample blocks: %lld samples per channel, %lld non-zero, %lld non-finite",
                            rate / 1000.0, block, (long long) total, nonZero, nonFinite));
            }
        }
    }

    //==========================================================================
    juce::MemoryBlock header (const char* magic, juce::uint32 version)
    {
        juce::MemoryBlock block;
        juce::MemoryOutputStream out (block, false);
        out.write (magic, 4);
        out.writeInt ((int) version);
        out.flush();
        return block;
    }

    void testStateRoundTrip()
    {
        section ("3. State: the 'STLK' magic and format version 1 round-trip");

        StrandLockProcessor a;
        juce::MemoryBlock saved;
        a.getStateInformation (saved);

        const auto* bytes = static_cast<const char*> (saved.getData());
        const bool magicOk = saved.getSize() >= 4 && std::memcmp (bytes, "STLK", 4) == 0;
        const juce::uint32 version = saved.getSize() >= 8 ? juce::ByteOrder::littleEndianInt (bytes + 4) : 0;
        check (saved.getSize() == 8, fmt ("version 1 saves the 8-byte header alone (%d bytes)", (int) saved.getSize()));
        check (magicOk, "it starts with the magic 'STLK'");
        check (version == 1, fmt ("then the format version as a little-endian uint32 (%u)", (unsigned) version));

        StrandLockProcessor b;
        check (b.getLoadedStateVersion() == 0, "a new instance has loaded no state");
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        check (b.getLoadedStateVersion() == 1, fmt ("a fresh instance accepts it as version %u", (unsigned) b.getLoadedStateVersion()));

        juce::MemoryBlock again;
        b.getStateInformation (again);
        check (again == saved, "saving the restored instance gives the same bytes");

        // Not a StrandLock state: nothing is taken from it.
        const juce::MemoryBlock full = header ("STLK", 1);
        const juce::MemoryBlock junk[] = {
            juce::MemoryBlock(),                       // empty
            juce::MemoryBlock (full.getData(), 6),     // the header cut short
            header ("GRLK", 1),                        // another plugin's magic
            header ("STLK", 0),                        // version 0 was never written
        };
        int taken = 0;
        for (const auto& data : junk)
        {
            StrandLockProcessor c;
            c.setStateInformation (data.getData(), (int) data.getSize());
            taken += c.getLoadedStateVersion() != 0 ? 1 : 0;
        }
        check (taken == 0, fmt ("ignored: an empty block, a cut-off header, the wrong magic, version 0 (%d of 4 accepted)", taken));
    }

    //==========================================================================
    void testNoAllocations (double minutes)
    {
        section ("4. Long run at 48 kHz / 64-sample blocks with MIDI: no allocations on the audio thread");

        const double rate = 48000.0;
        const int block = 64;
        StrandLockProcessor proc;
        proc.setPlayConfigDetails (0, 2, rate, block);
        proc.prepareToPlay (rate, block);

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        midi.ensureSize (8192);
        juce::Random rng (99);

        const juce::int64 totalBlocks = (juce::int64) (minutes * 60.0 * rate / block);
        std::array<int, 16> held {};
        int numHeld = 0;
        bool silentAndFinite = true;

        alloccount::count = 0;
        const auto startMs = juce::Time::getMillisecondCounterHiRes();

        for (juce::int64 b = 0; b < totalBlocks; ++b)
        {
            // Everything a host would do between callbacks happens outside the counted scope.
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < block; ++i)
                    d[i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.4f;
            }

            midi.clear();
            if (rng.nextInt (6) == 0 && numHeld < (int) held.size())
            {
                const int note = 30 + rng.nextInt (70);
                const juce::uint8 on[3] = { 0x90, (juce::uint8) note, (juce::uint8) (1 + rng.nextInt (127)) };
                midi.addEvent (on, 3, rng.nextInt (block));
                held[(size_t) numHeld++] = note;
            }
            if (numHeld > 0 && rng.nextInt (5) == 0)
            {
                const int idx = rng.nextInt (numHeld);
                const juce::uint8 off[3] = { 0x80, (juce::uint8) held[(size_t) idx], 0 };
                midi.addEvent (off, 3, rng.nextInt (block));
                held[(size_t) idx] = held[(size_t) --numHeld];
            }
            if (rng.nextInt (40) == 0)
            {
                const int bend = rng.nextInt (16384);
                const juce::uint8 pb[3] = { 0xe0, (juce::uint8) (bend & 0x7f), (juce::uint8) (bend >> 7) };
                midi.addEvent (pb, 3, 0);
            }
            if (rng.nextInt (60) == 0)
            {
                static const int controllers[] = { 1, 2, 11, 64, 120, 121, 123 };
                const juce::uint8 cc[3] = { 0xb0, (juce::uint8) controllers[rng.nextInt (7)], (juce::uint8) rng.nextInt (128) };
                midi.addEvent (cc, 3, rng.nextInt (block));
            }
            if (rng.nextInt (90) == 0)
            {
                const juce::uint8 pressure[2] = { 0xd0, (juce::uint8) rng.nextInt (128) };
                midi.addEvent (pressure, 2, block - 1);
            }

            {
                const alloccount::Scope counting;
                proc.processBlock (buffer, midi);
            }

            const float* l = buffer.getReadPointer (0);
            const float* r = buffer.getReadPointer (1);
            for (int i = 0; i < block; ++i)
                silentAndFinite = silentAndFinite && std::isfinite (l[i]) && std::isfinite (r[i])
                                      && juce::exactlyEqual (l[i], 0.0f) && juce::exactlyEqual (r[i], 0.0f);
        }

        const double seconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
        std::printf ("    %.1f minutes of audio in %.1f s; counting %s\n", minutes, seconds, alloccount::coverage);

        check (silentAndFinite, "long run with notes, bends and controllers: silent and finite throughout (the skeleton plays nothing)");
        check (alloccount::count.load() == 0, fmt ("allocations inside processBlock: %lld", alloccount::count.load()));
    }

    //==========================================================================
    int channelDistance (juce::Colour a, juce::Colour b)
    {
        return std::max ({ std::abs ((int) a.getRed() - (int) b.getRed()),
                           std::abs ((int) a.getGreen() - (int) b.getGreen()),
                           std::abs ((int) a.getBlue() - (int) b.getBlue()) });
    }

    /** How many pixels in area are within tolerance of colour. */
    int countNear (const juce::Image& image, juce::Rectangle<int> area, juce::Colour colour, int tolerance)
    {
        int n = 0;
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (channelDistance (pixels.getPixelColour (x, y), colour) <= tolerance)
                    ++n;
        return n;
    }

    void testEditor()
    {
        section ("5. Editor: 1100 x 680, resizable 75% to 200%, renders the dark skeleton");

        StrandLockProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

        check (editor != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 680,
               fmt ("opens at %d x %d", editor != nullptr ? editor->getWidth() : 0, editor != nullptr ? editor->getHeight() : 0));
        if (editor == nullptr)
            return;

        const auto* limits = editor->getConstrainer();
        check (editor->isResizable() && limits != nullptr
                   && limits->getMinimumWidth() == 825 && limits->getMinimumHeight() == 510
                   && limits->getMaximumWidth() == 2200 && limits->getMaximumHeight() == 1360,
               fmt ("resizable from 825 x 510 to 2200 x 1360 (%d x %d to %d x %d)",
                    limits != nullptr ? limits->getMinimumWidth() : 0, limits != nullptr ? limits->getMinimumHeight() : 0,
                    limits != nullptr ? limits->getMaximumWidth() : 0, limits != nullptr ? limits->getMaximumHeight() : 0));

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        check (image.isValid() && image.getWidth() == 1100 && image.getHeight() == 680,
               fmt ("renders a %d x %d image", image.getWidth(), image.getHeight()));

        juce::MemoryOutputStream png;
        juce::PNGImageFormat format;
        const bool encoded = image.isValid() && format.writeImageToStream (image, png);
        juce::MemoryInputStream pngIn (png.getData(), png.getDataSize(), false);
        const auto decoded = encoded ? format.decodeImage (pngIn) : juce::Image();
        check (decoded.isValid() && decoded.getWidth() == 1100 && decoded.getHeight() == 680,
               fmt ("encodes as a %d-byte PNG that reads back at %d x %d", (int) png.getDataSize(), decoded.getWidth(), decoded.getHeight()));

        if (! image.isValid() || image.getWidth() != 1100 || image.getHeight() != 680)
            return;

        // The dark theme: the background beside the wordmark, and the empty panel below it.
        const auto bg = image.getPixelAt (700, 22);
        const auto panel = image.getPixelAt (300, 200);
        check (channelDistance (bg, Theme::background) <= 1,
               fmt ("top bar is the suite's background #151618 (#%s)", bg.toDisplayString (false).toRawUTF8()));
        check (channelDistance (panel, Theme::panel) <= 1,
               fmt ("the empty panel is the suite's panel #1D1E21 (#%s)", panel.toDisplayString (false).toRawUTF8()));

        // The logo's ring is drawn in the orchid accent, and the wordmark in the text colour.
        const int ringPixels = countNear (image, { 16, 11, 22, 22 }, Theme::accent, 12);
        const int wordPixels = countNear (image, { 46, 10, 200, 24 }, Theme::text, 12);
        const int strayAccent = countNear (image, { 46, 0, 1054, 44 }, Theme::accent, 12);
        check (ringPixels >= 20, fmt ("logo ring in orchid #B58CFF (%d pixels)", ringPixels));
        check (wordPixels >= 30, fmt ("STRANDLOCK wordmark in the text colour (%d pixels)", wordPixels));
        check (strayAccent == 0, fmt ("no orchid elsewhere in the top bar (%d pixels)", strayAccent));
    }
}

namespace
{
    bool writePng (const juce::File& file, const juce::Image& image)
    {
        file.deleteFile();
        bool ok = false;
        {
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            ok = out.openedOk() && image.isValid() && png.writeImageToStream (image, out);
        }
        std::printf ("%s %s (%d x %d)\n", ok ? "wrote" : "FAILED to write", file.getFullPathName().toRawUTF8(),
                     image.getWidth(), image.getHeight());
        return ok;
    }

    /** Renders the real editor, exactly as the host shows it at its default size: once at 1x
        (1100 x 680) and once at 2x for a close look at the logo and type. */
    int renderSnapshots (const juce::String& directory)
    {
        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (directory);
        dir.createDirectory();

        StrandLockProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        if (editor == nullptr)
            return 1;

        const auto bounds = editor->getLocalBounds();
        bool ok = writePng (dir.getChildFile ("strandlock-skeleton-dark.png"), editor->createComponentSnapshot (bounds, true, 1.0f));
        ok = writePng (dir.getChildFile ("strandlock-skeleton-dark-2x.png"), editor->createComponentSnapshot (bounds, true, 2.0f)) && ok;
        return ok ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    STRANDLOCK_INSTALL_ALLOC_HOOK();
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String only;
    double allocMinutes = 10.0;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--only" && i + 1 < argc)
            only = argv[++i];
        else if (arg == "--alloc-minutes" && i + 1 < argc)
            allocMinutes = juce::String (argv[++i]).getDoubleValue();
        else if (arg == "--snapshot" && i + 1 < argc)
            return renderSnapshots (argv[++i]);
    }

    auto wants = [&only] (const char* name) { return only.isEmpty() || only == name; };

    if (wants ("type"))    testPluginType();
    if (wants ("silence")) testSilence();
    if (wants ("state"))   testStateRoundTrip();
    if (wants ("alloc"))   testNoAllocations (allocMinutes);
    if (wants ("editor"))  testEditor();

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
