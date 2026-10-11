#pragma once

#include "timer_logic.h"

namespace crucifix_timer {

struct GameTimeValue {
    bool valid;
    float seconds;
};

using GameTimeProvider = GameTimeValue (*)();

void Initialize(GameTimeProvider gameTimeProvider);
void Shutdown();
void ResetEvent();
void UpdateHotkeys(bool gameForeground);
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace crucifix_timer
