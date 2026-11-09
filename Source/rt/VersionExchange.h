#pragma once

// rt::VersionExchange<T> (PLUGIN-PLAN 1.3): hands immutable versions (the compiled Snapshot, the SampleData)
// from the message side to the audio thread, and frees each one on the message side once nothing on the
// audio thread reads it any more.
//
// A voice keeps raw pointers into the version it plays (its note's tables, a stolen voice's sample) through
// its release, so acquire () cannot decide when a version may go. The audio thread says it instead: after
// Engine::process it calls setInUse (engine.minEpochInUse (kind)), the lowest epoch that the engine's current
// version and every voice that is not done still read. collect () frees every pooled version below that.
// Epochs only grow and a voice never moves to an older version, so a freed version is never read again.
//
//   message side (any thread but the audio thread; a mutex the audio thread never touches)
//     publish (std::unique_ptr<T>)  stamps epoch = ++publishCount (and T::epoch when T has one), adds it to the
//                                   pool, and puts it in the one-slot mailbox. A version still waiting there was
//                                   never taken by the audio thread, so it is freed at once: while the host does
//                                   not call processBlock, edits keep replacing the one waiting version.
//     collect ()                    frees every pooled version whose epoch < inUse (the 10 Hz message timer).
//     setPrepared (bool)            true at the end of prepareToPlay, false at the start of releaseResources.
//                                   While not prepared the audio thread is idle, so publish swaps directly: the
//                                   new version becomes current and every older one is freed at once.
//   audio side (one thread, never blocks, never allocates, never frees)
//     acquire ()                    one exchange on the mailbox; true when current () changed (and once after
//                                   setPrepared (true), so the first block after a prepare hands it over again).
//     current (), currentEpoch ()   the version to give the engine (nullptr / 0 before anything was published).
//     setInUse (epoch)              the lowest epoch still read; clamped to currentEpoch (), so kNoEpoch
//                                   ("the engine reads nothing") never lets collect free current or a newer one.
//
// Debug poison: with poisoning on (Debug builds, AddressSanitizer builds, STRANDLOCK_RT_POISON, or the
// constructor's flag), a freed version is destroyed, its bytes are filled with 0xDD and the block is kept in a
// quarantine of the last 64 frees (under AddressSanitizer also marked unreadable) before it really goes back
// to the heap. A read of a freed version then sees 0xDD instead of whatever reused the memory: a live
// Snapshot starts with its version (1), a live SampleData with its epoch (>= 1), so looksPoisoned () on the
// first 8 bytes tells them apart. The poison covers the object itself; memory it owned (a vector's buffer,
// tables shared with other snapshots) is freed normally, and the sanitizer job catches reads of that.
//
// Not std::atomic<std::shared_ptr> (it locks on MSVC), and the audio thread never drops the last reference
// to anything (PLUGIN-PLAN 1.3, risks 7.2).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#if defined (__SANITIZE_ADDRESS__)
 #define SL_RT_ASAN 1
#elif defined (__has_feature)
 #if __has_feature (address_sanitizer)
  #define SL_RT_ASAN 1
 #endif
#endif
#ifndef SL_RT_ASAN
 #define SL_RT_ASAN 0
#endif

#if SL_RT_ASAN && __has_include (<sanitizer/asan_interface.h>)
 #include <sanitizer/asan_interface.h>
 #define SL_RT_POISON_REGION(p, n) __asan_poison_memory_region ((p), (n))
 #define SL_RT_UNPOISON_REGION(p, n) __asan_unpoison_memory_region ((p), (n))
#else
 #define SL_RT_POISON_REGION(p, n) ((void) (p), (void) (n))
 #define SL_RT_UNPOISON_REGION(p, n) ((void) (p), (void) (n))
#endif

namespace sl::rt
{
    /** "Reads nothing of this kind": what Engine::minEpochInUse returns then. setInUse clamps it to the current
        epoch, so it never frees the current version or one still waiting. */
    inline constexpr std::uint64_t kNoEpoch = std::numeric_limits<std::uint64_t>::max();

    /** The byte a poisoned (freed) version is filled with. */
    inline constexpr unsigned char kPoisonByte = 0xdd;

    /** Whether a VersionExchange poisons what it frees unless told otherwise. */
   #if ! defined (NDEBUG) || SL_RT_ASAN || defined (STRANDLOCK_RT_POISON)
    inline constexpr bool kPoisonByDefault = true;
   #else
    inline constexpr bool kPoisonByDefault = false;
   #endif

    /** True when the first 8 bytes at p are all kPoisonByte: what a version freed with poisoning on looks like
        while it is in the quarantine. A live Snapshot or SampleData never starts that way. */
    inline bool looksPoisoned (const void* p) noexcept
    {
        unsigned char head[8];
        std::memcpy (head, p, sizeof (head));
        for (const unsigned char b : head)
            if (b != kPoisonByte)
                return false;
        return true;
    }

    template <class T>
    class VersionExchange
    {
        static_assert (! std::is_array_v<T>, "a version is one object");
        static_assert (sizeof (T) >= 8, "looksPoisoned reads the first 8 bytes of a version");

