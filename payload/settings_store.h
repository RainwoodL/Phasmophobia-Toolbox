#pragma once

#include "timer_logic.h"

#include <array>
#include <cstdint>
#ifdef TOOLSBOX_SETTINGS_TESTING
#include <filesystem>
#endif

namespace settings_store {

enum class OverlayPlacementId : std::size_t {
    Smudge, GameTime, GhostStatus, Crucifix, CrosshairInfo,
    Inventory, Stamina, GhostSpeed, Sanity, OuijaTranslation, HuntTimer,
    GhostDistance, Count
};

struct OverlayPlacement {
    timer_logic::Position normalizedCenter{};
    bool initialized = false;
};

struct SmudgeSettings {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{20.0f, 20.0f};
    float fontSize = 26.0f;
    timer_logic::Color idleColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Color activeColor{1.0f, 1.0f, 0.0f, 1.0f};
    timer_logic::Color playerColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Color recordedTimeColor{1.0f, 1.0f, 1.0f, 1.0f};
};

struct GameTimeSettings {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{20.0f, 200.0f};
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct GhostStatusSettings {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{20.0f, 260.0f};
    float fontSize = 26.0f;
    timer_logic::Color waitingColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Color huntingColor{1.0f, 0.15f, 0.15f, 1.0f};
    bool flashing = true;
};

struct CrucifixSettings {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{20.0f, 100.0f};
    float fontSize = 26.0f;
    timer_logic::Color idleColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Color activeColor{1.0f, 0.65f, 0.3f, 1.0f};
    timer_logic::Color recordColor{1.0f, 1.0f, 1.0f, 1.0f};
};

struct CrosshairInfoSettings {
    bool enabled = false;
    bool fixed = false;
    timer_logic::Position position{};
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    bool showTier = true;
};

enum class CrosshairShape : int { FilledCircle = 0, HollowCircle = 1, Cross = 2 };

struct ScreenCrosshairSettings {
    bool enabled = false;
    CrosshairShape shape = CrosshairShape::FilledCircle;
    int radius = 2;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct PlayerNicknamesSettings {
    bool enabled = false;
    float height = 20.0f;
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct HuntTimerSettings {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{20.0f, 232.0f};
    bool positionInitialized = false;
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    int layout = 0;
};

struct InventoryBarSettings {
    bool enabled = false;
    bool fixed = false;
    float slotSize = 50.0f;
    float fontSize = 26.0f;
    timer_logic::Color highlightColor{1.0f, 1.0f, 1.0f, 1.0f};
    timer_logic::Position position{};
    bool positionInitialized = false;
    int layout = 0;
};

enum class MenuTheme : int { Dark = 0, Light = 1, DeepBlue = 2 };
enum class OverlayFrameRate : int { Low = 0, Medium = 1, High = 2, Ultra = 3 };

enum class FeatureId : std::size_t {
    Smudge, GameTime, GhostStatus, GhostSpeed, Crucifix, OuijaTranslation,
    CrosshairInfo, Inventory, Stamina, Sanity, ScreenCrosshair, PlayerNicknames,
    HuntTimer, GhostDistance, Count
};

enum class HotkeyAction : std::size_t {
    MenuToggle, LayoutModifier,
    ToggleSmudge, ToggleGameTime, ToggleGhostStatus, ToggleGhostSpeed,
    ToggleCrucifix, ToggleOuijaTranslation, ToggleCrosshairInfo, ToggleInventory,
    ToggleStamina, ToggleSanity, ToggleScreenCrosshair, TogglePlayerNicknames,
    SmudgeStart, SmudgeClear, CrucifixClear, ToggleHuntTimer,
    ToggleGhostDistance, Count
};

struct FeatureVisualSettings {
    float opacity = 1.0f;
};

struct HotkeyBinding {
    int key = 0;
    bool ctrl = false;
    bool shift = false;
};

struct LegacyMenuSettings {
    bool snapGuides = true;
    MenuTheme theme = MenuTheme::DeepBlue;
};

struct MenuSettings {
    bool snapGuides = true;
    bool blockGameInput = true;
    MenuTheme theme = MenuTheme::DeepBlue;
    OverlayFrameRate frameRate = OverlayFrameRate::High;
};

struct StaminaDisplaySettings {
    bool enabled = false;
    bool fixed = false;
    timer_logic::Position position{};
    bool positionInitialized = false;
    int removedDisplayTemplate = 0;
    float height = 26.0f;
    float width = 200.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct GhostSpeedDisplaySettings {
    bool enabled = false;
    bool fixed = false;
    timer_logic::Position position{};
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    bool positionInitialized = false;
};

struct GhostDistanceSettings {
    bool enabled = false;
    bool fixed = false;
    timer_logic::Position position{};
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    bool positionInitialized = false;
};

struct SanityDisplaySettings {
    bool enabled = false;
    bool fixed = false;
    timer_logic::Position position{};
    bool positionInitialized = false;
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct OuijaTranslationSettings {
    bool enabled = true;
    bool fixed = false;
    timer_logic::Position position{};
    bool positionInitialized = false;
    float fontSize = 26.0f;
    timer_logic::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SettingsV6 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    InventoryBarSettings inventoryBar;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
};

struct SettingsV7 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    InventoryBarSettings inventoryBar;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
    LegacyMenuSettings menu;
};

struct SettingsV8 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    InventoryBarSettings inventoryBar;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
    LegacyMenuSettings menu;
    StaminaDisplaySettings staminaDisplay;
};

struct SettingsV9 {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    InventoryBarSettings inventoryBar;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
    LegacyMenuSettings menu;
    StaminaDisplaySettings staminaDisplay;
};

struct Settings {
    SmudgeSettings smudge;
    GameTimeSettings gameTime;
    GhostStatusSettings ghostStatus;
    CrucifixSettings crucifix;
    CrosshairInfoSettings crosshairInfo;
    ScreenCrosshairSettings screenCrosshair;
    PlayerNicknamesSettings playerNicknames;
    InventoryBarSettings inventoryBar;
    timer_logic::Position menuPosition{};
    bool menuPositionInitialized = false;
    MenuSettings menu;
    StaminaDisplaySettings staminaDisplay;
    GhostSpeedDisplaySettings ghostSpeedDisplay;
    SanityDisplaySettings sanityDisplay;
    OuijaTranslationSettings ouijaTranslation;
    std::array<OverlayPlacement, static_cast<std::size_t>(OverlayPlacementId::Count)> overlayPlacements{};
    std::array<FeatureVisualSettings, static_cast<std::size_t>(FeatureId::Count)> featureVisuals{};
    std::array<HotkeyBinding, static_cast<std::size_t>(HotkeyAction::Count)> hotkeys{};
    OverlayPlacement menuPlacement{};
    std::uint32_t launchCount = 0;
    HuntTimerSettings huntTimer;
    GhostDistanceSettings ghostDistance;
};

Settings NormalizeSettings(Settings settings);
void ApplyDefaultHotkeys(Settings& settings);
void SetFeatureEnabled(Settings& settings, FeatureId feature, bool enabled);
void ResetFeatureSettings(Settings& settings, FeatureId feature);
void SetAllFeaturesEnabled(Settings& settings, bool enabled);
void ResetAllFeatureSettings(Settings& settings);
void ResetAllHotkeys(Settings& settings);
void ResetFeatureHotkeys(FeatureId feature);
Settings MigrateSettingsV6(const SettingsV6& settings);
Settings MigrateSettingsV7(const SettingsV7& settings);
Settings MigrateSettingsV8(const SettingsV8& settings);
Settings MigrateSettingsV9(const SettingsV9& settings);
bool Load();
bool NeedsInitialNewFeatureLayout();
bool NeedsInitialCrosshairInfoLayout();
bool NeedsInitialInventoryLayout();
bool NeedsInitialStaminaLayout();
Settings& Get();
void MarkDirty();
void Tick();
bool Flush();
bool LastSaveFailed();
#ifdef TOOLSBOX_SETTINGS_TESTING
namespace testing { void SetConfigPath(const std::filesystem::path& path); }
#endif
timer_logic::Position RestoreOverlayPosition(OverlayPlacementId id, timer_logic::Position fallback,
                                             timer_logic::Size window, timer_logic::Size viewport);
void RememberOverlayPosition(OverlayPlacementId id, timer_logic::Position position,
                             timer_logic::Size window, timer_logic::Size viewport);
void ClearOverlayPlacement(OverlayPlacementId id);

} // namespace settings_store
