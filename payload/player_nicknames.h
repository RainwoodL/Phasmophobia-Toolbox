#pragma once

namespace player_nicknames {

void Initialize();
void Shutdown();
void ClearSnapshot();
void SyncSettings();
void OnGameThreadUpdate();
void Draw();
void DrawSettings();

} // namespace player_nicknames
