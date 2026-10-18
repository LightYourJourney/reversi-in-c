# Phase 0 完成报告

- 日期：2026-10-19
- 阶段：项目初始化与技术架构建设
- 结论：**完成**。Debug、Release、ASan+UBSan 三套构建通过，CTest 通过，
  窗口程序可正常启动并干净退出。

> **Phase 1 修正（2026-10-19）**：本阶段的 `board_init` 在“行 0 = 顶部”
> 的坐标约定下把开局颜色放反了。Phase 1 已按
> D4=White、E5=White、D5=Black、E4=Black 修正，并加入记谱回归测试；
> 详见 [PHASE_1_REPORT.md](PHASE_1_REPORT.md)。

## 1. 实际完成的功能

- 从零建立 C11 + raylib + CMake 工程（仓库此前仅有 LICENSE 与空 README）。
- 建立模块边界：`core`（纯逻辑）、`ui`（raylib 绘制）、`app`（生命周期）。
- 实现最小可运行窗口：
  - 960×720、可缩放、VSync
  - 深色背景与居中标题
  - 60 FPS 事件/渲染循环
  - 窗口关闭事件触发干净退出
  - `CloseWindow()` 释放 raylib 资源
- 实现 `core.board` 基础能力：
  - 标准初始局面（白 d4/e5，黑 e4/d5）
  - 边界判断、格子读写、棋子计数
- 建立不依赖 GUI 的测试目标 `reversi_tests`，由 CTest 注册。
- 提供可选的 GUI 冒烟测试 `app.smoke`（`REVERSI_ENABLE_GUI_TESTS=ON`）。
- 建立可选 ASan/UBSan 构建配置。
- 建立资源复制机制：`assets/` 在构建后复制到可执行文件旁。
- 提供 `--frames N` 冒烟入口，使窗口启动/关闭可自动化验证。

## 2. 新增及修改的文件

新增：

```text
CMakeLists.txt
.gitignore
README.md                 （原文为空，已重写）
src/main.c
src/core/board.h
src/core/board.c
src/ui/render.h
src/ui/render.c
src/app/app.h
src/app/app.c
tests/test_harness.h
tests/test_board.c
assets/README.md
docs/ARCHITECTURE.md
docs/ROADMAP.md
docs/PHASE_0_REPORT.md
```

未修改：

```text
LICENSE
```

## 3. 架构上的重要决定

1. **依赖单向**：`main -> app -> ui -> raylib`，`app -> core`；
   core 不包含 raylib，测试可在无图形环境运行。
2. **raylib 6.2 固定**：优先系统 `find_package(raylib 6.2)`；否则
   FetchContent 下载并校验 SHA256。
3. **Sanitizer 只作用于第一方代码**：在 raylib 配置完成后才添加编译/链接标志。
4. **资源与工作目录解耦**：`assets/` 复制到可执行文件旁，运行时不依赖源码目录。
5. **不创建空模块目录**：`ai/`、`storage/`、`settings/` 等在实际实现出现时再建立。
6. **Board 只实现确定需要的最小接口**，不提前写规则或 AI 的占位实现。

## 4. 实际运行的命令及结果

环境：macOS Darwin 25.6.0，Apple M3 arm64，Apple Clang 21.0.0，CMake 4.4.4。

### 4.1 raylib 依赖

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

结果：成功。raylib 6.2 通过代理下载，SHA256 校验通过：

```text
df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee
```

配置输出包含：

```text
-- Reversi: using fetched raylib 6.2
-- Configuring done
```

### 4.2 Debug 构建与测试

```text
cmake --build build -j
ctest --test-dir build --output-on-failure
```

结果：构建成功；默认 CTest `1/1 passed`；启用 GUI 冒烟测试后 `2/2 passed`
（见 4.8）。

第一次运行测试时发现一处**测试期望值错误**：把 (0,0) 从棋子恢复为空后，
断言空格数为 61，正确值应为 60。实现本身正确，已修正测试断言并重跑通过。
未修改实现以迎合错误测试。

### 4.3 窗口启动与关闭（Debug）

```text
./build/bin/reversi --frames 10
```

结果：退出码 `0`；raylib 输出 `Window closed successfully`。

### 4.4 Release 构建与测试

