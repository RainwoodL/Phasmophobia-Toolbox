#include "timer_logic.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace timer_logic {
namespace {

float AnchorCoordinate(SnapAnchor anchor, float viewportExtent, float windowExtent) {
    switch (anchor) {
    case SnapAnchor::NearEdge: return 0.0f;
    case SnapAnchor::Quarter: return viewportExtent * 0.25f - windowExtent * 0.5f;
    case SnapAnchor::Center: return viewportExtent * 0.5f - windowExtent * 0.5f;
    case SnapAnchor::ThreeQuarter: return viewportExtent * 0.75f - windowExtent * 0.5f;
    case SnapAnchor::FarEdge: return viewportExtent - windowExtent;
    default: return 0.0f;
    }
}

struct AxisCandidate { SnapAnchor anchor; float position; float distance; };

AxisCandidate BestAxis(float position, float viewportExtent, float windowExtent, float enterDistance) {
    AxisCandidate best{SnapAnchor::None, position, enterDistance + 1.0f};
    for (const SnapAnchor anchor : {SnapAnchor::NearEdge, SnapAnchor::Quarter, SnapAnchor::Center,
                                    SnapAnchor::ThreeQuarter, SnapAnchor::FarEdge}) {
        const float target = AnchorCoordinate(anchor, viewportExtent, windowExtent);
        const float distance = std::fabs(target - position);
        if (distance <= enterDistance && distance < best.distance) best = {anchor, target, distance};
    }
    return best;
}

constexpr const char* kFixedUiText =
    u8"抓鬼工具箱(U键 显示/隐藏 | 按住Alt拖动功能位置)菜单设置参考线主题颜色暗色亮色深蓝调整帧率低中高超高"
    u8"透明度快捷键菜单快捷键移动布局未设置请按键冲突修改未保存手动计时清除记录"
    u8"键位重置快捷键冲突请重新设置相同按键和组合键不能分配给多个功能"
    u8"支持提醒你已经启动了次工具箱了可以给我赞助一下吗不强制赞助仅每启动50次时会弹出好算了自愿赞助已赞助感谢您的支持本再"
    u8"圣木计时器游戏时间物品栏布局横版竖版槽位大小高光颜色启用:固定:字体大小:字体颜色:无记录颜色:有记录颜色:"
    u8"玩家颜色:记录时间颜色:重置默认设置圣木被点燃玩家记录时间手动--："
    u8"鬼魂状态猎杀计时器猎杀时长按住移动布局键，在组件上滚轮调整大小"
    u8"十字架计时器猎杀等待状态变化闪烁等待颜色猎杀颜色"
    u8"十字架被燃烧淡橙准星信息屏幕准星玩家昵称显示等级形状半径高度颜色"
    u8"实心圆空心圆十字一级二级三级门抽屉灯光开关可交互物"
    u8"激光投影仪EMF读取仪火光手电筒鬼魂笔记头戴设备点火器熏香运动传感器"
    u8"收音器照相机盐理智药物录音机声音传感器通灵盒温度计三脚架紫外线灯摄像机"
    u8"空格，。！？；、（）【】《》“”‘’"
    u8"U显示隐藏工具箱菜单Alt鼠标左键移动未固定的功能浮窗"
    u8"I手动开始圣木计时O清除圣木计时记录P清除十字架燃烧记录支持作者感谢支持你的支持就是我最大的动力"
    u8"可互动物自愿赞助辅助永久免费如果无法赞助也可以去抖音bilibili给作者支持点赞关注QQ群304393689不定时更新"
    u8"作者霖木B站摆烂的霖木抖音号26953143462或搜索体力显示高度宽度鬼魂移速鬼魂距离"
    u8"非单人\\房主时因网络波动可能会导致速度不稳定输入拦截一键启动一键关闭一键重置"
    u8"理智显示团队平均值游戏设置不支持显示理智数据暂不可用正在重试"
    u8"设置未能保存，正在自动重试请检查磁盘空间和文件夹写入权限"
    u8"通灵板翻译等待回答是也许年轻年老就在这里很近很远在你身后所有人独自一人"
    u8"很好不好非常糟糕寒冷空虚强大虚弱愤怒害怕孤独迷失生病意外事故溺亡窒息被谋杀"
    u8"中枪坠落滑倒死于火灾被杀害杀戮伤害帮助准备好了波罗谁在那里厨房车库地下室阁楼"
    u8"储藏室卧室主卧男孩女孩儿童双床浴室客厅餐厅走廊门厅育儿室办公室图书馆教室体育馆"
    u8"食堂接待处大厅礼拜堂庭院牢房区医院病区禁区杂物间洗衣房工作间员工休息室候诊室经理"
    u8"健康良好一般较差不太疯狂非常疯狂已经疯狂兴奋受伤悲伤仇恨爱和平复仇被困在远处外面"
    u8"清洁工储物间柜台区用餐区冷藏室休息室男洗手间女洗手间入口浴室入口走廊游戏室维护室"
    u8"灯塔灯室上层下层阳台讲堂篮球场更衣室科学教室一楼二楼左侧右侧主走廊男教师女教师"
    u8"医务室探视区安保室控制室淋浴间监狱长露营区东北小径篝火区野餐区接待处建筑帐篷花园"
    u8"门廊侧边小径阁楼奖杯室软包病房连接走廊西东男病区女病区楼梯间仆人房手术室停尸房号"
    u8"套内主卧兴趣南希少年蓝色绿色橙色紫色楼下楼上卫生间健身房画室运动室酒吧游戏室厨房用餐区"
    u8"工作室缝纫室裁缝室茶室书房区域入口大厅中央警卫安保主管证物室前侧A区B区牢房"
    u8"中央小径北侧南侧东侧西侧情侣长椅储物区员工储藏室洗手间建筑清洁用品间码头木屋黄色红色白色灰色"
    u8"地下室中央走廊禁闭病区庭院区活动室宿舍访客室烘焙房锅炉房仆人餐厅护士长办公室医院病房禁闭室";

} // namespace

