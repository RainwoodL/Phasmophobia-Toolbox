# 2026-09-12 E:\Phasmophobia 全功能对齐

范围：只检查主项目 `payload/`、`injector/` 和根构建；按要求完全忽略 `定制/`，没有修改其中任何文件，也没有处理开始工作前已有的工作区变更。

## 对照基准

- `E:\Phasmophobia\dump.cs` 与工作区 `dump.cs` 完全相同，SHA-256 均为 `F85F79D5F7FDD86286FB9B99FAD3DD6A5ED922847ACA4C000F20EE9D8D1BD1A5`。
- `dump.deobfuscated.cs`：`796402FC14C9EC71AF1A893B5330CA67240684D3939FD2D5D45A0DCCC515734E`。
- `dump.enhanced.cs`：`5C356EE4E12590BA86D23C65863182D7018FE1134D9BCC84F2F639D20B1F116C`。
- `deobfuscation-map.csv`：`3F6D0A9F1EC04AC75F1009B453A91D5893BC017A787EBCB18EEF91A963863004`。
- 当前工作区 `GameAssembly.dll`：`C0C2F30492884000C3079440FE5F4F9375F541F22F70F6546CA4D1DBF3175848`。
- 增强报告覆盖全部 `132249` 个元数据方法和 RVA，解析 `136196` 个 C++ 定义；`130882` 个方法带 C++ 位置，符号/索引不匹配数为 `0`。

## 当前菜单逐项结论

| 菜单 | E:\Phasmophobia 依据 | 结论 |
| --- | --- | --- |
| 菜单设置 | 本地 ImGui、热键、配置和帧率逻辑 | 不依赖游戏 dump；无需改动 |
| 圣木计时器 | `WhiteSage.UseNetworked(bool, PhotonMessageInfo)`，RVA `0x821010` | Hook 名称、参数和调用后确认逻辑一致 |
| 游戏时间 | `LevelStats.get_Instance` → `timeSpentInvestigating` `0x34` | 字段与单例读取一致 |
| 猎杀计时器 | 复用鬼魂状态采样和 RPC 时间点 | 数据来源一致 |
| 鬼魂状态 | `GhostAI.Hunting(bool, int, PhotonMessageInfo)`，RVA `0x210F3A0`；状态 `0x30`，hunting `0xF3` | Hook 签名、枚举值 `hunting=2` 和字段一致 |
| 鬼魂移速 | `GhostAI.navMeshAgent` `0x40`、NavMeshAgent 速度及 Photon 同步 | 游戏类字段由元数据解析；同步协议不依赖 dump |
| 十字架计时器 | `Crucifix.CrucifixUseNetworked(bool, PhotonMessageInfo)`，RVA `0x5D3030`；`useCount` `0x410` | Hook 和使用次数确认一致 |
| 通灵板翻译 | `OuijaBoard.PlayMessageSequence(string, PhotonMessageInfo)`，RVA `0x711320`；答案列表 `0x3F0` | 方法签名一致；列表字段和 `_version` 继续动态解析 |
| 准星信息 | `PCPropGrab` → `PCCanvas` `0x28`；两个 Collider `0x80/0xD8`；交互状态枚举 `0x88` | 增加唯一枚举字段动态解析，`0x88` 作为本版本回退 |
| 物品栏 | `PCPropGrab` 的 `List<PhotonObjectInteract>` `0x50`、活动槽 `0x58`；`Equipment.storeItem` `0x180` | 背包列表改为按元素类型动态解析，`0x50` 作为本版本回退 |
| 体力显示 | `Network` → `LocalPlayer` → `PlayerStamina`；当前体力 `0x40` | 对象链动态解析；当前体力字段一致 |
| 理智显示 | `Network.PlayerSpot` 列表 `0x38`；`PlayerSanity` 当前 insanity `0x30`；当前 `Difficulty` `0x38`；`sanityMonitor` `0x90`；平均值方法 RVA `0x238DB10` | 玩家列表改为按元素类型动态解析；其余字段与方法一致 |
| 屏幕准星 | 本地 ImGui 绘制 | 不依赖游戏 dump；无需改动 |
| 玩家昵称 | `Network.PlayerSpot` `0x38`；`Player` 死亡标志 `0x28`、头部目标 `0x88`；`NetworkedHeadMountedSocket.modelHeadSpot` `0x30` | 当前实现已优先动态解析列表、socket 和 head spot，并保留已验证的直接头部目标回退 |

## 本次代码调整

1. IL2CPP 运行时增加“唯一实例枚举字段”解析，使用当前 `GameAssembly.dll` 已确认导出的 `il2cpp_class_is_enum`。
2. 准星信息优先解析 `PCCanvas` 唯一交互状态枚举，不再直接把 `canvas + 0x88` 写在采样逻辑里。
3. 物品栏优先按 `List<PhotonObjectInteract>` 定位背包字段。
4. 理智显示优先按 `List<Network.PlayerSpot>` 定位玩家列表字段。

以下固定值继续保留，因为增强 dump 中同类型字段不唯一，单靠反射无法可靠区分：`GhostAI` 状态 `0x30` 与 hunting 标志 `0xF3`、`PlayerStamina` 当前值 `0x40`、`PlayerSanity` 当前 insanity `0x30`、`LevelValues` 当前难度 `0x38`、`Player` 死亡标志 `0x28` 和直接头部目标 `0x88`。这些位置已同时通过字段表和对应 native 方法体核对。`PhotonMessageInfo` 仍为 x64 下 24 字节，现有 Hook ABI 无需修改。

## 验证

- Release 完整构建通过，生成 `payload.dll` 和 `PhasmophobiaInjector.exe`。
- CTest 共 8 项全部通过：计时/布局、物品纹理、配置持久化、覆盖层可见性、鬼速同步、用户消息、SHA-256、payload 加密。
- 启用 MSVC `/analyze` 的 `payload` 目标通过；完整分析构建仍会被既有的 `settings_store_tests.cpp` C6262（测试函数栈使用量）警告阻断，该诊断与本次游戏对齐改动无关。
- 尚未启动或注入游戏；多人 RPC、切图和实际交互对象仍需游戏内运行验证。
