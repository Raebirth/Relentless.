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

    if (mod->hasSetting("show-stats")) {
        PerformanceCache::s_showStats = mod->getSettingValue<bool>("show-stats");
        listenForSettingChanges<bool>("show-stats", +[](bool v) {
            PerformanceCache::s_showStats = v;
        });
    }
    if (mod->hasSetting("show-fps")) {
        PerformanceCache::s_showFps = mod->getSettingValue<bool>("show-fps");
        listenForSettingChanges<bool>("show-fps", +[](bool v) {
            PerformanceCache::s_showFps = v;
        });
    }
    if (mod->hasSetting("chronos-substep")) {
        PerformanceCache::s_chronosEnabled = mod->getSettingValue<bool>("chronos-substep");
        listenForSettingChanges<bool>("chronos-substep", +[](bool v) {
            PerformanceCache::s_chronosEnabled = v;
        });
    }
    if (mod->hasSetting("chronos-grid")) {
        PerformanceCache::s_chronosGrid = mod->getSettingValue<int64_t>("chronos-grid");
        listenForSettingChanges<int64_t>("chronos-grid", +[](int64_t v) {
            PerformanceCache::s_chronosGrid = v;
        });
    }
    if (mod->hasSetting("chronos-hud")) {
        PerformanceCache::s_chronosHud = mod->getSettingValue<bool>("chronos-hud");
        listenForSettingChanges<bool>("chronos-hud", +[](bool v) {
            PerformanceCache::s_chronosHud = v;
        });
    }

#ifdef GEODE_IS_WINDOWS
    timeBeginPeriod(1);
    log::info("[Relentless] Windows timer resolution set to 1 ms.");
#endif

#ifdef GEODE_IS_ANDROID
    ADPF::initForGLThread();
    log::info("[Relentless] Chronos v4 prototype + ADPF initialized.");
#endif
}
