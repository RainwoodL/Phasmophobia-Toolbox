#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghost_speed_sync_protocol {

constexpr std::uint32_t kMagic = 0x47535044; // "GSPD"
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kValidFlag = 0x01;
constexpr std::size_t kPacketSize = 16;

struct Packet {
    bool valid = false;
    std::uint32_t sequence = 0;
    float speed = 0.0f;
};

bool Encode(const Packet& packet, std::array<std::uint8_t, kPacketSize>& output);
bool Decode(const std::uint8_t* data, std::size_t size, Packet& output);
bool IsNewer(std::uint32_t candidate, std::uint32_t previous);

} // namespace ghost_speed_sync_protocol
