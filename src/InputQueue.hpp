#pragma once

#include "LockFreeQueue.hpp"
#include <Geode/Geode.hpp>  // PlayerButton

// ============================================================================
// InputEvent  —  one touch frame crossing the SPSC bridge.
//
// Kept 16 bytes (one ARMv9 vector lane) to fit 16 events in a single
// 256-byte cache line fetch from m_buffer on the consumer side.
// ============================================================================
struct InputEvent {
    PlayerButton button;       // Which button was pressed
    bool         isPress;      // true = began/press, false = ended/cancelled/release
    bool         isPlayer2;    // false = left half / p1, true = right half / p2
    uint8_t      _pad[2];      // align timestampNs to 8-byte boundary
    double       timestampNs;  // CLOCK_MONOTONIC nanoseconds at JNI entry
};
static_assert(sizeof(InputEvent) == 16, "InputEvent must be 16 bytes.");
static_assert(std::is_trivially_copyable_v<InputEvent>,
              "InputEvent must be trivially copyable for LockFreeQueue.");

// ============================================================================
// g_inputQueue  —  the SPSC bridge between the producer and consumer.
//
// Capacity 256: at 240 Hz with up to 4 simultaneous touches, the queue
// can absorb ~64 frames of buffered input before dropping.  In practice
// the consumer drains it every frame so occupancy stays near 0–2.
//
// `inline` (C++17): ODR-safe single definition across all translation
// units that include this header.  The linker collapses them to one symbol.
// ============================================================================
inline LockFreeQueue<InputEvent, 256> g_inputQueue;
