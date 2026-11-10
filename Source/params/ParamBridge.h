#pragma once

// Between the parameters and the instrument (PLUGIN-PLAN 2.4). readInst runs on the audio thread at the
// start of every block: lock-free atomic loads only, no allocation. A non-finite host value falls back to the
// default (GrainLock's readParameters), a choice index is rounded and kept in its list, and the result goes
// through core::clampForEngine. applyPatch goes the other way, on the message thread: an InstPatch (a sound
// load, a sound preset, a recipe, Dice, Detect, Undo) sets the parameters it names as host-visible edits.

#include "ParamIds.h"
#include "core/Inst.h"
#include "state/StateChunks.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace juce
{
    class AudioProcessorValueTreeState;
}

namespace sl::params
{
    /** Where the audio thread reads each parameter's plain value (a choice's index, a switch's 0 or 1). */
    struct ParameterRefs
    {
        std::array<const std::atomic<float>*, kNumParams> values {};
    };

    /** The plain-value atomics of the 50 parameters: FloatParameter's own value, the APVTS value for the others. */
    ParameterRefs makeParameterRefs (juce::AudioProcessorValueTreeState& apvts);

    /** The INST fields that are not parameters (PLUGIN-PLAN 2.2), as the audio thread reads them. The document
        (the INST chunk, M0's state) writes them on the message thread. */
    struct NonParamAtomics
    {
        std::atomic<std::int32_t> seed { 1 };
        std::atomic<std::int8_t> mod1Target { static_cast<std::int8_t> (core::LaneId::vib) };
        std::atomic<std::int8_t> mod2Target { static_cast<std::int8_t> (core::LaneId::tone) };
        std::atomic<std::int8_t> mod1Cc { 1 };
        std::atomic<std::int8_t> mod2Cc { 2 };
        std::atomic<std::int8_t> key { 0 };
        std::atomic<std::uint8_t> scale { 0 };
        std::atomic<bool> scaleLock { false };
        std::atomic<double> regionStartExact { std::numeric_limits<double>::quiet_NaN() };   // NaN = none
        std::atomic<double> regionEndExact { std::numeric_limits<double>::quiet_NaN() };

        static_assert (std::atomic<double>::is_always_lock_free, "the audio thread reads the Region edges lock-free");
    };

    /** The instrument the parameters and the non-parameter state describe now (audio thread, every block).
        A Region edge plays its exact f64 value while the parameter still holds (float) of it, and the
        parameter's own value once something (host automation) moved it (PLUGIN-PLAN 1.6 #12, 2.2). */
    core::Inst readInst (const ParameterRefs& refs, const NonParamAtomics& state) noexcept;

    /** The host bypass switch (parameter 49), which is not part of the instrument. */
    bool readBypass (const ParameterRefs& refs) noexcept;

    //==============================================================================================
    /** One parameter's new plain value (its index in the frozen order). */
    using ParamChange = std::pair<int, float>;

    /** The plain value the patch gives each parameter it names, in parameter order, made legal (clamped, a
        choice's index, a switch's 0 or 1, a whole number rounded). Nothing is set. The INST-state fields (seed,
        the wheels, key, scale, scale lock) are not parameters and are not in the list. */
    std::vector<ParamChange> patchValues (const core::InstPatch& patch);

    /** Message thread (PLUGIN-PLAN 2.4): sets every parameter of patchValues (patch) whose value differs, each
        in a gesture of its own (beginChangeGesture, the new value notifying the host, endChangeGesture), as
        GrainLock's applyPreset does, so Cubase records and shows them as edits. A float parameter takes the
        exact float (FloatParameter keeps it). Returns the parameters it changed, with their new values. */
    std::vector<ParamChange> applyPatch (juce::AudioProcessorValueTreeState& apvts, const core::InstPatch& patch);

    /** The patch's INST-state fields written into s (plan 2.2: they go to the document, not to parameters): the
        seed, each wheel's target (its lane id, or "off") and CC, the key, the scale's name and the scale lock.
        True when the patch named any of them. */
    bool applyStateFields (state::InstState& s, const core::InstPatch& patch);
}
