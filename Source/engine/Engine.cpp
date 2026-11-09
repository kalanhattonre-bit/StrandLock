// The M2 contract's stub Engine (PLUGIN-PLAN 7.4): it keeps the clock, the inputs and the stats a real engine
// would, renders silence and makes no voices. The engine chain replaces this file with the port of
// sl-engine.js; until then the plugin chain can build and test against the header. It allocates nothing (the
// real prepare allocates the voice pool, and only there).

#include "engine/Engine.h"

#include "rt/VersionExchange.h"

#include <algorithm>
#include <cmath>

namespace sl::engine
{
    void Engine::VoicePoolDeleter::operator() (Voice* pool) const noexcept
    {
        // The stub never makes a pool. The engine chain deletes it here (delete[] pool) once Voice is complete.
        (void) pool;
    }

    Engine::Engine() noexcept
    {
        st.budget = budgetFor (sr);
    }

    Engine::~Engine() = default;

    void Engine::prepare (double rate)
    {
        sr = std::isfinite (rate) ? std::clamp (rate, kMinSampleRate, kMaxSampleRate) : kDefaultSampleRate;
        k = 0;
        snap = nullptr;
        snapEpoch = 0;
        src = nullptr;
        srcEpoch = 0;
        transport = {};
        st = {};
        st.budget = budgetFor (sr);
        peakL = peakR = 0.0f;
    }

    void Engine::forgetVersions() noexcept
    {
        snap = nullptr;
        snapEpoch = 0;
        src = nullptr;
        srcEpoch = 0;
    }

    void Engine::reset() noexcept
    {
        peakL = peakR = 0.0f;
    }

    void Engine::setSample (const SampleData* sample, std::uint64_t epoch) noexcept
    {
        src = sample;
        srcEpoch = sample != nullptr ? epoch : 0;
    }

    void Engine::setInst (const core::Inst& next) noexcept
    {
        inst = core::clampForEngine (next);
    }

    void Engine::setSnapshot (const core::Snapshot* snapshot, std::uint64_t epoch) noexcept
    {
        snap = snapshot;
        snapEpoch = snapshot != nullptr ? epoch : 0;
    }

    void Engine::setTransport (const Transport& next) noexcept
    {
        transport.playing = next.playing;
        transport.loop = next.loop;
        transport.beat = next.beat;
        if (std::isfinite (next.bpm))
            transport.bpm = std::clamp (next.bpm, 10.0, 999.0);
    }

    void Engine::noteOn (double, double, const NoteOnOpts&) noexcept {}
    void Engine::noteOff (double) noexcept {}
    void Engine::allNotesOff() noexcept {}
    void Engine::setController (Ctrl, double) noexcept {}
    void Engine::releaseController (Ctrl) noexcept {}

    void Engine::process (float* L, float* R, int n) noexcept
    {
        if (n <= 0)
            return;
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        k += n;
    }

    void Engine::fillDisplay (DisplayFrame& frame) noexcept
    {
        frame.sampleTime = k;
        frame.beat = std::isfinite (transport.beat) ? transport.beat : 0.0;
        frame.playing = transport.playing;
        frame.numVoices = 0;
        frame.numGrains = 0;
        frame.stats = { st.voices, st.grains, st.thinnedHeads, st.maxGrains, st.budget, static_cast<std::int32_t> (st.cuts),
                        static_cast<std::int32_t> (st.steals) };
        frame.peakL = peakL;
        frame.peakR = peakR;
        peakL = peakR = 0.0f;
    }

    Stats Engine::stats() const noexcept
    {
        return st;
    }

    void Engine::resetStats() noexcept
    {
        st.maxGrains = 0;
        st.cuts = 0;
        st.steals = 0;
        st.deferredStarts = 0;
        st.chaseTicks = 0;
        st.nonFinite = 0;
        st.filterResets = 0;
        st.preLimiterPeak = 0.0;
    }

    std::uint64_t Engine::minEpochInUse (VersionKind kind) const noexcept
    {
        const std::uint64_t e = kind == VersionKind::snapshot ? snapEpoch : srcEpoch;
        return e != 0 ? e : rt::kNoEpoch;
    }

    void Engine::setRampMs (double ms) noexcept
    {
        rampMs = std::isfinite (ms) && ms > 0.0 ? ms : 0.0;
    }

    void Engine::setBypass (bool on) noexcept
    {
        bypassed = on;
    }

    void Engine::snapRamps() noexcept {}
}
