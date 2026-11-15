#pragma once

#include <atomic>

namespace GlobalState {
    alignas(64) inline std::atomic<bool> g_inLevel{false};
}