    public:
        /** How many freed blocks the poison quarantine keeps before they really go back to the heap. */
        static constexpr std::size_t kQuarantine = 64;

        explicit VersionExchange (bool poisonFreed = kPoisonByDefault)
            : poison (poisonFreed && ! kClassAllocates)
        {
            if (poison)
                quarantine.reserve (kQuarantine);
        }

        /** Frees everything. Nothing may use the exchange any more (the audio thread is stopped). */
        ~VersionExchange()
        {
            mailbox.store (nullptr, std::memory_order_relaxed);
            pool.clear();
            for (T* block : quarantine)
                releaseBlock (block);
            quarantine.clear();
        }

        VersionExchange (const VersionExchange&) = delete;
        VersionExchange& operator= (const VersionExchange&) = delete;

        //==========================================================================================
        // Message side: any thread but the audio thread (the compiler thread publishes, the message thread
        // collects); these take a mutex the audio thread never touches.

        /** Hands a new version to the audio thread and returns its epoch (1, 2, 3, ...; 0 for nullptr, which is
            ignored). Allocates (the pool entry), so never call it on the audio thread. */
        std::uint64_t publish (std::unique_ptr<T> version)
        {
            if (version == nullptr)
                return 0;

            const std::lock_guard<std::mutex> hold (lock);
            const std::uint64_t epoch = ++published;
            if constexpr (requires (T& t) { t.epoch = std::uint64_t {}; })
                version->epoch = epoch;

            auto entry = std::make_unique<Entry>();
            entry->value = std::move (version);
            entry->epoch = epoch;
            Entry* fresh = entry.get();
            pool.push_back (std::move (entry));

            if (! prepared.load (std::memory_order_relaxed))
            {
                // Not prepared: the audio thread is idle, so this becomes current now and nothing older is
                // read again. The mailbox stays empty in this mode.
                mailbox.store (nullptr, std::memory_order_relaxed);
                currentPtr.store (fresh->value.get(), std::memory_order_relaxed);
                currentEpochValue.store (epoch, std::memory_order_relaxed);
                inUseValue.store (epoch, std::memory_order_release);
                freeBelowLocked (epoch);
                return epoch;
            }

            if (Entry* old = mailbox.exchange (fresh, std::memory_order_acq_rel))
            {
                // Still waiting: the audio thread never took it, so nothing reads it.
                ++waitingFreed;
                removeLocked (old);
            }
            return epoch;
        }

        /** Frees every pooled version whose epoch is below the audio thread's last setInUse. Returns how many. */
        std::size_t collect()
        {
            const std::lock_guard<std::mutex> hold (lock);
            return freeBelowLocked (inUseValue.load (std::memory_order_acquire));
        }

        /** true: the audio thread may run from now on (call it at the end of prepareToPlay, after
            Engine::prepare). The next acquire () returns true, so the engine gets current () again.
            false: the audio thread is idle from now on (call it at the start of releaseResources, after
            Engine::forgetVersions, so the engine points at nothing): the newest version becomes current, every
            other one is freed, and publish swaps directly until setPrepared (true). */
        void setPrepared (bool nowPrepared)
        {
            const std::lock_guard<std::mutex> hold (lock);
            if (nowPrepared)
            {
                prepared.store (true, std::memory_order_release);
                resend.store (true, std::memory_order_release);
                return;
            }

            prepared.store (false, std::memory_order_release);
            if (Entry* waiting = mailbox.exchange (nullptr, std::memory_order_acq_rel))
            {
                currentPtr.store (waiting->value.get(), std::memory_order_relaxed);
                currentEpochValue.store (waiting->epoch, std::memory_order_relaxed);
            }
            const std::uint64_t e = currentEpochValue.load (std::memory_order_relaxed);
            inUseValue.store (e, std::memory_order_release);
            freeBelowLocked (e);
        }

        bool isPrepared() const noexcept { return prepared.load (std::memory_order_acquire); }

        /** Versions not freed yet (the current one, any still read, one waiting). */
        std::size_t poolSize() const
        {
            const std::lock_guard<std::mutex> hold (lock);
            return pool.size();
        }

        /** The epoch of the newest version published (0 before the first). */
        std::uint64_t latestEpoch() const
        {
            const std::lock_guard<std::mutex> hold (lock);
            return published;
        }

        /** Versions freed so far, and how many of those were freed while still waiting in the mailbox. */
        std::uint64_t freedCount() const
        {
            const std::lock_guard<std::mutex> hold (lock);
            return freed;
        }

        std::uint64_t waitingFreedCount() const
        {
            const std::lock_guard<std::mutex> hold (lock);
            return waitingFreed;
        }

        /** Whether freed versions are poisoned and quarantined (the constructor's flag, unless T has its own
            operator new: then they are deleted normally). */
        bool poisoning() const noexcept { return poison; }

        /** For the tests: p was freed, is still in the quarantine, and still shows the poison. Reads the
            quarantined block safely (also under AddressSanitizer). */
        bool holdsFreed (const T* p) const
        {
            const std::lock_guard<std::mutex> hold (lock);
            for (T* block : quarantine)
            {
                if (block != p)
                    continue;
                SL_RT_UNPOISON_REGION (block, sizeof (T));
                const bool poisoned = looksPoisoned (block);
                SL_RT_POISON_REGION (block, sizeof (T));
                return poisoned;
            }
            return false;
        }

