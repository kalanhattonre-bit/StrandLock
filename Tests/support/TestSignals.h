#pragma once

// The prototype tests' signal makers, ported (PLUGIN-PLAN 5.3, Tests/fixtures/support/signals.json): lcg, midiHz,
// tone, noise, formantTone, smoothNoise, chord and rampSource from prototype-v4/tests/test-engine.js and dc from
// test-engine-extra.js. Each is ported line by line, with the prototype's Float32 arrays as std::vector<float>
// (every `x[i] += v` reads the float, adds in double and stores a float again, as a Float32Array does).
// engine/renders.json and analysis/detect.json name their sounds as {signal, args} with these functions, so the
// engine and analysis tests make them through makeSignal. test_signals.cpp checks every case of signals.json.

#include "core/JsonLite.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sl::test::signals
{
    /** lcg (seed): s = seed >>> 0 || 1; each call s = (imul (s, 1664525) + 1013904223) >>> 0 and returns s / 2^32. */
    class Lcg
    {
    public:
        explicit Lcg (double seed) noexcept;
        double operator()() noexcept;

    private:
        std::uint32_t s;
    };

    /** midiHz (m) = 440 x 2^((m - 69) / 12). */
    double midiHz (double m) noexcept;

    /** tone's opts. 0 (or NaN) takes the JavaScript default, as `opts.x || d` does: 12 partials, amp 0.5, seed 7. */
    struct ToneOpts
    {
        double partials = 0.0;
        double amp = 0.0;
        double seed = 0.0;
    };

    /** A sum of partials 1/k with lcg phases, cut at 0.45 sr, scaled to a peak of amp. round (secs x sr) frames. */
    std::vector<float> tone (double f0, double secs, double sr, const ToneOpts& opts = {});

    /** (lcg () x 2 - 1) x amp. */
    std::vector<float> noise (double secs, double sr, double seed = 3.0, double amp = 0.5);

    /** Partials under 0.4 sr (at most 40) shaped by a Gaussian formant at fc with width bw, peak 0.5. */
    std::vector<float> formantTone (double f0, double secs, double sr, double fc, double bw);

    /** noise (secs, sr, seed, 1) through two 4-point moving averages, peak 0.5. */
    std::vector<float> smoothNoise (double secs, double sr, double seed);

    /** The tones of midis (8 partials, seeds 11, 12, ..., amp 1) summed, peak 0.5. */
    std::vector<float> chord (const std::vector<double>& midis, double secs, double sr);

    /** dc: every frame v. */
    std::vector<float> dc (double secs, double sr, double v = 0.5);

    /** A signal with its channels (one for a mono signal) and rate. */
    struct Signal
    {
        std::vector<std::vector<float>> channels;
        double sampleRate = 48000.0;
        std::string name;
    };

    /** rampSource: L rises 0.05 -> 0.85 over the file, R is 0.5 (two channels, name "ramp"). */
    Signal rampSource (double secs, double sr);

    /** A signal by name and JSON arguments, as support.js makeSignal: the rate is args[2] for tone, chord and
        formantTone, args[1] for noise, smoothNoise and dc, rampSource's own; a missing argument takes the
        function's default. False (out untouched) for "lcg" or an unknown name or argument shape. */
    bool makeSignal (std::string_view fn, const core::JsonValue& args, Signal& out);

    /** The same from a fixture's {signal, args}. */
    bool makeSignal (const core::JsonValue& spec, Signal& out);

    /** FNV-1a 32 over the little-endian bytes of each channel in order (the fixtures' audio `hash`). */
    std::uint32_t fnvF32 (const std::vector<std::vector<float>>& channels) noexcept;

    /** 8 lowercase hex digits, as the fixtures write a hash. */
    std::string hex8 (std::uint32_t h);
}
