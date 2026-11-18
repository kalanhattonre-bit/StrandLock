#include "InspectorText.h"

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/Rates.h"
#include "core/Travel.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <system_error>
#include <vector>

namespace sl::ui::roll
{
    namespace
    {
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

        bool fin (double x) noexcept { return std::isfinite (x); }
        double clampD (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

        bool isDigit (char c) noexcept { return c >= '0' && c <= '9'; }
        bool isSpace (char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
        bool isWord (char c) noexcept { return isDigit (c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

        bool startsAt (std::string_view s, std::size_t at, std::string_view word) noexcept
        {
            return at <= s.size() && s.substr (at, word.size()) == word;
        }

        bool endsWith (std::string_view s, std::string_view word) noexcept
        {
            return s.size() >= word.size() && s.substr (s.size() - word.size()) == word;
        }

        bool contains (std::string_view s, std::string_view word) noexcept { return s.find (word) != std::string_view::npos; }

        std::size_t skipSpaces (std::string_view s, std::size_t at) noexcept
        {
            while (at < s.size() && isSpace (s[at]))
                ++at;
            return at;
        }

        /** JavaScript's parseFloat / Number on text the patterns below already matched as a decimal number
            (digits, an optional point, an optional exponent): locale independent. */
        double parseNumber (std::string_view text) noexcept
        {
            double value = 0.0;
            const auto r = std::from_chars (text.data(), text.data() + text.size(), value, std::chars_format::general);
            if (r.ec == std::errc::result_out_of_range)
            {
                const bool negative = ! text.empty() && text.front() == '-';
                const bool tiny = contains (text, "e-");
                return tiny ? (negative ? -0.0 : 0.0) : (negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
            }
            if (r.ec != std::errc())
                return kNaN;
            return value;
        }

        //==========================================================================================
        // The number pattern \d*\.?\d+ at `at`, tried in the order a backtracking regular expression tries it
        // (the digits before the point greedy, the point when there is one, then the digits after it, longest
        // first). For each candidate end, cont (end) says whether the rest of the pattern matches there; the
        // first end it accepts is returned.
        template <class Cont>
        std::optional<std::size_t> matchNumber (std::string_view s, std::size_t at, Cont&& cont)
        {
            std::size_t maxDigits = 0;
            while (at + maxDigits < s.size() && isDigit (s[at + maxDigits]))
                ++maxDigits;
            for (std::size_t k = maxDigits + 1; k-- > 0;)
            {
                const std::size_t j = at + k;
                for (int dot = 1; dot >= 0; --dot)
                {
                    std::size_t q = j;
                    if (dot == 1)
                    {
                        if (q < s.size() && s[q] == '.')
                            ++q;
                        else
                            continue;
                    }
                    std::size_t run = 0;
                    while (q + run < s.size() && isDigit (s[q + run]))
                        ++run;
                    for (std::size_t len = run; len >= 1; --len)
                        if (cont (q + len))
                            return q + len;
                }
            }
            return std::nullopt;
        }

        struct Found
        {
            std::size_t begin = 0, end = 0;
        };

        /** The first (leftmost) match of \d*\.?\d+ followed by what cont accepts. */
        template <class Cont>
        std::optional<Found> searchNumber (std::string_view s, Cont&& cont)
        {
            for (std::size_t i = 0; i <= s.size(); ++i)
                if (const auto e = matchNumber (s, i, cont))
                    return Found { i, *e };
            return std::nullopt;
        }

        /** The prototype's clean (): trimmed, lower case, the minus and dash signs as '-', ',' as '.', every run of
            white space one space. */
        std::string clean (std::string_view text)
        {
            std::string t;
            t.reserve (text.size());
            for (std::size_t i = 0; i < text.size();)
            {
                const auto c = static_cast<unsigned char> (text[i]);
                // U+2212 minus, U+2013 en dash, U+2014 em dash -> '-'
                if (c == 0xe2 && i + 2 < text.size())
                {
                    const auto c1 = static_cast<unsigned char> (text[i + 1]), c2 = static_cast<unsigned char> (text[i + 2]);
                    if ((c1 == 0x88 && c2 == 0x92) || (c1 == 0x80 && (c2 == 0x93 || c2 == 0x94)))
                    {
                        t += '-';
                        i += 3;
                        continue;
                    }
                }
                // U+00A0 (no-break space) is white space too
                if (c == 0xc2 && i + 1 < text.size() && static_cast<unsigned char> (text[i + 1]) == 0xa0)
                {
                    t += ' ';
                    i += 2;
                    continue;
                }
                char ch = text[i];
                if (ch >= 'A' && ch <= 'Z')
                    ch = static_cast<char> (ch - 'A' + 'a');
                else if (ch == ',')
                    ch = '.';
                else if (isSpace (ch))
                    ch = ' ';
                t += ch;
                ++i;
            }
            // trim, then one space for every run
            std::string out;
            out.reserve (t.size());
            std::size_t a = 0, b = t.size();
            while (a < b && t[a] == ' ')
                ++a;
            while (b > a && t[b - 1] == ' ')
                --b;
            for (std::size_t i = a; i < b; ++i)
            {
                if (t[i] == ' ' && ! out.empty() && out.back() == ' ')
                    continue;
                out += t[i];
            }
            return out;
        }

        /** s with every byte sequence `from` replaced by `to`. */
        std::string replaceAll (std::string s, std::string_view from, std::string_view to)
        {
            std::size_t at = 0;
            while ((at = s.find (from, at)) != std::string::npos)
            {
                s.replace (at, from.size(), to);
                at += to.size();
            }
            return s;
        }

        /** /\s*(w1|w2|...)$/ replaced by `with`: the first word (in that order) the text ends with, and the white
            space before it. */
        std::string replaceEnding (std::string s, std::initializer_list<std::string_view> words, std::string_view with)
        {
            for (const auto w : words)
            {
                if (endsWith (s, w))
                {
                    std::size_t start = s.size() - w.size();
                    while (start > 0 && isSpace (s[start - 1]))
                        --start;
                    s.replace (start, std::string::npos, with);
                    return s;
                }
            }
            return s;
        }

        bool allDigits (std::string_view s) noexcept
        {
            if (s.empty())
                return false;
            for (const char c : s)
                if (! isDigit (c))
                    return false;
            return true;
        }

        /** /^\d*\.?\d+$/ */
        bool isPlainNumber (std::string_view s)
        {
            const auto e = matchNumber (s, 0, [&] (std::size_t end) { return end == s.size(); });
            return e.has_value();
        }

        /** The prototype's numPart: the first "a / b" (a may be negative) as a / b, else the first number (with a
            sign and an exponent), else NaN. */
        double numPart (std::string_view s)
        {
            // /(-?\d*\.?\d+)\s*\/\s*(\d*\.?\d+)/
            for (std::size_t i = 0; i <= s.size(); ++i)
            {
                for (int sign = (i < s.size() && s[i] == '-') ? 1 : 0; sign >= 0; --sign)
                {
                    const std::size_t at = i + (std::size_t) sign;
                    Found second;
                    const auto e = matchNumber (s, at, [&] (std::size_t end)
                    {
                        std::size_t q = skipSpaces (s, end);
                        if (q >= s.size() || s[q] != '/')
                            return false;
                        q = skipSpaces (s, q + 1);
                        const auto e2 = matchNumber (s, q, [] (std::size_t) { return true; });
                        if (! e2)
                            return false;
                        second = Found { q, *e2 };
                        return true;
                    });
                    if (e)
                        return parseNumber (s.substr (i, *e - i)) / parseNumber (s.substr (second.begin, second.end - second.begin));
                }
            }
            // /-?\d*\.?\d+(e-?\d+)?/
            for (std::size_t i = 0; i <= s.size(); ++i)
            {
                for (int sign = (i < s.size() && s[i] == '-') ? 1 : 0; sign >= 0; --sign)
                {
                    const std::size_t at = i + (std::size_t) sign;
                    const auto e = matchNumber (s, at, [] (std::size_t) { return true; });
                    if (! e)
                        continue;
                    std::size_t end = *e;
                    if (end < s.size() && s[end] == 'e')
                    {
                        std::size_t p = end + 1;
                        if (p < s.size() && s[p] == '-' && p + 1 < s.size() && isDigit (s[p + 1]))
                            ++p;
                        if (p < s.size() && isDigit (s[p]))
                        {
                            while (p < s.size() && isDigit (s[p]))
                                ++p;
                            end = p;
                        }
                    }
                    return parseNumber (s.substr (i, end - i));
                }
            }
            return kNaN;
        }

        std::string numberText (double x) { return core::jsNumberToString (x); }
    }

    //==============================================================================================
    std::string fmtPos (double b, int beatsPerBar)
    {
        const double bpb = (double) std::max (1, beatsPerBar);
        const double tt = std::max (0.0, core::jsRound (fin (b) ? b * kTicksPerBeat : 0.0));
        const double bar = std::floor (tt / (bpb * kTicksPerBeat)) + 1.0;
        const double r1 = tt - (bar - 1.0) * bpb * kTicksPerBeat;
        const double beat = std::floor (r1 / kTicksPerBeat) + 1.0;
        const double r2 = r1 - (beat - 1.0) * kTicksPerBeat;
        const double six = std::floor (r2 / kTicksPerSixteenth) + 1.0;
        const double tick = r2 - (six - 1.0) * kTicksPerSixteenth;
        std::string out = numberText (bar) + "." + numberText (beat) + "." + numberText (six);
        if (tick != 0.0)
            out += "." + numberText (tick);
        return out;
    }

    std::optional<double> parsePos (std::string_view text, int beatsPerBar)
    {
        const double bpb = (double) std::max (1, beatsPerBar);
        const std::string s = clean (text);
        if (s.empty())
            return std::nullopt;
        // split on runs of '.', ':' and white space, dropping empty parts
        std::vector<std::string_view> parts;
        std::size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && (s[i] == '.' || s[i] == ':' || isSpace (s[i])))
                ++i;
            const std::size_t a = i;
            while (i < s.size() && ! (s[i] == '.' || s[i] == ':' || isSpace (s[i])))
                ++i;
            if (i > a)
                parts.push_back (std::string_view (s).substr (a, i - a));
        }
        if (parts.empty() || parts.size() > 4)
            return std::nullopt;
        for (const auto p : parts)
            if (! allDigits (p))
                return std::nullopt;
        const double bar = parseNumber (parts[0]);
        const double beat = parts.size() > 1 ? parseNumber (parts[1]) : 1.0;
        const double six = parts.size() > 2 ? parseNumber (parts[2]) : 1.0;
        const double tick = parts.size() > 3 ? parseNumber (parts[3]) : 0.0;
        if (bar < 1 || beat < 1 || six < 1)
            return std::nullopt;
        return (bar - 1) * bpb + (beat - 1) + (six - 1) / 4 + tick / kTicksPerBeat;
    }

    std::optional<std::string> rateId (std::string_view text)
    {
        std::string x = clean (text);
        x = replaceEnding (std::move (x), { "triplets", "triplet", "trip" }, "t");
        x = replaceEnding (std::move (x), { "dotted", "dot" }, "d");
        // /^1\s*bars?$/ -> "1bar", then /^bars?$/ -> "1bar"
        if (startsAt (x, 0, "1"))
        {
            const std::string_view rest = std::string_view (x).substr (skipSpaces (x, 1));
            if (rest == "bar" || rest == "bars")
                x = "1bar";
        }
        if (x == "bar" || x == "bars")
            x = "1bar";
        std::string squeezed;
        for (const char c : x)
            if (! isSpace (c))
                squeezed += c;
        if (! squeezed.empty() && (squeezed.back() == 't' || squeezed.back() == 'd'))
            squeezed.back() = static_cast<char> (squeezed.back() - 'a' + 'A');
        if (core::parseRate (squeezed))
            return squeezed;
        return std::nullopt;
    }

    std::optional<double> parseBeats (std::string_view text)
    {
        const std::string s = replaceEnding (clean (text), { "beats", "beat", "b" }, "");
        if (s.empty())
            return std::nullopt;
        if (const auto r = rateId (s))
            return core::rateBeats (*r);
        // /^(\d*\.?\d+)\s*\/\s*(\d*\.?\d+)$/
        {
            Found second;
            const auto e = matchNumber (s, 0, [&] (std::size_t end)
            {
                std::size_t q = skipSpaces (s, end);
                if (q >= s.size() || s[q] != '/')
                    return false;
                q = skipSpaces (s, q + 1);
                const auto e2 = matchNumber (s, q, [&] (std::size_t e2end) { return e2end == s.size(); });
                if (! e2)
                    return false;
                second = Found { q, *e2 };
                return true;
            });
            if (e)
            {
                const double v = 4 * parseNumber (std::string_view (s).substr (0, *e))
                               / parseNumber (std::string_view (s).substr (second.begin, second.end - second.begin));
                return fin (v) && v > 0 ? std::optional<double> (v) : std::nullopt;
            }
        }
        // /^\d+(\.\d+){2,3}$/
        {
            std::vector<std::string_view> parts;
            std::size_t a = 0;
            for (std::size_t i = 0; i <= s.size(); ++i)
            {
                if (i == s.size() || s[i] == '.')
                {
                    parts.push_back (std::string_view (s).substr (a, i - a));
                    a = i + 1;
                }
            }
            bool ok = parts.size() >= 3 && parts.size() <= 4;
            for (const auto p : parts)
                ok = ok && allDigits (p);
            if (ok)
            {
                const double p0 = parseNumber (parts[0]), p1 = parseNumber (parts[1]);
                const double p2 = parts.size() > 2 ? parseNumber (parts[2]) : 0.0;
                const double p3 = parts.size() > 3 ? parseNumber (parts[3]) : 0.0;
                return p0 * 4 + p1 + p2 / 4 + p3 / kTicksPerBeat;
            }
        }
        if (isPlainNumber (s))
        {
            const double v = parseNumber (s);
            return fin (v) ? std::optional<double> (v) : std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<double> parsePitch (std::string_view text)
    {
        std::string s;
        for (const char c : clean (text))
            if (! isSpace (c))
                s += c;
        s = replaceAll (std::move (s), "\xe2\x99\xaf", "#");   // U+266F sharp sign
        s = replaceAll (std::move (s), "\xe2\x99\xad", "b");   // U+266D flat sign
        if (! s.empty() && s.size() <= 3 && allDigits (s))
        {
            const double p = parseNumber (s);
            return p <= 127 ? std::optional<double> (p) : std::nullopt;
        }
        // /^([a-g])(#|b)?(-?\d+)$/
        if (s.empty() || s[0] < 'a' || s[0] > 'g')
            return std::nullopt;
        static constexpr int pc[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
        const int cls = pc[s[0] - 'a'];
        auto octaveFrom = [&] (std::size_t at, double& octave)
        {
            std::size_t q = at;
            if (q < s.size() && s[q] == '-')
                ++q;
            if (! allDigits (std::string_view (s).substr (q)))
                return false;
            octave = parseNumber (std::string_view (s).substr (at));
            return true;
        };
        double octave = 0.0;
        int accidental = 0;
        bool matched = false;
        if (s.size() > 1 && (s[1] == '#' || s[1] == 'b') && octaveFrom (2, octave))
        {
            accidental = s[1] == '#' ? 1 : -1;
            matched = true;
        }
        else if (octaveFrom (1, octave))
        {
            matched = true;
        }
        if (! matched)
            return std::nullopt;
        const double p = (octave + 2) * 12 + cls + accidental;
        return p >= 0 && p <= 127 ? std::optional<double> (p) : std::nullopt;
    }

    std::string fmtNum (double x, int digits)
    {
        if (! fin (x))
            return {};
        const std::string fixed = core::jsToFixed (x, digits < 0 ? 3 : digits);
        return numberText (parseNumber (fixed));
    }

    std::optional<int> parseVelocity (std::string_view text)
    {
        // JavaScript's parseFloat on the cleaned text: an optional sign, then the longest decimal number at the
        // start (digits, a point, more digits, an exponent); "infinity" reads as not finite
        const std::string s = clean (text);
        std::size_t i = 0;
        const bool negative = i < s.size() && s[i] == '-';
        if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            ++i;
        const std::size_t digits0 = i;
        while (i < s.size() && isDigit (s[i]))
            ++i;
        const bool intDigits = i > digits0;
        bool fracDigits = false;
        if (i < s.size() && s[i] == '.')
        {
            std::size_t j = i + 1;
            while (j < s.size() && isDigit (s[j]))
                ++j;
            fracDigits = j > i + 1;
            if (intDigits || fracDigits)
                i = j;
        }
        if (! intDigits && ! fracDigits)
            return std::nullopt;
        if (i < s.size() && s[i] == 'e')
        {
            std::size_t j = i + 1;
            if (j < s.size() && (s[j] == '+' || s[j] == '-'))
                ++j;
            std::size_t k = j;
            while (k < s.size() && isDigit (s[k]))
                ++k;
            if (k > j)
                i = k;
        }
        std::string number = s.substr (digits0, i - digits0);
        if (! number.empty() && number.front() == '.')
            number.insert (number.begin(), '0');
        if (! number.empty() && number.back() == '.')
            number.pop_back();
        const std::size_t ePos = number.find ('e');
        if (ePos != std::string::npos && ePos + 1 < number.size() && number[ePos + 1] == '+')
            number.erase (ePos + 1, 1);
        double x = parseNumber (number);
        if (negative)
            x = -x;
        if (! fin (x))
            return std::nullopt;
        return (int) clampD (core::jsRound (x), 1.0, 127.0);
    }

    //==============================================================================================
    namespace
    {
        /** A global replace of /(^|[^\w.])BODY/g, as String.replace does it: the leftmost match at or after the end
            of the last one, where BODY is matched by body (from) (its end, or npos) and rewritten by write (out, from,
            end). The character before the body (or nothing at the very start) is kept. */
        template <class Body, class Write>
        std::string replaceAfterBoundary (const std::string& t, Body&& body, Write&& write)
        {
            std::string out;
            std::size_t pos = 0, copied = 0;
            for (;;)
            {
                std::size_t from = std::string::npos, end = std::string::npos;
                for (std::size_t i = pos; i <= t.size(); ++i)
                {
                    if (i == 0)
                    {
                        end = body (0);
                        if (end != std::string::npos)
                        {
                            from = 0;
                            break;
                        }
                    }
                    if (i < t.size() && ! isWord (t[i]) && t[i] != '.')
                    {
                        end = body (i + 1);
                        if (end != std::string::npos)
                        {
                            from = i + 1;
                            break;
                        }
                    }
                }
                if (end == std::string::npos)
                    break;
                out.append (t, copied, from - copied);   // up to and with the character before the body
                write (out, from, end);
                copied = pos = end;
            }
            out.append (t, copied, std::string::npos);
            return out;
        }
    }

    std::string plainUnits (std::string t)
    {
        // /(^|[^\w.])([+-]?)(\d+(?:\.\d+)?) st\b/g -> "semitone" when the number is 1, else "semitones"
        t = replaceAfterBoundary (t,
            [&t] (std::size_t from) -> std::size_t
            {
                std::size_t q = from;
                if (q < t.size() && (t[q] == '+' || t[q] == '-') && q + 1 < t.size() && isDigit (t[q + 1]))
                    ++q;
                if (q >= t.size() || ! isDigit (t[q]))
                    return std::string::npos;
                while (q < t.size() && isDigit (t[q]))
                    ++q;
                if (q + 1 < t.size() && t[q] == '.' && isDigit (t[q + 1]))
                {
                    ++q;
                    while (q < t.size() && isDigit (t[q]))
                        ++q;
                }
                if (! startsAt (t, q, " st"))
                    return std::string::npos;
                q += 3;
                if (q < t.size() && isWord (t[q]))
                    return std::string::npos;
                return q;
            },
            [&t] (std::string& out, std::size_t from, std::size_t end)
            {
                std::size_t d = from;
                if (t[d] == '+' || t[d] == '-')
                    out += t[d++];
                const std::string n = t.substr (d, end - 3 - d);
                out += n;
                out += n == "1" ? " semitone" : " semitones";
            });

        // / (semitones?) vibrato\b/ (the first one) -> " semitones of vibrato"
        for (std::size_t i = 0; i < t.size(); ++i)
        {
            std::size_t q = std::string::npos;
            const char* unit = "";
            if (startsAt (t, i, " semitones vibrato"))
            {
                q = i + 18;
                unit = " semitones of vibrato";
            }
            else if (startsAt (t, i, " semitone vibrato"))
            {
                q = i + 17;
                unit = " semitone of vibrato";
            }
            if (q != std::string::npos && (q == t.size() || ! isWord (t[q])))
            {
                t.replace (i, q - i, unit);
                break;
            }
        }

        // /(^|[^\w.])1 (cycles|wavelengths)\b/g -> "1 cycle", "1 wavelength"
        t = replaceAfterBoundary (t,
            [&t] (std::size_t from) -> std::size_t
            {
                if (! startsAt (t, from, "1 "))
                    return std::string::npos;
                for (const std::string_view unit : { std::string_view ("cycles"), std::string_view ("wavelengths") })
                {
                    const std::size_t u = from + 2, end = u + unit.size();
                    if (startsAt (t, u, unit) && (end == t.size() || ! isWord (t[end])))
                        return end;
                }
                return std::string::npos;
            },
            [&t] (std::string& out, std::size_t from, std::size_t end)
            {
                out += "1 ";
                out.append (t, from + 2, end - (from + 2) - 1);   // the unit without its "s"
            });
        return t;
    }

    std::string laneText (core::LaneId lane, double v, const model::LaneCtx& ctx)
    {
        std::string t = model::formatLane (lane, v, ctx);
        if (lane == core::LaneId::travel && ctx.travelKind != model::TravelKind::path)
        {
            const double x = model::mapLane (core::LaneId::travel, v, ctx);
            if (std::abs (std::abs (x) - 0.125) < 0.0007 && startsAt (t, 0, "0.13x"))
                t.replace (0, 5, "1/8x");
        }
        else if (lane == core::LaneId::refresh)
        {
            // /^Refresh (\S+) \(/ -> the rate's label; then / beat \(/ -> " beats ("
            if (startsAt (t, 0, "Refresh "))
            {
                std::size_t q = 8;
                while (q < t.size() && ! isSpace (t[q]))
                    ++q;
                if (q > 8 && startsAt (t, q, " ("))
                {
                    const std::string id = t.substr (8, q - 8);
                    if (const auto r = core::parseRate (id))
                        t = "Refresh " + std::string (core::rateLabel (*r)) + " (" + t.substr (q + 2);
                }
            }
            const auto at = t.find (" beat (");
            if (at != std::string::npos)
                t.replace (at, 7, " beats (");
        }
        return plainUnits (std::move (t));
    }

    std::string laneReadout (core::LaneId lane, double v, const model::LaneCtx& ctx)
    {
        if (lane == core::LaneId::refresh && core::normalizeInst (ctx.inst).refresh.mode == core::RefreshMode::smooth)
            return "Smooth: set Refresh to Free or Synced on the Sample tab to use this line";
        return laneText (lane, v, ctx);
    }

    LaneValue laneValueFromText (core::LaneId lane, std::string_view text, const model::LaneCtx& ctx)
    {
        const core::Inst I = core::normalizeInst (ctx.inst);
        const std::string s = clean (text);
        auto err = [] (const char* e) { LaneValue r; r.text = e; return r; };
        if (s.empty())
            return err ("Type a value");
        const double x = numPart (s);
        const bool has = fin (x);
        double v = 0.0;
        switch (lane)
        {
            case core::LaneId::pitch:
            case core::LaneId::formant:
                if (! has) return err ("Type semitones, for example +3 or -2.5");
                v = 0.5 + x / 24;
                break;
            case core::LaneId::vib:
                if (! has) return err ("Type the vibrato depth in semitones, for example 0.3");
                v = x;
                break;
            case core::LaneId::amp:
                if (contains (s, "inf") || contains (s, "off") || contains (s, "silent")) { v = 0; break; }
                if (! has) return err ("Type a level in dB, for example -6");
                v = std::sqrt (std::pow (10.0, x / 20) / 1.3);
                break;
            case core::LaneId::tone:
            {
                if (! has && contains (s, "open")) { v = 1; break; }
                if (! has) return err ("Type a frequency, for example 2.4 kHz or 800 Hz");
                const double hz = contains (s, "k") ? x * 1000 : x;
                if (! (hz > 0)) return err ("Type a frequency above 0 Hz");
                v = std::log2 (hz / 60) / 8.3;
                break;
            }
            case core::LaneId::hollow:
                if (! has) return err ("Type how deep the cut is in dB, for example -12");
                v = std::abs (x) / 28;
                break;
            case core::LaneId::travel:
            {
                if (ctx.travelKind == model::TravelKind::path)
                {
                    if (! has) return err ("Type where in the Region the note is, for example 25%");
                    v = contains (s, "%") || std::abs (x) > 1 ? x / 100 : x;
                    break;
                }
                if (contains (s, "stop") || contains (s, "still") || contains (s, "freez") || (has && x == 0)) { v = 0.5; break; }
                double sp = has ? x : 1;
                const bool back = contains (s, "back") || contains (s, "rev");
                const bool forw = contains (s, "forw");
                if (back) sp = -std::abs (sp);
                else if (forw) sp = std::abs (sp);
                if (! has && ! back && ! forw) return err ("Type a speed such as 2x, 0.5x backwards or stopped");
                v = core::travelSpeedToV (sp);
                break;
            }
            case core::LaneId::heads:
                if (! has) return err ("Type how many heads, 1 to 8");
                v = 0.5 + (clampD (x, 1, 8) - I.heads) / 14;
                break;
            case core::LaneId::grain:
                if (! has || ! (x > 0)) return err ("Type a grain size above 0");
                v = 0.5 + std::log2 (x / model::grainBase (I).base) / 4;
                break;
            case core::LaneId::refresh:
            {
                if (contains (s, "smooth")) { v = 0; break; }
                if (I.refresh.mode == core::RefreshMode::smooth)
                    return err ("Refresh is set to Smooth on the Sample tab: pick Free or Synced there first");
                const double bpm = clampD (fin (ctx.bpm) ? ctx.bpm : 120, 1, 1000);
                // the readout reads 'Refresh 1/16 (156 ms at 96 bpm)' or 'Refresh 1.464 beats (915 ms ...)': ms wins,
                // then beats, a rate, seconds
                const auto msM = searchNumber (s, [&] (std::size_t e) { return startsAt (s, skipSpaces (s, e), "ms"); });
                const auto beatM = searchNumber (s, [&] (std::size_t e) { return startsAt (s, skipSpaces (s, e), "beat"); });
                const auto secM = searchNumber (s, [&] (std::size_t e)
                {
                    std::size_t q = skipSpaces (s, e);
                    if (q >= s.size() || s[q] != 's')
                        return false;
                    ++q;
                    auto boundary = [&] (std::size_t p) { return p >= s.size() || ! isWord (s[p]); };
                    if (startsAt (s, q, "ec") && boundary (q + 2))
                        return true;
                    if (startsAt (s, q, "econd"))
                    {
                        if (q + 5 < s.size() && s[q + 5] == 's' && boundary (q + 6))
                            return true;
                        if (boundary (q + 5))
                            return true;
                    }
                    return boundary (q);
                });
                // the rate: without a leading "refresh" and without everything from the first "(" on
                std::string r = s;
                if (startsAt (r, 0, "refresh"))
                    r.erase (0, skipSpaces (r, 7));
                if (const auto paren = r.find ('('); paren != std::string::npos)
                {
                    std::size_t start = paren;
                    while (start > 0 && isSpace (r[start - 1]))
                        --start;
                    r.erase (start);
                }
                const auto rate = rateId (r);
                auto numberOf = [&] (const Found& f) { return parseNumber (std::string_view (s).substr (f.begin, f.end - f.begin)); };
                double ms = 0.0;
                if (msM) ms = numberOf (*msM);
                else if (beatM) ms = numberOf (*beatM) * 60000 / bpm;
                else if (rate) ms = core::rateBeats (*rate) * 60000 / bpm;
                else if (secM) ms = numberOf (*secM) * 1000;
                else if (has) ms = x;
                else return err ("Type a time in ms, a rate such as 1/16, or Smooth");
                if (! (ms > 0)) return err ("Type a time above 0 ms");
                const double base = I.refresh.mode == core::RefreshMode::sync ? core::rateBeats (I.refresh.rate) * 60000 / bpm : I.refresh.ms;
                const double w = 0.5 + std::log2 (base / ms) / 6;
                v = std::isnan (w) ? w : std::max (0.0601, w);   // Math.max keeps a NaN
                break;
            }
        }
        if (! fin (v))
            return err ("That value does not work here");
        v = clampD (v, 0, 1);
        LaneValue out;
        out.ok = true;
        out.v = v;
        out.text = laneText (lane, v, ctx);
        return out;
    }
}
