#include "engine/EngineRender.h"

#include "engine/EngineFixture.h"
#include "engine/VoiceRig.h"
#include "support/FixtureJson.h"
#include "support/TestSignals.h"

#include "model/Normalize.h"
#include "model/Presets.h"
#include "model/Recipes.h"
#include "model/SnapshotCompiler.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

namespace sl::test::render
{
    using core::JsonValue;

    namespace
    {
        constexpr std::int64_t kStatsEvery = 1024, kDisplayEvery = 4800, kTraceEvery = 32;

        engine::Ctrl ctrlOf (const std::string& name, bool& ok)
        {
            ok = true;
            if (name == "mod1")
                return engine::Ctrl::mod1;
            if (name == "mod2")
                return engine::Ctrl::mod2;
            if (name == "bend")
                return engine::Ctrl::bend;
            if (name == "sustain")
                return engine::Ctrl::sustain;
            ok = false;
            return engine::Ctrl::mod1;
        }

        std::vector<std::int64_t> blockSizes (const JsonValue& block, std::int64_t n)
        {
            std::vector<std::int64_t> sizes;
            if (block.isNumber())
            {
                const auto b = static_cast<std::int64_t> (block.asNumber());
                for (std::int64_t i = 0; i < n; i += b)
                    sizes.push_back (std::min (b, n - i));
            }
            else if (block.isArray())
            {
                std::size_t q = 0;
                for (std::int64_t i = 0; i < n;)
                {
                    const auto m = std::min (static_cast<std::int64_t> (block[q++ % block.size()].asNumber()), n - i);
                    sizes.push_back (m);
                    i += m;
                }
            }
            else
            {
                signals::Lcg r (block["random"].asNumber());
                const double lo = block["min"].asNumber(), hi = block["max"].asNumber();
                for (std::int64_t i = 0; i < n;)
                {
                    const auto m = std::min (static_cast<std::int64_t> (lo + std::floor (r() * (hi - lo + 1))), n - i);
                    sizes.push_back (m);
                    i += m;
                }
            }
            return sizes;
        }

        bool isCompileKeyChange (const core::Inst& a, const core::Inst& b)
        {
            // sl-ui.js COMPILE_KEYS: engine lock motion tape rev jump glideMode glideTime
            return a.engine != b.engine || a.lock != b.lock || a.motion != b.motion || a.tape != b.tape || a.rev != b.rev || a.jump != b.jump
                   || a.glideMode != b.glideMode || a.glideTime != b.glideTime;
        }
    }

    std::unique_ptr<engine::SampleData> soundOf (const JsonValue& spec)
    {
        if (spec.contains ("starter"))
        {
            const auto id = sounds::parseStarter (spec["starter"].asString());
            if (! id.has_value())
                return nullptr;
            const auto seed = static_cast<std::int32_t> (spec["seed"].asNumber (1));
            const sounds::StarterSound s = sounds::makeStarterSound (*id, seed);
            return engine::makeSampleData (s.left, s.right, s.sampleRate, sounds::starterInfo (*id).name);
        }
        signals::Signal sig;
        if (! signals::makeSignal (spec, sig))
            return nullptr;
        return rig::sampleOf (sig);
    }

    bool clipOf (const JsonValue& ref, const JsonValue* recipe, const core::Inst& inst, const std::map<std::string, model::Clip>& clips,
                 model::Clip& out)
    {
        std::optional<model::Clip> clip;
        if (ref.isString())
        {
            const std::string& name = ref.asString();
            if (name.rfind ("preset:", 0) == 0)
                clip = model::loadClipPreset (name.substr (7));
            else if (const auto it = clips.find (name); it != clips.end())
                clip = it->second;
        }
        else if (ref.isObject())
        {
            clip = model::clipFromJson (ref);
        }
        if (! clip.has_value())
            return false;
        if (recipe != nullptr && recipe->isNumber())
        {
            const auto r = model::applyRecipe (*clip, inst, static_cast<int> (recipe->asNumber()), model::Ids::all());
            if (! r.has_value())
                return false;
            clip = r->clip;
        }
        out = *clip;
        return true;
    }

    std::unique_ptr<core::Snapshot> compile (model::Clip clip, int res)
    {
        clip.beatsPerBar = 4;
        clip.offsetBeats = 0;
        clip.loop = true;
        clip.tieAcrossLoop = false;
        return model::compileSnapshot (clip, res);
    }

    JsonValue mergeInstJson (const JsonValue& current, const JsonValue& patch)
    {
        JsonValue out = current;
        for (std::size_t i = 0; i < patch.size(); ++i)
        {
            const std::string& key = patch.keyAt (i);
            const JsonValue& v = patch.valueAt (i);
            if (v.isObject() && current[key].isObject())
            {
                JsonValue merged = current[key];
                for (std::size_t j = 0; j < v.size(); ++j)
                    merged.set (v.keyAt (j), v.valueAt (j));
                out.set (key, merged);
            }
            else
            {
                out.set (key, v);
            }
        }
        return out;
    }

