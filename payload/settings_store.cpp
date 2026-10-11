#include "settings_store.h"
#include "save_schedule.h"
#include "feature_logic.h"
#include "settings_wire.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <io.h>
#include <vector>

namespace settings_store {
namespace {

constexpr std::uint32_t kMagic = 0x54424F58;
constexpr std::uint32_t kVersion = 21;
constexpr std::uint32_t kVersion20 = 20;
constexpr std::uint32_t kVersion19 = 19;
constexpr std::uint32_t kVersion18 = 18;
constexpr std::uint32_t kVersion17 = 17;
constexpr std::uint32_t kVersion16 = 16;
constexpr std::uint32_t kVersion15 = 15;
constexpr std::uint32_t kVersion14 = 14;
constexpr std::uint32_t kVersion13 = 13;
constexpr std::uint32_t kVersion12 = 12;
constexpr std::uint32_t kVersion11 = 11;
constexpr std::uint32_t kVersion10 = 10;
constexpr std::uint32_t kVersion9 = 9;
constexpr std::uint32_t kVersion8 = 8;
constexpr std::uint32_t kVersion7 = 7;
constexpr std::uint32_t kVersion6 = 6;
constexpr std::uint32_t kVersion5 = 5;
constexpr std::uint32_t kVersion4 = 4;
constexpr std::uint32_t kVersion3 = 3;
constexpr std::uint32_t kVersion2 = 2;
constexpr std::uint32_t kVersion1 = 1;
struct SettingsV1 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
};

struct SettingsV2 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
};

struct GhostStatusSettingsV3 {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{20.0f, 260.0f};
    float fontSize = 26.0f;
    timer_logic::Color waitingColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Color huntingColor{1.0f, 0.15f, 0.15f, 1.0f};
    bool flashing = true;
};

struct CrucifixSettingsV3 {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{20.0f, 100.0f};
    float fontSize = 26.0f;
    timer_logic::Color idleColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Color activeColor{1.0f, 0.65f, 0.3f, 1.0f};
    timer_logic::Color recordColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Color removedColor{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SettingsV3 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettingsV3 ghostStatus;
    CrucifixSettingsV3 crucifix;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
};

struct SettingsV4 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
};

struct SettingsV5 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
};

Settings g_settings{};
bool g_needsInitialNewFeatureLayout = false;
bool g_needsInitialCrosshairInfoLayout = false;
bool g_needsInitialInventoryLayout = false;
bool g_needsInitialStaminaLayout = false;
SaveSchedule g_saveSchedule;
bool g_saveFailed = false;
#ifdef TOOLSBOX_SETTINGS_TESTING
std::filesystem::path g_testConfigPath;
#endif

std::filesystem::path ConfigPath() {
#ifdef TOOLSBOX_SETTINGS_TESTING
    return g_testConfigPath;
#else
    wchar_t localAppData[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) return {};
    return std::filesystem::path(localAppData) / L"GhostRoomTools" / L"settings.bin";
#endif
}

} // namespace

