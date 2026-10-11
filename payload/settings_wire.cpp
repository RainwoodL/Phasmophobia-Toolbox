#include "settings_wire.h"

#include <cstring>
#include <limits>

namespace settings_wire {
namespace {

void U8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }
void Bool(std::vector<std::uint8_t>& out, bool value) { U8(out, value ? 1u : 0u); }
void U32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) U8(out, static_cast<std::uint8_t>(value >> (i * 8)));
}
void I32(std::vector<std::uint8_t>& out, std::int32_t value) { U32(out, static_cast<std::uint32_t>(value)); }
void Float(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    U32(out, bits);
}
void Position(std::vector<std::uint8_t>& out, timer_logic::Position value) { Float(out, value.x); Float(out, value.y); }
void Color(std::vector<std::uint8_t>& out, timer_logic::Color value) {
    Float(out, value.r); Float(out, value.g); Float(out, value.b); Float(out, value.a);
}

struct Reader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t offset = 0;

    bool U8(std::uint8_t& value) {
        if (offset >= size) return false;
        value = data[offset++];
        return true;
    }
    bool Bool(bool& value) {
        std::uint8_t raw = 0;
        if (!U8(raw) || raw > 1) return false;
        value = raw != 0;
        return true;
    }
    bool U32(std::uint32_t& value) {
        if (size - offset < 4) return false;
        value = static_cast<std::uint32_t>(data[offset]) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return true;
    }
    bool I32(std::int32_t& value) {
        std::uint32_t raw = 0;
        if (!U32(raw)) return false;
        value = static_cast<std::int32_t>(raw);
        return true;
    }
    bool Float(float& value) {
        std::uint32_t bits = 0;
        if (!U32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool Position(timer_logic::Position& value) { return Float(value.x) && Float(value.y); }
    bool Color(timer_logic::Color& value) { return Float(value.r) && Float(value.g) && Float(value.b) && Float(value.a); }
};

template <typename SettingsType>
bool DecodeV9Prefix(Reader& r, SettingsType& decoded) {
    std::uint8_t shape = 0, layout = 0, theme = 0;
    std::int32_t radius = 0;
    const bool ok =
        r.Bool(decoded.smudge.enabled) && r.Bool(decoded.smudge.fixed) && r.Position(decoded.smudge.position) && r.Float(decoded.smudge.fontSize) &&
        r.Color(decoded.smudge.idleColor) && r.Color(decoded.smudge.activeColor) && r.Color(decoded.smudge.playerColor) && r.Color(decoded.smudge.recordedTimeColor) &&
        r.Bool(decoded.gameTime.enabled) && r.Bool(decoded.gameTime.fixed) && r.Position(decoded.gameTime.position) && r.Float(decoded.gameTime.fontSize) && r.Color(decoded.gameTime.color) &&
        r.Bool(decoded.ghostStatus.enabled) && r.Bool(decoded.ghostStatus.fixed) && r.Position(decoded.ghostStatus.position) && r.Float(decoded.ghostStatus.fontSize) &&
        r.Color(decoded.ghostStatus.waitingColor) && r.Color(decoded.ghostStatus.huntingColor) && r.Bool(decoded.ghostStatus.flashing) &&
        r.Bool(decoded.crucifix.enabled) && r.Bool(decoded.crucifix.fixed) && r.Position(decoded.crucifix.position) && r.Float(decoded.crucifix.fontSize) &&
        r.Color(decoded.crucifix.idleColor) && r.Color(decoded.crucifix.activeColor) && r.Color(decoded.crucifix.recordColor) &&
        r.Bool(decoded.crosshairInfo.enabled) && r.Bool(decoded.crosshairInfo.fixed) && r.Position(decoded.crosshairInfo.position) && r.Float(decoded.crosshairInfo.fontSize) &&
        r.Color(decoded.crosshairInfo.color) && r.Bool(decoded.crosshairInfo.showTier) &&
        r.Bool(decoded.screenCrosshair.enabled) && r.U8(shape) && r.I32(radius) && r.Color(decoded.screenCrosshair.color) &&
        r.Bool(decoded.playerNicknames.enabled) && r.Float(decoded.playerNicknames.height) && r.Float(decoded.playerNicknames.fontSize) && r.Color(decoded.playerNicknames.color) &&
        r.Bool(decoded.inventoryBar.enabled) && r.Bool(decoded.inventoryBar.fixed) && r.Float(decoded.inventoryBar.slotSize) && r.Float(decoded.inventoryBar.fontSize) &&
        r.Color(decoded.inventoryBar.highlightColor) && r.Position(decoded.inventoryBar.position) && r.Bool(decoded.inventoryBar.positionInitialized) && r.U8(layout) &&
        r.Position(decoded.menuPosition) && r.Bool(decoded.menuPositionInitialized) && r.Bool(decoded.menu.snapGuides) && r.U8(theme) &&
        r.Bool(decoded.staminaDisplay.enabled) && r.Bool(decoded.staminaDisplay.fixed) && r.Position(decoded.staminaDisplay.position) &&
        r.Bool(decoded.staminaDisplay.positionInitialized) && r.Float(decoded.staminaDisplay.height) && r.Float(decoded.staminaDisplay.width) && r.Color(decoded.staminaDisplay.color);
    if (!ok || shape > 2 || layout > 1 || theme > 2) return false;
    decoded.screenCrosshair.shape = static_cast<settings_store::CrosshairShape>(shape);
    decoded.screenCrosshair.radius = radius;
    decoded.inventoryBar.layout = layout;
    decoded.menu.theme = static_cast<settings_store::MenuTheme>(theme);
    decoded.staminaDisplay.removedDisplayTemplate = 0;
    return true;
}

} // namespace

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<std::uint8_t> Encode(const settings_store::Settings& s) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    std::vector<std::uint8_t> out;
    out.reserve(kPayloadSize);
    Bool(out, s.smudge.enabled); Bool(out, s.smudge.fixed); Position(out, s.smudge.position); Float(out, s.smudge.fontSize);
    Color(out, s.smudge.idleColor); Color(out, s.smudge.activeColor); Color(out, s.smudge.playerColor); Color(out, s.smudge.recordedTimeColor);
    Bool(out, s.gameTime.enabled); Bool(out, s.gameTime.fixed); Position(out, s.gameTime.position); Float(out, s.gameTime.fontSize); Color(out, s.gameTime.color);
    Bool(out, s.ghostStatus.enabled); Bool(out, s.ghostStatus.fixed); Position(out, s.ghostStatus.position); Float(out, s.ghostStatus.fontSize);
    Color(out, s.ghostStatus.waitingColor); Color(out, s.ghostStatus.huntingColor); Bool(out, s.ghostStatus.flashing);
    Bool(out, s.crucifix.enabled); Bool(out, s.crucifix.fixed); Position(out, s.crucifix.position); Float(out, s.crucifix.fontSize);
    Color(out, s.crucifix.idleColor); Color(out, s.crucifix.activeColor); Color(out, s.crucifix.recordColor);
    Bool(out, s.crosshairInfo.enabled); Bool(out, s.crosshairInfo.fixed); Position(out, s.crosshairInfo.position); Float(out, s.crosshairInfo.fontSize);
    Color(out, s.crosshairInfo.color); Bool(out, s.crosshairInfo.showTier);
    Bool(out, s.screenCrosshair.enabled); U8(out, static_cast<std::uint8_t>(s.screenCrosshair.shape)); I32(out, s.screenCrosshair.radius); Color(out, s.screenCrosshair.color);
    Bool(out, s.playerNicknames.enabled); Float(out, s.playerNicknames.height); Float(out, s.playerNicknames.fontSize); Color(out, s.playerNicknames.color);
    Bool(out, s.inventoryBar.enabled); Bool(out, s.inventoryBar.fixed); Float(out, s.inventoryBar.slotSize); Float(out, s.inventoryBar.fontSize);
    Color(out, s.inventoryBar.highlightColor); Position(out, s.inventoryBar.position); Bool(out, s.inventoryBar.positionInitialized); U8(out, static_cast<std::uint8_t>(s.inventoryBar.layout));
    Position(out, s.menuPosition); Bool(out, s.menuPositionInitialized); Bool(out, s.menu.snapGuides); U8(out, static_cast<std::uint8_t>(s.menu.theme));
    Bool(out, s.staminaDisplay.enabled); Bool(out, s.staminaDisplay.fixed); Position(out, s.staminaDisplay.position); Bool(out, s.staminaDisplay.positionInitialized);
    Float(out, s.staminaDisplay.height); Float(out, s.staminaDisplay.width); Color(out, s.staminaDisplay.color);
    Bool(out, s.ghostSpeedDisplay.enabled); Bool(out, s.ghostSpeedDisplay.fixed); Position(out, s.ghostSpeedDisplay.position);
    Float(out, s.ghostSpeedDisplay.fontSize); Color(out, s.ghostSpeedDisplay.color); Bool(out, s.ghostSpeedDisplay.positionInitialized);
    Bool(out, s.menu.blockGameInput);
    Bool(out, s.sanityDisplay.enabled); Bool(out, s.sanityDisplay.fixed);
    Position(out, s.sanityDisplay.position); Bool(out, s.sanityDisplay.positionInitialized);
    Float(out, s.sanityDisplay.fontSize); Color(out, s.sanityDisplay.color);
    Bool(out, s.ouijaTranslation.enabled); Bool(out, s.ouijaTranslation.fixed);
    Position(out, s.ouijaTranslation.position); Bool(out, s.ouijaTranslation.positionInitialized);
    Float(out, s.ouijaTranslation.fontSize); Color(out, s.ouijaTranslation.color);
    constexpr std::size_t kPlacementCountV17 = 10;
    constexpr std::size_t kFeatureCountV17 = 12;
    constexpr std::size_t kHotkeyCountV17 = 17;
    for (std::size_t index = 0; index < kPlacementCountV17; ++index) {
        const auto& placement = s.overlayPlacements[index];
        Position(out, placement.normalizedCenter);
        Bool(out, placement.initialized);
    }
    U8(out, static_cast<std::uint8_t>(s.menu.frameRate));
    for (std::size_t index = 0; index < kFeatureCountV17; ++index)
        Float(out, s.featureVisuals[index].opacity);
    for (std::size_t index = 0; index < kHotkeyCountV17; ++index) {
        const auto& binding = s.hotkeys[index];
        I32(out, binding.key); Bool(out, binding.ctrl); Bool(out, binding.shift);
    }
    Position(out, s.menuPlacement.normalizedCenter);
    Bool(out, s.menuPlacement.initialized);
    U32(out, s.launchCount);
    const auto& huntPlacement = s.overlayPlacements[
        static_cast<std::size_t>(settings_store::OverlayPlacementId::HuntTimer)];
    Position(out, huntPlacement.normalizedCenter); Bool(out, huntPlacement.initialized);
    Float(out, s.featureVisuals[
        static_cast<std::size_t>(settings_store::FeatureId::HuntTimer)].opacity);
    const auto& huntHotkey = s.hotkeys[
        static_cast<std::size_t>(settings_store::HotkeyAction::ToggleHuntTimer)];
    I32(out, huntHotkey.key); Bool(out, huntHotkey.ctrl); Bool(out, huntHotkey.shift);
    Bool(out, s.huntTimer.enabled); Bool(out, s.huntTimer.fixed);
    Position(out, s.huntTimer.position); Bool(out, s.huntTimer.positionInitialized);
    Float(out, s.huntTimer.fontSize); Color(out, s.huntTimer.color);
    U8(out, static_cast<std::uint8_t>(s.huntTimer.layout));
    Bool(out, s.ghostDistance.enabled); Bool(out, s.ghostDistance.fixed);
    Position(out, s.ghostDistance.position); Float(out, s.ghostDistance.fontSize);
    Color(out, s.ghostDistance.color); Bool(out, s.ghostDistance.positionInitialized);
    const auto& distancePlacement = s.overlayPlacements[
        static_cast<std::size_t>(settings_store::OverlayPlacementId::GhostDistance)];
    Position(out, distancePlacement.normalizedCenter); Bool(out, distancePlacement.initialized);
    Float(out, s.featureVisuals[
        static_cast<std::size_t>(settings_store::FeatureId::GhostDistance)].opacity);
    const auto& distanceHotkey = s.hotkeys[
        static_cast<std::size_t>(settings_store::HotkeyAction::ToggleGhostDistance)];
    I32(out, distanceHotkey.key); Bool(out, distanceHotkey.ctrl); Bool(out, distanceHotkey.shift);
    return out;
}

