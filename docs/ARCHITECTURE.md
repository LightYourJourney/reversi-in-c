# 架构设计

本文档描述 Reversi 的模块边界、依赖方向、数据流与工程约定。
它与仓库中的实际代码同步维护；任何架构调整都应先更新本文档再落地。

## 1. 目标与约束

- 核心实现使用 **ISO C11**，不依赖编译器扩展。
- 游戏规则、AI、持久化均不得依赖 GUI 或图形上下文。
- AI 不得直接修改 UI 状态或绕过规则校验。
- 唯一第三方图形库为 raylib；其余能力使用 C 标准库。后台线程使用平台线程
  API（POSIX pthread / Win32 CreateThread），不引入额外第三方依赖。
- 模块边界清晰，避免巨型源文件、全局可变状态和循环依赖。

## 2. 分层与依赖方向

```text
                              main
                                │
                               app
      ┌────────┬────────┬───────┼────────┬──────────┐
      ▼        ▼        ▼       ▼        ▼          ▼
     ui       ai     storage  settings  audio      core
      │        │        │        │        │      (board/rules/game/
      ▼        │        ▼        ▼        ▼       history/match)
   raylib      │       core    storage  raylib
               └────────┴────────┴────────┴──────────┘
                 （只依赖 core 的只读接口或数据结构）
```

强制规则：

1. **core 不依赖任何上层模块**，也不包含 raylib、文件 I/O 或 UI 代码。
2. **ui 不修改对局状态**：只接收只读视图与输入事件，动画状态属于 UI 自身。
3. **ai 不包含 ui**，只读取对局快照并返回一个候选落子。
4. **storage / settings 不包含 ui**，只与数据模型和 C/平台文件 API 打交道；
   `settings` 通过 `storage` 的原子写入落盘。
5. **audio 不包含 ui 或对局逻辑**：只接受枚举化的音效提示；没有音频设备时
   全部降级为空操作。
6. **app 是唯一的组装根（composition root）**：负责创建、连接、销毁所有模块。
7. 依赖关系只允许沿上图向下，不允许反向引用或跨层回调环。
8. `ui` 只在编译期引用 `settings` 与 `storage/stats` 的数据结构（不链接持久化
   库），用于绘制设置值与统计表；文件读写仍由 `app` 完成。
9. **对局状态只有一条写路径**：`match_*` 是唯一会改动 `Match`（含棋盘）的
   接口；UI 层只拿到 `const Match *`，无法直接写棋盘。

## 3. 模块职责

| 模块 | 计划路径 | 当前状态 | 职责 | 允许依赖 |
|---|---|---|---|---|
| Board | `src/core/board.[ch]` | Phase 0/1 已实现 | 8×8 棋盘、坐标/记谱映射、安全读写、初始局面、棋子计数 | C 标准库 |
| Bitboard | `src/core/bitboard.[ch]` | Phase 11 已实现 | 64 位棋盘、位运算合法着法/翻转、计数、Pass/终局 | Board |
| Rules | `src/core/rules.[ch]` | Phase 1 已实现 | 单步合法性、八方向翻转、合法着法生成/计数、必须跳过查询、无状态落子 | Board |
| Game | `src/core/game.[ch]` | Phase 1 已实现 | 当前玩家、落子应用、自动跳过、主动跳过限制、终局、结果、一致性检查 | Board、Rules |
| History | `src/core/history.[ch]` | Phase 3 已实现 | 局面快照栈、撤销/重做游标、重做分支截断、跳过事件记录 | Game |
| Match | `src/core/match.[ch]` | Phase 3 已实现 | 对局状态机、模式与 AI 边界、落子/暂停/继续/撤销/重做/重开、阶段一致性检查 | Game、History、Rules |
| AI | `src/ai/` | Phase 4/11 已实现 | 局面评估、Bitboard Negamax/Alpha-Beta、增量 Zobrist、难度配置、精确残局求解、后台搜索工作者 | Board、Bitboard、Rules、Game（只读）、Match 的 `AiController` 边界类型、C 标准库、POSIX/Windows 线程 |
| Endgame | `src/ai/ai_endgame.[ch]` | Phase 11 已实现 | 全宽度残局求解、EXACT/INCOMPLETE 分类、预算与取消 | Bitboard、GameState、AiStats、AI 配置、C 标准库、平台时钟头 |
| UI | `src/ui/` | Phase 2/3/4/5 已实现 | 运行时主题、绘制基础、字体、菜单/新游戏/对局/暂停/历史/设置/结算/帮助/统计/存档视图、动画、模态 | raylib、core 只读类型、settings/stats 的数据结构 |
| App | `src/app/` | Phase 2/3/4/5 已实现 | 页面路由、转场、模态编排、输入路由、自动跳过与终局计时、AI 回合生命周期、存档与统计编排 | 所有下层模块 |
| Storage | `src/storage/` | Phase 5 已实现 | 用户数据目录、原子文件读写、版本化存档、严格回放校验、统计持久化 | Game、History、Match、C 标准库、平台文件 API |
| Settings | `src/settings/` | Phase 5 已实现 | 用户偏好默认值、严格解析、原子保存 | C 标准库 / Storage |
| Audio | `src/audio/` | Phase 5 已实现 | 程序化音效合成、设备初始化、音量/开关、无设备降级 | raylib、C 标准库 |
| Web platform | `src/platform/web/` | Phase 10 已实现 | 浏览器尺寸、`localStorage`、AI Worker 适配 | Emscripten、`storage.h`、`ai_worker.h` |
| Utils | `src/utils/` | 按需 | 无业务含义的纯工具函数 | C 标准库 |

