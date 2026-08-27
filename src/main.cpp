#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <charconv>
#include <chrono>
#include <cstring>

#include "InputQueue.hpp"
#include "FrameSmoother.hpp"

using namespace geode::prelude;

// ============================================================================
// OS TIMER RESOLUTION — Windows only.
//
//  timeBeginPeriod(1) drops the Windows scheduler quantum from the default
//  ~15.6 ms to 1 ms. This is a system-wide setting that reverts automatically
//  when the process exits. It is NOT a game speed change — it only affects
//  how precisely the OS can wake up the process after a sleep/yield, reducing
//  systemic input latency by up to ~14 ms on affected hardware.
//
//  This technique is used by virtually every competitive PC game and many
//  Geode mods. It does not violate Demon List or any rating list rules.
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
    log::info("[Repentless] Windows timer resolution set to 1 ms.");
#endif
}

$on_mod(Unloaded) {
#ifdef GEODE_IS_WINDOWS
    timeEndPeriod(1);
    log::info("[Repentless] Windows timer resolution restored.");
#endif
}

// ============================================================================
// FRAME-TIMING HOOK — feeds FrameSmoother for accurate FPS display.
//
//  Hooks CCDirector::drawScene which fires exactly once per rendered frame,
//  before any game logic or rendering runs. getDeltaTime() returns the raw
//  elapsed seconds since the previous frame — fed into the EMA smoother.
//  Physics dt is NOT touched or modified in any way.
// ============================================================================
class $modify(SmoothDirector, CCDirector) {
    void drawScene() {
        FrameSmoother::update(this->getDeltaTime());
        CCDirector::drawScene();
    }
};

// ============================================================================
// INPUT DISPATCH HOOK — earliest possible game-side interception.
//
//  GJBaseGameLayer::handleButton is the single function the game calls for
//  every player input (keyboard, mobile tap, controller). By hooking it here
//  — above PlayerObject — we capture the event as early as the game engine
//  delivers it and enqueue it into the lock-free queue with a high-resolution
//  timestamp before passing control to the original handler.
//
//  This replaces the old FastPlayerObject approach which was a no-op:
//  pushButton / releaseButton just called super and added an extra vtable
//  hop on every input with zero benefit.
//
//  Queue: g_inputQueue (LockFreeSPSCQueue<InputEvent, 256>)
//    Producer: this hook (game/input thread)
//    Consumer: any future frame-interpolation or analytics system
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
//
//  Key optimizations vs. the previous version:
//    1. Stats label is a child of m_uiLayer (screen-space) instead of
//       PlayLayer itself (world-space). This means the label is immune to
//       camera shake and no per-frame matrix transform is applied to it.
//    2. Dirty check: label string is only re-written when the player moves.
//    3. All string formatting uses stack buffers + std::to_chars: 0 heap
//       allocations in the entire update path.
//    4. Optional FPS counter appended to the same stack buffer at no
//       extra allocation cost.
//    5. [[likely]] / [[unlikely]] attributes guide branch predictor.
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

        // ----------------------------------------------------------------
        // Stack-only string formatting — 0 heap allocations
        //
        //  Worst-case output:  "X: -99999.99 | Y: -99999.99 | FPS: 9999\0"
        //  That is 45 chars — safely within the 128-byte buffer.
        // ----------------------------------------------------------------
        char  buf[128];
        char* p   = buf;
        char* end = buf + sizeof(buf) - 1; // reserve 1 byte for null terminator

        // Helper: copy a string literal into the buffer
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
