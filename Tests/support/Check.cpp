#include "Check.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace sl::test
{
    namespace
    {
        std::atomic<long long> checkCount { 0 };
        std::atomic<long long> failureCount { 0 };
    }

    void check (bool ok, const std::string& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
        std::fflush (stdout);
        ++checkCount;
        if (! ok)
            ++failureCount;
    }

    void section (const std::string& name)
    {
        std::printf ("\n== %s ==\n", name.c_str());
        std::fflush (stdout);
    }

    void note (const std::string& text)
    {
        std::printf ("    %s\n", text.c_str());
        std::fflush (stdout);
    }

    std::string fmt (const char* format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start (args, format);
        va_list again;
        va_copy (again, args);
        const int length = std::vsnprintf (buffer, sizeof (buffer), format, args);
        va_end (args);

        std::string out;
        if (length < 0)
        {
            out = format;
        }
        else if ((std::size_t) length < sizeof (buffer))
        {
            out.assign (buffer, (std::size_t) length);
        }
        else
        {
            std::vector<char> big ((std::size_t) length + 1);
            std::vsnprintf (big.data(), big.size(), format, again);
            out.assign (big.data(), (std::size_t) length);
        }
        va_end (again);
        return out;
    }

    Tally tally() noexcept
    {
        return { checkCount.load(), failureCount.load() };
    }
}
