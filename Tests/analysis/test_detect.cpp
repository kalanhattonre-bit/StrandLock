// The analysis port (PLUGIN-PLAN 4, the M7 table's analysis rows; 7.9 V1). Group "analysis" (the sanitizer job runs it).
//  - analysis/detect.json, every case: detectPitch null-ness and MIDI exact, cents within 0.1 (one rounding step),
//    Hz within 1e-4 relative; detectKey exact; suggestParts kinds and labels exact, start and end within 1e-3 of the
//    file; the case count gated. The DC case shows decision 3's answer beside the prototype's reading.
//  - detection (PROMPT 14.11): synthetic tones at 5 pitches within +-10 cents; chord progressions in C major (and G
//    major and A minor, so a key rotated the wrong way cannot pass); white noise gives "no clear pitch".
//  - speed: suggestParts on the 5-minute stereo song of prototype-v4/tests/test-analysis.js, made at test time
//    (target 1.0 s, printed; gated at 5 times the target in an optimised build, plan 4's timing rule).
//  - what M7 V2 relies on: no full-length copy of the sound is kept (live memory), Stop ends an analysis early,
//    and every read stays inside the sound.
// The prototype tests' generators (hash, tone, progression, buildSong) are ported from tests/test-analysis.js.

#include "model/ModelFixture.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "analysis/Analysis.h"
#include "analysis/Fft.h"
#include "core/JsCompat.h"
#include "core/JsMath.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    /** analysis/detect.json's cases: 28, and 6 appended in M7 V1 (other sample rates and the 5-minute song). */
    constexpr std::size_t kDetectCases = 34;

    struct Audio
    {
        std::vector<std::vector<float>> channels;
        double sampleRate = 0.0;

        std::size_t frames() const noexcept { return channels.empty() ? 0 : channels.front().size(); }
        analysis::FloatSource source() const { return analysis::FloatSource::of (channels, sampleRate); }
    };

    double secondsSince (std::chrono::steady_clock::time_point t0)
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    }

    /** Plan 4's timing gate: printed against the target, failing only past 5 times it, and only in an optimised build
        without the sanitizers. */
    void timing (const std::string& what, double ms, double targetMs)
    {
#if defined (NDEBUG) && ! defined (STRANDLOCK_SANITIZE)
        const bool gated = true;
#else
        const bool gated = false;
#endif
        note (fmt ("%s: %.1f ms (target %.0f ms, gate %.0f ms%s)", what.c_str(), ms, targetMs, 5 * targetMs, gated ? "" : ", not gated in this build"));
        check (! gated || ms <= 5 * targetMs, fmt ("%s within 5 x its target (plan 4 timing gates)", what.c_str()));
    }

    //==============================================================================================
    // tests/test-analysis.js's generators

    /** hash (a, b): the test file's seeded noise (no Math.random). */
    double testHash (std::int64_t a, std::int64_t b) noexcept
    {
        std::uint32_t h = (static_cast<std::uint32_t> (a) * 0x9e3779b1u) ^ (static_cast<std::uint32_t> (b) * 0x85ebca6bu);
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return static_cast<double> (h) / 4294967296.0;
    }

    /** tone (midi, cents, sr, secs, harmonics, seed): harmonics 1/k with hashed phases up to 0.45 sr, 10 ms fades. */
    std::vector<float> testTone (int midi, double cents, double sr, double secs, int harmonics, std::int64_t seed)
    {
        const auto n = static_cast<std::size_t> (core::jsRound (secs * sr));
        std::vector<float> x (n);
        const double f = 440 * std::pow (2.0, (midi + cents / 100 - 69) / 12);
        for (int k = 1; k <= harmonics; ++k)
        {
            if (k * f > 0.45 * sr)
                break;
            const double a = 1.0 / k, ph = testHash (seed, k) * 2 * std::numbers::pi, w = 2 * std::numbers::pi * k * f / sr;
            for (std::size_t i = 0; i < n; ++i)
                x[i] = static_cast<float> (static_cast<double> (x[i]) + a * std::sin (w * static_cast<double> (i) + ph));
        }
        for (std::size_t i = 0; i < n; ++i)
            x[i] = static_cast<float> (static_cast<double> (x[i])
                                       * (0.5 * std::min ({ 1.0, static_cast<double> (i) / 441, static_cast<double> (n - i) / 441 })));
        return x;
    }

    /** progression (chords, sr, secs): each chord's notes with 6 harmonics 0.3 / k, secs each, 400-frame fades. */
    std::vector<float> progression (const std::vector<std::vector<int>>& chords, double sr, double secs)
    {
        const auto n = static_cast<std::size_t> (core::jsRound (secs * sr));
        std::vector<float> x (n * chords.size());
        for (std::size_t ci = 0; ci < chords.size(); ++ci)
            for (const int m : chords[ci])
            {
                const double f = 440 * std::pow (2.0, (m - 69) / 12.0);
                for (int k = 1; k <= 6; ++k)
                {
                    const double w = 2 * std::numbers::pi * k * f / sr, a = 0.3 / k, ph = testHash (m, k) * 6.28;
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        float& v = x[ci * n + i];
                        v = static_cast<float> (static_cast<double> (v)
                                                + a * std::sin (w * static_cast<double> (i) + ph)
                                                      * std::min ({ 1.0, static_cast<double> (i) / 400, static_cast<double> (n - i) / 400 }));
                    }
                }
            }
        return x;
    }

    struct Section
    {
        std::string id;
        double gain = 0.0, t0 = 0.0, t1 = 0.0;
    };

    struct Hit
    {
        double t = 0.0, gain = 0.0;
    };

    /** buildSong (totalSec, sections, hits) at the file's SR = 44100: each section loops its starter (seed 1) with
        20 ms fades; each hit is the beat's first kick (a 2 ms fade-in) plus a seeded noise crash; then the peak is
        scaled to 0.95. Line by line, Float32Array arithmetic as floats. False for an unknown starter. */
    bool buildSong (double totalSec, const std::vector<Section>& sections, const std::vector<Hit>& hits, Audio& out)
    {
        constexpr double SR = 44100;
        const auto N = static_cast<std::int64_t> (core::jsRound (totalSec * SR));
        std::vector<float> L (static_cast<std::size_t> (N)), R (static_cast<std::size_t> (N));
        const double fade = core::jsRound (0.02 * SR);
        // the starters are held for the whole song (the shared cache makes one again once nothing holds it)
        std::vector<std::shared_ptr<const sounds::StarterSound>> held;
        for (const Section& sec : sections)
        {
            const auto id = sounds::parseStarter (sec.id);
            if (! id)
                return false;
            const auto snd = sounds::sharedStarterSound (*id, 1);
            held.push_back (snd);
            const auto len = static_cast<std::int64_t> (snd->left.size());
            const auto a = static_cast<std::int64_t> (core::jsRound (sec.t0 * SR));
            const auto b = std::min (N, static_cast<std::int64_t> (core::jsRound (sec.t1 * SR)));
            for (std::int64_t i = a; i < b; ++i)
            {
                const auto j = static_cast<std::size_t> ((i - a) % len);
                const double g = sec.gain * std::min ({ 1.0, static_cast<double> (i - a) / fade, static_cast<double> (b - i) / fade });
                const auto at = static_cast<std::size_t> (i);
                L[at] = static_cast<float> (static_cast<double> (L[at]) + static_cast<double> (snd->left[j]) * g);
                R[at] = static_cast<float> (static_cast<double> (R[at]) + static_cast<double> (snd->right[j]) * g);
            }
        }
        // a loud hit: the beat's first kick (a 2 ms fade-in) plus a seeded noise crash
        const auto beat = sounds::sharedStarterSound (sounds::StarterId::beat, 1);
        const double hl = core::jsRound (0.45 * SR), cl = core::jsRound (2.5 * SR);
        for (const Hit& h : hits)
        {
            const auto a = static_cast<std::int64_t> (core::jsRound (h.t * SR));
            for (std::int64_t i = 0; static_cast<double> (i) < std::max (hl, cl) && a + i < N; ++i)
            {
                const auto di = static_cast<double> (i);
                const double fi = std::min (1.0, di / 88);
                double v = 0;
                if (di < hl)
                    v += static_cast<double> (beat->left[static_cast<std::size_t> (i)]) * fi * std::min (1.0, (hl - di) / 400) * 1.6;
                if (di < cl)
                    v += (testHash (a, i) * 2 - 1) * 0.55 * core::jsExp (-di / SR / 0.45) * fi;
                const auto at = static_cast<std::size_t> (a + i);
                L[at] = static_cast<float> (static_cast<double> (L[at]) + v * h.gain);
                R[at] = static_cast<float> (static_cast<double> (R[at]) + v * h.gain);
            }
        }
        double pk = 0;
        for (std::size_t i = 0; i < L.size(); ++i)
            pk = std::max ({ pk, std::fabs (static_cast<double> (L[i])), std::fabs (static_cast<double> (R[i])) });
        for (std::size_t i = 0; i < L.size(); ++i)
        {
            L[i] = static_cast<float> (static_cast<double> (L[i]) * (0.95 / pk));
            R[i] = static_cast<float> (static_cast<double> (R[i]) * (0.95 / pk));
        }
        out.channels.clear();
        out.channels.push_back (std::move (L));
        out.channels.push_back (std::move (R));
        out.sampleRate = SR;
        return true;
    }

    /** A fixture {song: {seconds, sampleRate, sections, hits}}. */
    bool songFrom (const JsonValue& spec, Audio& out, std::string& why)
    {
        if (! fixture::same (spec["sampleRate"].asNumber(), 44100.0))
        {
            why = "a song is made at 44100 Hz";
            return false;
        }
        std::vector<Section> sections;
        for (std::size_t i = 0; i < spec["sections"].size(); ++i)
        {
            const JsonValue& s = spec["sections"][i];
            sections.push_back ({ s[0].asString(), s[1].asNumber(), s[2].asNumber(), s[3].asNumber() });
        }
        std::vector<Hit> hits;
        for (std::size_t i = 0; i < spec["hits"].size(); ++i)
            hits.push_back ({ spec["hits"][i][0].asNumber(), spec["hits"][i][1].asNumber() });
        if (! buildSong (spec["seconds"].asNumber(), sections, hits, out))
        {
            why = "a song section names an unknown starter";
            return false;
        }
        return true;
    }

    /** A fixture input: {starter, seed?} | {signal, args} | {silence: seconds, sampleRate} | {concat: [...]} | {song}. */
    bool inputAudio (const JsonValue& inp, Audio& out, std::string& why)
    {
        if (const JsonValue* s = inp.find ("starter"); s != nullptr)
        {
            const auto id = sounds::parseStarter (s->asString());
            if (! id)
            {
                why = "unknown starter " + s->asString();
                return false;
            }
            const JsonValue* seed = inp.find ("seed");
            const auto snd = sounds::sharedStarterSound (*id, seed != nullptr && seed->isNumber() ? static_cast<std::int32_t> (seed->asNumber()) : 1);
            out.channels = { snd->left, snd->right };
            out.sampleRate = snd->sampleRate;
            return true;
        }
        if (inp.find ("signal") != nullptr)
        {
            signals::Signal sig;
            if (! signals::makeSignal (inp, sig))
            {
                why = "makeSignal does not know " + core::toJson (inp);
                return false;
            }
            out.channels = std::move (sig.channels);
            out.sampleRate = sig.sampleRate;
            return true;
        }
        if (const JsonValue* song = inp.find ("song"); song != nullptr)
            return songFrom (*song, out, why);
        if (const JsonValue* silence = inp.find ("silence"); silence != nullptr)
        {
            out.sampleRate = inp["sampleRate"].asNumber();
            out.channels = { std::vector<float> (static_cast<std::size_t> (core::jsRound (silence->asNumber() * out.sampleRate)), 0.0f) };
            return true;
        }
        if (const JsonValue* parts = inp.find ("concat"); parts != nullptr && parts->size() > 0)
        {
            // joined in order; every part shares one rate; a mono part fills every channel
            std::vector<Audio> pieces (parts->size());
            std::size_t channels = 0, frames = 0;
            for (std::size_t i = 0; i < parts->size(); ++i)
            {
                if (! inputAudio ((*parts)[i], pieces[i], why))
                    return false;
                if (! fixture::same (pieces[i].sampleRate, pieces[0].sampleRate))
                {
                    why = "concat parts at two rates";
                    return false;
                }
                channels = std::max (channels, pieces[i].channels.size());
                frames += pieces[i].frames();
            }
            out.sampleRate = pieces[0].sampleRate;
            out.channels.assign (channels, std::vector<float> (frames, 0.0f));
            std::size_t at = 0;
            for (const Audio& p : pieces)
            {
                for (std::size_t c = 0; c < channels; ++c)
                {
                    const std::vector<float>& from = p.channels[std::min (c, p.channels.size() - 1)];
                    std::copy (from.begin(), from.end(), out.channels[c].begin() + static_cast<std::ptrdiff_t> (at));
                }
                at += p.frames();
            }
            return true;
        }
        why = "unknown input " + core::toJson (inp);
        return false;
    }

    //==============================================================================================
    /** A Source that reads another and counts every request outside the sound (the analysis must make none). */
    class BoundsSource final : public analysis::Source
    {
    public:
        explicit BoundsSource (const analysis::Source& s) : inner (s) {}

        int numChannels() const override { return inner.numChannels(); }
        std::int64_t numFrames() const override { return inner.numFrames(); }
        double sampleRate() const override { return inner.sampleRate(); }

        void read (int c, std::int64_t start, int count, float* out) const override
        {
            ++reads;
            if (c < 0 || c >= inner.numChannels() || start < 0 || count <= 0 || start + count > inner.numFrames())
            {
                ++outside;
                for (int k = 0; k < count; ++k)
                    out[k] = 0.0f;
                return;
            }
            inner.read (c, start, count, out);
        }

        mutable long long reads = 0;
        mutable long long outside = 0;

    private:
        const analysis::Source& inner;
    };

    //==============================================================================================
    // Comparing with the fixture

    std::string pitchText (const std::optional<analysis::Pitch>& p)
    {
        return p ? fmt ("MIDI %d %+.1f c (%.4f Hz)", p->midi, p->cents, p->hz) : std::string ("no clear pitch");
    }

    std::string keyText (const std::optional<analysis::Key>& k)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return k ? fmt ("%s %s (r %.3f)", names[static_cast<std::size_t> (k->key % 12)], analysis::scaleName (k->scale), k->r) : std::string ("no key");
    }

    std::string partsText (const std::vector<analysis::Part>& parts)
    {
        std::string s;
        for (const analysis::Part& p : parts)
            s += fmt ("%s%s \"%s\" %.4f-%.4f", s.empty() ? "" : ", ", analysis::partKindName (p.kind), p.label.c_str(), p.start, p.end);
        return s.empty() ? std::string ("no parts") : s;
    }

    /** What a case within tolerance holds that is not bit for bit: exact is cleared, and diffs names each number. */
    struct Exactness
    {
        bool exact = true;
        std::string diffs;

        void compare (const char* what, double got, double want)
        {
            if (fixture::same (got, want))
                return;
            exact = false;
            diffs += fmt ("%s%s %.17g against %.17g", diffs.empty() ? "" : "; ", what, got, want);
        }
    };

    /** got against a fixture detectPitch ({midi, cents, hz} | null). */
    bool pitchMatches (const std::optional<analysis::Pitch>& got, const JsonValue& want, Exactness& exact, std::string& why)
    {
        if (want.isNull() || ! got)
        {
            if (want.isNull() == ! got.has_value())
                return true;
            why = "detectPitch " + pitchText (got) + ", the prototype " + (want.isNull() ? std::string ("null") : core::toJson (want));
            return false;
        }
        const double cents = want["cents"].asNumber(), hz = want["hz"].asNumber();
        if (! fixture::same (static_cast<double> (got->midi), want["midi"].asNumber()) || ! fixture::withinAbs (got->cents, cents, 0.1 + 1e-9)
            || ! fixture::withinRel (got->hz, hz, 1e-4))
        {
            why = "detectPitch " + pitchText (got) + ", the prototype " + core::toJson (want);
            return false;
        }
        exact.compare ("cents", got->cents, cents);
        exact.compare ("Hz", got->hz, hz);
        return true;
    }

    bool keyMatches (const std::optional<analysis::Key>& got, const JsonValue& want, std::string& why)
    {
        const bool ok = want.isNull() ? ! got.has_value()
                                      : got.has_value() && fixture::same (static_cast<double> (got->key), want["key"].asNumber())
                                            && want["scale"].asString() == analysis::scaleName (got->scale) && fixture::same (got->r, want["r"].asNumber());
        if (! ok)
            why = "detectKey " + keyText (got) + ", the prototype " + core::toJson (want);
        return ok;
    }

    bool partsMatch (const std::vector<analysis::Part>& got, const JsonValue& want, Exactness& exact, std::string& why)
    {
        if (got.size() != want.size())
        {
            why = fmt ("suggestParts gave %zu parts (%s), the prototype %zu: %s", got.size(), partsText (got).c_str(), want.size(), core::toJson (want).c_str());
            return false;
        }
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            const analysis::Part& p = got[i];
            const JsonValue& w = want[i];
            const bool hasTempo = w.find ("bpm") != nullptr;
            const bool tempoOk = hasTempo ? fixture::same (p.bpm, w["bpm"].asNumber()) && p.tempoFound == w["tempoFound"].asBool()
                                          : fixture::same (p.bpm, 0.0) && ! p.tempoFound;
            if (w["kind"].asString() != analysis::partKindName (p.kind) || w["label"].asString() != p.label
                || ! fixture::withinAbs (p.start, w["start"].asNumber(), 1e-3) || ! fixture::withinAbs (p.end, w["end"].asNumber(), 1e-3) || ! tempoOk)
            {
                why = fmt ("part %zu: %s \"%s\" %.6f-%.6f bpm %g %s, the prototype %s", i, analysis::partKindName (p.kind), p.label.c_str(), p.start,
                           p.end, p.bpm, p.tempoFound ? "found" : "not found", core::toJson (w).c_str());
                return false;
            }
            exact.compare (fmt ("%s start", analysis::partKindName (p.kind)).c_str(), p.start, w["start"].asNumber());
            exact.compare (fmt ("%s end", analysis::partKindName (p.kind)).c_str(), p.end, w["end"].asNumber());
        }
        return true;
    }

    bool samePitch (const std::optional<analysis::Pitch>& a, const std::optional<analysis::Pitch>& b)
    {
        if (! a || ! b)
            return a.has_value() == b.has_value();
        return a->midi == b->midi && fixture::same (a->cents, b->cents) && fixture::same (a->hz, b->hz);
    }

    bool sameParts (const std::vector<analysis::Part>& a, const std::vector<analysis::Part>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].kind != b[i].kind || a[i].label != b[i].label || ! fixture::same (a[i].start, b[i].start) || ! fixture::same (a[i].end, b[i].end)
                || ! fixture::same (a[i].bpm, b[i].bpm) || a[i].tempoFound != b[i].tempoFound)
                return false;
        return true;
    }

    /** The prototype test's constraints (ps, dur): at most 4 parts of known, distinct kinds with labels, each inside
        the file and 1 to 4 s long, no pair overlapping by more than half the shorter. Empty when they hold. */
    std::string partProblems (const std::vector<analysis::Part>& ps, double dur)
    {
        std::string errs;
        if (ps.size() > 4)
            errs += fmt ("%zu parts; ", ps.size());
        for (std::size_t i = 0; i < ps.size(); ++i)
        {
            const analysis::Part& p = ps[i];
            for (std::size_t j = 0; j < i; ++j)
                if (ps[j].kind == p.kind)
                    errs += std::string ("duplicate ") + analysis::partKindName (p.kind) + "; ";
            if (p.label.empty())
                errs += "a part without a label; ";
            if (! (p.start >= 0 && p.end <= 1 && p.end > p.start))
                errs += std::string (analysis::partKindName (p.kind)) + " outside 0..1; ";
            const double len = (p.end - p.start) * dur;
            if (len < 1 - 1e-6 || len > 4 + 1e-6)
                errs += fmt ("%s %.3f s long; ", analysis::partKindName (p.kind), len);
            for (std::size_t j = 0; j < i; ++j)
            {
                const analysis::Part& q = ps[j];
                const double ov = std::min (p.end, q.end) - std::max (p.start, q.start);
                const double r = std::max (0.0, ov) / std::min (p.end - p.start, q.end - q.start);
                if (r > 0.5 + 1e-9)
                    errs += fmt ("%s/%s overlap %.0f%%; ", analysis::partKindName (q.kind), analysis::partKindName (p.kind), 100 * r);
            }
        }
        return errs;
    }

    /** The fixture's 5-minute song, built at test time (false, with a failed check, when it cannot be). */
    bool fiveMinuteSong (Audio& out)
    {
        JsonValue doc;
        if (! fixture::load ("analysis/detect.json", doc))
            return false;
        const JsonValue& cases = doc["cases"];
        for (std::size_t i = 0; i < cases.size(); ++i)
            if (const JsonValue* song = cases[i]["input"].find ("song"); song != nullptr && fixture::same ((*song)["seconds"].asNumber(), 300.0))
            {
                std::string why;
                const bool ok = songFrom (*song, out, why);
                check (ok, "the 5-minute song of analysis/detect.json is made at test time" + (ok ? std::string() : ": " + why));
                return ok;
            }
        check (false, "analysis/detect.json holds the 5-minute song");
        return false;
    }
}

