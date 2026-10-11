#include "inventory_resources.h"
#include "inventory_png_data.h"

#include <array>

namespace inventory_resources {
namespace {

const std::array<EmbeddedPng, kInventoryPngCount>& Resources() {
    static const std::array<EmbeddedPng, kInventoryPngCount> resources = [] {
        std::array<EmbeddedPng, kInventoryPngCount> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto& source = generated::kInventoryPngs[i];
            result[i] = {source.data, source.size, source.sourceName};
        }
        return result;
    }();
    return resources;
}

} // namespace

bool IsValidVisual(InventoryItemVisual visual) {
    return visual.equipmentType >= 0 && visual.equipmentType < kEquipmentTypeCount &&
           visual.tier >= 0 && visual.tier < kTierCount;
}

int ResourceIndex(InventoryItemVisual visual) {
    return IsValidVisual(visual) ? visual.equipmentType * kTierCount + visual.tier
                                 : kInvalidResourceIndex;
}

const EmbeddedPng* GetEmbeddedPng(InventoryItemVisual visual) {
    const int index = ResourceIndex(visual);
    return index == kInvalidResourceIndex ? nullptr : &Resources()[static_cast<std::size_t>(index)];
}

const EmbeddedPng* GetSponsorPng() {
    static const EmbeddedPng sponsor{
        generated::kSponsorPng.data,
        generated::kSponsorPng.size,
        generated::kSponsorPng.sourceName,
    };
    return &sponsor;
}

} // namespace inventory_resources
