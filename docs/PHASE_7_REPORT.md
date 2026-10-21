# Phase 7 发布就绪性报告

日期：2026-10-21  
版本：**1.0.0**（根目录 `VERSION`）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64  
起始核对提交：`7231828`  
发布工程提交：`4a0c943`、`6117ab7` 及本报告所在提交  
状态标记：PASS / FAIL / NOT TESTED

## 1. 结论

发布工程已实现并通过实际构建：版本信息、CMake install/CPack、macOS
应用包、Windows/Linux 便携包、运行时资源、安装验收脚本、跨平台 CI 与
标签发布工作流均已落地。

**当前可以生成并验证 1.0.0 发行包，但不应直接宣称“无阻碍公开分发”**：
macOS 未使用 Developer ID 签名/公证，Windows 未做 Authenticode 签名，
两者都会触发系统安全提示；Windows/macOS 的物理桌面 GUI 仍未验证。
详细阻碍见 §8。

## 2. 起始状态核对

没有直接采信此前报告，实际检查结果：

| 项目 | 起始状态 |
|---|---|
| 版本 | `project(... VERSION 0.1.0)`，无独立 `VERSION` 文件 |
| CPack | 未配置；只有 `install(... --component Reversi)` |
| macOS | 裸可执行文件，不是 `.app` |
| Windows | 无版本资源、无静态 CRT 配置、无发行包 |
| CI | 仅 Debug 构建/测试与 Linux Xvfb，无 Release、Sanitizer、打包上传 |
| 发布 | 无标签发布工作流、无发布脚本、无安装/卸载指南 |
| README | 状态仍写 Phase 5，无实际截图、无发行安装说明 |
| 许可证 | 有 raylib/Inter 声明，缺 GLFW、miniaudio 与单头库说明 |

## 3. 版本与构建产物

- `VERSION` 是唯一版本源，CMake 读取并校验语义化版本。
- 生成 `reversi/version.h`：版本、Git 提交；`--version` 输出构建类型。
- 窗口标题为 `Reversi 1.0.0`。
- macOS 生成 `Reversi.app`：
  - 自定义 `Info.plist`（bundle id、版本、最低系统 11.0、HiDPI）；
  - 字体位于 `Contents/Resources/assets/fonts`，运行时优先从该目录加载；
  - 复制资源后重新做 ad-hoc 签名，`codesign --verify --deep --strict` 通过；
  - 无可执行文件旁的重复资源依赖。
- Windows 生成版本资源 `.rc`，并使用静态 MSVC 运行库。
- CPack 只安装项目 `Reversi` 组件，不携带 raylib/GLFW 开发文件。
- 每个归档自动生成 `.sha256`；Linux 同时生成 ZIP 与 TGZ。

实际产物命名：

```text
Reversi-1.0.0-Darwin-arm64.zip
Reversi-1.0.0-Windows-AMD64.zip
Reversi-1.0.0-Linux-x86_64.zip
Reversi-1.0.0-Linux-x86_64.tar.gz
```

## 4. 本机实际执行结果

| 配置 | 命令要点 | 结果 | 状态 |
|---|---|---|---|
| Debug + GUI | `build`；`ctest -C Debug` | 17/17 | PASS |
| Release | `build-release`；`ctest -C Release` | 15/15 | PASS |
| ASan+UBSan | `build-sanitize`；`ctest -C Debug` | 17/17，无报告 | PASS |
| 全新 Release 树 | `build-p7-clean`，联网 FetchContent 获取 raylib 6.2 | 15/15 | PASS |
| 干净树打包 | `cpack -C Release -B package` | ZIP + `.sha256` | PASS |
| 包启动 | `scripts/verify-package.sh ...zip` | 字体加载、`--frames 2` 退出 0 | PASS |
| 版本输出 | `Reversi --version` | `Reversi 1.0.0 (commit ..., Release build)` | PASS |

macOS 的 ASan 不支持 LeakSanitizer，因此 LeakSanitizer 仍为
**NOT TESTED**；ASan+UBSan 本身通过。

## 5. 独立安装验收（macOS 发行包）

验收方式：把 zip 解压到 `/tmp` 下的新目录，使用隔离的
`HOME`/`XDG_DATA_HOME`，从 `/` 启动包内可执行文件，不经过源码树。

