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

字体文件位于 `assets/fonts/`，构建时复制到可执行文件旁。字体缺失时程序
回退到 raylib 默认字体，不会阻止启动。

## 2. Debug 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

未指定 `CMAKE_BUILD_TYPE` 时默认 `Debug`。

## 3. Release 构建

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

macOS 下产物为 `build-release/bin/reversi`。

## 4. 运行

```bash
./build/bin/reversi
```

仅渲染指定帧数后退出，用于窗口初始化/关闭冒烟：

```bash
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
APPDATA=/tmp/reversi-home ./build/bin/reversi
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
./build-sanitize/bin/reversi --frames 5
```

Sanitizer 只作用于第一方目标，不重新编译 raylib。macOS 的 ASan 不支持
LeakSanitizer；不要设置 `ASAN_OPTIONS=detect_leaks=1`，否则测试会直接中止。

## 7. raylib 依赖策略

1. `find_package(raylib 6.2 QUIET)` 使用系统版本；
2. 未找到时下载固定版本 6.2：
   `df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee`；
3. 强制下载：`-DREVERSI_FORCE_FETCH_RAYLIB=ON`；
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

以上两条路径的 Debug 构建与无窗口测试已由 `.github/workflows/ci.yml`
在 GitHub Actions 的 Linux/Windows 托管 VM 上验证（运行 `39588002802`），
Linux 另有 Xvfb GUI 冒烟。物理机与桌面 GUI 仍未验证。

## 9. 打包与许可证

可执行文件需要与其同级的 `assets/` 目录一起分发。字体许可证为
`assets/fonts/OFL.txt`；项目自身为 GPL-3.0；raylib 为 zlib 许可证。
详见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

推荐使用安装规则（只安装本项目的 `Reversi` 组件，不携带 raylib/GLFW
的开发头文件与静态库）：

```bash
cmake --install build-release --prefix dist/reversi --component Reversi
```

安装树包含 `reversi`、`assets/`、`docs/`、`LICENSE`、
`THIRD_PARTY_NOTICES.md`、`README.md` 与 `CHANGELOG.md`。