//==================================================================================================
SL_TEST ("analysis", detectFixture)
{
    section ("analysis/detect.json: detectPitch, detectKey and suggestParts against the prototype, case by case");

    JsonValue doc;
    if (! fixture::load ("analysis/detect.json", doc))
        return;
    const JsonValue& cases = doc["cases"];
    check (cases.size() == kDetectCases, fmt ("the fixture has %zu cases (%zu expected: 28, and 6 appended in M7 V1)", cases.size(), kDetectCases));
    modelfx::FileTally tally ("analysis/detect.json", cases.size());

    int inputsExact = 0, otherCases = 0, ruleNarrow = 0;
    bool dcSeen = false;
    long long reads = 0, outside = 0;
    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const JsonValue& input = c["input"];
        const std::string name = core::toJson (input).substr (0, 90);
        Audio audio;
        std::string why;
        if (! inputAudio (input, audio, why))
        {
            tally.fail (name + ": " + why);
            continue;
        }
        if (! fixture::same (static_cast<double> (audio.frames()), c["n"].asNumber())
            || ! fixture::same (static_cast<double> (audio.channels.size()), c["channels"].asNumber())
            || ! fixture::same (audio.sampleRate, c["sampleRate"].asNumber()))
        {
            tally.fail (fmt ("%s: the input is %zu frames x %zu channels at %g Hz, the fixture's %s x %s at %s", name.c_str(), audio.frames(),
                             audio.channels.size(), audio.sampleRate, core::toJson (c["n"]).c_str(), core::toJson (c["channels"]).c_str(),
                             core::toJson (c["sampleRate"]).c_str()));
            continue;
        }
        // the starters are within 2e-7 of the prototype's, not all bit for bit (sounds/starter.json), so the hash is counted, not gated
        if (signals::hex8 (signals::fnvF32 (audio.channels)) == c["hash"].asString())
            ++inputsExact;

        const analysis::FloatSource floats = audio.source();
        const BoundsSource source (floats);
        const auto pitch = analysis::detectPitch (source);
        const auto prototypePitch = analysis::detectPitch (source, {}, { .prototypeDc = true });
        const auto key = analysis::detectKey (source);
        const auto parts = analysis::suggestParts (source);
        reads += source.reads;
        outside += source.outside;

        bool ok = true;
        Exactness exact;
        const bool isDc = input["signal"].asString() == "dc";
        if (isDc)
        {
            // decision 3 beside the prototype's reading: the plugin says "no clear pitch"; the port with the prototype's
            // rule gives the recorded MIDI 90
            dcSeen = true;
            std::string protoWhy;
            const bool protoOk = pitchMatches (prototypePitch, c["detectPitch"], exact, protoWhy);
            check (! pitch.has_value() && protoOk && ! c["detectPitch"].isNull(),
                   "DC 0.5 at 48 kHz: the plugin says " + pitchText (pitch) + " (decision 3); the prototype's reading " + core::toJson (c["detectPitch"])
                       + " is kept in the fixture, and the port with the prototype's rule gives " + pitchText (prototypePitch));
            if (pitch.has_value() || ! protoOk)
            {
                ok = false;
                why = pitch.has_value() ? "DC: the plugin found " + pitchText (pitch) : protoWhy;
            }
        }
        else
        {
            ++otherCases;
            if (samePitch (pitch, prototypePitch))
                ++ruleNarrow;
            ok = pitchMatches (pitch, c["detectPitch"], exact, why);
        }
        if (ok)
            ok = keyMatches (key, c["detectKey"], why);
        if (ok)
            ok = partsMatch (parts, c["suggestParts"], exact, why);
        note (fmt ("%2zu %s: %s | %s | %s", ci, name.c_str(), pitchText (pitch).c_str(), keyText (key).c_str(), partsText (parts).c_str()));
        if (ok && ! exact.exact)
            note ("   within tolerance, not bit for bit: " + exact.diffs);
        if (ok)
            tally.pass (exact.exact);
        else
            tally.fail (name + ": " + why);
    }
    tally.finish();
    note (fmt ("%d of %zu inputs are bit-exact copies of the prototype's (the starter sounds are within 2e-7, not all bit for bit)", inputsExact,
               cases.size()));
    check (dcSeen, "the fixture holds the DC case");
    check (otherCases > 0 && ruleNarrow == otherCases,
           fmt ("decision 3 changes nothing else: detectPitch with and without the prototype's DC reading agree on %d of the %d other cases", ruleNarrow,
                otherCases));
    check (reads > 0 && outside == 0, fmt ("every read stays inside the sound: %lld reads, %lld outside", reads, outside));
}

