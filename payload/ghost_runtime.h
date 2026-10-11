#pragma once

#include <cstdint>

namespace ghost_runtime {

struct GameTimeValue {
    bool valid = false;
    float seconds = 0.0f;
};

using GameTimeProvider = GameTimeValue (*)();

struct Snapshot {
    bool valid = false;
    std::uintptr_t instance = 0;
    std::uint64_t generation = 0;
};

void Initialize(GameTimeProvider provider);
void Shutdown();
void Update();
void ObserveInstance(void* instance);
void ResetSession();
Snapshot ReadSnapshot();

} // namespace ghost_runtime
