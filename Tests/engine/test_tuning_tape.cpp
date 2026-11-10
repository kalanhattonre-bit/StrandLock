// Tape tuning (PLUGIN-PLAN 4, M2; PROMPT 14.1 in v4 naming): MIDI 36, 60 and 84 (C1, C3, C5) against a C3 root, at
// 44.1, 48 and 96 kHz, and a 44.1 kHz sample in a 48 kHz session (F2: the rate ratio is applied). The source is the
// prototype tests' tone at C3 (12 partials), long enough that no Region seam falls in the measured stretch. Each
// note is a live key on the Engine; f0 is measured by FFT on the last 2^16 samples of a 2.4 s render and must be
// within +-3 cents. Group "engine".

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/Measure.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    /** The C3 tone recorded at a rate: 10 s, so 2.4 s at 4x reads no Region seam. Made once per rate. */
    const std::vector<float>& c3Tone (double rate)
    {
        static std::map<double, std::vector<float>> made;
        auto it = made.find (rate);
        if (it == made.end())
            it = made.emplace (rate, signals::tone (signals::midiHz (60), 10.0, rate)).first;
        return it->second;
    }

    /** f0 of a live Tape key p played on the C3 tone recorded at sourceRate and declared as declaredRate, in a session
        at sessionRate. */
    double playedF0 (double sessionRate, double sourceRate, double declaredRate, int p)
    {
        const double secs = 2.4;
        auto e = std::make_unique<engine::Engine>();
        e->prepare (sessionRate);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        e->setInst (inst);
        auto sample = render::monoSample (c3Tone (sourceRate), declaredRate);
        e->setSample (sample.get(), 1);
        render::Script script;
        script.at (0, [p] (engine::Engine& en) { en.noteOn (p, 1.0); });
        std::vector<float> L, R;
        script.run (*e, static_cast<std::int64_t> (secs * sessionRate), render::fixedBlocks (256), L, R);
        return measure::fftFundamentalHz (L, sessionRate, 16);
    }
}

SL_TEST ("engine", tapeTuning)
{
    section ("Tape tuning: MIDI 36 / 60 / 84 against a C3 root at 44.1, 48 and 96 kHz, and a 44.1 kHz sample at 48 kHz (+-3 cents)");

    struct Case
    {
        double session, source;
    };
    const Case cases[] = { { 44100, 44100 }, { 48000, 48000 }, { 96000, 96000 }, { 48000, 44100 } };
    const int keys[] = { 36, 60, 84 };
    int ok = 0, total = 0;
    double worst = 0;
    for (const Case& c : cases)
        for (const int p : keys)
        {
            const double expected = signals::midiHz (p);
            const double f0 = playedF0 (c.session, c.source, c.source, p);
            const double cents = measure::centsBetween (f0, expected);
            worst = std::max (worst, std::abs (cents));
            ++total;
            if (std::abs (cents) <= 3.0)
                ++ok;
            else
                note (fmt ("MIDI %d, a %g Hz sample at %g Hz: %.3f Hz, %+.2f cents from %.3f Hz", p, c.source, c.session, f0, cents, expected));
        }
    check (ok == total && total == 12, fmt ("all 12 within +-3 cents (%d of %d; the worst %.2f cents)", ok, total, worst));

    // A no-op is not a pass: the same 44.1 kHz recording declared as 48 kHz (the F2 bug: the rate ratio left out)
    // plays about 147 cents sharp, which this measurement catches.
    const double f0Wrong = playedF0 (48000, 44100, 48000, 60);
    const double fromC3 = measure::centsBetween (f0Wrong, signals::midiHz (60));
    const double fromPlayed = measure::centsBetween (f0Wrong, signals::midiHz (60) * 48000.0 / 44100.0);
    check (std::abs (fromPlayed) <= 3.0 && std::abs (fromC3) > 100.0,
           fmt ("negative control: a sample declared at the wrong rate measures %+.1f cents from C3 (%+.2f from the pitch it then plays)", fromC3,
                fromPlayed));
}
