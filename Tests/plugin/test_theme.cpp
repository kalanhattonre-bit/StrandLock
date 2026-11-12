// The theme (PLUGIN-PLAN 4 M0, "theme"): both palettes hold every one of the 77 tokens of
// Tests/fixtures/ui/theme.json with the same values, the accents are the plan's, and the lane lines and the
// playhead keep at least 2.5:1 contrast against the roll in both themes. From M2 (7.4 decision 2) three light
// tokens are StrandLock's own: the Tone and Sample lane colours and the playhead, darkened in the prototype's
// hue; the test checks those instead of equality. Since the M3 fix pass (7.5 decision 1) the reference is what is
// drawn under them, the darker roll row #eef0f5 and the strip #e8ebf6 (M2 used white, which the roll never
// draws): at least 3:1 against both, and no darker than needed (under 3.1:1 against the strip, the darker one).

#include <juce_gui_basics/juce_gui_basics.h>

#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::Tok;
    using ui::ThemeId;

    const char* themeName (ThemeId id) { return id == ThemeId::light ? "light" : "dark"; }

    bool isDarkenedLight (const std::string& name)
    {
        for (const Tok t : ui::kDarkenedLight)
            if (name == ui::kTokens[static_cast<std::size_t> (t)].name)
                return true;
        return false;
    }

    /** The colour of a theme's token in ui/theme.json (the prototype's own value). */
    juce::Colour fixtureColour (const core::JsonValue& doc, bool light, Tok t)
    {
        const auto& cases = doc["cases"];
        for (std::size_t c = 0; c < cases.size(); ++c)
            if ((cases[c]["theme"].asString() == "light") == light)
                if (const auto* v = cases[c]["tokens"].find (ui::kTokens[static_cast<std::size_t> (t)].name))
                    return ui::parseCss (v->asString().c_str()).colour;
        return juce::Colours::transparentBlack;
    }

    /** The hue in degrees (0..360) of an sRGB colour. */
    double hueOf (juce::Colour c)
    {
        return (double) c.getHue() * 360.0;
    }
}

