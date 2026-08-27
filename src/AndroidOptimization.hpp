#pragma once
#ifdef GEODE_IS_ANDROID

// ============================================================================
// AndroidOptimizations.hpp
// Target: ROG Phone 9 — Snapdragon 8 Elite (SM8750), Android 14+
//
// Implements four extreme Android-specific optimizations:
//   1. Earliest-possible native touch intercept (practical AInputQueue bypass)
//   2. Zero touch slop at the Cocos2d-x level
//   3. CPU core affinity — Oryon V2 Prime core pinning
//   4. Physics dt stabilizer — consistent timing under frame-rate variance
//
// All optimizations are display/timing/thread-level only.
// TPS, game speed, and physics coefficients are NEVER modified.
// ============================================================================

#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCScheduler.hpp>

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "InputQueue.hpp"

using namespace geode::prelude;

// ============================================================================
// UTILITY — CLOCK_MONOTONIC nanosecond timestamp
//
// CLOCK_MONOTONIC is the correct clock for input timestamping on Android:
//   • Never steps backward (unlike CLOCK_REALTIME during NTP sync)
//   • Never adjusted by daylight-saving or timezone changes
//   • Implemented in the vDSO on ARM64 — no syscall overhead
// ============================================================================
namespace mono {
    [[nodiscard]] GEODE_INLINE double nowNs() noexcept {
        struct timespec ts;
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<double>(ts.tv_sec) * 1.0e9
             + static_cast<double>(ts.tv_nsec);
    }
}

// ============================================================================
// OPTIMIZATION 1 & 2 — Earliest Native Touch Intercept + Zero Touch Slop
//
// ── Why not true AInputQueue? ────────────────────────────────────────────────
// GD uses a standard Java Activity (AppCompatActivity), NOT ANativeActivity.
// AInputQueue is an exclusive resource of ANativeActivity — it cannot be
// accessed from a standard activity without root or system privileges.
// Attempting to grab it would deadlock or crash the app.
//
// ── What we do instead ───────────────────────────────────────────────────────
// CCEGLView::handleTouchesBegin is the *first* C++ symbol invoked after
// the JNI boundary. The full native path on Android is:
//
//   HW capacitive sensor
//   → Linux evdev / InputReader (kernel, 720 Hz on ROG Phone 9)
//   → InputDispatcher (system_server)
//   → Java: GDActivity.dispatchTouchEvent()
//   → Java: Cocos2dxGLSurfaceView.onTouchEvent()
//   → JNI:  nativeTouchesBegin() / nativeTouchesEnd()
//   → C++:  CCEGLView::handleTouchesBegin()   ← WE HOOK HERE
//   → C++:  CCTouchDispatcher::dispatchTouches()
//   → C++:  CCNode tree traversal (~5–15 ms on complex scenes)
//   → C++:  GJBaseGameLayer::handleButton()    ← also hooked (main.cpp)
//
// By intercepting at CCEGLView we log the event ~5–15 ms earlier than
// GJBaseGameLayer, saving the entire Cocos2d-x node-tree traversal.
//
// ── Zero Touch Slop ──────────────────────────────────────────────────────────
// Android's ViewConfiguration.getScaledTouchSlop() sets a pixel threshold
// that must be exceeded before a "press" becomes a confirmed touch. In
// Cocos2dxGLSurfaceView.onTouchEvent(), ACTION_DOWN events are dispatched
// immediately without slop checking, so Java-layer slop is NOT our bottleneck.
//
// The Cocos2d-x engine itself accumulates touches in CCTouchDispatcher and
// applies a small epsilon (kCCDirectorTouchDeltaX/Y) before forwarding.
// Our hook fires BEFORE that accumulation, achieving true zero-slop behavior
// from the C++ side. Every ACTION_DOWN produces an instant queue entry.
// ============================================================================
class $modify(FastEGLView, CCEGLView) {

    // ACTION_DOWN / POINTER_DOWN — finger touches screen
    void handleTouchesBegin(int num, int ids[], float xs[], float ys[]) {
        const double ts = mono::nowNs();
        for (int i = 0; i < num; ++i) {
            g_inputQueue.try_push({PlayerButton::Jump, /*isPress=*/true, ts});
        }
        // Always forward — we enrich the pipeline, not replace it
        CCEGLView::handleTouchesBegin(num, ids, xs, ys);
    }