## 4. 数据流

### 4.1 一次人类落子

```text
raylib 输入事件
      │
      ▼
app：read_input() 汇总为 UiInput（含快捷键与滚轮）
      │
      ▼
ui：棋盘命中测试 / 键盘光标 / 按钮状态 → GameViewAction
      │
      ▼
app：调用 match_apply_move（内部走 rules 校验与翻转，并写入 History）
      │
      ▼
app：比较落子前后棋盘，生成 UiMoveAnimation
      │
      ▼
ui：从只读 Match 与动画进度重新绘制
```

`match_apply_move()` 成功后阶段变为 `ANIMATING`；动画结束由 `app` 调用
`match_finish_animation()` 落到 `PLAYER_TURN` / `PASSING_TURN` / `GAME_OVER`。
撤销/重做同样只走 `match_undo()` / `match_redo()`，UI 不自行改动棋盘。

### 4.2 对局状态机

对局逻辑状态是 `MatchPhase`，与“当前显示哪个页面”（`AppScreen`）完全分离：
同一页面可以处于不同阶段（对局页在暂停时仍是对局页），同一阶段也可以对应
不同页面（历史页在暂停中查看）。

```text
        match_apply_move 成功
PLAYER_TURN ────────────────> ANIMATING
     ▲                            │
     │  match_finish_animation    ├── 终局 ────────> GAME_OVER
     │  match_finish_pass         ├── 对手必须跳过 ─> PASSING_TURN ─┐
     │                            └── 否则 ────────> PLAYER_TURN   │
     │                                                             │
     └─────────────────────────────────────────────────────────────┘

PLAYER_TURN ──match_pause──> PAUSED ──match_resume──> PLAYER_TURN
PLAYER_TURN ──轮到 AI──> AI_THINKING ──落子──> ANIMATING
AI_THINKING ──match_cancel_ai_turn──> PLAYER_TURN
GAME_OVER   ──match_undo / match_redo──> PLAYER_TURN / GAME_OVER
```

- 每个转换都在 `match.c` 内先校验来源阶段，非法转换返回 `false` 且不改状态；
  `match_is_consistent()` 会重复检查这些前提，供测试与调试使用。
- `PASSING_TURN` 是**展示用**的中间态：规则层的自动跳过已经发生，`Match`
  只是把“对手无子可下”暴露成一个可被 UI 显示、可被撤销的阶段。
- 暂停只允许从 `PLAYER_TURN` 进入，继续时回到 `PLAYER_TURN`；没有额外的
  来源阶段字段需要维护。
- `AI_THINKING` 只可能出现在人机模式、轮到电脑且棋盘仍有合法着法时。进入
  该阶段后棋盘与暂停被禁用，但撤销、重开、返回菜单仍可取消搜索。
- 撤销只允许在 `PLAYER_TURN`、`AI_THINKING` 与 `GAME_OVER` 进行；重做只允许
  在 `PLAYER_TURN` 与 `GAME_OVER`。恢复目标由快照本身决定，不需要 UI 参与；
  人机模式会继续回退到玩家上一个决策点。

### 4.3 UI 状态与动画

UI 的瞬时状态（悬停、光标位置、按钮按压、动画进度、页面转场、模态）
全部由 `app` 或视图结构体持有：

- `UiMoveAnimation` 只保存落子格与翻转格的渲染进度，来源是落子前后的
  棋盘快照；它不写回 `Match`。
- 动画进行时 `app` 不向对局页传递可接受输入，落子输入被暂时拒绝。
- 设置开关只影响绘制与动画启动，不改变规则判定。
- 页面转场期间 `app_update` 只推进被动布局，不读取操作输入。
- 模态打开时输入只交给模态，背景页面不更新，因此背景按钮与棋盘无法被操作。
- `PASSING_TURN` 与终局延迟由 `app` 计时，`Match` 不含任何计时器。

### 4.4 AI 落子

```text
app ──只读状态快照 + AiConfig──> ai_worker（后台线程）
app <──候选落子/AiStats──（轮询，不阻塞事件循环）
  │
  ▼
与人类落子完全相同的校验与应用路径（match/game/rules/history）
```

AI 返回的落子即使非法，也会被规则层拒绝；AI 不拥有跳过校验的特权。
`ai_worker` 只复制 `GameState` 与 `AiConfig`，不持有 `Match`、UI 或视图指针，
因此后台线程无法写入对局或界面状态。

