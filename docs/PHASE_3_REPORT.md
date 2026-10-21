# Phase 3 完成报告：游戏流程与对局状态管理

> 历史阶段报告：记录该阶段完成时的状态；最新发布状态见 README 与 [PHASE_7_REPORT.md](PHASE_7_REPORT.md)。

日期：2026-10-19
平台：macOS 26.6.2（Darwin 25.6.0，Apple M3 / arm64）

## 1. 交付范围

Phase 3 让本地双人能够从“新游戏页”开始完整走完一局，并可靠地撤销、重做、
重开与查看历史：

- **显式状态机**：`MatchPhase`（`PLAYER_TURN` / `ANIMATING` / `PASSING_TURN` /
  `PAUSED` / `GAME_OVER`）与页面状态 `AppScreen`（MainMenu / GameSetup / Game /
  History / Settings / Result）互相独立；每个转换都有前置条件，非法转换返回
  `false` 且不改变状态。
- **对局状态唯一写入口**：`match_*` 是唯一改动棋盘与阶段的接口，UI 只持有
  `const Match *`。
- **模式**：本地双人完整可用；人机模式的状态、配置校验、AI 控制器接口与
  “撤销到玩家上一个决策点”的语义均已实现，AI 搜索本身属 Phase 4。
- **操作**：新游戏、重开、暂停/继续、返回主菜单、合法提示开关、最后一步显示、
  自动跳过提示、完整终局结果、撤销、重做、历史查看。
- **历史**：局面快照栈保存完整棋盘、当前玩家、对局状态/结果、跳过事件与
  落子信息；撤销后再次落子会截断重做分支。
- **边界情况**：开局立即撤销、终局撤销、重做到终局、自动跳过后的撤销、
  动画中途退出、快速连点、对局中途重开、无合法着法的终局、切换模式后无残留。
- **界面**：新增新游戏页、暂停浮层、历史列表；对局页增加撤销/重做/暂停/
  重开/返回操作区与跳过提示；结算页支持“再来一局”与“撤销最后一步”。

## 2. 文件变更

### 新增

```text
src/core/history.[ch]      局面快照栈、撤销/重做游标、分支截断
src/core/match.[ch]        对局状态机、模式与 AI 边界、撤销/重做/暂停/重开
src/ui/setup_view.[ch]     新游戏页（模式、先手颜色）
src/ui/pause_view.[ch]     暂停浮层
src/ui/history_view.[ch]   着法历史列表
tests/test_history.c       core.history 测试（124 项断言）
tests/test_match.c         core.match 测试（653 项断言）
docs/PHASE_3_REPORT.md     本报告
```

### 修改

```text
CMakeLists.txt             core 增加 history/match，ui 增加三个视图，注册两个测试目标
src/ui/draw.h              UiInput 增加暂停/撤销/重做/历史快捷键与滚轮字段
src/ui/animation.[ch]      新增 ui_animation_finish()，供跳过动画使用
src/ui/game_view.[ch]      改为接收 const Match *；新增操作按钮、跳过提示、终局显示
src/ui/result_view.[ch]    改为接收 const Match *；新增“撤销最后一步”
src/app/app.c              新增 AppScreen（Setup/History）、Match 取代 GameState、
                           跳过与终局计时、重开/撤销的待执行转场、自动对局种子
tests/test_ui_helpers.c    改为 Match 驱动；新增撤销/重做、新游戏、暂停/历史、
                           结算四个视图的动作映射与共享 UI 辅助（65 → 128 项断言）
README.md                  状态、状态机说明、操作、测试计数、已知限制
docs/ARCHITECTURE.md       History/Match 模块、状态机小节、测试策略、Phase 3 决策
docs/ROADMAP.md            Phase 3 完成；Phase 4/5 目标调整
```

## 3. 架构决策

1. **页面状态与对局状态分离**：`AppScreen` 与 `MatchPhase` 不互相推导。
   对局页在 `PAUSED` 时仍是同一个页面；历史页在暂停中查看也不改变 `MatchPhase`。