    bool renderFixtureCase (const JsonValue& c, const std::map<std::string, model::Clip>& clips, Rendered& out, std::string& why)
    {
        const double sr = c["sr"].asNumber (48000);
        const auto n = static_cast<std::int64_t> (c["n"].asNumber());
        const int res = c.contains ("res") ? static_cast<int> (c["res"].asNumber()) : 256;
        const JsonValue& o = c["out"];
        const bool statsOnly = ! o.contains ("blockRms");
        const bool trace = o.contains ("trace");
        if (c.contains ("snapshot"))
        {
            why = "a raw snapshot (the engine-only case is a grains case)";
            return false;
        }

        auto e = std::make_unique<engine::Engine>();
        e->prepare (sr);
        e->setRampMs (0);
        JsonValue instJson = c["inst"];
        core::Inst inst = enginefx::instFromEngineJson (instJson);
        e->setInst (inst);
        auto sample = soundOf (c["sound"]);
        if (sample == nullptr)
        {
            why = "the sound could not be made";
            return false;
        }
        e->setSample (sample.get(), 1);

        std::uint64_t epoch = 0;
        std::vector<std::unique_ptr<core::Snapshot>> snaps;   // every version stays alive to the end (no frees here)
        std::optional<model::Clip> clipNow;
        auto setClip = [&] (const model::Clip& clip) {
            clipNow = clip;
            snaps.push_back (compile (clip, res));
            e->setSnapshot (snaps.back().get(), ++epoch);
        };
        if (c.contains ("clip"))
        {
            model::Clip clip;
            if (! clipOf (c["clip"], c.find ("recipe"), inst, clips, clip))
            {
                why = "the clip could not be made";
                return false;
            }
            setClip (clip);
        }

        const JsonValue& transport = c["transport"];
        const JsonValue& events = c["events"];
        std::set<std::int64_t> pts { 0, n };
        std::int64_t acc = 0;
        for (const std::int64_t m : blockSizes (c["block"], n))
        {
            acc += m;
            pts.insert (acc);
        }
        for (std::size_t i = 0; i < transport.size(); ++i)
            pts.insert (static_cast<std::int64_t> (transport[i][0].asNumber()));
        for (std::size_t i = 0; i < events.size(); ++i)
            pts.insert (static_cast<std::int64_t> (events[i][0].asNumber()));
        for (std::int64_t k = kStatsEvery; k <= n; k += kStatsEvery)
            pts.insert (k);
        for (std::int64_t k = kDisplayEvery; k <= n; k += kDisplayEvery)
            pts.insert (k);
        if (trace)
            for (std::int64_t k = kTraceEvery; k <= n; k += kTraceEvery)
                pts.insert (k);
        std::vector<std::int64_t> order;
        for (const std::int64_t p : pts)
            if (p >= 0 && p <= n)
                order.push_back (p);

        out.L.assign (static_cast<std::size_t> (n), 0.0f);
        out.R.assign (static_cast<std::size_t> (n), 0.0f);
        auto frame = std::make_unique<engine::DisplayFrame>();
        bool playing = false, loop = true;
        std::size_t ti = 0, ei = 0;
        for (std::size_t q = 0; q < order.size(); ++q)
        {
            const std::int64_t p = order[q];
            if (p > 0)
            {
                if (p % kStatsEvery == 0)
                {
                    const engine::Stats s = e->stats();
                    out.stats.push_back ({ p, s.voices, s.grains, s.thinnedHeads, s.maxGrains, s.budget, s.cuts, s.steals });
                }
                if (p % kDisplayEvery == 0 && ! statsOnly)
                {
                    e->fillDisplay (*frame);
                    out.displayAt.push_back (p);
                    out.display.push_back (*frame);
                }
                if (trace && p % kTraceEvery == 0)
                {
                    e->fillDisplay (*frame);
                    float pos = std::numeric_limits<float>::quiet_NaN();
                    if (frame->numVoices > 0)
                        for (int h = 0; h < frame->voices[0].numHeads; ++h)
                            if (frame->voices[0].heads[h].index == 0)
                            {
                                pos = frame->voices[0].heads[h].pos;
                                break;
                            }
                    out.trace.push_back (pos);
                }
            }
            if (p == n)
                break;
            while (ti < transport.size() && static_cast<std::int64_t> (transport[ti][0].asNumber()) == p)
            {
                const JsonValue& t = transport[ti++][1];
                engine::Transport tr;
                if (t.contains ("playing"))
                    playing = t["playing"].asBool();
                if (t.contains ("loop"))
                    loop = t["loop"].asBool();
                tr.playing = playing;
                tr.loop = loop;
                tr.bpm = t.contains ("bpm") ? fixture::num (t["bpm"]) : std::numeric_limits<double>::quiet_NaN();
                tr.beat = t.contains ("beat") ? fixture::num (t["beat"]) : std::numeric_limits<double>::quiet_NaN();
                e->setTransport (tr);
            }
            while (ei < events.size() && static_cast<std::int64_t> (events[ei][0].asNumber()) == p)
            {
                const JsonValue& ev = events[ei++];
                const std::string& kind = ev[1].asString();
                if (kind == "noteOn")
                {
                    engine::NoteOnOpts opts;
                    if (ev[4].isObject() && ev[4].contains ("startPos") && ! ev[4]["startPos"].isNull())
                        opts.startPos = fixture::num (ev[4]["startPos"]);
                    e->noteOn (fixture::num (ev[2]), fixture::num (ev[3]), opts);
                }
                else if (kind == "noteOff")
                {
                    e->noteOff (fixture::num (ev[2]));
                }
                else if (kind == "ctrl")
                {
                    bool ok = false;
                    const engine::Ctrl ctrl = ctrlOf (ev[2].asString(), ok);
                    if (! ok)
                    {
                        why = "unknown controller " + ev[2].asString();
                        return false;
                    }
                    if (ev[3].isNull())
                        e->releaseController (ctrl);
                    else
                        e->setController (ctrl, fixture::num (ev[3]));
                }
                else if (kind == "allOff")
                {
                    e->allNotesOff();
                }
                else if (kind == "inst")
                {
                    const core::Inst prev = inst;
                    instJson = mergeInstJson (instJson, ev[2]);
                    inst = core::instFromJson (instJson);
                    instJson = core::instToJson (inst);
                    e->setInst (inst);
                    if (clipNow.has_value() && isCompileKeyChange (prev, inst))
                        setClip (*clipNow);
                }
                else if (kind == "snapshot")
                {
                    model::Clip clip;
                    if (! clipOf (ev[2], nullptr, inst, clips, clip))
                    {
                        why = "the snapshot event's clip could not be made";
                        return false;
                    }
                    setClip (clip);
                }
                else
                {
                    why = "unknown event " + kind;
                    return false;
                }
            }
            const std::int64_t next = order[q + 1];
            e->process (out.L.data() + p, out.R.data() + p, static_cast<int> (next - p));
        }
        if (ti < transport.size() || ei < events.size())
        {
            why = "events past the end";
            return false;
        }
        out.last = e->stats();
        return true;
    }

