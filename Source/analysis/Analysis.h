#pragma once

// Pitch and key detection and part suggestions (PLUGIN-PLAN 1.2, M7): a line-by-line port of
// prototype-v4/src/sl-analysis.js (SLAnalysis). The same operation order, constants and names (in camelCase),
// double maths, and the prototype's Float32Arrays as floats: every `d[i] += x` reads the float, adds in double
// and stores a float again (plan 1.6). Plain C++20 with no JUCE (StrandCore).
//
// What differs from the prototype:
//  - Decision 3 (plan 7.9): DC gives "no clear pitch". The prototype reads DC as MIDI 90 at 48 kHz: its YIN
//    difference function is 0 at every lag, its normalised curve is then 0 everywhere, and the shortest lag
//    wins. Here a lag whose running sum of differences is still 0 scores 1 (not periodic), as YIN's own
//    definition does for lag 0. Only a frame that is exactly constant over the lags YIN compares is affected.
//    PitchOptions::prototypeDc keeps the prototype's reading, so the tests can show both side by side.
//  - Memory, not results. The prototype makes a full-length mono copy (detectPitch, detectKey) and an 11 kHz
//    copy (suggestParts). This port reads the sound through Source in blocks instead and keeps only per-frame
//    values (a few MB for a 10-minute file), so a long file stored as int16 is analysed without a float copy.
//    Each mono or 11 kHz sample is made from the same samples with the same operations, so the values are the
//    same.
//  - Guards for input no real file gives, where the prototype reads past an array, divides by zero or never
//    ends: suggestParts below 1 kHz, a pitch at a lag of 1 frame (below 3 kHz) and estimateTempo's bar-lag
//    refinement (see each function).
//
// Threads: the SampleManager worker only (plan 7.9 decision 1), never the audio or message thread. Every
// function allocates. A Stop function lets a newer load end an analysis early.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sl::analysis
{
    //==============================================================================================
    // The sound to analyse

    /** Where the analysis reads a sound, in blocks, so the samples can be stored any way (float, or the int16 of
        a long file) without a float copy of the whole file. The analysis reads it once or twice in order, plus
        short windows around the frames it examines. */
    class Source
    {
    public:
        virtual ~Source() = default;

        virtual int numChannels() const = 0;

        /** Frames per channel (every channel has this many). */
        virtual std::int64_t numFrames() const = 0;

        virtual double sampleRate() const = 0;

        /** Copies frames [start, start + count) of channel c to out, as floats. The analysis only asks for
            0 <= c < numChannels (), 0 <= start, 0 < count and start + count <= numFrames (). */
        virtual void read (int c, std::int64_t start, int count, float* out) const = 0;
    };

    /** A Source over float channels that someone else keeps alive (a decoded file, a starter sound, a test
        signal). The channel pointers are copied, the samples are not. */
    class FloatSource final : public Source
    {
    public:
        FloatSource (std::vector<const float*> channels, std::int64_t frames, double rate)
            : pointers (std::move (channels)), length (frames), sr (rate)
        {
        }

        /** A view of equal-length vectors. When they differ, the shortest length is used. */
        static FloatSource of (const std::vector<std::vector<float>>& channels, double rate)
        {
            std::vector<const float*> each;
            std::size_t shortest = channels.empty() ? 0 : channels.front().size();
            for (const auto& c : channels)
            {
                each.push_back (c.data());
                shortest = c.size() < shortest ? c.size() : shortest;
            }
            return FloatSource (std::move (each), static_cast<std::int64_t> (shortest), rate);
        }

        int numChannels() const override { return static_cast<int> (pointers.size()); }
        std::int64_t numFrames() const override { return length; }
        double sampleRate() const override { return sr; }

        void read (int c, std::int64_t start, int count, float* out) const override
        {
            std::memcpy (out, pointers[static_cast<std::size_t> (c)] + start, static_cast<std::size_t> (count) * sizeof (float));
        }

    private:
        std::vector<const float*> pointers;
        std::int64_t length;
        double sr;
    };

    /** Asked now and then while an analysis runs; true ends it early (a newer load came in). An analysis that was
        stopped returns its empty answer (no pitch, no key, no parts), which the caller, who asked it to stop,
        discards. An empty function never stops. */
    using Stop = std::function<bool()>;

    //==============================================================================================
    // detectPitch: YIN on the 5 loudest 2048-sample frames, median

    /** { midi, cents, hz }: the nearest MIDI note (60 = C3, Cubase naming), the cents from it (one decimal,
        Math.round (x * 1000) / 10) and the frequency. */
    struct Pitch
    {
        int midi = 0;
        double cents = 0.0;
        double hz = 0.0;
    };

    struct PitchOptions
    {
        /** Tests only: read a frame that is constant over every lag as the prototype does (DC gives MIDI 90 at
            48 kHz) instead of decision 3's "no clear pitch". */
        bool prototypeDc = false;
    };

    /** SLAnalysis.detectPitch: the pitch, or nothing ("no clear pitch") for silence, DC, noise, a sound shorter
        than 2048 + sr / 45 + 10 frames, or when fewer than half the loud frames are periodic and no two periodic
        frames agree within 20 cents. The 5 loudest 2048-frame blocks are examined; when fewer than half of them
        are periodic, the next 10 loudest are examined too. */
    std::optional<Pitch> detectPitch (const Source& source, const Stop& stop = {}, PitchOptions options = {});

    //==============================================================================================
    // detectKey: Goertzel chroma over MIDI 36-95 in up to 40 frames, Krumhansl-Kessler

    enum class Scale : std::uint8_t
    {
        major,
        minor
    };

    /** "Major" or "Minor": the prototype's strings (SMPL's analysis cache stores them, plan 3). */
    const char* scaleName (Scale scale) noexcept;

    /** { key: 0..11 (C = 0), scale, r }: r is the profile correlation, rounded to 3 decimals. */
    struct Key
    {
        int key = 0;
        Scale scale = Scale::major;
        double r = 0.0;
    };

    /** SLAnalysis.detectKey: the key, or nothing for silence or a sound shorter than 4096 frames. */
    std::optional<Key> detectKey (const Source& source, const Stop& stop = {});

    //==============================================================================================
    // suggestParts: up to 4 parts of a long file

    /** The kinds in the prototype's display order (its `order` table): steady 0, hit 1, quiet 2, loop 3. */
    enum class PartKind : std::uint8_t
    {
        steady = 0,
        hit = 1,
        quiet = 2,
        loop = 3
    };

    /** "steady", "hit", "quiet" or "loop": the prototype's kind strings. */
    const char* partKindName (PartKind kind) noexcept;

    /** One suggestion. start and end are fractions of the analysed sound (0..1). The label holds the part's time
        ("Steady note 0:02", "Big hit 2:45", "Quiet texture 0:06", "One-bar loop 120 bpm 0:29", with " (guess)"
        after "bpm" when no tempo was found), so it is stored, not rebuilt (plan 3). bpm and tempoFound are the
        loop part's (0 and false on the others). */
    struct Part
    {
        PartKind kind = PartKind::steady;
        std::string label;
        double start = 0.0;
        double end = 0.0;
        double bpm = 0.0;
        bool tempoFound = false;
    };

    /** SLAnalysis.suggestParts: at most one part of each kind, in the order steady, hit, quiet, loop. None for a
        sound shorter than 1.2 s, with fewer than 8 coarse frames, or silent, and (a C++ guard) at a rate below
        1 kHz. */
    std::vector<Part> suggestParts (const Source& source, const Stop& stop = {});

    //==============================================================================================
    // The pieces of suggestParts (the prototype exports _features and _estimateTempo for its tests)

    /** features ()' frame values. The ~11 kHz mono copy (the prototype's y: channels summed, D frames at a time,
        times g = 1 / (D x channels)) is not kept: suggestParts reads it again around each onset. Fine frames
        are 128 samples of it (about 11.6 ms): level Lf and high-band level Hf in dB. Coarse frames are 4 fine
        frames (512 samples, about 46 ms): level Lc in dB, spectral flatness and spectral flux. */
    struct Features
    {
        std::int64_t D = 1;     // max (1, round (sr / 11025)): 4 at 44.1 and 48 kHz
        double g = 0.0;         // 1 / (D x channels)
        double srd = 0.0;       // sr / D
        std::int64_t M = 0;     // the 11 kHz copy's length: floor (frames / D)
        int H = 128;
        std::int64_t nf = 0;    // fine frames: floor (M / H)
        double tf = 0.0;        // seconds per fine frame: H / srd
        std::vector<double> Lf, Hf;
        int C = 4;
        std::int64_t nc = 0;    // coarse frames: floor (nf / C)
        double tc = 0.0;        // seconds per coarse frame: tf x C
        std::vector<double> Lc, flat, flux;
        bool stopped = false;   // Stop asked for the end before every frame was made
    };

    /** SLAnalysis features (channels, sr), on the cleaned channels (a NaN, an infinity or a value at or beyond
        1e30 reads as 0). A rate outside 1 kHz .. 10 MHz gives no frames (a C++ guard: below about 120 Hz the
        prototype reads past its spectrum). */
    Features features (const Source& source, const Stop& stop = {});

    /** { bpm, found }: the loop part's tempo. */
    struct Tempo
    {
        double bpm = 120.0;
        bool found = false;
    };

    /** SLAnalysis estimateTempo (on, tf): the tempo from the autocorrelation of the onset curve, 60-200 bpm,
        weighted towards 120, refined on the bar lag; 120 and not found when it is not clear. One guard the
        prototype lacks: a bar-lag refinement that would give no finite positive tempo is dropped (the
        prototype's octave loop would never end on it). */
    Tempo estimateTempo (const std::vector<double>& on, double tf);

    /** A stretch of the sound in seconds. */
    struct Span
    {
        double s = 0.0;
        double e = 0.0;
    };

    /** SLAnalysis overlapOk (a, chosen): a overlaps every chosen span by at most half the shorter of the two
        (plus 1e-9). */
    bool overlapOk (Span a, const std::vector<Span>& chosen) noexcept;
}
