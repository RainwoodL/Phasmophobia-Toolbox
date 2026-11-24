#include <windows.h>

// Windows graphics drivers and the Direct3D 12 Agility SDK query these
// exports on the main executable. The installed game supplies the SDK files.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) extern const UINT D3D12SDKVersion = 611;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
}
