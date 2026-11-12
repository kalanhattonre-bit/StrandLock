// sl-engine.js Voice: the constructor (in place), setNote, switchTo, release, steal, and the plugin's follow and
// live-pitch helpers (PLUGIN-PLAN 1.2: "Voice::init (...) (in place; the pool never allocates)").

#include "engine/Voice.h"

#include "core/Seeds.h"
#include "engine/EngineMath.h"

#include <cmath>
#include <string_view>

namespace sl::engine
{
    VoiceSource VoiceSource::of (const SampleData* sample, std::uint64_t sampleEpoch, double engineRate) noexcept
    {
        // JS setSample: sr = clamp (fin (s.sampleRate, this.sr), 4000, 768000)
        VoiceSource s;
        s.data = sample;
        s.epoch = sample != nullptr ? sampleEpoch : 0;
        s.sr = sample != nullptr ? clampJs (fin (sample->sr, engineRate), kMinSourceRate, kMaxSourceRate) : engineRate;
        return s;
    }

    const core::NoteSnap* snapshotNote (const core::Snapshot* snap, std::int32_t index) noexcept
    {
        if (snap == nullptr || index < 0 || static_cast<std::size_t> (index) >= snap->notes.size())
            return nullptr;
        const core::NoteSnap& n = snap->notes[static_cast<std::size_t> (index)];
        // JS setSnapshot: ok = n => n && n.id != null && isFinite(n.p) && isFinite(n.s) && isFinite(n.l) && n.l > 0
        if (! (std::isfinite (n.p) && std::isfinite (n.s) && std::isfinite (n.l) && n.l > 0))
            return nullptr;
        return &n;
    }

    std::uint32_t liveIdHash (int p) noexcept
    {
        char text[16] = { 'l', 'i', 'v', 'e', ':' };
        std::size_t len = 5;
        const int v = p < 0 ? 0 : p > 999 ? 999 : p;
        if (v >= 100)
            text[len++] = static_cast<char> ('0' + v / 100);
        if (v >= 10)
            text[len++] = static_cast<char> ('0' + (v / 10) % 10);
        text[len++] = static_cast<char> ('0' + v % 10);
        return core::idHash (std::string_view (text, len));
    }

    //==============================================================================================

    void Voice::initCommon (VoiceContext& context, const VoiceSource& source, VoiceKind voiceKind, double startBeat, std::int64_t pass,
                            std::int64_t kStart, std::uint64_t voiceSerial) noexcept
    {
        ctx = &context;
        src = source;
        kind = voiceKind;
        U = startBeat;
        m = pass;
        startK = kStart;
        serial = voiceSerial;

        for (int i = 0; i < kMaxHeads; ++i)
            heads[static_cast<std::size_t> (i)].init (i);

        vt = 0;
        ph = 0;
        first = true;
        posInit = false;
        startPos = std::numeric_limits<double>::quiet_NaN();
        pre = false;

        const double sr = context.sr;
        env = 0;
        st = EnvStage::attack;
        aStep = 1 / (kAttackSeconds * sr);
        rStep = 1 / (kReleaseSeconds * sr);
        sStep = 1 / (kStealSeconds * sr);

        released = false;
        done = false;
        bridging = false;
        sustained = false;
        key = -1;

        vibPh = 0;
        refPh = 0;
        refIdx = 0;
        refInit = false;
        jIdx = 0;
        jInit = false;

        hL.init();
        hR.init();
        fL.init();
        fR.init();
        lL.init();
        lR.init();

        g0 = 0;
        g1 = 0;
        dg = 0;
        for (int j = 0; j < kCtrl; ++j)
        {
            bufL[j] = 0;
            bufR[j] = 0;
        }

        reg = RegionState();
        for (double& v : lanes)
            v = 0;

        kc = Kc::voice;
        lastKc = -1;
        Hf = 1;
        level = 0;
        semis = 0;
        nh = 0;
        rev = false;

        engineName = Kc::voice;
        motion = core::Motion::forward;
        asTape = false;
        lpHz = 0;
        fmtDb = 0;
        fmtHz = 0;
        hollowDb = 0;

        chasing = false;
        chaseK = 0;
        pathSteer = false;
        reanchorSlack = 0;
    }