2. **`Match` 是唯一写入口**：`match_apply_move()` 内部完成规则校验、翻转、
   历史记录与阶段推进；撤销/重做/暂停/重开同样只有 `match_*` 接口。UI 只拿
   `const Match *`，从类型上排除了“UI 直接改棋盘”。
3. **历史用快照而非增量**：`HistoryEntry` 保存完整 `GameState` 加落子信息，
   撤销/重做只需替换状态并重算阶段，避免为“反向翻转”维护第二套规则实现。
4. **重做分支显式截断**：撤销后再次落子直接覆盖重做项，与常见编辑器一致。
5. **自动跳过只做展示**：规则层的自动跳过行为不变；`Match` 额外暴露
   `PASSING_TURN` 与 `last_pass`，让 UI 能提示并能从该阶段撤销。
6. **AI 边界先于 AI 实现**：`AiController` 只是函数指针占位，`Match` 不调用它；
   `match_start(&m, &cfg)` 校验人机模式必须带合法控制器，Phase 4 不会出现
   “模式存在但没有对手”的中间状态。
7. **UI 不伪造不可用能力**：AI 缺失时新游戏页禁用 Computer 与颜色按钮并说明原因。
8. **计时留在 app**：跳过提示时长与终局延迟是展示参数，放在 `app`，使核心层
   保持无计时器、可确定性测试。

## 4. 实际执行的命令与结果

### 4.1 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 8
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_ASAN=ON -DREVERSI_ENABLE_UBSAN=ON
cmake --build build-sanitize -j 8
```

三套构建均成功；第一方源码重新编译后未出现 `warning:` 或 `error:`。
第三方 raylib 仍打印其既有的 macOS OpenGL 废弃提示。

### 4.2 测试

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build-release --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
```

| 构建 | 结果 |
|---|---|
| Debug（GUI 测试开启） | **9/9 通过**（含 `app.smoke`） |
| Release | **8/8 通过** |
| ASan+UBSan（`detect_leaks=0`） | **8/8 通过** |

Sanitizer 下额外运行窗口程序：

```bash
ASAN_OPTIONS=detect_leaks=0 ./build-sanitize/bin/reversi --frames 5   # exit 0，无报告
```

各测试二进制的断言数：

```text
core.board      623 checks
core.rules      227 checks
core.game       115 checks
core.random 557,924 checks
core.history    124 checks
core.match      653 checks
ui.button        30 checks
ui.helpers      128 checks
```

`core.history` 覆盖：快照初始化与空历史、撤销/重做完整恢复、新落子截断重做
分支、自动跳过事件与终局恢复、容量与空指针。`core.match` 覆盖：配置校验、
阶段门控与暂停、自动跳过 → `PASSING_TURN` → 撤销/重做、完整对局到终局并撤销、
重开、模式切换与人机撤销到决策点。

### 4.3 完整对局（应用层）

以 `REVERSI_AUTOPLAY=<seed>`（Debug 专用钩子，参数为随机种子）运行窗口程序，
每局都实际走完并在终局断点读取状态：

| 种子 | 步数（history.count - 1） | 结果 | 状态自检 |
|---|---|---|---|
| 1 | 60 | 白胜 | 一致 |
| 2 | 60 | 黑胜 | 一致 |
| 3 | 60 | 平局 | 一致 |
| 4 | 60 | 黑胜 | 一致 |
| 5 | 60 | 白胜 | 一致 |

5 局全部进入 `GAME_OVER`，历史 61 项、游标 60，`match_is_consistent()` 为真，
无重复落子、无错误回合、结果与最终比分一致。

## 5. 界面与交互验证

验证方式：在真实窗口中运行程序，用窗口截图核对渲染，用调试器在
`app_update()` 入口注入 `UiInput` 字段（与真实按键走同一条路由）推进交互。
所有画面都是真实渲染结果，不是静态预览。

