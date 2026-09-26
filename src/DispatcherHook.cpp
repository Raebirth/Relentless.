#ifdef GEODE_IS_ANDROID

#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <time.h>

#include "InputQueue.hpp"
#include "GlobalState.hpp"

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Monotonic nanosecond clock via ARM vDSO.
// ---------------------------------------------------------------------------
namespace {

[[nodiscard]] __attribute__((always_inline))
inline double monoNs() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1.0e9
         + static_cast<double>(ts.tv_nsec);
}

[[nodiscard]] inline float halfScreenWidth() noexcept {
    CCEGLView* view = CCEGLView::sharedOpenGLView();
    if (!view) [[unlikely]] return 540.0f;
    return view->getFrameSize().width * 0.5f;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// FastEGLView — Producer hook
// ---------------------------------------------------------------------------
class $modify(FastEGLView, CCEGLView) {

    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        if (num > 0 && ids && xs && ys) [[likely]] {
            if (GlobalState::g_inLevel.load(std::memory_order_acquire)) {
                const double ts = monoNs();
                const float  halfWidth = halfScreenWidth();

                for (int i = 0; i < num; ++i) {
                    (void)g_inputQueue.try_push({
                        PlayerButton::Jump,
                        /*isPress=*/true,
                        /*isPlayer2=*/ xs[i] >= halfWidth,
                        /*_pad=*/{},
                        ts
                    });
                }
                return;
            }
        }
        CCEGLView::handleTouchesBegin(num, ids, xs, ys, timestamp);
    }

    void handleTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp) {
        if (num > 0 && ids && xs && ys) [[likely]] {
            if (GlobalState::g_inLevel.load(std::memory_order_acquire)) {
                const double ts = monoNs();
                const float  halfWidth = halfScreenWidth();

                for (int i = 0; i < num; ++i) {
                    (void)g_inputQueue.try_push({
                        PlayerButton::Jump,
                        /*isPress=*/false,
                        /*isPlayer2=*/ xs[i] >= halfWidth,
                        {},
                        ts
                    });
                }
                return;
            }
        }
        CCEGLView::handleTouchesEnd(num, ids, xs, ys, timestamp);
    }

    void handleTouchesCancel(int num, int ids[], float xs[], float ys[], double timestamp) {
        if (num > 0 && ids && xs && ys) [[likely]] {
            if (GlobalState::g_inLevel.load(std::memory_order_acquire)) {
                const double ts = monoNs();
                const float  halfWidth = halfScreenWidth();

                for (int i = 0; i < num; ++i) {
                    (void)g_inputQueue.try_push({
                        PlayerButton::Jump,
                        /*isPress=*/false,
                        /*isPlayer2=*/ xs[i] >= halfWidth,
                        {},
                        ts
                    });
                }
                return;
            }
        }
        CCEGLView::handleTouchesCancel(num, ids, xs, ys, timestamp);
    }
};

#endif // GEODE_IS_ANDROID
