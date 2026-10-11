#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr int kExpectedResourceCount = 63;
constexpr std::array<std::uint8_t, 8> kPngSignature{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

bool ReadPng(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input && bytes.size() >= kPngSignature.size() &&
           std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin());
}
} // namespace

int main(int argc, char** argv) {
    if (argc != kExpectedResourceCount + 3) {
        std::cerr << "usage: gen_inventory_resources <output-header> <sponsor png> <63 png files>\n";
        return 1;
    }

    std::vector<std::uint8_t> sponsor;
    if (!ReadPng(argv[2], sponsor)) {
        std::cerr << "invalid PNG: " << argv[2] << '\n';
        return 2;
    }
    std::vector<std::vector<std::uint8_t>> resources(kExpectedResourceCount);
    for (int i = 0; i < kExpectedResourceCount; ++i) {
        if (!ReadPng(argv[i + 3], resources[static_cast<std::size_t>(i)])) {
            std::cerr << "invalid PNG: " << argv[i + 3] << '\n';
            return 2;
        }
    }

    const std::filesystem::path outputPath = argv[1];
    std::filesystem::create_directories(outputPath.parent_path());
    const std::filesystem::path temporaryPath = outputPath.string() + ".tmp";
    std::ofstream output(temporaryPath, std::ios::trunc);
    if (!output) return 3;
    output << "#pragma once\n#include <array>\n#include <cstddef>\n#include <cstdint>\n\n";
    output << "namespace inventory_resources::generated {\n";
    const auto writeBytes = [&output](const char* symbol, const std::vector<std::uint8_t>& bytes) {
        output << "inline constexpr std::uint8_t " << symbol << "[] = {\n";
        for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
            if (byte % 16 == 0) output << "    ";
            output << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(bytes[byte]) << ",";
            if (byte % 16 == 15) output << '\n';
        }
        output << std::dec << "\n};\n";
    };
    writeBytes("kSponsorPngData", sponsor);
    for (int i = 0; i < kExpectedResourceCount; ++i) {
        const std::string symbol = "kPng" + std::to_string(i);
        writeBytes(symbol.c_str(), resources[static_cast<std::size_t>(i)]);
    }
    output << "struct GeneratedPng { const std::uint8_t* data; std::size_t size; const char* sourceName; };\n";
    output << "inline constexpr GeneratedPng kSponsorPng{kSponsorPngData, sizeof(kSponsorPngData), \"wx_zfb.png\"};\n";
    output << "inline constexpr std::array<GeneratedPng, 63> kInventoryPngs{{\n";
    for (int i = 0; i < kExpectedResourceCount; ++i) {
        const std::string name = std::filesystem::path(argv[i + 3]).filename().string();
        output << "    {kPng" << i << ", sizeof(kPng" << i << "), \"" << name << "\"},\n";
    }
    output << "}};\n} // namespace inventory_resources::generated\n";
    output.close();
    if (!output) return 4;

    std::error_code error;
    std::filesystem::remove(outputPath, error);
    error.clear();
    std::filesystem::rename(temporaryPath, outputPath, error);
    if (error) {
        std::cerr << "cannot publish generated header: " << error.message() << '\n';
        return 5;
    }
    return 0;
}
