#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <charconv>
#include <cstring>

#include "InputQueue.hpp"
#include "GlobalState.hpp"

using namespace geode::prelude;

// ============================================================================
// GameLoopHook.cpp  —  The Consumer
//
// This file owns ALL PlayLayer hooks.  main.cpp is reduced to settings
// and lifecycle only.
//
// CONSUMER RESPONSIBILITIES:
//
//   1. PlayLayer::init  — set g_inLevel = true, create stats label.
//   2. PlayLayer::update — drain g_inputQueue → call pushButton/releaseButton,
//                          apply dt clamping, update stats display.
//   3. PlayLayer::onQuit — set g_inLevel = false, flush queue.
//
// WHY DRAIN AT THE TOP OF update() AND NOT BOTTOM?
// ──────────────────────────────────────────────────
// PlayLayer::update(float dt) processes physics INSIDE the call to
// PlayLayer::update(dt).  If we drain the queue BEFORE calling the original,
// any jump that arrived during the previous frame is fed to the physics
// accumulator at the EARLIEST possible point in the current tick — before
// any position integration, before collision detection, before the renderer
// prepares draw calls.  Draining AFTER would defer the jump's effect one
// additional physics step.
//
// SETTINGS CACHE:
// ────────────────
// PerformanceCache (defined in main.cpp as an inline namespace) is visible
// here because both translation units link the same symbols.  We read
// s_showStats / s_showFps from the cache, never from Mod::get() on the
// hot path.
// ============================================================================

// Forward-declare the setting cache from main.cpp (same shared library,
// no ODR violation with inline namespace).
namespace PerformanceCache {
    extern bool s_showStats;
    extern bool s_showFps;
}

class $modify(GameLoopConsumer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_statsLabel = nullptr;
        float          m_lastX      = -9999.0f;
        float          m_lastY      = -9999.0f;
        float          m_smoothFps  = 60.0f;
    };

    // ── init ────────────────────────────────────────────────────────────────
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        // Signal DispatcherHook to switch to queue path.
        GlobalState::g_inLevel.store(true, std::memory_order_release);

        // Build stats label.
        auto* label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("stats-label"_spr);
        label->setAnchorPoint({0.0f, 0.0f});
        label->setScale(0.4f);
        label->setPosition({10.0f, 10.0f});
        label->setVisible(PerformanceCache::s_showStats);
        this->addChild(label, 100);
        m_fields->m_statsLabel = label;

        log::info("[Relentless] Level started — input queue active.");
        return true;
    }

    // ── onQuit ──────────────────────────────────────────────────────────────
    void onQuit() {
        // Return to normal CCEGLView pass-through BEFORE calling original,
        // so any remaining touch events during the quit transition are
        // handled normally (e.g. the end-of-level UI).
        GlobalState::g_inLevel.store(false, std::memory_order_release);

        // Flush any stale events that arrived after the last update().
        InputEvent discard;
        while (g_inputQueue.try_pop(discard)) { /* drop */ }

        log::info("[Relentless] Level ended — input queue flushed.");
        PlayLayer::onQuit();
    }

    // ── update ──────────────────────────────────────────────────────────────
    void update(float dt) {

        // ── 1. FPS tracking (before dt clamping = real elapsed time) ────────
        if (dt > 0.0001f) [[likely]] {
            m_fields->m_smoothFps = m_fields->m_smoothFps * 0.9f
                                  + (1.0f / dt) * 0.1f;
        }

        // ── 2. Android dt stabilizer ─────────────────────────────────────────
        // Cap runaway dt from lag spikes (GC pause, shader warm-up, etc.)
        // This does NOT change TPS — only prevents a single slow frame from
        // simulating many frames of physics in one shot.
#ifdef GEODE_IS_ANDROID
        constexpr float kMaxDt = 1.0f / 30.0f;   // cap at ~33 ms
        constexpr float kMinDt = 1.0f / 1000.0f;  // floor at 1 ms
        dt = (dt > kMaxDt) ? kMaxDt : (dt < kMinDt ? kMinDt : dt);
#endif

        // ── 3. QUEUE DRAIN — dispatch queued inputs before physics tick ──────
        //
        // We call pushButton / releaseButton directly on the PlayerObject
        // rather than going through GJBaseGameLayer::handleButton.  This is:
        //   A. Confirmed safe (PlayerObject hooks worked in v1.0.0).
        //   B. One vtable hop shorter (skips GJBaseGameLayer → PlayerObject).
        //   C. Avoids any risk from GJBaseGameLayer::handleButton being
        //      unresolved in Geode's Android arm64 bindings.
        {
            InputEvent ev;
            while (g_inputQueue.try_pop(ev)) [[unlikely]] {
                // Select the correct PlayerObject based on screen half.
                PlayerObject* target = ev.isPlayer2 ? m_player2 : m_player1;
                if (!target) [[unlikely]] continue;

                if (ev.isPress) {
                    target->pushButton(ev.button);
                } else {
                    target->releaseButton(ev.button);
                }
            }
        }

        // ── 4. Forward to original PlayLayer physics + rendering ─────────────
        PlayLayer::update(dt);

        // ── 5. Stats overlay ─────────────────────────────────────────────────
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

        // Dirty check — skip string rebuild when the player is stationary.
        if (posX == m_fields->m_lastX && posY == m_fields->m_lastY) [[likely]] return;
        m_fields->m_lastX = posX;
        m_fields->m_lastY = posY;

        // Zero-allocation stack formatting.  Buffer sized for:
        // "X: -99999.99 | Y: -99999.99 | FPS: 9999 | Q: 256\0" (60 chars max)
        char  buf[96];
        char* p   = buf;
        char* end = buf + sizeof(buf) - 1;

        auto lit = [&](const char* s, std::size_t n) noexcept {
            std::memcpy(p, s, n); p += n;
        };

        { constexpr char t[] = "X: ";    lit(t, sizeof(t)-1); }
        p = std::to_chars(p, end, posX, std::chars_format::fixed, 2).ptr;
        { constexpr char t[] = " | Y: "; lit(t, sizeof(t)-1); }
        p = std::to_chars(p, end, posY, std::chars_format::fixed, 2).ptr;

        if (PerformanceCache::s_showFps) {
            { constexpr char t[] = " | FPS: "; lit(t, sizeof(t)-1); }
            p = std::to_chars(p, end,
                static_cast<uint32_t>(m_fields->m_smoothFps + 0.5f)).ptr;
            // Queue depth diagnostic — shows how many events are buffered.
            { constexpr char t[] = " | Q: "; lit(t, sizeof(t)-1); }
            p = std::to_chars(p, end,
                static_cast<uint32_t>(g_inputQueue.size_approx())).ptr;
        }

        *p = '\0';
        label->setString(buf);
    }
};
