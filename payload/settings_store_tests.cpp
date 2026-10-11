#include "settings_store.h"
#include "settings_wire.h"
#include "save_schedule.h"

#include <windows.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (false)

int main() {
    using namespace settings_store;
    using namespace std::chrono_literals;
    const SaveSchedule::Clock::time_point epoch{};
    SaveSchedule schedule;
    CHECK(!schedule.Due(epoch + 10s));
    schedule.MarkDirty(epoch);
    CHECK(!schedule.Due(epoch + 999ms));
    CHECK(schedule.Due(epoch + 1s));
    for (int i = 1; i <= 20; ++i) schedule.MarkDirty(epoch + i * 100ms);
    CHECK(schedule.Due(epoch + 2s)); // Continuous edits cannot starve saving.
    schedule.Complete(false, epoch + 2s);
    schedule.MarkDirty(epoch + 3s);
    CHECK(schedule.Pending() && !schedule.Due(epoch + 3999ms));
    CHECK(schedule.Due(epoch + 4s));
    schedule.Complete(true, epoch + 4s);
    CHECK(!schedule.Pending() && !schedule.Due(epoch + 10s));
    static_assert(settings_wire::kPayloadSize == 826);
    static_assert(sizeof(SettingsV7) == 384);
    static_assert(offsetof(SettingsV7, menu) == 376);
    static_assert(sizeof(SettingsV9) == 428);
    static_assert(sizeof(Settings) == 984);
    static_assert(sizeof(SettingsV8) == 428);
    static_assert(offsetof(SettingsV9, staminaDisplay) == 384);
    static_assert(offsetof(Settings, ghostSpeedDisplay) == 432);
    static_assert(sizeof(GhostSpeedDisplaySettings) == 36);
    static_assert(sizeof(GhostDistanceSettings) == 36);
    static_assert(offsetof(Settings, ghostDistance) == 948);
    static_assert(offsetof(SettingsV8, staminaDisplay) == 384);
    static_assert(std::is_standard_layout_v<SettingsV6>);
    static_assert(sizeof(SettingsV6) == 376);
    static_assert(offsetof(SettingsV6, smudge) == 0);
    static_assert(offsetof(SettingsV6, gameTime) == 80);
    static_assert(offsetof(SettingsV6, ghostStatus) == 112);
    static_assert(offsetof(SettingsV6, crucifix) == 164);
    static_assert(offsetof(SettingsV6, crosshairInfo) == 228);
    static_assert(offsetof(SettingsV6, screenCrosshair) == 264);
    static_assert(offsetof(SettingsV6, playerNicknames) == 292);
    static_assert(offsetof(SettingsV6, inventoryBar) == 320);
    static_assert(offsetof(SettingsV6, menuPosition) == 364);
    static_assert(offsetof(SettingsV6, menuPositionInitialized) == 372);
    const Settings defaults{};
    CHECK(defaults.menu.snapGuides);
    CHECK(defaults.menu.blockGameInput);
    CHECK(defaults.menu.theme == MenuTheme::DeepBlue);
    CHECK(defaults.menu.frameRate == OverlayFrameRate::High);
    CHECK(defaults.featureVisuals[0].opacity == 1.0f);
    CHECK(!defaults.staminaDisplay.enabled);
    CHECK(defaults.staminaDisplay.removedDisplayTemplate == 0);
    CHECK(defaults.staminaDisplay.height == 26.0f);
    CHECK(defaults.staminaDisplay.width == 200.0f);
    CHECK(defaults.staminaDisplay.color.r == 1.0f && defaults.staminaDisplay.color.g == 1.0f &&
          defaults.staminaDisplay.color.b == 1.0f && defaults.staminaDisplay.color.a == 1.0f);
    CHECK(!defaults.ghostSpeedDisplay.enabled && !defaults.ghostSpeedDisplay.fixed);
    CHECK(!defaults.ghostSpeedDisplay.positionInitialized && defaults.ghostSpeedDisplay.fontSize == 26.0f);
    CHECK(defaults.ghostSpeedDisplay.color.r == 1.0f && defaults.ghostSpeedDisplay.color.g == 1.0f &&
           defaults.ghostSpeedDisplay.color.b == 1.0f && defaults.ghostSpeedDisplay.color.a == 1.0f);
    CHECK(!defaults.ghostDistance.enabled && !defaults.ghostDistance.fixed);
    CHECK(!defaults.ghostDistance.positionInitialized && defaults.ghostDistance.fontSize == 26.0f);
    CHECK(defaults.ghostDistance.color.r == 1.0f && defaults.ghostDistance.color.g == 1.0f &&
          defaults.ghostDistance.color.b == 1.0f && defaults.ghostDistance.color.a == 1.0f);
    CHECK(!defaults.sanityDisplay.enabled && !defaults.sanityDisplay.fixed);
    CHECK(!defaults.sanityDisplay.positionInitialized && defaults.sanityDisplay.fontSize == 26.0f);
    CHECK(defaults.sanityDisplay.color.r == 1.0f && defaults.sanityDisplay.color.g == 1.0f &&
          defaults.sanityDisplay.color.b == 1.0f && defaults.sanityDisplay.color.a == 1.0f);
    CHECK(defaults.ouijaTranslation.enabled && !defaults.ouijaTranslation.fixed);
    CHECK(!defaults.ouijaTranslation.positionInitialized && defaults.ouijaTranslation.fontSize == 26.0f);
    CHECK(defaults.ouijaTranslation.color.r == 1.0f && defaults.ouijaTranslation.color.g == 1.0f &&
          defaults.ouijaTranslation.color.b == 1.0f && defaults.ouijaTranslation.color.a == 1.0f);
    CHECK(defaults.huntTimer.enabled && !defaults.huntTimer.fixed);
    CHECK(!defaults.huntTimer.positionInitialized && defaults.huntTimer.fontSize == 26.0f);
    CHECK((defaults.huntTimer.position == timer_logic::Position{20.0f, 232.0f}));
    CHECK(defaults.huntTimer.layout == 0);
    CHECK(defaults.huntTimer.color.r == 1.0f && defaults.huntTimer.color.g == 1.0f &&
          defaults.huntTimer.color.b == 1.0f && defaults.huntTimer.color.a == 1.0f);

    Settings batch{};
    SetAllFeaturesEnabled(batch, false);
    CHECK(!batch.smudge.enabled && !batch.gameTime.enabled && !batch.huntTimer.enabled &&
          !batch.ghostDistance.enabled);
    CHECK(!batch.ghostStatus.enabled && !batch.crucifix.enabled &&
          !batch.ouijaTranslation.enabled);
    SetAllFeaturesEnabled(batch, true);
    CHECK(batch.smudge.enabled && batch.gameTime.enabled && batch.huntTimer.enabled &&
          batch.ghostDistance.enabled);
    CHECK(batch.ghostStatus.enabled && batch.crucifix.enabled &&
          batch.ouijaTranslation.enabled);
    batch.huntTimer.enabled = false;
    batch.huntTimer.fixed = true;
    batch.huntTimer.fontSize = 48.0f;
    batch.huntTimer.layout = 1;
    batch.huntTimer.color = {0.1f, 0.2f, 0.3f, 0.4f};
    batch.featureVisuals[static_cast<std::size_t>(FeatureId::HuntTimer)].opacity = 0.25f;
    batch.ghostDistance.fixed = true;
    batch.ghostDistance.fontSize = 48.0f;
    batch.ghostDistance.color = {0.1f, 0.2f, 0.3f, 0.4f};
    batch.featureVisuals[static_cast<std::size_t>(FeatureId::GhostDistance)].opacity = 0.35f;
    ResetAllFeatureSettings(batch);
    CHECK(!batch.huntTimer.enabled);
    CHECK(!batch.huntTimer.fixed && batch.huntTimer.fontSize == 26.0f && batch.huntTimer.layout == 0);
    CHECK(batch.huntTimer.color.r == 1.0f && batch.huntTimer.color.a == 1.0f);
    CHECK(batch.featureVisuals[static_cast<std::size_t>(FeatureId::HuntTimer)].opacity == 1.0f);
    CHECK(batch.ghostDistance.enabled && !batch.ghostDistance.fixed &&
          batch.ghostDistance.fontSize == 26.0f && batch.ghostDistance.color.r == 1.0f);
    CHECK(batch.featureVisuals[static_cast<std::size_t>(FeatureId::GhostDistance)].opacity == 1.0f);
    SetFeatureEnabled(batch, FeatureId::HuntTimer, true);
    CHECK(batch.huntTimer.enabled);
    SetFeatureEnabled(batch, FeatureId::HuntTimer, false);
    CHECK(!batch.huntTimer.enabled);
    SetFeatureEnabled(batch, FeatureId::GhostDistance, false);
    CHECK(!batch.ghostDistance.enabled);
    batch.hotkeys[static_cast<std::size_t>(HotkeyAction::ToggleHuntTimer)] = {'H', true, true};
    batch.hotkeys[static_cast<std::size_t>(HotkeyAction::ToggleGhostDistance)] = {'D', true, false};
    ResetAllHotkeys(batch);
    const auto resetHuntHotkey = batch.hotkeys[
        static_cast<std::size_t>(HotkeyAction::ToggleHuntTimer)];
    CHECK(resetHuntHotkey.key == 0 && !resetHuntHotkey.ctrl && !resetHuntHotkey.shift);
    const auto resetDistanceHotkey = batch.hotkeys[
        static_cast<std::size_t>(HotkeyAction::ToggleGhostDistance)];
    CHECK(resetDistanceHotkey.key == 0 && !resetDistanceHotkey.ctrl && !resetDistanceHotkey.shift);

    Settings invalid{};
    invalid.menu.snapGuides = false;
    invalid.menu.theme = static_cast<MenuTheme>(99);
    invalid.menu.frameRate = static_cast<OverlayFrameRate>(99);
    invalid.featureVisuals[0].opacity = 2.0f;
    invalid.featureVisuals[1].opacity = -1.0f;
    invalid.hotkeys[0].key = 1;
    const Settings normalized = NormalizeSettings(invalid);
    CHECK(!normalized.menu.snapGuides);
    CHECK(normalized.menu.theme == MenuTheme::DeepBlue);
    CHECK(normalized.menu.frameRate == OverlayFrameRate::High);
    CHECK(normalized.featureVisuals[0].opacity == 1.0f);
    CHECK(normalized.featureVisuals[1].opacity == 0.0f);
    CHECK(normalized.hotkeys[0].key == 0);
    invalid.menu.snapGuides = true;
    CHECK(NormalizeSettings(invalid).menu.snapGuides);
    invalid.staminaDisplay.removedDisplayTemplate = 99;
    invalid.staminaDisplay.height = -1.0f;
    invalid.staminaDisplay.width = 999.0f;
    invalid.staminaDisplay.color = {-1.0f, 2.0f, 0.5f, 3.0f};
    const Settings normalizedStamina = NormalizeSettings(invalid);
    CHECK(normalizedStamina.staminaDisplay.removedDisplayTemplate == 0);
    CHECK(normalizedStamina.staminaDisplay.height == 16.0f);
    CHECK(normalizedStamina.staminaDisplay.width == 500.0f);
    CHECK(normalizedStamina.staminaDisplay.color.r == 0.0f && normalizedStamina.staminaDisplay.color.g == 1.0f &&
          normalizedStamina.staminaDisplay.color.b == 0.5f && normalizedStamina.staminaDisplay.color.a == 1.0f);
    invalid.ghostSpeedDisplay.fontSize = 55.0f;
    invalid.ghostSpeedDisplay.color = {-1.0f, 2.0f, 0.5f, 3.0f};
    const Settings normalizedGhostSpeed = NormalizeSettings(invalid);
    CHECK(normalizedGhostSpeed.ghostSpeedDisplay.fontSize == 56.0f);
    CHECK(normalizedGhostSpeed.ghostSpeedDisplay.color.r == 0.0f && normalizedGhostSpeed.ghostSpeedDisplay.color.g == 1.0f &&
          normalizedGhostSpeed.ghostSpeedDisplay.color.b == 0.5f && normalizedGhostSpeed.ghostSpeedDisplay.color.a == 1.0f);
    invalid.ghostDistance.fontSize = 55.0f;
    invalid.ghostDistance.color = {-1.0f, 2.0f, 0.5f, 3.0f};
    const Settings normalizedGhostDistance = NormalizeSettings(invalid);
    CHECK(normalizedGhostDistance.ghostDistance.fontSize == 56.0f);
    CHECK(normalizedGhostDistance.ghostDistance.color.r == 0.0f &&
          normalizedGhostDistance.ghostDistance.color.g == 1.0f &&
          normalizedGhostDistance.ghostDistance.color.b == 0.5f &&
          normalizedGhostDistance.ghostDistance.color.a == 1.0f);

    SettingsV6 old{};
    old.smudge.enabled = false; old.smudge.position = {11.0f, 12.0f}; old.smudge.fontSize = 32.0f;
    old.gameTime.enabled = false; old.gameTime.position = {21.0f, 22.0f};
    old.ghostStatus.enabled = false; old.ghostStatus.flashing = false; old.ghostStatus.position = {31.0f, 32.0f};
    old.crucifix.enabled = false; old.crucifix.position = {41.0f, 42.0f};
    old.crosshairInfo.enabled = true; old.crosshairInfo.showTier = false; old.crosshairInfo.position = {51.0f, 52.0f};
    old.screenCrosshair.enabled = true; old.screenCrosshair.radius = 9;
    old.playerNicknames.enabled = true; old.playerNicknames.height = 33.0f;
    old.inventoryBar.enabled = true; old.inventoryBar.fixed = true; old.inventoryBar.position = {61.0f, 62.0f};
    old.inventoryBar.positionInitialized = true; old.inventoryBar.layout = 1;
    old.menuPosition = {71.0f, 72.0f}; old.menuPositionInitialized = true;

    const Settings migrated = MigrateSettingsV6(old);
    CHECK(std::memcmp(&migrated.smudge, &old.smudge, sizeof(old.smudge)) == 0);
    CHECK(std::memcmp(&migrated.gameTime, &old.gameTime, sizeof(old.gameTime)) == 0);
    CHECK(std::memcmp(&migrated.ghostStatus, &old.ghostStatus, sizeof(old.ghostStatus)) == 0);
    CHECK(std::memcmp(&migrated.crucifix, &old.crucifix, sizeof(old.crucifix)) == 0);
    CHECK(std::memcmp(&migrated.crosshairInfo, &old.crosshairInfo, sizeof(old.crosshairInfo)) == 0);
    CHECK(std::memcmp(&migrated.screenCrosshair, &old.screenCrosshair, sizeof(old.screenCrosshair)) == 0);
    CHECK(std::memcmp(&migrated.playerNicknames, &old.playerNicknames, sizeof(old.playerNicknames)) == 0);
    CHECK(std::memcmp(&migrated.inventoryBar, &old.inventoryBar, sizeof(old.inventoryBar)) == 0);
    CHECK(migrated.menuPosition == old.menuPosition && migrated.menuPositionInitialized);
    CHECK(migrated.menu.snapGuides);
    CHECK(migrated.menu.theme == MenuTheme::DeepBlue);

    SettingsV7 oldV7{};
    oldV7.smudge = old.smudge;
    oldV7.gameTime = old.gameTime;
    oldV7.ghostStatus = old.ghostStatus;
    oldV7.crucifix = old.crucifix;
    oldV7.crosshairInfo = old.crosshairInfo;
    oldV7.screenCrosshair = old.screenCrosshair;
    oldV7.playerNicknames = old.playerNicknames;
    oldV7.inventoryBar = old.inventoryBar;
    oldV7.menuPosition = old.menuPosition;
    oldV7.menuPositionInitialized = old.menuPositionInitialized;
    oldV7.menu.snapGuides = false;
    oldV7.menu.theme = MenuTheme::Light;
    const Settings migratedV7 = MigrateSettingsV7(oldV7);
    CHECK(std::memcmp(&migratedV7.smudge, &oldV7.smudge, sizeof(oldV7.smudge)) == 0);
    CHECK(std::memcmp(&migratedV7.gameTime, &oldV7.gameTime, sizeof(oldV7.gameTime)) == 0);
    CHECK(std::memcmp(&migratedV7.ghostStatus, &oldV7.ghostStatus, sizeof(oldV7.ghostStatus)) == 0);
    CHECK(std::memcmp(&migratedV7.crucifix, &oldV7.crucifix, sizeof(oldV7.crucifix)) == 0);
    CHECK(std::memcmp(&migratedV7.crosshairInfo, &oldV7.crosshairInfo, sizeof(oldV7.crosshairInfo)) == 0);
    CHECK(std::memcmp(&migratedV7.screenCrosshair, &oldV7.screenCrosshair, sizeof(oldV7.screenCrosshair)) == 0);
    CHECK(std::memcmp(&migratedV7.playerNicknames, &oldV7.playerNicknames, sizeof(oldV7.playerNicknames)) == 0);
    CHECK(std::memcmp(&migratedV7.inventoryBar, &oldV7.inventoryBar, sizeof(oldV7.inventoryBar)) == 0);
    CHECK(migratedV7.menuPosition == oldV7.menuPosition && migratedV7.menuPositionInitialized);
    CHECK(!migratedV7.menu.snapGuides && migratedV7.menu.theme == MenuTheme::Light);
    CHECK(!migratedV7.staminaDisplay.enabled);
    CHECK(!migratedV7.staminaDisplay.positionInitialized);
    CHECK(migratedV7.staminaDisplay.removedDisplayTemplate == 0);
    CHECK(migratedV7.staminaDisplay.height == 26.0f && migratedV7.staminaDisplay.width == 200.0f);

    SettingsV8 oldV8{};
    oldV8.smudge.enabled = false;
    oldV8.menu.theme = MenuTheme::Light;
    oldV8.staminaDisplay.enabled = true;
    oldV8.staminaDisplay.height = 42.0f;
    oldV8.staminaDisplay.width = 260.0f;
    const Settings migratedV8 = MigrateSettingsV8(oldV8);
    CHECK(!migratedV8.smudge.enabled && migratedV8.menu.theme == MenuTheme::Light);
    CHECK(migratedV8.staminaDisplay.enabled && migratedV8.staminaDisplay.height == 42.0f);
    CHECK(migratedV8.staminaDisplay.width == 260.0f);

    SettingsV9 oldV9{};
    oldV9.smudge.enabled = false;
    oldV9.menu.theme = MenuTheme::Light;
    oldV9.staminaDisplay.enabled = true;
    oldV9.staminaDisplay.height = 42.0f;
    const Settings migratedV9 = MigrateSettingsV9(oldV9);
    CHECK(!migratedV9.smudge.enabled && migratedV9.menu.theme == MenuTheme::Light);
    CHECK(migratedV9.staminaDisplay.enabled && migratedV9.staminaDisplay.height == 42.0f);
    CHECK(!migratedV9.ghostSpeedDisplay.enabled && !migratedV9.ghostSpeedDisplay.positionInitialized);
    CHECK(migratedV9.ghostSpeedDisplay.fontSize == 26.0f);

    CHECK(settings_wire::Crc32(nullptr, 0) == 0x00000000u);
    const char crcVector[] = "123456789";
    CHECK(settings_wire::Crc32(reinterpret_cast<const std::uint8_t*>(crcVector), 9) == 0xCBF43926u);
    Settings wireSource{};
    wireSource.smudge.enabled = false;
    wireSource.screenCrosshair.enabled = true;
    wireSource.screenCrosshair.shape = CrosshairShape::Cross;
    wireSource.screenCrosshair.radius = 11;
    wireSource.inventoryBar.layout = 1;
    wireSource.menu.theme = MenuTheme::Light;
    wireSource.menu.blockGameInput = false;
    wireSource.menu.frameRate = OverlayFrameRate::Ultra;
    wireSource.featureVisuals[static_cast<std::size_t>(FeatureId::Inventory)].opacity = 0.42f;
    wireSource.hotkeys[static_cast<std::size_t>(HotkeyAction::ToggleInventory)] = {'B', true, false};
    wireSource.menuPlacement = {{0.75f, 0.4f}, true};
    wireSource.launchCount = 149;
    wireSource.staminaDisplay.enabled = true;
    wireSource.staminaDisplay.height = 36.0f;
    wireSource.staminaDisplay.width = 260.0f;
    wireSource.ghostSpeedDisplay.enabled = true;
    wireSource.ghostSpeedDisplay.fixed = true;
    wireSource.ghostSpeedDisplay.position = {123.0f, 45.0f};
    wireSource.ghostSpeedDisplay.fontSize = 32.0f;
    wireSource.ghostSpeedDisplay.color = {0.1f, 0.2f, 0.3f, 0.4f};
    wireSource.ghostSpeedDisplay.positionInitialized = true;
    wireSource.sanityDisplay.enabled = true;
    wireSource.sanityDisplay.fixed = true;
    wireSource.sanityDisplay.position = {1500.0f, 20.0f};
    wireSource.sanityDisplay.positionInitialized = true;
    wireSource.sanityDisplay.fontSize = 26.0f;
    wireSource.sanityDisplay.color = {0.9f, 0.8f, 0.7f, 0.6f};
    wireSource.ouijaTranslation.enabled = true;
    wireSource.ouijaTranslation.fixed = true;
    wireSource.ouijaTranslation.position = {810.0f, 257.0f};
    wireSource.ouijaTranslation.positionInitialized = true;
    wireSource.ouijaTranslation.fontSize = 32.0f;
    wireSource.ouijaTranslation.color = {0.6f, 0.7f, 0.8f, 0.9f};
    wireSource.huntTimer.enabled = false;
    wireSource.huntTimer.fixed = true;
    wireSource.huntTimer.position = {444.0f, 555.0f};
    wireSource.huntTimer.positionInitialized = true;
    wireSource.huntTimer.fontSize = 42.0f;
    wireSource.huntTimer.color = {0.2f, 0.4f, 0.6f, 0.8f};
    wireSource.huntTimer.layout = 1;
    wireSource.featureVisuals[static_cast<std::size_t>(FeatureId::HuntTimer)].opacity = 0.73f;
    wireSource.hotkeys[static_cast<std::size_t>(HotkeyAction::ToggleHuntTimer)] = {'H', false, true};
    wireSource.overlayPlacements[static_cast<std::size_t>(OverlayPlacementId::HuntTimer)] = {
        {0.45f, 0.55f}, true};
    wireSource.overlayPlacements[static_cast<std::size_t>(OverlayPlacementId::Sanity)] = {
        {0.85f, 0.15f}, true};
    wireSource.ghostDistance.enabled = true;
    wireSource.ghostDistance.fixed = true;
    wireSource.ghostDistance.position = {321.0f, 54.0f};
    wireSource.ghostDistance.fontSize = 32.0f;
    wireSource.ghostDistance.color = {0.2f, 0.3f, 0.4f, 0.5f};
    wireSource.ghostDistance.positionInitialized = true;
    wireSource.featureVisuals[static_cast<std::size_t>(FeatureId::GhostDistance)].opacity = 0.64f;
    wireSource.hotkeys[static_cast<std::size_t>(HotkeyAction::ToggleGhostDistance)] = {'D', true, false};
    wireSource.overlayPlacements[static_cast<std::size_t>(OverlayPlacementId::GhostDistance)] = {
        {0.35f, 0.15f}, true};
    const auto encoded = settings_wire::Encode(wireSource);
    CHECK(encoded.size() == settings_wire::kPayloadSize);
    CHECK(encoded == settings_wire::Encode(wireSource));
    Settings decoded{};
    CHECK(settings_wire::Decode(encoded.data(), encoded.size(), decoded));
    CHECK(!decoded.smudge.enabled && decoded.screenCrosshair.enabled);
    CHECK(decoded.screenCrosshair.shape == CrosshairShape::Cross && decoded.screenCrosshair.radius == 11);
    CHECK(decoded.inventoryBar.layout == 1 && decoded.menu.theme == MenuTheme::Light);
    CHECK(!decoded.menu.blockGameInput);
    CHECK(decoded.menu.frameRate == OverlayFrameRate::Ultra);
    CHECK(decoded.featureVisuals[static_cast<std::size_t>(FeatureId::Inventory)].opacity == 0.42f);
    const auto decodedInventoryHotkey = decoded.hotkeys[static_cast<std::size_t>(HotkeyAction::ToggleInventory)];
    CHECK(decodedInventoryHotkey.key == 'B' && decodedInventoryHotkey.ctrl && !decodedInventoryHotkey.shift);
    CHECK(decoded.menuPlacement.initialized && decoded.menuPlacement.normalizedCenter.x == 0.75f);
    CHECK(decoded.launchCount == 149);
    CHECK(decoded.staminaDisplay.enabled && decoded.staminaDisplay.height == 36.0f && decoded.staminaDisplay.width == 260.0f);
    CHECK(decoded.ghostSpeedDisplay.enabled && decoded.ghostSpeedDisplay.fixed);
    CHECK((decoded.ghostSpeedDisplay.position == timer_logic::Position{123.0f, 45.0f}));
    CHECK(decoded.ghostSpeedDisplay.fontSize == 32.0f && decoded.ghostSpeedDisplay.positionInitialized);
    CHECK(decoded.sanityDisplay.enabled && decoded.sanityDisplay.fixed);
    CHECK((decoded.sanityDisplay.position == timer_logic::Position{1500.0f, 20.0f}));
    CHECK(decoded.sanityDisplay.positionInitialized && decoded.sanityDisplay.fontSize == 26.0f);
    CHECK(decoded.sanityDisplay.color.r == 0.9f && decoded.sanityDisplay.color.a == 0.6f);
    CHECK(decoded.ouijaTranslation.enabled && decoded.ouijaTranslation.fixed);
    CHECK((decoded.ouijaTranslation.position == timer_logic::Position{810.0f, 257.0f}));
    CHECK(decoded.ouijaTranslation.positionInitialized && decoded.ouijaTranslation.fontSize == 32.0f);
    CHECK(decoded.ouijaTranslation.color.r == 0.6f && decoded.ouijaTranslation.color.a == 0.9f);
    CHECK(!decoded.huntTimer.enabled && decoded.huntTimer.fixed);
    CHECK((decoded.huntTimer.position == timer_logic::Position{444.0f, 555.0f}));
    CHECK(decoded.huntTimer.positionInitialized && decoded.huntTimer.fontSize == 42.0f);
    CHECK(decoded.huntTimer.color.g == 0.4f && decoded.huntTimer.color.a == 0.8f);
    CHECK(decoded.huntTimer.layout == 1);
    CHECK(decoded.featureVisuals[static_cast<std::size_t>(FeatureId::HuntTimer)].opacity == 0.73f);
    const auto decodedHuntHotkey = decoded.hotkeys[
        static_cast<std::size_t>(HotkeyAction::ToggleHuntTimer)];
    CHECK(decodedHuntHotkey.key == 'H' && !decodedHuntHotkey.ctrl && decodedHuntHotkey.shift);
    CHECK(decoded.overlayPlacements[
        static_cast<std::size_t>(OverlayPlacementId::HuntTimer)].initialized);
    CHECK(decoded.ghostDistance.enabled && decoded.ghostDistance.fixed);
    CHECK((decoded.ghostDistance.position == timer_logic::Position{321.0f, 54.0f}));
    CHECK(decoded.ghostDistance.positionInitialized && decoded.ghostDistance.fontSize == 32.0f);
    CHECK(decoded.ghostDistance.color.g == 0.3f && decoded.ghostDistance.color.a == 0.5f);
    CHECK(decoded.featureVisuals[
        static_cast<std::size_t>(FeatureId::GhostDistance)].opacity == 0.64f);
    const auto decodedDistanceHotkey = decoded.hotkeys[
        static_cast<std::size_t>(HotkeyAction::ToggleGhostDistance)];
    CHECK(decodedDistanceHotkey.key == 'D' && decodedDistanceHotkey.ctrl && !decodedDistanceHotkey.shift);
    CHECK(decoded.overlayPlacements[
        static_cast<std::size_t>(OverlayPlacementId::GhostDistance)].initialized);
    Settings wireSourceV20 = wireSource;
    wireSourceV20.ghostDistance = {};
    wireSourceV20.overlayPlacements[
        static_cast<std::size_t>(OverlayPlacementId::GhostDistance)] = {};
    wireSourceV20.featureVisuals[
        static_cast<std::size_t>(FeatureId::GhostDistance)] = {};
    wireSourceV20.hotkeys[
        static_cast<std::size_t>(HotkeyAction::ToggleGhostDistance)] = {};
    const auto encodedV20Defaults = settings_wire::Encode(wireSourceV20);
    Settings decodedV18{};
    CHECK(settings_wire::DecodeV18(encoded.data(), settings_wire::kPayloadSizeV18, decodedV18));
    CHECK(settings_wire::Encode(decodedV18) == encodedV20Defaults);
    auto legacyV19 = std::vector<std::uint8_t>(
        encoded.begin(), encoded.begin() + settings_wire::kPayloadSizeV20);
    legacyV19.resize(settings_wire::kPayloadSizeV19, 0);
    legacyV19[settings_wire::kPayloadSizeV20 + 13] = 'G'; // Removed binding.
    legacyV19[settings_wire::kPayloadSizeV20 + 19] = 1; // Removed feature was enabled.
    Settings decodedV19{};
    CHECK(settings_wire::DecodeV19(legacyV19.data(), legacyV19.size(), decodedV19));
    CHECK(settings_wire::Encode(decodedV19) == encodedV20Defaults);
    CHECK(!settings_wire::DecodeV19(legacyV19.data(), legacyV19.size() - 1, decodedV19));
    Settings decodedV17{};
    CHECK(settings_wire::DecodeV17(encoded.data(), settings_wire::kPayloadSizeV17, decodedV17));
    CHECK(decodedV17.huntTimer.enabled && !decodedV17.huntTimer.fixed);
    CHECK(!decodedV17.huntTimer.positionInitialized && decodedV17.huntTimer.fontSize == 26.0f);
    const auto& decodedPlacement = decoded.overlayPlacements[
        static_cast<std::size_t>(OverlayPlacementId::Sanity)];
    CHECK(decodedPlacement.initialized);
    const timer_logic::Position expectedSanityCenter{0.85f, 0.15f};
    CHECK(decodedPlacement.normalizedCenter == expectedSanityCenter);
    Settings decodedV13{};
    CHECK(settings_wire::DecodeV13(encoded.data(), settings_wire::kPayloadSizeV13, decodedV13));
    CHECK(!decodedV13.overlayPlacements[
        static_cast<std::size_t>(OverlayPlacementId::Sanity)].initialized);
    std::vector<std::uint8_t> legacyV14;
    legacyV14.insert(legacyV14.end(), encoded.begin(),
                     encoded.begin() + settings_wire::kPayloadSizeV13);
    constexpr std::size_t currentPlacementsOffset = settings_wire::kPayloadSizeV13 + 31;
    legacyV14.insert(legacyV14.end(), encoded.begin() + currentPlacementsOffset,
                     encoded.begin() + currentPlacementsOffset + 81);
    CHECK(legacyV14.size() == settings_wire::kPayloadSizeV14);
    Settings decodedV14{};
    CHECK(settings_wire::DecodeV14(legacyV14.data(), legacyV14.size(), decodedV14));
    CHECK(decodedV14.ouijaTranslation.enabled && !decodedV14.ouijaTranslation.positionInitialized);
    Settings decodedV12{};
    CHECK(settings_wire::DecodeV12(encoded.data(), settings_wire::kPayloadSizeV12, decodedV12));
    CHECK(!decodedV12.menu.blockGameInput);
    CHECK(!decodedV12.sanityDisplay.enabled);
    Settings decodedV11{};
    CHECK(settings_wire::DecodeV11(encoded.data(), settings_wire::kPayloadSizeV11, decodedV11));
    CHECK(decodedV11.menu.blockGameInput);
    CHECK(decodedV11.ghostSpeedDisplay.enabled && decodedV11.ghostSpeedDisplay.fixed);
    auto invalidBool = encoded;
    invalidBool[0] = 2;
    CHECK(!settings_wire::Decode(invalidBool.data(), invalidBool.size(), decoded));
    auto invalidShape = encoded;
    invalidShape[249] = 3;
    CHECK(!settings_wire::Decode(invalidShape.data(), invalidShape.size(), decoded));
    auto invalidLayout = encoded;
    invalidLayout[330] = 2;
    CHECK(!settings_wire::Decode(invalidLayout.data(), invalidLayout.size(), decoded));
    auto invalidTheme = encoded;
    invalidTheme[341] = 3;
    CHECK(!settings_wire::Decode(invalidTheme.data(), invalidTheme.size(), decoded));
    auto invalidBlockInput = encoded;
    invalidBlockInput[408] = 2;
    CHECK(!settings_wire::Decode(invalidBlockInput.data(), invalidBlockInput.size(), decoded));
    auto invalidSanityEnabled = encoded;
    invalidSanityEnabled[409] = 2;
    CHECK(!settings_wire::Decode(invalidSanityEnabled.data(), invalidSanityEnabled.size(), decoded));
    auto invalidSanityPosition = encoded;
    invalidSanityPosition[419] = 2;
    CHECK(!settings_wire::Decode(invalidSanityPosition.data(), invalidSanityPosition.size(), decoded));
    auto invalidPlacementInitialized = encoded;
    invalidPlacementInitialized[560] = 2;
    CHECK(!settings_wire::Decode(
        invalidPlacementInitialized.data(), invalidPlacementInitialized.size(), decoded));
    auto invalidFrameRate = encoded;
    invalidFrameRate[561] = 4;
    CHECK(!settings_wire::Decode(invalidFrameRate.data(), invalidFrameRate.size(), decoded));
    auto invalidHuntLayout = encoded;
    invalidHuntLayout[settings_wire::kPayloadSizeV18 - 1] = 2;
    CHECK(!settings_wire::Decode(invalidHuntLayout.data(), invalidHuntLayout.size(), decoded));
    auto invalidDistanceEnabled = encoded;
    invalidDistanceEnabled[settings_wire::kPayloadSizeV20] = 2;
    CHECK(!settings_wire::Decode(
        invalidDistanceEnabled.data(), invalidDistanceEnabled.size(), decoded));
    auto invalidDistancePositionInitialized = encoded;
    invalidDistancePositionInitialized[settings_wire::kPayloadSizeV20 + 30] = 2;
    CHECK(!settings_wire::Decode(
        invalidDistancePositionInitialized.data(), invalidDistancePositionInitialized.size(), decoded));
    Settings decodedV16{};
    ApplyDefaultHotkeys(decodedV16);
    CHECK(settings_wire::DecodeV16(encoded.data(), settings_wire::kPayloadSizeV16, decodedV16));
    CHECK(decodedV16.menu.frameRate == OverlayFrameRate::Ultra);
    CHECK(decodedV16.featureVisuals[static_cast<std::size_t>(FeatureId::Inventory)].opacity == 1.0f);
    CHECK(decodedV16.hotkeys[static_cast<std::size_t>(HotkeyAction::MenuToggle)].key == 'U');
    auto invalidGhostEnabled = encoded;
    invalidGhostEnabled[377] = 2;
    CHECK(!settings_wire::Decode(invalidGhostEnabled.data(), invalidGhostEnabled.size(), decoded));
    auto invalidGhostFixed = encoded;
    invalidGhostFixed[378] = 2;
    CHECK(!settings_wire::Decode(invalidGhostFixed.data(), invalidGhostFixed.size(), decoded));
    auto invalidGhostPosition = encoded;
    invalidGhostPosition[407] = 2;
    CHECK(!settings_wire::Decode(invalidGhostPosition.data(), invalidGhostPosition.size(), decoded));
    auto invalidOuijaEnabled = encoded;
    invalidOuijaEnabled[440] = 2;
    CHECK(!settings_wire::Decode(invalidOuijaEnabled.data(), invalidOuijaEnabled.size(), decoded));
    auto invalidOuijaPositionInitialized = encoded;
    invalidOuijaPositionInitialized[450] = 2;
    CHECK(!settings_wire::Decode(
        invalidOuijaPositionInitialized.data(), invalidOuijaPositionInitialized.size(), decoded));
    SettingsV9 decodedV9{};
    CHECK(settings_wire::DecodeV9(encoded.data(), settings_wire::kPayloadSizeV9, decodedV9));
    CHECK(!decodedV9.smudge.enabled && decodedV9.screenCrosshair.shape == CrosshairShape::Cross);
    CHECK(decodedV9.staminaDisplay.enabled && decodedV9.staminaDisplay.width == 260.0f);
    CHECK(!settings_wire::DecodeV9(encoded.data(), settings_wire::kPayloadSizeV9 - 1, decodedV9));
    CHECK(!settings_wire::Decode(encoded.data(), encoded.size() - 1, decoded));

    ClearOverlayPlacement(OverlayPlacementId::GameTime);
    RememberOverlayPosition(OverlayPlacementId::GameTime, {860.0f, 20.0f},
                            {200.0f, 26.0f}, {1920.0f, 1080.0f});
    CHECK((RestoreOverlayPosition(OverlayPlacementId::GameTime, {},
                                  {300.0f, 26.0f}, {1920.0f, 1080.0f}) ==
           timer_logic::Position{810.0f, 20.0f}));
    CHECK((RestoreOverlayPosition(OverlayPlacementId::GameTime, {},
                                  {300.0f, 26.0f}, {2560.0f, 1440.0f}) ==
           timer_logic::Position{1130.0f, 31.0f}));
    // Fractional centers / changing text widths must settle to actual pixels.
    RememberOverlayPosition(OverlayPlacementId::GameTime, {137.0f, 29.0f},
                            {211.0f, 27.0f}, {1920.0f, 1080.0f});
    const auto restored = RestoreOverlayPosition(OverlayPlacementId::GameTime, {},
                                                 {214.0f, 27.0f}, {1920.0f, 1080.0f});
    CHECK(restored.x == std::floor(restored.x) && restored.y == 29.0f);
    RememberOverlayPosition(OverlayPlacementId::GameTime, restored,
                            {214.0f, 27.0f}, {1920.0f, 1080.0f});
    CHECK((RestoreOverlayPosition(OverlayPlacementId::GameTime, {},
                                  {214.0f, 27.0f}, {1920.0f, 1080.0f}) == restored));

    // Test the real disk path, never the user's configuration.
    const auto testDirectory = std::filesystem::temp_directory_path() /
        (L"toolsbox-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    CHECK(std::filesystem::create_directory(testDirectory));
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(directory, ec); }
    } cleanup{testDirectory};
    const auto config = testDirectory / L"settings.bin";
    testing::SetConfigPath(config);
    CHECK(!Load());
    Get().hotkeys[static_cast<std::size_t>(HotkeyAction::MenuToggle)] = {};
    Get().hotkeys[static_cast<std::size_t>(HotkeyAction::LayoutModifier)] = {};
    Get().hotkeys[static_cast<std::size_t>(HotkeyAction::SmudgeStart)] = {'K', true, false};
    Get().smudge.enabled = false;
    Get().menu.theme = MenuTheme::Light;
    RememberOverlayPosition(OverlayPlacementId::GameTime, {500, 300}, {200, 26}, {1920, 1080});
    const auto expectedDisk = settings_wire::Encode(NormalizeSettings(Get()));
    MarkDirty();
    CHECK(Flush() && !LastSaveFailed());
    CHECK(Load());
    CHECK(settings_wire::Encode(Get()) == expectedDisk);
    const auto readFile = [&config]() {
        std::ifstream input(config, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(input), {});
    };
    const auto originalFile = readFile();

    // A sharing violation must preserve the previous file and pending changes.
    HANDLE lockedFile = CreateFileW(config.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(lockedFile != INVALID_HANDLE_VALUE);
    Get().smudge.enabled = true;
    MarkDirty();
    const bool failedSave = !Flush();
    CloseHandle(lockedFile);
    CHECK(failedSave && LastSaveFailed());
    CHECK(readFile() == originalFile);
    CHECK(Flush() && !LastSaveFailed()); // Retry without another MarkDirty.
    CHECK(Load() && Get().smudge.enabled);
    CHECK(!std::filesystem::exists(testDirectory / L"settings.tmp"));

    // Legacy files lacking a hotkey section still receive usable defaults.
    const auto writeLegacy = [&config](std::uint32_t version, const void* data,
                                        std::size_t size, bool withChecksum) {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        const timer_logic::ConfigHeader header{0x54424F58, version, static_cast<std::uint32_t>(size)};
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (withChecksum) {
            const auto crc = settings_wire::Crc32(static_cast<const std::uint8_t*>(data), size);
            output.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
        }
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        output.close();
        return !output.fail();
    };
    const SettingsV6 legacySettings{};
    CHECK(writeLegacy(6, &legacySettings, sizeof(legacySettings), false));
    CHECK(Load());
    CHECK(Get().hotkeys[static_cast<std::size_t>(HotkeyAction::MenuToggle)].key == 'U');
    CHECK(writeLegacy(16, encoded.data(), settings_wire::kPayloadSizeV16, true));
    CHECK(Load());
    CHECK(Get().hotkeys[static_cast<std::size_t>(HotkeyAction::MenuToggle)].key == 'U');
    CHECK(writeLegacy(17, expectedDisk.data(), settings_wire::kPayloadSizeV17, true));
    CHECK(Load());
    CHECK(Get().hotkeys[static_cast<std::size_t>(HotkeyAction::MenuToggle)].key == 0);
    CHECK(Get().hotkeys[static_cast<std::size_t>(HotkeyAction::SmudgeStart)].key == 'K');
    CHECK(writeLegacy(18, expectedDisk.data(), settings_wire::kPayloadSizeV18, true));
    CHECK(Load());
    CHECK(Get().hotkeys[static_cast<std::size_t>(HotkeyAction::LayoutModifier)].key == 0);
    const auto diskV20 = std::vector<std::uint8_t>(
        expectedDisk.begin(), expectedDisk.begin() + settings_wire::kPayloadSizeV20);
    CHECK(writeLegacy(20, diskV20.data(), diskV20.size(), true));
    CHECK(Load());
    CHECK(settings_wire::Encode(Get()) == expectedDisk);
    auto diskV19 = diskV20;
    diskV19.resize(settings_wire::kPayloadSizeV19, 0);
    diskV19[settings_wire::kPayloadSizeV20 + 13] = 'K';
    diskV19[settings_wire::kPayloadSizeV20 + 19] = 1;
    CHECK(writeLegacy(19, diskV19.data(), diskV19.size(), true));
    CHECK(Load());
    CHECK(settings_wire::Encode(Get()) == expectedDisk);
    MarkDirty();
    CHECK(Flush());
    const auto migratedFile = readFile();
    CHECK(migratedFile.size() == sizeof(timer_logic::ConfigHeader) + sizeof(std::uint32_t) + expectedDisk.size());
    timer_logic::ConfigHeader migratedHeader{};
    std::memcpy(&migratedHeader, migratedFile.data(), sizeof(migratedHeader));
    CHECK(migratedHeader.version == 21 && migratedHeader.payloadLength == settings_wire::kPayloadSize);
    CHECK(Load() && settings_wire::Encode(Get()) == expectedDisk);

    // The legacy tail is discarded only after the complete file passes CRC.
    CHECK(writeLegacy(19, diskV19.data(), diskV19.size(), true));
    {
        std::fstream corrupted(config, std::ios::binary | std::ios::in | std::ios::out);
        corrupted.seekp(-1, std::ios::end);
        corrupted.put('\x01');
        CHECK(corrupted.good());
    }
    CHECK(!Load());
    return 0;
}
