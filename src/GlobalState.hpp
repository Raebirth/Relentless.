#pragma once

#include <atomic>

namespace GlobalState {
    alignas(64) inline std::atomic<bool> g_inLevel{false};
}

namespace PerformanceCache {
    inline bool s_showStats = false;
    inline bool s_showFps   = false;
}
