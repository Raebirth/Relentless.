#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <charconv>
#include <cstring>

#ifdef GEODE_IS_ANDROID
#include <time.h>
#include "AndroidOptimizations.hpp"
#endif

#include "InputQueue.hpp"
#include "GlobalState.hpp"

using namespace geode::prelude;

#ifdef GEODE_IS_ANDROID
namespace {
[[nodiscard]] __attribute__((always_inline))
inline int64_t monoNsInt() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL
         + static_cast<int64_t>(ts.tv_nsec);
}
} // anonymous namespace
#endif

class $modify(GameLoopConsumer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_statsLabel = nullptr;
        float          m_lastX      = -9999.0f;
        float          m_lastY      = -9999.0f;
        float          m_smoothFps  = 60.0f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

#ifdef GEODE_IS_ANDROID
        ADPF::initForGLThread();
#endif

        GlobalState::g_inLevel.store(true, std::memory_order_release);

        auto* label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("stats-label"_spr);
        label->setAnchorPoint({0.0f, 0.0f});
        label->setScale(0.4f);
        label->setPosition({10.0f, 10.0f});
        label->setVisible(PerformanceCache::s_showStats);
        this->addChild(label, 100);
        m_fields->m_statsLabel = label;

        return true;
    }

    void onQuit() {
        GlobalState::g_inLevel.store(false, std::memory_order_release);

        InputEvent discard;
        while (g_inputQueue.try_pop(discard)) {}

        PlayLayer::onQuit();
    }

    void update(float dt) {
#ifdef GEODE_IS_ANDROID
        const int64_t frameStartNs = monoNsInt();
#endif

        if (dt > 0.0001f) [[likely]] {
            m_fields->m_smoothFps = m_fields->m_smoothFps * 0.9f + (1.0f / dt) * 0.1f;
        }

#ifdef GEODE_IS_ANDROID
        constexpr float kMaxDt = 1.0f / 30.0f;
        constexpr float kMinDt = 1.0f / 1000.0f;
        dt = (dt > kMaxDt) ? kMaxDt : (dt < kMinDt ? kMinDt : dt);
#endif

        {
            InputEvent ev;
            while (g_inputQueue.try_pop(ev)) [[unlikely]] {
                PlayerObject* target = ev.isPlayer2 ? m_player2 : m_player1;
                if (!target) [[unlikely]] continue;

                if (ev.isPress) {
                    target->pushButton(ev.button);
                } else {
                    target->releaseButton(ev.button);
                }
            }
        }

        PlayLayer::update(dt);

#ifdef GEODE_IS_ANDROID
        ADPF::reportFrameWork(monoNsInt() - frameStartNs);
#endif

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

        if (posX == m_fields->m_lastX && posY == m_fields->m_lastY) [[likely]] return;
        m_fields->m_lastX = posX;
        m_fields->m_lastY = posY;

        char  buf[96];
        char* p   = buf;
        char* end = buf + sizeof(buf) - 1;

        auto lit = [&](const char* s, std::size_t n) noexcept {
            std::memcpy(p, s, n); p += n;
        };

        { constexpr char t[] = "X: "; lit(t, sizeof(t)-1); }
        p = std::to_chars(p, end, posX, std::chars_format::fixed, 2).ptr;
        { constexpr char t[] = " | Y: "; lit(t, sizeof(t)-1); }
        p = std::to_chars(p, end, posY, std::chars_format::fixed, 2).ptr;

        if (PerformanceCache::s_showFps) {
            { constexpr char t[] = " | FPS: "; lit(t, sizeof(t)-1); }
            p = std::to_chars(p, end, static_cast<uint32_t>(m_fields->m_smoothFps + 0.5f)).ptr;
            { constexpr char t[] = " | Q: "; lit(t, sizeof(t)-1); }
            p = std::to_chars(p, end, static_cast<uint32_t>(g_inputQueue.size_approx())).ptr;
        }

        *p = '\0';
        label->setString(buf);
    }
};
