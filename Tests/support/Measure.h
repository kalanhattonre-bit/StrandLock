#pragma once

// Measurements on rendered audio (PLUGIN-PLAN 1.1, Tests/support): the fundamental by FFT and by
// autocorrelation, cents, RMS, peak, the largest sample-to-sample step and the spectral centroid. Plain C++,
// its own radix-2 FFT (the plugin has no juce_dsp). Owned by M0.

#include <complex>
#include <cstddef>
#include <vector>

namespace sl::test::measure
{
    /** In-place radix-2 FFT; the size must be a power of two. */
    void fft (std::vector<std::complex<double>>& a);

    /** The fundamental of the last 2^order samples of x: Blackman-Harris window, FFT, then the lowest spectral
        peak above 20 Hz whose magnitude is within `withinDb` of the strongest one, refined by parabolic
        interpolation on the dB magnitudes (GrainLock's method). 0 when x is too short. */
    double fftFundamentalHz (const std::vector<float>& x, double sampleRate, int order, double withinDb = 12.0);

    /** The fundamental by normalised autocorrelation over the last part of x (GrainLock's method). */
    double autocorrFundamentalHz (const std::vector<float>& x, double sampleRate, double fMin, double fMax);

    /** 1200 log2 (measured / expected); a huge number when measured is not positive. */
    double centsBetween (double measured, double expected);

    double rms (const float* x, std::size_t n);
    double peak (const float* x, std::size_t n);

    /** The largest |x[i] - x[i-1]|. */
    double largestStep (const float* x, std::size_t n);

    /** The magnitude-weighted mean frequency of the last 2^order samples. */
    double centroidHz (const std::vector<float>& x, double sampleRate, int order);
}
