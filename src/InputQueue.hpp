#pragma once
#include <Geode/Geode.hpp>
#include <chrono>
#include "LockFreeQueue.hpp"

using namespace geode::prelude;

// ============================================================================
// INPUT EVENT — fits entirely in a single cache line slot (< 64 B)
// ============================================================================
struct InputEvent {
    PlayerButton btn;       // which button was pressed/released
    bool         isPress;   // true = pushButton, false = releaseButton
    double       timestamp; // steady_clock time point in nanoseconds
};

// ============================================================================
// GLOBAL SPSC QUEUE — 256 slots, power-of-two, zero allocation
//
//  Producer: GJBaseGameLayer::handleButton  (game/input thread)
//  Consumer: any future frame-interpolation system or analytics
// ============================================================================
inline LockFreeSPSCQueue<InputEvent, 256> g_inputQueue;
