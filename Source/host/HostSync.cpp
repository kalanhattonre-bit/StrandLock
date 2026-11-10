#include "HostSync.h"

#include <cmath>

namespace sl::host
{
    void HostSync::prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0;
        reset();
    }

    void HostSync::reset() noexcept
    {
        haveLast = false;
        lastPpq = 0.0;
    }

    double HostSync::tempoOf (const diag::HostBlock& host) noexcept
    {
        if (host.hasPosition && host.hasBpm && std::isfinite (host.bpm) && host.bpm >= kMinHostBpm && host.bpm <= kMaxHostBpm)
            return host.bpm;
        return kFallbackBpm;
    }

    BlockTransport HostSync::plan (const diag::HostBlock& host, int numSamples, double offsetBeats, bool clipLoop) noexcept
    {
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        const double offset = std::isfinite (offsetBeats) ? offsetBeats : 0.0;
        const bool hasPpq = host.hasPosition && host.hasPpq && std::isfinite (host.ppq);

        BlockTransport out;
        out.start.bpm = tempoOf (host);
        out.start.loop = clipLoop;
        out.start.playing = host.hasPosition && host.playing;

        if (out.start.playing)
        {
            out.start.beat = hasPpq ? host.ppq - offset : nan;
            out.beatsPerSample = out.start.bpm / 60.0 / sr;

            // A cycle wrap inside this block: split there and start again at the loop start.
            const bool cycling = host.looping && host.hasLoop && std::isfinite (host.loopStart) && std::isfinite (host.loopEnd)
                              && host.loopEnd > host.loopStart;
            if (cycling && hasPpq && host.ppq < host.loopEnd && out.beatsPerSample > 0.0)
            {
                const double at = std::ceil ((host.loopEnd - host.ppq) / out.beatsPerSample);
                if (at >= 1.0 && at < (double) numSamples)
                {
                    out.wrapAt = (int) at;
                    out.wrap = out.start;
                    out.wrap.beat = host.loopStart - offset;
                }
            }
        }
        else
        {
            // Stopped: the beat only when the host moved it, so the engine's free clock runs on otherwise.
            const bool moved = hasPpq && (! haveLast || std::abs (host.ppq - lastPpq) > kStoppedMoveBeats);
            out.start.beat = moved ? host.ppq - offset : nan;
        }

        haveLast = hasPpq;
        lastPpq = hasPpq ? host.ppq : 0.0;
        return out;
    }
}
