#pragma once
#include <array>
#include <atomic>
#include <cstddef>

template<typename T, size_t Capacity>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    LockFreeSPSCQueue() : m_head(0), m_tail(0) {}

    bool try_push(const T& item) noexcept {
        const size_t currentTail = m_tail.load(std::memory_order_relaxed);
        const size_t currentHead = m_head.load(std::memory_order_acquire);

        if ((currentTail - currentHead) >= Capacity) {
            return false;
        }

        m_buffer[currentTail & BufferMask] = item;
        m_tail.store(currentTail + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& outItem) noexcept {
        const size_t currentHead = m_head.load(std::memory_order_relaxed);
        const size_t currentTail = m_tail.load(std::memory_order_acquire);

        if (currentHead == currentTail) {
            return false;
        }

        outItem = m_buffer[currentHead & BufferMask];
        m_head.store(currentHead + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t BufferMask = Capacity - 1;

    alignas(64) std::array<T, Capacity> m_buffer;
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};
