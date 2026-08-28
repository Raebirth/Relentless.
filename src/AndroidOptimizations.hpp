#pragma once
#ifdef GEODE_IS_ANDROID

// ============================================================================
// AndroidOptimizations.hpp
// Target: ROG Phone 9 — Snapdragon 8 Elite (SM8750), Android 14+
//
// IMPORTANT — NO GEODE HOOKS IN THIS FILE.
// CCEGLView and CCScheduler hooks were removed because their method
// addresses may not be resolvable through Geode's Android arm64 bindings,
// causing a crash at mod-load time before the game opens.
//
// All Geode $modify hooks live exclusively in main.cpp where bindings
// are confirmed to exist. This file contains only POSIX syscall wrappers
// that cannot crash on load.
//
// Optimizations provided here:
//   • CPU Core Affinity — pin the game thread to Oryon V2 Prime cores
//   • Real-time scheduling — SCHED_FIFO → SCHED_RR → normal fallback
// ============================================================================

#include <Geode/Geode.hpp>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

using namespace geode::prelude;

// ============================================================================
// CPU CORE AFFINITY — Thread Pinning
//
// Snapdragon 8 Elite (SM8750) "Phoenix" Oryon V2 core layout:
//   CPU 0–1  Oryon V2 Efficiency  ~2.0 GHz   (avoid — too slow)
//   CPU 2–5  Oryon V2 Performance ~3.53 GHz  (acceptable fallback)
//   CPU 6–7  Oryon V2 Prime       ~4.32 GHz  (TARGET)
//
// Pinning eliminates:
//   • L1/L2 cache invalidation on migration (~80–200 ns)
//   • Clock re-ramp latency when waking a power-gated core (~100–500 µs)
//
// Dynamic detection: reads cpuinfo_max_freq from sysfs and pins to whichever
// CPUs run at the system maximum. Falls back to CPUs 6–7 if sysfs is
// restricted by SELinux on the current ROM.
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
    long freqs[kMaxCpus]   = {};
    long maxFreq           = 0L;
    int  numOnline         = 0;

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
        log::warn("[Repentless] Affinity: sysfs unreadable, using fallback CPUs 6-7.");
    }

    pid_t tid = static_cast<pid_t>(::syscall(SYS_gettid));
    if (::sched_setaffinity(tid, sizeof(mask), &mask) == 0) {
        log::info("[Repentless] Thread {} pinned to prime core(s).", tid);
    } else {
        log::warn("[Repentless] sched_setaffinity failed (errno {}). "
                  "Enable Armoury Crate performance mode for best results.", errno);
    }

    // Attempt real-time scheduling: SCHED_FIFO → SCHED_RR → SCHED_OTHER
    // SCHED_FIFO requires RLIMIT_RTPRIO > 0, set by ASUS for Armoury Crate apps.
    struct sched_param param{};
    param.sched_priority = ::sched_get_priority_max(SCHED_FIFO);
    if (::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param) == 0) {
        log::info("[Repentless] Scheduling: SCHED_FIFO (prio {}).", param.sched_priority);
        return;
    }
    param.sched_priority = ::sched_get_priority_max(SCHED_RR);
    if (::pthread_setschedparam(::pthread_self(), SCHED_RR, &param) == 0) {
        log::info("[Repentless] Scheduling: SCHED_RR (prio {}).", param.sched_priority);
        return;
    }
    log::info("[Repentless] Scheduling: SCHED_OTHER (normal priority).");
}

} // namespace ThreadAffinity

// ============================================================================
// ANDROID INIT — must be called on the main thread inside $on_mod(Loaded)
// ============================================================================
inline void androidInit() noexcept {
    log::info("[Repentless] Applying ROG Phone 9 optimizations...");
    ThreadAffinity::pinToHighPerformanceCores();
    log::info("[Repentless] ROG Phone 9 optimizations applied.");
}

#endif // GEODE_IS_ANDROID