| 检查项 | 实际结果 | 状态 |
|---|---|---|
| 不依赖源代码目录启动 | 从 `/` 启动成功 | PASS |
| 运行时资源完整 | `Contents/Resources/assets/fonts` 两个 TTF 与 OFL 存在，日志 `loaded UI fonts` | PASS |
| 首次启动 | 窗口、字体、音频初始化，`--frames 2` 退出 0 | PASS |
| 用户数据目录 | 生成 `~/Library/Application Support/Reversi/settings.dat`、`stats.dat` | PASS |
| 关闭和重新启动 | `verify-package.sh` 连续两次启动均退出 0，并检查第二次加载 | PASS |
| 保存与读取 | 打包 Debug 版自动对局完成一局，`pvp_games 0→1`，重启后再跑一局变为 2；`verify-package.sh` 检查 `settings.dat`/`stats.dat` 持久化；`storage.save` 往返/损坏拒绝测试通过 | PASS（逻辑与用户数据）；打包 GUI 手动存/读档 **NOT TESTED** |
| 无缺失动态库 | `otool -L` 仅显示系统 Framework 与 `libSystem` | PASS |
| 包结构签名 | ad-hoc 签名；`codesign --verify --deep --strict` 通过 | PASS |
| 运行目录改变 | 从 `/` 启动成功 | PASS |
| 发布版/开发版行为一致 | Debug 17/17、Release 15/15，同一代码路径 | PASS |
| LaunchServices | `open Reversi.app` 可启动进程 | PASS |

Windows 包中的 `reversi.exe --version` 在 CI 上实际执行成功；Windows GUI
未运行。

## 6. GitHub Actions 实际结果

工作流：

- `.github/workflows/ci.yml`
  - Linux/Windows/macOS × Debug/Release 构建与 CTest；
  - Release 打包并上传；
  - Linux ASan+UBSan；
  - Linux Xvfb GUI；
  - Linux 发行包在 Xvfb 下独立启动验收。
- `.github/workflows/release.yml`
  - `v*` 标签或手动触发；
  - 三平台 Release 打包、`SHA256SUMS`、GitHub Release；
  - 标签必须等于 `VERSION` 对应的 `v` + 版本号。

发布工程验证 CI（运行 `39597445741`，提交 `6117ab7`）结果：

| 作业 | 结果 |
|---|---|
| Linux Debug / Release | PASS |
| Windows Debug / Release | PASS |
| macOS Debug / Release | PASS |
| Linux ASan+UBSan | PASS |
| Linux Xvfb GUI | PASS |
| Linux packaged smoke | PASS |
| Windows package `--version` smoke | PASS |
| macOS release package | PASS |

Linux packaged smoke 日志：

```text
Reversi 1.0.0 (commit ..., Release build)
PASS: .../Reversi-1.0.0-Linux-x86_64.zip
```

v1.0.0 已由标签触发的发布工作流（运行 `39609234964`）正式发布：
<https://github.com/LightYourJourney/reversi-in-c/releases/tag/v1.0.0>。
发布附件与 SHA-256 记录在 [RELEASES.md](RELEASES.md)。

GitHub 托管运行器是虚拟机，不是物理机；Windows/macOS GUI 与
Windows/Linux 物理桌面仍未验证。

## 7. 许可证与资源审计

| 组件 | 许可证 | 分发形式 |
|---|---|---|
| 项目自身 | GPL-3.0 | 源码/二进制 |
| raylib 6.2 | zlib | 静态链接 |
| GLFW（raylib 内置） | zlib/libpng | 静态链接 |
| miniaudio（raylib 内置） | Public Domain / MIT-0 | 静态链接 |
| stb_image / stb_truetype / stb_vorbis 等 | MIT / Public Domain | 静态链接 |
| dr_flac / dr_mp3 / dr_wav | Public Domain / MIT-0 | 静态链接 |
| Inter 4.1 | SIL OFL 1.1 | 随包字体 |

声明集中在 `THIRD_PARTY_NOTICES.md` 与 `assets/fonts/OFL.txt`。macOS
应用包还会把 `LICENSE` 与 `THIRD_PARTY_NOTICES.md` 复制到
`Contents/Resources/`，避免用户单独拖动应用后丢失声明。游戏音效、棋盘、
棋子与提示均由代码生成，没有第三方音频、贴图或图标文件。

## 8. 尚未解决的发布阻碍