```text
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

结果：配置与构建成功；CTest `1/1 passed`；`reversi --frames 3` 退出码 `0`。

### 4.5 ASan + UBSan 构建与测试

```text
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_ASAN=ON -DREVERSI_ENABLE_UBSAN=ON
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
./build-sanitize/bin/reversi --frames 5
```

结果：配置输出 `Reversi: sanitizers enabled (address,undefined)`；构建成功；
CTest `1/1 passed`；窗口程序退出码 `0`，无 Sanitizer 诊断。

该构建从空的 `build-sanitize/` 目录重新配置，raylib 由 FetchContent 重新下载；
`CMakeCache.txt` 中 `FETCHCONTENT_SOURCE_DIR_RAYLIB` 为空，未复用其他构建目录。

### 4.6 工作目录独立性

从 `/tmp` 启动 Debug 与 ASan 构建的可执行文件：

```text
cd /tmp
/Users/leo/leocode/mine/reversi-in-c/build/bin/reversi --frames 3
/Users/leo/leocode/mine/reversi-in-c/build-sanitize/bin/reversi --frames 5
```

结果：两者退出码均为 `0`。raylib 日志显示 `Working Directory: /private/tmp`，
程序未依赖源码目录。

### 4.7 静态检查

```text
find src tests -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \
  -o -name '*.hpp' \)
```

结果：无输出，即第一方代码不含 C++ 源文件。

### 4.8 GUI 冒烟测试（CTest）

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

结果：`2/2 passed`。`core.board` 执行 24 项断言；`app.smoke` 运行
`reversi --frames 2`，耗时约 1.3 秒，退出码 `0`。该测试默认不注册，
因为无显示环境会失败。

### 4.9 Code review 修复

Standards / Spec 两轴审查后完成以下修复：

- 测试 harness 移除文件作用域可变全局变量，改为调用方持有的 `TestStats`。
- 第一方测试宏统一 `REVERSI_` 前缀（`REVERSI_TEST_ASSERT` / `REVERSI_RUN_TEST`）。
- 新增可选 `app.smoke` CTest，覆盖 `app_run` / `ui_draw_frame` 的窗口路径。
- CMake 抽取 `reversi_resolve_raylib_target()`，消除重复的 target 探测。
- `parse_frames` 返回 `bool`，与工程风格一致。
- 补齐 Windows/Linux 构建说明与多配置生成器的输出路径。
- sanitizer 构建改为从空目录按文档命令重建，报告记录与缓存一致。
- `assets/README.md` 改为中文，与文档语言约定一致。

未修改的判断项：`(row, col)` 仍以两个整数传递，这是 C 的惯用写法；
Phase 1 规则模块若大量使用再评估 `Coord` 类型。`InitWindow` 失败路径不调用
`CloseWindow()`：raylib 的 `CloseWindow()` 不检查初始化状态，失败时调用不安全。

重新编译第一方源码并筛选警告：

```text
touch src/main.c src/app/app.c src/ui/render.c src/core/board.c tests/test_board.c
cmake --build build -j 2>&1 | grep -Ei "warning|error"
```

结果：`no first-party warnings`。

## 5. 已知问题与风险

1. **第三方警告（非第一方）**
   raylib 编译其内置 `stb_vorbis.c` 时产生一条
   `-Wtautological-compare` 警告。它不影响构建，也不属于本项目代码。
2. **raylib 的 macOS OpenGL 提示**
   配置阶段 raylib 自身打印“OpenGL is deprecated starting with macOS 10.14”。
   这是 raylib 6.2 的既有 developer warning。
3. **平台覆盖**
   仅 macOS arm64 实机验证；Windows 与 Linux **未验证**。
4. **首次配置需要网络**
   无系统 raylib 时需要下载约 50 MB 源码包。可通过标准代理环境变量或
   `FETCHCONTENT_SOURCE_DIR_RAYLIB` 离线复用。
5. **构建启用了 CXX 编译器探测**
   这是 raylib 自身 `project()` 行为；第一方代码与目标均为纯 C，不含 C++ 源文件。
6. **CMake 发布包哈希**
   固定的是 GitHub 生成的 6.2 归档；若上游重新生成同一 tag 的归档，哈希可能变化，
   此时需同时更新 `REVERSI_RAYLIB_URL` 与 `REVERSI_RAYLIB_SHA256`。
7. **GUI 冒烟测试默认关闭**
   无显示环境应保持 `REVERSI_ENABLE_GUI_TESTS=OFF`；否则 `app.smoke` 会失败。
8. **尚未实现游戏功能**
   当前仅有窗口与 Board 基础接口；规则、UI、AI、存档均在后续阶段。

## 6. 下一阶段的依赖与建议

Phase 1 可以直接在现有结构上推进：

- 新增 `src/core/rules.[ch]`，只依赖 `board` 与 C 标准库。
- 新增 `tests/test_rules.c`，并在 CMake 中注册新的测试目标/测试用例。
- 保持 `reversi_core` 不链接 raylib。
- 规则 API 建议返回显式结果（合法着法列表、翻转列表、状态码），
  避免在 core 中打印或退出。
- 每个规则函数先写测试再实现，重点覆盖八个方向、边界、跳过与终局。
