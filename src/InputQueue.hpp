#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include "LockFreeQueue.hpp"

struct alignas(16) InputEvent {
    PlayerButton button;
    bool         isPress;
    bool         isPlayer2;
    uint8_t      _pad[2];
    double       timestampNs;
};

alignas(64) inline LockFreeQueue<InputEvent, 256> g_inputQueue{};
