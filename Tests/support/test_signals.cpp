// TestSignals against Tests/fixtures/support/signals.json (PLUGIN-PLAN 5.3): every case, lcg values exact, every
// other signal's length, channels and rate exact and its first and last 256 samples within 2 Float32 ulps; the
// whole-signal hash is compared and printed (the bit-for-bit count). Group "engine": the engine and analysis
// fixtures name their sounds with these functions, and the sanitizer job runs this group.

#include "Check.h"
#include "FixtureJson.h"
#include "TestRegistry.h"
#include "TestSignals.h"
#include "model/ModelFixture.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using namespace sl::test;
    using sl::core::JsonValue;

    // got[from .. from + want.size ()) against want, within maxUlps; exact is cleared on any difference.
    bool windowMatches (const std::vector<float>& got, std::size_t from, const std::vector<float>& want, std::uint64_t maxUlps, bool& exact,
                        std::string& why)
    {
        if (from + want.size() > got.size())
        {
            why = fmt ("window of %zu at %zu runs past the %zu frames", want.size(), from, got.size());
            return false;
        }
        for (std::size_t i = 0; i < want.size(); ++i)
        {
            const std::uint64_t u = fixture::ulps (got[from + i], want[i]);
            if (u != 0)
                exact = false;
            if (u > maxUlps)
            {
                why = fmt ("frame %zu: %.9g against %.9g (%llu ulps)", from + i, static_cast<double> (got[from + i]),
                           static_cast<double> (want[i]), static_cast<unsigned long long> (u));
                return false;
            }
        }
        return true;
    }

    // head / tail: one F32 for a mono signal, [F32, F32] for two channels.
    bool readWindows (const JsonValue& v, std::size_t channels, std::vector<std::vector<float>>& out)
    {
        out.clear();
        if (channels == 1)
        {
            out.emplace_back();
            return fixture::f32 (v, out.back());
        }
        if (! v.isArray() || v.size() != channels)
            return false;
        for (std::size_t c = 0; c < channels; ++c)
        {
            out.emplace_back();
            if (! fixture::f32 (v[c], out.back()))
                return false;
        }
        return true;
    }
}

SL_TEST ("engine", testSignalsMatchTheFixture)
{
    section ("TestSignals reproduce the prototype tests' signal makers (support/signals.json)");

    JsonValue doc;
    if (! fixture::load ("support/signals.json", doc))
        return;
    const JsonValue& cases = doc["cases"];
    modelfx::FileTally tally ("support/signals.json", cases.size());
    int hashesEqual = 0, signalCases = 0;

    for (std::size_t ci = 0; ci < cases.size(); ++ci)
    {
        const JsonValue& c = cases[ci];
        const std::string fn = c["fn"].asString();
        const JsonValue& args = c["args"];

        if (fn == "lcg")
        {
            // 64 values, exact (doubles).
            signals::Lcg r (args[0].asNumber());
            const JsonValue& values = c["values"];
            bool ok = values.size() == 64;
            std::string why = ok ? "" : fmt ("%zu values in the file", values.size());
            for (std::size_t i = 0; ok && i < values.size(); ++i)
            {
                const double got = r(), want = values[i].asNumber();
                if (! fixture::same (got, want))
                {
                    ok = false;
                    why = fmt ("value %zu: %.17g against %.17g", i, got, want);
                }
            }
            if (ok)
                tally.pass (true);
            else
                tally.fail (fmt ("lcg (%g): %s", args[0].asNumber(), why.c_str()));
            continue;
        }

        ++signalCases;
        signals::Signal s;
        if (! signals::makeSignal (fn, args, s))
        {
            tally.fail ("case " + std::to_string (ci) + ": makeSignal does not know " + fn);
            continue;
        }

        const auto n = static_cast<std::size_t> (c["n"].asNumber());
        const auto channels = static_cast<std::size_t> (c["channels"].asNumber());
        std::string why;
        bool ok = true, exact = true;
        if (s.channels.size() != channels || s.sampleRate != c["sampleRate"].asNumber())
        {
            ok = false;
            why = fmt ("%zu channels at %g Hz, the file has %zu at %g", s.channels.size(), s.sampleRate, channels, c["sampleRate"].asNumber());
        }
        for (std::size_t ch = 0; ok && ch < s.channels.size(); ++ch)
            if (s.channels[ch].size() != n)
            {
                ok = false;
                why = fmt ("channel %zu has %zu frames, the file %zu", ch, s.channels[ch].size(), n);
            }

        std::vector<std::vector<float>> head, tail;
        if (ok && (! readWindows (c["head"], channels, head) || ! readWindows (c["tail"], channels, tail)))
        {
            ok = false;
            why = "unreadable head or tail";
        }
        for (std::size_t ch = 0; ok && ch < channels; ++ch)
        {
            ok = windowMatches (s.channels[ch], 0, head[ch], 2, exact, why)
              && windowMatches (s.channels[ch], n - tail[ch].size(), tail[ch], 2, exact, why);
            if (! ok)
                why = fmt ("channel %zu: ", ch) + why;
        }

        const std::string hash = signals::hex8 (signals::fnvF32 (s.channels));
        const bool hashEqual = hash == c["hash"].asString();
        if (hashEqual)
            ++hashesEqual;
        if (ok)
            tally.pass (exact && hashEqual);
        else
            tally.fail (fn + " " + sl::core::toJson (args) + ": " + why);
    }

    note (fmt ("%d of %d signals have the file's whole-signal hash (bit for bit everywhere)", hashesEqual, signalCases));
    check (signalCases == 9, fmt ("the file has the 9 signal cases the plan lists (%d)", signalCases));
    tally.finish();
}

SL_TEST ("engine", testSignalsCanFail)
{
    section ("The signals comparison can fail: a wrong seed, a wrong rate and an unknown name are caught");

    // A tone with another seed differs in its first samples by far more than 2 ulps.
    signals::ToneOpts seven, eight;
    eight.seed = 8;
    const auto a = signals::tone (261.6255653005986, 0.05, 48000, seven);
    const auto b = signals::tone (261.6255653005986, 0.05, 48000, eight);
    bool exact = true;
    std::string why;
    const std::vector<float> head (b.begin(), b.begin() + 256);
    check (! windowMatches (a, 0, head, 2, exact, why), "seed 7 against seed 8's first 256 samples fails (" + why + ")");

    // The default seed is 7, as `opts.seed || 7`: seed 0 is the same signal.
    signals::ToneOpts zero;
    zero.seed = 0;
    check (signals::tone (261.6255653005986, 0.05, 48000, zero) == a, "seed 0 reads as the default 7, as in JavaScript");

    // Length follows Math.round (secs * sr).
    check (signals::noise (0.5, 44100).size() == 22050 && signals::noise (1.00001, 1000).size() == 1000,
           "lengths are Math.round (secs x sr)");

    signals::Signal s;
    JsonValue args;
    sl::core::parseJson ("[1, 48000]", args);
    check (! signals::makeSignal ("lcg", args, s) && ! signals::makeSignal ("sawtooth", args, s), "lcg and unknown names make no signal");

    // The hash is the fixtures' FNV-1a over little-endian Float32 bytes: one float 1.0f (00 00 80 3f).
    std::uint32_t h = 0x811c9dc5u;
    for (const std::uint32_t byte : { 0x00u, 0x00u, 0x80u, 0x3fu })
    {
        h ^= byte;
        h *= 0x01000193u;
    }
    check (signals::fnvF32 ({ { 1.0f } }) == h && signals::hex8 (0x0a0b0c0du) == "0a0b0c0d", "fnvF32 and hex8 follow the fixtures' hash");
}
