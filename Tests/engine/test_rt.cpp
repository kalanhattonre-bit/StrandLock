// The real-time hand-off pieces (PLUGIN-PLAN 1.3, 1.5, 1.6 rule 7): rt::VersionExchange, rt::LatestSlot and
// rt::stableInsertionSort. Single-thread semantics first, then two-thread stresses with the allocation counter
// on the "audio" side. Every check has a negative control beside it (a deliberately wrong use that the same
// check must catch), so none of them can pass by doing nothing. Group "engine" (the sanitizer job runs it);
// the exchange stress runs again in group "alloc", so the Debug-CRT job counts every heap call on its reader.

#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include "rt/LatestSlot.h"
#include "rt/StableSort.h"
#include "rt/VersionExchange.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using Clock = std::chrono::steady_clock;

    //==============================================================================================
    // A version the tests can check word by word. The first 8 bytes are a magic number, so a poisoned (freed)
    // version is told apart from a live one; publish stamps `epoch`.

    constexpr std::uint64_t kAlive = 0x5354524e444c4f4bull;   // "STRNDLOK"

    struct TestVersion
    {
        std::uint64_t magic = kAlive;
        std::uint64_t epoch = 0;
        std::uint64_t serial = 0;
        std::array<std::uint64_t, 29> payload {};
    };

    std::uint64_t payloadWord (std::uint64_t serial, std::size_t i) noexcept
    {
        return serial * 0x9e3779b97f4a7c15ull + static_cast<std::uint64_t> (i);
    }

    std::unique_ptr<TestVersion> makeVersion (std::uint64_t serial)
    {
        auto v = std::make_unique<TestVersion>();
        v->serial = serial;
        for (std::size_t i = 0; i < v->payload.size(); ++i)
            v->payload[i] = payloadWord (serial, i);
        return v;
    }

    // Every word is what makeVersion wrote.
    bool intact (const TestVersion* v) noexcept
    {
        if (v->magic != kAlive)
            return false;
        for (std::size_t i = 0; i < v->payload.size(); ++i)
            if (v->payload[i] != payloadWord (v->serial, i))
                return false;
        return true;
    }

    using Exchange = rt::VersionExchange<TestVersion>;

    //==============================================================================================
    // The two-thread exchange stress: a publisher at about 1,000 versions a second, the message side collecting
    // at about 100 Hz, and an "audio" reader that runs 100-microsecond blocks: acquire, keep the version it was
    // playing in a releasing "voice" for 1..64 blocks, read every version it holds over and over, set inUse.

    struct ReaderResult
    {
        long long blocks = 0, changes = 0, reads = 0, poisonSeen = 0, damaged = 0;
        bool epochWentBack = false;
        alloc::Counts counts;
    };

    void readerLoop (Exchange& ex, const std::atomic<bool>& stop, const std::atomic<bool>& drain, ReaderResult& out)
    {
        struct Held
        {
            const TestVersion* p = nullptr;
            std::uint64_t epoch = 0;
            int blocksLeft = 0;
        };
        std::array<Held, 16> held {};
        std::uint32_t rng = 12345u;
        std::uint64_t lastEpoch = 0;

        auto readOne = [&out] (const TestVersion* p) noexcept
        {
            ++out.reads;
            if (rt::looksPoisoned (p))
                ++out.poisonSeen;
            else if (! intact (p))
                ++out.damaged;
        };

        alloc::reset();
        {
            const alloc::Scope counting;
            while (! stop.load (std::memory_order_acquire))
            {
                const auto blockEnd = Clock::now() + std::chrono::microseconds (100);
                const TestVersion* was = ex.current();
                const std::uint64_t wasEpoch = ex.currentEpoch();
                if (ex.acquire())
                {
                    ++out.changes;
                    if (ex.currentEpoch() < lastEpoch)
                        out.epochWentBack = true;
                    lastEpoch = ex.currentEpoch();
                    if (was != nullptr && was != ex.current() && ! drain.load (std::memory_order_relaxed))
                    {
                        // The voice that played it releases for a while, still reading it.
                        rng = rng * 1664525u + 1013904223u;
                        for (Held& h : held)
                            if (h.p == nullptr)
                            {
                                h = { was, wasEpoch, 1 + static_cast<int> ((rng >> 16) % 64u) };
                                break;
                            }
                    }
                }

                // The whole block reads what it holds, again and again (a use after free has many chances).
                do
                {
                    if (const TestVersion* cur = ex.current())
                        readOne (cur);
                    for (const Held& h : held)
                        if (h.p != nullptr)
                            readOne (h.p);
                }
                while (Clock::now() < blockEnd);

                std::uint64_t lowest = ex.current() != nullptr ? ex.currentEpoch() : rt::kNoEpoch;
                for (Held& h : held)
                {
                    if (h.p == nullptr)
                        continue;
                    if (--h.blocksLeft <= 0)
                        h = {};   // done after this block: no longer read
                    else
                        lowest = std::min (lowest, h.epoch);
                }
                ex.setInUse (lowest);
                ++out.blocks;
            }
        }
        out.counts = alloc::counts();
    }

    void exchangeStress (const char* title, double seconds)
    {
        section (title);

        Exchange ex (true);
        ex.setPrepared (true);
        check (ex.poisoning(), "freed versions are poisoned in this test");

        std::atomic<bool> stopPublisher { false }, stopReader { false }, drain { false };
        std::atomic<long long> published { 0 };
        ReaderResult result;

        std::thread reader ([&] { readerLoop (ex, stopReader, drain, result); });
        std::thread publisher ([&]
        {
            auto next = Clock::now();
            std::uint64_t serial = 0;
            while (! stopPublisher.load (std::memory_order_acquire))
            {
                ex.publish (makeVersion (++serial));
                published.fetch_add (1, std::memory_order_relaxed);
                next += std::chrono::milliseconds (1);
                while (Clock::now() < next && ! stopPublisher.load (std::memory_order_relaxed))
                    std::this_thread::yield();
            }
        });

        const auto start = Clock::now();
        std::size_t largestPool = 0, collected = 0;
        while (Clock::now() - start < std::chrono::duration<double> (seconds))
        {
            collected += ex.collect();
            largestPool = std::max (largestPool, ex.poolSize());
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        const double ran = std::chrono::duration<double> (Clock::now() - start).count();

        stopPublisher.store (true, std::memory_order_release);
        publisher.join();

        // The quiet period: no publishes, the releasing voices finish, the reader holds only the newest.
        drain.store (true, std::memory_order_relaxed);
        const auto quietStart = Clock::now();
        while (ex.inUse() != ex.latestEpoch() && Clock::now() - quietStart < std::chrono::seconds (5))
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        collected += ex.collect();
        const std::size_t poolAfterQuiet = ex.poolSize();

        stopReader.store (true, std::memory_order_release);
        reader.join();

        const long long pub = published.load();
        note (fmt ("%.2f s: %lld versions published (%.0f a second), %lld reader blocks, %lld changes, %lld version reads",
                   ran, pub, static_cast<double> (pub) / ran, result.blocks, result.changes, result.reads));
        note (fmt ("freed %llu (%llu still waiting when replaced, %zu by collect); the pool held at most %zu",
                   static_cast<unsigned long long> (ex.freedCount()), static_cast<unsigned long long> (ex.waitingFreedCount()), collected,
                   largestPool));

        check (ran >= seconds, fmt ("the stress ran %.2f s (at least %.0f)", ran, seconds));
        check (pub >= static_cast<long long> (300 * seconds), fmt ("the publisher made %lld versions (at least 300 a second)", pub));
        check (result.blocks >= static_cast<long long> (1000 * seconds) && result.changes >= pub / 4,
               fmt ("the reader ran %lld blocks and took %lld new versions", result.blocks, result.changes));
        check (result.poisonSeen == 0, fmt ("the reader never saw a freed (poisoned) version: %lld of %lld reads", result.poisonSeen, result.reads));
        check (result.damaged == 0, fmt ("every version read was intact (%lld damaged)", result.damaged));
        check (result.counts.news == 0 && result.counts.deletes == 0,
               fmt ("the reader made 0 allocations and 0 frees (%lld new, %lld delete; counting %s)", result.counts.news,
                    result.counts.deletes, alloc::coverage()));
        check (! result.epochWentBack && ex.inUseRegressions() == 0, "epochs only grew on the reader, and inUse never went back");
        check (ex.freedCount() + ex.poolSize() == static_cast<std::uint64_t> (pub),
               fmt ("every version published was freed or is still pooled (%llu + %zu of %lld)",
                    static_cast<unsigned long long> (ex.freedCount()), ex.poolSize(), pub));
        check (largestPool > 1, fmt ("releasing voices kept older versions alive while new ones came (the pool reached %zu)", largestPool));
        check (poolAfterQuiet == 1, fmt ("after a quiet period collect leaves 1 version (%zu)", poolAfterQuiet));
    }

    //==============================================================================================
    // LatestSlot: a frame about the size of the display frame, every word derived from its sequence number.

    struct SeqFrame
    {
        std::uint64_t seq;
        std::uint64_t words[1062];
    };

    std::uint64_t frameWord (std::uint64_t seq, std::size_t i) noexcept
    {
        return (seq << 12) ^ static_cast<std::uint64_t> (i);
    }

    void fillFrame (SeqFrame& f, std::uint64_t seq) noexcept
    {
        f.seq = seq;
        for (std::size_t i = 0; i < std::size (f.words); ++i)
            f.words[i] = frameWord (seq, i);
    }

    // The frame is one write, whole: every word belongs to its sequence number.
    bool wholeFrame (const SeqFrame& f) noexcept
    {
        for (std::size_t i = 0; i < std::size (f.words); ++i)
            if (f.words[i] != frameWord (f.seq, i))
                return false;
        return true;
    }

    //==============================================================================================
    // Sorting.

    struct Item
    {
        int key;
        int order;     // the position before sorting: stability shows here
        double sub;    // a second key with ties of its own
    };

    bool sameItems (const std::vector<Item>& a, const std::vector<Item>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].key != b[i].key || a[i].order != b[i].order)
                return false;
        return true;
    }

    // A plausible bug: an insertion sort that also moves past equal elements (<= instead of <). Not stable.
    template <class Less>
    void unstableInsertionSort (std::vector<Item>& v, Less less)
    {
        for (std::size_t i = 1; i < v.size(); ++i)
        {
            const Item key = v[i];
            std::size_t j = i;
            while (j > 0 && ! less (v[j - 1], key))
            {
                v[j] = v[j - 1];
                --j;
            }
            v[j] = key;
        }
    }
}

