#include "feature_logic.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstdio>

namespace feature_logic {

InventorySlotState ClassifyInventorySlot(InventorySlotEvidence evidence) {
    if (!evidence.slotPresent) return InventorySlotState::Empty;
    if (!evidence.propValid) return InventorySlotState::Unknown;
    if (!evidence.equipmentFound) return InventorySlotState::NonEquipment;
    if (!evidence.storeItemValid || evidence.equipmentType < 0 || evidence.equipmentType >= 21 ||
        evidence.tier < 0 || evidence.tier >= 3)
        return InventorySlotState::Unknown;
    return InventorySlotState::Equipment;
}

InventorySlotText InventorySlotTextForState(InventorySlotState state) {
    return state == InventorySlotState::NonEquipment
        ? InventorySlotText{u8"可互", u8"动物"}
        : InventorySlotText{};
}

bool operator==(const InventoryTextOffset& lhs, const InventoryTextOffset& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

std::array<InventoryTextOffset, 4> InventoryTextOutlineOffsets() {
    return {{{-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}}};
}

int NormalizeInventoryActiveIndex(int rawIndex) {
    return rawIndex >= 0 && rawIndex < 4 ? rawIndex : -1;
}

bool ShouldHighlightInventorySlot(int activeIndex, int slotIndex) {
    return NormalizeInventoryActiveIndex(activeIndex) == slotIndex;
}

int MatchInventorySlot(std::uintptr_t heldObject, const std::array<std::uintptr_t, 4>& slots) {
    if (!heldObject) return -1;
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (slots[i] == heldObject) return static_cast<int>(i);
    return -1;
}

float FitInventoryFontSize(float configuredSize, float minimumSize,
                           float measuredWidth, float availableWidth) {
    if (!std::isfinite(configuredSize) || configuredSize <= 0.0f) configuredSize = 26.0f;
    if (!std::isfinite(minimumSize) || minimumSize <= 0.0f) minimumSize = 12.0f;
    minimumSize = std::min(minimumSize, configuredSize);
    if (!std::isfinite(measuredWidth) || measuredWidth <= 0.0f ||
        !std::isfinite(availableWidth) || availableWidth <= 0.0f || measuredWidth <= availableWidth)
        return configuredSize;
    return std::clamp(configuredSize * availableWidth / measuredWidth, minimumSize, configuredSize);
}

bool operator==(const NormalizedPoint& lhs, const NormalizedPoint& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.valid == rhs.valid;
}

int ClampCrosshairRadius(int radius) { return std::clamp(radius, 1, 30); }

StaminaValue NormalizeStamina(float current, float maximum) {
    if (!std::isfinite(current) || !std::isfinite(maximum) || maximum <= 0.0f ||
        current < -0.1f || current > maximum + 0.5f) return {};
    const float clamped = std::clamp(current, 0.0f, maximum);
    return {true, clamped, clamped / maximum};
}

float SmoothObservedSpeed(float previous, float observed, float elapsedSeconds,
                          float timeConstantSeconds) {
    if (!std::isfinite(observed) || observed < 0.0f) return previous;
    if (!std::isfinite(previous) || previous < 0.0f) return observed;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f ||
        !std::isfinite(timeConstantSeconds) || timeConstantSeconds <= 0.0f) return previous;
    const float alpha = 1.0f - std::exp(-elapsedSeconds / timeConstantSeconds);
    return previous + alpha * (observed - previous);
}

bool CanMoveOverlay(bool fixed, bool altDown) { return !fixed && altDown; }

bool HotkeyPressed(bool gameForeground, bool down, bool previousDown) {
    return gameForeground && down && !previousDown;
}

float NicknameFontSize(float distance) {
    constexpr float kMaximumSize = 22.0f;
    constexpr float kFullSizeDistance = 1.8f;
    constexpr float kPixelsPerMeter = 0.6f;
    constexpr float kLinearEndDistance = 20.0f;
    constexpr float kLinearEndSize =
        kMaximumSize - kPixelsPerMeter * (kLinearEndDistance - kFullSizeDistance);
    if (!std::isfinite(distance) || distance <= kFullSizeDistance) return kMaximumSize;
    if (distance <= kLinearEndDistance)
        return kMaximumSize - kPixelsPerMeter * (distance - kFullSizeDistance);
    // Keep both size and slope continuous at 20 m, then approach zero smoothly.
    return kLinearEndSize /
        (1.0f + (kPixelsPerMeter / kLinearEndSize) * (distance - kLinearEndDistance));
}

float NicknameAnchorY(float headScreenY, float heightPixels) {
    return headScreenY - heightPixels;
}

bool IsSnapshotFresh(long long elapsedMilliseconds, long long maximumAgeMilliseconds) {
    return elapsedMilliseconds >= 0 && elapsedMilliseconds <= maximumAgeMilliseconds;
}

std::string FormatGhostDistance(bool hunting, bool valid, float meters) {
    if (!hunting || !valid || !std::isfinite(meters) || meters < 0.0f)
        return "-- m";
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.2f m", meters);
    return text;
}

NormalizedPoint NormalizeScreenPoint(float x, float y, float width, float height) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0.0f || height <= 0.0f) return {};
    return {x / width, 1.0f - y / height, true};
}