Settings NormalizeSettings(Settings settings) {
#pragma warning(suppress: 4459) // Local alias is intentionally scoped to the value being normalized.
    Settings& g_settings = settings;
    const SmudgeSettings smudgeDefaults{};
    const GameTimeSettings gameDefaults{};
    const GhostStatusSettings ghostDefaults{};
    const CrucifixSettings crucifixDefaults{};
    g_settings.smudge.fontSize = timer_logic::SnapFontSize(g_settings.smudge.fontSize);
    g_settings.smudge.idleColor = timer_logic::ClampColor(g_settings.smudge.idleColor, smudgeDefaults.idleColor);
    g_settings.smudge.activeColor = timer_logic::ClampColor(g_settings.smudge.activeColor, smudgeDefaults.activeColor);
    g_settings.smudge.playerColor = timer_logic::ClampColor(g_settings.smudge.playerColor, smudgeDefaults.playerColor);
    g_settings.smudge.recordedTimeColor = timer_logic::ClampColor(g_settings.smudge.recordedTimeColor, smudgeDefaults.recordedTimeColor);
    g_settings.gameTime.fontSize = timer_logic::SnapFontSize(g_settings.gameTime.fontSize);
    g_settings.gameTime.color = timer_logic::ClampColor(g_settings.gameTime.color, gameDefaults.color);
    g_settings.ghostStatus.fontSize = timer_logic::SnapFontSize(g_settings.ghostStatus.fontSize);
    g_settings.ghostStatus.waitingColor = timer_logic::ClampColor(g_settings.ghostStatus.waitingColor, ghostDefaults.waitingColor);
    g_settings.ghostStatus.huntingColor = timer_logic::ClampColor(g_settings.ghostStatus.huntingColor, ghostDefaults.huntingColor);
    g_settings.crucifix.fontSize = timer_logic::SnapFontSize(g_settings.crucifix.fontSize);
    g_settings.crucifix.idleColor = timer_logic::ClampColor(g_settings.crucifix.idleColor, crucifixDefaults.idleColor);
    g_settings.crucifix.activeColor = timer_logic::ClampColor(g_settings.crucifix.activeColor, crucifixDefaults.activeColor);
    g_settings.crucifix.recordColor = timer_logic::ClampColor(g_settings.crucifix.recordColor, crucifixDefaults.recordColor);
    const CrosshairInfoSettings crosshairDefaults{};
    const ScreenCrosshairSettings screenDefaults{};
    const PlayerNicknamesSettings nicknameDefaults{};
    const InventoryBarSettings inventoryDefaults{};
    const StaminaDisplaySettings staminaDefaults{};
    const GhostSpeedDisplaySettings ghostSpeedDefaults{};
    const GhostDistanceSettings ghostDistanceDefaults{};
    const SanityDisplaySettings sanityDefaults{};
    const OuijaTranslationSettings ouijaDefaults{};
    const HuntTimerSettings huntTimerDefaults{};
    g_settings.crosshairInfo.fontSize = timer_logic::SnapFontSize(g_settings.crosshairInfo.fontSize);
    g_settings.crosshairInfo.color = timer_logic::ClampColor(g_settings.crosshairInfo.color, crosshairDefaults.color);
    g_settings.screenCrosshair.radius = feature_logic::ClampCrosshairRadius(g_settings.screenCrosshair.radius);
    const int shape = static_cast<int>(g_settings.screenCrosshair.shape);
    if (shape < 0 || shape > 2) g_settings.screenCrosshair.shape = screenDefaults.shape;
    g_settings.screenCrosshair.color = timer_logic::ClampColor(g_settings.screenCrosshair.color, screenDefaults.color);
    g_settings.playerNicknames.fontSize = timer_logic::SnapFontSize(g_settings.playerNicknames.fontSize);
    g_settings.playerNicknames.height = std::isfinite(g_settings.playerNicknames.height)
        ? std::clamp(g_settings.playerNicknames.height, 0.0f, 100.0f) : nicknameDefaults.height;
    g_settings.playerNicknames.color = timer_logic::ClampColor(g_settings.playerNicknames.color, nicknameDefaults.color);
    g_settings.inventoryBar.slotSize = std::isfinite(g_settings.inventoryBar.slotSize)
        ? std::clamp(g_settings.inventoryBar.slotSize, 50.0f, 96.0f) : inventoryDefaults.slotSize;
    g_settings.inventoryBar.fontSize = timer_logic::SnapFontSize(g_settings.inventoryBar.fontSize);
    g_settings.inventoryBar.highlightColor = timer_logic::ClampColor(
        g_settings.inventoryBar.highlightColor, inventoryDefaults.highlightColor);
    if (g_settings.inventoryBar.layout < 0 || g_settings.inventoryBar.layout > 1)
        g_settings.inventoryBar.layout = inventoryDefaults.layout;
    g_settings.staminaDisplay.removedDisplayTemplate = 0;
    g_settings.staminaDisplay.height = std::isfinite(g_settings.staminaDisplay.height)
        ? std::clamp(g_settings.staminaDisplay.height, 16.0f, 56.0f) : staminaDefaults.height;
    g_settings.staminaDisplay.width = std::isfinite(g_settings.staminaDisplay.width)
        ? std::clamp(g_settings.staminaDisplay.width, 100.0f, 500.0f) : staminaDefaults.width;
    g_settings.staminaDisplay.color = timer_logic::ClampColor(
        g_settings.staminaDisplay.color, staminaDefaults.color);
    g_settings.ghostSpeedDisplay.fontSize = timer_logic::SnapFontSize(g_settings.ghostSpeedDisplay.fontSize);
    g_settings.ghostSpeedDisplay.color = timer_logic::ClampColor(
        g_settings.ghostSpeedDisplay.color, ghostSpeedDefaults.color);
    g_settings.ghostDistance.fontSize = timer_logic::SnapFontSize(g_settings.ghostDistance.fontSize);
    g_settings.ghostDistance.color = timer_logic::ClampColor(
        g_settings.ghostDistance.color, ghostDistanceDefaults.color);
    g_settings.sanityDisplay.fontSize = timer_logic::SnapFontSize(g_settings.sanityDisplay.fontSize);
    g_settings.sanityDisplay.color = timer_logic::ClampColor(
        g_settings.sanityDisplay.color, sanityDefaults.color);
    g_settings.ouijaTranslation.fontSize = timer_logic::SnapFontSize(
        g_settings.ouijaTranslation.fontSize);
    g_settings.ouijaTranslation.color = timer_logic::ClampColor(
        g_settings.ouijaTranslation.color, ouijaDefaults.color);
    g_settings.huntTimer.fontSize = timer_logic::SnapFontSize(g_settings.huntTimer.fontSize);
    g_settings.huntTimer.color = timer_logic::ClampColor(
        g_settings.huntTimer.color, huntTimerDefaults.color);
    if (g_settings.huntTimer.layout < 0 || g_settings.huntTimer.layout > 1)
        g_settings.huntTimer.layout = huntTimerDefaults.layout;
    g_settings.smudge.position = timer_logic::ClampPosition(g_settings.smudge.position, {0, 0}, {16384, 16384});
    g_settings.gameTime.position = timer_logic::ClampPosition(g_settings.gameTime.position, {0, 0}, {16384, 16384});
    g_settings.ghostStatus.position = timer_logic::ClampPosition(g_settings.ghostStatus.position, {0, 0}, {16384, 16384});
    g_settings.crucifix.position = timer_logic::ClampPosition(g_settings.crucifix.position, {0, 0}, {16384, 16384});
    g_settings.crosshairInfo.position = timer_logic::ClampPosition(g_settings.crosshairInfo.position, {0, 0}, {16384, 16384});
    g_settings.inventoryBar.position = timer_logic::ClampPosition(g_settings.inventoryBar.position, {0, 0}, {16384, 16384});
    g_settings.staminaDisplay.position = timer_logic::ClampPosition(
        g_settings.staminaDisplay.position, {0, 0}, {16384, 16384});
    g_settings.ghostSpeedDisplay.position = timer_logic::ClampPosition(
        g_settings.ghostSpeedDisplay.position, {0, 0}, {16384, 16384});
    g_settings.ghostDistance.position = timer_logic::ClampPosition(
        g_settings.ghostDistance.position, {0, 0}, {16384, 16384});
    g_settings.sanityDisplay.position = timer_logic::ClampPosition(
        g_settings.sanityDisplay.position, {0, 0}, {16384, 16384});
    g_settings.ouijaTranslation.position = timer_logic::ClampPosition(
        g_settings.ouijaTranslation.position, {0, 0}, {16384, 16384});
    g_settings.huntTimer.position = timer_logic::ClampPosition(
        g_settings.huntTimer.position, {0, 0}, {16384, 16384});
    g_settings.menuPosition = timer_logic::ClampPosition(g_settings.menuPosition, {0, 0}, {16384, 16384});
    for (auto& placement : g_settings.overlayPlacements) {
        if (!std::isfinite(placement.normalizedCenter.x) || !std::isfinite(placement.normalizedCenter.y))
            placement = {};
        else {
            placement.normalizedCenter.x = std::clamp(placement.normalizedCenter.x, 0.0f, 1.0f);
            placement.normalizedCenter.y = std::clamp(placement.normalizedCenter.y, 0.0f, 1.0f);
        }
    }
    g_settings.menu.snapGuides = g_settings.menu.snapGuides == true;
    g_settings.menu.blockGameInput = g_settings.menu.blockGameInput == true;
    const int theme = static_cast<int>(g_settings.menu.theme);
    if (theme < static_cast<int>(MenuTheme::Dark) || theme > static_cast<int>(MenuTheme::DeepBlue))
        g_settings.menu.theme = MenuSettings{}.theme;
    const int frameRate = static_cast<int>(g_settings.menu.frameRate);
    if (frameRate < static_cast<int>(OverlayFrameRate::Low) ||
        frameRate > static_cast<int>(OverlayFrameRate::Ultra))
        g_settings.menu.frameRate = MenuSettings{}.frameRate;
    for (auto& visual : g_settings.featureVisuals) {
        visual.opacity = std::isfinite(visual.opacity)
            ? std::clamp(visual.opacity, 0.0f, 1.0f) : 1.0f;
    }
    for (auto& binding : g_settings.hotkeys) {
        if (binding.key < 0 || binding.key > 0xFE ||
            (binding.key >= VK_LBUTTON && binding.key <= VK_XBUTTON2)) binding = {};
    }
    if (!std::isfinite(g_settings.menuPlacement.normalizedCenter.x) ||
        !std::isfinite(g_settings.menuPlacement.normalizedCenter.y)) g_settings.menuPlacement = {};
    else {
        g_settings.menuPlacement.normalizedCenter.x = std::clamp(g_settings.menuPlacement.normalizedCenter.x, 0.0f, 1.0f);
        g_settings.menuPlacement.normalizedCenter.y = std::clamp(g_settings.menuPlacement.normalizedCenter.y, 0.0f, 1.0f);
    }
    return settings;
}

