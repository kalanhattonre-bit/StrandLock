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
        previewRunning = false;
        previewBeat0 = 0.0;
        previewSamples = 0;
    }

    double HostSync::tempoOf (const diag::HostBlock& host) noexcept
    {
        if (host.hasPosition && host.hasBpm && std::isfinite (host.bpm) && host.bpm >= kMinHostBpm && host.bpm <= kMaxHostBpm)
            return host.bpm;
        return kFallbackBpm;
    }

    double HostSync::previewBeat() const noexcept
    {
        // the beat counted in samples from the Preview start, at the Preview's tempo (the engine's own clock counts
        // the same way: its anchor plus samples x bpm / 60 / sr)
        return previewBeat0 + static_cast<double> (previewSamples) * (previewBpm / 60.0 / sr);
    }

    BlockTransport HostSync::plan (const diag::HostBlock& host, int numSamples, double offsetBeats, bool clipLoop, double clipBeats) noexcept
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
            // Preview never runs while the host plays (plan 7.8 decision 3): its switch goes off, and Cubase's
            // transport takes over from here.
            out.previewEnded = previewOn;
            previewOn = false;
            previewRunning = false;

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
        else if (previewOn)
        {
            // Preview (U5): StrandLock's own clock at the host's tempo. It starts where the host's position is (the
            // cursor in Cubase), again wherever that position moves to while stopped, and carries on from the beat it
            // reached when the tempo changes.
            const double bpm = out.start.bpm;
            const bool moved = hasPpq && haveLast && std::abs (host.ppq - lastPpq) > kStoppedMoveBeats;
            if (! previewRunning || moved)
            {
                previewRunning = true;
                previewBeat0 = hasPpq ? host.ppq - offset : 0.0;
                previewSamples = 0;
                previewBpm = bpm;
            }
            else if (! juce::exactlyEqual (bpm, previewBpm))
            {
                previewBeat0 = previewBeat();
                previewSamples = 0;
                previewBpm = bpm;
            }

            const double beat = previewBeat();
            if (! clipLoop && clipBeats > 0.0 && beat >= clipBeats)
            {
                // a clip that does not loop has ended: Preview stops there (sl-ui.js stops its Play at the end)
                previewOn = false;
                previewRunning = false;
                out.previewEnded = true;
                out.start.beat = nan;
            }
            else
            {
                out.start.playing = true;
                out.start.beat = beat;
                out.beatsPerSample = bpm / 60.0 / sr;
                out.previewing = true;
                previewSamples += numSamples > 0 ? numSamples : 0;
            }
        }
        else
        {
            // Stopped: the beat only when the host moved it, so the engine's free clock runs on otherwise.
            previewRunning = false;
            const bool moved = hasPpq && (! haveLast || std::abs (host.ppq - lastPpq) > kStoppedMoveBeats);
            out.start.beat = moved ? host.ppq - offset : nan;
        }

        haveLast = hasPpq;
        lastPpq = hasPpq ? host.ppq : 0.0;
        return out;
    }
}