已确认（截图）：

- **新游戏页**：模式行（Local 选中、Computer 置灰）、颜色行（Black 选中、
  两键置灰）、说明文字、Start game / Back。
- **对局页（宽布局，1100×800 @2×）**：开局四子、四处提示点、回合卡、双方比分、
  Undo/Redo 置灰、Pause/Restart/Main menu 可用（起始局面无可撤销内容）。
- **暂停浮层**：遮罩 + 面板，Resume（强调）/ History / Restart / Main menu；
  背景对局页整体变暗。
- **历史页（第 10 手）**：`10 plies | viewing position 10 of 10`，逐行显示
  `序号 + 落子方 + 记谱 + 翻转数`，滚动条与 Back。
- **终局页**：回合卡替换为 “White wins” 与胜方棋子，比分 27:37，
  状态 “Game over”、最后一步 `b7`，Undo 可用而 Pause 置灰。
- **结算页**：胜方棋子 + “White wins”、最终比分 27:37、Play again / Undo last
  move / Main menu；出现动画完成后三个按钮均可用。

已确认（输入注入）：

- 主菜单 →（Enter）新游戏页 →（Enter）对局页；
- 对局页落子 D3 → 历史 1 项；撤销 → 回到起始局面且 `has_last_move=false`；
  重做 → 回到 1 项且最后一步恢复；
- P 暂停（`PAUSED`）→ 暂停浮层选 History → 历史页 → Esc 返回对局页仍为
  `PAUSED` → Resume 回到 `PLAYER_TURN`；
- 结算页选 “Undo last move” → 回到对局页、`PLAYER_TURN`、历史 60 → 59，
  重做可用，状态自检通过；
- 结算页选 “Play again” → 历史重置为 1 项、游标 0、无最后一步；
- 暂停浮层选 Restart → 打开重开确认模态（`MODAL_PURPOSE_RESTART`）；
  再按 Enter 确认 → 回到对局页、`PLAYER_TURN`、历史重置为 1 项、模态关闭、
  状态自检通过；
- 对局中按 M 打开返回菜单确认框（`MODAL_PURPOSE_RETURN_MENU`）；确认后回到
  主菜单（`APP_SCREEN_MENU`）且模态关闭；
- 落子动画进行中按 M → 立即结束动画并打开返回菜单确认框，而不是进入暂停；
- 落子动画进行中按 Esc/P → 立即结束动画并暂停（`PAUSED`）。

未验证：

- **真实鼠标按下/释放完成一次按钮激活**：与 Phase 2 相同的限制，其语义由
  `ui.button`（30 项断言）覆盖，未覆盖窗口坐标换算。
- **人机对局**：没有 AI，无法人工验证；`App` 已接好 `const AiController *`
  注入点，模式与撤销语义由 `core.match` 断言覆盖。
- Windows 与 Linux 实机运行。

## 6. 修正的问题

在验证过程中发现并修复了三处缺陷，均补充了回归覆盖：

1. **终局被误记为“对手跳过”**（`core/match.c`）：终局且棋盘未满（双方都无子
   可下，但仍有空格）时，`last_pass` 会被置为 `occurred`，撤销后的历史条目
   也会带上错误的跳过标记。现在只有终局结果仍为 `ONGOING` 时才记录跳过事件，
   并新增 `test_terminal_without_full_board`（80 项断言）固定该行为——测试用
   局面由独立脚本搜索得到（59 手、仅剩 1 个空格的真实终局）。
2. **历史页分隔符渲染为方块**（`ui/history_view.c`）：分隔符使用了 U+00B7
   `·`，而内置 Inter 子集只加载了 95 个 ASCII 字形，导致方框字符。已改为 ASCII
   `|`。