bool operator==(const Position& lhs, const Position& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

bool IsGameForeground(std::uintptr_t foreground, std::uintptr_t game) {
    return foreground != 0 && game != 0 && foreground == game;
}

std::uint64_t NormalizeSeconds(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return 0;
    return static_cast<std::uint64_t>(seconds);
}

std::uint64_t ElapsedSeconds(std::uint64_t startedMilliseconds, std::uint64_t nowMilliseconds) {
    if (nowMilliseconds <= startedMilliseconds) return 0;
    return (nowMilliseconds - startedMilliseconds) / 1000;
}

std::string FormatMmSs(std::uint64_t seconds) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << (seconds / 60)
           << ':' << std::setw(2) << (seconds % 60);
    return output.str();
}

Position ClampPosition(Position position, Size window, Size viewport) {
    const float maxX = std::max(0.0f, viewport.width - window.width);
    const float maxY = std::max(0.0f, viewport.height - window.height);
    if (!std::isfinite(position.x)) position.x = 0.0f;
    if (!std::isfinite(position.y)) position.y = 0.0f;
    return {std::clamp(position.x, 0.0f, maxX), std::clamp(position.y, 0.0f, maxY)};
}

CompactSideWindowLayout GetCompactSideWindowLayout(float menuHeight) {
    return {
        {344.0f, menuHeight},
        58.0f,
        {252.0f, 120.0f},
    };
}

AutoResizeDecision DecideAutoResize(bool appearing, bool hasBaseline,
                                    Size previous, Size current) {
    if (appearing) return {};
    if (!hasBaseline) return {true, false};
    const bool changed = previous.width != current.width ||
                         previous.height != current.height;
    return {false, changed};
}

bool operator==(const AxisSnapState& lhs, const AxisSnapState& rhs) { return lhs.anchor == rhs.anchor; }
bool operator==(const SnapState& lhs, const SnapState& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }

const MovableWindowResult* FindMovableWindowById(
    const MovableWindowResult* windows, std::size_t count, const char* id) {
    if (!windows || !id) return nullptr;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& window = windows[index];
        if (window.id && window.submitPosition && std::strcmp(window.id, id) == 0)
            return &window;
    }
    return nullptr;
}

