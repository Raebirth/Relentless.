#include <Geode/Geode.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCTextureAtlas.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <algorithm>
#include <cstdint>

#ifdef GEODE_IS_ANDROID
#include <time.h>
#endif

#include "GlobalState.hpp"

using namespace geode::prelude;

namespace {

[[nodiscard]] __attribute__((always_inline))
inline uint64_t readArm64Counter() noexcept {
#if defined(__aarch64__)
    uint64_t val = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#elif defined(GEODE_IS_ANDROID)
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
#else
    return 0;
#endif
}

[[nodiscard]] __attribute__((always_inline))
inline float counterDeltaToMs(uint64_t delta) noexcept {
#if defined(__aarch64__)
    static double s_ticksToMs = 0.0;
    if (s_ticksToMs == 0.0) [[unlikely]] {
        uint64_t freq = 0;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
        s_ticksToMs = (freq > 0) ? (1000.0 / static_cast<double>(freq)) : 0.0;
    }
    return static_cast<float>(static_cast<double>(delta) * s_ticksToMs);
#elif defined(GEODE_IS_ANDROID)
    return static_cast<float>(delta) * 1.0e-6f;
#else
    (void)delta;
    return 0.0f;
#endif
}

} // anonymous namespace

// 1. Descarte inmediato de SpriteBatches vacios (evita push/pop de matrices y transform)
class $modify(RelentlessBatchNode, CCSpriteBatchNode) {
    void visit() {
        if (PerformanceCache::s_emptyBatchCull) [[likely]] {
            if (!this->isVisible()) [[unlikely]] {
                return;
            }
            if (!this->getGrid() && this->getChildrenCount() == 0) {
                auto* atlas = this->getTextureAtlas();
                if (!atlas || atlas->getTotalQuads() == 0) {
                    return;
                }
            }
        }
        CCSpriteBatchNode::visit();
    }
};

// 2. VBO Buffer Orphaning para Adreno TBDR (rompe stalls CPU-GPU en glBufferSubData)
class $modify(RelentlessTextureAtlas, CCTextureAtlas) {
    void drawNumberOfQuads(unsigned int n, unsigned int start) {
        if (n == 0) [[unlikely]] {
            return;
        }

#ifdef GEODE_IS_ANDROID
        if (PerformanceCache::s_vboOrphaning
            && GlobalState::g_inLevel.load(std::memory_order_relaxed)
            && m_bDirty
            && m_pQuads != nullptr
            && m_uCapacity > 0
            && m_pBuffersVBO[0] != 0) [[likely]] {

            const unsigned int uploadQuads = std::min(
                m_uCapacity,
                std::max(m_uTotalQuads, start + n)
            );

            if (uploadQuads > 0) [[likely]] {
                constexpr GLsizeiptr kQuadBytes =
                    static_cast<GLsizeiptr>(sizeof(ccV3F_C4B_T2F_Quad));
                const GLsizeiptr capBytes    = static_cast<GLsizeiptr>(m_uCapacity) * kQuadBytes;
                const GLsizeiptr uploadBytes = static_cast<GLsizeiptr>(uploadQuads) * kQuadBytes;

                glBindBuffer(GL_ARRAY_BUFFER, m_pBuffersVBO[0]);
                glBufferData(GL_ARRAY_BUFFER, capBytes, nullptr, GL_DYNAMIC_DRAW);
                glBufferSubData(GL_ARRAY_BUFFER, 0, uploadBytes, m_pQuads);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                m_bDirty = false;
            }
        }
#endif

        CCTextureAtlas::drawNumberOfQuads(n, start);
    }
};

// 3. Simulacion adaptativa de particulas a 60Hz en pantallas de 185Hz + salto inactivo
class $modify(RelentlessParticleSys, CCParticleSystem) {
    struct Fields {
        float m_accumDt = 0.0f;
        bool  m_primed  = false;
    };

    void update(float dt) {
        if (PerformanceCache::s_particleOpt
            && GlobalState::g_inLevel.load(std::memory_order_relaxed)) [[likely]] {

            // Si esta inactivo, sin particulas vivas y no se auto-destruye, saltar completo
            if (!this->isActive()
                && this->getParticleCount() == 0
                && !this->isAutoRemoveOnFinish()) [[unlikely]] {
                return;
            }

            // Solo desacelera a 60Hz cuando el juego corre por encima de ~75 FPS
            if (dt > 0.0f && dt < 0.0135f) [[likely]] {
                auto* f = m_fields.self();
                if (!f->m_primed) [[unlikely]] {
                    f->m_primed = true;
                    CCParticleSystem::update(dt);
                    return;
                }

                f->m_accumDt += dt;
                constexpr float kParticleStep = 1.0f / 60.0f;
                if (f->m_accumDt < kParticleStep) [[likely]] {
                    return;
                }

                const float stepDt = f->m_accumDt;
                f->m_accumDt = 0.0f;
                CCParticleSystem::update(stepDt);
                return;
            }
        }

        CCParticleSystem::update(dt);
    }
};

// 4. Culling de particulas vacias y emisores agrupados fuera de pantalla (reduce overdraw GPU)
class $modify(RelentlessParticleQuad, CCParticleSystemQuad) {
    void draw() {
        if (this->getParticleCount() == 0) [[unlikely]] {
            return;
        }

        if (PerformanceCache::s_particleOpt
            && GlobalState::g_inLevel.load(std::memory_order_relaxed)
            && this->getPositionType() == kCCPositionTypeGrouped) {

            const CCPoint wp  = this->convertToWorldSpace(CCPointZero);
            const CCSize  win = CCDirector::sharedDirector()->getWinSize();
            constexpr float kMargin = 220.0f;

            if (wp.x < -kMargin || wp.x > win.width + kMargin
                || wp.y < -kMargin || wp.y > win.height + kMargin) [[unlikely]] {
                return;
            }
        }

        CCParticleSystemQuad::draw();
    }
};

// 5. Micro-Profiler ARM64 CNTVCT_EL0 sobre el ciclo completo de escena
class $modify(RelentlessSceneProfiler, CCDirector) {
    void drawScene() {
        if (!PerformanceCache::s_renderProfiler
            || !GlobalState::g_inLevel.load(std::memory_order_relaxed)) [[unlikely]] {
            CCDirector::drawScene();
            return;
        }

        RenderTelemetry::lastUpdateMs = 0.0f;
        const uint64_t t0 = readArm64Counter();

        CCDirector::drawScene();

        const float totalMs  = counterDeltaToMs(readArm64Counter() - t0);
        const float updMs    = RenderTelemetry::lastUpdateMs;
        const float renderMs = (totalMs > updMs) ? (totalMs - updMs) : 0.0f;

        RenderTelemetry::smoothUpdateMs =
            RenderTelemetry::smoothUpdateMs * 0.90f + updMs * 0.10f;
        RenderTelemetry::smoothRenderMs =
            RenderTelemetry::smoothRenderMs * 0.90f + renderMs * 0.10f;
    }
};