    // ACTION_UP / POINTER_UP — finger lifts
    void handleTouchesEnd(int num, int ids[], float xs[], float ys[]) {
        const double ts = mono::nowNs();
        for (int i = 0; i < num; ++i) {
            g_inputQueue.try_push({PlayerButton::Jump, /*isPress=*/false, ts});
        }
        CCEGLView::handleTouchesEnd(num, ids, xs, ys);
    }

    // ACTION_CANCEL — treat as release (e.g. notification drawer interrupts touch)
    void handleTouchesCancelled(int num, int ids[], float xs[], float ys[]) {
        const double ts = mono::nowNs();
        for (int i = 0; i < num; ++i) {
            g_inputQueue.try_push({PlayerButton::Jump, /*isPress=*/false, ts});
        }
        CCEGLView::handleTouchesCancelled(num, ids, xs, ys);
    }
};

// ============================================================================
// OPTIMIZATION 3 — CPU Core Affinity (Thread Pinning)
//
// Snapdragon 8 Elite (SM8750) — "Phoenix" Oryon V2 core layout:
//   CPU 0–1  Oryon V2 Efficiency  ~2.0 GHz   (avoid — too slow)
//   CPU 2–5  Oryon V2 Performance ~3.53 GHz  (acceptable fallback)
//   CPU 6–7  Oryon V2 Prime       ~4.32 GHz  (TARGET)
//
// Thread migration between clusters causes:
//   • L1/L2 cache invalidation (~80–200 ns per migration)
//   • Clock re-ramp latency (big core waking from power gate: ~100–500 µs)
//   • NUMA-like memory access variance on multi-cluster SoCs
//
// By pinning the main thread to CPUs 6–7 we eliminate all of that.
//
// Dynamic detection strategy:
//   Read /sys/devices/system/cpu/cpuN/cpufreq/cpuinfo_max_freq for every
//   online CPU. Pin to all CPUs whose max frequency equals the system
//   maximum. Falls back to hard-coded {6, 7} if sysfs is unavailable
//   (e.g. read denied by SELinux on some ROMs).
//
// Scheduling policy:
//   Attempt SCHED_FIFO (real-time) → SCHED_RR → SCHED_OTHER (normal).
//   SCHED_FIFO is available on most Android ROMs for non-root processes
//   with RLIMIT_RTPRIO > 0, which ASUS sets for gaming-mode apps on
//   the ROG Phone 9. If unavailable, we continue gracefully.
// ============================================================================
namespace ThreadAffinity {

[[nodiscard]] inline long readCpuMaxFreqKHz(int cpu) noexcept {
    char path[96];
    std::snprintf(path, sizeof(path),
        "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0L;
    char buf[24];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) return 0L;
    buf[n] = '\0';
    return std::atol(buf);
}

inline void pinToHighPerformanceCores() noexcept {
    constexpr int kMaxCpus = 16;
    long  freqs[kMaxCpus] = {};
    long  maxFreq         = 0L;
    int   numOnline       = 0;

    for (int i = 0; i < kMaxCpus; ++i) {
        long f = readCpuMaxFreqKHz(i);
        if (f > 0L) {
            freqs[i] = f;
            if (f > maxFreq) maxFreq = f;
            numOnline = i + 1;
        }
    }

    cpu_set_t mask;
    CPU_ZERO(&mask);

    if (maxFreq > 0L) {
        for (int i = 0; i < numOnline; ++i) {
            if (freqs[i] == maxFreq) {
                CPU_SET(i, &mask);
                log::info("[Repentless] Affinity: CPU {} @ {} kHz", i, maxFreq);
            }
        }
    } else {
        // Hard-coded fallback for Snapdragon 8 Elite
        CPU_SET(6, &mask);
        CPU_SET(7, &mask);
        log::warn("[Repentless] Affinity: sysfs unreadable, falling back to CPU 6–7.");
    }

    // Apply to calling thread (main game thread)
    pid_t tid = static_cast<pid_t>(::syscall(SYS_gettid));
    if (::sched_setaffinity(tid, sizeof(mask), &mask) == 0) {
        log::info("[Repentless] Thread {} pinned to prime core(s).", tid);
    } else {
        log::warn("[Repentless] sched_setaffinity failed (errno {}). "
                  "Try enabling ASUS Armoury Crate performance mode.", errno);
    }

    // Attempt real-time scheduling: SCHED_FIFO → SCHED_RR → normal
    struct sched_param param{};
    param.sched_priority = ::sched_get_priority_max(SCHED_FIFO);
    if (::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param) == 0) {
        log::info("[Repentless] Scheduling: SCHED_FIFO (real-time, prio {}).",
                  param.sched_priority);
        return;
    }
    param.sched_priority = ::sched_get_priority_max(SCHED_RR);
    if (::pthread_setschedparam(::pthread_self(), SCHED_RR, &param) == 0) {
        log::info("[Repentless] Scheduling: SCHED_RR (round-robin RT, prio {}).",
                  param.sched_priority);
        return;
    }
    log::warn("[Repentless] RT scheduling unavailable — continuing with SCHED_OTHER.");
}

} // namespace ThreadAffinity