const MovableWindowResult* FindTopmostMovableWindowAtPoint(
    const MovableWindowResult* windows, std::size_t count, Position point) {
    if (!windows || !std::isfinite(point.x) || !std::isfinite(point.y)) return nullptr;
    for (std::size_t index = count; index > 0; --index) {
        const auto& window = windows[index - 1];
        if (!window.id || !window.movable || window.bounds.size.width <= 0.0f ||
            window.bounds.size.height <= 0.0f) continue;
        const auto& bounds = window.bounds;
        if (point.x >= bounds.position.x && point.y >= bounds.position.y &&
            point.x < bounds.position.x + bounds.size.width &&
            point.y < bounds.position.y + bounds.size.height) return &window;
    }
    return nullptr;
}

SnapResult ComputeSnap(Bounds window, Size viewport, SnapState previous,
                       bool enabled, float enterDistance, float releaseDistance) {
    SnapResult result{};
    result.position = window.position;
    if (!enabled) {
        result.position = ClampPosition(result.position, window.size, viewport);
        return result;
    }

    const auto retain = [&](AxisSnapState previousAxis, float position,
                            float viewportExtent, float windowExtent,
                            AxisSnapState& nextAxis, bool& snapped, float& output) {
        if (previousAxis.anchor == SnapAnchor::None) return;
        const float target = AnchorCoordinate(previousAxis.anchor, viewportExtent, windowExtent);
        if (std::fabs(position - target) <= releaseDistance) {
            nextAxis = previousAxis;
            snapped = true;
            output = target;
        }
    };
    retain(previous.x, window.position.x, viewport.width, window.size.width,
           result.state.x, result.xSnapped, result.position.x);
    retain(previous.y, window.position.y, viewport.height, window.size.height,
           result.state.y, result.ySnapped, result.position.y);

    struct PointCandidate { SnapAnchor x; SnapAnchor y; float distanceSquared; };
    PointCandidate best{SnapAnchor::None, SnapAnchor::None, 0.0f};
    if (!result.xSnapped && !result.ySnapped) {
        for (const auto point : {std::pair{SnapAnchor::Quarter, SnapAnchor::Quarter},
                                 std::pair{SnapAnchor::ThreeQuarter, SnapAnchor::Quarter},
                                 std::pair{SnapAnchor::Quarter, SnapAnchor::ThreeQuarter},
                                 std::pair{SnapAnchor::ThreeQuarter, SnapAnchor::ThreeQuarter},
                                 std::pair{SnapAnchor::Center, SnapAnchor::Center}}) {
            const float x = AnchorCoordinate(point.first, viewport.width, window.size.width);
            const float y = AnchorCoordinate(point.second, viewport.height, window.size.height);
            const float dx = x - window.position.x;
            const float dy = y - window.position.y;
            const float distanceSquared = dx * dx + dy * dy;
            if (std::fabs(dx) <= enterDistance && std::fabs(dy) <= enterDistance &&
                (best.x == SnapAnchor::None || distanceSquared < best.distanceSquared))
                best = {point.first, point.second, distanceSquared};
        }
    }
    if (best.x != SnapAnchor::None) {
        result.state = {{best.x}, {best.y}};
        result.xSnapped = result.ySnapped = result.pointSnapped = true;
        result.position.x = AnchorCoordinate(best.x, viewport.width, window.size.width);
        result.position.y = AnchorCoordinate(best.y, viewport.height, window.size.height);
        result.point = {result.position.x + window.size.width * 0.5f,
                        result.position.y + window.size.height * 0.5f};
    }
    if (!result.xSnapped) {
        const auto candidate = BestAxis(window.position.x, viewport.width, window.size.width, enterDistance);
        if (candidate.anchor != SnapAnchor::None) {
            result.state.x.anchor = candidate.anchor;
            result.position.x = candidate.position;
            result.xSnapped = true;
        }
    }
    if (!result.ySnapped) {
        const auto candidate = BestAxis(window.position.y, viewport.height, window.size.height, enterDistance);
        if (candidate.anchor != SnapAnchor::None) {
            result.state.y.anchor = candidate.anchor;
            result.position.y = candidate.position;
            result.ySnapped = true;
        }
    }
    result.position = ClampPosition(result.position, window.size, viewport);
    return result;
}

