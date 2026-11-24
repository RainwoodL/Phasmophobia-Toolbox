#include "unity_coordinates.h"

#include <cmath>
#include <cstddef>

namespace unity_coordinates {
namespace {

// The public IL2CPP wrappers take a managed receiver and return Vector3 by
// value. The x64 compiler supplies the hidden result buffer for this layout.
static_assert(sizeof(Vector3) == 12 && offsetof(Vector3, x) == 0 &&
              offsetof(Vector3, y) == 4 && offsetof(Vector3, z) == 8);

template <typename Function>
bool ResolveUnityMethod(const char* className, const char* methodName, int arity,
                        Function& function, const il2cpp_runtime::Method*& method) {
    if (function) return true;
    method = il2cpp_runtime::FindMethod(il2cpp_runtime::FindClass("UnityEngine", className), methodName, arity);
    void* entry = il2cpp_runtime::GetMethodEntry(method);
    if (!entry || !il2cpp_runtime::IsExecutable(entry)) return false;
    function = reinterpret_cast<Function>(entry);
    return true;
}

bool IsFinite(Vector3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

} // namespace

bool PositionReader::Resolve() {
    return ResolveUnityMethod("Component", "get_transform", 0, getTransform_, transformMethod_) &&
           ResolveUnityMethod("Transform", "get_position", 0, getPosition_, positionMethod_);
}

bool PositionReader::ReadTransform(void* transform, Vector3& position) const {
    if (!transform || !getPosition_) return false;
    position = getPosition_(transform, positionMethod_);
    return IsFinite(position);
}

bool PositionReader::ReadComponent(void* component, Vector3& position) const {
    return component && getTransform_ && ReadTransform(getTransform_(component, transformMethod_), position);
}

bool ScreenProjector::Resolve() {
    return ResolveUnityMethod("Camera", "WorldToScreenPoint", 1, project_, projectMethod_);
}

bool ScreenProjector::WorldToScreen(void* camera, Vector3 world, Vector3& screen) const {
    if (!camera || !project_ || !IsFinite(world)) return false;
    screen = project_(camera, world, projectMethod_);
    return IsFinite(screen);
}

} // namespace unity_coordinates
