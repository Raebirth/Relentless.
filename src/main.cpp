#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include "LockFreeQueue.hpp"
#include <charconv>
#include <cstring>

using namespace geode::prelude;

namespace PerformanceCache {
    inline std::atomic<bool> s_showStats{false};
}

$execute {
    listenForSettingChanges("show-stats", +[](bool value) {
        PerformanceCache::s_showStats.store(value, std::memory_order_relaxed);
    });
}

class $modify(OptimizedPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_statsLabel = nullptr;
        float m_lastX = -99999.0f;
        float m_lastY = -99999.0f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        PerformanceCache::s_showStats.store(
            Mod::get()->getSettingValue<bool>("show-stats"), 
            std::memory_order_relaxed
        );

        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("stats-label"_spr);
        label->setPosition({ 12.0f, 12.0f });
        label->setAnchorPoint({ 0.0f, 0.0f });
        label->setScale(0.35f);
        this->addChild(label, 100);

        label->setVisible(PerformanceCache::s_showStats.load(std::memory_order_relaxed));
        m_fields->m_statsLabel = label;

        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        const bool showStats = PerformanceCache::s_showStats.load(std::memory_order_relaxed);
        CCLabelBMFont* label = m_fields->m_statsLabel;

        if (!showStats) [[likely]] {
            if (label && label->isVisible()) {
                label->setVisible(false);
            }
            return;
        }

        if (!label || !m_player1) [[unlikely]] {
            return;
        }

        if (!label->isVisible()) {
            label->setVisible(true);
        }

        const float posX = m_player1->m_position.x;
        const float posY = m_player1->m_position.y;

        // Dirty check para no forzar re-renderizado del sprite si la posición no varió
        if (posX == m_fields->m_lastX && posY == m_fields->m_lastY) [[likely]] {
            return;
        }
        m_fields->m_lastX = posX;
        m_fields->m_lastY = posY;

        // Formateo seguro en stack (cero llamadas a malloc / free)
        char stackBuffer[64];
        char* ptr = stackBuffer;
        char* const endPtr = stackBuffer + sizeof(stackBuffer) - 1;

        constexpr char prefixX[] = "X: ";
        std::memcpy(ptr, prefixX, sizeof(prefixX) - 1);
        ptr += sizeof(prefixX) - 1;

        auto resX = std::to_chars(ptr, endPtr - 12, posX, std::chars_format::fixed, 2);
        ptr = resX.ptr;

        constexpr char separator[] = " | Y: ";
        std::memcpy(ptr, separator, sizeof(separator) - 1);
        ptr += sizeof(separator) - 1;

        auto resY = std::to_chars(ptr, endPtr, posY, std::chars_format::fixed, 2);
        ptr = resY.ptr;

        *ptr = '\0';

        label->setString(stackBuffer);
    }
};

// Hook de entrada directo sin overhead de despachadores lentos
class $modify(FastPlayerObject, PlayerObject) {
    void pushButton(PlayerButton btn) {
        PlayerObject::pushButton(btn);
    }

    void releaseButton(PlayerButton btn) {
        PlayerObject::releaseButton(btn);
    }
};
