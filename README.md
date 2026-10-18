# Reversi

一款使用 **ISO C11** 与 **raylib 6.2** 编写的翻转棋（Reversi / Othello）桌面游戏。

> **当前状态：Phase 1（规则引擎）已完成。**
> 标准 8×8 规则、合法着法生成、翻转、自动跳过、终局与胜负判定均已实现，
> 并通过确定性随机对局测试。当前窗口仍只绘制基础画面；完整界面、AI、
> 存档等功能将在后续阶段实现。详见 [docs/ROADMAP.md](docs/ROADMAP.md)。

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
│   ├── ui/render.[ch]    raylib 绘制入口
│   └── app/app.[ch]      应用生命周期与主循环
├── tests/
│   ├── test_harness.h    极简测试断言
│   ├── test_helpers.h    棋盘/状态比较辅助
│   ├── test_oracle.h     随机测试用独立合法性 oracle
│   ├── test_board.c      core.board 测试
│   ├── test_rules.c      core.rules 测试
│   ├── test_game.c       core.game 测试
│   └── test_random_games.c 随机合法对局测试
├── docs/
│   ├── ARCHITECTURE.md   模块职责、依赖方向与数据流
│   ├── ROADMAP.md        分阶段交付计划
│   ├── PHASE_0_REPORT.md Phase 0 完成报告
│   └── PHASE_1_REPORT.md Phase 1 完成报告
├── assets/              字体、音效、图像（目前仅有说明）
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

- `core.board`：棋盘、坐标/记谱、边界与安全访问
- `core.rules`：开局着法、八方向翻转、多方向/多子翻转、间隙与边界
- `core.game`：合法/非法落子、自动跳过、主动跳过限制、终局与结果
- `core.random`：500 局确定性随机对局，用独立合法性 oracle 校验每步，
  并检查棋子占用数、总数与终局不变量

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
| macOS（Darwin 25.6.0，Apple M3 / arm64，Apple Clang 21，CMake 4.4.4） | Debug、Release、ASan+UBSan 构建与测试已实际验证（2026-10-19） |
| Windows | 未验证 |
| Linux | 未验证 |

工程未使用平台相关路径或硬编码编译选项；跨平台构建需要对应的 raylib
依赖与 OpenGL 环境。

## 已知问题

- raylib 自带的 `stb_vorbis.c` 在编译第三方源码时会产生一条
  `-Wtautological-compare` 警告。它不属于第一方代码，也不影响构建结果。
- 在 macOS 上配置阶段，raylib 自身会打印一条关于 OpenGL 已废弃的
  developer warning；这是 raylib 6.2 的既有提示。
- 规则引擎已完成；完整图形界面、AI 与存档尚未实现，`--frames` 仅用于冒烟测试。

## 文档

- [架构设计](docs/ARCHITECTURE.md)
- [路线图](docs/ROADMAP.md)
- [Phase 0 报告](docs/PHASE_0_REPORT.md)
- [Phase 1 报告](docs/PHASE_1_REPORT.md)

## 许可证

本项目使用 **GNU General Public License v3.0**，详见 [LICENSE](LICENSE)。
