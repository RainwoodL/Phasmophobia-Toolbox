# 昵称固定锚点与圣木玩家默认色 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让玩家昵称绘制 Y 坐标仅由投影头部 Y 与设置高度决定，并将圣木计时器玩家颜色默认值改为纯白。

**Architecture:** 将昵称 Y 锚点保留为 `feature_logic` 中的无平台纯函数，渲染层只传入屏幕 Y 和像素高度；距离继续仅控制现有字号曲线。圣木玩家默认色继续以 `SmudgeSettings` 默认成员初始化器作为唯一来源，Normalize 的非法值回退和 UI 重置均复用该默认结构，不改变已有有效配置的加载值。

**Tech Stack:** C++17、CMake、CTest、MSVC、Dear ImGui

---

### Task 1: 昵称固定 Y 锚点

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/feature_logic.h`
- Modify: `payload/feature_logic.cpp`
- Modify: `payload/player_nicknames.cpp`

- [ ] 将测试改为 `assert(NicknameAnchorY(500.0f, 20.0f) == 480.0f);`，先构建 Debug 测试目标，确认因旧三参数 API 而 RED。
- [ ] 将函数改为 `float NicknameAnchorY(float headScreenY, float heightPixels)`，仅返回 `headScreenY - heightPixels`。
- [ ] 玩家昵称绘制使用两参数函数；水平位置仍减去 `textSize.x * 0.5f`，字号仍由 `NicknameFontSize(distance, settings.fontSize)` 计算。
- [ ] 重新构建并运行 Debug CTest，确认 GREEN。

### Task 2: 圣木计时器玩家默认色

**Files:**
- Modify: `payload/timer_logic_tests.cpp`
- Modify: `payload/settings_store.h`

- [ ] 测试包含 `settings_store.h`，断言默认 `SmudgeSettings{}.playerColor` 为 RGBA `(1,1,1,1)`；先运行 Debug 测试并确认旧粉色值导致 RED。
- [ ] 仅将 `SmudgeSettings::playerColor` 默认成员初始化器改为纯白。保留 `Normalize()` 使用默认结构作为非法颜色回退，保留重置按钮赋值 `SmudgeSettings{}`，不加入加载迁移或覆盖逻辑。
- [ ] 重新构建并运行 Debug CTest，确认 GREEN；搜索旧默认色，确认没有独立的重置硬编码。

### Task 3: 全量验证

**Files:**
- Verify only

- [ ] 构建 Debug 并运行全部 CTest。
- [ ] 构建 Release 并运行全部 CTest。
- [ ] 获取编辑器诊断；静态确认玩家 Y 定位不含 `textSize.y` 或 scale，默认玩家色为纯白，且保存配置加载路径仍直接保留有效颜色。
- [ ] 不创建提交。