SL_TEST ("theme", themeTokensMatchFixture)
{
    section ("Theme: both palettes define all 77 tokens of ui/theme.json with identical values, but for the three light tokens of decision 2");

    core::JsonValue doc;
    if (! fixture::load ("ui/theme.json", doc))
        return;

    const auto& cases = doc["cases"];
    check (cases.size() == 2, fmt ("the fixture has 2 themes (%d)", (int) cases.size()));
    check (ui::kTokens.size() == 77 && ui::kNumTokens == 77, fmt ("the plugin has 77 tokens (%d)", (int) ui::kTokens.size()));

    std::set<std::string> ourNames;
    for (const auto& t : ui::kTokens)
        ourNames.insert (t.name);
    check (ourNames.size() == ui::kTokens.size(), fmt ("no token name twice (%d different names)", (int) ourNames.size()));

    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const auto& themeCase = cases[c];
        const bool light = themeCase["theme"].asString() == "light";
        const auto& tokens = themeCase["tokens"];
        check ((int) tokens.size() == 77 && (int) themeCase["count"].asNumber() == 77,
               fmt ("%s: the fixture lists 77 tokens (%d)", light ? "light" : "dark", (int) tokens.size()));

        // Every fixture token is ours with the same text, in the same order, and we have no others; the three
        // darkened light tokens (decision 2) are checked in themeLaneContrast instead, and must differ here.
        int missing = 0, different = 0, outOfOrder = 0, darkenedSame = 0;
        std::string firstProblem;
        for (std::size_t i = 0; i < tokens.size(); ++i)
        {
            const auto& name = tokens.keyAt (i);
            const auto& want = tokens.valueAt (i).asString();
            const ui::TokenText* ours = nullptr;
            for (const auto& t : ui::kTokens)
                if (name == t.name)
                    ours = &t;
            if (ours == nullptr)
            {
                ++missing;
                if (firstProblem.empty())
                    firstProblem = name + " is missing";
                continue;
            }
            const std::string have = light ? ours->light : ours->dark;
            if (light && isDarkenedLight (name))
            {
                darkenedSame += have == want ? 1 : 0;
            }
            else if (have != want)
            {
                ++different;
                if (firstProblem.empty())
                    firstProblem = name + ": " + have + " instead of " + want;
            }
            if (i < ui::kTokens.size() && name != ui::kTokens[i].name)
                ++outOfOrder;
        }
        int extra = 0;
        for (const auto& t : ui::kTokens)
            if (tokens.find (t.name) == nullptr)
                ++extra;
        const int equalWanted = light ? 77 - (int) ui::kDarkenedLight.size() : 77;
        check (missing == 0 && different == 0 && extra == 0 && outOfOrder == 0,
               fmt ("%s: %d tokens equal the fixture (%d missing, %d different, %d extra, %d out of order)%s%s",
                    light ? "light" : "dark", equalWanted, missing, different, extra, outOfOrder,
                    firstProblem.empty() ? "" : "; first: ", firstProblem.c_str()));
        if (light)
            check (darkenedSame == 0, fmt ("light: the %d darkened tokens are StrandLock's own, not the prototype's (%d still equal)",
                                           (int) ui::kDarkenedLight.size(), darkenedSame));
    }

    // Every value reads as a colour, except the four opacities, which read as numbers.
    for (const ThemeId id : { ThemeId::dark, ThemeId::light })
    {
        int bad = 0, numbers = 0;
        for (const auto& t : ui::kTokens)
        {
            const auto v = ui::parseCss (id == ThemeId::light ? t.light : t.dark);
            bad += v.ok ? 0 : 1;
            numbers += v.ok && v.isNumber ? 1 : 0;
            const bool shouldBeNumber = std::string (t.name).rfind ("a-", 0) == 0;
            bad += v.ok && v.isNumber != shouldBeNumber ? 1 : 0;
        }
        check (bad == 0 && numbers == 4, fmt ("%s: every token reads (%d bad), the 4 a-* opacities as numbers (%d)", themeName (id), bad, numbers));
    }

    const auto& dark = ui::palette (ThemeId::dark);
    const auto& light = ui::palette (ThemeId::light);
    check (dark[Tok::acc] == juce::Colour (0xffb58cff) && ui::Theme::darkAccent == dark[Tok::acc],
           fmt ("dark accent #B58CFF (#%s)", dark[Tok::acc].toDisplayString (false).toRawUTF8()));
    check (light[Tok::acc] == juce::Colour (0xff7b4fe0) && ui::Theme::lightAccent == light[Tok::acc],
           fmt ("light accent #7B4FE0 (#%s)", light[Tok::acc].toDisplayString (false).toRawUTF8()));
    check (dark[Tok::bg] == juce::Colour (0xff151618) && light[Tok::bg] == juce::Colour (0xfff3f4f7),
           "the backgrounds: dark #151618, light #F3F4F7");
    check (dark[Tok::boxFill] == juce::Colour ((juce::uint8) 181, (juce::uint8) 140, (juce::uint8) 255, (juce::uint8) 31),
           fmt ("rgba(181,140,255,.12) reads with alpha 31 of 255 (%d)", (int) dark[Tok::boxFill].getAlpha()));
    check (std::abs (dark.number (Tok::aAct) - 0.42f) < 1.0e-6f && std::abs (light.number (Tok::aIdleSel) - 0.24f) < 1.0e-6f,
           fmt ("the opacities read as numbers: a-act %.2f (dark), a-idle-sel %.2f (light)", (double) dark.number (Tok::aAct), (double) light.number (Tok::aIdleSel)));

    // A switch is one call: the same Theme object answers with the other palette.
    ui::Theme live (ThemeId::dark);
    const auto before = live (Tok::panel);
    live.set (ThemeId::light);
    check (before == juce::Colour (0xff1d1e21) && live (Tok::panel) == juce::Colour (0xffffffff) && ! live.isDark(),
           "a Theme switches from the dark panel #1D1E21 to the light #FFFFFF");
}

SL_TEST ("theme", themeParsesCss)
{
    section ("Theme: the CSS reader takes what shell.html writes and nothing else");

    const auto hex = ui::parseCss ("#4f8bff");
    check (hex.ok && ! hex.isNumber && hex.colour == juce::Colour (0xff4f8bff), "#4f8bff");
    const auto rgba = ui::parseCss ("rgba(20,22,30,.9)");
    check (rgba.ok && rgba.colour.getRed() == 20 && rgba.colour.getGreen() == 22 && rgba.colour.getBlue() == 30 && rgba.colour.getAlpha() == 230,
           fmt ("rgba(20,22,30,.9): alpha 230 (%d)", (int) rgba.colour.getAlpha()));
    const auto number = ui::parseCss ("0.13");
    check (number.ok && number.isNumber && std::abs (number.number - 0.13) < 1.0e-12, "0.13 is a number");

    int refused = 0;
    for (const char* bad : { "#12345", "#12345g", "rgba(1,2,3)", "rgba(1,2,3,.5", "rgba(300,0,0,1)", "rgba(0,0,0,2)", "blue", "", "1.2.3x" })
        refused += ui::parseCss (bad).ok ? 0 : 1;
    check (refused == 9, fmt ("9 malformed values refused (%d)", refused));
}

