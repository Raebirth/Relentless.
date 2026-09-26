#pragma once
#include <atomic>

// ============================================================================
// GlobalState.hpp  —  lock-free flags shared across translation units.
//
// `inline` (C++17) gives each flag a single definition regardless of
// how many .cpp files include this header.
// ============================================================================
namespace GlobalState {

    // Set to true by GameLoopHook::PlayLayer::init.
    // Set to false by GameLoopHook::PlayLayer::onQuit.
    //
    // DispatcherHook reads this to decide whether to suppress the original
    // CCEGLView touch path:
    //   • false (menus / loading) → call original, normal UI works
    //   • true  (in level)       → push to g_inputQueue, suppress original
    //                              so GameLoopHook is the sole dispatcher
    //
    // Trade-off: while in-level, the in-game pause button (top-left corner
    // touch target) is also suppressed.  The hardware back button / volume
    // keys still work for pausing.  This is acceptable for competitive play
    // where the pause UI is never triggered during an attempt.
    inline std::atomic<bool> g_inLevel{false};

}  // namespace GlobalState