//==================================================================================================
SL_TEST ("engine", versionExchangeSemantics)
{
    section ("VersionExchange, one thread: waiting versions, epochs, collect, direct swaps, the poison");

    {
        Exchange ex (true);
        ex.setPrepared (true);
        check (! ex.acquire() && ex.current() == nullptr && ex.currentEpoch() == 0, "nothing published: acquire is false, current is null");

        auto a = makeVersion (1);
        const TestVersion* pa = a.get();
        const std::uint64_t ea = ex.publish (std::move (a));
        auto b = makeVersion (2);
        const TestVersion* pb = b.get();
        const std::uint64_t eb = ex.publish (std::move (b));
        check (ea == 1 && eb == 2, fmt ("publish stamps epochs 1, 2 (%llu, %llu)", static_cast<unsigned long long> (ea),
                                        static_cast<unsigned long long> (eb)));
        check (ex.waitingFreedCount() == 1 && ex.freedCount() == 1 && ex.poolSize() == 1,
               "a version the audio thread never took is freed at once when the next one comes");
        check (ex.holdsFreed (pa), "and it is poisoned in the quarantine");
        check (ex.acquire() && ex.current() == pb && ex.currentEpoch() == 2 && pb->epoch == 2,
               "acquire takes the newest; publish stamped its epoch member");
        check (! ex.acquire() && ex.current() == pb, "a second acquire with nothing new is false and keeps current");
        check (ex.publish (nullptr) == 0 && ex.latestEpoch() == 2, "publishing nullptr is ignored");

        // Epochs only grow, whether taken or replaced.
        std::uint64_t last = 2;
        bool grew = true;
        for (int i = 0; i < 40; ++i)
        {
            const std::uint64_t e = ex.publish (makeVersion (100 + static_cast<std::uint64_t> (i)));
            if (e != last + 1)
                grew = false;
            last = e;
            if (i % 3 == 0)
            {
                ex.acquire();
                if (ex.currentEpoch() != e || ex.current()->epoch != e)
                    grew = false;
            }
        }
        check (grew, "40 more publishes: every epoch is the previous + 1, and acquire hands over the newest");
    }

    {
        Exchange ex (true);
        ex.setPrepared (true);
        const TestVersion* p[4] = {};
        for (int i = 1; i <= 3; ++i)
        {
            auto v = makeVersion (static_cast<std::uint64_t> (i));
            p[i] = v.get();
            ex.publish (std::move (v));
            ex.acquire();
        }
        check (ex.poolSize() == 3 && ex.currentEpoch() == 3, "three versions taken one after another stay pooled until inUse moves");

        ex.setInUse (1);
        check (ex.collect() == 0 && ex.poolSize() == 3, "inUse 1 (a voice still reads version 1): collect frees nothing");
        ex.setInUse (2);
        check (ex.collect() == 1 && ex.holdsFreed (p[1]) && intact (p[2]) && intact (p[3]),
               "inUse 2: collect frees version 1 only; 2 and 3 are intact");
        ex.setInUse (rt::kNoEpoch);
        check (ex.inUse() == 3, "kNoEpoch (the engine reads nothing) is clamped to the current epoch");
        check (ex.collect() == 1 && ex.poolSize() == 1 && ex.current() == p[3] && intact (p[3]), "collect then frees 2 and keeps current");

        auto w = makeVersion (4);
        const TestVersion* pw = w.get();
        ex.publish (std::move (w));
        ex.setInUse (rt::kNoEpoch);
        check (ex.collect() == 0 && ex.poolSize() == 2 && intact (pw), "a waiting version is never collected, whatever inUse says");
        check (ex.acquire() && ex.current() == pw, "and the next acquire takes it");

        ex.setInUse (2);
        check (ex.inUseRegressions() == 1, "a setInUse below the previous one is counted as a bookkeeping bug");
    }

    {
        // After a quiet period with nothing releasing, collect leaves exactly one version.
        Exchange ex (true);
        ex.setPrepared (true);
        for (int i = 0; i < 50; ++i)
        {
            ex.publish (makeVersion (static_cast<std::uint64_t> (i)));
            if (i % 2 == 0)
            {
                ex.acquire();
                ex.setInUse (ex.currentEpoch());
            }
            if (i % 5 == 0)
                ex.collect();
        }
        ex.acquire();
        ex.setInUse (ex.currentEpoch());
        ex.collect();
        check (ex.poolSize() == 1 && ex.freedCount() == 49 && intact (ex.current()),
               fmt ("50 publishes, then quiet: 1 version left, 49 freed (%zu, %llu)", ex.poolSize(),
                    static_cast<unsigned long long> (ex.freedCount())));
    }

    {
        // Not prepared (before prepareToPlay, after releaseResources): the message side swaps directly.
        Exchange ex (true);
        check (! ex.isPrepared(), "a new exchange is not prepared");
        auto first = makeVersion (1);
        const TestVersion* pFirst = first.get();
        ex.publish (std::move (first));
        ex.publish (makeVersion (2));
        auto third = makeVersion (3);
        const TestVersion* pThird = third.get();
        ex.publish (std::move (third));
        check (ex.poolSize() == 1 && ex.current() == pThird && ex.currentEpoch() == 3 && ex.inUse() == 3 && ex.holdsFreed (pFirst),
               "three publishes while not prepared: the newest is current at once, the others are freed");
        check (! ex.acquire(), "acquire has nothing new while not prepared");
        ex.setPrepared (true);
        check (ex.acquire() && ex.current() == pThird, "after setPrepared (true) acquire is true once, so the engine gets current again");
        check (! ex.acquire(), "and then false");

        auto waiting = makeVersion (4);
        const TestVersion* pWaiting = waiting.get();
        ex.publish (std::move (waiting));
        ex.setPrepared (false);
        check (ex.poolSize() == 1 && ex.current() == pWaiting && ex.holdsFreed (pThird) && ex.inUse() == 4,
               "setPrepared (false) settles on the waiting version and frees the rest");
    }

    {
        // The negative control for the stress test's poison check: a reader that ignores a releasing voice.
        Exchange ex (true);
        ex.setPrepared (true);
        ex.publish (makeVersion (1));
        ex.acquire();
        const TestVersion* releasing = ex.current();   // a voice keeps reading version 1 through its release
        ex.publish (makeVersion (2));
        ex.acquire();
        ex.setInUse (2);                               // the bug: inUse forgets the releasing voice
        ex.collect();
        check (ex.holdsFreed (releasing), "a version freed under a releasing voice is poisoned: the stress reader's check would see it");
       #if ! SL_RT_ASAN
        check (rt::looksPoisoned (releasing) && ! intact (releasing),
               "read directly, the freed version shows the poison (what the stress reader counts)");
       #else
        note ("under AddressSanitizer a direct read of the freed version would stop the run (that is the sanitizer's check)");
       #endif
    }

    {
        // Poisoning off: freed versions really go back to the heap at once.
        Exchange ex (false);
        ex.setPrepared (true);
        auto a = makeVersion (1);
        const TestVersion* pa = a.get();
        ex.publish (std::move (a));
        ex.publish (makeVersion (2));
        check (! ex.poisoning() && ex.freedCount() == 1 && ! ex.holdsFreed (pa), "with poisoning off nothing is quarantined");

        // A type without an epoch member works too (nothing is stamped).
        rt::VersionExchange<std::array<std::uint64_t, 2>> plain (true);
        plain.setPrepared (true);
        const std::uint64_t e = plain.publish (std::make_unique<std::array<std::uint64_t, 2>> (std::array<std::uint64_t, 2> { 7, 8 }));
        check (e == 1 && plain.acquire() && (*plain.current())[0] == 7 && plain.currentEpoch() == 1, "a version without an epoch member");
    }
}