3. **结算页按钮未标记为可用**（`ui/result_view.c`）：Play again 与 Main menu
   从未设置 `enabled`，在仅做了 `BeginDrawing()` 的调试截图里呈现为置灰。
   现在按 `buttons_accept` 显式设置，并新增结算页焦点/撤销可用性的断言。

### 6.1 代码审查后的修正

Phase 3 合并前执行了 Standards 与 Spec 两轴审查，并按发现修正：

- **删除死代码**：`match_phase_name()` 无调用者，已从 `match.h/.c` 移除。
- **补齐公开函数测试**：`ui_animation_finish()` 新增单元测试（完成即失活、
  重复调用幂等、NULL 安全）；`ui_player_name()`、`ui_result_title()`、
  `ui_focus_step()` 均有断言覆盖。
- **合并重复实现**：显示名称、终局标题、跳过禁用项的焦点推进提取为
  `ui_*` 共享函数；`match_undo()`/`match_redo()` 共用一个内部
  `travel_history()`；`app_undo()`/`app_redo()` 共用
  `app_after_history_change()`。
- **消除隐式布尔**：`start_match_pending` / `undo_to_game_pending` 合并为
  `PendingGameAction` 枚举；`app_switch_screen()` 在 `match_start` /
  `match_undo` 失败时回退到新游戏页/结算页，而不是停在一个状态未改变的页面。
- **接通 PvE 预留点**：`App` 持有 `const AiController *ai`，开始人机对局时写入
  `MatchConfig`；控制器非空才允许 PvE 开始。Phase 4 只需在 `app_init` 中注入
  控制器并按 `ai != NULL` 设置 `setup_view_set_ai_available()`。
- **修正动画中退出语义**：动画进行中按 M/R 现在会在结束动画后打开对应的
  返回/重开确认框；只有 Esc/P 才转入暂停。
- **删除冗余状态**：`Match.paused_from` 恒为 `PLAYER_TURN`，已移除，继续时
  直接回到 `PLAYER_TURN`。
- **计时令牌归位**：跳过提示与终局延迟移入 `theme.h`
  （`REVERSI_UI_DUR_PASS_NOTICE` / `..._GAME_OVER` / `..._GAME_OVER_FAST`）。
- **补充状态转换测试**：从 `PAUSED` 与 `GAME_OVER` 重开、人机重做停在
  分支末端（轮到 AI）的语义均有断言。

修正后：Debug 9/9、Release 8/8、ASan+UBSan 8/8 全部通过；`core.match`
653 项、`ui.helpers` 128 项断言。

## 7. 已知问题与风险

- AI 尚未实现：新游戏页的 Computer 与颜色选项按构建禁用。
- 历史容量为 65 项（起始局面 + 64 步）；Reversi 单局最多 60 手，因此不会触顶。
  若触顶，`match_apply_move()` 会回滚并返回 `false`，界面表现为该次点击无效。
- 动画期间会刻意阻断操作输入，快速连点需要等待落子/翻转动画结束
  （Esc/P 可立即结束动画并暂停，M/R 可立即结束动画并打开确认框）。
- 设置与对局仍不持久化（Phase 5）。
- 锁屏、显示器休眠时 GLFW 无法获取主显示器，`app.smoke` 与手动运行都会以
  `Failed to get primary monitor` 失败；唤醒显示后重跑即可。

## 8. 下一阶段依赖

Phase 4（AI）可以直接复用：

- `AiController` 与 `MatchConfig.ai`：`app` 在轮到 AI 时调用 `select_move()`，
  返回的落子仍交给 `match_apply_move()` 校验，非法即被拒绝；
- `match_is_human_turn()` 判断当前是否该玩家操作；
- 人机撤销/重做的“决策点”语义已实现并有测试，Phase 4 不需要改动撤销代码；
- `App` 已持有 `const AiController *ai` 并写入 `MatchConfig`；Phase 4 在
  `app_init` 中注入控制器，并按 `ai != NULL` 调用
  `setup_view_set_ai_available(view, true)` 即可启用 Computer 与颜色选项。
