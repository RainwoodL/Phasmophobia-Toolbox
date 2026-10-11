#pragma once

namespace game_update_dispatcher {

using CrosshairCallback = void (*)(void* pcPropGrab);
using NicknamesCallback = void (*)();
using InventoryCallback = void (*)(void* pcPropGrab);
using GhostSpeedCallback = void (*)();
using GhostDistanceCallback = void (*)();
using SanityCallback = void (*)();

bool Initialize(CrosshairCallback crosshairCallback, NicknamesCallback nicknamesCallback,
                InventoryCallback inventoryCallback, GhostSpeedCallback ghostSpeedCallback,
                GhostDistanceCallback ghostDistanceCallback, SanityCallback sanityCallback);
void Shutdown();
// Called on the render thread; publishes the sampling rate to the game thread.
void SyncSettings();

} // namespace game_update_dispatcher