std::string FormatEquipmentLabel(int equipmentType, int tier, bool showTier) {
    const std::array<const char*, 21> names = {
        u8"十字架", u8"激光投影仪", u8"EMF读取仪", u8"火光", u8"手电筒",
        u8"鬼魂笔记", u8"头戴设备", u8"点火器", u8"熏香", u8"运动传感器",
        u8"收音器", u8"照相机", u8"盐", u8"理智药物", u8"录音机",
        u8"声音传感器", u8"通灵盒", u8"温度计", u8"三脚架", u8"紫外线灯", u8"摄像机"
    };
    if (equipmentType < 0 || equipmentType >= static_cast<int>(names.size())) return {};
    std::string result = names[static_cast<std::size_t>(equipmentType)];
    if (!showTier) return result;
    const std::array<const char*, 3> tiers = {u8"一级", u8"二级", u8"三级"};
    if (tier >= 0 && tier < static_cast<int>(tiers.size()))
        result += std::string(u8" · ") + tiers[static_cast<std::size_t>(tier)];
    return result;
}

std::string TranslateOuijaAnswer(std::string_view answer) {
    std::size_t first = 0;
    while (first < answer.size() && std::isspace(static_cast<unsigned char>(answer[first]))) ++first;
    std::size_t last = answer.size();
    while (last > first && std::isspace(static_cast<unsigned char>(answer[last - 1]))) --last;
    std::string original(answer.substr(first, last - first));
    if (original.empty()) return {};

    std::string key;
    key.reserve(original.size());
    bool previousSpace = false;
    for (unsigned char value : original) {
        const bool separator = std::isspace(value) || value == '_' || value == '-' || value == '/';
        if (separator) {
            if (!key.empty() && !previousSpace) key.push_back(' ');
            previousSpace = true;
            continue;
        }
        key.push_back(static_cast<char>(std::tolower(value)));
        previousSpace = false;
    }
    while (!key.empty() && (key.back() == ' ' || key.back() == '.' || key.back() == ',' ||
                            key.back() == '!' || key.back() == '?' || key.back() == ':' ||
                            key.back() == ';')) key.pop_back();
    std::size_t normalizedFirst = 0;
    while (normalizedFirst < key.size() && (key[normalizedFirst] == ' ' || key[normalizedFirst] == '.' ||
           key[normalizedFirst] == ',' || key[normalizedFirst] == '!' || key[normalizedFirst] == '?' ||
           key[normalizedFirst] == ':' || key[normalizedFirst] == ';')) ++normalizedFirst;
    if (normalizedFirst > 0) key.erase(0, normalizedFirst);

    struct Translation { const char* source; const char* translated; };
    static constexpr Translation translations[] = {
        {"yes", u8"是"}, {"no", u8"否"}, {"maybe", u8"也许"},
        {"young", u8"年轻"}, {"old", u8"年老"},
        {"here", u8"就在这里"}, {"close", u8"很近"}, {"near", u8"很近"},
        {"far", u8"很远"}, {"behind you", u8"在你身后"},
        {"i'm here", u8"我在这里"}, {"im here", u8"我在这里"},
        {"i am here", u8"我在这里"}, {"away", u8"在远处"}, {"outside", u8"外面"},
        {"everyone", u8"所有人"}, {"you", u8"你"}, {"alone", u8"独自一人"},
        {"healthy", u8"健康"}, {"good", u8"良好"}, {"average", u8"一般"},
        {"bad", u8"较差"}, {"awful", u8"非常糟糕"},
        {"not very", u8"不太疯狂"}, {"very", u8"非常疯狂"}, {"insane", u8"已经疯狂"},
        {"cold", u8"寒冷"}, {"empty", u8"空虚"}, {"strong", u8"强大"},
        {"weak", u8"虚弱"}, {"angry", u8"愤怒"}, {"scared", u8"害怕"},
        {"excited", u8"兴奋"}, {"hurt", u8"受伤"}, {"lonely", u8"孤独"},
        {"sad", u8"悲伤"}, {"lost", u8"迷失"}, {"sick", u8"生病"},
        {"accident", u8"意外事故"}, {"drowned", u8"溺亡"},
        {"choked", u8"窒息"}, {"murder", u8"被谋杀"}, {"murdered", u8"被谋杀"},
        {"shot", u8"中枪"}, {"fell", u8"坠落"}, {"slipped", u8"滑倒"},
        {"fire", u8"死于火灾"}, {"killed", u8"被杀害"},
        {"hatred", u8"仇恨"}, {"kill", u8"杀戮"}, {"help", u8"帮助"},
        {"love", u8"爱"}, {"peace", u8"和平"}, {"revenge", u8"复仇"},
        {"trapped", u8"被困"}, {"vengeance", u8"复仇"},
        {"ready", u8"准备好了"}, {"polo", u8"波罗"},
        {"who's there", u8"谁在那里"}, {"whos there", u8"谁在那里"},
        {"who is there", u8"谁在那里"},
        {"kitchen", u8"厨房"}, {"garage", u8"车库"}, {"basement", u8"地下室"},
        {"attic", u8"阁楼"}, {"storage", u8"储藏室"}, {"storage room", u8"储藏室"},
        {"bedroom", u8"卧室"}, {"master bedroom", u8"主卧"},
        {"main bedroom", u8"主卧"}, {"ensuite", u8"套内浴室"},
        {"ensuite bathroom", u8"套内浴室"}, {"master bathroom", u8"主卧浴室"},
        {"main bathroom", u8"主浴室"}, {"hobby room", u8"兴趣室"},
        {"nancy's room", u8"南希卧室"}, {"nancys room", u8"南希卧室"},
        {"boys bedroom", u8"男孩卧室"}, {"girls bedroom", u8"女孩卧室"},
        {"boy's bedroom", u8"男孩卧室"}, {"girl's bedroom", u8"女孩卧室"},
        {"teen boys bedroom", u8"少年男孩卧室"},
        {"teen boy's bedroom", u8"少年男孩卧室"},
        {"child bedroom", u8"儿童卧室"}, {"twin bedroom", u8"双床卧室"},
        {"blue bedroom", u8"蓝色卧室"}, {"green bedroom", u8"绿色卧室"},
        {"orange bedroom", u8"橙色卧室"}, {"purple bedroom", u8"紫色卧室"},
        {"bathroom", u8"浴室"}, {"living room", u8"客厅"},
        {"dining room", u8"餐厅"}, {"hallway", u8"走廊"}, {"foyer", u8"门厅"},
        {"downstairs hallway", u8"楼下走廊"}, {"upstairs hallway", u8"楼上走廊"},
        {"basement hallway", u8"地下室走廊"}, {"upstairs toilet", u8"楼上卫生间"},
        {"downstairs toilet", u8"楼下卫生间"}, {"nursery", u8"育儿室"},
        {"office", u8"办公室"}, {"library", u8"图书馆"},
        {"classroom", u8"教室"}, {"gym", u8"健身房"}, {"gymnasium", u8"体育馆"},
        {"art studio", u8"画室"}, {"art room", u8"画室"},
        {"sports room", u8"运动室"}, {"bar and game room", u8"酒吧游戏室"},
        {"bar game room", u8"酒吧游戏室"},
        {"kitchen and dining area", u8"厨房用餐区"},
        {"kitchen dining area", u8"厨房用餐区"},
        {"gym bathroom", u8"健身房浴室"}, {"basement bathroom", u8"地下室浴室"},
        {"cafeteria", u8"食堂"},
        {"reception", u8"接待处"}, {"lobby", u8"大厅"}, {"chapel", u8"礼拜堂"},
        {"courtyard", u8"庭院"}, {"cell block", u8"牢房区"},
        {"hospital wing", u8"医院病区"}, {"restricted area", u8"禁区"},
        {"utility", u8"杂物间"}, {"utility room", u8"杂物间"},
        {"laundry room", u8"洗衣房"}, {"workshop", u8"工作间"},
        {"work room", u8"工作室"}, {"sewing room", u8"缝纫室"},
        {"seamstress room", u8"裁缝室"}, {"tea room", u8"茶室"},
        {"study", u8"书房"}, {"attic space", u8"阁楼区域"},
        {"staff room", u8"员工休息室"}, {"waiting room", u8"候诊室"},
        {"manager office", u8"经理办公室"},
        {"manager's office", u8"经理办公室"}, {"janitors closet", u8"清洁工储物间"},
        {"janitor's closet", u8"清洁工储物间"}, {"counter area", u8"柜台区"},
        {"dining area", u8"用餐区"}, {"cooler room", u8"冷藏室"},
        {"cold room", u8"冷藏室"}, {"break room", u8"休息室"},
        {"mens bathroom", u8"男洗手间"}, {"men's bathroom", u8"男洗手间"},
        {"womens bathroom", u8"女洗手间"}, {"women's bathroom", u8"女洗手间"},
        {"entry bathroom", u8"入口浴室"}, {"entry hallway", u8"入口走廊"},
        {"games room", u8"游戏室"}, {"bathroom hallway", u8"浴室走廊"},
        {"master bedroom hallway", u8"主卧走廊"},
        {"child bedroom hallway", u8"儿童卧室走廊"},
        {"maintenance room", u8"维护室"}, {"lantern room bottom", u8"灯塔灯室下层"},
        {"lantern room top", u8"灯塔灯室上层"}, {"balcony", u8"阳台"},
        {"lecture hall", u8"讲堂"}, {"basketball court", u8"篮球场"},
        {"locker room", u8"更衣室"}, {"science classroom", u8"科学教室"},
        {"science class room", u8"科学教室"},
        {"first floor main corridor", u8"一楼主走廊"},
        {"first floor front corridor", u8"一楼前侧走廊"},
        {"first floor left corridor", u8"一楼左侧走廊"},
        {"first floor right corridor", u8"一楼右侧走廊"},
        {"second floor main corridor", u8"二楼主走廊"},
        {"second floor front corridor", u8"二楼前侧走廊"},
        {"second floor left corridor", u8"二楼左侧走廊"},
        {"second floor right corridor", u8"二楼右侧走廊"},
        {"downstairs boys bathroom", u8"楼下男孩洗手间"},
        {"downstairs girls bathroom", u8"楼下女孩洗手间"},
        {"upstairs boys bathroom", u8"楼上男孩洗手间"},
        {"upstairs girls bathroom", u8"楼上女孩洗手间"},
        {"downstairs male teachers bathroom", u8"楼下男教师洗手间"},
        {"downstairs female teachers bathroom", u8"楼下女教师洗手间"},
        {"upstairs male teachers bathroom", u8"楼上男教师洗手间"},
        {"upstairs female teachers bathroom", u8"楼上女教师洗手间"},
        {"main hallway", u8"主走廊"}, {"office hallway", u8"办公室走廊"},
        {"entrance", u8"入口大厅"}, {"entrance hallway", u8"入口走廊"},
        {"central control", u8"中央控制室"}, {"central control room", u8"中央控制室"},
        {"infirmary", u8"医务室"}, {"visitation", u8"探视区"},
        {"infirmary hallway", u8"医务室走廊"},
        {"visitation room", u8"探视室"}, {"visitation hallway", u8"探视区走廊"},
        {"visitation security", u8"探视区安保室"},
        {"cafeteria security", u8"食堂安保室"},
        {"security", u8"安保室"}, {"security room", u8"安保室"},
        {"control room", u8"控制室"}, {"guard office", u8"警卫办公室"},
        {"guards office", u8"警卫办公室"}, {"chief security office", u8"安保主管办公室"},
        {"evidence room", u8"证物室"},
        {"showers", u8"淋浴间"}, {"bathrooms", u8"洗手间"},
        {"toilets", u8"洗手间"}, {"warden office", u8"监狱长办公室"},
        {"warden's office", u8"监狱长办公室"},
        {"a block hallway", u8"A区牢房走廊"}, {"b block hallway", u8"B区牢房走廊"},
        {"first floor a block left", u8"一楼A区左侧牢房"},
        {"first floor a block right", u8"一楼A区右侧牢房"},
        {"second floor a block left", u8"二楼A区左侧牢房"},
        {"second floor a block right", u8"二楼A区右侧牢房"},
        {"first floor b block left", u8"一楼B区左侧牢房"},
        {"first floor b block right", u8"一楼B区右侧牢房"},
        {"second floor b block left", u8"二楼B区左侧牢房"},
        {"second floor b block right", u8"二楼B区右侧牢房"},
        {"campsite a", u8"露营区A"}, {"campsite b", u8"露营区B"},
        {"central path", u8"中央小径"}, {"north path", u8"北侧小径"},
        {"south path", u8"南侧小径"}, {"east path", u8"东侧小径"},
        {"west path", u8"西侧小径"}, {"north east path", u8"东北小径"},
        {"northeast path", u8"东北小径"}, {"lovers bench", u8"情侣长椅区"},
        {"lover's bench", u8"情侣长椅区"}, {"campfire", u8"篝火区"},
        {"picnic area", u8"野餐区"}, {"reception building", u8"接待处建筑"},
        {"games tent", u8"游戏帐篷"}, {"food tent", u8"食物帐篷"},
        {"storage tent", u8"储物帐篷"}, {"storage tents", u8"储物帐篷区"},
        {"storage area", u8"储物区"}, {"staff storage", u8"员工储藏室"},
        {"bathroom building", u8"洗手间建筑"}, {"toilet block", u8"洗手间建筑"},
        {"male toilets", u8"男洗手间"}, {"female toilets", u8"女洗手间"},
        {"cleaning closet", u8"清洁用品间"}, {"pier", u8"码头"},
        {"cabin", u8"木屋"}, {"cabin living room", u8"木屋客厅"},
        {"cabin kitchen", u8"木屋厨房"}, {"cabin bathroom", u8"木屋浴室"},
        {"cabin bedroom", u8"木屋卧室"}, {"blue tent", u8"蓝色帐篷"},
        {"green tent", u8"绿色帐篷"}, {"yellow tent", u8"黄色帐篷"},
        {"red tent", u8"红色帐篷"}, {"white tent", u8"白色帐篷"},
        {"grey tent", u8"灰色帐篷"}, {"gray tent", u8"灰色帐篷"},
        {"garden porch", u8"花园门廊"},
        {"garden", u8"花园"}, {"side path", u8"侧边小径"},
        {"attic hallway", u8"阁楼走廊"}, {"trophy room", u8"奖杯室"},
        {"attic bedroom", u8"阁楼卧室"}, {"padded room", u8"软包病房"},
        {"connecting corridor", u8"连接走廊"},
        {"basement west corridor", u8"地下室西走廊"},
        {"basement east corridor", u8"地下室东走廊"},
        {"courtyard corridor", u8"庭院走廊"}, {"entrance corridor", u8"入口走廊"},
        {"basement corridor", u8"地下室中央走廊"},
        {"male wing", u8"男病区"}, {"female wing", u8"女病区"},
        {"hospital wing", u8"医院病区"}, {"restricted wing", u8"禁闭病区"},
        {"courtyard wing", u8"庭院区"},
        {"left entrance stairwell", u8"入口左楼梯间"},
        {"right entrance stairwell", u8"入口右楼梯间"},
        {"west stairwell", u8"西侧楼梯间"}, {"east stairwell", u8"东侧楼梯间"},
        {"male bathroom", u8"男病区浴室"}, {"female bathroom", u8"女病区浴室"},
        {"male dorm room", u8"男病区宿舍"}, {"female dorm room", u8"女病区宿舍"},
        {"male day room", u8"男病区活动室"}, {"female day room", u8"女病区活动室"},
        {"west visitors room", u8"西侧访客室"},
        {"west visitors' room", u8"西侧访客室"},
        {"east visitors room", u8"东侧访客室"},
        {"east visitors' room", u8"东侧访客室"},
        {"bake house", u8"烘焙房"}, {"boiler room", u8"锅炉房"},
        {"servant dining room", u8"仆人餐厅"},
        {"servants dining room", u8"仆人餐厅"},
        {"servants' dining room", u8"仆人餐厅"},
        {"matron office", u8"护士长办公室"}, {"matron's office", u8"护士长办公室"},
        {"basement storage", u8"地下室储藏室"},
        {"servant room", u8"仆人房"}, {"operating room", u8"手术室"},
        {"morgue", u8"停尸房"}, {"pantry", u8"食品储藏室"},
    };
    for (const auto& translation : translations) {
        if (key == translation.source) return translation.translated;
    }

    std::string compactKey;
    compactKey.reserve(key.size());
    for (const char value : key) {
        if (value != ' ' && value != '\'' && value != '.') compactKey.push_back(value);
    }
    for (const auto& translation : translations) {
        std::string compactSource;
        for (const char value : std::string_view(translation.source)) {
            if (value != ' ' && value != '\'' && value != '.') compactSource.push_back(value);
        }
        if (compactKey == compactSource) return translation.translated;
    }

    struct NumberedRoom { const char* source; const char* translated; };
    static constexpr NumberedRoom numberedRooms[] = {
        {"classroom", u8"教室"}, {"science classroom", u8"科学教室"},
        {"science class room", u8"科学教室"}, {"storage room", u8"储藏室"},
        {"padded room", u8"软包病房"}, {"servant room", u8"仆人房"},
        {"servants room", u8"仆人房"}, {"servants' room", u8"仆人房"},
        {"male room", u8"男病区病房"}, {"female room", u8"女病区病房"},
        {"hospital room", u8"医院病房"}, {"restricted room", u8"禁闭室"},
        {"bedroom", u8"卧室"}, {"bathroom", u8"浴室"}, {"cell block", u8"牢房区"},
        {"blue tent", u8"蓝色帐篷"}, {"green tent", u8"绿色帐篷"},
        {"yellow tent", u8"黄色帐篷"}, {"red tent", u8"红色帐篷"},
        {"white tent", u8"白色帐篷"}, {"grey tent", u8"灰色帐篷"},
        {"gray tent", u8"灰色帐篷"},
    };
    for (const auto& room : numberedRooms) {
        const std::string prefix = std::string(room.source) + ' ';
        if (key.rfind(prefix, 0) != 0 || key.size() == prefix.size()) continue;
        const std::string suffix = key.substr(prefix.size());
        if (std::all_of(suffix.begin(), suffix.end(), [](unsigned char value) {
                return std::isalnum(value) != 0;
            })) return std::string(room.translated) + suffix + u8"号";
    }
    return original;
}

} // namespace feature_logic
