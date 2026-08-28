#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>   // same hooks as v1.0.0 — known safe
#include <charconv>
#include <chrono>
#include <cstring>

#include "InputQueue.hpp"
#include "FrameSmoother.hpp"

// Android: POSIX-only (CPU affinity). No new $modify hooks in this file.
#ifdef GEODE_IS_ANDROID
#  include "AndroidOptimizations.hpp"
#endif

// Windows: OS timer resolution
#ifdef GEODE_IS_WINDOWS
#  include <windows.h>
#  include <timeapi.h>
#endif

using namespace geode::prelude;

// ============================================================================
// HOT-SETTING CACHE
// ============================================================================
namespace PerformanceCache {
    inline bool s_showStats = false;
    inline bool s_showFps   = false;
}

// ============================================================================
// MOD LIFECYCLE
// ============================================================================
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
    // Pure POSIX — CPU affinity + RT scheduling. Cannot crash on load.
    androidInit();
#endif
}

// ============================================================================
// INPUT QUEUE PRODUCER — PlayerObject hooks (v1.0.0-safe, confirmed working)
//
// We use PlayerObject::pushButton / releaseButton because these are in
// Geode's Android bindings and worked in v1.0.0. GJBaseGameLayer::handleButton
// was REMOVED after it caused crashes — it may not exist in GD 2.2081 Android.
//
// Benefit: every button event is timestamped with a monotonic nanosecond
// clock and pushed into the lock-free SPSC queue for future consumers.
// ============================================================================
class $modify(FastPlayerObject, PlayerObject) {
    void pushButton(PlayerButton btn) {
        g_inputQueue.try_push({
            btn,
            /*isPress=*/true,
            static_cast<double>(
                std::chrono::steady_clock::now().time_since_epoch().count())
        });
        PlayerObject::pushButton(btn);
    }

    void releaseButton(PlayerButton btn) {
        g_inputQueue.try_push({
            btn,
            /*isPress=*/false,
            static_cast<double>(
                std::chrono::steady_clock::now().time_since_epoch().count())
        });
        PlayerObject::releaseButton(btn);
    }
};

// ============================================================================
// OPTIMIZED PLAY LAYER
//
// Uses ONLY PlayLayer hooks — the same target class as v1.0.0.
// Removed from previous crashing versions:
//   • m_uiLayer usage — reverted to this->addChild (safe, no offset lookup)
//   • CCDirector::drawScene hook — replaced by tracking dt in update()
//   • GJBaseGameLayer::handleButton hook — replaced by PlayerObject hooks above
// ============================================================================
class $modify(OptimizedPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_statsLabel = nullptr;
        float          m_lastX      = -9999.0f;
        float          m_lastY      = -9999.0f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        PerformanceCache::s_showStats = Mod::get()->getSettingValue<bool>("show-stats");
        PerformanceCache::s_showFps   = Mod::get()->getSettingValue<bool>("show-fps");

        auto* label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("stats-label"_spr);
        label->setAnchorPoint({0.0f, 0.0f});
        label->setScale(0.4f);
        label->setPosition({10.0f, 10.0f});
        label->setVisible(PerformanceCache::s_showStats);

        // Use this directly — same as v1.0.0. m_uiLayer removed:
        // its member offset may differ on Android, causing addChild to crash.
        this->addChild(label, 100);

        m_fields->m_statsLabel = label;
        return true;
    }

    void update(float dt) {
#ifdef GEODE_IS_ANDROID
        // Physics dt stabilizer. Replaces the removed CCScheduler hook.
        // Caps runaway dt from lag spikes without changing TPS.
        constexpr float kMaxDt = 1.0f / 30.0f;
        constexpr float kMinDt = 1.0f / 1000.0f;
        dt = (dt > kMaxDt) ? kMaxDt : (dt < kMinDt ? kMinDt : dt);
#endif

        // Feed FrameSmoother with this frame's (clamped) delta.
        // Replaces the removed CCDirector::drawScene hook.
        FrameSmoother::update(dt);

        PlayLayer::update(dt);

        // ---- Fast path: stats hidden ----
        if (!PerformanceCache::s_showStats) [[likely]] {
            CCLabelBMFont* lbl = m_fields->m_statsLabel;
            if (lbl && lbl->isVisible()) lbl->setVisible(false);
            return;
        }

        CCLabelBMFont* label = m_fields->m_statsLabel;
        if (!label || !m_player1) [[unlikely]] return;

        if (!label->isVisible()) label->setVisible(true);

        const float posX = m_player1->m_position.x;
        const float posY = m_player1->m_position.y;

        // Dirty check — skip string rebuild + GPU upload when player is still.
        if (posX == m_fields->m_lastX && posY == m_fields->m_lastY) [[likely]] return;
        m_fields->m_lastX = posX;
        m_fields->m_lastY = posY;

        // Stack-only formatting — 0 heap allocations
        char  buf[128];
        char* p   = buf;
        char* end = buf + sizeof(buf) - 1;

        auto lit = [&](const char* s, size_t n) noexcept {
            std::memcpy(p, s, n);
            p += n;
        };

        { constexpr char t[] = "X: ";    lit(t, sizeof(t) - 1); }
        p = std::to_chars(p, end, posX, std::chars_format::fixed, 2).ptr;

        { constexpr char t[] = " | Y: "; lit(t, sizeof(t) - 1); }
        p = std::to_chars(p, end, posY, std::chars_format::fixed, 2).ptr;

        if (PerformanceCache::s_showFps) {
            { constexpr char t[] = " | FPS: "; lit(t, sizeof(t) - 1); }
            p = std::to_chars(p, end, FrameSmoother::getAverageFPS()).ptr;
        }

        *p = '\0';
        label->setString(buf);
    }
};
