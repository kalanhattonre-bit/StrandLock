#include "Theme.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sl::ui
{
    // Copied from prototype-v4/src/shell.html (:root, and :root[data-theme="light"]). The theme test checks
    // every name and value against Tests/fixtures/ui/theme.json, which tools/fixtures/ui.js reads from the
    // same file.
    const std::array<TokenText, kNumTokens> kTokens { {
        { "bg",              "#151618",               "#f3f4f7" },
        { "panel",           "#1d1e21",               "#ffffff" },
        { "panel2",          "#18191c",               "#f7f8fb" },
        { "line",            "#33353a",               "#d9dce5" },
        { "txt",             "#ece8e1",               "#22242a" },
        { "dim",             "#9a9ea6",               "#6b707c" },
        { "acc",             "#b58cff",               "#7b4fe0" },
        { "onacc",           "#170a2a",               "#ffffff" },
        { "warn",            "#ff5468",               "#e8384f" },
        { "btn",             "#27282c",               "#eef0f5" },
        { "btnb",            "#3a3c42",               "#cfd3de" },
        { "roll",            "#0f1012",               "#ffffff" },
        { "hdr",             "#1d1e21",               "#eceef3" },
        { "menu",            "#27282c",               "#ffffff" },
        { "hover",           "#33353a",               "#eef0f5" },
        { "shadow",          "rgba(0,0,0,.45)",       "rgba(38,43,82,.25)" },
        { "toast-bg",        "rgba(0,0,0,.84)",       "rgba(20,22,30,.9)" },
        { "toast-txt",       "#ffffff",               "#ffffff" },
        { "row",             "#1d1e21",               "#fbfbfd" },
        { "row-black",       "#18191c",               "#eef0f5" },
        { "row-out",         "#141517",               "#e3e6ee" },
        { "row-root",        "#22202c",               "#ece5fb" },
        { "row-line",        "#25272b",               "#e4e7ee" },
        { "oct-line",        "#3a3c42",               "#c3c8d6" },
        { "grid-bar",        "#5d6169",               "#9aa0b4" },
        { "grid-beat",       "#33353a",               "#cdd1dc" },
        { "grid-sub",        "#232428",               "#e4e7ee" },
        { "note-line",       "#3ee6a8",               "#0b9e74" },
        { "note-line-sel",   "#9dffd9",               "#067a59" },
        { "start-ring",      "#f5c84a",               "#e0a400" },
        { "dot-fill",        "#151618",               "#ffffff" },
        { "piano-white",     "#d8d6d0",               "#ffffff" },
        { "piano-black",     "#151618",               "#2a2c33" },
        { "piano-text",      "#555555",               "#555555" },
        { "piano-out",       "rgba(0,0,0,.33)",       "rgba(34,36,42,.15)" },
        { "piano-held",      "#ff5468",               "#e8384f" },
        { "ruler-text",      "#ece8e1",               "#22242a" },
        { "ruler-tick",      "#5d6169",               "#b8bdcc" },
        { "link",            "#b58cff",               "#7b4fe0" },
        { "handle-ring",     "#151618",               "#ffffff" },
        { "playhead",        "#ff7a00",               "#ff7a00" },
        { "box-fill",        "rgba(181,140,255,.12)", "rgba(123,79,224,.10)" },
        { "box-line",        "#b58cff",               "#7b4fe0" },
        { "node-sel",        "#ffffff",               "#22242a" },
        { "ghost",           "#ffffff",               "#22242a" },
        { "lane-pitch",      "#3ee6a8",               "#0b9e74" },
        { "lane-amp",        "#4f8bff",               "#2f6fe8" },
        { "lane-vib",        "#e05cff",               "#b03fd6" },
        { "lane-tone",       "#ff9f1c",               "#e08600" },
        { "lane-formant",    "#ff4d8d",               "#e0306f" },
        { "lane-hollow",     "#22c3e6",               "#0fa0c2" },
        { "lane-sample",     "#e2c275",               "#b8913a" },
        { "on-lane",         "#111111",               "#ffffff" },
        { "a-act",           "0.42",                  "0.38" },
        { "a-act-sel",       "0.62",                  "0.55" },
        { "a-idle",          "0.13",                  "0.15" },
        { "a-idle-sel",      "0.22",                  "0.24" },
        { "strip-bg",        "#1c2046",               "#e8ebf6" },
        { "wave",            "#5fffc8",               "#2e9c7c" },
        { "strip-shade",     "rgba(0,0,0,.5)",        "rgba(255,255,255,.62)" },
        { "region-edge",     "#ffb000",               "#d48a00" },
        { "region-band",     "rgba(255,176,0,.38)",   "rgba(212,138,0,.32)" },
        { "head",            "#c9a8ff",               "#7b4fe0" },
        { "head-txt",        "#170a2a",               "#ffffff" },
        { "grain",           "#efe4ff",               "#5a2fc0" },
        { "strip-txt",       "#ffffff",               "#22242a" },
        { "strip-txt-b",     "#5fffc8",               "#0b9e74" },
        { "start-mark",      "#f5c84a",               "#d48a00" },
        { "meter-bg",        "#0f1012",               "#e3e6ee" },
        { "meter-ok",        "#3ee6a8",               "#0b9e74" },
        { "meter-hot",       "#ff5468",               "#e8384f" },
        { "tip-bg",          "rgba(0,0,0,.86)",       "rgba(20,22,30,.9)" },
        { "tip-txt",         "#ffffff",               "#ffffff" },
        { "drop-bg",         "rgba(181,140,255,.16)", "rgba(123,79,224,.12)" },
        { "kbd-bg",          "rgba(0,0,0,.18)",       "rgba(0,0,0,.08)" },
        { "chip-warn-bg",    "#3a2a0a",               "#fff1d6" },
        { "chip-warn-txt",   "#ffd27a",               "#7a4a00" },
    } };

    namespace
    {
        int hexDigit (char c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        /** Reads a decimal number at text[pos] ("255", ".45", "0.42"), moving pos past it. */
        bool readNumber (const std::string& text, std::size_t& pos, double& out)
        {
            const std::size_t start = pos;
            while (pos < text.size() && (std::isdigit (static_cast<unsigned char> (text[pos])) != 0 || text[pos] == '.'))
                ++pos;
            if (pos == start)
                return false;
            out = std::strtod (text.substr (start, pos - start).c_str(), nullptr);
            return true;
        }

        double linear (std::uint8_t channel) noexcept
        {
            const double c = static_cast<double> (channel) / 255.0;
            return c <= 0.04045 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
        }

        Palette readPalette (ThemeId id)
        {
            Palette p;
            p.id = id;
            for (std::size_t i = 0; i < kTokens.size(); ++i)
            {
                const auto value = parseCss (id == ThemeId::light ? kTokens[i].light : kTokens[i].dark);
                jassert (value.ok);
                p.colours[i] = value.isNumber ? juce::Colours::transparentBlack : value.colour;
                p.numbers[i] = value.isNumber ? static_cast<float> (value.number) : 0.0f;
            }
            return p;
        }
    }

    CssValue parseCss (const char* textIn)
    {
        CssValue v;
        if (textIn == nullptr)
            return v;
        const std::string text (textIn);

        if (text.size() == 7 && text[0] == '#')
        {
            int digits[6] {};
            for (std::size_t i = 0; i < 6; ++i)
            {
                digits[i] = hexDigit (text[i + 1]);
                if (digits[i] < 0)
                    return v;
            }
            v.colour = juce::Colour (static_cast<juce::uint8> (digits[0] * 16 + digits[1]),
                                     static_cast<juce::uint8> (digits[2] * 16 + digits[3]),
                                     static_cast<juce::uint8> (digits[4] * 16 + digits[5]));
            v.ok = true;
            return v;
        }

        if (text.rfind ("rgba(", 0) == 0 && text.back() == ')')
        {
            double parts[4] {};
            std::size_t pos = 5;
            for (int i = 0; i < 4; ++i)
            {
                if (! readNumber (text, pos, parts[i]))
                    return v;
                const char expected = i < 3 ? ',' : ')';
                if (pos >= text.size() || text[pos] != expected)
                    return v;
                ++pos;
            }
            if (pos != text.size())
                return v;
            for (int i = 0; i < 3; ++i)
                if (parts[i] < 0.0 || parts[i] > 255.0)
                    return v;
            if (parts[3] < 0.0 || parts[3] > 1.0)
                return v;
            v.colour = juce::Colour (static_cast<juce::uint8> (parts[0]), static_cast<juce::uint8> (parts[1]),
                                     static_cast<juce::uint8> (parts[2]), static_cast<float> (parts[3]));
            v.ok = true;
            return v;
        }

        std::size_t pos = 0;
        double number = 0.0;
        if (readNumber (text, pos, number) && pos == text.size())
        {
            v.isNumber = true;
            v.number = number;
            v.ok = true;
        }
        return v;
    }

    double relativeLuminance (juce::Colour c) noexcept
    {
        return 0.2126 * linear (c.getRed()) + 0.7152 * linear (c.getGreen()) + 0.0722 * linear (c.getBlue());
    }

    double contrastRatio (juce::Colour a, juce::Colour b) noexcept
    {
        const double la = relativeLuminance (a), lb = relativeLuminance (b);
        return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
    }

    const Palette& palette (ThemeId id)
    {
        static const Palette dark = readPalette (ThemeId::dark);
        static const Palette light = readPalette (ThemeId::light);
        return id == ThemeId::light ? light : dark;
    }

    juce::String Theme::fontName()
    {
       #if JUCE_WINDOWS
        return "Segoe UI";
       #else
        return juce::Font::getDefaultSansSerifFontName();
       #endif
    }

    juce::Font Theme::font (float height, bool bold, float tracking)
    {
        return juce::Font (juce::FontOptions (fontName(), height, bold ? juce::Font::bold : juce::Font::plain)
                               .withKerningFactor (tracking));
    }

    juce::Font Theme::px (float cssPixels, bool bold, float tracking)
    {
        return juce::Font (juce::FontOptions (fontName(), cssPixels, bold ? juce::Font::bold : juce::Font::plain)
                               .withPointHeight (cssPixels)
                               .withKerningFactor (tracking));
    }

    float Theme::textWidth (const juce::Font& f, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (f, text);
    }
}