    //==============================================================================================

    void Script::at (std::int64_t k, Action fn)
    {
        actions.emplace (k, std::move (fn));
    }

    void Script::run (engine::Engine& e, std::int64_t n, const std::function<int (std::int64_t)>& block, std::vector<float>& L,
                      std::vector<float>& R, const std::function<void (engine::Engine&, std::int64_t)>& onBlock)
    {
        L.assign (static_cast<std::size_t> (n), 0.0f);
        R.assign (static_cast<std::size_t> (n), 0.0f);
        auto next = actions.begin();
        while (next != actions.end() && next->first < 0)
            ++next;
        std::int64_t k = 0;
        while (k < n)
        {
            const std::int64_t blockEnd = std::min (n, k + std::max (1, block (k)));
            if (onBlock)
                onBlock (e, k);
            std::int64_t pos = k;
            while (pos < blockEnd)
            {
                while (next != actions.end() && next->first == pos)
                {
                    next->second (e);
                    ++next;
                }
                const std::int64_t stop = next != actions.end() && next->first < blockEnd ? next->first : blockEnd;
                if (stop > pos)
                    e.process (L.data() + pos, R.data() + pos, static_cast<int> (stop - pos));
                pos = stop;
            }
            k = blockEnd;
        }
    }

    std::function<int (std::int64_t)> fixedBlocks (int size)
    {
        return [size] (std::int64_t) { return size; };
    }

    std::unique_ptr<engine::SampleData> monoSample (std::vector<float> x, double sampleRate)
    {
        return engine::makeSampleData (std::move (x), {}, sampleRate, "test");
    }

    std::int64_t firstSounding (const std::vector<float>& L, const std::vector<float>& R, std::int64_t from, std::int64_t to)
    {
        const auto n = static_cast<std::int64_t> (L.size());
        for (std::int64_t i = std::max<std::int64_t> (0, from); i < std::min (n, to); ++i)
            if (L[static_cast<std::size_t> (i)] != 0.0f || R[static_cast<std::size_t> (i)] != 0.0f)
                return i;
        return -1;
    }
}
