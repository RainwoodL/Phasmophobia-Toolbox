#pragma once

#include "timer_logic.h"

struct ID3D11Device;

namespace inventory_bar {

void Initialize(ID3D11Device* device);
void Shutdown();
void SyncSettings();
void ClearSnapshot();
void OnGameThreadUpdate(void* pcPropGrab);
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();
void RequestResetPosition();

} // namespace inventory_bar
