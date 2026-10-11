#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

template <typename Offset, typename Size>
bool RangeWithin(Offset offset, Size size, std::size_t limit) {
    const auto start = static_cast<std::uint64_t>(offset);
    const auto count = static_cast<std::uint64_t>(size);
    return start <= limit && count <= static_cast<std::uint64_t>(limit) - start;
}

std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment) {
    if (alignment == 0 || value > std::numeric_limits<std::uint32_t>::max() - (alignment - 1)) {
        throw std::runtime_error("invalid PE alignment");
    }
    return (value + alignment - 1) / alignment * alignment;
}

Bytes ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input file");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length <= 0 || static_cast<std::uint64_t>(length) >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("input file has invalid size");
    }
    input.seekg(0, std::ios::beg);
    Bytes data(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input) throw std::runtime_error("cannot read input file");
    return data;
}

void WriteFile(const std::filesystem::path& path, const Bytes& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create output file");
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output) throw std::runtime_error("cannot write output file");
}

struct PeImage {
    Bytes* data = nullptr;
    IMAGE_NT_HEADERS64* nt = nullptr;
    IMAGE_SECTION_HEADER* sections = nullptr;
};

PeImage ParseExecutable(Bytes& data) {
    if (data.size() < sizeof(IMAGE_DOS_HEADER)) throw std::runtime_error("missing DOS header");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        !RangeWithin(dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64), data.size())) {
        throw std::runtime_error("invalid DOS header");
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        (nt->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0 ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.FileAlignment == 0 || nt->OptionalHeader.SectionAlignment == 0 ||
        nt->FileHeader.NumberOfSections == 0) {
        throw std::runtime_error("unsupported executable format");
    }
    const std::size_t sectionOffset = static_cast<std::size_t>(dos->e_lfanew) +
        offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
    const std::size_t sectionBytes =
        static_cast<std::size_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (!RangeWithin(sectionOffset, sectionBytes, data.size())) {
        throw std::runtime_error("invalid section table");
    }
    auto* sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(data.data() + sectionOffset);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = sections[index];
        if (section.SizeOfRawData != 0 &&
            !RangeWithin(section.PointerToRawData, section.SizeOfRawData, data.size())) {
            throw std::runtime_error("section exceeds file bounds");
        }
        if (std::memcmp(section.Name, ".tbx", 4) == 0) {
            throw std::runtime_error("executable is already patched");
        }
    }
    const std::size_t newHeaderEnd = sectionOffset + sectionBytes + sizeof(IMAGE_SECTION_HEADER);
    if (newHeaderEnd > nt->OptionalHeader.SizeOfHeaders ||
        newHeaderEnd > sections[0].PointerToRawData) {
        throw std::runtime_error("PE headers have no room for another section");
    }
    return {&data, nt, sections};
}

std::size_t RvaToOffset(const PeImage& image, std::uint32_t rva, std::size_t size) {
    if (rva < image.nt->OptionalHeader.SizeOfHeaders &&
        RangeWithin(rva, size, image.data->size())) return rva;
    for (WORD index = 0; index < image.nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = image.sections[index];
        const std::uint64_t span = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        if (rva >= section.VirtualAddress &&
            static_cast<std::uint64_t>(rva) + size <=
                static_cast<std::uint64_t>(section.VirtualAddress) + span) {
            const std::uint64_t offset = static_cast<std::uint64_t>(section.PointerToRawData) +
                rva - section.VirtualAddress;
            if (!RangeWithin(offset, static_cast<std::uint64_t>(size), image.data->size())) break;
            return static_cast<std::size_t>(offset);
        }
    }
    throw std::runtime_error("RVA is outside file-backed PE data");
}

std::string ReadAscii(const PeImage& image, std::uint32_t rva, std::size_t maximum = 512) {
    const std::size_t offset = RvaToOffset(image, rva, 1);
    const std::size_t available = (std::min)(maximum, image.data->size() - offset);
    const char* text = reinterpret_cast<const char*>(image.data->data() + offset);
    const void* end = std::memchr(text, '\0', available);
    if (!end) throw std::runtime_error("unterminated string in import table");
    return std::string(text, static_cast<const char*>(end));
}

