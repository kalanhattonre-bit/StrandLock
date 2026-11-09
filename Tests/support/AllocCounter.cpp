// The allocation counter (see AllocCounter.h). This file replaces the global operator new and delete
// for the whole test runner, and on glibc also malloc and free, so it must stay the only one that does.

#include "AllocCounter.h"

#include <cstdint>
#include <cstdlib>
#include <new>

#if defined (__linux__) && defined (__GLIBC__) && ! defined (STRANDLOCK_SANITIZE)
 #define SL_ALLOC_GLIBC 1
 #include <cerrno>
 #include <malloc.h>
extern "C"
{
    void* __libc_malloc (std::size_t);
    void* __libc_calloc (std::size_t, std::size_t);
    void* __libc_realloc (void*, std::size_t);
    void* __libc_memalign (std::size_t, std::size_t);
    void  __libc_free (void*);
}
#else
 #define SL_ALLOC_GLIBC 0
#endif

#if defined (_MSC_VER) && defined (_DEBUG)
 #define SL_ALLOC_CRT_HOOK 1
 #include <crtdbg.h>
#else
 #define SL_ALLOC_CRT_HOOK 0
#endif

namespace
{
    using sl::test::alloc::Counts;

    struct ThreadState
    {
        bool active = false;   // inside a Scope
        int inside = 0;        // > 0 while this file's operator new or delete is calling the C allocator
        Counts counts;
    };

    thread_local ThreadState tls;

    void noteAlloc (std::size_t requested, std::size_t bytes) noexcept
    {
        ThreadState& s = tls;
        if (! s.active)
            return;
        Counts& c = s.counts;
        ++c.news;
        if (requested > c.largest)
            c.largest = requested;
        c.liveBytes += static_cast<long long> (bytes);
        if (c.liveBytes > c.peakLiveBytes)
            c.peakLiveBytes = c.liveBytes;
    }

    void noteFree (std::size_t bytes) noexcept
    {
        ThreadState& s = tls;
        if (! s.active)
            return;
        ++s.counts.deletes;
        s.counts.liveBytes -= static_cast<long long> (bytes);
    }

   #if SL_ALLOC_GLIBC || SL_ALLOC_CRT_HOOK
    void noteRealloc (std::size_t requested, std::size_t before, std::size_t after) noexcept
    {
        ThreadState& s = tls;
        if (! s.active)
            return;
        Counts& c = s.counts;
        ++c.news;
        if (requested > c.largest)
            c.largest = requested;
        c.liveBytes += static_cast<long long> (after) - static_cast<long long> (before);
        if (c.liveBytes > c.peakLiveBytes)
            c.peakLiveBytes = c.liveBytes;
    }
   #endif

    //==============================================================================================
    // operator new and delete keep each block's size in a header just before it.

    constexpr std::size_t kHeaderBytes = 16;
    constexpr std::size_t kMinAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__ < 16 ? 16 : __STDCPP_DEFAULT_NEW_ALIGNMENT__;

    struct Header
    {
        std::size_t size;     // the size the caller asked for
        std::size_t offset;   // from the start of the C allocator's block to the caller's pointer
    };

    static_assert (sizeof (Header) <= kHeaderBytes, "the header must fit in front of the block");

    void* rawAlloc (std::size_t size) noexcept
    {
       #if SL_ALLOC_GLIBC
        return __libc_malloc (size);   // not through the interposed malloc, so it is not counted twice
       #else
        ++tls.inside;                  // the Debug CRT hook ignores this call
        void* p = std::malloc (size);
        --tls.inside;
        return p;
       #endif
    }

    void rawFree (void* p) noexcept
    {
       #if SL_ALLOC_GLIBC
        __libc_free (p);
       #else
        ++tls.inside;
        std::free (p);
        --tls.inside;
       #endif
    }

    void* allocate (std::size_t size, std::size_t alignment) noexcept
    {
        if (alignment < kMinAlignment)
            alignment = kMinAlignment;

        const std::size_t extra = kHeaderBytes + alignment;
        if (size > SIZE_MAX - extra)
            return nullptr;

        void* raw = rawAlloc (size + extra);
        if (raw == nullptr)
            return nullptr;

        const auto base = reinterpret_cast<std::uintptr_t> (raw);
        const auto mask = static_cast<std::uintptr_t> (alignment) - 1;
        const std::uintptr_t user = (base + kHeaderBytes + mask) & ~mask;

        auto* header = reinterpret_cast<Header*> (user - kHeaderBytes);
        header->size = size;
        header->offset = static_cast<std::size_t> (user - base);

        noteAlloc (size, size);
        return reinterpret_cast<void*> (user);
    }

    void release (void* p) noexcept
    {
        if (p == nullptr)
            return;

        const auto user = reinterpret_cast<std::uintptr_t> (p);
        const auto* header = reinterpret_cast<const Header*> (user - kHeaderBytes);
        const std::size_t size = header->size;
        void* raw = reinterpret_cast<void*> (user - header->offset);

        noteFree (size);
        rawFree (raw);
    }

    void* allocateOrThrow (std::size_t size, std::size_t alignment)
    {
        if (void* p = allocate (size, alignment))
            return p;
        throw std::bad_alloc();
    }