`MatchConfig.ai` 是该边界的占位：`Match` 只保存指向 `AiController` 的指针，
不调用它、也不拥有它。Phase 4 中 `app` 在轮到电脑时：

1. 调用 `match_begin_ai_turn()` 进入 `AI_THINKING`；
2. 把状态快照与难度配置交给 `AiWorker`；
3. 每帧轮询；搜索完成后再调用 `match_apply_move()` 校验并应用；
4. 撤销、重开、返回菜单、退出或新对局时先取消并回收工作者，再改状态。

`AiWorker` 的取消标志与完成标志使用 C11 原子变量；工作者在搜索中每 1024 个
节点检查一次取消。线程后端在 POSIX 上使用 `pthread`，Windows 上使用
`CreateThread`，不依赖 C11 `<threads.h>`。即使线程创建失败，`app` 仍会同步
调用 `ai_choose_move()`，再退化为“第一个合法着法”，保证对局继续。

人机模式下 `match_undo()` 会一次回退到玩家上一个决策点，使“撤销”始终对应
玩家自己的一步；在 `AI_THINKING` 中撤销会先取消搜索。

### 4.5 AI 评估与搜索

- **搜索**：Negamax + Alpha-Beta；无合法着法的节点显式切换到对手（pass
  node），双方都无着法时返回精确终局分。搜索不调用 `game_apply_move()` 的
  自动跳过，而是自己控制回合，保证负极大极小的视角一致。
- **迭代加深**：从深度 1 开始逐层加深，保留上一层最佳着法用于排序；时间预算
  用 `timespec_get()`（UTC 毫秒）检查，每 1024 个节点检查一次取消回调。
- **置换表（Hard）**：Zobrist 哈希（棋盘 + 行棋方），`1<<16` 项直接映射；
  保存精确/下界/上界标志与最佳着法，用于剪枝与着法排序。
- **着法排序**：置换表最佳着法优先，其次角点，再按静态位置分排序。
- **终局**：Hard 在空格 ≤12 时把搜索深度扩展到剩余空格数；时间不足时保留
  最后一个完整深度的结果。
- **评估**（始终从行棋方视角，own - opponent）：角点、机动性、潜在机动性、
  位置表、边稳定子、前沿子、棋子数、奇偶。位置表不含角点（角点单独计分），
  边缘位置分与稳定子分别描述“即时形状”和“持久归属”，不是同一语义的重复；
  前沿子不计角点。Easy/Medium 的总分只用基础分量（角点、机动性、潜在机动性、
  位置、棋子数），Hard 额外计入边稳定子、前沿子与奇偶；所有分量字段始终
  填充，便于解释与测试。各分量权重随开局/中局/残局切换。
- **置换表边界**：TT 命中收紧 `alpha/beta` 后，节点结果的上/下界标签以收紧后
  的实际搜索窗口为准，避免把 fail-low 误存为精确分；测试在固定深度下比较
  开/关 TT 的分数是否一致。
- **着法排序**：先按静态位置分排序，再把置换表/上一层最佳着法提升到首位，
  确保它真正先被搜索。

| 难度 | 算法 | 深度/预算 | 评估 | 随机性 |
|---|---|---|---|---|
| Easy | 单层静态评估 | 深度 1 / 30 ms | 基础分量 | ±40 分，仍只返回合法着法 |
| Medium | Alpha-Beta + 基础评估 | 深度 4 / 200 ms | 基础分量 | 无 |
| Hard | 迭代加深 + 置换表 + 完整评估 | 深度 10 / 500 ms，残局扩展 | 全部分量 | 无 |

### 4.6 存档与读档

```text
app（唯一有权读取 Match 的层）
  │  当前历史分支的着法 + 模式/颜色/难度
  ▼
storage/save：文本序列化 + FNV-1a 校验和 + 临时文件原子重命名
  │
  ▼
用户数据目录/autosave.rvs 或 saveN.rvs
  ▲
  │  save_read：版本/校验和/行格式/着法范围检查
  │  game_apply_move 回放 + 最终棋盘/回合/结果比对
app ──> match_restore_checked（以 Match API 回放并比对期望局面）
```

- 存档版本为 1；文件头为 `REVERSI_SAVE 1`，随后是 32 位 FNV-1a 校验和。
- `save_read` 不依赖 `Match` 或 AI：它用 `GameState` 回放着法并严格拒绝
  非法着法、越界坐标、棋盘/回合/结果不一致、截断、错误版本与错误校验和。
- `app` 拿到校验后的着法序列后，用当前难度的 `AiController` 调用
  `match_restore_checked()` 重建完整 `Match`（含历史与阶段）并直接比对期望
  局面；失败时目标 `Match` 保持不变，只有完全一致才进入对局页。
- 自动保存只在应用层编排：每次落子/撤销/重做/重开后写自动存档；终局动画
  期间仍保留该存档，进入 `GAME_OVER` 后才删除，避免动画中退出或崩溃丢失对局。
  手动存档可选择 3 个槽位，读取页支持删除（带确认）。