SL_TEST ("engine", versionExchangeTwoThreads)
{
    exchangeStress ("VersionExchange, two threads for 3 s: publisher at 1,000 a second, a reader with releasing voices", 3.0);
}

SL_TEST ("alloc", versionExchangeReaderAllocatesNothing)
{
    exchangeStress ("VersionExchange reader, counted with this build's allocation hooks (3 s)", 3.0);
}

//==================================================================================================
SL_TEST ("engine", latestSlotSemantics)
{
    section ("LatestSlot, one thread: the newest finished value, never an older one");

    auto slot = std::make_unique<rt::LatestSlot<SeqFrame>>();
    check (! slot->takeLatest() && slot->latest().seq == 0, "nothing written: takeLatest is false, latest is a zero frame");

    fillFrame (slot->beginWrite(), 1);
    slot->endWrite();
    check (slot->takeLatest() && slot->latest().seq == 1 && wholeFrame (slot->latest()), "one write: takeLatest brings it in");
    check (! slot->takeLatest() && slot->latest().seq == 1, "a second takeLatest has nothing new and keeps it");

    for (std::uint64_t s = 2; s <= 5; ++s)
    {
        fillFrame (slot->beginWrite(), s);
        slot->endWrite();
    }
    check (slot->takeLatest() && slot->latest().seq == 5, "four writes without a read: the reader gets the newest (5), never an old one");

    auto f = std::make_unique<SeqFrame>();
    fillFrame (*f, 9);
    slot->write (*f);
    check (slot->takeLatest() && slot->latest().seq == 9 && wholeFrame (slot->latest()), "write () copies and publishes");

    // The negative controls for the stress test's checks.
    fillFrame (*f, 10);
    f->words[531] = frameWord (11, 531);
    check (! wholeFrame (*f), "a frame mixing two writes (torn) is caught by the check");
}

