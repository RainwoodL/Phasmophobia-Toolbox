#include <windows.h>

#include "hud_runtime.h"
#include "log.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        LOG("DllMain: DLL_PROCESS_ATTACH, module=%p", (void*)module);
        DisableThreadLibraryCalls(module);
        return hud_runtime::Start(hud_runtime::Startup::GameAlreadyRunning);
    }
    return TRUE;
}
