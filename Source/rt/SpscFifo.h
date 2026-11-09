#pragma once

// A fixed-capacity ring for one producer thread and one consumer thread (PLUGIN-PLAN 1.3: the UI's
// commands to the audio thread). Lock-free and wait-free: push and pop never block, never allocate,
// and fail instead of waiting when the ring is full or empty.

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace sl::rt
{
    template <class T, std::size_t Capacity>
    class SpscFifo
    {
        static_assert (std::is_trivially_copyable_v<T>, "SpscFifo holds trivially copyable items");
        static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    public:
        static constexpr std::size_t capacity() noexcept { return Capacity; }

        /** Producer only. False (and nothing written) when the ring is full. */
        bool push (const T& item) noexcept
        {
            const std::size_t w = writeCount.load (std::memory_order_relaxed);
            const std::size_t r = readCount.load (std::memory_order_acquire);
            if (w - r >= Capacity)
                return false;
            items[w & (Capacity - 1)] = item;
            writeCount.store (w + 1, std::memory_order_release);
            return true;
        }

        /** Consumer only. False (and item untouched) when the ring is empty. */
        bool pop (T& item) noexcept
        {
            const std::size_t r = readCount.load (std::memory_order_relaxed);
            const std::size_t w = writeCount.load (std::memory_order_acquire);
            if (w == r)
                return false;
            item = items[r & (Capacity - 1)];
            readCount.store (r + 1, std::memory_order_release);
            return true;
        }

        /** Items waiting. Exact on either side when the other side is idle, a snapshot otherwise. */
        std::size_t size() const noexcept
        {
            return writeCount.load (std::memory_order_acquire) - readCount.load (std::memory_order_acquire);
        }

        bool empty() const noexcept { return size() == 0; }

    private:
        // Both counters only grow; the unsigned difference stays right across their wrap.
        std::atomic<std::size_t> writeCount { 0 };
        std::atomic<std::size_t> readCount { 0 };
        std::array<T, Capacity> items {};
    };
}