SL_TEST ("engine", latestSlotTornReadStress)
{
    section ("LatestSlot, two threads for 1 s: no torn frame, never an older one, the newest after the last write");

    auto slot = std::make_unique<rt::LatestSlot<SeqFrame>>();
    std::atomic<bool> stop { false };
    std::atomic<std::uint64_t> lastWritten { 0 };
    alloc::Counts writerCounts, readerCounts;
    long long reads = 0, torn = 0, older = 0;

    std::thread writer ([&]
    {
        alloc::reset();
        {
            const alloc::Scope counting;
            std::uint64_t seq = 0;
            while (! stop.load (std::memory_order_acquire))
            {
                fillFrame (slot->beginWrite(), ++seq);
                slot->endWrite();
                lastWritten.store (seq, std::memory_order_release);
            }
        }
        writerCounts = alloc::counts();
    });

    std::thread reader ([&]
    {
        alloc::reset();
        {
            const alloc::Scope counting;
            std::uint64_t lastSeen = 0;
            const auto end = Clock::now() + std::chrono::seconds (1);
            while (Clock::now() < end)
            {
                if (! slot->takeLatest())
                    continue;
                const SeqFrame& got = slot->latest();
                ++reads;
                if (! wholeFrame (got))
                    ++torn;
                if (got.seq <= lastSeen)
                    ++older;
                lastSeen = got.seq;
            }
        }
        readerCounts = alloc::counts();
    });

    reader.join();
    stop.store (true, std::memory_order_release);
    writer.join();

    // After the last write the reader gets exactly it.
    slot->takeLatest();
    const std::uint64_t newest = lastWritten.load();
    note (fmt ("%llu frames written, %lld taken by the reader", static_cast<unsigned long long> (newest), reads));
    check (reads >= 100 && newest > static_cast<std::uint64_t> (reads), fmt ("the threads raced (%lld reads, %llu writes)", reads,
                                                                             static_cast<unsigned long long> (newest)));
    check (torn == 0, fmt ("no frame read was torn (%lld of %lld)", torn, reads));
    check (older == 0, fmt ("every frame taken was newer than the one before (%lld were not)", older));
    check (slot->latest().seq == newest && wholeFrame (slot->latest()), "after the writer stops, the reader holds its newest frame");
    check (writerCounts.news == 0 && writerCounts.deletes == 0 && readerCounts.news == 0 && readerCounts.deletes == 0,
           fmt ("0 allocations and 0 frees on either side (writer %lld/%lld, reader %lld/%lld)", writerCounts.news, writerCounts.deletes,
                readerCounts.news, readerCounts.deletes));
}