- 写入使用同目录临时文件 + `fsync`/`_commit` + 原子替换；进程在写入中途退出
  不会留下半个正式文件。读取前检查文件大小与 NUL 字节，拒绝异常输入。
- `settings` 与 `stats` 使用相同的原子写入路径；损坏/缺失时回到默认值并保证
  程序继续运行。

## 5. 状态与所有权

- `Match`、`History`、`GameStats`、`Settings` 的实例由 `app` 拥有，
  生命周期与应用一致。
- `AiWorker` 由 `app` 创建并在退出时销毁；其任务对象在搜索结束时回收，
  不跨对局存活。AI 搜索只拥有状态与配置副本。
- `MenuView`、`SetupView`、`GameView`、`PauseView`、`HistoryView`、
  `SettingsView`、`ResultView`、`HelpView`、`StatsView`、`SavesView`、
  `Modal`、`UiMoveAnimation` 与 `UiFonts` 同样由 `app` 拥有；设置由
  `settings` 模块负责磁盘往返。
- 下层模块通过指针或 `const` 视图接收状态，不持有隐藏的全局单例。
- `ui` 仅保有自己的瞬时状态（动画进度、悬停格、光标、布局计算结果、
  历史滚动位置）；历史列表只读 `const Match *`，不持有对局副本。
- 字体在启动时加载一次，退出时统一卸载；绘制循环不加载资源。
- raylib 的窗口/GPU 状态是进程级的，由 `app` 在启动和退出时统一初始化与释放。
- 原则上不引入可变的文件作用域全局变量。两个有意的例外：`ui_theme` 是
  指向两张 `static const` 调色板的只读指针，用于运行时主题切换；`audio.c`
  拥有进程级音效句柄与音量状态，由 `app` 通过 `audio_*` 接口初始化/释放。

## 6. 错误处理

- **非法输入**（越界坐标、空指针、非法棋子值）由公开 API 检查并返回安全值；
  不依赖 `assert()` 处理用户输入，也不造成越界访问。
- **可恢复错误**（非法落子、文件不存在、格式损坏）由函数返回状态或布尔值：
  `rules_apply_move()` 返回 `-1` 且不修改棋盘，`board_set()` 返回 `false`，
  `game_apply_move()` 返回 `false` 且不修改状态；存档返回 `SaveStatus`，
  设置/统计读写返回 `bool`，损坏数据不会产生部分写入或非法对局。
- **状态不变量**由 `game_is_consistent()` 检查，主要用于测试与调试；
  它不修改状态，也不作为正常游戏流程的必需步骤。
- **资源错误**（窗口初始化失败、内存分配失败）在 `app` 边界统一报告并退出。
- 库代码不调用 `exit()`，不直接向终端打印用户界面文本。

## 7. 资源路径

- 构建完成后，CMake 将 `assets/` 复制到可执行文件所在目录。
- 运行时资源路径以**可执行文件目录**为基准（`GetApplicationDirectory()`），
  而不是源码目录或当前工作目录。
- 当前资源为 `assets/fonts/Inter-Regular.ttf` 与 `Inter-SemiBold.ttf`；
  `ui_fonts_load()` 按显示器 DPI 加载 32/64 像素字体图集。
- 字体缺失或加载失败时记录警告并回退到 `GetFontDefault()`，界面仍可操作。
- 测试和程序都不依赖调用者从哪个目录启动。
- 规则引擎不涉及任何资源文件。
- 用户数据目录由 `storage_user_dir()` 按平台解析：macOS
  `~/Library/Application Support/Reversi`，Linux
  `$XDG_DATA_HOME/reversi` 或 `~/.local/share/reversi`，Windows
  `%APPDATA%\Reversi`；程序安装目录只读，不写入用户数据。
- 音效没有外部资源文件，全部在运行时合成；字体是唯一随程序分发的外部资源。

## 8. 测试策略

| 层级 | 内容 | 是否依赖 GUI |
|---|---|---|
| 单元测试 | Board、Rules、Game、History、Match、AI、Storage 的纯逻辑 | 否 |
| 规则回归 | 开局着法、八方向/多方向翻转、间隙、边界、非法落子 | 否 |
| 自动跳过/终局 | 当前玩家无着法、双方无着法、棋盘填满、主动跳过限制 | 否 |
| 状态机 | 非法转换被拒绝、暂停/继续回到来源阶段、终局与满盘/非满盘两种结束方式 | 否 |
| 撤销/重做 | 快照恢复一致、新落子截断重做分支、终局撤销、跳过事件恢复 | 否 |
| 随机对局 | 500 局确定性随机对局：每步合法、占用数一致、有限步结束 | 否 |
| AI 合法性 | 所有难度在大量局面与完整人机对局中只能返回合法着法 | 否 |
| AI 搜索 | 配置校验、评估分量、pass 节点、终局分、取消、确定性、后台工作者快照 | 否 |
| 存档往返 | 保存后读取得到等价状态；缺失/截断/错误版本/错误校验和/非法着法/棋盘或回合不匹配均被拒绝 | 否 |
| 设置/统计 | 默认值、往返、损坏文件回退、未知键向前兼容、PVE/PVP 计数与汇总校验 | 否 |
| 音频降级 | 未初始化设备时所有音效调用安全返回 | 否 |
| UI 按钮 | 合成 `UiInput` 验证悬停、按下、释放、移出取消、禁用与输入阻断 | 否（链接 raylib，不建窗口） |
| UI 纯逻辑 | 缓动/颜色、棋盘命中、合法格判断、各视图动作映射、动画插值 | 否（链接 raylib，不建窗口） |
| 冒烟测试 | `reversi --frames N` 验证窗口初始化、渲染、关闭 | 是（需显示环境） |