//==================================================================================================
SL_TEST ("analysis", detectionSyntheticTones)
{
    section ("Detection (PROMPT 14.11): synthetic tones at 5 pitches within +-10 cents (offsets 0, +23, -37 c; 1 and 24 harmonics; "
             "44.1 and 48 kHz)");

    const char* names[] = { "C1 (MIDI 36)", "A1 (MIDI 45)", "A2 (MIDI 57)", "A3 (MIDI 69)", "C5 (MIDI 84)" };
    const int midis[] = { 36, 45, 57, 69, 84 };
    for (std::size_t mi = 0; mi < 5; ++mi)
    {
        const int m = midis[mi];
        double worst = 0;
        int found = 0, total = 0;
        std::string off;
        for (const double sr : { 44100.0, 48000.0 })
            for (const int harm : { 1, 24 })
                for (const double c : { 0.0, 23.0, -37.0 })
                {
                    ++total;
                    const std::vector<std::vector<float>> x { testTone (m, c, sr, 1.2, harm, m * 7 + harm) };
                    const auto p = analysis::detectPitch (analysis::FloatSource::of (x, sr));
                    if (! p)
                    {
                        off += fmt ("%+.0f c h%d at %.0f Hz: no pitch; ", c, harm, sr);
                        continue;
                    }
                    ++found;
                    const double err = (p->midi + p->cents / 100 - (m + c / 100)) * 100;
                    worst = std::max (worst, std::fabs (err));
                    if (std::fabs (err) > 10)
                        off += fmt ("%+.0f c h%d at %.0f Hz: %+.1f c; ", c, harm, sr, err);
                }
        check (found == total && worst <= 10,
               fmt ("%s: %d of %d tones found, the largest error %.2f cents (at most 10)%s%s", names[mi], found, total, worst, off.empty() ? "" : "; ",
                    off.c_str()));
    }
}

