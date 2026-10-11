#pragma once

#include "inventory_resources.h"

#include <d3d11.h>

#include <cstddef>
#include <cstdint>

namespace inventory_textures {

struct TextureView {
    ID3D11ShaderResourceView* shaderResourceView = nullptr;
    unsigned width = 0;
    unsigned height = 0;
};

bool CreateFromMemory(ID3D11Device* device, const std::uint8_t* data,
                      std::size_t size, TextureView& result);
void Release(TextureView& texture);

bool Initialize(ID3D11Device* device);
const TextureView* Get(inventory_resources::InventoryItemVisual visual);
void Shutdown();

} // namespace inventory_textures