CTest 默认注册无 GUI 测试；GUI 冒烟测试通过
`-DREVERSI_ENABLE_GUI_TESTS=ON` 注册为 `app.smoke`（Debug 另有
`app.autoplay`），避免在无显示环境中失败。
第一方目标的编译警告视为缺陷。新增**纯逻辑**公开函数必须同时新增单元测试
（`ui.button`、`ui.helpers`、`ui.views` 覆盖按钮、缓动、颜色、动画插值与
各页面动作映射）；依赖窗口尺寸/绘制上下文的视图函数由 `app.smoke` 与人工
截图验证覆盖，不以伪单元测试替代。

## 9. 目录决策

| 决策 | 理由 |
|---|---|
| 不创建 `include/` | 头文件与实现同目录，通过 `src/` 作为包含根；避免重复的公共/私有头文件树 |
| 暂不创建 `cmake/` | 当前单个 `CMakeLists.txt` 足够清晰；出现可复用模块时再拆分 |
| `storage/` 与 `settings/` 分目录 | 文件/格式/统计与用户偏好各自独立；settings 复用 storage 的原子写入 |
| `tests/` 独立于 `src/` | core 测试只链接 `reversi_core`，保证规则层与 GUI 解耦；`ui.button` 单独链接 `reversi_ui` 但不创建窗口 |
| `assets/` 在构建后复制 | 运行时与源码目录、当前工作目录彻底解耦 |

## 10. 编码约定

- 语言标准：C11，`CMAKE_C_EXTENSIONS OFF`。
- 函数：`snake_case`；类型：`PascalCase`；宏：`REVERSI_` 前缀。
- 每个头文件自包含，使用 include guard，不依赖包含顺序。
- 仅在本翻译单元使用的函数与变量声明为 `static`。
- 无参数函数写 `(void)`；公开函数必须在头文件中声明。
- 源码注释使用英文；面向使用者的文档使用中文。
- 第一方代码启用：
  `-Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes`
  （MSVC 使用 `/W4`）。

## 11. Phase 0 关键决策

1. **raylib 固定为 6.2**：优先系统包，缺失时通过 `FetchContent` 下载并用
   SHA256 校验，保证可复现。
2. **CMake 最低版本 3.24**：使用 `DOWNLOAD_EXTRACT_TIMESTAMP` 避免解压时间戳
   相关的策略警告。
3. **Sanitizer 只作用于第一方目标**：raylib 在 Sanitizer 选项生效前已完成配置，
   避免对第三方源码重复插桩。
4. **资源随可执行文件分发**：构建后复制 `assets/`，从第一天起消除工作目录依赖。
5. **`--frames` 冒烟入口**：让“窗口能启动并干净关闭”成为可自动验证的事实，
   而不是手工观察。
6. **Board 先于 Rules 落地**：只实现确定需要且可独立测试的数据结构，不提前
   编写规则或 AI 的伪实现。

## 12. Phase 1 关键决策

1. **坐标与记谱统一**：行 0 为顶部、列 0 为左侧；`row = '8' - rank`、
   `col = file - 'a'`。Phase 0 的初始局面在“行 0 顶部”约定下颜色放反，
   Phase 1 已按 D4/E5 白、D5/E4 黑修正，并新增记谱回归测试。
2. **Board 访问安全化**：`board_get()` 对非法坐标返回 `EMPTY`，
   `board_set()` 返回 `false` 且不修改棋盘；规则层仍显式校验边界与空位。
3. **Rules 无状态、Game 有状态**：`rules_*` 只操作传入的 `Board` 或只读查询；
   `GameState` 由上层拥有，负责回合、自动跳过与终局。
4. **自动跳过在 `game_apply_move()` 内完成**：对手无合法着法而落子方仍有，
   则当前玩家保持为落子方；双方都无合法着法时立即结算终局。
5. **显式 `game_pass()` 只在必须跳过时成功**：当前玩家有合法着法时返回
   `false` 并保持状态不变；该函数用于验证规则与处理外部构造的必须跳过状态。
6. **一致性定义**：进行中的对局至少一方有合法着法（允许“当前方必须跳过”
   的中间态）；终局必须无合法着法且结果与最终棋子数一致。
7. **返回约定**：查询函数不修改状态；`rules_apply_move()` 返回翻转数或 `-1`；
   `game_apply_move()`/`game_pass()` 返回布尔值，失败时不改变状态。

