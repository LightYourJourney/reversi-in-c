# Reversi

一款使用 **ISO C11** 与 **raylib 6.2** 编写的翻转棋（Reversi / Othello）桌面游戏。

> **当前状态：Phase 4（AI 与难度）已完成。**
> 本地双人与人机对战均可完整走完一局。人机模式支持选择黑白方与
> Easy / Medium / Hard 三档难度；AI 在后台线程搜索，思考期间界面不冻结，
> 新对局、撤销、重开、返回菜单与退出都会取消旧搜索。
> 三档 AI 均只返回合法着法，真实对局基准与结果见
> [docs/PHASE_4_REPORT.md](docs/PHASE_4_REPORT.md)。
> 详见 [docs/ROADMAP.md](docs/ROADMAP.md)。

## 技术栈

- ISO C11（核心逻辑与全部第一方代码，不使用编译器扩展）
- [raylib](https://www.raylib.com/) 6.2（窗口、渲染、输入）
- CMake 3.24+（构建）
- CTest（测试）

## 设计原则

- **规则层不依赖图形库**：`core/` 只使用 C 标准库，可在无窗口环境下测试。
- **AI 不接触 UI**：AI 只读取对局快照并返回落子，最终仍由规则层校验。
- **依赖单向**：`main -> app -> ui -> raylib`，`app -> core`；不存在反向依赖。
- **对局状态只有一条写路径**：棋盘只能经 `match_apply_move()` 改动，
  它内部调用规则层并记录历史；UI 不接触 `Board` 的可写接口。
- **无隐式工作目录**：构建后资源复制到可执行文件旁，运行时不依赖源码目录。
- **严格警告**：第一方代码启用 `-Wall -Wextra -Wpedantic -Wshadow` 等检查。

## 目录结构

```text
.
├── CMakeLists.txt
├── src/
│   ├── main.c            程序入口与命令行解析
│   ├── core/
│   │   ├── board.[ch]    棋盘、坐标/记谱映射、棋子统计
│   │   ├── rules.[ch]    合法着法、翻转、无状态规则
│   │   ├── game.[ch]     回合、自动跳过、终局与结果
│   │   ├── history.[ch]  局面快照栈、撤销/重做、分支截断
│   │   └── match.[ch]    对局状态机、模式与 AI 边界、撤销语义
│   ├── ai/
│   │   ├── ai.[ch]       难度配置、评估入口、合法性兜底、同步控制器
│   │   ├── ai_eval.c     角点/机动性/位置/稳定子/前沿/奇偶评估
│   │   ├── ai_search.[ch] Negamax、Alpha-Beta、迭代加深、置换表
│   │   └── ai_worker.[ch] 后台搜索线程、快照、取消与结果回收
│   ├── ui/
│   │   ├── theme.h       颜色、字号、间距、圆角与时长令牌
│   │   ├── draw.[ch]     圆角面板、文字、按钮状态、程序化棋子
│   │   ├── fonts.[ch]    Inter 字体加载、HiDPI 缩放与回退
│   │   ├── animation.[ch] 落子/翻转动画（仅影响渲染）
│   │   ├── board_view.[ch] 棋盘、坐标、合法提示、最后一步
│   │   ├── menu_view.[ch] 主菜单
│   │   ├── setup_view.[ch] 新游戏页（模式与颜色选择）
│   │   ├── game_view.[ch] 对局页、操作按钮与响应式布局
│   │   ├── pause_view.[ch] 暂停浮层
│   │   ├── history_view.[ch] 着法历史列表
│   │   ├── settings_view.[ch] 设置页
│   │   ├── result_view.[ch] 结算页
│   │   └── modal.[ch]    退出/重开/返回确认
│   └── app/app.[ch]      页面路由、转场与主循环
├── tests/
│   ├── test_harness.h    极简测试断言
│   ├── test_helpers.h    棋盘/状态比较辅助
│   ├── test_oracle.h     随机测试用独立合法性 oracle
│   ├── test_board.c      core.board 测试
│   ├── test_rules.c      core.rules 测试
│   ├── test_game.c       core.game 测试
│   ├── test_random_games.c 随机合法对局测试
│   ├── test_history.c    core.history 测试
│   ├── test_match.c      core.match 测试
│   ├── test_ai.c         core.ai 测试（评估、搜索、PvE 完整对局）
│   ├── test_ai_worker.c  ai.worker 测试（快照、取消、生命周期）
│   ├── bench_ai.c        AI 基准（对局、节点/剪枝/耗时、局面探针）
│   ├── test_ui_button.c  按钮悬停/按下/释放/禁用测试
│   └── test_ui_helpers.c 缓动/颜色、命中、各视图输入、动画插值测试
├── docs/
│   ├── ARCHITECTURE.md   模块职责、依赖方向与数据流
│   ├── ROADMAP.md        分阶段交付计划
│   ├── PHASE_0_REPORT.md Phase 0 完成报告
│   ├── PHASE_1_REPORT.md Phase 1 完成报告
│   ├── PHASE_2_REPORT.md Phase 2 完成报告
│   ├── PHASE_3_REPORT.md Phase 3 完成报告
│   └── PHASE_4_REPORT.md Phase 4 完成报告（含 AI 基准结果）
├── assets/
│   └── fonts/            Inter 4.1（SIL OFL 1.1）
└── LICENSE              GPL-3.0
```

`storage/`、`settings/` 等目录会在对应阶段拥有真实实现后再创建，
避免出现无意义的空目录。理由见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 规则引擎与对局流程

- `board`：8×8 棋盘、安全的格子读写、棋子统计、坐标与标准记谱互转。
- `rules`：单步合法性、八方向翻转、合法着法生成/计数、无状态落子。
- `game`：黑方先行、回合切换、自动跳过、主动跳过限制、终局与胜负/平局。
- `history`：局面快照栈（第 0 项为起始局面），每步记录落子方、记谱、翻转数
  与跳过事件；撤销后新落子会截断重做分支。
- `match`：对局状态机与模式；唯一允许写入对局状态的入口，负责历史记录、
  暂停/继续、撤销/重做、重开与终局判定。

坐标约定：行 0 为顶部，列 0 为左侧；记谱 `a1` 为左下角，`h8` 为右上角。
标准开局为 D4/E5 白、D5/E4 黑。

## AI 引擎

- **搜索**：Negamax + Alpha-Beta，显式 pass 节点，双方无着法时返回精确终局分；
  迭代加深、时间预算、取消回调、置换表（Hard）与着法排序。
- **评估**：角点、机动性、潜在机动性、位置权重、边稳定子、前沿子、棋子数与
  奇偶，按开局/中局/残局切换权重；分量值单独保存在 `AiEvaluation` 中可检查。
- **难度**：Easy 为单层静态评估加随机扰动；Medium 为固定深度 Alpha-Beta；
  Hard 为迭代加深 + 置换表 + 残局扩展。三档均只返回合法着法。
- **线程边界**：`AiWorker` 复制状态快照在后台搜索；取消/完成用 C11 原子变量
  同步。后台线程不接触 `Match`、UI 或全局状态，结果仍由 `match_apply_move()`
  校验后应用。线程创建失败时退回同步搜索，再退化为第一个合法着法。
- **基准**：`reversi_ai_bench --games N` 用固定种子跑 Random/Greedy 与不同难度
  对局，输出 W/D/L、非法着法、失败、未完成局数、节点数、剪枝率、置换表命中、
  平均耗时与最大深度，并对开局/中局/残局做局面探针。实际结果见
  [docs/PHASE_4_REPORT.md](docs/PHASE_4_REPORT.md)。

### 状态机

对局逻辑状态（`MatchPhase`）与应用页面（`AppScreen`）是两个独立的维度：

```text
AppScreen:  MainMenu → GameSetup → Game → History ─┐
                                     ↑              │
                                  Settings / Result ┘

MatchPhase: PLAYER_TURN ──落子──> ANIMATING ──动画结束─┬─> PLAYER_TURN
                 │      ↑                              ├─> PASSING_TURN ──> PLAYER_TURN
                 │      └──────────────────────────────┘                  └─> GAME_OVER
                 ├──暂停──> PAUSED ──继续──> PLAYER_TURN
                 └──轮到 AI──> AI_THINKING ──落子──> ANIMATING
                                      └──取消──> PLAYER_TURN
```

- 每个转换都有显式前置条件，非法的转换返回 `false` 且不改变任何状态。
- 输入按阶段门控：`ANIMATING`、`PASSING_TURN`、`PAUSED` 期间棋盘与操作按钮
  不接受输入，而不是依赖散落的布尔标志。
- 自动跳过仍由规则层的 `game_apply_move()` 内部完成；`Match` 只负责把
  “对手无子可下”这一事实翻译成短暂的 `PASSING_TURN` 提示，不改变规则。
- 终局且棋盘未满（双方都无子可下）时按终局处理，不会记录成一次跳过。

## 图形界面与交互

- **页面**：主菜单、新游戏、对局、历史、设置、结算；退出、重开与返回菜单
  使用模态确认。
- **新游戏页**：模式（Local / Computer）、先手颜色与 AI 难度（Easy / Medium /
  Hard）；难度只在 Computer 模式可选。AI 控制器不可用时 Computer、颜色与
  难度按钮禁用，并显示原因。
- **人机对战**：轮到电脑时进入 `AI_THINKING`，状态栏显示 “Computer is
  thinking...” 动态提示；棋盘与暂停禁用，撤销/重开/返回菜单仍可用。搜索在
  后台线程执行，主线程继续渲染；撤销、重开、返回菜单、退出或开始新对局都会
  取消并回收旧搜索。电脑无子可下时沿用自动跳过提示。
- **对局页**：8×8 棋盘与坐标、当前回合、双方实时棋子数、合法着法提示、
  最后一步脉冲标记、状态文本；宽窗口使用侧栏（含 Undo/Redo/Pause/Restart/
  Main menu），窄窗口使用上下信息栏与底部五按钮操作栏。
- **自动跳过**：对手无子可下时显示 “X has no legal moves / Turn passed to Y”
  浮层，约 0.9 秒后自动继续；期间棋盘输入被禁用，Esc/P 立即跳过提示并暂停。
- **暂停**：Esc 或 P 打开浮层，提供 Resume / History / Restart / Main menu；
  继续后回到暂停前的阶段。
- **历史**：显示每一步的序号、落子方、记谱、翻转数与跳过标记；当前局面
  高亮为 CURRENT，撤销后可重做的行标记为 REDO 并置灰；支持滚轮与上下键滚动。
- **撤销/重做**：本地双人撤销一步；人机模式撤销到玩家上一个决策点（搜索中
  撤销会先取消 AI 搜索）。新落子会丢弃重做分支。
- **操作**：鼠标悬停/点击；方向键移动棋盘光标、Enter/Space 落子；
  `R` 重开、`M` 返回菜单、`P`/`Esc` 暂停、`H` 查看历史；
  `Ctrl/Cmd+Z` 撤销、`Ctrl/Cmd+Shift+Z` 或 `Ctrl/Cmd+Y` 重做；
  各页面按钮支持方向键 + Enter，模态支持 Enter/Esc；所有按钮均支持鼠标操作。
- **反馈**：非法落点红色闪框；落子缩放、翻转与最后一步动画只影响渲染，
  不修改 `Match`；落子动画期间棋盘与操作按钮均被禁用。动画中按 Esc/P 会
  立即结束动画并暂停；按 M/R 则立即结束动画并打开对应的返回/重开确认框。
- **设置（内存内，尚未持久化）**：动画、合法着法提示、最后一步高亮开关。
  Animations 同时控制落子/翻转、页面转场、模态/结算淡入与按钮缓动。
- **布局**：最小窗口 760×600，支持任意缩放；宽/窄两套布局；HiDPI 按显示器
  缩放加载字体；超长状态文本使用裁剪区域。
- **字体**：内置 Inter 4.1 Regular/SemiBold（SIL OFL 1.1）。资源缺失时自动
  回退到 raylib 默认字体并记录警告，不阻断启动。
- **程序化资源**：棋子、棋盘、阴影、按钮与提示均为绘制生成，不依赖贴图。

开发辅助（仅 Debug 构建；Release 不编译这些钩子）：

```bash
# 自动落子，快速走完一局并检查结算页；数值为随机种子，便于复现具体对局
REVERSI_AUTOPLAY=7 ./build/bin/reversi

# 启用 F12 窗口截图，写入指定目录
REVERSI_SHOT_DIR=/tmp/reversi-shots ./build/bin/reversi
```

## 构建

### 环境要求

- CMake **3.24** 或更高
- 支持 C11 的编译器（macOS 上为 Apple Clang）
- 首次配置时需要网络下载 raylib；若系统已安装 raylib 6.2，则直接复用
- 若本机使用 HTTP 代理，请设置标准环境变量，例如：

  ```bash
  export https_proxy=http://127.0.0.1:7897
  export http_proxy=http://127.0.0.1:7897
  ```

  代理只影响依赖下载，不会被写进工程配置。

### Debug

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

### Release

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

若未指定 `CMAKE_BUILD_TYPE`，工程默认使用 `Debug`。

### 跨平台构建

**Windows（Visual Studio 生成器，多配置）**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\bin\Release\reversi.exe
```

需要 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载，以及可用的 OpenGL 驱动。

**Linux（单配置生成器）**

```bash
sudo apt install build-essential cmake libasound2-dev libx11-dev \
  libxrandr-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev \
  libxcursor-dev libxinerama-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/reversi
```

不同发行版的包名可能不同；raylib 通过其内置 GLFW 处理 X11/Wayland 后端。

> Windows 与 Linux 的构建说明已补充，但**尚未在实机验证**；macOS 为当前已验证平台。

### raylib 依赖策略

1. 优先调用 `find_package(raylib 6.2 QUIET)` 使用系统安装版本；
2. 未找到时，通过 `FetchContent` 下载固定版本 **6.2**，并校验 SHA256：
   `df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee`；
3. 强制使用下载版本：

   ```bash
   cmake -S . -B build -DREVERSI_FORCE_FETCH_RAYLIB=ON
   ```

4. 离线或离线镜像环境可复用本地源码树：

   ```bash
   cmake -S . -B build \
     -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib-6.2
   ```

## 运行

```bash
./build/bin/reversi
```

使用多配置生成器（如 Visual Studio）时，可执行文件位于
`build/bin/<Config>/reversi`（Windows 下为 `reversi.exe`）。

开发/CI 可让程序只渲染若干帧后自动退出，用于验证窗口能正常启动与关闭：

```bash
./build/bin/reversi --frames 10
```

退出码 `0` 表示正常初始化、渲染并关闭窗口。

## 测试

```bash
ctest --test-dir build --output-on-failure
```

`core.*` 与 `ai.*` 测试目标不依赖 raylib，因此可在无图形环境运行：

- `core.board`：623 项断言；棋盘、坐标/记谱、边界与安全访问
- `core.rules`：227 项断言；开局着法、八方向翻转、多方向/多子翻转、间隙与边界
- `core.game`：115 项断言；合法/非法落子、自动跳过、主动跳过限制、终局与结果
- `core.random`：557,924 项断言；500 局确定性随机对局，用独立合法性 oracle
  校验每步，并检查棋子占用数、总数与终局不变量
- `core.history`：124 项断言；快照初始化、撤销/重做完整恢复、新落子截断重做
  分支、跳过事件与终局恢复、容量与空指针
- `core.match`：701 项断言；配置校验、阶段门控与暂停、自动跳过与终局、
  从暂停/终局重开、撤销/重做恢复一致性、模式切换、AI_THINKING 与人机撤销
- `core.ai`：2,618 项断言；配置/名称、评估分量、大量局面的合法性、单步与
  pass 局面、取消与确定性，以及三档难度 × 黑白方的 6 局完整人机对局
- `ai.worker`：22 项断言；后台搜索、状态快照隔离、取消、空指针与生命周期

`ui.button`（30 项断言）不创建窗口，直接用合成 `UiInput` 校验按钮的
悬停、按下、释放、移出取消、禁用、输入阻断与 reduced-motion 语义。
`ui.helpers`（175 项断言）覆盖缓动/颜色、棋盘命中、合法格判断、显示名称/
终局标题、焦点跳转、对局输入门控、AI_THINKING 门控、新游戏难度选择、
各视图（新游戏/暂停/历史/结算）的键盘与鼠标动作，以及落子/翻转/跳过动画
插值，并确认动画不修改棋盘快照。

实际结果（2026-10-20，macOS arm64，Phase 4 完成后）：

| 构建 | 测试结果 |
|---|---|
| Debug | 11/11，含 `app.smoke`（1.21 s） |
| Release | 10/10 无 GUI 测试通过 |
| ASan+UBSan | 10/10 无 GUI 测试通过，无 Sanitizer 报告 |

AI 基准（Release，Apple M3，seed `0x5EED1234`，每组 4 局）中所有对局
`illegal=0 failures=0 unfinished=0`；Hard 对 Random/Greedy 全胜，Medium 对
Hard 全负，Easy 对 Medium 1:3。完整数据见
[docs/PHASE_4_REPORT.md](docs/PHASE_4_REPORT.md)。

GUI 冒烟测试默认不注册（需要可用的图形会话）。启用方式：

```bash
cmake -S . -B build -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

它会运行 `reversi --frames 2`，打开窗口渲染两帧后退出，用于验证窗口初始化与关闭路径。

### Sanitizer（可选）

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_ASAN=ON -DREVERSI_ENABLE_UBSAN=ON
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
./build-sanitize/bin/reversi --frames 5
```

Sanitizer 标志只作用于第一方目标，不会重新编译 raylib。macOS 上的
AddressSanitizer 不支持 LeakSanitizer，运行测试时请使用
`ASAN_OPTIONS=detect_leaks=0`。

## 平台状态

| 平台 | 状态 |
|---|---|
| macOS（Darwin 25.6.0，Apple M3 / arm64，Apple Clang 21，CMake 4.4.4） | Debug 11/11（含 GUI 冒烟）、Release 10/10、ASan+UBSan 10/10 已实际验证（2026-10-20）；Phase 3/4 界面在 1100×800/1100×832（HiDPI 2×）下完成截图与真实鼠标验证 |
| Windows | 未验证 |
| Linux | 未验证 |

工程未使用平台相关路径或硬编码编译选项；跨平台构建需要对应的 raylib
依赖与 OpenGL 环境。

## 已知问题

- raylib 自带的 `stb_vorbis.c` 在编译第三方源码时会产生一条
  `-Wtautological-compare` 警告。它不属于第一方代码，也不影响构建结果。
- 在 macOS 上配置阶段，raylib 自身会打印一条关于 OpenGL 已废弃的
  developer warning；这是 raylib 6.2 的既有提示。
- 设置项当前只保存在内存中，重启后恢复默认；持久化计划在 Phase 5 实现。
- 存档/读档计划在 Phase 5 落地。
- Phase 4 界面已实际验证：难度与颜色选择、人机执黑/执白、思考提示、
  AI 自动落子、思考中撤销、重做后重新搜索、重开与确认框取消。
  完整人机对局到结算页、人机自动跳过提示、思考中关闭窗口这三项**未人工验证**；
  对应逻辑由 `core.ai` / `core.match` / `ai.worker` 测试覆盖。
- AI 基准每组仅 4 局，样本量有限；结果用于验证强弱顺序与零非法着法，
  不作为精确胜率估计。
- 在锁屏、显示器休眠或没有活动图形会话时，GLFW 无法获取主显示器，
  `app.smoke` 与手动运行都会以 “Failed to get primary monitor /
  Failed to initialize platform” 失败；解锁并保持显示唤醒后重跑即可。
- Windows 与 Linux 构建路径已记录，但尚未实机验证。

## 文档

- [架构设计](docs/ARCHITECTURE.md)
- [路线图](docs/ROADMAP.md)
- [Phase 0 报告](docs/PHASE_0_REPORT.md)
- [Phase 1 报告](docs/PHASE_1_REPORT.md)
- [Phase 2 报告](docs/PHASE_2_REPORT.md)
- [Phase 3 报告](docs/PHASE_3_REPORT.md)
- [Phase 4 报告](docs/PHASE_4_REPORT.md)

## 许可证

本项目使用 **GNU General Public License v3.0**，详见 [LICENSE](LICENSE)。
