#pragma once

#include "timer_logic.h"

namespace hunt_timer {

void RequestResetPosition();
void ResetSettings();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace hunt_timer
