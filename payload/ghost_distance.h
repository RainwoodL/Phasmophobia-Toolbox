#pragma once

#include "timer_logic.h"

namespace ghost_distance {

void Initialize();
void Shutdown();
void ClearSnapshot();
void SyncSettings();
void OnGameThreadUpdate();
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace ghost_distance