SL_TEST ("analysis", detectionKeyProgressions)
{
    section ("Detection (PROMPT 14.11): chord progressions, I-IV-V-I in C and G major and i-iv-V-i in A minor, 1.5 s per chord");

    struct Prog
    {
        const char* name;
        std::vector<std::vector<int>> chords;
        int key;
        analysis::Scale scale;
    };
    const Prog progs[] = {
        { "C major I-IV-V-I", { { 48, 60, 64, 67 }, { 53, 60, 65, 69 }, { 55, 59, 62, 67 }, { 48, 60, 64, 67 } }, 0, analysis::Scale::major },
        { "G major I-IV-V-I", { { 43, 55, 59, 62 }, { 48, 60, 64, 67 }, { 50, 57, 62, 66 }, { 43, 55, 59, 62 } }, 7, analysis::Scale::major },
        { "A minor i-iv-V-i", { { 45, 57, 60, 64 }, { 50, 57, 62, 65 }, { 52, 56, 59, 64 }, { 45, 57, 60, 64 } }, 9, analysis::Scale::minor },
    };
    for (const Prog& pr : progs)
    {
        const std::vector<float> x = progression (pr.chords, 44100, 1.5);
        const std::vector<std::vector<float>> stereo { x, x };
        const auto k = analysis::detectKey (analysis::FloatSource::of (stereo, 44100));
        check (k.has_value() && k->key == pr.key && k->scale == pr.scale,
               fmt ("%s: detected as %s", pr.name, keyText (k).c_str()));
    }
    const std::vector<std::vector<float>> silence { std::vector<float> (44100, 0.0f) };
    check (! analysis::detectKey (analysis::FloatSource::of (silence, 44100)).has_value(), "1 s of silence has no key");
}

