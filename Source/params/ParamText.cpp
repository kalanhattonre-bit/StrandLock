#include "ParamText.h"

#include "ParamIds.h"
#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/Music.h"
#include "core/Rates.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace sl::params
{
    namespace
    {
        using core::jsNumberToString;
        using core::jsRound;
        using core::jsToFixed;
        using core::jsToPrecision;

        bool isSpace (char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
        bool isDigit (char c) noexcept { return c >= '0' && c <= '9'; }
        bool isWordChar (char c) noexcept { return isDigit (c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
        char lower (char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char> (c - 'A' + 'a') : c; }

        std::string lowerAscii (std::string_view s)
        {
            std::string out (s);
            for (char& c : out)
                c = lower (c);
            return out;
        }

        /** String.prototype.trim for the spaces a typed value can hold: ASCII white space and U+00A0. */
        std::string_view trim (std::string_view s) noexcept
        {
            auto startsSpace = [] (std::string_view v, std::size_t& len)
            {
                if (! v.empty() && isSpace (v.front())) { len = 1; return true; }
                if (v.size() >= 2 && v[0] == '\xc2' && v[1] == '\xa0') { len = 2; return true; }
                return false;
            };
            auto endsSpace = [] (std::string_view v, std::size_t& len)
            {
                if (! v.empty() && isSpace (v.back())) { len = 1; return true; }
                if (v.size() >= 2 && v[v.size() - 2] == '\xc2' && v.back() == '\xa0') { len = 2; return true; }
                return false;
            };
            std::size_t len = 0;
            while (startsSpace (s, len))
                s.remove_prefix (len);
            while (endsSpace (s, len))
                s.remove_suffix (len);
            return s;
        }

        bool contains (std::string_view hay, std::string_view needle)
        {
            return lowerAscii (hay).find (lowerAscii (needle)) != std::string::npos;
        }

        /** parseFloat / Number of a plain decimal: an optional sign, digits, an optional fraction. */
        std::optional<double> readDecimal (std::string_view s) noexcept
        {
            if (! s.empty() && s.front() == '+')
                s.remove_prefix (1);
            double v = 0.0;
            const auto r = std::from_chars (s.data(), s.data() + s.size(), v);
            if (r.ec != std::errc() || r.ptr == s.data())
                return std::nullopt;
            return v;
        }

        /** Digits from i; returns the index after them. */
        std::size_t digitsFrom (std::string_view s, std::size_t i) noexcept
        {
            while (i < s.size() && isDigit (s[i]))
                ++i;
            return i;
        }

        /** /^([+-]?\d+(?:\.\d+)?)\s*\/\s*(\d+(?:\.\d+)?)/: fills the two numbers' text when the start matches. */
        bool leadingFraction (std::string_view s, std::string_view& top, std::string_view& bottom) noexcept
        {
            std::size_t i = 0;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                ++i;
            const std::size_t d0 = i;
            i = digitsFrom (s, i);
            if (i == d0)
                return false;
            if (i + 1 < s.size() && s[i] == '.' && isDigit (s[i + 1]))
                i = digitsFrom (s, i + 1);
            top = s.substr (0, i);
            while (i < s.size() && isSpace (s[i]))
                ++i;
            if (i >= s.size() || s[i] != '/')
                return false;
            ++i;
            while (i < s.size() && isSpace (s[i]))
                ++i;
            const std::size_t b0 = i;
            i = digitsFrom (s, i);
            if (i == b0)
                return false;
            if (i + 1 < s.size() && s[i] == '.' && isDigit (s[i + 1]))
                i = digitsFrom (s, i + 1);
            bottom = s.substr (b0, i - b0);
            return true;
        }

        /** The end of /(?:\d+\.?\d*|\.\d+)/ matched at i, or npos. */
        std::size_t unsignedNumberAt (std::string_view s, std::size_t i) noexcept
        {
            if (i < s.size() && isDigit (s[i]))
            {
                i = digitsFrom (s, i);
                if (i < s.size() && s[i] == '.')
                    i = digitsFrom (s, i + 1);
                return i;
            }
            if (i + 1 < s.size() && s[i] == '.' && isDigit (s[i + 1]))
                return digitsFrom (s, i + 1);
            return std::string_view::npos;
        }

        /** The first match of /[+-]?(?:\d+\.?\d*|\.\d+)/ in s. */
        std::optional<std::string_view> firstNumber (std::string_view s) noexcept
        {
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                if ((s[i] == '+' || s[i] == '-'))
                {
                    const std::size_t e = unsignedNumberAt (s, i + 1);
                    if (e != std::string_view::npos)
                        return s.substr (i, e - i);
                }
                const std::size_t e = unsignedNumberAt (s, i);
                if (e != std::string_view::npos)
                    return s.substr (i, e - i);
            }
            return std::nullopt;
        }

        double num (double x, double d) noexcept { return std::isfinite (x) ? x : d; }

        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        std::string semis (const std::string& s) { return s + (s == "1" ? " semitone" : " semitones"); }

        std::string times (double f, const char* up, const char* down)
        {
            return f >= 1.0 ? text::trimZ (jsToPrecision (f, 3)) + "x " + up
                            : text::trimZ (jsToPrecision (1.0 / f, 3)) + "x " + down;
        }

        /** A value read back from a macro or offset readout lands just outside the "no change" band
            (|d| < 0.0025) when the text says it changes something. */
        double outsideNoChange (double d, bool negative) noexcept
        {
            if (std::fabs (d) >= 0.0026)
                return d;
            return negative ? -0.0026 : 0.0026;
        }

        bool isMacro (P p) noexcept { return index (p) >= index (P::macro1) && index (p) <= index (P::macro8); }
        bool isOffset (P p) noexcept { return index (p) >= index (P::ampOffset) && index (p) <= index (P::hollowOffset); }

        text::Offset offsetKind (P p) noexcept
        {
            return static_cast<text::Offset> (index (p) - index (P::ampOffset));
        }

        int choiceIndex (double v, int count) noexcept
        {
            const double r = jsRound (num (v, 0.0));
            return r < 0.0 ? 0 : r > count - 1 ? count - 1 : static_cast<int> (r);
        }

        std::string regionText (double v, const TextContext* ctx)
        {
            std::string s = text::trimZ (jsToFixed (v * 100.0, 3)) + "%";
            const double secs = ctx != nullptr ? ctx->soundSeconds.load (std::memory_order_relaxed) : 0.0;
            if (secs > 0.0 && std::isfinite (secs))
            {
                const double cs = jsRound (v * secs * 100.0);   // centiseconds
                const double minutes = std::floor (cs / 6000.0);
                const double rest = cs - minutes * 6000.0;
                s += " (" + jsNumberToString (minutes) + ":" + (rest < 1000.0 ? "0" : "") + jsToFixed (rest / 100.0, 2) + ")";
            }
            return s;
        }

        std::optional<double> regionFromText (std::string_view t, const TextContext* ctx)
        {
            const double secs = ctx != nullptr ? ctx->soundSeconds.load (std::memory_order_relaxed) : 0.0;
            const auto colon = t.find (':');
            if (colon != std::string_view::npos && t.find ('%') == std::string_view::npos && secs > 0.0)
            {
                const auto m = readDecimal (trim (t.substr (0, colon)));
                const auto s = text::parseNum (t.substr (colon + 1));
                if (! m || ! s)
                    return std::nullopt;
                return (*m * 60.0 + (*m < 0.0 ? -*s : *s)) / secs;
            }
            const auto pct = text::parseNum (t);
            return pct ? std::optional<double> (*pct / 100.0) : std::nullopt;
        }

        // The prototype's own codes for a choice (as typed, as saved), beside the labels.
        const char* choiceCode (P p, int i) noexcept
        {
            switch (p)
            {
                case P::engine:      return core::kEngineCodes[i];
                case P::motion:      return core::kMotionCodes[i];
                case P::headTune:    return core::kHeadTuneCodes[i];
                case P::refreshMode: return core::kRefreshModeCodes[i];
                case P::atEnd:       return core::kAtEndCodes[i];
                case P::glideMode:   return core::kGlideModeCodes[i];
                case P::jump:
                case P::refreshRate: return core::kRates[i].code;
                case P::vibSync:     return i == 0 ? "free" : core::kRates[i - 1].code;
                default:             return "";
            }
        }

        std::optional<double> choiceFromText (P p, std::string_view t)
        {
            const ParamSpec& s = spec (p);
            const std::string lowerText = lowerAscii (t);
            for (int i = 0; i < s.numChoices; ++i)
                if (lowerText == lowerAscii (s.choices[i]))
                    return static_cast<double> (i);
            for (int i = 0; i < s.numChoices; ++i)
                if (t == choiceCode (p, i))
                    return static_cast<double> (i);
            if (const auto n = text::parseNum (t); n && jsRound (*n) >= 0.0 && jsRound (*n) < s.numChoices)
                return jsRound (*n);
            return std::nullopt;
        }

        std::optional<double> toggleFromText (std::string_view t)
        {
            const std::string s = lowerAscii (t);
            if (s == "on" || s == "true" || s == "yes")
                return 1.0;
            if (s == "off" || s == "false" || s == "no")
                return 0.0;
            if (const auto n = text::parseNum (t))
                return *n >= 0.5 ? 1.0 : 0.0;
            return std::nullopt;
        }

        std::optional<double> macroFromText (int i, std::string_view t)
        {
            if (lowerAscii (t) == "no change")
                return 0.5;
            const auto n = text::parseNum (t);
            if (! n)
                return std::nullopt;
            double d = 0.0;
            bool negative = *n < 0.0;
            switch (i)
            {
                case 0: case 3: case 4:
                {
                    if (! (*n > 0.0))
                        return std::nullopt;
                    negative = contains (t, "slower") || contains (t, "shorter");
                    const double factor = negative ? 1.0 / *n : *n;
                    d = std::log2 (factor) / (i == 3 ? 6.0 : 4.0);
                    break;
                }
                case 1: case 5: d = *n / 200.0; break;
                case 2:         d = *n / 14.0; break;
                case 6:         negative = negative || contains (t, "darker"); d = *n / 8.3; break;
                case 7:         d = *n / 24.0; break;
                default:        return std::nullopt;
            }
            return 0.5 + outsideNoChange (d, negative);
        }

        std::optional<double> offsetFromText (text::Offset k, std::string_view t)
        {
            if (lowerAscii (t) == "no change")
                return 0.0;
            if (k == text::Offset::amp && lowerAscii (t) == "silent")
                return -0.9;
            const auto n = text::parseNum (t);
            if (! n)
                return std::nullopt;
            bool negative = *n < 0.0;
            double o = 0.0;
            switch (k)
            {
                case text::Offset::amp:     o = 0.8 * std::pow (10.0, *n / 40.0) - 0.8; break;
                case text::Offset::vib:     o = *n; break;
                case text::Offset::tone:    negative = negative || contains (t, "darker"); o = *n / 8.3; break;
                case text::Offset::formant: o = *n / 24.0; break;
                case text::Offset::hollow:  negative = contains (t, "less"); o = (negative ? -std::fabs (*n) : std::fabs (*n)) / 28.0; break;
            }
            return outsideNoChange (o, negative);
        }
    }

    //==============================================================================================
    namespace text
    {
        std::string trimZ (std::string s)
        {
            if (s.find ('.') == std::string::npos || s.find ('e') != std::string::npos)
                return s;
            while (! s.empty() && s.back() == '0')
                s.pop_back();
            if (! s.empty() && s.back() == '.')
                s.pop_back();
            return s;
        }

        std::string signedText (double x, int d)
        {
            const double r = readDecimal (jsToFixed (x, d)).value_or (0.0);
            return std::string (r > 0.0 ? "+" : r < 0.0 ? "-" : "") + trimZ (jsToFixed (std::fabs (r), d));
        }

        std::string fmtX (double v)
        {
            return v <= 0.0 ? std::string ("stopped") : trimZ (jsToPrecision (v, 3)) + "x";
        }

        std::string fmtPct (double v)
        {
            return jsNumberToString (jsRound (v * 100.0)) + "%";
        }

        std::string fmtSt (double v)
        {
            const double r = jsRound (v * 100.0) / 100.0;
            if (std::fabs (r) < 0.005)
                return "0 semitones";
            return std::string (r > 0.0 ? "+" : "-") + semis (trimZ (jsToFixed (std::fabs (r), 2)));
        }

        std::string fmtCycles (double v)
        {
            const std::string s = trimZ (jsToFixed (v, 2));
            return s + (s == "1" ? " cycle" : " cycles");
        }

        std::string fmtMs (double v)
        {
            return v >= 1000.0 ? trimZ (jsToFixed (v / 1000.0, 2)) + " s" : jsNumberToString (jsRound (v)) + " ms";
        }

        std::string fmtHeads (double v)
        {
            const double r = jsRound (v * 100.0) / 100.0;
            return trimZ (jsToFixed (r, 2)) + (r == 1.0 ? " head" : " heads");
        }

        std::string macroText (int i, double m)
        {
            const double d = num (m, 0.5) - 0.5;
            if (std::fabs (d) < 0.0025)
                return "no change";
            switch (i)
            {
                case 0: return times (std::pow (2.0, d * 4.0), "faster", "slower");
                case 1: return signedText (d * 200.0, 0) + "% spread";
                case 2: return signedText (d * 14.0, 1) + " heads";
                case 3: return times (std::pow (2.0, d * 6.0), "faster", "slower");
                case 4: return times (std::pow (2.0, d * 4.0), "longer", "shorter");
                case 5: return signedText (d * 200.0, 0) + "% spray";
                case 6: return signedText (d * 8.3, 1) + " oct " + (d > 0.0 ? "brighter" : "darker");
                case 7: return fmtSt (d * 24.0) + " formant";
                default: break;
            }
            return jsNumberToString (m);
        }

        std::string offsetText (Offset k, double o)
        {
            o = num (o, 0.0);
            if (std::fabs (o) < 0.0025)
                return "no change";
            switch (k)
            {
                case Offset::amp:
                {
                    // in dB for a note at the default Volume (0.8 on the lane; gain = v squared): the lane stops
                    // at its top, so raising it adds at most +3.9 dB; lowering it to the bottom is silence
                    const double v0 = 0.8, v = clampD (v0 + o, 0.0, 1.0);
                    if (v <= 0.0)
                        return "silent";
                    const double db = 40.0 * std::log10 (v / v0), r = jsRound (db * 10.0) / 10.0;
                    return std::string (r > 0.0 ? "+" : r < 0.0 ? "-" : "") + jsToFixed (std::fabs (r), 1) + " dB";
                }
                case Offset::vib:
                {
                    const double r = readDecimal (jsToFixed (o, 2)).value_or (0.0);
                    return signedText (o, 2) + (std::fabs (r) == 1.0 ? " semitone" : " semitones") + " of vibrato";
                }
                case Offset::tone:    return signedText (o * 8.3, 1) + " oct " + (o > 0.0 ? "brighter" : "darker");
                case Offset::formant: return fmtSt (o * 24.0);
                case Offset::hollow:  return jsToFixed (std::fabs (o) * 28.0, 1) + " dB " + (o > 0.0 ? "more hollow" : "less hollow");
            }
            return jsNumberToString (o);
        }

        std::optional<double> parseNum (std::string_view t)
        {
            std::string s (trim (t));
            if (const auto comma = s.find (','); comma != std::string::npos)
                s[comma] = '.';   // String.replace (',', '.') changes the first comma only

            std::string_view top, bottom;
            if (leadingFraction (s, top, bottom))
            {
                const auto a = readDecimal (top), b = readDecimal (bottom);
                if (! a || ! b || *b == 0.0)
                    return std::nullopt;
                return *a / *b;
            }
            if (const auto m = firstNumber (s))
                return readDecimal (*m);
            return std::nullopt;
        }

        std::optional<double> parseMs (std::string_view t)
        {
            const auto v = parseNum (t);
            if (! v)
                return std::nullopt;

            // /\d\s*s\b/i.test (t) && ! /ms/i.test (t)
            bool secondsUnit = false;
            for (std::size_t i = 0; i < t.size() && ! secondsUnit; ++i)
            {
                if (! isDigit (t[i]))
                    continue;
                std::size_t j = i + 1;
                while (j < t.size() && isSpace (t[j]))
                    ++j;
                secondsUnit = j < t.size() && lower (t[j]) == 's' && (j + 1 == t.size() || ! isWordChar (t[j + 1]));
            }
            const bool ms = lowerAscii (t).find ("ms") != std::string::npos;
            return secondsUnit && ! ms ? *v * 1000.0 : *v;
        }

        std::optional<double> parsePct (std::string_view t)
        {
            const auto v = parseNum (t);
            return v ? std::optional<double> (*v / 100.0) : std::nullopt;
        }

        std::optional<int> parsePitch (std::string_view t)
        {
            // clean (): trim, lower case, the dashes to '-', commas to '.', then every space removed;
            // then the sharp and flat signs to '#' and 'b'.
            std::string s;
            const std::string_view in = trim (t);
            for (std::size_t i = 0; i < in.size(); ++i)
            {
                const char c = in[i];
                const std::string_view rest = in.substr (i);
                auto starts = [&rest] (std::string_view u) { return rest.substr (0, u.size()) == u; };
                if (starts ("\xe2\x88\x92") || starts ("\xe2\x80\x93") || starts ("\xe2\x80\x94"))   // − – —
                {
                    s += '-';
                    i += 2;
                }
                else if (starts ("\xe2\x99\xaf"))   // ♯
                {
                    s += '#';
                    i += 2;
                }
                else if (starts ("\xe2\x99\xad"))   // ♭
                {
                    s += 'b';
                    i += 2;
                }
                else if (starts ("\xc2\xa0") || isSpace (c))
                {
                    i += isSpace (c) ? 0 : 1;
                }
                else
                {
                    s += c == ',' ? '.' : lower (c);
                }
            }

            // /^\d{1,3}$/
            if (! s.empty() && s.size() <= 3 && digitsFrom (s, 0) == s.size())
            {
                const int p = std::stoi (s);
                return p <= 127 ? std::optional<int> (p) : std::nullopt;
            }

            // /^([a-g])(#|b)?(-?\d+)$/
            static constexpr int PC[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
            if (s.size() < 2 || s[0] < 'a' || s[0] > 'g')
                return std::nullopt;
            std::size_t i = 1;
            int accidental = 0;
            if (s[i] == '#' || s[i] == 'b')
            {
                accidental = s[i] == '#' ? 1 : -1;
                ++i;
            }
            const std::size_t octaveStart = i;
            if (i < s.size() && s[i] == '-')
                ++i;
            const std::size_t digits = i;
            i = digitsFrom (s, i);
            if (i == digits || i != s.size() || i - digits > 6)
                return std::nullopt;
            const int octave = std::stoi (s.substr (octaveStart));
            const int p = (octave + 2) * 12 + PC[s[0] - 'a'] + accidental;
            return p >= 0 && p <= 127 ? std::optional<int> (p) : std::nullopt;
        }
    }

    //==============================================================================================
    std::string paramText (int i, double v, const TextContext* ctx)
    {
        if (i < 0 || i >= kNumParams)
            return {};
        const P p = static_cast<P> (i);
        const ParamSpec& s = kParams[i];
        if (s.kind == Kind::choice)
            return s.choices[choiceIndex (v, s.numChoices)];
        if (s.kind == Kind::toggle)
            return v >= 0.5 ? "On" : "Off";
        if (isMacro (p))
            return text::macroText (i - index (P::macro1), v);
        if (isOffset (p))
            return text::offsetText (offsetKind (p), v);

        switch (p)
        {
            case P::outGain:
            {
                const double r = jsRound (v * 10.0) / 10.0;
                return std::string (r > 0.0 ? "+" : r < 0.0 ? "-" : "") + jsToFixed (std::fabs (r), 1) + " dB";
            }
            case P::speed:      return text::fmtX (v);
            case P::heads:      return text::fmtHeads (v);
            case P::spread:
            case P::spray:
            case P::drift:
            case P::refreshSkip:
            case P::velSens:    return text::fmtPct (v);
            case P::grainVoice: return text::fmtCycles (v);
            case P::grainCloud: return jsNumberToString (jsRound (v)) + " ms";
            case P::grainLock:
            {
                const std::string t = text::trimZ (jsToFixed (v, 1));
                return t + (t == "1" ? " wave" : " waves");
            }
            case P::pitch:      return text::fmtSt (v);
            case P::refreshMs:
            case P::loopXfade:  return text::fmtMs (v);
            case P::regionStart:
            case P::regionEnd:  return regionText (v, ctx);
            case P::glideTime:
            {
                const auto rate = core::rateFromBeats (v, 0.01);
                return rate ? std::string (core::rateLabel (*rate)) : jsToFixed (v, 3) + " beats";
            }
            case P::vibRate:    return jsToFixed (v, 1) + " Hz";
            case P::bendRange:
            {
                const std::string n = jsNumberToString (jsRound (v));
                return n + (n == "1" ? " semitone" : " semitones");
            }
            case P::polyphony:  return jsNumberToString (jsRound (v));
            case P::rootNote:   return core::noteName (v);
            case P::rootCents:
            {
                const double r = jsRound (v);
                return std::string (r > 0.0 ? "+" : "") + jsNumberToString (r) + " cents";
            }
            default:            break;
        }
        return jsNumberToString (v);
    }

    std::optional<double> paramValueFromText (int i, std::string_view raw, const TextContext* ctx)
    {
        if (i < 0 || i >= kNumParams)
            return std::nullopt;
        const P p = static_cast<P> (i);
        const std::string_view t = trim (raw);
        const ParamSpec& s = kParams[i];
        if (s.kind == Kind::choice)
            return choiceFromText (p, t);
        if (s.kind == Kind::toggle)
            return toggleFromText (t);
        if (isMacro (p))
            return macroFromText (i - index (P::macro1), t);
        if (isOffset (p))
            return offsetFromText (offsetKind (p), t);

        switch (p)
        {
            case P::speed:
                if (lowerAscii (t) == "stopped")
                    return 0.0;
                return text::parseNum (t);
            case P::spread:
            case P::spray:
            case P::drift:
            case P::refreshSkip:
            case P::velSens:    return text::parsePct (t);
            case P::grainCloud:
            case P::refreshMs:
            case P::loopXfade:  return text::parseMs (t);
            case P::regionStart:
            case P::regionEnd:  return regionFromText (t, ctx);
            case P::glideTime:
            {
                const std::string lowerText = lowerAscii (t);
                for (const auto& rate : core::kRates)
                    if (lowerText == lowerAscii (rate.label) || t == rate.code)
                        return rate.beats;
                return text::parseNum (t);
            }
            case P::rootNote:
            {
                const auto n = text::parsePitch (t);
                return n ? std::optional<double> (static_cast<double> (*n)) : std::nullopt;
            }
            case P::polyphony:
            case P::bendRange:
            {
                const auto n = text::parseNum (t);
                return n ? std::optional<double> (jsRound (*n)) : std::nullopt;
            }
            default:
                return text::parseNum (t);
        }
    }

    double readoutHalfStep (int i, double v)
    {
        if (i < 0 || i >= kNumParams)
            return 0.0;
        const P p = static_cast<P> (i);
        const ParamSpec& s = kParams[i];
        if (s.kind != Kind::real)
            return 0.0;
        if (isMacro (p) || isOffset (p))
            return std::numeric_limits<double>::infinity();

        switch (p)
        {
            case P::outGain:    return 0.05;
            case P::speed:      return v <= 0.0 ? 0.0 : 0.5 * std::pow (10.0, std::floor (std::log10 (v)) - 2.0);
            case P::heads:
            case P::spread:
            case P::spray:
            case P::drift:
            case P::refreshSkip:
            case P::velSens:
            case P::grainVoice:
            case P::pitch:      return 0.005;
            case P::grainCloud: return 0.5;
            case P::grainLock:
            case P::vibRate:    return 0.05;
            case P::refreshMs:
            case P::loopXfade:  return v >= 1000.0 ? 5.0 : 0.5;
            case P::regionStart:
            case P::regionEnd:  return 5e-6;
            case P::glideTime:  return core::rateFromBeats (v, 0.01) ? 0.0101 * v : 0.0005;
            case P::rootCents:  return 0.5;
            default:            break;
        }
        return 0.0;
    }
}
