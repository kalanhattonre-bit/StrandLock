// The aliasing figure (PLUGIN-PLAN 4, M3 "aliasing (printed only)", 7.5 decision 3, open question 6): Tape played far
// up on the Saw starter reads the sound faster than it was recorded, and with no anti-aliasing (8.2 #15) the
// harmonics above half the sample rate fold back as tones that are not part of the note. The figure is the energy
// off the harmonic lines of the played note, in dB of the whole (the measure of prototype-v4/tests/test-sounds.js
// inharmonicDb: every bin more than 10 Hz from a multiple of f0, from 20 Hz up), on 2^16 samples from 0.5 s, at
// 48 kHz, for +0, +12, +24 and +36 semitones. Printed next to the prototype's (node). Gated: each render sounds, and
// the measure itself works (the band-limited Saw source reads < -60 dB, a naive saw > -40 dB). Group "engine".

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    /** Energy more than 10 Hz from every multiple of f0 (from 20 Hz up), in dB of the total, over 2^order samples. */
    double offLinesDb (const std::vector<float>& x, double sr, double f0, std::int64_t start, int order)
    {
        const std::vector<double> m = grains::spectrum (x, start, order);
        const double bin = sr / static_cast<double> (std::int64_t { 1 } << order);
        double total = 0, off = 0;
        for (auto i = static_cast<std::size_t> (std::ceil (20 / bin)); i < m.size(); ++i)
        {
            const double hz = static_cast<double> (i) * bin;
            const double k = std::max (1.0, std::floor (hz / f0 + 0.5));
            const double p = m[i] * m[i];
            total += p;
            if (std::abs (hz - k * f0) > 10)
                off += p;
        }
        return 10 * std::log10 (off / total + 1e-30);
    }
}

SL_TEST ("engine", tapeAliasingFigure)
{
    section ("aliasing: Tape on the Saw at +0, +12, +24 and +36 st (48 kHz): the energy off the harmonic lines, printed (open question 6)");

    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    const double sr = 48000;
    const int steps[] = { 0, 12, 24, 36 };
    const double proto[] = { -42.13, -27.77, -19.58, -15.10 };
    double figure[4] = { 0, 0, 0, 0 };
    for (int q = 0; q < 4; ++q)
    {
        core::Inst inst = grains::instWith (core::Engine::tape);
        grains::Play play (sr, inst, engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw"));
        play.e->noteOn (60 + steps[q], 1);
        const auto start = static_cast<std::int64_t> (0.5 * sr);
        play.render (start + 65536);
        figure[q] = offLinesDb (play.L, sr, signals::midiHz (60 + steps[q]), start, 16);
        double s = 0;
        for (const float v : play.L)
            s += static_cast<double> (v) * v;
        const double rms = std::sqrt (s / static_cast<double> (play.L.size()));
        check (rms > 0.01, fmt ("+%d st: the note sounds (rms %.3f)", steps[q], rms));
        note (fmt ("+%d st: %.2f dB of the energy is off the harmonic lines (the prototype %.2f dB)", steps[q], figure[q], proto[q]));
    }
    note (fmt ("In plain words: played three octaves up (+36 st), the Tape engine turns the Saw into a note where %.1f %% of the sound "
               "(%.1f dB) is aliasing, tones that are not part of the note; one octave up it is %.1f %%, at the recorded pitch %.3f %%.",
               100 * std::pow (10.0, figure[3] / 10), figure[3], 100 * std::pow (10.0, figure[1] / 10), 100 * std::pow (10.0, figure[0] / 10)));

    // The measure itself: the band-limited Saw source reads very low, a naive (aliased) saw high.
    const double source = offLinesDb (saw.left, saw.sampleRate, signals::midiHz (60), 0, 16);
    std::vector<float> naive (1 << 16);
    const double f = signals::midiHz (60);
    for (std::size_t i = 0; i < naive.size(); ++i)
    {
        const double ph = std::fmod (static_cast<double> (i) * f / 44100, 1.0);
        naive[i] = static_cast<float> (1 - 2 * ph);
    }
    const double aliased = offLinesDb (naive, 44100, f, 0, 16);
    check (source < -60 && aliased > -40,
           fmt ("the measure works: the Saw source itself %.1f dB (< -60; the prototype -104.1), a naive saw %.1f dB (> -40)", source, aliased));
}
