#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <type_traits>
#include "LockFreeQueue.hpp"

struct alignas(32) InputEvent {
    PlayerButton button;
    bool         isPress;
    bool         isPlayer2;
    uint8_t      tsSource;
    uint8_t      _pad;
    int64_t      eventTimeNs;
    int64_t      captureTimeNs;
};

static_assert(std::is_trivially_copyable_v<InputEvent>,
              "InputEvent must be trivially copyable");

alignas(64) inline LockFreeQueue<InputEvent, 256> g_inputQueue{};