std::unordered_map<std::string, std::uint32_t> FindKernel32Iat(const PeImage& image) {
    const auto& directory =
        image.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        throw std::runtime_error("missing import directory");
    }
    const std::size_t descriptorOffset =
        RvaToOffset(image, directory.VirtualAddress, directory.Size);
    const std::size_t descriptorCount = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    const auto* descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        image.data->data() + descriptorOffset);

    constexpr std::array required{
        "CreateFileW", "WriteFile", "FlushFileBuffers", "LoadLibraryExW", "CloseHandle",
        "GetModuleHandleW", "GetProcAddress"};
    std::unordered_map<std::string, std::uint32_t> result;
    bool terminated = false;
    for (std::size_t descriptorIndex = 0; descriptorIndex < descriptorCount; ++descriptorIndex) {
        const auto& descriptor = descriptors[descriptorIndex];
        if (descriptor.Name == 0) {
            terminated = true;
            break;
        }
        const std::string module = ReadAscii(image, descriptor.Name);
        if (_stricmp(module.c_str(), "kernel32.dll") != 0) continue;
        const std::uint32_t namesRva =
            descriptor.OriginalFirstThunk ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
        if (!namesRva || !descriptor.FirstThunk) throw std::runtime_error("invalid kernel32 import descriptor");
        for (std::uint32_t index = 0; index < 4096; ++index) {
            const std::size_t thunkOffset =
                RvaToOffset(image, namesRva + index * sizeof(IMAGE_THUNK_DATA64),
                            sizeof(IMAGE_THUNK_DATA64));
            const auto thunk = *reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                image.data->data() + thunkOffset);
            if (thunk.u1.AddressOfData == 0) break;
            if (IMAGE_SNAP_BY_ORDINAL64(thunk.u1.Ordinal)) continue;
            const auto nameRva = static_cast<std::uint32_t>(thunk.u1.AddressOfData);
            const std::string name = ReadAscii(image, nameRva + sizeof(WORD));
            if (std::find(required.begin(), required.end(), name) != required.end()) {
                result.emplace(name, descriptor.FirstThunk +
                    static_cast<std::uint32_t>(index * sizeof(std::uint64_t)));
            }
        }
    }
    if (!terminated) throw std::runtime_error("unterminated import directory");
    for (const char* name : required) {
        if (result.find(name) == result.end()) {
            throw std::runtime_error(std::string("required kernel32 import is missing: ") + name);
        }
    }
    return result;
}

enum class FixupKind { SectionOffset, AbsoluteRva };

struct Fixup {
    std::size_t displacementOffset = 0;
    FixupKind kind = FixupKind::SectionOffset;
    std::uint32_t target = 0;
};

class StubBuilder {
public:
    void Emit(std::initializer_list<std::uint8_t> bytes) {
        code_.insert(code_.end(), bytes.begin(), bytes.end());
    }

    void U32(std::uint32_t value) {
        const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
        code_.insert(code_.end(), first, first + sizeof(value));
    }

    void RipRelative(std::initializer_list<std::uint8_t> opcode, FixupKind kind,
                     std::uint32_t target) {
        Emit(opcode);
        fixups_.push_back({code_.size(), kind, target});
        U32(0);
    }

    std::size_t JumpIfZero() {
        Emit({0x0F, 0x84});
        const std::size_t offset = code_.size();
        U32(0);
        return offset;
    }

    std::size_t JumpIfNotZero() {
        Emit({0x0F, 0x85});
        const std::size_t offset = code_.size();
        U32(0);
        return offset;
    }

    std::size_t Jump() {
        Emit({0xE9});
        const std::size_t offset = code_.size();
        U32(0);
        return offset;
    }

    void PatchInternal(std::size_t displacementOffset, std::size_t targetOffset) {
        const auto displacement = static_cast<std::int64_t>(targetOffset) -
            static_cast<std::int64_t>(displacementOffset + sizeof(std::int32_t));
        Patch32(displacementOffset, displacement);
    }

    void Resolve(std::uint32_t sectionRva) {
        for (const auto& fixup : fixups_) {
            const std::uint64_t targetRva = fixup.kind == FixupKind::SectionOffset
                ? static_cast<std::uint64_t>(sectionRva) + fixup.target
                : fixup.target;
            const std::uint64_t nextRva = static_cast<std::uint64_t>(sectionRva) +
                fixup.displacementOffset + sizeof(std::int32_t);
            Patch32(fixup.displacementOffset,
                    static_cast<std::int64_t>(targetRva) - static_cast<std::int64_t>(nextRva));
        }
    }

    void ReplaceSectionTarget(std::uint32_t marker, std::uint32_t target) {
        bool replaced = false;
        for (auto& fixup : fixups_) {
            if (fixup.kind == FixupKind::SectionOffset && fixup.target == marker) {
                fixup.target = target;
                replaced = true;
            }
        }
        if (!replaced) throw std::runtime_error("bootstrap marker was not found");
    }