void ApplyDefaultHotkeys(Settings& settings) {
    auto& keys = settings.hotkeys;
    keys[static_cast<std::size_t>(HotkeyAction::MenuToggle)].key = 'U';
    keys[static_cast<std::size_t>(HotkeyAction::LayoutModifier)].key = VK_MENU;
    keys[static_cast<std::size_t>(HotkeyAction::SmudgeStart)].key = 'I';
    keys[static_cast<std::size_t>(HotkeyAction::SmudgeClear)].key = 'O';
    keys[static_cast<std::size_t>(HotkeyAction::CrucifixClear)].key = 'P';
}

void SetFeatureEnabled(Settings& settings, FeatureId feature, bool enabled) {
    switch (feature) {
    case FeatureId::Smudge: settings.smudge.enabled = enabled; break;
    case FeatureId::GameTime: settings.gameTime.enabled = enabled; break;
    case FeatureId::GhostStatus: settings.ghostStatus.enabled = enabled; break;
    case FeatureId::GhostSpeed: settings.ghostSpeedDisplay.enabled = enabled; break;
    case FeatureId::Crucifix: settings.crucifix.enabled = enabled; break;
    case FeatureId::OuijaTranslation: settings.ouijaTranslation.enabled = enabled; break;
    case FeatureId::CrosshairInfo: settings.crosshairInfo.enabled = enabled; break;
    case FeatureId::Inventory: settings.inventoryBar.enabled = enabled; break;
    case FeatureId::Stamina: settings.staminaDisplay.enabled = enabled; break;
    case FeatureId::Sanity: settings.sanityDisplay.enabled = enabled; break;
    case FeatureId::ScreenCrosshair: settings.screenCrosshair.enabled = enabled; break;
    case FeatureId::PlayerNicknames: settings.playerNicknames.enabled = enabled; break;
    case FeatureId::HuntTimer: settings.huntTimer.enabled = enabled; break;
    case FeatureId::GhostDistance: settings.ghostDistance.enabled = enabled; break;
    default: break;
    }
}

