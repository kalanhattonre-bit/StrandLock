// The JS compatibility helpers row of PLUGIN-PLAN 4 M1: jsNumberToString, jsToFixed, jsToPrecision, toBase36,
// jsRound and toInt32 equal the 500+ fixture strings and numbers of model/seeds.json exactly; plus the
// UTF-16 string order (jsStringLess) the prototype's sorts use, and V8's exp (JsMath.h).

#include "core/JsCompat.h"
#include "core/JsMath.h"
#include "core/JsonLite.h"
#include "core/Seeds.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using core::JsonValue;

    std::string str (double x) { return core::jsNumberToString (x); }

    /** a < b by UTF-16 code units, the long way: both strings to char16_t, then std::u16string's order. */
    bool utf16Less (const std::string& a, const std::string& b)
    {
        std::u16string ua, ub;
        core::forEachUtf16Unit (a, [&ua] (std::uint32_t u) { ua.push_back (static_cast<char16_t> (u)); });
        core::forEachUtf16Unit (b, [&ub] (std::uint32_t u) { ub.push_back (static_cast<char16_t> (u)); });
        return ua < ub;
    }
}

SL_TEST ("model", jsCompatHelpers)
{
    section ("JS compatibility helpers against model/seeds.json: String (x), toFixed, toPrecision, toString (36), Math.round, x | 0");

    JsonValue doc;
    if (! fixture::load ("model/seeds.json", doc))
        return;

    int total = 0, wrong = 0;
    auto kind = [&] (const char* name, auto&& matches)
    {
        int n = 0, bad = 0;
        for (const auto* c : fixture::casesOfKind (doc, name))
        {
            ++n;
            std::string why;
            if (! matches (*c, why))
            {
                if (bad < 5)
                    note ("differs: " + why);
                ++bad;
            }
        }
        check (n > 0 && bad == 0, fmt ("%s: %d of %d fixture cases exactly", name, n - bad, n));
        total += n;
        wrong += bad;
    };

    kind ("jsNumber", [] (const JsonValue& c, std::string& why)
    {
        const double x = fixture::num (c["x"]);
        const std::string got = core::jsNumberToString (x);
        why = "String (" + str (x) + ") = " + got + ", want " + c["s"].asString();
        return got == c["s"].asString();
    });
    kind ("toFixed", [] (const JsonValue& c, std::string& why)
    {
        const double x = fixture::num (c["x"]);
        const int d = static_cast<int> (fixture::num (c["d"]));
        const std::string got = core::jsToFixed (x, d);
        why = "(" + str (x) + ").toFixed (" + std::to_string (d) + ") = " + got + ", want " + c["s"].asString();
        return got == c["s"].asString();
    });
    kind ("toPrecision", [] (const JsonValue& c, std::string& why)
    {
        const double x = fixture::num (c["x"]);
        const int d = static_cast<int> (fixture::num (c["d"]));
        const std::string got = core::jsToPrecision (x, d);
        why = "(" + str (x) + ").toPrecision (" + std::to_string (d) + ") = " + got + ", want " + c["s"].asString();
        return got == c["s"].asString();
    });
    kind ("base36", [] (const JsonValue& c, std::string& why)
    {
        const auto u = static_cast<std::uint32_t> (fixture::num (c["u"]));
        const std::string got = core::toBase36 (u);
        why = "(" + std::to_string (u) + ").toString (36) = " + got + ", want " + c["s"].asString();
        return got == c["s"].asString();
    });
    kind ("jsRound", [] (const JsonValue& c, std::string& why)
    {
        const double x = fixture::num (c["x"]), got = core::jsRound (x);
        why = "Math.round (" + str (x) + ") = " + str (got) + ", want " + str (fixture::num (c["r"]));
        return fixture::same (got, fixture::num (c["r"]));
    });
    kind ("toInt32", [] (const JsonValue& c, std::string& why)
    {
        const double x = fixture::num (c["x"]);
        const auto got = core::toInt32 (x);
        why = str (x) + " | 0 = " + std::to_string (got) + ", want " + str (fixture::num (c["i"]));
        return fixture::same (static_cast<double> (got), fixture::num (c["i"]));
    });
    check (total >= 500 && wrong == 0, fmt ("the six helpers: %d of %d fixture strings and numbers exactly", total - wrong, total));

    // Base 36 at the edges (the values chop, duplicate and add ids are made of).
    check (core::toBase36 (0) == "0" && core::toBase36 (35) == "z" && core::toBase36 (36) == "10" && core::toBase36 (4294967295u) == "1z141z3",
           "toBase36: 0, 35, 36 and 2^32 - 1 (\"0\", \"z\", \"10\", \"1z141z3\")");
}

