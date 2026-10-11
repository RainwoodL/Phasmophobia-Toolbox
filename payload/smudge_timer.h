#pragma once

#include "timer_logic.h"

namespace smudge_timer {

struct GameTimeValue {
    bool valid;
    float seconds;
};
using GameTimeProvider = GameTimeValue (*)();

void Initialize(GameTimeProvider provider);
void Shutdown();
void ResetEvent();
void UpdateHotkeys(bool gameForeground);
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace smudge_timer
