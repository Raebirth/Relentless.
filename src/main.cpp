#include <Geode/Geode.hpp>
#include <charconv>
#include <cstring>

// GameLoopHook.cpp owns all PlayLayer hooks.
// DispatcherHook.cpp owns all CCEGLView hooks (Android).
// ThreadOptimizer.cpp owns GL-thread scheduling.
// This file handles only: settings cache + mod lifecycle.

#ifdef GEODE_IS_ANDROID
// ThreadOptimizer is compiled as a separate TU; declare its API here.
namespace ThreadOptimizer { void applyToCurrentThread() noexcept; }
#endif

#ifdef GEODE_IS_WINDOWS
#  include <windows.h>
#  include <timeapi.h>
#endif

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Setting cache — accessed from GameLoopHook.cpp's hot path.
// `inline` (C++17) → single definition across all TUs.
// ---------------------------------------------------------------------------
namespace PerformanceCache {
    inline bool s_showStats = false;
    inline bool s_showFps   = false;
}

// ---------------------------------------------------------------------------
// Mod lifecycle
// ---------------------------------------------------------------------------
$on_mod(Loaded) {
    PerformanceCache::s_showStats = Mod::get()->getSettingValue<bool>("show-stats");
    PerformanceCache::s_showFps   = Mod::get()->getSettingValue<bool>("show-fps");

    listenForSettingChanges<bool>("show-stats", +[](bool v) {
        PerformanceCache::s_showStats = v;
    });
    listenForSettingChanges<bool>("show-fps", +[](bool v) {
        PerformanceCache::s_showFps = v;
    });

#ifdef GEODE_IS_WINDOWS
    // Drop OS scheduler quantum from 15.6 ms → 1 ms.
    timeBeginPeriod(1);
    log::info("[Relentless] Windows timer resolution set to 1 ms.");
#endif

#ifdef GEODE_IS_ANDROID
    // Apply CPU affinity + SCHED_FIFO to the GL thread.
    ThreadOptimizer::applyToCurrentThread();
#endif
}