SL_TEST ("analysis", detectionNoClearPitch)
{
    section ("Detection (PROMPT 14.11): white noise gives \"no clear pitch\"; so do silence, a 30 ms note and DC (decision 3)");

    std::string res;
    int nulls = 0;
    for (std::int64_t seed = 1; seed <= 6; ++seed)
    {
        std::vector<float> x (44100 * 3);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> ((testHash (seed, static_cast<std::int64_t> (i)) * 2 - 1) * 0.5);
        const std::vector<std::vector<float>> stereo { x, x };
        const auto p = analysis::detectPitch (analysis::FloatSource::of (stereo, 44100));
        nulls += p ? 0 : 1;
        res += (res.empty() ? "" : ", ") + pitchText (p);
    }
    check (nulls == 6, "white noise, 3 s at 44.1 kHz, 6 seeds: " + res);

    const std::vector<std::vector<float>> silence { std::vector<float> (88200, 0.0f) };
    const std::vector<std::vector<float>> shortNote { testTone (60, 0, 44100, 0.03, 4, 1) };
    const auto ps = analysis::detectPitch (analysis::FloatSource::of (silence, 44100));
    const auto pn = analysis::detectPitch (analysis::FloatSource::of (shortNote, 44100));
    check (! ps && ! pn, "2 s of silence: " + pitchText (ps) + "; a 30 ms note: " + pitchText (pn));

    // decision 3 is narrow: DC alone is no pitch, a tone riding on the same DC keeps its pitch
    const std::vector<std::vector<float>> dc { std::vector<float> (72000, 0.5f) };
    std::vector<float> dcTone = testTone (69, 0, 48000, 1.5, 8, 5);
    for (float& v : dcTone)
        v = static_cast<float> (static_cast<double> (v) * 0.4 + 0.3);
    const std::vector<std::vector<float>> onDc { dcTone };
    const auto pd = analysis::detectPitch (analysis::FloatSource::of (dc, 48000));
    const auto pt = analysis::detectPitch (analysis::FloatSource::of (onDc, 48000));
    check (! pd, "DC 0.5, 1.5 s at 48 kHz: " + pitchText (pd) + " (the prototype read MIDI 90)");
    check (pt && pt->midi == 69 && std::fabs (pt->cents) <= 10, "A3 riding on a DC offset of 0.3: " + pitchText (pt));
}

