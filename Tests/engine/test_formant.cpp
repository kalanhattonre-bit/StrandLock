// The Voice formant (PLUGIN-PLAN 4, M3 part G; PROMPT 14.2), as prototype-v4/tests/test-engine.js measures it: a
// live C3 on the formant tone (partials under a formant peak at 1 kHz, 350 Hz wide), the formant offset +6 st
// (offsets.formant +0.25) and -6 st (-0.25) against the centre: +6 raises the spectral centroid by more than 25 %
// and -6 lowers it by more than 10 %, f0 within +-3 cents in both (Voice moves the formant by the grains' read
// speed, never the pitch, which the grain spacing sets). Centroid: power-weighted below 12 kHz over the last 2^15
// samples of a 2 s render; f0: harmonicF0 over the last 2^16. Group "engine".

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"

#include <cmath>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    struct Measured
    {
        double centroid = 0, cents = 0;
    };

    Measured voiceC3 (const std::vector<float>& src, double formantOffset)
    {
        const double sr = 48000;
        core::Inst inst = grains::instWith (core::Engine::voice);
        inst.offsets.formant = formantOffset;
        grains::Play play (sr, inst, grains::mono (src, sr));
        play.e->noteOn (60, 1);
        play.render (static_cast<std::int64_t> (2.0 * sr));
        Measured m;
        m.centroid = grains::centroid (play.L, sr, 15);
        m.cents = grains::cents (grains::harmonicF0 (play.L, sr, signals::midiHz (60), 16).f0, signals::midiHz (60));
        return m;
    }
}

SL_TEST ("engine", voiceFormantShift)
{
    section ("Voice formant: +6 st raises the centroid > 25 % and -6 st lowers it > 10 %, f0 within +-3 cents in both");

    const std::vector<float> src = signals::formantTone (signals::midiHz (60), 6, 48000, 1000, 350);
    const Measured centre = voiceC3 (src, 0), up = voiceC3 (src, 0.25), down = voiceC3 (src, -0.25);
    const double rise = up.centroid / centre.centroid - 1, fall = down.centroid / centre.centroid - 1;
    check (rise > 0.25, fmt ("formant +6 st: the centroid rises %+.1f %% (%.0f -> %.0f Hz; > +25 %%)", rise * 100, centre.centroid, up.centroid));
    check (std::abs (up.cents) <= 3.0, fmt ("formant +6 st: f0 %+.4f cents (+-3)", up.cents));
    check (fall < -0.1, fmt ("formant -6 st: the centroid falls %+.1f %% (%.0f -> %.0f Hz; < -10 %%)", fall * 100, centre.centroid, down.centroid));
    check (std::abs (down.cents) <= 3.0, fmt ("formant -6 st: f0 %+.4f cents (+-3)", down.cents));
    check (std::abs (centre.cents) <= 3.0, fmt ("the centre: f0 %+.4f cents (+-3)", centre.cents));

    // A no-op is not a pass: the measurement is steady, so the rise and fall are the formant's. The centre rendered
    // again with an offset of 0.002 (0.05 st) moves the centroid by well under 2 %.
    const Measured near = voiceC3 (src, 0.002);
    const double drift = near.centroid / centre.centroid - 1;
    check (std::abs (drift) < 0.02, fmt ("negative control: a 0.05 st formant offset moves the centroid %+.2f %% (under 2 %%)", drift * 100));
}