bool DecodeV20(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV20) return false;
    Reader r{data, size};
    settings_store::Settings decoded{};
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || !r.Bool(decoded.menu.blockGameInput) ||
        !r.Bool(decoded.sanityDisplay.enabled) || !r.Bool(decoded.sanityDisplay.fixed) ||
        !r.Position(decoded.sanityDisplay.position) || !r.Bool(decoded.sanityDisplay.positionInitialized) ||
        !r.Float(decoded.sanityDisplay.fontSize) || !r.Color(decoded.sanityDisplay.color) ||
        !r.Bool(decoded.ouijaTranslation.enabled) || !r.Bool(decoded.ouijaTranslation.fixed) ||
        !r.Position(decoded.ouijaTranslation.position) ||
        !r.Bool(decoded.ouijaTranslation.positionInitialized) ||
        !r.Float(decoded.ouijaTranslation.fontSize) || !r.Color(decoded.ouijaTranslation.color) ||
        r.offset > size) return false;
    constexpr std::size_t kPlacementCountV17 = 10;
    constexpr std::size_t kFeatureCountV17 = 12;
    constexpr std::size_t kHotkeyCountV17 = 17;
    for (std::size_t index = 0; index < kPlacementCountV17; ++index) {
        auto& placement = decoded.overlayPlacements[index];
        if (!r.Position(placement.normalizedCenter) || !r.Bool(placement.initialized)) return false;
    }
    std::uint8_t frameRate = 0;
    if (!r.U8(frameRate) || frameRate > 3) return false;
    decoded.menu.frameRate = static_cast<settings_store::OverlayFrameRate>(frameRate);
    for (std::size_t index = 0; index < kFeatureCountV17; ++index)
        if (!r.Float(decoded.featureVisuals[index].opacity)) return false;
    for (std::size_t index = 0; index < kHotkeyCountV17; ++index) {
        auto& binding = decoded.hotkeys[index];
        std::int32_t key = 0;
        if (!r.I32(key) || !r.Bool(binding.ctrl) || !r.Bool(binding.shift)) return false;
        binding.key = key;
    }
    std::uint8_t huntLayout = 0;
    auto& huntPlacement = decoded.overlayPlacements[
        static_cast<std::size_t>(settings_store::OverlayPlacementId::HuntTimer)];
    auto& huntVisual = decoded.featureVisuals[
        static_cast<std::size_t>(settings_store::FeatureId::HuntTimer)];
    auto& huntHotkey = decoded.hotkeys[
        static_cast<std::size_t>(settings_store::HotkeyAction::ToggleHuntTimer)];
    std::int32_t huntKey = 0;
    if (!r.Position(decoded.menuPlacement.normalizedCenter) ||
        !r.Bool(decoded.menuPlacement.initialized) || !r.U32(decoded.launchCount) ||
        !r.Position(huntPlacement.normalizedCenter) || !r.Bool(huntPlacement.initialized) ||
        !r.Float(huntVisual.opacity) || !r.I32(huntKey) ||
        !r.Bool(huntHotkey.ctrl) || !r.Bool(huntHotkey.shift) ||
        !r.Bool(decoded.huntTimer.enabled) || !r.Bool(decoded.huntTimer.fixed) ||
        !r.Position(decoded.huntTimer.position) ||
        !r.Bool(decoded.huntTimer.positionInitialized) ||
        !r.Float(decoded.huntTimer.fontSize) || !r.Color(decoded.huntTimer.color) ||
        !r.U8(huntLayout) || huntLayout > 1 ||
        r.offset != size) return false;
    huntHotkey.key = huntKey;
    decoded.huntTimer.layout = huntLayout;
    s = decoded;
    return true;
}