        //==========================================================================================
        // Audio side: one thread. Lock-free, wait-free, no allocation, no free.

        /** Once per block, before Engine::setSnapshot / setSample: takes the waiting version, if any.
            True when current () changed, and once after setPrepared (true); false while nothing was ever
            published. */
        bool acquire() noexcept
        {
            bool changed = resend.exchange (false, std::memory_order_acq_rel);
            if (Entry* p = mailbox.exchange (nullptr, std::memory_order_acq_rel))
            {
                currentPtr.store (p->value.get(), std::memory_order_relaxed);
                currentEpochValue.store (p->epoch, std::memory_order_relaxed);
                changed = true;
            }
            return changed && currentPtr.load (std::memory_order_relaxed) != nullptr;
        }

        /** The version the audio thread plays now (nullptr before anything was published). */
        const T* current() const noexcept { return currentPtr.load (std::memory_order_relaxed); }

        /** Its epoch (0 before anything was published). */
        std::uint64_t currentEpoch() const noexcept { return currentEpochValue.load (std::memory_order_relaxed); }

        /** After Engine::process: the lowest epoch the engine still reads (Engine::minEpochInUse). Clamped to
            currentEpoch (). A value below the previous one is a bookkeeping bug in the caller (epochs only grow);
            it is stored (that frees less, never more) and counted in inUseRegressions (). */
        void setInUse (std::uint64_t epoch) noexcept
        {
            const std::uint64_t cur = currentEpochValue.load (std::memory_order_relaxed);
            const std::uint64_t e = epoch < cur ? epoch : cur;
            if (e < inUseValue.load (std::memory_order_relaxed))
                regressions.fetch_add (1, std::memory_order_relaxed);
            inUseValue.store (e, std::memory_order_release);
        }

        /** The last setInUse value (after the clamp). */
        std::uint64_t inUse() const noexcept { return inUseValue.load (std::memory_order_acquire); }

        /** How many setInUse calls went below the value before them (0 when the caller's bookkeeping is right). */
        std::uint64_t inUseRegressions() const noexcept { return regressions.load (std::memory_order_relaxed); }

    private:
        struct Entry
        {
            std::unique_ptr<T> value;
            std::uint64_t epoch = 0;
        };

        // A type with its own operator new is deleted normally (the quarantine frees with the global one).
        static constexpr bool kClassAllocates = requires (std::size_t n) { T::operator new (n); };

        void removeLocked (const Entry* entry)
        {
            for (auto it = pool.begin(); it != pool.end(); ++it)
            {
                if (it->get() != entry)
                    continue;
                freeValue (std::move ((*it)->value));
                pool.erase (it);
                return;
            }
        }

        std::size_t freeBelowLocked (std::uint64_t limit)
        {
            std::size_t count = 0;
            for (std::size_t i = 0; i < pool.size();)
            {
                if (pool[i]->epoch < limit)
                {
                    freeValue (std::move (pool[i]->value));
                    pool.erase (pool.begin() + static_cast<std::ptrdiff_t> (i));
                    ++count;
                }
                else
                {
                    ++i;
                }
            }
            return count;
        }

        void freeValue (std::unique_ptr<T> value)
        {
            if (value == nullptr)
                return;
            ++freed;
            if (! poison)
                return;   // value deletes it here

            T* raw = value.release();
            raw->~T();
            std::memset (static_cast<void*> (raw), kPoisonByte, sizeof (T));
            SL_RT_POISON_REGION (raw, sizeof (T));
            if (quarantine.size() >= kQuarantine)
            {
                releaseBlock (quarantine.front());
                quarantine.erase (quarantine.begin());
            }
            quarantine.push_back (raw);
        }

        // The storage `new T` took, given back the way `delete` would after the destructor (already run).
        static void releaseBlock (T* block) noexcept
        {
            SL_RT_UNPOISON_REGION (block, sizeof (T));
            if constexpr (alignof (T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                ::operator delete (static_cast<void*> (block), std::align_val_t { alignof (T) });
            else
                ::operator delete (static_cast<void*> (block));
        }

        // Message side, under lock.
        mutable std::mutex lock;
        std::vector<std::unique_ptr<Entry>> pool;   // every version not freed yet, oldest first
        std::vector<T*> quarantine;                 // poisoned blocks, oldest first (poisoning only)
        std::uint64_t published = 0, freed = 0, waitingFreed = 0;
        const bool poison;

        // Shared.
        std::atomic<Entry*> mailbox { nullptr };
        std::atomic<std::uint64_t> inUseValue { 0 };
        std::atomic<bool> prepared { false };
        std::atomic<bool> resend { false };
        std::atomic<std::uint64_t> regressions { 0 };

        // Audio side (written by the message side only while not prepared, when the audio thread is idle).
        std::atomic<T*> currentPtr { nullptr };
        std::atomic<std::uint64_t> currentEpochValue { 0 };
    };
}
