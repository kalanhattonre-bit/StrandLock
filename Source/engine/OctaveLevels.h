#pragma once

// Fast reads without aliasing (M7 V4; PLUGIN-PLAN 4 M7 "fast reads", 7.9 decision 6, question 9.6). Played faster than
// it was recorded, a sound's top octaves fold back below half the sample rate as tones that are not part of the note:
// Tape three octaves up on the Saw had 3 % of its sound off the harmonic lines in M3. So after the decode the
// SampleManager's worker builds octave levels of the sound. Level L is level L - 1 low-passed (flat to a fifth of its
// rate, 60 dB down from a quarter of it) and decimated by 2; levels 1 to 3, in the sound's own format (16-bit sounds
// get 16-bit levels).
//
// The level choice (levelFor): a read of r frames of the sound per output sample reads the sound itself up to 1.25x,
// else the lowest level at which it is at most 1.25x, level L at r / 2^L. A level holds nothing above 0.4 of its own
// rate, so a read of up to 1.25x of it never folds. A read at or below 1.25x never touches a level, so it plays bit for
// bit as in M3. (The plan's floor (log2 rate) would read level 0 at 1.84x in a 48 kHz session with a 44.1 kHz sound one
// octave up, which aliases as M3 did; NOTES-M7 V4 says why the rule is this one.)
//
// A level is read with an 8-tap Kaiser-windowed sinc (512 fractions, the nearest taken), not the engine's 4-point
// Hermite. Read slower than its own rate (a 44.1 kHz sound in a 48 kHz session), the Hermite leaves images of the upper
// part of the band at -8 to -30 dB; a level's band is full of the note's own strong harmonics, so those images were more
// than the -40 dB the plan allows (NOTES-M7 V4). The kernel and the filter are built at static-init time, like the
// engine's tables.
//
// Plain C++ (StrandCore). Building allocates (the worker, or a test); reading is the audio thread's and allocates nothing.

#include "engine/SampleData.h"
#include "engine/SourceRead.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

namespace sl::engine
{
    /** A read at most this fast (frames of what it reads per output sample) reads it as it is. */
    inline constexpr double kLevelReadMost = 1.25;

    /** The level a read of `rate` frames of the sound per output sample (either direction) reads: 0 (the sound itself) up
        to 1.25x, level 1 up to 2.5x, level 2 up to 5x, level 3 above; never above `levels`, the levels the sound has. A
        NaN rate reads the sound. */
    inline int levelFor (double rate, int levels) noexcept
    {
        double r = rate < 0 ? -rate : rate;
        int level = 0;
        while (level < levels && r > kLevelReadMost)
        {
            ++level;
            r *= 0.5;
        }
        return level;
    }

    /** 2^-level (exact): level L's frame for a frame of the sound. */
    inline constexpr double kLevelScale[kMaxOctaveLevels + 1] = { 1.0, 0.5, 0.25, 0.125 };

    inline constexpr int kLevelTaps = 8;        // the read: the level's frames i - 3 .. i + 4 around the position i + f
    inline constexpr int kLevelPhases = 512;    // the fractions f = k / 512, the nearest one taken
    inline constexpr int kLevelHalf = 37;       // the decimation filter: 2 x 37 + 1 = 75 taps
    inline constexpr int kLevelMinFrames = 8;   // a level that would have fewer frames is not made

    namespace detail
    {
        struct LevelTables
        {
            float kernel[(kLevelPhases + 1) * kLevelTaps];   // row k: the 8 taps for f = k / 512, summing to 1
            float filter[2 * kLevelHalf + 1];                // the decimation filter, centred on filter[kLevelHalf], summing to 1

            LevelTables() noexcept;
        };

        extern const LevelTables kLevelTables;
    }

    /** The read kernel: (kLevelPhases + 1) rows of kLevelTaps floats. */
    inline const float* levelKernel() noexcept { return detail::kLevelTables.kernel; }

    /** The decimation filter: 2 kLevelHalf + 1 floats. */
    inline const float* levelFilter() noexcept { return detail::kLevelTables.filter; }