## 13. Phase 2 关键决策

1. **按页面拆分 UI**：`menu_view`、`game_view`、`settings_view`、
   `result_view`、`modal` 各自拥有布局与局部状态；`draw` 提供共享绘制原语，
   `theme.h` 是共享颜色、字号、间距、圆角与动画时长的唯一来源；各页面内部的
   布局几何尺寸保留为局部常量。
2. **app 仍是唯一组装根**：页面不直接修改 `GameState`，而是返回动作
   （`GAME_VIEW_MOVE`、`SETTINGS_ACTION_*` 等），由 `app` 调用规则层并处理转场。
3. **动画与规则状态分离**：落子前保存棋盘快照，落子后比较差异生成
   `UiMoveAnimation`；动画只读取快照并在渲染层插值，绝不写回棋盘。
4. **动画期间阻断全部操作输入**：落子动画激活时 `accept_input` 与
   `board_accept` 同时为 `false`，Restart/Main menu 以禁用样式显示，R/M/Esc
   也不响应；模态淡入与结算出现动画期间同样延迟接受操作。
5. **宽/窄双布局**：宽度 ≥1000 且高度 ≥680 时使用棋盘 + 侧栏；否则使用
   顶部回合/计分栏、居中棋盘与底部状态/操作栏。最小窗口 760×600。
6. **模态与转场统一门控**：模态打开时只更新模态；页面转场期间只推进被动
   布局，不读取操作输入，从而保证背景页面不可误触。Animations 关闭时页面
   直接切换、模态与结算不淡入、按钮与开关不缓动，最后一步只显示静态环。
7. **HiDPI 字体一次加载**：按 `GetWindowScaleDPI()` 以 32/64 像素加载
   Inter 图集，使用双线性过滤；缺失时回退默认字体并记录警告。
8. **程序化视觉资源**：棋盘、棋子、阴影、提示点、按钮全部由绘制生成，
   运行时不加载贴图；仅字体是运行时外部资源。应用图标（macOS `.icns` /
   Windows `.ico`）只作为打包输入，运行时不读取。
9. **设置暂存内存**：本阶段只提供三项显示/动画开关；Animations 统一控制
   落子/翻转、页面转场、模态与结算淡入、按钮/开关缓动；Last move highlight
   控制高亮与脉冲。持久化属于 Phase 5，避免在 UI 阶段提前引入存储格式。

## 14. Phase 3 关键决策

1. **页面状态与对局状态分离**：`AppScreen` 管页面，`MatchPhase` 管对局逻辑；
   两者不互相推导。对局页在 `PAUSED` 时仍是同一个页面，历史页在暂停中查看
   也不会改变 `MatchPhase`。
2. **`Match` 是唯一写入口**：`match_apply_move()` 内部完成规则校验、翻转、
   历史记录与阶段推进；撤销/重做/暂停/重开同样只有 `match_*` 接口。UI 只拿
   `const Match *`，从类型上排除了“UI 直接改棋盘”。
3. **历史用快照而非增量**：`HistoryEntry` 保存完整 `GameState` 加落子信息，
   撤销/重做只需替换状态并重算阶段，避免为“反向翻转”维护第二套规则实现。
   容量上限 65 项（64 步 + 起始局面），超过 64 步的对局停止记录新历史
   （`match_apply_move` 会因此返回 `false`），不会出现半条记录。
4. **重做分支显式截断**：撤销后再次落子会把重做项直接覆盖，语义与常见编辑器
   一致，并由 `core.history` / `core.match` 两层测试固定。
5. **自动跳过只做展示**：规则层的自动跳过行为保持不变；`Match` 额外暴露
   `PASSING_TURN` 与 `last_pass`，让 UI 能提示“对手无子可下”，并允许从该
   阶段撤销。终局（双方都无子可下）不会被记成跳过。
6. **AI 边界先于 AI 实现**：`AiController` 只是函数指针占位，`Match` 不调用
   它；`App` 持有 `const AiController *ai` 并在启动人机对局时写入
   `MatchConfig`，新游戏页只在控制器非空时允许 PvE 开始。`MatchConfig` 校验
   要求人机模式必须带合法控制器，因此 Phase 4 不会出现“模式已存在但没有
   对手”的中间状态。人机撤销语义（一次回退到玩家上一个决策点）在本阶段就
   实现并测试，避免 Phase 4 再改撤销代码。
7. **UI 不伪造不可用能力**：新游戏页的 Computer 与颜色按钮在 AI 缺失时禁用
   并说明原因，而不是提供一个点了没有反应的选项。
8. **计时留在 app**：跳过提示时长与终局延迟是展示参数，放在 `app` 而不是
   `Match`，使核心层保持无计时器、可确定性测试。

## 15. Phase 4 关键决策

1. **显式 `AI_THINKING` 阶段**：AI 回合不再借用 `PLAYER_TURN` 加布尔标志，
   而是成为状态机的一等阶段。这样输入门控、暂停/撤销/重开的前置条件、
   一致性检查都能集中表达；AI 落子由 `match_apply_move()` 在
   `AI_THINKING` 中接受，人机模式之外该阶段不可进入。
