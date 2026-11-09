#pragma once

// The suite's check style (GrainLock's Tests/TestMain.cpp): every check prints one line,
// "[PASS] what" or "[FAIL] what", and the runner exits non-zero when any failed.
// Frozen after the scaffold: a change needs both M0's and M1's notes (PLUGIN-PLAN 7.2).

#include <string>

namespace sl::test
{
    /** Prints "[PASS] what" or "[FAIL] what" and counts it. */
    void check (bool ok, const std::string& what);

    /** Prints "\n== name ==" before a block of checks. */
    void section (const std::string& name);

    /** Prints an indented line that is not a check: a measured figure, a target, a skipped case. */
    void note (const std::string& text);

    /** printf-style formatting into a std::string (any length). */
    std::string fmt (const char* format, ...)
#if defined (__GNUC__)
        __attribute__ ((format (printf, 1, 2)))
#endif
        ;

    struct Tally
    {
        long long checks = 0;
        long long failures = 0;
    };

    /** Every check made so far in this run. */
    Tally tally() noexcept;
}
