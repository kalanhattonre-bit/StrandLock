#pragma once

// rt::stableInsertionSort (PLUGIN-PLAN 1.2, 1.6 rule 7): JavaScript's Array.prototype.sort is stable, and the
// engine's order decisions (scanStarts' starts, plan()'s candidates and wait list, voice orders) depend on that.
// std::stable_sort may allocate a buffer, so the audio thread uses this instead: a stable insertion sort in
// place, on the fixed arrays the engine keeps (64 voices, 256 starts, 512 (head, voice) pairs). It never
// allocates; its cost is O(n^2) moves at worst, which is fine at these sizes (512 elements in reverse order:
// about 131,000 moves).
//
// Stability: an element moves left only past elements it is strictly less than, so equal elements keep their
// order. With a comparator that is a strict weak order the result is the same as std::stable_sort's and as the
// prototype's sort with the same comparator.

#include <iterator>
#include <type_traits>
#include <utility>

namespace sl::rt
{
    /** Sorts [first, last) so that less (b, a) is false for every a before b; equal elements keep their order. */
    template <class It, class Less>
    void stableInsertionSort (It first, It last, Less less)
    {
        using Value = typename std::iterator_traits<It>::value_type;
        if (first == last)
            return;
        for (It i = std::next (first); i != last; ++i)
        {
            if (! less (*i, *std::prev (i)))
                continue;
            Value key = std::move (*i);
            It j = i;
            do
            {
                *j = std::move (*std::prev (j));
                --j;
            }
            while (j != first && less (key, *std::prev (j)));
            *j = std::move (key);
        }
    }

    /** The same with a JavaScript-style comparator, so a port can keep the prototype's expression as written:
        cmp (a, b) < 0 puts a first (for example (a, b) => a.kS - b.kS || a.X.s - b.X.s). */
    template <class It, class Cmp>
    void stableInsertionSortCmp (It first, It last, Cmp cmp)
    {
        stableInsertionSort (first, last, [&cmp] (const auto& a, const auto& b) { return cmp (a, b) < 0; });
    }
}
