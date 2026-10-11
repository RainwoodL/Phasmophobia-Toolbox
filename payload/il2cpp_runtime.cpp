#include "il2cpp_runtime.h"

#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace il2cpp_runtime {
namespace {

using Domain = void;
using Assembly = void;
using Image = void;
using DomainGetFn = Domain* (*)();
using DomainAssembliesFn = const Assembly** (*)(Domain*, std::size_t*);
using AssemblyImageFn = const Image* (*)(const Assembly*);
using ClassFromNameFn = Class* (*)(const Image*, const char*, const char*);
using MethodFromNameFn = const Method* (*)(Class*, const char*, int);
using Field = void;
using Type = void;
using ClassGetFieldsFn = Field* (*)(Class*, void**);
using ClassGetNestedTypesFn = Class* (*)(Class*, void**);
using ClassGetNameFn = const char* (*)(Class*);
using ClassGetElementClassFn = Class* (*)(Class*);
using ClassIsEnumFn = bool (*)(Class*);
using FieldGetFlagsFn = std::uint32_t (*)(Field*);
using FieldGetNameFn = const char* (*)(Field*);
using FieldGetTypeFn = const Type* (*)(Field*);
using FieldGetOffsetFn = std::int32_t (*)(Field*);
using FieldStaticGetValueFn = void (*)(Field*, void*);
using ClassFromTypeFn = Class* (*)(const Type*);
using ClassGetTypeFn = const Type* (*)(Class*);
using TypeGetObjectFn = void* (*)(const Type*);
using ObjectNewFn = void* (*)(Class*);
using ArrayNewFn = void* (*)(Class*, std::size_t);

std::mutex g_mutex;
HMODULE g_module = nullptr;
std::uintptr_t g_moduleBegin = 0;
std::uintptr_t g_moduleEnd = 0;
DomainGetFn g_domainGet = nullptr;
DomainAssembliesFn g_domainAssemblies = nullptr;
AssemblyImageFn g_assemblyImage = nullptr;
ClassFromNameFn g_classFromName = nullptr;
MethodFromNameFn g_methodFromName = nullptr;
ClassGetFieldsFn g_classGetFields = nullptr;
ClassGetNestedTypesFn g_classGetNestedTypes = nullptr;
ClassGetNameFn g_classGetName = nullptr;
ClassGetElementClassFn g_classGetElementClass = nullptr;
ClassIsEnumFn g_classIsEnum = nullptr;
FieldGetFlagsFn g_fieldGetFlags = nullptr;
FieldGetNameFn g_fieldGetName = nullptr;
FieldGetTypeFn g_fieldGetType = nullptr;
FieldGetOffsetFn g_fieldGetOffset = nullptr;
FieldStaticGetValueFn g_fieldStaticGetValue = nullptr;
ClassFromTypeFn g_classFromType = nullptr;
ClassGetTypeFn g_classGetType = nullptr;
TypeGetObjectFn g_typeGetObject = nullptr;
ObjectNewFn g_objectNew = nullptr;
ArrayNewFn g_arrayNew = nullptr;

bool HasProtection(DWORD protection, bool executable) {
    if ((protection & PAGE_GUARD) != 0 || protection == PAGE_NOACCESS) return false;
    const DWORD value = protection & 0xFF;
    if (executable) {
        return value == PAGE_EXECUTE || value == PAGE_EXECUTE_READ ||
               value == PAGE_EXECUTE_READWRITE || value == PAGE_EXECUTE_WRITECOPY;
    }
    return value == PAGE_READONLY || value == PAGE_READWRITE || value == PAGE_WRITECOPY ||
           value == PAGE_EXECUTE_READ || value == PAGE_EXECUTE_READWRITE || value == PAGE_EXECUTE_WRITECOPY;
}

bool CheckRange(const void* address, std::size_t size, bool executable) {
    if (!address || size == 0) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    const auto end = begin + size;
    auto cursor = begin;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || !HasProtection(info.Protect, executable)) return false;
        const auto regionBegin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (info.RegionSize > std::numeric_limits<std::uintptr_t>::max() - regionBegin) return false;
        const auto regionEnd = regionBegin + info.RegionSize;
        if (cursor < regionBegin || cursor >= regionEnd) return false;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return true;
}

} // namespace