   #if SL_ALLOC_CRT_HOOK
    // The debug CRT reports every heap allocation, including malloc calls that bypass operator new.
    int crtAllocHook (int allocType, void* userData, std::size_t size, int blockType, long, const unsigned char*, int)
    {
        const ThreadState& s = tls;
        if (! s.active || s.inside > 0)
            return 1;   // let the CRT go ahead

        switch (allocType)
        {
            case _HOOK_ALLOC:
                noteAlloc (size, size);
                break;
            case _HOOK_REALLOC:
                noteRealloc (size, userData != nullptr ? _msize_dbg (userData, blockType) : 0, size);
                break;
            case _HOOK_FREE:
                if (userData != nullptr)
                    noteFree (_msize_dbg (userData, blockType));
                break;
            default:
                break;
        }
        return 1;   // let the CRT go ahead
    }
   #endif
}

//==================================================================================================
void* operator new (std::size_t size)                                              { return allocateOrThrow (size, 0); }
void* operator new[] (std::size_t size)                                            { return allocateOrThrow (size, 0); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept              { return allocate (size, 0); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept            { return allocate (size, 0); }
void* operator new (std::size_t size, std::align_val_t a)                          { return allocateOrThrow (size, static_cast<std::size_t> (a)); }
void* operator new[] (std::size_t size, std::align_val_t a)                        { return allocateOrThrow (size, static_cast<std::size_t> (a)); }
void* operator new (std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept   { return allocate (size, static_cast<std::size_t> (a)); }
void* operator new[] (std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate (size, static_cast<std::size_t> (a)); }

void operator delete (void* p) noexcept                                            { release (p); }
void operator delete[] (void* p) noexcept                                          { release (p); }
void operator delete (void* p, std::size_t) noexcept                               { release (p); }
void operator delete[] (void* p, std::size_t) noexcept                             { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept                     { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept                   { release (p); }
void operator delete (void* p, std::align_val_t) noexcept                          { release (p); }
void operator delete[] (void* p, std::align_val_t) noexcept                        { release (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept             { release (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept           { release (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept   { release (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { release (p); }

//==================================================================================================
#if SL_ALLOC_GLIBC
// glibc lets the executable interpose the C allocator, which also catches JUCE's HeapBlock
// (it calls malloc directly, never operator new). Sizes come from malloc_usable_size.
extern "C"
{
    void* malloc (std::size_t size) noexcept
    {
        void* p = __libc_malloc (size);
        noteAlloc (size, p != nullptr ? malloc_usable_size (p) : 0);
        return p;
    }

    void* calloc (std::size_t n, std::size_t size) noexcept
    {
        void* p = __libc_calloc (n, size);
        noteAlloc (p != nullptr ? n * size : 0, p != nullptr ? malloc_usable_size (p) : 0);
        return p;
    }

    void* realloc (void* old, std::size_t size) noexcept
    {
        const std::size_t before = old != nullptr ? malloc_usable_size (old) : 0;
        void* p = __libc_realloc (old, size);
        if (p != nullptr)
            noteRealloc (size, before, malloc_usable_size (p));
        else if (old != nullptr && size == 0)
            noteFree (before);   // realloc (p, 0) freed it
        return p;
    }

    void free (void* p) noexcept
    {
        if (p != nullptr)
            noteFree (malloc_usable_size (p));
        __libc_free (p);
    }

    void* memalign (std::size_t align, std::size_t size) noexcept
    {
        void* p = __libc_memalign (align, size);
        noteAlloc (size, p != nullptr ? malloc_usable_size (p) : 0);
        return p;
    }

    void* aligned_alloc (std::size_t align, std::size_t size) noexcept
    {
        return memalign (align, size);
    }

    int posix_memalign (void** out, std::size_t align, std::size_t size) noexcept
    {
        void* p = memalign (align, size);
        if (p == nullptr)
            return ENOMEM;
        *out = p;
        return 0;
    }
}
#endif

//==================================================================================================
namespace sl::test::alloc
{
    Scope::Scope() noexcept : wasActive (tls.active)
    {
        tls.active = true;
    }

    Scope::~Scope() noexcept
    {
        tls.active = wasActive;
    }

    void reset() noexcept
    {
        tls.counts = Counts {};
    }

    Counts counts() noexcept
    {
        return tls.counts;
    }

    bool countsMalloc() noexcept
    {
        return SL_ALLOC_GLIBC != 0 || SL_ALLOC_CRT_HOOK != 0;
    }

    const char* coverage() noexcept
    {
       #if SL_ALLOC_GLIBC
        return "operator new/delete (size header) + malloc/calloc/realloc/memalign/free (glibc interposition)";
       #elif SL_ALLOC_CRT_HOOK
        return "operator new/delete (size header) + every CRT heap allocation and free (debug CRT hook)";
       #elif defined (STRANDLOCK_SANITIZE)
        return "operator new/delete only (size header; malloc is left to the sanitizer)";
       #else
        return "operator new/delete only (size header; the Debug CRT run counts malloc too)";
       #endif
    }

    void install() noexcept
    {
       #if SL_ALLOC_CRT_HOOK
        _CrtSetAllocHook (crtAllocHook);
       #endif
    }
}
