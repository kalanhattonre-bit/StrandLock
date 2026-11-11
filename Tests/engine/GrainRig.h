#pragma once

// The grain engines' test rows (M3 part G) follow prototype-v4/tests/test-engine.js line by line, so they use its
// measurements and its way of driving the engine. This rig holds both:
//  - the measurements: spectrum (Blackman-Harris), harmonicF0 (the harmonic-refined f0), centroid (power-weighted,
//    below 12 kHz), corr, stats (min, median, max of the finite values, the JS quantile rule), cents;
//  - a Play: a fresh Engine with an instrument and a sample (mkEngine), rendered in host blocks (render) with a
//    hook after every block (the JS hook (i)), live keys and a raw snapshot (rig::rawSnapshot) as the JS tests
//    build them.
// The FFT is the suite's own (measure::fft). Group "engine" tests only.

#include "engine/EngineRender.h"
#include "engine/VoiceRig.h"

#include "core/Inst.h"
#include "core/Snapshot.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "engine/SampleData.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace sl::test::grains
{
    /** spectrum (x, start, order): the Blackman-Harris magnitude spectrum of x[start .. start + 2^order) (samples
        past the end read 0), 2^(order - 1) bins. */
    std::vector<double> spectrum (const std::vector<float>& x, std::int64_t start, int order);

    struct F0
    {
        double f0 = 0;
        int used = 0;   // harmonics that went into the estimate
    };

    /** harmonicF0 (x, sr, fExp, order, start, maxK): every harmonic k f of the spectrum within +-60 cents of k fExp
        and within 40 dB of the strongest gives f_k / k; the magnitude-weighted mean is the estimate. start < 0 =
        the last 2^order samples. */
    F0 harmonicF0 (const std::vector<float>& x, double sr, double fExp, int order = 16, std::int64_t start = -1, int maxK = 12);

    /** centroid (x, sr, order, start, fMax): the power-weighted mean frequency of the spectrum below fMax.
        start < 0 = the last 2^order samples. */
    double centroid (const std::vector<float>& x, double sr, int order = 15, std::int64_t start = -1, double fMax = 12000);

    /** corr (a, b): sum ab / sqrt (sum aa sum bb). */
    double corr (const float* a, const float* b, std::size_t n);

    /** 1200 log2 (f / ref). */
    double cents (double f, double ref);

    /** The JS stats (): the finite values sorted; med = a[floor (0.5 (n - 1))]. */
    struct Stats
    {
        int n = 0;
        double min = 0, med = 0, max = 0;
    };
    Stats stats (std::vector<double> values);

    /** The largest |a[i] - b[i]| (0 = bit for bit). */
    double largestDifference (const std::vector<float>& a, const std::vector<float>& b);

    /** One engine, as the JS tests' mkEngine: prepare (sr), ramps 0 ms, the instrument, the sample. */
    struct Play
    {
        explicit Play (double sampleRate, const core::Inst& inst, std::unique_ptr<engine::SampleData> sample);

        /** setSnapshot (a raw snapshot, epoch 1); kept alive here. */
        void snapshot (std::unique_ptr<core::Snapshot> snap);

        /** setTransport ({ playing: true, bpm, beat, loop }). */
        void play (double bpm, double beat, bool loop);

        /** Renders n more samples in blocks of `block`, appending to L and R; after each block, hook (samples
            rendered so far) (the JS render's hook). */
        void render (std::int64_t n, int block = 128, const std::function<void (std::int64_t)>& hook = {});

        /** fillDisplay into a frame owned here, and that frame. */
        const engine::DisplayFrame& display();

        /** decodeAt (out, i, w) on what was rendered (the ramp source's read position). */
        double decodeAt (std::int64_t i, std::int64_t w) const;

        std::unique_ptr<engine::Engine> e;
        std::unique_ptr<engine::SampleData> sample;
        std::unique_ptr<core::Snapshot> snap;
        std::unique_ptr<engine::DisplayFrame> frame;
        std::vector<float> L, R;
    };

    /** The JS tests' mkInst (patch) is core::defaultInst () with fields set; this is defaultInst with an engine. */
    core::Inst instWith (core::Engine engine);

    /** A mono sample at a rate. */
    std::unique_ptr<engine::SampleData> mono (std::vector<float> x, double sampleRate);

    /** The ramp source (rampSource (secs, sr)) as a sample. */
    std::unique_ptr<engine::SampleData> ramp (double secs, double sampleRate);

    /** One raw note as the JS tests' mkNote ({ id, p, s, l, engine }): every other setting at mkNote's default
        (Voice is mkNote's default engine; pass it explicitly). */
    rig::RawNote rawNote (const char* id, double p, double s, double l, core::Engine engine);
}
