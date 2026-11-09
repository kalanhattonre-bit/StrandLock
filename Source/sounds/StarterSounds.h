#pragma once

// The 8 built-in starter sounds (prototype-v4/src/sl-sounds.js, SLSounds), made in code so nothing goes
// missing on another computer. A line-by-line port: the same operation order and constants, double maths,
// one rounding to float32 in finish (PLUGIN-PLAN 1.2, 1.6). Every sound is stereo, 44.1 kHz, peak 0.8, and
// loops seamlessly. Plain C++ (StrandCore): no JUCE.
//
// SLSounds has its own hash32 (0x9e3779b9 chained through fmix32) and noiseAt. They are not the model's
// MurmurHash (core/Seeds), so they live here.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace sl::sounds
{
    enum class StarterId : std::uint8_t { sawopen, saw, square, pad, vowel, pluck, beat, noise };

    inline constexpr int kNumStarters = 8;

    struct StarterInfo
    {
        StarterId id;
        const char* code;   // the prototype's id, as saved in SMPL: "sawopen"
        const char* name;   // "Saw that opens up"
        bool pitched;       // SLSounds.PITCHED: rooted at C3 and exactly periodic
    };

    /** SLSounds.list in its order: the Saw that opens up first (D9). */
    inline constexpr StarterInfo kStarterList[kNumStarters] = {
        { StarterId::sawopen, "sawopen", "Saw that opens up", true },
        { StarterId::saw,     "saw",     "Saw",               true },
        { StarterId::square,  "square",  "Square sweep",      true },
        { StarterId::pad,     "pad",     "Evolving pad",      true },
        { StarterId::vowel,   "vowel",   "Vowel aah to ooh",  true },
        { StarterId::pluck,   "pluck",   "Pluck arpeggio",    false },
        { StarterId::beat,    "beat",    "Beat loop",         false },
        { StarterId::noise,   "noise",   "Noise",             false },
    };

    /** SLSounds.defaultId. */
    inline constexpr StarterId kDefaultStarter = StarterId::sawopen;

    inline constexpr double kStarterSampleRate = 44100.0;
    inline constexpr int kStarterRootMidi = 60;   // C3 in Cubase naming

    const StarterInfo& starterInfo (StarterId id) noexcept;

    /** The id for a saved string ("saw"), or nothing for a string this version does not know. */
    std::optional<StarterId> parseStarter (std::string_view code) noexcept;

    /** One made sound: SLSounds.make's { sampleRate, channels: [L, R], rootMidi }. */
    struct StarterSound
    {
        StarterId id = kDefaultStarter;
        std::int32_t seed = 1;
        double sampleRate = kStarterSampleRate;
        int rootMidi = kStarterRootMidi;
        std::vector<float> left, right;

        std::size_t frames() const noexcept { return left.size(); }
        double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (left.size()) / sampleRate : 0.0; }
    };

    /** SLSounds.make (id, { seed }). Allocates and takes tens of milliseconds: never on the audio thread. */
    StarterSound makeStarterSound (StarterId id, std::int32_t seed = 1);

    /** The same sound shared: one copy per (id, seed) while anything still holds it. Thread-safe (a mutex),
        message and worker threads only. */
    std::shared_ptr<const StarterSound> sharedStarterSound (StarterId id, std::int32_t seed = 1);

    //==============================================================================================
    // SLSounds' own seeded hashes (exposed for the tests).

    /** MurmurHash3's finaliser, as SLSounds.fmix32. */
    std::uint32_t fmix32 (std::uint32_t h) noexcept;

    /** SLSounds.hash32 (a, b, ...): each argument is the int32 the JavaScript `x | 0` makes, passed as its bits. */
    std::uint32_t hash32 (std::initializer_list<std::uint32_t> args) noexcept;

    /** SLSounds.strHash: FNV-1a over the string's code units (the ids are ASCII). */
    std::uint32_t strHash (std::string_view s) noexcept;

    /** SLSounds.noiseAt (base, i): seeded white noise in [-1, 1). */
    double noiseAt (std::uint32_t base, std::uint32_t i) noexcept;
}
