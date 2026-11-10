#include "engine/VoiceRig.h"

#include "core/Lanes.h"
#include "core/Seeds.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace sl::test::rig
{
    std::unique_ptr<core::Snapshot> rawSnapshot (const std::vector<RawNote>& notes, int bars, bool chained)
    {
        auto snap = std::make_unique<core::Snapshot>();
        snap->bars = bars;
        snap->beatsPerBar = 4;
        snap->res = 256;
        snap->loop = true;
        snap->tieAcrossLoop = false;
        const auto count = static_cast<std::int32_t> (notes.size());

        auto indexOf = [&notes] (const std::string& id) -> std::int32_t {
            for (std::size_t i = 0; i < notes.size(); ++i)
                if (notes[i].id == id)
                    return static_cast<std::int32_t> (i);
            return -1;
        };

        for (std::int32_t i = 0; i < count; ++i)
        {
            const RawNote& o = notes[static_cast<std::size_t> (i)];
            auto tables = std::make_shared<core::NoteTables>();
            const int len = std::max (2, static_cast<int> (std::ceil (o.l * o.res)) + 1);
            for (int li = 0; li < core::kNumLanes; ++li)
            {
                auto& t = tables->lanes[static_cast<std::size_t> (li)];
                t.resize (static_cast<std::size_t> (len));
                const LaneSpec& spec = o.lanes[static_cast<std::size_t> (li)];
                for (int j = 0; j < len; ++j)
                {
                    const double u = static_cast<double> (j) / (len - 1);
                    const double v = ! spec.set ? core::kLanes[li].def : spec.fn ? spec.fn (u) : spec.value;
                    t[static_cast<std::size_t> (j)] = static_cast<float> (v);
                }
                tables->fullLength[static_cast<std::size_t> (li)] = len;
            }

            core::NoteSnap n;
            n.idHash = core::idHash (std::string_view (o.id));
            n.idIndex = i;
            n.p = o.p;
            n.s = o.s;
            n.l = o.l;
            n.vel = o.vel;
            n.engine = static_cast<std::int8_t> (o.engine);
            n.lock = o.lock ? 1 : 0;
            n.motion = static_cast<std::int8_t> (o.motion);
            n.tape = o.tape ? 1 : 0;
            n.rev = o.rev ? 1 : 0;
            n.travel = o.travel;
            n.jumpBeats = o.jump;
            n.restart = o.restart;
            n.start = o.start;
            n.glide = o.glideLen;   // resolve (): min (max (0, glide), l * 0.95)
            core::GlideSnap g;
            g.present = o.hasGlide;
            g.d = o.glideD;
            g.curve = o.glideCurve;
            g.toIndex = o.glideTo.empty() ? -1 : indexOf (o.glideTo);
            core::ChainSnap c;
            c.firstIndex = chained ? 0 : i;
            c.index = chained ? i : 0;
            c.prevIndex = chained && i > 0 ? i - 1 : -1;
            c.nextIndex = chained && i < count - 1 ? i + 1 : -1;
            for (int mode = 0; mode < 3; ++mode)
            {
                n.glides[mode] = g;
                n.chains[mode] = c;
            }
            for (int li = 0; li < core::kNumLanes; ++li)
            {
                const auto& t = tables->lanes[static_cast<std::size_t> (li)];
                n.lanes[li] = core::LaneTab { t.data(), static_cast<std::int32_t> (t.size()) };
            }
            snap->notes.push_back (n);
            snap->ids.push_back (o.id);
            snap->byHash.emplace_back (n.idHash, i);
            snap->tables.push_back (std::move (tables));
        }
        std::sort (snap->byHash.begin(), snap->byHash.end());
        return snap;
    }

    std::unique_ptr<engine::SampleData> sampleOf (const signals::Signal& s)
    {
        std::vector<float> l = s.channels.empty() ? std::vector<float>() : s.channels[0];
        std::vector<float> r = s.channels.size() > 1 ? s.channels[1] : std::vector<float>();
        return engine::makeSampleData (std::move (l), std::move (r), s.sampleRate, s.name);
    }

    std::unique_ptr<engine::SampleData> sampleOf (std::vector<float> mono, double sampleRate)
    {
        return engine::makeSampleData (std::move (mono), {}, sampleRate, "test");
    }

    double maxStep (const std::vector<float>& x, std::int64_t a, std::int64_t b)
    {
        double p = 0;
        const auto n = static_cast<std::int64_t> (x.size());
        for (std::int64_t i = std::max<std::int64_t> (1, a); i < std::min (n, b); ++i)
            p = std::max (p, std::abs (static_cast<double> (x[static_cast<std::size_t> (i)]) - x[static_cast<std::size_t> (i - 1)]));
        return p;
    }

    double decodeAt (const std::vector<float>& L, const std::vector<float>& R, std::int64_t i, std::int64_t w)
    {
        double a = 0, b = 0;
        const auto n = static_cast<std::int64_t> (L.size());
        for (std::int64_t j = std::max<std::int64_t> (0, i - w); j <= std::min (n - 1, i + w); ++j)
        {
            a += L[static_cast<std::size_t> (j)];
            b += R[static_cast<std::size_t> (j)];
        }
        return std::abs (b) > 1e-6 ? (a / b * 0.5 - 0.05) / 0.8 : std::nan ("");
    }

    double rms (const std::vector<float>& x, std::int64_t a, std::int64_t b)
    {
        double s = 0;
        for (std::int64_t i = a; i < b; ++i)
            s += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
        return std::sqrt (s / static_cast<double> (std::max<std::int64_t> (1, b - a)));
    }

    std::uint32_t hashOf (const std::vector<float>& L, const std::vector<float>& R)
    {
        return signals::fnvF32 ({ L, R });
    }

    //==============================================================================================

    VoiceRig::VoiceRig (double sampleRate)
    {
        ctx.sr = sampleRate;
        ctx.inst = &inst;
        ctx.clock = &clock;
        clock.db = clock.bpm / 60 / sampleRate;
        voice = std::make_unique<engine::Voice>();
    }

    void VoiceRig::setInst (const core::Inst& i)
    {
        inst = core::clampForEngine (i);
    }

    void VoiceRig::setSample (std::unique_ptr<engine::SampleData> s)
    {
        sample = std::move (s);
        source = engine::VoiceSource::of (sample.get(), 1, ctx.sr);
    }

    void VoiceRig::setSnapshot (std::unique_ptr<core::Snapshot> snapshot)
    {
        snap = std::move (snapshot);
        clock.Lb = std::max (1.0, static_cast<double> (snap->bars)) * std::max (1.0, static_cast<double> (snap->beatsPerBar));
    }

    void VoiceRig::play (double bpm, double beat, bool loop)
    {
        clock.bpm = bpm;
        clock.db = bpm / 60 / ctx.sr;
        clock.loop = loop;
        clock.anchorBeat = beat;
        clock.anchorK = 0;
        clock.playing = true;
    }

    engine::Voice& VoiceRig::live (int p, double vel, std::int64_t k0)
    {
        voice->initLive (ctx, source, p, vel, k0, ++serial);
        voice->startAt (k0);
        return *voice;
    }

    engine::Voice& VoiceRig::clip (std::int32_t index, std::int64_t pass)
    {
        const core::NoteSnap& X = snap->notes[static_cast<std::size_t> (index)];
        const double U = X.s + static_cast<double> (pass) * clock.Lb;
        const std::int64_t kS = clock.kOf (U);
        voice->initClip (ctx, source, snap.get(), 1, index, U, pass, kS, ++serial);
        voice->startAt (kS);
        return *voice;
    }

    std::unique_ptr<engine::Voice> VoiceRig::newClipVoice (std::int32_t index, std::int64_t pass)
    {
        auto v = std::make_unique<engine::Voice>();
        const core::NoteSnap& X = snap->notes[static_cast<std::size_t> (index)];
        const double U = X.s + static_cast<double> (pass) * clock.Lb;
        const std::int64_t kS = clock.kOf (U);
        v->initClip (ctx, source, snap.get(), 1, index, U, pass, kS, ++serial);
        return v;
    }

    engine::Voice& VoiceRig::clipChased (std::int32_t index, std::int64_t kNow, std::int64_t* ticks)
    {
        const core::NoteSnap& X = snap->notes[static_cast<std::size_t> (index)];
        const double U = X.s;
        voice->initClip (ctx, source, snap.get(), 1, index, U, 0, clock.kOf (U), ++serial);
        const std::int64_t t = voice->chase (kNow);
        if (ticks != nullptr)
            *ticks = t;
        voice->ffChain (kNow);
        voice->startAt (kNow);
        return *voice;
    }

    void VoiceRig::render (engine::Voice& v, std::vector<float>& L, std::vector<float>& R, std::int64_t from, std::int64_t to, int block,
                           const std::vector<std::int64_t>& splits, const std::function<void (std::int64_t)>& before)
    {
        std::vector<std::int64_t> pts { from, to };
        for (std::int64_t k = from + block; k < to; k += block)
            pts.push_back (k);
        for (const std::int64_t k : splits)
            if (k > from && k < to)
                pts.push_back (k);
        std::sort (pts.begin(), pts.end());
        pts.erase (std::unique (pts.begin(), pts.end()), pts.end());
        for (std::size_t q = 0; q + 1 < pts.size(); ++q)
        {
            const std::int64_t a = pts[q], b = pts[q + 1];
            if (before)
                before (a);
            if (! v.done)
                v.render (L.data(), R.data(), static_cast<int> (a), static_cast<int> (b - a), a);
        }
    }
}
