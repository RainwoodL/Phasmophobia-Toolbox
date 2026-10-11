# 圣木计时器与游戏时间设计

## 目标

在现有进程内 D3D11/ImGui Overlay 中新增两个相互独立的常驻功能：

1. 圣木计时器：捕获圣木点燃事件，显示点燃后的经过时间、触发玩家和点燃时的本局时间。
2. 游戏时间：显示当前调查的本局统计时间。

实现仅参考 `K:\备份\GhostHuntingToolkit项目` 的数据分层思路，不复制其源代码、固定偏移、Hook 字节实现或设置存储实现。

## 功能规格

### 圣木计时器

默认显示：

```text
圣木被点燃: 00:00
玩家:--  记录时间: 00:00
```

- 默认字体大小：26 px。
- 无记录时主计时颜色：白色。
- 有记录时主计时颜色：黄色。
- 玩家字段默认颜色：淡红色。
- 记录时间字段默认颜色：白色。
- 点燃事件来源：`WhiteSage.UseNetworked`。
- 点燃时立即产生记录；暂不判断圣木是否实际影响鬼。
- 主计时使用单调时钟计算，避免游戏时间缩放或来源切换影响经过时间。
- 玩家名无法解析时显示 `--`。
- 尚无点燃记录时，主计时和记录时间均显示 `00:00`。
- 新点燃事件替换当前展示的最新记录，并将主计时归零。

### 游戏时间

默认显示：

```text
游戏时间: 00:00
```

- 默认字体大小：26 px。
- 默认字体颜色：白色。
- 数据语义为当前调查的本局统计时间。
- 从运行时解析到的 `LevelStats` 实例读取本局时间。
- 不使用进程累计时间代替本局时间。
- 数据暂不可用时显示 `00:00`。
- 时间值为负数、非有限数或超出合理范围时视为不可用。

## 最终文件边界

实现采用“功能独立 + 公共层”结构。两个功能模块不包含彼此的头文件、不调用彼此的符号，也不共享可变全局状态；它们只能依赖公共层。`overlay` 仅在顶层装配并调度模块。

| 文件 | 唯一职责 | 明确禁止 |
|---|---|---|
| `payload/overlay.cpp` / `payload/overlay.h` | ImGui 生命周期、菜单总入口、每帧模块调度、模块初始化与关闭顺序 | IL2CPP 导出解析、类/方法解析、安全内存读取、WhiteSage Hook、LevelStats 读取、事件状态、功能窗口细节、功能专属设置、配置序列化或磁盘写入 |
| `payload/smudge_timer.cpp` / `payload/smudge_timer.h` | `WhiteSage.UseNetworked` Hook、玩家名提取、线程安全事件快照、圣木窗口、圣木专属菜单和设置 | 解析或读取 `LevelStats`；包含或调用 `game_time`；直接读写配置文件 |
| `payload/game_time.cpp` / `payload/game_time.h` | `LevelStats` 类/方法/实例使用、本局时间查询、游戏时间窗口、游戏时间专属菜单和设置 | WhiteSage Hook、圣木事件；包含或调用 `smudge_timer`；直接读写配置文件 |
| `payload/il2cpp_runtime.cpp` / `payload/il2cpp_runtime.h` | IL2CPP API 初始化、按命名空间和类名解析类、解析方法、安全内存读取、IL2CPP 字符串复制、模块范围与可执行地址校验 | 任何具体功能窗口、功能设置、事件状态或配置持久化策略 |
| `payload/settings_store.cpp` / `payload/settings_store.h` | 配置加载、格式校验、脏标记、延迟保存、关闭时刷新和原子写入 | ImGui 绘制、WhiteSage/LevelStats 逻辑、功能运行时状态 |
| `payload/timer_logic.cpp` / `payload/timer_logic.h` | 时间规范化与格式化、单调经过时间、位置/字号/颜色值域约束、配置头校验等纯逻辑 | Windows、ImGui、D3D11、IL2CPP、Hook、文件 I/O |
| `payload/timer_logic_tests.cpp` | `timer_logic` 的独立单元测试 | 游戏进程、Windows Hook 或 ImGui 依赖 |

`payload/CMakeLists.txt` 负责将上述实现文件加入 `payload`，并注册独立的 `timer_logic_tests` CTest 目标。`payload/payload.cpp` 保持 DLL 和 D3D11 Hook 的现有生命周期入口，不承载两个功能的实现。当前 `overlay.cpp` 中不属于上述最终边界的旧 GhostAI 探测、入口覆盖和实例扫描代码随实施删除；它们不迁入公共层，也不由两个新功能接管。

## 依赖方向与模块契约

```text
payload.cpp
  → overlay

overlay
  → smudge_timer
  → game_time
  → settings_store

smudge_timer → il2cpp_runtime, timer_logic, settings_store 的数据契约
game_time    → il2cpp_runtime, timer_logic, settings_store 的数据契约
settings_store → timer_logic
il2cpp_runtime → Windows/IL2CPP runtime
timer_logic → C++ 标准库

smudge_timer -X→ game_time
game_time    -X→ smudge_timer
```

