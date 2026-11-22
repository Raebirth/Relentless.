#pragma once

#include <atomic>
#include <cstdint>

namespace GlobalState {
    alignas(64) inline std::atomic<bool> g_inLevel{false};
}

namespace PerformanceCache {
    inline bool    s_showStats      = true;
    inline bool    s_showFps        = true;
    inline bool    s_chronosEnabled = true;
    inline int64_t s_chronosGrid    = 0;
    inline bool    s_chronosHud     = true;
}

namespace ChronosTelemetry {
    inline float                lastAlpha          = 0.0f;
    inline uint32_t             lastSplitCount     = 1;
    inline uint32_t             stepsInLastFrame   = 1;
    inline float                lastLatencyMs      = 0.0f;
    inline std::atomic<uint8_t> tsSource{0};
    inline uint64_t             totalSplitsSession = 0;

    inline const char* tsSourceTag(uint8_t src) noexcept {
        switch (src) {
            case 1: return "HWs";
            case 2: return "HWm";
            case 3: return "HWn";
            case 4: return "BTs";
            case 5: return "BTm";
            case 6: return "FB";
            default: return "--";
        }
    }
}
