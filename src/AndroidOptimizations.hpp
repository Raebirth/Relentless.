#pragma once
#ifdef GEODE_IS_ANDROID

#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <time.h>

#include "InputQueue.hpp"

using namespace geode::prelude;

namespace mono {

[[nodiscard]] __attribute__((always_inline))
inline double nowNs() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1.0e9
         + static_cast<double>(ts.tv_nsec);
}

} // namespace mono

// ============================================================================
// CCEGLVIEW TOUCH INTERCEPT - ZERO LATENCY
// ============================================================================
class $modify(FastEGLView, CCEGLView) {

    void handleTouchesBegin(int num, int* ids, float* xs, float* ys) {
        if (num > 0 && ids != nullptr && xs != nullptr && ys != nullptr) [[likely]] {
            const double ts = mono::nowNs();
            for (int i = 0; i < num; ++i) {
                g_inputQueue.try_push({ PlayerButton::Jump, /*isPress=*/true, ts });
            }
        }
        CCEGLView::handleTouchesBegin(num, ids, xs, ys);
    }

    void handleTouchesEnd(int num, int* ids, float* xs, float* ys) {
        if (num > 0 && ids != nullptr && xs != nullptr && ys != nullptr) [[likely]] {
            const double ts = mono::nowNs();
            for (int i = 0; i < num; ++i) {
                g_inputQueue.try_push({ PlayerButton::Jump, /*isPress=*/false, ts });
            }
        }
        CCEGLView::handleTouchesEnd(num, ids, xs, ys);
    }

    void handleTouchesCancelled(int num, int* ids, float* xs, float* ys) {
        if (num > 0 && ids != nullptr && xs != nullptr && ys != nullptr) [[likely]] {
            const double ts = mono::nowNs();
            for (int i = 0; i < num; ++i) {
                g_inputQueue.try_push({ PlayerButton::Jump, /*isPress=*/false, ts });
            }
        }
        CCEGLView::handleTouchesCancelled(num, ids, xs, ys);
    }
};

inline void androidInit() noexcept {
    log::info("[Relentless] CCEGLView touch hook installed.");
    log::info("[Relentless] Touch intercept path: JNI → handleTouchesBegin → g_inputQueue.");
}

#endif // GEODE_IS_ANDROID
