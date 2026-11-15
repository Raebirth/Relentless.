#pragma once

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_ANDROID

#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdint>
#include <cstddef>

namespace ADPF {

struct APerformanceHintManager;
struct APerformanceHintSession;

using PFN_getManager = APerformanceHintManager* (*)();
using PFN_createSession = APerformanceHintSession* (*)(
    APerformanceHintManager*, const int32_t*, size_t, int64_t);
using PFN_reportActualWorkDuration = int (*)(
    APerformanceHintSession*, int64_t);

struct SessionState {
    APerformanceHintSession*     session     = nullptr;
    PFN_reportActualWorkDuration reportFn    = nullptr;
    bool                         initialized = false;
};

alignas(64) inline SessionState g_state{};

inline constexpr int64_t kTargetFrameNs = 5405405LL;
inline constexpr int64_t kMinReportedWorkNs = 4800000LL;

inline void initForGLThread() noexcept {
    if (g_state.initialized) [[likely]] return;
    g_state.initialized = true;

    void* lib = ::dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        geode::log::warn("[Relentless] ADPF: No se pudo enlazar libandroid.so");
        return;
    }

    auto getMgr = reinterpret_cast<PFN_getManager>(
        ::dlsym(lib, "APerformanceHint_getManager"));
    auto createSess = reinterpret_cast<PFN_createSession>(
        ::dlsym(lib, "APerformanceHint_createSession"));
    auto reportWork = reinterpret_cast<PFN_reportActualWorkDuration>(
        ::dlsym(lib, "APerformanceHint_reportActualWorkDuration"));

    if (!getMgr || !createSess || !reportWork) {
        geode::log::warn("[Relentless] ADPF: API no disponible en este NDK/OS.");
        return;
    }

    APerformanceHintManager* mgr = getMgr();
    if (!mgr) {
        geode::log::warn("[Relentless] ADPF: Manager nulo.");
        return;
    }

    const int32_t tid = static_cast<int32_t>(::syscall(SYS_gettid));
    APerformanceHintSession* sess = createSess(mgr, &tid, 1, kTargetFrameNs);
    if (!sess) {
        geode::log::warn("[Relentless] ADPF: No se pudo crear sesion para TID {}.", tid);
        return;
    }

    g_state.session  = sess;
    g_state.reportFn = reportWork;
    geode::log::info("[Relentless] ADPF activo en GLThread (TID {} | Target: 5.40ms @ 185Hz).", tid);
}

__attribute__((always_inline))
inline void reportFrameWork(int64_t actualDurationNs) noexcept {
    if (!g_state.session || !g_state.reportFn) [[unlikely]] return;

    const int64_t effectiveNs = (actualDurationNs < kMinReportedWorkNs)
                              ? kMinReportedWorkNs
                              : actualDurationNs;
    g_state.reportFn(g_state.session, effectiveNs);
}

} // namespace ADPF

#endif // GEODE_IS_ANDROID