2. **搜索自己处理 pass**：Negamax 不调用 `game_apply_move()` 的自动跳过，
   而是显式把行棋方切换给对手。若依赖自动跳过，pass 会被规则层静默吞掉，
   负极大极小的视角与深度计数都会出错。
3. **时间预算与取消检查分离**：时间用 UTC 毫秒计算，取消通过回调检查；
   两者都在固定的节点间隔触发，避免每节点查询时钟。根节点每一步都检查，
   保证预算耗尽时不会把半层结果当成完整结果。
4. **置换表只给 Hard**：Zobrist 表约 2 MiB，Easy/Medium 的收益不足以抵消
   分配与复杂度；Hard 的迭代加深可从置换表获得排序与剪枝收益。命中后收紧
   `alpha/beta` 时，节点结果的边界标签以收紧后的窗口为准；测试在固定深度下
   比较开/关置换表的分数。
5. **评估分量显式暴露**：`AiEvaluation` 保存每个分量的原值（行棋方视角），
   测试与基准可以直接检查分量而不是只看总分；位置表排除角点，避免与角点
   权重重复计算。Easy/Medium 的总分只使用基础分量，Hard 额外使用稳定子、
   前沿子与奇偶，使“更完整的评估”落在代码而不是标签上。
6. **后台工作者只持有副本**：`AiWorker` 复制 `GameState` 与 `AiConfig`，
   搜索线程不接触 `Match`、`App` 或 UI。取消/完成用 C11 原子变量同步，
   结果由主线程取回后仍走 `match_apply_move()`。
7. **失败路径必须仍能落子**：线程创建失败时同步搜索；搜索失败时选择第一个
   合法着法；任何情况下都不会把非法着法交给规则层或让 AI 回合卡死。
8. **难度差异来自参数而非标签**：Easy 单层 + 随机扰动、Medium 固定深度
   Alpha-Beta + 基础评估、Hard 迭代加深 + 置换表 + 完整评估 + 残局扩展；
   基准对局使用固定深度与种子以获得可复现结果，实际时间预算由局面探针单独
   记录；非法/失败/未完成对局会使基准返回非零。

## 16. Phase 5 关键决策

1. **存档基于着法回放**：文件保存模式、颜色、难度、当前历史的着法序列，以及
   最终棋盘/回合/结果。读取时先用 `GameState` 严格回放校验，再由
   `match_restore_checked()` 重建 `Match` 与历史并比对期望局面；这样不需要
   暴露可写的 `Match` 内部，也不会让存档格式与 `History` 的内存布局耦合。
2. **校验和 + 原子替换**：格式带版本与 FNV-1a 校验，写入使用同目录临时文件、
   `fsync`/`_commit` 和原子重命名；读取前限制文件大小并拒绝 NUL 污染。
3. **自动存档在终局动画期间仍保留**：每次落子/撤销/重做/重开后在应用层写
   `autosave.rvs`；进入 `GAME_OVER` 后才删除。这样终局翻转动画中退出或崩溃
   仍能继续该局。“继续”只在自动存档存在且有效时启用。
4. **设置是带默认值的宽松恢复**：缺失或损坏时回到默认值并继续运行；已知键
   严格校验范围，未知键忽略以便后续版本扩展。主题、音量、音效开关立即生效。
5. **运行时主题不重写调用点**：`theme.h` 将颜色宏映射到 `ui_theme` 指针的
   字段，保留全部现有绘制代码；两张调色板为 `static const`，切换只改指针。
6. **音效程序化生成**：不引入外部音频资源；`audio_init()` 在设备不可用或
   任一音效加载失败时回滚并静默降级。落子/翻转/按钮/胜负/平局使用不同波形，
   音量与开关由设置控制。
7. **统计只记录完成局**：终局状态一产生（终局着法落地时）就记录一次并写盘，
   而不是等动画结束；这样终局动画期间退出或保存不会漏记。读入已结束的存档
   时将标记设为已记录。结算后撤销会撤回该局记录，重新结束或重做回结算只
   记录一次，不会重复计数。
8. **存档删除需要确认**：删除槽位与退出/重开/返回菜单共用模态机制，避免
   一次误点永久丢失存档。
9. **帮助内容与真实快捷键同步**：`U`/`Y` 直接撤销/重做与 Ctrl/Cmd 组合键
   一并实现；帮助页列出的 `Ctrl/Cmd+S` 对应真实保存入口。


## 17. Phase 6 审计决策

1. **完成由状态决定，不由动画决定**：`GameState.result` 进入终局时即记录
   统计并写盘；`ANIMATING`/`GAME_OVER` 只影响展示。这消除了“终局动画中
   退出/存档导致漏记”的窄路径。
2. **内部入口也要防守**：`ai_search_choose_ex()`、`ai_easy_choose()` 在进入
   搜索前校验 `AiConfig`，避免维护者绕过 `ai_choose_move()` 时把非法深度/
   预算带入搜索。
