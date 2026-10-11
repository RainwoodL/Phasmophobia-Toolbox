#include "inventory_textures.h"

#include <cstdint>

#define CHECK(condition) do { if (!(condition)) return 1; } while (false)

int main() {
    inventory_textures::TextureView texture{};
    constexpr std::uint8_t invalidPng[]{0x89, 0x50, 0x4E, 0x47};
    CHECK(!inventory_textures::CreateFromMemory(nullptr, invalidPng, sizeof(invalidPng), texture));
    CHECK(!texture.shaderResourceView && texture.width == 0 && texture.height == 0);
    CHECK(!inventory_textures::CreateFromMemory(nullptr, nullptr, 0, texture));
    inventory_textures::Release(texture);
    CHECK(!texture.shaderResourceView && texture.width == 0 && texture.height == 0);
    return 0;
}