void ResetFeatureSettings(Settings& settings, FeatureId feature) {
    bool enabled = false;
    switch (feature) {
    case FeatureId::Smudge: enabled = settings.smudge.enabled; settings.smudge = {}; settings.smudge.enabled = enabled; break;
    case FeatureId::GameTime: enabled = settings.gameTime.enabled; settings.gameTime = {}; settings.gameTime.enabled = enabled; break;
    case FeatureId::GhostStatus: enabled = settings.ghostStatus.enabled; settings.ghostStatus = {}; settings.ghostStatus.enabled = enabled; break;
    case FeatureId::GhostSpeed: enabled = settings.ghostSpeedDisplay.enabled; settings.ghostSpeedDisplay = {}; settings.ghostSpeedDisplay.enabled = enabled; break;
    case FeatureId::Crucifix: enabled = settings.crucifix.enabled; settings.crucifix = {}; settings.crucifix.enabled = enabled; break;
    case FeatureId::OuijaTranslation: enabled = settings.ouijaTranslation.enabled; settings.ouijaTranslation = {}; settings.ouijaTranslation.enabled = enabled; break;
    case FeatureId::CrosshairInfo: enabled = settings.crosshairInfo.enabled; settings.crosshairInfo = {}; settings.crosshairInfo.enabled = enabled; break;
    case FeatureId::Inventory: enabled = settings.inventoryBar.enabled; settings.inventoryBar = {}; settings.inventoryBar.enabled = enabled; break;
    case FeatureId::Stamina: enabled = settings.staminaDisplay.enabled; settings.staminaDisplay = {}; settings.staminaDisplay.enabled = enabled; break;
    case FeatureId::Sanity: enabled = settings.sanityDisplay.enabled; settings.sanityDisplay = {}; settings.sanityDisplay.enabled = enabled; break;
    case FeatureId::ScreenCrosshair: enabled = settings.screenCrosshair.enabled; settings.screenCrosshair = {}; settings.screenCrosshair.enabled = enabled; break;
    case FeatureId::PlayerNicknames: enabled = settings.playerNicknames.enabled; settings.playerNicknames = {}; settings.playerNicknames.enabled = enabled; break;
    case FeatureId::HuntTimer: enabled = settings.huntTimer.enabled; settings.huntTimer = {}; settings.huntTimer.enabled = enabled; break;
    case FeatureId::GhostDistance: enabled = settings.ghostDistance.enabled; settings.ghostDistance = {}; settings.ghostDistance.enabled = enabled; break;
    default: return;
    }
    settings.featureVisuals[static_cast<std::size_t>(feature)] = {};
}

