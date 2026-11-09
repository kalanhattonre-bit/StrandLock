#pragma once

// Lane values in real units and as readout text (BLUEPRINT 1.2, 1.3; sl-model.js "lane values to real
// units"). Readouts use jsToFixed and jsNumberToString, so they are character for character the prototype's.

#include "Clip.h"

#include "core/Inst.h"
#include "core/Lanes.h"

#include <cstdint>
#include <string>

namespace sl::model
{
    /** What a readout needs besides the value (SLApp.formatCtx): the instrument (normalized by every call, as
        the prototype does), the tempo (120 when not finite; clamped 1..1000) and the note's Travel kind. */
    struct LaneCtx
    {
        core::Inst inst = core::defaultInst();
        double bpm = 120.0;
        TravelKind travelKind = TravelKind::speed;
    };

    /** SLApp.formatCtx (note): the context for a note's readouts (no note: Speed kind). */
    LaneCtx formatCtx (const core::Inst& inst, double bpm, const Note* note) noexcept;

    enum class GrainUnit : std::uint8_t { cycles, ms, wavelengths, x };

    struct GrainBase
    {
        double base;
        GrainUnit unit;
    };

    /** SLModel.grainBase: the grain size the Grain lane scales, in the engine's unit (Tape: the bare 1x). */
    GrainBase grainBase (const core::Inst& inst) noexcept;

    /** SLModel.mapLane: lane value -> real value. pitch and formant semitones (v - 0.5) * 24, amp gain v^2 * 1.3,
        vib semitones, tone Hz 60 * 2^(8.3 v), hollow dB -28 v, travel the signed speed of the Float32 value
        (Speed kind) or 0..1 of the Region (Path kind), heads the count, grain in the engine's unit, refresh
        the period in ms (0 = Smooth). A non-finite v reads as the lane default. Macros, offsets and wheels
        are live and not included. */
    double mapLane (core::LaneId lane, double v, const LaneCtx& ctx);

    /** SLModel.formatLane: '+3 st', '0.5x backwards', '2.4 kHz', '-6.0 dB', 'Refresh 1/16 (125 ms at 120 bpm)'. */
    std::string formatLane (core::LaneId lane, double v, const LaneCtx& ctx);

    /** The prototype's small readout helpers. */
    std::string trimNum (std::string s);     // '1.50' -> '1.5', '2.0' -> '2' (only when there is a point)
    std::string fmtMag (double x);           // 0 digits from 10, 1 from 1, else 2, trimmed
    std::string fmtSemis (double x);         // '+3 st', '-0.5 st', '0 st'
    std::string fmtDb (double db);           // '+1.5 dB', '-6.0 dB', '0.0 dB', '-inf dB'
}
