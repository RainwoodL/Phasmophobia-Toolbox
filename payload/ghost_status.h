#pragma once

#include "timer_logic.h"

#include <cstdint>

namespace ghost_status {

struct GameTimeValue {
    bool valid;
    float seconds;
};

using GameTimeProvider = GameTimeValue (*)();

struct HuntTimingSnapshot {
    std::uint64_t durationSeconds = 0;
};

void Initialize(GameTimeProvider provider);
void Shutdown();
void Update();
void ResetSession();
bool IsHunting();
HuntTimingSnapshot ReadHuntTimingSnapshot();
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace ghost_status