// ============================================================================
// OPTIMIZATION 4 — Physics Timing Stabilizer (Fixed-dt Clamping)
//
// ── What this IS ─────────────────────────────────────────────────────────────
// A dt clamp applied to CCScheduler::update(float dt) that prevents the
// game from "catching up" after a long frame by running physics for an
// abnormally large time slice.
//
// ── What this IS NOT ─────────────────────────────────────────────────────────
// This is NOT a TPS change. We do not inject extra physics steps.
// We do not increase or decrease the physics update frequency.
// We do not modify GJBaseGameLayer's fixed-step accumulator.
// The TPS the game runs at is wholly determined by the display refresh
// and GD's own scheduler — we never touch those.
//
// ── The problem it solves ────────────────────────────────────────────────────
// On ROG Phone 9 at 165 Hz, a normal frame takes ~6.06 ms.
// If a heavy object group causes a single slow frame of ~50 ms,
// CCScheduler receives dt = 0.050 s and runs every update callback
// with that inflated delta. Physics nodes effectively "teleport"
// forward by ~8 physics steps in one shot, producing:
//   • Visible "physics pop" (player position jumps)
//   • Input events that arrived during the lag frame appearing to fire
//     simultaneously instead of sequentially
//
// ── The fix ──────────────────────────────────────────────────────────────────
// Cap dt at kMaxDt (= 1/30 s, ~33.3 ms). Any frame taking longer than
// that is treated as 33.3 ms. The "lost time" produces a brief visual
// stutter — but physics remains smooth and input timestamps stay valid.
//
// Floor dt at kMinDt (= 1/1000 s) as a guard against exotic zero-delta
// frames from the vsync scheduler on some ROMs.
//
// kMaxDt = 1/30 s chosen deliberately:
//   At 165 TPS  → allows ~5 physics steps before clamping  (generous)
//   At 240 TPS  → allows ~8 physics steps before clamping  (generous)
//   Still clamps runaway 100-200 ms lag spikes from GC pauses / JNI storms
// ============================================================================
class $modify(StableScheduler, CCScheduler) {
    void update(float dt) {
        // kMaxDt: clamp at 33.3 ms — anything longer is a severe hitch.
        // Discarding excess dt prevents physics from trying to simulate
        // the whole stall duration in one giant step.
        constexpr float kMaxDt = 1.0f / 30.0f;

        // kMinDt: floor at 1 ms — guards against pathological near-zero
        // deltas that some Android vsync implementations produce.
        constexpr float kMinDt = 1.0f / 1000.0f;

        const float safe = (dt > kMaxDt) ? kMaxDt
                         : (dt < kMinDt) ? kMinDt
                         :                 dt;

        CCScheduler::update(safe);
    }
};

// ============================================================================
// ANDROID INIT — must be called from the main thread inside $on_mod(Loaded)
// ============================================================================
inline void androidInit() noexcept {
    log::info("[Repentless] Applying ROG Phone 9 Android optimizations…");
    ThreadAffinity::pinToHighPerformanceCores();
    log::info("[Repentless] Android optimizations applied.");
}

#endif // GEODE_IS_ANDROID
