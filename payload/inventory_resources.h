#pragma once

#include <cstddef>
#include <cstdint>

namespace inventory_resources {

inline constexpr int kEquipmentTypeCount = 21;
inline constexpr int kTierCount = 3;
inline constexpr int kInventoryPngCount = kEquipmentTypeCount * kTierCount;
inline constexpr int kInvalidResourceIndex = -1;

struct InventoryItemVisual {
    int equipmentType = -1;
    int tier = -1;
};

struct EmbeddedPng {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    const char* sourceName = nullptr;
};

bool IsValidVisual(InventoryItemVisual visual);
int ResourceIndex(InventoryItemVisual visual);
const EmbeddedPng* GetEmbeddedPng(InventoryItemVisual visual);
const EmbeddedPng* GetSponsorPng();

} // namespace inventory_resources
