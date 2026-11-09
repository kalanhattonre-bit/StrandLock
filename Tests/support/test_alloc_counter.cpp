// The allocation counter's own test: it must see a deliberate allocation and free, and nothing in an
// empty scope. Without this, a counter that counts nothing would pass every "0 allocations" test.
// Group "alloc", so it runs in the Release runs (Windows, Linux) and in the Debug-CRT run.

#include "AllocCounter.h"
#include "Check.h"
#include "TestRegistry.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    using namespace sl::test;

    // The deliberate allocations go through this, so the compiler cannot remove them as unused.
    void* volatile sink = nullptr;

    struct alignas (64) Wide
    {
        char bytes[200];
    };

    std::string describe (const alloc::Counts& c)
    {
        return fmt ("%lld new, %lld delete, largest %zu bytes, %lld bytes live, peak %lld", c.news, c.deletes, c.largest,
                    c.liveBytes, c.peakLiveBytes);
    }
}

SL_TEST ("alloc", counterSeesWhatHappens)
{
    section ("The allocation counter counts a deliberate new and delete, and nothing else");
    note (fmt ("counting %s", alloc::coverage()));

    constexpr std::size_t kSize = 1000;

    // An empty scope counts exactly nothing.
    alloc::reset();
    {
        const alloc::Scope counting;
    }
    auto c = alloc::counts();
    check (c.news == 0 && c.deletes == 0 && c.largest == 0 && c.liveBytes == 0 && c.peakLiveBytes == 0,
           "an empty counting scope counts 0 (" + describe (c) + ")");

    // new[] of a known size.
    alloc::reset();
    {
        const alloc::Scope counting;
        auto* bytes = new char[kSize];
        std::memset (bytes, 0x5a, kSize);
        sink = bytes;
    }
    c = alloc::counts();
    check (c.news >= 1, fmt ("new char[%zu] counts at least 1 new (%lld)", kSize, c.news));
    check (c.largest >= kSize, fmt ("and a largest request of at least %zu bytes (%zu)", kSize, c.largest));
    check (c.liveBytes >= (long long) kSize, fmt ("and at least %zu bytes live (%lld)", kSize, c.liveBytes));
    check (c.deletes == 0, fmt ("and no delete yet (%lld)", c.deletes));

    // delete[] of it, in a second scope that keeps adding to the same counts.
    {
        const alloc::Scope counting;
        delete[] static_cast<char*> (sink);
        sink = nullptr;
    }
    c = alloc::counts();
    check (c.deletes >= 1, fmt ("delete[] counts at least 1 delete (%lld)", c.deletes));
    check (c.liveBytes == 0, fmt ("after the delete, the bytes live are back to 0 (%lld)", c.liveBytes));
    check (c.peakLiveBytes >= (long long) kSize, fmt ("the peak remembers the %zu bytes (%lld)", kSize, c.peakLiveBytes));

    // The same new and delete outside a scope are not counted.
    alloc::reset();
    sink = new char[kSize];
    delete[] static_cast<char*> (sink);
    sink = nullptr;
    c = alloc::counts();
    check (c.news == 0 && c.deletes == 0, "outside a scope nothing is counted (" + describe (c) + ")");

    // A single over-aligned object: aligned new and delete are counted, and the block is aligned.
    alloc::reset();
    bool aligned = false;
    {
        const alloc::Scope counting;
        auto* w = new Wide;
        sink = w;
        aligned = reinterpret_cast<std::uintptr_t> (w) % alignof (Wide) == 0;
        std::memset (w->bytes, 1, sizeof (w->bytes));
        delete static_cast<Wide*> (sink);
        sink = nullptr;
    }
    c = alloc::counts();
    check (c.news >= 1 && c.deletes >= 1 && c.largest >= sizeof (Wide) && c.liveBytes == 0,
           fmt ("new and delete of a %zu-byte, %zu-aligned object: ", sizeof (Wide), alignof (Wide)) + describe (c));
    check (aligned, fmt ("the object came back %zu-byte aligned", alignof (Wide)));

    // malloc and free, where this build counts them (Linux and the Windows Debug CRT).
    if (alloc::countsMalloc())
    {
        alloc::reset();
        {
            const alloc::Scope counting;
            sink = std::malloc (3000);
            std::free (sink);
            sink = nullptr;
        }
        c = alloc::counts();
        check (c.news >= 1 && c.deletes >= 1 && c.largest >= 3000 && c.liveBytes == 0,
               "malloc (3000) and free are counted (" + describe (c) + ")");
    }
    else
    {
        note ("this build does not count malloc (Windows Release): the Debug-CRT run checks it");
    }
}
