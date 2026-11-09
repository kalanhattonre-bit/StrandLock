#pragma once

// Reading the model fixtures' shapes (PLUGIN-PLAN 5.1, tools/README-fixtures.md) into model types, comparing
// results with them, and the case tally every fixture test prints: how many cases it ran (it fails when that
// differs from the file's count: no silent skips) and how many matched bit for bit.

#include "core/JsonLite.h"
#include "model/Clip.h"
#include "model/ClipEdit.h"
#include "model/Node.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sl::test::modelfx
{
    /** Nodes = [[t, v, shape, bend], ...], shape 0 smooth, 1 linear, 2 bend, 3 step. */
    bool nodesFrom (const core::JsonValue& tuples, model::NodeList& out, std::string& why);

    /** A fixture Note: omitted lanes are flat at their default, a lane with no generator is left out of gen,
        and gen holds each generator as normalizeGen returns it. */
    bool noteFrom (const core::JsonValue& v, model::Note& out, std::string& why);

    /** A fixture Clip = { bars, notes }. Every other clip setting keeps its default. */
    bool clipFrom (const core::JsonValue& v, model::Clip& out, std::string& why);

    /** The file's "clips" table, decoded. Fails (a check) when one cannot be read. */
    bool clipsFrom (const core::JsonValue& doc, std::map<std::string, model::Clip>& out);

    /** The note with this id, or null. */
    const model::Note* findNote (const model::Clip& clip, const std::string& id) noexcept;

    /** got against a fixture Nodes list: the same length and shapes, and t, v and bend each within tol.
        bitExact is cleared when any value differs at all. */
    bool nodesMatch (const model::NodeList& got, const core::JsonValue& want, double tol, bool& bitExact, std::string& why);

    /** got against a fixture Table (F32 or {n, flat}): the same length, and every value within maxUlps
        Float32 ulps. bitExact is cleared when any value differs at all. */
    bool tableMatch (const std::vector<float>& got, const core::JsonValue& want, std::uint64_t maxUlps, bool& bitExact, std::string& why);

    /** Readable short forms for failure lines. */
    std::string nodesText (const model::NodeList& nodes);

    /** got against want, both model Notes (want usually decoded from a fixture with noteFrom): id, p, s, l,
        glideTo and every flag, choice and integer exact; vel, glide, gc, the Start's pos and range, every node
        value and every generator depth, warp, amount and smooth within tol. bitExact is cleared when any value
        differs at all. */
    bool notesClose (const model::Note& got, const model::Note& want, double tol, bool& bitExact, std::string& why);

    /** got's notes against a fixture Note list, in order (bars too when the fixture clip has them). */
    bool clipNotesClose (const std::vector<model::NotePtr>& got, const core::JsonValue& want, double tol, bool& bitExact, std::string& why);

    /** Every bit of two notes the same: the fields of notesClose compared as bit patterns (so -0 differs from
        0 and a NaN equals the same NaN), plus the extras pointer. The serial is not compared. */
    bool bitIdentical (const model::Note& a, const model::Note& b, std::string& why);

    /** A fixture's ids argument: null = every note, a string = one id, an array = a list. */
    model::Ids idsFrom (const core::JsonValue& v);

    /** A JSON array of strings. */
    std::vector<std::string> stringsFrom (const core::JsonValue& v);

    /** Structural equality of two JSON values: numbers by value (+0 equal to -0), objects by key whatever
        their order. why names the first difference. */
    bool jsonSame (const core::JsonValue& a, const core::JsonValue& b, const std::string& path, std::string& why);

    /** Counts one fixture file's cases. Every test that reads a fixture file makes one of these, counts each
        case into it, and calls finish (). */
    class FileTally
    {
    public:
        FileTally (std::string fileName, std::size_t casesInFile);

        /** A case within tolerance; bitExact when every value it compares is identical. */
        void pass (bool bitExact);

        /** A case outside tolerance (the first few are printed). */
        void fail (const std::string& detail);

        /** Prints "N cases run (the file has M), K bit for bit, F outside tolerance" and makes two checks:
            the count equals the file's, and no case is outside tolerance. */
        void finish();

        int ran() const noexcept { return passed + failed; }

    private:
        std::string file;
        std::size_t inFile;
        int passed = 0;
        int failed = 0;
        int exact = 0;
    };
}
