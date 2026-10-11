#pragma once

#include "timer_logic.h"

namespace ouija_translation {

void Initialize();
void Shutdown();
void SyncSettings();
void ClearSnapshot();
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace ouija_translation
