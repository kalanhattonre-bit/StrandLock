// The 8 starter sounds against the prototype's own (Tests/fixtures/sounds/starter.json, PLUGIN-PLAN 4 M0 and 5.3):
// lengths and root exact, windows within 2e-7, block RMS within 1e-6 relative, peak within 1e-7, the hash printed;
// how long they take (printed against the targets, gated only past 5 times them: the plan's timing rule).

#include "sounds/StarterSounds.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    double secondsSince (std::chrono::steady_clock::time_point t0)
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    }

    /** FNV-1a 32 over the little-endian bytes of each float channel in order (the prototype tests' fnv). */
    std::uint32_t fnv (const std::vector<float>& left, const std::vector<float>& right)
    {
        std::uint32_t h = 0x811c9dc5u;
        for (const auto* ch : { &left, &right })
            for (const float x : *ch)
            {
                std::uint32_t b = 0;
                std::memcpy (&b, &x, sizeof (b));
                for (int i = 0; i < 4; ++i)
                {
                    h ^= (b >> (8 * i)) & 0xffu;
                    h *= 0x01000193u;
                }
            }
        return h;
    }

    /** sounds.js blockRms: sqrt (mean (x^2)) per block in double, stored as binary32, L R interleaved. */
    std::vector<float> blockRms (const std::vector<float>& L, const std::vector<float>& R, std::size_t block)
    {
        const std::size_t nb = (L.size() + block - 1) / block;
        std::vector<float> out (nb * 2);
        for (std::size_t b = 0; b < nb; ++b)
        {
            const std::size_t a = b * block, e = std::min (L.size(), a + block);
            double sl = 0.0, sr = 0.0;
            for (std::size_t i = a; i < e; ++i)
            {
                sl += (double) L[i] * (double) L[i];
                sr += (double) R[i] * (double) R[i];
            }
            out[2 * b] = (float) std::sqrt (sl / (double) (e - a));
            out[2 * b + 1] = (float) std::sqrt (sr / (double) (e - a));
        }
        return out;
    }

    std::string hex8 (std::uint32_t h) { return fmt ("%08x", (unsigned) h); }
}

SL_TEST ("sounds", starterSoundsMatchThePrototype)
{
    section ("Starter sounds: sl-sounds.js sample for sample (sounds/starter.json)");

    core::JsonValue doc;
    if (! fixture::load ("sounds/starter.json", doc))
        return;

    const auto& cases = doc["cases"];
    check (cases.size() == 10, fmt ("the fixture has 10 cases (8 sounds at seed 1, pad and beat at seed 2): %d", (int) cases.size()));

    int bitExact = 0;
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const auto& k = cases[c];
        const std::string id = k["id"].asString();
        const int seed = (int) k["seed"].asNumber();
        const auto starter = sounds::parseStarter (id);
        if (! starter)
        {
            check (false, fmt ("the fixture's sound \"%s\" is one of the 8 starters", id.c_str()));
            continue;
        }

        const auto made = sounds::makeStarterSound (*starter, seed);
        const std::size_t n = made.frames();
        const bool lengthOk = n == (std::size_t) k["n"].asNumber() && made.right.size() == n;
        const bool rootOk = made.rootMidi == (int) k["rootMidi"].asNumber();
        const bool rateOk = fixture::same (made.sampleRate, k["sampleRate"].asNumber());

        // Three 512-frame windows: at 0, n / 2 and n - 512.
        double windowDiff = 0.0;
        bool windowsOk = lengthOk;
        const auto& windows = k["windows"];
        for (std::size_t w = 0; w < windows.size() && lengthOk; ++w)
        {
            const auto at = (std::size_t) windows[w]["at"].asNumber();
            std::vector<float> wl, wr;
            if (! fixture::f32 (windows[w]["L"], wl) || ! fixture::f32 (windows[w]["R"], wr) || at + wl.size() > n || wl.size() != wr.size())
            {
                windowsOk = false;
                continue;
            }
            for (std::size_t i = 0; i < wl.size(); ++i)
            {
                windowDiff = std::max (windowDiff, std::abs ((double) made.left[at + i] - (double) wl[i]));
                windowDiff = std::max (windowDiff, std::abs ((double) made.right[at + i] - (double) wr[i]));
            }
        }
        windowsOk = windowsOk && windows.size() == 3 && windowDiff <= 2e-7;

        // Block RMS per 4096 frames, relative.
        std::vector<float> wantRms;
        const bool rmsRead = fixture::f32 (k["blockRms"], wantRms);
        const auto gotRms = blockRms (made.left, made.right, 4096);
        double rmsRel = 0.0;
        const bool rmsSized = rmsRead && wantRms.size() == gotRms.size();
        if (rmsSized)
            for (std::size_t i = 0; i < gotRms.size(); ++i)
            {
                const double a = gotRms[i], b = wantRms[i];
                const double scale = std::max (std::abs (a), std::abs (b));
                if (scale > 0.0)
                    rmsRel = std::max (rmsRel, std::abs (a - b) / scale);
            }
        const bool rmsOk = rmsSized && rmsRel <= 1e-6;

        double peak = 0.0;
        for (const auto* ch : { &made.left, &made.right })
            for (const float x : *ch)
                peak = std::max (peak, (double) std::abs (x));
        const double peakDiff = std::abs (peak - k["peak"].asNumber());

        const std::string hash = hex8 (fnv (made.left, made.right));
        const bool same = hash == k["hash"].asString();
        if (same)
            ++bitExact;

        check (lengthOk && rootOk && rateOk && windowsOk && rmsOk && peakDiff <= 1e-7,
               fmt ("%s seed %d: %d frames (%d), root %d, %.0f Hz; windows within %.2g (2e-7); block rms within %.2g relative (1e-6); "
                    "peak %.9f (diff %.2g, 1e-7); hash %s %s", id.c_str(), seed, (int) n, (int) k["n"].asNumber(), made.rootMidi,
                    made.sampleRate, windowDiff, rmsRel, peak, peakDiff, hash.c_str(), same ? "(bit-exact)" : ("(prototype " + k["hash"].asString() + ")").c_str()));
    }
    note (fmt ("%d of %d sounds are bit-exact copies of the prototype's", bitExact, (int) cases.size()));
}

