#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace timer_logic {

struct Position {
    float x;
    float y;
};

struct Size {
    float width;
    float height;
};

struct AutoResizeDecision {
    bool initializeBaseline = false;
    bool recenter = false;
};

struct Bounds {
    Position position;
    Size size;
};

struct CompactSideWindowLayout {
    Size supportSize;
    float titleHeight;
    Size sponsorImageSize;
};

enum class SnapAnchor {
    None,
    NearEdge,
    Quarter,
    Center,
    ThreeQuarter,
    FarEdge,
};

struct AxisSnapState {
    SnapAnchor anchor = SnapAnchor::None;
};

struct SnapState {
    AxisSnapState x;
    AxisSnapState y;
};

struct SnapResult {
    Position position{};
    SnapState state{};
    bool xSnapped = false;
    bool ySnapped = false;
    bool pointSnapped = false;
    Position point{};
};

using PositionSubmitter = void (*)(Position);

struct MovableWindowResult {
    const char* id = nullptr;
    Bounds bounds{};
    bool movable = false;
    bool dragging = false;
    PositionSubmitter submitPosition = nullptr;
};

const MovableWindowResult* FindMovableWindowById(
    const MovableWindowResult* windows, std::size_t count, const char* id);
const MovableWindowResult* FindTopmostMovableWindowAtPoint(
    const MovableWindowResult* windows, std::size_t count, Position point);

struct Color {
    float r;
    float g;
    float b;
    float a;
};

struct ConfigHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t payloadLength;
};

bool operator==(const Position& lhs, const Position& rhs);
bool IsGameForeground(std::uintptr_t foreground, std::uintptr_t game);
std::uint64_t NormalizeSeconds(float seconds);
std::uint64_t ElapsedSeconds(std::uint64_t startedMilliseconds, std::uint64_t nowMilliseconds);
std::string FormatMmSs(std::uint64_t seconds);
Position ClampPosition(Position position, Size window, Size viewport);
CompactSideWindowLayout GetCompactSideWindowLayout(float menuHeight);
bool operator==(const AxisSnapState& lhs, const AxisSnapState& rhs);
bool operator==(const SnapState& lhs, const SnapState& rhs);
SnapResult ComputeSnap(Bounds window, Size viewport, SnapState previous,
                       bool enabled, float enterDistance = 10.0f,
                       float releaseDistance = 16.0f);
Size InventoryLayoutSize(float slotSize, bool vertical);
AutoResizeDecision DecideAutoResize(bool appearing, bool hasBaseline,
                                    Size previous, Size current);
Position CenterPositionForResize(Position position, Size previousWindow,
                                 Size nextWindow, Size viewport);
float InventoryKeyFontSize(float slotSize);
Position DefaultRightCenter(Size display, Size window, float margin = 20.0f);
Position DefaultUpperCenter(Size display, Size window);
enum class DefaultOverlaySlot {
    TopLeftPrimary,
    TopLeftSecondary,
    TopCenterLeft,
    TopCenter,
    TopCenterRight,
    TopRight,
    CenterContext,
    BottomLeft,
    BottomCenter,
};
Position DefaultOverlayPosition(DefaultOverlaySlot slot, Size viewport, Size window);
bool SessionTimeWentBack(bool previousValid, float previousSeconds, bool currentValid, float currentSeconds,
                         float thresholdSeconds);
bool BlinkPhase(std::uint64_t elapsedMilliseconds, std::uint64_t periodMilliseconds);
struct LifecycleResetState {
    bool hasSeenActive = false;
    bool active = false;
};
bool UpdateLifecycleResetState(LifecycleResetState& state, bool active);
struct HuntingEvidenceResult {
    bool hunting;
    std::uint32_t negativeSamples;
};
HuntingEvidenceResult ResolveHuntingEvidence(bool rpcKnown, bool rpcHunting, std::uint64_t rpcAgeMilliseconds,
                                             bool fieldsRead, bool huntingFlag, bool stateHunting,
                                             bool currentHunting, std::uint32_t negativeSamples);
float ClampFontSize(float fontSize, float defaultValue = 26.0f);
float SnapFontSize(float fontSize, float defaultValue = 26.0f);
Color ClampColor(Color color, Color defaultValue);
bool IsConfigHeaderValid(ConfigHeader header, std::uint32_t expectedMagic,
                         std::uint32_t expectedVersion, std::uint32_t expectedPayloadLength);
std::vector<std::uint16_t> ExtractSupportedGlyphs(std::string_view utf8Text);
const char* GetFixedUiText();

} // namespace timer_logic