SL_TEST ("theme", themeLaneContrast)
{
    section ("Theme: lane lines and the playhead keep >= 2.5:1 contrast against the roll, in both themes");

    // The WCAG formula, checked on the two ends first: white on black is 21:1.
    check (std::abs (ui::contrastRatio (juce::Colours::white, juce::Colours::black) - 21.0) < 1.0e-9,
           fmt ("white on black is 21:1 (%.4f)", ui::contrastRatio (juce::Colours::white, juce::Colours::black)));

    const Tok lanes[] = { Tok::lanePitch, Tok::laneAmp, Tok::laneVib, Tok::laneTone, Tok::laneFormant, Tok::laneHollow,
                          Tok::laneSample, Tok::noteLine };
    for (const ThemeId id : { ThemeId::dark, ThemeId::light })
    {
        const auto& p = ui::palette (id);
        double lo = 100.0, hi = 0.0;
        int below = 0;
        for (const Tok t : lanes)
        {
            const double r = ui::contrastRatio (p[t], p[Tok::roll]);
            lo = std::min (lo, r);
            hi = std::max (hi, r);
            below += r >= 2.5 ? 0 : 1;
        }
        const double head = ui::contrastRatio (p[Tok::playhead], p[Tok::roll]);
        check (below == 0, fmt ("%s: the 7 lane colours and the note line are %.2f to %.2f against the roll", themeName (id), lo, hi));
        check (head >= 2.5, fmt ("%s: the playhead is %.2f against the roll", themeName (id), head));
    }

    // The figures the plan quotes for the v4 palette (the prototype's, in ui/theme.json): lanes 2.77 to 11.86,
    // the playhead 2.61 on white.
    core::JsonValue doc;
    if (! fixture::load ("ui/theme.json", doc))
        return;
    const auto& light = ui::palette (ThemeId::light);
    const auto& dark = ui::palette (ThemeId::dark);
    const auto protoTone = fixtureColour (doc, true, Tok::laneTone);
    const auto protoHead = fixtureColour (doc, true, Tok::playhead);
    check (std::abs (ui::contrastRatio (protoTone, light[Tok::roll]) - 2.77) < 0.005
               && std::abs (ui::contrastRatio (dark[Tok::lanePitch], dark[Tok::roll]) - 11.86) < 0.005
               && std::abs (ui::contrastRatio (protoHead, light[Tok::roll]) - 2.61) < 0.005,
           "the plan's figures for the prototype's palette: light Tone 2.77, dark Pitch 11.86, light playhead 2.61");

    // Decision 2 (Kalan's answer 9.1) and 7.5 decision 1 (M2 review R2-4): in the light theme the Tone and Sample
    // lane colours and the playhead are darkened, in the same hue, to at least 3:1 against what is drawn under them,
    // the darker roll row (row-black #eef0f5) and the strip (strip-bg #e8ebf6); the prototype's were 2.2 to 2.6
    // there. The strip is the darker of the two, so "no darker than needed" is under 3.1:1 against the strip.
    const auto rowBlack = light[Tok::rowBlack], strip = light[Tok::stripBg];
    check (rowBlack == juce::Colour (0xffeef0f5) && strip == juce::Colour (0xffe8ebf6)
               && ui::relativeLuminance (strip) < ui::relativeLuminance (rowBlack),
           "light: the references are the roll row #EEF0F5 and the strip #E8EBF6 (the darker one)");
    for (const Tok t : ui::kDarkenedLight)
    {
        const auto ours = light[t], proto = fixtureColour (doc, true, t);
        const double onRow = ui::contrastRatio (ours, rowBlack), onStrip = ui::contrastRatio (ours, strip);
        const double beforeRow = ui::contrastRatio (proto, rowBlack), beforeStrip = ui::contrastRatio (proto, strip);
        const double hueShift = std::abs (hueOf (ours) - hueOf (proto));
        const auto* name = ui::kTokens[static_cast<std::size_t> (t)].name;
        check (onRow >= 3.0 && onStrip >= 3.0 && beforeRow < 3.0 && beforeStrip < 3.0,
               fmt ("light %s: %s is %.3f:1 against the roll row and %.3f:1 against the strip (the prototype's %s was %.3f and %.3f)", name,
                    ours.toDisplayString (false).toRawUTF8(), onRow, onStrip, proto.toDisplayString (false).toRawUTF8(), beforeRow,
                    beforeStrip));
        check (hueShift < 0.5 && ui::relativeLuminance (ours) < ui::relativeLuminance (proto) && onStrip < 3.1,
               fmt ("light %s: the same hue (%.2f and %.2f degrees), darker, and no darker than needed (%.3f:1 against the strip, under 3.1)",
                    name, hueOf (ours), hueOf (proto), onStrip));
    }
}
