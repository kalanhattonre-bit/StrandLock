// sl-engine.js Engine.getDisplay, stats and resetStats (PLUGIN-PLAN 1.5): the display writes into a DisplayFrame
// instead of building objects, so the audio thread allocates nothing. The voices that are not done, in voice
// order, with their heads that are on; up to 128 grains; the beat, playing, the sample time, the stats and the
// output peaks since the frame before.
//
// Plugin differences: a head's position is a fraction of the voice's own sample (the same as the engine's for
// every voice except one fading on the sample before a swap, 8.2 #23); a voice still in a bounded chase is silent
// and not shown; the note id is its hash (8.2 #19).

#include "engine/Engine.h"

#include "engine/EngineMath.h"

#include <cmath>
#include <limits>

namespace sl::engine
{
    namespace
    {
        std::int32_t toInt32Sat (std::int64_t x) noexcept
        {
            return x > std::numeric_limits<std::int32_t>::max() ? std::numeric_limits<std::int32_t>::max() : static_cast<std::int32_t> (x);
        }
    }

    void Engine::fillDisplay (DisplayFrame& frame) noexcept
    {
        frame.sampleTime = k;
        frame.beat = clock.songBeat (k);
        frame.playing = clock.playing;
        int nv = 0, ng = 0;
        if (src != nullptr)
            for (int i = 0; i < numVoices && nv < kDisplayVoices; ++i)
            {
                const Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (v.done || v.chasing || v.src.data == nullptr || v.note == nullptr)
                    continue;
                const SampleData& S = *v.src.data;
                const double n = static_cast<double> (S.n);
                const RegionState& G = v.reg;
                DisplayVoice& d = frame.voices[nv++];
                d.noteIdHash = v.note->idHash;
                const double p = std::isfinite (v.note->p) ? clampJs (v.note->p, -32768, 32767) : 0;
                d.p = static_cast<std::int16_t> (p);
                d.kind = static_cast<std::uint8_t> (v.kind);
                d.engine = static_cast<std::uint8_t> (v.engineName);
                d.motion = static_cast<std::uint8_t> (v.motion);
                d.released = v.released;
                d.env = static_cast<float> (v.env);
                d.numHeads = 0;
                for (const Head& h : v.heads)
                {
                    if (! h.on)
                        continue;
                    DisplayHead& dh = d.heads[d.numHeads++];
                    dh.index = static_cast<std::uint8_t> (h.i);
                    dh.dir = static_cast<std::int8_t> (h.dir);
                    dh.thinned = h.want && ! h.allowed;
                    dh.pos = static_cast<float> ((G.rs + mapPos (G, h.raw + h.off)) / n);
                    dh.level = static_cast<float> (0.5 * (h.g1L + h.g1R) * h.stopG);
                    if (ng < kDisplayGrains)
                    {
                        if (v.kc == Kc::lock)
                        {
                            frame.grains[ng++] = { static_cast<float> ((G.rs + mapPos (G, h.capA)) / n), static_cast<float> (h.th) };
                        }
                        else
                        {
                            for (int g = 0; g < h.gc && ng < kDisplayGrains; ++g)
                            {
                                const auto gi = static_cast<std::size_t> (g);
                                const double a = jsMax (0, static_cast<double> (v.vt) - h.gT[gi]);
                                frame.grains[ng++] = { static_cast<float> ((G.rs + mapPos (G, h.gX[gi] + a * h.gS[gi])) / n),
                                                       static_cast<float> (clampJs (a / h.gN[gi], 0, 1)) };
                            }
                        }
                    }
                }
            }
        frame.numVoices = static_cast<std::uint16_t> (nv);
        frame.numGrains = static_cast<std::uint16_t> (ng);
        const Stats s = stats();
        frame.stats = { s.voices, s.grains, s.thinnedHeads, s.maxGrains, s.budget, toInt32Sat (s.cuts), toInt32Sat (s.steals) };
        frame.peakL = peakL;
        frame.peakR = peakR;
        peakL = peakR = 0.0f;
    }

    Stats Engine::stats() const noexcept
    {
        Stats s = st;
        s.voices = 0;
        s.tapeStandIns = 0;
        for (int i = 0; i < numVoices; ++i)
        {
            const Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (v.done)
                continue;
            ++s.voices;
            if (v.asTape)
                ++s.tapeStandIns;
        }
        s.budget = budget;
        s.filterResets = ctx.filterResets;
        return s;
    }

    void Engine::resetStats() noexcept
    {
        st.maxGrains = 0;
        st.cuts = 0;
        st.steals = 0;
        st.deferredStarts = 0;
        st.chaseTicks = 0;
        st.nonFinite = 0;
        st.preLimiterPeak = 0.0;
        ctx.filterResets = 0;
    }
}