Size InventoryLayoutSize(float slotSize, bool vertical) {
    constexpr float gap = 4.0f;
    constexpr float slotCount = 4.0f;
    const float longSide = slotSize * slotCount + gap * (slotCount - 1.0f);
    return vertical ? Size{slotSize, longSide} : Size{longSide, slotSize};
}

Position CenterPositionForResize(Position position, Size previousWindow,
                                 Size nextWindow, Size viewport) {
    return ClampPosition(
        {position.x + (previousWindow.width - nextWindow.width) * 0.5f,
         position.y + (previousWindow.height - nextWindow.height) * 0.5f},
        nextWindow, viewport);
}

float InventoryKeyFontSize(float slotSize) {
    const float clamped = std::isfinite(slotSize) ? std::clamp(slotSize, 50.0f, 96.0f) : 50.0f;
    return 16.0f + (clamped - 50.0f) * (34.0f - 16.0f) / (96.0f - 50.0f);
}

Position DefaultRightCenter(Size display, Size window, float margin) {
    return ClampPosition({display.width - window.width - margin,
                          (display.height - window.height) * 0.5f}, window, display);
}

Position DefaultUpperCenter(Size display, Size window) {
    return ClampPosition({(display.width - window.width) * 0.5f,
                          display.height * 0.25f - window.height * 0.5f}, window, display);
}

Position DefaultOverlayPosition(DefaultOverlaySlot slot, Size viewport, Size window) {
    const float scale = std::clamp(
        std::min(viewport.width / 1920.0f, viewport.height / 1080.0f), 0.7f, 1.5f);
    const float margin = std::clamp(20.0f * scale, 12.0f, 32.0f);
    const float topRowOffset = 120.0f * scale;
    const float secondRowY = margin + 80.0f * scale;
    const float contextOffset = 48.0f * scale;
    const float centerX = viewport.width * 0.5f;

    Position position{};
    switch (slot) {
    case DefaultOverlaySlot::TopLeftPrimary: position = {margin, margin}; break;
    case DefaultOverlaySlot::TopLeftSecondary: position = {margin, secondRowY}; break;
    case DefaultOverlaySlot::TopCenterLeft: position = {centerX - topRowOffset - window.width, margin}; break;
    case DefaultOverlaySlot::TopCenter: position = {centerX - window.width * 0.5f, margin}; break;
    case DefaultOverlaySlot::TopCenterRight: position = {centerX + topRowOffset, margin}; break;
    case DefaultOverlaySlot::TopRight: position = {viewport.width - margin - window.width, margin}; break;
    case DefaultOverlaySlot::CenterContext: position = {centerX - window.width * 0.5f, viewport.height * 0.5f + contextOffset}; break;
    case DefaultOverlaySlot::BottomLeft: position = {margin, viewport.height - margin - window.height}; break;
    case DefaultOverlaySlot::BottomCenter: position = {centerX - window.width * 0.5f, viewport.height - margin - window.height}; break;
    }
    return ClampPosition(position, window, viewport);
}

HuntingEvidenceResult ResolveHuntingEvidence(bool rpcKnown, bool rpcHunting, std::uint64_t rpcAgeMilliseconds,
                                             bool fieldsRead, bool huntingFlag, bool stateHunting,
                                             bool currentHunting, std::uint32_t negativeSamples) {
    if (rpcKnown && !rpcHunting) return {false, 0};
    if (fieldsRead && (huntingFlag || stateHunting)) return {true, 0};
    if (!fieldsRead) return {currentHunting, negativeSamples};
    const std::uint32_t samples = negativeSamples < 3 ? negativeSamples + 1 : negativeSamples;
    if (rpcKnown && rpcHunting && rpcAgeMilliseconds < 750) return {true, samples};
    return {currentHunting && samples < 3, samples};
}

bool SessionTimeWentBack(bool previousValid, float previousSeconds, bool currentValid, float currentSeconds,
                         float thresholdSeconds) {
    return previousValid && currentValid && std::isfinite(previousSeconds) &&
           std::isfinite(currentSeconds) && std::isfinite(thresholdSeconds) &&
           thresholdSeconds >= 0.0f && currentSeconds + thresholdSeconds < previousSeconds;
}