//==================================================================================================
SL_TEST ("analysis", suggestPartsFiveMinutes)
{
    section ("Speed: suggestParts on the 5-minute stereo song of tests/test-analysis.js, made at test time (target 1.0 s)");

    Audio song;
    const auto t0 = std::chrono::steady_clock::now();
    if (! fiveMinuteSong (song))
        return;
    note (fmt ("the song: %zu frames x %zu channels at %.0f Hz, made in %.0f ms", song.frames(), song.channels.size(), song.sampleRate,
               1000 * secondsSince (t0)));
    const analysis::FloatSource source = song.source();

    std::vector<double> ms;
    std::vector<analysis::Part> first, parts;
    for (int run = 0; run < 3; ++run)
    {
        const auto t = std::chrono::steady_clock::now();
        parts = analysis::suggestParts (source);
        ms.push_back (1000 * secondsSince (t));
        if (run == 0)
            first = parts;
    }
    note (fmt ("3 runs: %.1f / %.1f / %.1f ms", ms[0], ms[1], ms[2]));
    timing ("suggestParts on 5:00 of stereo 44.1 kHz (the slowest of 3 runs)", *std::max_element (ms.begin(), ms.end()), 1000);
    check (sameParts (first, parts), "suggestParts is deterministic: three runs give the same parts");
    const std::string problems = partProblems (parts, static_cast<double> (song.frames()) / song.sampleRate);
    check (parts.size() >= 3 && problems.empty(), fmt ("%zu valid parts: %s%s%s", parts.size(), partsText (parts).c_str(), problems.empty() ? "" : "; ",
                                                       problems.c_str()));

    auto t = std::chrono::steady_clock::now();
    const auto p = analysis::detectPitch (source);
    const double pitchMs = 1000 * secondsSince (t);
    t = std::chrono::steady_clock::now();
    const auto k = analysis::detectKey (source);
    const double keyMs = 1000 * secondsSince (t);
    note (fmt ("the same song: detectPitch %.1f ms (%s), detectKey %.1f ms (%s); no target", pitchMs, pitchText (p).c_str(), keyMs, keyText (k).c_str()));
}