bool Decode(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSize) return false;
    settings_store::Settings decoded{};
    if (!DecodeV20(data, kPayloadSizeV20, decoded)) return false;
    Reader r{data + kPayloadSizeV20, size - kPayloadSizeV20};
    auto& placement = decoded.overlayPlacements[
        static_cast<std::size_t>(settings_store::OverlayPlacementId::GhostDistance)];
    auto& visual = decoded.featureVisuals[
        static_cast<std::size_t>(settings_store::FeatureId::GhostDistance)];
    auto& hotkey = decoded.hotkeys[
        static_cast<std::size_t>(settings_store::HotkeyAction::ToggleGhostDistance)];
    std::int32_t key = 0;
    if (!r.Bool(decoded.ghostDistance.enabled) || !r.Bool(decoded.ghostDistance.fixed) ||
        !r.Position(decoded.ghostDistance.position) || !r.Float(decoded.ghostDistance.fontSize) ||
        !r.Color(decoded.ghostDistance.color) ||
        !r.Bool(decoded.ghostDistance.positionInitialized) ||
        !r.Position(placement.normalizedCenter) || !r.Bool(placement.initialized) ||
        !r.Float(visual.opacity) || !r.I32(key) ||
        !r.Bool(hotkey.ctrl) || !r.Bool(hotkey.shift) || r.offset != r.size) return false;
    hotkey.key = key;
    s = decoded;
    return true;
}

