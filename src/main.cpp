#include <Geode/Geode.hpp>
#include "GlobalState.hpp"

#ifdef GEODE_IS_ANDROID
#include "AndroidOptimizations.hpp"
#endif

#ifdef GEODE_IS_WINDOWS
#  include <windows.h>
#  include <timeapi.h>
#endif

using namespace geode::prelude;

$on_mod(Loaded) {
    auto* mod = Mod::get();

    auto bindBool = [&](const char* key, bool& target) {
        if (mod->hasSetting(key)) {
            target = mod->getSettingValue<bool>(key);
            listenForSettingChanges<bool>(key, [&target](bool v) {
                target = v;
            });
        }
    };

    bindBool("show-stats",       PerformanceCache::s_showStats);
    bindBool("show-fps",         PerformanceCache::s_showFps);
    bindBool("chronos-substep",  PerformanceCache::s_chronosEnabled);
    bindBool("chronos-hud",      PerformanceCache::s_chronosHud);
    bindBool("vbo-orphaning",    PerformanceCache::s_vboOrphaning);
    bindBool("empty-batch-cull", PerformanceCache::s_emptyBatchCull);
    bindBool("particle-opt",     PerformanceCache::s_particleOpt);
    bindBool("render-profiler",  PerformanceCache::s_renderProfiler);

    if (mod->hasSetting("chronos-grid")) {
        PerformanceCache::s_chronosGrid = mod->getSettingValue<int64_t>("chronos-grid");
        listenForSettingChanges<int64_t>("chronos-grid", +[](int64_t v) {
            PerformanceCache::s_chronosGrid = v;
        });
    }

#ifdef GEODE_IS_WINDOWS
    timeBeginPeriod(1);
    log::info("[Relentless] Windows timer resolution set to 1 ms.");
#endif

#ifdef GEODE_IS_ANDROID
    ADPF::initForGLThread();
    log::info("[Relentless] v1.5 RenderOptimizer + Chronos + ADPF initialized.");
#endif
}