SL_TEST ("analysis", longFileMemoryAndStop)
{
    section ("A long file (M7 V2 relies on it): no full-length copy is kept, and Stop ends an analysis early");

    Audio song;
    if (! fiveMinuteSong (song))
        return;
    const analysis::FloatSource source = song.source();
    // the prototype's copies: a full-length mono Float32Array (detectPitch, detectKey) and an 11 kHz one (suggestParts)
    const auto monoBytes = static_cast<long long> (song.frames() * sizeof (float));
    const long long elevenBytes = monoBytes / 4, bound = elevenBytes / 2;

    std::optional<analysis::Pitch> pitch;
    std::optional<analysis::Key> key;
    std::vector<analysis::Part> parts;
    alloc::Counts counted[3];
    for (int f = 0; f < 3; ++f)
    {
        alloc::reset();
        {
            alloc::Scope scope;
            if (f == 0)
                pitch = analysis::detectPitch (source);
            else if (f == 1)
                key = analysis::detectKey (source);
            else
                parts = analysis::suggestParts (source);
        }
        counted[f] = alloc::counts();
    }
    const char* fnNames[] = { "detectPitch", "detectKey", "suggestParts" };
    for (int f = 0; f < 3; ++f)
        check (counted[f].peakLiveBytes < bound,
               fmt ("%s: at most %.2f MB live (the largest block %.2f MB), under half the prototype's 11 kHz copy (%.2f MB); its mono copy is %.2f MB",
                    fnNames[f], static_cast<double> (counted[f].peakLiveBytes) / 1e6, static_cast<double> (counted[f].largest) / 1e6,
                    static_cast<double> (elevenBytes) / 1e6, static_cast<double> (monoBytes) / 1e6));

    // a Stop that never fires changes nothing, and is asked all the way through
    int asked = 0;
    const analysis::Stop never = [&asked] { ++asked; return false; };
    const auto pitch2 = analysis::detectPitch (source, never);
    const int askedPitch = asked;
    const auto key2 = analysis::detectKey (source, never);
    const int askedKey = asked - askedPitch;
    const auto parts2 = analysis::suggestParts (source, never);
    const int askedParts = asked - askedPitch - askedKey;
    check (samePitch (pitch, pitch2) && key.has_value() == key2.has_value()
               && (! key || (key->key == key2->key && key->scale == key2->scale && fixture::same (key->r, key2->r))) && sameParts (parts, parts2),
           "a Stop that never fires gives the same answers");
    check (askedPitch >= 10 && askedKey >= 10 && askedParts >= 100,
           fmt ("Stop is asked through each analysis: %d times in detectPitch, %d in detectKey, %d in suggestParts", askedPitch, askedKey, askedParts));

    // a Stop that fires on its fifth question ends each analysis with its empty answer
    int n = 0;
    const analysis::Stop fifth = [&n] { return ++n >= 5; };
    const auto stoppedPitch = analysis::detectPitch (source, fifth);
    n = 0;
    const auto stoppedKey = analysis::detectKey (source, fifth);
    n = 0;
    const auto stoppedParts = analysis::suggestParts (source, fifth);
    check (! stoppedPitch && ! stoppedKey && stoppedParts.empty() && pitch && key && ! parts.empty(),
           "a Stop that fires part-way gives no pitch, no key and no parts (the song has all three)");
}

