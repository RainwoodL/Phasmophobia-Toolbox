#pragma once

namespace hud_runtime {

enum class Startup { GameStarting, GameAlreadyRunning };

bool Start(Startup startup);

} // namespace hud_runtime
