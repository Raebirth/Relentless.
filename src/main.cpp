#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <charconv>
#include <chrono>
#include <cstring>

#include "InputQueue.hpp"
#include "FrameSmoother.hpp"

// Android-specific extreme optimizations (CCEGLView hook, CPU affinity,
// physics dt stabilizer). Compiled only for Android targets.
#ifdef GEODE_IS_ANDROID
#  include "AndroidOptimizations.hpp"
#endif

using namespace geode::prelude;

// ============================================================================
// OS TIMER RESOLUTION — Windows only.
// ============================================================================
#ifdef GEODE_IS_WINDOWS
#  include <windows.h>
#  include <timeapi.h>
#endif

// ============================================================================
// HOT-SETTING CACHE — avoids touching Mod::get() / disk inside the frame loop
// ============================================================================
namespace PerformanceCache {
    inline bool s_showStats = false;
    inline bool s_showFps   = false;
}

// ============================================================================
// MOD LIFECYCLE
// ============================================================================
$on_mod(Loaded) {
    // Seed caches immediately (disk read happens once here, never per-frame)
    PerformanceCache::s_showStats = Mod::get()->getSettingValue<bool>("show-stats");
    PerformanceCache::s_showFps   = Mod::get()->getSettingValue<bool>("show-fps");

    // Keep caches in sync whenever the user changes settings at runtime
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
    // Must run on the main thread — $on_mod(Loaded) guarantees this.
    androidInit();
#endif
}

// ============================================================================
// FRAME-TIMING HOOK — feeds FrameSmoother for accurate FPS display.
// ============================================================================
class $modify(SmoothDirector, CCDirector) {
    void drawScene() {
        FrameSmoother::update(this->getDeltaTime());
        CCDirector::drawScene();
    }
};

// ============================================================================
// INPUT DISPATCH HOOK — earliest possible game-side interception.
// ============================================================================
class $modify(FastGameLayer, GJBaseGameLayer) {
    void handleButton(bool push, int button, bool isPlayer1) {
        // O(1) enqueue — no allocation, no lock, no branch on full (drops silently)
        g_inputQueue.try_push({
            static_cast<PlayerButton>(button),
            push,
            static_cast<double>(
                std::chrono::steady_clock::now().time_since_epoch().count())
        });
        GJBaseGameLayer::handleButton(push, button, isPlayer1);
    }
};

// ============================================================================
// OPTIMIZED PLAY LAYER — zero-allocation stat rendering
// ============================================================================
class $modify(OptimizedPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_statsLabel = nullptr;
        float          m_lastX      = -9999.0f;
        float          m_lastY      = -9999.0f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        // Refresh caches — PlayLayer::init runs on the main thread so this
        // is a one-time disk read, safe here but never in update().
        PerformanceCache::s_showStats = Mod::get()->getSettingValue<bool>("show-stats");
        PerformanceCache::s_showFps   = Mod::get()->getSettingValue<bool>("show-fps");

        auto* label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("stats-label"_spr);
        label->setAnchorPoint({0.0f, 0.0f});
        label->setScale(0.4f);
        label->setVisible(PerformanceCache::s_showStats);

        // Parent to m_uiLayer so the label lives in screen-space.
        // Falls back to PlayLayer itself in case m_uiLayer is somehow null.
        CCLayer* host = m_uiLayer ? m_uiLayer : static_cast<CCLayer*>(this);
        label->setPosition({10.0f, 10.0f});
        host->addChild(label, 100);

        m_fields->m_statsLabel = label;
        return true;
    }

    void update(float dt) {
#ifdef GEODE_IS_ANDROID
        // Physics dt stabilizer — replaces the removed CCScheduler hook.
        constexpr float kMaxDt = 1.0f / 30.0f;
        constexpr float kMinDt = 1.0f / 1000.0f;
        dt = (dt > kMaxDt) ? kMaxDt : (dt < kMinDt ? kMinDt : dt);
#endif
        PlayLayer::update(dt);

        // Fast path: stats are hidden — just ensure the label is invisible.
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

        // Stack-only string formatting — 0 heap allocations
        char  buf[128];
        char* p   = buf;
        char* end = buf + sizeof(buf) - 1; // reserve 1 byte for null terminator

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