void SetAllFeaturesEnabled(Settings& settings, bool enabled) {
    for (std::size_t index = 0; index < static_cast<std::size_t>(FeatureId::Count); ++index)
        SetFeatureEnabled(settings, static_cast<FeatureId>(index), enabled);
}

void ResetAllFeatureSettings(Settings& settings) {
    for (std::size_t index = 0; index < static_cast<std::size_t>(FeatureId::Count); ++index)
        ResetFeatureSettings(settings, static_cast<FeatureId>(index));
}

void ResetAllHotkeys(Settings& settings) {
    settings.hotkeys = {};
    ApplyDefaultHotkeys(settings);
}

void ResetFeatureHotkeys(FeatureId feature) {
    auto& keys = g_settings.hotkeys;
    const auto clear = [&](HotkeyAction action) { keys[static_cast<std::size_t>(action)] = {}; };
    switch (feature) {
    case FeatureId::Smudge:
        clear(HotkeyAction::ToggleSmudge);
        clear(HotkeyAction::SmudgeStart);
        clear(HotkeyAction::SmudgeClear);
        break;
    case FeatureId::Crucifix:
        clear(HotkeyAction::ToggleCrucifix);
        clear(HotkeyAction::CrucifixClear);
        break;
    case FeatureId::GameTime: clear(HotkeyAction::ToggleGameTime); break;
    case FeatureId::GhostStatus: clear(HotkeyAction::ToggleGhostStatus); break;
    case FeatureId::GhostSpeed: clear(HotkeyAction::ToggleGhostSpeed); break;
    case FeatureId::OuijaTranslation: clear(HotkeyAction::ToggleOuijaTranslation); break;
    case FeatureId::CrosshairInfo: clear(HotkeyAction::ToggleCrosshairInfo); break;
    case FeatureId::Inventory: clear(HotkeyAction::ToggleInventory); break;
    case FeatureId::Stamina: clear(HotkeyAction::ToggleStamina); break;
    case FeatureId::Sanity: clear(HotkeyAction::ToggleSanity); break;
    case FeatureId::ScreenCrosshair: clear(HotkeyAction::ToggleScreenCrosshair); break;
    case FeatureId::PlayerNicknames: clear(HotkeyAction::TogglePlayerNicknames); break;
    case FeatureId::HuntTimer: clear(HotkeyAction::ToggleHuntTimer); break;
    case FeatureId::GhostDistance: clear(HotkeyAction::ToggleGhostDistance); break;
    default: break;
    }
    MarkDirty();
}

Settings MigrateSettingsV6(const SettingsV6& old) {
    Settings settings{};
    settings.smudge = old.smudge;
    settings.gameTime = old.gameTime;
    settings.ghostStatus = old.ghostStatus;
    settings.crucifix = old.crucifix;
    settings.crosshairInfo = old.crosshairInfo;
    settings.screenCrosshair = old.screenCrosshair;
    settings.playerNicknames = old.playerNicknames;
    settings.inventoryBar = old.inventoryBar;
    settings.menuPosition = old.menuPosition;
    settings.menuPositionInitialized = old.menuPositionInitialized;
    return settings;
}

Settings MigrateSettingsV7(const SettingsV7& old) {
    Settings settings{};
    settings.smudge = old.smudge;
    settings.gameTime = old.gameTime;
    settings.ghostStatus = old.ghostStatus;
    settings.crucifix = old.crucifix;
    settings.crosshairInfo = old.crosshairInfo;
    settings.screenCrosshair = old.screenCrosshair;
    settings.playerNicknames = old.playerNicknames;
    settings.inventoryBar = old.inventoryBar;
    settings.menuPosition = old.menuPosition;
    settings.menuPositionInitialized = old.menuPositionInitialized;
    settings.menu.snapGuides = old.menu.snapGuides;
    settings.menu.theme = old.menu.theme;
    return settings;
}

