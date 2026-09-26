#pragma once
#include <atomic>
#include <cstdint>

// ============================================================================
// FrameSmoother — Lock-free fixed-point EMA FPS counter
//
//  • Integer-only arithmetic — no FP division per frame
//  • Smoothing factor α = 1/8  →  smoothed = (7*prev + raw) >> 3
//  • Thread-safe: update() and getAverageFPS() may be called concurrently
//    (both use relaxed atomics — reads are approximate but stale-safe)
//  • DOES NOT modify dt passed to physics — purely observational
// ============================================================================
namespace FrameSmoother {

    // Smoothed frame time in microseconds. Starts at 16 667 µs (~60 FPS).
    inline std::atomic<uint32_t> s_smoothedUs{16'667u};

    // Call once per rendered frame with the raw delta-time in seconds.
    // Clamps extreme values (spikes, stalls) before feeding the EMA.
    inline void update(float dt) noexcept {
        // Convert seconds → microseconds; clamp to [100 µs, 1 s]
        auto rawUs = static_cast<uint32_t>(dt * 1'000'000.0f);
        if (rawUs <         100u) rawUs =       100u; // > 10 000 FPS cap
        if (rawUs > 1'000'000u)   rawUs = 1'000'000u; // < 1 FPS cap

        // EMA: α = 1/8 implemented with integer bit-shift (no division)
        const uint32_t prev = s_smoothedUs.load(std::memory_order_relaxed);
        const uint32_t next = ((prev * 7u) + rawUs) >> 3u;
        s_smoothedUs.store(next, std::memory_order_relaxed);
    }

    // Returns the smoothed FPS as an integer. Safe to call every frame.
    [[nodiscard]] inline uint32_t getAverageFPS() noexcept {
        const uint32_t us = s_smoothedUs.load(std::memory_order_relaxed);
        return (us > 0u) ? (1'000'000u / us) : 9'999u;
    }

} // namespace FrameSmoother