3. **不追求形式整洁的大重构**：审计确认分层、所有权与状态机健康；
   `app.c` 规模作为观察项保留，拆分留到有独立收益时进行。测试、日志与
   文档优先记录真实行为，不用“看起来完整”的报告替代验证。

## 18. Phase 7 发布工程决策

1. **版本单一来源**：根目录 `VERSION` 由 CMake 读取并校验，生成
   `reversi/version.h`；窗口标题、`--version`、Info.plist 与 Windows
   版本资源都从同一版本派生，避免多处手写。
2. **安装布局平台化**：macOS 使用 `Reversi.app`，字体与许可证进入
   `Contents/Resources/`；Windows/Linux 保持可执行文件与同级 `assets/`
   的便携布局。CMake install 只安装 `Reversi` 组件，不把 raylib/GLFW
   开发文件带入发行包。
3. **签名在资源之后**：macOS 先复制字体、`LICENSE` 与第三方声明，再做
   ad-hoc 重签名，保证 bundle 的资源封印有效；Developer ID 通过
   `REVERSI_MACOS_CODESIGN_IDENTITY` 可选注入，不提交任何私钥。
4. **发布构建锁定依赖**：CI 与 `scripts/package.sh` 强制
   `REVERSI_FORCE_FETCH_RAYLIB=ON`，使用已审计的 raylib 6.2，而不是
   构建机上的系统版本。
5. **验收脚本独立于源码**：`verify-package.sh` 校验归档校验和、解压到
   临时目录、从 `/` 连续启动、检查字体/许可证/用户数据持久化，并在
   Linux 上检查 `ldd`；CI 在 Xvfb 下复用同一脚本。
6. **发布与打包分离**：`ci.yml` 负责每次提交的矩阵验证与产物上传；
   `release.yml` 只在 `v*` 标签触发正式 GitHub Release，标签必须与
   `VERSION` 一致，避免误发布。

## 19. Phase 10 关键决策：Web 平台适配

1. **主循环分离**：`app_run` 拆成 `app_boot` / `app_frame` /
   `app_release`；桌面保留 `while (!WindowShouldClose())`，Web 使用
   `emscripten_set_main_loop_arg`。`AppRun` 在 Web 上是静态存储，桌面仍在
   栈上。
2. **存储适配层**：`storage.h` 的 7 个函数在 Web 由 `storage_web.c`
   实现，键名取代路径，`localStorage` 同步读写；`save`/`settings`/`stats`
   与严格校验完全复用，存档格式不变。
3. **AI Worker 隔离**：Web 端不使用 pthread。`ai_worker_web.c` 保持
   `ai_worker.h` 语义，通过 `postMessage` 驱动独立的 `ai_worker` 模块；
   取消 = 终止 Worker，主线程从不 join，也不执行阻塞搜索。
4. **资源与尺寸**：`--preload-file assets@/assets` 提供字体；Web 端关闭
   `FLAG_WINDOW_HIGHDPI`，由 JS 按 CSS 尺寸调用 `reversi_web_resize`，
   避免 raylib Web 把画布按 DPR 缩小后只绘制左上角。
5. **诊断接口**：Web 构建导出 `reversi_web_*` 诊断函数（帧数、屏幕/鼠标、
   游戏状态、取消、堆大小），供 Playwright 测试使用；桌面构建不编译这些
   函数。
6. **平台边界**：存储与 AI Worker 适配在 `src/platform/web/`；浏览器主循环、
   尺寸与诊断导出留在 `app.c` 的 `__EMSCRIPTEN__` 条件块中，因为它们直接
   读写 `App` 内部状态；页面与测试代码在 `web/`、`tests/web/`。
   `core/`、`ai/`、`storage/` 的序列化层未分叉。

## 20. Phase 11 关键决策：AI 引擎 2.0

1. **双表示并存**：搜索递归同时携带 `GameState`（评估、终局计分、对外
   结果）与 `Bitboard`（合法着法、落子、翻转位、增量哈希）。`Board` 仍然是
   唯一对外棋盘类型，存档、UI 与历史未改。
2. **单一状态转换点**：`apply_search_move()` 先按标量规则落子，再用
   Bitboard 落子；Bitboard 落子失败时该分支失败。两套规则等价由差分测试
   保证，运行时不逐节点比对。
3. **增量 Zobrist**：落子异或落点、翻转位与回合键；Pass 只交换回合键。
   TT 的键/深度/边界语义未改。
4. **残局求解器独立**：`ai_endgame` 全宽度搜索剩余空位，返回 EXACT 或
   INCOMPLETE；Hard 在 ≤12 空时优先尝试，并为主搜索保留预算。
5. **只合并可证明的优化**：位板着法与增量哈希有同语料 A/B 支撑；评估仍
   为标量，中局未获明显提升，PVS/Aspiration 等未实现。
6. **单调时钟**：Windows / Emscripten / POSIX 分支，其他环境回退
   `timespec_get`；预算仍按节点节奏检查。
