#pragma once

#include "il2cpp_runtime.h"

namespace unity_coordinates {

struct Vector3 { float x, y, z; };

class PositionReader {
public:
    bool Resolve();
    bool ReadTransform(void* transform, Vector3& position) const;
    bool ReadComponent(void* component, Vector3& position) const;

private:
    using GetTransform = void* (*)(void*, const il2cpp_runtime::Method*);
    using GetPosition = Vector3 (*)(void*, const il2cpp_runtime::Method*);
    GetTransform getTransform_ = nullptr;
    const il2cpp_runtime::Method* transformMethod_ = nullptr;
    GetPosition getPosition_ = nullptr;
    const il2cpp_runtime::Method* positionMethod_ = nullptr;
};

class ScreenProjector {
public:
    bool Resolve();
    bool WorldToScreen(void* camera, Vector3 world, Vector3& screen) const;

private:
    using Project = Vector3 (*)(void*, Vector3, const il2cpp_runtime::Method*);
    Project project_ = nullptr;
    const il2cpp_runtime::Method* projectMethod_ = nullptr;
};

} // namespace unity_coordinates
