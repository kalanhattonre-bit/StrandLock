#pragma once

// Counts heap allocations and frees made by one thread while a Scope is alive, so a test can prove the
// audio thread allocates nothing (PLUGIN-PLAN 7.1). AllocCounter.cpp replaces every global operator new
// and delete (with a size header, so it knows each block's size), and adds, where the platform allows:
//   - Linux (glibc): malloc, calloc, realloc, memalign, aligned_alloc, posix_memalign and free, by
//     interposition, which also catches JUCE's HeapBlock (it calls malloc, never operator new);
//   - Windows Debug CRT: every CRT heap allocation and free, through _CrtSetAllocHook.
// Windows Release counts operator new and delete only (coverage() says which). Sanitizer builds leave
// malloc to the sanitizer and count operator new and delete only.
// Frozen after the scaffold: a change needs both M0's and M1's notes (PLUGIN-PLAN 7.2).

#include <cstddef>

namespace sl::test::alloc
{
    struct Counts
    {
        long long news = 0;            // allocations: operator new (any form), and malloc & co where counted
        long long deletes = 0;         // frees of a non-null pointer: operator delete, and free where counted
        std::size_t largest = 0;       // the largest single request, in bytes
        long long liveBytes = 0;       // bytes allocated minus bytes freed (net, can go below 0)
        long long peakLiveBytes = 0;   // the highest liveBytes reached
    };

    /** Counting is on for the calling thread while one of these is alive (they nest). */
    class Scope
    {
    public:
        Scope() noexcept;
        ~Scope() noexcept;
        Scope (const Scope&) = delete;
        Scope& operator= (const Scope&) = delete;

    private:
        bool wasActive;
    };

    /** Sets this thread's counts to zero. */
    void reset() noexcept;

    /** This thread's counts since its last reset (only what happened inside a Scope). */
    Counts counts() noexcept;

    /** Whether this build counts malloc and free too (glibc interposition or the Debug CRT hook). */
    bool countsMalloc() noexcept;

    /** What this build counts, in words, for the test output. */
    const char* coverage() noexcept;

    /** Installs the Debug CRT hook (a no-op elsewhere). TestMain calls it first thing. */
    void install() noexcept;
}