bool BlinkPhase(std::uint64_t elapsedMilliseconds, std::uint64_t periodMilliseconds) {
    return periodMilliseconds != 0 && ((elapsedMilliseconds / periodMilliseconds) % 2) != 0;
}

bool UpdateLifecycleResetState(LifecycleResetState& state, bool active) {
    if (!state.hasSeenActive) {
        if (!active) return false;
        state.hasSeenActive = true;
        state.active = true;
        return true;
    }
    if (state.active == active) return false;
    state.active = active;
    return true;
}

float ClampFontSize(float fontSize, float defaultValue) {
    if (!std::isfinite(fontSize)) return defaultValue;
    return std::clamp(fontSize, 16.0f, 56.0f);
}

float SnapFontSize(float fontSize, float defaultValue) {
    if (!std::isfinite(fontSize)) return defaultValue;
    constexpr float sizes[] = {16.0f, 20.0f, 24.0f, 26.0f, 28.0f, 32.0f, 36.0f, 42.0f, 48.0f, 56.0f};
    float closest = sizes[0];
    float closestDistance = std::abs(fontSize - closest);
    for (float size : sizes) {
        const float distance = std::abs(fontSize - size);
        if (distance < closestDistance) {
            closest = size;
            closestDistance = distance;
        }
    }
    return closest;
}

Color ClampColor(Color color, Color defaultValue) {
    float* components = &color.r;
    const float* defaults = &defaultValue.r;
    for (int i = 0; i < 4; ++i) {
        components[i] = std::isfinite(components[i]) ? std::clamp(components[i], 0.0f, 1.0f) : defaults[i];
    }
    return color;
}

bool IsConfigHeaderValid(ConfigHeader header, std::uint32_t expectedMagic,
                         std::uint32_t expectedVersion, std::uint32_t expectedPayloadLength) {
    return header.magic == expectedMagic && header.version == expectedVersion &&
           header.payloadLength == expectedPayloadLength;
}

std::vector<std::uint16_t> ExtractSupportedGlyphs(std::string_view utf8Text) {
    std::vector<std::uint16_t> result;
    std::unordered_set<std::uint16_t> seen;
    for (std::size_t i = 0; i < utf8Text.size();) {
        const auto first = static_cast<unsigned char>(utf8Text[i]);
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        if (first < 0x80) { codepoint = first; length = 1; }
        else if (first >= 0xC2 && first <= 0xDF) { codepoint = first & 0x1F; length = 2; }
        else if (first >= 0xE0 && first <= 0xEF) { codepoint = first & 0x0F; length = 3; }
        else if (first >= 0xF0 && first <= 0xF4) { codepoint = first & 0x07; length = 4; }
        else { ++i; continue; }
        if (length > utf8Text.size() - i) { ++i; continue; }
        bool valid = true;
        for (std::size_t j = 1; j < length; ++j) {
            const auto continuation = static_cast<unsigned char>(utf8Text[i + j]);
            if ((continuation & 0xC0) != 0x80) { valid = false; break; }
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if (!valid) { ++i; continue; }
        if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
            (length == 4 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) { ++i; continue; }
        i += length;

        const bool variation = codepoint >= 0xFE00 && codepoint <= 0xFE0F;
        const bool regional = codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF;
        const bool pictographBmp = (codepoint >= 0x2300 && codepoint <= 0x23FF) ||
                                  (codepoint >= 0x2600 && codepoint <= 0x27BF) ||
                                  (codepoint >= 0x2B00 && codepoint <= 0x2BFF);
        if (codepoint == 0x20E3) {
            if (!result.empty() && (result.back() == '#' || result.back() == '*' ||
                (result.back() >= '0' && result.back() <= '9'))) {
                seen.erase(result.back());
                result.pop_back();
            }
            continue;
        }
        if (codepoint > 0xFFFF || codepoint == 0x200D || variation || regional || pictographBmp) continue;
        const auto glyph = static_cast<std::uint16_t>(codepoint);
        if (seen.insert(glyph).second) result.push_back(glyph);
    }
    return result;
}

const char* GetFixedUiText() { return kFixedUiText; }

} // namespace timer_logic
