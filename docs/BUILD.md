# 构建与运行

本文档记录经过验证的构建、运行、测试与打包命令。验证平台为
macOS 26.6.2（Darwin 25.6.0，Apple M3 / arm64，Apple Clang 21，
CMake 4.4.4）。Windows（MSVC）与 Linux 的 Debug 构建和测试已在
GitHub Actions 托管 VM 上实际运行；托管运行器不是物理机，桌面 GUI 只在
Linux Xvfb 软件渲染下做过冒烟。

## 1. 依赖

- CMake 3.24 或更高
- 支持 C11 的编译器
- raylib 6.2
  - 优先使用系统安装的 raylib；
  - 未找到时由 CMake `FetchContent` 下载固定版本并校验 SHA256；
  - 首次配置需要网络；离线环境可指定本地 raylib 源码目录。
- 线程库（AI 后台搜索；macOS/Linux 为 pthread，Windows 为 Win32 线程）
- OpenGL 驱动与桌面图形会话（运行游戏时）

字体文件位于 `assets/fonts/`。Windows/Linux 构建复制到可执行文件旁；
macOS 构建复制到 `Reversi.app/Contents/Resources/assets/`。字体缺失时
程序回退到 raylib 默认字体，不会阻止启动。

## 2. Debug 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

未指定 `CMAKE_BUILD_TYPE` 时默认 `Debug`。

## 3. Release 构建

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DREVERSI_FORCE_FETCH_RAYLIB=ON
cmake --build build-release -j
```

发行包必须使用固定的 raylib 6.2；`scripts/package.sh` 会自动启用该选项。

macOS 下产物为 `build-release/bin/Reversi.app`；Windows/Linux 为
`build-release/bin/<Config>/reversi[.exe]` 或
`build-release/bin/reversi`。

## 4. 运行

macOS：

```bash
open build/bin/Reversi.app
./build/bin/Reversi.app/Contents/MacOS/Reversi
```

Linux / Windows：

```bash
./build/bin/reversi
.\build\bin\Release\reversi.exe
```

仅渲染指定帧数后退出，用于窗口初始化/关闭冒烟：

```bash
# macOS
./build/bin/Reversi.app/Contents/MacOS/Reversi --frames 10
./build/bin/Reversi.app/Contents/MacOS/Reversi --version
# Linux（Windows 为 build\bin\<Config>\reversi.exe）
./build/bin/reversi --frames 10
```

退出码 `0` 表示窗口、字体、音频与清理路径完成。没有活动图形会话时，
GLFW 会报 `Failed to get primary monitor` / `Failed to initialize platform`，
这是环境限制，不是测试通过。

用户数据目录：

- macOS：`~/Library/Application Support/Reversi`
- Linux：`$XDG_DATA_HOME/reversi`，未设置时为 `~/.local/share/reversi`
- Windows：`%APPDATA%\Reversi`

测试或演示时可隔离用户数据：

```bash
HOME=/tmp/reversi-home XDG_DATA_HOME=/tmp/reversi-home \
APPDATA=/tmp/reversi-home ./build/bin/Reversi.app/Contents/MacOS/Reversi
```

## 5. 测试

```bash
ctest --test-dir build --output-on-failure
```

GUI 冒烟默认不注册，需要显式开启：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

开启后 Debug 构建的 CTest 注册 17 个测试（15 个无窗口测试 +
`app.smoke` + `app.autoplay`）。
完整说明见 [TESTING.md](TESTING.md)。

## 6. Sanitizer

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_ASAN=ON -DREVERSI_ENABLE_UBSAN=ON
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
./build-sanitize/bin/Reversi.app/Contents/MacOS/Reversi --frames 5
```

Sanitizer 只作用于第一方目标，不重新编译 raylib。macOS 的 ASan 不支持
LeakSanitizer；不要设置 `ASAN_OPTIONS=detect_leaks=1`，否则测试会直接中止。

## 7. raylib 依赖策略

1. `find_package(raylib 6.2 QUIET)` 使用系统版本；
2. 未找到时下载固定版本 6.2：
   `df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee`；
3. 强制下载：`-DREVERSI_FORCE_FETCH_RAYLIB=ON`；发布脚本与发布 CI
   始终启用，确保发行包使用已审计的 6.2；
4. 离线构建：

```bash
cmake -S . -B build \
  -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib-6.2
```

## 8. 其他平台

Windows（Visual Studio 多配置）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\bin\Release\reversi.exe
```

Linux（Debian/Ubuntu 示例）：

```bash
sudo apt install build-essential cmake libasound2-dev libx11-dev \
  libxrandr-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev \
  libxcursor-dev libxinerama-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/reversi
```

以上两条路径的 Debug/Release 构建、无窗口测试与 Release 打包已由
`.github/workflows/ci.yml` 在 GitHub Actions 的 Linux/Windows 托管 VM
上验证（运行 `39613503376`）；Linux 另有 Xvfb GUI 冒烟与发行包独立
启动验收。物理机与桌面 GUI 仍未验证。

## 9. 打包与许可证

CPack 生成发行归档（版本号来自根目录 `VERSION`）：

```bash
cd build-release
cpack -C Release -B package
```

- macOS：`Reversi-1.0.0-Darwin-arm64.zip`，内含 `Reversi.app`；
- Windows：`Reversi-1.0.0-Windows-AMD64.zip`，内含 `reversi.exe`；
- Linux：`Reversi-1.0.0-Linux-x86_64.zip` 与 `.tar.gz`。

每个归档同时生成 `.sha256`。CPack 只安装本项目的 `Reversi` 组件，
不会带入 raylib/GLFW 的开发头文件与静态库。一次性完成构建、测试、
打包与校验：

```bash
./scripts/package.sh
./scripts/verify-package.sh build-release/package/Reversi-*.zip
```

安装或用 `cmake --install` 时，macOS 安装 `Reversi.app`，其
`Contents/Resources/assets/` 含字体，`Contents/Resources/` 还包含
`LICENSE` 与 `THIRD_PARTY_NOTICES.md`；Windows/Linux 安装可执行文件与
同级 `assets/`、`LICENSE`、`THIRD_PARTY_NOTICES.md`。字体许可证为
`assets/fonts/OFL.txt`；项目自身为 GPL-3.0；raylib、GLFW、miniaudio
的声明见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。
