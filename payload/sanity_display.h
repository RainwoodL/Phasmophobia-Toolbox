#pragma once

#include "timer_logic.h"

namespace sanity_display {

void Initialize();
void Shutdown();
void SyncSettings();
void ClearSnapshot();
void OnGameThreadUpdate();
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace sanity_display
