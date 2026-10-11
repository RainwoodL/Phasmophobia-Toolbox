#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace il2cpp_runtime {

using Class = void;
using Method = void;

bool Initialize();
void Shutdown();
Class* FindClass(const char* namespaceName, const char* className);
Class* FindNestedClass(Class* ownerClass, const char* className);
const Method* FindMethod(Class* klass, const char* methodName, int parameterCount);
void* GetMethodEntry(const Method* method);
bool IsInGameAssembly(const void* address, std::size_t size = 1);
bool IsReadable(const void* address, std::size_t size);
bool IsExecutable(const void* address);
bool ReadBytes(std::uintptr_t address, void* output, std::size_t size);
bool ReadPointer(std::uintptr_t address, std::uintptr_t& output);
bool ReadInt32(std::uintptr_t address, std::int32_t& output);
bool ReadFloat(std::uintptr_t address, float& output);
bool ReadByte(std::uintptr_t address, std::uint8_t& output);
std::uintptr_t ReadStaticObjectFieldByType(const char* ownerClassName, const char* targetClassName);
bool FindInstanceFieldByName(Class* ownerClass, const char* name, std::size_t& offset);
bool FindInstanceFieldByType(Class* ownerClass, Class* targetClass, std::uintptr_t& offset);
std::vector<std::uintptr_t> FindInstanceFieldsByType(Class* ownerClass, Class* targetClass);
bool FindUniqueInstanceEnumField(Class* ownerClass, std::uintptr_t& offset);
bool FindInstanceListFieldByElementType(Class* ownerClass, Class* elementClass,
                                      std::uintptr_t& offset, std::size_t& versionOffset);
void* GetTypeObject(Class* klass);
void* NewObject(Class* klass);
void* NewArray(Class* elementClass, std::size_t length);
std::vector<std::uintptr_t> ReadManagedList(std::uintptr_t list, std::int32_t maxCount);
std::string CopyUtf8String(std::uintptr_t il2cppString, const std::string& fallback = "--");
} // namespace il2cpp_runtime