bool Initialize() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_module && g_domainGet && g_domainAssemblies && g_assemblyImage && g_classFromName && g_methodFromName) return true;
    g_module = GetModuleHandleW(L"GameAssembly.dll");
    if (!g_module) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_module);
    if (!IsReadable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const std::uint8_t*>(g_module) + dos->e_lfanew);
    if (!IsReadable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE) return false;
    g_moduleBegin = reinterpret_cast<std::uintptr_t>(g_module);
    g_moduleEnd = g_moduleBegin + nt->OptionalHeader.SizeOfImage;
    g_domainGet = reinterpret_cast<DomainGetFn>(GetProcAddress(g_module, "il2cpp_domain_get"));
    g_domainAssemblies = reinterpret_cast<DomainAssembliesFn>(GetProcAddress(g_module, "il2cpp_domain_get_assemblies"));
    g_assemblyImage = reinterpret_cast<AssemblyImageFn>(GetProcAddress(g_module, "il2cpp_assembly_get_image"));
    g_classFromName = reinterpret_cast<ClassFromNameFn>(GetProcAddress(g_module, "il2cpp_class_from_name"));
    g_methodFromName = reinterpret_cast<MethodFromNameFn>(GetProcAddress(g_module, "il2cpp_class_get_method_from_name"));
    g_classGetFields = reinterpret_cast<ClassGetFieldsFn>(GetProcAddress(g_module, "il2cpp_class_get_fields"));
    g_classGetNestedTypes = reinterpret_cast<ClassGetNestedTypesFn>(GetProcAddress(g_module, "il2cpp_class_get_nested_types"));
    g_classGetName = reinterpret_cast<ClassGetNameFn>(GetProcAddress(g_module, "il2cpp_class_get_name"));
    g_classGetElementClass = reinterpret_cast<ClassGetElementClassFn>(GetProcAddress(g_module, "il2cpp_class_get_element_class"));
    g_classIsEnum = reinterpret_cast<ClassIsEnumFn>(GetProcAddress(g_module, "il2cpp_class_is_enum"));
    g_fieldGetFlags = reinterpret_cast<FieldGetFlagsFn>(GetProcAddress(g_module, "il2cpp_field_get_flags"));
    g_fieldGetName = reinterpret_cast<FieldGetNameFn>(GetProcAddress(g_module, "il2cpp_field_get_name"));
    g_fieldGetType = reinterpret_cast<FieldGetTypeFn>(GetProcAddress(g_module, "il2cpp_field_get_type"));
    g_fieldGetOffset = reinterpret_cast<FieldGetOffsetFn>(GetProcAddress(g_module, "il2cpp_field_get_offset"));
    g_fieldStaticGetValue = reinterpret_cast<FieldStaticGetValueFn>(GetProcAddress(g_module, "il2cpp_field_static_get_value"));
    g_classFromType = reinterpret_cast<ClassFromTypeFn>(GetProcAddress(g_module, "il2cpp_class_from_type"));
    g_classGetType = reinterpret_cast<ClassGetTypeFn>(GetProcAddress(g_module, "il2cpp_class_get_type"));
    g_typeGetObject = reinterpret_cast<TypeGetObjectFn>(GetProcAddress(g_module, "il2cpp_type_get_object"));
    g_objectNew = reinterpret_cast<ObjectNewFn>(GetProcAddress(g_module, "il2cpp_object_new"));
    g_arrayNew = reinterpret_cast<ArrayNewFn>(GetProcAddress(g_module, "il2cpp_array_new"));
    return g_domainGet && g_domainAssemblies && g_assemblyImage && g_classFromName && g_methodFromName;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_module = nullptr;
    g_moduleBegin = g_moduleEnd = 0;
    g_domainGet = nullptr;
    g_domainAssemblies = nullptr;
    g_assemblyImage = nullptr;
    g_classFromName = nullptr;
    g_methodFromName = nullptr;
    g_classGetFields = nullptr;
    g_classGetNestedTypes = nullptr;
    g_classGetName = nullptr;
    g_classGetElementClass = nullptr;
    g_classIsEnum = nullptr;
    g_fieldGetFlags = nullptr;
    g_fieldGetName = nullptr;
    g_fieldGetType = nullptr;
    g_fieldGetOffset = nullptr;
    g_fieldStaticGetValue = nullptr;
    g_classFromType = nullptr;
    g_classGetType = nullptr;
    g_typeGetObject = nullptr;
    g_objectNew = nullptr;
    g_arrayNew = nullptr;
}

Class* FindClass(const char* namespaceName, const char* className) {
    if (!Initialize() || !namespaceName || !className) return nullptr;
    Domain* domain = g_domainGet();
    if (!domain) return nullptr;
    std::size_t count = 0;
    const Assembly** assemblies = g_domainAssemblies(domain, &count);
    if (!assemblies) return nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        const Image* image = assemblies[i] ? g_assemblyImage(assemblies[i]) : nullptr;
        if (image) {
            if (Class* klass = g_classFromName(image, namespaceName, className)) return klass;
        }
    }
    return nullptr;
}

Class* FindNestedClass(Class* ownerClass, const char* className) {
    if (!Initialize() || !ownerClass || !className || !g_classGetNestedTypes || !g_classGetName) return nullptr;
    void* iterator = nullptr;
    while (Class* nested = g_classGetNestedTypes(ownerClass, &iterator)) {
        const char* nestedName = g_classGetName(nested);
        if (nestedName && std::strcmp(nestedName, className) == 0) return nested;
    }
    return nullptr;
}

