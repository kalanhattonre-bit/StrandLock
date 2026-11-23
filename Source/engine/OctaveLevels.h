#pragma once

// Fast reads without aliasing (M7 V4; PLUGIN-PLAN 4 M7 "fast reads", 7.9 decision 6, question 9.6). Played faster than
// it was recorded, a sound's top octaves fold back below half the sample rate as tones that are not part of the note:
// Tape three octaves up on the Saw had 3 % of its sound off the harmonic lines in M3. So after the decode the
// SampleManager's worker builds octave levels of the sound. Level L is level L - 1 low-passed (flat to 0.15 of level
// L - 1's rate, at least 60 dB down from 0.2 of it) and decimated by 2: flat to 0.3 of its own rate, nothing above 0.4 of
// it. Levels 1 to 3, in the sound's own format (16-bit sounds get 16-bit levels).
//
// The level choice (levelFor): a read of r frames of the sound per output sample reads the sound itself up to 1.25x,
// else the lowest level at which it is at most 1.25x, level L at r / 2^L, so a level is read at 0.625x to 1.25x of its
// own rate (level 3 also faster, above 10x). A level holds nothing above 0.4 of its own rate (at least 60 dB down from
// there), so a read of up to 1.25x of it never folds (0.4 x 1.25 = 0.5), and read slower its images start at 0.6 of its
// rate, where the read kernel takes them out. In the node model every level read of the Saw from +4 to +39 st at 44.1,
// 48 and 96 kHz is at most -53 dB off the harmonic lines (NOTES-M7 Fix, F1). The price is the top of the band: just
// after each change a level is flat to 0.3 x 0.625 of the output's rate (8.3 kHz at 44.1 kHz), rising to 0.3 x 1.25
// of it (16.5 kHz) just before the next change. A read at or below 1.25x never touches a level, so it plays bit for bit
// as in M3 (and between 1x and 1.25x folds as M3's reads did). (The plan's floor (log2 rate) would read level 0 at
// 1.84x in a 48 kHz session with a 44.1 kHz sound one octave up, which aliases as M3 did; NOTES-M7 V4 says why the rule
// is this one.)
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

// SSE2 (every x64 machine has it; never AVX or FMA, PLUGIN-PLAN 1.6) holds two doubles per register: the level read below
// works on two taps at a time. Elsewhere the same operations run one at a time, in the same order, so the bits are the same.
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
 #define SL_LEVEL_READ_SSE2 1
 #include <emmintrin.h>
#else
 #define SL_LEVEL_READ_SSE2 0
#endif

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
            double kernel[(kLevelPhases + 1) * kLevelTaps];  // row k: the 8 taps for f = k / 512, summing to 1 (doubles, so a
                                                             // read converts nothing but the samples: fix pass P7-1)
            float filter[2 * kLevelHalf + 1];                // the decimation filter, centred on filter[kLevelHalf], summing to 1

            LevelTables() noexcept;
        };

        extern const LevelTables kLevelTables;

#if SL_LEVEL_READ_SSE2
        /** Two stored samples p[0], p[1] as doubles in one register: exactly the values sampleValue gives (a float as it is, a
            16-bit sample times 2^-15). The float pair is one 8-byte load, never past p[1]. */
        inline __m128d levelPair (const float* p) noexcept
        {
            return _mm_cvtps_pd (_mm_castsi128_ps (_mm_loadl_epi64 (reinterpret_cast<const __m128i*> (p))));
        }

        inline __m128d levelPair (const std::int16_t* p) noexcept
        {
            return _mm_mul_pd (_mm_cvtepi32_pd (_mm_setr_epi32 (p[0], p[1], 0, 0)), _mm_set1_pd (kInt16Scale));
        }
#endif
    }

    /** The read kernel: (kLevelPhases + 1) rows of kLevelTaps doubles. */
    inline const double* levelKernel() noexcept { return detail::kLevelTables.kernel; }

    /** The decimation filter: 2 kLevelHalf + 1 floats. */
    inline const float* levelFilter() noexcept { return detail::kLevelTables.filter; }

    /** The 8 taps h over the 8 frames p[0 .. 7]: the even taps summed in one lane and the odd taps in another, each in tap
        order, the two lanes added last. With SSE2 one register holds both lanes, so a read takes half the steps of one
        running sum (fix pass P7-1); without it the same operations run in the same order, so every machine gives the same
        bits. (A level read matches no older read, so the order is free; it is the same every time.) */
    template <class T>
    inline double levelDot (const double* h, const T* p) noexcept
    {
#if SL_LEVEL_READ_SSE2
        __m128d acc = _mm_mul_pd (_mm_loadu_pd (h), detail::levelPair (p));
        acc = _mm_add_pd (acc, _mm_mul_pd (_mm_loadu_pd (h + 2), detail::levelPair (p + 2)));
        acc = _mm_add_pd (acc, _mm_mul_pd (_mm_loadu_pd (h + 4), detail::levelPair (p + 4)));
        acc = _mm_add_pd (acc, _mm_mul_pd (_mm_loadu_pd (h + 6), detail::levelPair (p + 6)));
        return _mm_cvtsd_f64 (acc) + _mm_cvtsd_f64 (_mm_unpackhi_pd (acc, acc));
#else
        double even = h[0] * sampleValue (p[0]);
        double odd = h[1] * sampleValue (p[1]);
        even = even + h[2] * sampleValue (p[2]);
        odd = odd + h[3] * sampleValue (p[3]);
        even = even + h[4] * sampleValue (p[4]);
        odd = odd + h[5] * sampleValue (p[5]);
        even = even + h[6] * sampleValue (p[6]);
        odd = odd + h[7] * sampleValue (p[7]);
        return even + odd;
#endif
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
        const double* h = levelKernel() + phase * kLevelTaps;
        double a = 0, b = 0;
        if (i >= 3 && i + 4 < n)
        {
            a = levelDot (h, L + (i - 3));
            if (stereo)
                b = levelDot (h, R + (i - 3));
        }
        else
        {
            // near an edge: the frames with the edge held, then the same sum as inside
            const std::int64_t m = n - 1;
            T heldL[kLevelTaps], heldR[kLevelTaps];
            for (int j = 0; j < kLevelTaps; ++j)
            {
                std::int64_t q = i - 3 + j;
                q = q < 0 ? 0 : q > m ? m : q;
                heldL[j] = L[q];
                heldR[j] = stereo ? R[q] : L[q];
            }
            a = levelDot (h, heldL);
            if (stereo)
                b = levelDot (h, heldR);
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