Settings MigrateSettingsV8(const SettingsV8& old) {
    Settings settings{};
    settings.smudge = old.smudge;
    settings.gameTime = old.gameTime;
    settings.ghostStatus = old.ghostStatus;
    settings.crucifix = old.crucifix;
    settings.crosshairInfo = old.crosshairInfo;
    settings.screenCrosshair = old.screenCrosshair;
    settings.playerNicknames = old.playerNicknames;
    settings.inventoryBar = old.inventoryBar;
    settings.menuPosition = old.menuPosition;
    settings.menuPositionInitialized = old.menuPositionInitialized;
    settings.menu.snapGuides = old.menu.snapGuides;
    settings.menu.theme = old.menu.theme;
    settings.staminaDisplay = old.staminaDisplay;
    return settings;
}

Settings MigrateSettingsV9(const SettingsV9& old) {
    Settings settings{};
    settings.smudge = old.smudge;
    settings.gameTime = old.gameTime;
    settings.ghostStatus = old.ghostStatus;
    settings.crucifix = old.crucifix;
    settings.crosshairInfo = old.crosshairInfo;
    settings.screenCrosshair = old.screenCrosshair;
    settings.playerNicknames = old.playerNicknames;
    settings.inventoryBar = old.inventoryBar;
    settings.menuPosition = old.menuPosition;
    settings.menuPositionInitialized = old.menuPositionInitialized;
    settings.menu.snapGuides = old.menu.snapGuides;
    settings.menu.theme = old.menu.theme;
    settings.staminaDisplay = old.staminaDisplay;
    return settings;
}