SL_TEST ("sounds", starterSoundsListAndIds)
{
    section ("Starter sounds: the list, its order and its ids");

    const char* expected[] = { "sawopen", "saw", "square", "pad", "vowel", "pluck", "beat", "noise" };
    bool orderOk = true;
    for (int i = 0; i < sounds::kNumStarters; ++i)
        orderOk = orderOk && std::strcmp (sounds::kStarterList[i].code, expected[i]) == 0 && (int) sounds::kStarterList[i].id == i;
    check (orderOk, "SLSounds.list order: sawopen, saw, square, pad, vowel, pluck, beat, noise (the Saw that opens up first, D9)");
    check (sounds::kDefaultStarter == sounds::StarterId::sawopen, "the default starter is sawopen");

    bool parseOk = true;
    for (const auto& s : sounds::kStarterList)
        parseOk = parseOk && sounds::parseStarter (s.code) == s.id && &sounds::starterInfo (s.id) == &s;
    check (parseOk, "every id parses back to its starter");
    check (! sounds::parseStarter ("Saw") && ! sounds::parseStarter ("") && ! sounds::parseStarter ("organ"),
           "\"Saw\", \"\" and \"organ\" are not starter ids (ids are case-sensitive)");

    // SLSounds' own hashes (not the model's MurmurHash), against values node printed from sl-sounds.js's code:
    // strHash ('saw'), fmix32 (1), hash32 (1, strHash ('saw')) and hash32 (-5, 7, 0x5157).
    const auto sawTag = sounds::hash32 ({ 1u, sounds::strHash ("saw") });
    const auto negative = sounds::hash32 ({ static_cast<std::uint32_t> (-5), 7u, 0x5157u });
    check (sounds::strHash ("saw") == 0xd71c946au, fmt ("strHash (\"saw\") = 0x%08x (0xd71c946a)", (unsigned) sounds::strHash ("saw")));
    check (sounds::fmix32 (0u) == 0u && sounds::fmix32 (1u) == 0x514e28b7u,
           fmt ("fmix32 (0) = 0 and fmix32 (1) = 0x%08x (0x514e28b7)", (unsigned) sounds::fmix32 (1u)));
    check (sawTag == 0xb65e0b16u && negative == 0xc51472d2u,
           fmt ("hash32 (1, strHash (\"saw\")) = 0x%08x (0xb65e0b16), hash32 (-5, 7, 0x5157) = 0x%08x (0xc51472d2)",
                (unsigned) sawTag, (unsigned) negative));

    const auto a = sounds::sharedStarterSound (sounds::StarterId::saw, 1);
    const auto b = sounds::sharedStarterSound (sounds::StarterId::saw, 1);
    const auto c = sounds::sharedStarterSound (sounds::StarterId::saw, 2);
    check (a != nullptr && a == b && a != c && c != nullptr && a->left != c->left,
           "sharedStarterSound keeps one copy per (id, seed); another seed is another sound");
}

SL_TEST ("sounds", starterSoundsTiming)
{
    section ("Starter sounds: how long they take (printed; gated only past 5 times the target)");

    auto t0 = std::chrono::steady_clock::now();
    const auto sawopen = sounds::makeStarterSound (sounds::StarterId::sawopen, 1);
    const double sawopenMs = secondsSince (t0) * 1000.0;
    note (fmt ("sawopen made synchronously in %.1f ms (target 60 ms; the JS takes 88 ms)", sawopenMs));
    check (sawopen.frames() > 0 && sawopenMs <= 5.0 * 60.0, fmt ("sawopen in %.1f ms, under 5 x the 60 ms target", sawopenMs));

    t0 = std::chrono::steady_clock::now();
    std::size_t frames = 0;
    for (const auto& s : sounds::kStarterList)
        frames += sounds::makeStarterSound (s.id, 1).frames();
    const double all = secondsSince (t0);
    note (fmt ("all 8 made in %.3f s (target 2 s)", all));
    check (frames > 0 && all <= 5.0 * 2.0, fmt ("all 8 starters in %.3f s, under 5 x the 2 s target", all));
}
