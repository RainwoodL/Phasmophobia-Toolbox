#pragma once

namespace inline_hook {

struct Hook {
    void* target = nullptr;
    void* original = nullptr;
    void* detour = nullptr;
    bool attached = false;
};

bool Attach(Hook& hook, void* target, void* detour);
bool Detach(Hook& hook);

} // namespace inline_hook
