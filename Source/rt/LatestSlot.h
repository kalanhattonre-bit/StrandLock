#pragma once

// rt::LatestSlot<T> (PLUGIN-PLAN 1.5): a triple buffer that always hands the reader the newest value the writer
// finished. One writer thread (the audio thread writes the display frame) and one reader thread (the editor's
// 30 Hz timer). Both sides are wait-free: one atomic exchange each, no lock, no allocation, no copy beyond what
// the caller does. The writer never waits and never loses the newest value; the reader never sees a value
// half-written. Values written while nobody reads are simply overwritten.
//
// Three buffers: the writer owns `back`, the reader owns `front`, and `middle` holds the last finished value
// with a "fresh" bit. endWrite swaps back and middle (setting the bit); takeLatest swaps middle and front when
// the bit is set (clearing it). (Not GrainLock's 8-frame ScopeFifo: that drops the new frame when full, so after
// a pause the UI would show the frame from when the pause began.)
//
//   writer:  T& f = slot.beginWrite(); fill f completely; slot.endWrite();     (or slot.write (value))
//   reader:  if (slot.takeLatest()) draw (slot.latest());

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace sl::rt
{
    template <class T>
    class LatestSlot
    {
        static_assert (std::is_trivially_copyable_v<T>, "LatestSlot holds plain data");
        static_assert (std::atomic<std::uint8_t>::is_always_lock_free, "the slot index must be lock-free");

    public:
        LatestSlot() noexcept = default;
        LatestSlot (const LatestSlot&) = delete;
        LatestSlot& operator= (const LatestSlot&) = delete;

        //==========================================================================================
        // Writer (one thread).

        /** The buffer to fill. It holds an older value (not necessarily the last one written): overwrite every
            field before endWrite. */
        T& beginWrite() noexcept { return buffers[back]; }

        /** Publishes what beginWrite's buffer holds as the newest value. */
        void endWrite() noexcept
        {
            const auto was = middle.exchange (static_cast<std::uint8_t> (back | kFresh), std::memory_order_acq_rel);
            back = static_cast<std::uint8_t> (was & kIndex);
        }

        /** beginWrite () = value, then endWrite (). */
        void write (const T& value) noexcept
        {
            beginWrite() = value;
            endWrite();
        }

        //==========================================================================================
        // Reader (one thread).

        /** True when a value newer than latest () was finished since the last call: latest () is now that
            value. False (latest () unchanged) otherwise. */
        bool takeLatest() noexcept
        {
            if ((middle.load (std::memory_order_relaxed) & kFresh) == 0)
                return false;
            const auto was = middle.exchange (front, std::memory_order_acq_rel);
            front = static_cast<std::uint8_t> (was & kIndex);
            return true;
        }

        /** The value takeLatest last brought in (a value-initialised T before the first). */
        const T& latest() const noexcept { return buffers[front]; }

    private:
        static constexpr std::uint8_t kIndex = 3;
        static constexpr std::uint8_t kFresh = 4;

        std::array<T, 3> buffers {};
        std::atomic<std::uint8_t> middle { 1 };   // index | kFresh
        std::uint8_t back = 2;                    // writer only
        std::uint8_t front = 0;                   // reader only
    };
}