顶层装配允许 `overlay` 分别调用两个模块，但不能把某个功能的实现搬回 `overlay.cpp`。圣木事件需要记录点燃时的本局时间，但 `smudge_timer` 不得调用 `game_time`：`smudge_timer.h` 暴露不依赖具体模块的本局时间提供器契约，`overlay` 在初始化调度时将 `game_time` 的只读查询函数注入该契约。圣木模块只调用已注入的函数对象；其头文件和实现均不包含 `game_time.h`。该装配方式保持源代码依赖单向，并确保 `LevelStats` 仍只由 `game_time.cpp/.h` 负责。

公共生命周期契约遵循以下形态：

- `il2cpp_runtime::Initialize()` / `Shutdown()`：建立和清理公共运行时解析状态。
- `settings_store::Load()` / `MarkDirty()` / `Tick()` / `Flush()`：管理一份版本化设置快照。
- `game_time::Initialize()` / `Shutdown()` / `ReadSnapshot()` / `DrawWindow()` / `DrawSettings()`。
- `smudge_timer::Initialize(GameTimeProvider)` / `Shutdown()` / `DrawWindow()` / `DrawSettings()`。
- `overlay::Initialize()` 依次初始化公共层和两个功能；`overlay::Render()` 只开始 ImGui 帧、调用模块窗口与菜单入口、调用延迟保存 tick，并提交 DrawData；`overlay::Shutdown()` 先停止功能 Hook/工作线程，再刷新设置和释放 ImGui/D3D 资源。

具体签名可按现有代码风格调整，但职责和依赖方向不得改变。

## 公共 IL2CPP 运行时层

`il2cpp_runtime.cpp/.h` 沿用当前项目从 `GameAssembly.dll` 动态解析 IL2CPP 导出 API 的机制，并集中提供：

- 按命名空间和类名查找类型。
- 按方法名和显式参数数查找方法。
- 获取并验证方法入口。
- 验证地址属于指定模块映像。
- 验证内存页可读或可执行，并防止范围溢出。
- 安全读取指针、整数、浮点值和字节范围。
- 将受长度限制的 IL2CPP UTF-16 字符串复制为拥有型 UTF-8 字符串。

不硬编码绝对地址或方法 RVA。如具体功能必须使用经当前 `dump.cs` 确认的字段偏移，偏移常量留在该功能实现中，所有解引用均通过公共安全读取接口完成。

## 圣木计时器模块

### Hook 与 ABI 门槛

`smudge_timer.cpp/.h` 独立拥有 WhiteSage Hook 的目标地址、原入口信息、原字节、安装状态、停止标志和工作线程状态。

安装 Hook 前必须完成运行时验证门槛：

1. 按类 `WhiteSage`、方法 `UseNetworked`、显式参数数 `2` 解析元数据。
2. 确认入口属于 `GameAssembly.dll` 映像、页面可执行且所需覆盖字节可读。
3. 检查当前运行版本的方法入口与调用点，确认 24 字节值类型 `PhotonMessageInfo` 的原生传参方式，以及是否存在尾部 `MethodInfo*`。
4. 只有运行时证据与当前 `dump.cs` 一致后才声明原生函数类型并允许安装；无法确认时保持功能安全禁用，不猜测 ABI。

预期托管语义为 `WhiteSage.UseNetworked(bool, Photon.Pun.PhotonMessageInfo)`，但不得据此直接假设原生 ABI。现有 14 字节入口覆盖仅可在上述校验通过后复用，不复制参考项目 Hook 实现，本次也不扩展为 Hook 框架重写。

### 事件快照

Hook 回调只执行以下步骤：

1. 按已验证 ABI 从调用参数提取玩家对象并复制玩家名；失败使用 `--`。
2. 调用初始化时注入的只读本局时间提供器；失败记录 `0`。
3. 写入完整、拥有型、线程安全的最新圣木事件快照。
4. 保持原游戏方法正常执行。

快照包含：有效标志、`std::chrono::steady_clock` 点燃时刻、玩家名拥有型字符串和点燃时的本局时间秒数。Hook 线程写入，Present/ImGui 线程按值读取，不暴露内部字符串指针或容器。

### 圣木窗口与专属设置

圣木模块独立绘制无标题、无背景、自动尺寸窗口，并独立处理其菜单控件：

- 启用和固定。
- 窗口位置。
- 字体大小，默认 26 px。
- 无记录颜色，默认白色。
- 有记录颜色，默认黄色。
- 玩家颜色，默认淡红色。
- 记录时间颜色，默认白色。

未固定时允许拖动并更新位置；固定时使用 `ImGuiWindowFlags_NoMove` 且每帧恢复保存位置。位置通过 `timer_logic` 限制在客户区。设置发生变化时只调用 `settings_store::MarkDirty()`，不自行执行文件 I/O。

## 游戏时间模块

`game_time.cpp/.h` 独立负责 `LevelStats`：

1. 通过 `il2cpp_runtime` 查找 `LevelStats`。
2. 解析并验证当前版本可用的单例访问方法。
3. 获取并缓存当前有效实例。
4. 使用当前 `dump.cs` 确认的字段偏移，经公共安全读取接口读取时间。
5. 每次读取验证实例和值；实例失效后清除缓存并重新解析。

