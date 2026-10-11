#pragma once

#include "timer_logic.h"

namespace stamina_display {

struct Snapshot {
    bool valid = false;
    float current = 0.0f;
    float maximum = 3.0f;
    float percentage = 0.0f;
};

void Initialize();
void Shutdown();
Snapshot ReadSnapshot();
void RequestResetPosition();
timer_logic::MovableWindowResult DrawWindow(bool menuVisible, bool altDragActive);
void DrawSettings();

} // namespace stamina_display
