#include "d3d11_hook.h"
#include "log.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <vector>
#include <mutex>
#include <cstdint>
#include <atomic>

namespace d3d11_hook {

static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static HWND g_hwnd = nullptr;
static WNDPROC g_originalWndProc = nullptr;
static std::vector<std::function<void()>> g_callbacks;
static std::function<void()> g_beforeResizeCallback;
static std::recursive_mutex g_mutex;
static std::recursive_mutex g_imguiMutex;
static std::mutex g_cursorMutex;

static PresentFn g_originalPresent = nullptr;
static ResizeBuffersFn g_originalResizeBuffers = nullptr;
static bool g_installed = false;

static void** g_presentVTable = nullptr;
static HWND g_temporaryWindow = nullptr;

static std::atomic<bool> g_imguiInputEnabled{false};
static std::atomic<bool> g_gameInputBlocked{false};
static std::atomic<bool> g_imguiContextReady{false};
static std::atomic_flag g_rendering = ATOMIC_FLAG_INIT;
static RECT g_savedCursorClip{};
static bool g_savedCursorClipValid = false;
static ULONGLONG g_nextCursorMaintenance = 0;

static bool IsFullVirtualScreenClip(const RECT& clip) {
    const RECT desktop{
        GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    return EqualRect(&clip, &desktop) != FALSE;
}

static void MaintainMenuCursorLocked() {
    RECT clip{};
    if (!GetClipCursor(&clip) || !IsFullVirtualScreenClip(clip)) ClipCursor(nullptr);
}

static void SetMenuCursorActive(bool active) {
    std::lock_guard<std::mutex> lock(g_cursorMutex);
    if (active) {
        const bool wasActive = g_gameInputBlocked.load();
        if (!wasActive) {
            g_savedCursorClipValid = GetClipCursor(&g_savedCursorClip) != FALSE;
        }
        g_gameInputBlocked.store(true);
        const ULONGLONG now = GetTickCount64();
        if (wasActive && now < g_nextCursorMaintenance) return;
        g_nextCursorMaintenance = now + 100;
        MaintainMenuCursorLocked();
        return;
    }

    if (!g_gameInputBlocked.exchange(false)) return;
    if (g_savedCursorClipValid) ClipCursor(&g_savedCursorClip);
    g_savedCursorClipValid = false;
    g_nextCursorMaintenance = 0;
}

static bool IsGameInputMessage(UINT message) {
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           message == WM_INPUT || message == WM_INPUT_DEVICE_CHANGE ||
           message == WM_APPCOMMAND || message == WM_HOTKEY;
}

static bool IsInputReleaseMessage(UINT message) {
    return message == WM_KEYUP || message == WM_SYSKEYUP ||
           message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
           message == WM_MBUTTONUP || message == WM_XBUTTONUP;
}

static LRESULT CALLBACK PassthroughWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_gameInputBlocked.load() && msg == WM_SETCURSOR) {
        std::lock_guard<std::mutex> lock(g_cursorMutex);
        MaintainMenuCursorLocked();
        SetCursor(nullptr);
        return TRUE;
    }
    bool handledByImgui = false;
    {
        std::lock_guard<std::recursive_mutex> imguiLock(g_imguiMutex);
        if (g_imguiInputEnabled.load() && g_imguiContextReady.load())
            handledByImgui = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;
    }
    WNDPROC original = g_originalWndProc;
    if (g_gameInputBlocked.load() && IsGameInputMessage(msg)) {
        // Release events clear keys/buttons that were held when the menu opened.
        if (IsInputReleaseMessage(msg))
            return original ? CallWindowProc(original, hwnd, msg, wParam, lParam) : 0;
        // WM_INPUT requires DefWindowProc cleanup for foreground raw-input packets.
        if (msg == WM_INPUT) return DefWindowProc(hwnd, msg, wParam, lParam);
        return msg == WM_APPCOMMAND ? TRUE : 0;
    }
    if (handledByImgui) return 1;
    return original ? CallWindowProc(original, hwnd, msg, wParam, lParam)
                    : DefWindowProc(hwnd, msg, wParam, lParam);
}

