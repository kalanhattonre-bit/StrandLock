#include "FixtureJson.h"

#include "Check.h"
#include "TestRegistry.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace sl::test::fixture
{
    namespace
    {
        juce::File fileFor (const std::string& path)
        {
            return juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (path.c_str()));
        }
    }

    std::string dir()
    {
        const auto& o = options();
        return o.fixturesDir.empty() ? std::string ("Tests/fixtures") : o.fixturesDir;
    }

    bool readFile (const std::string& path, std::string& bytes)
    {
        juce::MemoryBlock block;
        const auto file = fileFor (path);
        if (! file.existsAsFile() || ! file.loadFileAsData (block))
            return false;
        bytes.assign (static_cast<const char*> (block.getData()), block.getSize());
        return true;
    }

    bool readText (const std::string& path, std::string& text)
    {
        std::string raw;
        if (! readFile (path, raw))
            return false;
        text.clear();
        text.reserve (raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i)
            if (! (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n'))
                text += raw[i];
        return true;
    }

    bool load (std::string_view relativePath, core::JsonValue& out)
    {
        const std::string path = dir() + "/" + std::string (relativePath);
        std::string text;
        if (! readText (path, text))
        {
            check (false, fmt ("fixture %s can be read (looked for %s)", std::string (relativePath).c_str(), path.c_str()));
            return false;
        }

        core::JsonError error;
        if (! core::parseJson (text, out, &error))
        {
            check (false, fmt ("fixture %s parses (%s at byte %zu)", std::string (relativePath).c_str(),
                               error.message.c_str(), error.offset));
            return false;
        }
        return true;
    }

    std::vector<std::string> listFiles()
    {
        std::vector<std::string> out;
        const auto root = fileFor (dir());
        for (const auto& f : root.findChildFiles (juce::File::findFiles, true, "*.json"))
            out.push_back (f.getRelativePathFrom (root).replaceCharacter ('\\', '/').toStdString());
        std::sort (out.begin(), out.end());
        return out;
    }

    std::vector<const core::JsonValue*> casesOfKind (const core::JsonValue& doc, std::string_view kind)
    {
        std::vector<const core::JsonValue*> out;
        const auto& cases = doc["cases"];
        for (std::size_t i = 0; i < cases.size(); ++i)
            if (cases[i]["kind"].asString() == kind)
                out.push_back (&cases[i]);
        return out;
    }

    double num (const core::JsonValue& v) noexcept
    {
        if (v.isNumber())
            return v.asNumber();
        const auto& s = v.asString();
        if (s == "Infinity")
            return std::numeric_limits<double>::infinity();
        if (s == "-Infinity")
            return -std::numeric_limits<double>::infinity();
        return std::numeric_limits<double>::quiet_NaN();
    }

    bool isNum (const core::JsonValue& v) noexcept
    {
        if (v.isNumber())
            return true;
        const auto& s = v.asString();
        return v.isString() && (s == "NaN" || s == "Infinity" || s == "-Infinity");
    }

    bool base64Decode (std::string_view text, std::vector<std::uint8_t>& bytes)
    {
        auto value = [] (char c) -> int
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };

        bytes.clear();
        if (text.size() % 4 != 0)
            return false;
        bytes.reserve (text.size() / 4 * 3);

        for (std::size_t i = 0; i < text.size(); i += 4)
        {
            const bool last = i + 4 == text.size();
            const int pad = last ? (text[i + 3] == '=' ? (text[i + 2] == '=' ? 2 : 1) : 0) : 0;
            int v[4] = { 0, 0, 0, 0 };
            for (int j = 0; j < 4 - pad; ++j)
            {
                v[j] = value (text[i + (std::size_t) j]);
                if (v[j] < 0)
                    return false;
            }
            const auto triple = (static_cast<std::uint32_t> (v[0]) << 18) | (static_cast<std::uint32_t> (v[1]) << 12)
                              | (static_cast<std::uint32_t> (v[2]) << 6) | static_cast<std::uint32_t> (v[3]);
            bytes.push_back (static_cast<std::uint8_t> ((triple >> 16) & 0xffu));
            if (pad < 2)
                bytes.push_back (static_cast<std::uint8_t> ((triple >> 8) & 0xffu));
            if (pad < 1)
                bytes.push_back (static_cast<std::uint8_t> (triple & 0xffu));
        }
        return true;
    }

    bool f32 (const core::JsonValue& v, std::vector<float>& out)
    {
        out.clear();
        const auto* encoded = v.find ("f32");
        if (encoded == nullptr || ! encoded->isString())
            return false;

        std::vector<std::uint8_t> bytes;
        if (! base64Decode (encoded->asString(), bytes) || bytes.size() % 4 != 0)
            return false;

        out.resize (bytes.size() / 4);
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            const std::uint32_t bits = static_cast<std::uint32_t> (bytes[4 * i])
                                     | (static_cast<std::uint32_t> (bytes[4 * i + 1]) << 8)
                                     | (static_cast<std::uint32_t> (bytes[4 * i + 2]) << 16)
                                     | (static_cast<std::uint32_t> (bytes[4 * i + 3]) << 24);
            out[i] = std::bit_cast<float> (bits);
        }
        return true;
    }

    bool table (const core::JsonValue& v, std::vector<float>& out)
    {
        if (v.contains ("f32"))
            return f32 (v, out);

        out.clear();
        const auto& n = v["n"];
        const auto& flat = v["flat"];
        if (! n.isNumber() || ! isNum (flat) || ! (n.asNumber() >= 0.0) || n.asNumber() > 1.0e8)
            return false;
        out.assign (static_cast<std::size_t> (n.asNumber()), static_cast<float> (num (flat)));
        return true;
    }

    namespace
    {
        std::int64_t orderedBits (float x) noexcept
        {
            const auto b = std::bit_cast<std::int32_t> (x);
            return b >= 0 ? static_cast<std::int64_t> (b)
                          : static_cast<std::int64_t> (std::numeric_limits<std::int32_t>::min()) - static_cast<std::int64_t> (b);
        }

        std::int64_t orderedBits (double x) noexcept
        {
            const auto b = std::bit_cast<std::int64_t> (x);
            return b >= 0 ? b : std::numeric_limits<std::int64_t>::min() - b;
        }

        template <class T>
        std::uint64_t ulpDistance (T a, T b) noexcept
        {
            if (std::isnan (a) || std::isnan (b))
                return std::isnan (a) && std::isnan (b) ? 0 : std::numeric_limits<std::uint64_t>::max();
            const std::int64_t ka = orderedBits (a), kb = orderedBits (b);
            return ka >= kb ? static_cast<std::uint64_t> (ka) - static_cast<std::uint64_t> (kb)
                            : static_cast<std::uint64_t> (kb) - static_cast<std::uint64_t> (ka);
        }
    }

    std::uint64_t ulps (float a, float b) noexcept   { return ulpDistance (a, b); }
    std::uint64_t ulps (double a, double b) noexcept { return ulpDistance (a, b); }

    bool same (double a, double b) noexcept
    {
        if (std::isnan (a) || std::isnan (b))
            return std::isnan (a) && std::isnan (b);
        return ! (a < b) && ! (b < a);   // +0 and -0 compare equal
    }

    bool withinRel (double a, double b, double rel) noexcept
    {
        if (same (a, b))
            return true;
        if (! std::isfinite (a) || ! std::isfinite (b))
            return false;
        return std::fabs (a - b) <= rel * std::max (std::fabs (a), std::fabs (b));
    }

    bool withinAbs (double a, double b, double tolerance) noexcept
    {
        if (same (a, b))
            return true;
        if (! std::isfinite (a) || ! std::isfinite (b))
            return false;
        return std::fabs (a - b) <= tolerance;
    }
}
