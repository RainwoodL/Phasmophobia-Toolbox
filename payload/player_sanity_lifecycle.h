#pragma once

namespace player_sanity_lifecycle {

using ResetCallback = void (*)();

void Initialize(ResetCallback resetCallback);
void Shutdown();

} // namespace player_sanity_lifecycle
