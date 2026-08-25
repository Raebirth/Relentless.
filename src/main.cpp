#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <charconv>

using namespace geode::prelude;

// ============================================================================
// CACHÉ DE RENDIMIENTO (Evita lecturas en disco durante el frame)
// ============================================================================
namespace PerformanceCache {
    inline bool s_showStats = false;
}

$on_mod(Loaded) {
    // Geode v5 requiere explícitamente el <bool>
    listenForSettingChanges<bool>("show-stats", +[](bool value) {
        PerformanceCache::s_showStats = value;
    });
}

// ============================================================================
// OPTIMIZACIÓN DEL BUCLE PRINCIPAL (Cero Asignaciones Dinámicas)
// ============================================================================
class $modify(OptimizedPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* m_statsLabel = nullptr;
        float m_lastX = -9999.0f;
        float m_lastY = -9999.0f;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        PerformanceCache::s_showStats = Mod::get()->getSettingValue<bool>("show-stats");

        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID("stats-label"_spr);
        label->setPosition({ 10.0f, 10.0f });
        label->setAnchorPoint({ 0.0f, 0.0f });
        label->setScale(0.4f);
        this->addChild(label, 100);

        label->setVisible(PerformanceCache::s_showStats);
        m_fields->m_statsLabel = label;

        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (!PerformanceCache::s_showStats) [[likely]] {
            if (m_fields->m_statsLabel && m_fields->m_statsLabel->isVisible()) {
                m_fields->m_statsLabel->setVisible(false);
            }
            return;
        }

        CCLabelBMFont* label = m_fields->m_statsLabel;
        if (!label || !m_player1) [[unlikely]] {
            return;
        }

        if (!label->isVisible()) {
            label->setVisible(true);
        }

        const float posX = m_player1->m_position.x;
        const float posY = m_player1->m_position.y;

        // Dirty Check: Si no te moviste (ej. estás muerto o en pausa), no reescribe la GPU
        if (posX == m_fields->m_lastX && posY == m_fields->m_lastY) [[likely]] {
            return;
        }
        m_fields->m_lastX = posX;
        m_fields->m_lastY = posY;

        // Buffer en stack: 0% uso de memoria Heap (sin lag spikes por Garbage Collection)
        char stackBuffer[64];
        char* ptr = stackBuffer;

        constexpr char prefixX[] = "X: ";
        std::memcpy(ptr, prefixX, sizeof(prefixX) - 1);
        ptr += sizeof(prefixX) - 1;

        auto res = std::to_chars(ptr, stackBuffer + sizeof(stackBuffer) - 10, posX, std::chars_format::fixed, 2);
        ptr = res.ptr;

        constexpr char separator[] = " | Y: ";
        std::memcpy(ptr, separator, sizeof(separator) - 1);
        ptr += sizeof(separator) - 1;

        res = std::to_chars(ptr, stackBuffer + sizeof(stackBuffer) - 1, posY, std::chars_format::fixed, 2);
        ptr = res.ptr;

        *ptr = '\0'; 

        label->setString(stackBuffer);
    }
};

// ============================================================================
// DESPACHO DE ENTRADA DIRECTO SIN ROMPER LA UI NI EL MODO DUAL
// ============================================================================
class $modify(FastPlayerObject, PlayerObject) {
    void pushButton(PlayerButton btn) {
        // Pasa el toque directo a la lógica nativa del cubo en C++
        PlayerObject::pushButton(btn);
    }

    void releaseButton(PlayerButton btn) {
        PlayerObject::releaseButton(btn);
    }
};