SL_TEST ("model", jsStringOrder)
{
    section ("jsStringLess: JavaScript's string order (UTF-16 code units) for ids stored as UTF-8");

    JsonValue doc;
    if (! fixture::load ("model/seeds.json", doc))
        return;

    // Every pair of the 60 idHash strings (ASCII, accents, CJK, emoji and surrogate pairs) against the
    // UTF-16 order computed the long way.
    std::vector<std::string> ids;
    for (const auto* c : fixture::casesOfKind (doc, "idHash"))
        ids.push_back ((*c)["s"].asString());
    int pairs = 0, bad = 0;
    for (const auto& a : ids)
        for (const auto& b : ids)
        {
            ++pairs;
            if (core::jsStringLess (a, b) != utf16Less (a, b))
            {
                if (bad < 5)
                    note ("differs: \"" + a + "\" < \"" + b + "\"");
                ++bad;
            }
        }
    check (ids.size() >= 60 && bad == 0, fmt ("%d of %d ordered pairs of the fixture ids agree with UTF-16 order", pairs - bad, pairs));

    // Where UTF-16 order is not code point (UTF-8 byte) order: U+FFFF sorts after U+1F600, whose first unit
    // is the surrogate 0xD83D. A port comparing UTF-8 bytes gets these backwards.
    const std::string ffff = "\xEF\xBF\xBF", grin = "\xF0\x9F\x98\x80";
    check (core::jsStringLess (grin, ffff) && ! core::jsStringLess (ffff, grin),
           "\"\\u{1F600}\" < \"\\uFFFF\" (surrogates sort below U+E000..U+FFFF)");
    check (core::jsStringLess ("", "a") && core::jsStringLess ("ab", "abc") && ! core::jsStringLess ("abc", "ab") && ! core::jsStringLess ("a", "a")
               && core::jsStringLess ("B", "a") && core::jsStringLess ("sd-10", "sd-9"),
           "shorter first, equal is not less, 'B' < 'a', 'sd-10' < 'sd-9'");
}

SL_TEST ("model", jsExpMatchesV8)
{
    section ("jsExp: V8's Math.exp (fdlibm e_exp.c plus exp (1) = e), bit for bit");

    // Math.exp in node 24.16.0 for these inputs, as bit patterns (the inputs bendShape's fixture cases use, and edges).
    struct Pair
    {
        double x;
        std::uint64_t bits;
    };
    const Pair pairs[] = {
        { 1.0, 0x4005bf0a8b145769ull },  { -1.0, 0x3fd78b56362cef38ull }, { 0.5, 0x3ffa61298e1e069cull },
        { 0.04, 0x3ff0a72932c7b125ull }, { -0.04, 0x3feebec97e700b8dull }, { 0.8, 0x4001cde866fe46e9ull },
        { -0.8, 0x3fdcc1ce4581db88ull }, { 1.6, 0x4013cfe7bb5b37ecull },  { 7.2, 0x4094edb91a4d293aull },
        { -7.2, 0x3f4876d0d0b8aa81ull }, { 8.0, 0x40a749ea7d470c6eull },  { -8.0, 0x3f35fc21041027adull },
        { 0.1, 0x3ff1aec7b35a00d4ull },  { 1e-9, 0x3ff000000044b830ull }, { -1e-9, 0x3fefffffff768fa1ull },
        { 700.0, 0x7f0d945df4f8ec8eull }, { -700.0, 0x00d14f2b0fb9307full }, { 0.6931471805599453, 0x4000000000000000ull },
        { 3.4657359027997265, 0x4040000000000000ull }, { -0.34657359027997264, 0x3fe6a09e667f3bccull },
    };
    int bad = 0;
    for (const auto& p : pairs)
    {
        const auto got = std::bit_cast<std::uint64_t> (core::jsExp (p.x));
        if (got != p.bits)
        {
            note (fmt ("jsExp (%s) = %s", str (p.x).c_str(), str (core::jsExp (p.x)).c_str()));
            ++bad;
        }
    }
    check (bad == 0, fmt ("jsExp equals Math.exp at %d inputs, bit for bit", static_cast<int> (std::size (pairs))));
    check (std::isinf (core::jsExp (710.0)) && fixture::same (core::jsExp (-746.0), 0.0) && std::isnan (core::jsExp (std::nan ("")))
               && std::isinf (core::jsExp (std::numeric_limits<double>::infinity()))
               && fixture::same (core::jsExp (-std::numeric_limits<double>::infinity()), 0.0) && fixture::same (core::jsExp (0.0), 1.0),
           "jsExp: overflow is infinity, underflow 0, NaN NaN, exp (+-inf) = inf, 0, exp (0) = 1");
}