static bool InvokeCallbacksGuarded(std::vector<std::function<void()>>* callbacks) {
    __try {
        for (auto& callback : *callbacks) callback();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool InvokeCallbackGuarded(std::function<void()>* callback) {
    __try {
        (*callback)();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool CaptureDeviceObjects(IDXGISwapChain* swapChain) {
    if (!swapChain || g_swapChain) return g_swapChain != nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    DXGI_SWAP_CHAIN_DESC desc{};
    const HRESULT deviceResult = swapChain->GetDevice(
        __uuidof(ID3D11Device), reinterpret_cast<void**>(&device));
    const HRESULT descResult = SUCCEEDED(deviceResult) ? swapChain->GetDesc(&desc) : deviceResult;
    if (FAILED(deviceResult) || !device || FAILED(descResult) || !desc.OutputWindow || !IsWindow(desc.OutputWindow)) {
        LOG("CaptureDeviceObjects: invalid swap chain deviceHr=0x%08X descHr=0x%08X device=%p hwnd=%p",
            (unsigned)deviceResult, (unsigned)descResult, (void*)device,
            (void*)desc.OutputWindow);
        if (device) device->Release();
        return false;
    }
    device->GetImmediateContext(&context);
    if (!context) { device->Release(); return false; }

    SetLastError(ERROR_SUCCESS);
    WNDPROC original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        desc.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(PassthroughWndProc)));
    if (!original && GetLastError() != ERROR_SUCCESS) {
        LOG("CaptureDeviceObjects: WndProc subclass failed, err=%lu", GetLastError());
        context->Release();
        device->Release();
        return false;
    }

    swapChain->AddRef();
    g_swapChain = swapChain;
    g_device = device;
    g_context = context;
    g_hwnd = desc.OutputWindow;
    g_originalWndProc = original;

    LOG("CaptureDeviceObjects: swapChain=%p device=%p context=%p hwnd=%p",
        (void*)g_swapChain, (void*)g_device, (void*)g_context, (void*)g_hwnd);
    return true;
}

// 创建隐藏窗口用于临时 swapchain
// 不能使用 GetDesktopWindow()，在某些进程中会返回属于其他窗口站的句柄，
// 导致 D3D11CreateDeviceAndSwapChain 返回 E_ACCESSDENIED (0x80070005)
static HWND CreateHiddenWindow() {
    static const char* kClassName = "GhostRoomTempWnd";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = kClassName;
        ATOM atom = RegisterClassExA(&wc);
        if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            LOG("CreateHiddenWindow: RegisterClassExA failed, err=%lu", GetLastError());
            return nullptr;
        }
        registered = true;
    }
    HWND hwnd = CreateWindowExA(
        0, kClassName, "temp", WS_OVERLAPPED,
        0, 0, 1, 1,
        nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!hwnd) {
        LOG("CreateHiddenWindow: CreateWindowExA failed, err=%lu", GetLastError());
    }
    return hwnd;
}

static bool CreateTemporarySwapChain(IDXGISwapChain** swapChain, ID3D11Device** device, ID3D11DeviceContext** context) {
    // 创建自己的隐藏窗口，避免使用 GetDesktopWindow()
    HWND hwnd = CreateHiddenWindow();
    if (!hwnd) {
        LOG("CreateTemporarySwapChain: no window handle available");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 2;
    scd.BufferDesc.Height = 2;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    // 尝试的 feature levels
    D3D_FEATURE_LEVEL tryLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        tryLevels,
        3,
        D3D11_SDK_VERSION,
        &scd,
        swapChain,
        device,
        &level,
        context);

    if (FAILED(hr) || !*swapChain) {
        LOG("D3D11CreateDeviceAndSwapChain HARDWARE failed: hr=0x%08X, trying WARP", (unsigned)hr);
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            tryLevels,
            3,
            D3D11_SDK_VERSION,
            &scd,
            swapChain,
            device,
            &level,
            context);
    }

    if (FAILED(hr) || !*swapChain) {
        LOG("CreateTemporarySwapChain failed entirely: hr=0x%08X", (unsigned)hr);
        DestroyWindow(hwnd);
        return false;
    }

    LOG("CreateTemporarySwapChain success (feature level=0x%X)", (unsigned)level);
    // Some drivers require the window to outlive the temporary swap chain.
    g_temporaryWindow = hwnd;
    return true;
}