查询返回包含有效标志与秒数的只读快照。仅接受有限、非负且处于合理范围内的值；失败返回无效快照，UI 显示 `00:00`。读取失败不影响 Overlay 或圣木 Hook。

游戏时间模块独立绘制 `游戏时间: MM:SS` 窗口，并独立处理启用、固定、位置、字号（默认 26 px）和字体颜色（默认白色）。拖动、固定、位置限制和脏标记规则与圣木窗口一致，但两组设置和窗口状态完全分离。

## 设置持久化公共层

`settings_store.cpp/.h` 管理版本化设置快照，包含两个功能各自的启用状态、固定状态、窗口坐标、字号和 RGBA 颜色。设置 DTO 属于公共持久化契约；两个功能各自只读取和修改自己的字段。

配置写入当前用户的本地应用数据目录，不写入游戏安装目录。使用带魔数、版本号和结构长度的紧凑二进制格式：

1. 加载时验证魔数、版本和结构长度。
2. 配置缺失或无效时恢复本文定义的全部默认值。
3. 加载后通过 `timer_logic` 规范化字号、颜色和位置。
4. 变化后标记为脏，静止一秒后合并保存，避免拖动窗口时逐帧写磁盘。
5. 先写临时文件，再使用 Windows 原子替换更新正式文件。
6. Overlay 关闭时刷新尚未保存的设置。

本次不引入加密；配置仅包含非敏感 UI 设置。

## 渲染与菜单调度

`overlay.cpp/.h` 只负责 ImGui/D3D 资源与顶层调度：

```text
D3D11 Present
  → overlay 开始 ImGui 帧
  → smudge_timer::DrawWindow()
  → game_time::DrawWindow()
  → overlay 绘制菜单总入口
       → smudge_timer::DrawSettings()
       → game_time::DrawSettings()
  → settings_store::Tick()
  → overlay 提交 ImGui DrawData
```

两个窗口不依赖总菜单是否打开；关闭菜单后仍继续显示。菜单只负责选择功能页和调用对应模块的设置绘制函数，不保存任何功能专属状态，也不实现任何功能控件细节。

字体由 Overlay 的 ImGui 生命周期统一加载中文字体资源；功能模块根据各自字号选择可用字体。字体资源所有权仍属于 Overlay，功能模块不创建或销毁 ImGui 上下文。

## 生命周期与失败处理

- `GameAssembly.dll` 尚未就绪：公共运行时初始化可重试；Overlay 保持可用并显示默认时间。
- 目标类或方法未找到：对应功能不安装 Hook 或调用未知入口。
- ABI 未通过运行时验证：圣木 Hook 保持禁用并保留诊断信息，不进行猜测性调用。
- 玩家名读取失败：记录仍有效，玩家显示 `--`。
- 本局时间读取失败：当前游戏时间和记录时间显示 `00:00`。
- 游戏对象失效：游戏时间模块清除实例缓存并在后续查询重新解析。
- Overlay 卸载：先停止圣木工作线程并恢复 Hook，再关闭两个功能和公共运行时，随后刷新设置并释放 ImGui/D3D 资源。

## 验证策略

### 纯逻辑 TDD

`timer_logic_tests.cpp` 必须先于实现扩展并经历红—绿过程，覆盖：

- `00:00`、分钟进位和超过一小时继续累计分钟。
- 非有限数、负数、超出合理范围和单调时钟回退。
- 四边越界和窗口大于客户区。
- 字号、RGBA 颜色和配置头的边界与无效输入。

### 静态与构建验证

- `timer_logic_tests` 在 Debug 和 Release 下均通过 CTest。
- 项目以 x64 Debug 和 Release 配置全量构建成功。
- 编译器警告和 IDE 诊断无本次改动新增错误。
- `smudge_timer` 与 `game_time` 互不包含、互不链接调用。
- `overlay.cpp` 不含 WhiteSage、LevelStats、安全读取、配置 I/O 或功能窗口实现。

### 运行时验证

1. 注入后两个组件按默认样式显示。
2. 两个组件可分别拖动；固定后无法拖动。
3. 重启游戏并重新注入后，位置、固定状态、字号和颜色恢复。
4. 未点燃圣木时显示默认值和无记录颜色。
5. 本地玩家点燃圣木后，主计时归零并递增，颜色变为黄色。
6. 其他玩家点燃圣木后，显示对应玩家名。
7. 记录时间等于事件发生时的本局时间。
8. 新点燃事件替换旧记录并重新计时。
9. 进入新调查后游戏时间显示当前调查统计时间，不延续上一局。
10. ABI 验证失败时不安装 Hook，Overlay 与游戏保持稳定。
11. 退出或卸载时恢复 Hook，原游戏方法继续正常执行，无崩溃或卡死。

## 非目标

本次不实现：

- 圣木实际影响鬼后才计时。
- 十字架计时器。
- 多条圣木历史记录列表。
- 网络事件 ID 或物品实例 ID 去重。
- Hook 框架的全面重写。
- 复制参考项目的代码、偏移、存储格式或界面实现。