    std::size_t Size() const { return code_.size(); }
    Bytes& Code() { return code_; }

private:
    void Patch32(std::size_t offset, std::int64_t value) {
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("relative branch is out of range");
        }
        const auto narrowed = static_cast<std::int32_t>(value);
        std::memcpy(code_.data() + offset, &narrowed, sizeof(narrowed));
    }

    Bytes code_;
    std::vector<Fixup> fixups_;
};

Bytes BuildSection(std::uint32_t sectionRva, std::uint32_t originalEntry,
                   const std::unordered_map<std::string, std::uint32_t>& iat,
                   const std::wstring& extractedPath, const Bytes& payload) {
    StubBuilder stub;

    constexpr std::uint32_t kWorkerMarker = 0xFFFFFFE0u;
    constexpr std::uint32_t kPathMarker = 0xFFFFFFE1u;
    constexpr std::uint32_t kPayloadMarker = 0xFFFFFFE2u;
    constexpr std::uint32_t kKernel32Marker = 0xFFFFFFE3u;
    constexpr std::uint32_t kCreateThreadMarker = 0xFFFFFFE4u;
    constexpr std::uint32_t kSleepMarker = 0xFFFFFFE5u;
    constexpr std::uint32_t kUnityPlayerMarker = 0xFFFFFFE6u;
    constexpr std::uint32_t kGameAssemblyMarker = 0xFFFFFFE7u;
    constexpr std::uint32_t kDxgiMarker = 0xFFFFFFE8u;
    constexpr std::uint32_t kD3d11Marker = 0xFFFFFFE9u;
    constexpr std::uint32_t kSetFileAttributesMarker = 0xFFFFFFEAu;
    constexpr std::uint32_t kHardStartupDelayMs = 5000u;

    // The entry point only starts the delayed loader and immediately resumes the game.
    stub.Emit({0x9C, 0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57,
               0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
               0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
    stub.Emit({0x48, 0x83, 0xEC, 0x38});
    stub.RipRelative({0x48, 0x8D, 0x0D}, FixupKind::SectionOffset, kKernel32Marker);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("GetModuleHandleW"));
    stub.Emit({0x48, 0x85, 0xC0});
    const std::size_t kernel32Missing = stub.JumpIfZero();
    stub.Emit({0x48, 0x89, 0xC1});
    stub.RipRelative({0x48, 0x8D, 0x15}, FixupKind::SectionOffset, kCreateThreadMarker);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("GetProcAddress"));
    stub.Emit({0x48, 0x85, 0xC0});
    const std::size_t createThreadMissing = stub.JumpIfZero();
    stub.Emit({0x49, 0x89, 0xC3, 0x33, 0xC9, 0x33, 0xD2});
    stub.RipRelative({0x4C, 0x8D, 0x05}, FixupKind::SectionOffset, kWorkerMarker);
    stub.Emit({0x45, 0x33, 0xC9,
               0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00,
               0x48, 0xC7, 0x44, 0x24, 0x28, 0x00, 0x00, 0x00, 0x00,
               0x41, 0xFF, 0xD3,
               0x48, 0x85, 0xC0});
    const std::size_t createThreadFailed = stub.JumpIfZero();
    stub.Emit({0x48, 0x89, 0xC1});
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("CloseHandle"));

    const std::size_t restoreLabel = stub.Size();
    stub.Emit({0x48, 0x83, 0xC4, 0x38,
               0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C,
               0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58,
               0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59, 0x58, 0x9D});
    stub.RipRelative({0xE9}, FixupKind::AbsoluteRva, originalEntry);
    stub.PatchInternal(kernel32Missing, restoreLabel);
    stub.PatchInternal(createThreadMissing, restoreLabel);
    stub.PatchInternal(createThreadFailed, restoreLabel);

    const std::uint32_t workerOffset = static_cast<std::uint32_t>(stub.Size());
    stub.Emit({0x48, 0x83, 0xEC, 0x58});

    // Resolve Sleep without changing the game's import table.
    stub.RipRelative({0x48, 0x8D, 0x0D}, FixupKind::SectionOffset, kKernel32Marker);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("GetModuleHandleW"));
    stub.Emit({0x48, 0x85, 0xC0});
    const std::size_t workerKernel32Missing = stub.JumpIfZero();
    stub.Emit({0x48, 0x89, 0xC1});
    stub.RipRelative({0x48, 0x8D, 0x15}, FixupKind::SectionOffset, kSleepMarker);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("GetProcAddress"));
    stub.Emit({0x48, 0x85, 0xC0});
    const std::size_t sleepMissing = stub.JumpIfZero();
    stub.Emit({0x48, 0x89, 0x44, 0x24, 0x40,
               0xB9});
    stub.U32(kHardStartupDelayMs);
    stub.Emit({0xFF, 0x54, 0x24, 0x40,
               0xC7, 0x44, 0x24, 0x48});
    stub.U32(120);

    const std::size_t pollModulesLabel = stub.Size();
    std::vector<std::size_t> moduleMissing;
    for (const std::uint32_t marker :
         {kUnityPlayerMarker, kGameAssemblyMarker, kDxgiMarker, kD3d11Marker}) {
        stub.RipRelative({0x48, 0x8D, 0x0D}, FixupKind::SectionOffset, marker);
        stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("GetModuleHandleW"));
        stub.Emit({0x48, 0x85, 0xC0});
        moduleMissing.push_back(stub.JumpIfZero());
    }
    const std::size_t modulesReady = stub.Jump();

    const std::size_t waitForModulesLabel = stub.Size();
    stub.Emit({0xB9});
    stub.U32(250);
    stub.Emit({0xFF, 0x54, 0x24, 0x40,
               0x83, 0x6C, 0x24, 0x48, 0x01});
    const std::size_t pollAgain = stub.JumpIfNotZero();

    // The worker owns file extraction and LoadLibrary, outside the startup path.
    const std::size_t loadPayloadLabel = stub.Size();
    stub.RipRelative({0x48, 0x8D, 0x0D}, FixupKind::SectionOffset, kKernel32Marker);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("GetModuleHandleW"));
    stub.Emit({0x48, 0x89, 0xC1});
    stub.RipRelative({0x48, 0x8D, 0x15}, FixupKind::SectionOffset, kSetFileAttributesMarker);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("GetProcAddress"));
    stub.Emit({0x48, 0x85, 0xC0});
    const std::size_t setAttributesMissing = stub.JumpIfZero();
    stub.RipRelative({0x48, 0x8D, 0x0D}, FixupKind::SectionOffset, kPathMarker);
    stub.Emit({0xBA});
    stub.U32(FILE_ATTRIBUTE_NORMAL);
    stub.Emit({0xFF, 0xD0});
    const std::size_t createPayloadLabel = stub.Size();
    stub.RipRelative({0x48, 0x8D, 0x0D}, FixupKind::SectionOffset, kPathMarker);
    stub.Emit({0xBA});
    stub.U32(GENERIC_WRITE);
    stub.Emit({0x41, 0xB8});
    stub.U32(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    stub.Emit({0x45, 0x33, 0xC9});
    stub.Emit({0x48, 0xC7, 0x44, 0x24, 0x20});
    stub.U32(CREATE_ALWAYS);
    stub.Emit({0x48, 0xC7, 0x44, 0x24, 0x28});
    stub.U32(FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN);
    stub.Emit({0x48, 0xC7, 0x44, 0x24, 0x30});
    stub.U32(0);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("CreateFileW"));
    stub.Emit({0x48, 0x89, 0x44, 0x24, 0x30, 0x48, 0x83, 0xF8, 0xFF});
    const std::size_t createFailed = stub.JumpIfZero();

    stub.Emit({0x48, 0x8B, 0x4C, 0x24, 0x30});
    stub.RipRelative({0x48, 0x8D, 0x15}, FixupKind::SectionOffset, kPayloadMarker);
    stub.Emit({0x41, 0xB8});
    stub.U32(static_cast<std::uint32_t>(payload.size()));
    stub.Emit({0x4C, 0x8D, 0x4C, 0x24, 0x38});
    stub.Emit({0x48, 0xC7, 0x44, 0x24, 0x20});
    stub.U32(0);
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("WriteFile"));
    stub.Emit({0x85, 0xC0});
    const std::size_t writeFailed = stub.JumpIfZero();
    stub.Emit({0x81, 0x7C, 0x24, 0x38});
    stub.U32(static_cast<std::uint32_t>(payload.size()));
    const std::size_t shortWrite = stub.JumpIfNotZero();

    stub.Emit({0x48, 0x8B, 0x4C, 0x24, 0x30});
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("FlushFileBuffers"));
    stub.Emit({0x85, 0xC0});
    const std::size_t flushFailed = stub.JumpIfZero();
    stub.Emit({0x48, 0x8B, 0x4C, 0x24, 0x30});
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("CloseHandle"));
    stub.RipRelative({0x48, 0x8D, 0x0D}, FixupKind::SectionOffset, kPathMarker);
    stub.Emit({0x33, 0xD2, 0x45, 0x33, 0xC0});
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("LoadLibraryExW"));
    const std::size_t loadComplete = stub.Jump();

    const std::size_t closeLabel = stub.Size();
    stub.Emit({0x48, 0x8B, 0x4C, 0x24, 0x30});
    stub.RipRelative({0xFF, 0x15}, FixupKind::AbsoluteRva, iat.at("CloseHandle"));
    const std::size_t workerReturnLabel = stub.Size();
    stub.Emit({0x33, 0xC0, 0x48, 0x83, 0xC4, 0x58, 0xC3});

    stub.PatchInternal(workerKernel32Missing, loadPayloadLabel);
    stub.PatchInternal(sleepMissing, loadPayloadLabel);
    stub.PatchInternal(setAttributesMissing, createPayloadLabel);
    for (const std::size_t branch : moduleMissing) {
        stub.PatchInternal(branch, waitForModulesLabel);
    }
    stub.PatchInternal(modulesReady, loadPayloadLabel);
    stub.PatchInternal(pollAgain, pollModulesLabel);
    stub.PatchInternal(createFailed, workerReturnLabel);
    stub.PatchInternal(writeFailed, closeLabel);
    stub.PatchInternal(shortWrite, closeLabel);
    stub.PatchInternal(flushFailed, closeLabel);
    stub.PatchInternal(loadComplete, workerReturnLabel);

    const auto alignedOffset = [](std::size_t value) {
        return static_cast<std::uint32_t>((value + 15u) & ~std::size_t{15u});
    };
    std::uint32_t cursor = alignedOffset(stub.Size());
    const auto reserveWide = [&cursor, &alignedOffset](const std::wstring& text) {
        const std::uint32_t offset = cursor;
        cursor = alignedOffset(static_cast<std::size_t>(cursor) +
                               (text.size() + 1) * sizeof(wchar_t));
        return offset;
    };
    const auto reserveAscii = [&cursor, &alignedOffset](const std::string& text) {
        const std::uint32_t offset = cursor;
        cursor = alignedOffset(static_cast<std::size_t>(cursor) + text.size() + 1);
        return offset;
    };

    const std::wstring kernel32 = L"kernel32.dll";
    const std::string createThread = "CreateThread";
    const std::string sleep = "Sleep";
    const std::string setFileAttributes = "SetFileAttributesW";
    const std::wstring unityPlayer = L"UnityPlayer.dll";
    const std::wstring gameAssembly = L"GameAssembly.dll";
    const std::wstring dxgi = L"dxgi.dll";
    const std::wstring d3d11 = L"d3d11.dll";
    const std::uint32_t pathOffset = reserveWide(extractedPath);
    const std::uint32_t kernel32Offset = reserveWide(kernel32);
    const std::uint32_t createThreadOffset = reserveAscii(createThread);
    const std::uint32_t sleepOffset = reserveAscii(sleep);
    const std::uint32_t setFileAttributesOffset = reserveAscii(setFileAttributes);
    const std::uint32_t unityPlayerOffset = reserveWide(unityPlayer);
    const std::uint32_t gameAssemblyOffset = reserveWide(gameAssembly);
    const std::uint32_t dxgiOffset = reserveWide(dxgi);
    const std::uint32_t d3d11Offset = reserveWide(d3d11);
    const std::uint32_t payloadOffset = cursor;

    stub.ReplaceSectionTarget(kWorkerMarker, workerOffset);
    stub.ReplaceSectionTarget(kPathMarker, pathOffset);
    stub.ReplaceSectionTarget(kPayloadMarker, payloadOffset);
    stub.ReplaceSectionTarget(kKernel32Marker, kernel32Offset);
    stub.ReplaceSectionTarget(kCreateThreadMarker, createThreadOffset);
    stub.ReplaceSectionTarget(kSleepMarker, sleepOffset);
    stub.ReplaceSectionTarget(kSetFileAttributesMarker, setFileAttributesOffset);
    stub.ReplaceSectionTarget(kUnityPlayerMarker, unityPlayerOffset);
    stub.ReplaceSectionTarget(kGameAssemblyMarker, gameAssemblyOffset);
    stub.ReplaceSectionTarget(kDxgiMarker, dxgiOffset);
    stub.ReplaceSectionTarget(kD3d11Marker, d3d11Offset);
    stub.Resolve(sectionRva);

    Bytes section = stub.Code();
    const auto appendData = [&section](std::uint32_t offset, const void* data, std::size_t size) {
        if (section.size() > offset) throw std::runtime_error("bootstrap data overlaps code");
        section.resize(offset, 0);
        const auto* first = static_cast<const std::uint8_t*>(data);
        section.insert(section.end(), first, first + size);
    };
    const auto appendWide = [&appendData](std::uint32_t offset, const std::wstring& text) {
        appendData(offset, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    };
    const auto appendAscii = [&appendData](std::uint32_t offset, const std::string& text) {
        appendData(offset, text.c_str(), text.size() + 1);
    };
    appendWide(pathOffset, extractedPath);
    appendWide(kernel32Offset, kernel32);
    appendAscii(createThreadOffset, createThread);
    appendAscii(sleepOffset, sleep);
    appendAscii(setFileAttributesOffset, setFileAttributes);
    appendWide(unityPlayerOffset, unityPlayer);
    appendWide(gameAssemblyOffset, gameAssembly);
    appendWide(dxgiOffset, dxgi);
    appendWide(d3d11Offset, d3d11);
    section.resize(payloadOffset, 0);
    section.insert(section.end(), payload.begin(), payload.end());
    return section;
}

