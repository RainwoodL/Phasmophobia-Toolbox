#include "inventory_textures.h"

#include "inventory_resources.h"
#include "log.h"

#include <wincodec.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace inventory_textures {
namespace {

std::array<TextureView, inventory_resources::kInventoryPngCount> g_textures{};

bool DecodeRgba(IWICImagingFactory* factory, const std::uint8_t* data, std::size_t size,
                std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) {
    if (!data || size == 0 || size > MAXDWORD) return false;

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool success = false;
    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data)), static_cast<DWORD>(size))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0 &&
        width <= 4096 && height <= 4096) {
        const std::size_t stride = static_cast<std::size_t>(width) * 4;
        const std::size_t pixelBytes = stride * static_cast<std::size_t>(height);
        if (stride <= (std::numeric_limits<UINT>::max)() &&
            pixelBytes <= (std::numeric_limits<UINT>::max)()) {
            pixels.resize(pixelBytes);
            success = SUCCEEDED(converter->CopyPixels(
                nullptr, static_cast<UINT>(stride), static_cast<UINT>(pixelBytes), pixels.data()));
        }
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    return success;
}

bool CreateTexture(ID3D11Device* device, IWICImagingFactory* factory,
                   const std::uint8_t* data, std::size_t size, TextureView& result) {
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!DecodeRgba(factory, data, size, pixels, width, height)) return false;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    const HRESULT textureResult = device->CreateTexture2D(&description, &initial, &texture);
    if (FAILED(textureResult) || !texture) return false;
    const HRESULT viewResult = device->CreateShaderResourceView(texture, nullptr, &result.shaderResourceView);
    texture->Release();
    if (FAILED(viewResult) || !result.shaderResourceView) return false;
    result.width = width;
    result.height = height;
    return true;
}

} // namespace

void Release(TextureView& texture) {
    if (texture.shaderResourceView) texture.shaderResourceView->Release();
    texture = {};
}

bool CreateFromMemory(ID3D11Device* device, const std::uint8_t* data,
                      std::size_t size, TextureView& result) {
    Release(result);
    if (!device || !data || size == 0) return false;
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = initializeResult == S_OK || initializeResult == S_FALSE;
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) return false;
    IWICImagingFactory* factory = nullptr;
    const HRESULT factoryResult = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    const bool success = SUCCEEDED(factoryResult) && factory &&
                         CreateTexture(device, factory, data, size, result);
    if (factory) factory->Release();
    if (uninitialize) CoUninitialize();
    if (!success) Release(result);
    return success;
}

bool Initialize(ID3D11Device* device) {
    Shutdown();
    if (!device) return false;
    for (int index = 0; index < inventory_resources::kInventoryPngCount; ++index) {
        const inventory_resources::InventoryItemVisual visual{
            index / inventory_resources::kTierCount,
            index % inventory_resources::kTierCount
        };
        const auto* png = inventory_resources::GetEmbeddedPng(visual);
        if (!png || !CreateFromMemory(device, png->data, png->size,
                                      g_textures[static_cast<std::size_t>(index)])) {
            LOG("InventoryTextures: failed to create %s",
                png && png->sourceName ? png->sourceName : "unknown resource");
        }
    }
    return true;
}

const TextureView* Get(inventory_resources::InventoryItemVisual visual) {
    const int index = inventory_resources::ResourceIndex(visual);
    if (index == inventory_resources::kInvalidResourceIndex) return nullptr;
    const auto& texture = g_textures[static_cast<std::size_t>(index)];
    return texture.shaderResourceView ? &texture : nullptr;
}

void Shutdown() {
    for (auto& texture : g_textures) Release(texture);
}

} // namespace inventory_textures