HRESULT STDAPICALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    PresentFn original = nullptr;
    std::vector<std::function<void()>> callbacks;
    bool render = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (!g_swapChain) CaptureDeviceObjects(swapChain);
        render = g_swapChain == swapChain && (flags & DXGI_PRESENT_TEST) == 0;
        if (render) callbacks = g_callbacks;
        original = g_originalPresent;
    }
    if (render) {
        if (!g_rendering.test_and_set(std::memory_order_acquire)) {
            std::lock_guard<std::recursive_mutex> imguiLock(g_imguiMutex);
            try {
                if (!InvokeCallbacksGuarded(&callbacks))
                    LOG("HookedPresent: overlay callback raised an access violation; frame skipped");
            } catch (...) {
                LOG("HookedPresent: overlay callback threw; frame skipped");
            }
            g_rendering.clear(std::memory_order_release);
        }
    }
    if (!original) {
        LOG("HookedPresent: g_originalPresent is null!");
        return DXGI_ERROR_INVALID_CALL;
    }
    return original(swapChain, syncInterval, flags);
}

HRESULT STDAPICALLTYPE HookedResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount,
                                           UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    ResizeBuffersFn original = nullptr;
    std::function<void()> beforeResize;
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        original = g_originalResizeBuffers;
        beforeResize = g_beforeResizeCallback;
    }
    if (beforeResize) {
        if (!InvokeCallbackGuarded(&beforeResize))
            LOG("HookedResizeBuffers: release callback raised an access violation");
    }
    return original ? original(swapChain, bufferCount, width, height, format, flags)
                    : DXGI_ERROR_INVALID_CALL;
}

