#pragma once

#include "timer_logic.h"

namespace game_time {

struct GameTimeSnapshot {
    bool valid;
    float seconds;
};

void Initialize();
void Shutdown();
GameTimeSnapshot ReadSnapshot();
timer_logic::Bounds LastWindowBounds();
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace game_time