    void Voice::seedHeads() noexcept
    {
        // Drift and the tiny head offsets belong to the voice, seeded from its chain's first note (chain.id), so a
        // chain hand-over never bends or jumps a head (D2) and a chain plays exactly like one long note.
        std::int32_t vh = nh;
        if (kind == VoiceKind::clip && snap != nullptr)
        {
            const std::int32_t first0 = resolved().chain.firstIndex;
            if (first0 >= 0 && static_cast<std::size_t> (first0) < snap->notes.size())
                vh = static_cast<std::int32_t> (snap->notes[static_cast<std::size_t> (first0)].idHash);
        }
        const std::int32_t seed = ctx->inst->seed;
        for (Head& h : heads)
        {
            h.drift = core::rand01 (core::hash32 (vh, h.i, kSaltDrift, seed)) * 2 - 1;
            h.tiny = h.i > 0 ? core::rand01 (core::hash32 (vh, h.i, kSaltTiny, seed)) * 0.008 : 0;   // seconds of source
        }
    }

    void Voice::initClip (VoiceContext& context, const VoiceSource& source, const core::Snapshot* snapshot, std::uint64_t snapshotEpoch,
                          std::int32_t noteIndex, double startBeat, std::int64_t pass, std::int64_t kStart, std::uint64_t voiceSerial) noexcept
    {
        initCommon (context, source, VoiceKind::clip, startBeat, pass, kStart, voiceSerial);
        snap = snapshot;
        snapEpoch = snapshot != nullptr ? snapshotEpoch : 0;
        const core::NoteSnap* X = snapshot != nullptr && noteIndex >= 0 && static_cast<std::size_t> (noteIndex) < snapshot->notes.size()
                                      ? &snapshot->notes[static_cast<std::size_t> (noteIndex)]
                                      : &liveNote;
        setNote (X);
        seedHeads();
    }

    void Voice::initLive (VoiceContext& context, const VoiceSource& source, int p, double vel, std::int64_t kStart, std::uint64_t voiceSerial) noexcept
    {
        initCommon (context, source, VoiceKind::live, 0, 0, kStart, voiceSerial);
        snap = nullptr;
        snapEpoch = 0;

        // { id: 'live:' + p, idHash, p, s: 0, l: Infinity, vel, start: { mode: 'region', pos: 0, range: 0 }, lanes: null,
        //   glide: null, chain: null }
        liveNote = core::NoteSnap();
        liveNote.idHash = liveIdHash (p);
        liveNote.p = p;
        liveNote.s = 0;
        liveNote.l = std::numeric_limits<double>::infinity();
        liveNote.vel = vel;
        liveNote.start.mode = 0;   // region
        liveNote.start.pos = 0;
        liveNote.start.range = 0;
        liveNote.start.seed = 0;
        for (core::LaneTab& t : liveNote.lanes)
            t = core::LaneTab();   // no tables: every lane reads its default
        key = p;
        setNote (&liveNote);
        seedHeads();
    }

    void Voice::setNote (const core::NoteSnap* X) noexcept
    {
        note = X;
        nh = static_cast<std::int32_t> (X->idHash);
    }

    void Voice::switchTo (const core::NoteSnap* X, double startBeat) noexcept
    {
        setNote (X);
        U = startBeat;
        bridging = false;
    }

    void Voice::follow (const core::Snapshot* snapshot, std::uint64_t epoch, const core::NoteSnap* X, double startBeat) noexcept
    {
        snap = snapshot;
        snapEpoch = epoch;
        setNote (X);
        U = startBeat;
    }

    void Voice::setLivePitch (int p, double vel) noexcept
    {
        liveNote.p = p;
        liveNote.vel = vel;
        key = p;
    }

    void Voice::setLivePitch (int p) noexcept
    {
        liveNote.p = p;
        key = p;
    }

    void Voice::release() noexcept
    {
        if (released)
            return;
        released = true;
        if (st != EnvStage::steal)
            st = EnvStage::release;
    }

    void Voice::steal() noexcept
    {
        released = true;
        st = EnvStage::steal;
    }

    const core::NoteSnap* Voice::chainNext() const noexcept
    {
        if (kind != VoiceKind::clip || snap == nullptr)
            return nullptr;
        return snapshotNote (snap, resolved().chain.nextIndex);
    }

    const core::NoteSnap* Voice::chainPrev() const noexcept
    {
        if (kind != VoiceKind::clip || snap == nullptr)
            return nullptr;
        return snapshotNote (snap, resolved().chain.prevIndex);
    }
}
