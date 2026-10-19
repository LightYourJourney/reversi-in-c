# Reversi

一款使用 **ISO C11** 与 **raylib 6.2** 编写的翻转棋（Reversi / Othello）桌面游戏。

> **当前状态：Phase 2（图形界面与交互）已完成。**
> 主菜单、对局、设置、结算页与模态确认均可操作；落子/翻转/最后一步动画、
> 窗口缩放与 HiDPI、字体回退均已验证。AI、撤销/重做与存档将在后续阶段实现。
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
│   │   └── game.[ch]     回合、自动跳过、终局与结果
│   ├── ui/
│   │   ├── theme.h       颜色、字号、间距、圆角与时长令牌
│   │   ├── draw.[ch]     圆角面板、文字、按钮状态、程序化棋子
│   │   ├── fonts.[ch]    Inter 字体加载、HiDPI 缩放与回退
│   │   ├── animation.[ch] 落子/翻转动画（仅影响渲染）
│   │   ├── board_view.[ch] 棋盘、坐标、合法提示、最后一步
│   │   ├── menu_view.[ch] 主菜单
│   │   ├── game_view.[ch] 对局页与响应式布局
│   │   ├── settings_view.[ch] 设置页
│   │   ├── result_view.[ch] 结算页
│   │   └── modal.[ch]    退出/重开/返回确认
│   └── app/app.[ch]      应用状态机、页面切换与主循环
├── tests/
│   ├── test_harness.h    极简测试断言
│   ├── test_helpers.h    棋盘/状态比较辅助
│   ├── test_oracle.h     随机测试用独立合法性 oracle
│   ├── test_board.c      core.board 测试
│   ├── test_rules.c      core.rules 测试
│   ├── test_game.c       core.game 测试
│   ├── test_random_games.c 随机合法对局测试
│   ├── test_ui_button.c  按钮悬停/按下/释放/禁用测试
│   └── test_ui_helpers.c 缓动/颜色、命中、合法判断与动画插值测试
├── docs/
│   ├── ARCHITECTURE.md   模块职责、依赖方向与数据流
│   ├── ROADMAP.md        分阶段交付计划
│   ├── PHASE_0_REPORT.md Phase 0 完成报告
│   ├── PHASE_1_REPORT.md Phase 1 完成报告
│   └── PHASE_2_REPORT.md Phase 2 完成报告
├── assets/
│   └── fonts/            Inter 4.1（SIL OFL 1.1）
└── LICENSE              GPL-3.0
```

`ai/`、`storage/`、`settings/` 等目录会在对应阶段拥有真实实现后再创建，
避免出现无意义的空目录。理由见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 规则引擎

- `board`：8×8 棋盘、安全的格子读写、棋子统计、坐标与标准记谱互转。
- `rules`：单步合法性、八方向翻转、合法着法生成/计数、无状态落子。
- `game`：黑方先行、回合切换、自动跳过、主动跳过限制、终局与胜负/平局。

坐标约定：行 0 为顶部，列 0 为左侧；记谱 `a1` 为左下角，`h8` 为右上角。
标准开局为 D4/E5 白、D5/E4 黑。

## 图形界面与交互

- **页面**：主菜单、对局、设置、结算；退出、重开与返回菜单使用模态确认。
- **对局页**：8×8 棋盘与坐标、当前回合、双方实时棋子数、合法着法提示、
  最后一步脉冲标记、状态文本；宽窗口使用侧栏，窄窗口使用上下信息栏。
- **操作**：鼠标悬停/点击；方向键移动棋盘光标、Enter/Space 落子；
  `R` 重开、`M`/`Esc` 返回菜单；菜单/设置/结算按钮支持方向键 + Enter，
  对局页按钮提供 R/M 快捷键，模态支持 Enter/Esc；所有按钮均支持鼠标悬停/点击。
- **反馈**：非法落点红色闪框；落子缩放、翻转与最后一步动画只影响渲染，
  不修改 `GameState`；落子动画期间棋盘与操作按钮均被禁用。
- **设置（内存内，尚未持久化）**：动画、合法着法提示、最后一步高亮开关。
  Animations 同时控制落子/翻转、页面转场、模态/结算淡入与按钮缓动。
- **布局**：最小窗口 760×600，支持任意缩放；宽/窄两套布局；HiDPI 按显示器
  缩放加载字体；超长状态文本使用裁剪区域。
- **字体**：内置 Inter 4.1 Regular/SemiBold（SIL OFL 1.1）。资源缺失时自动
  回退到 raylib 默认字体并记录警告，不阻断启动。
- **程序化资源**：棋子、棋盘、阴影、按钮与提示均为绘制生成，不依赖贴图。

开发辅助（仅 Debug 构建；Release 不编译这些钩子）：

```bash
# 自动落子，用于快速走完一局并检查结算页
REVERSI_AUTOPLAY=1 ./build/bin/reversi

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

四个 core 测试目标只链接 `reversi_core`，不依赖 raylib，因此可在无图形环境运行：

- `core.board`：623 项断言；棋盘、坐标/记谱、边界与安全访问
- `core.rules`：227 项断言；开局着法、八方向翻转、多方向/多子翻转、间隙与边界
- `core.game`：115 项断言；合法/非法落子、自动跳过、主动跳过限制、终局与结果
- `core.random`：557,924 项断言；500 局确定性随机对局，用独立合法性 oracle
  校验每步，并检查棋子占用数、总数与终局不变量

`ui.button`（30 项断言）不创建窗口，直接用合成 `UiInput` 校验按钮的
悬停、按下、释放、移出取消、禁用、输入阻断与 reduced-motion 语义。
`ui.helpers`（65 项断言）覆盖缓动/颜色、棋盘命中、合法格判断、对局输入
门控与落子/翻转动画插值，并确认动画不修改棋盘快照。

实际结果（2026-10-19，macOS arm64，代码审查修正后）：

| 构建 | 测试结果 |
|---|---|
| Debug（`REVERSI_ENABLE_GUI_TESTS=ON`） | 7/7，含 `app.smoke` |
| Release | 6/6 |
| ASan+UBSan（`ASAN_OPTIONS=detect_leaks=0`） | 6/6，另运行 `--frames 5` 无报告 |

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
| macOS（Darwin 25.6.0，Apple M3 / arm64，Apple Clang 21，CMake 4.4.4） | Debug、Release、ASan+UBSan 构建与测试已实际验证；Phase 2 界面在 760×600、800×620、1100×800、1400×1000 下完成截图验证（2026-10-19） |
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
- AI 对战、撤销/重做、存档/读档尚未实现，分别计划在 Phase 3–5 落地。
- 在锁屏或没有活动图形会话时，GLFW 无法获取主显示器，`app.smoke`
  会以“Failed to initialize platform”失败；解锁并保持显示唤醒后重跑即可。
- Windows 与 Linux 构建路径已记录，但尚未实机验证。

## 文档

- [架构设计](docs/ARCHITECTURE.md)
- [路线图](docs/ROADMAP.md)
- [Phase 0 报告](docs/PHASE_0_REPORT.md)
- [Phase 1 报告](docs/PHASE_1_REPORT.md)
- [Phase 2 报告](docs/PHASE_2_REPORT.md)

## 许可证

本项目使用 **GNU General Public License v3.0**，详见 [LICENSE](LICENSE)。
