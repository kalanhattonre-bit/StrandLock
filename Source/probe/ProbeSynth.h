#pragma once

// The M0 probe voice (PLUGIN-PLAN 4, M0): plays the loaded starter (the Saw) from MIDI so Kalan can check
// tuning, chords, bends and the host's blocks in Cubase before the real engine exists. Deleted in M2.
//
//   - up to `polyphony` voices (16 by default), each a 4-point Hermite read of the sound at
//     2^((p - root + bend + pitch) / 12) x soundRate / sampleRate (with the default root 60 and no bend:
//     2^((p - 60) / 12) x 44100 / sr), looping over the whole sound (the starters loop seamlessly);
//   - 8 ms attack, 60 ms release, 5 ms steal (the oldest sounding voice when a new key needs room);
//   - velocity gain 1 - velSens x (1 - vel), at the Volume lane's default level (0.8, squared);
//   - Output Gain and the host bypass, each ramped over 20 ms per sample (PLUGIN-PLAN 2.3);
//   - the engine's output stage: a soft limiter (instant attack, 80 ms release, ceiling 0.9), a guard that turns a
//     non-finite or huge sample into silence (and counts it), and a clamp at 0.98.
//
// Everything runs per sample and every event lands on its sample, so the output does not depend on how the
// host splits its blocks. No allocation, lock or system call after prepare().

#include "core/Inst.h"
#include "sounds/StarterSounds.h"

#include <array>
#include <cstdint>

namespace sl::probe
{
    class ProbeSynth
    {
    public:
        static constexpr int kVoices = 64;          // the pool: sounding voices plus their release and steal tails
        static constexpr int kMaxPolyphony = 32;

        ProbeSynth() noexcept { prepare (48000.0); }

        /** Message thread, before playing. Allocates nothing. */
        void prepare (double sampleRate) noexcept;

        /** Silences everything at once (all voices off, ramps and limiter at rest). */
        void reset() noexcept;

        /** The sound to play (nullptr = silence). A different sound stops every voice at once (M0's probe has no
            steal tail across a sound change). The sound must stay alive while it is set. */
        void setSound (const sounds::StarterSound* sound) noexcept;

        /** The instrument for this block (velSens, outGain, polyphony, root, cents, pitch, bendRange). */
        void setInst (const core::Inst& inst) noexcept;

        void setBypass (bool bypassed) noexcept;

        // MIDI, applied at the sample where it happens (the caller renders up to it first).
        void noteOn (int note, int velocity) noexcept;   // velocity 1..127 (0 is a note off)
        void noteOff (int note) noexcept;
        void sustain (bool down) noexcept;
        void allNotesOff() noexcept;                     // CC123: every voice releases (60 ms)
        void allSoundOff() noexcept;                     // CC120: every voice fades out in 5 ms
        void pitchBend (int value14) noexcept;           // 0..16383, 8192 = centre

        /** Renders n samples into left and right (overwriting them). right may be nullptr (mono). */
        void render (float* left, float* right, int n) noexcept;

        struct Stats
        {
            long long nonFinite = 0;      // samples the output guard silenced (counted before it, so a NaN cannot pass as silence)
            double preLimiterPeak = 0.0;  // the largest |sample| before the limiter (after Output Gain)
            int maxSounding = 0;          // the most voices that sounded at once
            long long steals = 0;
        };

        const Stats& stats() const noexcept { return st; }
        void resetStats() noexcept { st = {}; }

        /** Voices sounding now (attack, held, released or stealing). */
        int activeVoices() const noexcept;

    private:
        enum class Stage : std::uint8_t { off, attack, hold, release, steal };

        struct Voice
        {
            Stage stage = Stage::off;
            int note = 0;
            bool keyDown = false;       // the key is still held
            bool sustained = false;     // let go while the sustain pedal was down
            double pos = 0.0;           // read position, in frames of the sound
            double rate = 1.0;          // frames per output sample
            double gain = 0.0;          // velocity gain x level
            double env = 0.0;
            double envStep = 0.0;       // per sample (attack up, release and steal down)
            std::uint64_t age = 0;      // start order: the oldest is stolen first
        };

        void startVoice (Voice& v, int note, double velocity) noexcept;
        void releaseVoice (Voice& v) noexcept;
        void stealVoice (Voice& v) noexcept;
        double rateFor (int note) const noexcept;
        void updateRates() noexcept;

        std::array<Voice, kVoices> voices {};
        const sounds::StarterSound* sound = nullptr;
        double sr = 48000.0;
        std::uint64_t nextAge = 0;

        // settings
        double velSens = 0.7, root = 60.0, pitch = 0.0, bendRange = 2.0;
        int polyphony = 16;
        double bend = 0.0;              // -1 .. +1
        bool sustainDown = false;

        // output stage
        double gainTarget = 1.0, gainNow = 1.0, gainStep = 0.0;
        int gainSteps = 0;
        double bypassTarget = 1.0, bypassNow = 1.0;
        double limG = 1.0, limRel = 0.0;
        bool firstBlock = true;

        Stats st;
    };
}
