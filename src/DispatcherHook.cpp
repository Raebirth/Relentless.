#include <Geode/Geode.hpp>

#ifdef GEODE_IS_ANDROID

#include <Geode/modify/CCEGLView.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <cmath>
#include <cstdint>
#include <time.h>

#include "InputQueue.hpp"
#include "GlobalState.hpp"

using namespace geode::prelude;

namespace {

[[nodiscard]] __attribute__((always_inline))
inline int64_t monoNsInt() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL
         + static_cast<int64_t>(ts.tv_nsec);
}

[[nodiscard]] __attribute__((always_inline))
inline int64_t bootNsInt() noexcept {
#ifdef CLOCK_BOOTTIME
    struct timespec ts{};
    ::clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL
         + static_cast<int64_t>(ts.tv_nsec);
#else
    return monoNsInt();
#endif
}

[[nodiscard]] inline int64_t resolveEventTimeNs(
    double rawTs, int64_t nowMonoNs, uint8_t& outSource
) noexcept {
    static bool s_loggedOnce = false;

    if (rawTs > 0.0 && std::isfinite(rawTs)) {
        const double nowMonoSec = static_cast<double>(nowMonoNs) * 1.0e-9;
        const double nowMonoMs  = static_cast<double>(nowMonoNs) * 1.0e-6;
        const double nowMonoDbl = static_cast<double>(nowMonoNs);

        if (std::abs(nowMonoSec - rawTs) < 2.0) {
            outSource = 1;
            const int64_t evNs = static_cast<int64_t>(rawTs * 1.0e9);
            if (!s_loggedOnce) {
                s_loggedOnce = true;
                log::info("[Chronos] TS detectado: MONOTONIC seg (raw={:.6f}, lag={:.3f}ms)",
                          rawTs, static_cast<double>(nowMonoNs - evNs) * 1e-6);
            }
            return evNs;
        }
        if (std::abs(nowMonoMs - rawTs) < 2000.0) {
            outSource = 2;
            const int64_t evNs = static_cast<int64_t>(rawTs * 1.0e6);
            if (!s_loggedOnce) {
                s_loggedOnce = true;
                log::info("[Chronos] TS detectado: MONOTONIC ms (raw={:.3f}, lag={:.3f}ms)",
                          rawTs, static_cast<double>(nowMonoNs - evNs) * 1e-6);
            }
            return evNs;
        }
        if (std::abs(nowMonoDbl - rawTs) < 2.0e9) {
            outSource = 3;
            const int64_t evNs = static_cast<int64_t>(rawTs);
            if (!s_loggedOnce) {
                s_loggedOnce = true;
                log::info("[Chronos] TS detectado: MONOTONIC ns (lag={:.3f}ms)",
                          static_cast<double>(nowMonoNs - evNs) * 1e-6);
            }
            return evNs;
        }

        const int64_t nowBootNs  = bootNsInt();
        const int64_t bootOffset = nowBootNs - nowMonoNs;
        const double nowBootSec  = static_cast<double>(nowBootNs) * 1.0e-9;
        const double nowBootMs   = static_cast<double>(nowBootNs) * 1.0e-6;

        if (std::abs(nowBootSec - rawTs) < 2.0) {
            outSource = 4;
            const int64_t evNs = static_cast<int64_t>(rawTs * 1.0e9) - bootOffset;
            if (!s_loggedOnce) {
                s_loggedOnce = true;
                log::info("[Chronos] TS detectado: BOOTTIME seg (raw={:.6f}, lag={:.3f}ms)",
                          rawTs, static_cast<double>(nowMonoNs - evNs) * 1e-6);
            }
            return evNs;
        }
        if (std::abs(nowBootMs - rawTs) < 2000.0) {
            outSource = 5;
            const int64_t evNs = static_cast<int64_t>(rawTs * 1.0e6) - bootOffset;
            if (!s_loggedOnce) {
                s_loggedOnce = true;
                log::info("[Chronos] TS detectado: BOOTTIME ms (raw={:.3f}, lag={:.3f}ms)",
                          rawTs, static_cast<double>(nowMonoNs - evNs) * 1e-6);
            }
            return evNs;
        }
    }

    outSource = 6;
    if (!s_loggedOnce) {
        s_loggedOnce = true;
        log::warn("[Chronos] TS fallback (raw={:.6f}, monoSec={:.6f}). Requiere UIThread Bridge.",
                  rawTs, static_cast<double>(nowMonoNs) * 1.0e-9);
    }
    return nowMonoNs;
}

