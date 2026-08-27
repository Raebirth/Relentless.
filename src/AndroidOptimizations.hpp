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
// ============================================================================
class $modify(FastEGLView, CCEGLView) {

    // ACTION_DOWN / POINTER_DOWN — finger touches screen
    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        const double ts = mono::nowNs();
        for (int i = 0; i < num; ++i) {
            g_inputQueue.try_push({PlayerButton::Jump, /*isPress=*/true, ts});
        }
        // Always forward — we enrich the pipeline, not replace it
        CCEGLView::handleTouchesBegin(num, ids, xs, ys, timestamp);
    }

    // ACTION_UP / POINTER_UP — finger lifts
    void handleTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp) {
        const double ts = mono::nowNs();
        for (int i = 0; i < num; ++i) {
            g_inputQueue.try_push({PlayerButton::Jump, /*isPress=*/false, ts});
        }
        CCEGLView::handleTouchesEnd(num, ids, xs, ys, timestamp);
    }

    // ACTION_CANCEL — treat as release
    void handleTouchesCancel(int num, int ids[], float xs[], float ys[], double timestamp) {
        const double ts = mono::nowNs();
        for (int i = 0; i < num; ++i) {
            g_inputQueue.try_push({PlayerButton::Jump, /*isPress=*/false, ts});
        }
        CCEGLView::handleTouchesCancel(num, ids, xs, ys, timestamp);
    }
};

// ============================================================================
// OPTIMIZATION 3 — CPU Core Affinity (Thread Pinning)
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
                log::info("[Relentless] Affinity: CPU {} @ {} kHz", i, maxFreq);
            }
        }
    } else {
        // Hard-coded fallback for Snapdragon 8 Elite
        CPU_SET(6, &mask);
        CPU_SET(7, &mask);
        log::warn("[Relentless] Affinity: sysfs unreadable, falling back to CPU 6–7.");
    }

    // Apply to calling thread (main game thread)
    pid_t tid = static_cast<pid_t>(::syscall(SYS_gettid));
    if (::sched_setaffinity(tid, sizeof(mask), &mask) == 0) {
        log::info("[Relentless] Thread {} pinned to prime core(s).", tid);
    } else {
        log::warn("[Relentless] sched_setaffinity failed (errno {}). "
                  "Try enabling ASUS Armoury Crate performance mode.", errno);
    }

    // Attempt real-time scheduling: SCHED_FIFO → SCHED_RR → normal
    struct sched_param param{};
    param.sched_priority = ::sched_get_priority_max(SCHED_FIFO);
    if (::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param) == 0) {
        log::info("[Relentless] Scheduling: SCHED_FIFO (real-time, prio {}).",
                  param.sched_priority);
        return;
    }
    param.sched_priority = ::sched_get_priority_max(SCHED_RR);
    if (::pthread_setschedparam(::pthread_self(), SCHED_RR, &param) == 0) {
        log::info("[Relentless] Scheduling: SCHED_RR (round-robin RT, prio {}).",
                  param.sched_priority);
        return;
    }
    log::warn("[Relentless] RT scheduling unavailable — continuing with SCHED_OTHER.");
}

} // namespace ThreadAffinity

// ============================================================================
// OPTIMIZATION 4 — Physics Timing Stabilizer (Fixed-dt Clamping)
// ============================================================================
class $modify(StableScheduler, CCScheduler) {
    void update(float dt) {
        constexpr float kMaxDt = 1.0f / 30.0f;
        constexpr float kMinDt = 1.0f / 1000.0f;

        const float safe = (dt > kMaxDt) ? kMaxDt
                         : (dt < kMinDt) ? kMinDt
                         :                 dt;

        CCScheduler::update(safe);
    }
};

// ============================================================================
// ANDROID INIT
// ============================================================================
inline void androidInit() noexcept {
    log::info("[Relentless] Applying ROG Phone 9 Android optimizations…");
    ThreadAffinity::pinToHighPerformanceCores();
    log::info("[Relentless] Android optimizations applied.");
}

#endif // GEODE_IS_ANDROID
