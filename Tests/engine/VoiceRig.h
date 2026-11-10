#pragma once

// A test rig that drives one engine::Voice directly, without the Engine (M2 E1: the engine's scheduler, planner and
// output stage come in part 2). It owns what a voice reads from its engine (a VoiceContext, a Clock, the
// instrument, the sample, the snapshot) and renders the voice into Float32 buffers the way Engine::process calls
// it. Also: raw snapshots built the way the prototype tests' mkNote / mkSnap / chainNotes build them (dense lane
// tables, every setting explicit), so a test can follow a JS test line by line, and the signal helpers the JS
// tests use (maxStep, decodeAt).

#include "core/Inst.h"
#include "core/Snapshot.h"
#include "engine/Clock.h"
#include "engine/SampleData.h"
#include "engine/Voice.h"
#include "support/TestSignals.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sl::test::rig
{
    /** One lane of a raw note: unset (the lane default), a constant, or a function of u in 0..1. */
    struct LaneSpec
    {
        bool set = false;
        double value = 0;
        std::function<double (double)> fn;
    };

    /** The JS tests' mkNote (o): every setting explicit (no "follow the instrument" markers). */
    struct RawNote
    {
        std::string id;
        double p = 60, s = 0, l = 1, vel = 1;
        core::Engine engine = core::Engine::tape;
        bool lock = false;
        core::Motion motion = core::Motion::forward;
        std::uint8_t travel = 0;   // 0 speed, 1 path
        core::StartSnap start { 0, 0.0, 0.0, 0 };
        bool tape = false, rev = false;
        double jump = 0.5;
        bool restart = false;
        bool hasGlide = false;
        double glideD = 0, glideLen = 0, glideCurve = 0;
        std::string glideTo;
        std::array<LaneSpec, core::kNumLanes> lanes {};
        int res = 256;
    };

    /** mkSnap (notes, bars): the notes in the given order, each lane a dense table of max (2, ceil (l res) + 1)
        Float32 values. chained = chainNotes (notes): one chain in order. The glide and the chain are the same in all
        three Auto Slide modes. */
    std::unique_ptr<core::Snapshot> rawSnapshot (const std::vector<RawNote>& notes, int bars, bool chained);

    /** A sample from a signal's channels (one channel = mono). */
    std::unique_ptr<engine::SampleData> sampleOf (const signals::Signal& s);
    std::unique_ptr<engine::SampleData> sampleOf (std::vector<float> mono, double sampleRate);

    /** The JS tests' maxStep (x, a, b): the largest |x[i] - x[i - 1]| for i in max (1, a) .. min (n, b). */
    double maxStep (const std::vector<float>& x, std::int64_t a, std::int64_t b);

    /** decodeAt (out, i, w): the ramp source's read position from L and R summed over i - w .. i + w. */
    double decodeAt (const std::vector<float>& L, const std::vector<float>& R, std::int64_t i, std::int64_t w = 16);

    /** rms (x, a, b). */
    double rms (const std::vector<float>& x, std::int64_t a, std::int64_t b);

    /** FNV-1a over L then R (the fixtures' audio hash). */
    std::uint32_t hashOf (const std::vector<float>& L, const std::vector<float>& R);

    class VoiceRig
    {
    public:
        explicit VoiceRig (double sampleRate = 48000.0);

        /** The engine's setInst: the engine's own clamps. */
        void setInst (const core::Inst& i);

        void setSample (std::unique_ptr<engine::SampleData> sample);

        /** setSnapshot: Lb = max (1, bars) x max (1, beatsPerBar). */
        void setSnapshot (std::unique_ptr<core::Snapshot> snapshot);

        /** setTransport ({ playing: true, bpm, beat, loop }) at sample 0. */
        void play (double bpm, double beat, bool loop);

        /** A live key at sample k0 (the engine's noteOn: new Voice, addVoice's first tick). */
        engine::Voice& live (int p, double vel, std::int64_t k0 = 0);

        /** A clip voice for snapshot note index at its start (startClip / addVoice), or chased to kNow (doChase:
            chase, ffChain, then addVoice's first tick at kNow) when chaseTo is given. */
        engine::Voice& clip (std::int32_t index, std::int64_t pass = 0);
        engine::Voice& clipChased (std::int32_t index, std::int64_t kNow, std::int64_t* ticks = nullptr);

        /** A second voice object (for comparisons), started like clip (). */
        std::unique_ptr<engine::Voice> newClipVoice (std::int32_t index, std::int64_t pass = 0);

        /** Renders samples [from, to) of the voice into L and R in blocks, calling before (k) at the start of each
            block (and at every extra split point in splits). */
        void render (engine::Voice& v, std::vector<float>& L, std::vector<float>& R, std::int64_t from, std::int64_t to, int block,
                     const std::vector<std::int64_t>& splits = {}, const std::function<void (std::int64_t)>& before = {});

        engine::Clock clock;
        engine::VoiceContext ctx;
        core::Inst inst = core::defaultInst();
        std::unique_ptr<engine::SampleData> sample;
        engine::VoiceSource source;
        std::unique_ptr<core::Snapshot> snap;
        std::unique_ptr<engine::Voice> voice;
        std::uint64_t serial = 0;
    };
}
