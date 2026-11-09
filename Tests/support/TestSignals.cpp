// The prototype tests' signal makers (see TestSignals.h). Each function follows its JavaScript line by line;
// the comment above it is the original.

#include "TestSignals.h"

#include "core/JsCompat.h"
#include "core/JsMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace sl::test::signals
{
    namespace
    {
        constexpr double kPi = 3.141592653589793;   // Math.PI

        // `x || d` for a number: 0 and NaN are falsy.
        double orDefault (double x, double d) noexcept
        {
            return x == 0.0 || x != x ? d : x;
        }

        // new Float32Array (Math.round (secs * sr)).
        std::size_t frames (double secs, double sr) noexcept
        {
            const double n = core::jsRound (secs * sr);
            return n > 0.0 && n < 1e9 ? static_cast<std::size_t> (n) : 0;
        }

        // x[i] += v on a Float32Array.
        void addTo (float& x, double v) noexcept
        {
            x = static_cast<float> (static_cast<double> (x) + v);
        }

        // let pk = 0; for (...) pk = Math.max (pk, Math.abs (x[i])); for (...) x[i] *= amp / pk;
        void normalise (std::vector<float>& x, double amp) noexcept
        {
            double pk = 0.0;
            for (const float v : x)
                pk = std::max (pk, std::abs (static_cast<double> (v)));
            const double g = amp / pk;
            for (float& v : x)
                v = static_cast<float> (static_cast<double> (v) * g);
        }

        double argOr (const core::JsonValue& args, std::size_t i, double fallback) noexcept
        {
            return args.isArray() && i < args.size() && args[i].isNumber() ? args[i].asNumber() : fallback;
        }

        bool hasNumber (const core::JsonValue& args, std::size_t i) noexcept
        {
            return args.isArray() && i < args.size() && args[i].isNumber();
        }
    }

    // function lcg(seed) { let s = seed >>> 0 || 1;
    //   return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }
    Lcg::Lcg (double seed) noexcept
        : s (core::toUint32 (seed))
    {
        if (s == 0)
            s = 1;
    }

    double Lcg::operator()() noexcept
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<double> (s) / 4294967296.0;
    }

    // const midiHz = m => 440 * Math.pow(2, (m - 69) / 12);
    double midiHz (double m) noexcept
    {
        return 440 * std::pow (2.0, (m - 69) / 12);
    }

    // function tone(f0, secs, sr, opts = {}) {
    //   const n = Math.round(secs * sr), K = opts.partials || 12, amp = opts.amp || 0.5, r = lcg(opts.seed || 7);
    //   const x = new Float32Array(n), ph = [], a = [];
    //   let norm = 0;
    //   for (let k = 1; k <= K; k++) { ph.push(r() * 2 * Math.PI); a.push(1 / k); norm += 1 / k; }
    //   for (let k = 1; k <= K; k++) {
    //     if (f0 * k >= sr * 0.45) break;
    //     const w = 2 * Math.PI * f0 * k / sr, ak = a[k - 1] / norm * 2;
    //     for (let i = 0; i < n; i++) x[i] += ak * Math.sin(w * i + ph[k - 1]);
    //   }
    //   (peak normalise to amp)
    std::vector<float> tone (double f0, double secs, double sr, const ToneOpts& opts)
    {
        const std::size_t n = frames (secs, sr);
        const double K = orDefault (opts.partials, 12.0), amp = orDefault (opts.amp, 0.5);
        Lcg r (orDefault (opts.seed, 7.0));
        std::vector<float> x (n, 0.0f);
        std::vector<double> ph, a;
        double norm = 0.0;
        for (int k = 1; k <= K; k++)
        {
            ph.push_back (r() * 2 * kPi);
            a.push_back (1.0 / k);
            norm += 1.0 / k;
        }
        for (int k = 1; k <= K; k++)
        {
            if (f0 * k >= sr * 0.45)
                break;
            const auto ki = static_cast<std::size_t> (k - 1);
            const double w = 2 * kPi * f0 * k / sr, ak = a[ki] / norm * 2;
            for (std::size_t i = 0; i < n; i++)
                addTo (x[i], ak * std::sin (w * static_cast<double> (i) + ph[ki]));
        }
        normalise (x, amp);
        return x;
    }

    // function noise(secs, sr, seed = 3, amp = 0.5) {
    //   const n = Math.round(secs * sr), r = lcg(seed), x = new Float32Array(n);
    //   for (let i = 0; i < n; i++) x[i] = (r() * 2 - 1) * amp;
    std::vector<float> noise (double secs, double sr, double seed, double amp)
    {
        const std::size_t n = frames (secs, sr);
        Lcg r (seed);
        std::vector<float> x (n, 0.0f);
        for (std::size_t i = 0; i < n; i++)
            x[i] = static_cast<float> ((r() * 2 - 1) * amp);
        return x;
    }

    // function formantTone(f0, secs, sr, fc, bw) {
    //   const n = Math.round(secs * sr), x = new Float32Array(n), r = lcg(17);
    //   for (let k = 1; k * f0 < sr * 0.4 && k <= 40; k++) {
    //     const a = Math.exp(-Math.pow((k * f0 - fc) / bw, 2)) + 0.05 / k, w = 2 * Math.PI * f0 * k / sr, ph = r() * 2 * Math.PI;
    //     for (let i = 0; i < n; i++) x[i] += a * Math.sin(w * i + ph);
    //   }
    //   (peak normalise to 0.5)
    std::vector<float> formantTone (double f0, double secs, double sr, double fc, double bw)
    {
        const std::size_t n = frames (secs, sr);
        std::vector<float> x (n, 0.0f);
        Lcg r (17);
        for (int k = 1; k * f0 < sr * 0.4 && k <= 40; k++)
        {
            const double a = core::jsExp (-std::pow ((k * f0 - fc) / bw, 2.0)) + 0.05 / k, w = 2 * kPi * f0 * k / sr, ph = r() * 2 * kPi;
            for (std::size_t i = 0; i < n; i++)
                addTo (x[i], a * std::sin (w * static_cast<double> (i) + ph));
        }
        normalise (x, 0.5);
        return x;
    }

    // function smoothNoise(secs, sr, seed) {
    //   const a = noise(secs, sr, seed, 1), b = new Float32Array(a.length), c = new Float32Array(a.length);
    //   for (let i = 3; i < a.length; i++) b[i] = (a[i] + a[i - 1] + a[i - 2] + a[i - 3]) / 4;
    //   for (let i = 3; i < a.length; i++) c[i] = (b[i] + b[i - 1] + b[i - 2] + b[i - 3]) / 4;
    //   (peak normalise c to 0.5)
    std::vector<float> smoothNoise (double secs, double sr, double seed)
    {
        const std::vector<float> a = noise (secs, sr, seed, 1);
        std::vector<float> b (a.size(), 0.0f), c (a.size(), 0.0f);
        auto d = [] (float v) { return static_cast<double> (v); };
        for (std::size_t i = 3; i < a.size(); i++)
            b[i] = static_cast<float> ((d (a[i]) + d (a[i - 1]) + d (a[i - 2]) + d (a[i - 3])) / 4);
        for (std::size_t i = 3; i < a.size(); i++)
            c[i] = static_cast<float> ((d (b[i]) + d (b[i - 1]) + d (b[i - 2]) + d (b[i - 3])) / 4);
        normalise (c, 0.5);
        return c;
    }

    // function chord(midis, secs, sr) {
    //   const n = Math.round(secs * sr), x = new Float32Array(n);
    //   midis.forEach((m, j) => { const t = tone(midiHz(m), secs, sr, { partials: 8, seed: 11 + j, amp: 1 }); for (let i = 0; i < n; i++) x[i] += t[i]; });
    //   (peak normalise to 0.5)
    std::vector<float> chord (const std::vector<double>& midis, double secs, double sr)
    {
        const std::size_t n = frames (secs, sr);
        std::vector<float> x (n, 0.0f);
        for (std::size_t j = 0; j < midis.size(); j++)
        {
            ToneOpts o;
            o.partials = 8;
            o.seed = 11.0 + static_cast<double> (j);
            o.amp = 1;
            const std::vector<float> t = tone (midiHz (midis[j]), secs, sr, o);
            for (std::size_t i = 0; i < n; i++)
                addTo (x[i], static_cast<double> (t[i]));
        }
        normalise (x, 0.5);
        return x;
    }

    // function dc(secs, sr, v = 0.5) { return new Float32Array(Math.round(secs * sr)).fill(v); }
    std::vector<float> dc (double secs, double sr, double v)
    {
        return std::vector<float> (frames (secs, sr), static_cast<float> (v));
    }

    // function rampSource(secs, sr) {
    //   const n = Math.round(secs * sr), L = new Float32Array(n), R = new Float32Array(n);
    //   for (let i = 0; i < n; i++) { L[i] = 0.05 + 0.8 * i / n; R[i] = 0.5; }
    //   return { channels: [L, R], sampleRate: sr, name: 'ramp' };
    Signal rampSource (double secs, double sr)
    {
        const std::size_t n = frames (secs, sr);
        Signal s;
        s.channels.assign (2, std::vector<float> (n, 0.0f));
        for (std::size_t i = 0; i < n; i++)
        {
            s.channels[0][i] = static_cast<float> (0.05 + 0.8 * static_cast<double> (i) / static_cast<double> (n));
            s.channels[1][i] = 0.5f;
        }
        s.sampleRate = sr;
        s.name = "ramp";
        return s;
    }

    bool makeSignal (std::string_view fn, const core::JsonValue& args, Signal& out)
    {
        if (! args.isArray())
            return false;

        Signal s;
        s.name = std::string (fn);
        if (fn == "tone")
        {
            if (! hasNumber (args, 0) || ! hasNumber (args, 1) || ! hasNumber (args, 2))
                return false;
            ToneOpts o;
            if (args.size() > 3 && args[3].isObject())
            {
                const core::JsonValue& opts = args[3];
                if (const auto* v = opts.find ("partials"); v != nullptr && v->isNumber())
                    o.partials = v->asNumber();
                if (const auto* v = opts.find ("amp"); v != nullptr && v->isNumber())
                    o.amp = v->asNumber();
                if (const auto* v = opts.find ("seed"); v != nullptr && v->isNumber())
                    o.seed = v->asNumber();
            }
            s.sampleRate = args[2].asNumber();
            s.channels.push_back (tone (args[0].asNumber(), args[1].asNumber(), s.sampleRate, o));
        }
        else if (fn == "noise")
        {
            if (! hasNumber (args, 0) || ! hasNumber (args, 1))
                return false;
            s.sampleRate = args[1].asNumber();
            s.channels.push_back (noise (args[0].asNumber(), s.sampleRate, argOr (args, 2, 3.0), argOr (args, 3, 0.5)));
        }
        else if (fn == "formantTone")
        {
            for (std::size_t i = 0; i < 5; ++i)
                if (! hasNumber (args, i))
                    return false;
            s.sampleRate = args[2].asNumber();
            s.channels.push_back (formantTone (args[0].asNumber(), args[1].asNumber(), s.sampleRate, args[3].asNumber(), args[4].asNumber()));
        }
        else if (fn == "smoothNoise")
        {
            if (! hasNumber (args, 0) || ! hasNumber (args, 1))
                return false;
            s.sampleRate = args[1].asNumber();
            s.channels.push_back (smoothNoise (args[0].asNumber(), s.sampleRate, argOr (args, 2, 3.0)));
        }
        else if (fn == "chord")
        {
            if (args.size() < 3 || ! args[0].isArray() || ! hasNumber (args, 1) || ! hasNumber (args, 2))
                return false;
            std::vector<double> midis;
            for (std::size_t i = 0; i < args[0].size(); ++i)
                midis.push_back (args[0][i].asNumber());
            s.sampleRate = args[2].asNumber();
            s.channels.push_back (chord (midis, args[1].asNumber(), s.sampleRate));
        }
        else if (fn == "rampSource")
        {
            if (! hasNumber (args, 0) || ! hasNumber (args, 1))
                return false;
            s = rampSource (args[0].asNumber(), args[1].asNumber());
        }
        else if (fn == "dc")
        {
            if (! hasNumber (args, 0) || ! hasNumber (args, 1))
                return false;
            s.sampleRate = args[1].asNumber();
            s.channels.push_back (dc (args[0].asNumber(), s.sampleRate, argOr (args, 2, 0.5)));
        }
        else
        {
            return false;   // "lcg" makes numbers, not a signal; anything else is unknown
        }

        out = std::move (s);
        return true;
    }

    bool makeSignal (const core::JsonValue& spec, Signal& out)
    {
        const core::JsonValue* fn = spec.find ("signal");
        const core::JsonValue* args = spec.find ("args");
        if (fn == nullptr || ! fn->isString() || args == nullptr)
            return false;
        return makeSignal (fn->asString(), *args, out);
    }

    std::uint32_t fnvF32 (const std::vector<std::vector<float>>& channels) noexcept
    {
        std::uint32_t h = 0x811c9dc5u;
        for (const auto& c : channels)
            for (const float v : c)
            {
                std::uint32_t bits = 0;
                std::memcpy (&bits, &v, sizeof (bits));
                for (int b = 0; b < 4; ++b)   // little-endian byte order, whatever the machine's
                {
                    h ^= (bits >> (8 * b)) & 0xffu;
                    h *= 0x01000193u;
                }
            }
        return h;
    }

    std::string hex8 (std::uint32_t h)
    {
        static const char digits[] = "0123456789abcdef";
        std::string s (8, '0');
        for (int i = 7; i >= 0; --i)
        {
            s[static_cast<std::size_t> (i)] = digits[h & 0xfu];
            h >>= 4;
        }
        return s;
    }
}