bool DecodeV18(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV18) return false;
    return DecodeV20(data, size, s);
}

bool DecodeV19(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    // Version 19 appended 50 bytes for a removed feature. Load verifies the CRC
    // over the complete old payload; only the surviving prefix is migrated.
    if (!data || size != kPayloadSizeV19) return false;
    return DecodeV20(data, kPayloadSizeV20, s);
}

bool DecodeV17(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV17) return false;
    Reader r{data, size};
    settings_store::Settings decoded{};
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || !r.Bool(decoded.menu.blockGameInput) ||
        !r.Bool(decoded.sanityDisplay.enabled) || !r.Bool(decoded.sanityDisplay.fixed) ||
        !r.Position(decoded.sanityDisplay.position) || !r.Bool(decoded.sanityDisplay.positionInitialized) ||
        !r.Float(decoded.sanityDisplay.fontSize) || !r.Color(decoded.sanityDisplay.color) ||
        !r.Bool(decoded.ouijaTranslation.enabled) || !r.Bool(decoded.ouijaTranslation.fixed) ||
        !r.Position(decoded.ouijaTranslation.position) ||
        !r.Bool(decoded.ouijaTranslation.positionInitialized) ||
        !r.Float(decoded.ouijaTranslation.fontSize) || !r.Color(decoded.ouijaTranslation.color)) return false;
    constexpr std::size_t kPlacementCountV17 = 10;
    constexpr std::size_t kFeatureCountV17 = 12;
    constexpr std::size_t kHotkeyCountV17 = 17;
    for (std::size_t index = 0; index < kPlacementCountV17; ++index) {
        auto& placement = decoded.overlayPlacements[index];
        if (!r.Position(placement.normalizedCenter) || !r.Bool(placement.initialized)) return false;
    }
    std::uint8_t frameRate = 0;
    if (!r.U8(frameRate) || frameRate > 3) return false;
    decoded.menu.frameRate = static_cast<settings_store::OverlayFrameRate>(frameRate);
    for (std::size_t index = 0; index < kFeatureCountV17; ++index)
        if (!r.Float(decoded.featureVisuals[index].opacity)) return false;
    for (std::size_t index = 0; index < kHotkeyCountV17; ++index) {
        std::int32_t key = 0;
        auto& binding = decoded.hotkeys[index];
        if (!r.I32(key) || !r.Bool(binding.ctrl) || !r.Bool(binding.shift)) return false;
        binding.key = key;
    }
    if (!r.Position(decoded.menuPlacement.normalizedCenter) ||
        !r.Bool(decoded.menuPlacement.initialized) || !r.U32(decoded.launchCount) ||
        r.offset != size) return false;
    s = decoded;
    return true;
}

