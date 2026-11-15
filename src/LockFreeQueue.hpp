#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

template <typename T, std::size_t Capacity>
class LockFreeQueue {
    static_assert((Capacity >= 2) && ((Capacity & (Capacity - 1)) == 0),
                  "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable");

    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> m_tail{0};
    alignas(64) std::atomic<std::size_t> m_head{0};
    alignas(64) T m_buffer[Capacity]{};

public:
    constexpr LockFreeQueue() noexcept = default;

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    [[nodiscard]] __attribute__((always_inline))
    inline bool try_push(const T& item) noexcept {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t head = m_head.load(std::memory_order_acquire);

        if (tail - head >= Capacity) [[unlikely]] {
            return false;
        }

        m_buffer[tail & kMask] = item;
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] __attribute__((always_inline))
    inline bool try_pop(T& out) noexcept {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);

        if (head == tail) [[likely]] {
            return false;
        }

        out = m_buffer[head & kMask];
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] __attribute__((always_inline))
    inline std::size_t size_approx() const noexcept {
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        const std::size_t head = m_head.load(std::memory_order_acquire);
        return (tail >= head) ? (tail - head) : 0;
    }
};
