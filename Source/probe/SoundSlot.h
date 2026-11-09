#pragma once

// Hands the loaded starter sound to the audio thread (M0's probe; M2 replaces it with rt::VersionExchange,
// PLUGIN-PLAN 1.3). The message thread publishes a new sound; the audio thread takes the newest at the start
// of a block with one atomic exchange and says which one it plays (an epoch). A sound is freed on the message
// thread only once the audio thread has moved past it, so the audio thread never frees, locks or waits.

#include "sounds/StarterSounds.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace sl::probe
{
    class SoundSlot
    {
    public:
        SoundSlot() = default;
        SoundSlot (const SoundSlot&) = delete;
        SoundSlot& operator= (const SoundSlot&) = delete;

        /** Message thread: the sound to play from the next block on. */
        void publish (std::shared_ptr<const sounds::StarterSound> sound)
        {
            const std::lock_guard<std::mutex> hold (lock);
            auto item = std::make_unique<Item>();
            item->sound = std::move (sound);
            item->epoch = ++published;
            Item* fresh = item.get();
            pool.push_back (std::move (item));

            // A sound still waiting in the mailbox was never taken by the audio thread: it can go now.
            if (Item* old = mailbox.exchange (fresh, std::memory_order_acq_rel))
                removeLocked (old);
            collectLocked();
        }

        /** Message thread: frees what the audio thread has moved past. */
        void collect()
        {
            const std::lock_guard<std::mutex> hold (lock);
            collectLocked();
        }

        /** Message thread: the newest published sound (what the waveform strip shows), or nullptr. */
        std::shared_ptr<const sounds::StarterSound> latest() const
        {
            const std::lock_guard<std::mutex> hold (lock);
            return pool.empty() ? nullptr : pool.back()->sound;
        }

        /** Audio thread, once per block: the sound to play (nullptr before anything was published). */
        const sounds::StarterSound* acquire() noexcept
        {
            if (Item* p = mailbox.exchange (nullptr, std::memory_order_acq_rel))
            {
                current = p;
                inUse.store (p->epoch, std::memory_order_release);
            }
            return current != nullptr ? current->sound.get() : nullptr;
        }

        /** How many sounds are held (the tests check that old ones are freed). */
        std::size_t heldCount() const
        {
            const std::lock_guard<std::mutex> hold (lock);
            return pool.size();
        }

    private:
        struct Item
        {
            std::shared_ptr<const sounds::StarterSound> sound;
            std::uint64_t epoch = 0;
        };

        void removeLocked (const Item* item)
        {
            for (auto it = pool.begin(); it != pool.end(); ++it)
                if (it->get() == item)
                {
                    pool.erase (it);
                    return;
                }
        }

        void collectLocked()
        {
            // Epochs only grow and the audio thread never goes back to an older one, so everything below the
            // epoch it plays is never read again. The newest item always stays (it is waiting or playing).
            const std::uint64_t playing = inUse.load (std::memory_order_acquire);
            for (std::size_t i = 0; i + 1 < pool.size();)
            {
                if (pool[i]->epoch < playing)
                    pool.erase (pool.begin() + static_cast<std::ptrdiff_t> (i));
                else
                    ++i;
            }
        }

        mutable std::mutex lock;
        std::vector<std::unique_ptr<Item>> pool;   // message side: every sound not yet freed, oldest first
        std::uint64_t published = 0;
        std::atomic<Item*> mailbox { nullptr };
        std::atomic<std::uint64_t> inUse { 0 };
        Item* current = nullptr;                   // audio thread only
    };
}
