#include "ghost_speed_sync_protocol.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using namespace ghost_speed_sync_protocol;

    std::array<std::uint8_t, kPacketSize> bytes{};
    Packet original{true, 17, 3.75f};
    assert(Encode(original, bytes));
    Packet decoded{};
    assert(Decode(bytes.data(), bytes.size(), decoded));
    assert(decoded.valid && decoded.sequence == original.sequence && decoded.speed == original.speed);

    assert(Encode(Packet{false, 18, 0.0f}, bytes));
    assert(Decode(bytes.data(), bytes.size(), decoded));
    assert(!decoded.valid && decoded.sequence == 18 && decoded.speed == 0.0f);

    assert(!Encode(Packet{true, 1, std::numeric_limits<float>::quiet_NaN()}, bytes));
    assert(!Decode(bytes.data(), kPacketSize - 1, decoded));
    bytes[4] = static_cast<std::uint8_t>(kVersion + 1);
    assert(!Decode(bytes.data(), bytes.size(), decoded));

    assert(IsNewer(2, 1));
    assert(!IsNewer(1, 1));
    assert(IsNewer(0, std::numeric_limits<std::uint32_t>::max()));
    assert(!IsNewer(std::numeric_limits<std::uint32_t>::max(), 0));
    return 0;
}