const Method* FindMethod(Class* klass, const char* methodName, int parameterCount) {
    return Initialize() && klass && methodName ? g_methodFromName(klass, methodName, parameterCount) : nullptr;
}

void* GetMethodEntry(const Method* method) {
    void* entry = nullptr;
    return method && ReadBytes(reinterpret_cast<std::uintptr_t>(method), &entry, sizeof(entry)) ? entry : nullptr;
}

bool IsInGameAssembly(const void* address, std::size_t size) {
    if (!address || size == 0 || g_moduleBegin == 0) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    return begin >= g_moduleBegin && begin < g_moduleEnd && size <= g_moduleEnd - begin;
}

bool IsReadable(const void* address, std::size_t size) { return CheckRange(address, size, false); }
bool IsExecutable(const void* address) { return CheckRange(address, 1, true); }

bool ReadBytes(std::uintptr_t address, void* output, std::size_t size) {
    if (!output || !IsReadable(reinterpret_cast<void*>(address), size)) return false;
    __try { std::memcpy(output, reinterpret_cast<void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { std::memset(output, 0, size); return false; }
}

bool ReadPointer(std::uintptr_t address, std::uintptr_t& output) { return ReadBytes(address, &output, sizeof(output)); }
bool ReadInt32(std::uintptr_t address, std::int32_t& output) { return ReadBytes(address, &output, sizeof(output)); }
bool ReadFloat(std::uintptr_t address, float& output) { return ReadBytes(address, &output, sizeof(output)); }
bool ReadByte(std::uintptr_t address, std::uint8_t& output) { return ReadBytes(address, &output, sizeof(output)); }

bool FindInstanceFieldByName(Class* ownerClass, const char* name, std::size_t& offset) {
    constexpr std::uint32_t kStatic = 0x10;
    if (!Initialize() || !ownerClass || !name || !g_classGetFields || !g_fieldGetFlags ||
        !g_fieldGetName || !g_fieldGetOffset) return false;
    void* iterator = nullptr;
    while (Field* field = g_classGetFields(ownerClass, &iterator)) {
        const char* fieldName = g_fieldGetName(field);
        if ((g_fieldGetFlags(field) & kStatic) != 0 || !fieldName || std::strcmp(fieldName, name) != 0) continue;
        const std::int32_t fieldOffset = g_fieldGetOffset(field);
        if (fieldOffset <= 0 || fieldOffset >= 0x1000) return false;
        offset = static_cast<std::uintptr_t>(fieldOffset);
        return true;
    }
    return false;
}

std::vector<std::uintptr_t> FindInstanceFieldsByType(Class* ownerClass, Class* targetClass) {
    constexpr std::uint32_t kStatic = 0x10;
    std::vector<std::uintptr_t> offsets;
    if (!Initialize() || !ownerClass || !targetClass || !g_classGetFields || !g_fieldGetFlags ||
        !g_fieldGetType || !g_fieldGetOffset || !g_classFromType) return offsets;
    void* iterator = nullptr;
    while (Field* field = g_classGetFields(ownerClass, &iterator)) {
        if ((g_fieldGetFlags(field) & kStatic) != 0 || g_classFromType(g_fieldGetType(field)) != targetClass) continue;
        const std::int32_t fieldOffset = g_fieldGetOffset(field);
        if (fieldOffset > 0 && fieldOffset < 0x1000)
            offsets.push_back(static_cast<std::uintptr_t>(fieldOffset));
    }
    return offsets;
}

bool FindInstanceFieldByType(Class* ownerClass, Class* targetClass, std::uintptr_t& offset) {
    const auto offsets = FindInstanceFieldsByType(ownerClass, targetClass);
    if (offsets.size() != 1) return false;
    offset = offsets.front();
    return true;
}

bool FindUniqueInstanceEnumField(Class* ownerClass, std::uintptr_t& offset) {
    constexpr std::uint32_t kStatic = 0x10;
    if (!Initialize() || !ownerClass || !g_classGetFields || !g_fieldGetFlags ||
        !g_fieldGetType || !g_fieldGetOffset || !g_classFromType || !g_classIsEnum) return false;
    std::uintptr_t candidate = 0;
    void* iterator = nullptr;
    while (Field* field = g_classGetFields(ownerClass, &iterator)) {
        if ((g_fieldGetFlags(field) & kStatic) != 0) continue;
        Class* fieldClass = g_classFromType(g_fieldGetType(field));
        if (!fieldClass || !g_classIsEnum(fieldClass)) continue;
        const std::int32_t fieldOffset = g_fieldGetOffset(field);
        if (candidate || fieldOffset <= 0 || fieldOffset >= 0x1000) return false;
        candidate = static_cast<std::uintptr_t>(fieldOffset);
    }
    if (!candidate) return false;
    offset = candidate;
    return true;
}

void* GetTypeObject(Class* klass) {
    return Initialize() && klass && g_classGetType && g_typeGetObject ? g_typeGetObject(g_classGetType(klass)) : nullptr;
}

void* NewObject(Class* klass) {
    return Initialize() && klass && g_objectNew ? g_objectNew(klass) : nullptr;
}

void* NewArray(Class* elementClass, std::size_t length) {
    return Initialize() && elementClass && g_arrayNew ? g_arrayNew(elementClass, length) : nullptr;
}

std::vector<std::uintptr_t> ReadManagedList(std::uintptr_t list, std::int32_t maxCount) {
    std::vector<std::uintptr_t> result;
    std::uintptr_t items = 0;
    std::int32_t size = 0;
    if (!list || maxCount < 0 || !ReadPointer(list + 0x10, items) ||
        !ReadInt32(list + 0x18, size) || size < 0 || size > maxCount || (!items && size != 0)) return result;
    result.reserve(static_cast<std::size_t>(size));
    for (std::int32_t i = 0; i < size; ++i) {
        std::uintptr_t value = 0;
        if (!ReadPointer(items + 0x20 + static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t), value)) {
            result.clear();
            return result;
        }
        result.push_back(value);
    }
    return result;
}

bool FindInstanceListFieldByElementType(Class* ownerClass, Class* elementClass,
                                      std::uintptr_t& offset, std::size_t& versionOffset) {
    if (!Initialize() || !ownerClass || !elementClass || !g_classGetFields ||
        !g_fieldGetFlags || !g_fieldGetType || !g_fieldGetOffset || !g_fieldGetName ||
        !g_classFromType || !g_classGetName || !g_classGetElementClass) return false;
    std::uintptr_t candidate = 0;
    std::size_t candidateVersion = 0;
    void* iterator = nullptr;
    while (Field* field = g_classGetFields(ownerClass, &iterator)) {
        if ((g_fieldGetFlags(field) & 0x10) != 0) continue;
        auto* listClass = g_classFromType(g_fieldGetType(field));
        const char* name = listClass ? g_classGetName(listClass) : nullptr;
        if (!name || std::strcmp(name, "List`1") != 0) continue;
        void* memberIterator = nullptr;
        bool matchingElement = false;
        while (Field* member = g_classGetFields(listClass, &memberIterator)) {
            const char* memberName = g_fieldGetName(member);
            if (!memberName || std::strcmp(memberName, "_items") != 0) continue;
            auto* arrayClass = g_classFromType(g_fieldGetType(member));
            matchingElement = arrayClass && g_classGetElementClass(arrayClass) == elementClass;
            break;
        }
        if (!matchingElement) continue;
        const auto fieldOffset = g_fieldGetOffset(field);
        std::size_t version = 0;
        if (candidate || fieldOffset <= 0 || fieldOffset >= 0x1000 ||
            !FindInstanceFieldByName(listClass, "_version", version)) return false;
        candidate = static_cast<std::uintptr_t>(fieldOffset);
        candidateVersion = version;
    }
    if (!candidate) return false;
    offset = candidate;
    versionOffset = candidateVersion;
    return true;
}

std::uintptr_t ReadStaticObjectFieldByType(const char* ownerClassName, const char* targetClassName) {
    constexpr std::uint32_t kStatic = 0x10;
    if (!Initialize() || !g_classGetFields || !g_fieldGetFlags || !g_fieldGetType ||
        !g_fieldStaticGetValue || !g_classFromType) return 0;
    Class* ownerClass = FindClass("", ownerClassName);
    Class* targetClass = FindClass("", targetClassName);
    if (!ownerClass || !targetClass) return 0;
    void* iterator = nullptr;
    while (Field* field = g_classGetFields(ownerClass, &iterator)) {
        if ((g_fieldGetFlags(field) & kStatic) == 0 || g_classFromType(g_fieldGetType(field)) != targetClass) continue;
        std::uintptr_t value = 0;
        g_fieldStaticGetValue(field, &value);
        return value;
    }
    return 0;
}

std::string CopyUtf8String(std::uintptr_t il2cppString, const std::string& fallback) {
    std::int32_t length = 0;
    if (!il2cppString || !ReadInt32(il2cppString + 0x10, length) || length < 1 || length > 128) return fallback;
    std::vector<wchar_t> wide(static_cast<std::size_t>(length));
    if (!ReadBytes(il2cppString + 0x14, wide.data(), wide.size() * sizeof(wchar_t))) return fallback;
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return fallback;
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, result.data(), required, nullptr, nullptr) != required) return fallback;
    return result;
}

} // namespace il2cpp_runtime
