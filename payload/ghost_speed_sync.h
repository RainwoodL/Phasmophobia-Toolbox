#pragma once

namespace ghost_speed_sync {

void Initialize();
void Shutdown();

// Called for every game-thread speed sample. Sending is independent of the UI toggle.
// Returns true when this client is the current room master, even when the sample is rate-limited.
bool PublishLocalSpeed(bool valid, float speed);

// Returns a recent speed received from the current master client.
bool ReadRemoteSpeed(float& speed);

} // namespace ghost_speed_sync
