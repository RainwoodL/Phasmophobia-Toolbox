#pragma once

#include "timer_logic.h"

namespace crosshair_info {

void Initialize();
void Shutdown();
void SyncSettings();
void ClearSnapshot();
void OnGameThreadUpdate(void* pcPropGrab);
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();
void RequestResetPosition();

} // namespace crosshair_info