//==================================================================================================
SL_TEST ("analysis", tempoOfResampledBeats)
{
    section ("The loop part's tempo on the Beat loop resampled to other tempos, 8 bars each (tests/test-analysis.js): within 1 bpm");

    const auto beat = sounds::sharedStarterSound (sounds::StarterId::beat, 1);
    const std::vector<float>& src = beat->left;
    std::string res;
    bool ok = true;
    for (const double bpm : { 92.0, 100.0, 108.0, 128.0, 140.0 })
    {
        const double ratio = 120 / bpm;
        const auto n = static_cast<std::size_t> (core::jsRound (static_cast<double> (src.size()) * ratio));
        constexpr std::size_t reps = 8;
        std::vector<std::vector<float>> x { std::vector<float> (n * reps) };
        for (std::size_t r = 0; r < reps; ++r)
            for (std::size_t i = 0; i < n; ++i)
            {
                const double p = static_cast<double> (i) / ratio, i0 = std::floor (p), fr = p - i0;
                const auto k = static_cast<std::size_t> (i0);
                x[0][r * n + i] = static_cast<float> (static_cast<double> (src[k % src.size()]) * (1 - fr) + static_cast<double> (src[(k + 1) % src.size()]) * fr);
            }
        const auto parts = analysis::suggestParts (analysis::FloatSource::of (x, 44100));
        const analysis::Part* loop = nullptr;
        for (const analysis::Part& p : parts)
            if (p.kind == analysis::PartKind::loop)
                loop = &p;
        res += fmt ("%s%.0f -> %s", res.empty() ? "" : ", ", bpm, loop ? fmt ("%g%s", loop->bpm, loop->tempoFound ? "" : " (guess)").c_str() : "none");
        if (loop == nullptr || ! loop->tempoFound || std::fabs (loop->bpm - bpm) > 1)
            ok = false;
    }
    check (ok, res + " bpm (each within 1)");
}

SL_TEST ("analysis", analysisPieces)
{
    section ("The pieces: the FFT against a direct DFT, overlapOk and estimateTempo against the prototype's values");

    // fft against a direct DFT of the same 512 values
    const analysis::FftPlan plan (512);
    bool permutation = true;
    for (std::size_t i = 0; i < 512; ++i)
        permutation = permutation && plan.rev[i] < 512 && plan.rev[plan.rev[i]] == i;
    check (permutation, "the bit-reversal table is a permutation that undoes itself");
    std::vector<double> re (512), im (512), x (512);
    signals::Lcg r (5);
    for (std::size_t i = 0; i < 512; ++i)
    {
        x[i] = std::sin (0.3 * static_cast<double> (i)) + 0.5 * std::cos (2.1 * static_cast<double> (i)) + (r() - 0.5);
        re[i] = x[i];
    }
    analysis::fft (re.data(), im.data(), plan);
    double worst = 0, scale = 0;
    for (std::size_t k = 0; k < 512; ++k)
    {
        double sr = 0, si = 0;
        for (std::size_t j = 0; j < 512; ++j)
        {
            const double a = -2 * std::numbers::pi * static_cast<double> ((k * j) % 512) / 512;
            sr += x[j] * std::cos (a);
            si += x[j] * std::sin (a);
        }
        worst = std::max ({ worst, std::fabs (re[k] - sr), std::fabs (im[k] - si) });
        scale = std::max (scale, std::hypot (sr, si));
    }
    check (worst <= 1e-12 * scale * 512, fmt ("fft (512) equals the direct DFT: the largest difference %.3g (the largest bin %.1f)", worst, scale));

    // overlapOk: at most half the shorter span, plus 1e-9
    check (analysis::overlapOk ({ 0, 2 }, {}) && analysis::overlapOk ({ 0, 2 }, { { 1, 3 } }) && ! analysis::overlapOk ({ 0, 2 }, { { 0.9, 2.9 } })
               && ! analysis::overlapOk ({ 0, 1 }, { { 5, 6 }, { 0.4, 1.4 } }) && analysis::overlapOk ({ 0, 1 }, { { 5, 6 }, { 0.5, 1.5 } }),
           "overlapOk: half the shorter span overlaps, more does not; every chosen span is checked");

    // estimateTempo on onset curves, against SLAnalysis._estimateTempo (node, tf = 128 / 11025)
    struct Curve
    {
        const char* name;
        std::size_t n;
        int period, half;
        double height, halfHeight;
        double bpm;
        bool found;
    };
    const Curve curves[] = {
        { "a spike every 43 frames", 2000, 43, 0, 20, 0, 120, true },
        { "every 37, a smaller one half-way", 3000, 37, 18, 20, 8, 139.67, true },
        { "every 61, a smaller one half-way", 4000, 61, 30, 15, 9, 84.72, true },
        { "5 spikes (fewer than 8 strong onsets)", 500, 100, 0, 20, 0, 120, false },
    };
    const double tf = 128.0 / 11025;
    for (const Curve& cv : curves)
    {
        std::vector<double> on (cv.n, 0.0);
        for (std::size_t i = 0; i < cv.n; ++i)
        {
            const auto phase = static_cast<int> (i % static_cast<std::size_t> (cv.period));
            on[i] = phase == 0 ? cv.height : cv.half != 0 && phase == cv.half ? cv.halfHeight : 0.0;
        }
        const analysis::Tempo t = analysis::estimateTempo (on, tf);
        check (fixture::same (t.bpm, cv.bpm) && t.found == cv.found,
               fmt ("estimateTempo, %s: %g bpm%s (the prototype: %g%s)", cv.name, t.bpm, t.found ? "" : " (not found)", cv.bpm, cv.found ? "" : " (not found)"));
    }
    const analysis::Tempo flat = analysis::estimateTempo (std::vector<double> (1000, 3.0), tf);
    check (fixture::same (flat.bpm, 120.0) && ! flat.found, "estimateTempo on a flat curve: 120, not found");
}
