#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace {

template <typename Container>
void WriteArray(FILE* output, const char* name, const Container& data) {
    std::fprintf(output, "static const unsigned char %s[] = {", name);
    for (std::size_t index = 0; index < data.size(); ++index) {
        std::fprintf(output, "%s0x%02X", index ? "," : "", data[index]);
        if ((index + 1) % 16 == 0) std::fprintf(output, "\n");
    }
    std::fprintf(output, "};\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: gen_payload <input.dll> <output.h>\n");
        return 1;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 1;
    const std::istreambuf_iterator<char> end;
    const std::vector<std::uint8_t> data(std::istreambuf_iterator<char>(input), end);
    const bool readFailed = input.bad();
    input.close();
    if (readFailed || data.empty() ||
        data.size() > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) return 1;

    FILE* output = nullptr;
    if (fopen_s(&output, argv[2], "w") != 0 || !output) return 1;
    std::fprintf(output, "#pragma once\n#include <cstddef>\n");
    WriteArray(output, "kPayloadData", data);
    std::fprintf(output, "static const std::size_t kPayloadSize = sizeof(kPayloadData);\n");
    const bool writeOk = std::ferror(output) == 0;
    const bool flushOk = std::fflush(output) == 0;
    const bool closeOk = std::fclose(output) == 0;
    if (!writeOk || !flushOk || !closeOk) {
        std::remove(argv[2]);
        return 1;
    }
    std::fprintf(stderr, "Embedded raw payload (%zu bytes)\n", data.size());
    return 0;
}