namespace {
bool Save() {
    const auto path = ConfigPath();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = path.parent_path() / L"settings.tmp";
    const Settings normalized = NormalizeSettings(g_settings);
    const auto payload = settings_wire::Encode(normalized);
    if (payload.size() != settings_wire::kPayloadSize) return false;
    const timer_logic::ConfigHeader header{
        kMagic, kVersion, static_cast<std::uint32_t>(payload.size())};
    const std::uint32_t checksum = settings_wire::Crc32(payload.data(), payload.size());
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0 || !file) return false;
    const bool written = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
        std::fwrite(&checksum, sizeof(checksum), 1, file) == 1 &&
        std::fwrite(payload.data(), payload.size(), 1, file) == 1 &&
        std::fflush(file) == 0 && _commit(_fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) { DeleteFileW(temporary.c_str()); return false; }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace

bool Load() {
    g_settings = {};
    ApplyDefaultHotkeys(g_settings);
    g_needsInitialNewFeatureLayout = true;
    g_needsInitialCrosshairInfoLayout = true;
    g_needsInitialInventoryLayout = true;
    g_needsInitialStaminaLayout = true;
    bool loadedFromDisk = false;
    std::uint32_t loadedVersion = 0;
    const auto path = ConfigPath();
    FILE* file = nullptr;
    if (!path.empty() && _wfopen_s(&file, path.c_str(), L"rb") == 0 && file) {
        timer_logic::ConfigHeader header{};
        if (std::fread(&header, sizeof(header), 1, file) == 1 && header.magic == kMagic) {
            loadedVersion = header.version;
            if (header.version == kVersion && header.payloadLength == settings_wire::kPayloadSize) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSize);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::Decode(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion20 && header.payloadLength == settings_wire::kPayloadSizeV20) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV20);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV20(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion19 && header.payloadLength == settings_wire::kPayloadSizeV19) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV19);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV19(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion18 && header.payloadLength == settings_wire::kPayloadSizeV18) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV18);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV18(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion17 && header.payloadLength == settings_wire::kPayloadSizeV17) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV17);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV17(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion16 && header.payloadLength == settings_wire::kPayloadSizeV16) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV16);
                Settings settings{};
                ApplyDefaultHotkeys(settings);
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV16(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion15 && header.payloadLength == settings_wire::kPayloadSizeV15) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV15);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV15(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion14 && header.payloadLength == settings_wire::kPayloadSizeV14) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV14);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV14(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion13 && header.payloadLength == settings_wire::kPayloadSizeV13) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV13);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV13(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion12 && header.payloadLength == settings_wire::kPayloadSizeV12) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV12);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV12(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion11 && header.payloadLength == settings_wire::kPayloadSizeV11) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV11);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV11(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion10 && header.payloadLength == settings_wire::kPayloadSizeV11) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV11);
                Settings settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV11(payload.data(), payload.size(), settings)) {
                    if (settings.ghostSpeedDisplay.positionInitialized)
                        settings.ghostSpeedDisplay.position.x -= 20.0f;
                    g_settings = NormalizeSettings(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion9 && header.payloadLength == settings_wire::kPayloadSizeV9) {
                std::uint32_t checksum = 0;
                std::vector<std::uint8_t> payload(settings_wire::kPayloadSizeV9);
                SettingsV9 settings{};
                if (std::fread(&checksum, sizeof(checksum), 1, file) == 1 &&
                    std::fread(payload.data(), payload.size(), 1, file) == 1 &&
                    std::fgetc(file) == EOF && !std::ferror(file) &&
                    settings_wire::Crc32(payload.data(), payload.size()) == checksum &&
                    settings_wire::DecodeV9(payload.data(), payload.size(), settings)) {
                    g_settings = NormalizeSettings(MigrateSettingsV9(settings));
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion8 && header.payloadLength == sizeof(SettingsV8)) {
                SettingsV8 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF && !std::ferror(file)) {
                    g_settings = MigrateSettingsV8(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = !settings.staminaDisplay.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion7 && header.payloadLength == sizeof(SettingsV7)) {
                SettingsV7 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
                    g_settings = MigrateSettingsV7(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    g_needsInitialStaminaLayout = true;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion6 && header.payloadLength == sizeof(SettingsV6)) {
                SettingsV6 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
                    g_settings = MigrateSettingsV6(settings);
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = !settings.inventoryBar.positionInitialized;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion5 && header.payloadLength == sizeof(SettingsV5)) {
                SettingsV5 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
                    g_settings.smudge = settings.smudge;
                    g_settings.gameTime = settings.gameTime;
                    g_settings.ghostStatus = settings.ghostStatus;
                    g_settings.crucifix = settings.crucifix;
                    g_settings.crosshairInfo = settings.crosshairInfo;
                    g_settings.screenCrosshair = settings.screenCrosshair;
                    g_settings.playerNicknames = settings.playerNicknames;
                    g_settings.menuPosition = settings.menuPosition;
                    g_settings.menuPositionInitialized = settings.menuPositionInitialized;
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = false;
                    g_needsInitialInventoryLayout = true;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion4 && header.payloadLength == sizeof(SettingsV4)) {
                SettingsV4 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
                    g_settings.smudge = settings.smudge;
                    g_settings.gameTime = settings.gameTime;
                    g_settings.ghostStatus = settings.ghostStatus;
                    g_settings.crucifix = settings.crucifix;
                    g_settings.menuPosition = settings.menuPosition;
                    g_settings.menuPositionInitialized = settings.menuPositionInitialized;
                    g_needsInitialNewFeatureLayout = false;
                    g_needsInitialCrosshairInfoLayout = true;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion3 && header.payloadLength == sizeof(SettingsV3)) {
                SettingsV3 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
                    g_settings.smudge = settings.smudge;
                    g_settings.gameTime = settings.gameTime;
                    g_settings.ghostStatus.enabled = settings.ghostStatus.enabled;
                    g_settings.ghostStatus.fixed = settings.ghostStatus.fixed;
                    g_settings.ghostStatus.position = settings.ghostStatus.position;
                    g_settings.ghostStatus.fontSize = settings.ghostStatus.fontSize;
                    g_settings.ghostStatus.waitingColor = settings.ghostStatus.waitingColor;
                    g_settings.ghostStatus.huntingColor = settings.ghostStatus.huntingColor;
                    g_settings.ghostStatus.flashing = settings.ghostStatus.flashing;
                    g_settings.crucifix.enabled = settings.crucifix.enabled;
                    g_settings.crucifix.fixed = settings.crucifix.fixed;
                    g_settings.crucifix.position = settings.crucifix.position;
                    g_settings.crucifix.fontSize = settings.crucifix.fontSize;
                    g_settings.crucifix.idleColor = settings.crucifix.idleColor;
                    g_settings.crucifix.activeColor = settings.crucifix.activeColor;
                    g_settings.crucifix.recordColor = settings.crucifix.recordColor;
                    g_settings.menuPosition = settings.menuPosition;
                    g_settings.menuPositionInitialized = settings.menuPositionInitialized;
                    g_needsInitialNewFeatureLayout = false;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion2 && header.payloadLength == sizeof(SettingsV2)) {
                SettingsV2 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
                    g_settings.smudge = settings.smudge;
                    g_settings.gameTime = settings.gameTime;
                    g_settings.menuPosition = settings.menuPosition;
                    g_settings.menuPositionInitialized = settings.menuPositionInitialized;
                    g_needsInitialNewFeatureLayout = true;
                    loadedFromDisk = true;
                }
            } else if (header.version == kVersion1 && header.payloadLength == sizeof(SettingsV1)) {
                SettingsV1 settings{};
                if (std::fread(&settings, sizeof(settings), 1, file) == 1 && std::fgetc(file) == EOF) {
                    g_settings.smudge = settings.smudge;
                    g_settings.gameTime = settings.gameTime;
                    g_settings.menuPositionInitialized = false;
                    g_needsInitialNewFeatureLayout = true;
                    loadedFromDisk = true;
                }
            }
        }
        std::fclose(file);
    }
    // Versions before 17 did not store hotkeys. Only those files need defaults;
    // an explicitly unbound key in a newer file is a valid user preference.
    if (loadedFromDisk && loadedVersion < kVersion17) ApplyDefaultHotkeys(g_settings);
    g_settings = NormalizeSettings(g_settings);
    g_saveSchedule = {};
    g_saveFailed = false;
    return loadedFromDisk;
}

