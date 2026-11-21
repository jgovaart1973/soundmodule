#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace km
{
/**
    Bounded, wait-free single-producer / single-consumer queue.
    Capacity must be a power of two. Never allocates after construction, so it is
    safe to use from the audio thread on either side.
*/
template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push (const T& item) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);

        if (w - r >= Capacity)
            return false; // full: drop rather than block

        items[w & (Capacity - 1)] = item;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);

        if (r == w)
            return std::nullopt;

        T item = items[r & (Capacity - 1)];
        readIndex.store (r + 1, std::memory_order_release);
        return item;
    }

    size_t size() const noexcept
    {
        return writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_acquire);
    }

    bool empty() const noexcept { return size() == 0; }

private:
    std::array<T, Capacity> items {};
    alignas (64) std::atomic<size_t> writeIndex { 0 };
    alignas (64) std::atomic<size_t> readIndex { 0 };
};

} // namespace km
