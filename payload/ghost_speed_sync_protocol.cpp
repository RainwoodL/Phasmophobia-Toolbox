#include "ghost_speed_sync_protocol.h"

#include <cmath>
#include <cstring>

namespace ghost_speed_sync_protocol {
namespace {

constexpr float kMaximumSpeed = 20.0f;

} // namespace

bool Encode(const Packet& packet, std::array<std::uint8_t, kPacketSize>& output) {
    if (packet.valid && (!std::isfinite(packet.speed) || packet.speed < 0.0f || packet.speed > kMaximumSpeed)) {
        return false;
    }

    output.fill(0);
    const std::uint32_t magic = kMagic;
    const std::uint8_t version = kVersion;
    const std::uint8_t flags = packet.valid ? kValidFlag : 0;
    std::memcpy(output.data() + 0, &magic, sizeof(magic));
    std::memcpy(output.data() + 4, &version, sizeof(version));
    std::memcpy(output.data() + 5, &flags, sizeof(flags));
    std::memcpy(output.data() + 8, &packet.sequence, sizeof(packet.sequence));
    const float speed = packet.valid ? packet.speed : 0.0f;
    std::memcpy(output.data() + 12, &speed, sizeof(speed));
    return true;
}

bool Decode(const std::uint8_t* data, std::size_t size, Packet& output) {
    if (!data || size != kPacketSize) return false;

    std::uint32_t magic = 0;
    std::uint8_t version = 0;
    std::uint8_t flags = 0;
    std::memcpy(&magic, data + 0, sizeof(magic));
    std::memcpy(&version, data + 4, sizeof(version));
    std::memcpy(&flags, data + 5, sizeof(flags));
    if (magic != kMagic || version != kVersion || (flags & ~kValidFlag) != 0) return false;

    std::memcpy(&output.sequence, data + 8, sizeof(output.sequence));
    std::memcpy(&output.speed, data + 12, sizeof(output.speed));
    output.valid = (flags & kValidFlag) != 0;
    if (!output.valid) {
        output.speed = 0.0f;
        return true;
    }
    return std::isfinite(output.speed) && output.speed >= 0.0f && output.speed <= kMaximumSpeed;
}

bool IsNewer(std::uint32_t candidate, std::uint32_t previous) {
    return candidate != previous && static_cast<std::int32_t>(candidate - previous) > 0;
}

} // namespace ghost_speed_sync_protocol
