#ifdef GEODE_IS_ANDROID

#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <time.h>

#include "InputQueue.hpp"
#include "GlobalState.hpp"

using namespace geode::prelude;

// ============================================================================
// DispatcherHook.cpp  —  The Producer
//
// ARCHITECTURE: WHY NOT CCTouchDispatcher::touches?
// ─────────────────────────────────────────────────
// CCTouchDispatcher::touches(CCSet*, CCEvent*, unsigned int) is one hop
// LATER in the pipeline than CCEGLView.  The call chain is:
//
//   JNI nativeTouchesBegin()
//     → CCEGLView::handleTouchesBegin()  ← WE HOOK HERE (earliest C++ frame)
//         → CCTouchDispatcher::touches()  (CCSet already allocated on heap)
//             → delegates (per-layer ccTouchBegan)
//
// CCTouchDispatcher::touches also requires Geode to have its non-virtual
// method address in the Android arm64 bindings.  If the address is absent
// from the .bro file, $modify silently installs a null hook → SIGSEGV on
// first touch.  No such risk exists for CCEGLView methods, which we have
// confirmed through iteration.
//
// THREADING MODEL:
// ─────────────────
// In GD on Android, the Java SurfaceView's renderer and the JNI input
// callbacks both execute on the same GL thread.  This means the producer
// (DispatcherHook) and the consumer (GameLoopHook) run on the SAME thread.
// The SPSC queue is still correct in single-threaded mode — it simply
// acts as a timestamped FIFO that decouples touch capture time from
// dispatch time (beginning of the game loop tick).
//
// DOUBLE-DISPATCH PREVENTION:
// ────────────────────────────
// When GlobalState::g_inLevel is true, we push to the queue AND suppress
// the original CCEGLView handler.  GameLoopHook becomes the sole dispatcher
// for that frame.  In menus (g_inLevel false), we call the original normally.
//
// TOUCH → PLAYER MAPPING:
// ─────────────────────────
// Raw pixel X coordinate (from the JNI bridge) is compared against the
// EGL framebuffer half-width.  Left half → Player 1 Jump.  Right half →
// Player 2 Jump (for two-player mode or dual-zone setups).
// ============================================================================

// ---------------------------------------------------------------------------
// Monotonic nanosecond clock via ARM vDSO.
//
// On Android 14+, CLOCK_MONOTONIC is serviced entirely in userspace through
// a kernel-mapped vDSO page.  No syscall ring transition occurs.
// Measured overhead on Cortex-A720: ~10–15 ns.
// ---------------------------------------------------------------------------
namespace {

[[nodiscard]] __attribute__((always_inline))
inline double monoNs() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1.0e9
         + static_cast<double>(ts.tv_nsec);
}

// Get the EGL framebuffer half-width in pixels once per touch event.
// This is a fast inline read from a CCEGLView member — no syscall.
[[nodiscard]] inline float halfScreenWidth() noexcept {
    CCEGLView* view = CCEGLView::sharedOpenGLView();
    if (!view) [[unlikely]] return 540.0f;  // safe fallback (1080p half)
    return view->getFrameSize().width * 0.5f;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// FastEGLView — the Producer hook class.
// ---------------------------------------------------------------------------
class $modify(FastEGLView, CCEGLView) {

    // ── Touch BEGIN ─────────────────────────────────────────────────────────
    void handleTouchesBegin(int num, int* ids, float* xs, float* ys) {
        if (num > 0 && ids && xs && ys) [[likely]] {
            if (GlobalState::g_inLevel.load(std::memory_order_acquire)) {
                // In-level: push to queue, suppress original to prevent
                // double-dispatch.  GameLoopHook will call pushButton.
                const double   ts        = monoNs();
                const float    halfWidth = halfScreenWidth();

                for (int i = 0; i < num; ++i) {
                    g_inputQueue.try_push({
                        PlayerButton::Jump,
                        /*isPress=*/true,
                        /*isPlayer2=*/ xs[i] >= halfWidth,
                        /*_pad=*/{},
                        ts
                    });
                }
                // Do NOT call CCEGLView::handleTouchesBegin — game loop
                // will dispatch from the queue instead.
                return;
            }
        }
        // Out of level (menus, loading): normal path.
        CCEGLView::handleTouchesBegin(num, ids, xs, ys);
    }

    // ── Touch END ───────────────────────────────────────────────────────────
    void handleTouchesEnd(int num, int* ids, float* xs, float* ys) {
        if (num > 0 && ids && xs && ys) [[likely]] {
            if (GlobalState::g_inLevel.load(std::memory_order_acquire)) {
                const double   ts        = monoNs();
                const float    halfWidth = halfScreenWidth();
                for (int i = 0; i < num; ++i) {
                    g_inputQueue.try_push({
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
        CCEGLView::handleTouchesEnd(num, ids, xs, ys);
    }

    // ── Touch CANCELLED ─────────────────────────────────────────────────────
    // "handleTouchesCancelled" WITH the 'd' — the correct Cocos2d-x 2.x name.
    // "handleTouchesCancel" (no 'd') does not exist and resolves to a null
    // hook address, causing SIGSEGV on first touch.
    void handleTouchesCancelled(int num, int* ids, float* xs, float* ys) {
        if (num > 0 && ids && xs && ys) [[likely]] {
            if (GlobalState::g_inLevel.load(std::memory_order_acquire)) {
                const double   ts        = monoNs();
                const float    halfWidth = halfScreenWidth();
                for (int i = 0; i < num; ++i) {
                    // Cancel = release.  Must release to avoid a stuck jump.
                    g_inputQueue.try_push({
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
        CCEGLView::handleTouchesCancelled(num, ids, xs, ys);
    }
};

#endif  // GEODE_IS_ANDROID