bool NeedsInitialNewFeatureLayout() { return g_needsInitialNewFeatureLayout; }
bool NeedsInitialCrosshairInfoLayout() { return g_needsInitialCrosshairInfoLayout; }
bool NeedsInitialInventoryLayout() { return g_needsInitialInventoryLayout; }
bool NeedsInitialStaminaLayout() { return g_needsInitialStaminaLayout; }
Settings& Get() { return g_settings; }
void MarkDirty() { g_saveSchedule.MarkDirty(SaveSchedule::Clock::now()); }
void Tick() {
    if (g_saveSchedule.Due(SaveSchedule::Clock::now())) Flush();
}
bool Flush() {
    if (!g_saveSchedule.Pending()) return true;
    const bool saved = Save();
    g_saveFailed = !saved;
    g_saveSchedule.Complete(saved, SaveSchedule::Clock::now());
    return saved;
}
bool LastSaveFailed() { return g_saveFailed; }
#ifdef TOOLSBOX_SETTINGS_TESTING
void testing::SetConfigPath(const std::filesystem::path& path) { g_testConfigPath = path; }
#endif

timer_logic::Position RestoreOverlayPosition(OverlayPlacementId id, timer_logic::Position fallback,
                                             timer_logic::Size window, timer_logic::Size viewport) {
    auto& placement = g_settings.overlayPlacements[static_cast<std::size_t>(id)];
    if (viewport.width <= 0.0f || viewport.height <= 0.0f) return fallback;
    if (!placement.initialized) {
        placement.normalizedCenter = {
            (fallback.x + window.width * 0.5f) / viewport.width,
            (fallback.y + window.height * 0.5f) / viewport.height};
        placement.initialized = true;
        MarkDirty();
    }
    // ImGui stores pixel-aligned positions. Fractional restores otherwise fight
    // its truncation every frame and continuously dirty the configuration.
    const auto clamped = timer_logic::ClampPosition(
        {std::round(placement.normalizedCenter.x * viewport.width - window.width * 0.5f),
         std::round(placement.normalizedCenter.y * viewport.height - window.height * 0.5f)},
        window, viewport);
    return {std::floor(clamped.x), std::floor(clamped.y)};
}

void RememberOverlayPosition(OverlayPlacementId id, timer_logic::Position position,
                             timer_logic::Size window, timer_logic::Size viewport) {
    if (viewport.width <= 0.0f || viewport.height <= 0.0f) return;
    auto& placement = g_settings.overlayPlacements[static_cast<std::size_t>(id)];
    const timer_logic::Position normalized{
        std::clamp((position.x + window.width * 0.5f) / viewport.width, 0.0f, 1.0f),
        std::clamp((position.y + window.height * 0.5f) / viewport.height, 0.0f, 1.0f)};
    if (!placement.initialized || !(placement.normalizedCenter == normalized)) {
        placement.normalizedCenter = normalized;
        placement.initialized = true;
        MarkDirty();
    }
}

void ClearOverlayPlacement(OverlayPlacementId id) {
    g_settings.overlayPlacements[static_cast<std::size_t>(id)] = {};
    MarkDirty();
}

} // namespace settings_store
