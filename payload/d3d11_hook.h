#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <functional>

namespace d3d11_hook {

using PresentFn = HRESULT(STDAPICALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDAPICALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

bool Install();
void Uninstall();
IDXGISwapChain* GetSwapChain();
ID3D11Device* GetDevice();
ID3D11DeviceContext* GetContext();
HWND GetWindowHandle();
void SetImguiInputEnabled(bool enabled);
void SetGameInputBlocked(bool blocked);
void SetImguiContextReady(bool ready);
void OnFrame(const std::function<void()>& callback);
void OnBeforeResize(const std::function<void()>& callback);

} // namespace d3d11_hook
