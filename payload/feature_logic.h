#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace feature_logic {

struct NormalizedPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool valid = false;
};

struct StaminaValue {
    bool valid = false;
    float current = 0.0f;
    float percentage = 0.0f;
};

enum class InventoryLayout { Horizontal, Vertical };

enum class InventorySlotState { Empty, Equipment, NonEquipment, Unknown };

struct InventorySlotEvidence {
    bool slotPresent = false;
    bool propValid = false;
    bool equipmentFound = false;
    bool storeItemValid = false;
    int equipmentType = -1;
    int tier = -1;
};

struct InventorySlotText {
    const char* firstLine = nullptr;
    const char* secondLine = nullptr;
};

struct InventoryTextOffset {
    float x = 0.0f;
    float y = 0.0f;
};

InventorySlotState ClassifyInventorySlot(InventorySlotEvidence evidence);
InventorySlotText InventorySlotTextForState(InventorySlotState state);
bool operator==(const InventoryTextOffset& lhs, const InventoryTextOffset& rhs);
std::array<InventoryTextOffset, 4> InventoryTextOutlineOffsets();
bool ShouldHighlightInventorySlot(int activeIndex, int slotIndex);
int NormalizeInventoryActiveIndex(int rawIndex);

int MatchInventorySlot(std::uintptr_t heldObject, const std::array<std::uintptr_t, 4>& slots);
float FitInventoryFontSize(float configuredSize, float minimumSize,
                           float measuredWidth, float availableWidth);

bool operator==(const NormalizedPoint& lhs, const NormalizedPoint& rhs);
int ClampCrosshairRadius(int radius);
StaminaValue NormalizeStamina(float current, float maximum);
float SmoothObservedSpeed(float previous, float observed, float elapsedSeconds,
                          float timeConstantSeconds);
bool CanMoveOverlay(bool fixed, bool altDown);
bool HotkeyPressed(bool gameForeground, bool down, bool previousDown);
float NicknameFontSize(float distance);
float NicknameAnchorY(float headScreenY, float heightPixels);
bool IsSnapshotFresh(long long elapsedMilliseconds, long long maximumAgeMilliseconds);
std::string FormatGhostDistance(bool hunting, bool valid, float meters);
NormalizedPoint NormalizeScreenPoint(float x, float y, float width, float height);
std::string FormatEquipmentLabel(int equipmentType, int tier, bool showTier);
std::string TranslateOuijaAnswer(std::string_view answer);

} // namespace feature_logic
