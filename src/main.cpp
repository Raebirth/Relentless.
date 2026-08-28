#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <charconv>
#include <cstring>

// Windows only: 1 ms OS timer resolution for tighter sleep/yield precision.
// Does NOT change game speed or physics. No equivalent needed on Android.
#ifdef GEODE_IS_WINDOWS
#  include <windows.h>
#  include <timeapi.h>
#endif

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Setting cache — read once, never inside the per-frame hot path.
// ---------------------------------------------------------------------------
namespace PerformanceCache {
    inline bool s_showStats = false;
    inline bool s_showFps   = false;
}

// ---------------------------------------------------------------------------
// Mod lifecycle
// ---------------------------------------------------------------------------
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
}

// ---------------------------------------------------------------------------
// Play layer — stats label + FPS counter.
// Only hooks PlayLayer (same target class as v1.0.0 — confirmed safe).
// ---------------------------------------------------------------------------
class $modify(OptimizedPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_statsLabel = nullptr;
        float          m_lastX      = -9999.0f;
        float          m_lastY      = -9999.0f;
        float          m_smoothFps  = 60.0f;   // simple EMA, no external header
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
        this->addChild(label, 100);  // same as v1.0.0 — no m_uiLayer

        m_fields->m_statsLabel = label;
        return true;
    }

    void update(float dt) {
        // Track FPS BEFORE clamping so the display shows actual frame rate.
        if (dt > 0.0001f) {
            float instFps = 1.0f / dt;
            // EMA with α=0.1: smooth without any allocations or atomics.
            m_fields->m_smoothFps = m_fields->m_smoothFps * 0.9f + instFps * 0.1f;
        }

#ifdef GEODE_IS_ANDROID
        // Prevent physics "pop" after a lag spike (GC pause, shader compile, etc.).
        // Clamps the time step so a single slow frame doesn't simulate
        // 8 normal frames of physics in one shot.
        // This is NOT a TPS change — it only guards against extreme spikes.
        constexpr float kMaxDt = 1.0f / 30.0f;
        constexpr float kMinDt = 1.0f / 1000.0f;
        dt = (dt > kMaxDt) ? kMaxDt : (dt < kMinDt ? kMinDt : dt);
#endif

        PlayLayer::update(dt);

        // Fast path — stats off
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

        // Skip rebuild when the player hasn't moved.
        if (posX == m_fields->m_lastX && posY == m_fields->m_lastY) [[likely]] return;
        m_fields->m_lastX = posX;
        m_fields->m_lastY = posY;

        // Zero-allocation stack formatting.
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
            auto fps = static_cast<uint32_t>(m_fields->m_smoothFps + 0.5f);
            p = std::to_chars(p, end, fps).ptr;
        }

        *p = '\0';
        label->setString(buf);
    }
};
