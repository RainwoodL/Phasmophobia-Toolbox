# 2026-09-11 游戏更新检查与修复

范围：主项目 `payload/`，不修改根目录 `定制/`。保留开始工作前已有的修改。

后续更正：按用户要求，已从主项目删除鬼魂类型显示，包括菜单、快捷键、采样、配置字段和构建依赖。配置升级为版本 20，读取版本 19 时校验完整 CRC 后丢弃已删除功能的尾部数据，保留其他设置。下表记录当时检查结果，不代表该功能仍保留。

## 对照基准

- `dump.cs` 已从 `F:\Roomten\dump.cs` 同步，SHA-256：`F85F79D5F7FDD86286FB9B99FAD3DD6A5ED922847ACA4C000F20EE9D8D1BD1A5`。
- 本地与游戏目录 `GameAssembly.dll` 的 SHA-256 一致：`C0C2F30492884000C3079440FE5F4F9375F541F22F70F6546CA4D1DBF3175848`。
- 方法体对照目录：`D:\steam\steamapps\common\Phasmophobia\Phasmophobia_BackUpThisFolder_ButDontShipItWithYourGame\il2cppOutput`。
- 对照旧 dump 后，所检查的 20 个相关类的字段偏移序列一致；未发现本次更新引起的已检查字段位移。六个 Hook 的签名保持一致。方法入口通过 IL2CPP 解析，没有需要整体替换的固定 RVA 表。

## 功能检查

| 功能 | 调用或字段依据 | 结果与修复 |
| --- | --- | --- |
| 圣木计时 | `WhiteSage.UseNetworked`，发送者昵称 | 在原方法完成后记录事件；保留游戏忽略传入 bool 的语义 |
| 游戏时间 | `LevelStats.get_Instance` → `timeSpentInvestigating` | 每次取得当前单例，避免上一局对象仍可读导致时间卡住 |
| 猎杀计时 | 鬼魂状态快照 → 本地计时 | 随状态修复，并在会话清理时清空计时状态 |
| 鬼魂状态 | `GhostAI.Hunting` → hunting 字段 `0xF3` | 原方法执行后确认实际状态变化，避免被拒绝的 RPC 触发假猎杀 |
| 鬼魂移速 | GhostAI → NavMeshAgent / 房主同步 / 位移估算 | 回调异常后清空样本并冷却重试；Photon 事件 Code 改为按字段名解析 |
| 鬼魂类型 | GhostAI → GhostInfo → 类型 `0x28` | 未发现类型偏移变化；修复位置提交只改配置、不移动 ImGui 窗口的问题 |
| 十字架计时 | `CrucifixUseNetworked` → `useCount` | 保留原方法前后次数变化判断；当前 `useCount` 为 `0x410` |
| 通灵板翻译 | `PlayMessageSequence` → 答案字符列表 | 游戏通过校验后才 Clear/填充列表；按 List 的版本变化确认接受，再发布翻译 |
| 准星信息 | PCPropGrab → PCCanvas / Collider → 父组件 | 两个 Collider 从元数据枚举并检查数量，避免固定 `0x80/0xD8`；解析失败限制重试频率 |
| 物品栏 | PCPropGrab → 列表 / 当前槽位 → StoreItem | 未发现对应字段偏移变化；异常后清空快照并自动重试 |
| 体力 | LocalPlayer → PlayerStamina | 当前体力字段 `0x40` 与 dump 一致 |
| 理智 | Network → Player → PlayerSanity；Difficulty | 已检查偏移一致；保留 LevelValues 两个 Difficulty 字段的区别，不能用“唯一同类型字段”替换 |
| 屏幕准星 | 本地 ImGui 绘制 | 不依赖游戏字段偏移 |
| 玩家昵称 | Network → PlayerSpot → Photon Player / 游戏 Player → 头部 / Camera | 实际读取改用解析出的偏移；删除未使用的骨骼接口；补齐入口校验、解析重试和快照有效期处理 |
| 菜单设置 | 本地配置 / 热键 / 帧率 | 修复菜单设置页误绘制玩家昵称设置；采样帧率通过原子变量传给游戏线程 |

通灵板原方法依据：`Assembly-CSharp__112.cpp` 的 `PlayMessageSequence`，发送者、屏蔽词、激活状态检查先于 `List<string>.Clear()`。列表字段和 `_version` 从运行时元数据解析，避免依赖 Tween 对象地址；对象池可能重复使用相同地址。

## 持久化修复

1. 延迟保存原先在每次 MarkDirty 后重新计时，持续移动或坐标误差会一直推迟写盘。现在保留 1 秒静默保存，并设置 2 秒最长等待。
2. 恢复的归一化坐标对齐到实际像素，避免与 ImGui 的坐标取整反复冲突。
3. 拖动结束提交最后一帧的位置，同时更新像素坐标和归一化锚点。吸附参考边界也更新为恢复后的实际位置。
4. 关闭菜单、结束拖动时立即保存。失败保持待保存状态，按 2 秒间隔重试；菜单设置页显示失败提示。
5. 版本 17 及之后保留主动清空的热键。版本 16 及之前没有热键段，迁移时补齐默认值。
6. 保留现有 CRC、临时文件、flush/commit 和替换文件流程；格式仍为版本 19，无需清空原配置。

## 验证与边界

- Release payload 编译、链接通过；6 项 CTest 全部通过。
- 新增确定性保存调度测试：静默保存、持续变更最长等待、失败重试限速。
- 新增隔离临时目录中的实际写盘/重载测试：完整配置恢复、清空热键保留、文件被占用时旧文件不变、解除占用后无需再次修改即可重试成功。
- 验证版本 6/16 的默认热键迁移，以及版本 17/18/19 的空热键保留；验证浮点位置恢复可稳定落到像素。
- 未启动或注入游戏，Hook 接受条件、多人房间、切图和实际鼠标拖动尚未完成游戏内验证。静态匹配与本地测试不能替代这些运行时检查。
