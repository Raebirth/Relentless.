#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// ============================================================================
// LockFreeQueue<T, N>  —  Single-Producer / Single-Consumer Ring Buffer
//
// Micro-architectural design rationale  (Cortex-A720 / ARMv9.2-A)
// ──────────────────────────────────────────────────────────────────
//
// [1] MESI False-Sharing Elimination  (alignas(64))
//     m_tail is exclusively written by the Producer.
//     m_head is exclusively written by the Consumer.
//     If they share a 64-byte cache line, every STLR from the producer
//     invalidates the consumer's L1D line for m_head (and vice-versa),
//     triggering a MOESI RFO (Request-For-Ownership).  On Cortex-A720,
//     L1D→L2 RFO round-trip is ~10-20 cycles (L2 hit) or ~40-60 cycles
//     (L3/LLC hit).  At 240 Hz the queue is touched ~3.7 M times/second,
//     so false sharing wastes ~37–220 M cycles/sec on the critical path.
//     alignas(64) pins each pointer to its own exclusive cache line.
//
// [2] Bitwise Wraparound  (& kMask, NOT % N)
//     Modulo on ARM64: the compiler emits UDIV + MUL + SUB (3 μops,
//     ≥9-cycle latency through the divide unit).
//     Bitwise AND: single AND instruction, 1 cycle, zero branch.
//     Requires N to be a power-of-two — enforced by static_assert.
//
// [3] Monotonic uint64_t Indices
//     Head/tail never wrap themselves.  Buffer slot = index & kMask.
//     Full check: (tail - head) >= N  (correct under uint64 overflow,
//     since both wrap at the same modulus, and overflow of uint64
//     takes ~580 years at 1 GHz throughput).
//     This eliminates the "next-pointer" pre-calculation that can
//     create a false dependency chain in the out-of-order window.
//
// [4] LSE Atomics  (ARMv8.1+, mandatory on Cortex-A720)
//     std::memory_order_acquire   → compiler emits LDAR
//     std::memory_order_release   → compiler emits STLR
//     No LDAXR/STLXR retry loop: single instruction, bounded latency,
//     no store-exclusive failure under high bus contention.
//     With Clang and -mcpu=cortex-a720, these lower to the correct
//     LDAR/STLR opcodes automatically.  No explicit __builtin needed.
//
// [5] Trivially-Copyable Constraint
//     T must be memcpy-able.  Non-trivial types would call destructors/
//     constructors inside the lock-free path — unsafe without a lock.
// ============================================================================

template <typename T, std::size_t N>
class LockFreeQueue {
    static_assert((N & (N - 1)) == 0,
                  "LockFreeQueue: capacity N must be a power of two.");
    static_assert(N >= 2,
                  "LockFreeQueue: minimum capacity is 2.");
    static_assert(std::is_trivially_copyable_v<T>,
                  "LockFreeQueue: T must be trivially copyable.");

    static constexpr std::size_t kMask = N - 1;

    // ----------------------------------------------------------------
    // Cache-line isolation map:
    //
    //   Line 0 (bytes   0-63): m_tail   — Producer writes, Consumer reads
    //   Line 1 (bytes  64-127): m_head   — Consumer writes, Producer reads
    //   Lines 2+ (bytes 128+): m_buffer — payload, one slot per element
    //
    // The 7 padding bytes after each atomic ensure no struct packing
    // can fold them into the same line on any ABI.
    // ----------------------------------------------------------------
    alignas(64) std::atomic<uint64_t> m_tail{0};   // Producer's write pointer
    alignas(64) std::atomic<uint64_t> m_head{0};   // Consumer's write pointer
    alignas(64) T                     m_buffer[N];  // Ring buffer payload

public:
    // ----------------------------------------------------------------
    // try_push — PRODUCER THREAD ONLY.
    //
    // Returns true  → item enqueued.
    // Returns false → queue full; caller must decide to drop or retry.
    //
    // Memory ordering:
    //   load(relaxed) on m_tail  — we own this pointer; no ordering needed
    //   load(acquire) on m_head  — syncs with consumer's store(release);
    //                             maps to LDAR on ARM64.
    //   store(release) on m_tail — publishes the new slot to the consumer;
    //                             maps to STLR on ARM64, ensuring the
    //                             buffer write is visible before the tail
    //                             update.
    // ----------------------------------------------------------------
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const uint64_t tail = m_tail.load(std::memory_order_relaxed);

        // Full check using unsigned subtraction — correct under wrap.
        if (tail - m_head.load(std::memory_order_acquire) >= static_cast<uint64_t>(N))
            [[unlikely]] return false;

        m_buffer[static_cast<std::size_t>(tail & kMask)] = item;

        // Release: guarantees buffer write completes before tail is visible.
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    // ----------------------------------------------------------------
    // try_pop — CONSUMER THREAD ONLY.
    //
    // Returns true  → item written to `out`.
    // Returns false → queue empty.
    //
    // Memory ordering mirrors try_push symmetrically.
    // ----------------------------------------------------------------
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const uint64_t head = m_head.load(std::memory_order_relaxed);

        // Empty check: acquire syncs with producer's store(release) to tail.
        if (head == m_tail.load(std::memory_order_acquire))
            [[likely]] return false;   // [[likely]] because queues are usually sparse

        out = m_buffer[static_cast<std::size_t>(head & kMask)];

        // Release: makes the freed slot visible to the producer.
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    // Approximate occupancy snapshot — safe from either thread.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        const uint64_t tail = m_tail.load(std::memory_order_acquire);
        const uint64_t head = m_head.load(std::memory_order_acquire);
        return static_cast<std::size_t>(tail - head);
    }

    [[nodiscard]] bool         empty()    const noexcept { return size_approx() == 0; }
    static constexpr std::size_t capacity() noexcept     { return N; }
};