[[nodiscard]] inline float halfScreenWidth() noexcept {
    CCEGLView* view = CCEGLView::sharedOpenGLView();
    if (!view) [[unlikely]] return 540.0f;
    return view->getFrameSize().width * 0.5f;
}

[[nodiscard]] inline bool shouldInterceptLevelTouch() noexcept {
    if (!GlobalState::g_inLevel.load(std::memory_order_acquire)) [[likely]] {
        return false;
    }
    auto* pl = PlayLayer::get();
    if (!pl) [[unlikely]] return false;
    return !pl->m_isPaused && !pl->m_hasCompletedLevel;
}

} // anonymous namespace

class $modify(FastEGLView, CCEGLView) {

    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        if (num > 0 && ids && xs && ys && shouldInterceptLevelTouch()) [[likely]] {
            const int64_t nowNs = monoNsInt();
            uint8_t src = 6;
            const int64_t evNs = resolveEventTimeNs(timestamp, nowNs, src);
            const float halfWidth = halfScreenWidth();
            ChronosTelemetry::tsSource.store(src, std::memory_order_relaxed);

            for (int i = 0; i < num; ++i) {
                (void)g_inputQueue.try_push({
                    PlayerButton::Jump,
                    /*isPress=*/true,
                    /*isPlayer2=*/ xs[i] >= halfWidth,
                    src,
                    /*_pad=*/0,
                    evNs,
                    nowNs
                });
            }
            return;
        }
        CCEGLView::handleTouchesBegin(num, ids, xs, ys, timestamp);
    }

    void handleTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp) {
        if (num > 0 && ids && xs && ys && shouldInterceptLevelTouch()) [[likely]] {
            const int64_t nowNs = monoNsInt();
            uint8_t src = 6;
            const int64_t evNs = resolveEventTimeNs(timestamp, nowNs, src);
            const float halfWidth = halfScreenWidth();
            ChronosTelemetry::tsSource.store(src, std::memory_order_relaxed);

            for (int i = 0; i < num; ++i) {
                (void)g_inputQueue.try_push({
                    PlayerButton::Jump,
                    /*isPress=*/false,
                    /*isPlayer2=*/ xs[i] >= halfWidth,
                    src,
                    /*_pad=*/0,
                    evNs,
                    nowNs
                });
            }
            return;
        }
        CCEGLView::handleTouchesEnd(num, ids, xs, ys, timestamp);
    }

    void handleTouchesCancel(int num, int ids[], float xs[], float ys[], double timestamp) {
        if (num > 0 && ids && xs && ys && shouldInterceptLevelTouch()) [[likely]] {
            const int64_t nowNs = monoNsInt();
            uint8_t src = 6;
            const int64_t evNs = resolveEventTimeNs(timestamp, nowNs, src);
            const float halfWidth = halfScreenWidth();
            ChronosTelemetry::tsSource.store(src, std::memory_order_relaxed);

            for (int i = 0; i < num; ++i) {
                (void)g_inputQueue.try_push({
                    PlayerButton::Jump,
                    /*isPress=*/false,
                    /*isPlayer2=*/ xs[i] >= halfWidth,
                    src,
                    /*_pad=*/0,
                    evNs,
                    nowNs
                });
            }
            return;
        }
        CCEGLView::handleTouchesCancel(num, ids, xs, ys, timestamp);
    }
};

#endif // GEODE_IS_ANDROID