bool Install() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_installed) return true;

    LOG("Install: begin");

    IDXGISwapChain* tempSwapChain = nullptr;
    ID3D11Device* tempDevice = nullptr;
    ID3D11DeviceContext* tempContext = nullptr;
    if (!CreateTemporarySwapChain(&tempSwapChain, &tempDevice, &tempContext)) {
        if (tempContext) tempContext->Release();
        if (tempDevice) tempDevice->Release();
        if (tempSwapChain) tempSwapChain->Release();
        return false;
    }

    // 获取 vtable 和 Present 地址
    g_presentVTable = *reinterpret_cast<void***>(tempSwapChain);
    if (!g_presentVTable || !g_presentVTable[8] || !g_presentVTable[13]) {
        LOG("Install: temporary swap-chain vtable is invalid vtable=%p", (void*)g_presentVTable);
        tempContext->Release();
        tempDevice->Release();
        tempSwapChain->Release();
        g_presentVTable = nullptr;
        return false;
    }
    std::uintptr_t presentAddr = reinterpret_cast<std::uintptr_t>(g_presentVTable[8]);
    LOG("Install: tempSwapChain=%p, vtable=%p, Present=%p",
        (void*)tempSwapChain, (void*)g_presentVTable, (void*)presentAddr);

    // 打印 Present 前 16 字节用于调试
    {
        unsigned char* p = reinterpret_cast<unsigned char*>(presentAddr);
        LOG("Install: Present bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    }

    // Replace one aligned vtable pointer atomically. Avoid patching live x64 instructions.
    g_originalPresent = reinterpret_cast<PresentFn>(g_presentVTable[8]);
    g_originalResizeBuffers = reinterpret_cast<ResizeBuffersFn>(g_presentVTable[13]);
    DWORD oldProtect = 0;
    if (!VirtualProtect(&g_presentVTable[8], sizeof(void*) * 6, PAGE_READWRITE, &oldProtect)) {
        LOG("Install: vtable VirtualProtect failed err=%lu", GetLastError());
        tempContext->Release();
        tempDevice->Release();
        tempSwapChain->Release();
        if (g_temporaryWindow) {
            DestroyWindow(g_temporaryWindow);
            g_temporaryWindow = nullptr;
        }
        g_presentVTable = nullptr;
        g_originalPresent = nullptr;
        g_originalResizeBuffers = nullptr;
        return false;
    }
    InterlockedExchangePointer(&g_presentVTable[8], reinterpret_cast<void*>(&HookedPresent));
    InterlockedExchangePointer(&g_presentVTable[13], reinterpret_cast<void*>(&HookedResizeBuffers));
    DWORD ignored = 0;
    VirtualProtect(&g_presentVTable[8], sizeof(void*) * 6, oldProtect, &ignored);
    LOG("Install: atomic Present/ResizeBuffers vtable hooks installed");

    tempContext->Release();
    tempDevice->Release();
    tempSwapChain->Release();
    if (g_temporaryWindow) {
        DestroyWindow(g_temporaryWindow);
        g_temporaryWindow = nullptr;
    }

    g_installed = true;
    LOG("Install: success (vtable)");
    return true;
}

void Uninstall() {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (!g_installed) return;
    SetMenuCursorActive(false);

    if (g_presentVTable && g_originalPresent && g_originalResizeBuffers) {
        DWORD oldProtect = 0;
        if (!VirtualProtect(&g_presentVTable[8], sizeof(void*) * 6, PAGE_READWRITE, &oldProtect)) {
            LOG("Uninstall: cannot restore swap-chain vtable; hooks remain installed");
            return;
        }
        if (g_presentVTable[8] == reinterpret_cast<void*>(&HookedPresent))
            InterlockedExchangePointer(&g_presentVTable[8], reinterpret_cast<void*>(g_originalPresent));
        if (g_presentVTable[13] == reinterpret_cast<void*>(&HookedResizeBuffers))
            InterlockedExchangePointer(&g_presentVTable[13], reinterpret_cast<void*>(g_originalResizeBuffers));
        DWORD ignored = 0;
        VirtualProtect(&g_presentVTable[8], sizeof(void*) * 6, oldProtect, &ignored);
    }

    if (g_hwnd && g_originalWndProc) {
        if (IsWindow(g_hwnd) && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC)) == PassthroughWndProc)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
        g_originalWndProc = nullptr;
    }

    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
    if (g_swapChain) {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }

    g_hwnd = nullptr;
    if (g_temporaryWindow) {
        DestroyWindow(g_temporaryWindow);
        g_temporaryWindow = nullptr;
    }
    g_presentVTable = nullptr;
    g_originalPresent = nullptr;
    g_originalResizeBuffers = nullptr;
    g_beforeResizeCallback = {};
    g_callbacks.clear();
    g_installed = false;
    LOG("Uninstall: done");
}

IDXGISwapChain* GetSwapChain() { return g_swapChain; }
ID3D11Device* GetDevice() { return g_device; }
ID3D11DeviceContext* GetContext() { return g_context; }
HWND GetWindowHandle() { return g_hwnd; }
void SetImguiInputEnabled(bool enabled) { g_imguiInputEnabled.store(enabled); }
void SetGameInputBlocked(bool blocked) { SetMenuCursorActive(blocked); }
void SetImguiContextReady(bool ready) { g_imguiContextReady.store(ready); }

void OnFrame(const std::function<void()>& callback) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_callbacks.push_back(callback);
}

void OnBeforeResize(const std::function<void()>& callback) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_beforeResizeCallback = callback;
}

} // namespace d3d11_hook
