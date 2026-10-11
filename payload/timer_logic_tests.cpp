#include "timer_logic.h"
#include "feature_logic.h"
#include "inventory_resources.h"
#include "settings_store.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

int main() {
    using namespace timer_logic;
    using namespace feature_logic;

    assert(!IsGameForeground(0, 0));
    assert(!IsGameForeground(1, 0));
    assert(!IsGameForeground(0, 1));
    assert(!IsGameForeground(1, 2));
    assert(IsGameForeground(1, 1));

    bool previousDown = false;
    assert(!HotkeyPressed(false, false, previousDown));

    // 游戏在前台时，首次按下产生且只产生一个上升沿。
    assert(HotkeyPressed(true, true, previousDown));
    previousDown = true;
    assert(!HotkeyPressed(true, true, previousDown));

    // 松开不触发；松开后重新按下可再次触发。
    assert(!HotkeyPressed(true, false, previousDown));
    previousDown = false;
    assert(HotkeyPressed(true, true, previousDown));

    // 非前台按下不触发，但调用方仍记录物理按下状态；按住切回前台也不触发。
    previousDown = false;
    assert(!HotkeyPressed(false, true, previousDown));
    previousDown = true;
    assert(!HotkeyPressed(true, true, previousDown));

    // 必须先松开，再重新按下才触发。
    assert(!HotkeyPressed(true, false, previousDown));
    previousDown = false;
    assert(HotkeyPressed(true, true, previousDown));

    assert(ClampCrosshairRadius(-1) == 1);
    assert(ClampCrosshairRadius(2) == 2);
    assert(ClampCrosshairRadius(31) == 30);
    assert(!NormalizeStamina(std::numeric_limits<float>::quiet_NaN(), 3.0f).valid);
    assert(!NormalizeStamina(1.0f, 0.0f).valid);
    assert(!NormalizeStamina(-1.0f, 3.0f).valid);
    assert(!NormalizeStamina(4.0f, 3.0f).valid);
    assert(NormalizeStamina(0.0f, 3.0f).valid && NormalizeStamina(0.0f, 3.0f).percentage == 0.0f);
    assert(NormalizeStamina(1.5f, 3.0f).percentage == 0.5f);
    assert(NormalizeStamina(3.0f, 3.0f).percentage == 1.0f);
    assert(SmoothObservedSpeed(1.0f, 3.0f, 0.6f, 0.6f) > 2.2f);
    assert(SmoothObservedSpeed(1.0f, 3.0f, 0.6f, 0.6f) < 2.3f);
    assert(SmoothObservedSpeed(1.0f, std::numeric_limits<float>::quiet_NaN(), 0.1f, 0.6f) == 1.0f);
    assert(SmoothObservedSpeed(1.0f, 3.0f, 0.0f, 0.6f) == 1.0f);
    assert(!CanMoveOverlay(true, false));
    assert(!CanMoveOverlay(true, true));
    assert(!CanMoveOverlay(false, false));
    assert(CanMoveOverlay(false, true));
    assert(NicknameFontSize(0.0f) == 22.0f);
    assert(NicknameFontSize(1.8f) == 22.0f);
    assert(NicknameFontSize(std::numeric_limits<float>::quiet_NaN()) == 22.0f);
    assert(std::fabs(NicknameFontSize(5.0f) - 20.08f) < 0.001f);
    assert(std::fabs(NicknameFontSize(10.0f) - 17.08f) < 0.001f);
    assert(std::fabs(NicknameFontSize(20.0f) - 11.08f) < 0.001f);
    for (float distance = 2.0f; distance < 20.0f; distance += 1.0f)
        assert(std::fabs(NicknameFontSize(distance) - NicknameFontSize(distance + 1.0f) - 0.6f) < 0.001f);
    // The far-distance curve joins without a jump in size or shrink rate.
    const float leftSlope = (NicknameFontSize(20.0f) - NicknameFontSize(19.99f)) / 0.01f;
    const float rightSlope = (NicknameFontSize(20.01f) - NicknameFontSize(20.0f)) / 0.01f;
    assert(std::fabs(leftSlope + 0.6f) < 0.002f);
    assert(std::fabs(rightSlope - leftSlope) < 0.002f);
    for (float distance = 20.0f; distance < 200.0f; distance += 1.0f) {
        assert(NicknameFontSize(distance + 1.0f) > 0.0f);
        assert(NicknameFontSize(distance + 1.0f) < NicknameFontSize(distance));
    }
    assert(NicknameFontSize(std::numeric_limits<float>::max()) > 0.0f);
    assert(NicknameAnchorY(500.0f, 20.0f) == 480.0f);
    assert((NormalizeScreenPoint(960, 540, 1920, 1080) == NormalizedPoint{0.5f, 0.5f, true}));
    assert(!NormalizeScreenPoint(1, 1, 0, 1080).valid);
    assert(FormatEquipmentLabel(0, 0, true) == u8"十字架 · 一级");
    assert(FormatEquipmentLabel(0, 1, true) == u8"十字架 · 二级");
    assert(FormatEquipmentLabel(0, 2, true) == u8"十字架 · 三级");
    assert(FormatEquipmentLabel(2, 0, false) == u8"EMF读取仪");
    assert(FormatEquipmentLabel(2, 2, true) == u8"EMF读取仪 · 三级");
    assert(FormatEquipmentLabel(5, 1, true) == u8"鬼魂笔记 · 二级");
    assert(FormatEquipmentLabel(10, 0, true) == u8"收音器 · 一级");
    assert(FormatEquipmentLabel(20, 2, false) == u8"摄像机");
    assert(FormatEquipmentLabel(0, -1, true) == u8"十字架");
    assert(FormatEquipmentLabel(0, 3, true) == u8"十字架");
    assert(FormatEquipmentLabel(21, 1, true).empty());
    assert(TranslateOuijaAnswer(" yes ") == u8"是");
    assert(TranslateOuijaAnswer("DROWNED") == u8"溺亡");
    assert(TranslateOuijaAnswer("Master Bedroom") == u8"主卧");
    assert(TranslateOuijaAnswer("HEALTHY!") == u8"健康");
    assert(TranslateOuijaAnswer("Not_Very") == u8"不太疯狂");
    assert(TranslateOuijaAnswer("Excited") == u8"兴奋");
    assert(TranslateOuijaAnswer("Hatred") == u8"仇恨");
    assert(TranslateOuijaAnswer("Who's there?") == u8"谁在那里");
    assert(TranslateOuijaAnswer("Lantern-Room-Top") == u8"灯塔灯室上层");
    assert(TranslateOuijaAnswer("Science Classroom 4") == u8"科学教室4号");
    assert(TranslateOuijaAnswer("ManagerOffice") == u8"经理办公室");
    // Current and legacy house-room names.
    assert(TranslateOuijaAnswer("Nancy's Room") == u8"南希卧室");
    assert(TranslateOuijaAnswer("Purple Bedroom") == u8"紫色卧室");
    assert(TranslateOuijaAnswer("Teen Boys Bedroom") == u8"少年男孩卧室");
    assert(TranslateOuijaAnswer("Art Studio") == u8"画室");
    assert(TranslateOuijaAnswer("Bar_and_Game_Room") == u8"酒吧游戏室");
    // Farmhouses and campsites.
    assert(TranslateOuijaAnswer("Tea Room") == u8"茶室");
    assert(TranslateOuijaAnswer("Seamstress Room") == u8"裁缝室");
    assert(TranslateOuijaAnswer("Lover's Bench") == u8"情侣长椅区");
    assert(TranslateOuijaAnswer("White Tent 2") == u8"白色帐篷2号");
    assert(TranslateOuijaAnswer("Cabin Kitchen") == u8"木屋厨房");
    // Prison, school, and Sunny Meadows compound/numbered names.
    assert(TranslateOuijaAnswer("First Floor A Block Left") == u8"一楼A区左侧牢房");
    assert(TranslateOuijaAnswer("Cafeteria Security") == u8"食堂安保室");
    assert(TranslateOuijaAnswer("Second Floor Front Corridor") == u8"二楼前侧走廊");
    assert(TranslateOuijaAnswer("Male Room 5") == u8"男病区病房5号");
    assert(TranslateOuijaAnswer("Servants' Room 2") == u8"仆人房2号");
    assert(TranslateOuijaAnswer("Hospital Room 2") == u8"医院病房2号");
    assert(TranslateOuijaAnswer("East Visitors' Room") == u8"东侧访客室");
    assert(TranslateOuijaAnswer("42") == "42");
    assert(TranslateOuijaAnswer("Unknown Place") == "Unknown Place");
    assert(IsSnapshotFresh(250, 250));
    assert(!IsSnapshotFresh(251, 250));
    assert(FormatGhostDistance(false, true, 12.345f) == "-- m");
    assert(FormatGhostDistance(true, false, 12.345f) == "-- m");
    assert(FormatGhostDistance(true, true, 12.345f) == "12.35 m");
    assert(FormatGhostDistance(true, true, -1.0f) == "-- m");

    using feature_logic::InventorySlotEvidence;
    using feature_logic::InventorySlotState;
    assert(ClassifyInventorySlot({}) == InventorySlotState::Empty);
    assert(ClassifyInventorySlot({true, true, true, true, 0, 0}) == InventorySlotState::Equipment);
    assert(ClassifyInventorySlot({true, true, true, true, 20, 2}) == InventorySlotState::Equipment);
    assert(ClassifyInventorySlot({true, true, false, false, -1, -1}) == InventorySlotState::NonEquipment);
    assert(ClassifyInventorySlot({true, false, false, false, -1, -1}) == InventorySlotState::Unknown);
    assert(ClassifyInventorySlot({true, true, true, false, -1, -1}) == InventorySlotState::Unknown);
    assert(ClassifyInventorySlot({true, true, true, true, -1, 0}) == InventorySlotState::Unknown);
    assert(ClassifyInventorySlot({true, true, true, true, 21, 0}) == InventorySlotState::Unknown);
    assert(ClassifyInventorySlot({true, true, true, true, 0, -1}) == InventorySlotState::Unknown);
    assert(ClassifyInventorySlot({true, true, true, true, 0, 3}) == InventorySlotState::Unknown);

    const auto equipmentWithoutTextureEvidence = InventorySlotEvidence{true, true, true, true, 7, 1};
    assert(ClassifyInventorySlot(equipmentWithoutTextureEvidence) == InventorySlotState::Equipment);
    const auto emptyText = InventorySlotTextForState(InventorySlotState::Empty);
    const auto equipmentText = InventorySlotTextForState(InventorySlotState::Equipment);
    const auto nonEquipmentText = InventorySlotTextForState(InventorySlotState::NonEquipment);
    const auto unknownText = InventorySlotTextForState(InventorySlotState::Unknown);
    assert(emptyText.firstLine == nullptr && emptyText.secondLine == nullptr);
    assert(equipmentText.firstLine == nullptr && equipmentText.secondLine == nullptr);
    assert(std::string(nonEquipmentText.firstLine) == u8"可互");
    assert(std::string(nonEquipmentText.secondLine) == u8"动物");
    assert(unknownText.firstLine == nullptr && unknownText.secondLine == nullptr);

    const auto outlineOffsets = InventoryTextOutlineOffsets();
    assert(outlineOffsets.size() == 4);
    assert((outlineOffsets[0] == InventoryTextOffset{-1.0f, 0.0f}));
    assert((outlineOffsets[1] == InventoryTextOffset{1.0f, 0.0f}));
    assert((outlineOffsets[2] == InventoryTextOffset{0.0f, -1.0f}));
    assert((outlineOffsets[3] == InventoryTextOffset{0.0f, 1.0f}));

    assert(NormalizeInventoryActiveIndex(0) == 0);
    assert(NormalizeInventoryActiveIndex(3) == 3);
    assert(NormalizeInventoryActiveIndex(-1) == -1);
    assert(NormalizeInventoryActiveIndex(4) == -1);
    assert(ShouldHighlightInventorySlot(2, 2));
    assert(!ShouldHighlightInventorySlot(2, 1));
    assert(!ShouldHighlightInventorySlot(-1, 0));
    assert(!ShouldHighlightInventorySlot(4, 0));
    const auto emptySelectedState = ClassifyInventorySlot({});
    assert(emptySelectedState == InventorySlotState::Empty);
    assert(ShouldHighlightInventorySlot(2, 2));

    const std::array<std::uintptr_t, 4> inventorySlots{0x1000u, 0x2000u, 0u, 0x4000u};
    assert(MatchInventorySlot(0x1000u, inventorySlots) == 0);
    assert(MatchInventorySlot(0x4000u, inventorySlots) == 3);
    assert(MatchInventorySlot(0u, inventorySlots) == -1);
    assert(MatchInventorySlot(0x3000u, inventorySlots) == -1);
    assert(FitInventoryFontSize(26.0f, 12.0f, 20.0f, 24.0f) == 26.0f);
    assert(FitInventoryFontSize(26.0f, 12.0f, 52.0f, 24.0f) == 12.0f);
    assert(FitInventoryFontSize(26.0f, 12.0f, 39.0f, 24.0f) == 16.0f);

    using inventory_resources::InventoryItemVisual;
    assert(!inventory_resources::IsValidVisual({-1, 0}));
    assert(!inventory_resources::IsValidVisual({0, -1}));
    assert(!inventory_resources::IsValidVisual({21, 0}));
    assert(!inventory_resources::IsValidVisual({0, 3}));
    assert(inventory_resources::IsValidVisual({0, 0}));
    assert(inventory_resources::IsValidVisual({20, 2}));
    assert(!inventory_resources::IsValidVisual({-1, -1}));
    assert(!inventory_resources::IsValidVisual({7, 3}));
    assert(inventory_resources::ResourceIndex({0, 0}) == 0);
    assert(inventory_resources::ResourceIndex({20, 2}) == 62);
    assert(inventory_resources::ResourceIndex({21, 2}) == inventory_resources::kInvalidResourceIndex);

    constexpr std::array<const char*, 21> stems{
        "Crucifix", "DOTS", "EMF", "Firelight", "Flashlight", "Ghost w", "Head",
        "Igniter", "Repellent", "Motion", "Para", "Photo", "Salt", "Sanity",
        "Sound Recorder", "Sound", "Spirit box", "Thermo", "Tripod", "UV", "Video"
    };
    constexpr std::array<std::uint8_t, 8> pngSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (int equipmentType = 0; equipmentType < 21; ++equipmentType) {
        for (int tier = 0; tier < 3; ++tier) {
            const InventoryItemVisual visual{equipmentType, tier};
            const auto* png = inventory_resources::GetEmbeddedPng(visual);
            assert(png != nullptr && png->data != nullptr && png->size > pngSignature.size());
            assert(std::equal(pngSignature.begin(), pngSignature.end(), png->data));
            const std::string sourceName = png->sourceName;
            assert(sourceName.rfind(stems[static_cast<std::size_t>(equipmentType)], 0) == 0);
            assert(sourceName.find(std::to_string(tier + 1)) != std::string::npos);
            assert(inventory_resources::ResourceIndex(visual) == equipmentType * 3 + tier);
        }
    }
    assert(inventory_resources::GetEmbeddedPng({-1, 0}) == nullptr);
    assert(inventory_resources::GetEmbeddedPng({0, 3}) == nullptr);
    const auto* sponsor = inventory_resources::GetSponsorPng();
    assert(sponsor != nullptr && sponsor->data != nullptr && sponsor->size >= 24);
    assert(std::equal(pngSignature.begin(), pngSignature.end(), sponsor->data));
    assert(std::string(sponsor->sourceName) == "wx_zfb.png");
    const auto readBigEndian32 = [](const std::uint8_t* bytes) {
        return (static_cast<std::uint32_t>(bytes[0]) << 24) |
               (static_cast<std::uint32_t>(bytes[1]) << 16) |
               (static_cast<std::uint32_t>(bytes[2]) << 8) |
               static_cast<std::uint32_t>(bytes[3]);
    };
    assert(readBigEndian32(sponsor->data + 16) == 315);
    assert(readBigEndian32(sponsor->data + 20) == 150);
    assert(inventory_resources::kInventoryPngCount == 63);
    assert(inventory_resources::ResourceIndex({0, 0}) == 0);
    assert(inventory_resources::ResourceIndex({20, 2}) == 62);

    assert(FormatMmSs(0) == "00:00");
    assert(FormatMmSs(59) == "00:59");
    assert(FormatMmSs(60) == "01:00");
    assert(FormatMmSs(7507) == "125:07");
    assert(NormalizeSeconds(-1.0f) == 0);
    assert(NormalizeSeconds(std::numeric_limits<float>::quiet_NaN()) == 0);
    assert(NormalizeSeconds(59.9f) == 59);
    assert(ElapsedSeconds(1000, 999) == 0);
    assert(ElapsedSeconds(1000, 2501) == 1);
    assert((ClampPosition({-20.0f, 900.0f}, {200.0f, 80.0f}, {800.0f, 600.0f}) == Position{0.0f, 520.0f}));
    assert((ClampPosition({50.0f, 50.0f}, {900.0f, 700.0f}, {800.0f, 600.0f}) == Position{0.0f, 0.0f}));
    assert((ClampPosition({std::numeric_limits<float>::quiet_NaN(), 10.0f}, {100.0f, 100.0f}, {800.0f, 600.0f}) == Position{0.0f, 10.0f}));

    constexpr Size menuSize{685.0f, 560.0f};
    const Bounds menuBounds{{400.0f, 100.0f}, menuSize};
    const auto compact = GetCompactSideWindowLayout(menuBounds.size.height);
    assert(compact.supportSize.width == 344.0f && compact.supportSize.height == 560.0f);
    assert(compact.titleHeight == 58.0f);
    assert(compact.sponsorImageSize.width == 252.0f && compact.sponsorImageSize.height == 120.0f);

    const std::array<MovableWindowResult, 3> dragTargets{{
        {"##first", {}, true, false, +[](Position) {}},
        {"##disabled", {}, false, false, +[](Position) {}},
        {"##missing_submitter", {}, true, false, nullptr},
    }};
    assert(FindMovableWindowById(dragTargets.data(), dragTargets.size(), "##first") == &dragTargets[0]);
    assert(FindMovableWindowById(dragTargets.data(), dragTargets.size(), "##disabled") == &dragTargets[1]);
    assert(FindMovableWindowById(dragTargets.data(), dragTargets.size(), "##missing_submitter") == nullptr);
    assert(FindMovableWindowById(dragTargets.data(), dragTargets.size(), "##unknown") == nullptr);
    assert(FindMovableWindowById(dragTargets.data(), dragTargets.size(), nullptr) == nullptr);
    const std::array<MovableWindowResult, 3> resizeTargets{{
        {"##bottom", {{10.0f, 10.0f}, {100.0f, 100.0f}}, true, false, nullptr},
        {"##fixed", {{20.0f, 20.0f}, {100.0f, 100.0f}}, false, false, nullptr},
        {"##top", {{30.0f, 30.0f}, {100.0f, 100.0f}}, true, false, nullptr},
    }};
    assert(FindTopmostMovableWindowAtPoint(
        resizeTargets.data(), resizeTargets.size(), {40.0f, 40.0f}) == &resizeTargets[2]);
    assert(FindTopmostMovableWindowAtPoint(
        resizeTargets.data(), resizeTargets.size(), {15.0f, 15.0f}) == &resizeTargets[0]);
    assert(FindTopmostMovableWindowAtPoint(
        resizeTargets.data(), resizeTargets.size(), {200.0f, 200.0f}) == nullptr);

    const Size snapViewport{800.0f, 600.0f};
    const Size snapWindow{100.0f, 40.0f};
    const SnapState noSnap{};
    for (const Position expected : {
             Position{150.0f, 130.0f}, Position{550.0f, 130.0f},
             Position{150.0f, 430.0f}, Position{550.0f, 430.0f},
             Position{350.0f, 280.0f}}) {
        const auto result = ComputeSnap({{expected.x + 6.0f, expected.y - 8.0f}, snapWindow},
                                        snapViewport, noSnap, true);
        assert(result.position == expected);
        assert(result.xSnapped && result.ySnapped && result.pointSnapped);
    }
    const auto pointPriority = ComputeSnap({{344.0f, 274.0f}, snapWindow}, snapViewport, noSnap, true);
    assert((pointPriority.position == Position{350.0f, 280.0f}));
    assert(pointPriority.pointSnapped);
    const auto nearestPoint = ComputeSnap({{347.0f, 278.0f}, snapWindow}, snapViewport, noSnap, true);
    assert((nearestPoint.point == Position{400.0f, 300.0f}));
    assert((nearestPoint.position == Position{350.0f, 280.0f}));
    assert((ComputeSnap({{7.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true).position == Position{0.0f, 211.0f}));
    assert((ComputeSnap({{694.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true).position == Position{700.0f, 211.0f}));
    assert((ComputeSnap({{311.0f, 9.0f}, snapWindow}, snapViewport, noSnap, true).position == Position{311.0f, 0.0f}));
    assert((ComputeSnap({{311.0f, 557.0f}, snapWindow}, snapViewport, noSnap, true).position == Position{311.0f, 560.0f}));
    const auto xOnly = ComputeSnap({{145.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true);
    assert((xOnly.position == Position{150.0f, 211.0f}));
    assert(xOnly.xSnapped && !xOnly.ySnapped && !xOnly.pointSnapped);
    const auto yOnly = ComputeSnap({{311.0f, 125.0f}, snapWindow}, snapViewport, noSnap, true);
    assert((yOnly.position == Position{311.0f, 130.0f}));
    assert(!yOnly.xSnapped && yOnly.ySnapped && !yOnly.pointSnapped);
    const auto minimumAxis = ComputeSnap({{341.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true);
    assert((minimumAxis.position == Position{350.0f, 211.0f}));
    assert(minimumAxis.xSnapped && minimumAxis.state.x.anchor == SnapAnchor::Center);
    assert((ComputeSnap({{140.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true).position == Position{150.0f, 211.0f}));
    const auto outsideEnter = ComputeSnap({{139.0f, 211.0f}, snapWindow}, snapViewport, noSnap, true);
    assert((outsideEnter.position == Position{139.0f, 211.0f}));
    assert(!outsideEnter.xSnapped);
    SnapState latched{};
    latched.x.anchor = SnapAnchor::Quarter;
    latched.y.anchor = SnapAnchor::Center;
    const auto retained = ComputeSnap({{165.0f, 295.0f}, snapWindow}, snapViewport, latched, true);
    assert((retained.position == Position{150.0f, 280.0f}));
    assert(retained.xSnapped && retained.ySnapped);
    const auto releasedX = ComputeSnap({{167.0f, 295.0f}, snapWindow}, snapViewport, latched, true);
    assert((releasedX.position == Position{167.0f, 280.0f}));
    assert(!releasedX.xSnapped && releasedX.ySnapped);
    const auto releaseBoundary = ComputeSnap({{166.0f, 280.0f}, snapWindow}, snapViewport, latched, true);
    assert((releaseBoundary.position == Position{150.0f, 280.0f}));
    assert(releaseBoundary.xSnapped);
    const auto disabled = ComputeSnap({{-5.0f, 570.0f}, snapWindow}, snapViewport, latched, false);
    assert((disabled.position == Position{0.0f, 560.0f}));
    assert((disabled.state == SnapState{}));
    assert(!disabled.xSnapped && !disabled.ySnapped && !disabled.pointSnapped);
    const auto clampedSnap = ComputeSnap({{706.0f, 566.0f}, snapWindow}, snapViewport, noSnap, true);
    assert((clampedSnap.position == Position{700.0f, 560.0f}));

    const Size horizontal50 = InventoryLayoutSize(50.0f, false);
    const Size vertical50 = InventoryLayoutSize(50.0f, true);
    const Size horizontal96 = InventoryLayoutSize(96.0f, false);
    const Size vertical96 = InventoryLayoutSize(96.0f, true);
    assert(horizontal50.width == 212.0f && horizontal50.height == 50.0f);
    assert(vertical50.width == 50.0f && vertical50.height == 212.0f);
    assert(horizontal96.width == 396.0f && horizontal96.height == 96.0f);
    assert(vertical96.width == 96.0f && vertical96.height == 396.0f);
    assert(InventoryKeyFontSize(50.0f) == 16.0f);
    assert(InventoryKeyFontSize(73.0f) == 25.0f);
    assert(InventoryKeyFontSize(96.0f) == 34.0f);
    assert(InventoryKeyFontSize(20.0f) == 16.0f);
    assert(InventoryKeyFontSize(120.0f) == 34.0f);

    const Size measuringSize{32.0f, 16.0f};
    const Size stableSize{180.0f, 32.0f};
    const auto appearingWithoutBaseline = DecideAutoResize(
        true, false, {}, measuringSize);
    assert(!appearingWithoutBaseline.initializeBaseline);
    assert(!appearingWithoutBaseline.recenter);

    const auto firstStable = DecideAutoResize(
        false, false, {}, stableSize);
    assert(firstStable.initializeBaseline);
    assert(!firstStable.recenter);

    const auto unchangedStable = DecideAutoResize(
        false, true, stableSize, stableSize);
    assert(!unchangedStable.initializeBaseline);
    assert(!unchangedStable.recenter);

    const Size widerSize{260.0f, 32.0f};
    const auto changedStable = DecideAutoResize(
        false, true, stableSize, widerSize);
    assert(!changedStable.initializeBaseline);
    assert(changedStable.recenter);

    const auto appearingWithBaseline = DecideAutoResize(
        true, true, stableSize, measuringSize);
    assert(!appearingWithBaseline.initializeBaseline);
    assert(!appearingWithBaseline.recenter);

    Position persistedPosition{700.0f, 500.0f};
    bool hasBaseline = false;
    Size baseline{};
    const auto newLifecycleAppearing = DecideAutoResize(
        true, hasBaseline, baseline, measuringSize);
    if (newLifecycleAppearing.initializeBaseline) {
        baseline = measuringSize;
        hasBaseline = true;
    }
    if (newLifecycleAppearing.recenter) {
        persistedPosition = CenterPositionForResize(
            persistedPosition, baseline, measuringSize, {1920.0f, 1080.0f});
    }
    assert((persistedPosition == Position{700.0f, 500.0f}));
    assert(!hasBaseline);

    const auto newLifecycleStable = DecideAutoResize(
        false, hasBaseline, baseline, stableSize);
    if (newLifecycleStable.initializeBaseline) {
        baseline = stableSize;
        hasBaseline = true;
    }
    if (newLifecycleStable.recenter) {
        persistedPosition = CenterPositionForResize(
            persistedPosition, baseline, stableSize, {1920.0f, 1080.0f});
    }
    assert((persistedPosition == Position{700.0f, 500.0f}));
    assert(hasBaseline);
    assert(baseline.width == stableSize.width && baseline.height == stableSize.height);

    assert((CenterPositionForResize(
        {100.0f, 200.0f}, {212.0f, 50.0f}, {396.0f, 96.0f}, {1920.0f, 1080.0f}) ==
        Position{8.0f, 177.0f}));
    assert((CenterPositionForResize(
        {100.0f, 200.0f}, {212.0f, 50.0f}, {50.0f, 212.0f}, {1920.0f, 1080.0f}) ==
        Position{181.0f, 119.0f}));
    assert((CenterPositionForResize(
        {0.0f, 0.0f}, {212.0f, 50.0f}, {396.0f, 96.0f}, {800.0f, 600.0f}) ==
        Position{0.0f, 0.0f}));
    assert((CenterPositionForResize(
        {588.0f, 550.0f}, {212.0f, 50.0f}, {396.0f, 96.0f}, {800.0f, 600.0f}) ==
        Position{404.0f, 504.0f}));
    const Size edgeViewport{800.0f, 600.0f};
    const Size edgeWindow{200.0f, 80.0f};
    assert((ClampPosition({0.0f, 123.0f}, edgeWindow, edgeViewport) == Position{0.0f, 123.0f}));
    assert((ClampPosition({321.0f, 0.0f}, edgeWindow, edgeViewport) == Position{321.0f, 0.0f}));
    assert((ClampPosition({600.0f, 123.0f}, edgeWindow, edgeViewport) == Position{600.0f, 123.0f}));
    assert((ClampPosition({321.0f, 520.0f}, edgeWindow, edgeViewport) == Position{321.0f, 520.0f}));
    bool lastSessionTimeValid = true;
    float lastSessionSeconds = 120.0f;
    assert(!SessionTimeWentBack(lastSessionTimeValid, lastSessionSeconds, false, 0.0f, 5.0f));
    assert(SessionTimeWentBack(lastSessionTimeValid, lastSessionSeconds, true, 2.0f, 5.0f));
    assert(!SessionTimeWentBack(false, 0.0f, true, 2.0f, 5.0f));
    assert(!SessionTimeWentBack(true, 120.0f, true, 116.0f, 5.0f));
    assert(!SessionTimeWentBack(true, std::numeric_limits<float>::quiet_NaN(), true, 2.0f, 5.0f));
    assert(!BlinkPhase(0, 500));
    assert(!BlinkPhase(499, 500));
    assert(BlinkPhase(500, 500));
    assert(BlinkPhase(999, 500));
    assert(!BlinkPhase(1000, 500));

    LifecycleResetState lifecycle{};
    assert(!UpdateLifecycleResetState(lifecycle, false));
    assert(UpdateLifecycleResetState(lifecycle, true));
    assert(!UpdateLifecycleResetState(lifecycle, true));
    assert(UpdateLifecycleResetState(lifecycle, false));
    assert(!UpdateLifecycleResetState(lifecycle, false));
    assert(UpdateLifecycleResetState(lifecycle, true));
    assert(!UpdateLifecycleResetState(lifecycle, true));

    assert(ResolveHuntingEvidence(false, false, 0, true, true, false, false, 0).hunting);
    assert(ResolveHuntingEvidence(false, false, 0, true, false, true, false, 0).hunting);
    assert(ResolveHuntingEvidence(true, true, 0, true, false, false, false, 0).hunting);
    assert(!ResolveHuntingEvidence(true, false, 0, true, true, true, true, 0).hunting);
    assert(ResolveHuntingEvidence(false, false, 0, false, false, false, true, 2).hunting);
    assert(ResolveHuntingEvidence(false, false, 0, true, false, false, true, 1).hunting);
    assert(!ResolveHuntingEvidence(false, false, 0, true, false, false, true, 2).hunting);
    assert(ResolveHuntingEvidence(false, false, 0, true, false, false, false, 0).negativeSamples == 1);
    assert(ResolveHuntingEvidence(true, true, 749, true, false, false, true, 3).hunting);
    assert(!ResolveHuntingEvidence(true, true, 750, true, false, false, true, 3).hunting);

    assert((DefaultOverlayPosition(DefaultOverlaySlot::TopLeftPrimary,
                                   {1920.0f, 1080.0f}, {400.0f, 56.0f}) == Position{20.0f, 20.0f}));
    assert((DefaultUpperCenter({1920.0f, 1080.0f}, {300.0f, 26.0f}) ==
            Position{810.0f, 257.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::TopLeftSecondary,
                                   {1920.0f, 1080.0f}, {280.0f, 56.0f}) == Position{20.0f, 100.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::TopCenterLeft,
                                   {1920.0f, 1080.0f}, {180.0f, 26.0f}) == Position{660.0f, 20.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::TopCenter,
                                   {1920.0f, 1080.0f}, {200.0f, 26.0f}) == Position{860.0f, 20.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::TopCenterRight,
                                   {1920.0f, 1080.0f}, {360.0f, 26.0f}) == Position{1080.0f, 20.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::TopRight,
                                   {1920.0f, 1080.0f}, {260.0f, 146.0f}) == Position{1640.0f, 20.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::CenterContext,
                                   {1920.0f, 1080.0f}, {200.0f, 26.0f}) == Position{860.0f, 588.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::BottomLeft,
                                   {1920.0f, 1080.0f}, {200.0f, 26.0f}) == Position{20.0f, 1034.0f}));
    assert((DefaultOverlayPosition(DefaultOverlaySlot::BottomCenter,
                                   {1920.0f, 1080.0f}, {212.0f, 50.0f}) == Position{854.0f, 1010.0f}));
    assert((CenterPositionForResize(
        {800.0f, 510.0f}, {320.0f, 40.0f}, {480.0f, 40.0f}, {1920.0f, 1080.0f}) ==
        Position{720.0f, 510.0f}));
    assert((CenterPositionForResize(
        {720.0f, 510.0f}, {480.0f, 40.0f}, {200.0f, 40.0f}, {1920.0f, 1080.0f}) ==
        Position{860.0f, 510.0f}));
    assert((CenterPositionForResize(
        {860.0f, 510.0f}, {200.0f, 40.0f}, {200.0f, 60.0f}, {1920.0f, 1080.0f}) ==
        Position{860.0f, 500.0f}));
    assert((CenterPositionForResize(
        {300.0f, 200.0f}, {200.0f, 40.0f}, {400.0f, 60.0f}, {1920.0f, 1080.0f}) ==
        Position{200.0f, 190.0f}));
    assert((CenterPositionForResize(
        {10.0f, 100.0f}, {200.0f, 40.0f}, {300.0f, 40.0f}, {800.0f, 600.0f}) ==
        Position{0.0f, 100.0f}));
    assert((CenterPositionForResize(
        {590.0f, 520.0f}, {200.0f, 80.0f}, {300.0f, 120.0f}, {800.0f, 600.0f}) ==
        Position{500.0f, 480.0f}));

    assert(ClampFontSize(10.0f) == 16.0f);
    assert(ClampFontSize(80.0f) == 56.0f);
    assert(ClampFontSize(std::numeric_limits<float>::quiet_NaN()) == 26.0f);
    assert(SnapFontSize(16.0f) == 16.0f);
    assert(SnapFontSize(25.0f) == 24.0f);
    assert(SnapFontSize(27.0f) == 26.0f);
    assert(SnapFontSize(55.0f) == 56.0f);
    assert(SnapFontSize(std::numeric_limits<float>::quiet_NaN()) == 26.0f);
    assert(SnapFontSize(std::numeric_limits<float>::infinity()) == 26.0f);
    const Color defaults{0.2f, 0.3f, 0.4f, 0.5f};
    const Color color = ClampColor({-1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), 0.75f}, defaults);
    assert(color.r == 0.0f && color.g == 1.0f && color.b == defaults.b && color.a == 0.75f);
    const Color smudgePlayerDefault = settings_store::SmudgeSettings{}.playerColor;
    assert(smudgePlayerDefault.r == 1.0f && smudgePlayerDefault.g == 1.0f &&
           smudgePlayerDefault.b == 1.0f && smudgePlayerDefault.a == 1.0f);
    const settings_store::InventoryBarSettings inventoryDefaults{};
    assert(!inventoryDefaults.enabled && !inventoryDefaults.fixed);
    assert(inventoryDefaults.slotSize == 50.0f && inventoryDefaults.fontSize == 26.0f);
    assert(inventoryDefaults.highlightColor.r == 1.0f && inventoryDefaults.highlightColor.g == 1.0f &&
           inventoryDefaults.highlightColor.b == 1.0f && inventoryDefaults.highlightColor.a == 1.0f);
    assert(!inventoryDefaults.positionInitialized);
    assert(inventoryDefaults.layout == 0);
    const settings_store::StaminaDisplaySettings staminaDefaults{};
    assert(!staminaDefaults.enabled && !staminaDefaults.fixed && !staminaDefaults.positionInitialized);
    assert(staminaDefaults.removedDisplayTemplate == 0);
    assert(staminaDefaults.height == 26.0f && staminaDefaults.width == 200.0f);

    constexpr std::uint32_t magic = 0x54424F58;
    constexpr std::uint32_t version = 1;
    constexpr std::uint32_t length = 128;
    assert(!IsConfigHeaderValid({0, version, length}, magic, version, length));
    assert(!IsConfigHeaderValid({magic, 2, length}, magic, version, length));
    assert(!IsConfigHeaderValid({magic, version, 64}, magic, version, length));
    assert(IsConfigHeaderValid({magic, version, length}, magic, version, length));

    assert((ExtractSupportedGlyphs(u8"中日한Жé") == std::vector<std::uint16_t>{0x4E2D, 0x65E5, 0xD55C, 0x0416, 0x00E9}));
    assert(ExtractSupportedGlyphs(u8"😀❤\uFE0F👩‍💻🇨🇳1️⃣").empty());
    assert((ExtractSupportedGlyphs(std::string("A\xF0\x28\x8C\x28")) == std::vector<std::uint16_t>{0x0041, 0x0028}));
    assert((ExtractSupportedGlyphs(u8"中中A中A") == std::vector<std::uint16_t>{0x4E2D, 0x0041}));
    const auto fixedGlyphs = ExtractSupportedGlyphs(GetFixedUiText());
    const auto containsGlyph = [&fixedGlyphs](std::uint16_t glyph) {
        return std::find(fixedGlyphs.begin(), fixedGlyphs.end(), glyph) != fixedGlyphs.end();
    };
    for (std::uint16_t glyph : ExtractSupportedGlyphs(
             u8"抓鬼工具箱(U键 显示/隐藏 | 按住Alt拖动功能位置)菜单设置参考线主题颜色暗色亮色深蓝重置默认设置圣木计时器游戏时间"
             u8"启用:固定:字体大小:无记录颜色:有记录颜色:玩家颜色:记录时间颜色:字体颜色:："
             u8"鬼魂状态猎杀计时器猎杀时长按住移动布局键，在组件上滚轮调整大小"
             u8"设置未能保存，正在自动重试请检查磁盘空间和文件夹写入权限"
             u8"十字架计时器猎杀等待状态变化闪烁等待颜色猎杀颜色"
             u8"十字架被燃烧准星信息屏幕准星玩家昵称物品栏布局横版竖版槽位大小高光颜色显示等级形状半径高度颜色"
             u8"实心圆空心圆十字一级二级三级门抽屉灯光开关可交互物"
             u8"激光投影仪EMF读取仪火光手电筒鬼魂笔记头戴设备点火器熏香运动传感器"
             u8"收音器照相机盐理智药物录音机声音传感器通灵盒温度计三脚架紫外线灯摄像机"
              u8"显示隐藏工具箱菜单Alt鼠标左键移动未固定的功能浮窗"
             u8"手动开始圣木计时清除圣木计时记录清除十字架燃烧记录支持作者感谢支持你的支持就是我最大的动力"
             u8"可互动物自愿赞助辅助永久免费如果无法赞助也可以去抖音bilibili给作者支持点赞关注QQ群不定时更新作者霖木"
              u8"B站摆烂的霖木抖音号体力显示高度宽度鬼魂移速鬼魂距离非单人\\房主时因网络波动可能会导致速度不稳定输入拦截"
              u8"一键启动一键关闭一键重置理智显示团队平均值游戏设置不支持显示理智数据暂不可用本再")) {
        assert(containsGlyph(glyph));
    }
    return 0;
}