bool DecodeV16(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV16) return false;
    Reader r{data, size};
    settings_store::Settings decoded = s;
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || !r.Bool(decoded.menu.blockGameInput) ||
        !r.Bool(decoded.sanityDisplay.enabled) || !r.Bool(decoded.sanityDisplay.fixed) ||
        !r.Position(decoded.sanityDisplay.position) || !r.Bool(decoded.sanityDisplay.positionInitialized) ||
        !r.Float(decoded.sanityDisplay.fontSize) || !r.Color(decoded.sanityDisplay.color) ||
        !r.Bool(decoded.ouijaTranslation.enabled) || !r.Bool(decoded.ouijaTranslation.fixed) ||
        !r.Position(decoded.ouijaTranslation.position) ||
        !r.Bool(decoded.ouijaTranslation.positionInitialized) ||
        !r.Float(decoded.ouijaTranslation.fontSize) || !r.Color(decoded.ouijaTranslation.color)) return false;
    constexpr std::size_t kPlacementCountV16 = 10;
    for (std::size_t index = 0; index < kPlacementCountV16; ++index) {
        auto& placement = decoded.overlayPlacements[index];
        if (!r.Position(placement.normalizedCenter) || !r.Bool(placement.initialized)) return false;
    }
    std::uint8_t frameRate = 0;
    if (!r.U8(frameRate) || frameRate > 3 || r.offset != size) return false;
    decoded.menu.frameRate = static_cast<settings_store::OverlayFrameRate>(frameRate);
    s = decoded;
    return true;
}

