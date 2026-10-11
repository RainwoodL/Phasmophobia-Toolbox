#pragma once

#include "settings_store.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace settings_wire {

constexpr std::size_t kPayloadSizeV9 = 377;
constexpr std::size_t kPayloadSizeV11 = 408;
constexpr std::size_t kPayloadSizeV12 = 409;
constexpr std::size_t kPayloadSizeV13 = 440;
constexpr std::size_t kPayloadSizeV14 = 521;
constexpr std::size_t kPayloadSizeV15 = 561;
constexpr std::size_t kPayloadSizeV16 = 562;
constexpr std::size_t kPayloadSizeV17 = 725;
constexpr std::size_t kPayloadSizeV18 = 776;
constexpr std::size_t kPayloadSizeV19 = 826;
constexpr std::size_t kPayloadSizeV20 = 776;
constexpr std::size_t kPayloadSize = 826;

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> Encode(const settings_store::Settings& settings);
bool Decode(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV20(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV18(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV19(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV17(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV15(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV16(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV11(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV12(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV13(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV14(const std::uint8_t* data, std::size_t size, settings_store::Settings& settings);
bool DecodeV9(const std::uint8_t* data, std::size_t size, settings_store::SettingsV9& settings);

} // namespace settings_wire