    /** The 8 taps h over the 8 frames p[0 .. 7], summed in pairs and then pairs of pairs: fewer dependent steps than one
        running sum (a level read matches no older read, so the order is free; it is the same every time). */
    template <class T>
    inline double levelDot (const float* h, const T* p) noexcept
    {
        const double s01 = static_cast<double> (h[0]) * sampleValue (p[0]) + static_cast<double> (h[1]) * sampleValue (p[1]);
        const double s23 = static_cast<double> (h[2]) * sampleValue (p[2]) + static_cast<double> (h[3]) * sampleValue (p[3]);
        const double s45 = static_cast<double> (h[4]) * sampleValue (p[4]) + static_cast<double> (h[5]) * sampleValue (p[5]);
        const double s67 = static_cast<double> (h[6]) * sampleValue (p[6]) + static_cast<double> (h[7]) * sampleValue (p[7]);
        return (s01 + s23) + (s45 + s67);
    }

    static_assert (kLevelTaps == 8, "levelDot sums 8 taps");

    /** One level's read at its frame p (fractional, unbounded: beyond +-1e12 it reads frame 0), both channels at the same
        position, every index guarded (outside the level, its edge frame), as readAbsOf guards the sound's. */
    template <class T>
    inline void readLevelOf (const T* L, const T* R, std::int64_t n, bool stereo, double p, double& l, double& r) noexcept
    {
        if (! (p > -1e12 && p < 1e12))
            p = 0;
        // floor (p), without the library call for the usual p >= 0 (a cast truncates toward zero, which is the floor there)
        const double fl = p >= 0 ? static_cast<double> (static_cast<std::int64_t> (p)) : std::floor (p);
        const auto i = static_cast<std::int64_t> (fl);
        const auto phase = static_cast<std::int64_t> ((p - fl) * kLevelPhases + 0.5);   // 0 .. kLevelPhases
        const float* h = levelKernel() + phase * kLevelTaps;
        double a = 0, b = 0;
        if (i >= 3 && i + 4 < n)
        {
            a = levelDot (h, L + (i - 3));
            if (stereo)
                b = levelDot (h, R + (i - 3));
        }
        else
        {
            const std::int64_t m = n - 1;
            for (int j = 0; j < kLevelTaps; ++j)
            {
                std::int64_t q = i - 3 + j;
                q = q < 0 ? 0 : q > m ? m : q;
                a += static_cast<double> (h[j]) * sampleValue (L[q]);
                if (stereo)
                    b += static_cast<double> (h[j]) * sampleValue (R[q]);
            }
        }
        l = a;
        r = stereo ? b : a;
    }

    /** A read of octave level `level` (1 .. s.numLevels) at frame x of the sound: the level's frame x / 2^level. */
    inline void readLevel (const SampleData& s, int level, double x, double& l, double& r) noexcept
    {
        const OctaveLevel& q = s.levels[static_cast<std::size_t> (level - 1)];
        const double p = x * kLevelScale[level];
        if (s.format == SampleFormat::i16)
            readLevelOf (q.L16, q.R16, q.n, s.stereo, p, l, r);
        else
            readLevelOf (q.L, q.R, q.n, s.stereo, p, l, r);
    }

    /** Octave levels built for a sound, owning their samples: attach them to any SampleData over the same samples (the
        SampleManager keeps a starter's with the starter). */
    struct LevelSet
    {
        std::shared_ptr<const void> storage;   // null: none (the sound is too short, or the build was stopped)
        std::array<OctaveLevel, kMaxOctaveLevels> levels {};
        int count = 0;
        bool stopped = false;                  // the stop question ended the build
    };

    /** Builds levels 1 to 3 of s (fewer when a level would have fewer than 8 frames), each the level below through the
        decimation filter (its edges held, as the reads hold them) and every second frame kept; 16-bit when s is 16-bit
        (toInt16 of the filtered value). They take at most s's own sample bytes again (7/8 of them). stop is asked
        between blocks of 4096 frames: true ends the build with no levels. Allocates: the worker, or a test. */
    LevelSet buildOctaveLevels (const SampleData& s, const std::function<bool()>& stop = {});

    /** Puts the levels on s (numLevels, levels, levelStorage); an empty set leaves s with none. */
    inline void attachOctaveLevels (SampleData& s, const LevelSet& set) noexcept
    {
        s.numLevels = set.storage != nullptr ? set.count : 0;
        s.levels = s.numLevels > 0 ? set.levels : std::array<OctaveLevel, kMaxOctaveLevels> {};
        s.levelStorage = s.numLevels > 0 ? set.storage : nullptr;
    }

    /** buildOctaveLevels, then attach them to s. False when the build was stopped (s then has no levels). */
    inline bool addOctaveLevels (SampleData& s, const std::function<bool()>& stop = {})
    {
        const LevelSet set = buildOctaveLevels (s, stop);
        attachOctaveLevels (s, set);
        return ! set.stopped;
    }
}
