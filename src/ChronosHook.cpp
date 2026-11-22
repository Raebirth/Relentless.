#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

#ifdef GEODE_IS_ANDROID
#include <time.h>
#endif

#include "InputQueue.hpp"
#include "GlobalState.hpp"

using namespace geode::prelude;

namespace {

#ifdef GEODE_IS_ANDROID
[[nodiscard]] __attribute__((always_inline))
inline int64_t chronosNowNs() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL
         + static_cast<int64_t>(ts.tv_nsec);
}
#else
[[nodiscard]] inline int64_t chronosNowNs() noexcept {
    return 0;
}
#endif

struct QueuedFrameEvent {
    InputEvent ev;
    double     normU;
    bool       dispatched;
};

struct StepSplitEvent {
    InputEvent ev;
    double     alpha;
};

constexpr int    kMaxFrameEvents = 16;
constexpr double kMinSliceFrac   = 0.015;

inline void dispatchChronosButton(GJBaseGameLayer* layer, const InputEvent& ev) noexcept {
    const bool twoPlayer = (layer->m_levelSettings != nullptr)
                         && layer->m_levelSettings->m_twoPlayerMode;
    const bool isPlayer1 = twoPlayer ? !ev.isPlayer2 : true;
    layer->handleButton(ev.isPress, static_cast<int>(ev.button), isPlayer1);
}

} // anonymous namespace

class $modify(ChronosPhysicsHook, GJBaseGameLayer) {
    struct Fields {
        int64_t          m_lastPhysicsFrameNs = 0;
        float            m_modDt              = 0.0f;
        int              m_stepIndex          = 0;
        int              m_totalSteps         = 1;
        int              m_frameEventCount    = 0;
        bool             m_inSubStep          = false;
        QueuedFrameEvent m_frameEvents[kMaxFrameEvents]{};
    };

