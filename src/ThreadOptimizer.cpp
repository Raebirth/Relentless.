#ifdef GEODE_IS_ANDROID

#include <Geode/Geode.hpp>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace geode::prelude;

// ============================================================================
// ThreadOptimizer.cpp  —  Kernel Scheduler Bypass
//
// Goal: move the Cocos2d-x GL thread (which runs game logic, rendering,
// AND CCEGLView JNI callbacks) off the Android CFS/EAS scheduler and onto
// a deterministic POSIX real-time policy pinned to a specific CPU core.
//
// Target: CPU core 4 (index in /proc/cpuinfo terms).
//         On Snapdragon 8 Gen 3 (SM8650): Cortex-A720 cluster (cores 1-5),
//         pinning to core 4 avoids the Cortex-X4 prime core (core 7) and
//         its thermal throttle ramp-up latency.
//         On Snapdragon 8 Elite (SM8750): Oryon V2 layout differs —
//         core 4 falls in the mid-range Performance cluster, which has
//         the same sustained-frequency benefit.
//
// PRIVILEGE NOTE:
//   SCHED_FIFO requires CAP_SYS_NICE.  On a stock (non-rooted) device
//   this syscall returns EPERM and we fall back gracefully without any
//   crash.  With ASUS Armoury Crate "Performance Mode" granted to the
//   Geode launcher package, the scheduler grant may be pre-authorized.
//   We try SCHED_FIFO (priority 2) → SCHED_RR → log and continue.
//   The CPU affinity (sched_setaffinity) IS allowed without root.
// ============================================================================

namespace ThreadOptimizer {

// ---------------------------------------------------------------------------
// applyToCurrentThread() — call once on the GL thread (from $on_mod(Loaded)
// or the first frame of the game loop).
// ---------------------------------------------------------------------------
void applyToCurrentThread() noexcept {

    // ── Step 1: CPU Core Affinity ─────────────────────────────────────────
    // Pin exclusively to CPU core 4.
    //
    // Why pinning reduces latency:
    //   A. Eliminates inter-core migration: migrating between cores requires
    //      flushing the L1D TLB and re-warming the L1I/L1D cache.
    //      On Cortex-A720, L1I = 64 KB, L1D = 32 KB.  A full cold-start
    //      of the game-loop working set takes ~500 μs on first touch.
    //
    //   B. Clock re-ramp: if core 4 power-gates between frames, it needs
    //      ~100–500 μs to ramp back to maximum frequency.  Keeping the
    //      thread pinned prevents the power governor from power-gating it.
    //
    //   C. Cache locality: g_inputQueue's hot cache line stays in core 4's
    //      L1D exclusively, avoiding MOESI protocol traffic.
    //
    // sched_setaffinity() is allowed by Android SELinux policy for an app's
    // own thread without root or special capabilities.
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(4, &mask);  // Cortex-A720 core index 4

        const pid_t tid = static_cast<pid_t>(::syscall(SYS_gettid));
        if (::sched_setaffinity(tid, sizeof(mask), &mask) == 0) {
            log::info("[Relentless] CPU affinity: pinned GL thread {} to core 4.", tid);
        } else {
            log::warn("[Relentless] CPU affinity: sched_setaffinity failed (errno {}). "
                      "Cache locality not guaranteed.", errno);
        }
    }

    // ── Step 2: Real-Time Scheduling ──────────────────────────────────────
    // Attempt SCHED_FIFO priority 2.
    //
    // CFS / EAS problems this eliminates:
    //   • CFS vruntime accumulation: the game thread's quantum is shared
    //     with all other tasks at the same nice level.  Under load (shader
    //     compilation, GC), the kernel can preempt the GL thread for
    //     10–40 ms — enough to miss 2–7 frames at 240 Hz.
    //   • EAS load-balancing: EAS may migrate the game thread to a cooler
    //     but slower core to balance thermal budget.
    //
    // SCHED_FIFO priority 2:
    //   Priority 1 = minimum RT; priority 99 = maximum.
    //   Priority 2 preempts all SCHED_OTHER tasks and all nice-level tasks.
    //   Using 2 (not 99) ensures the audio driver (typically SCHED_FIFO 1
    //   or 3) and system compositor (SCHED_FIFO 2) are not preempted,
    //   preventing audio glitches or vsync stalls.
    {
        struct sched_param param{};
        param.sched_priority = 2;  // Preempts CFS; yields to audio/compositor

        if (::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param) == 0) {
            log::info("[Relentless] Scheduling: SCHED_FIFO priority 2 (bypasses CFS/EAS).");
            return;
        }

        // EPERM: no CAP_SYS_NICE.  Try SCHED_RR as intermediate fallback.
        // SCHED_RR is round-robin RT — still preempts CFS, but yields to
        // other SCHED_RR tasks of the same priority after its quantum.
        param.sched_priority = 1;
        if (::pthread_setschedparam(::pthread_self(), SCHED_RR, &param) == 0) {
            log::info("[Relentless] Scheduling: SCHED_RR priority 1 (partial CFS bypass).");
            return;
        }

        // No RT access. Log and continue — core affinity still helps.
        log::warn("[Relentless] Scheduling: SCHED_FIFO/RR denied (EPERM). "
                  "Enable Armoury Crate Performance Mode for RT scheduling. "
                  "Core affinity is still active.");
    }
}

}  // namespace ThreadOptimizer

#endif  // GEODE_IS_ANDROID