1. **macOS 无 Developer ID、未公证**：当前只做 ad-hoc 签名（包结构校验
   通过），Gatekeeper 仍可能阻止首次启动。不能宣称“双击即无警告”，
   也不能作为已公证正式构建分发。
2. **Windows 未做 Authenticode 签名**：SmartScreen 可能提示未知发布者。
3. **Windows/macOS 桌面 GUI 未在物理机验证**：CI 只验证构建、无窗口
   测试、Windows `--version` 与 macOS 本机包；Windows GUI 为
   **NOT TESTED**。
4. **Linux 仅在 Xvfb 软件渲染下验证 GUI**：物理桌面、不同发行版与
   Wayland 原生会话未验证。
5. **后续修复尚未进入已发布包**：v1.0.0 已于 2026-10-21 发布；本次审查
   之后的修复需要新标签（如 `v1.0.1`）才能进入发行附件。v1.0.0 的校验值
   见 [RELEASES.md](RELEASES.md)。
6. **打包 GUI 手动存/读档未走查**：存档格式、往返与拒绝路径有自动化
   测试，打包进程的统计/设置持久化已验证；通过 GUI 点击“保存/读取游戏”
   仍需人工复核。
7. **无应用图标**：macOS/Windows 使用系统默认图标，不影响运行，但不属于
   完整品牌化发行物。

## 9. 交付物

| 交付物 | 状态 |
|---|---|
| `VERSION`、生成式版本头、`--version` | PASS |
| CMake install/CPack 与组件隔离 | PASS |
| macOS `Reversi.app` ZIP | PASS（ad-hoc 签名；无 Developer ID/公证） |
| Windows 便携 ZIP（静态 CRT、版本资源） | PASS（未签名，GUI NOT TESTED） |
| Linux ZIP/TGZ | PASS（Xvfb 包启动 PASS） |
| 各归档 `.sha256` 与 `SHA256SUMS` | PASS（v1.0.0 记录见 `docs/RELEASES.md`） |
| `scripts/package.sh`、`scripts/verify-package.sh` | PASS |
| `docs/RELEASE.md` 安装/卸载/签名限制 | PASS |
| CI 与 Release 工作流 | PASS（v1.0.0 已发布） |
| README 截图、功能、平台、安装、构建、测试、架构、许可证 | PASS |
| 第三方许可证声明 | PASS |

发行归档的 SHA-256 由 CPack 写入同目录 `.sha256`，发布工作流另生成汇总
`SHA256SUMS`。已发布的 v1.0.0 校验值记录在 [RELEASES.md](RELEASES.md)；
后续构建因提交号与归档时间不同，哈希会变化，以对应运行的校验文件为准。

## 10. 发布后审查修正

v1.0.0 发布后进行的代码审查发现以下问题，已在主分支修正；这些修正需要
新的版本标签才能进入再次发布的发行包。

| 问题 | 修正 | 验证 |
|---|---|---|
| `verify-package.sh` 在 `pipefail` 下组合 `find` 与 `head` 可能因 SIGPIPE 退出 | 改为精确匹配唯一包根目录 | 脚本连续运行通过 |
| 验收脚本未校验 `.sha256`、未验证重启/资源/许可证/动态库 | 校验归档校验和；连续启动两次；检查 `settings.dat`/`stats.dat`；Linux 执行 `ldd` 缺失检查；检查 `LICENSE`/`THIRD_PARTY_NOTICES.md` | 本地发行包 PASS |
| macOS 应用包未包含 GPL 与第三方声明 | 复制到 `Contents/Resources/` 后重新 ad-hoc 签名 | `codesign --verify` 与包验收 PASS |
| 发布构建可能使用系统 raylib | 发布脚本与 CI 打包强制 `REVERSI_FORCE_FETCH_RAYLIB=ON` | CMake 配置输出使用 fetched 6.2 |
| Windows 包 `--version` 冒烟不检查退出码/文本 | 断言退出码为 0 且输出匹配 `Reversi <VERSION>` | 下次 CI 验证 |
| 字体加载重复卸载判断 | 提取 `unload_if_not_default()` | Debug 17/17、Release 15/15、ASan+UBSan 15/15 |

仍保留的判断项：CI 中三平台打包步骤与 `release.yml` 有重复，Linux 依赖
安装列表重复；提取 reusable workflow 会扩大改动面，本次未做。