    float getModifiedDelta(float dt) {
        const float modDt = GJBaseGameLayer::getModifiedDelta(dt);

        if (!PerformanceCache::s_chronosEnabled) [[unlikely]] {
            return modDt;
        }
        if (!GlobalState::g_inLevel.load(std::memory_order_acquire)) [[unlikely]] {
            return modDt;
        }
        if (static_cast<GJBaseGameLayer*>(this) != static_cast<GJBaseGameLayer*>(PlayLayer::get())) [[unlikely]] {
            return modDt;
        }

        auto* f = m_fields.self();
        f->m_stepIndex       = 0;
        f->m_totalSteps      = 1;
        f->m_frameEventCount = 0;
        f->m_modDt           = modDt;

        if (modDt <= 0.0f) [[unlikely]] {
            return modDt;
        }

        const int64_t nowNs  = chronosNowNs();
        const int64_t prevNs = f->m_lastPhysicsFrameNs;
        const int64_t spanNs = (prevNs > 0) ? (nowNs - prevNs) : 0;
        f->m_lastPhysicsFrameNs = nowNs;

        const bool validSpan = (spanNs > 500000LL) && (spanNs < 50000000LL);

        InputEvent ev;
        while (f->m_frameEventCount < kMaxFrameEvents && g_inputQueue.try_pop(ev)) [[unlikely]] {
            double u = 0.0;

            if (ev.tsSource >= 1 && ev.tsSource <= 5 && validSpan) {
                const double rawU = static_cast<double>(ev.eventTimeNs - prevNs)
                                  / static_cast<double>(spanNs);
                u = std::clamp(rawU, 0.0, 1.0);
                ChronosTelemetry::lastLatencyMs =
                    static_cast<float>(nowNs - ev.eventTimeNs) * 1.0e-6f;
            } else {
                u = 0.0;
                ChronosTelemetry::lastLatencyMs = 0.0f;
            }

            f->m_frameEvents[f->m_frameEventCount++] = { ev, u, false };
        }

        return modDt;
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto* f = m_fields.self();

        if (f->m_inSubStep
            || !PerformanceCache::s_chronosEnabled
            || f->m_frameEventCount == 0
            || !GlobalState::g_inLevel.load(std::memory_order_acquire)
            || static_cast<GJBaseGameLayer*>(this) != static_cast<GJBaseGameLayer*>(PlayLayer::get())) [[likely]] {
            GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            return;
        }

        if (f->m_stepIndex == 0) {
            if (dt > 1.0e-6f && f->m_modDt > 0.0f) {
                const int calcSteps = static_cast<int>(
                    std::llround(static_cast<double>(f->m_modDt) / static_cast<double>(dt))
                );
                f->m_totalSteps = std::max(1, calcSteps);
            } else {
                f->m_totalSteps = 1;
            }
            ChronosTelemetry::stepsInLastFrame = static_cast<uint32_t>(f->m_totalSteps);
        }

        const int  curStep    = f->m_stepIndex++;
        const int  totalSteps = std::max(1, f->m_totalSteps);
        const bool finalStep  = isLastTick || (curStep >= totalSteps - 1);

        StepSplitEvent stepEvs[kMaxFrameEvents];
        int stepEvCount = 0;
        const int64_t grid = PerformanceCache::s_chronosGrid;

        for (int i = 0; i < f->m_frameEventCount; ++i) {
            auto& item = f->m_frameEvents[i];
            if (item.dispatched) continue;

            const double scaled = item.normU * static_cast<double>(totalSteps);
            int targetStep = std::min(totalSteps - 1, static_cast<int>(scaled));

            if (targetStep == curStep || finalStep) {
                double alpha = (targetStep == curStep)
                             ? std::clamp(scaled - static_cast<double>(curStep), 0.0, 1.0)
                             : (targetStep < curStep ? 0.0 : 1.0);

                if (grid > 0) {
                    const double g = static_cast<double>(grid);
                    alpha = std::clamp(std::round(alpha * g) / g, 0.0, 1.0);
                }

                stepEvs[stepEvCount++] = { item.ev, alpha };
                item.dispatched = true;
            }
        }

        if (stepEvCount == 0) {
            GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            return;
        }

        for (int i = 1; i < stepEvCount; ++i) {
            const StepSplitEvent key = stepEvs[i];
            int j = i - 1;
            while (j >= 0 && stepEvs[j].alpha > key.alpha) {
                stepEvs[j + 1] = stepEvs[j];
                --j;
            }
            stepEvs[j + 1] = key;
        }

        f->m_inSubStep = true;

        double   cursor    = 0.0;
        uint32_t slicesRun = 0;
        InputEvent tailEvs[kMaxFrameEvents];
        int tailCount = 0;

        for (int i = 0; i < stepEvCount; ++i) {
            const double a = stepEvs[i].alpha;

            if (a >= 1.0 - kMinSliceFrac) {
                tailEvs[tailCount++] = stepEvs[i].ev;
                continue;
            }

            const double seg = a - cursor;
            if (seg >= kMinSliceFrac) {
                GJBaseGameLayer::processCommands(
                    static_cast<float>(static_cast<double>(dt) * seg),
                    /*isHalfTick=*/true,
                    /*isLastTick=*/false
                );
                cursor = a;
                ++slicesRun;
            }

            dispatchChronosButton(this, stepEvs[i].ev);
            ChronosTelemetry::lastAlpha = static_cast<float>(a);
            ++ChronosTelemetry::totalSplitsSession;
        }

        const double rem = 1.0 - cursor;
        GJBaseGameLayer::processCommands(
            static_cast<float>(static_cast<double>(dt) * rem),
            isHalfTick,
            isLastTick
        );
        ++slicesRun;

        for (int i = 0; i < tailCount; ++i) {
            dispatchChronosButton(this, tailEvs[i]);
            ChronosTelemetry::lastAlpha = 1.0f;
            ++ChronosTelemetry::totalSplitsSession;
        }

        ChronosTelemetry::lastSplitCount = slicesRun;
        f->m_inSubStep = false;
    }
};