bool DecodeV15(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV15) return false;
    Reader r{data, size};
    settings_store::Settings decoded{};
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || !r.Bool(decoded.menu.blockGameInput) ||
        !r.Bool(decoded.sanityDisplay.enabled) || !r.Bool(decoded.sanityDisplay.fixed) ||
        !r.Position(decoded.sanityDisplay.position) || !r.Bool(decoded.sanityDisplay.positionInitialized) ||
        !r.Float(decoded.sanityDisplay.fontSize) || !r.Color(decoded.sanityDisplay.color) ||
        !r.Bool(decoded.ouijaTranslation.enabled) || !r.Bool(decoded.ouijaTranslation.fixed) ||
        !r.Position(decoded.ouijaTranslation.position) ||
        !r.Bool(decoded.ouijaTranslation.positionInitialized) ||
        !r.Float(decoded.ouijaTranslation.fontSize) || !r.Color(decoded.ouijaTranslation.color)) return false;
    constexpr std::size_t kPlacementCountV15 = 10;
    for (std::size_t index = 0; index < kPlacementCountV15; ++index) {
        auto& placement = decoded.overlayPlacements[index];
        if (!r.Position(placement.normalizedCenter) || !r.Bool(placement.initialized)) return false;
    }
    if (r.offset != size) return false;
    s = decoded;
    return true;
}

bool DecodeV14(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV14) return false;
    Reader r{data, size};
    settings_store::Settings decoded{};
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || !r.Bool(decoded.menu.blockGameInput) ||
        !r.Bool(decoded.sanityDisplay.enabled) || !r.Bool(decoded.sanityDisplay.fixed) ||
        !r.Position(decoded.sanityDisplay.position) || !r.Bool(decoded.sanityDisplay.positionInitialized) ||
        !r.Float(decoded.sanityDisplay.fontSize) || !r.Color(decoded.sanityDisplay.color)) return false;
    constexpr std::size_t legacyPlacementCount =
        static_cast<std::size_t>(settings_store::OverlayPlacementId::OuijaTranslation);
    for (std::size_t index = 0; index < legacyPlacementCount; ++index) {
        auto& placement = decoded.overlayPlacements[index];
        if (!r.Position(placement.normalizedCenter) || !r.Bool(placement.initialized)) return false;
    }
    if (r.offset != size) return false;
    s = decoded;
    return true;
}

bool DecodeV12(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV12) return false;
    Reader r{data, size};
    settings_store::Settings decoded{};
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || !r.Bool(decoded.menu.blockGameInput) ||
        r.offset != size) return false;
    s = decoded;
    return true;
}

bool DecodeV13(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV13) return false;
    Reader r{data, size};
    settings_store::Settings decoded{};
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || !r.Bool(decoded.menu.blockGameInput) ||
        !r.Bool(decoded.sanityDisplay.enabled) || !r.Bool(decoded.sanityDisplay.fixed) ||
        !r.Position(decoded.sanityDisplay.position) || !r.Bool(decoded.sanityDisplay.positionInitialized) ||
        !r.Float(decoded.sanityDisplay.fontSize) || !r.Color(decoded.sanityDisplay.color) ||
        r.offset != size) return false;
    s = decoded;
    return true;
}

bool DecodeV11(const std::uint8_t* data, std::size_t size, settings_store::Settings& s) {
    if (!data || size != kPayloadSizeV11) return false;
    Reader r{data, size};
    settings_store::Settings decoded{};
    if (!DecodeV9Prefix(r, decoded) || !r.Bool(decoded.ghostSpeedDisplay.enabled) ||
        !r.Bool(decoded.ghostSpeedDisplay.fixed) || !r.Position(decoded.ghostSpeedDisplay.position) ||
        !r.Float(decoded.ghostSpeedDisplay.fontSize) || !r.Color(decoded.ghostSpeedDisplay.color) ||
        !r.Bool(decoded.ghostSpeedDisplay.positionInitialized) || r.offset != size) return false;
    s = decoded;
    return true;
}

bool DecodeV9(const std::uint8_t* data, std::size_t size, settings_store::SettingsV9& s) {
    if (!data || size != kPayloadSizeV9) return false;
    Reader r{data, size};
    settings_store::SettingsV9 decoded{};
    if (!DecodeV9Prefix(r, decoded) || r.offset != size) return false;
    s = decoded;
    return true;
}

} // namespace settings_wire