//==================================================================================================
SL_TEST ("engine", stableSortMatchesStd)
{
    section ("stableInsertionSort equals std::stable_sort (many ties), allocates nothing, and the check catches an unstable sort");

    std::uint32_t rng = 7u;
    auto nextRand = [&rng] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };

    const std::size_t fixedSizes[] = { 0, 1, 2, 3, 5, 17, 64, 256, 512, 600 };
    int arrays = 0, equalByKey = 0, equalByCmp = 0, equalBySub = 0, caughtUnstable = 0;
    long long sortAllocs = 0, sortFrees = 0;

    for (int round = 0; round < 210; ++round)
    {
        const std::size_t n = round < 10 ? fixedSizes[round] : 1 + nextRand() % 600u;
        std::vector<Item> in (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            // Mostly few distinct keys (ties everywhere); some rounds already sorted or reversed.
            int key = static_cast<int> (nextRand() % (round % 3 == 0 ? 4u : 12u));
            if (round % 7 == 1)
                key = static_cast<int> (i / 5);
            if (round % 7 == 2)
                key = static_cast<int> ((n - i) / 5);
            in[i] = { key, static_cast<int> (i), static_cast<double> (nextRand() % 3u) * 0.5 };
        }
        ++arrays;

        auto lessKey = [] (const Item& a, const Item& b) { return a.key < b.key; };
        auto lessKeySub = [] (const Item& a, const Item& b) { return a.key < b.key || (a.key == b.key && a.sub < b.sub); };

        std::vector<Item> want = in;
        std::stable_sort (want.begin(), want.end(), lessKey);
        std::vector<Item> wantSub = in;
        std::stable_sort (wantSub.begin(), wantSub.end(), lessKeySub);

        std::vector<Item> got = in, gotCmp = in, gotSub = in;
        alloc::reset();
        {
            const alloc::Scope counting;
            rt::stableInsertionSort (got.begin(), got.end(), lessKey);
            rt::stableInsertionSortCmp (gotCmp.begin(), gotCmp.end(), [] (const Item& a, const Item& b) { return a.key - b.key; });
            rt::stableInsertionSort (gotSub.data(), gotSub.data() + gotSub.size(), lessKeySub);
        }
        const auto c = alloc::counts();
        sortAllocs += c.news;
        sortFrees += c.deletes;

        if (sameItems (got, want))
            ++equalByKey;
        if (sameItems (gotCmp, want))
            ++equalByCmp;
        if (sameItems (gotSub, wantSub))
            ++equalBySub;

        std::vector<Item> bad = in;
        unstableInsertionSort (bad, lessKey);
        if (! sameItems (bad, want))
            ++caughtUnstable;
    }

    check (equalByKey == arrays, fmt ("by key: %d of %d arrays equal std::stable_sort element for element", equalByKey, arrays));
    check (equalByCmp == arrays, fmt ("with a JS-style comparator (a.key - b.key): %d of %d", equalByCmp, arrays));
    check (equalBySub == arrays, fmt ("by key then a second key with its own ties, on raw pointers: %d of %d", equalBySub, arrays));
    check (sortAllocs == 0 && sortFrees == 0, fmt ("0 allocations and 0 frees while sorting (%lld, %lld)", sortAllocs, sortFrees));
    check (caughtUnstable >= arrays / 2, fmt ("the same comparison catches an unstable insertion sort on %d of %d arrays", caughtUnstable, arrays));
}
