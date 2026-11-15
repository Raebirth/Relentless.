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
    PerformanceCache::s_showStats = Mod::get()->getSettingValue<bool>("show-stats");
    PerformanceCache::s_showFps   = Mod::get()->getSettingValue<bool>("show-fps");

    listenForSettingChanges<bool>("show-stats", +[](bool v) {
        PerformanceCache::s_showStats = v;
    });
    listenForSettingChanges<bool>("show-fps", +[](bool v) {
        PerformanceCache::s_showFps = v;
    });

#ifdef GEODE_IS_WINDOWS
    timeBeginPeriod(1);
    log::info("[Relentless] Windows timer resolution set to 1 ms.");
#endif

#ifdef GEODE_IS_ANDROID
    ADPF::initForGLThread();
    log::info("[Relentless] Engine hooks & ADPF initialized.");
#endif
}
