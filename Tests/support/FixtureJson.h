#pragma once

// Reading the golden fixtures (PLUGIN-PLAN 5.1, tools/README-fixtures.md) and comparing with them.
// Owned by M1 after the scaffold and append-only while M0 is open: add, never change (PLUGIN-PLAN 7.2).

#include "core/JsonLite.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sl::test::fixture
{
    /** The fixtures folder: --fixtures DIR, else Tests/fixtures found upward from the working folder. */
    std::string dir();

    /** A file's bytes, as they are on disk. False when it cannot be read. */
    bool readFile (const std::string& path, std::string& bytes);

    /** The same with every CRLF turned into LF (a Windows checkout of the text fixtures). */
    bool readText (const std::string& path, std::string& text);

    /** Reads and parses dir()/relativePath ("model/seeds.json"). On failure it makes a failed check that
        names the file and the parse error, and returns false. */
    bool load (std::string_view relativePath, core::JsonValue& out);

    /** Every .json file under dir(), as paths relative to it with forward slashes, sorted. */
    std::vector<std::string> listFiles();

    /** The cases of one kind: the elements of doc["cases"] whose "kind" is kind. */
    std::vector<const core::JsonValue*> casesOfKind (const core::JsonValue& doc, std::string_view kind);

    /** A fixture number: a JSON number, or one of the strings "NaN", "Infinity", "-Infinity" (JSON cannot
        hold them). Anything else is NaN. */
    double num (const core::JsonValue& v) noexcept;

    /** True for a JSON number or one of those three strings. */
    bool isNum (const core::JsonValue& v) noexcept;

    /** Standard base64 ('+', '/', '=' padding) to bytes. False on a bad character or length. */
    bool base64Decode (std::string_view text, std::vector<std::uint8_t>& bytes);

    /** F32 = {f32: base64} of little-endian binary32 values. False when the shape is wrong. */
    bool f32 (const core::JsonValue& v, std::vector<float>& out);

    /** Table = F32 | {n, flat}: n copies of the binary32 flat (written as its exact double). */
    bool table (const core::JsonValue& v, std::vector<float>& out);

    /** Units in the last place between two floats (or doubles): 0 for +0 against -0 and for NaN against
        NaN; the largest value for NaN against a number. */
    std::uint64_t ulps (float a, float b) noexcept;
    std::uint64_t ulps (double a, double b) noexcept;

    /** a == b, with +0 equal to -0 and NaN equal to NaN (PLUGIN-PLAN 1.6 rule 8). */
    bool same (double a, double b) noexcept;

    /** |a - b| <= rel * max (|a|, |b|), or same (a, b). */
    bool withinRel (double a, double b, double rel) noexcept;

    /** |a - b| <= tolerance, or same (a, b). */
    bool withinAbs (double a, double b, double tolerance) noexcept;
}