void Embed(Bytes& executable, const Bytes& payload, const std::wstring& extractedPath) {
    if (payload.empty() || payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("payload has invalid size");
    }
    PeImage image = ParseExecutable(executable);
    const auto iat = FindKernel32Iat(image);
    const WORD oldSectionCount = image.nt->FileHeader.NumberOfSections;
    const DWORD originalEntry = image.nt->OptionalHeader.AddressOfEntryPoint;
    const auto& last = image.sections[oldSectionCount - 1];
    const DWORD sectionRva = AlignUp(last.VirtualAddress +
        (std::max)(last.Misc.VirtualSize, last.SizeOfRawData),
        image.nt->OptionalHeader.SectionAlignment);
    const Bytes sectionData = BuildSection(sectionRva, originalEntry, iat, extractedPath, payload);
    const DWORD rawOffset = AlignUp(static_cast<DWORD>(executable.size()),
                                    image.nt->OptionalHeader.FileAlignment);
    const DWORD rawSize = AlignUp(static_cast<DWORD>(sectionData.size()),
                                  image.nt->OptionalHeader.FileAlignment);
    const DWORD imageSize = AlignUp(sectionRva + static_cast<DWORD>(sectionData.size()),
                                    image.nt->OptionalHeader.SectionAlignment);

    const std::size_t ntOffset = reinterpret_cast<std::uint8_t*>(image.nt) - executable.data();
    const std::size_t sectionsOffset = reinterpret_cast<std::uint8_t*>(image.sections) - executable.data();
    executable.resize(static_cast<std::size_t>(rawOffset) + rawSize, 0);
    image.nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(executable.data() + ntOffset);
    image.sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(executable.data() + sectionsOffset);

    auto& added = image.sections[oldSectionCount];
    std::memset(&added, 0, sizeof(added));
    std::memcpy(added.Name, ".tbx", 4);
    added.Misc.VirtualSize = static_cast<DWORD>(sectionData.size());
    added.VirtualAddress = sectionRva;
    added.SizeOfRawData = rawSize;
    added.PointerToRawData = rawOffset;
    added.Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA |
        IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    std::memcpy(executable.data() + rawOffset, sectionData.data(), sectionData.size());

    image.nt->FileHeader.NumberOfSections = oldSectionCount + 1;
    image.nt->OptionalHeader.AddressOfEntryPoint = sectionRva;
    image.nt->OptionalHeader.SizeOfImage = imageSize;
    image.nt->OptionalHeader.SizeOfCode += rawSize;
    image.nt->OptionalHeader.CheckSum = 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 5) {
        std::wcerr << L"Usage: pe_embed <input-exe> <payload-dll> <output-exe> <runtime-dll-path>\n";
        return 2;
    }
    try {
        Bytes executable = ReadFile(argv[1]);
        const Bytes payload = ReadFile(argv[2]);
        Embed(executable, payload, argv[4]);
        WriteFile(argv[3], executable);
        std::wcout << L"Embedded " << payload.size() << L" payload bytes into " << argv[3] << L"\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "pe_embed: " << error.what() << '\n';
        return 1;
    }
}
