#pragma once

// =============================================================================
// SPSCQueue — Lock-free Single-Producer Single-Consumer queue.
//
// Used for RT-safe communication between audio thread and UI thread.
// Audio thread rules: NO allocation, NO mutex, NO syscalls.
// Parameters are sent as pre-computed structs via this queue.
//
// Implementation: classic ring buffer with atomic head/tail.
// Memory ordering: acquire/release for correct visibility across threads.
// =============================================================================

#include <atomic>
#include <array>
#include <cstddef>

namespace transfo {

template <typename T, int Capacity>
class SPSCQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SPSCQueue capacity must be a power of 2");

public:
    SPSCQueue() = default;

    // Producer (UI thread) — push an element. Returns false if full.
    bool push(const T& item)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;

        if (next == tail_.load(std::memory_order_acquire))
            return false; // Full

        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer (audio thread) — pop an element. Returns false if empty.
    bool pop(T& item)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire))
            return false; // Empty

        item = buffer_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Check if empty (approximate — only valid from consumer side)
    bool empty() const
    {
        return head_.load(std::memory_order_acquire)
            == tail_.load(std::memory_order_acquire);
    }

    // Approximate count (not exact in concurrent context)
    int size() const
    {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return static_cast<int>((h - t) & kMask);
    }

    void reset()
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr size_t kMask = Capacity - 1;

    // Separate cache lines to avoid false sharing.
    // The alignas(64) deliberately introduces structure padding;
    // silence MSVC C4324 which flags this as a warning.
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4324)
#endif
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::array<T, Capacity> buffer_{};
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
};

} // namespace transfo
